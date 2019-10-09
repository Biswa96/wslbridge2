/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <winsock.h>
#include <assert.h>

#include "../hvsocket/hvsocket.h"
#include "HyperVSocket.hpp"

/* The range 49152–65535 contains dynamic ports */
#define DYNAMIC_PORT_LOW 49152
#define DYNAMIC_PORT_HIGH 65535
#define BIND_MAX_RETRIES 10

#define RANDOMPORT() \
rand() % (DYNAMIC_PORT_HIGH - DYNAMIC_PORT_LOW) + DYNAMIC_PORT_LOW

HyperVSocket::HyperVSocket(void)
{
    m_hModule = LoadLibraryExW(
                L"ws2_32.dll",
                nullptr,
                LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(m_hModule != nullptr);

    pfnAccept = (ACCEPTPROC)GetProcAddress(m_hModule, "accept");
    pfnBind = (BINDPROC)GetProcAddress(m_hModule, "bind");
    pfnCloseSocket = (CLOSESOCKETPROC)GetProcAddress(m_hModule, "closesocket");
    pfnConnect = (CONNETCPROC)GetProcAddress(m_hModule, "connect");
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

HyperVSocket::~HyperVSocket(void)
{
    if (m_hModule)
        FreeLibrary(m_hModule);
    pfnWSACleanup();
}

SOCKET HyperVSocket::Create(void)
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

SOCKET HyperVSocket::Accept(const SOCKET sock)
{
    const SOCKET cSock = pfnAccept(sock, nullptr, nullptr);
    assert(cSock > 0);
    return cSock;
}

void HyperVSocket::Connect(const SOCKET sock, const GUID *VmId, const int port)
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

int HyperVSocket::Listen(const SOCKET sock, const GUID *VmId)
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

    const int listenRet = pfnListen(sock, -1);
    assert(listenRet == 0);

    /* Return port number to caller */
    return port;
}

int HyperVSocket::Receive(const SOCKET sock, void *buf, int len)
{
    return pfnRecv(sock, buf, len, 0);
}

int HyperVSocket::Send(const SOCKET sock, void *buf, int len)
{
    return pfnSend(sock, buf, len, 0);
}

void HyperVSocket::Close(const SOCKET sock)
{
    if (sock > 0)
        pfnCloseSocket(sock);
}
