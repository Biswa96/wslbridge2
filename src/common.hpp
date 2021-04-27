/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2021 Biswapriyo Nath.
 */

#ifndef COMMON_HPP
#define COMMON_HPP

#define WSLBRIDGE2_VERSION v0.8

#define XSTRINGIFY(x) #x
#define STRINGIFY(x) XSTRINGIFY(x)

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif

void fatal(const char *fmt, ...)
    __attribute__((noreturn))
    __attribute__((format(printf, 1, 2)));
void fatalv(const char *fmt, va_list ap) __attribute__((noreturn));
void fatalPerror(const char *msg) __attribute__((noreturn));

#endif /* COMMON_HPP */
