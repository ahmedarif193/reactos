/*
 * PROJECT:         ReactOS Hardware Abstraction Layer (HAL)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * PURPOSE:         I/O Mapping and x86 Subs
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS  *******************************************************************/

UCHAR HalpSerialLen;
CHAR HalpSerialNumber[31];

/* PRIVATE FUNCTIONS **********************************************************/

#ifndef _MINIHAL_
CODE_SEG("INIT")
VOID
NTAPI
HalpReportSerialNumber(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING KeyString;
    HANDLE Handle;

    /* Make sure there is a serial number */
    if (!HalpSerialLen) return;

    /* Open the system key */
    RtlInitUnicodeString(&KeyString, L"\\Registry\\Machine\\Hardware\\Description\\System");
    Status = HalpOpenRegistryKey(&Handle, 0, &KeyString, KEY_ALL_ACCESS, FALSE);
    if (NT_SUCCESS(Status))
    {
        /* Add the serial number */
        RtlInitUnicodeString(&KeyString, L"Serial Number");
        ZwSetValueKey(Handle,
                      &KeyString,
                      0,
                      REG_BINARY,
                      HalpSerialNumber,
                      HalpSerialLen);

        /* Close the handle */
        ZwClose(Handle);
    }
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpMarkAcpiHal(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING KeyString;
    HANDLE KeyHandle;
    HANDLE Handle;
    ULONG Value = HalDisableFirmwareMapper ? 1 : 0;

    /* Open the control set key */
    RtlInitUnicodeString(&KeyString,
                         L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET");
    Status = HalpOpenRegistryKey(&Handle, 0, &KeyString, KEY_ALL_ACCESS, FALSE);
    if (NT_SUCCESS(Status))
    {
        /* Open the PNP key */
        RtlInitUnicodeString(&KeyString, L"Control\\Pnp");
        Status = HalpOpenRegistryKey(&KeyHandle,
                                     Handle,
                                     &KeyString,
                                     KEY_ALL_ACCESS,
                                     TRUE);
        /* Close root key */
        ZwClose(Handle);

        /* Check if PNP BIOS key exists */
        if (NT_SUCCESS(Status))
        {
            /* Set the disable value to false -- we need the mapper */
            RtlInitUnicodeString(&KeyString, L"DisableFirmwareMapper");
            Status = ZwSetValueKey(KeyHandle,
                                   &KeyString,
                                   0,
                                   REG_DWORD,
                                   &Value,
                                   sizeof(Value));

            /* Close subkey */
            ZwClose(KeyHandle);
        }
    }

    /* Return status */
    return Status;
}

NTSTATUS
NTAPI
HalpOpenRegistryKey(IN PHANDLE KeyHandle,
                    IN HANDLE RootKey,
                    IN PUNICODE_STRING KeyName,
                    IN ACCESS_MASK DesiredAccess,
                    IN BOOLEAN Create)
{
    NTSTATUS Status;
    ULONG Disposition;
    OBJECT_ATTRIBUTES ObjectAttributes;

    /* Setup the attributes we received */
    InitializeObjectAttributes(&ObjectAttributes,
                               KeyName,
                               OBJ_CASE_INSENSITIVE,
                               RootKey,
                               NULL);

    /* What to do? */
    if ( Create )
    {
        /* Create the key */
        Status = ZwCreateKey(KeyHandle,
                             DesiredAccess,
                             &ObjectAttributes,
                             0,
                             NULL,
                             REG_OPTION_VOLATILE,
                             &Disposition);
    }
    else
    {
        /* Open the key */
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    }

    /* We're done */
    return Status;
}
#endif /* !_MINIHAL_ */

VOID
NTAPI
HalpCheckPowerButton(VOID)
{
    //
    // Nothing to do on non-ACPI
    //
    return;
}

VOID
NTAPI
HalpFlushTLB(VOID)
{
    ULONG_PTR Flags, Cr4;
    INT CpuInfo[4];
    ULONG_PTR PageDirectory;

    //
    // Disable interrupts
    //
    Flags = __readeflags();
    _disable();

    //
    // Get page table directory base
    //
    PageDirectory = __readcr3();

    //
    // Check for CPUID support
    //
    if (KeGetCurrentPrcb()->CpuID)
    {
        //
        // Check for global bit in CPU features
        //
        __cpuid(CpuInfo, 1);
        if (CpuInfo[3] & 0x2000)
        {
            //
            // Get current CR4 value
            //
            Cr4 = __readcr4();

            //
            // Disable global bit
            //
            __writecr4(Cr4 & ~CR4_PGE);

            //
            // Flush TLB and re-enable global bit
            //
            __writecr3(PageDirectory);
            __writecr4(Cr4);

            //
            // Restore interrupts
            //
            __writeeflags(Flags);
            return;
        }
    }

    //
    // Legacy: just flush TLB
    //
    __writecr3(PageDirectory);
    __writeeflags(Flags);
}

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
UCHAR
FASTCALL
HalSystemVectorDispatchEntry(IN ULONG Vector,
                             OUT PKINTERRUPT_ROUTINE **FlatDispatch,
                             OUT PKINTERRUPT_ROUTINE *NoConnection)
{
    //
    // Not implemented on x86
    //
    return 0;
}

/*
 * @implemented
 */
VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    //
    // Not implemented on x86
    //
    return;
}

#ifndef _M_AMD64
KIRQL
NTAPI
HalConvertDeviceIdtToIrql(
    _In_ ULONG Vector)
{
    return HalpVectorToIrql((UCHAR)Vector);
}

NTSTATUS
NTAPI
HalGetInterruptTargetInformation(
    _Inout_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    KAFFINITY Mask;
    ULONG ProcessorNumber;

    if (TargetInformation == NULL ||
        TargetInformation->Version != HAL_INTERRUPT_TARGET_INFORMATION_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mask = TargetInformation->TargetProcessors;
    if (Mask == 0)
        Mask = KeQueryActiveProcessors();

    ProcessorNumber = 0;
    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        ProcessorNumber++;
    }

    TargetInformation->ProcessorNumber = ProcessorNumber;
    TargetInformation->TargetProcessors = ((KAFFINITY)1 << ProcessorNumber);
    TargetInformation->DestinationId = ProcessorNumber;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalGetMessageRoutingInfo(
    _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    HAL_INTERRUPT_TARGET_INFORMATION TargetInfo;
    NTSTATUS Status;

    if (RoutingInfo == NULL ||
        RoutingInfo->Version != HAL_MESSAGE_ROUTING_INFO_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (RoutingInfo->Flags & HAL_MSI_ROUTING_ALLOCATE_VECTOR)
        return STATUS_NOT_SUPPORTED;

    if (RoutingInfo->Vector == 0)
        return STATUS_INVALID_PARAMETER;

    TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
    TargetInfo.TargetProcessors = RoutingInfo->TargetProcessors;
    TargetInfo.ProcessorNumber = 0;
    TargetInfo.DestinationId = 0;
    Status = HalGetInterruptTargetInformation(&TargetInfo);
    if (!NT_SUCCESS(Status))
        return Status;

    RoutingInfo->TargetProcessors = TargetInfo.TargetProcessors;
    RoutingInfo->DestinationId = TargetInfo.DestinationId;
    if (RoutingInfo->Irql == 0)
        RoutingInfo->Irql = HalConvertDeviceIdtToIrql(RoutingInfo->Vector);
    RoutingInfo->MessageAddress.QuadPart = 0xFEE00000ULL |
                                           ((ULONGLONG)TargetInfo.DestinationId << 12);
    RoutingInfo->MessageData = (USHORT)(RoutingInfo->Vector & 0xFF);
    return STATUS_SUCCESS;
}
#endif

NTSTATUS
NTAPI
HalGetMemoryCachingRequirements(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ SIZE_T Length,
    _Out_ MEMORY_CACHING_TYPE *CacheType)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Length);

    if (CacheType == NULL)
        return STATUS_INVALID_PARAMETER;

    *CacheType = MmNonCached;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalGetProcessorIdByNtNumber(
    _In_ ULONG ProcessorNumber,
    _Out_ PULONG ProcessorId)
{
    if (ProcessorId == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ProcessorNumber >= MAXIMUM_PROCESSORS)
        return STATUS_INVALID_PARAMETER;

    *ProcessorId = ProcessorNumber;
    return STATUS_SUCCESS;
}

#ifdef _M_AMD64
#undef KfLowerIrql
#undef KeRaiseIrql
#undef KeAcquireSpinLock
#undef KfAcquireSpinLock
#undef KfReleaseSpinLock

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return (KIRQL)__readcr8();
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL OldIrql)
{
    KeLowerIrql(OldIrql);
}

VOID
NTAPI
KeRaiseIrql(
    _In_ KIRQL NewIrql,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KfRaiseIrql(NewIrql);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

VOID
NTAPI
KeAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KeAcquireSpinLockRaiseToDpc(SpinLock);
}

KIRQL
FASTCALL
KfAcquireSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock)
{
    return KeAcquireSpinLockRaiseToDpc(SpinLock);
}

