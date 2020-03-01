/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <getopt.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <wordexp.h>

#include <string>
#include <vector>

/* This requires linux-headers package */
#include <linux/vm_sockets.h>

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

/* Check if backend is invoked from WSL2 or WSL1 */
static bool IsVmMode(void)
{
    struct stat statbuf;
    if (stat("/dev/vsock", &statbuf) == 0)
        return true;
    else
        return false;
}

static int ConnectLocalSock(const int port)
{
    const int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sockfd > 0);

    const int flag = true;
    const int nodelayRet = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof flag);
    assert(nodelayRet == 0);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connectRet = connect(sockfd, (sockaddr*)&addr, sizeof addr);
    assert(connectRet == 0);

    return sockfd;
}

/* Return created client socket to send */
static int ConnectHvSock(const unsigned int initPort)
{
    const int sockfd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sockfd > 0);

    struct sockaddr_vm addr = {};
    addr.svm_family = AF_VSOCK;
    addr.svm_port = initPort;
    addr.svm_cid = VMADDR_CID_HOST;
    const int connectRet = connect(sockfd, (sockaddr*)&addr, sizeof addr);
    assert(connectRet == 0);

    return sockfd;
}

/* Return socket and random port number */
static int ListenVsockAnyPort(unsigned int *randomPort, const int backlog)
{
    const int sockfd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(sockfd > 0);

    /* Bind to any available port */
    struct sockaddr_vm addr = {};
    addr.svm_family = AF_VSOCK;
    addr.svm_port = VMADDR_PORT_ANY;
    addr.svm_cid = VMADDR_CID_ANY;
    const int bindRet = bind(sockfd, (sockaddr*)&addr, sizeof addr);
    assert(bindRet == 0);

    socklen_t addrlen = sizeof addr;
    const int getRet = getsockname(sockfd, (sockaddr*)&addr, &addrlen);
    assert(getRet == 0);

    const int listenRet = listen(sockfd, backlog);
    assert(listenRet == 0);

    /* Return port number and socket to caller */
    *randomPort = addr.svm_port;
    return sockfd;
}

/* Set custom environment variables, not so important */
static void CreateEnvironmentBlock(void)
{
    const int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        perror("socket(AF_INET)");

    struct ifreq ifr;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);
    const int ret = ioctl(sockfd, SIOCGIFADDR, &ifr);
    if (ret != 0)
        perror("ioctl(SIOCGIFADDR)");

    /* Do not override environment if the name already exists */
    struct sockaddr_in *addr_in = (struct sockaddr_in *)&ifr.ifr_addr;
    setenv("WSL_GUEST_IP", inet_ntoa(addr_in->sin_addr), false);

    close(sockfd);
}

static void usage(const char *prog)
{
    printf(
    "\nUsage: %s [--] [options] [arguments]\n"
    "\nbackend for wslbridge2\n"
    "This backend should not be executed directly without frontend\n\n"
    "Options:\n"
    "  -c, --cols N   Sets N columns for pty\n"
    "  -e, --env VAR  Copies VAR into the WSL environment.\n"
    "  -e VAR=VAL     Sets VAR to VAL in the WSL environment.\n"
    "  -h, --help     Shows this usage information\n"
    "  -l, --login    Starts a login shell\n"
    "  -P, --path dir Starts in certain path\n"
    "  -p, --port N   Sets port N to initialize connections\n"
    "  -r, --rows N   Sets N rows for pty\n"
    "  -x, --xmod     Enables debug output\n\n",
    prog);
    exit(0);
}

static void try_help(const char *prog)
{
    fprintf(stderr, "Try '%s --help' for more information.\n", prog);
    exit(1);
}

struct ChildParams
{
    std::vector<char*> env;
    std::string prog;
    std::vector<char*> argv;
    std::string cwd;
};

