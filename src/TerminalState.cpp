/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019 Biswapriyo Nath.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "common.hpp"
#include "TerminalState.hpp"

/*
 * Advanced Programming in UNIX Environment 3rd ed.
 * CH. 18th Terminal I/O Section 11. Fig. 18.20.
 * Put the input terminal into non-canonical mode.
 */
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
                fatalPerror("tcgetattr");
            modeValid_[i] = true;
        }
    }

    if (modeValid_[0])
    {
        struct termios termp;
        if (tcgetattr(STDIN_FILENO, &termp) < 0)
            fatalPerror("tcgetattr");

        /*
         * Echo off, canonical mode off, extended input
         * processing off, signal chars off.
         */
        termp.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

        /*
         * No SIGINT on BREAK, CR-to-NL off, input parity
         * check off, don’t strip 8th bit on input, output
         * flow control off.
         */
        termp.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

        /* Clear size bits, parity checking off. */
        termp.c_cflag &= ~(CSIZE | PARENB);

        /* Set 8 bits/char. */
        termp.c_cflag |= CS8;

        /* 1 byte at a time, no timer. */
        termp.c_cc[VMIN] = 1;
        termp.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &termp) < 0)
            fatalPerror("tcsetattr");
    }

    if (modeValid_[1])
    {
        struct termios termp;
        if (tcgetattr(STDOUT_FILENO, &termp) < 0)
            fatalPerror("tcgetattr");

        termp.c_cflag &= ~(CSIZE | PARENB);
        termp.c_cflag |= CS8;

        /* Output processing off. */
        termp.c_oflag &= ~OPOST;
        if (tcsetattr(STDOUT_FILENO, TCSAFLUSH, &termp) < 0)
            fatalPerror("tcsetattr");
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
                fatalPerror("leaveRawMode");
        }
    }
}

/* This function cannot be used from a signal handler. */
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
    /* Avoid calling exit, which would call global destructors */
    _exit(exitStatus);
}