VOID
FASTCALL
KfReleaseSpinLock(
    _Inout_ PKSPIN_LOCK SpinLock,
    _In_ KIRQL OldIrql)
{
    KeReleaseSpinLock(SpinLock, OldIrql);
}
#endif /* _M_AMD64 */

#ifdef _M_IX86
/* x86 fastcall wrappers */

#undef KeRaiseIrql
/*
 * @implemented
 */
VOID
NTAPI
KeRaiseIrql(KIRQL NewIrql,
            PKIRQL OldIrql)
{
    /* Call the fastcall function */
    *OldIrql = KfRaiseIrql(NewIrql);
}

#undef KeLowerIrql
/*
 * @implemented
 */
VOID
NTAPI
KeLowerIrql(KIRQL NewIrql)
{
    /* Call the fastcall function */
    KfLowerIrql(NewIrql);
}

#undef KeAcquireSpinLock
/*
 * @implemented
 */
VOID
NTAPI
KeAcquireSpinLock(PKSPIN_LOCK SpinLock,
                  PKIRQL OldIrql)
{
    /* Call the fastcall function */
    *OldIrql = KfAcquireSpinLock(SpinLock);
}

#undef KeReleaseSpinLock
/*
 * @implemented
 */
VOID
NTAPI
KeReleaseSpinLock(PKSPIN_LOCK SpinLock,
                  KIRQL NewIrql)
{
    /* Call the fastcall function */
    KfReleaseSpinLock(SpinLock, NewIrql);
}

#endif /* _M_IX86 */
