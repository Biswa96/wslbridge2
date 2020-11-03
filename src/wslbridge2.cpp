/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2020 Biswapriyo Nath.
 */

/* DO NOT include winsock.h, conflicts with poll.h. */
#include <windows.h>
#include <assert.h>
#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <string>
#include <thread>
#include <vector>

#include "common.hpp"
#include "GetVmId.hpp"
#include "Helpers.hpp"
#include "Environment.hpp"
#include "TerminalState.hpp"
#include "WindowsSock.hpp"

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

extern "C" {
WINBASEAPI int WINAPI closesocket(SOCKET s);
WINBASEAPI int WINAPI recv(SOCKET s, void *buf, int len, int flags);
WINBASEAPI int WINAPI send(SOCKET s, void *buf, int len, int flags);
WINBASEAPI int WINAPI WSACleanup(void);
}

union IoSockets
{
    SOCKET sock[3];
    struct
    {
        SOCKET inputSock;
        SOCKET outputSock;
        SOCKET controlSock;
    };
};

/* global variable */
static volatile union IoSockets g_ioSockets = { 0, 0, 0 };

static void resize_window(int signum)
{
    struct winsize winp;

    /* Send terminal window size to control socket */
    ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);
    send(g_ioSockets.controlSock, &winp, sizeof winp, 0);
}

static void* send_buffer(void *param)
{
    int ret;
    char data[1024];

    struct pollfd fds = { STDIN_FILENO, POLLIN, 0 };

    while (1)
    {
        ret = poll(&fds, 1, -1);

        if (fds.revents & POLLIN)
        {
            ret = read(STDIN_FILENO, data, sizeof data);
            if (ret > 0)
                ret = send(g_ioSockets.inputSock, data, ret, 0);
            else
                break;
        }
    }

    pthread_exit(&ret);
    return nullptr;
}

static void* receive_buffer(void *param)
{
    int ret;
    char data[1024];

    while (1)
    {
        ret = recv(g_ioSockets.outputSock, data, sizeof data, 0);
        if (ret > 0)
            ret = write(STDOUT_FILENO, data, ret);
        else
            break;
    }

    pthread_exit(&ret);
    return nullptr;
}

struct PipeHandles {
    HANDLE rh;
    HANDLE wh;
};

static struct PipeHandles createPipe(void)
{
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    PipeHandles ret = {};
    const BOOL success = CreatePipe(&ret.rh, &ret.wh, &sa, 0);
    assert(success && "CreatePipe failed");
    return ret;
}

static void usage(const char *prog)
{
    printf(
    "Usage: %s [options] [--] [command]...\n"
    "Runs a program within a Windows Subsystem for Linux (WSL) pty\n"
    "\n"
    "Options:\n"
    "  -b, --backend BACKEND\n"
    "                Overrides the default path of wslbridge2-backend to BACKEND\n"
    "  -d, --distribution Distribution Name\n"
    "                Run the specified distribution.\n"
    "  -e VAR        Copies VAR into the WSL environment.\n"
    "  -e VAR=VAL    Sets VAR to VAL in the WSL environment.\n"
    "  -h, --help    Show this usage information\n"
    "  -l, --login   Start a login shell.\n"
    "  -u, --user    WSL User Name\n"
    "                Run as the specified user\n"
    "  -w, --windir  Folder\n"
    "                Changes the working directory to Windows style path\n"
    "  -W, --wsldir  Folder\n"
    "                Changes the working directory to Unix style path\n"
    "  -x, --xmod    Shows hidden backend window and debug output.\n"
    , prog);
    exit(0);
}

static void invalid_arg(const char *arg)
{
    fatal("error: the %s option requires a non-empty string argument\n", arg);
}

