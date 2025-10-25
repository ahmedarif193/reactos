/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/legacy/bus/pcibus.c
 * PURPOSE:         PCI Bus Support (Configuration Space, Resource Allocation)
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <reactos/hal/acpi_pci.h>
#define NDEBUG
#include <debug.h>

#define HALP_PCI_DEFAULT_IO_BASE          0x0ULL
#define HALP_PCI_DEFAULT_IO_LIMIT         0xFFFFULL
#define HALP_PCI_DEFAULT_MEM_BASE         0xC0000000ULL
#define HALP_PCI_DEFAULT_MEM_LIMIT        0xFEBFFFFFULL
#define HALP_PCI_GSI_TAG                  'isGH'

typedef struct _HALP_PCI_GSI_INFO
{
    BOOLEAN Valid;
    UCHAR Polarity;
    UCHAR Trigger;
} HALP_PCI_GSI_INFO, *PHALP_PCI_GSI_INFO;

static PHALP_PCI_GSI_INFO HalpPciGsiInfo;
static ULONG HalpPciGsiCapacity;
static PHAL_ACPI_PCI_ROUTE_QUERY HalpPciRouteQueryCallback;

static VOID
HalpPciUpdateBridgeHierarchy(
    PBUS_HANDLER BusHandler);

static
VOID
HalpPciResetGsiTable(VOID)
{
    if (HalpPciGsiInfo)
    {
        ExFreePoolWithTag(HalpPciGsiInfo, HALP_PCI_GSI_TAG);
        HalpPciGsiInfo = NULL;
    }

    HalpPciGsiCapacity = 0;
}

static
BOOLEAN
HalpPciEnsureGsiCapacity(
    _In_ ULONG Gsi)
{
    ULONG Required = Gsi + 1;
    ULONG NewCapacity;
    PHALP_PCI_GSI_INFO NewTable;

    if (Required <= HalpPciGsiCapacity)
    {
        return TRUE;
    }

    NewCapacity = (HalpPciGsiCapacity != 0) ? HalpPciGsiCapacity : 64;
    while (NewCapacity < Required)
    {
        NewCapacity *= 2;
    }

    NewTable = ExAllocatePoolWithTag(NonPagedPool,
                                     NewCapacity * sizeof(HALP_PCI_GSI_INFO),
                                     HALP_PCI_GSI_TAG);
    if (!NewTable)
    {
        DPRINT1("HAL: Failed to grow PCI GSI table to %lu entries\n", NewCapacity);
        return FALSE;
    }

    RtlZeroMemory(NewTable, NewCapacity * sizeof(HALP_PCI_GSI_INFO));
    if (HalpPciGsiInfo)
    {
        RtlCopyMemory(NewTable,
                      HalpPciGsiInfo,
                      HalpPciGsiCapacity * sizeof(HALP_PCI_GSI_INFO));
        ExFreePoolWithTag(HalpPciGsiInfo, HALP_PCI_GSI_TAG);
    }

    HalpPciGsiInfo = NewTable;
    HalpPciGsiCapacity = NewCapacity;
    return TRUE;
}

static
VOID
HalpPciRecordGsiInfo(
    _In_ ULONG Gsi,
    _In_ UCHAR Polarity,
    _In_ UCHAR Trigger)
{
    if (!HalpPciEnsureGsiCapacity(Gsi))
    {
        return;
    }

    HalpPciGsiInfo[Gsi].Valid = TRUE;
    HalpPciGsiInfo[Gsi].Polarity = Polarity;
    HalpPciGsiInfo[Gsi].Trigger = Trigger;
}

BOOLEAN
HalpPciLookupGsiInfo(
    _In_ ULONG Gsi,
    _Out_ PUCHAR Polarity,
    _Out_ PUCHAR Trigger)
{
    if ((Gsi >= HalpPciGsiCapacity) ||
        !HalpPciGsiInfo ||
        !HalpPciGsiInfo[Gsi].Valid)
    {
        return FALSE;
    }

    if (Polarity) *Polarity = HalpPciGsiInfo[Gsi].Polarity;
    if (Trigger) *Trigger = HalpPciGsiInfo[Gsi].Trigger;
    return TRUE;
}

VOID
NTAPI
HalpRegisterPciRouteQuery(
    _In_opt_ PHAL_ACPI_PCI_ROUTE_QUERY Provider)
{
    HalpPciRouteQueryCallback = Provider;
    HalpPciResetGsiTable();
}

VOID
NTAPI
HalpConfigurePciRootBridge(
    _In_ const HAL_ACPI_PCI_ROOT_INFO *Info)
{
    PBUS_HANDLER Bus;
    PPCIPBUSDATA BusData;

    if (!Info)
    {
        return;
    }

    Bus = HalHandlerForBus(PCIBus, Info->Bus);
    if (!Bus)
    {
        DPRINT1("HAL: ACPI reported PCI segment %lu bus %lu which has no handler\n",
                Info->Segment,
                Info->Bus);
        return;
    }

    BusData = (PPCIPBUSDATA)Bus->BusData;
    if (!BusData)
    {
        return;
    }

    BusData->PciSegment = (USHORT)Info->Segment;

    if (Info->IoWindow.Present)
    {
        BusData->IoWindowBase = Info->IoWindow.Base;
        BusData->IoWindowLimit = Info->IoWindow.Limit;
        BusData->IoBase = Info->IoWindow.Base;
        BusData->IoLimit = Info->IoWindow.Limit;
        BusData->IoNext = Info->IoWindow.Base;
    }
    else
    {
        BusData->IoWindowBase = (ULONGLONG)-1;
        BusData->IoWindowLimit = 0;
        BusData->IoBase = HALP_PCI_DEFAULT_IO_BASE;
        BusData->IoLimit = HALP_PCI_DEFAULT_IO_LIMIT;
        BusData->IoNext = BusData->IoBase;
    }

    if (Info->MemoryWindow.Present)
    {
        BusData->MemoryWindowBase = Info->MemoryWindow.Base;
        BusData->MemoryWindowLimit = Info->MemoryWindow.Limit;
        BusData->MemoryBase = Info->MemoryWindow.Base;
        BusData->MemoryLimit = Info->MemoryWindow.Limit;
        BusData->MemoryNext = Info->MemoryWindow.Base;
    }
    else
    {
        BusData->MemoryWindowBase = (ULONGLONG)-1;
        BusData->MemoryWindowLimit = 0;
        BusData->MemoryBase = HALP_PCI_DEFAULT_MEM_BASE;
        BusData->MemoryLimit = HALP_PCI_DEFAULT_MEM_LIMIT;
        BusData->MemoryNext = BusData->MemoryBase;
    }

    if (Info->PrefetchWindow.Present)
    {
        BusData->PrefetchWindowBase = Info->PrefetchWindow.Base;
        BusData->PrefetchWindowLimit = Info->PrefetchWindow.Limit;
    }
    else
    {
        BusData->PrefetchWindowBase = (ULONGLONG)-1;
        BusData->PrefetchWindowLimit = 0;
    }

    BusData->AcpiRootConfigured = TRUE;
    BusData->ResourcesInitialized = TRUE;

    if (Bus->BusAddresses)
    {
        PSUPPORTED_RANGES Ranges = Bus->BusAddresses;

        Ranges->Sorted = TRUE;
        Ranges->IO.Next = NULL;
        Ranges->Memory.Next = NULL;
        Ranges->PrefetchMemory.Next = NULL;

        Ranges->NoIO = 1;
        Ranges->IO.Base = Info->IoWindow.Present ? Info->IoWindow.Base : 0;
        Ranges->IO.Limit = Info->IoWindow.Present ? Info->IoWindow.Limit : BusData->IoLimit;
        Ranges->IO.SystemBase = 0;
        Ranges->IO.SystemAddressSpace = 1;

        Ranges->NoMemory = 1;
        Ranges->Memory.Base = Info->MemoryWindow.Present ? Info->MemoryWindow.Base : 0;
        Ranges->Memory.Limit = Info->MemoryWindow.Present ? Info->MemoryWindow.Limit : BusData->MemoryLimit;
        Ranges->Memory.SystemBase = 0;
        Ranges->Memory.SystemAddressSpace = 0;

        if (Info->PrefetchWindow.Present)
        {
            Ranges->NoPrefetchMemory = 1;
            Ranges->PrefetchMemory.Base = Info->PrefetchWindow.Base;
            Ranges->PrefetchMemory.Limit = Info->PrefetchWindow.Limit;
        }
        else
        {
            Ranges->NoPrefetchMemory = 0;
            Ranges->PrefetchMemory.Base = 0;
            Ranges->PrefetchMemory.Limit = 0;
        }
        Ranges->PrefetchMemory.SystemBase = 0;
        Ranges->PrefetchMemory.SystemAddressSpace = 0;
    }

    HalpPciUpdateBridgeHierarchy(Bus);
}

static __inline ULONGLONG
HalpAlignUp(ULONGLONG Value,
            ULONGLONG Alignment)
{
    return (Value + (Alignment - 1)) & ~(Alignment - 1);
}

static VOID
HalpPciEnsureRangeInitialized(
    PPCIPBUSDATA BusData)
{
    ASSERT(BusData != NULL);

    if (!BusData->ResourcesInitialized)
    {
        BusData->IoBase = HALP_PCI_DEFAULT_IO_BASE;
        BusData->IoLimit = HALP_PCI_DEFAULT_IO_LIMIT;
        BusData->IoNext = BusData->IoBase;
        BusData->MemoryBase = HALP_PCI_DEFAULT_MEM_BASE;
        BusData->MemoryLimit = HALP_PCI_DEFAULT_MEM_LIMIT;
        BusData->MemoryNext = BusData->MemoryBase;
        BusData->IoWindowBase = (ULONGLONG)-1;
        BusData->IoWindowLimit = 0;
        BusData->MemoryWindowBase = (ULONGLONG)-1;
        BusData->MemoryWindowLimit = 0;
        BusData->PrefetchWindowBase = (ULONGLONG)-1;
        BusData->PrefetchWindowLimit = 0;
        BusData->ResourcesInitialized = TRUE;
    }
}

static ULONGLONG
HalpPciAllocateIoRange(
    PPCIPBUSDATA BusData,
    ULONGLONG Length)
{
    ULONGLONG Base;
    ULONGLONG Alignment;

    HalpPciEnsureRangeInitialized(BusData);

    Alignment = (Length < 4) ? 4 : Length;
    Base = HalpAlignUp(BusData->IoNext, Alignment);
    if ((Base + Length - 1) > BusData->IoLimit)
    {
        DPRINT1("HAL: Exhausted PCI I/O aperture allocating 0x%I64x bytes\n", Length);
        return 0;
    }

    BusData->IoNext = Base + Length;
    return Base;
}

