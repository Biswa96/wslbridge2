/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2022 Biswapriyo Nath.
 */

#include <winsock2.h>
#include <windows.h>
#include <assert.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/cygwin.h>
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
#include "windows-sock.h"
#include "GetVmIdWsl2.hpp"

union IoSockets
{
    SOCKET sock[4];
    struct
    {
        SOCKET xserverSock;
        SOCKET inputSock;
        SOCKET outputSock;
        SOCKET controlSock;
    };
};

/* global variable */
static volatile union IoSockets g_ioSockets = { 0 };

#define dont_debug_inband
#define dont_use_controlsocket

static void resize_window(int signum)
{
#ifdef use_controlsocket
#warning this may crash for unknown reason, maybe terminate the backend
    struct winsize winp;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);

    /* Send terminal window size to control socket */
    send(g_ioSockets.controlSock, (char *)&winp, sizeof winp, 0);
#else
    static char wins[2 + sizeof(struct winsize)] = {0, 16};
    static struct winsize * winsp = (struct winsize *)&wins[2];
    ioctl(STDIN_FILENO, TIOCGWINSZ, winsp);

#ifdef debug_inband
    /* Send terminal window size inband, visualized as ESC sequence */
    char resizesc[55];
    //sprintf(resizesc, "\e_8;%u;%u\a", winsp->ws_row, winsp->ws_col);
    sprintf(resizesc, "^[_8;%u;%u^G", winsp->ws_row, winsp->ws_col);
    send(g_ioSockets.inputSock, resizesc, strlen(resizesc), 0);
#else
    /* Send terminal window size inband, with NUL escape */
    send(g_ioSockets.inputSock, wins, sizeof wins, 0);
#endif
#endif
}

static void* send_buffer(void *param)
{
    int ret;
    char data[1024];
    assert(sizeof data <= PIPE_BUF);

    while (1)
    {
        ret = read(STDIN_FILENO, data, sizeof data);
        if (ret < 0)
        {
            closesocket(g_ioSockets.inputSock);
            break;
        }
            char * s = data;
            int len = ret;
            while (ret > 0 && len > 0)
            {
                if (!*s)
                {
                    // send NUL STX
#ifdef debug_inband
                    ret = send(g_ioSockets.inputSock, (void*)"nul", 3, 0);
#else
                    static char NUL_STX[] = {0, 2};
                    ret = send(g_ioSockets.inputSock, NUL_STX, 2, 0);
#endif
                    s++;
                    len--;
                }
                else
                {
                    int n = strnlen(s, len);
                    ret = send(g_ioSockets.inputSock, s, n, 0);
                    if (ret > 0)
                    {
                        s += ret;
                        len -= ret;
                    }
                }
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
        if (ret <= 0)
            break;

        if(!write(STDOUT_FILENO, data, ret))
        {
            shutdown(g_ioSockets.outputSock, SD_BOTH);
            break;
        }
    }

    pthread_exit(&ret);
    return nullptr;
}

struct PipeHandles { HANDLE rh, wh; };

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
    printf("\nwslbridge2 %s : Runs a program within a Windows Subsystem for Linux (WSL) pty.\n",
        STRINGIFY(WSLBRIDGE2_VERSION));
    printf("Copyright (C) 2019-2022 Biswapriyo Nath.\n");
    printf("Licensed under GNU General Public License version 3 or later.\n");
    printf("\n");
    printf("Usage: %s [options] [--] [command]...\n", prog);
    printf("Options:\n");
    printf("  -b, --backend BACKEND\n");
    printf("                Overrides the default path of wslbridge2-backend to BACKEND.\n");
    printf("  -d, --distribution Distribution Name\n");
    printf("                Run the specified distribution.\n");
    printf("  -e VAR        Copies VAR into the WSL environment.\n");
    printf("  -e VAR=VAL    Sets VAR to VAL in the WSL environment.\n");
    printf("  -h, --help    Show this usage information.\n");
    printf("  -l, --login   Start a login shell.\n");
    printf("  -s, --show    Shows hidden backend window and debug output.\n");
    printf("  -u, --user    WSL User Name\n");
    printf("                Run as the specified user.\n");
    printf("  -w, --windir  Folder\n");
    printf("                Changes the working directory to Windows style path.\n");
    printf("  -W, --wsldir  Folder\n");
    printf("                Changes the working directory to Unix style path.\n");

    exit(0);
}

static void invalid_arg(const char *arg)
{
    fatal("error: the %s option requires a non-empty string argument\n", arg);
}

