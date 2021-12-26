/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2020-2021 Biswapriyo Nath.
 */

/*
 * LxssUserSession.hpp: Declaration of LxssUserSession COM interface.
 */

#ifndef LXSSUSERSESSION_H
#define LXSSUSERSESSION_H

// COM IDs for lifted WSL service
static const GUID CLSID_WslService = {
    0xF122531F,
    0x326B,
    0x4514,
    { 0x85, 0xAE, 0xDC, 0x99, 0xD3, 0x1D, 0x82, 0x56 } };

static const GUID IID_IWSLService = {
    0x50047071,
    0x122C,
    0x4CAD,
    { 0x9C, 0x93, 0x94, 0x72, 0x0E, 0xB7, 0x7B, 0x06 } };

// COM IDs for system WSL service
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

//
// Build 17763 RS5
//
typedef struct ILxssUserSessionOne ILxssUserSessionOne;

typedef struct ILxssUserSessionVtblOne {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ILxssUserSessionOne *This, GUID *riid, PVOID *ppv);
    ULONG(STDMETHODCALLTYPE *AddRef)(ILxssUserSessionOne *This);
    ULONG(STDMETHODCALLTYPE *Release)(ILxssUserSessionOne *This);

    PVOID CreateInstance;
    PVOID RegisterDistribution;

    HRESULT(STDMETHODCALLTYPE *GetDistributionId)(
        ILxssUserSessionOne *This,
        PCWSTR DistroName,
        ULONG EnableEnumerate,
        GUID *DistroId);

    PVOID TerminateDistribution;
    PVOID UnregisterDistribution;
    PVOID ConfigureDistribution;

    HRESULT (STDMETHODCALLTYPE *GetDistributionConfiguration)(
        ILxssUserSessionOne *This,
        GUID *DistroId,
        PWSTR *DistributionName,
        PULONG Version,
        PWSTR *BasePath,
        PSTR *KernelCommandLine,
        PULONG DefaultUid,
        PULONG EnvironmentCount,
        PSTR **DefaultEnvironment,
        PULONG Flags);

    HRESULT(STDMETHODCALLTYPE *GetDefaultDistribution)(
        ILxssUserSessionOne *This,
        GUID *DistroId);

    PVOID SetDefaultDistribution;
    PVOID EnumerateDistributions;
    PVOID CreateLxProcess;
    PVOID BeginUpgradeDistribution;
    PVOID FinishUpgradeDistribution;
} ILxssUserSessionVtblOne;

struct ILxssUserSessionOne {
    const ILxssUserSessionVtblOne *lpVtbl;
};

//
// Build 19041 20H1
//
typedef struct ILxssUserSessionTwo ILxssUserSessionTwo;

typedef struct ILxssUserSessionVtblTwo {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ILxssUserSessionTwo *This, GUID *riid, PVOID *ppv);
    ULONG(STDMETHODCALLTYPE *AddRef)(ILxssUserSessionTwo *This);
    ULONG(STDMETHODCALLTYPE *Release)(ILxssUserSessionTwo *This);

    PVOID CreateInstance;
    PVOID RegisterDistribution;
    PVOID RegisterDistributionPipe;

    HRESULT(STDMETHODCALLTYPE *GetDistributionId)(
        ILxssUserSessionTwo *This,
        PCWSTR DistroName,
        ULONG EnableEnumerate,
        GUID *DistroId);

    PVOID TerminateDistribution;
    PVOID UnregisterDistribution;
    PVOID ConfigureDistribution;

    HRESULT (STDMETHODCALLTYPE *GetDistributionConfiguration)(
        ILxssUserSessionTwo *This,
        GUID *DistroId,
        PWSTR *DistributionName,
        PULONG Version,
        PWSTR *BasePath,
        PSTR *KernelCommandLine,
        PULONG DefaultUid,
        PULONG EnvironmentCount,
        PSTR **DefaultEnvironment,
        PULONG Flags);

    HRESULT(STDMETHODCALLTYPE *GetDefaultDistribution)(
        ILxssUserSessionTwo *This,
        GUID *DistroId);

    PVOID SetDefaultDistribution;
    PVOID EnumerateDistributions;

    HRESULT (STDMETHODCALLTYPE *CreateLxProcess)(
    /*_In_*/ ILxssUserSessionTwo *This,
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
    /*_Out_*/ SOCKET *ServerSocket);

    PVOID SetVersion;
    PVOID RegisterLxBusServer;
    PVOID ExportDistribution;
    PVOID ExportDistributionPipe;
    PVOID Shutdown;
} ILxssUserSessionVtblTwo;

struct ILxssUserSessionTwo {
    const ILxssUserSessionVtblTwo *lpVtbl;
};

//
// Build 22000 21H2
//
typedef struct ILxssUserSessionThree ILxssUserSessionThree;

typedef struct ILxssUserSessionVtblThree {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(ILxssUserSessionThree *This, GUID *riid, PVOID *ppv);
    ULONG(STDMETHODCALLTYPE *AddRef)(ILxssUserSessionThree *This);
    ULONG(STDMETHODCALLTYPE *Release)(ILxssUserSessionThree *This);

    PVOID CreateInstance;
    PVOID RegisterDistribution;
    PVOID RegisterDistributionPipe;

    HRESULT(STDMETHODCALLTYPE *GetDistributionId)(
        ILxssUserSessionThree *This,
        PCWSTR DistroName,
        ULONG EnableEnumerate,
        GUID *DistroId);

    PVOID TerminateDistribution;
    PVOID UnregisterDistribution;
    PVOID ConfigureDistribution;

    // Build 21313 Co: Removed BasePath and KernelCommandLine.
    HRESULT (STDMETHODCALLTYPE *GetDistributionConfiguration)(
        ILxssUserSessionThree *wslSession,
        GUID *DistroId,
        PWSTR *DistributionName,
        PULONG Version,
        PULONG DefaultUid,
        PULONG EnvironmentCount,
        PSTR **DefaultEnvironment,
        PULONG Flags);

    HRESULT(STDMETHODCALLTYPE *GetDefaultDistribution)(
        ILxssUserSessionThree *This,
        GUID *DistroId);

    PVOID SetDefaultDistribution;
    PVOID EnumerateDistributions;

    // Build 20211 Fe: ULONG InstanceFlags is added.
    HRESULT (STDMETHODCALLTYPE *CreateLxProcess)(
    /*_In_*/ ILxssUserSessionThree *This,
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
    /*_In_*/ ULONG InstanceFlags,
    /*_Out_*/ GUID *InitiatedDistroId,
    /*_Out_*/ GUID *LxInstanceId,
    /*_Out_*/ PHANDLE LxProcessHandle,
    /*_Out_*/ PHANDLE ServerHandle,
    /*_Out_*/ SOCKET *InputSocket,
    /*_Out_*/ SOCKET *OutputSocket,
    /*_Out_*/ SOCKET *ErrorSocket,
    /*_Out_*/ SOCKET *ServerSocket);

    PVOID SetVersion;
    PVOID RegisterLxBusServer;
    PVOID ExportDistribution;
    PVOID ExportDistributionPipe;
    PVOID AttachPassThroughDisk;
    PVOID DetachPassThroughDisk;
    PVOID MountDisk;
    PVOID Shutdown;
    PVOID CreateVm;
} ILxssUserSessionVtblThree;

struct ILxssUserSessionThree {
    const ILxssUserSessionVtblThree *lpVtbl;
};

#endif /* LXSSUSERSESSION_H */
