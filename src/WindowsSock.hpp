/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

#ifndef WINDOWSSOCK_HPP
#define WINDOWSSOCK_HPP

#ifndef SOCKET
#define SOCKET size_t
#endif

/* This wraps WinSock functions to separate identical Cygwin imports */
void WindowsSock_ctor(void);
void WindowsSock_dtor(void);
SOCKET CreateHvSock(void);
SOCKET CreateLocSock(void);
SOCKET AcceptHvSock(const SOCKET sock);
SOCKET AcceptLocSock(const SOCKET sock);
void ConnectHvSock(const SOCKET sock, const GUID *VmId, const int port);
int ListenHvSock(const SOCKET sock, const GUID *VmId, const int backlog);
int ListenLocSock(const SOCKET sock, const int backlog);
int WindowsSock_Recv(const SOCKET sock, void *buf, int len);
int WindowsSock_Send(const SOCKET sock, void *buf, int len);
void WindowsSock_Close(const SOCKET sock);

#endif /* WINDOWSSOCK_HPP */
