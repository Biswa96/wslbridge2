#include <combaseapi.h>
#include <Windows.h>
#include <TlHelp32.h>
#include <winternl.h>
#include <ntstatus.h>
#include <psapi.h>
#include <vector>
#include <string>

#include "common.hpp"
#include "GetVmIdWsl2.hpp"

bool ExtractGUID(const std::wstring key, const std::wstring& commandLine, std::wstring& guid) {
    size_t pos = commandLine.find(key);
    if (pos != std::wstring::npos) 
    {
        size_t start = commandLine.find(L'{', pos);
        size_t end = commandLine.find(L'}', start);
        if (start != std::wstring::npos && end != std::wstring::npos) 
        {
            guid = commandLine.substr(start, end - start + 1);
            return true;
        }
    }
    return false;
}

bool GetCommandLineForPID(DWORD pid, std::wstring& commandLine)
{
    HMODULE hNtdll = GetModuleHandle(L"ntdll.dll");
    using NtQueryInformationProcessFunc = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG, PULONG);
    NtQueryInformationProcessFunc NtQueryInformationProcess = (NtQueryInformationProcessFunc)GetProcAddress(hNtdll, "NtQueryInformationProcess");

    if (!NtQueryInformationProcess) 
        return false;

    // Open a handle to the process
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == NULL)
    {
        DWORD err = GetLastError();
        fatal("failed to open the process, error: %d", err);
        return false;
    }
    // Get the address of the PEB
    PROCESS_BASIC_INFORMATION pbi = {};
    NTSTATUS status = NtQueryInformationProcess(process, ProcessBasicInformation, &pbi, sizeof(pbi), NULL);
    if (status != STATUS_SUCCESS)
    {
        CloseHandle(process);
        fatal("failed to query the process, error: %d", status);
        return false;
    }
    // Get the address of the process parameters in the PEB
    PEB peb = {};
    if (!ReadProcessMemory(process, pbi.PebBaseAddress, &peb, sizeof(peb), NULL))
    {
        CloseHandle(process);
        DWORD err = GetLastError();
        fatal("failed to read the process PEB, error: %d", err);
        return false;
    }
    // Get the command line arguments from the process parameters
    RTL_USER_PROCESS_PARAMETERS params = {};
    if (!ReadProcessMemory(process, peb.ProcessParameters, &params, sizeof(params), NULL))
    {
        CloseHandle(process);
        DWORD err = GetLastError();
        fatal("failed to read the process params, error: %d", err);
        return false;
    }
    UNICODE_STRING &commandLineArgs = params.CommandLine;
    std::vector<WCHAR> buffer(commandLineArgs.Length / sizeof(WCHAR));
    if (!ReadProcessMemory(process, commandLineArgs.Buffer, buffer.data(), commandLineArgs.Length, NULL))
    {
        CloseHandle(process);
        DWORD err = GetLastError();
        fatal("failed to read the process command line, error: %d", err);
        return false;
    }

    CloseHandle(process);
    commandLine.assign(buffer.data(), buffer.size());
    return true;
}

std::vector<DWORD> GetProcessIDsByName(const std::wstring& processName) {
    std::vector<DWORD> processIDs;
    HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(hProcessSnap, &pe32)) 
    {
        do 
        {
            if (pe32.szExeFile == processName) 
            {
                processIDs.push_back(pe32.th32ProcessID);
            }
        } while (Process32Next(hProcessSnap, &pe32));
    }
    CloseHandle(hProcessSnap);
    return processIDs;
}

// Extract GUID from wslHost.exe command line
// Example commandline:
// wslhost.exe --vm-id {f6446e02-236e-4b24-9916-2d4ad9a1096f} --handle 1664
bool GetVmIdWsl2(GUID& vmId) {
    std::vector<DWORD>  pids = GetProcessIDsByName(L"wslhost.exe");
    for (DWORD pid : pids) {
        std::wstring cmdLine;
        if (!GetCommandLineForPID(pid, cmdLine))
            continue;

        std::wstring cmdVmId;
        if(!ExtractGUID(L"--vm-id", cmdLine, cmdVmId)) 
            continue;
        
        if (IIDFromString(cmdVmId.c_str(), &vmId) == S_OK) 
            return true;
    }
    return false;
}

