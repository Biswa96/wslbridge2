/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#define PORT_NUM 54321
#define BUFF_SIZE 400

/* return created client socket to send */
int Initialize(void)
{
    int ret;

    int cSock = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(cSock > 0);

    struct sockaddr_vm addr;
    memset(&addr, 0, sizeof addr);

    addr.svm_family = AF_VSOCK;
    addr.svm_port = PORT_NUM;
    addr.svm_cid = VMADDR_CID_HOST;
    ret = connect(cSock, (struct sockaddr *)&addr, sizeof addr);
    assert(ret == 0);

    return cSock;
}

/* return socket and random port number */
int create_vmsock(unsigned int *port)
{
    int ret;

    int sSock = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sSock > 0);

    struct sockaddr_vm addr;
    memset(&addr, 0, sizeof addr);

    addr.svm_family = AF_VSOCK;
    addr.svm_port = VMADDR_PORT_ANY;
    addr.svm_cid = VMADDR_CID_ANY;
    ret = bind(sSock, (struct sockaddr *)&addr, sizeof addr);
    assert(ret == 0);

    socklen_t addrlen = sizeof addr;
    ret = getsockname(sSock, (struct sockaddr *)&addr, &addrlen);
    assert(ret == 0);

    ret = listen(sSock, -1);
    assert(ret == 0);

    *port = addr.svm_port;

    return sSock;
}

union IoSockets
{
    int sock[4];
    struct
    {
        int inputSock;
        int outputSock;
        int errorSock;
        int controlSock;
    };
};

int main(void)
{
    int ret;

    int client_sock = Initialize();

    unsigned int port = 0;
    int server_sock = create_vmsock(&port);

    ret = send(client_sock, &port, sizeof port, 0);
    assert(ret > 0);

    union IoSockets ioSockets;

    for (int i = 0; i < 4; i++)
    {
        ioSockets.sock[i] = accept4(server_sock, NULL, NULL, SOCK_CLOEXEC);
        assert(ioSockets.sock[i] > 0);
    }

    struct winsize winp;
    ret = ioctl(0, TIOCGWINSZ, &winp);
    assert(ret == 0);

    struct passwd *pwd = getpwuid(getuid());
    if (pwd == NULL)
        perror("getpwuid");

    int masterfd;
    pid_t child = forkpty(&masterfd, NULL, NULL, &winp);

    if (child > 0) /* parent or master */
    {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGCHLD);
        ret = sigprocmask(SIG_BLOCK, &set, NULL);
        assert(ret == 0);

        int sigfd = signalfd(-1, &set, 0);
        assert(sigfd > 0);

        struct pollfd fds[6] = {
                { ioSockets.inputSock,    POLLIN, 0 },
                { ioSockets.controlSock,  POLLIN, 0 },
                { masterfd,               POLLIN, 0 },
                { sigfd,                  POLLIN, 0 }
            };

        while(1)
        {
            char data[100];

            ret = poll(fds, (sizeof fds / sizeof fds[0]), -1);
            assert(ret > 0);

            if (fds[0].revents & POLLIN)
            {
                ret = recv(ioSockets.inputSock, data, sizeof data, 0);
                assert(ret > 0);
                ret = write(masterfd, &data, ret);
            }

            /* resize window from receiving buffers */
            if (fds[1].revents & POLLIN)
            {
                unsigned short buff[2];
                ret = recv(ioSockets.controlSock, buff, sizeof buff, 0);
                assert(ret > 0);

                winp.ws_row = buff[0];
                winp.ws_col = buff[1];
                ret = ioctl(masterfd, TIOCSWINSZ, &winp);
                assert(ret == 0);
            }

            if (fds[2].revents & POLLIN)
            {
                ret = read(masterfd, data, sizeof data);
                assert(ret > 0);
                ret = send(ioSockets.outputSock, data, ret, 0);
            }

            if (fds[2].revents & (POLLERR | POLLHUP))
            {
                shutdown(ioSockets.outputSock, SHUT_WR);
                shutdown(ioSockets.errorSock, SHUT_WR);
            }

            /* if child process in slave side is terminated */
            if (fds[3].revents & POLLIN)
            {
                struct signalfd_siginfo sigbuff;
                ret = read(sigfd, &sigbuff, sizeof sigbuff);
                assert(sigbuff.ssi_signo == SIGCHLD);
                break;
            }
        }

        close(sigfd);
        close(masterfd);
    }
    else if (child == 0) /* child or slave */
    {
        char *args = NULL;
        ret = execvp(pwd->pw_shell, &args);
        assert(ret > 0);
    }
    else
        perror("fork");

    /* cleanup */
    for (int i = 0; i < 4; i++)
        close(ioSockets.sock[i]);
    close(server_sock);
    close(client_sock);
}
