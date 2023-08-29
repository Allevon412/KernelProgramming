#include <Windows.h>
#include <stdio.h>

#include "..\PriorityBooster\PriorityBoosterCommon.h"


int Error(const char* message)
{
	printf("%s (error=%d)\n", message, GetLastError());
	return 1;
}

int main(int argc, const char* argv[])
{
	if (argc < 3)
	{
		printf("Usage: Booster <threadId> <priority>\n");
		return 0;
	}

	//open handle to our device object that was created by the kernel driver & has a symbolic link.
	HANDLE hDevice = CreateFile(L"\\\\.\\PriorityBooster", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE)
		return Error("Failed to open Device");

	//setup target for thread priority boosting.
	ThreadData data;
	data.ThreadID = atoi(argv[1]);
	data.Priority = atoi(argv[2]);

	//send request to the device.
	//reaches the driver by invoking the IRP_MJ_DEVICE_CONTROL major function routine.
	DWORD returned;
	BOOL success = DeviceIoControl(hDevice, IOCTL_PRIORITY_BOOSTER_SET_PRIORITY, &data, sizeof(data), nullptr, 0, &returned, nullptr);
	
	//print success status.
	if (success)
	{
		printf("Priority change succeeded\n");
	}
	else
		Error("Priority Change failed\n");
	
	//close device handle.
	CloseHandle(hDevice);


}