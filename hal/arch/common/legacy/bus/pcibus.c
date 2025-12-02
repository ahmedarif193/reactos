/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/arch/common/legacy/bus/pcibus.c
 * PURPOSE:         PCI Bus Support (Configuration Space, Resource Allocation)
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <halacpi.h>
#include <reactos/hal/acpi_pci.h>
#include <halirq.h>
#define NDEBUG
#include <debug.h>

#define HALP_PCI_DEFAULT_IO_BASE          0x0ULL
#define HALP_PCI_DEFAULT_IO_LIMIT         0xFFFFULL
#define HALP_PCI_DEFAULT_MEM_BASE         0xC0000000ULL
#define HALP_PCI_DEFAULT_MEM_LIMIT        0xFEBFFFFFULL
#define HALP_PCI_GSI_TAG                  'isGH'
#define HALP_PCI_ROOT_TAG                 'rciP'

typedef struct _HALP_PCI_GSI_INFO
{
    BOOLEAN Valid;
    BOOLEAN FromFirmware;
    UCHAR Polarity;
    UCHAR Trigger;
    USHORT Segment;
    UCHAR Bus;
    UCHAR Device;
    UCHAR Function;
    UCHAR Pin;
} HALP_PCI_GSI_INFO, *PHALP_PCI_GSI_INFO;

#define HALP_PCI_GSI_STATIC_CAPACITY 256
static HALP_PCI_GSI_INFO HalpPciGsiStaticInfo[HALP_PCI_GSI_STATIC_CAPACITY];
static PHALP_PCI_GSI_INFO HalpPciGsiInfo = HalpPciGsiStaticInfo;
static ULONG HalpPciGsiCapacity = HALP_PCI_GSI_STATIC_CAPACITY;
static BOOLEAN HalpPciGsiInfoUsesPool;
static KSPIN_LOCK HalpPciGsiLock;
static volatile LONG HalpPciGsiLockInitState;
static PHAL_ACPI_PCI_ROUTE_QUERY HalpPciRouteQueryCallback;
BOOLEAN HalpPciBusRangeKnown;
static BOOLEAN HalpPciMsiSupported = TRUE;

static
VOID
HalpPciInitGsiLock(VOID)
{
    if (HalpPciGsiLockInitState == 2)
    {
        return;
    }

    if (InterlockedCompareExchange(&HalpPciGsiLockInitState, 1, 0) == 0)
    {
        KeInitializeSpinLock(&HalpPciGsiLock);
        InterlockedExchange(&HalpPciGsiLockInitState, 2);
        return;
    }

    while (HalpPciGsiLockInitState != 2)
    {
        KeStallExecutionProcessor(1);
    }
}

/* Optional: allocate a concrete PCI bus handler for a given bus number.
   Not all HAL variants provide this. Avoid hard link-time dependency. */
static __inline PBUS_HANDLER
HalpTryAllocateAndInitPciBusHandler(
    IN ULONG PciType,
    IN ULONG BusNo,
    IN BOOLEAN TestAllocation)
{
    UNREFERENCED_PARAMETER(PciType);
    UNREFERENCED_PARAMETER(BusNo);
    UNREFERENCED_PARAMETER(TestAllocation);
#if defined(__GNUC__)
    /* GCC/MinGW: declare weak reference and call only if present */
    extern PBUS_HANDLER NTAPI HalpAllocateAndInitPciBusHandler(ULONG, ULONG, BOOLEAN) __attribute__((weak));
    if (HalpAllocateAndInitPciBusHandler)
    {
        return HalpAllocateAndInitPciBusHandler(PciType, BusNo, TestAllocation);
    }
#endif
    return NULL;
}


static BOOLEAN
HalpPciBusBelongsToRoot(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PBUS_HANDLER RootBus);

static VOID
HalpPciApplyConfiguredWindows(
    _In_ PBUS_HANDLER BusHandler);

static VOID
HalpPciUpdateBridgeHierarchy(
    PBUS_HANDLER BusHandler);

static BOOLEAN
HalpPciFindCapability(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ UCHAR HeaderType,
    _In_ UCHAR CapabilityId,
    _Out_ USHORT *Offset);

static BOOLEAN
HalpPciFindExtendedCapability(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ USHORT CapabilityId,
    _Out_ USHORT *Offset);

static VOID
HalpPciConfigureNativeExpressServices(
    _In_ PBUS_HANDLER BusHandler,
    _Inout_ PPCIPBUSDATA BusData);

