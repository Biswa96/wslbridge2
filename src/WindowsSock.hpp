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
void WindowsSock(void);
SOCKET CreateHvSock(void);
SOCKET CreateLocSock(void);
SOCKET AcceptHvSock(const SOCKET sock);
SOCKET AcceptLocSock(const SOCKET sock);
void ConnectHvSock(const SOCKET sock, const GUID *VmId, const int port);
int ListenHvSock(const SOCKET sock, const GUID *VmId);
int ListenLocSock(const SOCKET sock);

#endif /* WINDOWSSOCK_HPP */
