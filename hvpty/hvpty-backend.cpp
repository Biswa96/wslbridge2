/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include <string>
#include <vector>

/* Enable this to show debug information */
static const char IsDebugMode = 0;

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

/* Return created client socket to send */
static int Initialize(const unsigned int initPort)
{
    int ret;

    const int cSock = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(cSock > 0);

    struct sockaddr_vm addr;
    memset(&addr, 0, sizeof addr);

    addr.svm_family = AF_VSOCK;
    addr.svm_port = initPort;
    addr.svm_cid = VMADDR_CID_HOST;
    ret = connect(cSock, (struct sockaddr *)&addr, sizeof addr);
    assert(ret == 0);

    return cSock;
}

/* Return socket and random port number */
static int create_vmsock(unsigned int *randomPort)
{
    int ret;

    const int sSock = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sSock > 0);

    struct sockaddr_vm addr;
    memset(&addr, 0, sizeof addr);

    /* Bind to any available port */
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

    /* Return port number and socket to caller */
    *randomPort = addr.svm_port;
    return sSock;
}

static void usage(const char *prog)
{
    printf(
    "backend for hvpty using AF_VSOCK sockets\n"
    "Usage: %s [--] [options] [arguments]\n"
    "\n"
    "Options:\n"
    "  -c, --cols N   Set N columns for pty\n"
    "  -h, --help     Show this usage information\n"
    "  -l, --login    Start a login shell\n"
    "  -P, --path dir Start in certain path\n"
    "  -p, --port N   Set port N to initialize connections\n"
    "  -r, --rows N   Set N rows for pty\n",
    prog);
    exit(0);
}

static void try_help(const char *prog)
{
    fprintf(stderr, "Try '%s --help' for more information.\n", prog);
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        try_help(argv[0]);

    int ret;
    struct winsize winp;
    unsigned int initPort = 0, randomPort = 0;
    std::string workDir;
    bool loginMode = false;
    int c = 0;

    const char shortopts[] = "+c:hlp:P:r:";
    const struct option longopts[] = {
        { "cols",  required_argument, 0, 'c' },
        { "help",  no_argument,       0, 'h' },
        { "login", no_argument,       0, 'l' },
        { "port",  required_argument, 0, 'p' },
        { "path",  required_argument, 0, 'P' },
        { "rows",  required_argument, 0, 'r' },
        { 0,       no_argument,       0,  0  },
    };

    while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1)
    {
        switch (c)
        {
            case 'c':
                winp.ws_col = atoi(optarg);
                break;
            case 'h':
                usage(argv[0]);
                break;
            case 'l':
                loginMode = true;
                break;
            case 'p':
                initPort = atoi(optarg);
                break;
            case 'P':
                workDir = optarg;
                break;
            case 'r':
                winp.ws_row = atoi(optarg);
                break;
            default:
                try_help(argv[0]);
                break;
        }
    }

    /* If size not provided use master window size */
    if (winp.ws_col == 0 || winp.ws_row == 0)
    {
        ret = ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);
        assert(ret == 0);
    }

    if (IsDebugMode)
        printf("cols: %d row: %d port: %d\n",
               winp.ws_col, winp.ws_row, initPort);

    /* First connect to Windows side then send random port */
    const int client_sock = Initialize(initPort);
    const int server_sock = create_vmsock(&randomPort);
    ret = send(client_sock, &randomPort, sizeof randomPort, 0);
    assert(ret > 0);
    close(client_sock);

    /* Now act as a server and accept four I/O channels */
    union IoSockets ioSockets;
    for (int i = 0; i < 4; i++)
    {
        ioSockets.sock[i] = accept4(server_sock, NULL, NULL, SOCK_CLOEXEC);
        assert(ioSockets.sock[i] > 0);
    }
    close(server_sock);

    int mfd;
    char ptyname[16];
    const pid_t child = forkpty(&mfd, ptyname, NULL, &winp);
    if (IsDebugMode)
        printf("pty name: %s\n", ptyname);

    if (child > 0) /* parent or master */
    {
        /* Use dupped master fd to read OR write */
        const int mfd_dp = dup(mfd);
        assert(mfd_dp > 0);

        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGCHLD);
        ret = sigprocmask(SIG_BLOCK, &set, NULL);
        assert(ret == 0);

        const int sigfd = signalfd(-1, &set, 0);
        assert(sigfd > 0);

        struct pollfd fds[] = {
                { ioSockets.inputSock,    POLLIN, 0 },
                { ioSockets.controlSock,  POLLIN, 0 },
                { mfd,                    POLLIN, 0 },
                { sigfd,                  POLLIN, 0 }
            };

        char data[1024];

        while(1)
        {
            ret = poll(fds, (sizeof fds / sizeof fds[0]), -1);
            assert(ret > 0);

            /* Receive input buffer and write it to master */
            if (fds[0].revents & POLLIN)
            {
                ret = recv(ioSockets.inputSock, data, sizeof data, 0);
                assert(ret > 0);
                ret = write(mfd_dp, &data, ret);
            }

            /* Resize window when buffer received in control socket */
            if (fds[1].revents & POLLIN)
            {
                ret = recv(ioSockets.controlSock, &winp, sizeof winp, 0);
                assert(ret > 0);

                /* Remove "unused" pixel values ioctl_tty(2) */
                winp.ws_xpixel = 0;
                winp.ws_ypixel = 0;
                ret = ioctl(mfd, TIOCSWINSZ, &winp);
                assert(ret == 0);

                if (IsDebugMode)
                    printf("cols: %d row: %d\n", winp.ws_col, winp.ws_row);
            }

            /* Receive buffers from master and send to output socket */
            if (fds[2].revents & POLLIN)
            {
                ret = read(mfd, data, sizeof data);
                assert(ret > 0);
                ret = send(ioSockets.outputSock, data, ret, 0);
            }

            if (fds[2].revents & (POLLERR | POLLHUP))
            {
                for (int i = 0; i < 4; i++)
                    shutdown(ioSockets.sock[i], SHUT_RDWR);
                break;
            }

            /* Break if child process in slave side is terminated */
            if (fds[3].revents & POLLIN)
            {
                struct signalfd_siginfo sigbuff;
                ret = read(sigfd, &sigbuff, sizeof sigbuff);
                assert(sigbuff.ssi_signo == SIGCHLD);
                break;
            }
        }

        close(sigfd);
        close(mfd_dp);
        close(mfd);
    }
    else if (child == 0) /* child or slave */
    {
        /* Changed directory should affect in child process */
        if (!workDir.empty())
        {
            ret = chdir(workDir.c_str());
            assert(ret == 0);
        }

        struct passwd *pwd = getpwuid(getuid());
        assert(pwd != nullptr);
        if (pwd->pw_shell == nullptr)
            pwd->pw_shell = strdup("/bin/sh");

        std::vector<char*> args;
        args.push_back(pwd->pw_shell);
        if (loginMode)
            args.push_back(strdup("--login"));
        args.push_back(nullptr);

        ret = execvp(pwd->pw_shell, args.data());
        assert(ret > 0);
    }
    else
        perror("fork");

    /* cleanup */
    for (int i = 0; i < 4; i++)
        close(ioSockets.sock[i]);
}
