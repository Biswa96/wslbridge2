<!--
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 *
 * FAQ.md: questions and answers for developers and enthusiasts
-->

# FAQ

## Terms used:

Some familiarity with the working principle of ssh and telnet will be useful.
See this beautiful scheme on [how sshd works] in Unix & Linux Stack Exchange.
Please be familiar with these following terms used in this project.

* frontend - wslbridge2.exe Windows executable PE binary.
* backend - wslbridge2-backend Linux executable ELF binary.
* IP - Internet Protocol.
* AF_INET - Address family which uses IPv4 addresses to communicate.
* AF_VSOCK (Linux) or AF_HYPERV (Windows) - Address family uses vSockets.


## Q&A

Here are the commonly asked questions and some tips & tricks.
Thanks to Brian Inglis (@BrianInglis) for asking [these questions][2].

**Q1: How does this work?**

**A1:** The story begins when user runs wslbridge2.exe in Windows.

  - First, it parses the valid options and stores arguments for further use.
  - Then it finds the backend and wsl.exe path and stitches them into a valid
command line string. The command line containing wsl.exe is used to create
the backend process which is generally hidden (use `-x` option to show).
  - Now both frontend and backend create sockets to connect with each other.
The socket type is different in case of WSL1 (AF_INET) and WSL2 (AF_VSOCK).
  - Three sockets from each side connects and tunnels the I/O buffers.
  - In WSL side, the backend creates a pseudo tty where master side connects
to the frontend and slave side execs the child process or default shell.
  - Remember, wslbridge2 does not know or care what buffers are passed through
the sockets. The responsibility goes to the terminal emulator or command line
program in Windows side which can understand it.

------

**Q2: WSL1 vs WSL2 -- what does wslbridge2 do differently?**

**A2:** Only the socket creation is different, the rest is as above.

  - In case of WSL1: The frontend creates three sockets, binds and listens for
the connection. The backend process gets the port numbers from command line.
Backend creates three sockets using those ports and the connections are
accepted in frontend. tl;dr, three sockets, three ports.

  - In case of WSL2: First frontend creates a server socket and listens for
connection. After the backend connection is accepted, the backend sends the
random port number to frontend and listens for connection. Now the frontend
creates three sockets using that port number and backend accepts those.
tl;dr, three sockets, one port.

------

**Q3: wslbridge vs wslbridge2 -- any difference?**

**A3:** The underlying concept is same, only buffer handling is different.

------

**Q4: How to get Windows IP address in WSL2 quickly?**

**A4:** tl;dr, Run `wslbridge2.exe -e WSL_HOST_IP` command. Windows 10 IPv4
address will be accessed from `WSL_HOST_IP` environment variable and WSL2 IPv4
address from `WSL_GUEST_IP`. e.g. for GUI programs, use
`export DISPLAY=$WSL_HOST_IP:0`.


<!-- Links -->

[1]: https://unix.stackexchange.com/a/158604/336403/
[2]: https://github.com/mintty/mintty/issues/921

