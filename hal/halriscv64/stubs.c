/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Explicit unavailable RISC-V platform services
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/halfuncs.h>
#include <ndk/iofuncs.h>
#include "halp.h"

/* Private kernel/HAL imports, not a stable third-party driver interface. */
DECLSPEC_NORETURN VOID NTAPI KiRiscvUnimplemented(const CHAR *Routine);
VOID NTAPI KiRiscvRequestSoftwareInterrupt(KIRQL Irql);
VOID NTAPI KiRiscvClearSoftwareInterrupt(KIRQL Irql);
BOOLEAN NTAPI InbvDisplayString(PCSTR String);

/* NT layering (ABI-127): boot video owns text output. The debugger transport
 * remains independent when firmware supplies no usable framebuffer. */
VOID
NTAPI
HalDisplayString(
    _In_ PCSTR String)
{
    InbvDisplayString(String);
}

/* Never infer an MMIO pointer from a port number or invent a counter rate. */
#define RISCV_HAL_REQUIRED(ReturnType, Name, Parameters) \
    ReturnType NTAPI Name Parameters { KiRiscvUnimplemented(#Name); }


/* Same contract as the x86 sti;hlt: entered with interrupts disabled, wakes
 * on the next unmasked interrupt and returns with interrupts enabled. The
 * kernel idle loop on this port issues its own wfi and does not call this. */
VOID
NTAPI
HalProcessorIdle(VOID)
{
    __asm__ __volatile__("csrsi sstatus, 2\n\twfi" ::: "memory");
}
RISCV_HAL_REQUIRED(UCHAR, READ_PORT_UCHAR, (PUCHAR Port))
RISCV_HAL_REQUIRED(USHORT, READ_PORT_USHORT, (PUSHORT Port))
RISCV_HAL_REQUIRED(ULONG, READ_PORT_ULONG, (PULONG Port))
RISCV_HAL_REQUIRED(VOID, READ_PORT_BUFFER_UCHAR, (PUCHAR Port, PUCHAR Buffer, ULONG Count))
RISCV_HAL_REQUIRED(VOID, READ_PORT_BUFFER_USHORT, (PUSHORT Port, PUSHORT Buffer, ULONG Count))
RISCV_HAL_REQUIRED(VOID, READ_PORT_BUFFER_ULONG, (PULONG Port, PULONG Buffer, ULONG Count))
RISCV_HAL_REQUIRED(VOID, WRITE_PORT_UCHAR, (PUCHAR Port, UCHAR Value))
RISCV_HAL_REQUIRED(VOID, WRITE_PORT_USHORT, (PUSHORT Port, USHORT Value))
RISCV_HAL_REQUIRED(VOID, WRITE_PORT_ULONG, (PULONG Port, ULONG Value))
RISCV_HAL_REQUIRED(VOID, WRITE_PORT_BUFFER_UCHAR, (PUCHAR Port, PUCHAR Buffer, ULONG Count))
RISCV_HAL_REQUIRED(VOID, WRITE_PORT_BUFFER_USHORT, (PUSHORT Port, PUSHORT Buffer, ULONG Count))
RISCV_HAL_REQUIRED(VOID, WRITE_PORT_BUFFER_ULONG, (PULONG Port, PULONG Buffer, ULONG Count))

/* Single-hart bring-up: the boot hart is the only processor (SMP is stubbed). */
BOOLEAN NTAPI HalAllProcessorsStarted(VOID) { return TRUE; }
/* QEMU virt exposes no PC speaker. Let beep.sys load and report unsupported
 * tone generation through the normal HAL failure result. */
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

/* No RISC-V MSI target exists yet. PCI falls back to legacy INTx. */
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
    ULONG FirstBus, LastBus;

    if (!Irql || !Affinity || BusInterruptLevel != BusInterruptVector ||
        !HalpRiscvPlicValidSource(BusInterruptLevel))
        return 0;
    if (InterfaceType == PCIBus)
    {
        if (!HalpRiscvGetPciBusRange(&FirstBus, &LastBus) ||
            BusNumber < FirstBus || BusNumber > LastBus)
            return 0;
    }
    else if (InterfaceType != Internal)
    {
        return 0;
    }
    *Irql = RISCV_HAL_EXTERNAL_IRQL;
    *Affinity = 1; /* This HAL currently owns only the boot hart. */
    return BusInterruptLevel;
}

VOID
FASTCALL
HalRequestSoftwareInterrupt(KIRQL Irql)
{
    KiRiscvRequestSoftwareInterrupt(Irql);
}

VOID
FASTCALL
HalClearSoftwareInterrupt(KIRQL Irql)
{
    KiRiscvClearSoftwareInterrupt(Irql);
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    /* Order memory and I/O accesses. This does not perform device readback
     * or imply that a posted write has completed at a particular device. */
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

/* Report the HAL-owned ECAM mapping, when present. PCI device BAR windows
 * are not HAL allocations; the timer is a CSR/SBI service. */
VOID
NTAPI
HalReportResourceUsage(VOID)
{
    static WCHAR Name[] = L"RISC-V SBI Uniprocessor HAL";
    UNICODE_STRING HalName;
    CM_RESOURCE_LIST List = {0};
    ULONG ListSize = FIELD_OFFSET(CM_RESOURCE_LIST, List);

    HalName.Buffer = Name;
    HalName.Length = sizeof(Name) - sizeof(WCHAR);
    HalName.MaximumLength = sizeof(Name);
    List.Count = 0;
    if (HalpRiscvGetPciResource(&List.List[0].PartialResourceList.PartialDescriptors[0]))
    {
        List.Count = 1;
        List.List[0].InterfaceType = Internal;
        List.List[0].PartialResourceList.Version = 1;
        List.List[0].PartialResourceList.Revision = 1;
        List.List[0].PartialResourceList.Count = 1;
        ListSize = sizeof(List);
    }
    IoReportHalResourceUsage(&HalName, &List, &List, ListSize);
}

VOID
NTAPI
HalReturnToFirmware(
    _In_ FIRMWARE_REENTRY Action)
{
    switch (Action)
    {
        case HalPowerDownRoutine:
            HalpRiscvSystemReset(RISCV_SBI_SRST_TYPE_SHUTDOWN);
            break;
        case HalRestartRoutine:
        case HalRebootRoutine:
            HalpRiscvSystemReset(RISCV_SBI_SRST_TYPE_COLD_REBOOT);
            break;
        default:
            break;
    }

    /* Firmware reset unavailable or not requested: halt this hart. */
    _disable();
    for (;;)
        __asm__ __volatile__("wfi");
}
