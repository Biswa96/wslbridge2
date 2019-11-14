/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
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

#ifndef SOCKET
#define SOCKET size_t
#endif

static const GUID CLSID_LxssUserSession = {
    0x4F476546,
    0xB412,
    0x4579,
    { 0xB6, 0x4C, 0x12, 0x3D, 0xF3, 0x31, 0xE3, 0xD6 } };

static const GUID IID_ILxssUserSession = {
    0x536A6BCF,
    0xFE04,
    0x41D9,
    { 0xB9, 0x78, 0xDC, 0xAC, 0xA9, 0xA9, 0xB5, 0xB9 } };

typedef struct _LXSS_STD_HANDLE
{
    ULONG Handle;
    ULONG Pipe;
} LXSS_STD_HANDLE, *PLXSS_STD_HANDLE;

typedef struct _LXSS_STD_HANDLES
{
    LXSS_STD_HANDLE StdIn;
    LXSS_STD_HANDLE StdOut;
    LXSS_STD_HANDLE StdErr;
} LXSS_STD_HANDLES, *PLXSS_STD_HANDLES;

/* unused COM methods are ignored with void parameters */
class ILxssUserSession : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateInstance(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterDistribution(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterDistributionFromPipe(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDistributionId(
        /*_In_*/ PCWSTR DistroName,
        /*_In_*/ ULONG EnableEnumerate,
        /*_Out_*/ GUID *DistroId) = 0;

    virtual HRESULT STDMETHODCALLTYPE TerminateDistribution(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnregisterDistribution(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE ConfigureDistribution(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDistributionConfiguration(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE GetDefaultDistribution(
        /*_Out_*/ GUID *DistroId) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetDefaultDistribution(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE EnumerateDistributions(void) = 0;

    virtual HRESULT STDMETHODCALLTYPE CreateLxProcess(
        /*_In_opt_*/ GUID *DistroId,
        /*_In_opt_*/ PCSTR CommandLine,
        /*_In_opt_*/ ULONG ArgumentCount,
        /*_In_opt_*/ PCSTR *Arguments,
        /*_In_opt_*/ PCWSTR CurrentDirectory,
        /*_In_opt_*/ PCWSTR SharedEnvironment,
        /*_In_opt_*/ PCWSTR ProcessEnvironment,
        /*_In_opt_*/ SIZE_T EnvironmentLength,
        /*_In_opt_*/ PCWSTR LinuxUserName,
        /*_In_opt_*/ USHORT WindowWidthX,
        /*_In_opt_*/ USHORT WindowHeightY,
        /*_In_*/ ULONG ConsoleHandle,
        /*_In_*/ PLXSS_STD_HANDLES StdHandles,
        /*_Out_*/ GUID *InitiatedDistroId,
        /*_Out_*/ GUID *LxInstanceId,
        /*_Out_*/ PHANDLE LxProcessHandle,
        /*_Out_*/ PHANDLE ServerHandle,
        /*_Out_*/ SOCKET *InputSocket,
        /*_Out_*/ SOCKET *OutputSocket,
        /*_Out_*/ SOCKET *ErrorSocket,
        /*_Out_*/ SOCKET *ControlSocket) = 0;

    virtual HRESULT STDMETHODCALLTYPE SetVersion(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE RegisterLxBusServer(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExportDistribution(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE ExportDistributionFromPipe(void) = 0;
    virtual HRESULT STDMETHODCALLTYPE Shutdown(void) = 0;
};

HRESULT GetVmId(
    GUID *LxInstanceID,
    const std::wstring &DistroName,
    int *WslVersion)
{
    HRESULT hRes;

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

    GUID DistroId;
    if (DistroName.empty())
        hRes = wslSession->GetDefaultDistribution(&DistroId);
    else
        hRes = wslSession->GetDistributionId(DistroName.c_str(), 0, &DistroId);

    if (hRes == 0)
    {
       /* StdHandles member must be zero */
        LXSS_STD_HANDLES StdHandles = { 0 };
        GUID InitiatedDistroID;
        HANDLE LxProcessHandle, ServerHandle;
        SOCKET SockIn, SockOut, SockErr, ServerSocket;

        const HANDLE ConsoleHandle = NtCurrentTeb()->
                                    ProcessEnvironmentBlock->
                                    ProcessParameters->
                                    Reserved2[0];

        hRes = wslSession->CreateLxProcess(
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

        /* ServerHandle and ServerSocket are exclusive */
        if (ServerHandle == nullptr && ServerSocket != 0)
            *WslVersion = WSL_VERSION_TWO;
        else
            *WslVersion = WSL_VERSION_ONE;
    }

Cleanup:
    if (wslSession)
        wslSession->Release();
    CoUninitialize();
    return hRes;
}
