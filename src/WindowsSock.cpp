/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2020 Biswapriyo Nath.
 */

#include <winsock2.h>
#include <hvsocket.h>
#include <assert.h>

#include "WindowsSock.hpp"

#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif

/* The range 49152–65535 contains dynamic ports */
#define DYNAMIC_PORT_LOW 49152
#define DYNAMIC_PORT_HIGH 65535
#define BIND_MAX_RETRIES 10

#define RANDOMPORT() \
rand() % (DYNAMIC_PORT_HIGH - DYNAMIC_PORT_LOW) + DYNAMIC_PORT_LOW

void WindowsSock(void)
{
    WSADATA wdata;
    const int wsaRet = WSAStartup(MAKEWORD(2,2), &wdata);
    assert(wsaRet == 0);
}

SOCKET CreateHvSock(void)
{
    const SOCKET sock = WSASocketW(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW,
                            NULL, 0, WSA_FLAG_OVERLAPPED);
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
    const SOCKET sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                            NULL, 0, WSA_FLAG_OVERLAPPED);
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
    const SOCKET cSock = WSAAccept(sock, NULL, NULL, NULL, 0);
    assert(cSock > 0);

    /* Server socket is no longer needed. */
    closesocket(sock);
    return cSock;
}

SOCKET AcceptLocSock(const SOCKET sock)
{
    const SOCKET cSock = WSAAccept(sock, NULL, NULL, NULL, 0);
    assert(cSock > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(
                           cSock,
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           (char*)&flag,
                           sizeof flag);
    assert(nodelayRet == 0);

    /* Server socket is no longer needed. */
    closesocket(sock);
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

    SOCKADDR_HV addr = {};
    addr.Family = AF_HYPERV;
    memcpy(&addr.VmId, VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = port;
    const int connectRet = WSAConnect(sock, (sockaddr*)&addr, sizeof addr,
                                NULL, NULL, NULL, NULL);
    assert(connectRet == 0);
}

int ListenHvSock(const SOCKET sock, const GUID *VmId)
{
    SOCKADDR_HV addr = {};
    addr.Family = AF_HYPERV;
    memcpy(&addr.VmId, VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);

    /* Try to bind to a dynamic port */
    int nretries = 0, port = 0;

    while (nretries < BIND_MAX_RETRIES)
    {
        port = RANDOMPORT();
        addr.ServiceId.Data1 = port;
        const int bindRet = bind(sock, (sockaddr*)&addr, sizeof addr);
        if (bindRet == 0)
            break;

        nretries++;
    }

    /* Listen for only one connection. */
    const int listenRet = listen(sock, 1);
    assert(listenRet == 0);

    /* Return port number to caller */
    return port;
}

int ListenLocSock(const SOCKET sock)
{
    /* Bind to any available port */
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int bindRet = bind(sock, (sockaddr*)&addr, sizeof addr);
    assert(bindRet == 0);

    /* Listen for only one connection. */
    const int listenRet = listen(sock, 1);
    assert(listenRet == 0);

    int addrLen = sizeof addr;
    const int getRet = getsockname(sock, (sockaddr*)&addr, &addrLen);
    assert(getRet == 0);

    /* Return port number to caller */
    return ntohs(addr.sin_port);
}
