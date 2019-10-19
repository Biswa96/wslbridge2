/* 
 * This file is part of wslbridge2 project
 * Licensed under the GNU General Public License version 3
 * Copyright (C) 2019 Biswapriyo Nath
 */

/*
 * WinHelper.cpp: Helper functions containing Windows APIs only.
 */

#include <windows.h>

DWORD GetWindowsBuild(void)
{
    typedef void (WINAPI *RTLGETVERSIONPROC)(OSVERSIONINFOW*);

    HMODULE hMod = LoadLibraryExW(
                   L"ntdll.dll",
                   nullptr,
                   LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hMod == nullptr)
        return 0;

    RTLGETVERSIONPROC pfnRtlGetVersion = (RTLGETVERSIONPROC)
                        GetProcAddress(hMod, "RtlGetVersion");
    if (pfnRtlGetVersion == nullptr)
        return 0;

    OSVERSIONINFOW info = {};
    info.dwOSVersionInfoSize = sizeof info;
    pfnRtlGetVersion(&info);

    FreeLibrary(hMod);
    return info.dwBuildNumber;
}
