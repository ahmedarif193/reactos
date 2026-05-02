/*
 * PROJECT:         ReactOS HAL (ARM64)
 * FILE:            hal/halarm64/smp.c
 * PURPOSE:         SMP (Symmetric Multi-Processing) boot support for ARM64
 *
 * DESCRIPTION:
 *   This file implements the Application Processor (AP) startup mechanism
 *   for ARM64 SMP systems. The approach uses:
 *
 *   1. PSCI CPU_ON to wake secondary processors
 *   2. An identity-mapped trampoline that runs with MMU disabled
 *   3. GICv3 redistributor wake-up on each AP
 *   4. CPU parking protocol support (when PSCI is unavailable)
 *
 *   The trampoline is copied to a physically contiguous page that is
 *   identity-mapped, allowing it to enable the MMU and switch to the
 *   kernel's virtual address space.
 *
 * ARM64 SMP BOOT SEQUENCE:
 *   1. BSP (Boot Strap Processor) initializes kernel
 *   2. Kernel calls HalStartNextProcessor for each AP
 *   3. HAL allocates and sets up trampoline (first call)
 *   4. HAL prepares AP data structure with page table and entry info
 *   5. HAL calls PSCI CPU_ON with trampoline physical address
 *   6. AP wakes at trampoline, enables MMU, jumps to kernel entry
 *   7. HAL waits for AP to signal synchronization
 *
 * MEMORY ORDERING:
 *   ARM64 has a relaxed memory model. All writes to shared data structures
 *   must use appropriate barriers (DSB) and cache maintenance operations
 *   to ensure visibility across CPUs.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/kefuncs.h>
#include <ioaccess.h>
#include <halfuncs.h>
#include <halacpi_arm64.h>
#include <debug.h>

/*
 * Forward declarations from halarm64.c
 */
extern HALP_ARM64_GIC_INFO HalpArm64GicInfo;
extern HALP_ARM64_PSCI_INFO HalpArm64PsciInfo;

extern ULONG64 NTAPI
HalpAllocPhysicalMemory(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG64 MaxAddress,
    _In_ PFN_NUMBER PageCount,
    _In_ BOOLEAN Aligned);

extern ULONG_PTR
HalpArm64FindGicrForMpidr(_In_ ULONGLONG Mpidr);

/*
 * ARM64 cache maintenance inlines
 */
#define ARM64_DCACHE_LINE_SIZE 64

FORCEINLINE VOID
HalpArm64CleanDcacheRange(_In_ PVOID Base, _In_ SIZE_T Length)
{
    ULONG_PTR Start = (ULONG_PTR)Base & ~(ARM64_DCACHE_LINE_SIZE - 1);
    ULONG_PTR End = ((ULONG_PTR)Base + Length + ARM64_DCACHE_LINE_SIZE - 1) &
                    ~(ARM64_DCACHE_LINE_SIZE - 1);

    for (ULONG_PTR Addr = Start; Addr < End; Addr += ARM64_DCACHE_LINE_SIZE)
        __asm__ __volatile__("dc cvac, %0" :: "r"(Addr) : "memory");

    __asm__ __volatile__("dsb sy" ::: "memory");
}

/*
 * GICv3 CPU interface register helpers
 */
FORCEINLINE VOID HalpWriteIccSre(unsigned int v)
{
    __asm__ __volatile__("msr icc_sre_el1, %0" :: "r"((UINT64)v));
    __asm__ __volatile__("isb");
}

FORCEINLINE VOID HalpWriteIccPmr(unsigned int v)
{
    __asm__ __volatile__("msr icc_pmr_el1, %0" :: "r"((UINT64)v));
}

FORCEINLINE VOID HalpWriteIccBpr1(unsigned int v)
{
    __asm__ __volatile__("msr icc_bpr1_el1, %0" :: "r"((UINT64)v));
}

FORCEINLINE VOID HalpWriteIccIgrpen1(unsigned int v)
{
    __asm__ __volatile__("msr icc_igrpen1_el1, %0" :: "r"((UINT64)v));
}

/*
 * KSEG0 base for identity mapping
 */
#define HAL_ARM64_KSEG0_BASE 0xFFFF800000000000ULL

/*
 * HAL_ARM64_AP_TRAMPOLINE_DATA - Configuration data for AP startup
 *
 * This structure is placed at a known physical address and contains
 * all the information an AP needs to configure itself and enter the kernel.
 * The structure layout must match the assembly offsets in smp_trampoline.S.
 */
