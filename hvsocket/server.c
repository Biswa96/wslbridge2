/* 
* This file is part of wslbridge2 project
* Licensed under the GNU General Public License version 3
*/

#include <winsock2.h>
#include "hvsocket.h"
#include <stdio.h>

#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif

#define PORT_NUM 5000
#define BUFF_SIZE 400

int main(void)
{
    struct WSAData wdata;
    int ret = WSAStartup(MAKEWORD(2,2), &wdata);
    if (ret != 0)
        printf("WSAStartup error: %d\n", ret);

    SOCKET sServer = socket(AF_HYPERV, SOCK_STREAM, IPPROTO_ICMP);
    if (sServer == INVALID_SOCKET)
        printf("socket error: %d\n", WSAGetLastError());

    struct _SOCKADDR_HV addr;
    memset(&addr, 0, sizeof addr);

    addr.Family = AF_HYPERV;
    GUID VmId = { 0 }; /* enter the last parameter (a GUID) of wslhost.exe process */
    memcpy(&addr.VmId, &VmId, sizeof addr.VmId);
    memcpy(&addr.ServiceId, &HV_GUID_VSOCK_TEMPLATE, sizeof addr.ServiceId);
    addr.ServiceId.Data1 = PORT_NUM;
    ret = bind(sServer, (struct sockaddr*)&addr, sizeof addr);
    if (ret != 0)
        printf("bind error: %d\n", ret);

    ret = listen(sServer, SOMAXCONN);
    if (ret != 0)
        printf("listen error: %d\n", WSAGetLastError());

    SOCKET sClient = accept(sServer, NULL, NULL);
    if (sClient == INVALID_SOCKET)
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
