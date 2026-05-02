/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal embedded symbol lookup stubs for UEFI backtraces
 */

#include <uefildr.h>
#include <arch/uefi/uefisym.h>

BOOLEAN
FreeldrLookupEmbeddedSymbol(
    _In_  ULONG_PTR Target,
    _Out_writes_(NameBufLen) CHAR* NameBuf,
    _In_  SIZE_T NameBufLen,
    _Out_opt_ ULONG_PTR* SymAddr)
{
    UNREFERENCED_PARAMETER(Target);

    if (NameBuf && NameBufLen)
        NameBuf[0] = '\0';
    if (SymAddr)
        *SymAddr = 0;
    return FALSE;
}

VOID
UefiInitializeDebugImageInfo(VOID)
{
}