VOID
HalpPciLogEcamCoverage(
    VOID)
{
    LONG Flags;

    Flags = HalpAcpiEcamCoverageFlags;
    if (Flags == 0)
    {
        DbgPrint("HAL: PCI ECAM path not exercised; using legacy configuration space.\n");
        return;
    }

    if ((Flags & HALP_ACPI_ECAM_COVERAGE_USED) &&
        !(Flags & (HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL | HALP_ACPI_ECAM_COVERAGE_FORCED_LEGACY)))
    {
        DbgPrint("HAL: PCI Express MMCONFIG (ECAM) active for configuration space.\n");
    }
    else
    {
        DbgPrint("HAL: PCI Express MMCONFIG unavailable; falling back to CF8/CFC ports.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_NO_TABLE)
    {
        DbgPrint("HAL:   ECAM fallback reason: ACPI MCFG table missing or empty.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_NO_ALLOCATION)
    {
        DbgPrint("HAL:   ECAM fallback reason: no MCFG allocation matched the requested bus.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_BUS_TOO_HIGH)
    {
        DbgPrint("HAL:   ECAM fallback reason: bus number exceeded 0xFF.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_OFFSET_TOO_HIGH)
    {
        DbgPrint("HAL:   ECAM fallback reason: offset reached beyond 4KB window.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_RANGE_OVERRUN)
    {
        DbgPrint("HAL:   ECAM fallback reason: access spanned multiple 4KB windows.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_MAP_FAILURE)
    {
        DbgPrint("HAL:   ECAM fallback reason: failed to map ECAM page.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_VENDOR_ALL_ONES)
    {
        DbgPrint("HAL:   ECAM fallback reason: configuration space read returned 0xFFFF.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_DISABLED_GLOBAL)
    {
        DbgPrint("HAL:   ECAM note: access path disabled globally after firmware failure.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_FORCED_LEGACY)
    {
        DbgPrint("HAL:   ECAM note: firmware quirk forced legacy CF8/CFC usage.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_ZERO_LENGTH)
    {
        DbgPrint("HAL:   ECAM note: zero-length configuration request observed.\n");
    }

    if (Flags & HALP_ACPI_ECAM_COVERAGE_SEGMENT_ANY)
    {
        DbgPrint("HAL:   ECAM note: callers used wildcard segment selection.\n");
    }
}

static BOOLEAN
HalpPciFindCapability(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ UCHAR HeaderType,
    _In_ UCHAR CapabilityId,
    _Out_ USHORT *Offset)
{
    PCI_CAPABILITIES_HEADER Header;
    UCHAR CapabilityPointer;
    USHORT Status;
    UCHAR ConfigType;
    ULONG GuardCount;

    if (!Offset)
    {
        return FALSE;
    }

    *Offset = 0;

    HalpReadPCIConfig(BusHandler,
                      Slot,
                      &Status,
                      FIELD_OFFSET(PCI_COMMON_CONFIG, Status),
                      sizeof(Status));
    if (!(Status & PCI_STATUS_CAPABILITIES_LIST))
    {
        return FALSE;
    }

    ConfigType = HeaderType & (UCHAR)~PCI_MULTIFUNCTION;

    switch (ConfigType)
    {
        case PCI_DEVICE_TYPE:
            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &CapabilityPointer,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.CapabilitiesPtr),
                              sizeof(CapabilityPointer));
            break;

        case PCI_BRIDGE_TYPE:
            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &CapabilityPointer,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.CapabilitiesPtr),
                              sizeof(CapabilityPointer));
            break;

        case PCI_CARDBUS_BRIDGE_TYPE:
            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &CapabilityPointer,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, u.type2.CapabilitiesPtr),
                              sizeof(CapabilityPointer));
            break;

        default:
            return FALSE;
    }

    CapabilityPointer &= (UCHAR)~0x3;

    for (GuardCount = 0;
         GuardCount < 64 &&
         CapabilityPointer >= 0x40;
         GuardCount++)
    {
        HalpReadPCIConfig(BusHandler,
                          Slot,
                          &Header,
                          CapabilityPointer,
                          sizeof(Header));

        if ((Header.CapabilityID == 0xFF) ||
            (Header.CapabilityID == 0))
        {
            break;
        }

        if (Header.CapabilityID == CapabilityId)
        {
            *Offset = CapabilityPointer;
            return TRUE;
        }

        if (!Header.Next)
        {
            break;
        }

        if ((Header.Next & ~0x3) == CapabilityPointer)
        {
            break;
        }

        CapabilityPointer = Header.Next & (UCHAR)~0x3;
    }

    return FALSE;
}

static BOOLEAN
HalpPciFindExtendedCapability(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PCI_SLOT_NUMBER Slot,
    _In_ USHORT CapabilityId,
    _Out_ USHORT *Offset)
{
    ULONG Header;
    USHORT Current;
    ULONG GuardCount;

    if (!Offset)
    {
        return FALSE;
    }

    *Offset = 0;
    Current = 0x100;

    for (GuardCount = 0;
         GuardCount < 256 &&
         Current >= 0x100 &&
         Current < PCI_EXTENDED_CONFIG_LENGTH;
         GuardCount++)
    {
        HalpReadPCIConfig(BusHandler,
                          Slot,
                          &Header,
                          Current,
                          sizeof(Header));

        if ((Header == 0) || (Header == 0xFFFFFFFF))
        {
            break;
        }

        if ((USHORT)(Header & 0xFFFF) == CapabilityId)
        {
            *Offset = Current;
            return TRUE;
        }

        USHORT Upper = (USHORT)((Header >> 16) & 0xFFFF);
        USHORT Next = (USHORT)(((Upper >> 4) & 0x0FFF) << 2);

        if (!Next || Next == Current)
        {
            break;
        }

        Current = Next;
    }

    return FALSE;
}

static VOID
HalpPciConfigureNativeExpressServices(
    _In_ PBUS_HANDLER BusHandler,
    _Inout_ PPCIPBUSDATA BusData)
{
    PCI_SLOT_NUMBER Slot;
    UCHAR Device;
    UCHAR Function;
    UCHAR MaxFunction;
    BOOLEAN HotPlugConfigured = FALSE;
    BOOLEAN PmeConfigured = FALSE;
    BOOLEAN AerConfigured = FALSE;

    if (!BusHandler || !BusData)
    {
        return;
    }

    if (BusData->NativeExpressServicesConfigured)
    {
        return;
    }

    if (!BusData->OscExpressCapability ||
        (!BusData->OscNativeHotPlug &&
         !BusData->OscNativePme &&
         !BusData->OscNativeAer))
    {
        BusData->NativeExpressServicesConfigured = TRUE;
        return;
    }

    Slot.u.AsULONG = 0;

    for (Device = 0; Device < BusData->MaxDevice; Device++)
    {
        MaxFunction = 1;

        for (Function = 0; Function < MaxFunction; Function++)
        {
            USHORT VendorId;
            UCHAR HeaderType;
            UCHAR BaseClass;
            UCHAR SubClass;
            USHORT ExpressOffset;
            PCI_EXPRESS_CAPABILITIES_REGISTER ExpressCaps;
            PCI_EXPRESS_DEVICE_TYPE DeviceType;
            BOOLEAN LocalHotPlugConfigured = FALSE;
            BOOLEAN LocalPmeConfigured = FALSE;
            BOOLEAN LocalAerConfigured = FALSE;

            Slot.u.AsULONG = 0;
            Slot.u.bits.DeviceNumber = Device;
            Slot.u.bits.FunctionNumber = Function;

            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &VendorId,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID),
                              sizeof(VendorId));
            if (VendorId == PCI_INVALID_VENDORID)
            {
                if (Function == 0)
                {
                    break;
                }

                continue;
            }

            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &HeaderType,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType),
                              sizeof(HeaderType));

            if (Function == 0 && (HeaderType & PCI_MULTIFUNCTION))
            {
                MaxFunction = PCI_MAX_FUNCTION;
            }

            if ((HeaderType & ~PCI_MULTIFUNCTION) != PCI_BRIDGE_TYPE)
            {
                continue;
            }

            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &BaseClass,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, BaseClass),
                              sizeof(BaseClass));
            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &SubClass,
                              FIELD_OFFSET(PCI_COMMON_CONFIG, SubClass),
                              sizeof(SubClass));

            if ((BaseClass != PCI_CLASS_BRIDGE_DEV) ||
                (SubClass != PCI_SUBCLASS_BR_PCI_TO_PCI))
            {
                continue;
            }

            if (!HalpPciFindCapability(BusHandler,
                                       Slot,
                                       HeaderType,
                                       PCI_CAPABILITY_ID_PCI_EXPRESS,
                                       &ExpressOffset))
            {
                continue;
            }

            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &ExpressCaps,
                              ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, ExpressCapabilities),
                              sizeof(ExpressCaps));

            DeviceType = (PCI_EXPRESS_DEVICE_TYPE)ExpressCaps.DeviceType;
            if ((DeviceType != PciExpressRootPort) &&
                (DeviceType != PciExpressRootComplexEventCollector))
            {
                continue;
            }

            if (BusData->OscNativeHotPlug && ExpressCaps.SlotImplemented)
            {
                PCI_EXPRESS_SLOT_CAPABILITIES_REGISTER SlotCaps;
                PCI_EXPRESS_SLOT_CONTROL_REGISTER SlotControl;
                PCI_EXPRESS_SLOT_STATUS_REGISTER SlotStatus;
                PCI_EXPRESS_LINK_CAPABILITIES_REGISTER LinkCaps;
                BOOLEAN Updated = FALSE;

                HalpReadPCIConfig(BusHandler,
                                  Slot,
                                  &SlotCaps,
                                  ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, SlotCapabilities),
                                  sizeof(SlotCaps));
                HalpReadPCIConfig(BusHandler,
                                  Slot,
                                  &SlotControl,
                                  ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, SlotControl),
                                  sizeof(SlotControl));
                HalpReadPCIConfig(BusHandler,
                                  Slot,
                                  &LinkCaps,
                                  ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, LinkCapabilities),
                                  sizeof(LinkCaps));

                if (SlotCaps.AttentionButtonPresent && !SlotControl.AttentionButtonEnable)
                {
                    SlotControl.AttentionButtonEnable = 1;
                    Updated = TRUE;
                }

                if (SlotCaps.PowerControllerPresent && !SlotControl.PowerFaultDetectEnable)
                {
                    SlotControl.PowerFaultDetectEnable = 1;
                    Updated = TRUE;
                }

                if (SlotCaps.MRLSensorPresent && !SlotControl.MRLSensorEnable)
                {
                    SlotControl.MRLSensorEnable = 1;
                    Updated = TRUE;
                }

                if (!SlotControl.PresenceDetectEnable)
                {
                    SlotControl.PresenceDetectEnable = 1;
                    Updated = TRUE;
                }

                if (!SlotCaps.NoCommandCompletedSupport && !SlotControl.CommandCompletedEnable)
                {
                    SlotControl.CommandCompletedEnable = 1;
                    Updated = TRUE;
                }

                if (!SlotControl.HotPlugInterruptEnable)
                {
                    SlotControl.HotPlugInterruptEnable = 1;
                    Updated = TRUE;
                }

                if (LinkCaps.DataLinkLayerActiveReportingCapable &&
                    !SlotControl.DataLinkStateChangeEnable)
                {
                    SlotControl.DataLinkStateChangeEnable = 1;
                    Updated = TRUE;
                }

                if (Updated)
                {
                    HalpWritePCIConfig(BusHandler,
                                       Slot,
                                       &SlotControl,
                                       ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, SlotControl),
                                       sizeof(SlotControl));

                    DPRINT1("HAL: PCIe hotplug armed for %04lx:%02x:%02x.%u (SlotCtrl=0x%04x)\n",
                            (ULONG)BusData->PciSegment,
                            BusHandler->BusNumber,
                            Device,
                            Function,
                            SlotControl.AsUSHORT);
                }

                HalpReadPCIConfig(BusHandler,
                                  Slot,
                                  &SlotStatus,
                                  ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, SlotStatus),
                                  sizeof(SlotStatus));
                if (SlotStatus.AsUSHORT)
                {
                    if (SlotStatus.AttentionButtonPressed) SlotStatus.AttentionButtonPressed = 1;
                    if (SlotStatus.PowerFaultDetected) SlotStatus.PowerFaultDetected = 1;
                    if (SlotStatus.MRLSensorChanged) SlotStatus.MRLSensorChanged = 1;
                    if (SlotStatus.PresenceDetectChanged) SlotStatus.PresenceDetectChanged = 1;
                    if (SlotStatus.CommandCompleted) SlotStatus.CommandCompleted = 1;
                    if (SlotStatus.DataLinkStateChanged) SlotStatus.DataLinkStateChanged = 1;

                    HalpWritePCIConfig(BusHandler,
                                       Slot,
                                       &SlotStatus,
                                       ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, SlotStatus),
                                       sizeof(SlotStatus));
                }

                if (SlotCaps.HotPlugCapable || SlotCaps.HotPlugSurprise)
                {
                    LocalHotPlugConfigured = SlotControl.HotPlugInterruptEnable ? TRUE : FALSE;
                }
            }

            if (BusData->OscNativePme || BusData->OscNativeAer)
            {
                PCI_EXPRESS_ROOT_CONTROL_REGISTER RootControl;
                BOOLEAN RootUpdated = FALSE;

                HalpReadPCIConfig(BusHandler,
                                  Slot,
                                  &RootControl,
                                  ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, RootControl),
                                  sizeof(RootControl));

                if (BusData->OscNativePme && !RootControl.PMEInterruptEnable)
                {
                    RootControl.PMEInterruptEnable = 1;
                    RootUpdated = TRUE;
                }

                if (BusData->OscNativeAer)
                {
                    if (!RootControl.CorrectableSerrEnable)
                    {
                        RootControl.CorrectableSerrEnable = 1;
                        RootUpdated = TRUE;
                    }

                    if (!RootControl.NonFatalSerrEnable)
                    {
                        RootControl.NonFatalSerrEnable = 1;
                        RootUpdated = TRUE;
                    }

                    if (!RootControl.FatalSerrEnable)
                    {
                        RootControl.FatalSerrEnable = 1;
                        RootUpdated = TRUE;
                    }
                }

                if (RootUpdated)
                {
                    HalpWritePCIConfig(BusHandler,
                                       Slot,
                                       &RootControl,
                                       ExpressOffset + FIELD_OFFSET(PCI_EXPRESS_CAPABILITY, RootControl),
                                       sizeof(RootControl));

                    DPRINT1("HAL: PCIe root control enabled for %04lx:%02x:%02x.%u (RootCtrl=0x%04x)\n",
                            (ULONG)BusData->PciSegment,
                            BusHandler->BusNumber,
                            Device,
                            Function,
                            RootControl.AsUSHORT);
                }

                if (BusData->OscNativePme && RootControl.PMEInterruptEnable)
                {
                    LocalPmeConfigured = TRUE;
                }

                if (BusData->OscNativeAer)
                {
                    USHORT AerOffset;

                    if (HalpPciFindExtendedCapability(BusHandler,
                                                       Slot,
                                                       PCI_EXPRESS_ADVANCED_ERROR_REPORTING_CAP_ID,
                                                       &AerOffset))
                    {
                        PCI_EXPRESS_ROOT_ERROR_COMMAND RootCommand;
                        PCI_EXPRESS_ROOT_ERROR_STATUS RootStatus;
                        BOOLEAN AerUpdated = FALSE;

                        HalpReadPCIConfig(BusHandler,
                                          Slot,
                                          &RootCommand,
                                          AerOffset + FIELD_OFFSET(PCI_EXPRESS_ROOTPORT_AER_CAPABILITY, RootErrorCommand),
                                          sizeof(RootCommand));

                        if (!RootCommand.CorrectableErrorReportingEnable)
                        {
                            RootCommand.CorrectableErrorReportingEnable = 1;
                            AerUpdated = TRUE;
                        }

                        if (!RootCommand.NonFatalErrorReportingEnable)
                        {
                            RootCommand.NonFatalErrorReportingEnable = 1;
                            AerUpdated = TRUE;
                        }

                        if (!RootCommand.FatalErrorReportingEnable)
                        {
                            RootCommand.FatalErrorReportingEnable = 1;
                            AerUpdated = TRUE;
                        }

                        if (AerUpdated)
                        {
                            HalpWritePCIConfig(BusHandler,
                                               Slot,
                                               &RootCommand,
                                               AerOffset + FIELD_OFFSET(PCI_EXPRESS_ROOTPORT_AER_CAPABILITY, RootErrorCommand),
                                               sizeof(RootCommand));
                        }

                        HalpReadPCIConfig(BusHandler,
                                          Slot,
                                          &RootStatus,
                                          AerOffset + FIELD_OFFSET(PCI_EXPRESS_ROOTPORT_AER_CAPABILITY, RootErrorStatus),
                                          sizeof(RootStatus));

                        if (RootStatus.AsULONG)
                        {
                            if (RootStatus.CorrectableErrorReceived) RootStatus.CorrectableErrorReceived = 1;
                            if (RootStatus.MultipleCorrectableErrorsReceived) RootStatus.MultipleCorrectableErrorsReceived = 1;
                            if (RootStatus.UncorrectableErrorReceived) RootStatus.UncorrectableErrorReceived = 1;
                            if (RootStatus.MultipleUncorrectableErrorsReceived) RootStatus.MultipleUncorrectableErrorsReceived = 1;
                            if (RootStatus.FirstUncorrectableFatal) RootStatus.FirstUncorrectableFatal = 1;
                            if (RootStatus.NonFatalErrorMessagesReceived) RootStatus.NonFatalErrorMessagesReceived = 1;
                            if (RootStatus.FatalErrorMessagesReceived) RootStatus.FatalErrorMessagesReceived = 1;

                            HalpWritePCIConfig(BusHandler,
                                               Slot,
                                               &RootStatus,
                                               AerOffset + FIELD_OFFSET(PCI_EXPRESS_ROOTPORT_AER_CAPABILITY, RootErrorStatus),
                                               sizeof(RootStatus));
                        }

                        LocalAerConfigured = TRUE;
                    }
                    else
                    {
                        DPRINT1("HAL: PCIe root port %04lx:%02x:%02x.%u missing AER capability while native control granted.\n",
                                (ULONG)BusData->PciSegment,
                                BusHandler->BusNumber,
                                Device,
                                Function);
                    }
                }
            }

            HotPlugConfigured |= LocalHotPlugConfigured;
            PmeConfigured |= LocalPmeConfigured;
            AerConfigured |= LocalAerConfigured;
        }
    }

    if (BusData->OscNativeHotPlug && !HotPlugConfigured)
    {
        DPRINT1("HAL: No PCIe slots on bus %lu accepted native hotplug control.\n",
                BusHandler->BusNumber);
    }

    if (BusData->OscNativePme && !PmeConfigured)
    {
        DPRINT1("HAL: No PCIe root ports on bus %lu exposed native PME routing.\n",
                BusHandler->BusNumber);
    }

    if (BusData->OscNativeAer && !AerConfigured)
    {
        DPRINT1("HAL: No PCIe root ports on bus %lu enabled native AER reporting.\n",
                BusHandler->BusNumber);
    }

    BusData->NativeExpressServicesConfigured = TRUE;
}

