/*
 * PROJECT:         ReactOS HAL (ARM64)
 * PURPOSE:         Minimal stub implementation to satisfy kernel linkage
 *                  while the real Windows 11 style ARM64 HAL is brought up.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <reactos/hal/acpi_pci.h>
#include <bugcodes.h>
#include <debug.h>

#ifndef KeGetCurrentProcessorNumber
ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID);
#endif

#ifndef KeRaiseIrqlToDpcLevel
KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID);
#endif

#ifndef KeLowerIrql
VOID
NTAPI
KeLowerIrql(_In_ KIRQL NewIrql);
#endif

#define UNIMPLEMENTED_STUB() ((void)0)

#undef READ_PORT_UCHAR
#undef READ_PORT_USHORT
#undef READ_PORT_ULONG
#undef READ_PORT_BUFFER_UCHAR
#undef READ_PORT_BUFFER_USHORT
#undef READ_PORT_BUFFER_ULONG
#undef WRITE_PORT_UCHAR
#undef WRITE_PORT_USHORT
#undef WRITE_PORT_ULONG
#undef WRITE_PORT_BUFFER_UCHAR
#undef WRITE_PORT_BUFFER_USHORT
#undef WRITE_PORT_BUFFER_ULONG

FORCEINLINE
VOID
HalpReadRegisterBufferUchar(
    _In_ volatile PUCHAR Port,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        *Buffer++ = READ_REGISTER_UCHAR(Port);
    }
}

FORCEINLINE
VOID
HalpReadRegisterBufferUshort(
    _In_ volatile PUSHORT Port,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        *Buffer++ = READ_REGISTER_USHORT(Port);
    }
}

FORCEINLINE
VOID
HalpReadRegisterBufferUlong(
    _In_ volatile PULONG Port,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        *Buffer++ = READ_REGISTER_ULONG(Port);
    }
}

FORCEINLINE
VOID
HalpWriteRegisterBufferUchar(
    _In_ volatile PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        WRITE_REGISTER_UCHAR(Port, *Buffer++);
    }
}

FORCEINLINE
VOID
HalpWriteRegisterBufferUshort(
    _In_ volatile PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        WRITE_REGISTER_USHORT(Port, *Buffer++);
    }
}

FORCEINLINE
VOID
HalpWriteRegisterBufferUlong(
    _In_ volatile PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    while (Count--)
    {
        WRITE_REGISTER_ULONG(Port, *Buffer++);
    }
}

VOID
NTAPI
READ_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    HalpReadRegisterBufferUchar(Port, Buffer, Count);
}

VOID
NTAPI
READ_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    HalpReadRegisterBufferUshort(Port, Buffer, Count);
}

VOID
NTAPI
READ_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    HalpReadRegisterBufferUlong(Port, Buffer, Count);
}

UCHAR
NTAPI
READ_PORT_UCHAR(
    _In_ PUCHAR Port)
{
    return READ_REGISTER_UCHAR(Port);
}

USHORT
NTAPI
READ_PORT_USHORT(
    _In_ PUSHORT Port)
{
    return READ_REGISTER_USHORT(Port);
}

ULONG
NTAPI
READ_PORT_ULONG(
    _In_ PULONG Port)
{
    return READ_REGISTER_ULONG(Port);
}

VOID
NTAPI
WRITE_PORT_BUFFER_UCHAR(
    _In_ PUCHAR Port,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    HalpWriteRegisterBufferUchar(Port, Buffer, Count);
}

VOID
NTAPI
WRITE_PORT_BUFFER_USHORT(
    _In_ PUSHORT Port,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    HalpWriteRegisterBufferUshort(Port, Buffer, Count);
}

VOID
NTAPI
WRITE_PORT_BUFFER_ULONG(
    _In_ PULONG Port,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    HalpWriteRegisterBufferUlong(Port, Buffer, Count);
}

VOID
NTAPI
WRITE_PORT_UCHAR(
    _In_ PUCHAR Port,
    _In_ UCHAR Value)
{
    WRITE_REGISTER_UCHAR(Port, Value);
}

VOID
NTAPI
WRITE_PORT_USHORT(
    _In_ PUSHORT Port,
    _In_ USHORT Value)
{
    WRITE_REGISTER_USHORT(Port, Value);
}

VOID
NTAPI
WRITE_PORT_ULONG(
    _In_ PULONG Port,
    _In_ ULONG Value)
{
WRITE_REGISTER_ULONG(Port, Value);
}

BOOLEAN
NTAPI
HalIsIoApicPresent(VOID)
{
    return FALSE;
}

BOOLEAN
NTAPI
HalQueryPciBusRange(
    _Out_opt_ PULONG MinBus,
    _Out_opt_ PULONG MaxBus)
{
    if (MinBus)
    {
        *MinBus = 0;
    }

    if (MaxBus)
    {
        *MaxBus = 0;
    }

    return FALSE;
}

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

PUCHAR KdComPortInUse = NULL;

/* Very small GICv2-style bring-up for QEMU virt */
#define HAL_ARM64_GICD_BASE   0x08000000ULL
#define HAL_ARM64_GICC_BASE   0x08010000ULL

