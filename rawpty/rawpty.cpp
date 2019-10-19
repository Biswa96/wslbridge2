/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <windows.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <string>
#include "../src/TerminalState.hpp"

#ifndef PointerToUInt
#define PointerToUInt(x) (unsigned int)(unsigned long int)(x)
#endif

/* remove headless option to reveal ConHost window */
#if defined(__x86_64__)

#define CONHOST_COMMAND_FORMAT \
"\\\\?\\%s\\System32\\conhost.exe --headless %s--width %hu --height %hu --signal 0x%x -- "

#elif defined(__i386__)

#define CONHOST_COMMAND_FORMAT \
"\\\\?\\%s\\Sysnative\\conhost.exe --headless %s--width %hu --height %hu --signal 0x%x -- "

#else
#error "Could not determine architecture"
#endif

#define RESIZE_CONHOST_SIGNAL_FLAG 8

struct RESIZE_PSEUDO_CONSOLE_BUFFER
{
    unsigned short flag;
    unsigned short width;
    unsigned short height;
};

struct PipeHandles
{
    void *hRead;
    void *hWrite;
};

/* global variable */
static struct PipeHandles pipeHandles;

static void* resize_conpty(void *set)
{
    int ret, signum;
    struct winsize winp;
    struct RESIZE_PSEUDO_CONSOLE_BUFFER buf;

    while (1)
    {
        /* wait for the window resize signal aka. SIGWINCH */
        ret = sigwait((sigset_t *)set, &signum);
        if (ret != 0 || signum != SIGWINCH)
            break;

        /* read terminal window size and write to signal ConHost */
        if (isatty(STDIN_FILENO)
            && ioctl(STDIN_FILENO, TIOCGWINSZ, &winp) == 0)
        {
            buf.width = winp.ws_col;
            buf.height = winp.ws_row;
            buf.flag = RESIZE_CONHOST_SIGNAL_FLAG;
            ret = WriteFile(pipeHandles.hWrite, &buf, sizeof buf, NULL, NULL);
            if (!ret)
                break;
        }
    }

    return NULL;
}

static void RawPty(std::string program, bool usePty, bool followCur)
{
    int ret;
    void *mhStdIn = GetStdHandle(STD_INPUT_HANDLE);
    void *mhStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    void *hProc = GetCurrentProcess();

    ret = DuplicateHandle(hProc, mhStdIn, hProc, &mhStdIn,
                          0, TRUE, DUPLICATE_SAME_ACCESS);
    assert(ret != 0);

    ret = DuplicateHandle(hProc, mhStdOut, hProc, &mhStdOut,
                          0, TRUE, DUPLICATE_SAME_ACCESS);
    assert(ret != 0);

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
    pipeAttr.bInheritHandle = FALSE;
    ret = CreatePipe(&pipeHandles.hRead, &pipeHandles.hWrite, &pipeAttr, 0);
    assert(ret != 0);

    ret = SetHandleInformation(pipeHandles.hRead,
            HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    assert(ret != 0);

    /* create thread for resizing window with SIGWINCH signal */
    pthread_t tid;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGWINCH);
    ret = pthread_sigmask(SIG_BLOCK, &set, NULL);
    assert(ret == 0);

    ret = pthread_create(&tid, NULL, resize_conpty, (void *)&set);
    assert(ret == 0);

    /* create ConHost command line */
    unsigned int nSize;
    nSize = ExpandEnvironmentStringsA("%SystemRoot%", NULL, 0);
    char *sysRoot = (char *)HeapAlloc(GetProcessHeap(), 0, nSize * sizeof(char));
    nSize = ExpandEnvironmentStringsA("%SystemRoot%", sysRoot, nSize);
    assert(nSize > 0);

    char dest[512];
    ret = snprintf(
        dest,
        512,
        CONHOST_COMMAND_FORMAT,
        sysRoot,
        followCur ? "--inheritcursor " : "",
        size.X,
        size.Y,
        PointerToUInt(pipeHandles.hRead));
    assert(ret > 0);

    HeapFree(GetProcessHeap(), 0, sysRoot);

    std::string command(dest);
    command.append(program);

    PROCESS_INFORMATION pi = {};
    STARTUPINFOA si = {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = mhStdIn;
    si.hStdOutput = mhStdOut;
    si.hStdError = mhStdOut;

    ret = CreateProcessA(NULL, &command[0], NULL, NULL,
                         true, 0, NULL, NULL, &si, &pi);
    assert(ret != 0);

    /* wait for ConHost to complete */
    if (ret)
        WaitForSingleObject(pi.hProcess, INFINITE);
    else
        printf("CreateProcess error: %d\n", (int)GetLastError());

    /* cleanup */
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(pipeHandles.hRead);
    CloseHandle(pipeHandles.hWrite);
    CloseHandle(mhStdIn);
    CloseHandle(mhStdOut);
    if (usePty)
        termState.exitCleanly(0);
}

static void usage(const char *prog)
{
    printf(
    "\n"
    "No executable provided.\n"
    "Usage: %s <Win32 executable & its options>\n"
    "Example:\n"
    "rawpty.exe cmd.exe\n"
    "rawpty.exe \"cmd.exe /c dir\"\n",
    prog);
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        usage(argv[0]);

    /* append all the arguments except argv[0] */
    std::string program;
    for (int i = 1; i < argc; i++)
    {
        program.append(argv[i]);
        program.append(" ");
    }

    /*
     * Detect if standard output is console handle e.g. ConPTY
     * If true then just execute the program without using RawPty
     */
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR)
    {
        system(program.c_str());
        return 0;
    }

    RawPty(program, true, true);

    return 0;
}
