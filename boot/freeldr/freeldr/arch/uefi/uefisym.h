/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64/AMD64 UEFI debug symbol helpers
 */

#pragma once

#include <freeldr.h>

BOOLEAN
FreeldrLookupEmbeddedSymbol(
    _In_  ULONG_PTR Target,
    _Out_writes_(NameBufLen) CHAR* NameBuf,
    _In_  SIZE_T NameBufLen,
    _Out_opt_ ULONG_PTR* SymAddr);

VOID
UefiInitializeDebugImageInfo(VOID);
