#include "pch.h"
#include "MiniFilter.h"
#include "DriverMain.h"
#include "C:\Users\Brendan Ortiz\Desktop\UA-Stuff\Fall23\CYBV498Capstone\DriverProjects\SupportingCode\FltGetFileNameRAIIWrapper.h"


NTSTATUS DelProtectInstanceQueryTeardown(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags);
VOID DelProtectInstanceTeardownStart(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags);
VOID DelProtectInstanceTeardownComplete(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags);
NTSTATUS DelProtectInstanceSetup(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDevicetype, FLT_FILESYSTEM_TYPE VolumeFileSystemType);
NTSTATUS DelProtectUnload(FLT_FILTER_UNLOAD_FLAGS flags);
FLT_PREOP_CALLBACK_STATUS DelProtectPreCreate(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*);
FLT_PREOP_CALLBACK_STATUS DelProtectPreSetInformation(
	PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*
);
bool IsDeleteAllowed(PCUNICODE_STRING filename);

NTSTATUS InitMiniFilter(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	WCHAR ext[] = L"PDF;";

	g_State.Extensions.Buffer = (PWSTR)ExAllocatePool2(POOL_FLAG_PAGED, sizeof(ext), DRIVER_TAG);
	if (g_State.Extensions.Buffer == nullptr)
		return STATUS_NO_MEMORY;

	memcpy(g_State.Extensions.Buffer, ext, sizeof(ext));
	g_State.Extensions.Length = g_State.Extensions.MaximumLength = sizeof(ext);


	HANDLE hKey = nullptr, hSubKey = nullptr;
	NTSTATUS status;
	do {
		//
		// add registry data for proper mini-filter registration
		//
		OBJECT_ATTRIBUTES keyAttr = RTL_CONSTANT_OBJECT_ATTRIBUTES(RegistryPath, OBJ_KERNEL_HANDLE);
		status = ZwOpenKey(&hKey, KEY_WRITE, &keyAttr);
		if (!NT_SUCCESS(status))
			break;

		UNICODE_STRING subKey = RTL_CONSTANT_STRING(L"Instances");
		OBJECT_ATTRIBUTES subKeyAttr;
		InitializeObjectAttributes(&subKeyAttr, &subKey, OBJ_KERNEL_HANDLE, hKey, nullptr);
		status = ZwCreateKey(&hSubKey, KEY_WRITE, &subKeyAttr, 0, nullptr, 0, nullptr);
		if (!NT_SUCCESS(status))
			break;

		//
		// set "DefaultInstance" value
		//
		UNICODE_STRING valueName = RTL_CONSTANT_STRING(L"DefaultInstance");
		WCHAR name[] = L"DelProtectDefaultInstance";
		status = ZwSetValueKey(hSubKey, &valueName, 0, REG_SZ, name, sizeof(name));
		if (!NT_SUCCESS(status))
			break;

		//
		// create "instance" key under "Instances"
		//
		UNICODE_STRING instKeyName;
		RtlInitUnicodeString(&instKeyName, name);
		HANDLE hInstKey;
		InitializeObjectAttributes(&subKeyAttr, &instKeyName, OBJ_KERNEL_HANDLE, hSubKey, nullptr);
		status = ZwCreateKey(&hInstKey, KEY_WRITE, &subKeyAttr, 0, nullptr, 0, nullptr);
		if (!NT_SUCCESS(status))
			break;

		//
		// write out altitude
		//
		WCHAR altitude[] = L"425342";
		UNICODE_STRING altitudeName = RTL_CONSTANT_STRING(L"Altitude");
		status = ZwSetValueKey(hInstKey, &altitudeName, 0, REG_SZ, altitude, sizeof(altitude));
		if (!NT_SUCCESS(status))
			break;

		//
		// write out flags
		//
		UNICODE_STRING flagsName = RTL_CONSTANT_STRING(L"Flags");
		ULONG flags = 0;
		status = ZwSetValueKey(hInstKey, &flagsName, 0, REG_DWORD, &flags, sizeof(flags));
		if (!NT_SUCCESS(status))
			break;

		ZwClose(hInstKey);

		FLT_OPERATION_REGISTRATION const callbacks[] =
		{
			{IRP_MJ_CREATE, 0, DelProtectPreCreate, nullptr},
			{IRP_MJ_SET_INFORMATION, 0, DelProtectPreSetInformation, nullptr},
			{IRP_MJ_OPERATION_END}
		};

		FLT_REGISTRATION const reg =
		{
			sizeof(FLT_REGISTRATION),
			FLT_REGISTRATION_VERSION,
			0,						//flags
			nullptr,				//context
			callbacks,				//operation callbacks
			DelProtectUnload,		//unload routine for mini-filter
			DelProtectInstanceSetup,	//instance setup
			DelProtectInstanceQueryTeardown,	//instance query teardown
			DelProtectInstanceTeardownStart,	//instance tear down start
			DelProtectInstanceTeardownComplete,	//InstanceTearDownComplete
		};
		status = FltRegisterFilter(DriverObject, &reg, &g_State.Filter);


	} while (false);

	if (hSubKey) {
		if (!NT_SUCCESS(status))
			ZwDeleteKey(hSubKey);
		ZwClose(hSubKey);
	}
	if (hKey)
		ZwClose(hKey);

	return status;

}