static ULONGLONG
HalpPciAllocateMemoryRange(
    PPCIPBUSDATA BusData,
    ULONGLONG Length)
{
    ULONGLONG Base;

    HalpPciEnsureRangeInitialized(BusData);

    Base = HalpAlignUp(BusData->MemoryNext, Length);
    if ((Base + Length - 1) > BusData->MemoryLimit)
    {
        DPRINT1("HAL: Exhausted PCI MMIO aperture allocating 0x%I64x bytes\n", Length);
        return 0;
    }

    BusData->MemoryNext = Base + Length;
    return Base;
}

static ULONGLONG
HalpPciBarLength(
    ULONGLONG Mask,
    BOOLEAN IsIo)
{
    ULONGLONG AddressMask;
    ULONGLONG Size;

    AddressMask = IsIo ? (ULONGLONG)PCI_ADDRESS_IO_ADDRESS_MASK
                       : 0xFFFFFFFFFFFFFFF0ULL;

    Size = Mask & AddressMask;
    if (!Size) return 0;

    Size = Size & ~(Size - 1);
    return Size;
}

static VOID
HalpPciPropagateUsage(
    PBUS_HANDLER BusHandler,
    BOOLEAN IsIo,
    BOOLEAN IsPrefetch,
    ULONGLONG Base,
    ULONGLONG Limit);

static VOID
HalpPciUpdateBridgeHierarchy(
    PBUS_HANDLER BusHandler);

/* GLOBALS *******************************************************************/

extern BOOLEAN HalpPciLockSettings;
ULONG HalpBusType;

BOOLEAN HalpPCIConfigInitialized;
ULONG HalpMinPciBus, HalpMaxPciBus;
KSPIN_LOCK HalpPCIConfigLock;
PCI_CONFIG_HANDLER PCIConfigHandler;

/* PCI Operation Matrix */
UCHAR PCIDeref[4][4] =
{
    {0, 1, 2, 2},   // ULONG-aligned offset
    {1, 1, 1, 1},   // UCHAR-aligned offset
    {2, 1, 2, 2},   // USHORT-aligned offset
    {1, 1, 1, 1}    // UCHAR-aligned offset
};

/* Type 1 PCI Bus */
PCI_CONFIG_HANDLER PCIConfigHandlerType1 =
{
    /* Synchronization */
    (FncSync)HalpPCISynchronizeType1,
    (FncReleaseSync)HalpPCIReleaseSynchronzationType1,

    /* Read */
    {
        (FncConfigIO)HalpPCIReadUlongType1,
        (FncConfigIO)HalpPCIReadUcharType1,
        (FncConfigIO)HalpPCIReadUshortType1
    },

    /* Write */
    {
        (FncConfigIO)HalpPCIWriteUlongType1,
        (FncConfigIO)HalpPCIWriteUcharType1,
        (FncConfigIO)HalpPCIWriteUshortType1
    }
};

/* Type 2 PCI Bus */
PCI_CONFIG_HANDLER PCIConfigHandlerType2 =
{
    /* Synchronization */
    (FncSync)HalpPCISynchronizeType2,
    (FncReleaseSync)HalpPCIReleaseSynchronizationType2,

    /* Read */
    {
        (FncConfigIO)HalpPCIReadUlongType2,
        (FncConfigIO)HalpPCIReadUcharType2,
        (FncConfigIO)HalpPCIReadUshortType2
    },

    /* Write */
    {
        (FncConfigIO)HalpPCIWriteUlongType2,
        (FncConfigIO)HalpPCIWriteUcharType2,
        (FncConfigIO)HalpPCIWriteUshortType2
    }
};

PCIPBUSDATA HalpFakePciBusData =
{
    {
        PCI_DATA_TAG,
        PCI_DATA_VERSION,
        HalpReadPCIConfig,
        HalpWritePCIConfig,
        NULL,
        NULL,
        {{{0, 0, 0}}},
        {0, 0, 0, 0}
    },
    {{0, 0}},
    32,
};

BUS_HANDLER HalpFakePciBusHandler =
{
    1,
    PCIBus,
    PCIConfiguration,
    0,
    NULL,
    NULL,
    &HalpFakePciBusData,
    0,
    NULL,
    {0, 0, 0, 0},
    (PGETSETBUSDATA)HalpGetPCIData,
    (PGETSETBUSDATA)HalpSetPCIData,
    NULL,
    HalpAssignPCISlotResources,
    NULL,
    NULL
};

/* TYPE 1 FUNCTIONS **********************************************************/

VOID
NTAPI
HalpPCISynchronizeType1(IN PBUS_HANDLER BusHandler,
                        IN PCI_SLOT_NUMBER Slot,
                        OUT PKIRQL OldIrql,
                        OUT PPCI_TYPE1_CFG_BITS PciCfg1)
{
    /* Setup the PCI Configuration Register */
    PciCfg1->u.AsULONG = 0;
    PciCfg1->u.bits.BusNumber = BusHandler->BusNumber;
    PciCfg1->u.bits.DeviceNumber = Slot.u.bits.DeviceNumber;
    PciCfg1->u.bits.FunctionNumber = Slot.u.bits.FunctionNumber;
    PciCfg1->u.bits.Enable = TRUE;

    /* Acquire the lock */
    KeRaiseIrql(HIGH_LEVEL, OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpPCIConfigLock);
}

VOID
NTAPI
HalpPCIReleaseSynchronzationType1(IN PBUS_HANDLER BusHandler,
                                  IN KIRQL OldIrql)
{
    PCI_TYPE1_CFG_BITS PciCfg1;

    /* Clear the PCI Configuration Register */
    PciCfg1.u.AsULONG = 0;
    WRITE_PORT_ULONG(((PPCIPBUSDATA)BusHandler->BusData)->Config.Type1.Address,
                     PciCfg1.u.AsULONG);

    /* Release the lock */
    KeReleaseSpinLock(&HalpPCIConfigLock, OldIrql);
}

TYPE1_READ(HalpPCIReadUcharType1, UCHAR)
TYPE1_READ(HalpPCIReadUshortType1, USHORT)
TYPE1_READ(HalpPCIReadUlongType1, ULONG)
TYPE1_WRITE(HalpPCIWriteUcharType1, UCHAR)
TYPE1_WRITE(HalpPCIWriteUshortType1, USHORT)
TYPE1_WRITE(HalpPCIWriteUlongType1, ULONG)

/* TYPE 2 FUNCTIONS **********************************************************/

VOID
NTAPI
HalpPCISynchronizeType2(IN PBUS_HANDLER BusHandler,
                        IN PCI_SLOT_NUMBER Slot,
                        OUT PKIRQL OldIrql,
                        OUT PPCI_TYPE2_ADDRESS_BITS PciCfg)
{
    PCI_TYPE2_CSE_BITS PciCfg2Cse;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;

    /* Setup the configuration register */
    PciCfg->u.AsUSHORT = 0;
    PciCfg->u.bits.Agent = (USHORT)Slot.u.bits.DeviceNumber;
    PciCfg->u.bits.AddressBase = (USHORT)BusData->Config.Type2.Base;

    /* Acquire the lock */
    KeRaiseIrql(HIGH_LEVEL, OldIrql);
    KeAcquireSpinLockAtDpcLevel(&HalpPCIConfigLock);

    /* Setup the CSE Register */
    PciCfg2Cse.u.AsUCHAR = 0;
    PciCfg2Cse.u.bits.Enable = TRUE;
    PciCfg2Cse.u.bits.FunctionNumber = (UCHAR)Slot.u.bits.FunctionNumber;
    PciCfg2Cse.u.bits.Key = -1;

    /* Write the bus number and CSE */
    WRITE_PORT_UCHAR(BusData->Config.Type2.Forward,
                     (UCHAR)BusHandler->BusNumber);
    WRITE_PORT_UCHAR(BusData->Config.Type2.CSE, PciCfg2Cse.u.AsUCHAR);
}

VOID
NTAPI
HalpPCIReleaseSynchronizationType2(IN PBUS_HANDLER BusHandler,
                                   IN KIRQL OldIrql)
{
    PCI_TYPE2_CSE_BITS PciCfg2Cse;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;

    /* Clear CSE and bus number */
    PciCfg2Cse.u.AsUCHAR = 0;
    WRITE_PORT_UCHAR(BusData->Config.Type2.CSE, PciCfg2Cse.u.AsUCHAR);
    WRITE_PORT_UCHAR(BusData->Config.Type2.Forward, 0);

    /* Release the lock */
    KeReleaseSpinLock(&HalpPCIConfigLock, OldIrql);
}

TYPE2_READ(HalpPCIReadUcharType2, UCHAR)
TYPE2_READ(HalpPCIReadUshortType2, USHORT)
TYPE2_READ(HalpPCIReadUlongType2, ULONG)
TYPE2_WRITE(HalpPCIWriteUcharType2, UCHAR)
TYPE2_WRITE(HalpPCIWriteUshortType2, USHORT)
TYPE2_WRITE(HalpPCIWriteUlongType2, ULONG)

/* PCI CONFIGURATION SPACE ***************************************************/

VOID
NTAPI
HalpPCIConfig(IN PBUS_HANDLER BusHandler,
              IN PCI_SLOT_NUMBER Slot,
              IN PUCHAR Buffer,
              IN ULONG Offset,
              IN ULONG Length,
              IN FncConfigIO *ConfigIO)
{
    KIRQL OldIrql;
    ULONG i;
    UCHAR State[20];

    /* Synchronize the operation */
    PCIConfigHandler.Synchronize(BusHandler, Slot, &OldIrql, State);

    /* Loop every increment */
    while (Length)
    {
        /* Find out the type of read/write we need to do */
        i = PCIDeref[Offset % sizeof(ULONG)][Length % sizeof(ULONG)];

        /* Do the read/write and return the number of bytes */
        i = ConfigIO[i]((PPCIPBUSDATA)BusHandler->BusData,
                        State,
                        Buffer,
                        Offset);

        /* Increment the buffer position and offset, and decrease the length */
        Offset += i;
        Buffer += i;
        Length -= i;
    }

    /* Release the lock and PCI bus */
    PCIConfigHandler.ReleaseSynchronzation(BusHandler, OldIrql);
}