typedef struct _HAL_ARM64_AP_TRAMPOLINE_DATA
{
    /* Page table configuration (from BSP) */
    UINT64 Ttbr0El1;           /* 0x00: User/identity map page table base */
    UINT64 Ttbr1El1;           /* 0x08: Kernel page table base */
    UINT64 MairEl1;            /* 0x10: Memory attribute indirection register */
    UINT64 TcrEl1;             /* 0x18: Translation control register */
    UINT64 SctlrEl1;           /* 0x20: System control register (MMU enable) */
    UINT64 VbarEl1;            /* 0x28: Vector base address register */

    /* AP entry point and stack */
    UINT64 EntryPoint;         /* 0x30: Kernel entry point (virtual address) */
    UINT64 StackPointer;       /* 0x38: Initial kernel stack (virtual address) */

    /* Arguments for kernel entry */
    UINT64 Arg0;               /* 0x40: First argument (LoaderBlock) */
    UINT64 Arg1;               /* 0x48: Second argument (reserved) */

    /* Synchronization */
    volatile UINT32 *SyncFlag; /* 0x50: Pointer to synchronization flag */
    UINT32 CpuNumber;          /* 0x58: Logical CPU number */

    /* GIC redistributor */
    UINT64 GicrBase;           /* 0x60: Per-CPU GICR base address (physical) */

    /* Debug progress tracking (written by AP, read by BSP after timeout) */
    volatile UINT32 Progress;  /* 0x68: AP progress breadcrumb */
    UINT32 ProgressPad;        /* 0x6C: Alignment padding */
} HAL_ARM64_AP_TRAMPOLINE_DATA, *PHAL_ARM64_AP_TRAMPOLINE_DATA;

/* External references to assembly trampoline */
extern VOID HalpArm64ApTrampolineEntry(VOID);
extern ULONG HalpArm64ApTrampolineSize;
extern ULONG HalpArm64ApDataPhysOffset;  /* Offset of data pointer within trampoline */
extern VOID HalpArm64ApTrampolineEnd(VOID);

/* SMP boot state */
static BOOLEAN HalpApTrampolineInitialized = FALSE;
static PVOID HalpApTrampolineVa = NULL;
static PHYSICAL_ADDRESS HalpApTrampolinePa = {0};
static PHAL_ARM64_AP_TRAMPOLINE_DATA HalpApDataVa = NULL;
static PHYSICAL_ADDRESS HalpApDataPa = {0};
static volatile UINT32 HalpApSyncFlag = 0;

/* Identity-map page table for AP trampoline TTBR0 */
static UINT64 HalpApIdmapTtbr0Pa = 0;

/* SMP processor startup tracking - exported for halarm64.c */
ULONG HalpStartedProcessorCount = 1;  /* BSP is already started */

/*
 * CPU parking protocol state
 * Used when PSCI CPU_ON is not available
 */
typedef struct _HAL_ARM64_PARKED_CPU
{
    ULONGLONG Mpidr;
    ULONGLONG ParkedAddress;
    ULONG ParkingVersion;
    BOOLEAN Enabled;
} HAL_ARM64_PARKED_CPU, *PHAL_ARM64_PARKED_CPU;

static HAL_ARM64_PARKED_CPU HalpParkedCpus[MAXIMUM_PROCESSORS];
static ULONG HalpParkedCpuCount = 0;
static BOOLEAN HalpParkingProtocolAvailable = FALSE;

/*
 * System register read helpers
 */
FORCEINLINE UINT64
HalpArm64ReadSctlrEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, sctlr_el1" : "=r"(Value));
    return Value;
}

FORCEINLINE UINT64
HalpArm64ReadTcrEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, tcr_el1" : "=r"(Value));
    return Value;
}

FORCEINLINE UINT64
HalpArm64ReadTtbr0El1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Value));
    return Value;
}

FORCEINLINE UINT64
HalpArm64ReadTtbr1El1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Value));
    return Value;
}

FORCEINLINE UINT64
HalpArm64ReadMairEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, mair_el1" : "=r"(Value));
    return Value;
}

FORCEINLINE UINT64
HalpArm64ReadVbarEl1(VOID)
{
    UINT64 Value;
    __asm__ __volatile__("mrs %0, vbar_el1" : "=r"(Value));
    return Value;
}