NTSTATUS DelProtectInstanceQueryTeardown(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags) {
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	return STATUS_SUCCESS;
}

VOID DelProtectInstanceTeardownStart(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags) {
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}

VOID DelProtectInstanceTeardownComplete(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_TEARDOWN_FLAGS Flags) {
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
}

NTSTATUS DelProtectInstanceSetup(PCFLT_RELATED_OBJECTS FltObjects, FLT_INSTANCE_SETUP_FLAGS Flags, DEVICE_TYPE VolumeDevicetype, FLT_FILESYSTEM_TYPE VolumeFileSystemType)
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDevicetype);

	return VolumeFileSystemType == FLT_FSTYPE_NTFS ? STATUS_SUCCESS : STATUS_FLT_DO_NOT_ATTACH;
}

NTSTATUS DelProtectUnload(FLT_FILTER_UNLOAD_FLAGS flags)
{
	UNREFERENCED_PARAMETER(flags);

	FltUnregisterFilter(g_State.Filter);
	g_State.Lock.Delete();
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\DelProtect");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(g_State.DriverObject->DeviceObject);

	return STATUS_SUCCESS;
}

FLT_PREOP_CALLBACK_STATUS DelProtectPreCreate(PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*)
{
	UNREFERENCED_PARAMETER(FltObjects);
	if (Data->RequestorMode == KernelMode)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;

	const auto& params = Data->Iopb->Parameters.Create;
	auto status = FLT_PREOP_SUCCESS_NO_CALLBACK;

	if (params.Options & FILE_DELETE_ON_CLOSE)
	{
		auto filename = &FltObjects->FileObject->FileName;
		KdPrint(("Delete on close: %wZ\n", filename));

		if (IsDeleteAllowed(filename))
		{
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			status = FLT_PREOP_COMPLETE;
			KdPrint(("(Pre Create) Prevent Deletion of %wZ\n", filename));
		}
	}

	return status;
}

FLT_PREOP_CALLBACK_STATUS DelProtectPreSetInformation(
	PFLT_CALLBACK_DATA Data, PCFLT_RELATED_OBJECTS FltObjects, PVOID*
)
{
	UNREFERENCED_PARAMETER(FltObjects);

	if (Data->RequestorMode == KernelMode)
		return FLT_PREOP_SUCCESS_NO_CALLBACK;

	auto status = FLT_PREOP_SUCCESS_NO_CALLBACK;
	auto& params = Data->Iopb->Parameters.SetFileInformation;
	if (params.FileInformationClass == FileDispositionInformation || params.FileInformationClass == FileDispositionInformationEx)
	{
		auto info = (FILE_DISPOSITION_INFORMATION*)params.InfoBuffer;
		if (info->DeleteFile & 1) // also covers the flags in the FileDispositionInformationEx struct.
		{

			//using FLT_FILE_NAME_NORMALIZED is important here for parsing purposes.
			FilterFileNameInformation nameInfo(Data);
			if (nameInfo)
			{
				if (NT_SUCCESS(nameInfo.Parse()))
				{
					if (!IsDeleteAllowed(&nameInfo->Name))
					{
						Data->IoStatus.Status = STATUS_ACCESS_DENIED;
						KdPrint(("(Pre Set Information) Prevent Deletion of %wZ\n", &nameInfo->Name));
						status = FLT_PREOP_COMPLETE;
					}
				}
			}
		}
	}
	return status;
}

bool IsDeleteAllowed(PCUNICODE_STRING filename)
{
	UNICODE_STRING ext;
	if (NT_SUCCESS(FltParseFileName(filename, &ext, nullptr, nullptr)))
	{
		WCHAR uext[16] = { 0 };
		UNICODE_STRING suext;
		suext.Buffer = uext;

		//save space for null terminator and semicolon
		suext.MaximumLength = sizeof(uext) - 2 * sizeof(WCHAR);
		RtlUpcaseUnicodeString(&suext, &ext, FALSE);
		RtlAppendUnicodeToString(&suext, L";");

		//search for prefix
		return wcsstr(g_State.Extensions.Buffer, uext) == nullptr;
	}

	return true;
}