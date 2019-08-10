/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#ifndef LOCALSOCK_HPP
#define LOCALSOCK_HPP

class LocalSock
{
public:
    LocalSock();
    ~LocalSock() { close(); }
    int port() { return mPort; }
    int accept();
    void close();

private:
    int mSockFd;
    int mPort;
};

#endif /* LOCALSOCK_HPP */
