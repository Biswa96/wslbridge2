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
