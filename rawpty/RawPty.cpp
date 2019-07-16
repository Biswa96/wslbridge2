/*
 * GNU GENERAL PUBLIC LICENSE Version 3 (GNU GPL v3)
 * Copyright (c) 2019 Biswapriyo Nath
 * This file is part of wslbridge2 project
 */

#include <windows.h>
#include <stdio.h>

#if defined(__CYGWIN__) || defined(__MSYS__)
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

class RawPty
{
public:
    RawPty();
    ~RawPty();
    void CreateConHost(char *program, bool usePty, bool followCur);

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
    if (mhStdIn != nullptr)
        CloseHandle(mhStdIn);
    if (mhStdOut != nullptr)
        CloseHandle(mhStdOut);
}

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

#else /* msvc or windows */

    CONSOLE_SCREEN_BUFFER_INFO ConBuffer = {};
    if (GetConsoleScreenBufferInfo(hStdOut, &ConBuffer))
    {
        size.X = ConBuffer.srWindow.Right - ConBuffer.srWindow.Left + 1;
        size.Y = ConBuffer.srWindow.Bottom - ConBuffer.srWindow.Top + 1;
    }

#endif /* cygwin or msys */

    char command[200];
    snprintf(
        command,
        200,
        "conhost.exe --headless %s--width %hu --height %hu --vtmode %s --feature pty -- %s",
        followCur ? "--inheritcursor " : "",
        size.X,
        size.Y,
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
    bRes = CreateProcess(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (bRes)
        WaitForSingleObject(pi.hProcess, INFINITE);

    /* cleanup */
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

#if defined(__CYGWIN__) || defined(__MSYS__)
    if (usePty)
        termState.exitCleanly(0);
#endif
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    RawPty rawPty;
    rawPty.CreateConHost(argv[1], true, false);

    return 0;
}