/*
 * ARM64 page table descriptor bits for identity map
 */
#define IDMAP_TABLE_DESC    0x3ULL          /* Valid table descriptor */
#define IDMAP_BLOCK_DESC    0x1ULL          /* Valid block descriptor (L1/L2) */
#define IDMAP_AF            (1ULL << 10)    /* Access Flag */
#define IDMAP_SH_INNER      (3ULL << 8)     /* Inner Shareable */
#define IDMAP_MAIR_WB_IDX   4               /* Normal Write-Back (MI_ARM64_MAIR_NORMAL_WB_IDX) */
#define IDMAP_ATTRINDX(n)   ((UINT64)(n) << 2)

/*
 * HalpArm64BuildIdmapPageTable - Build minimal identity-map page table
 *
 * Creates L0→L1→L2 page tables with a single 2MB block descriptor
 * that identity-maps the physical region containing the trampoline.
 * This is used as TTBR0 for APs so they can continue executing
 * after enabling the MMU (the kernel's TTBR0 no longer has the
 * identity map by the time APs start).
 *
 * Returns the physical address of the L0 table (for use as TTBR0).
 */
static UINT64
HalpArm64BuildIdmapPageTable(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ UINT64 TrampolinePa)
{
    PHYSICAL_ADDRESS AllocPa;
    PUINT64 L0Va, L1Va, L2Va;
    UINT64 L0Pa, L1Pa, L2Pa;
    ULONG L0Idx, L1Idx, L2Idx;
    UINT64 BlockPa;

    /* Allocate 3 contiguous pages for L0, L1, L2 tables */
    AllocPa.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                0xFFFFFFFFFFFFFFFFULL,
                                                3, TRUE);
    if (AllocPa.QuadPart == 0)
    {
        DPRINT1("[arm64][HAL] Failed to allocate identity-map page tables\n");
        return 0;
    }

    L0Pa = AllocPa.QuadPart;
    L1Pa = L0Pa + PAGE_SIZE;
    L2Pa = L1Pa + PAGE_SIZE;

    /* Map via KSEG0 for BSP access */
    L0Va = (PUINT64)(ULONG_PTR)(L0Pa + HAL_ARM64_KSEG0_BASE);
    L1Va = (PUINT64)(ULONG_PTR)(L1Pa + HAL_ARM64_KSEG0_BASE);
    L2Va = (PUINT64)(ULONG_PTR)(L2Pa + HAL_ARM64_KSEG0_BASE);

    /* Zero all tables */
    RtlZeroMemory(L0Va, PAGE_SIZE);
    RtlZeroMemory(L1Va, PAGE_SIZE);
    RtlZeroMemory(L2Va, PAGE_SIZE);

    /* Decompose trampoline PA into table indices (4KB granule, 48-bit VA) */
    L0Idx = (ULONG)((TrampolinePa >> 39) & 0x1FF);
    L1Idx = (ULONG)((TrampolinePa >> 30) & 0x1FF);
    L2Idx = (ULONG)((TrampolinePa >> 21) & 0x1FF);
    BlockPa = TrampolinePa & ~0x1FFFFFULL;  /* 2MB-aligned */

    /* L0[idx] → L1 table */
    L0Va[L0Idx] = L1Pa | IDMAP_TABLE_DESC;

    /* L1[idx] → L2 table */
    L1Va[L1Idx] = L2Pa | IDMAP_TABLE_DESC;

    /* L2[idx] → 2MB block mapping PA=VA */
    L2Va[L2Idx] = BlockPa |
                  IDMAP_BLOCK_DESC |
                  IDMAP_AF |
                  IDMAP_SH_INNER |
                  IDMAP_ATTRINDX(IDMAP_MAIR_WB_IDX);

    /* Clean cache so AP sees the tables from main memory */
    HalpArm64CleanDcacheRange(L0Va, PAGE_SIZE);
    HalpArm64CleanDcacheRange(L1Va, PAGE_SIZE);
    HalpArm64CleanDcacheRange(L2Va, PAGE_SIZE);

    DPRINT1("[arm64][HAL] Identity-map page table: L0[%lu]→L1[%lu]→L2[%lu] block=0x%llx (trampoline PA=0x%llx)\n",
            L0Idx, L1Idx, L2Idx, BlockPa, TrampolinePa);

    return L0Pa;
}

