/*
 * PROJECT:     ReactOS Boot Runtime Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal boot-time RtlPcToFileHeader implementation
 */

#include <rtl.h>

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
