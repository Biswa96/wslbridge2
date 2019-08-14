/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <windows.h>
#include <winsock.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <string>

#include "../hvsocket/hvsocket.h"
#include "../wslbridge/Helpers.hpp"
#include "../wslbridge/SocketIo.hpp"
#include "../wslbridge/TerminalState.hpp"

#define DEFAULT_INIT_PORT 54321

void GetVmId(GUID *LxInstanceID, wchar_t *DistroName);

/* return accepted client socket to receive */
static SOCKET Initialize(std::wstring command)
{
    int ret;

    SOCKET sServer = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    assert(sServer > 0);

    const int optval = 1;
    ret = setsockopt(sServer, HV_PROTOCOL_RAW, HVSOCKET_CONNECTED_SUSPEND,
                    (const char *)&optval, sizeof optval);
    assert(ret == 0);

    struct SOCKADDR_HV addr;
    memset(&addr, 0, sizeof addr);

    addr.Family = AF_HYPERV;
    GetVmId(&addr.VmId, nullptr);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = DEFAULT_INIT_PORT;
    ret = bind(sServer, (struct sockaddr *)&addr, sizeof addr);
    assert(ret == 0);

    ret = listen(sServer, -1);
    assert(ret == 0);

    PROCESS_INFORMATION pi = {};
    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;

#if defined (DEBUG) || defined (_DEBUG)
    si.wShowWindow = SW_SHOW;
#else
    si.wShowWindow = SW_HIDE;
#endif

    if (!command.empty())
    {
        ret = CreateProcessW(nullptr, &command[0], nullptr,
                             nullptr, false, CREATE_NEW_CONSOLE,
                             nullptr, nullptr, &si, &pi);
        if (!ret)
            fprintf(stderr, "backend start error: %d\n", GetLastError());
    }
    else
        fatal("error: backend command string is empty\n");

    SOCKET sClient = accept(sServer, nullptr, nullptr);
    assert(sClient > 0);

    closesocket(sServer);
    return sClient;
}

/* return socket and connect to random port number */
static SOCKET create_hvsock(unsigned int port)
{
    int ret;

    SOCKET sock = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    assert(sock > 0);

    const int optval = 1;
    ret = setsockopt(sock, HV_PROTOCOL_RAW, HVSOCKET_CONNECTED_SUSPEND,
                    (const char *)&optval, sizeof optval);
    assert(ret == 0);

    const int timeout = 10 * 1000;
    ret = setsockopt(sock, HV_PROTOCOL_RAW, HVSOCKET_CONNECT_TIMEOUT,
                    (const char *)&timeout, sizeof timeout);
    assert(ret == 0);

    struct SOCKADDR_HV addr;
    memset(&addr, 0, sizeof addr);

    addr.Family = AF_HYPERV;
    GetVmId(&addr.VmId, nullptr);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = port;
    ret = connect(sock, (struct sockaddr *)&addr, sizeof addr);
    assert(ret == 0);

    return sock;
}

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

static void resize_window(int signum)
{
    struct winsize winp;
    ioctl(0, TIOCGWINSZ, &winp);
    unsigned short buff[2] = { winp.ws_row, winp.ws_col };
    send(g_ioSockets.controlSock, (char *)buff, sizeof buff, 0);
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

    return NULL;
}

static void usage(const char *prog)
{
    printf("backend for hvpty using AF_VSOCK sockets\n"
           "Usage: %s [--] [options] [arguments]\n"
           "\n"
           "Options:\n"
           "  -b, --backend BACKEND\n"
           "                Overrides the default path of wslbridge2-backend to BACKEND\n"
           "  -C, --directory WINDIR\n"
           "                Changes the working directory to WINDIR first\n"
           "  -h, --help    Show this usage information\n"
           "  -d, --distribution Distribution Name\n"
           "                Run the specified distribution\n", prog);
    exit(0);
}

int main(int argc, char *argv[])
{
    int ret;

    struct WSAData wdata;
    ret = WSAStartup(MAKEWORD(2,2), &wdata);
    assert(ret == 0);

    const char shortopts[] = "+b:C:d:h";
    const struct option longopts[] = {
        { "backend",       required_argument, 0, 'b' },
        { "directory",     required_argument, 0, 'C' },
        { "distribution",  required_argument, 0, 'd' },
        { "help",          no_argument,       0, 'h' },
        { 0,               no_argument,       0,  0  },
    };

    std::string spawnCwd;
    std::string distroName;
    std::string customBackendPath;
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
                    fatal("error: the --backend option requires a non-empty string argument\n");
                break;

            case 'C':
                spawnCwd = optarg;
                if (spawnCwd.empty())
                    fatal("error: the -C option requires a non-empty string argument\n");
                break;

            case 'd':
                distroName = optarg;
                if (distroName.empty())
                    fatal("error: the --distro argument '%s' is invalid\n", optarg);
                break;

            case 'h':
                usage(argv[0]);
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

    /* Prepare the backend command line. */
    std::wstring wslCmdLine;
    wslCmdLine.append(L"\"$(wslpath -u");
    appendWslArg(wslCmdLine, backendPathWin);
    wslCmdLine.append(L")\"");

    {
        std::array<wchar_t, 1024> buffer;
        ret = swprintf(buffer.data(), buffer.size(),
                       L" --cols %d --rows %d --port %d",
                       initialSize.cols,
                       initialSize.rows,
                       DEFAULT_INIT_PORT);

        assert(ret > 0);
        wslCmdLine.append(buffer.data());
    }

    std::wstring cmdLine;
    cmdLine.append(L"\"");
    cmdLine.append(wslPath);
    cmdLine.append(L"\"");

    if (!distroName.empty())
    {
        cmdLine.append(L" -d ");
        cmdLine.append(mbsToWcs(distroName));
    }

   /* this option is taken from registry
    * HKCU\Directory\Background\shell\WSL\command
    */
    if (!spawnCwd.empty())
    {
        cmdLine.append(L" --cd ");
        cmdLine.append(mbsToWcs(spawnCwd));
    }

    cmdLine.append(L" bash -c ");
    appendWslArg(cmdLine, wslCmdLine);

#if defined (DEBUG) || defined (_DEBUG)
    wprintf(L"%ls\n", cmdLine.c_str());
#endif

    SOCKET sClient = Initialize(cmdLine);

    unsigned int port = 0;
    ret = recv(sClient, (char *)&port, sizeof port, 0);
    assert(ret > 0);

    for (int i = 0; i < 4; i++)
        g_ioSockets.sock[i] = create_hvsock(port);

    struct sigaction act;
    memset(&act, 0, sizeof act);
    act.sa_flags = SA_RESTART;
    act.sa_handler = resize_window;
    ret = sigaction(SIGWINCH, &act, nullptr);
    assert(ret == 0);

    pthread_t tid;
    ret = pthread_create(&tid, nullptr, send_buffer, nullptr);
    assert(ret == 0);

    TerminalState termState;
    termState.enterRawMode();

    char data[1024];

    while (1)
    {
        ret = recv(g_ioSockets.outputSock, data, sizeof data, 0);
        if (ret > 0)
            ret = write(STDOUT_FILENO, data, ret);
        else
            break;
    }

    /* cleanup */
    for (int i = 0; i < 4; i++)
        closesocket(g_ioSockets.sock[i]);
    closesocket(sClient);
    WSACleanup();
    termState.exitCleanly(0);
}