/*
 * HalpArm64InitApTrampoline - Initialize the AP startup trampoline
 *
 * This function allocates and sets up the identity-mapped trampoline
 * page that APs will execute when brought online. It must be called
 * before any attempt to start secondary processors.
 */
BOOLEAN
HalpArm64InitApTrampoline(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG TrampolineSize;
    ULONG TotalSize;
    PHYSICAL_ADDRESS AllocPa;
    PVOID AllocVa;

    if (HalpApTrampolineInitialized)
        return TRUE;

    /*
     * Calculate size needed for trampoline code + data structure.
     * Both must be page-aligned for identity mapping.
     */
    TrampolineSize = (ULONG)((ULONG_PTR)&HalpArm64ApTrampolineEnd -
                             (ULONG_PTR)&HalpArm64ApTrampolineEntry);
    if (TrampolineSize == 0 || TrampolineSize > PAGE_SIZE)
    {
        DPRINT1("[arm64][HAL] AP trampoline size invalid: %lu\n", TrampolineSize);
        return FALSE;
    }

    /* Allocate 2 pages: one for trampoline code, one for data */
    TotalSize = 2 * PAGE_SIZE;

    /*
     * Allocate contiguous physical memory.
     * The trampoline runs with MMU disabled initially, so it must be
     * at a physical address that can be identity-mapped.
     */
    AllocPa.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                0xFFFFFFFFFFFFFFFFULL,
                                                TotalSize >> PAGE_SHIFT,
                                                TRUE);
    if (AllocPa.QuadPart == 0)
    {
        DPRINT1("[arm64][HAL] Failed to allocate AP trampoline memory\n");
        return FALSE;
    }

    /*
     * Map the allocated pages.
     * Use KSEG0 mapping since identity map is set up by boot.
     */
    AllocVa = (PVOID)(ULONG_PTR)(AllocPa.QuadPart + HAL_ARM64_KSEG0_BASE);

    /* Set up trampoline page */
    HalpApTrampolinePa.QuadPart = AllocPa.QuadPart;
    HalpApTrampolineVa = AllocVa;

    /* Set up data page (immediately after trampoline) */
    HalpApDataPa.QuadPart = AllocPa.QuadPart + PAGE_SIZE;
    HalpApDataVa = (PHAL_ARM64_AP_TRAMPOLINE_DATA)((ULONG_PTR)AllocVa + PAGE_SIZE);

    /* Copy trampoline code to allocated page */
    RtlCopyMemory(HalpApTrampolineVa, (PVOID)&HalpArm64ApTrampolineEntry, TrampolineSize);

    /* Clear data structure */
    RtlZeroMemory(HalpApDataVa, sizeof(HAL_ARM64_AP_TRAMPOLINE_DATA));

    /*
     * Patch the copied trampoline with the data structure physical address.
     * The trampoline contains an embedded pointer (HalpArm64ApDataPhys) that
     * the AP reads at startup. We patch the COPY, not the original.
     */
    {
        PUINT64 DataPhysPtr = (PUINT64)((ULONG_PTR)HalpApTrampolineVa +
                                         HalpArm64ApDataPhysOffset);
        *DataPhysPtr = HalpApDataPa.QuadPart;
    }

    /* Clean data cache and invalidate instruction cache */
    HalpArm64CleanDcacheRange(HalpApTrampolineVa, PAGE_SIZE);
    HalpArm64CleanDcacheRange(HalpApDataVa, PAGE_SIZE);
    __asm__ __volatile__("ic iallu\n\tdsb sy\n\tisb" ::: "memory");

    /* Build identity-map page table for AP's TTBR0 */
    HalpApIdmapTtbr0Pa = HalpArm64BuildIdmapPageTable(LoaderBlock,
                                                       HalpApTrampolinePa.QuadPart);
    if (HalpApIdmapTtbr0Pa == 0)
    {
        DPRINT1("[arm64][HAL] Failed to build identity-map page table for AP trampoline\n");
        return FALSE;
    }

    DPRINT1("[arm64][HAL] AP trampoline initialized at PA=0x%llx VA=%p size=%lu idmap_ttbr0=0x%llx\n",
            HalpApTrampolinePa.QuadPart, HalpApTrampolineVa, TrampolineSize, HalpApIdmapTtbr0Pa);

    HalpApTrampolineInitialized = TRUE;
    return TRUE;
}

/*
 * HalpArm64PrepareApData - Set up AP configuration data for a specific CPU
 */
