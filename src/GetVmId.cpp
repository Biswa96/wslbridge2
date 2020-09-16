/* 
 * This file is part of wslbridge2 project
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath
 */

/*
 * GetVmId.cpp: Get GUID of WSL2 Utility VM with LxssUserSession COM interface.
 */

#include <windows.h>
#include <winternl.h> /* TEB, PEB, ConsoleHandle */
#include <assert.h>
#include <string>

#include "GetVmId.hpp"
#include "LxssUserSession.hpp"

HRESULT GetVmId(
    GUID *LxInstanceID,
    const std::wstring &DistroName,
    int *WslVersion)
{
    HRESULT hRes;
    GUID DistroId, InitiatedDistroID;
    LXSS_STD_HANDLES StdHandles = { 0 }; /* StdHandles member must be zero */
    HANDLE LxProcessHandle, ServerHandle;
    SOCKET SockIn, SockOut, SockErr, ServerSocket;

    const HANDLE ConsoleHandle = NtCurrentTeb()->
                                 ProcessEnvironmentBlock->
                                 ProcessParameters->
                                 Reserved2[0];

    hRes = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    assert(hRes == 0);

    hRes = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                                RPC_C_AUTHN_LEVEL_DEFAULT,
                                SecurityDelegation, nullptr,
                                EOAC_STATIC_CLOAKING, nullptr);
    assert(hRes == 0);

    ILxssUserSession *wslSession = nullptr;

    hRes = CoCreateInstance(CLSID_LxssUserSession,
                            nullptr,
                            CLSCTX_LOCAL_SERVER,
                            IID_ILxssUserSession,
                            (PVOID *)&wslSession);
    assert(hRes == 0);

    if (DistroName.empty())
    {
        hRes = wslSession->lpVtbl->GetDefaultDistribution(
            wslSession, &DistroId);
    }
    else
    {
        hRes = wslSession->lpVtbl->GetDistributionId(
            wslSession, DistroName.c_str(), 0, &DistroId);
    }

    assert(hRes == 0);

    if (hRes == 0)
    {
        hRes = wslSession->lpVtbl->CreateLxProcess(
            wslSession,
            &DistroId,
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

        if (hRes != 0)
        {
            *WslVersion = 0;
            goto Cleanup;
        }
    }

    /* ServerHandle and ServerSocket are exclusive */
    if (ServerHandle == nullptr && ServerSocket != 0)
        *WslVersion = WSL_VERSION_TWO;
    else
        *WslVersion = WSL_VERSION_ONE;

Cleanup:
    if (wslSession)
        wslSession->lpVtbl->Release(wslSession);
    CoUninitialize();
    return hRes;
}