int main(int argc, char *argv[])
{
    /* Minimum requirement Windows 10 build 17763 aka. version 1809 */
    if (GetWindowsBuild() < 17763)
        fatal("Windows 10 version is older than minimal requirement.\n");

#ifdef __CYGWIN__
    cygwin_internal(CW_SYNC_WINENV);
#endif

    /*
     * Set time as seed for generation of random port.
     * wslbridge2 #24 and #27: Add aditional entropy
     * to randomize port even in a split of seconds.
     */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long seed = tv.tv_usec << 16 | (getpid() & 0xFFFF);
    srand(seed);

    int ret;
    const char shortopts[] = "+b:d:e:hlu:w:W:V:x";
    const struct option longopts[] = {
        { "backend",       required_argument, 0, 'b' },
        { "distribution",  required_argument, 0, 'd' },
        { "env",           required_argument, 0, 'e' },
        { "help",          no_argument,       0, 'h' },
        { "login",         no_argument,       0, 'l' },
        { "user",          required_argument, 0, 'u' },
        { "windir",        required_argument, 0, 'w' },
        { "wsldir",        required_argument, 0, 'W' },
        { "wslver",        required_argument, 0, 'V' },
        { "xmod",          no_argument,       0, 'x' },
        { 0,               no_argument,       0,  0  },
    };

    class Environment env;
    class TerminalState termState;
    std::string distroName;
    std::string customBackendPath;
    std::string winDir;
    std::string wslDir;
    std::string userName;
    bool debugMode = false;
    bool loginMode = false;

    if (argv[0][0] == '-')
        loginMode = true;

    int ch = 0;
    while ((ch = getopt_long(argc, argv, shortopts, longopts, nullptr)) != -1)
    {
        switch (ch)
        {
            case 0:
                /* Ignore long option. */
                break;

            case 'b':
                customBackendPath = optarg;
                if (customBackendPath.empty())
                    invalid_arg("backend");
                break;

            case 'd':
                distroName = optarg;
                if (distroName.empty())
                    invalid_arg("distribution");
                break;

            case 'e':
            {
                const char *eq = strchr(optarg, '=');
                const std::string varname = eq ?
                                            std::string(optarg, eq - optarg)
                                            : std::string(optarg);
                if (varname.empty())
                    invalid_arg("environment");

                if (eq)
                    env.set(varname, eq + 1);
                else
                    env.set(varname);
                break;
            }

            case 'h':
                usage(argv[0]);
                break;


            case 'l':
                loginMode = true;
                break;

            case 'u':
                userName = optarg;
                if (userName.empty())
                    invalid_arg("user");
                break;

            case 'w':
                winDir = optarg;
                if (winDir.empty())
                    invalid_arg("windir");
                break;

            case 'W':
                wslDir = optarg;
                if (wslDir.empty())
                    invalid_arg("wsldir");
                break;

            case 'V':
                /* empty */
                break;

            case 'x':
                debugMode = true;
                break;

            default:
                fatal("Try '%s --help' for more information.\n", argv[0]);
        }
    }

    const std::wstring wslPath = findSystemProgram(L"wsl.exe");
    const std::wstring backendPathWin = normalizePath(
                findBackendProgram(customBackendPath, L"wslbridge2-backend"));

    /* Prepare the backend command line. */
    std::wstring wslCmdLine;
    wslCmdLine.append(L"exec \"$(wslpath -u");
    appendWslArg(wslCmdLine, backendPathWin);
    wslCmdLine.append(L")\"");

    for (const auto &envPair : env.pairs())
    {
        appendWslArg(wslCmdLine, L"--env");
        appendWslArg(wslCmdLine, envPair.first + L"=" + envPair.second);
    }

    if (loginMode)
        appendWslArg(wslCmdLine, L"--login");

    if (!wslDir.empty())
    {
        wslCmdLine.append(L" --path \"");
        wslCmdLine.append(mbsToWcs(wslDir));
        wslCmdLine.append(L"\"");
    }

    /* Initialize WinSock. */
    WindowsSock();

    /* Intialize COM. */
    ComInit();

    GUID DistroId, VmId;
    SOCKET serverSock = 0;
    SOCKET inputSocket = 0;
    SOCKET outputSocket = 0;
    SOCKET controlSocket = 0;

    /* Detect WSL version. Assume distroName is initialized empty. */
    const bool wslTwo = IsWslTwo(&DistroId, mbsToWcs(distroName));

    if (wslTwo) /* WSL2: Use Hyper-V sockets. */
    {
        const HRESULT hRes = GetVmId(&DistroId, &VmId);
        if (hRes != 0)
            fatal("GetVmId: %s\n", GetErrorMessage(hRes).c_str());

        /* Create server to receive random port number */
        serverSock = CreateHvSock();

        struct winsize winp = {};
        ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);

        std::array<wchar_t, 1024> buffer;
        ret = swprintf(
                buffer.data(),
                buffer.size(),
                L" %ls--cols %d --rows %d --port %d",
                debugMode ? L"--xmod " : L"",
                winp.ws_col,
                winp.ws_row,
                ListenHvSock(serverSock, &VmId));
        assert(ret > 0);
        wslCmdLine.append(buffer.data());
    }
    else /* WSL1: use localhost IPv4 sockets. */
    {
        inputSocket = CreateLocSock();
        outputSocket = CreateLocSock();
        controlSocket = CreateLocSock();

        struct winsize winp = {};
        ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);

        std::array<wchar_t, 1024> buffer;
        ret = swprintf(
                buffer.data(),
                buffer.size(),
                L" %ls--cols %d --rows %d -0%d -1%d -3%d",
                debugMode ? L"--xmod " : L"",
                winp.ws_col,
                winp.ws_row,
                ListenLocSock(inputSocket),
                ListenLocSock(outputSocket),
                ListenLocSock(controlSocket));
        assert(ret > 0);
        wslCmdLine.append(buffer.data());
    }

    /* Append remaining non-option arguments as is */
    appendWslArg(wslCmdLine, L"--");
    for (int i = optind; i < argc; ++i)
        appendWslArg(wslCmdLine, mbsToWcs(argv[i]));

    /* Append wsl.exe options and its arguments */
    std::wstring cmdLine;
    cmdLine.append(L"\"");
    cmdLine.append(wslPath);
    cmdLine.append(L"\"");

    if (!distroName.empty())
    {
        cmdLine.append(L" -d ");
        cmdLine.append(mbsToWcs(distroName));
    }

   /* Taken from HKCU\Directory\Background\shell\WSL\command */
    if (!winDir.empty())
    {
        cmdLine.append(L" --cd \"");
        cmdLine.append(mbsToWcs(winDir));
        cmdLine.append(L"\"");
    }

    if (!userName.empty())
    {
        cmdLine.append(L" --user ");
        cmdLine.append(mbsToWcs(userName));
    }

    cmdLine.append(L" /bin/sh -c");
    appendWslArg(cmdLine, wslCmdLine);

    if (debugMode)
        wprintf(L"Backend CommandLine: %ls\n", &cmdLine[0]);

    const struct PipeHandles outputPipe = createPipe();
    const struct PipeHandles errorPipe = createPipe();

    /* Initialize thread attribute list to inherit pipe handles. */
    HANDLE Values[2] = { outputPipe.wh, errorPipe.wh };
    SIZE_T AttrSize;
    LPPROC_THREAD_ATTRIBUTE_LIST AttrList = NULL;
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttrSize);
    AttrList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, AttrSize);
    InitializeProcThreadAttributeList(AttrList, 1, 0, &AttrSize);
    ret = UpdateProcThreadAttribute(
            AttrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, /* 0x20002u */
            Values, sizeof Values, NULL, NULL);
    if (!ret)
        fatal("UpdateProcThreadAttribute: %s", GetErrorMessage(GetLastError()).c_str());

    DWORD CreationFlags = EXTENDED_STARTUPINFO_PRESENT;
    PROCESS_INFORMATION pi = {};
    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof si;

    /* DO NOT use pipe handles to redirect output from debug window. */
    if (debugMode)
    {
        CreationFlags |= CREATE_NEW_CONSOLE;
    }
    else
    {
        CreationFlags |= CREATE_NO_WINDOW;
        si.lpAttributeList = AttrList;
        si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        si.StartupInfo.hStdOutput = outputPipe.wh;
        si.StartupInfo.hStdError = errorPipe.wh;
    }

    ret = CreateProcessW(
            wslPath.c_str(),
            &cmdLine[0],
            NULL,
            NULL,
            TRUE,
            CreationFlags,
            NULL,
            NULL,
            &si.StartupInfo,
            &pi);
    if (!ret)
        fatal("CreateProcessW: %s", GetErrorMessage(GetLastError()).c_str());

    HeapFree(GetProcessHeap(), 0, AttrList);
    CloseHandle(outputPipe.wh);
    CloseHandle(errorPipe.wh);
    ret = SetHandleInformation(pi.hProcess, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (!ret)
        fatal("SetHandleInformation: %s", GetErrorMessage(GetLastError()).c_str());

    const auto watchdog = std::thread([&]() {
        WaitForSingleObject(pi.hProcess, INFINITE);

        std::vector<char> outVec = readAllFromHandle(outputPipe.rh);
        std::vector<char> errVec = readAllFromHandle(errorPipe.rh);
        std::wstring outWide(outVec.size() / sizeof(wchar_t), L'\0');
        memcpy(&outWide[0], outVec.data(), outWide.size() * sizeof(wchar_t));
        std::string out { wcsToMbs(outWide, true) };
        std::string err { errVec.begin(), errVec.end() };

        std::string msg;
        if (!out.empty()) {
            msg.append("note: wsl.exe output: ");
            msg.append(out);
        }
        if (!err.empty()) {
            msg.append("note: backend error output: ");
            msg.append(err);
        }

        /* Exit with error if output/error message is not empty. */
        if (!msg.empty())
            termState.fatal("%s", msg.c_str());
    });

    if (wslTwo)
    {
        const SOCKET sClient = AcceptHvSock(serverSock);

        int randomPort = 0;
        ret = recv(sClient, &randomPort, sizeof randomPort, 0);
        assert(ret > 0);
        closesocket(sClient);

        /* Create I/O sockets and connect with WSL server */
        for (size_t i = 0; i < ARRAYSIZE(g_ioSockets.sock); i++)
        {
            g_ioSockets.sock[i] = CreateHvSock();
            ConnectHvSock(g_ioSockets.sock[i], &VmId, randomPort);
        }
    }
    else
    {
        g_ioSockets.inputSock = AcceptLocSock(inputSocket);
        g_ioSockets.outputSock = AcceptLocSock(outputSocket);
        g_ioSockets.controlSock = AcceptLocSock(controlSocket);
    }

    /* Capture window resize signal and send buffer to control socket */
    struct sigaction act = {};
    act.sa_handler = resize_window;
    act.sa_flags = SA_RESTART;
    ret = sigaction(SIGWINCH, &act, NULL);
    assert(ret == 0);

    /* Create thread to send input buffer to input socket */
    pthread_t tidInput;
    ret = pthread_create(&tidInput, nullptr, send_buffer, nullptr);
    assert(ret == 0);

    /* Create thread to send input buffer to input socket */
    pthread_t tidOutput;
    ret = pthread_create(&tidOutput, nullptr, receive_buffer, nullptr);
    assert(ret == 0);

    termState.enterRawMode();

    /*
     * wsltty#254: WORKAROUND: Terminates input thread forcefully
     * when output thread exits. Need some inter-thread syncing.
     */
    pthread_join(tidOutput, nullptr);
    pthread_kill(tidInput, 0);
    // pthread_join(tidInput, nullptr);

    /* cleanup */
    for (size_t i = 0; i < ARRAYSIZE(g_ioSockets.sock); i++)
        closesocket(g_ioSockets.sock[i]);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    WSACleanup();
    termState.exitCleanly(0);
}
