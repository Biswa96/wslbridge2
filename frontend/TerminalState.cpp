/*
 * GNU GENERAL PUBLIC LICENSE Version 3 (GNU GPL v3)
 * Copyright (c) 2019 Biswapriyo Nath
 * This file is part of wslbridge2 project
 */

#include <termios.h>
#include <mutex>

#include "../common/SocketIo.hpp"
#include "TerminalState.hpp"

/* Advanced Programming in UNIX Environment 3rd ed. CH. 18th Terminal I/O */
/* Put the input terminal into non-canonical mode. */
void TerminalState::enterRawMode()
{
    std::lock_guard<std::mutex> lock(mutex_);

    assert(!inRawMode_);
    inRawMode_ = true;

    for (int i = 0; i < 2; ++i)
    {
        if (!isatty(i))
        {
            modeValid_[i] = false;
        }
        else
        {
            if (tcgetattr(i, &mode_[i]) < 0)
                fatalPerror("tcgetattr failed");
            modeValid_[i] = true;
        }
    }

    if (modeValid_[0])
    {
        struct termios termp;
        if (tcgetattr(0, &termp) < 0)
            fatalPerror("tcgetattr failed");

        termp.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        termp.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        termp.c_cflag &= ~(CSIZE | PARENB);
        termp.c_cflag |= CS8;
        termp.c_cc[VMIN] = 1;  // blocking read
        termp.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSAFLUSH, &termp) < 0)
            fatalPerror("tcsetattr failed");
    }

    if (modeValid_[1])
    {
        struct termios termp;
        if (tcgetattr(1, &termp) < 0)
            fatalPerror("tcgetattr failed");

        termp.c_cflag &= ~(CSIZE | PARENB);
        termp.c_cflag |= CS8;
        termp.c_oflag &= ~OPOST;
        if (tcsetattr(1, TCSAFLUSH, &termp) < 0)
            fatalPerror("tcsetattr failed");
    }
}

void TerminalState::leaveRawMode(const std::lock_guard<std::mutex> &lock)
{
    if (!inRawMode_)
        return;

    for (int i = 0; i < 2; ++i)
    {
        if (modeValid_[i])
        {
            if (tcsetattr(i, TCSAFLUSH, &mode_[i]) < 0)
                fatalPerror("error restoring terminal mode");
        }
    }
}

// This function cannot be used from a signal handler.
void TerminalState::fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    this->fatalv(fmt, ap);
    va_end(ap);
}

void TerminalState::fatalv(const char *fmt, va_list ap)
{
    std::lock_guard<std::mutex> lock(mutex_);
    leaveRawMode(lock);
    ::fatalv(fmt, ap);
}

void TerminalState::exitCleanly(int exitStatus)
{
    std::lock_guard<std::mutex> lock(mutex_);
    leaveRawMode(lock);
    fflush(stdout);
    fflush(stderr);
    // Avoid calling exit, which would call global destructors and destruct the
    // WakeupFd object.
    _exit(exitStatus);
}
