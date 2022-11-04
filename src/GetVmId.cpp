/* 
 * This file is part of wslbridge2 project
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2022 Biswapriyo Nath
 */

/*
 * GetVmId.cpp: Get GUID of WSL2 Utility VM with LxssUserSession COM interface.
 */

#include <winsock.h>
#include <winternl.h> /* TEB, PEB, ConsoleHandle */
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
    IWSLService *wslService;
} ComObj = { NULL };

static void LxssErrCode(HRESULT hRes)
{
    // Custom error code from LxssManager COM interface.
    if (hRes == (HRESULT)0x80040302)
        fatal("There is no distribution with the supplied name.\n");
}

void ComInit(bool *IsLiftedWSL)
{
    HRESULT hRes;

    hRes = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (hRes)
    {
        LOG_HRESULT_ERROR("CoInitializeEx", hRes);
    }

    hRes = CoInitializeSecurity(NULL, -1, NULL, NULL,
                                RPC_C_AUTHN_LEVEL_DEFAULT,
                                SecurityDelegation, NULL,
                                EOAC_STATIC_CLOAKING, NULL);
    if (hRes)
    {
        LOG_HRESULT_ERROR("CoInitializeSecurity", hRes);
    }

    // wsltty#302: First try with COM server in lifted WSL service
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
        if (hRes)
        {
            LOG_HRESULT_ERROR("CoCreateInstance", hRes);
        }

        *IsLiftedWSL = false;
    }
    else
        *IsLiftedWSL = true;
}

bool IsWslTwo(GUID *DistroId, const std::wstring DistroName, const bool IsLiftedWSL)
{
    HRESULT hRes;
    PWSTR DistributionName, BasePath;
    PSTR KernelCommandLine, *DefaultEnvironment;
    ULONG Version, DefaultUid, EnvironmentCount, Flags;
    EXECUTION_CONTEXT ExecutionContext;
    memset(&ExecutionContext, 0, sizeof ExecutionContext);

    const int WindowsBuild = GetWindowsBuild();

    if (IsLiftedWSL)
    {
        if (DistroName.empty())
            hRes = ComObj.wslService->lpVtbl->GetDefaultDistribution(
                ComObj.wslService, &ExecutionContext, DistroId);
        else
            hRes = ComObj.wslService->lpVtbl->GetDistributionId(
                ComObj.wslService, DistroName.c_str(), 0, &ExecutionContext, DistroId);

        LxssErrCode(hRes);

        hRes = ComObj.wslService->lpVtbl->GetDistributionConfiguration(
            ComObj.wslService,
            DistroId,
            &DistributionName,
            &Version,
            &DefaultUid,
            &EnvironmentCount,
            &DefaultEnvironment,
            &Flags,
            &ExecutionContext);

        if (hRes)
        {
            LOG_HRESULT_ERROR("GetDistributionConfiguration", hRes);
        }
    }
    else if (WindowsBuild == 17763)
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

        if (hRes)
        {
            LOG_HRESULT_ERROR("GetDistributionConfiguration", hRes);
        }

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

        if (hRes)
        {
            LOG_HRESULT_ERROR("GetDistributionConfiguration", hRes);
        }

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

        if (hRes)
        {
            LOG_HRESULT_ERROR("GetDistributionConfiguration", hRes);
        }
    }

    CoTaskMemFree(DistributionName);

    if (Flags > WSL_DISTRIBUTION_FLAGS_DEFAULT)
        return true;
    else
        return false;
}

HRESULT GetVmId(GUID *DistroId, GUID *LxInstanceID, const bool IsLiftedWSL)
{
    HRESULT hRes;
    GUID InitiatedDistroID;
    HANDLE LxProcessHandle, ServerHandle;
    SOCKET SockIn, SockOut, SockErr, ServerSocket;
    EXECUTION_CONTEXT ExecutionContext;
    memset(&ExecutionContext, 0, sizeof ExecutionContext);

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

    if (IsLiftedWSL)
    {
        hRes = ComObj.wslService->lpVtbl->CreateLxProcess(
            ComObj.wslService,
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
            &ServerSocket,
            &ExecutionContext);

        if (hRes)
        {
            LOG_HRESULT_ERROR("CreateLxProcess", hRes);
        }
    }
    else if (WindowsBuild < 20211) // Before Build 20211 Fe
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

        if (hRes)
        {
            LOG_HRESULT_ERROR("CreateLxProcess", hRes);
        }
    }
    else // After Build 20211 Fe
    {
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

        if (hRes)
        {
            LOG_HRESULT_ERROR("CreateLxProcess", hRes);
        }
    }

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
