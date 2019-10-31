/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

/*
 * WinHelper.hpp: Helper functions containing Windows APIs only.
 */

#ifndef WINHELPER_HPP
#define WINHELPER_HPP

std::string GetErrorMessage(DWORD MessageId);
bool IsWslTwo(std::wstring DistroName);
DWORD GetWindowsBuild(void);
void GetIp(void);

#endif /* WINHELPER_HPP */
