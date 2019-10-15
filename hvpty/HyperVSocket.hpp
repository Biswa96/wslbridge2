/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#ifndef HYPERVSOCKET_HPP
#define HYPERVSOCKET_HPP

#ifndef SOCKET
#define SOCKET size_t
#endif

/* Windows socket functions pointers from ws2_32.dll file */
typedef SOCKET (WINAPI *ACCEPTPROC)(SOCKET, void*, int*);
typedef int (WINAPI *BINDPROC)(SOCKET, const void*, int);
typedef int (WINAPI *CLOSESOCKETPROC)(SOCKET);
typedef int (WINAPI *CONNETCPROC)(SOCKET, const void*, int);
typedef int (WINAPI *LISTENPROC)(SOCKET, int);
typedef int (WINAPI *RECVPROC)(SOCKET, void*, int, int);
typedef int (WINAPI *SENDPROC)(SOCKET, const void*, int, int);
typedef SOCKET (WINAPI *SOCKETPROC)(int, int, int);
typedef int (WINAPI *SETSOCKOPTPROC)(SOCKET, int, int, const void*, int);
typedef int (WINAPI *WSASTARTUPPROC)(WORD, void*);
typedef int (WINAPI *WSACLEANUPPROC)(void);

/* This wraps WinSock functions to separate identical Cygwin imports */
class HyperVSocket
{
public:
    HyperVSocket(void);
    ~HyperVSocket(void);
    SOCKET Create(void);
    SOCKET Accept(const SOCKET sock);
    void Connect(const SOCKET sock, const GUID *VmId, const int port);
    int Listen(const SOCKET sock, const GUID *VmId, const int backlog);
    int Receive(const SOCKET sock, void *buf, int len);
    int Send(const SOCKET sock, void *buf, int len);
    void Close(const SOCKET sock);

private:
    HMODULE m_hModule;
    ACCEPTPROC pfnAccept;
    BINDPROC pfnBind;
    CLOSESOCKETPROC pfnCloseSocket;
    CONNETCPROC pfnConnect;
    LISTENPROC pfnListen;
    RECVPROC pfnRecv;
    SENDPROC pfnSend;
    SOCKETPROC pfnSocket;
    SETSOCKOPTPROC pfnSetSockOpt;
    WSASTARTUPPROC pfnWSAStartup;
    WSACLEANUPPROC pfnWSACleanup;
};

#endif /* HYPERVSOCKET_HPP */