static void start_dummy(std::wstring wslPath, std::wstring wslCmdLine,
    std::string distroName, const bool debugMode)
{
    std::wstring cmdLine;
    cmdLine.append(L"\"");
    cmdLine.append(wslPath);
    cmdLine.append(L"\"");

    if (!distroName.empty())
    {
        cmdLine.append(L" -d ");
        cmdLine.append(mbsToWcs(distroName));
    }

    cmdLine.append(L" /bin/sh -c");
    appendWslArg(wslCmdLine, L"-x");
    appendWslArg(cmdLine, wslCmdLine);

    if (debugMode)
        wprintf(L"Backend CommandLine: %ls\n", &cmdLine[0]);

    PROCESS_INFORMATION pi = {};
    STARTUPINFOW si = {};
    si.cb = sizeof si;

    if (CreateProcessW(wslPath.c_str(), &cmdLine[0],
        NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi) == FALSE)
    {
        LOG_WIN32_ERROR("CreateProcessW");
    }

    if (WaitForSingleObject(pi.hProcess, INFINITE))
    {
        LOG_WIN32_ERROR("WaitForSingleObject");
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

int main(int argc, char *argv[])
{
    /* Minimum requirement Windows 10 build 17763 aka. version 1809 */
    if (GetWindowsBuild() < 17763)
        fatal("Windows 10 version is older than minimal requirement.\n");

    /* wsltty#273: Make portable to all locales. */
    setlocale(LC_ALL, "");
    cygwin_internal(CW_SYNC_WINENV);

    /*
     * Set time as seed for generation of random port.
     * wslbridge2 #24 and #27: Add additional entropy
     * to randomize port even in a split of seconds.
     */
    {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long seed = tv.tv_usec << 16 | (getpid() & 0xFFFF);
    srand(seed);
    }

    int ret;
    const char shortopts[] = "+b:d:e:hlsu:V:w:W:";
    const struct option longopts[] = {
        { "backend",       required_argument, 0, 'b' },
        { "distribution",  required_argument, 0, 'd' },
        { "env",           required_argument, 0, 'e' },
        { "help",          no_argument,       0, 'h' },
        { "login",         no_argument,       0, 'l' },
        { "show",          required_argument, 0, 's' },
        { "user",          required_argument, 0, 'u' },
        { "wslver",        required_argument, 0, 'V' },
        { "windir",        required_argument, 0, 'w' },
        { "wsldir",        required_argument, 0, 'W' },
        { 0,               no_argument,       0,  0  },
    };

    class Environment env;
    class TerminalState termState;
    std::string distroName, customBackendPath;
    std::string winDir, wslDir, userName;
    volatile bool debugMode = false, loginMode = false;

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

            case 'h': usage(argv[0]); break;
            case 'l': loginMode = true; break;
            case 's': debugMode = true; break;

            case 'u':
                userName = optarg;
                if (userName.empty())
                    invalid_arg("user");
                break;

            case 'V': break; /* empty */

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
    win_sock_init();

    /* Initialize COM. */
    int LiftedWSLVersion = 0;
    ComInit(&LiftedWSLVersion);

    GUID DistroId, VmId;
    SOCKET inputSock = 0, outputSock = 0, controlSock = 0;

    /* Detect WSL version. Assume distroName is initialized empty. */
    const bool wslTwo = IsWslTwo(&DistroId, mbsToWcs(distroName), LiftedWSLVersion);

    if (wslTwo) /* WSL2: Use Hyper-V sockets. */
    {
        // wsltty#302: Start dummy process after ComInit, otherwise RPC_E_TOO_LATE.
        // wslbridge2#38: Do this only for WSL2 as WSL1 does not need the VM context.
        // wslbridge2#42: Required for WSL2 to get the VM ID.
        if (LiftedWSLVersion)
            start_dummy(wslPath, wslCmdLine, distroName, debugMode);

        if (!GetVmIdWsl2(VmId))
            fatal("Failed to get VM ID");

        inputSock = win_vsock_create();
        outputSock = win_vsock_create();
        controlSock = win_vsock_create();

        struct winsize winp = {};
        ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);

        std::array<wchar_t, 1024> buffer;
        ret = swprintf(
                buffer.data(),
                buffer.size(),
                L" %ls--cols %d --rows %d -0%d -1%d -3%d",
                debugMode ? L"--show " : L"",
                winp.ws_col,
                winp.ws_row,
                win_vsock_listen(inputSock, &VmId),
                win_vsock_listen(outputSock, &VmId),
                win_vsock_listen(controlSock, &VmId));
        assert(ret > 0);
        wslCmdLine.append(buffer.data());
    }
    else /* WSL1: use localhost IPv4 sockets. */
    {
        inputSock = win_local_create();
        outputSock = win_local_create();
        controlSock = win_local_create();

        struct winsize winp = {};
        ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);

        std::array<wchar_t, 1024> buffer;
        ret = swprintf(
                buffer.data(),
                buffer.size(),
                L" %ls--cols %d --rows %d -0%d -1%d -3%d",
                debugMode ? L"--show " : L"",
                winp.ws_col,
                winp.ws_row,
                win_local_listen(inputSock, 0),
                win_local_listen(outputSock, 0),
                win_local_listen(controlSock, 0));
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

    std::thread watchdog([&]()
    {
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
        g_ioSockets.inputSock = win_vsock_accept(inputSock);
        g_ioSockets.outputSock = win_vsock_accept(outputSock);
        g_ioSockets.controlSock = win_vsock_accept(controlSock);
    }
    else
    {
        g_ioSockets.inputSock = win_local_accept(inputSock);
        g_ioSockets.outputSock = win_local_accept(outputSock);
        g_ioSockets.controlSock = win_local_accept(controlSock);
    }

    /* Create thread to send input buffer to input socket */
    pthread_t tidInput;
    ret = pthread_create(&tidInput, nullptr, send_buffer, nullptr);
    assert(ret == 0);

    /* Create thread to send input buffer to input socket */
    pthread_t tidOutput;
    ret = pthread_create(&tidOutput, nullptr, receive_buffer, nullptr);
    assert(ret == 0);

    termState.enterRawMode();

    /* Create thread to send window size through control socket */
    struct sigaction act = {};
    act.sa_handler = resize_window;
    act.sa_flags = SA_RESTART;
    ret = sigaction(SIGWINCH, &act, NULL);
    assert(ret == 0);

    /* Notify initial size in case it's changed since starting */
    //resize_window(0);
    kill(getpid(), SIGWINCH);

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
