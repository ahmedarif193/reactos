/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         ARM64-specific KD fallback
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KD_ARM64_FALLBACK_MAX 512

extern VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);
extern BOOLEAN KdDebuggerNotPresent;

BOOLEAN
KdpPlatformSerialFallbackPrint(
    _In_reads_bytes_(Length) PCCHAR Text,
    _In_ USHORT Length)
{
    CHAR LocalBuffer[KD_ARM64_FALLBACK_MAX];
    SIZE_T CopyLength;

    if (!KdDebuggerNotPresent || Text == NULL || Length == 0)
        return FALSE;

    CopyLength = min((SIZE_T)Length, sizeof(LocalBuffer) - 1);
    if (CopyLength == 0)
        return TRUE;

    RtlCopyMemory(LocalBuffer, Text, CopyLength);
    LocalBuffer[CopyLength] = '\0';

    while (CopyLength > 0)
    {
        CHAR Ch = LocalBuffer[CopyLength - 1];

        if ((Ch != '\r') && (Ch != '\n'))
            break;

        LocalBuffer[--CopyLength] = '\0';
    }

    if (CopyLength == 0)
        return TRUE;

    KiArm64BootStageLog(LocalBuffer);
    return TRUE;
}