VOID
NTAPI
HalpReadPCIConfig(IN PBUS_HANDLER BusHandler,
                  IN PCI_SLOT_NUMBER Slot,
                  IN PVOID Buffer,
                  IN ULONG Offset,
                  IN ULONG Length)
{
    /* Validate the PCI Slot */
    if (!HalpValidPCISlot(BusHandler, Slot))
    {
        /* Fill the buffer with invalid data */
        RtlFillMemory(Buffer, Length, -1);
    }
    else
    {
        /* Send the request */
        HalpPCIConfig(BusHandler,
                      Slot,
                      Buffer,
                      Offset,
                      Length,
                      PCIConfigHandler.ConfigRead);
    }
}

VOID
NTAPI
HalpWritePCIConfig(IN PBUS_HANDLER BusHandler,
                   IN PCI_SLOT_NUMBER Slot,
                   IN PVOID Buffer,
                   IN ULONG Offset,
                   IN ULONG Length)
{
    /* Validate the PCI Slot */
    if (HalpValidPCISlot(BusHandler, Slot))
    {
        /* Send the request */
        HalpPCIConfig(BusHandler,
                      Slot,
                      Buffer,
                      Offset,
                      Length,
                      PCIConfigHandler.ConfigWrite);
    }
}

#ifdef SARCH_XBOX
static
BOOLEAN
HalpXboxBlacklistedPCISlot(
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER Slot)
{
    /* Trying to get PCI config data from devices 0:0:1 and 0:0:2 will completely
     * hang the Xbox. Also, the device number doesn't seem to be decoded for the
     * video card, so it appears to be present on 1:0:0 - 1:31:0.
     * We hack around these problems by indicating "device not present" for devices
     * 0:0:1, 0:0:2, 1:1:0, 1:2:0, 1:3:0, ...., 1:31:0 */
    if ((BusNumber == 0 && Slot.u.bits.DeviceNumber == 0 &&
        (Slot.u.bits.FunctionNumber == 1 || Slot.u.bits.FunctionNumber == 2)) ||
        (BusNumber == 1 && Slot.u.bits.DeviceNumber != 0))
    {
        DPRINT("Blacklisted PCI slot (%d:%d:%d)\n",
               BusNumber, Slot.u.bits.DeviceNumber, Slot.u.bits.FunctionNumber);
        return TRUE;
    }

    return FALSE;
}
#endif

BOOLEAN
NTAPI
HalpValidPCISlot(IN PBUS_HANDLER BusHandler,
                 IN PCI_SLOT_NUMBER Slot)
{
    PCI_SLOT_NUMBER MultiSlot;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;
    UCHAR HeaderType;
    //ULONG Device;

    /* Simple validation */
    if (Slot.u.bits.Reserved) return FALSE;
    if (Slot.u.bits.DeviceNumber >= BusData->MaxDevice) return FALSE;

#ifdef SARCH_XBOX
    if (HalpXboxBlacklistedPCISlot(BusHandler->BusNumber, Slot))
        return FALSE;
#endif

    /* Function 0 doesn't need checking */
    if (!Slot.u.bits.FunctionNumber) return TRUE;

    /* Functions 0+ need Multi-Function support, so check the slot */
    //Device = Slot.u.bits.DeviceNumber;
    MultiSlot = Slot;
    MultiSlot.u.bits.FunctionNumber = 0;

    /* Send function 0 request to get the header back */
    HalpReadPCIConfig(BusHandler,
                      MultiSlot,
                      &HeaderType,
                      FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType),
                      sizeof(UCHAR));

    /* Now make sure the header is multi-function */
    if (!(HeaderType & PCI_MULTIFUNCTION) || (HeaderType == 0xFF)) return FALSE;
    return TRUE;
}

CODE_SEG("INIT")
ULONG
HalpPhase0GetPciDataByOffset(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER PciSlot,
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    ULONG BytesLeft = Length;
    PUCHAR BufferPtr = Buffer;
    PCI_TYPE1_CFG_BITS PciCfg;

#ifdef SARCH_XBOX
    if (HalpXboxBlacklistedPCISlot(Bus, PciSlot))
    {
        RtlFillMemory(Buffer, Length, 0xFF);
        return Length;
    }
#endif

    PciCfg.u.AsULONG = 0;
    PciCfg.u.bits.BusNumber = Bus;
    PciCfg.u.bits.DeviceNumber = PciSlot.u.bits.DeviceNumber;
    PciCfg.u.bits.FunctionNumber = PciSlot.u.bits.FunctionNumber;
    PciCfg.u.bits.Enable = TRUE;

    while (BytesLeft)
    {
        ULONG i;

        PciCfg.u.bits.RegisterNumber = Offset / sizeof(ULONG);
        WRITE_PORT_ULONG((PULONG)PCI_TYPE1_ADDRESS_PORT, PciCfg.u.AsULONG);

        i = PCIDeref[Offset % sizeof(ULONG)][BytesLeft % sizeof(ULONG)];
        switch (i)
        {
            case 0:
            {
                *(PULONG)BufferPtr = READ_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT);

                /* Number of bytes read */
                i = sizeof(ULONG);
                break;
            }
            case 1:
            {
                *BufferPtr = READ_PORT_UCHAR((PUCHAR)(PCI_TYPE1_DATA_PORT +
                                             Offset % sizeof(ULONG)));
                break;
            }
            case 2:
            {
                *(PUSHORT)BufferPtr = READ_PORT_USHORT((PUSHORT)(PCI_TYPE1_DATA_PORT +
                                                                 Offset % sizeof(ULONG)));
                break;
            }

            DEFAULT_UNREACHABLE;
        }

        Offset += i;
        BufferPtr += i;
        BytesLeft -= i;
    }

    return Length;
}

CODE_SEG("INIT")
ULONG
HalpPhase0SetPciDataByOffset(
    _In_ ULONG Bus,
    _In_ PCI_SLOT_NUMBER PciSlot,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    ULONG BytesLeft = Length;
    PUCHAR BufferPtr = Buffer;
    PCI_TYPE1_CFG_BITS PciCfg;

#ifdef SARCH_XBOX
    if (HalpXboxBlacklistedPCISlot(Bus, PciSlot))
    {
        return 0;
    }
#endif

    PciCfg.u.AsULONG = 0;
    PciCfg.u.bits.BusNumber = Bus;
    PciCfg.u.bits.DeviceNumber = PciSlot.u.bits.DeviceNumber;
    PciCfg.u.bits.FunctionNumber = PciSlot.u.bits.FunctionNumber;
    PciCfg.u.bits.Enable = TRUE;

    while (BytesLeft)
    {
        ULONG i;

        PciCfg.u.bits.RegisterNumber = Offset / sizeof(ULONG);
        WRITE_PORT_ULONG((PULONG)PCI_TYPE1_ADDRESS_PORT, PciCfg.u.AsULONG);

        i = PCIDeref[Offset % sizeof(ULONG)][BytesLeft % sizeof(ULONG)];
        switch (i)
        {
            case 0:
            {
                WRITE_PORT_ULONG((PULONG)PCI_TYPE1_DATA_PORT, *(PULONG)BufferPtr);

                /* Number of bytes written */
                i = sizeof(ULONG);
                break;
            }
            case 1:
            {
                WRITE_PORT_UCHAR((PUCHAR)(PCI_TYPE1_DATA_PORT + Offset % sizeof(ULONG)),
                                 *BufferPtr);
                break;
            }
            case 2:
            {
                WRITE_PORT_USHORT((PUSHORT)(PCI_TYPE1_DATA_PORT + Offset % sizeof(ULONG)),
                                  *(PUSHORT)BufferPtr);
                break;
            }

            DEFAULT_UNREACHABLE;
        }

        Offset += i;
        BufferPtr += i;
        BytesLeft -= i;
    }

    return Length;
}

/* HAL PCI CALLBACKS *********************************************************/

ULONG
NTAPI
HalpGetPCIData(IN PBUS_HANDLER BusHandler,
               IN PBUS_HANDLER RootHandler,
               IN ULONG SlotNumber,
               IN PVOID Buffer,
               IN ULONG Offset,
               IN ULONG Length)
{
    PCI_SLOT_NUMBER Slot;
    union {
        PCI_COMMON_HEADER Header;
        PCI_COMMON_CONFIG Config;
    } PciBuffer;
    PPCI_COMMON_CONFIG PciConfig = &PciBuffer.Config;
    ULONG Len = 0;

    Slot.u.AsULONG = SlotNumber;
#ifdef SARCH_XBOX
    if (HalpXboxBlacklistedPCISlot(BusHandler->BusNumber, Slot))
    {
        RtlFillMemory(Buffer, Length, 0xFF);
        return Length;
    }
#endif

    /* Normalize the length */
    if (Length > sizeof(PCI_COMMON_CONFIG)) Length = sizeof(PCI_COMMON_CONFIG);

    /* Check if this is a vendor-specific read */
    if (Offset >= PCI_COMMON_HDR_LENGTH)
    {
        /* Read the header */
        HalpReadPCIConfig(BusHandler, Slot, PciConfig, 0, sizeof(ULONG));

        /* Make sure the vendor is valid */
        if (PciConfig->VendorID == PCI_INVALID_VENDORID) return 0;
    }
    else
    {
        /* Read the entire header */
        Len = PCI_COMMON_HDR_LENGTH;
        HalpReadPCIConfig(BusHandler, Slot, PciConfig, 0, Len);

        /* Validate the vendor ID */
        if (PciConfig->VendorID == PCI_INVALID_VENDORID)
        {
            /* It's invalid, but we want to return this much */
            Len = sizeof(USHORT);
        }

        /* Now check if there's space left */
        if (Len < Offset) return 0;

        /* There is, so return what's after the offset and normalize */
        Len -= Offset;
        if (Len > Length) Len = Length;

        /* Copy the data into the caller's buffer */
        RtlMoveMemory(Buffer, (PUCHAR)PciConfig + Offset, Len);

        /* Update buffer and offset, decrement total length */
        Offset += Len;
        Buffer = (PVOID)((ULONG_PTR)Buffer + Len);
        Length -= Len;
    }

    /* Now we still have something to copy */
    if (Length)
    {
        /* Check if it's vendor-specific data */
        if (Offset >= PCI_COMMON_HDR_LENGTH)
        {
            /* Read it now */
            HalpReadPCIConfig(BusHandler, Slot, Buffer, Offset, Length);
            Len += Length;
        }
    }

    /* Update the total length read */
    return Len;
}