static
VOID
HalpPciResetGsiTable(VOID)
{
    KIRQL OldIrql;

    HalpPciInitGsiLock();
    KeAcquireSpinLock(&HalpPciGsiLock, &OldIrql);

    if (HalpPciGsiInfoUsesPool && HalpPciGsiInfo)
    {
        ExFreePoolWithTag(HalpPciGsiInfo, HALP_PCI_GSI_TAG);
        HalpPciGsiInfoUsesPool = FALSE;
    }

    HalpPciGsiInfo = HalpPciGsiStaticInfo;
    HalpPciGsiCapacity = HALP_PCI_GSI_STATIC_CAPACITY;
    RtlZeroMemory(HalpPciGsiInfo,
                  HalpPciGsiCapacity * sizeof(HALP_PCI_GSI_INFO));

    KeReleaseSpinLock(&HalpPciGsiLock, OldIrql);
}

static
BOOLEAN
HalpPciEnsureGsiCapacity(
    _In_ ULONG Gsi)
{
    ULONG Required = Gsi + 1;
    ULONG NewCapacity;
    PHALP_PCI_GSI_INFO NewTable;
    BOOLEAN UsedAllocation = FALSE;
    KIRQL OldIrql;

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

    HalpPciInitGsiLock();
    KeAcquireSpinLock(&HalpPciGsiLock, &OldIrql);

    if (Required > HalpPciGsiCapacity)
    {
        if (HalpPciGsiInfo)
        {
            RtlCopyMemory(NewTable,
                          HalpPciGsiInfo,
                          HalpPciGsiCapacity * sizeof(HALP_PCI_GSI_INFO));

            if (HalpPciGsiInfoUsesPool)
            {
                ExFreePoolWithTag(HalpPciGsiInfo, HALP_PCI_GSI_TAG);
            }
        }

        HalpPciGsiInfo = NewTable;
        HalpPciGsiCapacity = NewCapacity;
        HalpPciGsiInfoUsesPool = TRUE;
        UsedAllocation = TRUE;
    }

    KeReleaseSpinLock(&HalpPciGsiLock, OldIrql);

    if (!UsedAllocation)
    {
        ExFreePoolWithTag(NewTable, HALP_PCI_GSI_TAG);
    }

    return TRUE;
}

