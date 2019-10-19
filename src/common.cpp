/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019 Biswapriyo Nath.
 */

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

void fatalv(const char *fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
    fflush(stdout);
    fflush(stderr);
    /* Avoid calling exit, which would call global destructors */
    _exit(1);
}

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fatalv(fmt, ap);
    va_end(ap);
}

void fatalPerror(const char *msg)
{
    perror(msg);
    fflush(stdout);
    fflush(stderr);
    /* Avoid calling exit, which would call global destructors */
    _exit(1);
}
