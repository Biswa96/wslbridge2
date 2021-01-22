// This file is part of wslbridge2 project.
// Licensed under the terms of the GNU General Public License v3 or later.
// Copyright (C) 2019-2021 Biswapriyo Nath.

// windows-sock.c: Wrappers for network functions in Windows side frontend.

#include <assert.h>
#include <stdbool.h>
#include <winsock2.h>
#include <hvsocket.h>

#include "windows-sock.h"

#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif

/* The range 49152–65535 contains dynamic ports */
#define DYNAMIC_PORT_LOW 49152
#define DYNAMIC_PORT_HIGH 65535
#define BIND_MAX_RETRIES 10

#define RANDOMPORT() \
rand() % (DYNAMIC_PORT_HIGH - DYNAMIC_PORT_LOW) + DYNAMIC_PORT_LOW

// Initialize Windows socket functionality.
void win_sock_init(void)
{
    WSADATA wdata;
    const int wsaRet = WSAStartup(MAKEWORD(2,2), &wdata);
    assert(wsaRet == 0);
}

// Return IPv4 family socket.
SOCKET win_local_create(void)
{
    const SOCKET sock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                            NULL, 0, WSA_FLAG_OVERLAPPED);
    assert(sock > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                            (const char *)&flag, sizeof flag);
    assert(nodelayRet == 0);

    const int reuseRet = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                            (const char *)&flag, sizeof flag);
    assert(reuseRet == 0);

    return sock;
}

// Accept IPv4 socket and return accepted socket.
SOCKET win_local_accept(const SOCKET sock)
{
    const SOCKET acceptSock = WSAAccept(sock, NULL, NULL, NULL, 0);
    assert(acceptSock > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(acceptSock, IPPROTO_TCP, TCP_NODELAY,
                            (const char *)&flag, sizeof flag);
    assert(nodelayRet == 0);

    const int reuseRet = setsockopt(acceptSock, SOL_SOCKET, SO_REUSEADDR,
                            (const char *)&flag, sizeof flag);
    assert(reuseRet == 0);

    // Server socket is no longer needed.
    closesocket(sock);
    return acceptSock;
}

// Create and connect with a localhost socket and return it.
SOCKET win_local_connect(const unsigned short port)
{
    const SOCKET sock = win_local_create();

    // Connect to a specific port and localhost address.
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connectRet = WSAConnect(sock, (const struct sockaddr *)&addr,
                                sizeof addr, NULL, NULL, NULL, NULL);
    assert(connectRet == 0);

    return sock;
}

// Listen to a localhost socket and return the port.
int win_local_listen(const SOCKET sock, const unsigned short port)
{
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int bindRet = bind(sock, (struct sockaddr *)&addr, sizeof addr);
    assert(bindRet == 0);

    // Listen for only one connection.
    const int listenRet = listen(sock, 1);
    assert(listenRet == 0);

    int addrLen = sizeof addr;
    const int getRet = getsockname(sock, (struct sockaddr *)&addr, &addrLen);
    assert(getRet == 0);

    // Return port number to caller.
    return ntohs(addr.sin_port);
}

// Return hyperv family socket.
SOCKET win_vsock_create(void)
{
    const SOCKET sock = WSASocketW(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW,
                            NULL, 0, WSA_FLAG_OVERLAPPED);
    assert(sock > 0);

    const int suspend = true;
    const int suspendRet = setsockopt(sock, HV_PROTOCOL_RAW, HVSOCKET_CONNECTED_SUSPEND,
                           (const char *)&suspend, sizeof suspend);
    assert(suspendRet == 0);

    return sock;
}

// Accept hyperv socket and return accepted socket.
SOCKET win_vsock_accept(const SOCKET sock)
{
    const SOCKET acceptSock = WSAAccept(sock, NULL, NULL, NULL, 0);
    assert(acceptSock > 0);

    // Server socket is no longer needed.
    closesocket(sock);
    return acceptSock;
}

// Create and connect with a hyperv socket and return it.
SOCKET win_vsock_connect(const GUID *VmId, const unsigned int port)
{
    const SOCKET sock = win_vsock_create();

    const int timeout = 10 * 1000; // Ten seconds.
    const int timeRet = setsockopt(sock, HV_PROTOCOL_RAW, HVSOCKET_CONNECT_TIMEOUT,
                        (const char *)&timeout, sizeof timeout);
    assert(timeRet == 0);

    SOCKADDR_HV addr = { 0 };
    addr.Family = AF_HYPERV;
    memcpy(&addr.VmId, VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = port;
    const int connectRet = WSAConnect(sock, (const struct sockaddr *)&addr,
                                sizeof addr, NULL, NULL, NULL, NULL);
    assert(connectRet == 0);

    return sock;
}

// Listen to a hyperv socket and return the port.
int win_vsock_listen(const SOCKET sock, const GUID *VmId)
{
    SOCKADDR_HV addr = { 0 };
    addr.Family = AF_HYPERV;
    memcpy(&addr.VmId, VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);

    //wslbridge2 #2: Try to bind to a dynamic port, AF_HYPERV can not.
    int nretries = 0, port = 0;

    while (nretries < BIND_MAX_RETRIES)
    {
        port = RANDOMPORT();
        addr.ServiceId.Data1 = port;
        const int bindRet = bind(sock, (struct sockaddr *)&addr, sizeof addr);
        if (bindRet == 0)
            break;

        nretries++;
    }

    // Listen for only one connection.
    const int listenRet = listen(sock, 1);
    assert(listenRet == 0);

    return port;
}
