#include "pch.h"
#include "..\ProcessProtector\ProcessProtectCommon.h"

std::vector<DWORD> ParsePids(const wchar_t* buffer[], int count);
int PrintUsage();

int Error(const char* message)
{
    printf("%s (error=%d)\n", message, GetLastError());
    return GetLastError();
}

int wmain(int argc, const wchar_t* argv[])
{
    if (argc < 2)
        return PrintUsage();

    enum class Options {
        Unkown,
        Add, Remove, Clear, Query
    };

    Options option = Options::Unkown;
    if (::_wcsicmp(argv[1], L"add") == 0)
        option = Options::Add;
    else if (::_wcsicmp(argv[1], L"remove") == 0)
        option = Options::Remove;
    else if (::_wcsicmp(argv[1], L"clear") == 0)
        option = Options::Clear;
    else if (::_wcsicmp(argv[1], L"query") == 0)
        option = Options::Clear;
    else
    {
        printf("Uknown Option.\n");
        return PrintUsage();
    }

    HANDLE hFile = ::CreateFile(L"\\\\.\\ProcProtect", GENERIC_WRITE | GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return Error("Failed to open device");
    }

    std::vector<DWORD> pids;
    BOOL success = FALSE;
    DWORD bytes;

    switch (option)
    {
        case Options::Add:
        {
            pids = ParsePids(argv + 2, argc - 2);
            success = ::DeviceIoControl(hFile, IOCTL_PROCESS_PROTECT_BY_PID, pids.data(), static_cast<DWORD>(pids.size()) * sizeof(DWORD), nullptr, 0, &bytes, nullptr);
            break;
        }

        case Options::Remove:
        {
            pids = ParsePids(argv + 2, argc - 2);
            success = ::DeviceIoControl(hFile, IOCTL_PROCESS_UNPROTECT_BY_PID, pids.data(), static_cast<DWORD>(pids.size()) * sizeof(DWORD), nullptr, 0, &bytes, nullptr);
            break;
        }

        case Options::Clear:
        {
            success = ::DeviceIoControl(hFile, IOCTL_PROCESS_PROTECT_CLEAR, nullptr, 0, nullptr, 0, &bytes, nullptr);
            break;
        }
        /* TODO: need to fix this implementation */
        case Options::Query:
        {
           success = ::DeviceIoControl(hFile, IOCTL_PROCESS_QUERY_PIDS, nullptr, 0, &pids, 256 * sizeof(DWORD), &bytes, nullptr);
           printf("[*] received %d bytes!", bytes);

           printf("[*] Current Protected Processes:  ");
           for (int i = 0; i < pids.size(); i++)
           {   
               printf("%d, ", pids[i]);
           }
           printf("\n");

           break;
        }
    }

    if (!success)
        return Error("Failed in DeviceIoControl");
    
    printf("Operation success.\n");

    ::CloseHandle(hFile);

    return 0;

}


std::vector<DWORD> ParsePids(const wchar_t* buffer[], int count)
{
    std::vector<DWORD> pids;
    for (int i = 0; i < count; i++)
    {
        pids.push_back(::_wtoi(buffer[i]));
    }
    return pids;
}

int PrintUsage()
{
    printf(".\ProcesProtectClientApp.exe add 1234 4523 2234: \n");
    printf(".\ProcesProtectClientApp.exe remove 1234 4523 2234: \n");
    printf(".\ProcesProtectClientApp.exe clear 1234 4523 2234: \n");
    return 1;
}