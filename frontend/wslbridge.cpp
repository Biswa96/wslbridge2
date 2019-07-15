/*
 * The MIT License (MIT)
 * Copyright (c) 2016 Ryan Prichard
 * Copyright (c) 2017-2018 Google LLC
 */

/*
 * GNU GENERAL PUBLIC LICENSE Version 3 (GNU GPL v3)
 * Copyright (c) 2019 Biswapriyo Nath
 */

#include <windows.h>

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/cygwin.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../common/SocketIo.hpp"
#include "LocalSock.hpp"
#include "TerminalState.hpp"

#define BACKEND_PROGRAM "wslbridge-backend"

namespace {

const int32_t kOutputWindowSize = 8192;

static WakeupFd *g_wakeupFd = nullptr;

static TermSize terminalSize() {
    winsize ws = {};
    if (isatty(STDIN_FILENO) && ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        return TermSize { ws.ws_col, ws.ws_row };
    } else {
        return TermSize { 80, 24 };
    }
}

static std::wstring mbsToWcs(const std::string &s) {
    const size_t len = mbstowcs(nullptr, s.c_str(), 0);
    if (len == static_cast<size_t>(-1)) {
        fatal("error: mbsToWcs: invalid string\n");
    }
    std::wstring ret;
    ret.resize(len);
    const size_t len2 = mbstowcs(&ret[0], s.c_str(), len);
    assert(len == len2);
    return ret;
}

static std::string wcsToMbs(const std::wstring &s, bool emptyOnError=false) {
    const size_t len = wcstombs(nullptr, s.c_str(), 0);
    if (len == static_cast<size_t>(-1)) {
        if (emptyOnError) {
            return {};
        }
        fatal("error: wcsToMbs: invalid string\n");
    }
    std::string ret;
    ret.resize(len);
    const size_t len2 = wcstombs(&ret[0], s.c_str(), len);
    assert(len == len2);
    return ret;
}

static TerminalState g_terminalState;

struct IoLoop {
    std::string spawnCwd;
    bool usePty = false;
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
            if (!ioloop->usePty && !isErrorPipe) {
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
                     bool usePty, int controlSocketFd,
                     int inputSocketFd, int outputSocketFd, int errorSocketFd,
                     TermSize termSize) {
    IoLoop ioloop;
    ioloop.spawnCwd = spawnCwd;
    ioloop.usePty = usePty;
    ioloop.controlSocketFd = controlSocketFd;
    std::thread p2s(parentToSocketThread, inputSocketFd);
    std::thread s2p(socketToParentThread, &ioloop, false, outputSocketFd, STDOUT_FILENO);
    std::unique_ptr<std::thread> es2p;
    if (errorSocketFd != -1) {
        es2p = std::unique_ptr<std::thread>(
            new std::thread(socketToParentThread, &ioloop, true, errorSocketFd, STDERR_FILENO));
    }
    std::thread rcs(readControlSocketThread<IoLoop, handlePacket, fatalConnectionBroken>,
                    controlSocketFd, &ioloop);
    int32_t exitStatus = -1;

    while (true) {
        g_wakeupFd->wait();
        const auto newSize = terminalSize();
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

static bool pathExists(const std::wstring &path) {
    return GetFileAttributesW(path.c_str()) != 0xFFFFFFFF;
}

static std::wstring dirname(const std::wstring &path) {
    std::wstring::size_type pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L"";
    } else {
        return path.substr(0, pos);
    }
}

static HMODULE getCurrentModule() {
    HMODULE module;
    if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(getCurrentModule),
                &module)) {
        fatal("error: GetModuleHandleEx failed\n");
    }
    return module;
}

static std::wstring getModuleFileName(HMODULE module) {
    const int bufsize = 4096;
    wchar_t path[bufsize];
    int size = GetModuleFileNameW(module, path, bufsize);
    assert(size != 0 && size != bufsize);
    return std::wstring(path);
}

static std::wstring findBackendProgram(const std::string &customBackendPath) {
    std::wstring ret;
    if (!customBackendPath.empty()) {
        char *winPath = static_cast<char*>(
            cygwin_create_path(CCP_POSIX_TO_WIN_A, customBackendPath.c_str()));
        if (winPath == nullptr) {
            fatalPerror(("error: bad path: '" + customBackendPath + "'").c_str());
        }
        ret = mbsToWcs(winPath);
        free(winPath);
    } else {
        const auto progDir = dirname(getModuleFileName(getCurrentModule()));
        ret = progDir + (L"\\" BACKEND_PROGRAM);
    }
    if (!pathExists(ret)) {
        fatal("error: '%s' backend program is missing\n",
            wcsToMbs(ret).c_str());
    }
    return ret;
}

static wchar_t lowerDrive(wchar_t ch) {
    if (ch >= L'a' && ch <= L'z') {
        return ch;
    } else if (ch >= L'A' && ch <= 'Z') {
        return ch - L'A' + L'a';
    } else {
        return L'\0';
    }
}

static std::pair<std::wstring, std::wstring>
normalizePath(const std::wstring &path) {
    const auto getFinalPathName = [&](HANDLE h) -> std::wstring {
        std::wstring ret;
        ret.resize(MAX_PATH + 1);
        while (true) {
            const auto sz = GetFinalPathNameByHandleW(h, &ret[0], ret.size(), 0);
            if (sz == 0) {
                fatal("error: GetFinalPathNameByHandle failed on '%s'\n",
                    wcsToMbs(path).c_str());
            } else if (sz < ret.size()) {
                ret.resize(sz);
                return ret;
            } else {
                assert(sz > ret.size());
                ret.resize(sz);
            }
        }
    };
    const auto h = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        fatal("error: could not open '%s'\n", wcsToMbs(path).c_str());
    }
    auto npath = getFinalPathName(h);
    std::array<wchar_t, MAX_PATH + 1> fsname;
    fsname.back() = L'\0';
    if (!GetVolumeInformationByHandleW(
            h, nullptr, 0, nullptr, nullptr, nullptr,
            &fsname[0], fsname.size())) {
        fsname[0] = L'\0';
    }
    CloseHandle(h);
    // Example of GetFinalPathNameByHandle result:
    //   \\?\C:\cygwin64\bin\wslbridge-backend
    //   0123456
    //   \\?\UNC\server\share\file
    //   01234567
    if (npath.size() >= 7 &&
            npath.substr(0, 4) == L"\\\\?\\" &&
            lowerDrive(npath[4]) &&
            npath.substr(5, 2) == L":\\") {
        // Strip off the atypical \\?\ prefix.
        npath = npath.substr(4);
    } else if (npath.substr(0, 8) == L"\\\\?\\UNC\\") {
        // Strip off the \\\\?\\UNC\\ prefix and replace it with \\.
        npath = L"\\\\" + npath.substr(8);
    }
    return std::make_pair(std::move(npath), fsname.data());
}