VOID
HalpPciRecordGsiInfo(
    _In_ ULONG Gsi,
    _In_ UCHAR Polarity,
    _In_ UCHAR Trigger,
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _In_ UCHAR Device,
    _In_ UCHAR Function,
    _In_ UCHAR Pin,
    _In_ BOOLEAN FromFirmware)
{
    KIRQL OldIrql;

    if (!HalpPciEnsureGsiCapacity(Gsi))
    {
        return;
    }

    HalpPciInitGsiLock();
    KeAcquireSpinLock(&HalpPciGsiLock, &OldIrql);

    if ((Gsi >= HalpPciGsiCapacity) || !HalpPciGsiInfo)
    {
        KeReleaseSpinLock(&HalpPciGsiLock, OldIrql);
        return;
    }

    HalpPciGsiInfo[Gsi].Valid = TRUE;
    HalpPciGsiInfo[Gsi].Polarity = Polarity;
    HalpPciGsiInfo[Gsi].Trigger = Trigger;
    HalpPciGsiInfo[Gsi].Segment = (USHORT)Segment;
    HalpPciGsiInfo[Gsi].Bus = Bus;
    HalpPciGsiInfo[Gsi].Device = Device;
    HalpPciGsiInfo[Gsi].Function = Function;
    HalpPciGsiInfo[Gsi].Pin = Pin;
    HalpPciGsiInfo[Gsi].FromFirmware = FromFirmware;

    KeReleaseSpinLock(&HalpPciGsiLock, OldIrql);
}

static VOID
HalpPciPropagateRootConfiguration(
    _In_ PBUS_HANDLER RootBus)
{
    ULONG BusNumber;
    PBUS_HANDLER Child;
    PPCIPBUSDATA RootData;

    if (!RootBus)
    {
        return;
    }

    RootData = (PPCIPBUSDATA)RootBus->BusData;
    if (!RootData)
    {
        return;
    }

    ULONG StartBus;
    ULONG EndBus;

    if (RootData->BusNumbersConfigured)
    {
        StartBus = RootData->BusNumberStart;
        EndBus = RootData->BusNumberEnd;
    }
    else
    {
        StartBus = HalpMinPciBus;
        EndBus = HalpMaxPciBus;
    }

    for (BusNumber = StartBus; BusNumber <= EndBus; ++BusNumber)
    {
        Child = HalHandlerForBus(PCIBus, BusNumber);
        if (!Child || Child == RootBus)
        {
            continue;
        }

        if (!HalpPciBusBelongsToRoot(Child, RootBus))
        {
            continue;
        }

        if (!Child->BusData)
        {
            continue;
        }

        ((PPCIPBUSDATA)Child->BusData)->PciSegment = RootData->PciSegment;
        ((PPCIPBUSDATA)Child->BusData)->AcpiRootInfo = RootData->AcpiRootInfo;
        ((PPCIPBUSDATA)Child->BusData)->OscInfo = RootData->OscInfo;
        ((PPCIPBUSDATA)Child->BusData)->OscSupportSet = RootData->OscSupportSet;
        ((PPCIPBUSDATA)Child->BusData)->OscControlRequest = RootData->OscControlRequest;
        ((PPCIPBUSDATA)Child->BusData)->OscControlGranted = RootData->OscControlGranted;
        ((PPCIPBUSDATA)Child->BusData)->OscMaskedControls = RootData->OscMaskedControls;
        ((PPCIPBUSDATA)Child->BusData)->OscNativeHotPlug = RootData->OscNativeHotPlug;
        ((PPCIPBUSDATA)Child->BusData)->OscNativePme = RootData->OscNativePme;
        ((PPCIPBUSDATA)Child->BusData)->OscNativeAer = RootData->OscNativeAer;
        ((PPCIPBUSDATA)Child->BusData)->OscExpressCapability = RootData->OscExpressCapability;
        ((PPCIPBUSDATA)Child->BusData)->NativeExpressServicesConfigured = RootData->NativeExpressServicesConfigured;
        ((PPCIPBUSDATA)Child->BusData)->MsiSupported = RootData->MsiSupported;
        ((PPCIPBUSDATA)Child->BusData)->BusNumbersConfigured =
            RootData->BusNumbersConfigured;
        ((PPCIPBUSDATA)Child->BusData)->BusNumberStart =
            RootData->BusNumberStart;
        ((PPCIPBUSDATA)Child->BusData)->BusNumberEnd =
            RootData->BusNumberEnd;

        if (!((PPCIPBUSDATA)Child->BusData)->AcpiRootConfigured)
        {
            ((PPCIPBUSDATA)Child->BusData)->AcpiRootConfigured = TRUE;
        }

        HalpPciApplyConfiguredWindows(Child);
    }
}



