<!--
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2020 Biswapriyo Nath.
 *
 * README.md: Main README file for wslbridge2 project
-->

# wslbridge2

[![Licence](https://img.shields.io/github/license/Biswa96/wslbridge2.svg?style=flat-square)][1]&nbsp;&nbsp;&nbsp;
[![Top Language](https://img.shields.io/github/languages/top/Biswa96/wslbridge2.svg?style=flat-square)][2]&nbsp;&nbsp;&nbsp;
[![Code size](https://img.shields.io/github/languages/code-size/Biswa96/wslbridge2.svg?style=flat-square)]()&nbsp;&nbsp;&nbsp;
[![GitHub release](https://img.shields.io/github/release/Biswa96/wslbridge2.svg?style=flat-square)][3]&nbsp;&nbsp;&nbsp;
[![Appveyor Build](https://img.shields.io/appveyor/ci/Biswa96/wslbridge2.svg?style=flat-square)][4]&nbsp;&nbsp;&nbsp;

Explore various ways to connect Windows Subsystem for Linux (WSL) with
Windows terminal emulators and command line programs.


## Requirements:

* **Windows 10 version 1809** (build 17763) aka. October 2018 Update
* A POSIX-compatible environment - cygwin or msys2
* A terminal emulator - mintty or ConEmu
* For compiling - GCC, make, linux-headers


## How to build

Clone this git repository. Run `make` in cygwin (or msys2) and WSL to make all.
To build individual programs, go to `src` folder and run `make` command with
the corresponding Makefile. By default the `make` command will create dynamically
linked executables. For statically liked binaries, use `make RELEASE=1` command.
All binaries will be placed in `bin` folder.


## How to use

Download the released stable binaries from [Release page][3]. Or to test
nightly builds, download the wslbridge2.zip from this [Appveyor project link][5].
Here are some info about the directories of this project.

### samples: sample C code using Hyper-V sockets

Pick up any one of 1. `win_client` & `wsl_server` 2. `win_server` & `wsl_client`.
Run `wsl.exe` first. Paste the VM ID from the last argument of `wslhost.exe`
process's command line. Compile the `win_` part in cygwin or msys2 and
the `wsl_` part in WSL. Run the server part first. It will wait for the client.

### wslbridge2: connect with WSL using network sockets

Place `wslbridge2.exe` and `wslbridge2-backend` in same Windows folder.
Run `wslbridge2.exe`. This requires cygwin or msys2 environment.

### Options

Running `wslbridge2.exe` will open default shell in default WSL distribution.
Here are the list of valid options:

* `-b` or `--backend`: Overrides the default path of backend binaries.
* `-d` or `--distribution`: Run the specified distribution.
* `-e` or `--env`:  Copies Windows environment variable into the WSL.
* `-h` or `--help`: Show this usage information.
* `-l` or `--login`: Start a login shell in WSL.
* `-u` or `--user`: Run as the specified user in WSL.
* `-w` or `--windir`: Changes the working directory to a Windows path.
* `-W` or `--wsldir`: Changes the working directory to WSL path.
* `-x` or `--xmod`: Shows hidden backend window and debug output.

Always use single quote or double quote to mention any folder path. For paths
in WSL, `"~"` can also be used for user's home folder. If no command line is
provided, this launches the default shell. The non-options arguments will be
executed as is. For example, `wslbridge2.exe ls` will execute `ls` command
in current working directory in default WSL distribution.


## Frequently Asked Questions

See the [FAQ page](FAQ.md) for the answers to commonly asked questions.


## Caveats

* The graphics may lag sometimes due to multiple layers of data transitions
between Windows and WSL side programs.

* There is no documented way to get VM ID from WSL2 Linux VM. See this
[issue](https://github.com/microsoft/WSL/issues/4131). Hence `GetVmId.cpp` will
change in future Windows 10 releases due to usage of undocumented COM methods.


## Further Readings

  - [Make your own integration services][6]
  - [Linux kernel: af_vsock.c][7]
  - [Linux kernel: vm_sockets.h][8]
  - [VMWare: VMCI Socket Programming Guide][9]
  - [man7: vsock(7)][10]
  - [wslbridge][11]
  - [win32-console-docs][12]
  - [XConPty][13]


## Acknowledgments

This is based on the Ryan Prichard's (@rprichard) [wslbridge][11] project.
Also thanks to @mintty, @therealkenc, @dxhisboy and all [contributors][14]
for helping with this project.


## Contributions

Contributions are greatly appreciated. Please keep these following points:

* For a big change, try to add an issue before creating a pull request.
* Append a tag name in commit message, see previous commits as reference.
* Make the code easy to read and understand with proper syntax.
* If possible try to minimize memory usage.
* Use 4 spaces for indentation.
* If you are not programmer you can contribute to further improve or additions
to documentations. Also share and discuss what features you like to have.


## License

wslbridge2 is licensed under the GNU General Public License v3.
A full copy of the license is provided in [LICENSE](LICENSE).

    wslbridge2 -- Explore various ways to connect WSL with Windows terminal emulators.
    Copyright (C) Biswapriyo Nath
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

<!-- Links -->

[1]: LICENSE
[2]: https://github.com/Biswa96/wslbridge2.git
[3]: https://github.com/Biswa96/wslbridge2/releases
[4]: https://ci.appveyor.com/project/Biswa96/wslbridge2
[5]: https://ci.appveyor.com/api/projects/Biswa96/wslbridge2/artifacts/wslbridge2.zip?branch=master&job=Image%3A%20Visual%20Studio%202019
[6]: https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/user-guide/make-integration-service
[7]: https://github.com/torvalds/linux/blob/master/net/vmw_vsock/af_vsock.c
[8]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/vm_sockets.h
[9]: https://www.vmware.com/support/developer/vmci-sdk/
[10]: http://man7.org/linux/man-pages/man7/vsock.7.html
[11]: https://github.com/rprichard/wslbridge.git
[12]: https://github.com/rprichard/win32-console-docs.git
[13]: https://github.com/Biswa96/XConPty.git
[14]: https://github.com/Biswa96/wslbridge2/graphs/contributors
