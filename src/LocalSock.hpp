/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

#ifndef LOCALSOCK_HPP
#define LOCALSOCK_HPP

#ifndef SOCKET
#define SOCKET size_t
#endif

/* Windows socket functions pointers from ws2_32.dll file */
typedef SOCKET (WINAPI *ACCEPTPROC)(SOCKET, void*, int*);
typedef int (WINAPI *BINDPROC)(SOCKET, const void*, int);
typedef int (WINAPI *CLOSESOCKETPROC)(SOCKET);
typedef int (WINAPI *CONNETCPROC)(SOCKET, const void*, int);
typedef int (WINAPI *GETSOCKNAMEPROC)(SOCKET, void*, int*);
typedef int (WINAPI *LISTENPROC)(SOCKET, int);
typedef int (WINAPI *RECVPROC)(SOCKET, void*, int, int);
typedef int (WINAPI *SENDPROC)(SOCKET, const void*, int, int);
typedef SOCKET (WINAPI *SOCKETPROC)(int, int, int);
typedef int (WINAPI *SETSOCKOPTPROC)(SOCKET, int, int, const void*, int);
typedef int (WINAPI *WSASTARTUPPROC)(WORD, void*);
typedef int (WINAPI *WSACLEANUPPROC)(void);

/* This wraps WinSock functions to separate identical Cygwin imports */
class LocalSock
{
public:
    LocalSock();
    ~LocalSock();
    SOCKET Create(void);
    SOCKET Accept(const SOCKET sock);
    int Listen(const SOCKET sock, const int backlog);
    int Receive(const SOCKET sock, void *buf, int len);
    int Send(const SOCKET sock, void *buf, int len);
    void Close(const SOCKET sock);

private:
    HMODULE m_hModule;
    ACCEPTPROC pfnAccept;
    BINDPROC pfnBind;
    CLOSESOCKETPROC pfnCloseSocket;
    CONNETCPROC pfnConnect;
    GETSOCKNAMEPROC pfnGetSockName;
    LISTENPROC pfnListen;
    RECVPROC pfnRecv;
    SENDPROC pfnSend;
    SOCKETPROC pfnSocket;
    SETSOCKOPTPROC pfnSetSockOpt;
    WSASTARTUPPROC pfnWSAStartup;
    WSACLEANUPPROC pfnWSACleanup;
};

#endif /* LOCALSOCK_HPP */
