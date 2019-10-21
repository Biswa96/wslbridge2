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

WindowsSock::WindowsSock(void)
{
    m_hModule = LoadLibraryExW(
                L"ws2_32.dll",
                NULL,
                LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(m_hModule != NULL);

    pfnAccept = (ACCEPTPROC)GetProcAddress(m_hModule, "accept");
    pfnBind = (BINDPROC)GetProcAddress(m_hModule, "bind");
    pfnCloseSocket = (CLOSESOCKETPROC)GetProcAddress(m_hModule, "closesocket");
    pfnConnect = (CONNETCPROC)GetProcAddress(m_hModule, "connect");
    pfnGetSockName = (GETSOCKNAMEPROC)GetProcAddress(m_hModule, "getsockname");
    pfnListen = (LISTENPROC)GetProcAddress(m_hModule, "listen");
    pfnRecv = (RECVPROC)GetProcAddress(m_hModule, "recv");
    pfnSend = (SENDPROC)GetProcAddress(m_hModule, "send");
    pfnSocket = (SOCKETPROC)GetProcAddress(m_hModule, "socket");
    pfnSetSockOpt = (SETSOCKOPTPROC)GetProcAddress(m_hModule, "setsockopt");
    pfnWSAStartup = (WSASTARTUPPROC)GetProcAddress(m_hModule, "WSAStartup");
    pfnWSACleanup = (WSACLEANUPPROC)GetProcAddress(m_hModule, "WSACleanup");

    struct WSAData wdata;
    const int wsaRet = pfnWSAStartup(MAKEWORD(2,2), &wdata);
    assert(wsaRet == 0);
}

WindowsSock::~WindowsSock(void)
{
    pfnWSACleanup();
    if (m_hModule)
        FreeLibrary(m_hModule);
}

SOCKET WindowsSock::CreateHvSock(void)
{
    const SOCKET sock = pfnSocket(
                        AF_HYPERV,
                        SOCK_STREAM,
                        HV_PROTOCOL_RAW);
    assert(sock > 0);

    const int suspend = true;
    const int suspendRet = pfnSetSockOpt(
                           sock,
                           HV_PROTOCOL_RAW,
                           HVSOCKET_CONNECTED_SUSPEND,
                           &suspend,
                           sizeof suspend);
    assert(suspendRet == 0);

    /* Return socket to caller */
    return sock;
}

SOCKET WindowsSock::CreateLocSock(void)
{
    const SOCKET sock = pfnSocket(
                        AF_INET,
                        SOCK_STREAM,
                        IPPROTO_TCP);
    assert(sock > 0);

    const int flag = true;
    const int nodelayRet = pfnSetSockOpt(
                           sock,
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           &flag,
                           sizeof flag);
    assert(nodelayRet == 0);

    /* Return socket to caller */
    return sock;
}

SOCKET WindowsSock::AcceptHvSock(const SOCKET sock)
{
    const SOCKET cSock = pfnAccept(sock, nullptr, nullptr);
    assert(cSock > 0);
    return cSock;
}

SOCKET WindowsSock::AcceptLocSock(const SOCKET sock)
{
    const SOCKET cSock = pfnAccept(sock, NULL, NULL);
    assert(cSock > 0);

    const int flag = true;
    const int nodelayRet = pfnSetSockOpt(
                           cSock,
                           IPPROTO_TCP,
                           TCP_NODELAY,
                           &flag,
                           sizeof flag);
    assert(nodelayRet == 0);

    return cSock;
}

void WindowsSock::ConnectHvSock(const SOCKET sock, const GUID *VmId, const int port)
{
    const int timeout = 10 * 1000; /* Ten seconds */
    const int timeRet = pfnSetSockOpt(
                        sock,
                        HV_PROTOCOL_RAW,
                        HVSOCKET_CONNECT_TIMEOUT,
                        &timeout,
                        sizeof timeout);
    assert(timeRet == 0);

    struct SOCKADDR_HV addr = {};
    addr.Family = AF_HYPERV;
    memcpy(&addr.VmId, VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = port;
    const int connectRet = pfnConnect(sock, &addr, sizeof addr);
    assert(connectRet == 0);
}

int WindowsSock::ListenHvSock(const SOCKET sock, const GUID *VmId, const int backlog)
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
        const int bindRet = pfnBind(sock, &addr, sizeof addr);
        if (bindRet == 0)
            break;

        nretries++;
    }

    const int listenRet = pfnListen(sock, backlog);
    assert(listenRet == 0);

    /* Return port number to caller */
    return port;
}

int WindowsSock::ListenLocSock(const SOCKET sock, const int backlog)
{
    /* Bind to any available port */
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int bindRet = pfnBind(sock, &addr, sizeof addr);
    assert(bindRet == 0);

    const int listenRet = pfnListen(sock, backlog);
    assert(listenRet == 0);

    int addrLen = sizeof addr;
    const int getRet = pfnGetSockName(sock, &addr, &addrLen);
    assert(getRet == 0);

    /* Return port number to caller */
    return ntohs(addr.sin_port);
}

int WindowsSock::Receive(const SOCKET sock, void *buf, int len)
{
    return pfnRecv(sock, buf, len, 0);
}

int WindowsSock::Send(const SOCKET sock, void *buf, int len)
{
    return pfnSend(sock, buf, len, 0);
}

void WindowsSock::Close(const SOCKET sock)
{
    if (sock > 0)
        pfnCloseSocket(sock);
}
