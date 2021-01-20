// This file is part of wslbridge2 project.
// Licensed under the terms of the GNU General Public License v3 or later.
// Copyright (C) 2019-2021 Biswapriyo Nath.

// nix-sock.h: Wrappers for network functions in WSL side backend.

#ifndef NIX_SOCK_H
#define NIX_SOCK_H

#ifdef __cplusplus
extern "C" {
#endif

// Return IPv4 family socket.
int nix_local_create(void);

// Accept IPv4 socket and return accepted socket.
int nix_local_accept(const int sock);

// Create and connect with a localhost socket and return it.
int nix_local_connect(const unsigned short port);

// Create and listen to a localhost socket and return it.
int nix_local_listen(const unsigned short port);

// Return vsock family socket.
int nix_vsock_create(void);

// Accept vsocket and return accepted socket.
int nix_vsock_accept(const int sock);

// Create and connect with a vsocket and return it.
int nix_vsock_connect(const unsigned int port);

// Create and listen to a vsocket and return it.
int nix_vsock_listen(unsigned int *port);

// Set custom environment variables with IP values for WSL2.
void nix_set_env(void);

#ifdef __cplusplus
}
#endif

#endif // NIX_SOCK_H
