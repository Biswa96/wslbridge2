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

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <pty.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "SocketIo.hpp"

namespace {

static int connectSocket(int port) {
    const int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    const int flag = 1;
    const int nodelayRet = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    assert(nodelayRet == 0);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int connectRet = connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    assert(connectRet == 0);

    return s;
}

static std::string resolveCwd(const std::string &cwd) {
    if (cwd == "~" || cwd.substr(0, 2) == "~/") {
        const auto home = getenv("HOME") ?: "";
        return home + cwd.substr(1);
    }
    return cwd;
}

struct ChildParams {
    int cols = -1;
    int rows = -1;
    std::vector<char*> env;
    std::string prog;
    std::vector<char*> argv;
    std::string cwd;
};

struct Child {
    SpawnError spawnError = {};
    pid_t pid = -1;
    int masterFd = -1;
    int inputFd = -1;
    int outputFd = -1;
};

class UniqueFd {
    int fd_;
public:
    int fd() const { return fd_; }
    UniqueFd() : fd_(-1) {}
    explicit UniqueFd(int fd) : fd_(fd) {}
    ~UniqueFd() { close(); }
    int release() {
        const int ret = fd_;
        fd_ = -1;
        return ret;
    }
    void close() {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    UniqueFd(const UniqueFd &other) = delete;
    UniqueFd &operator=(const UniqueFd &other) = delete;
    UniqueFd(UniqueFd &&other) : fd_(other.release()) {}
    UniqueFd &operator=(UniqueFd &&other) {
        close();
        fd_ = other.release();
        return *this;
    }
};

struct PipePair {
    UniqueFd read;
    UniqueFd write;
};

static PipePair makePipePair(int oflags) {
    int vals[2];
    if (pipe2(vals, oflags) != 0) {
        fatalPerror("error: pipe2 failed");
    }
    return PipePair {
        UniqueFd(vals[0]),
        UniqueFd(vals[1])
    };
}

static Child spawnChild(const ChildParams &params) {
    assert(params.argv.size() >= 2);
    assert(params.argv.back() == nullptr);

    winsize ws = {};
    ws.ws_col = params.cols;
    ws.ws_row = params.rows;

    PipePair spawnErrPipe = makePipePair(O_CLOEXEC);

    int masterFdRaw = -1;
    const pid_t pid = forkpty(&masterFdRaw, nullptr, nullptr, &ws);
    if (pid == static_cast<pid_t>(-1)) {
        // forkpty failed
        const SpawnError err = {
            SpawnError::Type::ForkPtyFailed,
            bridgedError(errno)
        };
        Child ret;
        ret.spawnError = err;
        return ret;

    } else if (pid == 0) {
        // forked process
        const auto childFailed = [&](SpawnError::Type type, int savedErrno) {
            const SpawnError err = {
                type,
                bridgedError(savedErrno)
            };
            writeAllRestarting(spawnErrPipe.write.fd(), &err, sizeof(err));
            _exit(1);
        };
        spawnErrPipe.read.close();
        for (const auto &setting : params.env) {
            putenv(setting);
        }
        if (!params.cwd.empty()) {
            if (chdir(resolveCwd(params.cwd).c_str()) != 0) {
                childFailed(SpawnError::Type::ChdirFailed, errno);
            }
        }
        execvp(params.prog.c_str(), params.argv.data());
        childFailed(SpawnError::Type::ExecFailed, errno);
    }

    UniqueFd masterFd(masterFdRaw);

    spawnErrPipe.write.close();

    SpawnError err = {};
    if (readAllRestarting(spawnErrPipe.read.fd(), &err, sizeof(err))) {
        // The child exec call failed.
        int dummy = 0;
        waitpid(pid, &dummy, 0);
        Child ret;
        ret.spawnError = err;
        return ret;
    }

    Child ret;
    ret.spawnError = SpawnError { SpawnError::Type::Success, bridgedError(0) };
    ret.pid = pid;
    ret.masterFd = masterFd.release();
    ret.inputFd = ret.masterFd;
    ret.outputFd = ret.masterFd;

    return ret;
}

struct ChannelWindow {
    std::mutex mutex;
    std::condition_variable increaseCV;
    std::atomic<int32_t> increaseAmt = {0};
};

struct IoLoop {
    int controlSocketFd = -1;
    int childFd = -1;
    WindowParams windowParams = {};
    ChannelWindow outputWindow;
    ChannelWindow errorWindow;
    struct {
        pthread_t thread;
        int pipeFd = -1;
        int socketFd = -1;
    } stdoutAutoClose;
};

static void connectionBrokenAbort() {
    fatal("error: connection broken\n");
}

// Atomically replace the FD with /dev/null.
// http://www.drdobbs.com/parallel/file-descriptors-and-multithreaded-progr/212001285
static void revokeFd(int fd) {
    const int nullFd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullFd < 0) {
        fatalPerror("revokeFd: could not open /dev/null");
    }
    if (dup2(nullFd, fd) < 0) {
        fatalPerror("revokeFd: dup2 failed");
    }
    close(nullFd);
}

static void writePacket(int controlSocketFd, const Packet &p) {
    assert(p.size >= sizeof(p));
    if (!writeAllRestarting(controlSocketFd,
            reinterpret_cast<const char*>(&p), p.size)) {
        connectionBrokenAbort();
    }
}

static void socketToChildThread(IoLoop *ioloop, int socketFd, int outputFd) {
    std::array<char, 8192> buf;
    while (true) {
        const ssize_t amt1 = readRestarting(socketFd, buf.data(), buf.size());
        if (amt1 <= 0) {
            break;
        }
        if (!writeAllRestarting(outputFd, buf.data(), amt1)) {
            break;
        }
    }
    revokeFd(socketFd);
}

static void childToSocketThread(IoLoop *ioloop, bool isErrorPipe, int inputFd, int socketFd) {
    ChannelWindow &window = isErrorPipe ? ioloop->errorWindow : ioloop->outputWindow;
    const auto windowThreshold = ioloop->windowParams.threshold;
    const auto windowSize = ioloop->windowParams.size;
    std::array<char, 32 * 1024> buf;
    int32_t locWindow = windowSize;
    const auto hasWindow = [&](bool readAtomic = true) -> bool {
        if (readAtomic) {
            const int32_t iw = window.increaseAmt.exchange(0);
            assert(iw <= windowSize - locWindow);
            locWindow += iw;
        }
        return locWindow >= windowThreshold;
    };
    while (true) {
        assert(locWindow >= 0 && locWindow <= windowSize);
        if (!hasWindow(false) && !hasWindow()) {
            std::unique_lock<std::mutex> lock(window.mutex);
            window.increaseCV.wait(lock, hasWindow);
        }
        const ssize_t amt1 =
            readRestarting(inputFd, buf.data(),
                std::min<size_t>(buf.size(), locWindow));
        if (amt1 <= 0) {
            break;
        }
        if (!writeAllRestarting(socketFd, buf.data(), amt1)) {
            break;
        }
        locWindow -= amt1;
    }
    // The pty has closed.  Shutdown I/O on the data socket to signal
    // I/O completion to the frontend.
    revokeFd(socketFd);
}

static void discardPacket(void*, const Packet&) {
    // Do nothing.
}

static void handlePacket(IoLoop *ioloop, const Packet &p) {
    switch (p.type) {
        case Packet::Type::SetSize: {
            winsize ws = {};
            ws.ws_col = p.u.termSize.cols;
            ws.ws_row = p.u.termSize.rows;
            if (ioloop->childFd != -1) {
                ioctl(ioloop->childFd, TIOCSWINSZ, &ws);
            }
            break;
        }
        case Packet::Type::IncreaseWindow: {
            ChannelWindow &window =
                p.u.window.isErrorPipe ?
                    ioloop->errorWindow : ioloop->outputWindow;
            {
                // Read ioloop->window into cw once to ensure a stable value.
                const int32_t max = ioloop->windowParams.size;
                const int32_t cw = window.increaseAmt;
                const int32_t iw = p.u.window.amount;
                assert(cw >= 0 && cw <= max &&
                       iw >= 0 && iw <= max - cw);
                std::lock_guard<std::mutex> lock(window.mutex);
                window.increaseAmt += iw;
            }
            window.increaseCV.notify_one();
            break;
        }
        case Packet::Type::CloseStdoutPipe: {
            // Shut down child->socket stdout I/O.  This code is insufficent
            // to kill the thread, because it could be blocked waiting for
            // bytes in its window.  It *is* sufficient to close the read-end
            // of the child stdout pipe and kill any in-progress syscall.
            revokeFd(ioloop->stdoutAutoClose.pipeFd);
            revokeFd(ioloop->stdoutAutoClose.socketFd);
            pthread_kill(ioloop->stdoutAutoClose.thread, SIGUSR1);
            break;
        }
        default: {
            fatal("internal error: unexpected packet %d\n",
                static_cast<int>(p.type));
        }
    }
}

static void mainLoop(int controlSocketFd,
                     int inputSocketFd, int outputSocketFd,
                     const char *exe, Child child, WindowParams windowParams) {
    if (child.spawnError.type == SpawnError::Type::Success) {
        IoLoop ioloop;
        ioloop.controlSocketFd = controlSocketFd;
        ioloop.childFd = child.masterFd;
        ioloop.windowParams = windowParams;

        std::thread s2c(socketToChildThread, &ioloop, inputSocketFd, child.inputFd);
        std::thread c2s(childToSocketThread, &ioloop, false,
                        child.outputFd, outputSocketFd);

        // handlePacket needs stdoutThread so it can propagate stdout closing
        // from the frontend to the child's stdout pipe.
        ioloop.stdoutAutoClose.thread = c2s.native_handle();
        ioloop.stdoutAutoClose.pipeFd = child.outputFd;
        ioloop.stdoutAutoClose.socketFd = outputSocketFd;

        std::thread rcs(
            readControlSocketThread<IoLoop, handlePacket, connectionBrokenAbort>,
            controlSocketFd, &ioloop);

        // Block until the child process finishes, then notify the frontend of
        // child exit.
        int exitStatus = 0;
        if (waitpid(child.pid, &exitStatus, 0) != child.pid) {
            fatalPerror("waitpid failed");
        }
        if (WIFEXITED(exitStatus)) {
            exitStatus = WEXITSTATUS(exitStatus);
        } else {
            // XXX: I'm just making something up here.  I've got
            // no idea whether this makes sense.
            exitStatus = 1;
        }
        Packet p = { sizeof(Packet), Packet::Type::ChildExitStatus };
        p.u.exitStatus = exitStatus;
        writePacket(controlSocketFd, p);

        // Ensure that the parent thread outlives its child threads.  The program
        // should exit before all the worker threads finish.  Join rcs first, so
        // that ioloop.stdoutThread remains valid if handlePacket is called.
        //
        // The rcs thread doesn't end gracefully -- when the control socket
        // reaches EOF, the backend process terminates.
        rcs.join();
        s2c.join();
        c2s.join();
    } else {
        PacketSpawnFailed p = {};
        p.size = sizeof(p);
        p.type = Packet::Type::SpawnFailed;
        p.u.spawnError = child.spawnError;
        snprintf(p.exe, sizeof(p.exe), "%s", exe);
        writePacket(controlSocketFd, p);

        // Keep the backend alive until the control socket closes.
        readControlSocketThread<void, discardPacket, connectionBrokenAbort>(
            controlSocketFd, nullptr);
    }
}

template <typename T>
void optionRequired(const char *opt, const T &val, const T &unset) {
    if (val == unset) {
        fatal("error: option '%s' is missing\n", opt);
    }
}

template <typename T>
void optionNotAllowed(const char *opt, const char *why, const T &val, const T &unset) {
    if (val != unset) {
        fatal("error: option '%s' is not allowed%s\n", opt, why);
    }
}

} // namespace