#define GICD_CTLR         0x000
#define GICD_TYPER        0x004
#define GICD_IGROUPR      0x080
#define GICD_ISENABLER    0x100
#define GICD_ICENABLER    0x180
#define GICD_ICPENDR      0x280
#define GICD_IPRIORITYR   0x400
#define GICD_ITARGETSR    0x800
#define GICD_SGIR         0xF00

#define GICC_CTLR         0x000
#define GICC_PMR          0x004
#define GICC_BPR          0x008
#define GICC_IAR          0x00C
#define GICC_EOIR         0x010

#define HAL_ARM64_SGI_IPI 0
#define HAL_ARM64_SGI_APC 1
#define HAL_ARM64_SGI_DPC 2

static __inline volatile ULONG *HalpMmio(ULONG_PTR Base, ULONG Offset)
{
    return (volatile ULONG *)(Base + Offset);
}

static ULONG HalpArm64ActiveIntId[MAXIMUM_PROCESSORS];

/* GIC detection: system-register interface (GICv3+) vs legacy CPU IF (GICv2) */
static BOOLEAN HalpGicUseSysRegs = FALSE;
static ULONG HalpGicArchRev = 0; /* 2=v2, 3=v3, 4=v4, etc. */
static BOOLEAN HalpLoggedGicOnce = FALSE; /* One-time post-KD log */

#if defined(_M_ARM64) || defined(__aarch64__)
FORCEINLINE ULONGLONG HalpReadPfr0(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r"(v)); return v;
}

FORCEINLINE ULONGLONG HalpReadCntfrq(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v)); return v;
}

FORCEINLINE ULONGLONG HalpReadCntpct(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(v)); return v;
}

FORCEINLINE unsigned int HalpReadIccSre(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, icc_sre_el1" : "=r"(v)); return (unsigned int)v;
}

