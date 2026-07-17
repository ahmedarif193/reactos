/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Windows-compatible ARM64 FP/NEON feature policy.
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>

#define KI_ARM64_LEGACY_XSTATE_MASK 0x3ULL
#define INVALID_EXTENDED_PROCESSOR_STATE 0x131

C_ASSERT(sizeof(KARM64_VFP_STATE) == 0x210);

NTSTATUS
NTAPI
KeSaveExtendedProcessorState(
    _In_ ULONG64 Mask,
    _Out_ PXSTATE_SAVE XStateSave)
{
    ULONG64 OptionalMask = Mask & ~KI_ARM64_LEGACY_XSTATE_MASK;

    UNREFERENCED_PARAMETER(XStateSave);

    if (OptionalMask != 0)
    {
        KeBugCheckEx(INVALID_EXTENDED_PROCESSOR_STATE,
                     0,
                     0,
                     (ULONG)OptionalMask,
                     (ULONG)(OptionalMask >> 32));
    }

    /* Win11 accepts legacy bits as a no-op and leaves XSTATE_SAVE untouched. */
    return STATUS_SUCCESS;
}

VOID
NTAPI
KeRestoreExtendedProcessorState(
    _In_ PXSTATE_SAVE XStateSave)
{
    UNREFERENCED_PARAMETER(XStateSave);
}

ULONG64
NTAPI
RtlGetEnabledExtendedFeatures(
    _In_ ULONG64 FeatureMask)
{
    UNREFERENCED_PARAMETER(FeatureMask);

    /* ReactOS exposes no optional ARM64 XState feature (Windows bit 2). */
    return 0;
}
