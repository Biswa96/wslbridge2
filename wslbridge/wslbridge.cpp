/*
 * The MIT License (MIT)
 * Copyright (c) 2016 Ryan Prichard
 * Copyright (c) 2017-2018 Google LLC
 */

/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <windows.h>

#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <wchar.h>

#include <array>
#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Helpers.hpp"
#include "Environment.hpp"
#include "SocketIo.hpp"
#include "LocalSock.hpp"
#include "TerminalState.hpp"
#include "WinHelper.hpp"

const int32_t kOutputWindowSize = 8192;

static WakeupFd *g_wakeupFd = nullptr;

static TerminalState g_terminalState;

struct IoLoop {
    std::string spawnCwd;
    std::mutex mutex;
    bool ioFinished = false;
    int controlSocketFd = -1;
    bool childReaped = false;
    int childExitStatus = -1;
};

static void fatalConnectionBroken() {
    g_terminalState.fatal("\nwslbridge2 frontend error: connection broken\n");
}

static void writePacket(IoLoop &ioloop, const Packet &p) {
    assert(p.size >= sizeof(p));
    std::lock_guard<std::mutex> lock(ioloop.mutex);
    if (!writeAllRestarting(ioloop.controlSocketFd,
            reinterpret_cast<const char*>(&p), p.size)) {
        fatalConnectionBroken();
    }
}

static void parentToSocketThread(int socketFd) {
    std::array<char, 8192> buf;
    while (true) {
        const ssize_t amt1 = readRestarting(STDIN_FILENO, buf.data(), buf.size());
        if (amt1 <= 0) {
            // If we reach EOF reading from stdin, propagate EOF to the child.
            close(socketFd);
            break;
        }
        if (!writeAllRestarting(socketFd, buf.data(), amt1)) {
            // We don't propagate EOF backwards, but we do let data build up.
            break;
        }
    }
}

static void socketToParentThread(IoLoop *ioloop, bool isErrorPipe, int socketFd, int outFd) {
    uint32_t bytesWritten = 0;
    std::array<char, 32 * 1024> buf;
    while (true) {
        const ssize_t amt1 = readRestarting(socketFd, buf.data(), buf.size());
        if (amt1 == 0) {
            std::lock_guard<std::mutex> lock(ioloop->mutex);
            ioloop->ioFinished = true;
            g_wakeupFd->set();
            break;
        }
        if (amt1 < 0) {
            break;
        }
        if (!writeAllRestarting(outFd, buf.data(), amt1)) {
            if (!isErrorPipe) {
                // ssh seems to propagate an stdout EOF backwards to the remote
                // program, so do the same thing.  It doesn't do this for
                // stderr, though, where the remote process is allowed to block
                // forever.
                Packet p = { sizeof(Packet), Packet::Type::CloseStdoutPipe };
                writePacket(*ioloop, p);
            }
            shutdown(socketFd, SHUT_RDWR);
            break;
        }
        bytesWritten += amt1;
        if (bytesWritten >= kOutputWindowSize / 2) {
            Packet p = { sizeof(Packet), Packet::Type::IncreaseWindow };
            p.u.window.amount = bytesWritten;
            p.u.window.isErrorPipe = isErrorPipe;
            writePacket(*ioloop, p);
            bytesWritten = 0;
        }
    }
}

static void handlePacket(IoLoop *ioloop, const Packet &p) {
    switch (p.type) {
        case Packet::Type::ChildExitStatus: {
            std::lock_guard<std::mutex> lock(ioloop->mutex);
            ioloop->childReaped = true;
            ioloop->childExitStatus = p.u.exitStatus;
            g_wakeupFd->set();
            break;
        }
        case Packet::Type::SpawnFailed: {
            const PacketSpawnFailed &psf = reinterpret_cast<const PacketSpawnFailed&>(p);
            std::string msg;
            switch (p.u.spawnError.type) {
                case SpawnError::Type::ForkPtyFailed:
                    msg = "error: forkpty failed: ";
                    break;
                case SpawnError::Type::ChdirFailed:
                    msg = "error: could not chdir to '" + ioloop->spawnCwd + "': ";
                    break;
                case SpawnError::Type::ExecFailed:
                    msg = "error: could not exec '" + std::string(psf.exe) + "': ";
                    break;
                default:
                    assert(false && "Unhandled SpawnError type");
            }
            msg += errorString(p.u.spawnError.error);
            g_terminalState.fatal("%s\n", msg.c_str());
            break;
        }
        default: {
            g_terminalState.fatal("internal error: unexpected packet %d\n",
                static_cast<int>(p.type));
        }
    }
}

