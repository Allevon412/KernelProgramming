#include <ntddk.h>


void SampleUnload(_In_ PDRIVER_OBJECT DriverObjet)
{
	UNREFERENCED_PARAMETER(DriverObjet);

	KdPrint(("Sample Driver Unload called\n"));
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);

	DriverObject->DriverUnload = SampleUnload;

	KdPrint(("Sample Driver Initialized successfully\n"));
	RTL_OSVERSIONINFOW osVersionW = { 0 };
	osVersionW.dwOSVersionInfoSize = sizeof(osVersionW);
	if (RtlGetVersion(&osVersionW) != STATUS_SUCCESS)
	{
		return -1;
	}
	KdPrint(("%d, %d, %d\n", osVersionW.dwBuildNumber, osVersionW.dwMajorVersion, osVersionW.dwMinorVersion));

	return STATUS_SUCCESS;
}