FORCEINLINE VOID HalpWriteIccSre(unsigned int v)
{
    __asm__ __volatile__("msr icc_sre_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE unsigned int HalpReadIccIar1(void)
{
    ULONGLONG v; __asm__ __volatile__("mrs %0, icc_iar1_el1" : "=r"(v)); return (unsigned int)(v & 0x3FFu);
}

FORCEINLINE VOID HalpWriteIccEoir1(unsigned int id)
{
    __asm__ __volatile__("msr icc_eoir1_el1, %0; isb" :: "r"((ULONGLONG)id) : "memory");
}

FORCEINLINE VOID HalpWriteIccPmr(unsigned int v)
{
    __asm__ __volatile__("msr icc_pmr_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE VOID HalpWriteIccBpr1(unsigned int v)
{
    __asm__ __volatile__("msr icc_bpr1_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}

FORCEINLINE VOID HalpWriteIccIgrpen1(unsigned int v)
{
    __asm__ __volatile__("msr icc_igrpen1_el1, %0; isb" :: "r"((ULONGLONG)v) : "memory");
}
#endif

static VOID
HalpArm64SendSgi(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG SgiId)
{
    ULONG TargetList;

    if ((TargetSet == 0) || (SgiId > 15))
        return;

    if (HalpGicUseSysRegs)
    {
        return;
    }

    TargetList = (ULONG)(TargetSet & 0xFF);
    if (TargetList == 0)
        return;

    *HalpMmio(HAL_ARM64_GICD_BASE, GICD_SGIR) = (SgiId & 0xF) | (TargetList << 16);
}

BOOLEAN
NTAPI
HalInitSystem(
    _In_ ULONG BootPhase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG i, lines, nregs;
    ULONG typer;
    UNREFERENCED_PARAMETER(LoaderBlock);

    if (BootPhase != 0)
    {
        if (!HalpLoggedGicOnce)
        {
            HalpLoggedGicOnce = TRUE;
#if defined(_M_ARM64) || defined(__aarch64__)
            {
                ULONGLONG pfr0 = HalpReadPfr0();
                ULONG pfr0_gic = (ULONG)((pfr0 >> 24) & 0xF);
                ULONG pidr2 = *HalpMmio(HAL_ARM64_GICD_BASE, 0xFE8);
                DPRINT1("[arm64][HAL] GIC probe: PFR0.GIC=%lu SRE=%lu PIDR2=0x%08lx ARCHREV=%lu\n",
                        pfr0_gic,
                        HalpGicUseSysRegs ? 1UL : 0UL,
                        pidr2,
                        HalpGicArchRev);
            }
#else
            DPRINT1("[arm64][HAL] GIC: post-KD log; ARCHREV=%lu\n", HalpGicArchRev);
#endif
            DPRINT1("[arm64][HAL] Using %s CPU interface\n",
                    HalpGicUseSysRegs ? "GICv3 system-register" : "GICv2 legacy (GICC)");
        }
        return TRUE;
    }

    /* Probe GIC capabilities before touching CPU IF */
    {
        ULONGLONG pfr0 = 0;
        ULONG pfr0_gic = 0;
        ULONG pidr2 = 0;

#if defined(_M_ARM64) || defined(__aarch64__)
        pfr0 = HalpReadPfr0();
        pfr0_gic = (ULONG)((pfr0 >> 24) & 0xF);
        /* Bring-up: force legacy GICC path; avoid ICC_* sysregs at EL1 */
        HalpGicUseSysRegs = FALSE;
#endif

        /* Identify distributor architecture revision */
        pidr2 = *HalpMmio(HAL_ARM64_GICD_BASE, 0xFE8); /* GICD_PIDR2 */
        HalpGicArchRev = ((pidr2 >> 4) & 0xF);
        if (HalpGicArchRev == 0)
            HalpGicArchRev = HalpGicUseSysRegs ? 3 : 2;

        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                   "[arm64][HAL] GIC probe: PFR0.GIC=%lu SRE=%lu PIDR2=0x%08lx ARCHREV=%lu\n",
                   pfr0_gic,
                   HalpGicUseSysRegs ? 1UL : 0UL,
                   pidr2,
                   HalpGicArchRev);
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                   "[arm64][HAL] Using %s CPU interface\n",
                   HalpGicUseSysRegs ? "GICv3 system-register" : "GICv2 legacy (GICC)");
    }

    /* Disable distributor while we (re)configure */
    *HalpMmio(HAL_ARM64_GICD_BASE, GICD_CTLR) = 0;

    /* How many interrupt lines? */
    typer = *HalpMmio(HAL_ARM64_GICD_BASE, GICD_TYPER);
    lines = 32 * ((typer & 0x1F) + 1);
    if (lines > 1020) lines = 1020;
    nregs = (lines + 31) / 32;

    /* Group 0, disable and clear pending, set priority, route to CPU0 */
    for (i = 1; i < nregs; ++i) /* start at 1 to skip SGI/PPI */
    {
        *HalpMmio(HAL_ARM64_GICD_BASE, GICD_ICENABLER + i * 4) = 0xFFFFFFFF;
        *HalpMmio(HAL_ARM64_GICD_BASE, GICD_ICPENDR   + i * 4) = 0xFFFFFFFF;
        *HalpMmio(HAL_ARM64_GICD_BASE, GICD_IGROUPR   + i * 4) = 0x00000000; /* G0 */
    }

    /* Set priorities to medium (0xA0) and route to CPU0 */
    for (i = 32; i < lines; i += 4)
    {
        *HalpMmio(HAL_ARM64_GICD_BASE, GICD_IPRIORITYR + (i & ~3)) = 0xA0A0A0A0;
        *HalpMmio(HAL_ARM64_GICD_BASE, GICD_ITARGETSR + (i & ~3))  = 0x01010101; /* CPU0 */
    }

    /* Enable distributor for Group0+Group1 */
    *HalpMmio(HAL_ARM64_GICD_BASE, GICD_CTLR) = 0x3;

    /* CPU interface: system registers (v3+) or legacy GICC (v2) */
    if (HalpGicUseSysRegs)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        HalpWriteIccPmr(0xFF); /* allow all priorities */
        HalpWriteIccBpr1(0);
        HalpWriteIccIgrpen1(1); /* enable Group1 */
#endif
    }
    else
    {
        *HalpMmio(HAL_ARM64_GICC_BASE, GICC_PMR) = 0xFF; /* allow all priorities */
        *HalpMmio(HAL_ARM64_GICC_BASE, GICC_BPR) = 0x0;
        *HalpMmio(HAL_ARM64_GICC_BASE, GICC_CTLR) = 0x3; /* enable Group0+Group1 */
    }

    return TRUE;
}

VOID
NTAPI
HalReportResourceUsage(VOID)
{
    UNIMPLEMENTED_STUB();
}

VOID
FASTCALL
HalRequestSoftwareInterrupt(
    _In_ KIRQL SoftwareInterruptRequested)
{
    KAFFINITY Target = (KAFFINITY)1 << KeGetCurrentProcessorNumber();

    if (SoftwareInterruptRequested >= DISPATCH_LEVEL)
    {
        HalpArm64SendSgi(Target, HAL_ARM64_SGI_DPC);
    }
    else if (SoftwareInterruptRequested >= APC_LEVEL)
    {
        HalpArm64SendSgi(Target, HAL_ARM64_SGI_APC);
    }
}

VOID
NTAPI
HalAcquireDisplayOwnership(
    _In_ PHAL_RESET_DISPLAY_PARAMETERS ResetDisplayParameters)
{
    UNREFERENCED_PARAMETER(ResetDisplayParameters);
    UNIMPLEMENTED_STUB();
}

NTSTATUS
NTAPI
HalAdjustResourceList(
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST *ResourceList)
{
    UNREFERENCED_PARAMETER(ResourceList);
    UNIMPLEMENTED_STUB();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    return TRUE;
}

NTSTATUS
NTAPI
HalAllocateAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PWAIT_CONTEXT_BLOCK Wcb,
    _In_ ULONG NumberOfMapRegisters,
    _In_ PDRIVER_CONTROL ExecutionRoutine)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Wcb);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    UNREFERENCED_PARAMETER(ExecutionRoutine);
    UNIMPLEMENTED_STUB();
    return STATUS_NOT_IMPLEMENTED;
}

