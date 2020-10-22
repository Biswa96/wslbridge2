<!--
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2020 Biswapriyo Nath.
 *
 * FAQ.md: questions and answers for developers and enthusiasts
-->

# FAQ

## Terms used:

Some familiarity with the working principle of ssh and telnet will be useful.
See this beautiful scheme on [how SSH works][1] in Unix & Linux Stack Exchange.
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
The socket domain is AF_INET in WSL1 and AF_VSOCK in WSL2.
  - Three network sockets from each side connects and tunnels the I/O buffers.
  - In WSL side, the backend creates a pseudo tty where master side connects
to the frontend and slave side execs the child process or default shell.
  - Remember, wslbridge2 does not know or care what buffer is passed through
the sockets. The responsibility goes to the terminal emulator or command line
program in Windows side which can understand the buffer.

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

**A3:** The underlying concept is same for both. Only socket creation procedure
and buffer handling are different.

------

**Q4: How to get Windows IP address in WSL2 quickly?**

**A4:** Run `wslbridge2.exe` command as usual. IPv4 address of Windows 10 side
is set to `WSL_HOST_IP` and of WSL2 side is set to `WSL_GUEST_IP` environment
variable. e.g. for GUI programs, use `export DISPLAY=$WSL_HOST_IP:0`.

------

**Q5: What are the error code starting with 0x8?**

**A5:** Generally the [system error codes][3] are translated into error
messages and shown in output. But there are some custom HRESULT values that
are returned from WSL APIs and Lxss COM methods. Here are some of those
HRESULT values and their meanings (copied form wsl.exe output):

  * 0x80040302 - There is no distribution with the supplied name.
  * 0x80040304 - The Legacy distribution does not support WSL2.
  * 0x80040308 - User not found.


<!-- Links -->

[1]: https://unix.stackexchange.com/a/158604/336403/
[2]: https://github.com/mintty/mintty/issues/921
[3]: https://docs.microsoft.com/en-us/windows/win32/debug/system-error-codes