BOOLEAN
HalpPciLookupGsiInfo(
    _In_ ULONG Gsi,
    _Out_ PUCHAR Polarity,
    _Out_ PUCHAR Trigger)
{
    BOOLEAN Found = FALSE;
    KIRQL OldIrql;

    HalpPciInitGsiLock();
    KeAcquireSpinLock(&HalpPciGsiLock, &OldIrql);

    if ((Gsi < HalpPciGsiCapacity) &&
        HalpPciGsiInfo &&
        HalpPciGsiInfo[Gsi].Valid)
    {
        if (Polarity) *Polarity = HalpPciGsiInfo[Gsi].Polarity;
        if (Trigger) *Trigger = HalpPciGsiInfo[Gsi].Trigger;
        Found = TRUE;
    }

    KeReleaseSpinLock(&HalpPciGsiLock, OldIrql);
    return Found;
}

BOOLEAN
HalpPciDescribeGsi(
    _In_ ULONG Gsi,
    _Out_ PHALP_PCI_GSI_DIAG Diag)
{
    BOOLEAN Found = FALSE;
    KIRQL OldIrql;

    if (!Diag)
    {
        return FALSE;
    }

    HalpPciInitGsiLock();
    KeAcquireSpinLock(&HalpPciGsiLock, &OldIrql);

    if ((Gsi < HalpPciGsiCapacity) &&
        HalpPciGsiInfo &&
        HalpPciGsiInfo[Gsi].Valid)
    {
        Diag->Segment = HalpPciGsiInfo[Gsi].Segment;
        Diag->Bus = HalpPciGsiInfo[Gsi].Bus;
        Diag->Device = HalpPciGsiInfo[Gsi].Device;
        Diag->Function = HalpPciGsiInfo[Gsi].Function;
        Diag->Pin = HalpPciGsiInfo[Gsi].Pin;
        Diag->FromFirmware = HalpPciGsiInfo[Gsi].FromFirmware;
        Found = TRUE;
    }

    KeReleaseSpinLock(&HalpPciGsiLock, OldIrql);
    return Found;
}

static
BOOLEAN
HalpPciBusBelongsToRoot(
    _In_ PBUS_HANDLER BusHandler,
    _In_ PBUS_HANDLER RootBus)
{
    PBUS_HANDLER Current;
    PPCIPBUSDATA RootData;
    PPCIPBUSDATA BusData;

    if (!BusHandler || !RootBus)
    {
        return FALSE;
    }

    if (BusHandler == RootBus)
    {
        return TRUE;
    }

    Current = BusHandler;
    while (Current && (Current->InterfaceType == PCIBus))
    {
        if (Current == RootBus)
        {
            return TRUE;
        }

        Current = Current->ParentHandler;
    }

    RootData = (PPCIPBUSDATA)RootBus->BusData;
    BusData = (PPCIPBUSDATA)BusHandler->BusData;

    if (!RootData)
    {
        return FALSE;
    }

    if (RootData->BusNumbersConfigured)
    {
        if ((BusHandler->BusNumber < RootData->BusNumberStart) ||
            (BusHandler->BusNumber > RootData->BusNumberEnd))
        {
            return FALSE;
        }
    }

    if (BusData && (BusData->PciSegment != RootData->PciSegment))
    {
        return FALSE;
    }

    return TRUE;
}

