#include "pch.h"
#include "ZeroCommon.h"

#define DRIVER_PREFIX "Zero: "


void ZeroUnload(PDRIVER_OBJECT DriverObject);
DRIVER_DISPATCH ZeroRead, ZeroWrite, ZeroCreateClose, ZeroDeviceControl;

long long g_totalRead;
long long g_totalWritten;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	//Initialize Dispatch Routines and Unload Routines.
	DriverObject->DriverUnload = ZeroUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = ZeroCreateClose;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = ZeroWrite;
	DriverObject->MajorFunction[IRP_MJ_READ] = ZeroRead;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = ZeroDeviceControl;


	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\Zero");
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Zero");

	PDEVICE_OBJECT DeviceObject = nullptr;

	auto status = STATUS_SUCCESS;

	do {
		status = IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
		if (!NT_SUCCESS(status))
		{
			KdPrint((DRIVER_PREFIX "Failed to create device (0x08X)\n", status));
			break;
		}

		DeviceObject->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status))
		{
			KdPrint((DRIVER_PREFIX "Failed to create symbolic link (0x08X)\n", status));
			break;
		}

	} while (false);

	if (!NT_SUCCESS(status))
	{
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
		
	}
	return status;

}

NTSTATUS CompleteIrp(PIRP irp, NTSTATUS status = STATUS_SUCCESS, ULONG_PTR info = 0)
{
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = info;
	IoCompleteRequest(irp, 0);
	return status;
}

NTSTATUS ZeroCreateClose(PDEVICE_OBJECT, PIRP Irp)
{
	return CompleteIrp(Irp);
}





void ZeroUnload(PDRIVER_OBJECT DriverObject)
{
	KdPrint(("Zero: Driver Unload\n"));
	//unload what we've initialized in reverse order.

	//delete symbolic link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\Zero");
	IoDeleteSymbolicLink(&symLink);

	//delete device object.
	IoDeleteDevice(DriverObject->DeviceObject);
}


NTSTATUS ZeroWrite(PDEVICE_OBJECT, PIRP Irp)
{
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Write.Length;

	KdPrint(("Overwriting Write Buffer with %d len\n", len));

	InterlockedAdd64(&g_totalWritten, len);
	return CompleteIrp(Irp, STATUS_SUCCESS, len);
}


NTSTATUS ZeroRead(PDEVICE_OBJECT, PIRP Irp)
{

	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	if (len == 0)
	{
		return CompleteIrp(Irp, STATUS_INVALID_BUFFER_SIZE);
	}

	auto buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer)
		return CompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES);


	
	KdPrint(("Overwriting Read Buffer with %d bytes\n", len));
	memset(buffer, 0, len);
	InterlockedAdd64(&g_totalRead, len);

	return CompleteIrp(Irp, STATUS_SUCCESS, len);
}

NTSTATUS ZeroDeviceControl(PDEVICE_OBJECT, PIRP Irp)
{

	auto irpSp = IoGetCurrentIrpStackLocation(Irp);
	auto& dic = irpSp->Parameters.DeviceIoControl;
	auto status = STATUS_INVALID_DEVICE_REQUEST;

	ULONG_PTR len = 0;

	switch (dic.IoControlCode)
	{
		case IOCTL_ZERO_GET_STATS:
		{
			if (dic.OutputBufferLength < sizeof(ZeroStats))
			{
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			auto stats = (ZeroStats*)Irp->AssociatedIrp.SystemBuffer;
			if (stats == nullptr)
			{
				status = STATUS_INVALID_PARAMETER;
				break;
			}

			stats->TotalRead = g_totalRead;
			stats->TotalWritten = g_totalWritten;
			len = sizeof(ZeroStats);
			status = STATUS_SUCCESS;
			break;
		}

		case IOCTL_ZERO_CLEAR_STATS:
		{
			g_totalRead = g_totalWritten = 0;
			status = STATUS_SUCCESS;
			break;
		}

	}
	return CompleteIrp(Irp, status, len);
}



