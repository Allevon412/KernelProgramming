#include "pch.h"
#include "ProcessProtect.h"
#include "ProcessProtectCommon.h"
#include "..\..\SupportingCode\Locker.h"

OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION OperationInformation);
DRIVER_DISPATCH ProcessProtectDeviceControl, ProcessProtectCreateClose;
bool AddProcess(ULONG pid);
bool FindProcess(ULONG pid);
bool RemoveProcess(ULONG pid);
void UnloadFunc(PDRIVER_OBJECT DriverObject);

Globals g_Data;

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING)
{
	auto status = STATUS_SUCCESS;

	g_Data.Init();

	DriverObject->DriverUnload = UnloadFunc;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ProcessProtectDeviceControl;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = ProcessProtectCreateClose;

	PDEVICE_OBJECT DeviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ProcProtect");
	bool symLinkCreated = false;

	do
	{
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\ProcProtect");
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failure to create device object 0x%08X\n", status));
			break;
		}

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status))
		{
			KdPrint(("Failure to create symbolic link for device object 0x%08X", status));
			break;
		}
		
		symLinkCreated = true;

		OB_OPERATION_REGISTRATION operations[] = {
		{
			PsProcessType, // object type
			OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE,
			OnPreOpenProcess, nullptr // pre operation, Post op.
		}
		};

		OB_CALLBACK_REGISTRATION reg = {
			OB_FLT_REGISTRATION_VERSION,
			1, // operation count
			RTL_CONSTANT_STRING(L"12345.6171"), //altitude
			nullptr, //context
			operations
		};

		status = ObRegisterCallbacks(&reg, &g_Data.RegHandle);
		if (!NT_SUCCESS(status))
		{
			break;
		}

	} while (false);

	
	if (!NT_SUCCESS(status))
	{
		if (symLinkCreated)
		{
			IoDeleteSymbolicLink(&symLink);
		}
		if (DeviceObject)
		{
			IoDeleteDevice(DeviceObject);
		}
	}


	return status;
}

void UnloadFunc(PDRIVER_OBJECT DriverObject)
{
	ObUnRegisterCallbacks(g_Data.RegHandle);
	
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ProcProtect");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);
}

OB_PREOP_CALLBACK_STATUS OnPreOpenProcess(PVOID RegistrationContext, POB_PRE_OPERATION_INFORMATION OperationInformation) 
{
	UNREFERENCED_PARAMETER(RegistrationContext);

	if (OperationInformation->KernelHandle)
		return OB_PREOP_SUCCESS;

	auto process = (PEPROCESS)OperationInformation->Object;
	auto pid = HandleToULong(PsGetProcessId(process));

	if (FindProcess(pid))
	{
		OperationInformation->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_TERMINATE;
	}

	return OB_PREOP_SUCCESS;
}

NTSTATUS ProcessProtectDeviceControl(PDEVICE_OBJECT, PIRP irp)
{
	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto status = STATUS_SUCCESS;
	auto len = 0;

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_PROCESS_PROTECT_BY_PID:
	{
		auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (size % sizeof(ULONG) != 0)
		{
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		auto data = (ULONG*)irp->AssociatedIrp.SystemBuffer;


		for (int i = 0; i < size / sizeof(ULONG); i++)
		{
			auto pid = data[i];
			if (pid <= 0)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (FindProcess(pid))
				continue;
			if (g_Data.PidsCount == MaxPids) {
				status = STATUS_TOO_MANY_CONTEXT_IDS;
				break;
			}

			if (!AddProcess(pid))
			{
				status = STATUS_UNSUCCESSFUL;
				break;
			}

			len += sizeof(ULONG);
		}
		break;
	}
	case IOCTL_PROCESS_QUERY_PIDS:
	{
		/* need to fix this implementation: */
		auto size = stack->Parameters.DeviceIoControl.OutputBufferLength;
		if (size % sizeof(ULONG) != 0 && size > MaxPids * sizeof(ULONG))
		{
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		auto data = (ULONG*)irp->AssociatedIrp.SystemBuffer;

		for (int i = 0; i < MaxPids; i++)
		{
			if (g_Data.Pids[i] == 0)
			{
				break;
			}
			*data = g_Data.Pids[i];
			data++;
			len += sizeof(ULONG);
		}

		break;
	}

	case IOCTL_PROCESS_UNPROTECT_BY_PID:
	{

		auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (size % sizeof(ULONG) != 0)
		{
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		auto data = (ULONG*)irp->AssociatedIrp.SystemBuffer;

		for (int i = 0; i < size / sizeof(ULONG); i++)
		{
			auto pid = data[i];
			if (pid <= 0)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (g_Data.PidsCount == 0)
			{
				status = STATUS_NO_MATCH;
				break;
			}
			if (!RemoveProcess(pid))
			{
				continue;
			}

			len += sizeof(ULONG);
		}
		break;
	}
	case IOCTL_PROCESS_PROTECT_CLEAR:
	{
		AutoLock locker(g_Data.lock);
		::memset(&g_Data.Pids, 0, sizeof(g_Data.Pids));
		g_Data.PidsCount = 0;
		break;
	}
	default:
	{
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	}

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = len;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

NTSTATUS CompleteIrp(PIRP irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0)
{
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = info;
	IoCompleteRequest(irp, 0);
	return status;
}

NTSTATUS ProcessProtectCreateClose(PDEVICE_OBJECT, PIRP irp)
{
	return CompleteIrp(irp);
}

bool AddProcess(ULONG pid)
{
	AutoLock locker(g_Data.lock);
	for (int i = 0; i < MaxPids; i++)
	{
		if (g_Data.Pids[i] == 0)
		{
			g_Data.Pids[i] = pid;
			g_Data.PidsCount++;
			return true;
		}
	}
	return false;
}

bool RemoveProcess(ULONG pid)
{
	AutoLock locker(g_Data.lock);
	for (int i = 0; i < MaxPids; i++)
	{
		if (g_Data.Pids[i] == pid)
		{
			g_Data.Pids[i] = 0;
			g_Data.PidsCount--;
			return true;
		}
	}
	return false;
}

bool FindProcess(ULONG pid)
{
	AutoLock locker(g_Data.lock);
	for (int i = 0; i < MaxPids; i++) {
		if (g_Data.Pids[i] == pid)
			return true;
	}
	return false;
}