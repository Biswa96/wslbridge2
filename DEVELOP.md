<!--
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 *
 * DEVELOP.md: questions and answers for developers and enthusiasts
-->

**Q: How to get Windows IP address in WSL2 quickly?**

**A:** tl;dr, Run `hvpty.exe -e WSL_HOST_IP` command. Windows 10 IPv4 address
will be accessed from `WSL_HOST_IP` environment variable and WSL2 IPv4 address
from `WSL_GUEST_IP`. e.g. for GUI programs, use `export DISPLAY=$WSL_HOST_IP:0`.

  * **Explanation:** In WSL2, the Linux kernel is run in a lightweight VM.
So, the VM is guest OS and Windows 10 is host OS. In Linux distribution
(i.e. guest OS), one can get WSL2 IP with `ip address show dev eth0` command
and Windows side IP from `/etc/resolv.conf` file. But here the `hvpty-backend`
saves the WSL2 side IPv4 address in `WSL_GUEST_IP` environment variable and
the hvpty frontend saves Windows side IPv4 address in `WSL_HOST_IP`.
With running the command `hvpty.exe -e WSL_HOST_IP` the frontend passes it to
backend automatically. The environment variables only valid in hvpty session.

