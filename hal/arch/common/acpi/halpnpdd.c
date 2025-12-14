/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/acpi/halpnpdd.c
 * PURPOSE:         HAL Plug and Play Device Driver
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <halirq.h>

#include <initguid.h>
#include <wdmguid.h>

#include <ntstrsafe.h>

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

#define NDEBUG
#include <debug.h>

typedef enum _EXTENSION_TYPE
{
    PdoExtensionType = 0xC0,
    FdoExtensionType
} EXTENSION_TYPE;

typedef enum _PDO_TYPE
{
    AcpiPdo = 0x80,
    WdPdo
} PDO_TYPE;

typedef enum _HALP_PNP_STATE
{
    HalpPnpStateNotStarted = 0,
    HalpPnpStateStarted,
    HalpPnpStateStopPending,
    HalpPnpStateStopped,
    HalpPnpStateRemovePending,
    HalpPnpStateSurpriseRemoved,
    HalpPnpStateDeleted
} HALP_PNP_STATE;

typedef struct _FDO_EXTENSION
{
    EXTENSION_TYPE ExtensionType;
    struct _PDO_EXTENSION* ChildPdoList;
    FAST_MUTEX ChildPdoLock;
    FAST_MUTEX PnpStateLock;
    HALP_PNP_STATE PnpState;
    HALP_PNP_STATE PreviousPnpState;
    LONG InterfaceReferenceCount;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT FunctionalDeviceObject;
    PDEVICE_OBJECT AttachedDeviceObject;
} FDO_EXTENSION, *PFDO_EXTENSION;

typedef struct _PDO_EXTENSION
{
    EXTENSION_TYPE ExtensionType;
    struct _PDO_EXTENSION* Next;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PFDO_EXTENSION ParentFdoExtension;
    PDEVICE_OBJECT ParentFdoDeviceObject;
    PDO_TYPE PdoType;
    PDESCRIPTION_HEADER WdTable;
    FAST_MUTEX PnpStateLock;
    HALP_PNP_STATE PnpState;
    HALP_PNP_STATE PreviousPnpState;
    DEVICE_POWER_STATE CurrentDevicePowerState;
    SYSTEM_POWER_STATE CurrentSystemPowerState;
    LONG InterfaceReferenceCount;
} PDO_EXTENSION, *PPDO_EXTENSION;

static
VOID
HalpPdoDetachFromParent(
    _Inout_ PPDO_EXTENSION PdoExtension)
{
    PFDO_EXTENSION ParentFdo;
    PDEVICE_OBJECT ParentFdoDeviceObject;

    ExAcquireFastMutex(&PdoExtension->PnpStateLock);
    ParentFdo = PdoExtension->ParentFdoExtension;
    ParentFdoDeviceObject = PdoExtension->ParentFdoDeviceObject;
    PdoExtension->ParentFdoExtension = NULL;
    PdoExtension->ParentFdoDeviceObject = NULL;
    ExReleaseFastMutex(&PdoExtension->PnpStateLock);

    if (ParentFdo)
    {
        PPDO_EXTENSION *Link;

        ExAcquireFastMutex(&ParentFdo->ChildPdoLock);
        Link = &ParentFdo->ChildPdoList;
        while (*Link)
        {
            if (*Link == PdoExtension)
            {
                *Link = PdoExtension->Next;
                break;
            }

            Link = &(*Link)->Next;
        }
        ExReleaseFastMutex(&ParentFdo->ChildPdoLock);
    }

    if (ParentFdoDeviceObject)
    {
        ObDereferenceObject(ParentFdoDeviceObject);
    }
}

/* GLOBALS ********************************************************************/

PDRIVER_OBJECT HalpDriverObject;
static LIST_ENTRY HalpPortRangeList;
static KSPIN_LOCK HalpPortRangeLock;
static USHORT HalpPortRangeNextId = 1;
static KEVENT HalpPortRangeInitEvent;
static volatile LONG HalpPortRangeInitState;

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

static
VOID
NTAPI
HalpAcpiInterfaceReference(
    _In_opt_ PVOID Context)
{
    PFDO_EXTENSION DeviceExtension;
    EXTENSION_TYPE ExtensionType;
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)Context;

    if (!DeviceObject)
    {
        return;
    }

    ObReferenceObject(DeviceObject);

    DeviceExtension = (PFDO_EXTENSION)DeviceObject->DeviceExtension;
    if (!DeviceExtension)
    {
        ObDereferenceObject(DeviceObject);
        return;
    }

    ExtensionType = DeviceExtension->ExtensionType;
    if (ExtensionType == FdoExtensionType)
    {
        InterlockedIncrement(&DeviceExtension->InterfaceReferenceCount);
    }
    else if (ExtensionType == PdoExtensionType)
    {
        InterlockedIncrement(&((PPDO_EXTENSION)DeviceExtension)->InterfaceReferenceCount);
    }
}

static
VOID
NTAPI
HalpAcpiInterfaceDereference(
    _In_opt_ PVOID Context)
{
    PFDO_EXTENSION DeviceExtension;
    EXTENSION_TYPE ExtensionType;
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)Context;

    if (!DeviceObject)
    {
        return;
    }

    DeviceExtension = (PFDO_EXTENSION)DeviceObject->DeviceExtension;
    if (DeviceExtension)
    {
        ExtensionType = DeviceExtension->ExtensionType;
        if (ExtensionType == FdoExtensionType)
        {
            InterlockedDecrement(&DeviceExtension->InterfaceReferenceCount);
        }
        else if (ExtensionType == PdoExtensionType)
        {
            InterlockedDecrement(&((PPDO_EXTENSION)DeviceExtension)->InterfaceReferenceCount);
        }
    }

    ObDereferenceObject(DeviceObject);
}