static
VOID
HalpPciApplyConfiguredWindows(
    _In_ PBUS_HANDLER BusHandler)
{
    PPCIPBUSDATA BusData;
    PSUPPORTED_RANGES Ranges;

    if (!BusHandler)
    {
        return;
    }

    BusData = (PPCIPBUSDATA)BusHandler->BusData;
    if (!BusData)
    {
        return;
    }

    PHAL_ACPI_PCI_ROOT_INFO RootInfo = BusData->AcpiRootInfo;

    BusData->ResourcesInitialized = TRUE;

    if (BusData->AcpiRootConfigured &&
        (BusData->IoWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->IoWindowBase <= BusData->IoWindowLimit))
    {
        BusData->IoBase = BusData->IoWindowBase;
        BusData->IoLimit = BusData->IoWindowLimit;
    }
    else
    {
        BusData->IoBase = HALP_PCI_DEFAULT_IO_BASE;
        BusData->IoLimit = HALP_PCI_DEFAULT_IO_LIMIT;
    }
    BusData->IoNext = BusData->IoBase;

    if (BusData->AcpiRootConfigured &&
        (BusData->MemoryWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->MemoryWindowBase <= BusData->MemoryWindowLimit))
    {
        BusData->MemoryBase = BusData->MemoryWindowBase;
        BusData->MemoryLimit = BusData->MemoryWindowLimit;
    }
    else
    {
        BusData->MemoryBase = HALP_PCI_DEFAULT_MEM_BASE;
        BusData->MemoryLimit = HALP_PCI_DEFAULT_MEM_LIMIT;
    }
    BusData->MemoryNext = BusData->MemoryBase;

    if (!BusData->AcpiRootConfigured ||
        (BusData->PrefetchWindowBase == HALP_INVALID_RANGE_BASE) ||
        (BusData->PrefetchWindowBase > BusData->PrefetchWindowLimit))
    {
        BusData->PrefetchWindowBase = HALP_INVALID_RANGE_BASE;
        BusData->PrefetchWindowLimit = 0;
    }

    Ranges = BusHandler->BusAddresses;
    if (!Ranges)
    {
        return;
    }

    Ranges->Sorted = TRUE;
    Ranges->IO.Next = NULL;
    Ranges->Memory.Next = NULL;
    Ranges->PrefetchMemory.Next = NULL;

    Ranges->NoIO = 1;
    if (BusData->AcpiRootConfigured &&
        (BusData->IoWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->IoWindowBase <= BusData->IoWindowLimit))
    {
        Ranges->IO.Base = BusData->IoWindowBase;
        Ranges->IO.Limit = BusData->IoWindowLimit;
    }
    else
    {
        Ranges->IO.Base = 0;
        Ranges->IO.Limit = BusData->IoLimit;
    }
    if (BusData->AcpiRootConfigured && RootInfo && RootInfo->IoWindow.HasTranslation &&
        (BusData->IoWindowBase != HALP_INVALID_RANGE_BASE))
    {
        Ranges->IO.SystemBase = (LONGLONG)(BusData->IoWindowBase + RootInfo->IoWindow.Translation);
    }
    else
    {
        Ranges->IO.SystemBase = 0;
    }
    Ranges->IO.SystemAddressSpace = 1;

    Ranges->NoMemory = 1;
    if (BusData->AcpiRootConfigured &&
        (BusData->MemoryWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->MemoryWindowBase <= BusData->MemoryWindowLimit))
    {
        Ranges->Memory.Base = BusData->MemoryWindowBase;
        Ranges->Memory.Limit = BusData->MemoryWindowLimit;
    }
    else
    {
        Ranges->Memory.Base = 0;
        Ranges->Memory.Limit = BusData->MemoryLimit;
    }
    if (BusData->AcpiRootConfigured && RootInfo && RootInfo->MemoryWindow.HasTranslation &&
        (BusData->MemoryWindowBase != HALP_INVALID_RANGE_BASE))
    {
        Ranges->Memory.SystemBase = (LONGLONG)(BusData->MemoryWindowBase + RootInfo->MemoryWindow.Translation);
    }
    else
    {
        Ranges->Memory.SystemBase = 0;
    }
    Ranges->Memory.SystemAddressSpace = 0;

    if (BusData->PrefetchWindowBase != HALP_INVALID_RANGE_BASE &&
        BusData->PrefetchWindowBase <= BusData->PrefetchWindowLimit)
    {
        Ranges->NoPrefetchMemory = 1;
        Ranges->PrefetchMemory.Base = BusData->PrefetchWindowBase;
        Ranges->PrefetchMemory.Limit = BusData->PrefetchWindowLimit;
    }
    else
    {
        Ranges->NoPrefetchMemory = 0;
        Ranges->PrefetchMemory.Base = 0;
        Ranges->PrefetchMemory.Limit = 0;
    }
    if (BusData->AcpiRootConfigured && RootInfo && RootInfo->PrefetchWindow.HasTranslation &&
        (BusData->PrefetchWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->PrefetchWindowBase <= BusData->PrefetchWindowLimit))
    {
        Ranges->PrefetchMemory.SystemBase = (LONGLONG)(BusData->PrefetchWindowBase + RootInfo->PrefetchWindow.Translation);
    }
    else
    {
        Ranges->PrefetchMemory.SystemBase = 0;
    }
    Ranges->PrefetchMemory.SystemAddressSpace = 0;
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
HalpSetPciRoutingMap(
    _In_reads_opt_(EntryCount) const HAL_ACPI_PCI_ROUTE_ENTRY *Entries,
    _In_ ULONG EntryCount)
{
    ULONG Index;
    ULONG Recorded = 0;
    ULONG MaxGsi = 0;

    HalpPciResetGsiTable();

    if (!Entries || EntryCount == 0)
    {
        DPRINT1("HAL: Cleared PCI routing table (no _PRT entries).\n");
        return;
    }

    for (Index = 0; Index < EntryCount; ++Index)
    {
        const HAL_ACPI_PCI_ROUTE_ENTRY *Entry = &Entries[Index];

        if ((Entry->Pin < 1) || (Entry->Pin > 4))
        {
            DPRINT1("HAL: Ignoring invalid PCI routing entry for bus %u dev %u pin %u (segment %lu).\n",
                    Entry->Bus,
                    Entry->Device,
                    Entry->Pin,
                    Entry->Segment);
            continue;
        }

        HalpPciRecordGsiInfo(Entry->Gsi,
                             Entry->Polarity,
                             Entry->TriggerMode,
                             Entry->Segment,
                             Entry->Bus,
                             Entry->Device,
                             0xFF,
                             Entry->Pin,
                             TRUE);
        ++Recorded;

        if (Entry->Gsi > MaxGsi)
        {
            MaxGsi = Entry->Gsi;
        }
    }

    if (Recorded == 0)
    {
        DPRINT1("HAL: PCI routing map contained only invalid entries; GSI table remains empty.\n");
    }
    else
    {
        DPRINT1("HAL: Imported %lu PCI routing entries (max GSI %lu).\n",
                Recorded,
                MaxGsi);
    }
}

VOID
NTAPI
HalpRecordPciMaxGsi(
    _In_ const HAL_ACPI_PCI_ROUTE_ENTRY *Entry)
{
    if (Entry && Entry->Gsi != 0)
    {
        HalpPciEnsureGsiCapacity(Entry->Gsi);
    }
}

BOOLEAN
NTAPI
HalIsPciMsiSupported(VOID)
{
    return HalpPciMsiSupported;
}

BOOLEAN
NTAPI
HalQueryPciMsiSupport(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _Out_opt_ PBOOLEAN Supported,
    _Out_opt_ PULONG OscStatusFlags,
    _Out_opt_ PULONG OscControlGranted,
    _Out_opt_ PUSHORT EffectiveSegment,
    _Out_opt_ PULONG OscMaskedControls)
{
    PBUS_HANDLER Handler;
    PPCIPBUSDATA BusData;

    Handler = HalHandlerForBus(PCIBus, Bus);
    if (!Handler)
    {
        return FALSE;
    }

    BusData = (PPCIPBUSDATA)Handler->BusData;
    if (!BusData)
    {
        return FALSE;
    }

    if ((Segment != 0) && (BusData->PciSegment != (USHORT)Segment))
    {
        return FALSE;
    }

    if (Supported)
    {
        *Supported = BusData->MsiSupported;
    }
    if (EffectiveSegment)
    {
        *EffectiveSegment = BusData->PciSegment;
    }
    if (OscStatusFlags)
    {
        *OscStatusFlags = BusData->OscInfo.StatusFlags;
    }
    if (OscControlGranted)
    {
        *OscControlGranted = BusData->OscControlGranted;
    }
    if (OscMaskedControls)
    {
        *OscMaskedControls = BusData->OscMaskedControls;
    }

    return TRUE;
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
        /* Try to create a concrete PCI bus handler (Type 1 by default) if available */
        Bus = HalpTryAllocateAndInitPciBusHandler(1, Info->Bus, FALSE);
        if (!Bus)
        {
            /* Fall back to the fake handler as a last resort */
            extern BUS_HANDLER HalpFakePciBusHandler;
            Bus = &HalpFakePciBusHandler;
            Bus->BusNumber = Info->Bus;
            DPRINT("HAL: Using temporary PCI handler for ACPI root bus %lu\n", Info->Bus);
        }
        else
        {
            DPRINT("HAL: Registered PCI bus handler for ACPI root bus %lu\n", Info->Bus);
        }
    }

    BusData = (PPCIPBUSDATA)Bus->BusData;
    if (!BusData)
    {
        return;
    }

    BusData->PciSegment = (USHORT)Info->Segment;
    if (BusData->AcpiRootInfo)
    {
        ExFreePoolWithTag(BusData->AcpiRootInfo, HALP_PCI_ROOT_TAG);
        BusData->AcpiRootInfo = NULL;
    }

    BusData->AcpiRootInfo = ExAllocatePoolWithTag(NonPagedPool,
                                                  sizeof(*Info),
                                                  HALP_PCI_ROOT_TAG);
    if (BusData->AcpiRootInfo)
    {
        *BusData->AcpiRootInfo = *Info;
    }
    else
    {
        DPRINT1("HAL: Failed to cache ACPI info for PCI segment %lu bus %lu\n",
                Info->Segment,
                Info->Bus);
    }

    BusData->OscInfo = Info->Osc;
    BusData->OscSupportSet = Info->Osc.SupportSet;
    BusData->OscControlRequest = Info->Osc.ControlRequest;
    BusData->OscControlGranted = Info->Osc.ControlGranted;
    BusData->OscMaskedControls = Info->Osc.ControlRequest & ~Info->Osc.ControlGranted;
    BusData->OscNativeHotPlug = (Info->Osc.ControlGranted & HAL_ACPI_OSC_CONTROL_NATIVE_HOTPLUG) != 0;
    BusData->OscNativePme = (Info->Osc.ControlGranted & HAL_ACPI_OSC_CONTROL_NATIVE_PME) != 0;
    BusData->OscNativeAer = (Info->Osc.ControlGranted & HAL_ACPI_OSC_CONTROL_NATIVE_AER) != 0;
    BusData->OscExpressCapability = (Info->Osc.ControlGranted & HAL_ACPI_OSC_CONTROL_EXPRESS_CAP) != 0;
    BusData->MsiSupported = TRUE;

    if (Info->MaxGsi != 0)
    {
        HalpPciEnsureGsiCapacity(Info->MaxGsi);
        if (Info->MaxGsi > 11)
        {
            DPRINT1("HAL: PCI root seg %lu bus %lu: extending GSI capacity to %lu based on _CRS\n",
                    Info->Segment,
                    Info->Bus,
                    Info->MaxGsi);
        }
    }

    if (Info->Osc.Evaluated)
    {
        const ULONG OscFatalMask = OSC_FIRMWARE_FAILURE; /* treat only firmware failure as fatal */
        const ULONG OscMasked = OSC_CAPABILITIES_MASKED; /* capabilities masked */

        if (Info->Osc.StatusFlags & OscMasked)
        {
            ULONG MaskedControls = Info->Osc.ControlRequest & ~Info->Osc.ControlGranted;
            DPRINT1("HAL: _OSC capabilities masked on segment %lu bus %lu (req 0x%lx grant 0x%lx masked 0x%lx)\n",
                    Info->Segment,
                    Info->Bus,
                    Info->Osc.ControlRequest,
                    Info->Osc.ControlGranted,
                    MaskedControls);
        }

        if (Info->Osc.StatusFlags & OscFatalMask)
        {
            HalpPciMsiSupported = FALSE;
            BusData->MsiSupported = FALSE;
        }
    }

    if (BusData->OscControlRequest && !BusData->OscExpressCapability)
    {
        /* Firmware withheld PCIe capability control. Keep ECAM available if
         * MCFG validated, but log that we may need to fall back to legacy
         * config on a per-access basis. */
        DPRINT1("HAL: PCI segment %lu bus %lu: firmware kept PCIe capability control; ECAM kept available (legacy fallback per access).\n",
                Info->Segment,
                Info->Bus);
    }

    if (BusData->OscControlRequest & HAL_ACPI_OSC_CONTROL_NATIVE_HOTPLUG)
    {
        if (!BusData->OscNativeHotPlug)
        {
            DPRINT1("HAL: PCI segment %lu bus %lu: using legacy hotplug helpers (firmware withheld native control).\n",
                    Info->Segment,
                    Info->Bus);
        }
    }

    if (BusData->OscControlRequest & HAL_ACPI_OSC_CONTROL_NATIVE_PME)
    {
        if (!BusData->OscNativePme)
        {
            DPRINT1("HAL: PCI segment %lu bus %lu: keeping legacy PME routing (firmware withheld native PME control).\n",
                    Info->Segment,
                    Info->Bus);
        }
    }

    if (BusData->OscControlRequest & HAL_ACPI_OSC_CONTROL_NATIVE_AER)
    {
        if (!BusData->OscNativeAer)
        {
            DPRINT1("HAL: PCI segment %lu bus %lu: firmware retained PCIe AER handling, leaving HAL in legacy mode.\n",
                    Info->Segment,
                    Info->Bus);
        }
    }

    if (Info->BusRangePresent)
    {
        BusData->BusNumbersConfigured = TRUE;
        BusData->BusNumberStart = (UCHAR)Info->BusStart;
        BusData->BusNumberEnd = (UCHAR)Info->BusEnd;

        if (!HalpPciBusRangeKnown)
        {
            HalpMinPciBus = BusData->BusNumberStart;
            HalpMaxPciBus = BusData->BusNumberEnd;
            HalpPciBusRangeKnown = TRUE;
        }
        else
        {
            if (BusData->BusNumberStart < HalpMinPciBus)
            {
                HalpMinPciBus = BusData->BusNumberStart;
            }

            if (BusData->BusNumberEnd > HalpMaxPciBus)
            {
                HalpMaxPciBus = BusData->BusNumberEnd;
            }
        }
    }
    else
    {
        BusData->BusNumbersConfigured = FALSE;
        BusData->BusNumberStart = (UCHAR)Info->Bus;
        BusData->BusNumberEnd = (UCHAR)Info->Bus;
    }

    if (Info->IoWindow.Present)
    {
        BusData->IoWindowBase = Info->IoWindow.Base;
        BusData->IoWindowLimit = Info->IoWindow.Limit;
    }
    else
    {
        BusData->IoWindowBase = HALP_INVALID_RANGE_BASE;
        BusData->IoWindowLimit = 0;
    }

    if (Info->MemoryWindow.Present)
    {
        BusData->MemoryWindowBase = Info->MemoryWindow.Base;
        BusData->MemoryWindowLimit = Info->MemoryWindow.Limit;
    }
    else
    {
        BusData->MemoryWindowBase = HALP_INVALID_RANGE_BASE;
        BusData->MemoryWindowLimit = 0;
    }

    if (Info->PrefetchWindow.Present)
    {
        BusData->PrefetchWindowBase = Info->PrefetchWindow.Base;
        BusData->PrefetchWindowLimit = Info->PrefetchWindow.Limit;
    }
    else
    {
        BusData->PrefetchWindowBase = HALP_INVALID_RANGE_BASE;
        BusData->PrefetchWindowLimit = 0;
    }

    if (!BusData->AcpiRootConfigured)
    {
        PHALP_ACPI_MCFG_ALLOCATION Allocation;

        Allocation = HalpAcpiGetMcfgAllocation((USHORT)Info->Segment,
                                               (UCHAR)Info->Bus);
        if (Allocation)
        {
            DbgPrint("HAL: ACPI root %04lu:%02lu using MMCONFIG base %I64x (buses %u-%u).\n",
                     Info->Segment,
                     Info->Bus,
                     Allocation->BaseAddress,
                     Allocation->StartBusNumber,
                     Allocation->EndBusNumber);
        }
        else
        {
            DbgPrint("HAL: ACPI root %04lu:%02lu using legacy PCI configuration access.\n",
                     Info->Segment,
                     Info->Bus);
        }
    }

    BusData->AcpiRootConfigured = TRUE;
    HalpPciConfigureNativeExpressServices(Bus, BusData);
    HalpPciApplyConfiguredWindows(Bus);
    HalpPciPropagateRootConfiguration(Bus);
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
        PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;

        if (Length && BusData && BusData->AcpiRootConfigured &&
            HalpAcpiAccessConfigEcam(FALSE,
                                     BusData->PciSegment,
                                     BusHandler->BusNumber,
                                     Slot,
                                     Buffer,
                                     Offset,
                                     Length))
        {
            return;
        }

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
        PPCIPBUSDATA BusData = (PPCIPBUSDATA)BusHandler->BusData;

        if (Length && BusData && BusData->AcpiRootConfigured &&
            HalpAcpiAccessConfigEcam(TRUE,
                                     BusData->PciSegment,
                                     BusHandler->BusNumber,
                                     Slot,
                                     Buffer,
                                     Offset,
                                     Length))
        {
            return;
        }

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

    /* Use ECAM for early config space only when globally enabled;
       otherwise rely on legacy type 1 (CF8/CFC) and avoid noisy
       "ECAM disabled globally" traces from the debug walker. */
    if (Length &&
        !HalpAcpiEcamDisabled &&
        HalpAcpiAccessConfigEcam(FALSE,
                                 HALP_ACPI_SEGMENT_ANY,
                                 Bus,
                                 PciSlot,
                                 Buffer,
                                 Offset,
                                 Length))
    {
        return Length;
    }

    {
        UCHAR ConfigControl;

        ConfigControl = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0xCFB);
        if (!(ConfigControl & 0x01))
        {
            WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)0xCFB, ConfigControl | 0x01);
        }
    }

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

    if (Length &&
        HalpAcpiAccessConfigEcam(TRUE,
                                 HALP_ACPI_SEGMENT_ANY,
                                 Bus,
                                 PciSlot,
                                 Buffer,
                                 Offset,
                                 Length))
    {
        return Length;
    }

    {
        UCHAR ConfigControl;

        ConfigControl = READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)0xCFB);
        if (!(ConfigControl & 0x01))
        {
            WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)0xCFB, ConfigControl | 0x01);
        }
    }

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
            HalpPciRecordGsiInfo(Gsi,
                                 Polarity,
                                 Trigger,
                                 BusData ? BusData->PciSegment : 0,
                                 (UCHAR)BusHandler->BusNumber,
                                 SlotNumber.u.bits.DeviceNumber,
                                 SlotNumber.u.bits.FunctionNumber,
                                 Pin,
                                 TRUE);
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
                HalpPciRecordGsiInfo(Gsi,
                                     Polarity,
                                     Trigger,
                                     BusData ? BusData->PciSegment : 0,
                                     (UCHAR)BusHandler->BusNumber,
                                     PciSlot.u.bits.DeviceNumber,
                                     PciSlot.u.bits.FunctionNumber,
                                     PciData.u.type0.InterruptPin,
                                     TRUE);
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

                case CmResourceTypeInterrupt:
                {
                    if (HalpPciRouteQueryCallback)
                    {
                        PCI_COMMON_CONFIG PciConfig;
                        ULONG BytesRead;
                        UCHAR Pin;

                        RtlZeroMemory(&PciConfig, sizeof(PciConfig));
                        BytesRead = HalGetBusData(PCIConfiguration,
                                                  BusHandler->BusNumber,
                                                  SlotNumber.u.AsULONG,
                                                  &PciConfig,
                                                  PCI_COMMON_HDR_LENGTH);
                        if (BytesRead == PCI_COMMON_HDR_LENGTH)
                        {
                            Pin = PciConfig.u.type0.InterruptPin;
                            if (Pin && Pin <= 4)
                            {
                                ULONG Gsi;
                                UCHAR Polarity;
                                UCHAR Trigger;

                                if (HalpPciRouteQueryCallback(BusData ? BusData->PciSegment : 0,
                                                              (UCHAR)BusHandler->BusNumber,
                                                              SlotNumber.u.bits.DeviceNumber,
                                                              SlotNumber.u.bits.FunctionNumber,
                                                              Pin,
                                                              &Gsi,
                                                              &Polarity,
                                                              &Trigger))
                                {
                                    HalpPciRecordGsiInfo(Gsi,
                                                         Polarity,
                                                         Trigger,
                                                         BusData ? BusData->PciSegment : 0,
                                                         (UCHAR)BusHandler->BusNumber,
                                                         SlotNumber.u.bits.DeviceNumber,
                                                         SlotNumber.u.bits.FunctionNumber,
                                                         Pin,
                                                         TRUE);

                                    Descriptor->u.Interrupt.MinimumVector = Gsi;
                                    Descriptor->u.Interrupt.MaximumVector = Gsi;
                                    Descriptor->ShareDisposition = CmResourceShareShared;
                                    Descriptor->u.Interrupt.AffinityPolicy = IrqPolicyMachineDefault;
                                    Descriptor->u.Interrupt.PriorityPolicy = IrqPriorityNormal;
                                    Descriptor->u.Interrupt.TargetedProcessors = HalpDefaultInterruptAffinity;

                                    Descriptor->Flags &= ~CM_RESOURCE_INTERRUPT_LEVEL_LATCHED_BITS;
                                    Descriptor->Flags |= (Trigger == HAL_ACPI_TRIGGER_LEVEL)
                                                             ? CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE
                                                             : CM_RESOURCE_INTERRUPT_LATCHED;
                                }
                                else
                                {
                                    DPRINT1("HAL: Failed to translate PCI interrupt for %02x:%02x.%u INT%c\n",
                                            (UCHAR)BusHandler->BusNumber,
                                            SlotNumber.u.bits.DeviceNumber,
                                            SlotNumber.u.bits.FunctionNumber,
                                            'A' + Pin - 1);
                                }
                            }
                        }
                    }

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
            ULONG InterruptLine = PciConfig.u.type0.InterruptLine;
            BOOLEAN Routed = FALSE;
            UCHAR Polarity = HAL_ACPI_POLARITY_LOW;
            UCHAR Trigger = HAL_ACPI_TRIGGER_LEVEL;

            if (HalpPciRouteQueryCallback && BusData)
            {
                ULONG Gsi;

                if (HalpPciRouteQueryCallback(BusData ? BusData->PciSegment : 0,
                                              (UCHAR)BusHandler->BusNumber,
                                              SlotNumber.u.bits.DeviceNumber,
                                              SlotNumber.u.bits.FunctionNumber,
                                              PciConfig.u.type0.InterruptPin,
                                              &Gsi,
                                              &Polarity,
                                              &Trigger))
                {
                    InterruptLine = Gsi;
                    Routed = TRUE;

                    HalpPciRecordGsiInfo(Gsi,
                                         Polarity,
                                         Trigger,
                                         BusData ? BusData->PciSegment : 0,
                                         (UCHAR)BusHandler->BusNumber,
                                         SlotNumber.u.bits.DeviceNumber,
                                         SlotNumber.u.bits.FunctionNumber,
                                         PciConfig.u.type0.InterruptPin,
                                         TRUE);
                }
            }

            Descriptor[Filled].Type = CmResourceTypeInterrupt;
            Descriptor[Filled].ShareDisposition = CmResourceShareShared;
            Descriptor[Filled].Flags = (Trigger == HAL_ACPI_TRIGGER_LEVEL)
                                           ? CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE
                                           : CM_RESOURCE_INTERRUPT_LATCHED;
            Descriptor[Filled].u.Interrupt.Level = InterruptLine;

            {
                ULONG SystemVector = 0;
                KAFFINITY InterruptAffinity;

                InterruptAffinity = (HalpDefaultInterruptAffinity != 0) ?
                                    HalpDefaultInterruptAffinity :
                                    (KAFFINITY)-1;

                if (InterruptLine <= 0xFF)
                {
                    SystemVector = HalpIrqToVector((UCHAR)InterruptLine);

                    if (SystemVector == 0)
                    {
                        KIRQL AllocatedIrql = 0;
                        KAFFINITY AllocatedAffinity = 0;
                        ULONG AllocatedVector;

                        AllocatedVector = HalpGetRootInterruptVector(InterruptLine,
                                                                    InterruptLine,
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

                if ((SystemVector != 0) && (SystemVector <= 0xFF))
                {
                    Descriptor[Filled].u.Interrupt.Vector = SystemVector;
                }
                else
                {
                    if (InterruptLine > 0xFF)
                    {
                        DPRINT1("HAL: GSI %u exceeds vector range; using raw value for %02x:%02x.%u INT%c.\n",
                                InterruptLine,
                                (UCHAR)BusHandler->BusNumber,
                                SlotNumber.u.bits.DeviceNumber,
                                SlotNumber.u.bits.FunctionNumber,
                                'A' + PciConfig.u.type0.InterruptPin - 1);
                    }
                    else if (SystemVector == 0)
                    {
                        DPRINT1("HAL: Unable to resolve system vector for GSI %u (%02x:%02x.%u INT%c); using raw value.\n",
                                InterruptLine,
                                (UCHAR)BusHandler->BusNumber,
                                SlotNumber.u.bits.DeviceNumber,
                                SlotNumber.u.bits.FunctionNumber,
                                'A' + PciConfig.u.type0.InterruptPin - 1);
                    }

                    Descriptor[Filled].u.Interrupt.Vector = InterruptLine;
                }

                Descriptor[Filled].u.Interrupt.Affinity = InterruptAffinity;
            }

            if (!Routed)
            {
                DPRINT1("HAL: Using legacy interrupt line %u for %02x:%02x.%u INT%c (route unavailable).\n",
                        InterruptLine,
                        (UCHAR)BusHandler->BusNumber,
                        SlotNumber.u.bits.DeviceNumber,
                        SlotNumber.u.bits.FunctionNumber,
                        'A' + PciConfig.u.type0.InterruptPin - 1);
            }

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

    HalpPciBusRangeKnown = FALSE;
    BusData->BusNumbersConfigured = FALSE;
    BusData->BusNumberStart = 0;
    BusData->BusNumberEnd = 0;
    RtlZeroMemory(&BusData->OscInfo, sizeof(BusData->OscInfo));
    BusData->OscSupportSet = 0;
    BusData->OscControlRequest = 0;
    BusData->OscControlGranted = 0;
    BusData->OscNativeHotPlug = FALSE;
    BusData->OscNativePme = FALSE;
    BusData->OscNativeAer = FALSE;
    BusData->OscExpressCapability = FALSE;
    BusData->MsiSupported = TRUE;
    BusData->NativeExpressServicesConfigured = FALSE;

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
