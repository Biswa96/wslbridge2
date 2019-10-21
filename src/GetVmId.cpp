/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <winsock2.h>
#include <windows.h>
#include <winternl.h>
#include <assert.h>
#include <string>

struct RTL_USER_PROCESS_PARAMETERS_mod
{
    ULONG MaximumLength;
    ULONG Length;
    ULONG Flags;
    ULONG DebugFlags;
    HANDLE ConsoleHandle;
    /* Removed unused members */
};

struct PEB_mod
{
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[1];
    PVOID Reserved3[2];
    PPEB_LDR_DATA Ldr;
    struct RTL_USER_PROCESS_PARAMETERS_mod *ProcessParameters;
    /* Removed unused members */
};

struct TEB_mod
{
    PVOID Reserved1[12];
    struct PEB_mod *ProcessEnvironmentBlock;
    /* Removed unused members */
};

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
        HANDLE ConsoleHandle, LxProcessHandle, ServerHandle;
        SOCKET SockIn, SockOut, SockErr, ServerSocket;

        /* Black magic due to absence of appropriate header */
        auto teb = (struct TEB_mod *)NtCurrentTeb();
        ConsoleHandle = teb->ProcessEnvironmentBlock->
                        ProcessParameters->ConsoleHandle;

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

        if (ServerHandle == nullptr && ServerSocket != 0)
            *WslVersion = 2;
        else
            *WslVersion = 1;
    }

Cleanup:
    if (wslSession)
        wslSession->Release();
    CoUninitialize();
    return hRes;
}