static std::wstring findSystemProgram(const wchar_t *name) {
    std::array<wchar_t, MAX_PATH> windir;
    windir[0] = L'\0';
    if (GetWindowsDirectoryW(windir.data(), windir.size()) == 0) {
        fatal("error: GetWindowsDirectory call failed\n");
    }
    const wchar_t *const kPart32 = L"\\System32\\";
    const auto path = [&](const wchar_t *part) -> std::wstring {
        return std::wstring(windir.data()) + part + name;
    };

    const auto ret = path(kPart32);
    if (pathExists(ret)) {
        return ret;
    } else {
        fatal("error: '%s' does not exist\n"
              "note: Ubuntu-on-Windows must be installed\n",
              wcsToMbs(ret).c_str());
    }
}

static void usage(const char *prog) {
    printf("Usage: %s [options] [--] [command]...\n", prog);
    printf("Runs a program within a Windows Subsystem for Linux (WSL) pty\n");
    printf("\n");
    printf("Options:\n");
    printf("  -C WSLDIR     Changes the working directory to WSLDIR first.\n");
    printf("                An initial '~' indicates the WSL home directory.\n");
    printf("  -e VAR        Copies VAR into the WSL environment.\n");
    printf("  -e VAR=VAL    Sets VAR to VAL in the WSL environment.\n");
    printf("  -l            Start a login shell.\n");
    printf("  --no-login    Do not start a login shell.\n");
    printf("  -T            Do not use a pty.\n");
    printf("  -t            Use a pty (as long as stdin is a tty).\n");
    printf("  -t -t         Force a pty (even if stdin is not a tty).\n");
    printf("  --distro      Distribution Name.\n");
    printf("                Run the specified distribution.\n");
    printf("  --backend BACKEND\n");
    printf("                Overrides the default path to wslbridge-backend. BACKEND is a\n");
    printf("                Cygwin-style path (not a WSL path).\n");
    exit(0);
}

