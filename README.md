# wslbridge2

[![Licence](https://img.shields.io/github/license/Biswa96/wslbridge2.svg)][1]
[![Top Language](https://img.shields.io/github/languages/top/Biswa96/wslbridge2.svg)][2]
[![Code size](https://img.shields.io/github/languages/code-size/Biswa96/wslbridge2.svg)]()

Connect WSL2 with Windows programs (in progress).

## How to use

### hvsocket: samples using Hyper-V sockets

Pick up any one of 1. `win_client` & `wsl_server` 2. `win_server` & `wsl_client`.
Compile the Windows part in cygwin or msys2 or msvc and the WSL part in WSL.
Run the server part first and it will wait for the client connection.

## Further Readings

* [Make your own integration services][3]
* [Linux kernel: af_vsock.c][4]
* [Linux kernel: vm_sockets.h][5]
* [VMWare: VMCI Socket Programming Guide][6]
* [man7: vsock(7)][7]

## License

wslbridge2 is licensed under the GNU General Public License v3.
A full copy of the license is provided in [LICENSE](LICENSE).

    Copyright (c) 2019 Biswapriyo Nath
    
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

[1]: https://www.gnu.org/licenses/gpl-3.0.en.html
[2]: https://github.com/Biswa96/wslbridge2.git
[3]: https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/user-guide/make-integration-service
[4]: https://github.com/torvalds/linux/blob/master/net/vmw_vsock/af_vsock.c
[5]: https://github.com/torvalds/linux/blob/master/include/uapi/linux/vm_sockets.h
[6]: https://www.vmware.com/support/developer/vmci-sdk/
[7]: http://man7.org/linux/man-pages/man7/vsock.7.html

