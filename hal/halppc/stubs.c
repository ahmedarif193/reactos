/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC PReP HAL miscellaneous services
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/halfuncs.h>
#include <ndk/iofuncs.h>
#include <reactos/hal/acpi_pci.h>
#include "halp.h"

/* Private kernel/HAL imports, not a stable third-party driver interface. */
DECLSPEC_NORETURN VOID NTAPI KiPpcUnimplemented(const CHAR *Routine);
VOID NTAPI KiPpcRequestSoftwareInterrupt(KIRQL Irql);
VOID NTAPI KiPpcClearSoftwareInterrupt(KIRQL Irql);
BOOLEAN NTAPI InbvDisplayString(PCSTR String);

/* Same contract as the x86 sti;hlt: returns with interrupts enabled after
 * the next one, or immediately. The 604 has no wait instruction that the
 * decrementer reliably ends, so this only enables interrupts. */
VOID
NTAPI
HalProcessorIdle(VOID)
{
    _enable();
}
BOOLEAN NTAPI HalMakeBeep(ULONG Frequency) { UNREFERENCED_PARAMETER(Frequency); return FALSE; }
ARC_STATUS NTAPI HalGetEnvironmentVariable(PCH Variable, USHORT Length, PCH Buffer) { return ENOENT; }
ARC_STATUS NTAPI HalSetEnvironmentVariable(PCH Name, PCH Value) { return EACCES; }
NTSTATUS NTAPI HalAdjustResourceList(PIO_RESOURCE_REQUIREMENTS_LIST *ResourceList)
{
    UNREFERENCED_PARAMETER(ResourceList);
    return STATUS_NOT_SUPPORTED; /* PCI resources are assigned by PnP. */
}

NTSTATUS
NTAPI
HalAssignSlotResources(PUNICODE_STRING RegistryPath, PUNICODE_STRING DriverClassName,
                       PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT DeviceObject,
                       INTERFACE_TYPE BusType, ULONG BusNumber, ULONG SlotNumber,
                       PCM_RESOURCE_LIST *AllocatedResources)
{
    /* This HAL supplies PCI config access and translation to the PnP PCI
     * bus driver. It has no legacy, non-PnP slot resource allocator. */
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    if (!AllocatedResources)
        return STATUS_INVALID_PARAMETER;
    *AllocatedResources = NULL;
    return STATUS_NOT_SUPPORTED;
}

/* No PowerPC MSI target exists yet. PCI falls back to legacy INTx. */
NTSTATUS
NTAPI
HalGetInterruptTargetInformation(PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    UNREFERENCED_PARAMETER(TargetInformation);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalGetMessageRoutingInfo(PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    UNREFERENCED_PARAMETER(RoutingInfo);
    return STATUS_NOT_SUPPORTED;
}

ULONG
NTAPI
HalGetInterruptVector(INTERFACE_TYPE InterfaceType, ULONG BusNumber, ULONG BusInterruptLevel, ULONG BusInterruptVector, PKIRQL Irql, PKAFFINITY Affinity)
{
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(BusInterruptVector);

    /* Every PReP device interrupt is an ISA IRQ of the 8259 pair (PCI
     * interrupts are routed onto it), delivered at PPC_HAL_EXTERNAL_IRQL. */
    if (!Irql || !Affinity || (BusInterruptLevel >= PPC_HAL_ISA_IRQ_COUNT) || (BusInterruptLevel == 2))
        return 0;
    if ((InterfaceType != Isa) && (InterfaceType != Eisa) && (InterfaceType != PCIBus) && (InterfaceType != Internal))
        return 0;
    *Irql = PPC_HAL_EXTERNAL_IRQL;
    *Affinity = 1;
    return BusInterruptLevel;
}

VOID
FASTCALL
HalRequestSoftwareInterrupt(KIRQL Irql)
{
    KiPpcRequestSoftwareInterrupt(Irql);
}

VOID
FASTCALL
HalClearSoftwareInterrupt(KIRQL Irql)
{
    KiPpcClearSoftwareInterrupt(Irql);
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    /* Order memory and I/O accesses. This does not perform device readback
     * or imply that a posted write has completed at a particular device. */
    __asm__ __volatile__("sync" ::: "memory");
}

VOID
NTAPI
HalReportResourceUsage(VOID)
{
    static WCHAR Name[] = L"PowerPC PReP HAL";
    UNICODE_STRING HalName;
    CM_RESOURCE_LIST List = {0};

    HalName.Buffer = Name;
    HalName.Length = sizeof(Name) - sizeof(WCHAR);
    HalName.MaximumLength = sizeof(Name);
    IoReportHalResourceUsage(&HalName, &List, &List, FIELD_OFFSET(CM_RESOURCE_LIST, List));
}

VOID
NTAPI
HalReturnToFirmware(
    _In_ FIRMWARE_REENTRY Action)
{
    if ((Action == HalRestartRoutine) || (Action == HalRebootRoutine))
    {
        /* PReP system control port A: bit 0 requests a fast reset. */
        WRITE_PORT_UCHAR((PUCHAR)0x92, READ_PORT_UCHAR((PUCHAR)0x92) & ~1);
        WRITE_PORT_UCHAR((PUCHAR)0x92, READ_PORT_UCHAR((PUCHAR)0x92) | 1);
    }

    /* Power-down is not wired: halt this processor. */
    _disable();
    for (;;)
        YieldProcessor();
}

/* The time base is the performance counter: take part in the rendezvous
 * without adjusting it. */
VOID
NTAPI
HalCalibratePerformanceCounter(
    _In_ volatile PLONG Count,
    _In_ ULONGLONG NewCount)
{
    UNREFERENCED_PARAMETER(NewCount);
    InterlockedDecrement(Count);
    while (*Count)
        YieldProcessor();
}

KIRQL
NTAPI
HalConvertDeviceIdtToIrql(
    _In_ ULONG Vector)
{
    UNREFERENCED_PARAMETER(Vector);
    return 0;
}

NTSTATUS
NTAPI
HalGetProcessorIdByNtNumber(
    _In_ ULONG ProcessorNumber,
    _Out_ PULONG ProcessorId)
{
    if (!ProcessorId || (ProcessorNumber != 0))
        return STATUS_INVALID_PARAMETER;
    *ProcessorId = 0;
    return STATUS_SUCCESS;
}

/* ACPI PCI routing hooks: this platform routes PCI INTx through its device
 * tree, and the ACPI driver is not part of it. */
VOID
NTAPI
HalpConfigurePciRootBridge(
    _In_ const HAL_ACPI_PCI_ROOT_INFO *Info)
{
    UNREFERENCED_PARAMETER(Info);
}

VOID
NTAPI
HalpRegisterPciRouteQuery(
    _In_opt_ PHAL_ACPI_PCI_ROUTE_QUERY Provider)
{
    UNREFERENCED_PARAMETER(Provider);
}

VOID
NTAPI
HalpSetPciRoutingMap(
    _In_reads_opt_(EntryCount) const HAL_ACPI_PCI_ROUTE_ENTRY *Entries,
    _In_ ULONG EntryCount)
{
    UNREFERENCED_PARAMETER(Entries);
    UNREFERENCED_PARAMETER(EntryCount);
}

VOID
NTAPI
HalpRecordPciMaxGsi(
    _In_ const HAL_ACPI_PCI_ROUTE_ENTRY *Entry)
{
    UNREFERENCED_PARAMETER(Entry);
}