class Environment {
public:
    void set(const std::string &var) {
        const char *value = getenv(var.c_str());
        if (value != nullptr) {
            set(var, value);
        }
    }

    void set(const std::string &var, const std::string &value) {
        pairs_.push_back(std::make_pair(mbsToWcs(var), mbsToWcs(value)));
    }

    bool hasVar(const std::wstring &var) {
        for (const auto &pair : pairs_) {
            if (pair.first == var) {
                return true;
            }
        }
        return false;
    }

    const std::vector<std::pair<std::wstring, std::wstring>> &pairs() { return pairs_; }

private:
    std::vector<std::pair<std::wstring, std::wstring>> pairs_;
};

static void appendWslArg(std::wstring &out, const std::wstring &arg) {
    if (!out.empty()) {
        out.push_back(L' ');
    }
    const auto isCharSafe = [](wchar_t ch) -> bool {
        switch (ch) {
            case L'%':
            case L'+':
            case L',':
            case L'-':
            case L'.':
            case L'/':
            case L':':
            case L'=':
            case L'@':
            case L'_':
            case L'{':
            case L'}':
                return true;
            default:
                return (ch >= L'0' && ch <= L'9') ||
                       (ch >= L'a' && ch <= L'z') ||
                       (ch >= L'A' && ch <= L'Z');
        }
    };
    if (arg.empty()) {
        out.append(L"''");
        return;
    }
    if (std::all_of(arg.begin(), arg.end(), isCharSafe)) {
        out.append(arg);
        return;
    }
    bool inQuote = false;
    const auto enterQuote = [&](bool newInQuote) {
        if (inQuote != newInQuote) {
            out.push_back(L'\'');
            inQuote = newInQuote;
        }
    };
    enterQuote(true);
    for (auto ch : arg) {
        if (ch == L'\'') {
            enterQuote(false);
            out.append(L"\\'");
            enterQuote(true);
        } else if (isCharSafe(ch)) {
            out.push_back(ch);
        } else {
            out.push_back(ch);
        }
    }
    enterQuote(false);
}

static std::string errorMessageToString(DWORD err) {
    // Use FormatMessageW rather than FormatMessageA, because we want to use
    // wcstombs to convert to the Cygwin locale, which might not match the
    // codepage FormatMessageA would use.  We need to convert using wcstombs,
    // rather than print using %ls, because %ls doesn't work in the original
    // MSYS.
    wchar_t *wideMsgPtr = NULL;
    const DWORD formatRet = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&wideMsgPtr),
        0,
        NULL);
    if (formatRet == 0 || wideMsgPtr == NULL) {
        return std::string();
    }
    std::string msg = wcsToMbs(wideMsgPtr);
    LocalFree(wideMsgPtr);
    const size_t pos = msg.find_last_not_of(" \r\n\t");
    if (pos == std::string::npos) {
        msg.clear();
    } else {
        msg.erase(pos + 1);
    }
    return msg;
}

