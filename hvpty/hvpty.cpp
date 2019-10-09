/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <windows.h>
#include <assert.h>
#include <errno.h>
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

#include <string>
#include <thread>
#include <vector>

#include "HyperVSocket.hpp"
#include "../wslbridge/Helpers.hpp"
#include "../wslbridge/Environment.hpp"
#include "../wslbridge/SocketIo.hpp"
#include "../wslbridge/TerminalState.hpp"

/* Enable this to show debug information */
static const char IsDebugMode = 0;

void WINAPI GetIp(void);
HRESULT WINAPI GetVmId(
    GUID *LxInstanceID,
    const std::wstring &DistroName,
    int *WslVersion);

union IoSockets
{
    SOCKET sock[4];
    struct
    {
        SOCKET inputSock;
        SOCKET outputSock;
        SOCKET errorSock;
        SOCKET controlSock;
    };
};

/* global variable */
static union IoSockets g_ioSockets = { 0 };
static class HyperVSocket *g_hvSock = nullptr;

static void* resize_window(void *set)
{
    int ret, signum;
    struct winsize winp;

    while (1)
    {
        /* wait for the window resize signal aka. SIGWINCH */
        ret = sigwait((sigset_t *)set, &signum);
        if (ret != 0 || signum != SIGWINCH)
            break;

        /* send terminal window size to control socket */
        ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);
        g_hvSock->Send(g_ioSockets.controlSock, &winp, sizeof winp);

        if (IsDebugMode)
            printf("cols: %d row: %d\n", winp.ws_col,winp.ws_row);
    }

    pthread_exit(&ret);
    return nullptr;
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
                ret = g_hvSock->Send(g_ioSockets.inputSock, data, ret);
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
        ret = g_hvSock->Receive(g_ioSockets.outputSock, data, sizeof data);
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

static struct PipeHandles createPipe()
{
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    PipeHandles ret {};
    const BOOL success = CreatePipe(&ret.rh, &ret.wh, &sa, 0);
    assert(success && "CreatePipe failed");
    return ret;
}

static void usage(const char *prog)
{
    printf(
    "Run a program in WSL using AF_HYPERV sockets\n"
    "Usage: %s [--] [options] [arguments]\n"
    "\n"
    "Options:\n"
    "  -b, --backend BACKEND\n"
    "                    Overrides the default path of wslbridge2-backend to BACKEND\n"
    "  -d, --distribution Distribution Name\n"
    "                    Run the specified distribution\n"
    "  -e VAR            Copies VAR into the WSL environment\n"
    "  -e VAR=VAL        Sets VAR to VAL in the WSL environment\n"
    "  -h, --help        Show this usage information\n"
    "  -l, --login       Start a login shell\n"
    "  -u, --user WSL User Name\n"
    "                    Run as the specified user\n"
    "  -w, --windir Folder\n"
    "                    Changes the working directory to Windows style path\n"
    "  -W, --wsldir Folder\n"
    "                    Changes the working directory to Unix style path\n",
    prog);
    exit(0);
}

static void invalid_arg(const char *arg)
{
    fatal("error: the %s option requires a non-empty string argument\n", arg);
}