ULONG
NTAPI
HalpSetPCIData(IN PBUS_HANDLER BusHandler,
               IN PBUS_HANDLER RootHandler,
               IN ULONG SlotNumber,
               IN PVOID Buffer,
               IN ULONG Offset,
               IN ULONG Length)
{
    PCI_SLOT_NUMBER Slot;
    union {
        PCI_COMMON_HEADER Header;
        PCI_COMMON_CONFIG Config;
    } PciBuffer;
    PPCI_COMMON_CONFIG PciConfig = &PciBuffer.Config;
    ULONG Len = 0;

    Slot.u.AsULONG = SlotNumber;
#ifdef SARCH_XBOX
    if (HalpXboxBlacklistedPCISlot(BusHandler->BusNumber, Slot))
        return 0;
#endif

    /* Normalize the length */
    if (Length > sizeof(PCI_COMMON_CONFIG)) Length = sizeof(PCI_COMMON_CONFIG);

    /* Check if this is a vendor-specific read */
    if (Offset >= PCI_COMMON_HDR_LENGTH)
    {
        /* Read the header */
        HalpReadPCIConfig(BusHandler, Slot, PciConfig, 0, sizeof(ULONG));

        /* Make sure the vendor is valid */
        if (PciConfig->VendorID == PCI_INVALID_VENDORID) return 0;
    }
    else
    {
        /* Read the entire header and validate the vendor ID */
        Len = PCI_COMMON_HDR_LENGTH;
        HalpReadPCIConfig(BusHandler, Slot, PciConfig, 0, Len);
        if (PciConfig->VendorID == PCI_INVALID_VENDORID) return 0;

        /* Return what's after the offset and normalize */
        Len -= Offset;
        if (Len > Length) Len = Length;

        /* Copy the specific caller data */
        RtlMoveMemory((PUCHAR)PciConfig + Offset, Buffer, Len);

        /* Write the actual configuration data */
        HalpWritePCIConfig(BusHandler, Slot, (PUCHAR)PciConfig + Offset, Offset, Len);

        /* Update buffer and offset, decrement total length */
        Offset += Len;
        Buffer = (PVOID)((ULONG_PTR)Buffer + Len);
        Length -= Len;
    }

    /* Now we still have something to copy */
    if (Length)
    {
        /* Check if it's vendor-specific data */
        if (Offset >= PCI_COMMON_HDR_LENGTH)
        {
            /* Read it now */
            HalpWritePCIConfig(BusHandler, Slot, Buffer, Offset, Length);
            Len += Length;
        }
    }

    /* Update the total length read */
    return Len;
}
#ifndef _MINIHAL_
ULONG
NTAPI
HalpGetPCIIntOnISABus(IN PBUS_HANDLER BusHandler,
                      IN PBUS_HANDLER RootHandler,
                      IN ULONG BusInterruptLevel,
                      IN ULONG BusInterruptVector,
                      OUT PKIRQL Irql,
                      OUT PKAFFINITY Affinity)
{
    /* Validate the level first */
    if (BusInterruptLevel < 1) return 0;

    /* PCI has its IRQs on top of ISA IRQs, so pass it on to the ISA handler */
    return HalGetInterruptVector(Isa,
                                 0,
                                 BusInterruptLevel,
                                 0,
                                 Irql,
                                 Affinity);
}
#endif // _MINIHAL_

VOID
NTAPI
HalpPCIPin2ISALine(IN PBUS_HANDLER BusHandler,
                   IN PBUS_HANDLER RootHandler,
                   IN PCI_SLOT_NUMBER SlotNumber,
                   IN PPCI_COMMON_CONFIG PciData)
{
    static const UCHAR DefaultIrqMap[4] = {10, 11, 9, 5};
    PPCIPBUSDATA BusData;
    UCHAR Pin;

    UNREFERENCED_PARAMETER(RootHandler);

    Pin = PciData->u.type0.InterruptPin;
    if (!Pin || Pin > 4) return;

    BusData = (PPCIPBUSDATA)BusHandler->BusData;
    if (HalpPciRouteQueryCallback && BusData)
    {
        ULONG Gsi;
        UCHAR Polarity;
        UCHAR Trigger;

        if (HalpPciRouteQueryCallback(BusData->PciSegment,
                                       (UCHAR)BusHandler->BusNumber,
                                       SlotNumber.u.bits.DeviceNumber,
                                       SlotNumber.u.bits.FunctionNumber,
                                       Pin,
                                       &Gsi,
                                       &Polarity,
                                       &Trigger))
        {
            HalpPciRecordGsiInfo(Gsi, Polarity, Trigger);
            PciData->u.type0.InterruptLine = (Gsi <= 0xFF) ? (UCHAR)Gsi : 0xFF;
            return;
        }
    }

    if ((PciData->u.type0.InterruptLine == 0) ||
        (PciData->u.type0.InterruptLine == 0xFF))
    {
        UCHAR VectorIndex;

        VectorIndex = (UCHAR)((SlotNumber.u.bits.DeviceNumber + Pin - 1) & 0x3);
        PciData->u.type0.InterruptLine = DefaultIrqMap[VectorIndex];
    }
}

VOID
NTAPI
HalpPCIISALine2Pin(IN PBUS_HANDLER BusHandler,
                   IN PBUS_HANDLER RootHandler,
                   IN PCI_SLOT_NUMBER SlotNumber,
                   IN PPCI_COMMON_CONFIG PciNewData,
                   IN PPCI_COMMON_CONFIG PciOldData)
{
    UNREFERENCED_PARAMETER(PciOldData);
    HalpPCIPin2ISALine(BusHandler, RootHandler, SlotNumber, PciNewData);
}

#ifndef _MINIHAL_
NTSTATUS
NTAPI
HalpGetISAFixedPCIIrq(IN PBUS_HANDLER BusHandler,
                      IN PBUS_HANDLER RootHandler,
                      IN PCI_SLOT_NUMBER PciSlot,
                      OUT PSUPPORTED_RANGE *Range)
{
    PCI_COMMON_HEADER PciData;

    /* Read PCI configuration data */
    HalGetBusData(PCIConfiguration,
                  BusHandler->BusNumber,
                  PciSlot.u.AsULONG,
                  &PciData,
                  PCI_COMMON_HDR_LENGTH);

    /* Make sure it's a real device */
    if (PciData.VendorID == PCI_INVALID_VENDORID) return STATUS_UNSUCCESSFUL;

    /* Allocate the supported range structure */
    *Range = ExAllocatePoolWithTag(PagedPool, sizeof(SUPPORTED_RANGE), TAG_HAL);
    if (!*Range) return STATUS_INSUFFICIENT_RESOURCES;

    /* Set it up */
    RtlZeroMemory(*Range, sizeof(SUPPORTED_RANGE));
    (*Range)->Base = 1;

    /* If the PCI device has no IRQ, nothing to do */
    if (!PciData.u.type0.InterruptPin) return STATUS_SUCCESS;

    /* FIXME: The PCI IRQ Routing Miniport should be called */

    /* Also if the INT# seems bogus, nothing to do either */
    if ((PciData.u.type0.InterruptLine == 0) ||
        (PciData.u.type0.InterruptLine == 255))
    {
        /* Fake success */
        return STATUS_SUCCESS;
    }

    if (HalpPciRouteQueryCallback)
    {
        PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;
        if (BusData)
        {
            ULONG Gsi;
            UCHAR Polarity;
            UCHAR Trigger;

            if (HalpPciRouteQueryCallback(BusData->PciSegment,
                                           (UCHAR)BusHandler->BusNumber,
                                           PciSlot.u.bits.DeviceNumber,
                                           PciSlot.u.bits.FunctionNumber,
                                           PciData.u.type0.InterruptPin,
                                           &Gsi,
                                           &Polarity,
                                           &Trigger))
            {
                HalpPciRecordGsiInfo(Gsi, Polarity, Trigger);
                (*Range)->Base = Gsi;
                (*Range)->Limit = Gsi;
                return STATUS_SUCCESS;
            }
        }
    }

    /* Otherwise, the INT# should be valid, return it to the caller */
    (*Range)->Base = PciData.u.type0.InterruptLine;
    (*Range)->Limit = PciData.u.type0.InterruptLine;
    return STATUS_SUCCESS;
}
#endif // _MINIHAL_

static VOID
HalpPciPropagateUsage(PBUS_HANDLER BusHandler,
                      BOOLEAN IsIo,
                      BOOLEAN IsPrefetch,
                      ULONGLONG Base,
                      ULONGLONG Limit)
{
    PBUS_HANDLER CurrentBus = BusHandler;

    while (CurrentBus && (CurrentBus->InterfaceType == PCIBus))
    {
        PPCIPBUSDATA BusData = (PPCIPBUSDATA)CurrentBus->BusData;

        HalpPciEnsureRangeInitialized(BusData);

        if (IsIo)
        {
            if (Base < BusData->IoWindowBase) BusData->IoWindowBase = Base;
            if (Limit > BusData->IoWindowLimit) BusData->IoWindowLimit = Limit;
        }
        else if (IsPrefetch)
        {
            if (Base < BusData->PrefetchWindowBase) BusData->PrefetchWindowBase = Base;
            if (Limit > BusData->PrefetchWindowLimit) BusData->PrefetchWindowLimit = Limit;
        }
        else
        {
            if (Base < BusData->MemoryWindowBase) BusData->MemoryWindowBase = Base;
            if (Limit > BusData->MemoryWindowLimit) BusData->MemoryWindowLimit = Limit;
        }

        CurrentBus = CurrentBus->ParentHandler;
    }
}

static VOID
HalpPciWriteBridgeWindow(IN PBUS_HANDLER ParentBus,
                         IN PCI_SLOT_NUMBER BridgeSlot,
                         IN PPCI_COMMON_CONFIG BridgeConfig)
{
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.IOBase,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.IOBase),
                       sizeof(BridgeConfig->u.type1.IOBase));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.IOLimit,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.IOLimit),
                       sizeof(BridgeConfig->u.type1.IOLimit));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.IOBaseUpper16,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.IOBaseUpper16),
                       sizeof(BridgeConfig->u.type1.IOBaseUpper16));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.IOLimitUpper16,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.IOLimitUpper16),
                       sizeof(BridgeConfig->u.type1.IOLimitUpper16));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.MemoryBase,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.MemoryBase),
                       sizeof(BridgeConfig->u.type1.MemoryBase));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.MemoryLimit,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.MemoryLimit),
                       sizeof(BridgeConfig->u.type1.MemoryLimit));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.PrefetchBase,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.PrefetchBase),
                       sizeof(BridgeConfig->u.type1.PrefetchBase));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.PrefetchLimit,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.PrefetchLimit),
                       sizeof(BridgeConfig->u.type1.PrefetchLimit));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.PrefetchBaseUpper32,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.PrefetchBaseUpper32),
                       sizeof(BridgeConfig->u.type1.PrefetchBaseUpper32));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.PrefetchLimitUpper32,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.PrefetchLimitUpper32),
                       sizeof(BridgeConfig->u.type1.PrefetchLimitUpper32));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->Command,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                       sizeof(BridgeConfig->Command));
    HalpWritePCIConfig(ParentBus,
                       BridgeSlot,
                       &BridgeConfig->u.type1.BridgeControl,
                       FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.BridgeControl),
                       sizeof(BridgeConfig->u.type1.BridgeControl));
}

