/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

/*
 * WinHelper.cpp: Helper functions containing Windows APIs only.
 */

#include <winsock2.h>
#include <assert.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <wchar.h>

#include <string>
#include <vector>

#include "common.hpp"
#include "Helpers.hpp"

#ifndef WSL_DISTRIBUTION_FLAGS_VALID

#define WSL_DISTRIBUTION_FLAGS_NONE 0
#define WSL_DISTRIBUTION_FLAGS_ENABLE_INTEROP 1
#define WSL_DISTRIBUTION_FLAGS_APPEND_NT_PATH 2
#define WSL_DISTRIBUTION_FLAGS_ENABLE_DRIVE_MOUNTING 4
#define WSL_DISTRIBUTION_FLAGS_DEFAULT \
    ( WSL_DISTRIBUTION_FLAGS_ENABLE_INTEROP | \
      WSL_DISTRIBUTION_FLAGS_APPEND_NT_PATH | \
      WSL_DISTRIBUTION_FLAGS_ENABLE_DRIVE_MOUNTING )

#endif /* WSL_DISTRIBUTION_FLAGS_VALID */

typedef HRESULT (WINAPI *GETDISTROCONFIGPROC)(
    const wchar_t *distributionName,
    int *distributionVersion,
    int *defaultUID,
    int *wslDistributionFlags,
    char **defaultEnvironmentVariables,
    int *defaultEnvironmentVariableCount);

typedef void (WINAPI *RTLGETVERSIONPROC)(
    OSVERSIONINFOW *lpVersionInformation);

std::string GetErrorMessage(DWORD MessageId)
{
    wchar_t *Buffer = NULL;
    const DWORD formatRet = FormatMessageW(
                            FORMAT_MESSAGE_FROM_SYSTEM |
                            FORMAT_MESSAGE_ALLOCATE_BUFFER |
                            FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL,
                            MessageId,
                            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                            (PWSTR)&Buffer,
                            0,
                            NULL);

    std::string msg;
    if (formatRet == 0 || Buffer == NULL)
    {
        char buf[64];
        sprintf(buf, "(%#x)", (unsigned int)MessageId);
        msg = buf;
    }
    else
        msg = wcsToMbs(Buffer);
    LocalFree(Buffer);

    return msg;
}

static std::wstring GetDefaultDistribution(void)
{
    LSTATUS ret;
    HKEY hKeyUser, hkResult;
    wchar_t* data = NULL;
    DWORD size;
    std::wstring subkey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Lxss";
    HANDLE hHeap = GetProcessHeap();

    ret = RegOpenCurrentUser(KEY_READ, &hKeyUser);
    assert(ret == 0);

    ret = RegOpenKeyExW(hKeyUser, subkey.c_str(), 0, KEY_READ, &hkResult);
    assert(ret == 0);

    ret = RegGetValueW(hkResult, NULL, L"DefaultDistribution",
            RRF_RT_REG_SZ, NULL, NULL, &size);

    data = (wchar_t*)HeapAlloc(hHeap, 0, size);
    ret = RegGetValueW(hkResult, NULL, L"DefaultDistribution",
            RRF_RT_REG_SZ, NULL, data, &size);
    assert(ret == 0);

    subkey += L"\\";
    subkey += data;

    HeapFree(hHeap, 0, data);
    RegCloseKey(hkResult);

    ret = RegOpenKeyExW(hKeyUser, subkey.c_str(), 0, KEY_READ, &hkResult);
    assert(ret == 0);

    ret = RegGetValueW(hkResult, NULL, L"DistributionName",
            RRF_RT_REG_SZ, NULL, NULL, &size);

    data = (wchar_t*)HeapAlloc(hHeap, 0, size);
    ret = RegGetValueW(hkResult, NULL, L"DistributionName",
            RRF_RT_REG_SZ, NULL, data, &size);
    assert(ret == 0);

    std::wstring DistroName(data);

    HeapFree(hHeap, 0, data);
    RegCloseKey(hkResult);

    RegCloseKey(hKeyUser);
    return DistroName;
}

