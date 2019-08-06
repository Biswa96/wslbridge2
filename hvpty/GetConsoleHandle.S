/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

/*
 * GetConsoleHandle: get console handle of attached ConHost process
 * this subroutine does not accpet any parameters
 * this uses NtCurrentTeb() logic with gs segment but for x86_64 only
 */

.global GetConsoleHandle
.text

GetConsoleHandle:
    movq %gs:0x30, %rax     /* TEB */
    movq 0x60(%rax), %rax   /* PEB */
    movq 0x20(%rax), %rax   /* RTL_USER_PROCESS_PARAMETERS */
    movq 0x10(%rax), %rax   /* ConsoleHandle */
    ret
