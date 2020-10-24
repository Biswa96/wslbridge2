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
#include "WinHelper.hpp"

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

    const DWORD BuildNumber = GetWindowsBuild();

    /* Before Windows 10 Build 20211 RS */
    if (BuildNumber < 20211)
    {
        hRes = wslSession->lpVtbl->CreateLxProcess_One(
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
    }
    else
    {
        /* After Windows 10 Build 20211 RS */
        hRes = wslSession->lpVtbl->CreateLxProcess_Two(
            wslSession,
            &DistroId,
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

    if (hRes != 0)
    {
        *WslVersion = 0;
        goto Cleanup;
    }

    /* ServerHandle is for WSL1. So, nullptr for WSL2. */
    if (ServerHandle == nullptr)
    {
        *WslVersion = WSL_VERSION_TWO;

        /* wsltty#254: Closes extra shell process. */
        if (SockIn) closesocket(SockIn);
        if (SockOut) closesocket(SockOut);
        if (SockErr) closesocket(SockErr);
    }

    /* ServerSocket is for WSL2. So, zero for WSL1. */
    if (ServerSocket == 0)
    {
        *WslVersion = WSL_VERSION_ONE;

        if (LxProcessHandle) CloseHandle(LxProcessHandle);
        if (ServerHandle) CloseHandle(ServerHandle);
    }

Cleanup:
    if (wslSession)
        wslSession->lpVtbl->Release(wslSession);
    CoUninitialize();
    return hRes;
}
