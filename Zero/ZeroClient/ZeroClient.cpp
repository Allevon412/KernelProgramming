#include "pch.h"
#include "..\Zero\ZeroCommon.h"

#include <Windows.h>
#include <stdio.h>

int Error(const char* message)
{
	printf("%s (error=%d)\n", message, GetLastError());
	return GetLastError();
}

int main()
{
	HANDLE hDevice = ::CreateFile(L"\\\\.\\Zero", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE)
	{
		return Error("failed to open device");
	}

	BYTE buffer[64];

	for (int i = 0; i < sizeof(buffer); i++) {
		buffer[i] = i + 1;
	}

	DWORD bytes;
	BOOL ok = ::ReadFile(hDevice, buffer, sizeof(buffer), &bytes, nullptr);
	if (!ok)
	{
		return Error("Failed to read buffer");
	}
	if (bytes != sizeof(buffer))
	{
		printf("Wrong number of bytes\n");
	}

	//check if buffer data sum is zero'd out meaning our driver did it's job.
	long total = 0;
	for (auto n : buffer)
	{
		total += n;
	}
	if (total != 0)
	{
		printf("Wrong data\n");
	}

	BYTE buffer2[1024];
	ok = ::WriteFile(hDevice, buffer2, sizeof(buffer2), &bytes, nullptr);
	if (!ok)
		return Error("failed to write");
	if (bytes != sizeof(buffer2))
	{
		printf("Wrong byte count\n");
	}

	ZeroStats stats;
	if (!DeviceIoControl(hDevice, IOCTL_ZERO_GET_STATS, nullptr, 0, &stats, sizeof(stats), &bytes, nullptr))
		return Error("failed in DeviceIoControl");

	printf("total Read: %lld, Total Write: %lld\n", stats.TotalRead, stats.TotalWritten);

	::CloseHandle(hDevice);
}