/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2022 Biswapriyo Nath.
 */

#include <assert.h>
#include <getopt.h>
#include <poll.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>
#include <limits.h> // PIPE_BUF

#include <string>
#include <vector>

#include "common.hpp"
#include "nix-sock.h"

/* Check if backend is invoked from WSL2 or WSL1 */
static bool IsVmMode(void)
{
    struct stat statbuf;
    if (stat("/dev/vsock", &statbuf) == 0)
        return true;
    else
        return false;
}

static void usage(const char *prog)
{
    printf("\nwslbridge2-backend %s : Backend for wslbridge2, should be executed by frontend.\n",
        STRINGIFY(WSLBRIDGE2_VERSION));
    printf("Copyright (C) 2019-2021 Biswapriyo Nath.\n");
    printf("Licensed under GNU General Public License version 3 or later.\n");
    printf("\n");
    printf("Usage: %s [options] [--] [command]...\n", prog);
    printf("Options:\n");
    printf("  -c, --cols N   Sets N columns for pty.\n");
    printf("  -e, --env VAR  Copies VAR into the WSL environment.\n");
    printf("  -e VAR=VAL     Sets VAR to VAL in the WSL environment.\n");
    printf("  -h, --help     Shows this usage information.\n");
    printf("  -l, --login    Starts a login shell.\n");
    printf("  -p, --path dir Starts in certain path.\n");
    printf("  -r, --rows N   Sets N rows for pty.\n");
    printf("  -s, --show     Shows hidden backend window and debug output.\n");
    printf("  -x, --xmod     Dummy mode just to start a WSL2 session.\n\n");

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

/* Structure only to hold socket file descriptors. */
union IoSockets
{
    int sock[4];
    struct
    {
        int xserverSock;
        int inputSock;
        int outputSock;
        int controlSock;
    };
};

/* Global variable. */
static volatile union IoSockets ioSockets = { 0 };

int main(int argc, char *argv[])
{
    if (argc < 2)
        try_help(argv[0]);

    int ret;
    struct winsize winp;
    struct ChildParams childParams;
    volatile bool debugMode = false, loginMode = false, xtraMode = false;
    unsigned int inputPort = 0, outputPort = 0, controlPort = 0;

    const char shortopts[] = "+0:1:3:c:e:hlp:r:sx";
    const struct option longopts[] = {
        { "cols",  required_argument, 0, 'c' },
        { "env",   required_argument, 0, 'e' },
        { "help",  no_argument,       0, 'h' },
        { "login", no_argument,       0, 'l' },
        { "path",  required_argument, 0, 'p' },
        { "rows",  required_argument, 0, 'r' },
        { "show",  no_argument,       0, 's' },
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
            case 'p': childParams.cwd = optarg; break;
            case 'r': winp.ws_row = atoi(optarg); break;
            case 's': debugMode = true; break;
            case 'x': xtraMode = true; break;
            default: try_help(argv[0]); break;
        }
    }

    if (xtraMode)
        return 0;

    /* If size not provided use master window size */
    if (winp.ws_col == 0 || winp.ws_row == 0)
    {
        ret = ioctl(STDIN_FILENO, TIOCGWINSZ, &winp);
        assert(ret == 0);
    }

    const bool vmMode = IsVmMode();
    if (vmMode) /* WSL2 */
    {
        ioSockets.inputSock = nix_vsock_connect(inputPort);
        ioSockets.outputSock = nix_vsock_connect(outputPort);
        ioSockets.controlSock = nix_vsock_connect(controlPort);
    }
    else /* WSL1 */
    {
        ioSockets.inputSock = nix_local_connect(inputPort);
        ioSockets.outputSock = nix_local_connect(outputPort);
        ioSockets.controlSock = nix_local_connect(controlPort);
    }

    printf("cols: %d rows: %d in: %d out: %d con: %d\n",
        winp.ws_col, winp.ws_row, inputPort, outputPort, controlPort);

    int mfd;
    char ptyname[16];
    const pid_t child = forkpty(&mfd, ptyname, NULL, &winp);

    if (child > 0) /* parent or master */
    {
        /*
         * wslbridge2#23: Wait for any child process changed state.
         * i.e. prevent zombies. Register sigaction after forkpty (musl).
         */
        struct sigaction act = { 0 };
        act.sa_flags = SA_SIGINFO;
        act.sa_sigaction = [](int signum, siginfo_t *info, void *ucontext)
        {
            for (size_t i = 0; i < ARRAYSIZE(ioSockets.sock); i++)
                shutdown(ioSockets.sock[i], SHUT_RDWR);

            int status;
            wait(&status);

            char str[100];
            int ret = sprintf(str, "signal: %d child status: %d child pid: %d\n",
                        signum, status, info->si_pid);
            ret = write(STDOUT_FILENO, str, ret);
        };
        sigaction(SIGCHLD, &act, NULL);

        printf("master fd: %d child pid: %d pty name: %s\n",
            mfd, child, ptyname);

        /* Use dupped master fd to read OR write */
        const int mfd_dp = dup(mfd);
        assert(mfd_dp > 0);

        struct pollfd fds[] = {
                { ioSockets.inputSock, POLLIN, 0 },
                { ioSockets.controlSock, POLLIN, 0 },
                { mfd, POLLIN, 0 }
            };

        ssize_t readRet = 0, writeRet = 0;
        char data[1024]; /* Buffer to hold raw data from pty */
        assert(sizeof data <= PIPE_BUF);

        do
        {
            ret = poll(fds, ARRAYSIZE(fds), -1);
            assert(ret > 0);

            /* Receive input buffer and write it to master */
            if (fds[0].revents & POLLIN)
            {
                readRet = recv(ioSockets.inputSock, data, sizeof data, 0);
                char * s = data;
                int len = readRet;
                writeRet = 1;
                while (writeRet > 0 && len > 0)
                {
                    if (!*s)
                    {
                        // dispatch NUL escaped inband information
                        s++;
                        len--;

                        if (len < 9 && s + 9 >= data + sizeof data)
                        {
                            // make room for additional loading
                            memcpy(data, s, len);
                            s = data;
                        }

                        // ensure 1 more byte is loaded to dispatch on
                        if (!len)
                        {
                            readRet = recv(ioSockets.inputSock, s, 1, 0);
                            if (readRet > 0)
                            {
                                len += readRet;
                            }
                            else
                            {
                                writeRet = -1;
                                break;
                            }
                        }
                        if (*s == 2)
                        {
                            // STX: escaped NUL
                            s++;
                            len--;
                            writeRet = write(mfd_dp, "", 1);
                        }
                        else if (*s == 16)
                        {
                            // DLE: terminal window size change
                            s++;
                            len--;
                            // ensure 8 more bytes are loaded for winsize
                            while (readRet > 0 && len < 8)
                            {
                                readRet = recv(ioSockets.inputSock, s + len, 8 - len, 0);
                                if (readRet > 0)
                                {
                                    len += readRet;
                                }
                            }
                            if (readRet <= 0)
                            {
                                writeRet = -1;
                                break;
                            }
                            struct winsize * winsp = (struct winsize *)s;
                            s += 8;
                            len -= 8;
                            winsp->ws_xpixel = 0;
                            winsp->ws_ypixel = 0;
                            ret = ioctl(mfd, TIOCSWINSZ, winsp);
                            if (ret != 0)
                                perror("ioctl(TIOCSWINSZ)");
                        }
                    }
                    else
                    {
                        int n = strnlen(s, len);
                        writeRet = write(mfd_dp, s, n);
                        if (writeRet > 0)
                        {
                            s += writeRet;
                            len -= writeRet;
                        }
                    }
                }
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
                for (size_t i = 0; i < ARRAYSIZE(ioSockets.sock); i++)
                    shutdown(ioSockets.sock[i], SHUT_RDWR);

                break;
            }
        }
        while (writeRet > 0);

        close(mfd_dp);
        close(mfd);
    }
    else if (child == 0) /* child or slave */
    {
        int res;

        for (char *const &setting : childParams.env)
            putenv(setting);

        /*
         * wsltty#225: Set WSL_GUEST_IP environment variable in WSL2 only.
         * As WSL1 gets same IP address as Windows and NIC may not be eth0.
         */
        if (vmMode)
            nix_set_env();

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

    if (debugMode)
    {
        printf("Press any key to continue...\n");
        getchar();
    }

    return 0;
}