static std::string formatErrorMessage(DWORD err) {
    char buf[64];
    sprintf(buf, "error %#x", static_cast<unsigned int>(err));
    std::string ret = errorMessageToString(err);
    if (ret.empty()) {
        ret += buf;
    } else {
        ret += " (";
        ret += buf;
        ret += ")";
    }
    return ret;
}

struct PipeHandles {
    HANDLE rh;
    HANDLE wh;
};

static PipeHandles createPipe() {
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
static void spawnPressReturnProcess(HANDLE wslProcess) {
    const auto exePath = getModuleFileName(getCurrentModule());
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

} // namespace

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
    cygwin_internal(CW_SYNC_WINENV);
    g_wakeupFd = new WakeupFd();

    if (argc == 3 && !strcmp(argv[1], "--press-return")) {
        return handlePressReturn(argv[2]);
    }

    Environment env;
    std::string spawnCwd;
    std::string distroName;
    std::string customBackendPath;
    enum class TtyRequest { Auto, Yes, No, Force } ttyRequest = TtyRequest::Auto;
    enum class LoginMode { Auto, Yes, No } loginMode = LoginMode::Auto;

    int debugFork = 0;
    int c = 0;
    if (argv[0][0] == '-') {
        loginMode = LoginMode::Yes;
    }
    const struct option kOptionTable[] = {
        { "help",           false, nullptr,     'h' },
        { "debug-fork",     false, &debugFork,  1   },
        { "distro",         true,  nullptr,     'd' },
        { "no-login",       false, nullptr,     'L' },
        { "backend",        true,  nullptr,     'b' },
        { nullptr,          false, nullptr,     0   },
    };
    while ((c = getopt_long(argc, argv, "+b:C:d:e:hlLtT", kOptionTable, nullptr)) != -1) {
        switch (c) {
            case 0:
                // Ignore long option.
                break;
            case 'e': {
                const char *eq = strchr(optarg, '=');
                const auto varname = eq ? std::string(optarg, eq - optarg) : std::string(optarg);
                if (varname.empty()) {
                    fatal("error: -e variable name cannot be empty: '%s'\n", optarg);
                }
                if (eq) {
                    env.set(varname, eq + 1);
                } else {
                    env.set(varname);
                }
                break;
            }
            case 'C':
                spawnCwd = optarg;
                if (spawnCwd.empty()) {
                    fatal("error: the -C option requires a non-empty string argument\n");
                }
                break;
            case 'h':
                usage(argv[0]);
                break;
            case 't':
                if (ttyRequest == TtyRequest::Yes) {
                    ttyRequest = TtyRequest::Force;
                } else {
                    ttyRequest = TtyRequest::Yes;
                }
                break;
            case 'T':
                ttyRequest = TtyRequest::No;
                break;
            case 'd':
                distroName = optarg;
                if (distroName.empty())
                    fatal("error: the --distro argument '%s' is invalid\n", optarg);
                break;
            case 'l':
                loginMode = LoginMode::Yes;
                break;
            case 'L':
                loginMode = LoginMode::No;
                break;
            case 'b':
                customBackendPath = optarg;
                if (customBackendPath.empty()) {
                    fatal("error: the --backend option requires a non-empty string argument\n");
                }
                break;
            default:
                fatal("Try '%s --help' for more information.\n", argv[0]);
        }
    }

    const bool hasCommand = optind < argc;
    if (loginMode == LoginMode::Auto) {
        loginMode = hasCommand ? LoginMode::No : LoginMode::Yes;
    }
    if (ttyRequest == TtyRequest::Auto) {
        ttyRequest = loginMode == LoginMode::No ? TtyRequest::No : TtyRequest::Yes;
    }
    if (ttyRequest == TtyRequest::Yes && !isatty(STDIN_FILENO)) {
        fprintf(stderr, "Pseudo-terminal will not be allocated because stdin is not a terminal.\n");
        ttyRequest = TtyRequest::No;
    }
    const bool usePty = ttyRequest != TtyRequest::No;

    if (!env.hasVar(L"TERM")) {
        // This seems to be what OpenSSH is doing.
        if (usePty) {
            const char *termVal = getenv("TERM");
            env.set("TERM", termVal && *termVal ? termVal : "dumb");
        } else {
            env.set("TERM", "dumb");
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
    std::unique_ptr<LocalSock> errorSocket;
    if (!usePty) {
        errorSocket = std::unique_ptr<LocalSock>(new LocalSock);
    }

    const auto wslPath = findSystemProgram(L"wsl.exe");
    const auto backendPathInfo = normalizePath(findBackendProgram(customBackendPath));
    const auto backendPathWin = backendPathInfo.first;
    const auto fsname = backendPathInfo.second;
    const auto initialSize = terminalSize();

    // Prepare the backend command line.
    std::wstring wslCmdLine;
    wslCmdLine.append(L"\"$(wslpath -u");
    appendWslArg(wslCmdLine, backendPathWin);
    wslCmdLine.append(L")\"");

    if (debugFork) {
        appendWslArg(wslCmdLine, L"--debug-fork");
    }

    std::array<wchar_t, 1024> buffer;
    int iRet = swprintf(buffer.data(), buffer.size(),
                        L" -3%d -0%d -1%d -w%d -t%d",
                        controlSocket.port(),
                        inputSocket.port(),
                        outputSocket.port(),
                        kOutputWindowSize,
                        kOutputWindowSize / 4);
    assert(iRet > 0);
    wslCmdLine.append(buffer.data());

    if (usePty) {
        iRet = swprintf(buffer.data(), buffer.size(),
                        L" --pty -c%d -r%d",
                        initialSize.cols,
                        initialSize.rows);
    } else {
        iRet = swprintf(buffer.data(), buffer.size(),
                        L" --pipes -2%d",
                        errorSocket->port());
    }
    assert(iRet > 0);
    wslCmdLine.append(buffer.data());

    if (loginMode == LoginMode::Yes) {
        appendWslArg(wslCmdLine, L"-l");
    }
    for (const auto &envPair : env.pairs()) {
        appendWslArg(wslCmdLine, L"-e" + envPair.first + L"=" + envPair.second);
    }
    if (!spawnCwd.empty()) {
        appendWslArg(wslCmdLine, L"-C" + mbsToWcs(spawnCwd));
    }
    appendWslArg(wslCmdLine, L"--");
    for (int i = optind; i < argc; ++i) {
        appendWslArg(wslCmdLine, mbsToWcs(argv[i]));
    }

    std::wstring cmdLine;
    cmdLine.append(L"\"");
    cmdLine.append(wslPath);
    cmdLine.append(L"\"");
    if (!distroName.empty()) {
        cmdLine.append(L" -d ");
        cmdLine.append(mbsToWcs(distroName));
    }
    cmdLine.append(L" bash -c ");
    appendWslArg(cmdLine, wslCmdLine);

    const auto outputPipe = createPipe();
    const auto errorPipe = createPipe();
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
        auto outVec = readAllFromHandle(outputPipe.rh);
        auto errVec = readAllFromHandle(errorPipe.rh);
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
    const int errorSocketC = !errorSocket ? -1 : (*errorSocket).accept();
    controlSocket.close();
    inputSocket.close();
    outputSocket.close();
    if (errorSocket) { errorSocket->close(); }

    if (usePty) {
        g_terminalState.enterRawMode();
    }

    backendStarted = true;

    mainLoop(spawnCwd,
             usePty, controlSocketC,
             inputSocketC, outputSocketC, errorSocketC,
             initialSize);
    return 0;
}
