/*
 * GNU GENERAL PUBLIC LICENSE Version 3 (GNU GPL v3)
 * Copyright (c) 2019 Biswapriyo Nath
 * This file is part of wslbridge2 project
 */
#include <windows.h>

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <mutex>
#include "../frontend/TerminalState.hpp"

/* VT modes strings in ConHost command options */
#define VT_PARSE_IO_MODE_XTERM "xterm"
#define VT_PARSE_IO_MODE_XTERM_ASCII "xterm-ascii"
#define VT_PARSE_IO_MODE_XTERM_256COLOR "xterm-256color"
#define VT_PARSE_IO_MODE_WIN_TELNET "win-telnet"

#define CONHOST_COMMAND_FORMAT \
"conhost.exe \
--headless %s\
--width %hu \
--height %hu \
--signal 0x%x \
--vtmode %s \
--feature pty -- "

#define RESIZE_CONHOST_SIGNAL_BUFFER 8

struct RESIZE_PSEUDO_CONSOLE_BUFFER
{
    unsigned short flag;
    unsigned short width;
    unsigned short height;
};

class RawPty
{
public:
    RawPty();
    ~RawPty();
    void CreateConHost(std::string program, bool usePty, bool followCur);

private:
    void *mhStdIn = nullptr;
    void *mhStdOut = nullptr;
};

RawPty::RawPty()
{
    mhStdIn = GetStdHandle(STD_INPUT_HANDLE);
    mhStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    void *hProc = GetCurrentProcess();

    DuplicateHandle(hProc, mhStdIn, hProc, &mhStdIn,
                    0, TRUE, DUPLICATE_SAME_ACCESS);

    DuplicateHandle(hProc, mhStdOut, hProc, &mhStdOut,
                    0, TRUE, DUPLICATE_SAME_ACCESS);
}

RawPty::~RawPty()
{
    if (mhStdIn != nullptr) CloseHandle(mhStdIn);
    if (mhStdOut != nullptr) CloseHandle(mhStdOut);
}

/* global variable */
static void *hPipe[2];

static void* resize_conpty(void *set)
{
    int ret, sig;
    struct winsize winp;
    struct RESIZE_PSEUDO_CONSOLE_BUFFER buf;

    while (1)
    {
        /* wait for the window resize signal aka. SIGWINCH */
        ret = sigwait((sigset_t*)set, &sig);
        if (ret != 0 && sig != SIGWINCH)
            break;

        /* read terminal window size and write to signal ConHost */
        if (isatty(STDIN_FILENO)
            && ioctl(STDIN_FILENO, TIOCGWINSZ, &winp) == 0)
        {
            buf.width = winp.ws_col;
            buf.height = winp.ws_row;
            buf.flag = RESIZE_CONHOST_SIGNAL_BUFFER;
            if (!WriteFile(hPipe[1], &buf, sizeof buf, nullptr, nullptr))
                break;
        }
    }

    return nullptr;
}

void RawPty::CreateConHost(std::string program, bool usePty, bool followCur)
{
    COORD size = {};
    struct winsize winp;

    /* detect terminal window size */
    if (isatty(STDIN_FILENO)
        && ioctl(STDIN_FILENO, TIOCGWINSZ, &winp) == 0)
    {
        size.X = winp.ws_col;
        size.Y = winp.ws_row;
    }

    TerminalState termState;
    if (usePty)
        termState.enterRawMode();

    /* create Windows pipes for signaling ConHost */
    SECURITY_ATTRIBUTES pipeAttr = {};
    pipeAttr.nLength = sizeof pipeAttr;
    pipeAttr.bInheritHandle = false;
    CreatePipe(&hPipe[0], &hPipe[1], &pipeAttr, 0);
    SetHandleInformation(hPipe[0], HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    /* create thread for resizing window with SIGWINCH signal */
    pthread_t tid;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGWINCH);
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
    pthread_create(&tid, nullptr, &resize_conpty, (void*)&set);

    /* create ConHost command line */
    char dest[200];
    snprintf(
        dest,
        200,
        CONHOST_COMMAND_FORMAT,
        followCur ? "--inheritcursor " : "",
        size.X,
        size.Y,
        HandleToUlong(hPipe[0]),
        VT_PARSE_IO_MODE_XTERM_256COLOR);

    std::string command(dest);
    command.append(program);

    PROCESS_INFORMATION pi = {};
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = mhStdIn;
    si.hStdOutput = mhStdOut;
    si.hStdError = mhStdOut;

    int bRes;
    bRes = CreateProcessA(NULL, &command[0], NULL, NULL,
                          true, 0, NULL, NULL, &si, &pi);

    /* wait for ConHost to complete */
    if (bRes)
        WaitForSingleObject(pi.hProcess, INFINITE);

    /* cleanup */
    if (usePty)
        termState.exitCleanly(0);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hPipe[0]);
    CloseHandle(hPipe[1]);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    /* append all the agruments except argv[0] */
    std::string program;
    for (int i = 1; i < argc; i++)
    {
        program.append(argv[i]);
        program.append(" ");
    }

    RawPty rawPty;
    rawPty.CreateConHost(program, true, true);

    return 0;
}
