// This file is part of wslbridge2 project.
// Licensed under the terms of the GNU General Public License v3 or later.
// Copyright (C) 2019-2021 Biswapriyo Nath.

// windows-sock.h: Wrappers for network functions in Windows side frontend.

#ifndef WINDOWS_SOCK_H
#define WINDOWS_SOCK_H

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Windows socket functionality.
void win_sock_init(void);

// Return IPv4 family socket.
SOCKET win_local_create(void);

// Accept IPv4 socket and return accepted socket.
SOCKET win_local_accept(const SOCKET sock);

// Create and connect with a localhost socket and return it.
SOCKET win_local_connect(const unsigned short port);

// Listen to a localhost socket and return the port.
int win_local_listen(const SOCKET sock, const unsigned short port);

// Return hyperv family socket.
SOCKET win_vsock_create(void);

// Accept hyperv socket and return accepted socket.
SOCKET win_vsock_accept(const SOCKET sock);

// Create and connect with a hyperv socket and return it.
SOCKET win_vsock_connect(const GUID *VmId, const unsigned int port);

// Listen to a hyperv socket and return the port.
int win_vsock_listen(const SOCKET sock, const GUID *VmId);

#ifdef __cplusplus
}
#endif

#endif // WINDOWS_SOCK_H