static
VOID
HalpAcpiInitPortRangeList(VOID)
{
    LONG State;

    State = InterlockedCompareExchange(&HalpPortRangeInitState, 1, 0);
    if (State == 0)
    {
        InitializeListHead(&HalpPortRangeList);
        KeInitializeSpinLock(&HalpPortRangeLock);
        HalpPortRangeNextId = 1;

        KeMemoryBarrier();
        InterlockedExchange(&HalpPortRangeInitState, 2);
        KeSetEvent(&HalpPortRangeInitEvent, IO_NO_INCREMENT, FALSE);
        return;
    }

    if (State == 2)
    {
        return;
    }

    KeWaitForSingleObject(&HalpPortRangeInitEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
}

static
BOOLEAN
HalpAcpiResolveInterfaceGas(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex,
    _Out_ GEN_ADDR *Gas);

static
USHORT
HalpAcpiInterfaceReadRegister(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex);

static
VOID
HalpAcpiInterfaceWriteRegister(
    _In_ ACPI_REG_TYPE RegisterType,
    _In_ ULONG RegisterIndex,
    _In_ USHORT Value);

static
NTSTATUS
HalpPortRangeQueryAllocate(
    _In_ BOOLEAN IsSparse,
    _In_ BOOLEAN PrimaryIsMmio,
    _In_opt_ PVOID VirtBaseAddr,
    _In_ PHYSICAL_ADDRESS PhysBaseAddr,
    _In_ ULONG Length,
    _Out_ PUSHORT NewRangeId);

static
VOID
HalpPortRangeFreeRange(
    _In_ USHORT RangeId);

/* PRIVATE FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
HalpAddDevice(IN PDRIVER_OBJECT DriverObject,
              IN PDEVICE_OBJECT TargetDevice)
{
    NTSTATUS Status;
    PFDO_EXTENSION FdoExtension;
    PPDO_EXTENSION PdoExtension;
    PDEVICE_OBJECT DeviceObject, AttachedDevice;
    PDEVICE_OBJECT PdoDeviceObject;
    PDESCRIPTION_HEADER Wdrt;

    DPRINT("HAL: PnP Driver ADD!\n");

    /* Create the FDO */
    Status = IoCreateDevice(DriverObject,
                            sizeof(FDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* Should not happen */
        DbgBreakPoint();
        return Status;
    }

    /* Setup the FDO extension */
    FdoExtension = DeviceObject->DeviceExtension;
    FdoExtension->ExtensionType = FdoExtensionType;
    FdoExtension->PhysicalDeviceObject = TargetDevice;
    FdoExtension->FunctionalDeviceObject = DeviceObject;
    FdoExtension->ChildPdoList = NULL;
    ExInitializeFastMutex(&FdoExtension->ChildPdoLock);
    ExInitializeFastMutex(&FdoExtension->PnpStateLock);
    FdoExtension->PnpState = HalpPnpStateNotStarted;
    FdoExtension->PreviousPnpState = HalpPnpStateNotStarted;
    FdoExtension->InterfaceReferenceCount = 0;

    /* Attach to the physical device object (the bus) */
    AttachedDevice = IoAttachDeviceToDeviceStack(DeviceObject, TargetDevice);
    if (!AttachedDevice)
    {
        /* Failed, undo everything */
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    /* Save the attachment */
    FdoExtension->AttachedDeviceObject = AttachedDevice;

    /* Create the PDO */
    Status = IoCreateDevice(DriverObject,
                            sizeof(PDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_AUTOGENERATED_DEVICE_NAME,
                            FALSE,
                            &PdoDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        DPRINT1("HAL: Could not create ACPI device object status=0x%08x\n", Status);
        IoDetachDevice(FdoExtension->AttachedDeviceObject);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    /* Setup the PDO device extension */
    PdoExtension = PdoDeviceObject->DeviceExtension;
    PdoExtension->ExtensionType = PdoExtensionType;
    PdoExtension->PhysicalDeviceObject = PdoDeviceObject;
    PdoExtension->ParentFdoExtension = FdoExtension;
    PdoExtension->ParentFdoDeviceObject = DeviceObject;
    ObReferenceObject(DeviceObject);
    PdoExtension->PdoType = AcpiPdo;
    ExInitializeFastMutex(&PdoExtension->PnpStateLock);
    PdoExtension->PnpState = HalpPnpStateNotStarted;
    PdoExtension->PreviousPnpState = HalpPnpStateNotStarted;
    PdoExtension->CurrentDevicePowerState = PowerDeviceD3;
    PdoExtension->CurrentSystemPowerState = PowerSystemWorking;
    PdoExtension->InterfaceReferenceCount = 0;

    /* Add the PDO to the head of the list */
    ExAcquireFastMutex(&FdoExtension->ChildPdoLock);
    PdoExtension->Next = FdoExtension->ChildPdoList;
    FdoExtension->ChildPdoList = PdoExtension;
    ExReleaseFastMutex(&FdoExtension->ChildPdoLock);

    /* Initialization is finished */
    PdoDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    /* Find the ACPI watchdog table */
    Wdrt = HalAcpiGetTable(0, 'TRDW');
    if (Wdrt)
    {
        /* FIXME: TODO */
        DPRINT1("You have an ACPI Watchdog. That's great! You should be proud ;-)\n");
    }

    /* Return status */
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    DPRINT("Device added %lx\n", Status);
    return Status;
}

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

    if (HalpPortRangeInitState != 2)
    {
        return STATUS_DEVICE_NOT_READY;
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

    if (HalpPortRangeInitState != 2)
    {
        return;
    }

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

static
BOOLEAN
HalpIsPortRangeListEmpty(VOID)
{
    KIRQL OldIrql;
    BOOLEAN IsEmpty;

    if (HalpPortRangeInitState != 2)
    {
        return TRUE;
    }

    KeAcquireSpinLock(&HalpPortRangeLock, &OldIrql);
    IsEmpty = IsListEmpty(&HalpPortRangeList);
    KeReleaseSpinLock(&HalpPortRangeLock, OldIrql);

    return IsEmpty;
}

static
BOOLEAN
HalpIsDeviceQuiesced(
    _In_opt_ PDEVICE_OBJECT DeviceObject)
{
    PFDO_EXTENSION DeviceExtension;
    EXTENSION_TYPE ExtensionType;
    LONG InterfaceRefs;

    if (!DeviceObject)
    {
        return TRUE;
    }

    DeviceExtension = (PFDO_EXTENSION)DeviceObject->DeviceExtension;
    if (!DeviceExtension)
    {
        return TRUE;
    }

    ExtensionType = DeviceExtension->ExtensionType;
    if (ExtensionType == FdoExtensionType)
    {
        InterfaceRefs = InterlockedCompareExchange(&DeviceExtension->InterfaceReferenceCount, 0, 0);
    }
    else if (ExtensionType == PdoExtensionType)
    {
        InterfaceRefs = InterlockedCompareExchange(&((PPDO_EXTENSION)DeviceExtension)->InterfaceReferenceCount, 0, 0);
    }
    else
    {
        InterfaceRefs = 0;
    }

    if (InterfaceRefs != 0)
    {
        return FALSE;
    }

    if (!HalpIsPortRangeListEmpty())
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
NTAPI
HalpQueryInterface(IN PDEVICE_OBJECT DeviceObject,
                   IN CONST GUID* InterfaceType,
                   IN USHORT Version,
                   IN PVOID InterfaceSpecificData,
                   IN ULONG InterfaceBufferSize,
                   IN PINTERFACE Interface,
                   OUT PULONG Length)
{
    PACPI_REGS_INTERFACE_STANDARD RegInterface;
    PHAL_PORT_RANGE_INTERFACE PortRangeInterface;

    UNREFERENCED_PARAMETER(InterfaceSpecificData);

    HalpAcpiInitPortRangeList();

    if (IsEqualIID(InterfaceType, &GUID_ACPI_REGS_INTERFACE_STANDARD))
    {
        const USHORT ProvidedVersion = 1;

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
        RegInterface->InterfaceReference = HalpAcpiInterfaceReference;
        RegInterface->InterfaceDereference = HalpAcpiInterfaceDereference;
        RegInterface->ReadAcpiRegister = HalpAcpiInterfaceReadRegister;
        RegInterface->WriteAcpiRegister = HalpAcpiInterfaceWriteRegister;
        RegInterface->InterfaceReference(RegInterface->Context);
        if (Length)
        {
            *Length = sizeof(*RegInterface);
        }

        return STATUS_SUCCESS;
    }
    else if (IsEqualIID(InterfaceType, &GUID_ACPI_PORT_RANGES_INTERFACE_STANDARD))
    {
        const USHORT ProvidedVersion = 1;

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
        PortRangeInterface->InterfaceReference = HalpAcpiInterfaceReference;
        PortRangeInterface->InterfaceDereference = HalpAcpiInterfaceDereference;
        PortRangeInterface->QueryAllocateRange = HalpPortRangeQueryAllocate;
        PortRangeInterface->FreeRange = HalpPortRangeFreeRange;
        PortRangeInterface->InterfaceReference(PortRangeInterface->Context);
        if (Length)
        {
            *Length = sizeof(*PortRangeInterface);
        }

        return STATUS_SUCCESS;
    }

    DPRINT1("HalpQueryInterface({%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}) is UNIMPLEMENTED\n",
            InterfaceType->Data1, InterfaceType->Data2, InterfaceType->Data3,
            InterfaceType->Data4[0], InterfaceType->Data4[1],
            InterfaceType->Data4[2], InterfaceType->Data4[3],
            InterfaceType->Data4[4], InterfaceType->Data4[5],
            InterfaceType->Data4[6], InterfaceType->Data4[7]);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalpQueryDeviceRelations(IN PDEVICE_OBJECT DeviceObject,
                         IN DEVICE_RELATION_TYPE RelationType,
                         OUT PDEVICE_RELATIONS* DeviceRelations)
{
    EXTENSION_TYPE ExtensionType;
    PPDO_EXTENSION PdoExtension;
    PFDO_EXTENSION FdoExtension;
    PDEVICE_RELATIONS PdoRelations, FdoRelations;
    PDEVICE_OBJECT* ObjectEntry;
    ULONG i = 0, PdoCount = 0;

    /* Get FDO device extension and PDO count */
    FdoExtension = DeviceObject->DeviceExtension;
    ExtensionType = FdoExtension->ExtensionType;

    /* What do they want? */
    if (RelationType == BusRelations)
    {
        /* This better be an FDO */
        if (ExtensionType == FdoExtensionType)
        {
            ExAcquireFastMutex(&FdoExtension->ChildPdoLock);

            /* Count how many PDOs we have */
            PdoExtension = FdoExtension->ChildPdoList;
            while (PdoExtension)
            {
                /* Next one */
                PdoExtension = PdoExtension->Next;
                PdoCount++;
            }

            /* Add the PDOs that already exist in the device relations */
            if (*DeviceRelations)
            {
                PdoCount += (*DeviceRelations)->Count;
            }

            /* Allocate our structure */
            FdoRelations = ExAllocatePoolWithTag(PagedPool,
                                                 FIELD_OFFSET(DEVICE_RELATIONS,
                                                              Objects) +
                                                 sizeof(PDEVICE_OBJECT) * PdoCount,
                                                 TAG_HAL);
            if (!FdoRelations)
            {
                ExReleaseFastMutex(&FdoExtension->ChildPdoLock);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            /* Save our count */
            FdoRelations->Count = PdoCount;

            /* Query existing relations */
            ObjectEntry = FdoRelations->Objects;
            if (*DeviceRelations)
            {
                /* Check if there were any */
                if ((*DeviceRelations)->Count)
                {
                    /* Loop them all */
                    do
                    {
                        /* Copy into our structure */
                        *ObjectEntry++ = (*DeviceRelations)->Objects[i];
                    }
                    while (++i < (*DeviceRelations)->Count);
                }

                /* Free existing structure */
                ExFreePool(*DeviceRelations);
            }

            /* Now check if we have a PDO list */
            PdoExtension = FdoExtension->ChildPdoList;
            if (PdoExtension)
            {
                /* Loop the PDOs */
                do
                {
                    /* Save our own PDO and reference it */
                    *ObjectEntry++ = PdoExtension->PhysicalDeviceObject;
                    ObReferenceObject(PdoExtension->PhysicalDeviceObject);

                    /* Go to our next PDO */
                    PdoExtension = PdoExtension->Next;
                }
                while (PdoExtension);
            }

            ExReleaseFastMutex(&FdoExtension->ChildPdoLock);

            /* Return the new structure */
            *DeviceRelations = FdoRelations;
            return STATUS_SUCCESS;
        }
    }
    else
    {
        /* The only other thing we support is a target relation for the PDO */
        if ((RelationType == TargetDeviceRelation) &&
            (ExtensionType == PdoExtensionType))
        {
            /* Only one entry */
            PdoRelations = ExAllocatePoolWithTag(PagedPool,
                                                 sizeof(DEVICE_RELATIONS),
                                                 TAG_HAL);
            if (!PdoRelations) return STATUS_INSUFFICIENT_RESOURCES;

            /* Fill it out and reference us */
            PdoRelations->Count = 1;
            PdoRelations->Objects[0] = DeviceObject;
            ObReferenceObject(DeviceObject);

            /* Return it */
            *DeviceRelations = PdoRelations;
            return STATUS_SUCCESS;
        }
    }

    /* We don't support anything else */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalpQueryCapabilities(IN PDEVICE_OBJECT DeviceObject,
                      OUT PDEVICE_CAPABILITIES Capabilities)
{
    PFDO_EXTENSION DeviceExtension;
    PPDO_EXTENSION PdoExtension;
    NTSTATUS Status;
    ULONG i;
    PAGED_CODE();

    /* Get the extension and check for valid version */
    DeviceExtension = DeviceObject->DeviceExtension;
    PdoExtension = NULL;
    if (DeviceExtension && (DeviceExtension->ExtensionType == PdoExtensionType))
    {
        PdoExtension = (PPDO_EXTENSION)DeviceExtension;
    }
    ASSERT(Capabilities->Version == 1);
    if (Capabilities->Version == 1)
    {
        /* Can't lock or eject us */
        Capabilities->LockSupported = FALSE;
        Capabilities->EjectSupported = FALSE;

        /* Can't remove or dock us */
        Capabilities->Removable = FALSE;
        Capabilities->DockDevice = FALSE;

        /* Can't access us raw */
        Capabilities->RawDeviceOK = FALSE;

        /* We have a unique ID, and don't bother the user */
        Capabilities->UniqueID = TRUE;
        Capabilities->SilentInstall = TRUE;

        /* HAL devices are not dynamically managed */
        Capabilities->NonDynamic = TRUE;
        Capabilities->NoDisplayInUI = TRUE;

        /* Fill out the address */
        Capabilities->Address = InterfaceTypeUndefined;
        Capabilities->UINumber = InterfaceTypeUndefined;

        /* Default to no intermediate D-states */
        Capabilities->DeviceD1 = FALSE;
        Capabilities->DeviceD2 = FALSE;

        Capabilities->WakeFromD0 = FALSE;
        Capabilities->WakeFromD1 = FALSE;
        Capabilities->WakeFromD2 = FALSE;
        Capabilities->WakeFromD3 = FALSE;
        Capabilities->WakeFromInterrupt = FALSE;

        Capabilities->SystemWake = PowerSystemUnspecified;
        Capabilities->DeviceWake = PowerDeviceUnspecified;

        /* Fill out latencies (ms). D3 is conservative for ACPI transitions. */
        Capabilities->D1Latency = 0;
        Capabilities->D2Latency = 0;
        Capabilities->D3Latency = 100;

        /* Fill out supported device states */
        for (i = 0; i < PowerSystemMaximum; i++)
        {
            Capabilities->DeviceState[i] = PowerDeviceD3;
        }

        if (PdoExtension && (PdoExtension->PdoType == WdPdo))
        {
            /* Watchdog is not power-manageable by the user */
            Capabilities->NoDisplayInUI = TRUE;
        }

        Capabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;

        /* Done */
        Status = STATUS_SUCCESS;
    }
    else
    {
        /* Fail */
        Status = STATUS_NOT_SUPPORTED;
    }

    /* Return status */
    return Status;
}

NTSTATUS
NTAPI
HalpQueryResources(IN PDEVICE_OBJECT DeviceObject,
                   OUT PCM_RESOURCE_LIST *Resources)
{
    PPDO_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;
    PCM_RESOURCE_LIST ResourceList;
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDesc;
    ULONG i;
    PAGED_CODE();

    /* Only the ACPI PDO has requirements */
    if (DeviceExtension->PdoType == AcpiPdo)
    {
        /* Query ACPI requirements */
        Status = HalpQueryAcpiResourceRequirements(&RequirementsList);
        if (!NT_SUCCESS(Status)) return Status;

        ASSERT(RequirementsList->AlternativeLists == 1);

        /* Allocate the resourcel ist */
        ResourceList = ExAllocatePoolWithTag(PagedPool,
                                             sizeof(CM_RESOURCE_LIST),
                                             TAG_HAL);
        if (!ResourceList )
        {
            /* Fail, no memory */
            Status = STATUS_INSUFFICIENT_RESOURCES;
            ExFreePoolWithTag(RequirementsList, TAG_HAL);
            return Status;
        }

        /* Initialize it */
        RtlZeroMemory(ResourceList, sizeof(CM_RESOURCE_LIST));
        ResourceList->Count = 1;

        /* Setup the list fields */
        ResourceList->List[0].BusNumber = -1;
        ResourceList->List[0].InterfaceType = PNPBus;
        ResourceList->List[0].PartialResourceList.Version = 1;
        ResourceList->List[0].PartialResourceList.Revision = 1;
        ResourceList->List[0].PartialResourceList.Count = 0;

        /* Setup the first descriptor */
        PartialDesc = ResourceList->List[0].PartialResourceList.PartialDescriptors;

        /* Find the requirement descriptor for the SCI */
        for (i = 0; i < RequirementsList->List[0].Count; i++)
        {
            /* Get this descriptor */
            Descriptor = &RequirementsList->List[0].Descriptors[i];
            if (Descriptor->Type == CmResourceTypeInterrupt)
            {
                /* Copy requirements descriptor into resource descriptor */
                PartialDesc->Type = CmResourceTypeInterrupt;
                PartialDesc->ShareDisposition = Descriptor->ShareDisposition;
                PartialDesc->Flags = Descriptor->Flags;
                ASSERT(Descriptor->u.Interrupt.MinimumVector ==
                       Descriptor->u.Interrupt.MaximumVector);

                {
                    ULONG Gsi;
                    ULONG SystemVector;
                    KAFFINITY InterruptAffinity;

                    Gsi = Descriptor->u.Interrupt.MinimumVector;
                    SystemVector = 0;
                    InterruptAffinity = (HalpDefaultInterruptAffinity != 0) ?
                                         HalpDefaultInterruptAffinity :
                                         (KAFFINITY)-1;

                    if (Gsi <= 0xFF)
                    {
                        SystemVector = HalpIrqToVector((UCHAR)Gsi);

                        if (SystemVector == 0)
                        {
                            KIRQL AllocatedIrql = 0;
                            KAFFINITY AllocatedAffinity = 0;
                            ULONG AllocatedVector;

                            AllocatedVector = HalpGetRootInterruptVector(Gsi,
                                                                        Gsi,
                                                                        &AllocatedIrql,
                                                                        &AllocatedAffinity);
                            if (AllocatedVector != 0)
                            {
                                SystemVector = AllocatedVector;
                                if (AllocatedAffinity != 0)
                                {
                                    InterruptAffinity = AllocatedAffinity;
                                }
                            }
                        }
                    }

                    PartialDesc->u.Interrupt.Vector =
                        ((SystemVector != 0) && (SystemVector <= 0xFF)) ? SystemVector : Gsi;
                    PartialDesc->u.Interrupt.Level = Gsi;
                    PartialDesc->u.Interrupt.Affinity = InterruptAffinity;
                }

                ResourceList->List[0].PartialResourceList.Count++;

                break;
            }
        }

        /* Return resources and success */
        *Resources = ResourceList;

        ExFreePoolWithTag(RequirementsList, TAG_HAL);

        return STATUS_SUCCESS;
    }
    else if (DeviceExtension->PdoType == WdPdo)
    {
        /* Watchdog doesn't */
        return STATUS_NOT_SUPPORTED;
    }
    else
    {
        /* This shouldn't happen */
        return STATUS_UNSUCCESSFUL;
    }
}

NTSTATUS
NTAPI
HalpQueryResourceRequirements(IN PDEVICE_OBJECT DeviceObject,
                              OUT PIO_RESOURCE_REQUIREMENTS_LIST *Requirements)
{
    PPDO_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PAGED_CODE();

    /* Only the ACPI PDO has requirements */
    if (DeviceExtension->PdoType == AcpiPdo)
    {
        /* Query ACPI requirements */
        return HalpQueryAcpiResourceRequirements(Requirements);
    }
    else if (DeviceExtension->PdoType == WdPdo)
    {
        /* Watchdog doesn't */
        return STATUS_NOT_SUPPORTED;
    }
    else
    {
        /* This shouldn't happen */
        return STATUS_UNSUCCESSFUL;
    }
}

NTSTATUS
NTAPI
HalpQueryIdPdo(IN PDEVICE_OBJECT DeviceObject,
               IN BUS_QUERY_ID_TYPE IdType,
               OUT PUSHORT *BusQueryId)
{
    PPDO_EXTENSION PdoExtension;
    PDO_TYPE PdoType;
    const WCHAR *Ids[2];
    ULONG IdCount;
    BOOLEAN MultiSz;
    PWCHAR Buffer;
    PWCHAR Current;
    SIZE_T TotalChars;
    SIZE_T TotalBytes;
    SIZE_T RemainingBytes;
    ULONG i;
    NTSTATUS Status;

    if (!BusQueryId)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Get the PDO type */
    PdoExtension = DeviceObject->DeviceExtension;
    PdoType = PdoExtension->PdoType;

    /* What kind of ID is being requested? */
    DPRINT("ID: %d\n", IdType);
    switch (IdType)
    {
        case BusQueryDeviceID:

            MultiSz = FALSE;
            IdCount = 1;
            if (PdoType == AcpiPdo)
            {
                Ids[0] = L"ACPI_HAL\\PNP0C08";
            }
            else if (PdoType == WdPdo)
            {
                Ids[0] = L"ACPI_HAL\\PNP0C18";
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
            break;

        case BusQueryHardwareIDs:

            MultiSz = TRUE;
            IdCount = 2;
            if (PdoType == AcpiPdo)
            {
                Ids[0] = L"ACPI_HAL\\PNP0C08";
                Ids[1] = L"*PNP0C08";
            }
            else if (PdoType == WdPdo)
            {
                Ids[0] = L"ACPI_HAL\\PNP0C18";
                Ids[1] = L"*PNP0C18";
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
            break;

        case BusQueryInstanceID:

            MultiSz = FALSE;
            IdCount = 1;
            Ids[0] = L"0";
            break;

        default:

            /* We don't support anything else */
            return STATUS_NOT_SUPPORTED;
    }

    TotalChars = 0;
    for (i = 0; i < IdCount; i++)
    {
        TotalChars += wcslen(Ids[i]) + 1;
    }

    if (MultiSz)
    {
        TotalChars += 1;
    }

    TotalBytes = TotalChars * sizeof(WCHAR);
    Buffer = ExAllocatePoolZero(PagedPool, TotalBytes, TAG_HAL);
    if (!Buffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Current = Buffer;
    RemainingBytes = TotalBytes;
    for (i = 0; i < IdCount; i++)
    {
        SIZE_T AdvanceChars;

        Status = RtlStringCbCopyW(Current, RemainingBytes, Ids[i]);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Buffer, TAG_HAL);
            return Status;
        }

        AdvanceChars = wcslen(Ids[i]) + 1;
        Current += AdvanceChars;
        RemainingBytes -= AdvanceChars * sizeof(WCHAR);
    }

    *BusQueryId = (PUSHORT)Buffer;
    DPRINT("Returning: %S\n", (PWCHAR)*BusQueryId);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpQueryIdFdo(IN PDEVICE_OBJECT DeviceObject,
               IN BUS_QUERY_ID_TYPE IdType,
               OUT PUSHORT *BusQueryId)
{
    NTSTATUS Status;
    SIZE_T Length;
    PWCHAR Id;
    PWCHAR Buffer;

    /* What kind of ID is being requested? */
    DPRINT("ID: %d\n", IdType);
    switch (IdType)
    {
        case BusQueryDeviceID:
        case BusQueryHardwareIDs:

            /* This is our hardware ID */
            Id = HalHardwareIdString;
            break;

        case BusQueryInstanceID:

            /* And our instance ID */
            Id = L"0";
            break;

        default:

            /* We don't support anything else */
            return STATUS_NOT_SUPPORTED;
    }

    /* Calculate the length */
    Length = (wcslen(Id) * sizeof(WCHAR)) + sizeof(UNICODE_NULL);

    /* Allocate the buffer */
    Buffer = ExAllocatePoolWithTag(PagedPool,
                                   Length + sizeof(UNICODE_NULL),
                                   TAG_HAL);
    if (Buffer)
    {
        /* Copy the string and null-terminate it */
        RtlCopyMemory(Buffer, Id, Length);
        Buffer[Length / sizeof(WCHAR)] = UNICODE_NULL;

        /* Return string */
        *BusQueryId = Buffer;
        Status = STATUS_SUCCESS;
        DPRINT("Returning: %S\n", *BusQueryId);
    }
    else
    {
        /* Fail */
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Return status */
    return Status;
}

NTSTATUS
NTAPI
HalpDispatchPnp(IN PDEVICE_OBJECT DeviceObject,
                IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStackLocation;
    //PPDO_EXTENSION PdoExtension;
    PFDO_EXTENSION FdoExtension;
    NTSTATUS Status;
    UCHAR Minor;

    /* Get the device extension and stack location */
    FdoExtension = DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    Minor = IoStackLocation->MinorFunction;

    /* FDO? */
    if (FdoExtension->ExtensionType == FdoExtensionType)
    {
        /* Query the IRP type */
        switch (Minor)
        {
            case IRP_MN_REMOVE_DEVICE:

                DPRINT("Remove device received for FDO\n");

                /*
                 * Orphan children before deleting the FDO so they won't touch
                 * freed FDO extension memory during late-remove paths.
                 */
                ExAcquireFastMutex(&FdoExtension->ChildPdoLock);
                {
                    PPDO_EXTENSION CurrentPdo;

                    CurrentPdo = FdoExtension->ChildPdoList;
                    FdoExtension->ChildPdoList = NULL;
                    while (CurrentPdo)
                    {
                        ExAcquireFastMutex(&CurrentPdo->PnpStateLock);
                        CurrentPdo->ParentFdoExtension = NULL;
                        ExReleaseFastMutex(&CurrentPdo->PnpStateLock);

                        CurrentPdo = CurrentPdo->Next;
                    }
                }
                ExReleaseFastMutex(&FdoExtension->ChildPdoLock);

                IoSkipCurrentIrpStackLocation(Irp);
                Status = IoCallDriver(FdoExtension->AttachedDeviceObject, Irp);
                IoDetachDevice(FdoExtension->AttachedDeviceObject);
                IoDeleteDevice(DeviceObject);
                return Status;

            case IRP_MN_QUERY_DEVICE_RELATIONS:

                /* Call the worker */
                DPRINT("Querying device relations for FDO\n");
                Status = HalpQueryDeviceRelations(DeviceObject,
                                                  IoStackLocation->Parameters.QueryDeviceRelations.Type,
                                                  (PVOID)&Irp->IoStatus.Information);
                break;

            case IRP_MN_QUERY_INTERFACE:

                /* Call the worker */
                DPRINT("Querying interface for FDO\n");
                Status = HalpQueryInterface(DeviceObject,
                                            IoStackLocation->Parameters.QueryInterface.InterfaceType,
                                            IoStackLocation->Parameters.QueryInterface.Size,
                                            IoStackLocation->Parameters.QueryInterface.InterfaceSpecificData,
                                            IoStackLocation->Parameters.QueryInterface.Version,
                                            IoStackLocation->Parameters.QueryInterface.Interface,
                                            (PVOID)&Irp->IoStatus.Information);
                break;


            case IRP_MN_QUERY_ID:

                /* Call the worker */
                DPRINT("Querying ID for FDO\n");
                Status = HalpQueryIdFdo(DeviceObject,
                                        IoStackLocation->Parameters.QueryId.IdType,
                                        (PVOID)&Irp->IoStatus.Information);
                break;

            default:

                DPRINT("Other IRP: %lx\n", Minor);
                Status = STATUS_NOT_SUPPORTED;
                break;
        }

        /* What happpened? */
        if ((NT_SUCCESS(Status)) || (Status == STATUS_NOT_SUPPORTED))
        {
            /* Set the IRP status, unless this isn't understood */
            if (Status != STATUS_NOT_SUPPORTED)
            {
                Irp->IoStatus.Status = Status;
            }

            /* Pass it on */
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(FdoExtension->AttachedDeviceObject, Irp);
        }

        /* Otherwise, we failed, so set the status and complete the request */
        DPRINT1("IRP failed with status: %lx\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    else
    {
        /* This is a PDO instead */
        ASSERT(FdoExtension->ExtensionType == PdoExtensionType);
        PPDO_EXTENSION PdoExtension = (PPDO_EXTENSION)FdoExtension;
        /* Query the IRP type */
        Status = STATUS_SUCCESS;
        switch (Minor)
        {
            case IRP_MN_START_DEVICE:

                DPRINT1("Start device received\n");
                ExAcquireFastMutex(&PdoExtension->PnpStateLock);
                PdoExtension->PreviousPnpState = PdoExtension->PnpState;
                PdoExtension->PnpState = HalpPnpStateStarted;
                PdoExtension->CurrentDevicePowerState = PowerDeviceD0;
                ExReleaseFastMutex(&PdoExtension->PnpStateLock);
                break;

            case IRP_MN_REMOVE_DEVICE:

                /* Check if this is a PCI device */
                DPRINT1("Remove device received\n");

                ExAcquireFastMutex(&PdoExtension->PnpStateLock);
                PdoExtension->PreviousPnpState = PdoExtension->PnpState;
                PdoExtension->PnpState = HalpPnpStateDeleted;
                ExReleaseFastMutex(&PdoExtension->PnpStateLock);

                HalpPdoDetachFromParent(PdoExtension);

                /* We're done */
                Status = STATUS_SUCCESS;
                break;

            case IRP_MN_QUERY_REMOVE_DEVICE:
            case IRP_MN_QUERY_STOP_DEVICE:

                DPRINT1("Query stop/remove received\n");
                ExAcquireFastMutex(&PdoExtension->PnpStateLock);

                if (Minor == IRP_MN_QUERY_STOP_DEVICE)
                {
                    if (PdoExtension->PnpState == HalpPnpStateStarted)
                    {
                        if (HalpIsDeviceQuiesced(DeviceObject))
                        {
                            PdoExtension->PreviousPnpState = PdoExtension->PnpState;
                            PdoExtension->PnpState = HalpPnpStateStopPending;
                            Status = STATUS_SUCCESS;
                        }
                        else
                        {
                            Status = STATUS_DEVICE_BUSY;
                        }
                    }
                    else if ((PdoExtension->PnpState == HalpPnpStateStopPending) ||
                             (PdoExtension->PnpState == HalpPnpStateStopped) ||
                             (PdoExtension->PnpState == HalpPnpStateNotStarted))
                    {
                        Status = STATUS_SUCCESS;
                    }
                    else
                    {
                        Status = STATUS_INVALID_DEVICE_STATE;
                    }
                }
                else if (Minor == IRP_MN_QUERY_REMOVE_DEVICE)
                {
                    if ((PdoExtension->PnpState == HalpPnpStateStarted) ||
                        (PdoExtension->PnpState == HalpPnpStateStopped) ||
                        (PdoExtension->PnpState == HalpPnpStateNotStarted))
                    {
                        if (HalpIsDeviceQuiesced(DeviceObject))
                        {
                            PdoExtension->PreviousPnpState = PdoExtension->PnpState;
                            PdoExtension->PnpState = HalpPnpStateRemovePending;
                            Status = STATUS_SUCCESS;
                        }
                        else
                        {
                            Status = STATUS_DEVICE_BUSY;
                        }
                    }
                    else if (PdoExtension->PnpState == HalpPnpStateRemovePending)
                    {
                        Status = STATUS_SUCCESS;
                    }
                    else
                    {
                        Status = STATUS_INVALID_DEVICE_STATE;
                    }
                }
                else
                {
                    Status = STATUS_INVALID_DEVICE_STATE;
                }

                ExReleaseFastMutex(&PdoExtension->PnpStateLock);
                break;

            case IRP_MN_CANCEL_REMOVE_DEVICE:
            case IRP_MN_CANCEL_STOP_DEVICE:

                DPRINT1("Cancel stop/remove received\n");
                ExAcquireFastMutex(&PdoExtension->PnpStateLock);

                if ((Minor == IRP_MN_CANCEL_STOP_DEVICE) &&
                    (PdoExtension->PnpState == HalpPnpStateStopPending))
                {
                    PdoExtension->PnpState = PdoExtension->PreviousPnpState;
                }
                else if ((Minor == IRP_MN_CANCEL_REMOVE_DEVICE) &&
                         (PdoExtension->PnpState == HalpPnpStateRemovePending))
                {
                    PdoExtension->PnpState = PdoExtension->PreviousPnpState;
                }

                ExReleaseFastMutex(&PdoExtension->PnpStateLock);
                Status = STATUS_SUCCESS;
                break;

            case IRP_MN_STOP_DEVICE:

                DPRINT1("Stop device received\n");
                ExAcquireFastMutex(&PdoExtension->PnpStateLock);
                PdoExtension->PreviousPnpState = PdoExtension->PnpState;
                PdoExtension->PnpState = HalpPnpStateStopped;
                PdoExtension->CurrentDevicePowerState = PowerDeviceD3;
                ExReleaseFastMutex(&PdoExtension->PnpStateLock);
                Status = STATUS_SUCCESS;
                break;

            case IRP_MN_SURPRISE_REMOVAL:

                /* Inherit whatever status we had */
                DPRINT1("Surprise removal IRP\n");
                ExAcquireFastMutex(&PdoExtension->PnpStateLock);
                PdoExtension->PreviousPnpState = PdoExtension->PnpState;
                PdoExtension->PnpState = HalpPnpStateSurpriseRemoved;
                ExReleaseFastMutex(&PdoExtension->PnpStateLock);

                HalpPdoDetachFromParent(PdoExtension);

                Status = STATUS_SUCCESS;
                break;

            case IRP_MN_QUERY_DEVICE_RELATIONS:

                /* Query the device relations */
                DPRINT("Querying PDO relations\n");
                Status = HalpQueryDeviceRelations(DeviceObject,
                                                  IoStackLocation->Parameters.QueryDeviceRelations.Type,
                                                  (PVOID)&Irp->IoStatus.Information);
                break;

            case IRP_MN_QUERY_INTERFACE:

                /* Call the worker */
                DPRINT("Querying interface for PDO\n");
                Status = HalpQueryInterface(DeviceObject,
                                            IoStackLocation->Parameters.QueryInterface.InterfaceType,
                                            IoStackLocation->Parameters.QueryInterface.Size,
                                            IoStackLocation->Parameters.QueryInterface.InterfaceSpecificData,
                                            IoStackLocation->Parameters.QueryInterface.Version,
                                            IoStackLocation->Parameters.QueryInterface.Interface,
                                            (PVOID)&Irp->IoStatus.Information);
                break;

            case IRP_MN_QUERY_CAPABILITIES:

                /* Call the worker */
                DPRINT("Querying the capabilities for the PDO\n");
                Status = HalpQueryCapabilities(DeviceObject,
                                               IoStackLocation->Parameters.DeviceCapabilities.Capabilities);
                break;

            case IRP_MN_QUERY_RESOURCES:

                /* Call the worker */
                DPRINT("Querying the resources for the PDO\n");
                Status = HalpQueryResources(DeviceObject, (PVOID)&Irp->IoStatus.Information);
                break;

            case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:

                /* Call the worker */
                DPRINT("Querying the resource requirements for the PDO\n");
                Status = HalpQueryResourceRequirements(DeviceObject,
                                                       (PVOID)&Irp->IoStatus.Information);
                break;

            case IRP_MN_QUERY_ID:

                /* Call the worker */
                DPRINT("Query the ID for the PDO\n");
                Status = HalpQueryIdPdo(DeviceObject,
                                        IoStackLocation->Parameters.QueryId.IdType,
                                        (PVOID)&Irp->IoStatus.Information);
                break;

            default:

                /* We don't handle anything else, so inherit the old state */
                DPRINT("Illegal IRP: %lx\n", Minor);
                Status = Irp->IoStatus.Status;
                break;
        }

        /* If it's not supported, inherit the old status */
        if (Status == STATUS_NOT_SUPPORTED) Status = Irp->IoStatus.Status;

        /* Complete the IRP */
        DPRINT("IRP completed with status: %lx\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        if ((Minor == IRP_MN_REMOVE_DEVICE) && NT_SUCCESS(Status))
        {
            IoDeleteDevice(DeviceObject);
        }
        return Status;
    }
}

NTSTATUS
NTAPI
HalpDispatchWmi(IN PDEVICE_OBJECT DeviceObject,
                IN PIRP Irp)
{
    PFDO_EXTENSION FdoExtension;

    DPRINT("HAL: PnP Driver WMI!\n");

    FdoExtension = DeviceObject->DeviceExtension;
    if (FdoExtension->ExtensionType == FdoExtensionType)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(FdoExtension->AttachedDeviceObject, Irp);
    }

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalpDispatchPower(IN PDEVICE_OBJECT DeviceObject,
                  IN PIRP Irp)
{
    PFDO_EXTENSION DeviceExtension;
    PPDO_EXTENSION PdoExtension;
    PIO_STACK_LOCATION IoStackLocation;
    POWER_STATE PowerState;
    POWER_STATE_TYPE PowerType;
    NTSTATUS Status;

    DPRINT("HAL: PnP Driver Power!\n");
    DeviceExtension = DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceExtension->ExtensionType == FdoExtensionType)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(DeviceExtension->AttachedDeviceObject, Irp);
    }

    ASSERT(DeviceExtension->ExtensionType == PdoExtensionType);
    PdoExtension = (PPDO_EXTENSION)DeviceExtension;

    Status = STATUS_SUCCESS;
    switch (IoStackLocation->MinorFunction)
    {
        case IRP_MN_SET_POWER:

            PowerType = IoStackLocation->Parameters.Power.Type;
            PowerState = IoStackLocation->Parameters.Power.State;
            if (PowerType == DevicePowerState)
            {
                PdoExtension->CurrentDevicePowerState = PowerState.DeviceState;
                PoSetPowerState(DeviceObject, DevicePowerState, PowerState);
            }
            else if (PowerType == SystemPowerState)
            {
                PdoExtension->CurrentSystemPowerState = PowerState.SystemState;
            }

            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_POWER:

            Status = STATUS_SUCCESS;
            break;

        default:

            Status = STATUS_SUCCESS;
            break;
    }

    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
HalpDriverEntry(IN PDRIVER_OBJECT DriverObject,
                IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    PDEVICE_OBJECT TargetDevice = NULL;

    DPRINT("HAL: PnP Driver ENTRY!\n");

    KeInitializeEvent(&HalpPortRangeInitEvent, NotificationEvent, FALSE);
    HalpPortRangeInitState = 0;

    /* This is us */
    HalpDriverObject = DriverObject;

    /* Set up add device */
    DriverObject->DriverExtension->AddDevice = HalpAddDevice;

    /* Set up the callouts */
    DriverObject->MajorFunction[IRP_MJ_PNP] = HalpDispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = HalpDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = HalpDispatchWmi;

    /* Create the PDO and tell the PnP manager about us*/
    Status = IoReportDetectedDevice(DriverObject,
                                    InterfaceTypeUndefined,
                                    -1,
                                    -1,
                                    NULL,
                                    NULL,
                                    FALSE,
                                    &TargetDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    TargetDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    /* Set up the device stack */
    Status = HalpAddDevice(DriverObject, TargetDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(TargetDevice);
        return Status;
    }

    /* Return to kernel */
    return Status;
}

NTSTATUS
NTAPI
HaliInitPnpDriver(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING DriverString;
    PAGED_CODE();

    /* Create the driver */
    RtlInitUnicodeString(&DriverString, L"\\Driver\\ACPI_HAL");
    Status = IoCreateDriver(&DriverString, HalpDriverEntry);

    /* Return status */
    return Status;
}

/* EOF */
