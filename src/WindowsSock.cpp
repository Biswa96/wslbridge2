/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

#include <winsock.h>
#include <assert.h>

#include "../hvsocket/hvsocket.h"
#include "WindowsSock.hpp"

/* The range 49152–65535 contains dynamic ports */
#define DYNAMIC_PORT_LOW 49152
#define DYNAMIC_PORT_HIGH 65535
#define BIND_MAX_RETRIES 10

#define RANDOMPORT() \
rand() % (DYNAMIC_PORT_HIGH - DYNAMIC_PORT_LOW) + DYNAMIC_PORT_LOW

void WindowsSock_ctor(void)
{
    struct WSAData wdata;
    const int wsaRet = WSAStartup(MAKEWORD(2,2), &wdata);
    assert(wsaRet == 0);
}

void WindowsSock_dtor(void)
{
    WSACleanup();
}

SOCKET CreateHvSock(void)
{
    const SOCKET sock = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    assert(sock > 0);

    const int suspend = true;
    const int suspendRet = setsockopt(
                           sock,
                           HV_PROTOCOL_RAW,
                           HVSOCKET_CONNECTED_SUSPEND,
                           (char*)&suspend,
                           sizeof suspend);
    assert(suspendRet == 0);

    /* Return socket to caller */
    return sock;
}

SOCKET CreateLocSock(void)
{
    const SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(sock > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(
                           sock,
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           (char*)&flag,
                           sizeof flag);
    assert(nodelayRet == 0);

    /* Return socket to caller */
    return sock;
}

SOCKET AcceptHvSock(const SOCKET sock)
{
    const SOCKET cSock = accept(sock, NULL, NULL);
    assert(cSock > 0);
    return cSock;
}

SOCKET AcceptLocSock(const SOCKET sock)
{
    const SOCKET cSock = accept(sock, NULL, NULL);
    assert(cSock > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(
                           cSock,
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           (char*)&flag,
                           sizeof flag);
    assert(nodelayRet == 0);

    return cSock;
}

void ConnectHvSock(const SOCKET sock, const GUID *VmId, const int port)
{
    const int timeout = 10 * 1000; /* Ten seconds */
    const int timeRet = setsockopt(
                        sock,
                        HV_PROTOCOL_RAW,
                        HVSOCKET_CONNECT_TIMEOUT,
                        (char*)&timeout,
                        sizeof timeout);
    assert(timeRet == 0);

    struct SOCKADDR_HV addr = {};
    addr.Family = AF_HYPERV;
    memcpy(&addr.VmId, VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = port;
    const int connectRet = connect(sock, (sockaddr*)&addr, sizeof addr);
    assert(connectRet == 0);
}

int ListenHvSock(const SOCKET sock, const GUID *VmId, const int backlog)
{
    struct SOCKADDR_HV addr = {};
    addr.Family = AF_HYPERV;
    memcpy(&addr.VmId, VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);

    /* Try to bind to a dynamic port */
    int nretries = 0;
    int port;

    while (nretries < BIND_MAX_RETRIES)
    {
        port = RANDOMPORT();
        addr.ServiceId.Data1 = port;
        const int bindRet = bind(sock, (sockaddr*)&addr, sizeof addr);
        if (bindRet == 0)
            break;

        nretries++;
    }

    const int listenRet = listen(sock, backlog);
    assert(listenRet == 0);

    /* Return port number to caller */
    return port;
}

int ListenLocSock(const SOCKET sock, const int backlog)
{
    /* Bind to any available port */
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int bindRet = bind(sock, (sockaddr*)&addr, sizeof addr);
    assert(bindRet == 0);

    const int listenRet = listen(sock, backlog);
    assert(listenRet == 0);

    int addrLen = sizeof addr;
    const int getRet = getsockname(sock, (sockaddr*)&addr, &addrLen);
    assert(getRet == 0);

    /* Return port number to caller */
    return ntohs(addr.sin_port);
}

int WindowsSock_Recv(const SOCKET sock, void *buf, int len)
{
    return recv(sock, (char*)buf, len, 0);
}

int WindowsSock_Send(const SOCKET sock, void *buf, int len)
{
    return send(sock, (char*)buf, len, 0);
}

void WindowsSock_Close(const SOCKET sock)
{
    if (sock > 0)
        closesocket(sock);
}
