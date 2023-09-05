#include "pch.h"
#include "PsMonitor.h"
#include "PsMonitorCommon.h"
#include "Locker.h"

#define DRIVER_TAG 'PsM'
#define DRIVER_PREFIX "PsMonitor: "

Globals g_Globals;

void PsMonitorUnload(PDRIVER_OBJECT DriverObject);
DRIVER_DISPATCH PsMonitorRead, PsMonitorCreateClose;
NTSTATUS CompleteIrp(PIRP irp, NTSTATUS, ULONG_PTR);
void OnProcessNotify(PEPROCESS, HANDLE, PPS_CREATE_NOTIFY_INFO);

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING) {
	auto status = STATUS_SUCCESS;


	DriverObject->DriverUnload = PsMonitorUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = PsMonitorCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = PsMonitorRead;

	//initialize our global variables. List first node + our lock on the list.
	InitializeListHead(&g_Globals.ItemsHead);
	g_Globals.Mutex.Init();
	HANDLE maxValueKey;
	OBJECT_ATTRIBUTES objectAttributes;
	UNICODE_STRING regPath;
	
	
	RtlInitUnicodeString(&regPath, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\PsMonitor");
	InitializeObjectAttributes(&objectAttributes, &regPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);


	status = ZwOpenKey(&maxValueKey, KEY_READ, &objectAttributes);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Error attempting to open registry key 0x%08X", status));
		return status;
	}

	ULONG bufferSize = 0;
	PKEY_VALUE_PARTIAL_INFORMATION pValueInfo = NULL;
	UNICODE_STRING maxItems;
	RtlInitUnicodeString(&maxItems, L"MaxItems");
	pValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(KEY_VALUE_PARTIAL_INFORMATION), DRIVER_TAG);

	status = ZwQueryValueKey(maxValueKey, &maxItems, KeyValuePartialInformation, nullptr, 0, &bufferSize);
	if (status == STATUS_BUFFER_TOO_SMALL || status == STATUS_BUFFER_OVERFLOW)
	{
		pValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePool2(POOL_FLAG_PAGED, bufferSize, DRIVER_TAG);
		if (pValueInfo)
		{
			status = ZwQueryValueKey(maxValueKey, &maxItems, KeyValuePartialInformation, pValueInfo, bufferSize, &bufferSize);
			if (!NT_SUCCESS(status))
			{
				KdPrint(("Failed to query registry value: 0x%08X\n", status));
			}
			UNICODE_STRING maxItemsValue;
			RtlInitUnicodeString(&maxItemsValue, (PCWSTR)pValueInfo->Data);

			status = RtlUnicodeStringToInteger(&maxItemsValue, 10, &g_Globals.MaxItems);
			if (!NT_SUCCESS(status)) {
				KdPrint(("Failed to convert Maximum item count to Integer: 0x%08X\n", status));
			}
			//if all succeeds
			ExFreePoolWithTag(pValueInfo, DRIVER_TAG);
		}
		else
		{
			KdPrint(("Failed to allocate memory\n"));
			status = STATUS_INSUFFICIENT_RESOURCES;
		}
	}
	else
	{
		g_Globals.MaxItems = 1024;
	}

	ZwClose(maxValueKey);

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
	AutoLock<FastMutex> lock(g_Globals.Mutex);
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

NTSTATUS PsMonitorRead(PDEVICE_OBJECT, PIRP irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto len = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	auto count = 0;
	NT_ASSERT(irp->MdlAddress); //were using direct IO

	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
	if (!buffer)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else {
		//access our linked list and pull itesm from the head.
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
			count += size;

			//free data after the copy.
			ExFreePool(info);
		}


	}

	return CompleteIrp(irp, status, count);
}

void PsMonitorUnload(PDRIVER_OBJECT DriverObject)
{
	//unregister the callback routine.
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);

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
