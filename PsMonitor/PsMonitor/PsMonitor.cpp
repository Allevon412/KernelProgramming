#include "pch.h"
#include "PsMonitor.h"
#include "PsMonitorCommon.h"
#include "Locker.h"

#define DRIVER_TAG 'PsM'
#define DRIVER_PREFIX "PsMonitor: "

Globals g_Globals;
newProcGlobalList g_NewProcList;

void PsMonitorUnload(PDRIVER_OBJECT DriverObject);
DRIVER_DISPATCH PsMonitorRead, PsMonitorCreateClose, PsMonitorWrite;
NTSTATUS CompleteIrp(PIRP irp, NTSTATUS, ULONG_PTR);
NTSTATUS OnRegistryNotify(PVOID context, PVOID arg1, PVOID arg2);
void OnProcessNotify(PEPROCESS, HANDLE, PPS_CREATE_NOTIFY_INFO);
void OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create);
void OnImageLoadNotify(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo);
bool AddNewProcess(HANDLE pid);
bool FindProcess(HANDLE pid);
bool RemoveProcess(HANDLE pid);

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING) {
	auto status = STATUS_SUCCESS;


	DriverObject->DriverUnload = PsMonitorUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = PsMonitorCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = PsMonitorRead;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = PsMonitorWrite;

	//initialize our global variables. List first node + our lock on the list.
	InitializeListHead(&g_Globals.ItemsHead);
	g_Globals.Mutex.Init();
	g_NewProcList.Mutex.Init();
	
	g_Globals.MaxItems = 10000;

	PDEVICE_OBJECT DeviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\psmonitor");
	bool symLinkCreated = false;

	do {
		//create device object.
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\psmonitor");
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failure to create device object 0x%08X.\n", status));
			break;
		}

		DeviceObject->Flags |= DO_DIRECT_IO; // set object to perform direct IO operations. manager maps the buffer to system memory so you can interact with the buffer via mapping.

		//create symbolic Link
		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failure to create symbolic link for device object 0x%08X", status));
			break;
		}

		symLinkCreated = true;

		//register for process notifications
		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failure to register function for process creation | deletion notifications 0x%08X\n", status));
			break;
		}

		status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failure to register function for thread creation | deletion notifications 0x%08X\n", status));
			break;
		}
		status = PsSetLoadImageNotifyRoutine(OnImageLoadNotify);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failure to register function for image loading notifications 0x%08X\n", status));
			break;
		}
		UNICODE_STRING altitude = RTL_CONSTANT_STRING(L"7657.124");
		status = CmRegisterCallbackEx(OnRegistryNotify, &altitude, DriverObject, nullptr, &g_Globals.RegCookie, nullptr);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failed to set registery callback 0x%08X\n", status));
			break;
		}


	} while (false);

	if (!NT_SUCCESS(status))
	{
		if (symLinkCreated) {
			IoDeleteSymbolicLink(&symLink);
		}
		if (DeviceObject)
		{
			IoDeleteDevice(DeviceObject);
		}
	}

	return status;

}