bool IsWslTwo(std::wstring DistroName)
{
    HMODULE hMod = LoadLibraryExW(
                   L"wslapi.dll",
                   NULL,
                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(hMod != NULL);

    GETDISTROCONFIGPROC pfnGetDistroConfig = (GETDISTROCONFIGPROC)
        GetProcAddress(hMod, "WslGetDistributionConfiguration");
    assert(pfnGetDistroConfig != NULL);

    if (DistroName.empty())
        DistroName = GetDefaultDistribution();

    int version, uid, flag, count;
    char* variable;
    HRESULT hRes = pfnGetDistroConfig(DistroName.c_str(),
                &version, &uid, &flag, &variable, &count);
    if (hRes != 0)
        fatal("WslGetDistributionConfiguration: %s", GetErrorMessage(hRes).c_str());

    FreeLibrary(hMod);

    if (flag > WSL_DISTRIBUTION_FLAGS_DEFAULT)
        return true;
    else
        return false;
}

DWORD GetWindowsBuild(void)
{
    HMODULE hMod = LoadLibraryExW(
                   L"ntdll.dll",
                   NULL,
                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(hMod != NULL);

    RTLGETVERSIONPROC pfnRtlGetVersion = (RTLGETVERSIONPROC)
                        GetProcAddress(hMod, "RtlGetVersion");
    assert(pfnRtlGetVersion != NULL);

    OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof info;
    pfnRtlGetVersion(&info);
    FreeLibrary(hMod);

    return info.dwBuildNumber;
}

void GetIp(void)
{
    ULONG ret, size;
    ULONG Flags = GAA_FLAG_SKIP_FRIENDLY_NAME |
                  GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_ANYCAST;
    HANDLE hHeap = GetProcessHeap();

    ret = GetAdaptersAddresses(AF_UNSPEC, Flags, NULL, NULL, &size);
    auto adpAddr = (PIP_ADAPTER_ADDRESSES)HeapAlloc(hHeap, 0, size);
    ret = GetAdaptersAddresses(AF_UNSPEC, Flags, NULL, adpAddr, &size);

    if (ret == 0)
    {
        auto adpAddrTemp = adpAddr;
        while (adpAddrTemp)
        {
            /* Find interface name containing "WSL" string */
            if (wcsstr(adpAddrTemp->FriendlyName, L"WSL"))
            {
                // fwprintf(stdout, L"Interface Name: %ls\n", adpAddrTemp->FriendlyName);

                ret = GetAdaptersInfo(NULL, &size);
                auto adpInfo = (PIP_ADAPTER_INFO)HeapAlloc(hHeap, 0, size);
                ret = GetAdaptersInfo(adpInfo, &size);

                if (ret == 0)
                {
                    auto adpInfoTemp = adpInfo;
                    while (adpInfoTemp)
                    {
                        /* Check if network adapter index matches */
                        if (adpAddrTemp->IfIndex == adpInfoTemp->Index)
                        {
                            // fprintf(stdout, "IP: %s\n", adpInfoTemp->IpAddressList.IpAddress.String);
                            // setenv("WSL_HOST_IP", adpInfoTemp->IpAddressList.IpAddress.String, false);
                            SetEnvironmentVariableA("WSL_HOST_IP", adpInfoTemp->IpAddressList.IpAddress.String);
                            break;
                        }
                        adpInfoTemp = adpInfoTemp->Next;
                    }
                }
                else
                    fprintf(stderr, "GetAdaptersInfo: %s", GetErrorMessage(ret).c_str());

                HeapFree(hHeap, 0, adpInfo);

                /* Exit from loop if we find that interface name */
                break;
            }
            adpAddrTemp = adpAddrTemp->Next;
        }
    }
    else
        fprintf(stderr, "GetAdaptersAddresses: %s", GetErrorMessage(ret).c_str());

    HeapFree(hHeap, 0, adpAddr);
}
