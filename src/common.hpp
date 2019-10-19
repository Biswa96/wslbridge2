/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019 Biswapriyo Nath.
 */

#ifndef COMMON_HPP
#define COMMON_HPP

void fatal(const char *fmt, ...)
    __attribute__((noreturn))
    __attribute__((format(printf, 1, 2)));
void fatalv(const char *fmt, va_list ap) __attribute__((noreturn));
void fatalPerror(const char *msg) __attribute__((noreturn));

#endif /* COMMON_HPP */