static void mainLoop(const std::string &spawnCwd,
                     int controlSocketFd,
                     int inputSocketFd, int outputSocketFd,
                     TermSize termSize) {
    IoLoop ioloop;
    ioloop.spawnCwd = spawnCwd;
    ioloop.controlSocketFd = controlSocketFd;
    std::thread p2s(parentToSocketThread, inputSocketFd);
    std::thread s2p(socketToParentThread, &ioloop, false, outputSocketFd, STDOUT_FILENO);
    std::thread rcs(readControlSocketThread<IoLoop, handlePacket, fatalConnectionBroken>,
                    controlSocketFd, &ioloop);
    int32_t exitStatus = -1;

    while (true) {
        g_wakeupFd->wait();
        const struct TermSize newSize = terminalSize();
        if (newSize != termSize) {
            Packet p = { sizeof(Packet), Packet::Type::SetSize };
            p.u.termSize = termSize = newSize;
            writePacket(ioloop, p);
        }
        std::lock_guard<std::mutex> lock(ioloop.mutex);
        if (ioloop.childReaped && ioloop.ioFinished) {
            exitStatus = ioloop.childExitStatus;
            break;
        }
    }

    // Socket-to-pty I/O is finished already.
    s2p.join();

    // We can't return, because the threads could still be running.  Rather
    // than shut them down gracefully, which seems hard(?), just let the OS
    // clean everything up.
    g_terminalState.exitCleanly(exitStatus);
}

static void usage(const char *prog)
{
    printf(
    "Usage: %s [options] [--] [command]...\n"
    "Runs a program within a Windows Subsystem for Linux (WSL) pty\n"
    "\n"
    "Options:\n"
    "  -b, --backend BACKEND\n"
    "                Overrides the default path of wslbridge2-backend to BACKEND\n"
    "  -d, --distribution Distribution Name\n"
    "                Run the specified distribution.\n"
    "  -e VAR        Copies VAR into the WSL environment.\n"
    "  -e VAR=VAL    Sets VAR to VAL in the WSL environment.\n"
    "  -h, --help    Show this usage information\n"
    "  -l, --login   Start a login shell.\n"
    "  -u, --user    WSL User Name\n"
    "                Run as the specified user\n"
    "  -w, --windir  Folder\n"
    "                Changes the working directory to Windows style path\n"
    "  -W, --wsldir  Folder\n"
    "                Changes the working directory to Unix style path\n", prog);
    exit(0);
}

struct PipeHandles {
    HANDLE rh;
    HANDLE wh;
};

static struct PipeHandles createPipe()
{
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    PipeHandles ret {};
    const BOOL success = CreatePipe(&ret.rh, &ret.wh, &sa, 0);
    assert(success && "CreatePipe failed");
    return ret;
}

static void invalid_arg(const char *arg)
{
    fatal("error: the %s option requires a non-empty string argument\n", arg);
}

