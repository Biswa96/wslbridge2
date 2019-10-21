/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

/*
 * WinHelper.cpp: Helper functions containing Windows APIs only.
 */

#include <windows.h>
#include <assert.h>
#include <wchar.h>

#include <string>

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

    subkey += L"\\\\";
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
    assert(hRes == 0);
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