void PushItem(LIST_ENTRY* entry)
{
	AutoLock locker(g_Globals.Mutex);
	if (g_Globals.ItemCount > g_Globals.MaxItems) //better to have the 1024 number read from the registry in the driver's service key.
	{
		//too many items in list removed the oldest one.
		auto head = RemoveHeadList(&g_Globals.ItemsHead);
		g_Globals.ItemCount--;
		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry);
		ExFreePool(item);
	}

	InsertTailList(&g_Globals.ItemsHead, entry);
	g_Globals.ItemCount++;
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	if (CreateInfo)
	{
		USHORT allocSize = sizeof(FullItem<ProcessCreateInfo>);
		USHORT commandLineSize = 0;
		USHORT imageFileNameSize = 0;

		//calculate size of memory region needed for process information + commandline / image name.
		if (CreateInfo->CommandLine)
		{
			commandLineSize = CreateInfo->CommandLine->Length;
			allocSize += commandLineSize;
		}
		if (CreateInfo->ImageFileName)
		{
			imageFileNameSize = CreateInfo->ImageFileName->Length;
			allocSize += imageFileNameSize;
		}

		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, allocSize, DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("Failed to allocate memory for process creation information\n"));
			return;
		}


		//check to see if process execution happens from an excluded path.
		// make sure commandline isn't null.
		if (CreateInfo->CommandLine)
		{
			//make sure that the excludedPath has been set by usermode code.
			if (g_Globals.ExcludedPath[0] != L'\0')
			{
				UNICODE_STRING tempExcludedPath;
				UNICODE_STRING tempCurrentPath;
				RtlInitUnicodeString(&tempExcludedPath, g_Globals.ExcludedPath);
				RtlInitUnicodeString(&tempCurrentPath, CreateInfo->CommandLine->Buffer);
				tempCurrentPath.Buffer++; // get rid of the leading " character.
				tempCurrentPath.Length = tempExcludedPath.Length;
				if (RtlCompareUnicodeString(&tempExcludedPath, &tempCurrentPath, TRUE) == 0)
				{
					KdPrint(("A process was attempted to start from an excluded path. Skipping adding to process monitor queue"));
					CreateInfo->CreationStatus = STATUS_ACCESS_DENIED;
					return;
				}
			}
		}

		//add process to the new process entry list.
		if (!AddNewProcess(ProcessId))
		{
			KdPrint(("New Process Created, but no room to store.\n"));
		}

		//obtain the static process information.
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessCreate;
		item.ProcessId = HandleToULong(ProcessId);
		item.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);
		item.size = sizeof(ProcessCreateInfo) + commandLineSize + imageFileNameSize;

		//copy the command line buffer just after the procecss info structure is filled.
		if (commandLineSize > 0)
		{
			::memcpy((UCHAR*)&item + sizeof(item), CreateInfo->CommandLine->Buffer, commandLineSize);
			item.CommandLineLength = commandLineSize / sizeof(WCHAR);
			item.CommandLineOffset = sizeof(item);
			

		}
		else
		{
			item.CommandLineLength = 0;
		}

		//copy the image name buffer to just after the process structrue + command line buffer 
		if (imageFileNameSize > 0)
		{
			::memcpy((UCHAR*)&item + sizeof(item) + commandLineSize, CreateInfo->ImageFileName->Buffer, imageFileNameSize);
			item.ImageFileNameLength = imageFileNameSize / sizeof(WCHAR);
			item.ImageFileNameOffset = sizeof(item) + commandLineSize;
		}
		else
		{
			item.ImageFileNameLength = 0;
		}

		PushItem(&info->Entry);

	}
	else
	{
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint((DRIVER_PREFIX "Error attmepting to allocate memory for the ProcessExitInfo structure.\n"));
			return; 
		}

		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.size = sizeof(ProcessExitInfo);

		PushItem(&info->Entry);
	}
}

void OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create)
{
	if (Create) // thread creation notification.
	{
		auto size = sizeof(FullItem<ThreadCreateExitInfo>);
		auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, size, DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("Failed to allocate memory for thread creation information\n"));
			return;
		}
		auto& item = info->Data;
		item.remote = false;
		do //give us a way to break out of certain features.
		{
			HANDLE currProcess = PsGetCurrentProcessId();
			if (currProcess != ProcessId && PsInitialSystemProcess != PsGetCurrentProcess() && PsGetProcessId(PsInitialSystemProcess) != ProcessId) //check to ensure that the process is not the system process (4) and is not equal to the current process is not equal to the processId.
			{
				bool found = FindProcess(ProcessId);
				if (found)
				{
					//if it's found then we have found first thread from starting a process. Remove it. "considered current process as parent process for new process starting."
					RemoveProcess(ProcessId);
					break;
				}
				//if it's not found then we have a truly remote thread.
				item.remote = true;

				item.CreateProcessId = HandleToULong(PsGetCurrentProcessId());
				item.CreatorThreadId = HandleToULong(PsGetCurrentThreadId());
			}
		} while (false);
		
		KeQuerySystemTimePrecise(&item.Time);
		item.size = sizeof(item);
		item.Type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.ThreadId = HandleToULong(ThreadId);
		

		PushItem(&info->Entry);
		
	}
	else // thread exiting notification.
	{
		auto size = sizeof(FullItem<ThreadCreateExitInfo>);
		auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, size, DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("Failed to allocate memory for thread creation information\n"));
			return;
		}

		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.size = sizeof(item);
		item.Type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.ThreadId = HandleToULong(ThreadId);
		item.remote = false;

		PushItem(&info->Entry);
	}
}

void OnImageLoadNotify(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo)
{
	if (!ImageInfo->SystemModeImage)
	{
		auto size = sizeof(FullItem<ImageLoadInfo>);
		auto info = (FullItem<ImageLoadInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, size + FullImageName->Length, DRIVER_TAG);
		if (info == nullptr)
		{
			KdPrint(("Failed to allocate memory for image load information\n"));
			return;
		}

		auto& item = info->Data;
		item.ImageNameLength = FullImageName->Length;
		item.ImageNameOffset = sizeof(FullItem<ImageLoadInfo>);
		item.ProcessId = HandleToULong(ProcessId);
		item.ImageBase = ImageInfo->ImageBase;
		item.size = sizeof(ImageLoadInfo);
		item.Type = ItemType::ImageLoad;
		if (FullImageName->Length > 0)
		{
			::memcpy((UCHAR*)&item + sizeof(item), FullImageName->Buffer, FullImageName->Length);
			item.size += FullImageName->Length;
		}

		PushItem(&info->Entry);
	}
	
}


NTSTATUS PsMonitorRead(PDEVICE_OBJECT, PIRP irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto len = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	auto bytes = 0;
	NT_ASSERT(irp->MdlAddress); //were using direct IO

	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
	if (!buffer)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else {
		//access our linked list and pull items from the head.
		AutoLock lock(g_Globals.Mutex);
		while (true)
		{
			if (IsListEmpty(&g_Globals.ItemsHead))
				break;

			auto entry = RemoveHeadList(&g_Globals.ItemsHead);
			auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
			auto size = info->Data.size;
			if (len < size)
			{
				//user's buffer is full. Insert item back.
				InsertHeadList(&g_Globals.ItemsHead, entry);
				break;
			}

			g_Globals.ItemCount--;
			::memcpy(buffer, &info->Data, size);
			len -= size;
			buffer += size;
			bytes += size;

			//free data after the copy.
			ExFreePool(info);
		}


	}

	return CompleteIrp(irp, status, bytes);
}

void PsMonitorUnload(PDRIVER_OBJECT DriverObject)
{
	//unregister the callback routine.
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
	PsRemoveLoadImageNotifyRoutine(OnImageLoadNotify);
	CmUnRegisterCallback(g_Globals.RegCookie);

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\psmonitor");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	while(!IsListEmpty(&g_Globals.ItemsHead))
	{
		auto entry = RemoveHeadList(&g_Globals.ItemsHead);
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));
	}

}

NTSTATUS CompleteIrp(PIRP irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0)
{
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = info;
	IoCompleteRequest(irp, 0);
	return status;
}

NTSTATUS PsMonitorCreateClose(PDEVICE_OBJECT, PIRP Irp)
{
	return CompleteIrp(Irp);
}

