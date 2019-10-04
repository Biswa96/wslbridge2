/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

#include <winsock2.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <wchar.h>

#define GAA_FLAG_CUSTOM ( GAA_FLAG_SKIP_FRIENDLY_NAME | \
                          GAA_FLAG_SKIP_MULTICAST | \
                          GAA_FLAG_SKIP_ANYCAST )

void WINAPI GetIp(void)
{
    ULONG ret, size;
    HANDLE hHeap = GetProcessHeap();

    ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_CUSTOM, NULL, NULL, &size);
    auto adpAddr = (PIP_ADAPTER_ADDRESSES)HeapAlloc(hHeap, 0, size);
    ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_CUSTOM, NULL, adpAddr, &size);

    if (ret == 0)
    {
        auto adpAddrTemp = adpAddr;
        while (adpAddrTemp)
        {
            /* Find interface name containing "WSL" string */
            if (wcsstr(adpAddrTemp->FriendlyName, L"WSL"))
            {
                // fwprintf(stdout, L"Interface Name: %ls\n", adpAddrTemp->FriendlyName);

                ret = GetAdaptersInfo(NULL, &size);
                auto adpInfo = (PIP_ADAPTER_INFO)HeapAlloc(hHeap, 0, size);
                ret = GetAdaptersInfo(adpInfo, &size);

                if (ret == 0)
                {
                    auto adpInfoTemp = adpInfo;
                    while (adpInfoTemp)
                    {
                        /* Check if network adapter index matches */
                        if (adpAddrTemp->IfIndex == adpInfoTemp->Index)
                        {
                            // fprintf(stdout, "IP: %s\n", adpInfoTemp->IpAddressList.IpAddress.String);
                            setenv("WSL_HOST_IP", adpInfoTemp->IpAddressList.IpAddress.String, false);
                            break;
                        }
                        adpInfoTemp = adpInfoTemp->Next;
                    }
                }
                else
                    fprintf(stderr, "GetAdaptersInfo error %d\n", ret);

                HeapFree(hHeap, 0, adpInfo);

                /* Exit from loop if we find that interface name */
                break;
            }
            adpAddrTemp = adpAddrTemp->Next;
        }
    }
    else
        fprintf(stderr, "GetAdaptersAddresses error %d\n", ret);

    HeapFree(hHeap, 0, adpAddr);
}
