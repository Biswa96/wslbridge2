/*
 * GNU GENERAL PUBLIC LICENSE Version 3 (GNU GPL v3)
 * Copyright (c) 2019 Biswapriyo Nath
 * This file is part of wslbridge2 project
 */

#include <windows.h>
#include <stdio.h>

#if defined(__CYGWIN__) || defined(__MSYS__)
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <mutex>
#include "../frontend/TerminalState.hpp"
#endif

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
--feature pty \
-- %s"

#define RESIZE_CONHOST_SIGNAL_BUFFER 8

struct RESIZE_PSEUDO_CONSOLE_BUFFER {
    unsigned short flag;
    unsigned short width;
    unsigned short height;
};

class RawPty
{
public:
    RawPty();
    ~RawPty();
    void CreateConHost(char *program, bool usePty, bool followCur);

private:
    void *mhStdIn = nullptr;
    void *mhStdOut = nullptr;
    void *mhReadPipe = nullptr;
    void *mhWritePipe = nullptr;
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

    SECURITY_ATTRIBUTES pipeAttr = {};
    pipeAttr.nLength = sizeof pipeAttr;
    pipeAttr.bInheritHandle = false;
    CreatePipe(&mhReadPipe, &mhWritePipe, &pipeAttr, 0);
    SetHandleInformation(mhReadPipe, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
}

RawPty::~RawPty()
{
    if (mhStdIn != nullptr) CloseHandle(mhStdIn);
    if (mhStdOut != nullptr) CloseHandle(mhStdOut);
    if (mhReadPipe != nullptr) CloseHandle(mhReadPipe);
    if (mhWritePipe != nullptr) CloseHandle(mhWritePipe);
}

/* global variable */
static int pipefd[2];

void RawPty::CreateConHost(char *program, bool usePty, bool followCur)
{
    COORD size = {};

#if defined(__CYGWIN__) || defined(__MSYS__)

    struct winsize winp;

    if (isatty(STDIN_FILENO)
        && ioctl(STDIN_FILENO, TIOCGWINSZ, &winp) == 0)
    {
        size.X = winp.ws_col;
        size.Y = winp.ws_row;
    }

    TerminalState termState;
    if (usePty)
        termState.enterRawMode();

    pipe2(pipefd, O_NONBLOCK | O_CLOEXEC);
    struct sigaction act = {};
    act.sa_flags = SA_RESTART;
    act.sa_handler = [](int sig)
    {
        char buf = 1;
        write(pipefd[1], &buf, sizeof buf);
    }; /* using lambda */

    sigaction(SIGWINCH, &act, nullptr);

#else /* msvc or windows */

    DWORD consoleMode;
    GetConsoleMode(mhStdOut, &consoleMode);
    SetConsoleMode(mhStdOut, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    CONSOLE_SCREEN_BUFFER_INFO ConBuffer = {};
    if (GetConsoleScreenBufferInfo(mhStdOut, &ConBuffer))
    {
        size.X = ConBuffer.srWindow.Right - ConBuffer.srWindow.Left + 1;
        size.Y = ConBuffer.srWindow.Bottom - ConBuffer.srWindow.Top + 1;
    }

#endif /* cygwin or msys */

    char command[200];
    snprintf(
        command,
        200,
        CONHOST_COMMAND_FORMAT,
        followCur ? "--inheritcursor " : "",
        size.X,
        size.Y,
        HandleToUlong(mhReadPipe),
        VT_PARSE_IO_MODE_XTERM_256COLOR,
        program);

    PROCESS_INFORMATION pi = {};
    STARTUPINFO si = {};

    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESIZE;
    si.hStdInput = mhStdIn;
    si.hStdOutput = mhStdOut;
    si.hStdError = mhStdOut;
    si.dwX = size.X;
    si.dwY = size.Y;

    int bRes;
    bRes = CreateProcess(NULL, command, NULL, NULL, TRUE,
                         0, NULL, NULL, &si, &pi);

#if defined(__CYGWIN__) || defined(__MSYS__)

    DWORD exitCode;
    struct fd_set fdset;
    FD_SET(pipefd[0], &fdset);
    struct RESIZE_PSEUDO_CONSOLE_BUFFER buf;

    while (1)
    {
        if (GetExitCodeProcess(pi.hProcess, &exitCode)
            && exitCode != STILL_ACTIVE)
        {
            break;
        }

        select(2, &fdset, nullptr, nullptr, nullptr);

        if (isatty(STDIN_FILENO)
            && ioctl(STDIN_FILENO, TIOCGWINSZ, &winp) == 0)
        {
            buf.width = winp.ws_col;
            buf.height = winp.ws_row;
            buf.flag = RESIZE_CONHOST_SIGNAL_BUFFER;
            if (!WriteFile(mhWritePipe, &buf, sizeof buf, nullptr, nullptr))
                break;
        }
    }

    if (usePty)
        termState.exitCleanly(0);

    close(pipefd[0]);
    close(pipefd[1]);

#endif /* cygwin or msys */

    if (bRes)
        WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    RawPty rawPty;
    rawPty.CreateConHost(argv[1], true, true);

    return 0;
}
