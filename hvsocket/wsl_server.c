/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/vm_sockets.h>

#define BUFF_SIZE 400

int main(void)
{
    int sockfd = socket(AF_VSOCK, SOCK_STREAM, 0);
    if (sockfd < 0)
        printf("socket error: %s\n", strerror(errno));
    else
        printf("socket: %d\n", sockfd);

    struct sockaddr_vm addr = { 0 };
    addr.svm_family = AF_VSOCK;
    addr.svm_port = VMADDR_PORT_ANY;
    addr.svm_cid = VMADDR_CID_ANY;
    int ret = bind(sockfd, (struct sockaddr*)&addr, sizeof addr);
    if (ret < 0)
        printf("bind error: %s\n", strerror(errno));

    socklen_t addrlen = sizeof addr;
    ret = getsockname(sockfd, (struct sockaddr*)&addr, &addrlen);
    if (ret < 0)
        printf("getsockname error: %s\n", strerror(errno));
    else
        printf("getsockname port: %d\n", addr.svm_port);

    ret = listen(sockfd, 1);
    if (ret < 0)
        printf("listen error: %s\n", strerror(errno));

    int csockfd = accept(sockfd, (struct sockaddr*)&addr, &addrlen);
    if (csockfd < 0)
        printf("accept error: %s\n", strerror(errno));
    else
        printf("client socket: %d\n", csockfd);

    char msg[BUFF_SIZE];

    do
    {
        memset(msg, 0, sizeof msg);
        ret = recv(csockfd, msg, BUFF_SIZE, 0);
        if(ret > 0)
            printf("%s\n", msg);
        else if (ret == 0)
            printf("server closing...\n");
        else
            break;

    } while (ret > 0);

    /* cleanup */
    if (sockfd > 0)
        close(sockfd);
    if (csockfd > 0)
        close(csockfd);
    return 0;
}
