/* 
 * This file is part of wslbridge2 project
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2021 Biswapriyo Nath
 */

/*
 * GetVmId.cpp: Get GUID of WSL2 Utility VM with LxssUserSession COM interface.
 */

#include <winsock.h>
#include <winternl.h> /* TEB, PEB, ConsoleHandle */
#include <assert.h>
#include <string>

#include "common.hpp"
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

static volatile union {
    ILxssUserSessionOne *wslSessionOne;
    ILxssUserSessionTwo *wslSessionTwo;
    ILxssUserSessionThree *wslSessionThree;
} ComObj = { NULL };

static void LxssErrCode(HRESULT hRes)
{
    // Custom error code from LxssManager COM interface.
    if (hRes == (HRESULT)0x80040302)
        fatal("There is no distribution with the supplied name.\n");
}

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

    // First try with COM server in lifted WSL service
    hRes = CoCreateInstance(CLSID_WslService,
                            NULL,
                            CLSCTX_LOCAL_SERVER,
                            IID_IWSLService,
                            (PVOID *)&ComObj);

    // Now try with COM server in system WSL service
    if (FAILED(hRes))
    {
        hRes = CoCreateInstance(CLSID_LxssUserSession,
                                NULL,
                                CLSCTX_LOCAL_SERVER,
                                IID_ILxssUserSession,
                                (PVOID *)&ComObj);
    }
    assert(hRes == 0);
}

bool IsWslTwo(GUID *DistroId, const std::wstring DistroName)
{
    HRESULT hRes;
    PWSTR DistributionName, BasePath;
    PSTR KernelCommandLine, *DefaultEnvironment;
    ULONG Version, DefaultUid, EnvironmentCount, Flags;

    const int WindowsBuild = GetWindowsBuild();

    if (WindowsBuild == 17763)
    {
        if (DistroName.empty())
            hRes = ComObj.wslSessionOne->lpVtbl->GetDefaultDistribution(
                ComObj.wslSessionOne, DistroId);
        else
            hRes = ComObj.wslSessionOne->lpVtbl->GetDistributionId(
                ComObj.wslSessionOne, DistroName.c_str(), 0, DistroId);

        LxssErrCode(hRes);

        hRes = ComObj.wslSessionOne->lpVtbl->GetDistributionConfiguration(
            ComObj.wslSessionOne,
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

        CoTaskMemFree(BasePath);
        CoTaskMemFree(KernelCommandLine);
    }
    else if (WindowsBuild < 21313) // Before Build 21313 Cobalt
    {
        if (DistroName.empty())
            hRes = ComObj.wslSessionTwo->lpVtbl->GetDefaultDistribution(
                ComObj.wslSessionTwo, DistroId);
        else
            hRes = ComObj.wslSessionTwo->lpVtbl->GetDistributionId(
                ComObj.wslSessionTwo, DistroName.c_str(), 0, DistroId);

        LxssErrCode(hRes);

        hRes = ComObj.wslSessionTwo->lpVtbl->GetDistributionConfiguration(
            ComObj.wslSessionTwo,
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

        CoTaskMemFree(BasePath);
        CoTaskMemFree(KernelCommandLine);
    }
    else // After Build 21313 Cobalt
    {
        if (DistroName.empty())
            hRes = ComObj.wslSessionThree->lpVtbl->GetDefaultDistribution(
                ComObj.wslSessionThree, DistroId);
        else
            hRes = ComObj.wslSessionThree->lpVtbl->GetDistributionId(
                ComObj.wslSessionThree, DistroName.c_str(), 0, DistroId);

        LxssErrCode(hRes);

        hRes = ComObj.wslSessionThree->lpVtbl->GetDistributionConfiguration(
            ComObj.wslSessionThree,
            DistroId,
            &DistributionName,
            &Version,
            &DefaultUid,
            &EnvironmentCount,
            &DefaultEnvironment,
            &Flags);

        assert(hRes == 0);
    }

    CoTaskMemFree(DistributionName);

    if (Flags > WSL_DISTRIBUTION_FLAGS_DEFAULT)
        return true;
    else
        return false;
}

HRESULT GetVmId(GUID *DistroId, GUID *LxInstanceID)
{
    HRESULT hRes;
    GUID InitiatedDistroID;
    HANDLE LxProcessHandle, ServerHandle;
    SOCKET SockIn, SockOut, SockErr, ServerSocket;

    // Initialize StdHandles members to be console handles by default.
    // otherwise LxssManager will catch undefined values.
    LXSS_STD_HANDLES StdHandles;
    memset(&StdHandles, 0, sizeof StdHandles);

    // Provides \Device\ConDrv\Connect interface of attached ConHost.
    const HANDLE ConsoleHandle = NtCurrentTeb()->
                                 ProcessEnvironmentBlock->
                                 ProcessParameters->
                                 Reserved2[0];

    const int WindowsBuild = GetWindowsBuild();

    /* Before Build 20211 Fe */
    if (WindowsBuild < 20211)
    {
        hRes = ComObj.wslSessionTwo->lpVtbl->CreateLxProcess(
            ComObj.wslSessionTwo,
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
        /* After Build 20211 Fe */
        hRes = ComObj.wslSessionThree->lpVtbl->CreateLxProcess(
            ComObj.wslSessionThree,
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

    if (ComObj.wslSessionOne)
        ComObj.wslSessionOne->lpVtbl->Release(ComObj.wslSessionOne);
    CoUninitialize();
    return hRes;
}