int main(int argc, char *argv[])
{
    if (argc < 2)
        try_help(argv[0]);

    int ret;
    struct winsize winp;
    struct ChildParams childParams;
    bool debugMode = false;
    bool loginMode = false;

    /* Ports for WSL1 */
    int controlPort = -1;
    int inputPort = -1;
    int outputPort = -1;

    /* Ports for WSL2 */
    unsigned int initPort = 0;
    unsigned int randomPort = 0;

    const char shortopts[] = "+0:1:3:c:e:hlp:P:r:x";
    const struct option longopts[] = {
        { "cols",  required_argument, 0, 'c' },
        { "env",   required_argument, 0, 'e' },
        { "help",  no_argument,       0, 'h' },
        { "login", no_argument,       0, 'l' },
        { "port",  required_argument, 0, 'p' },
        { "path",  required_argument, 0, 'P' },
        { "rows",  required_argument, 0, 'r' },
        { "xmod",  no_argument,       0, 'x' },
        { 0,       no_argument,       0,  0  },
    };

    int ch = 0;
    while ((ch = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1)
    {
        switch (ch)
        {
            case '0': inputPort = atoi(optarg); break;
            case '1': outputPort = atoi(optarg); break;
            case '3': controlPort = atoi(optarg); break;
            case 'c': winp.ws_col = atoi(optarg); break;
            case 'e': childParams.env.push_back(strdup(optarg)); break;
            case 'h': usage(argv[0]); break;
            case 'l': loginMode = true; break;
            case 'p': initPort = atoi(optarg); break;
            case 'P': childParams.cwd = optarg; break;
            case 'r': winp.ws_row = atoi(optarg); break;
            case 'x': debugMode = true; break;
            default: try_help(argv[0]); break;
        }
    }

    /* If size not provided use master window size */
    if (winp.ws_col == 0 || winp.ws_row == 0)
    {
        ret = ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);
        assert(ret == 0);
    }

    /* Structure only to hold socket file descriptors */
    union IoSockets {
        int sock[3];
        struct {
            int inputSock;
            int outputSock;
            int controlSock;
        };
    };
    union IoSockets ioSockets = {};

    const bool vmMode = IsVmMode();
    if (vmMode) /* WSL2 */
    {
        /* First connect to Windows side then send random port */
        const int client_sock = ConnectHvSock(initPort);
        const int server_sock = ListenVsockAnyPort(&randomPort, ARRAYSIZE(ioSockets.sock));
        ret = send(client_sock, &randomPort, sizeof randomPort, 0);
        assert(ret > 0);
        close(client_sock);

        /* Now act as a server and accept I/O channels */
        for (size_t i = 0; i < ARRAYSIZE(ioSockets.sock); i++)
        {
            ioSockets.sock[i] = accept4(server_sock, NULL, NULL, SOCK_CLOEXEC);
            assert(ioSockets.sock[i] > 0);
        }
        close(server_sock);

        if (debugMode)
        {
            printf(
            "cols: %d rows: %d initPort: %d randomPort: %d\n",
            winp.ws_col, winp.ws_row, initPort, randomPort);
        }
    }
    else /* WSL1 */
    {
        ioSockets.inputSock = ConnectLocalSock(inputPort);
        ioSockets.outputSock = ConnectLocalSock(outputPort);
        ioSockets.controlSock = ConnectLocalSock(controlPort);

        if (debugMode)
        {
            printf(
            "cols: %d rows: %d in: %d out: %d con: %d\n",
            winp.ws_col, winp.ws_row, inputPort, outputPort, controlPort);
        }
    }

    int mfd;
    char ptyname[16];
    const pid_t child = forkpty(&mfd, ptyname, NULL, &winp);
    if (child > 0 && debugMode)
    {
        printf(
        "master fd: %d child pid: %d pty name: %s\n",
        mfd, child, ptyname);
    }

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
                { ioSockets.inputSock, POLLIN, 0 },
                { ioSockets.controlSock, POLLIN, 0 },
                { mfd, POLLIN, 0 }
            };

        ssize_t readRet = 0, writeRet = 0;
        char data[1024]; /* Buffer to hold raw data from pty */

        do
        {
            ret = poll(fds, ARRAYSIZE(fds), -1);
            assert(ret > 0);

            /* Receive input buffer and write it to master */
            if (fds[0].revents & POLLIN)
            {
                readRet = recv(ioSockets.inputSock, data, sizeof data, 0);
                if (readRet > 0)
                    writeRet = write(mfd_dp, data, readRet);
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
                if (ret != 0)
                    perror("ioctl(TIOCSWINSZ)");

                if (debugMode)
                    printf("cols: %d rows: %d\n", winp.ws_col, winp.ws_row);
            }

            /* Receive buffers from master and send to output socket */
            if (fds[2].revents & POLLIN)
            {
                readRet = read(mfd, data, sizeof data);
                if (readRet > 0)
                    writeRet = send(ioSockets.outputSock, data, readRet, 0);
            }

            /* Shutdown I/O sockets when child process terminates */
            if (fds[2].revents & (POLLERR | POLLHUP))
            {
                struct signalfd_siginfo sigbuff;
                ret = read(sigfd, &sigbuff, sizeof sigbuff);
                if (sigbuff.ssi_signo != SIGCHLD)
                    perror("unexpected signal");

                int wstatus;
                if (waitpid(child, &wstatus, 0) != child)
                    perror("waitpid");

                for (size_t i = 0; i < ARRAYSIZE(ioSockets.sock); i++)
                    shutdown(ioSockets.sock[i], SHUT_RDWR);
                break;
            }
        }
        while (writeRet > 0);

        close(sigfd);
        close(mfd_dp);
        close(mfd);
    }
    else if (child == 0) /* child or slave */
    {
        int res;

        for (char *const &setting : childParams.env)
            putenv(setting);

        /* Set WSL_GUEST_IP environment variable */
        if (vmMode)
            CreateEnvironmentBlock();

        /* Changed directory should affect in child process */
        if (!childParams.cwd.empty())
        {
            wordexp_t expanded_cwd;
            wordexp(childParams.cwd.c_str(), &expanded_cwd, 0);
            if (expanded_cwd.we_wordc != 1)
            {
                fprintf(stderr,
                    "path expansion failed, word expanded to %ld paths",
                    expanded_cwd.we_wordc);
            }

            res = chdir(expanded_cwd.we_wordv[0]);
            wordfree(&expanded_cwd);
            if (res != 0)
                perror("chdir");
        }

        for (int i = optind; i < argc; ++i)
            childParams.argv.push_back(argv[i]);

        if (childParams.argv.empty())
        {
            const char *shell = "/bin/sh";
#ifdef use_getpwuid
            struct passwd *pw = getpwuid(getuid());
            assert(pw != NULL);
            if (pw->pw_shell != NULL)
                shell = pw->pw_shell;
#else
            if (getenv("SHELL"))
                shell = getenv("SHELL");
#endif
            childParams.argv.push_back(strdup(shell));
        }

        childParams.prog = childParams.argv[0];
        if (loginMode)
        {
            std::string argv0 = childParams.argv[0];
            const std::size_t pos = argv0.find_last_of('/');
            if (pos != std::string::npos)
                argv0 = argv0.substr(pos + 1);

            argv0 = '-' + argv0;
            childParams.argv[0] = strdup(argv0.c_str());
        }
        childParams.argv.push_back(NULL);

        res = execvp(childParams.prog.c_str(), childParams.argv.data());
        if (res != 0)
            perror("execvp");

        /*
         * Do not use exit() because it performs clean-up
         * related to user-mode constructs in the library
         */
        _exit(0);
    }
    else
        perror("fork");

    /* cleanup */
    for (size_t i = 0; i < ARRAYSIZE(ioSockets.sock); i++)
        close(ioSockets.sock[i]);

    return 0;
}
