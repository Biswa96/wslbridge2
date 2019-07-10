#include <assert.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "LocalSock.hpp"

LocalSock::LocalSock()
{
    mSockFd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    assert(mSockFd >= 0);

    const int flag = 1;
    const int nodelayRet = setsockopt(mSockFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof flag);
    assert(nodelayRet == 0);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int bindRet = bind(mSockFd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr);
    assert(bindRet == 0);

    const int listenRet = listen(mSockFd, 1);
    assert(listenRet == 0);

    socklen_t addrLen = sizeof(addr);
    const int getRet = getsockname(mSockFd, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    assert(getRet == 0);

    mPort = ntohs(addr.sin_port);
}

int LocalSock::accept()
{
    const int cs = ::accept(mSockFd, nullptr, nullptr);
    assert(cs >= 0);

    const int flag = 1;
    const int nodelayRet = setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof flag);
    assert(nodelayRet == 0);

    return cs;
}

void LocalSock::close()
{
    if (mSockFd != -1)
    {
        ::close(mSockFd);
        mSockFd = -1;
    }
}
