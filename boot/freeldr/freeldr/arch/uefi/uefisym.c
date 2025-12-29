/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Embedded symbol table for freeldr backtraces (no PE exports needed)
 */

#include <uefildr.h>
#include <debug.h>
#include <arch/uefi/uefisym.h>
#if defined(_M_ARM64) || defined(__aarch64__)
#include <arch/arm64/arm64.h>
#endif

/*
 * NOTE: This compact table lists a curated set of common freeldr entry points
 * used during UEFI boot on ARM64/AMD64. It enables symbolic backtraces even
 * though the EFI image is stripped and has no export directory.
 *
 * Add more functions as needed; order does not matter (lookup scans for the
 * nearest symbol at or below the target address).
 */

/* Forward declarations only for functions without public headers */
#if defined(_M_ARM64) || defined(__aarch64__)
VOID UefiArm64PrintBacktrace(ULONG_PTR Fp, ULONG_PTR StackTop, ULONG_PTR StackBottom);
VOID Arm64HandleException(VOID* Ctx);
#endif
#if defined(_M_AMD64) || defined(__x86_64__)
VOID UefiAmd64PrintBacktrace(ULONG_PTR Rbp, ULONG_PTR StackTop, ULONG_PTR StackBottom);
#endif
/* Not in public header */
VOID UefiExitBootServices(VOID);

/* Table contents */
const FREELDR_SYMBOL_ENTRY gFreeldrSymtab[] = {
    { "MachInit",                         (const VOID*)&MachInit },
    { "UefiInitializeDebugImageInfo",     (const VOID*)&UefiInitializeDebugImageInfo },

#if defined(_M_ARM64) || defined(__aarch64__)
    { "Arm64CanInitializeExceptions",     (const VOID*)&Arm64CanInitializeExceptions },
    { "Arm64InitializeExceptions",        (const VOID*)&Arm64InitializeExceptions },
    { "Arm64HandleException",             (const VOID*)&Arm64HandleException },
    { "UefiArm64PrintBacktrace",          (const VOID*)&UefiArm64PrintBacktrace },
#endif
#if defined(_M_AMD64) || defined(__x86_64__)
    { "UefiAmd64PrintBacktrace",          (const VOID*)&UefiAmd64PrintBacktrace },
#endif

    { "UefiMemGetMemoryMap",              (const VOID*)&UefiMemGetMemoryMap },
    { "UefiExitBootServices",             (const VOID*)&UefiExitBootServices },
    { "UefiPrepareForReactOS",            (const VOID*)&UefiPrepareForReactOS },

    { "UefiConsPutChar",                  (const VOID*)&UefiConsPutChar },
    { "UefiConsKbHit",                    (const VOID*)&UefiConsKbHit },
    { "UefiConsGetCh",                    (const VOID*)&UefiConsGetCh },
    { "UefiInitializeVideo",              (const VOID*)&UefiInitializeVideo },
    { "UefiVideoClearScreen",             (const VOID*)&UefiVideoClearScreen },
    { "UefiVideoPutChar",                 (const VOID*)&UefiVideoPutChar },
    { "UefiVideoSetTextCursorPosition",   (const VOID*)&UefiVideoSetTextCursorPosition },
    { "UefiVideoHideShowTextCursor",      (const VOID*)&UefiVideoHideShowTextCursor },

    { "UefiGetTime",                      (const VOID*)&UefiGetTime },
};

const SIZE_T gFreeldrSymCount = sizeof(gFreeldrSymtab) / sizeof(gFreeldrSymtab[0]);

BOOLEAN
FreeldrLookupEmbeddedSymbol(
    _In_  ULONG_PTR Target,
    _Out_writes_(NameBufLen) CHAR* NameBuf,
    _In_  SIZE_T NameBufLen,
    _Out_opt_ ULONG_PTR* SymAddr)
{
    if (!NameBuf || NameBufLen == 0)
        return FALSE;

    /* Prefer the highest address not exceeding Target. Track nearest as fallback. */
    const FREELDR_SYMBOL_ENTRY* best_le = NULL;
    const FREELDR_SYMBOL_ENTRY* best_near = NULL;
    ULONG_PTR best_near_dist = (ULONG_PTR)-1;

    for (SIZE_T i = 0; i < gFreeldrSymCount; ++i)
    {
        const FREELDR_SYMBOL_ENTRY* e = &gFreeldrSymtab[i];
        ULONG_PTR addr = (ULONG_PTR)e->Address;
        if (addr == 0)
            continue;

        if (addr <= Target)
        {
            if (!best_le || addr > (ULONG_PTR)best_le->Address)
                best_le = e;
        }

        ULONG_PTR dist = (addr > Target) ? (addr - Target) : (Target - addr);
        if (!best_near || dist < best_near_dist)
        {
            best_near = e;
            best_near_dist = dist;
        }
    }

    const FREELDR_SYMBOL_ENTRY* pick = best_le ? best_le : best_near;
    if (!pick)
        return FALSE;

    /* Copy name safely */
    SIZE_T n = 0;
    while (pick->Name[n] && n + 1 < NameBufLen) { NameBuf[n] = pick->Name[n]; ++n; }
    NameBuf[n] = '\0';
    if (SymAddr) *SymAddr = (ULONG_PTR)pick->Address;
    return TRUE;
}
