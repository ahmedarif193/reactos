/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/kd64/kdcontrol.c
 * PURPOSE:         KD64 Control-Space Access, shared by AMD64 and ARM64
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
KdpSysReadControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    PVOID ControlStart;
    PKPRCB Prcb;
    PKIPCR Pcr;

    if (Processor >= (ULONG)KeNumberProcessors)
    {
        *ActualLength = 0;
        return STATUS_INVALID_PARAMETER;
    }

    Prcb = KiProcessorBlock[Processor];
    Pcr = CONTAINING_RECORD(Prcb, KIPCR, Prcb);

    switch (BaseAddress)
    {
        case AMD64_DEBUG_CONTROL_SPACE_KPCR:
            /* Copy a pointer to the Pcr */
            ControlStart = &Pcr;
            *ActualLength = sizeof(PVOID);
            break;

        case AMD64_DEBUG_CONTROL_SPACE_KPRCB:
            /* Copy a pointer to the Prcb */
            ControlStart = &Prcb;
            *ActualLength = sizeof(PVOID);
            break;

        case AMD64_DEBUG_CONTROL_SPACE_KSPECIAL:
            /* Copy SpecialRegisters */
            ControlStart = &Prcb->ProcessorState.SpecialRegisters;
            *ActualLength = sizeof(KSPECIAL_REGISTERS);
            break;

        case AMD64_DEBUG_CONTROL_SPACE_KTHREAD:
            /* Copy a pointer to the current Thread */
            ControlStart = &Prcb->CurrentThread;
            *ActualLength = sizeof(PVOID);
            break;

        default:
            *ActualLength = 0;
            ASSERT(FALSE);
            return STATUS_UNSUCCESSFUL;
    }

    /* Copy the memory */
    RtlCopyMemory(Buffer, ControlStart, min(Length, *ActualLength));

    /* Finish up */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdpSysWriteControlSpace(
    _In_ ULONG Processor,
    _In_ ULONG64 BaseAddress,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG ActualLength)
{
    PVOID ControlStart;
    PKPRCB Prcb;

    if (Processor >= (ULONG)KeNumberProcessors)
    {
        *ActualLength = 0;
        return STATUS_INVALID_PARAMETER;
    }

    Prcb = KiProcessorBlock[Processor];

    switch (BaseAddress)
    {
        case AMD64_DEBUG_CONTROL_SPACE_KSPECIAL:
            /* Copy SpecialRegisters */
            ControlStart = &Prcb->ProcessorState.SpecialRegisters;
            *ActualLength = sizeof(KSPECIAL_REGISTERS);
            break;

        default:
            *ActualLength = 0;
            ASSERT(FALSE);
            return STATUS_UNSUCCESSFUL;
    }

    /* Copy the memory */
    RtlCopyMemory(ControlStart, Buffer, min(Length, *ActualLength));

    return STATUS_SUCCESS;
}
