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
#include <sys/cygwin.h>
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
    g_terminalState.fatal("\nwslbridge error: connection broken\n");
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
    "  -l            Start a login shell.\n"
    "  --no-login    Do not start a login shell.\n"
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

class StartupInfoAttributeList {
public:
    StartupInfoAttributeList(PPROC_THREAD_ATTRIBUTE_LIST &attrList, int count) {
        SIZE_T size {};
        InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        assert(size > 0 && "InitializeProcThreadAttributeList failed");
        buffer_ = std::unique_ptr<char[]>(new char[size]);
        const BOOL success = InitializeProcThreadAttributeList(get(), count, 0, &size);
        assert(success && "InitializeProcThreadAttributeList failed");
        attrList = get();
    }
    StartupInfoAttributeList(const StartupInfoAttributeList &) = delete;
    StartupInfoAttributeList &operator=(const StartupInfoAttributeList &) = delete;
    ~StartupInfoAttributeList() {
        DeleteProcThreadAttributeList(get());
    }
private:
    PPROC_THREAD_ATTRIBUTE_LIST get() {
        return reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(buffer_.get());
    }
    std::unique_ptr<char[]> buffer_;
};

class StartupInfoInheritList {
public:
    StartupInfoInheritList(PPROC_THREAD_ATTRIBUTE_LIST attrList,
                           std::vector<HANDLE> &&inheritList) :
            inheritList_(std::move(inheritList)) {
        const BOOL success = UpdateProcThreadAttribute(
            attrList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inheritList_.data(), inheritList_.size() * sizeof(HANDLE),
            nullptr, nullptr);
        assert(success && "UpdateProcThreadAttribute failed");
    }
    StartupInfoInheritList(const StartupInfoInheritList &) = delete;
    StartupInfoInheritList &operator=(const StartupInfoInheritList &) = delete;
    ~StartupInfoInheritList() {}
private:
    std::vector<HANDLE> inheritList_;
};

// WSL bash will print an error if the user tries to run elevated and
// non-elevated instances simultaneously, and maybe other situations.  We'd
// like to detect this situation and report the error back to the user.
//
// Two complications:
//  - WSL bash will print the error to stdout/stderr, but if the file is a
//    pipe, then WSL bash doesn't print it until it exits (presumably due to
//    block buffering).
//  - WSL bash puts up a prompt, "Press any key to continue", and it reads
//    that key from the attached console, not from stdin.
//
// This function spawns the frontend again and instructs it to attach to the
// new WSL bash console and send it a return keypress.
//
// The HANDLE must be inheritable.
static void spawnPressReturnProcess(HANDLE wslProcess)
{
    const std::wstring exePath = getModuleFileName(getCurrentModule());
    std::wstring cmdline;
    cmdline.append(L"\"");
    cmdline.append(exePath);
    cmdline.append(L"\" --press-return ");
    cmdline.append(std::to_wstring(reinterpret_cast<uintptr_t>(wslProcess)));
    STARTUPINFOEXW sui {};
    sui.StartupInfo.cb = sizeof(sui);
    StartupInfoAttributeList attrList { sui.lpAttributeList, 1 };
    StartupInfoInheritList inheritList { sui.lpAttributeList, { wslProcess } };
    PROCESS_INFORMATION pi {};
    const BOOL success = CreateProcessW(exePath.c_str(), &cmdline[0], nullptr, nullptr,
        true, 0, nullptr, nullptr, &sui.StartupInfo, &pi);
    if (!success) {
        fprintf(stderr, "wslbridge warning: could not spawn: %s\n", wcsToMbs(cmdline).c_str());
    }
    if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0) {
        fprintf(stderr, "wslbridge warning: process didn't exit after 10 seconds: %ls\n",
            cmdline.c_str());
    } else {
        DWORD code {};
        BOOL success = GetExitCodeProcess(pi.hProcess, &code);
        if (!success) {
            fprintf(stderr, "wslbridge warning: GetExitCodeProcess failed\n");
        } else if (code != 0) {
            fprintf(stderr, "wslbridge warning: process failed: %ls\n", cmdline.c_str());
        }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

static int handlePressReturn(const char *pidStr) {
    // AttachConsole replaces STD_INPUT_HANDLE with a new console input
    // handle.  See https://github.com/rprichard/win32-console-docs.  The
    // bash.exe process has already started, but console creation and
    // process creation don't happen atomically, so poll for the console's
    // existence.
    auto str2handle = [](const char *str) {
        std::stringstream ss(str);
        uintptr_t n {};
        ss >> n;
        return reinterpret_cast<HANDLE>(n);
    };
    const HANDLE wslProcess = str2handle(pidStr);
    const DWORD wslPid = GetProcessId(wslProcess);
    FreeConsole();
    for (int i = 0; i < 400; ++i) {
        if (WaitForSingleObject(wslProcess, 0) == WAIT_OBJECT_0) {
            // bash.exe has exited, give up immediately.
            return 0;
        } else if (AttachConsole(wslPid)) {
            std::array<INPUT_RECORD, 2> ir {};
            ir[0].EventType = KEY_EVENT;
            ir[0].Event.KeyEvent.bKeyDown = TRUE;
            ir[0].Event.KeyEvent.wRepeatCount = 1;
            ir[0].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
            ir[0].Event.KeyEvent.wVirtualScanCode = MapVirtualKey(VK_RETURN, MAPVK_VK_TO_VSC);
            ir[0].Event.KeyEvent.uChar.UnicodeChar = '\r';
            ir[1] = ir[0];
            ir[1].Event.KeyEvent.bKeyDown = FALSE;
            DWORD actual {};
            WriteConsoleInputW(
                GetStdHandle(STD_INPUT_HANDLE),
                ir.data(), ir.size(), &actual);
            return 0;
        }
        Sleep(25);
    }
    return 1;
}

static std::vector<char> readAllFromHandle(HANDLE h) {
    std::vector<char> ret;
    char buf[1024];
    DWORD actual {};
    while (ReadFile(h, buf, sizeof(buf), &actual, nullptr) && actual > 0) {
        ret.insert(ret.end(), buf, buf + actual);
    }
    return ret;
}

static std::string replaceAll(std::string str, const std::string &from, const std::string &to) {
    size_t pos {};
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str = str.replace(pos, from.size(), to);
        pos += to.size();
    }
    return str;
}