PVOID
NTAPI
HalAllocateCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(CacheEnabled);
    UNIMPLEMENTED_STUB();
    return NULL;
}

PVOID
NTAPI
HalAllocateCrashDumpRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _Inout_ PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    UNIMPLEMENTED_STUB();
    return NULL;
}

NTSTATUS
NTAPI
HalAssignSlotResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PCM_RESOURCE_LIST *AllocatedResources)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(DriverClassName);
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(BusType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(AllocatedResources);
    UNIMPLEMENTED_STUB();
    return STATUS_NOT_IMPLEMENTED;
}

BOOLEAN
NTAPI
HalBeginSystemInterrupt(
    _In_ KIRQL Irql,
    _In_ ULONG Vector,
    _Out_ PKIRQL OldIrql)
{
    ULONG cpu = KeGetCurrentProcessorNumber();
    ULONG intid = Vector;
    /* Consider INTID 1023 spurious on GICv2 */
    if (intid == 1023 || intid == 0)
        return FALSE;

    if (OldIrql) *OldIrql = KfRaiseIrql(Irql);
    if (cpu < MAXIMUM_PROCESSORS) HalpArm64ActiveIntId[cpu] = intid;
    return TRUE;
}

VOID
NTAPI
HalCalibratePerformanceCounter(
    _In_ ULONG Count,
    _In_ ULONG64 Period,
    _Out_ PULONG64 Frequency)
{
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Period);
    if (Frequency)
    {
        *Frequency = HalpReadCntfrq();
    }
}

VOID
FASTCALL
HalClearSoftwareInterrupt(
    _In_ KIRQL Request)
{
    UNREFERENCED_PARAMETER(Request);
}

VOID
NTAPI
HalDisableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql)
{
    ULONG reg = Vector / 32;
    ULONG bit = Vector % 32;
    UNREFERENCED_PARAMETER(Irql);
    *HalpMmio(HAL_ARM64_GICD_BASE, GICD_ICENABLER + reg * 4) = (1u << bit);
}

VOID
NTAPI
HalDisplayString(
    _In_ PCH String)
{
    UNREFERENCED_PARAMETER(String);
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    ULONG reg = Vector / 32;
    ULONG bit = Vector % 32;
    UNREFERENCED_PARAMETER(Irql);
    UNREFERENCED_PARAMETER(InterruptMode);
    *HalpMmio(HAL_ARM64_GICD_BASE, GICD_ISENABLER + reg * 4) = (1u << bit);
    return TRUE;
}

