/*
 * PROJECT:         LiberNT Kernel
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ARM64-specific KD fallback
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KD_ARM64_FALLBACK_MAX 512

extern BOOLEAN KdDebuggerNotPresent;

BOOLEAN
KdpPlatformSerialFallbackPrint(
    _In_reads_bytes_(Length) PCCHAR Text,
    _In_ USHORT Length)
{
    if (!KdDebuggerNotPresent || Text == NULL || Length == 0)
        return FALSE;

    UNREFERENCED_PARAMETER(Text);
    UNREFERENCED_PARAMETER(Length);
    return TRUE;
}
