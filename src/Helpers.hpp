/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) 2019-2020 Biswapriyo Nath.
 */

#ifndef HELPERS_HPP
#define HELPERS_HPP

#include <string>
#include <vector>

/* KUSER_SHARED_DATA.NtBuildNumber */
#define GetWindowsBuild() (*(unsigned int *)(0x7FFE0000 + 0x0260))

/* KUSER_SHARED_DATA.NtSystemRoot */
#define GetWinDir() ((wchar_t *)(0x7FFE0000 + 0x0030))

std::wstring mbsToWcs(const std::string &s);
std::string wcsToMbs(const std::wstring &s, bool emptyOnError=false);
std::wstring dirname(const std::wstring &path);
std::wstring getModuleFileName(void);
bool pathExists(const std::wstring &path);
wchar_t lowerDrive(wchar_t ch);
std::wstring findSystemProgram(const wchar_t *name);
std::wstring normalizePath(const std::wstring &path);
std::wstring findBackendProgram(const std::string &customBackendPath, const wchar_t *const backendName);
void appendWslArg(std::wstring &out, const std::wstring &arg);
std::vector<char> readAllFromHandle(HANDLE h);
std::string GetErrorMessage(DWORD MessageId);

#endif /* HELPERS_HPP */
