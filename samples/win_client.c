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

    SOCKET Sock = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (Sock > 0)
        printf("server socket: %lld\n", Sock);
    else
        printf("socket error: %d\n", WSAGetLastError());

    SOCKADDR_HV addr = { 0 };
    addr.Family = AF_HYPERV;
    GUID VmId = { 0 }; /* enter the last parameter (a GUID) of wslhost.exe process */
    memcpy(&addr.VmId, &VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);

    printf("Enter port number: ");
    unsigned long Port = 0;
    scanf("%lu", &Port);
    addr.ServiceId.Data1 = Port;
    ret = connect(Sock, (struct sockaddr*)&addr, sizeof addr);
    Log(ret, "connect");

    char msg[BUFF_SIZE];

    while (1)
    {
        memset(msg, 0, sizeof msg);
        printf("Enter message: ");
        scanf("%s", msg);
        ret = send(Sock, msg, strlen(msg), 0);
        if (ret < 0)
        {
            printf("send error: %d\n", WSAGetLastError());
            break;
        }
    }

    /* cleanup */
    if (Sock > 0)
        closesocket(Sock);
    return 0;
}