NTSTATUS PsMonitorWrite(PDEVICE_OBJECT, PIRP Irp)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;

	auto len = stack->Parameters.Write.Length;
	NT_ASSERT(Irp->MdlAddress);

	KdPrint(("Trying to obtain buffer from usermode code"));

	if (len <= 0)
	{
		return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);
	}
	if (len > 256)
	{
		return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);
	}

	auto buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer)
	{
		return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);
	}
	else
	{
		AutoLock lock(g_Globals.Mutex);
		
		::memcpy(g_Globals.ExcludedPath, buffer, len);
	}

	return CompleteIrp(Irp, status, len);
}

NTSTATUS OnRegistryNotify(PVOID context, PVOID arg1, PVOID arg2)
{
	UNREFERENCED_PARAMETER(context);

	switch ((REG_NOTIFY_CLASS)(ULONG_PTR)arg1)
	{
		case RegNtPostSetValueKey:
		{
			auto args = (REG_POST_OPERATION_INFORMATION*)arg2;
			if (!NT_SUCCESS(args->Status))
				break;

			static const WCHAR machine[] = L"\\REGISTRY\\MACHINE\\";
			PUNICODE_STRING name;
			if (NT_SUCCESS(CmCallbackGetKeyObjectIDEx(&g_Globals.RegCookie, args->Object, nullptr, &name, 0)))
			{
				//filter out none-HKLM writes
				if (::wcsncmp(name->Buffer, machine, ARRAYSIZE(machine) - 1) == 0)
				{
					auto preInfo = (REG_SET_VALUE_KEY_INFORMATION*)args->PreInformation;
					NT_ASSERT(preInfo);

					auto size = sizeof(FullItem<RegistrYSetValueInfo>);
					auto info = (FullItem<RegistrYSetValueInfo>*)ExAllocatePool2(POOL_FLAG_PAGED, size, DRIVER_TAG);
					if (info == nullptr)
						break;

					RtlZeroMemory(info, size);

					auto& item = info->Data;
					KeQuerySystemTimePrecise(&item.Time);
					item.size = sizeof(item);
					item.Type = ItemType::RegistrySetValue;

					item.ProcessId = HandleToULong(PsGetCurrentProcessId());
					item.ThreadId = HandleToULong(PsGetCurrentThreadId());

					::wcsncpy_s(item.KeyName, name->Buffer, name->Length / sizeof(WCHAR) - 1);
					::wcsncpy_s(item.ValueName, preInfo->ValueName->Buffer, preInfo->ValueName->Length / sizeof(WCHAR) -1);
					item.DataType = preInfo->Type;
					item.DataSize = preInfo->DataSize;
					::memcpy(item.Data, preInfo->Data, min(item.DataSize, sizeof(item.Data)));

					PushItem(&info->Entry);
				}
				CmCallbackReleaseKeyObjectIDEx(name);
			}

			break;
		}
	}
	return STATUS_SUCCESS;
}


bool AddNewProcess(HANDLE pid)
{
	AutoLock lock(g_NewProcList.Mutex);
	if (g_NewProcList.count == MaxNewProcesses)
		return false;

	for (int i = 0; i < MaxNewProcesses; i++)
	{
		if (g_NewProcList.NewProcesses[i] == 0)
		{
			g_NewProcList.NewProcesses[i] = HandleToULong(pid);
			break;
		}
	}
	g_NewProcList.count++;
	return true;
}

bool RemoveProcess(HANDLE pid)
{
	AutoLock lock(g_NewProcList.Mutex);

	for (int i = 0; i < MaxNewProcesses; i++)
	{
		if (g_NewProcList.NewProcesses[i] == HandleToULong(pid))
		{
			g_NewProcList.NewProcesses[i] = 0;
			g_NewProcList.count--;
			return true;
		}
	}
	return false;
}

bool FindProcess(HANDLE pid)
{
	AutoLock lock(g_NewProcList.Mutex);

	for (int i = 0; i < MaxNewProcesses; i++)
	{
		if (g_NewProcList.NewProcesses[i] == HandleToULong(pid))
		{
			return true;
		}
	}
	return false;
}