int main(int argc, char *argv[])
{
    if (GetWindowsBuild() < 17763)
        fatal("Windows 10 version is older than minimal requirement.\n");

#ifdef __CYGWIN__
    cygwin_internal(CW_SYNC_WINENV);
#endif

    setlocale(LC_ALL, "");
    g_wakeupFd = new WakeupFd();

    Environment env;
    std::string distroName;
    std::string customBackendPath;
    std::string winDir;
    std::string wslDir;
    std::string userName;
    int debugFork = 0;
    bool loginMode = false;

    if (argv[0][0] == '-')
        loginMode = true;

    const char shortopts[] = "+b:d:e:hlu:w:W:";
    const struct option longopts[] = {
        { "backend",        required_argument,  nullptr,     'b' },
        { "distribution",   required_argument,  nullptr,     'd' },
        { "help",           no_argument,        nullptr,     'h' },
        { "debug-fork",     no_argument,       &debugFork,    1  },
        { "login",          no_argument,        nullptr,     'l' },
        { "user",           required_argument,  nullptr,     'u' },
        { "windir",         required_argument,  nullptr,     'w' },
        { "wsldir",         required_argument,  nullptr,     'W' },
        { nullptr,          no_argument,        nullptr,      0  },
    };

    int ch = 0;
    while ((ch = getopt_long(argc, argv, shortopts, longopts, nullptr)) != -1)
    {
        switch (ch)
        {
            case 0:
                /* Ignore long option. */
                break;
            case 'e':
            {
                const char *eq = strchr(optarg, '=');
                const std::string varname = eq ?
                                            std::string(optarg, eq - optarg)
                                            : std::string(optarg);
                if (varname.empty())
                    invalid_arg("environment");

                if (eq)
                    env.set(varname, eq + 1);
                else
                    env.set(varname);
                break;
            }

            case 'h':
                usage(argv[0]);
                break;

            case 'd':
                distroName = optarg;
                if (distroName.empty())
                    invalid_arg("distribution");
                break;

            case 'l':
                loginMode = true;
                break;

            case 'b':
                customBackendPath = optarg;
                if (customBackendPath.empty())
                    invalid_arg("backend");
                break;

            case 'u':
                userName = optarg;
                if (userName.empty())
                    invalid_arg("user");
                break;

            case 'w':
                winDir = optarg;
                if (winDir.empty())
                    invalid_arg("windir");
                break;

            case 'W':
                wslDir = optarg;
                if (wslDir.empty())
                    invalid_arg("wsldir");
                break;

            default:
                fatal("Try '%s --help' for more information.\n", argv[0]);
        }
    }

    // We must register this handler *before* determining the initial terminal
    // size.
    struct sigaction sa = {};
    sa.sa_handler = [](int signo) { g_wakeupFd->set(); };
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGWINCH, &sa, nullptr);
    sa = {};
    // We want to handle EPIPE rather than receiving SIGPIPE.
    signal(SIGPIPE, SIG_IGN);

    LocalSock controlSocket;
    LocalSock inputSocket;
    LocalSock outputSocket;

    const std::wstring wslPath = findSystemProgram(L"wsl.exe");
    const auto backendPathInfo = normalizePath(
                    findBackendProgram(customBackendPath, L"wslbridge2-backend"));
    const std::wstring backendPathWin = backendPathInfo.first;
    const std::wstring fsname = backendPathInfo.second;
    const struct TermSize initialSize = terminalSize();

    /* Prepare the backend command line. */
    std::wstring wslCmdLine;
    wslCmdLine.append(L"exec \"$(wslpath -u");
    appendWslArg(wslCmdLine, backendPathWin);
    wslCmdLine.append(L")\"");

    if (debugFork)
        appendWslArg(wslCmdLine, L"--debug-fork");

    for (const auto &envPair : env.pairs())
        appendWslArg(wslCmdLine, L"-e" + envPair.first + L"=" + envPair.second);

    if (loginMode)
        appendWslArg(wslCmdLine, L"--login");

    if (!wslDir.empty())
    {
        appendWslArg(wslCmdLine, L"-C");
        appendWslArg(wslCmdLine, mbsToWcs(wslDir));
    }

    {
        std::array<wchar_t, 1024> buffer;
        int iRet = swprintf(
                    buffer.data(),
                    buffer.size(),
                    L" %ls-3%d -0%d -1%d -c%d -r%d -w%d -t%d",
                    debugFork ? L"--debug-fork " : L"",
                    controlSocket.port(),
                    inputSocket.port(),
                    outputSocket.port(),
                    initialSize.cols,
                    initialSize.rows,
                    kOutputWindowSize,
                    kOutputWindowSize / 4);
        assert(iRet > 0);
        wslCmdLine.append(buffer.data());
    }

    /* Append remaining non-option arguments as is */
    appendWslArg(wslCmdLine, L"--");
    for (int i = optind; i < argc; ++i)
        appendWslArg(wslCmdLine, mbsToWcs(argv[i]));

    /* Append wsl.exe options and its arguments */
    std::wstring cmdLine;
    cmdLine.append(L"\"");
    cmdLine.append(wslPath);
    cmdLine.append(L"\"");

    if (!distroName.empty())
    {
        cmdLine.append(L" -d ");
        cmdLine.append(mbsToWcs(distroName));
    }

   /* Taken from HKCU\Directory\Background\shell\WSL\command */
    if (!winDir.empty())
    {
        cmdLine.append(L" --cd \"");
        cmdLine.append(mbsToWcs(winDir));
        cmdLine.append(L"\"");
    }

    if (!userName.empty())
    {
        cmdLine.append(L" --user ");
        cmdLine.append(mbsToWcs(userName));
    }

    cmdLine.append(L" /bin/sh -c ");
    appendWslArg(cmdLine, wslCmdLine);

    const struct PipeHandles outputPipe = createPipe();
    const struct PipeHandles errorPipe = createPipe();

    /* Initialize thread attribute list */
    HANDLE Values[2];
    Values[0] = outputPipe.wh;
    Values[1] = errorPipe.wh;

    SIZE_T AttrSize;
    LPPROC_THREAD_ATTRIBUTE_LIST AttrList = NULL;
    InitializeProcThreadAttributeList(NULL, 1, 0, &AttrSize);
    AttrList = (LPPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, AttrSize);
    InitializeProcThreadAttributeList(AttrList, 1, 0, &AttrSize);
    BOOL bRes = UpdateProcThreadAttribute(
                AttrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, /* 0x20002u */
                Values, sizeof Values, NULL, NULL);
    assert(bRes != 0);

    STARTUPINFOEXW sui {};
    sui.StartupInfo.cb = sizeof(sui);
    sui.lpAttributeList = AttrList;
    sui.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    sui.StartupInfo.hStdOutput = outputPipe.wh;
    sui.StartupInfo.hStdError = errorPipe.wh;

    PROCESS_INFORMATION pi = {};
    BOOL success = CreateProcessW(
                    wslPath.c_str(),
                    &cmdLine[0],
                    NULL,
                    NULL,
                    TRUE,
                    debugFork ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW,
                    NULL,
                    NULL,
                    &sui.StartupInfo,
                    &pi);
    if (!success) {
        fatal("error starting wsl.exe adapter: %s\n",
            formatErrorMessage(GetLastError()).c_str());
    }

    HeapFree(GetProcessHeap(), 0, AttrList);
    CloseHandle(outputPipe.wh);
    CloseHandle(errorPipe.wh);
    success = SetHandleInformation(pi.hProcess, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    assert(success && "SetHandleInformation failed");

    std::atomic<bool> backendStarted = { false };

    // If the backend process exits before the frontend, then something has
    // gone wrong.
    const auto watchdog = std::thread([&]() {
        WaitForSingleObject(pi.hProcess, INFINITE);

        // Because bash.exe has exited, we know that the write ends of the
        // output pipes are closed.  Finish reading anything bash.exe wrote.
        // bash.exe writes at least one error via stdout in UTF-16;
        // wslbridge-backend could write to stderr in UTF-8.
        std::vector<char> outVec = readAllFromHandle(outputPipe.rh);
        std::vector<char> errVec = readAllFromHandle(errorPipe.rh);
        std::wstring outWide(outVec.size() / sizeof(wchar_t), L'\0');
        memcpy(&outWide[0], outVec.data(), outWide.size() * sizeof(wchar_t));
        std::string out { wcsToMbs(outWide, true) };
        std::string err { errVec.begin(), errVec.end() };
        out = stripTrailing(replaceAll(out, "Press any key to continue...", ""));
        err = stripTrailing(err);

        std::string msg;
        if (backendStarted) {
            msg = "\nwslbridge error: backend process died\n";
        } else {
            msg = "wslbridge error: failed to start backend process\n";
            if (fsname != L"NTFS") {
                msg.append("note: backend program is at '");
                msg.append(wcsToMbs(backendPathWin));
                msg.append("'\n");
                msg.append("note: backend is on a volume of type '");
                msg.append(wcsToMbs(fsname));
                msg.append("', expected 'NTFS'\n"
                           "note: WSL only supports local NTFS volumes\n");
            }
        }
        if (!out.empty()) {
            msg.append("note: wsl.exe output: ");
            msg.append(out);
            msg.push_back('\n');
        }
        if (!err.empty()) {
            msg.append("note: backend error output: ");
            msg.append(err);
            msg.push_back('\n');
        }

        g_terminalState.fatal("%s", msg.c_str());
    });

    const int controlSocketC = controlSocket.accept();
    const int inputSocketC = inputSocket.accept();
    const int outputSocketC = outputSocket.accept();
    controlSocket.close();
    inputSocket.close();
    outputSocket.close();

    g_terminalState.enterRawMode();

    backendStarted = true;

    mainLoop(winDir, controlSocketC, inputSocketC, outputSocketC, initialSize);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