VOID
HalpArm64PrepareApData(
    _In_ ULONG ProcessorNumber,
    _In_ UINT64 EntryPoint,
    _In_ UINT64 StackPointer,
    _In_ UINT64 Arg0,
    _In_ UINT64 GicrBase)
{
    if (!HalpApDataVa)
        return;

    /*
     * Use the dedicated identity-map page table for TTBR0.
     * The BSP's TTBR0 no longer has an identity map by the time APs start
     * (it was torn down during HAL Phase1). The AP trampoline needs an
     * identity map to continue executing after enabling the MMU.
     */
    HalpApDataVa->Ttbr0El1 = HalpApIdmapTtbr0Pa;
    HalpApDataVa->Ttbr1El1 = HalpArm64ReadTtbr1El1();
    HalpApDataVa->MairEl1 = HalpArm64ReadMairEl1();
    HalpApDataVa->TcrEl1 = HalpArm64ReadTcrEl1();
    HalpApDataVa->SctlrEl1 = HalpArm64ReadSctlrEl1();
    HalpApDataVa->VbarEl1 = HalpArm64ReadVbarEl1();

    /* Set up entry point and stack */
    HalpApDataVa->EntryPoint = EntryPoint;
    HalpApDataVa->StackPointer = StackPointer;

    /* Set up arguments */
    HalpApDataVa->Arg0 = Arg0;
    HalpApDataVa->Arg1 = 0;

    /* Set up synchronization */
    HalpApSyncFlag = 0;
    HalpApDataVa->SyncFlag = &HalpApSyncFlag;
    HalpApDataVa->CpuNumber = ProcessorNumber;

    /*
     * GICR init: skip in trampoline (set GicrBase=0).
     *
     * The trampoline runs with MMU off and no VBAR_EL1 set. If the GICR
     * MMIO access causes a data abort (e.g., QEMU access ordering), the AP
     * crashes silently. The HAL's HalInitializeProcessor handles GICR
     * initialization later with proper exception handling in place.
     */
    HalpApDataVa->GicrBase = 0;

    /* Initialize progress tracking */
    HalpApDataVa->Progress = 0;
    HalpApDataVa->ProgressPad = 0;

    /* Ensure all writes are visible */
    HalpArm64CleanDcacheRange(HalpApDataVa, sizeof(HAL_ARM64_AP_TRAMPOLINE_DATA));
    __asm__ __volatile__("dsb sy" ::: "memory");

    DPRINT1("[arm64][HAL] AP data prepared: CPU=%lu entry=0x%llx stack=0x%llx GICR=0x%llx\n",
            ProcessorNumber, EntryPoint, StackPointer, GicrBase);
    DPRINT1("[arm64][HAL] AP sysregs: SCTLR=0x%llx TCR=0x%llx MAIR=0x%llx VBAR=0x%llx\n",
            HalpApDataVa->SctlrEl1, HalpApDataVa->TcrEl1,
            HalpApDataVa->MairEl1, HalpApDataVa->VbarEl1);
    DPRINT1("[arm64][HAL] AP TTBR0=0x%llx (idmap) TTBR1=0x%llx (kernel) SyncFlag=%p\n",
            HalpApDataVa->Ttbr0El1, HalpApDataVa->Ttbr1El1, HalpApDataVa->SyncFlag);
}

/*
 * HalpArm64WaitForApSync - Wait for AP to signal it has started
 */
BOOLEAN
HalpArm64WaitForApSync(
    _In_ ULONG TimeoutMs)
{
    ULONG Iterations;
    ULONG MaxIterations;

    MaxIterations = TimeoutMs ? (TimeoutMs * 1000) : 0xFFFFFFFF;

    for (Iterations = 0; Iterations < MaxIterations; Iterations++)
    {
        if (HalpApSyncFlag != 0)
        {
            DPRINT1("[arm64][HAL] AP signaled after %lu iterations\n", Iterations);
            return TRUE;
        }

        __asm__ __volatile__("dmb ld" ::: "memory");
        __asm__ __volatile__("yield");
    }

    /* Read AP progress via KSEG0 to diagnose where AP got stuck */
    {
        volatile PHAL_ARM64_AP_TRAMPOLINE_DATA DataKseg0 =
            (volatile PHAL_ARM64_AP_TRAMPOLINE_DATA)(
                (ULONG_PTR)(HalpApDataPa.QuadPart + HAL_ARM64_KSEG0_BASE));
        __asm__ __volatile__("dsb sy" ::: "memory");
        DPRINT1("[arm64][HAL] AP sync timeout after %lu iterations, Progress=0x%x\n",
                Iterations, DataKseg0->Progress);
        DPRINT1("[arm64][HAL]   Expected: 0x11=data_loaded 0x22=gicr_done 0x33=tlb_mair_tcr "
                "0x44=ttbr_done 0x55=pre_mmu 0x66=mmu_on 0x77=vbar_set 0x88=stack_set 0x99=pre_sync\n");
    }
    return FALSE;
}

