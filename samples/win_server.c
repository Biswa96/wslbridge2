/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) Biswapriyo Nath
 */

#include <winsock2.h>
#include <hvsocket.h>
#include <stdio.h>

#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif

#define PORT_NUM 5000
#define BUFF_SIZE 400

void Log(int ret, const char* function)
{
    if (ret == 0)
        printf("%s success\n", function);
    else
        printf("%s error: %d\n", function, WSAGetLastError());
}

int main(void)
{
    WSADATA wdata;
    int ret = WSAStartup(MAKEWORD(2,2), &wdata);
    if (ret != 0)
        printf("WSAStartup error: %d\n", ret);

    SOCKET sServer = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (sServer > 0)
        printf("server socket: %lld\n", sServer);
    else
        printf("socket error: %d\n", WSAGetLastError());

    SOCKADDR_HV addr = { 0 };
    addr.Family = AF_HYPERV;
    GUID VmId = { 0 }; /* enter the last parameter (a GUID) of wslhost.exe process */
    memcpy(&addr.VmId, &VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = PORT_NUM;
    ret = bind(sServer, (struct sockaddr*)&addr, sizeof addr);
    Log(ret, "bind");

    ret = listen(sServer, 1);
    Log(ret, "listen");

    SOCKET sClient = accept(sServer, NULL, NULL);
    if (sClient > 0)
        printf("client socket: %lld\n", sClient);
    else
        printf("accept error: %d\n", WSAGetLastError());

    char msg[BUFF_SIZE];

    do
    {
        memset(msg, 0, sizeof msg);
        ret = recv(sClient, msg, BUFF_SIZE, 0);
        if(ret > 0)
            printf("%s\n", msg);
        else if (ret == 0)
            printf("server closing...\n");
        else
            break;

    } while (ret > 0);

/* cleanup */
    closesocket(sClient);
    closesocket(sServer);
    WSACleanup();
    return 0;
}
