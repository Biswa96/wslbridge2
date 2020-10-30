/* 
 * This file is part of wslbridge2 project
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2020 Biswapriyo Nath
 */

/*
 * GetVmId.cpp: Get GUID of WSL2 Utility VM with LxssUserSession COM interface.
 */

#include <winsock.h>
#include <winternl.h> /* TEB, PEB, ConsoleHandle */
#include <assert.h>
#include <string>

#include "GetVmId.hpp"
#include "LxssUserSession.hpp"
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

static ILxssUserSession *wslSession = NULL;

void ComInit(void)
{
    HRESULT hRes;

    hRes = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    assert(hRes == 0);

    hRes = CoInitializeSecurity(NULL, -1, NULL, NULL,
                                RPC_C_AUTHN_LEVEL_DEFAULT,
                                SecurityDelegation, NULL,
                                EOAC_STATIC_CLOAKING, NULL);
    assert(hRes == 0);

    hRes = CoCreateInstance(CLSID_LxssUserSession,
                            NULL,
                            CLSCTX_LOCAL_SERVER,
                            IID_ILxssUserSession,
                            (PVOID *)&wslSession);
    assert(hRes == 0);
}

bool IsWslTwo(GUID *DistroId, const std::wstring DistroName)
{
    HRESULT hRes;
    PWSTR DistributionName, BasePath;
    PSTR KernelCommandLine, *DefaultEnvironment;
    ULONG Version, DefaultUid, EnvironmentCount, Flags;

    if (DistroName.empty())
    {
        hRes = wslSession->lpVtbl->GetDefaultDistribution(
            wslSession, DistroId);
    }
    else
    {
        hRes = wslSession->lpVtbl->GetDistributionId(
            wslSession, DistroName.c_str(), 0, DistroId);
    }
    assert(hRes == 0);

    hRes = wslSession->lpVtbl->GetDistributionConfiguration(
            wslSession,
            DistroId,
            &DistributionName,
            &Version,
            &BasePath,
            &KernelCommandLine,
            &DefaultUid,
            &EnvironmentCount,
            &DefaultEnvironment,
            &Flags);

    assert(hRes == 0);

    CoTaskMemFree(DistributionName);
    CoTaskMemFree(BasePath);
    CoTaskMemFree(KernelCommandLine);

    if (Flags > WSL_DISTRIBUTION_FLAGS_DEFAULT)
        return true;
    else
        return false;
}

HRESULT GetVmId(GUID *DistroId, GUID *LxInstanceID)
{
    HRESULT hRes;
    GUID InitiatedDistroID;
    LXSS_STD_HANDLES StdHandles = { 0 }; /* StdHandles member must be zero */
    HANDLE LxProcessHandle, ServerHandle;
    SOCKET SockIn, SockOut, SockErr, ServerSocket;

    const HANDLE ConsoleHandle = NtCurrentTeb()->
                                 ProcessEnvironmentBlock->
                                 ProcessParameters->
                                 Reserved2[0];

    const DWORD BuildNumber = GetWindowsBuild();

    /* Before Windows 10 Build 20211 RS */
    if (BuildNumber < 20211)
    {
        hRes = wslSession->lpVtbl->CreateLxProcess_One(
            wslSession,
            DistroId,
            nullptr, 0, nullptr, nullptr, nullptr,
            nullptr, 0, nullptr, 0, 0,
            HandleToULong(ConsoleHandle),
            &StdHandles,
            &InitiatedDistroID,
            LxInstanceID,
            &LxProcessHandle,
            &ServerHandle,
            &SockIn,
            &SockOut,
            &SockErr,
            &ServerSocket);
    }
    else
    {
        /* After Windows 10 Build 20211 RS */
        hRes = wslSession->lpVtbl->CreateLxProcess_Two(
            wslSession,
            DistroId,
            nullptr, 0, nullptr, nullptr, nullptr,
            nullptr, 0, nullptr, 0, 0,
            HandleToULong(ConsoleHandle),
            &StdHandles,
            0,
            &InitiatedDistroID,
            LxInstanceID,
            &LxProcessHandle,
            &ServerHandle,
            &SockIn,
            &SockOut,
            &SockErr,
            &ServerSocket);
    }

    assert(hRes == 0);

    /* wsltty#254: Closes extra shell process. */
    if (SockIn) closesocket(SockIn);
    if (SockOut) closesocket(SockOut);
    if (SockErr) closesocket(SockErr);
    if (LxProcessHandle) CloseHandle(LxProcessHandle);
    if (ServerHandle) CloseHandle(ServerHandle);

    if (wslSession)
        wslSession->lpVtbl->Release(wslSession);
    CoUninitialize();
    return hRes;
}
