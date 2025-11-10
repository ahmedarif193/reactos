/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal RtlPcToFileHeader implementation for HAL binaries
 */

#include <hal.h>

PVOID
NTAPI
RtlPcToFileHeader(
    _In_opt_ PVOID PcValue,
    _Out_opt_ PVOID *BaseOfImage)
{
    UNREFERENCED_PARAMETER(PcValue);

    if (BaseOfImage)
        *BaseOfImage = NULL;

    return NULL;
}

/* Provide an import-thunk-compatible symbol so modules compiled with
 * __declspec(dllimport) still resolve without pulling kernel32 in. */
PVOID (NTAPI *__imp_RtlPcToFileHeader)(PVOID, PVOID *)
    __asm__("__imp__RtlPcToFileHeader@8") = RtlPcToFileHeader;
