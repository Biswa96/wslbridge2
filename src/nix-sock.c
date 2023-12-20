// This file is part of wslbridge2 project.
// Licensed under the terms of the GNU General Public License v3 or later.
// Copyright (C) 2019-2021 Biswapriyo Nath.

// nix-sock.c: Wrappers for network functions in WSL side backend.

#include <arpa/inet.h>
#include <assert.h>
#include <net/if.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

// Need linux-headers package. Should be after sys/socket.h.
#include <linux/vm_sockets.h>

#define VSOCK_BUFFER_SIZE 0x10000

// Return IPv4 family socket.
int nix_local_create(void)
{
    const int sock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sock > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof flag);
    assert(nodelayRet == 0);

    const int reuseRet = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof flag);
    assert(reuseRet == 0);

    return sock;
}

// Accept IPv4 socket and return accepted socket.
int nix_local_accept(const int sock)
{
    const int acceptSock = accept4(sock, NULL, NULL, SOCK_CLOEXEC);
    assert(acceptSock > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(acceptSock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof flag);
    assert(nodelayRet == 0);

    const int reuseRet = setsockopt(acceptSock, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof flag);
    assert(reuseRet == 0);

    return acceptSock;
}

// Create and connect with a localhost socket and return it.
int nix_local_connect(const unsigned short port)
{
    const int sock = nix_local_create();

    // Connect to a specific port and localhost address.
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connectRet = connect(sock, (struct sockaddr *)&addr, sizeof addr);
    assert(connectRet == 0);

    return sock;
}

// Create and listen to a localhost socket and return it.
int nix_local_listen(const unsigned short port)
{
    const int sock = nix_local_create();

    // Bind to a specific port and localhost address.
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int bindRet = bind(sock, (struct sockaddr *)&addr, sizeof addr);
    assert(bindRet == 0);

    const int listenRet = listen(sock, -1);
    assert(listenRet == 0);

    return sock;
}

// Return vsock family socket.
int nix_vsock_create(void)
{
    const int sock = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sock > 0);

    const int val = VSOCK_BUFFER_SIZE;
    const int sndbufRet = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &val, sizeof val);
    assert(sndbufRet == 0);

    const int rcvbufRet = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &val, sizeof val);
    assert(rcvbufRet == 0);

    return sock;
}

// Accept vsocket and return accepted socket.
int nix_vsock_accept(const int sock)
{
    const int acceptSock = accept4(sock, NULL, NULL, SOCK_CLOEXEC);
    assert(acceptSock > 0);

    const int val = VSOCK_BUFFER_SIZE;
    const int sndbufRet = setsockopt(acceptSock, SOL_SOCKET, SO_SNDBUF, &val, sizeof val);
    assert(sndbufRet == 0);

    const int rcvbufRet = setsockopt(acceptSock, SOL_SOCKET, SO_RCVBUF, &val, sizeof val);
    assert(rcvbufRet == 0);

    return acceptSock;
}

// Create and connect with a vsocket and return it.
int nix_vsock_connect(const unsigned int port)
{
    const int sock = nix_vsock_create();

    // Connect to a specific port and host context ID.
    struct sockaddr_vm addr = { 0 };
    addr.svm_family = AF_VSOCK;
    addr.svm_port = port;
    addr.svm_cid = VMADDR_CID_HOST;
    const int connectRet = connect(sock, (struct sockaddr *)&addr, sizeof addr);
    assert(connectRet == 0);

    return sock;
}

// Create and listen to a vsocket and return it.
int nix_vsock_listen(unsigned int *port)
{
    const int sock = nix_vsock_create();

    // Bind to any available port and context ID.
    struct sockaddr_vm addr = { 0 };
    addr.svm_family = AF_VSOCK;
    addr.svm_port = VMADDR_PORT_ANY;
    addr.svm_cid = VMADDR_CID_ANY;
    const int bindRet = bind(sock, (struct sockaddr *)&addr, sizeof addr);
    assert(bindRet == 0);

    socklen_t addrlen = sizeof addr;
    const int getRet = getsockname(sock, (struct sockaddr *)&addr, &addrlen);
    assert(getRet == 0);

    const int listenRet = listen(sock, -1);
    assert(listenRet == 0);

    // Return port number and socket to caller.
    *port = addr.svm_port;
    return sock;
}