static std::string stripTrailing(std::string str) {
    while (!str.empty() && isspace(str.back())) {
        str.pop_back();
    }
    return str;
}

static void invalid_arg(const char *arg)
{
    fatal("error: the %s option requires a non-empty string argument\n", arg);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    cygwin_internal(CW_SYNC_WINENV);
    g_wakeupFd = new WakeupFd();

    if (argc == 3 && !strcmp(argv[1], "--press-return"))
        return handlePressReturn(argv[2]);

    Environment env;
    std::string distroName;
    std::string customBackendPath;
    std::string winDir;
    std::string wslDir;
    std::string userName;
    enum class LoginMode { Auto, Yes, No } loginMode = LoginMode::Auto;

    int debugFork = 0;
    int c = 0;
    if (argv[0][0] == '-') {
        loginMode = LoginMode::Yes;
    }

    const char shortopts[] = "+b:d:e:hlLu:w:W:";
    const struct option longopts[] = {
        { "backend",        required_argument,  nullptr,     'b' },
        { "distribution",   required_argument,  nullptr,     'd' },
        { "help",           no_argument,        nullptr,     'h' },
        { "debug-fork",     no_argument,       &debugFork,    1  },
        { "no-login",       no_argument,        nullptr,     'L' },
        { "user",           required_argument,  nullptr,     'u' },
        { "windir",         required_argument,  nullptr,     'w' },
        { "wsldir",         required_argument,  nullptr,     'W' },
        { nullptr,          no_argument,        nullptr,      0  },
    };

    while ((c = getopt_long(argc, argv, shortopts, longopts, nullptr)) != -1)
    {
        switch (c)
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
                loginMode = LoginMode::Yes;
                break;
            case 'L':
                loginMode = LoginMode::No;
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

    /*
    const bool hasCommand = optind < argc;
    if (loginMode == LoginMode::Auto) {
        loginMode = hasCommand ? LoginMode::No : LoginMode::Yes;
    }
    */

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
    wslCmdLine.append(L"\"$(wslpath -u");
    appendWslArg(wslCmdLine, backendPathWin);
    wslCmdLine.append(L")\"");

    if (debugFork)
        appendWslArg(wslCmdLine, L"--debug-fork");

    if (!wslDir.empty())
    {
        appendWslArg(wslCmdLine, L"-C");
        appendWslArg(wslCmdLine, mbsToWcs(wslDir));
    }

    std::array<wchar_t, 1024> buffer;
    int iRet = swprintf(buffer.data(), buffer.size(),
                        L" -3%d -0%d -1%d -c%d -r%d -w%d -t%d",
                        controlSocket.port(),
                        inputSocket.port(),
                        outputSocket.port(),
                        initialSize.cols,
                        initialSize.rows,
                        kOutputWindowSize,
                        kOutputWindowSize / 4);
    assert(iRet > 0);
    wslCmdLine.append(buffer.data());

    if (loginMode == LoginMode::Yes)
        appendWslArg(wslCmdLine, L"-l");

    for (const auto &envPair : env.pairs())
        appendWslArg(wslCmdLine, L"-e" + envPair.first + L"=" + envPair.second);

    appendWslArg(wslCmdLine, L"--");
    for (int i = optind; i < argc; ++i)
        appendWslArg(wslCmdLine, mbsToWcs(argv[i]));

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
    STARTUPINFOEXW sui {};
    sui.StartupInfo.cb = sizeof(sui);
    StartupInfoAttributeList attrList { sui.lpAttributeList, 1 };
    StartupInfoInheritList inheritList { sui.lpAttributeList,
        { outputPipe.wh, errorPipe.wh }
    };

    sui.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
    sui.StartupInfo.hStdOutput = outputPipe.wh;
    sui.StartupInfo.hStdError = errorPipe.wh;

    PROCESS_INFORMATION pi = {};
    BOOL success = CreateProcessW(wslPath.c_str(), &cmdLine[0], nullptr, nullptr,
        true,
        debugFork ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW,
        nullptr, nullptr, &sui.StartupInfo, &pi);
    if (!success) {
        fatal("error starting wsl.exe adapter: %s\n",
            formatErrorMessage(GetLastError()).c_str());
    }

    CloseHandle(outputPipe.wh);
    CloseHandle(errorPipe.wh);
    success = SetHandleInformation(pi.hProcess, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    assert(success && "SetHandleInformation failed");
    spawnPressReturnProcess(pi.hProcess);

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
    return 0;
}
