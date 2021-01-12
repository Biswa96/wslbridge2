/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2021 Biswapriyo Nath.
 */

/*
 * WindowsSock.hpp: Wraps WinSock functions to separate identical Cygwin imports.
 */

#ifndef WINDOWSSOCK_HPP
#define WINDOWSSOCK_HPP

void WindowsSock(void);
SOCKET CreateHvSock(void);
SOCKET CreateLocSock(void);
SOCKET AcceptHvSock(const SOCKET sock);
SOCKET AcceptLocSock(const SOCKET sock);
void ConnectHvSock(const SOCKET sock, const GUID *VmId, const int port);
void ConnectLocSock(const SOCKET sock, const USHORT port);
int ListenHvSock(const SOCKET sock, const GUID *VmId);
int ListenLocSock(const SOCKET sock, const USHORT port);

#endif /* WINDOWSSOCK_HPP */