int main(int argc, char *argv[])
{
    /* Set WSL_HOST_IP environment variable */
    GetIp();
#ifdef __CYGWIN__
    cygwin_internal(CW_SYNC_WINENV);
#endif

    srand(time(nullptr));
    int ret;

    const char shortopts[] = "+b:d:e:hlu:w:W:";
    const struct option longopts[] = {
        { "backend",       required_argument, 0, 'b' },
        { "distribution",  required_argument, 0, 'd' },
        { "env",           required_argument, 0, 'e' },
        { "help",          no_argument,       0, 'h' },
        { "login",         no_argument,       0, 'l' },
        { "user",          required_argument, 0, 'u' },
        { "windir",        required_argument, 0, 'w' },
        { "wsldir",        required_argument, 0, 'W' },
        { 0,               no_argument,       0,  0  },
    };

    /* WinSock is initialized here */
    g_hvSock = new HyperVSocket();
    TerminalState termState;
    Environment env;
    std::string distroName;
    std::string customBackendPath;
    std::string userName;
    std::string winDir;
    std::string wslDir;
    bool loginMode = false;

    if (argv[0][0] == '-')
        loginMode = true;

    int c = 0;
    while ((c = getopt_long(argc, argv, shortopts, longopts, nullptr)) != -1)
    {
        switch (c)
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

            default:
                fatal("Try '%s --help' for more information.\n", argv[0]);
        }
    }

    const std::wstring wslPath = findSystemProgram(L"wsl.exe");
    const auto backendPathInfo = normalizePath(
                    findBackendProgram(customBackendPath, L"hvpty-backend"));
    const std::wstring backendPathWin = backendPathInfo.first;
    const std::wstring fsname = backendPathInfo.second;
    const struct TermSize initialSize = terminalSize();

    /* Prepare the backend command line */
    std::wstring wslCmdLine;
    wslCmdLine.append(L"\"$(wslpath -u");
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

    GUID VmId;
    int WslVersion;
    const HRESULT hRes = GetVmId(&VmId, mbsToWcs(distroName), &WslVersion);
    if (hRes != 0)
        fatal("GetVmId error: %s\n", formatErrorMessage(hRes).c_str());
    if (WslVersion != 2)
        fatal("This is for WSL2 distributions only\n");

    /* Create server to receive random port number */
    const SOCKET sServer = g_hvSock->Create();
    const int initPort = g_hvSock->Listen(sServer, &VmId);

    {
        std::array<wchar_t, 1024> buffer;
        ret = swprintf(
                buffer.data(),
                buffer.size(),
                L" --cols %d --rows %d --port %d",
                initialSize.cols,
                initialSize.rows,
                initPort);
        assert(ret > 0);
        wslCmdLine.append(buffer.data());
    }

    /* Append remaining non-option arguments as is */
    appendWslArg(wslCmdLine, L"--");
    for (int i = optind; i < argc; i++)
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

    cmdLine.append(L" /bin/sh -c ");
    appendWslArg(cmdLine, wslCmdLine);

    const struct PipeHandles outputPipe = createPipe();
    const struct PipeHandles errorPipe = createPipe();

    /* Initialize thread attribute list */
    HANDLE Values[2];
    Values[0] = outputPipe.wh;
    Values[1] = errorPipe.wh;

    SIZE_T AttrSize;
    LPPROC_THREAD_ATTRIBUTE_LIST AttrList = NULL;
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttrSize);
    AttrList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, AttrSize);
    InitializeProcThreadAttributeList(AttrList, 1, 0, &AttrSize);
    BOOL bRes = UpdateProcThreadAttribute(
                AttrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, /* 0x20002u */
                Values, sizeof Values, NULL, NULL);
    assert(bRes != 0);

    PROCESS_INFORMATION pi = {};
    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof si;
    si.lpAttributeList = AttrList;
    si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = outputPipe.wh;
    si.StartupInfo.hStdError = errorPipe.wh;

    ret = CreateProcessW(
            wslPath.c_str(),
            &cmdLine[0],
            NULL,
            NULL,
            TRUE,
            IsDebugMode ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si.StartupInfo,
            &pi);
    if (!ret)
    {
        fatal("error starting wsl.exe : %s\n",
              formatErrorMessage(GetLastError()).c_str());
    }

    HeapFree(GetProcessHeap(), 0, AttrList);
    CloseHandle(outputPipe.wh);
    CloseHandle(errorPipe.wh);
    ret = SetHandleInformation(pi.hProcess, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    assert(ret && "SetHandleInformation failed");

    const auto watchdog = std::thread([&]() {
        WaitForSingleObject(pi.hProcess, INFINITE);

        std::vector<char> outVec = readAllFromHandle(outputPipe.rh);
        std::vector<char> errVec = readAllFromHandle(errorPipe.rh);
        std::wstring outWide(outVec.size() / sizeof(wchar_t), L'\0');
        memcpy(&outWide[0], outVec.data(), outWide.size() * sizeof(wchar_t));
        std::string out { wcsToMbs(outWide, true) };
        std::string err { errVec.begin(), errVec.end() };
        out = stripTrailing(replaceAll(out, "Press any key to continue...", ""));
        err = stripTrailing(err);

        std::string msg;
        if (!out.empty()) {
            msg.append("note: wsl.exe output: ");
            msg.append(out);
            msg.push_back('\n');
        }
        if (!err.empty()) {
            msg.append("note: backend error output: ");
            msg.append(err);
            msg.push_back('\n');
        }

        termState.fatal("%s", msg.c_str());
    });

    const SOCKET sClient = g_hvSock->Accept(sServer);
    g_hvSock->Close(sServer);

    int randomPort = 0;
    ret = g_hvSock->Receive(sClient, &randomPort, sizeof randomPort);
    assert(ret > 0);
    g_hvSock->Close(sClient);

    if (IsDebugMode)
    {
        wprintf(L"cols: %d row: %d initPort: %d randomPort: %d\n",
                initialSize.cols, initialSize.rows, initPort, randomPort);
        wprintf(L"command: %ls\n", &cmdLine[0]);
    }

    /* Create four I/O sockets and connect with WSL server */
    for (int i = 0; i < 4; i++)
    {
        g_ioSockets.sock[i] = g_hvSock->Create();
        g_hvSock->Connect(g_ioSockets.sock[i], &VmId, randomPort);
    }

    /* Create thread to send window size through control socket */
    pthread_t tidResize;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGWINCH);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    ret = pthread_create(&tidResize, nullptr, resize_window, (void *)&set);
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

    pthread_join(tidResize, nullptr);
    pthread_join(tidInput, nullptr);
    pthread_join(tidOutput, nullptr);

    /* cleanup */
    for (int i = 0; i < 4; i++)
        g_hvSock->Close(g_ioSockets.sock[i]);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    termState.exitCleanly(0);
}