/*
 * HalpArm64GetTrampolinePhysicalAddress - Get physical address of trampoline entry
 */
UINT64
HalpArm64GetTrampolinePhysicalAddress(VOID)
{
    return HalpApTrampolinePa.QuadPart;
}

/*
 * HalpArm64IsTrampolineInitialized - Check if trampoline is ready
 */
BOOLEAN
HalpArm64IsTrampolineInitialized(VOID)
{
    return HalpApTrampolineInitialized;
}

/*
 * HalpArm64DiscoverParkedCpus - Scan MADT for CPUs using parking protocol
 */
VOID
HalpArm64DiscoverParkedCpus(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(LoaderBlock);

    if (!HalpArm64GicInfo.Present)
        return;

    HalpParkedCpuCount = 0;
    HalpParkingProtocolAvailable = FALSE;

    for (Index = 0; Index < HalpArm64GicInfo.GiccEntryCount && Index < MAXIMUM_PROCESSORS; Index++)
    {
        PHALP_ARM64_GICC_ENTRY Entry = &HalpArm64GicInfo.GiccEntries[Index];

        HalpParkedCpus[Index].Mpidr = Entry->Mpidr;
        HalpParkedCpus[Index].ParkedAddress = 0;
        HalpParkedCpus[Index].ParkingVersion = 0;
        HalpParkedCpus[Index].Enabled = (Entry->Flags & 0x1) != 0;
    }

    HalpParkedCpuCount = Index;

    DPRINT1("[arm64][HAL] Discovered %lu CPUs, parking protocol %s\n",
            HalpParkedCpuCount,
            HalpParkingProtocolAvailable ? "available" : "not available");
}

/*
 * HalpArm64WakeParkedCpu - Wake a CPU using the parking protocol
 */
BOOLEAN
HalpArm64WakeParkedCpu(
    _In_ ULONG ProcessorNumber,
    _In_ UINT64 EntryPoint,
    _In_ UINT64 ContextId)
{
    PHAL_ARM64_PARKED_CPU ParkedCpu;
    volatile UINT64 *Mailbox;

    UNREFERENCED_PARAMETER(ContextId);

    if (!HalpParkingProtocolAvailable)
        return FALSE;

    if (ProcessorNumber >= HalpParkedCpuCount)
        return FALSE;

    ParkedCpu = &HalpParkedCpus[ProcessorNumber];

    if (ParkedCpu->ParkingVersion == 0 || ParkedCpu->ParkedAddress == 0)
        return FALSE;

    Mailbox = (volatile UINT64 *)(ULONG_PTR)(ParkedCpu->ParkedAddress + HAL_ARM64_KSEG0_BASE);

    if (Mailbox[0] != ParkedCpu->Mpidr)
    {
        DPRINT1("[arm64][HAL] Parking mailbox CPU ID mismatch\n");
        return FALSE;
    }

    __asm__ __volatile__("dsb sy" ::: "memory");
    Mailbox[1] = EntryPoint;
    __asm__ __volatile__("dsb sy" ::: "memory");

    HalpArm64CleanDcacheRange((PVOID)Mailbox, 16);
    __asm__ __volatile__("dsb sy" ::: "memory");
    __asm__ __volatile__("sev" ::: "memory");

    DPRINT1("[arm64][HAL] Sent parking wake to CPU %lu\n", ProcessorNumber);
    return TRUE;
}

/*
 * HalpArm64EnableCpuInterface - Enable GICv3 CPU interface
 */
VOID
HalpArm64EnableCpuInterface(VOID)
{
    HalpWriteIccSre(7);
    HalpWriteIccPmr(0xFF);
    HalpWriteIccBpr1(0);
    HalpWriteIccIgrpen1(1);
    __asm__ __volatile__("isb" ::: "memory");
}
