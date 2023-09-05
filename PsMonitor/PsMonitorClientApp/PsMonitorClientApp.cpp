// PsMonitorClientApp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "..\PsMonitor\PsMonitorCommon.h"



int Error(const char* message)
{
    printf("%s (error=%d)\n", message, GetLastError());
    return GetLastError();
}

void DisplayInfo(BYTE* buffer, DWORD size);
void DisplayTime(const LARGE_INTEGER& time);

int main()
{
    auto hFile = ::CreateFile(L"\\\\.\\psmonitor", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return Error("Failed to Open File");
    }

    BYTE buffer[1 << 16]; // 64KB buffer.
    while (true)
    {
        DWORD bytes;
        if (!::ReadFile(hFile, buffer, sizeof(buffer), &bytes, nullptr))
            return Error("Failed to Read");

        if (bytes != 0)
            DisplayInfo(buffer, bytes);

        ::Sleep(200);
    }

}

void DisplayInfo(BYTE* buffer, DWORD size)
{
    auto count = size;
    while (count > 0)
    {
        auto header = (ItemHeader*)buffer;

        switch (header->Type)
        {
            case ItemType::ProcessExit:
            {
                DisplayTime(header->Time);
                auto info = (ProcessExitInfo*)buffer;
                printf("Process %d Exited\n", info->ProcessId);
                break;
            }

            case ItemType::ProcessCreate:
            {
                DisplayTime(header->Time);
                auto info = (ProcessCreateInfo*)buffer;
                std::wstring commandline((WCHAR*)(buffer + info->CommandLineOffset), info->CommandLineLength);
                std::wstring imagename((WCHAR*)(buffer + info->ImageFileNameOffset), info->ImageFileNameLength);
                std::wstring applicationName;
                for (int i = imagename.length() -1; i >= 0; i--)
                {
                    if (imagename[i] == L'\\') {
                        applicationName = imagename.substr(i+1, imagename.length() - i);
                        break;
                    }
                }
                printf("[*] Process %ws : %d Created.\n\t\tCommand Line: %ws\n", applicationName.c_str(), info->ProcessId, commandline.c_str());
                break;
            }
            default:
                break;
        }
        buffer += header->size;
        count -= header->size;
    }
}

void DisplayTime(const LARGE_INTEGER& time)
{
    SYSTEMTIME st;
    ::FileTimeToSystemTime((FILETIME*)&time, &st);
    printf("%02d:%02d:%02d.%03d: ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}