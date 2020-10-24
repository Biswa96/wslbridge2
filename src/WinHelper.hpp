/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2020 Biswapriyo Nath.
 */

/*
 * WinHelper.hpp: Helper functions containing Windows APIs only.
 */

#ifndef WINHELPER_HPP
#define WINHELPER_HPP

/* KUSER_SHARED_DATA.NtBuildNumber */
#define GetWindowsBuild() (*(unsigned int *)(0x7FFE0000 + 0x0260))

std::string GetErrorMessage(DWORD MessageId);
bool IsWslTwo(std::wstring DistroName);

#endif /* WINHELPER_HPP */