static VOID
HalpPciUpdateBridge(IN PBUS_HANDLER ChildBus)
{
    PPCIPBUSDATA ChildData = (PPCIPBUSDATA)ChildBus->BusData;
    PCI_SLOT_NUMBER BridgeSlot;
    PBUS_HANDLER ParentBus;
    PCI_COMMON_CONFIG BridgeConfig;
    BOOLEAN IoDecode = FALSE;
    BOOLEAN MemoryDecode = FALSE;

    ParentBus = ChildBus->ParentHandler;
    if (!ParentBus || (ParentBus->InterfaceType != PCIBus)) return;

    BridgeSlot = ChildData->CommonData.ParentSlot;
    if (BridgeSlot.u.AsULONG == 0)
    {
        return;
    }

    RtlZeroMemory(&BridgeConfig, sizeof(BridgeConfig));
    HalpReadPCIConfig(ParentBus,
                      BridgeSlot,
                      &BridgeConfig,
                      0,
                      sizeof(BridgeConfig));

    if ((BridgeConfig.HeaderType & ~PCI_MULTIFUNCTION) != PCI_BRIDGE_TYPE)
    {
        return;
    }

    if (ChildData->IoWindowBase <= ChildData->IoWindowLimit)
    {
        ULONGLONG Base = ChildData->IoWindowBase & ~0xFFFULL;
        ULONGLONG Limit = ChildData->IoWindowLimit | 0xFFFULL;

        BridgeConfig.u.type1.IOBase = (UCHAR)((Base >> 8) & 0xF0);
        BridgeConfig.u.type1.IOLimit = (UCHAR)((Limit >> 8) & 0xF0);
        BridgeConfig.u.type1.IOBaseUpper16 = (USHORT)((Base >> 16) & 0xFFFF);
        BridgeConfig.u.type1.IOLimitUpper16 = (USHORT)((Limit >> 16) & 0xFFFF);
        IoDecode = TRUE;
    }
    else
    {
        BridgeConfig.u.type1.IOBase = 0xF0;
        BridgeConfig.u.type1.IOLimit = 0x0;
        BridgeConfig.u.type1.IOBaseUpper16 = 0;
        BridgeConfig.u.type1.IOLimitUpper16 = 0;
    }

    if (ChildData->MemoryWindowBase <= ChildData->MemoryWindowLimit)
    {
        ULONGLONG Base = ChildData->MemoryWindowBase & ~0xFFFFFULL;
        ULONGLONG Limit = ChildData->MemoryWindowLimit | 0xFFFFFULL;

        BridgeConfig.u.type1.MemoryBase = (USHORT)((Base >> 16) & 0xFFF0);
        BridgeConfig.u.type1.MemoryLimit = (USHORT)((Limit >> 16) & 0xFFF0);
        MemoryDecode = TRUE;
    }
    else
    {
        BridgeConfig.u.type1.MemoryBase = 0xFFF0;
        BridgeConfig.u.type1.MemoryLimit = 0;
    }

    if (ChildData->PrefetchWindowBase <= ChildData->PrefetchWindowLimit)
    {
        ULONGLONG Base = ChildData->PrefetchWindowBase & ~0xFFFFFULL;
        ULONGLONG Limit = ChildData->PrefetchWindowLimit | 0xFFFFFULL;

        BridgeConfig.u.type1.PrefetchBase = (USHORT)((Base >> 16) & 0xFFF0);
        BridgeConfig.u.type1.PrefetchLimit = (USHORT)((Limit >> 16) & 0xFFF0);
        BridgeConfig.u.type1.PrefetchBaseUpper32 = (ULONG)(Base >> 32);
        BridgeConfig.u.type1.PrefetchLimitUpper32 = (ULONG)(Limit >> 32);
        MemoryDecode = TRUE;
    }
    else
    {
        BridgeConfig.u.type1.PrefetchBase = 0xFFF0;
        BridgeConfig.u.type1.PrefetchLimit = 0;
        BridgeConfig.u.type1.PrefetchBaseUpper32 = 0;
        BridgeConfig.u.type1.PrefetchLimitUpper32 = 0;
    }

    if (IoDecode)
    {
        BridgeConfig.Command |= PCI_ENABLE_IO_SPACE;
    }
    else
    {
        BridgeConfig.Command &= ~PCI_ENABLE_IO_SPACE;
    }

    if (MemoryDecode)
    {
        BridgeConfig.Command |= PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;
    }
    else
    {
        BridgeConfig.Command &= ~PCI_ENABLE_MEMORY_SPACE;
    }

    HalpPciWriteBridgeWindow(ParentBus, BridgeSlot, &BridgeConfig);
}

static VOID
HalpPciUpdateBridgeHierarchy(PBUS_HANDLER BusHandler)
{
    PBUS_HANDLER Current = BusHandler;

    while (Current && Current->ParentHandler &&
           (Current->ParentHandler->InterfaceType == PCIBus))
    {
        HalpPciUpdateBridge(Current);
        Current = Current->ParentHandler;
    }
}

