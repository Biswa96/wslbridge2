/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

#include <winsock.h>
#include <assert.h>

#include "LocalSock.hpp"

LocalSock::LocalSock()
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

LocalSock::~LocalSock(void)
{
    pfnWSACleanup();
    if (m_hModule)
        FreeLibrary(m_hModule);
}

SOCKET LocalSock::Create(void)
{
    const SOCKET sock = pfnSocket(
                        AF_INET,
                        SOCK_STREAM,
                        0);
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

SOCKET LocalSock::Accept(const SOCKET sock)
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

int LocalSock::Listen(const SOCKET sock, const int backlog)
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

int LocalSock::Receive(const SOCKET sock, void *buf, int len)
{
    return pfnRecv(sock, buf, len, 0);
}

int LocalSock::Send(const SOCKET sock, void *buf, int len)
{
    return pfnSend(sock, buf, len, 0);
}

void LocalSock::Close(const SOCKET sock)
{
    if (sock > 0)
        pfnCloseSocket(sock);
}