VOID
NTAPI
HalEndSystemInterrupt(
    _In_ KIRQL Irql,
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG cpu = KeGetCurrentProcessorNumber();
    ULONG intid = (cpu < MAXIMUM_PROCESSORS) ? HalpArm64ActiveIntId[cpu] : 0;
    if (intid)
    {
        if (HalpGicUseSysRegs)
        {
#if defined(_M_ARM64) || defined(__aarch64__)
            HalpWriteIccEoir1(intid);
#endif
        }
        else
        {
            *HalpMmio(HAL_ARM64_GICC_BASE, GICC_EOIR) = intid;
        }
        if (cpu < MAXIMUM_PROCESSORS) HalpArm64ActiveIntId[cpu] = 0;
    }
    KeLowerIrql(Irql);
    UNREFERENCED_PARAMETER(TrapFrame);
}

VOID
NTAPI
HalFlushCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID VirtualAddress,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);
    UNIMPLEMENTED_STUB();
}

VOID
NTAPI
HalFreeCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(CacheEnabled);
    UNIMPLEMENTED_STUB();
}

PADAPTER_OBJECT
NTAPI
HalGetAdapter(
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _Out_ PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(DeviceDescription);
    if (NumberOfMapRegisters)
    {
        *NumberOfMapRegisters = 0;
    }
    UNIMPLEMENTED_STUB();
    return NULL;
}

ULONG
NTAPI
HalGetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    UNIMPLEMENTED_STUB();
    return 0;
}

ULONG
NTAPI
HalGetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    UNIMPLEMENTED_STUB();
    return 0;
}

ARC_STATUS
NTAPI
HalGetEnvironmentVariable(
    _In_ PCH Variable,
    _In_ USHORT ValueLength,
    _Out_writes_bytes_(ValueLength) PCH Value)
{
    UNREFERENCED_PARAMETER(Variable);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Value);
    UNIMPLEMENTED_STUB();
    return ESUCCESS;
}

ULONG
NTAPI
HalGetInterruptVector(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ ULONG BusInterruptLevel,
    _In_ ULONG BusInterruptVector,
    _Out_ PKIRQL Irql,
    _Out_ PKAFFINITY Affinity)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(BusInterruptLevel);
    UNREFERENCED_PARAMETER(BusInterruptVector);
    if (Irql)
    {
        *Irql = PASSIVE_LEVEL;
    }
    if (Affinity)
    {
        *Affinity = 1;
    }
    UNIMPLEMENTED_STUB();
    return 0;
}

ULONG
FASTCALL
HalGetInterruptSource(VOID)
{
    if (HalpGicUseSysRegs)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        return HalpReadIccIar1();
#else
        return 0;
#endif
    }
    else
    {
        return *HalpMmio(HAL_ARM64_GICC_BASE, GICC_IAR) & 0x3FFu;
    }
}

VOID
NTAPI
HalInitializeProcessor(
    _In_ ULONG ProcessorNumber,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(ProcessorNumber);
    UNREFERENCED_PARAMETER(LoaderBlock);
}

BOOLEAN
NTAPI
HalMakeBeep(
    _In_ ULONG Frequency)
{
    UNREFERENCED_PARAMETER(Frequency);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

VOID
NTAPI
HalProcessorIdle(VOID)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    __asm__ __volatile__("wfi" ::: "memory");
#else
    UNIMPLEMENTED_STUB();
#endif
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
    UNIMPLEMENTED_STUB();
    return FALSE;
}