NTSTATUS
NTAPI
HalpAdjustPCIResourceList(IN PBUS_HANDLER BusHandler,
                          IN PBUS_HANDLER RootHandler,
                          IN OUT PIO_RESOURCE_REQUIREMENTS_LIST *pResourceList)
{
    PPCIPBUSDATA BusData;
    PCI_SLOT_NUMBER SlotNumber;
    PSUPPORTED_RANGE Interrupt = NULL;
    NTSTATUS Status;
    ULONG AltIndex, DescriptorIndex;

    BusData = (PPCIPBUSDATA)BusHandler->BusData;
    SlotNumber.u.AsULONG = (*pResourceList)->SlotNumber;

    Status = BusData->GetIrqRange(BusHandler, RootHandler, SlotNumber, &Interrupt);
    if (!NT_SUCCESS(Status) && (Status != STATUS_UNSUCCESSFUL))
        return Status;

    if (Interrupt) ExFreePool(Interrupt);

#ifndef _MINIHAL_
    if (HalpPciLockSettings)
    {
        UNIMPLEMENTED_DBGBREAK("/PCILOCK boot switch is not yet supported.");
    }
#endif

    HalpPciEnsureRangeInitialized(BusData);

    for (AltIndex = 0; AltIndex < (*pResourceList)->AlternativeLists; AltIndex++)
    {
        PIO_RESOURCE_LIST ResourceList = &(*pResourceList)->List[AltIndex];

        for (DescriptorIndex = 0; DescriptorIndex < ResourceList->Count; DescriptorIndex++)
        {
            PIO_RESOURCE_DESCRIPTOR Descriptor = &ResourceList->Descriptors[DescriptorIndex];

            switch (Descriptor->Type)
            {
                case CmResourceTypePort:
                {
                    ULONGLONG Minimum, Maximum, Length, Alignment;

                    Minimum = Descriptor->u.Port.MinimumAddress.QuadPart;
                    Maximum = Descriptor->u.Port.MaximumAddress.QuadPart;
                    Length = Descriptor->u.Port.Length;
                    Alignment = Descriptor->u.Port.Alignment ? Descriptor->u.Port.Alignment : 1;

                    if (Minimum < BusData->IoBase) Minimum = BusData->IoBase;
                    if (!Maximum || (Maximum > BusData->IoLimit)) Maximum = BusData->IoLimit;
                    if (Alignment < Length) Alignment = Length;

                    if ((Minimum + Length - 1) > Maximum)
                    {
                        DPRINT1("HAL: PCI port requirement outside bus range (%I64x-%I64x len %I64x)\n",
                                Minimum, Maximum, Length);
                        return STATUS_CONFLICTING_ADDRESSES;
                    }

                    Descriptor->u.Port.MinimumAddress.QuadPart = Minimum;
                    Descriptor->u.Port.MaximumAddress.QuadPart = Maximum;
                    Descriptor->u.Port.Alignment = Alignment;
                    break;
                }

                case CmResourceTypeMemory:
                case CmResourceTypeMemoryLarge:
                {
                    ULONGLONG Minimum, Maximum, Length, Alignment;

                    Minimum = Descriptor->u.Memory.MinimumAddress.QuadPart;
                    Maximum = Descriptor->u.Memory.MaximumAddress.QuadPart;
                    Length = Descriptor->u.Memory.Length;
                    Alignment = Descriptor->u.Memory.Alignment ? Descriptor->u.Memory.Alignment : 0x10;

                    if (Minimum < BusData->MemoryBase) Minimum = BusData->MemoryBase;
                    if (!Maximum || (Maximum > BusData->MemoryLimit)) Maximum = BusData->MemoryLimit;
                    if (Alignment < Length) Alignment = Length;

                    if ((Minimum + Length - 1) > Maximum)
                    {
                        DPRINT1("HAL: PCI memory requirement outside bus range (%I64x-%I64x len %I64x)\n",
                                Minimum, Maximum, Length);
                        return STATUS_CONFLICTING_ADDRESSES;
                    }

                    Descriptor->u.Memory.MinimumAddress.QuadPart = Minimum;
                    Descriptor->u.Memory.MaximumAddress.QuadPart = Maximum;
                    Descriptor->u.Memory.Alignment = Alignment;
                    break;
                }

                default:
                    break;
            }
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpAssignPCISlotResources(IN PBUS_HANDLER BusHandler,
                           IN PBUS_HANDLER RootHandler,
                           IN PUNICODE_STRING RegistryPath,
                           IN PUNICODE_STRING DriverClassName OPTIONAL,
                           IN PDRIVER_OBJECT DriverObject,
                           IN PDEVICE_OBJECT DeviceObject OPTIONAL,
                           IN ULONG Slot,
                           IN OUT PCM_RESOURCE_LIST *AllocatedResources)
{
    typedef struct _HALP_PCI_BAR_INFO
    {
        BOOLEAN Present;
        BOOLEAN IsIo;
        BOOLEAN IsPrefetch;
        BOOLEAN Is64Bit;
        UCHAR Index;
        ULONGLONG Base;
        ULONGLONG Length;
        ULONG Attributes;
    } HALP_PCI_BAR_INFO, *PHALP_PCI_BAR_INFO;

    HALP_PCI_BAR_INFO BarInfo[PCI_TYPE0_ADDRESSES];
    PCI_COMMON_CONFIG PciConfig;
    PCI_SLOT_NUMBER SlotNumber;
    PPCIPBUSDATA BusData;
    ULONG BarLimit, BarNumber, DescriptorCount, SlotBit;
    BOOLEAN IoSpacePresent = FALSE, MemorySpacePresent = FALSE;
    ULONG Command;
    BOOLEAN Prefetch;
    ULONG Attributes;

    RtlZeroMemory(BarInfo, sizeof(BarInfo));

    if (!AllocatedResources)
    {
        DPRINT1("HAL: HalpAssignPCISlotResources missing resource list for bus %p\n",
                BusHandler);
        return STATUS_INVALID_PARAMETER;
    }

    if (!BusHandler)
    {
        DPRINT1("HAL: HalpAssignPCISlotResources called with NULL BusHandler\n");
        return STATUS_INVALID_PARAMETER;
    }

    if (!BusHandler->BusData)
    {
        DPRINT1("HAL: BusHandler %p on bus %lu has no BusData\n",
                BusHandler,
                BusHandler->BusNumber);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    BusData = (PPCIPBUSDATA)BusHandler->BusData;

    if (!BusData->DeviceConfigured.Buffer ||
        BusData->DeviceConfigured.SizeOfBitMap == 0)
    {
        DPRINT1("HAL: PCI bus %lu DeviceConfigured bitmap is not initialised\n",
                BusHandler->BusNumber);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    SlotNumber.u.AsULONG = Slot;

    HalpReadPCIConfig(BusHandler, SlotNumber, &PciConfig, 0, PCI_COMMON_HDR_LENGTH);
    if (PciConfig.VendorID == PCI_INVALID_VENDORID) return STATUS_NO_SUCH_DEVICE;

    HalpPciEnsureRangeInitialized(BusData);

    switch (PciConfig.HeaderType & ~PCI_MULTIFUNCTION)
    {
        case PCI_DEVICE_TYPE:
            BarLimit = PCI_TYPE0_ADDRESSES;
            break;

        case PCI_BRIDGE_TYPE:
            BarLimit = PCI_TYPE1_ADDRESSES;
            break;

        case PCI_CARDBUS_BRIDGE_TYPE:
        default:
            BarLimit = 0;
            break;
    }

    SlotBit = (SlotNumber.u.bits.DeviceNumber * PCI_MAX_FUNCTION) +
              SlotNumber.u.bits.FunctionNumber;

    if (SlotBit >= BusData->DeviceConfigured.SizeOfBitMap)
    {
        DPRINT1("HAL: SlotBit %lu outside bitmap bounds %lu on bus %lu\n",
                SlotBit,
                BusData->DeviceConfigured.SizeOfBitMap,
                BusHandler->BusNumber);
        return STATUS_INVALID_PARAMETER;
    }

    Command = PciConfig.Command;

    for (BarNumber = 0; BarNumber < BarLimit; BarNumber++)
    {
        ULONG Offset;
        ULONG OriginalValue;
        ULONG MaskLow;
        ULONGLONG Mask;
        ULONGLONG Length;
        ULONGLONG Base;
        BOOLEAN IsIo, Is64Bit;
        ULONG OriginalHigh = 0;

        Offset = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[BarNumber]);
        OriginalValue = PciConfig.u.type0.BaseAddresses[BarNumber];

        MaskLow = 0xFFFFFFFF;
        HalpWritePCIConfig(BusHandler, SlotNumber, &MaskLow, Offset, sizeof(ULONG));
        HalpReadPCIConfig(BusHandler, SlotNumber, &MaskLow, Offset, sizeof(ULONG));

        IsIo = (MaskLow & PCI_ADDRESS_IO_SPACE) ? TRUE : FALSE;
        Is64Bit = FALSE;
        Prefetch = FALSE;
        Mask = MaskLow;

        if (!IsIo)
        {
            Prefetch = (MaskLow & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
            if ((MaskLow & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
            {
                ULONG MaskHigh = 0xFFFFFFFF;
                HalpWritePCIConfig(BusHandler,
                                   SlotNumber,
                                   &MaskHigh,
                                   Offset + sizeof(ULONG),
                                   sizeof(ULONG));
                HalpReadPCIConfig(BusHandler,
                                  SlotNumber,
                                  &MaskHigh,
                                  Offset + sizeof(ULONG),
                                  sizeof(ULONG));
                Mask |= ((ULONGLONG)MaskHigh << 32);
                Is64Bit = TRUE;
            }
        }

        /* Restore original BAR value */
        HalpWritePCIConfig(BusHandler, SlotNumber, &OriginalValue, Offset, sizeof(ULONG));

        if (Is64Bit)
        {
            OriginalHigh = PciConfig.u.type0.BaseAddresses[BarNumber + 1];
            HalpWritePCIConfig(BusHandler,
                               SlotNumber,
                               &OriginalHigh,
                               Offset + sizeof(ULONG),
                               sizeof(ULONG));
        }

        Length = HalpPciBarLength(Mask, IsIo);
        if (!Length)
        {
            if (Is64Bit) BarNumber++;
            continue;
        }

        if (IsIo)
        {
            Base = OriginalValue & PCI_ADDRESS_IO_ADDRESS_MASK;
            Attributes = (OriginalValue & ~PCI_ADDRESS_IO_ADDRESS_MASK) | PCI_ADDRESS_IO_SPACE;

            if (!Base)
            {
                Base = HalpPciAllocateIoRange(BusData, Length);
                if (!Base) return STATUS_INSUFFICIENT_RESOURCES;

                ASSERT((Base + Length - 1) <= BusData->IoLimit);
                ASSERT((Base & (Length - 1)) == 0);

                PciConfig.u.type0.BaseAddresses[BarNumber] = (ULONG)(Base | Attributes);
                HalpWritePCIConfig(BusHandler,
                                   SlotNumber,
                                   &PciConfig.u.type0.BaseAddresses[BarNumber],
                                   Offset,
                                   sizeof(ULONG));
                DPRINT1("HAL: Assigned IO BAR %lu -> %I64x len %I64x\n", BarNumber, Base, Length);
            }

            IoSpacePresent = TRUE;
        }
        else
        {
            Base = OriginalValue & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
            Attributes = (OriginalValue & ~PCI_ADDRESS_MEMORY_ADDRESS_MASK);
            if (!Attributes)
            {
                Attributes = MaskLow & (PCI_ADDRESS_MEMORY_TYPE_MASK |
                                         PCI_ADDRESS_MEMORY_PREFETCHABLE);
            }

            if (Is64Bit)
            {
                Base |= ((ULONGLONG)OriginalHigh << 32);
            }

            if (!Base)
            {
                Base = HalpPciAllocateMemoryRange(BusData, Length);
                if (!Base) return STATUS_INSUFFICIENT_RESOURCES;

                ASSERT((Base + Length - 1) <= BusData->MemoryLimit);
                ASSERT((Base & (Length - 1)) == 0);

                PciConfig.u.type0.BaseAddresses[BarNumber] =
                    (ULONG)((Base & PCI_ADDRESS_MEMORY_ADDRESS_MASK) | Attributes);
                HalpWritePCIConfig(BusHandler,
                                   SlotNumber,
                                   &PciConfig.u.type0.BaseAddresses[BarNumber],
                                   Offset,
                                   sizeof(ULONG));

                if (Is64Bit)
                {
                    ULONG HighPart = (ULONG)(Base >> 32);
                    PciConfig.u.type0.BaseAddresses[BarNumber + 1] = HighPart;
                    HalpWritePCIConfig(BusHandler,
                                       SlotNumber,
                                       &HighPart,
                                       Offset + sizeof(ULONG),
                                       sizeof(ULONG));
                }

                DPRINT1("HAL: Assigned MMIO BAR %lu -> %I64x len %I64x%s\n",
                        BarNumber,
                        Base,
                        Length,
                        Prefetch ? " (prefetch)" : "");
            }

            MemorySpacePresent = TRUE;
        }

        BarInfo[BarNumber].Present = TRUE;
        BarInfo[BarNumber].IsIo = IsIo;
        BarInfo[BarNumber].IsPrefetch = Prefetch;
        BarInfo[BarNumber].Is64Bit = Is64Bit;
        BarInfo[BarNumber].Index = (UCHAR)BarNumber;
        BarInfo[BarNumber].Base = Base;
        BarInfo[BarNumber].Length = Length;
        BarInfo[BarNumber].Attributes = Attributes;

        HalpPciPropagateUsage(BusHandler,
                              IsIo,
                              Prefetch,
                              Base,
                              Base + Length - 1);

        if (Is64Bit) BarNumber++;
    }

    if (IoSpacePresent)
        Command |= PCI_ENABLE_IO_SPACE;
    else
        Command &= ~PCI_ENABLE_IO_SPACE;

    if (MemorySpacePresent)
        Command |= PCI_ENABLE_MEMORY_SPACE;
    else
        Command &= ~PCI_ENABLE_MEMORY_SPACE;

    if (IoSpacePresent || MemorySpacePresent)
        Command |= PCI_ENABLE_BUS_MASTER;
    else
        Command &= ~PCI_ENABLE_BUS_MASTER;

    if (Command != PciConfig.Command)
    {
        HalpWritePCIConfig(BusHandler,
                           SlotNumber,
                           &Command,
                           FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                           sizeof(Command));
        PciConfig.Command = Command;
    }

    HalpPciUpdateBridgeHierarchy(BusHandler);

    DescriptorCount = 0;
    for (BarNumber = 0; BarNumber < BarLimit; BarNumber++)
    {
        if (BarInfo[BarNumber].Present) DescriptorCount++;
    }

    /* Special-case legacy IDE compatibility mode I/O ports (PIIX/compat) */
    if ((PciConfig.BaseClass == PCI_CLASS_MASS_STORAGE_CTLR) &&
        (PciConfig.SubClass == PCI_SUBCLASS_MSC_IDE_CTLR))
    {
        /* If primary channel is in compatibility mode (ProgIf bit0 == 0) */
        if (!(PciConfig.ProgIf & 0x01))
        {
            DescriptorCount += 2; /* Primary Cmd + Ctl */
        }

        /* If secondary channel is in compatibility mode (ProgIf bit2 == 0) */
        if (!(PciConfig.ProgIf & 0x04))
        {
            DescriptorCount += 2; /* Secondary Cmd + Ctl */
        }
    }

    if (PciConfig.u.type0.InterruptPin &&
        PciConfig.u.type0.InterruptLine &&
        (PciConfig.u.type0.InterruptLine != 0xFF))
    {
        DescriptorCount++;
    }

    /* Add legacy IDE IRQs in compatibility mode */
    if ((PciConfig.BaseClass == PCI_CLASS_MASS_STORAGE_CTLR) &&
        (PciConfig.SubClass == PCI_SUBCLASS_MSC_IDE_CTLR))
    {
        if (!(PciConfig.ProgIf & 0x01)) DescriptorCount++; /* IRQ14 */
        if (!(PciConfig.ProgIf & 0x04)) DescriptorCount++; /* IRQ15 */
    }

    if (DescriptorCount == 0)
        DescriptorCount = 1;

    *AllocatedResources = ExAllocatePoolWithTag(
        PagedPool,
        sizeof(CM_RESOURCE_LIST) +
        (DescriptorCount - 1) * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR),
        TAG_HAL);

    if (!*AllocatedResources) return STATUS_NO_MEMORY;

    (*AllocatedResources)->Count = 1;
    (*AllocatedResources)->List[0].InterfaceType = PCIBus;
    (*AllocatedResources)->List[0].BusNumber = BusHandler->BusNumber;
    (*AllocatedResources)->List[0].PartialResourceList.Version = 1;
    (*AllocatedResources)->List[0].PartialResourceList.Revision = 1;
    (*AllocatedResources)->List[0].PartialResourceList.Count = 0;

    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
        ULONG Filled = 0;

        Descriptor = (*AllocatedResources)->List[0].PartialResourceList.PartialDescriptors;

        for (BarNumber = 0; BarNumber < BarLimit; BarNumber++)
        {
            PHALP_PCI_BAR_INFO Info = &BarInfo[BarNumber];

            if (!Info->Present) continue;

            Descriptor[Filled].ShareDisposition = CmResourceShareDeviceExclusive;

            if (Info->IsIo)
            {
                if ((PciConfig.BaseClass == PCI_CLASS_MASS_STORAGE_CTLR) &&
                    (PciConfig.SubClass == PCI_SUBCLASS_MSC_IDE_CTLR) &&
                    (Info->Index == 4) &&
                    (Info->Length == 16))
                {
                    Descriptor[Filled].ShareDisposition = CmResourceShareShared;
                }
                Descriptor[Filled].Type = CmResourceTypePort;
                Descriptor[Filled].Flags = CM_RESOURCE_PORT_IO;
                ASSERT(Info->Length <= MAXULONG);
                Descriptor[Filled].u.Port.Length = (ULONG)Info->Length;
                Descriptor[Filled].u.Port.Start.QuadPart = Info->Base;
                DPRINT1("HAL: Bus %lu Slot %lu BAR %u IO @ %I64x len %lu\n",
                        BusHandler->BusNumber,
                        SlotNumber.u.AsULONG,
                        Info->Index,
                        Info->Base,
                        (ULONG)Info->Length);
            }
            else
            {
                Descriptor[Filled].Type = CmResourceTypeMemory;
                Descriptor[Filled].Flags = CM_RESOURCE_MEMORY_READ_WRITE;
                if (Info->IsPrefetch)
                    Descriptor[Filled].Flags |= CM_RESOURCE_MEMORY_PREFETCHABLE;

                ASSERT(Info->Length <= MAXULONG);
                Descriptor[Filled].u.Memory.Length = (ULONG)Info->Length;
                Descriptor[Filled].u.Memory.Start.QuadPart = Info->Base;
                DPRINT1("HAL: Bus %lu Slot %lu BAR %u MEM @ %I64x len %lu%s\n",
                        BusHandler->BusNumber,
                        SlotNumber.u.AsULONG,
                        Info->Index,
                        Info->Base,
                        (ULONG)Info->Length,
                        Info->IsPrefetch ? " prefetch" : "");
            }

            Filled++;
        }

        /* Add legacy IDE compatibility ranges if applicable */
        if ((PciConfig.BaseClass == PCI_CLASS_MASS_STORAGE_CTLR) &&
            (PciConfig.SubClass == PCI_SUBCLASS_MSC_IDE_CTLR))
        {
            if (!(PciConfig.ProgIf & 0x01))
            {
                /* Primary Command: 0x1F0-0x1F7 (8 bytes) */
                Descriptor[Filled].ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor[Filled].Type = CmResourceTypePort;
                Descriptor[Filled].Flags = CM_RESOURCE_PORT_IO;
                Descriptor[Filled].u.Port.Start.QuadPart = 0x1F0;
                Descriptor[Filled].u.Port.Length = 8;
                DPRINT1("HAL: IDE compat Primary Cmd IO @ 0x1F0 len 8\n");
                Filled++;

                /* Primary Control: 0x3F6 (1 byte) */
                Descriptor[Filled].ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor[Filled].Type = CmResourceTypePort;
                Descriptor[Filled].Flags = CM_RESOURCE_PORT_IO;
                Descriptor[Filled].u.Port.Start.QuadPart = 0x3F6;
                Descriptor[Filled].u.Port.Length = 1;
                DPRINT1("HAL: IDE compat Primary Ctl IO @ 0x3F6 len 1\n");
                Filled++;
            }

            if (!(PciConfig.ProgIf & 0x04))
            {
                /* Secondary Command: 0x170-0x177 (8 bytes) */
                Descriptor[Filled].ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor[Filled].Type = CmResourceTypePort;
                Descriptor[Filled].Flags = CM_RESOURCE_PORT_IO;
                Descriptor[Filled].u.Port.Start.QuadPart = 0x170;
                Descriptor[Filled].u.Port.Length = 8;
                DPRINT1("HAL: IDE compat Secondary Cmd IO @ 0x170 len 8\n");
                Filled++;

                /* Secondary Control: 0x376 (1 byte) */
                Descriptor[Filled].ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor[Filled].Type = CmResourceTypePort;
                Descriptor[Filled].Flags = CM_RESOURCE_PORT_IO;
                Descriptor[Filled].u.Port.Start.QuadPart = 0x376;
                Descriptor[Filled].u.Port.Length = 1;
                DPRINT1("HAL: IDE compat Secondary Ctl IO @ 0x376 len 1\n");
                Filled++;
            }
        }

        if (PciConfig.u.type0.InterruptPin &&
            PciConfig.u.type0.InterruptLine &&
            (PciConfig.u.type0.InterruptLine != 0xFF))
        {
            Descriptor[Filled].Type = CmResourceTypeInterrupt;
            Descriptor[Filled].ShareDisposition = CmResourceShareShared;
            Descriptor[Filled].Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
            Descriptor[Filled].u.Interrupt.Level = PciConfig.u.type0.InterruptLine;
            Descriptor[Filled].u.Interrupt.Vector = PciConfig.u.type0.InterruptLine;
            Descriptor[Filled].u.Interrupt.Affinity = (KAFFINITY)-1;
            Filled++;
        }

        /* Add fixed IDE IRQs (compatibility mode) */
        if ((PciConfig.BaseClass == PCI_CLASS_MASS_STORAGE_CTLR) &&
            (PciConfig.SubClass == PCI_SUBCLASS_MSC_IDE_CTLR))
        {
            if (!(PciConfig.ProgIf & 0x01))
            {
                Descriptor[Filled].Type = CmResourceTypeInterrupt;
                Descriptor[Filled].ShareDisposition = CmResourceShareShared;
                Descriptor[Filled].Flags = CM_RESOURCE_INTERRUPT_LATCHED;
                Descriptor[Filled].u.Interrupt.Level = 14;
                Descriptor[Filled].u.Interrupt.Vector = 14;
                Descriptor[Filled].u.Interrupt.Affinity = (KAFFINITY)-1;
                DPRINT1("HAL: IDE compat Primary IRQ 14 added\n");
                Filled++;
            }
            if (!(PciConfig.ProgIf & 0x04))
            {
                Descriptor[Filled].Type = CmResourceTypeInterrupt;
                Descriptor[Filled].ShareDisposition = CmResourceShareShared;
                Descriptor[Filled].Flags = CM_RESOURCE_INTERRUPT_LATCHED;
                Descriptor[Filled].u.Interrupt.Level = 15;
                Descriptor[Filled].u.Interrupt.Vector = 15;
                Descriptor[Filled].u.Interrupt.Affinity = (KAFFINITY)-1;
                DPRINT1("HAL: IDE compat Secondary IRQ 15 added\n");
                Filled++;
            }
        }

        (*AllocatedResources)->List[0].PartialResourceList.Count = Filled;
    }

    RtlSetBits(&BusData->DeviceConfigured, SlotBit, 1);

    return STATUS_SUCCESS;
}

ULONG
NTAPI
HaliPciInterfaceReadConfig(IN PBUS_HANDLER RootBusHandler,
                           IN ULONG BusNumber,
                           IN PCI_SLOT_NUMBER SlotNumber,
                           IN PVOID Buffer,
                           IN ULONG Offset,
                           IN ULONG Length)
{
    BUS_HANDLER BusHandler;

    /* Setup fake PCI Bus handler */
    RtlCopyMemory(&BusHandler, &HalpFakePciBusHandler, sizeof(BUS_HANDLER));
    BusHandler.BusNumber = BusNumber;

    /* Read configuration data */
    HalpReadPCIConfig(&BusHandler, SlotNumber, Buffer, Offset, Length);

    /* Return length */
    return Length;
}

CODE_SEG("INIT")
PPCI_REGISTRY_INFO_INTERNAL
NTAPI
HalpQueryPciRegistryInfo(VOID)
{
#ifndef _MINIHAL_
    WCHAR NameBuffer[8];
    OBJECT_ATTRIBUTES  ObjectAttributes;
    UNICODE_STRING KeyName, ConfigName, IdentName;
    HANDLE KeyHandle, BusKeyHandle, CardListHandle;
    NTSTATUS Status;
    UCHAR KeyBuffer[sizeof(CM_FULL_RESOURCE_DESCRIPTOR) + 100];
    PKEY_VALUE_FULL_INFORMATION ValueInfo = (PVOID)KeyBuffer;
    UCHAR PartialKeyBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) +
                           sizeof(PCI_CARD_DESCRIPTOR)];
    PKEY_VALUE_PARTIAL_INFORMATION PartialValueInfo = (PVOID)PartialKeyBuffer;
    KEY_FULL_INFORMATION KeyInformation;
    ULONG ResultLength;
    PWSTR Tag;
    ULONG i, ElementCount;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PPCI_REGISTRY_INFO PciRegInfo;
    PPCI_REGISTRY_INFO_INTERNAL PciRegistryInfo;
    PPCI_CARD_DESCRIPTOR CardDescriptor;

    /* Setup the object attributes for the key */
    RtlInitUnicodeString(&KeyName,
                         L"\\Registry\\Machine\\Hardware\\Description\\"
                         L"System\\MultiFunctionAdapter");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    /* Open the key */
    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return NULL;

    /* Setup the receiving string */
    KeyName.Buffer = NameBuffer;
    KeyName.MaximumLength = sizeof(NameBuffer);

    /* Setup the configuration and identifier key names */
    RtlInitUnicodeString(&ConfigName, L"Configuration Data");
    RtlInitUnicodeString(&IdentName, L"Identifier");

    /* Keep looping for each ID */
    for (i = 0; TRUE; i++)
    {
        /* Setup the key name */
        RtlIntegerToUnicodeString(i, 10, &KeyName);
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE,
                                   KeyHandle,
                                   NULL);

        /* Open it */
        Status = ZwOpenKey(&BusKeyHandle, KEY_READ, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            /* None left, fail */
            ZwClose(KeyHandle);
            return NULL;
        }

        /* Read the registry data */
        Status = ZwQueryValueKey(BusKeyHandle,
                                 &IdentName,
                                 KeyValueFullInformation,
                                 ValueInfo,
                                 sizeof(KeyBuffer),
                                 &ResultLength);
        if (!NT_SUCCESS(Status))
        {
            /* Failed, try the next one */
            ZwClose(BusKeyHandle);
            continue;
        }

        /* Get the PCI Tag and validate it */
        Tag = (PWSTR)((ULONG_PTR)ValueInfo + ValueInfo->DataOffset);
        if ((Tag[0] != L'P') ||
            (Tag[1] != L'C') ||
            (Tag[2] != L'I') ||
            (Tag[3]))
        {
            /* Not a valid PCI entry, skip it */
            ZwClose(BusKeyHandle);
            continue;
        }

        /* Now read our PCI structure */
        Status = ZwQueryValueKey(BusKeyHandle,
                                 &ConfigName,
                                 KeyValueFullInformation,
                                 ValueInfo,
                                 sizeof(KeyBuffer),
                                 &ResultLength);
        ZwClose(BusKeyHandle);
        if (!NT_SUCCESS(Status)) continue;

        /* We read it OK! Get the actual resource descriptors */
        FullDescriptor  = (PCM_FULL_RESOURCE_DESCRIPTOR)
                          ((ULONG_PTR)ValueInfo + ValueInfo->DataOffset);
        PartialDescriptor = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)
                            ((ULONG_PTR)FullDescriptor->
                                        PartialResourceList.PartialDescriptors);

        /* Check if this is our PCI Registry Information */
        if (PartialDescriptor->Type == CmResourceTypeDeviceSpecific)
        {
            /* It is, stop searching */
            break;
        }
    }

    /* Close the key */
    ZwClose(KeyHandle);

    /* Save the PCI information for later */
    PciRegInfo = (PPCI_REGISTRY_INFO)(PartialDescriptor + 1);

    /* Assume no Card List entries */
    ElementCount = 0;

    /* Set up for checking the PCI Card List key */
    RtlInitUnicodeString(&KeyName,
                         L"\\Registry\\Machine\\System\\CurrentControlSet\\"
                         L"Control\\PnP\\PCI\\CardList");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    /* Attempt to open it */
    Status = ZwOpenKey(&CardListHandle, KEY_READ, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        /* It exists, so let's query it */
        Status = ZwQueryKey(CardListHandle,
                            KeyFullInformation,
                            &KeyInformation,
                            sizeof(KEY_FULL_INFORMATION),
                            &ResultLength);
        if (!NT_SUCCESS(Status))
        {
            /* Failed to query, so no info */
            PciRegistryInfo = NULL;
        }
        else
        {
            /* Allocate the full structure */
            PciRegistryInfo =
                ExAllocatePoolWithTag(NonPagedPool,
                                      sizeof(PCI_REGISTRY_INFO_INTERNAL) +
                                      (KeyInformation.Values *
                                       sizeof(PCI_CARD_DESCRIPTOR)),
                                       TAG_HAL);
            if (PciRegistryInfo)
            {
                /* Get the first card descriptor entry */
                CardDescriptor = (PPCI_CARD_DESCRIPTOR)(PciRegistryInfo + 1);

                /* Loop all the values */
                for (i = 0; i < KeyInformation.Values; i++)
                {
                    /* Attempt to get the value */
                    Status = ZwEnumerateValueKey(CardListHandle,
                                                 i,
                                                 KeyValuePartialInformation,
                                                 PartialValueInfo,
                                                 sizeof(PartialKeyBuffer),
                                                 &ResultLength);
                    if (!NT_SUCCESS(Status))
                    {
                        /* Something went wrong, stop the search */
                        break;
                    }

                    /* Make sure it is correctly sized */
                    if (PartialValueInfo->DataLength == sizeof(PCI_CARD_DESCRIPTOR))
                    {
                        /* Sure is, copy it over */
                        *CardDescriptor = *(PPCI_CARD_DESCRIPTOR)
                                           PartialValueInfo->Data;

                        /* One more Card List entry */
                        ElementCount++;

                        /* Move to the next descriptor */
                        CardDescriptor = (CardDescriptor + 1);
                    }
                }
            }
        }

        /* Close the Card List key */
        ZwClose(CardListHandle);
    }
    else
    {
       /* No key, no Card List */
       PciRegistryInfo = NULL;
    }

    /* Check if we failed to get the full structure */
    if (!PciRegistryInfo)
    {
        /* Just allocate the basic structure then */
        PciRegistryInfo = ExAllocatePoolWithTag(NonPagedPool,
                                                sizeof(PCI_REGISTRY_INFO_INTERNAL),
                                                TAG_HAL);
        if (!PciRegistryInfo) return NULL;
    }

    /* Save the info we got */
    PciRegistryInfo->MajorRevision = PciRegInfo->MajorRevision;
    PciRegistryInfo->MinorRevision = PciRegInfo->MinorRevision;
    PciRegistryInfo->NoBuses = PciRegInfo->NoBuses;
    PciRegistryInfo->HardwareMechanism = PciRegInfo->HardwareMechanism;
    PciRegistryInfo->ElementCount = ElementCount;

    /* Return it */
    return PciRegistryInfo;
#else
    return NULL;
#endif
}

CODE_SEG("INIT")
VOID
NTAPI
HalpInitializePciStubs(VOID)
{
    PPCI_REGISTRY_INFO_INTERNAL PciRegistryInfo;
    UCHAR PciType;
    PPCIPBUSDATA BusData = (PPCIPBUSDATA)HalpFakePciBusHandler.BusData;
    ULONG i;
    PCI_SLOT_NUMBER j;
    ULONG VendorId = 0;
    ULONG MaxPciBusNumber;

    if (!BusData)
    {
        DPRINT1("HAL: HalpInitializePciStubs has no bus data for fake PCI handler\n");
        return;
    }

    RtlZeroMemory(BusData->ConfiguredBits, sizeof(BusData->ConfiguredBits));
    RtlInitializeBitMap(&BusData->DeviceConfigured,
                        BusData->ConfiguredBits,
                        sizeof(BusData->ConfiguredBits) * 8);

    /* Query registry information */
    PciRegistryInfo = HalpQueryPciRegistryInfo();
    if (!PciRegistryInfo)
    {
        /* Assume type 1 */
        PciType = 1;

        /* Force a manual bus scan later */
        MaxPciBusNumber = MAXULONG;
    }
    else
    {
        /* Get the PCI type */
        PciType = PciRegistryInfo->HardwareMechanism & 0xF;

        /* Get MaxPciBusNumber and make it 0-based */
        MaxPciBusNumber = PciRegistryInfo->NoBuses - 1;

        /* Free the info structure */
        ExFreePoolWithTag(PciRegistryInfo, TAG_HAL);
    }

    /* Initialize the PCI lock */
    KeInitializeSpinLock(&HalpPCIConfigLock);

    /* Check the type of PCI bus */
    switch (PciType)
    {
        /* Type 1 PCI Bus */
        case 1:

            /* Copy the Type 1 handler data */
            RtlCopyMemory(&PCIConfigHandler,
                          &PCIConfigHandlerType1,
                          sizeof(PCIConfigHandler));

            /* Set correct I/O Ports */
            BusData->Config.Type1.Address = PCI_TYPE1_ADDRESS_PORT;
            BusData->Config.Type1.Data = PCI_TYPE1_DATA_PORT;
            break;

        /* Type 2 PCI Bus */
        case 2:

            /* Copy the Type 2 handler data */
            RtlCopyMemory(&PCIConfigHandler,
                          &PCIConfigHandlerType2,
                          sizeof (PCIConfigHandler));

            /* Set correct I/O Ports */
            BusData->Config.Type2.CSE = PCI_TYPE2_CSE_PORT;
            BusData->Config.Type2.Forward = PCI_TYPE2_FORWARD_PORT;
            BusData->Config.Type2.Base = PCI_TYPE2_ADDRESS_BASE;

            /* Only 16 devices supported, not 32 */
            BusData->MaxDevice = 16;
            break;

        default:

            /* Invalid type */
            DbgPrint("HAL: Unknown PCI type\n");
    }

    /* Run a forced bus scan if needed */
    if (MaxPciBusNumber == MAXULONG)
    {
        /* Initialize the max bus number to 0xFF */
        HalpMaxPciBus = 0xFF;

        /* Initialize the counter */
        MaxPciBusNumber = 0;

        /* Loop all possible buses */
        for (i = 0; i < HalpMaxPciBus; i++)
        {
            /* Loop all devices */
            for (j.u.AsULONG = 0; j.u.AsULONG < BusData->MaxDevice; j.u.AsULONG++)
            {
                /* Query the interface */
                if (HaliPciInterfaceReadConfig(NULL,
                                               i,
                                               j,
                                               &VendorId,
                                               0,
                                               sizeof(ULONG)))
                {
                    /* Validate the vendor ID */
                    if ((VendorId & 0xFFFF) != PCI_INVALID_VENDORID)
                    {
                        /* Set this as the maximum ID */
                        MaxPciBusNumber = i;
                        break;
                    }
                }
            }
        }
    }

    /* Set the real max bus number */
    HalpMaxPciBus = MaxPciBusNumber;

    /* We're done */
    HalpPCIConfigInitialized = TRUE;
}

/* EOF */
