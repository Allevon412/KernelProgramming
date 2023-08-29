#include <ntifs.h>

#include "PriorityBoosterCommon.h"


void PriorityBoosterUnload(_In_ PDRIVER_OBJECT DriverObject);
NTSTATUS PriorityBoosterCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);
NTSTATUS PriorityBoosterDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp);

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	KdPrint(("Booster: DriverEntry \n"));

	UNREFERENCED_PARAMETER(RegistryPath);

	//initialize the unload function.
	DriverObject->DriverUnload = PriorityBoosterUnload;

	//initialize the dispatch routines we want to support.
	DriverObject->MajorFunction[IRP_MJ_CREATE] = PriorityBoosterCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = PriorityBoosterCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PriorityBoosterDeviceControl;

	//initialize a unicode string for a device object name.
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\PriorityBooster");
	//RtlInitUnicodeString(&devName, L"\\Device\\ThreadBoost");
	
	//initialize a device object.
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
	if(!NT_SUCCESS(status))
	{
		KdPrint(("Failed to create device object (0x%08X)\n", status));
		return status;
	}

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\PriorityBooster");
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status))
	{
		KdPrint(("Failed to create symbolic link (0x%08X)\n", status));
		return status;
	}

	return STATUS_SUCCESS;
}

void PriorityBoosterUnload(_In_ PDRIVER_OBJECT DriverObject) 
{
	KdPrint(("Booster: Driver Unload\n"));
	//unload what we've initialized in reverse order.

	//delete symbolic link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\PriorityBooster");
	IoDeleteSymbolicLink(&symLink);

	//delete device object.
	IoDeleteDevice(DriverObject->DeviceObject);

}

_Use_decl_annotations_
NTSTATUS PriorityBoosterCreateClose(_In_ PDEVICE_OBJECT DeviceObject, _In_ PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}


_Use_decl_annotations_
NTSTATUS PriorityBoosterDeviceControl(_In_ PDEVICE_OBJECT, _In_ PIRP Irp)
{

	//get our Io_STACK_LOCATION
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto status = STATUS_SUCCESS;

	switch (stack->Parameters.DeviceIoControl.IoControlCode)
	{
		case IOCTL_PRIORITY_BOOSTER_SET_PRIORITY: {

			if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ThreadData))
			{
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}
			auto data = (ThreadData*)(stack->Parameters.DeviceIoControl.Type3InputBuffer);
			if (data == nullptr)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			if (data->Priority < 1 || data->Priority > 31)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			PETHREAD thread;
			status = PsLookupThreadByThreadId(ULongToHandle(data->ThreadID), &thread);
			if (!NT_SUCCESS(status))
			{
				break;
			}
			KeSetPriorityThread((PKTHREAD)thread, data->Priority);
			ObDereferenceObject(thread);
			KdPrint(("Thread Priority Changed for %d to %d succeed!\n", data->ThreadID, data->Priority));



			break;
		}

		default:
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
	}

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

