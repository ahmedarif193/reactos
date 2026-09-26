/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halarm64/acpi/halpnpdd.c
 * PURPOSE:         ARM64 hooks for the ACPI HAL bus driver
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <halpnp.h>

#include <wdmguid.h>

#include "../halext.h"

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

#define NDEBUG
#include <debug.h>

typedef struct _HALP_PORT_RANGE_ENTRY
{
    LIST_ENTRY ListEntry;
    USHORT RangeId;
    BOOLEAN IsSparse;
    BOOLEAN PrimaryIsMmio;
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG Length;
} HALP_PORT_RANGE_ENTRY, *PHALP_PORT_RANGE_ENTRY;

/* GLOBALS ********************************************************************/

static LIST_ENTRY HalpPortRangeList = { &HalpPortRangeList, &HalpPortRangeList };
static KSPIN_LOCK HalpPortRangeLock;
static USHORT HalpPortRangeNextId = 1;

/*
 * The HAL claims these vectors in the ACPI device's boot configuration so
 * the arbiter never hands them to a device.
 */
#define HALP_RESERVED_VECTOR_COUNT 1280

/* FUNCTIONS ******************************************************************/

static
BOOLEAN
HalpAcpiResolveInterfaceGas(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex,
    _Out_ GEN_ADDR *Gas)
{
    const GEN_ADDR *Source = NULL;
    GEN_ADDR TempGas;
    ULONG Offset = 0;
    ULONG SegmentLength = 0;
    ULONG Stride = 0;

    switch (RegisterType)
    {
        case PM1a_ENABLE:
            if (!HalpPm1EventBlockValid[0] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Offset = SegmentLength;
            Stride = sizeof(USHORT);
            break;

        case PM1b_ENABLE:
            if (!HalpPm1EventBlockValid[1] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[1];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Offset = SegmentLength;
            Stride = sizeof(USHORT);
            break;

        case PM1a_STATUS:
            if (!HalpPm1EventBlockValid[0] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Stride = sizeof(USHORT);
            break;

        case PM1b_STATUS:
            if (!HalpPm1EventBlockValid[1] || HalpFixedAcpiDescTable.pm1_evt_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1EventBlocks[1];
            SegmentLength = HalpFixedAcpiDescTable.pm1_evt_len / 2;
            Stride = sizeof(USHORT);
            break;

        case PM1a_CONTROL:
            if (!HalpPm1ControlBlockValid[0] || HalpFixedAcpiDescTable.pm1_ctrl_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1ControlBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.pm1_ctrl_len;
            Stride = sizeof(USHORT);
            break;

        case PM1b_CONTROL:
            if (!HalpPm1ControlBlockValid[1] || HalpFixedAcpiDescTable.pm1_ctrl_len < sizeof(USHORT))
                return FALSE;
            Source = &HalpPm1ControlBlocks[1];
            SegmentLength = HalpFixedAcpiDescTable.pm1_ctrl_len;
            Stride = sizeof(USHORT);
            break;

        case GP_STATUS:
            if (!HalpGeneralPurposeBlockValid[0] || HalpFixedAcpiDescTable.gp0_blk_len < 2)
                return FALSE;
            Source = &HalpGeneralPurposeBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.gp0_blk_len / 2;
            Stride = sizeof(UCHAR);
            break;

        case GP_ENABLE:
            if (!HalpGeneralPurposeBlockValid[0] || HalpFixedAcpiDescTable.gp0_blk_len < 2)
                return FALSE;
            Source = &HalpGeneralPurposeBlocks[0];
            SegmentLength = HalpFixedAcpiDescTable.gp0_blk_len / 2;
            Offset = SegmentLength;
            Stride = sizeof(UCHAR);
            break;

        case SMI_CMD:
            if (!HalpFixedAcpiDescTable.smi_cmd_io_port)
                return FALSE;

            RtlZeroMemory(&TempGas, sizeof(TempGas));
            TempGas.AddressSpaceID = ACPI_GAS_SYSTEM_IO;
            TempGas.BitWidth = 8;
            TempGas.Address.QuadPart = HalpFixedAcpiDescTable.smi_cmd_io_port;
            Source = &TempGas;
            Offset = 0;
            SegmentLength = sizeof(UCHAR);
            Stride = sizeof(UCHAR);
            break;

        default:
            return FALSE;
    }

    if (!Source || !Gas || !Stride)
    {
        return FALSE;
    }

    if ((RegisterIndex + 1) * Stride > SegmentLength)
    {
        return FALSE;
    }

    *Gas = *Source;
    Gas->Address.QuadPart += Offset + (RegisterIndex * Stride);
    return TRUE;
}

static
USHORT
HalpAcpiInterfaceReadRegister(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex)
{
    GEN_ADDR Gas;
    ULONG Value;

    if (!HalpAcpiResolveInterfaceGas(RegisterType, RegisterIndex, &Gas))
    {
        return 0;
    }

    if (!HalpAcpiReadRegister(&Gas, &Value))
    {
        return 0;
    }

    return (USHORT)Value;
}

static
VOID
HalpAcpiInterfaceWriteRegister(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex,
    _In_ USHORT Value)
{
    GEN_ADDR Gas;

    if (!HalpAcpiResolveInterfaceGas(RegisterType, RegisterIndex, &Gas))
    {
        return;
    }

    HalpAcpiWriteRegister(&Gas, Value);
}

static
NTSTATUS
HalpPortRangeQueryAllocate(
    _In_ BOOLEAN IsSparse,
    _In_ BOOLEAN PrimaryIsMmio,
    _In_opt_ PVOID VirtBaseAddr,
    _In_ PHYSICAL_ADDRESS PhysBaseAddr,
    _In_ ULONG Length,
    _Out_ PUSHORT NewRangeId)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    USHORT StartId;
    USHORT CandidateId;
    USHORT Attempt;
    BOOLEAN IdAssigned = FALSE;
    PHALP_PORT_RANGE_ENTRY RangeEntry;

    if (!NewRangeId || (Length == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RangeEntry = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RangeEntry), TAG_HAL);
    if (!RangeEntry)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RangeEntry->IsSparse = IsSparse;
    RangeEntry->PrimaryIsMmio = PrimaryIsMmio;
    RangeEntry->VirtualAddress = VirtBaseAddr;
    RangeEntry->PhysicalAddress = PhysBaseAddr;
    RangeEntry->Length = Length;

    KeAcquireSpinLock(&HalpPortRangeLock, &OldIrql);

    StartId = HalpPortRangeNextId ? HalpPortRangeNextId : 1;

    for (Attempt = 0; Attempt < 0xFFFF; Attempt++)
    {
        PHALP_PORT_RANGE_ENTRY ExistingEntry;

        CandidateId = (USHORT)(((StartId - 1 + Attempt) % 0xFFFF) + 1);

        Entry = HalpPortRangeList.Flink;
        while (Entry != &HalpPortRangeList)
        {
            ExistingEntry = CONTAINING_RECORD(Entry, HALP_PORT_RANGE_ENTRY, ListEntry);
            if (ExistingEntry->RangeId == CandidateId)
            {
                break;
            }

            Entry = Entry->Flink;
        }

        if (Entry == &HalpPortRangeList)
        {
            RangeEntry->RangeId = CandidateId;
            HalpPortRangeNextId = CandidateId + 1;
            if (HalpPortRangeNextId == 0)
            {
                HalpPortRangeNextId = 1;
            }

            InsertTailList(&HalpPortRangeList, &RangeEntry->ListEntry);
            IdAssigned = TRUE;
            break;
        }
    }

    KeReleaseSpinLock(&HalpPortRangeLock, OldIrql);

    if (!IdAssigned)
    {
        ExFreePoolWithTag(RangeEntry, TAG_HAL);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *NewRangeId = RangeEntry->RangeId;
    return STATUS_SUCCESS;
}

static
VOID
HalpPortRangeFreeRange(
    _In_ USHORT RangeId)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PHALP_PORT_RANGE_ENTRY RangeEntry;

    KeAcquireSpinLock(&HalpPortRangeLock, &OldIrql);
    Entry = HalpPortRangeList.Flink;
    while (Entry != &HalpPortRangeList)
    {
        RangeEntry = CONTAINING_RECORD(Entry, HALP_PORT_RANGE_ENTRY, ListEntry);
        if (RangeEntry->RangeId == RangeId)
        {
            RemoveEntryList(Entry);
            KeReleaseSpinLock(&HalpPortRangeLock, OldIrql);
            ExFreePoolWithTag(RangeEntry, TAG_HAL);
            return;
        }

        Entry = Entry->Flink;
    }

    KeReleaseSpinLock(&HalpPortRangeLock, OldIrql);
}

NTSTATUS
NTAPI
HalpQueryBusInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ CONST GUID *InterfaceType,
    _In_ USHORT Version,
    _In_ ULONG InterfaceBufferSize,
    _Out_ PINTERFACE Interface,
    _Out_opt_ PULONG Length)
{
    PACPI_REGS_INTERFACE_STANDARD RegInterface;
    PHAL_PORT_RANGE_INTERFACE PortRangeInterface;
    const USHORT ProvidedVersion = 1;

    if (IsEqualIID(InterfaceType, &GUID_ACPI_REGS_INTERFACE_STANDARD))
    {
        if (InterfaceBufferSize < sizeof(*RegInterface))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        if ((Version != 0) && (Version != ProvidedVersion))
        {
            return STATUS_NOT_SUPPORTED;
        }

        RegInterface = (PACPI_REGS_INTERFACE_STANDARD)Interface;
        RtlZeroMemory(RegInterface, sizeof(*RegInterface));
        RegInterface->Size = sizeof(*RegInterface);
        RegInterface->Version = ProvidedVersion;
        RegInterface->Context = DeviceObject;
        RegInterface->InterfaceReference = HalpPnpInterfaceReference;
        RegInterface->InterfaceDereference = HalpPnpInterfaceDereference;
        RegInterface->ReadAcpiRegister = HalpAcpiInterfaceReadRegister;
        RegInterface->WriteAcpiRegister = HalpAcpiInterfaceWriteRegister;
        RegInterface->InterfaceReference(RegInterface->Context);
        if (Length)
        {
            *Length = sizeof(*RegInterface);
        }

        return STATUS_SUCCESS;
    }

    if (IsEqualIID(InterfaceType, &GUID_ACPI_PORT_RANGES_INTERFACE_STANDARD))
    {
        if (InterfaceBufferSize < sizeof(*PortRangeInterface))
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        if ((Version != 0) && (Version != ProvidedVersion))
        {
            return STATUS_NOT_SUPPORTED;
        }

        PortRangeInterface = (PHAL_PORT_RANGE_INTERFACE)Interface;
        RtlZeroMemory(PortRangeInterface, sizeof(*PortRangeInterface));
        PortRangeInterface->Size = sizeof(*PortRangeInterface);
        PortRangeInterface->Version = ProvidedVersion;
        PortRangeInterface->Context = DeviceObject;
        PortRangeInterface->InterfaceReference = HalpPnpInterfaceReference;
        PortRangeInterface->InterfaceDereference = HalpPnpInterfaceDereference;
        PortRangeInterface->QueryAllocateRange = HalpPortRangeQueryAllocate;
        PortRangeInterface->FreeRange = HalpPortRangeFreeRange;
        PortRangeInterface->InterfaceReference(PortRangeInterface->Context);
        if (Length)
        {
            *Length = sizeof(*PortRangeInterface);
        }

        return STATUS_SUCCESS;
    }

    return STATUS_NOT_SUPPORTED;
}

ULONG
NTAPI
HalpQueryReservedResources(
    _Out_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptors)
{
    ULONG i;

    if (Descriptors)
    {
        for (i = 0; i < HALP_RESERVED_VECTOR_COUNT; i++, Descriptors++)
        {
            ULONG Vector = (i < 0x400) ? (0x800 + i) : (0x1000 + (i - 0x400));
            Descriptors->Type = CmResourceTypeInterrupt;
            Descriptors->ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptors->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
            Descriptors->u.Interrupt.Level = Vector;
            Descriptors->u.Interrupt.Vector = Vector;
            Descriptors->u.Interrupt.Affinity = 0xFFFFFFFF;
        }
    }

    return HALP_RESERVED_VECTOR_COUNT;
}

/* EOF */
