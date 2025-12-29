
/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Embedded symbol table helpers for UEFI backtraces.
 */

#pragma once

#include <freeldr.h>

typedef struct _FREELDR_SYMBOL_ENTRY
{
    PCSTR Name;
    const VOID* Address;
} FREELDR_SYMBOL_ENTRY, *PFREELDR_SYMBOL_ENTRY;

extern const FREELDR_SYMBOL_ENTRY gFreeldrSymtab[];
extern const SIZE_T gFreeldrSymCount;

BOOLEAN
FreeldrLookupEmbeddedSymbol(
    _In_  ULONG_PTR Target,
    _Out_writes_(NameBufLen) CHAR* NameBuf,
    _In_  SIZE_T NameBufLen,
    _Out_opt_ ULONG_PTR* SymAddr);

VOID
UefiInitializeDebugImageInfo(VOID);