int main(int argc, char *argv[]) {

    // If the backend crashes, it prints a message to its stderr, which is a
    // hidden console, and immediately exits.  We can show the console, but we
    // need to keep the process around to see the error.  To do this, have a
    // mode where the backend immediately forks itself, and the child does the
    // real work.  The parent just sticks around for a while.
    if (argc >= 2 && !strcmp(argv[1], "--debug-fork")) {
        pid_t child = fork();
        if (child != 0) {
            sleep(3600);
            return 0;
        }
    }

    int controlSocketPort = -1;
    int inputSocketPort = -1;
    int outputSocketPort = -1;
    int windowSize = -1;
    int windowThreshold = -1;
    ChildParams childParams;
    bool loginMode = false;

    const char shortopts[] = "+3:0:1:c:r:w:t:e:C:l";
    const struct option longopts[] = {
        // This debugging option is handled earlier.  Include it in this table
        // just to discard it.
        { "cols",       required_argument, 0, 'c' },
        { "debug-fork", no_argument,       0,  0  },
        { "login",      no_argument,       0, 'l' },
        { "rows",       required_argument, 0, 'r' },
        { 0,            no_argument,       0,  0  },
    };

    int ch = 0;
    while ((ch = getopt_long(argc, argv, shortopts, longopts, nullptr)) != -1)
    {
        switch (ch)
        {
            case '3': controlSocketPort = atoi(optarg); break;
            case '0': inputSocketPort = atoi(optarg); break;
            case '1': outputSocketPort = atoi(optarg); break;
            case 'c': childParams.cols = atoi(optarg); break;
            case 'r': childParams.rows = atoi(optarg); break;
            case 'w': windowSize = atoi(optarg); break;
            case 't': windowThreshold = atoi(optarg); break;
            case 'e': childParams.env.push_back(strdup(optarg)); break;
            case 'C': childParams.cwd = optarg; break;
            case 'l': loginMode = true; break;
            default:
                exit(1);
        }
    }

    for (int i = optind; i < argc; ++i) {
        childParams.argv.push_back(argv[i]);
    }
    if (childParams.argv.empty()) {
        const char *shell = "/bin/sh";
#ifdef use_getpwuid
        struct passwd *pw = getpwuid(getuid());
        if (pw == nullptr) {
            fatalPerror("error: getpwuid failed");
        } else if (pw->pw_shell == nullptr) {
            fatal("error: getpwuid(...)->pw_shell is NULL\n");
        } else {
            shell = pw->pw_shell;
        }
#else
        if (getenv("SHELL"))
          shell = getenv("SHELL");
#endif
        childParams.argv.push_back(strdup(shell));
    }
    // XXX: Replace char* args/envstrings with std::string?
    childParams.prog = childParams.argv[0];
    if (loginMode) {
        std::string argv0 = childParams.argv[0];
        const auto pos = argv0.find_last_of('/');
        if (pos != std::string::npos) {
            argv0 = argv0.substr(pos + 1);
        }
        argv0 = '-' + argv0;
        childParams.argv[0] = strdup(argv0.c_str());
    }
    childParams.argv.push_back(nullptr);

    const WindowParams windowParams = { windowSize, windowThreshold };
    assert(windowParams.size >= 1);
    assert(windowParams.threshold >= 1);
    assert(windowParams.threshold <= windowParams.size);

    const int controlSocket = connectSocket(controlSocketPort);
    const int inputSocket = connectSocket(inputSocketPort);
    const int outputSocket = connectSocket(outputSocketPort);

    const auto child = spawnChild(childParams);

    // We must not register signal handlers until *after* spawning the child.
    // It will inherit at least any SIG_IGN settings.
    //
    // We want to handle EPIPE rather than receiving SIGPIPE.
    signal(SIGPIPE, SIG_IGN);
    // Register a do-nothing SIGUSR1 handler that can be used to interrupt
    // threads.  On Linux (and WSL), simply closing a file descriptor (whether
    // via close or dup2), doesn't interrupt blocking I/O syscalls.  We need to
    // fire a signal after closing the FDs.
    struct sigaction sa = {};
    sa.sa_handler = [](int signo) {};
    sigaction(SIGUSR1, &sa, nullptr);

    mainLoop(controlSocket,
             inputSocket, outputSocket,
             childParams.prog.c_str(), child, windowParams);

    return 0;
}