BOOLEAN
NTAPI
HalQueryRealTimeClock(
    _Inout_ PTIME_FIELDS TimeFields)
{
    UNREFERENCED_PARAMETER(TimeFields);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

ULONG
NTAPI
HalReadDmaCounter(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNIMPLEMENTED_STUB();
    return 0;
}

VOID
NTAPI
HalRequestIpi(
    _In_ KAFFINITY TargetSet)
{
    HalpArm64SendSgi(TargetSet, HAL_ARM64_SGI_IPI);
}

ARC_STATUS
NTAPI
HalSetEnvironmentVariable(
    _In_ PCH Variable,
    _In_ PCH Value)
{
    UNREFERENCED_PARAMETER(Variable);
    UNREFERENCED_PARAMETER(Value);
    UNIMPLEMENTED_STUB();
    return ESUCCESS;
}

ULONG_PTR
NTAPI
HalSetProfileInterval(
    _In_ ULONG_PTR Interval)
{
    UNREFERENCED_PARAMETER(Interval);
    UNIMPLEMENTED_STUB();
    return Interval;
}

BOOLEAN
NTAPI
HalSetRealTimeClock(
    _In_ PTIME_FIELDS TimeFields)
{
    UNREFERENCED_PARAMETER(TimeFields);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

ULONG
NTAPI
HalSetTimeIncrement(
    _In_ ULONG Increment)
{
    UNREFERENCED_PARAMETER(Increment);
    UNIMPLEMENTED_STUB();
    return Increment;
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    UNREFERENCED_PARAMETER(ProcessorState);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

VOID
NTAPI
HalStartProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
    UNIMPLEMENTED_STUB();
}

VOID
NTAPI
HalStopProfileInterrupt(
    _In_ KPROFILE_SOURCE ProfileSource)
{
    UNREFERENCED_PARAMETER(ProfileSource);
    UNIMPLEMENTED_STUB();
}

VOID
FASTCALL
HalSweepDcache(VOID)
{
    __asm__ __volatile__("dsb sy\n\tisb" ::: "memory");
}

VOID
FASTCALL
HalSweepIcache(VOID)
{
    __asm__ __volatile__("ic iallu\n\tdsb sy\n\tisb" ::: "memory");
}

UCHAR
FASTCALL
HalSystemVectorDispatchEntry(
    _In_ ULONG Vector,
    _Out_ PKINTERRUPT_ROUTINE **FlatDispatch,
    _Out_ PKINTERRUPT_ROUTINE *NoConnection)
{
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(FlatDispatch);
    UNREFERENCED_PARAMETER(NoConnection);
    UNIMPLEMENTED_STUB();
    return 0;
}

BOOLEAN
NTAPI
HalTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(BusAddress);
    if (AddressSpace) *AddressSpace = 0;
    if (TranslatedAddress) TranslatedAddress->QuadPart = 0;
    UNIMPLEMENTED_STUB();
    return FALSE;
}

BOOLEAN
NTAPI
IoFlushAdapterBuffers(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(CurrentVa);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);
    UNIMPLEMENTED_STUB();
    return FALSE;
}

VOID
NTAPI
IoFreeAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNIMPLEMENTED_STUB();
}

VOID
NTAPI
IoFreeMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    UNIMPLEMENTED_STUB();
}

PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(CurrentVa);
    UNREFERENCED_PARAMETER(WriteToDevice);
    if (Length)
    {
        *Length = 0;
    }
    UNIMPLEMENTED_STUB();
    return (PHYSICAL_ADDRESS){0};
}

VOID
NTAPI
HalReturnToFirmware(
    _In_ FIRMWARE_REENTRY Action)
{
    UNREFERENCED_PARAMETER(Action);
    UNIMPLEMENTED_STUB();
}

VOID
NTAPI
HalSetDisplayParameters(
    _In_ ULONG Width,
    _In_ ULONG Height)
{
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNIMPLEMENTED_STUB();
}

ULONG
NTAPI
HalSetBusData(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    UNIMPLEMENTED_STUB();
    return 0;
}

ULONG
NTAPI
HalSetBusDataByOffset(
    _In_ BUS_DATA_TYPE BusDataType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Length);
    UNIMPLEMENTED_STUB();
    return 0;
}

VOID
NTAPI
KeFlushWriteBuffer(VOID)
{
    __asm__ __volatile__("dsb sy" ::: "memory");
}

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    _Out_opt_ PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Counter = {0};
    if (PerformanceFrequency)
    {
        PerformanceFrequency->QuadPart = (LONGLONG)HalpReadCntfrq();
    }
    Counter.QuadPart = (LONGLONG)HalpReadCntpct();
    return Counter;
}

VOID
NTAPI
KeStallExecutionProcessor(
    _In_ ULONG MicroSeconds)
{
    ULONGLONG Frequency = HalpReadCntfrq();
    ULONGLONG Start = HalpReadCntpct();
    ULONGLONG Ticks;

    if (Frequency == 0)
        return;

    Ticks = (Frequency / 1000000ULL) * (ULONGLONG)MicroSeconds;
    while ((HalpReadCntpct() - Start) < Ticks)
    {
        __asm__ __volatile__("isb" ::: "memory");
    }
}
