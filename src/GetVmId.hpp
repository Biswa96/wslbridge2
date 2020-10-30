/* 
 * This file is part of wslbridge2 project.
 * Licensed under the terms of the GNU General Public License v3 or later.
 * Copyright (C) Biswapriyo Nath.
 */

/*
 * GetVmId.hpp: Get GUID of WSL2 Utility VM with LxssUserSession COM interface.
 */

#ifndef GETVMID_HPP
#define GETVMID_HPP

void ComInit(void);
bool IsWslTwo(GUID *DistroId, const std::wstring DistroName);
HRESULT GetVmId(GUID *DistroId, GUID *LxInstanceID);

#endif /* GETVMID_HPP */
