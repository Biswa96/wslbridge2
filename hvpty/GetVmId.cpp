/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <winsock2.h>
#include <windows.h>
#include <assert.h>

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

typedef struct _LXSS_STD_HANDLE {
    ULONG Handle;
    ULONG Pipe;
} LXSS_STD_HANDLE, *PLXSS_STD_HANDLE;

typedef struct _LXSS_STD_HANDLES {
    LXSS_STD_HANDLE StdIn;
    LXSS_STD_HANDLE StdOut;
    LXSS_STD_HANDLE StdErr;
} LXSS_STD_HANDLES, *PLXSS_STD_HANDLES;

/* unused COM methods are ignored with void parameters */
class ILxssUserSession : public IUnknown
{
public:
    virtual HRESULT CreateInstance(void) = 0;
    virtual HRESULT RegisterDistribution(void) = 0;
    virtual HRESULT RegisterDistributionFromPipe(void) = 0;

    virtual HRESULT GetDistributionId(
        /*_In_*/ PCWSTR DistroName,
        /*_In_*/ ULONG EnableEnumerate,
        /*_Out_*/ GUID *DistroId) = 0;

    virtual HRESULT TerminateDistribution(void) = 0;
    virtual HRESULT UnregisterDistribution(void) = 0;
    virtual HRESULT ConfigureDistribution(void) = 0;
    virtual HRESULT GetDistributionConfiguration(void) = 0;

    virtual HRESULT GetDefaultDistribution(
        /*_Out_*/ GUID *DistroId) = 0;

    virtual HRESULT SetDefaultDistribution(void) = 0;
    virtual HRESULT EnumerateDistributions(void) = 0;

    virtual HRESULT CreateLxProcess(
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

    virtual HRESULT SetVersion(void) = 0;
    virtual HRESULT RegisterLxBusServer(void) = 0;
    virtual HRESULT ExportDistribution(void) = 0;
    virtual HRESULT ExportDistributionFromPipe(void) = 0;
    virtual HRESULT Shutdown(void) = 0;
};

/* from assembly source file */
extern "C"
{
    HANDLE GetConsoleHandle(void);
}

void GetVmId(GUID *LxInstanceID, PWSTR DistroName)
{
    int bRes;
    HRESULT hRes;

    WSADATA wsaData;
    bRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
    assert(bRes == 0);

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
    if (DistroName == nullptr)
        hRes = wslSession->GetDefaultDistribution(&DistroId);
    else
        hRes = wslSession->GetDistributionId(DistroName, 0, &DistroId);
    assert(hRes == 0);

    if (hRes == 0)
    {
       /* StdHandles member must be zero */
        LXSS_STD_HANDLES StdHandles = { 0 };
        GUID InitiatedDistroID;
        HANDLE LxProcessHandle, ServerHandle;
        SOCKET SockIn, SockOut, SockErr, ServerSocket;

        hRes = wslSession->CreateLxProcess(
            &DistroId,
            nullptr, 0, nullptr, nullptr, nullptr,
            nullptr, 0, nullptr, 0, 0,
            HandleToULong(GetConsoleHandle()),
            &StdHandles,
            &InitiatedDistroID,
            LxInstanceID,
            &LxProcessHandle,
            &ServerHandle,
            &SockIn,
            &SockOut,
            &SockErr,
            &ServerSocket);
        assert(hRes == 0);
    }

    /* cleanup */
    wslSession->Release();
    CoUninitialize();
    WSACleanup();
}
