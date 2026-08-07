/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Implementation of GetSystemDefaultLocaleName
 * COPYRIGHT:   Adapted from the ReactOS/Wine locale implementation
 */

#include "k32_vista.h"

INT
WINAPI
GetSystemDefaultLocaleName(
    LPWSTR lpLocaleName,
    INT cchLocaleName)
{
    return LCIDToLocaleName(GetSystemDefaultLCID(), lpLocaleName, cchLocaleName, 0);
}
