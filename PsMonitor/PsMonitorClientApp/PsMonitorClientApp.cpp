// PsMonitorClientApp.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include "..\PsMonitor\PsMonitorCommon.h"
#include "PsMonitorClientApp.h"



LinkedList<Process> ProcessList;
//implementation of linkedlist failed becaus i forgot to realize that the processes that already exist on the system will cause a crash when attempting to remove them 

int Error(const char* message)
{
    printf("%s (error=%d)\n", message, GetLastError());
    return GetLastError();
}

void DisplayInfo(BYTE* buffer, DWORD size);
void DisplayTime(const LARGE_INTEGER& time);
void populateProcessList();

int main()
{
    populateProcessList();
    auto hFile = ::CreateFile(L"\\\\.\\psmonitor", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        return Error("Failed to Open File");
    }
    
    WCHAR exclusionPath[] = L"C:\\Users\\Breadman\\Downloads";
    DWORD bytes;

    if (!::WriteFile(hFile, exclusionPath, sizeof(exclusionPath), &bytes, nullptr))
        return Error("Failure to send exclusionpath to driver");

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
                Process removal = { info->ProcessId, L"" };
                Node<Process>* procPtr = ProcessList.retreiveNode(removal);
                if (procPtr != nullptr)
                {
                    printf("Process %ws : %d Exited\n", procPtr->data.ImageName.c_str(), info->ProcessId);
                    ProcessList.removeNode(removal);
                }
                else
                {
                    printf("Process %d Exited\n", info->ProcessId);
                }
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
                printf("Process %ws : %d Created.\n\t\tCommand Line: %ws\n", applicationName.c_str(), info->ProcessId, commandline.c_str());

                Process node;
                node.ProcessId = info->ProcessId;
                node.ImageName = applicationName;
                ProcessList.insert(node);
                
                break;
            }
            
            case ItemType::ThreadCreate:
            {
                DisplayTime(header->Time);
                auto info = (ThreadCreateExitInfo*)buffer;
                if (info->remote == false)
                {
                    Process temp = { info->ProcessId, L"" };
                    Node<Process>* procPtr = ProcessList.retreiveNode(temp);
                    if (procPtr != nullptr)
                    {
                        printf("Process %ws : %d Created a Thread: %d\n", procPtr->data.ImageName.c_str(), info->ProcessId, info->ThreadId);
                    }
                    else
                    {
                        printf("Thread %d Created in Process %d\n", info->ThreadId, info->ProcessId);
                    }
                    break;
                }
                else
                {
                    Process temp = { info->CreateProcessId, L"" };
                    Process temp2 = { info->ProcessId, L"" };
                    Node<Process>* procPtr = ProcessList.retreiveNode(temp);
                    Node<Process>* procPtr2 = ProcessList.retreiveNode(temp2);
                    if (procPtr != nullptr)
                    {
                        printf("Thread %d in Process %ws : %d created a remote thread %d in process %ws : %d\n", info->CreatorThreadId, procPtr->data.ImageName.c_str(), info->CreateProcessId, info->ThreadId, procPtr2->data.ImageName.c_str(), info->ProcessId);
                    }
                    else
                    {
                        printf("Thread %d in process %d created a remote thread %d in process %d\n", info->CreatorThreadId, info->CreateProcessId, info->ThreadId, info->ProcessId);
                    }
                    break;
                }
               
            }
            case ItemType::ThreadExit:
            {
                DisplayTime(header->Time);
                auto info = (ThreadCreateExitInfo*)buffer;
               
                Process temp = { info->ProcessId, L"" };
                Node<Process>* procPtr = ProcessList.retreiveNode(temp);
                if (procPtr != nullptr)
                {
                    printf("Process %ws : %d Exited a Thread: %d\n", procPtr->data.ImageName.c_str(), info->ProcessId, info->ThreadId);
                }
                else
                {
                    printf("Thread %d Exit in Process %d\n", info->ThreadId, info->ProcessId);
                }
                break;
              
            }
            case ItemType::ImageLoad:
            {
                DisplayTime(header->Time);
                auto info = (ImageLoadInfo*)buffer;
                Process temp = { info->ProcessId, L"" };
                Node<Process>* procPtr = ProcessList.retreiveNode(temp);
                if (procPtr != nullptr)
                {
                    std::wstring imagename((WCHAR*)(buffer + info->ImageNameOffset), info->ImageNameLength);
                    printf("Process %ws : %d loaded image: %ws at loc: 0x%08X\n", procPtr->data.ImageName.c_str(), info->ProcessId, imagename.c_str(), info->ImageBase);
                }
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
    printf("[*] %02d:%02d:%02d.%03d: ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void populateProcessList()
{
    DWORD procId = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32W procEntry;
        procEntry.dwSize = sizeof(PROCESSENTRY32W);

        if (Process32First(hSnap, &procEntry))
        {
            do {

                Process proc;
                proc.ImageName = procEntry.szExeFile;
                proc.ProcessId = procEntry.th32ProcessID;
                ProcessList.insert(proc);

            } while (Process32Next(hSnap, &procEntry));
        }
    }
}