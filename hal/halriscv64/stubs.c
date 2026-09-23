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
#include <reactos/hal/acpi_pci.h>
#include <reactos/hal/msi.h>
#include "halp.h"

/* Private kernel/HAL imports, not a stable third-party driver interface. */
DECLSPEC_NORETURN VOID NTAPI KiRiscvUnimplemented(const CHAR *Routine);
VOID NTAPI KiRiscvRequestSoftwareInterrupt(KIRQL Irql);
VOID NTAPI KiRiscvClearSoftwareInterrupt(KIRQL Irql);
VOID NTAPI KiRiscvSendSoftwareInterrupt(KAFFINITY TargetSet, KIRQL Irql);
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

/* Kernel-private entry points of the same routing services.
 * TODO: route messages through the AIA IMSIC once the HAL supports it. */
NTSTATUS
NTAPI
HalpGetInterruptTargetInformation(PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    return HalGetInterruptTargetInformation(TargetInformation);
}

NTSTATUS
NTAPI
HalpGetMessageRoutingInfo(PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    return HalGetMessageRoutingInfo(RoutingInfo);
}

/* The kernel dispatcher claims and completes each PLIC source around its
 * service routines. These keep only the IRQL part of the HAL contract. */
BOOLEAN
NTAPI
HalBeginSystemInterrupt(KIRQL Irql, ULONG Vector, PKIRQL OldIrql)
{
    UNREFERENCED_PARAMETER(Vector);

    /* A source masked at the current level is not taken. */
    if (Irql <= KeGetCurrentIrql())
        return FALSE;

    *OldIrql = KfRaiseIrql(Irql);
    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(KIRQL OldIrql, PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    KfLowerIrql(OldIrql);
}

VOID
NTAPI
HalSendSoftwareInterrupt(KAFFINITY TargetSet, KIRQL Irql)
{
    KiRiscvSendSoftwareInterrupt(TargetSet, Irql);
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
    *Affinity = 1; /* Route PLIC device interrupts to logical CPU 0. */
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
    static WCHAR Name[] = L"RISC-V SBI HAL";
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

/* Boot video owns the display; no firmware display state is reset. */
VOID
NTAPI
HalAcquireDisplayOwnership(
    _In_ PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters)
{
    UNREFERENCED_PARAMETER(ResetDisplayParameters);
}

BOOLEAN
NTAPI
HalQueryDisplayParameters(
    _Out_opt_ PULONG Width,
    _Out_opt_ PULONG Height,
    _Out_opt_ PULONG Depth,
    _Out_opt_ PULONG Frequency)
{
    if (Width) *Width = 0;
    if (Height) *Height = 0;
    if (Depth) *Depth = 0;
    if (Frequency) *Frequency = 0;
    return FALSE;
}

VOID
NTAPI
HalSetDisplayParameters(
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
}

/* The time CSR is one platform counter, consistent on every hart and not
 * writable from S-mode: take part in the rendezvous without adjusting it. */
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

/* Interrupts dispatch through the kernel's KINTERRUPT chains. */
UCHAR
FASTCALL
HalSystemVectorDispatchEntry(
    _In_ ULONG Vector,
    _Out_ PKINTERRUPT_ROUTINE **FlatDispatch,
    _Out_ PKINTERRUPT_ROUTINE *NoConnection)
{
    UNREFERENCED_PARAMETER(Vector);
    if (FlatDispatch) *FlatDispatch = NULL;
    if (NoConnection) *NoConnection = NULL;
    return 0;
}

NTSTATUS
NTAPI
HalEnableInterrupt(
    _In_ PINTERRUPT_CONNECTION_DATA ConnectionData)
{
    UNREFERENCED_PARAMETER(ConnectionData);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalDisableInterrupt(
    _In_ PINTERRUPT_CONNECTION_DATA ConnectionData)
{
    UNREFERENCED_PARAMETER(ConnectionData);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalGetVectorInput(
    _In_ ULONG Vector,
    _In_ PGROUP_AFFINITY Affinity,
    _Out_ PULONG Input,
    _Out_ PKINTERRUPT_POLARITY Polarity,
    _Out_ PINTERRUPT_REMAPPING_INFO IntRemapInfo)
{
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(Affinity);
    if (Input) *Input = 0;
    if (Polarity) *Polarity = InterruptPolarityUnknown;
    if (IntRemapInfo) RtlZeroMemory(IntRemapInfo, sizeof(*IntRemapInfo));
    return STATUS_NOT_SUPPORTED;
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
HalGetMemoryCachingRequirements(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ SIZE_T Length,
    _Out_ MEMORY_CACHING_TYPE *CacheType)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Length);
    if (!CacheType)
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
    ULONG_PTR HartId;

    if (!ProcessorId || !HalpRiscvQueryProcessorHartId(ProcessorNumber, &HartId) ||
        (HartId > MAXULONG))
        return STATUS_INVALID_PARAMETER;
    *ProcessorId = (ULONG)HartId;
    return STATUS_SUCCESS;
}

/* No WHEA error source, PMU counter set or UEFI variable service. */
VOID
NTAPI
HalBugCheckSystem(
    _In_ PWHEA_ERROR_SOURCE_DESCRIPTOR ErrorSource,
    _In_ PWHEA_ERROR_RECORD ErrorRecord)
{
    UNREFERENCED_PARAMETER(ErrorSource);
    UNREFERENCED_PARAMETER(ErrorRecord);
}

NTSTATUS
NTAPI
HalAllocateHardwareCounters(
    _In_reads_(GroupCount) PGROUP_AFFINITY GroupAffinity,
    _In_ ULONG GroupCount,
    _In_ PPHYSICAL_COUNTER_RESOURCE_LIST ResourceList,
    _Out_ PHANDLE CounterSetHandle)
{
    UNREFERENCED_PARAMETER(GroupAffinity);
    UNREFERENCED_PARAMETER(GroupCount);
    UNREFERENCED_PARAMETER(ResourceList);
    if (CounterSetHandle) *CounterSetHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalFreeHardwareCounters(
    _In_ HANDLE CounterSetHandle)
{
    UNREFERENCED_PARAMETER(CounterSetHandle);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalEnumerateEnvironmentVariablesEx(
    _In_ ULONG InformationClass,
    _Out_writes_bytes_opt_(*BufferLength) PVOID Buffer,
    _Inout_opt_ PULONG BufferLength)
{
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(Buffer);
    if (BufferLength) *BufferLength = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalGetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _Out_writes_bytes_opt_(*ValueLength) PVOID Value,
    _Inout_ PULONG ValueLength,
    _Out_opt_ PULONG Attributes)
{
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);
    if (ValueLength) *ValueLength = 0;
    if (Attributes) *Attributes = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalSetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_ ULONG Attributes)
{
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Attributes);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalQueryEnvironmentVariableInfoEx(
    _In_ ULONG Attributes,
    _Out_opt_ PULONGLONG MaximumVariableStorageSize,
    _Out_opt_ PULONGLONG RemainingVariableStorageSize,
    _Out_opt_ PULONGLONG MaximumVariableSize)
{
    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(MaximumVariableStorageSize);
    UNREFERENCED_PARAMETER(RemainingVariableStorageSize);
    UNREFERENCED_PARAMETER(MaximumVariableSize);
    return STATUS_NOT_SUPPORTED;
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
