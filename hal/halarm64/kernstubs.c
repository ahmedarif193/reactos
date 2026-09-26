/*
 * PROJECT:         ReactOS HAL (ARM64)
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <halacpi_arm64.h>
#include <reactos/hal/msi.h>
#define NDEBUG
#include <debug.h>

NTSTATUS NTAPI HalGetInterruptTargetInformation(PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation);
NTSTATUS NTAPI HalGetMessageRoutingInfo(PHAL_MESSAGE_ROUTING_INFO RoutingInfo);

NTSTATUS NTAPI HalpGetInterruptTargetInformation(PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    return HalGetInterruptTargetInformation(TargetInformation);
}

NTSTATUS NTAPI HalpGetMessageRoutingInfo(PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    return HalGetMessageRoutingInfo(RoutingInfo);
}

/* Additional HAL exports */
#undef READ_PORT_BUFFER_UCHAR
#undef READ_PORT_BUFFER_USHORT
#undef READ_PORT_BUFFER_ULONG
#undef WRITE_PORT_BUFFER_UCHAR
#undef WRITE_PORT_BUFFER_USHORT
#undef WRITE_PORT_BUFFER_ULONG
#undef READ_REGISTER_UCHAR
#undef READ_REGISTER_USHORT
#undef READ_REGISTER_ULONG
#undef WRITE_REGISTER_UCHAR
#undef WRITE_REGISTER_USHORT
#undef WRITE_REGISTER_ULONG

/*
 * ===========================================================================
 * Win11 hal.dll ABI export-table parity (UNIMPLEMENTED-for-ABI-export)
 * ===========================================================================
 *
 * The Win11 ARM64 hal.dll re-exports these names; in Win11 they are all
 * FORWARDERS to ntoskrnl (HAL is merged into the kernel since Win10), e.g.
 *   llvm-readobj --coff-exports hal.dll
 *     Name: HalEnumerateProcessors  ForwardedTo: ntoskrnl.HalEnumerateProcessors
 *
 * ReactOS has NOT merged HAL into ntoskrnl, and ReactOS ntoskrnl.spec exports
 * none of these names, so forwarding to ntoskrnl would produce dangling
 * forwarders. To achieve Win11 hal.dll export-NAME-set parity while staying
 * inside the hal tree, we provide local UNIMPLEMENTED stubs here and export the
 * bare names from hal.spec (-arch=arm64).
 *
 * Boot-path note: none of these names are referenced by any ARM64 ntoskrnl/hal
 * source path (verified by grep). They are pure ABI-completeness exports and
 * are therefore boot-neutral (never invoked on the early boot path). If any of
 * these ever becomes part of the live ARM64 interrupt/processor path, it must
 * be wired to the real internal implementation instead of this stub.
 *
 * Prototypes are the well-known NT HAL signatures (the public Win11 hal.pdb is
 * stripped, so name->prototype could not be recovered from it).
 */

/* WHEA / RAS (ARM64 Synchronous External Abort, SError Interrupt, CMCI). */
NTSTATUS NTAPI HalWheaHandleSea(PVOID Context)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI HalWheaHandleSei(PVOID Context)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI HalWheaUpdateCmciPolicy(PVOID Policy)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Policy);
    return STATUS_NOT_SUPPORTED;
}

/* Processor enumeration / dynamic (hot-add) processor management. */
NTSTATUS NTAPI HalEnumerateProcessors(PVOID Buffer, PULONG Count)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Buffer);
    if (Count)
        *Count = 0;
    return STATUS_NOT_SUPPORTED;
}

ULONG NTAPI HalQueryMaximumProcessorCount(VOID)
{
    ULONG Count = HalpArm64GicInfo.GiccEntryCount;
    return (Count != 0) ? Count : 1;
}

NTSTATUS NTAPI HalRegisterDynamicProcessor(PVOID NewProcessor, PVOID ProcessorInfo)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(NewProcessor);
    UNREFERENCED_PARAMETER(ProcessorInfo);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI HalStartDynamicProcessor(PVOID ProcessorState, PVOID Context)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(ProcessorState);
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_SUPPORTED;
}

BOOLEAN NTAPI HalIsHyperThreadingEnabled(VOID)
{
    /* ARM64 has no x86-style SMT/hyper-threading. */
    return FALSE;
}

/* Interrupt / IPI / clock request helpers. */
BOOLEAN NTAPI HalBeginSystemInterruptUnspecified(KIRQL Irql, PKIRQL OldIrql)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Irql);
    if (OldIrql)
        *OldIrql = PASSIVE_LEVEL;
    return FALSE;
}

VOID NTAPI HalRequestClockInterrupt(ULONG ProcessorNumber)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(ProcessorNumber);
}

VOID NTAPI HalRequestDeferredRecoveryServiceInterrupt(VOID)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
}

VOID NTAPI HalSendSoftwareInterrupt(KAFFINITY TargetSet, KIRQL Irql)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(TargetSet);
    UNREFERENCED_PARAMETER(Irql);
}

/* ARM64 MPAM (Memory Partitioning And Monitoring) register 0. */
VOID NTAPI HalSetMpam0(ULONG64 Value)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Value);
}

/* ACPI / firmware / resume / errata. */
PVOID NTAPI HalAcpiGetTableEx(PVOID LoaderBlock, ULONG Signature)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(Signature);
    return NULL;
}

VOID NTAPI HalInitializeOnResume(PVOID Context)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Context);
}

NTSTATUS NTAPI HalRegisterErrataCallbacks(PVOID Callbacks)
{
    DPRINT1("%s: UNIMPLEMENTED\n", __FUNCTION__);
    UNREFERENCED_PARAMETER(Callbacks);
    return STATUS_NOT_SUPPORTED;
}
