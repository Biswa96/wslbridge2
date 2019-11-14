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

#define WSL_VERSION_ONE 1
#define WSL_VERSION_TWO 2

HRESULT GetVmId(
    GUID *LxInstanceID,
    const std::wstring &DistroName,
    int *WslVersion);

#endif /* GETVMID_HPP */
