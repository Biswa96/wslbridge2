/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <windows.h>
#include <winsock2.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "../hvsocket/hvsocket.h"
#include "../wslbridge/TerminalState.hpp"

#define PORT_NUM 54321
#define BUFF_SIZE 400

void GetVmId(GUID *LxInstanceID, wchar_t *DistroName);

/* return accepted client socket to receive */
SOCKET Initialize(void)
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
    addr.ServiceId.Data1 = PORT_NUM;
    ret = bind(sServer, (struct sockaddr *)&addr, sizeof addr);
    assert(ret == 0);

    ret = listen(sServer, -1);
    assert(ret == 0);

    SOCKET sClient = accept(sServer, nullptr, nullptr);
    assert(sClient > 0);

    closesocket(sServer);
    return sClient;
}

/* return socket and connect to random port number */
SOCKET create_hvsock(unsigned int port)
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
union IoSockets g_ioSockets = { 0 };

void resize_window(int signum)
{
    struct winsize winp;
    ioctl(0, TIOCGWINSZ, &winp);
    unsigned short buff[2] = { winp.ws_row, winp.ws_col };
    send(g_ioSockets.controlSock, (char *)buff, sizeof buff, 0);
}

void* send_buffer(void *param)
{
    int ret;
    char data[100];

    struct pollfd fds = { 0, POLLIN, 0 };

    while (1)
    {
        ret = poll(&fds, 1, -1);

        if (fds.revents & POLLIN)
        {
            ret = read(0, data, sizeof data);
            if (ret > 0)
                ret = send(g_ioSockets.inputSock, data, ret, 0);
            else
                break;
        }
    }

    return NULL;
}

int main(void)
{
    int ret;

    struct WSAData wdata;
    ret = WSAStartup(MAKEWORD(2,2), &wdata);
    assert(ret == 0);

    SOCKET sClient = Initialize();

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

    char data[100];

    while (1)
    {
        ret = recv(g_ioSockets.outputSock, data, sizeof data, 0);
        if (ret > 0)
            ret = write(1, data, ret);
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
