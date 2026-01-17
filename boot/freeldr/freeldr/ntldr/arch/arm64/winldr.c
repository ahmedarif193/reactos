/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Windows NT Loader Functions
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 *
 * Notes (2025-09):
 * - Implements hierarchical mapper (1GB -> 2MB -> 4KB) instead of coarse 1GB-only.
 * - Derives cache line sizes from CTR_EL0; keeps conservative L1/L2 capacities if topology probing is unavailable.
 * - UART helper uses TXFF polling; uses WFI in the wait loop for efficiency.
 */

#include <freeldr.h>
#include <ntldr/winldr.h>
#include <peloader.h>
#include <arch/arm64/arm64.h>
#ifdef UEFIBOOT
#include <uefildr.h>
#include <drivers/acpi/acpi.h>
extern EFI_SYSTEM_TABLE *GlobalSystemTable;
#endif
#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

#ifndef ARM64_PSCI_METHOD_SMC
#define ARM64_PSCI_METHOD_SMC 0
#define ARM64_PSCI_METHOD_HVC 1
#endif

#ifndef ARM64_FADT_PSCI_COMPLIANT
#ifdef ACPI_FADT_PSCI_COMPLIANT
#define ARM64_FADT_PSCI_COMPLIANT ACPI_FADT_PSCI_COMPLIANT
#else
#define ARM64_FADT_PSCI_COMPLIANT 0x0001
#endif
#endif

#ifndef ARM64_FADT_PSCI_USE_HVC
#ifdef ACPI_FADT_PSCI_USE_HVC
#define ARM64_FADT_PSCI_USE_HVC ACPI_FADT_PSCI_USE_HVC
#else
#define ARM64_FADT_PSCI_USE_HVC 0x0002
#endif
#endif

/* -------------------------------------------------------------------------- */
/* Compatibility helpers                                                      */
/* -------------------------------------------------------------------------- */
#ifndef FORCEINLINE
# if defined(_MSC_VER)
#  define FORCEINLINE __forceinline
# else
#  define FORCEINLINE __attribute__((always_inline)) inline
# endif
#endif

#ifndef ARM64_MAP_ATTR_UXN
#define ARM64_MAP_ATTR_UXN 0
#endif
#ifndef ARM64_MAP_ATTR_PXN
#define ARM64_MAP_ATTR_PXN 0
#endif

/* If your arch headers don't provide these, define them here */
#ifndef ARM64_BLOCK_SIZE_1G
#define ARM64_BLOCK_SIZE_1G (1ULL << 30)
#endif
#ifndef ARM64_BLOCK_MASK_1G
#define ARM64_BLOCK_MASK_1G (ARM64_BLOCK_SIZE_1G - 1)
#endif
#ifndef ARM64_BLOCK_SIZE_2M
#define ARM64_BLOCK_SIZE_2M (1ULL << 21)
#endif
#ifndef ARM64_BLOCK_MASK_2M
#define ARM64_BLOCK_MASK_2M (ARM64_BLOCK_SIZE_2M - 1)
#endif

#define ALIGN_DOWN_16(x) ((ULONG_PTR)((x) & ~((ULONG_PTR)0xF)))
#define IS_ALIGNED(x, a) (((x) & ((a) - 1)) == 0)

#ifndef KI_USER_SHARED_DATA
#define KI_USER_SHARED_DATA     0xFFFFF78000000000ULL
#endif

#ifdef UEFIBOOT
static PRSDP Arm64LocateRsdp(VOID);
static PFADT Arm64LocateFadt(VOID);
static VOID Arm64PopulatePsciConfiguration(PLOADER_PARAMETER_BLOCK LoaderBlock);
#endif

/* -------------------------------------------------------------------------- */
/* Minimal PL011 UART helper for bring-up logs (QEMU -M virt default)         */
/* -------------------------------------------------------------------------- */
#if defined(_M_ARM64) || defined(__aarch64__)
#define PL011_BASE   0x09000000U      /* QEMU virt PL011 base */
#define PL011_DR     (*(volatile ULONG *)(PL011_BASE + 0x00))
#define PL011_FR     (*(volatile ULONG *)(PL011_BASE + 0x18))
#define PL011_TXFF   (1u << 5)

#ifdef UEFIBOOT
static inline VOID UartPutc(char c) { (void)c; }
static VOID UartPuts(const char* s) { (void)s; }
#else
static inline VOID UartPutc(char c)
{
    /* Use yield instead of wfi while TX FIFO is full (wfi hangs on Apple Silicon HVF). */
    while (PL011_FR & PL011_TXFF) { __asm__ __volatile__("yield"); }
    PL011_DR = (unsigned char)c;
}
static VOID UartPuts(const char* s)
{
    while (*s) { if (*s == '\n') UartPutc('\r'); UartPutc(*s++); }
}
#endif
#endif

/* -------------------------------------------------------------------------- */
/* ARM64-specific data structures for kernel initialization                   */
/* -------------------------------------------------------------------------- */
/*
 * PCR allocation size.
 * The KIPCR structure on ARM64 is approximately 15KB (0x3AC0 bytes) because
 * it contains an embedded KPRCB which includes large arrays like
 * DispatcherReadyListHead[32], LockQueue[], PPLookasideList[], etc.
 * We allocate 4 pages (16KB) to be safe with alignment.
 */
#define PCR_ALLOCATION_SIZE     (4 * PAGE_SIZE)

typedef struct _ARM64_KERNEL_DATA
{
    CHAR KernelStack[KERNEL_STACK_SIZE];    /* Main kernel stack */
    CHAR PanicStack[KERNEL_STACK_SIZE];     /* Panic/emergency stack */
    CHAR InterruptStack[KERNEL_STACK_SIZE]; /* Interrupt handling stack */
    CHAR InitialProcess[PAGE_SIZE];         /* Initial system process */
    CHAR InitialThread[PAGE_SIZE];          /* Initial system thread */
    CHAR Prcb[PAGE_SIZE];                   /* Processor Control Block (unused, PCR embeds PRCB) */
    CHAR Pcr[PCR_ALLOCATION_SIZE];          /* Processor Control Region (~15KB with embedded PRCB) */
} ARM64_KERNEL_DATA, *PARM64_KERNEL_DATA;

static PARM64_KERNEL_DATA KernelDataBlock = NULL;
static PVOID Arm64SharedUserDataPage = NULL;

static BOOLEAN Arm64InitializeMemory(IN PLOADER_PARAMETER_BLOCK LoaderBlock);
static VOID    Arm64ConfigureProcessorContext(USHORT OperatingSystemVersion);
static BOOLEAN Arm64AllocateKernelDataStructures(VOID);

/* Low-level mapper provided by the platform */
extern BOOLEAN Arm64MapVirtualMemory(ULONGLONG Va, ULONGLONG Pa, ULONGLONG Size, ULONG Attrs);

static
BOOLEAN
Arm64EnsureSharedUserDataMapped(VOID)
{
    if (Arm64SharedUserDataPage)
        return TRUE;

    PVOID shared_page = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderStartupPcrPage);
    if (!shared_page)
    {
        ERR("ARM64: Failed to allocate SharedUserData page\n");
        return FALSE;
    }

    RtlZeroMemory(shared_page, MM_PAGE_SIZE);

    ULONGLONG shared_pa = (ULONGLONG)(ULONG_PTR)shared_page;
    ULONGLONG shared_va = KI_USER_SHARED_DATA;
    ULONG shared_attrs = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_UXN | ARM64_MAP_ATTR_PXN;

    if (!Arm64MapVirtualMemory(shared_va, shared_pa, MM_PAGE_SIZE, shared_attrs))
    {
        if (!Arm64MapUserSharedDataPage(shared_va, shared_pa, shared_attrs))
        {
            ERR("ARM64: Failed to map SharedUserData (PA=0x%llx VA=0x%llx)\n",
                (unsigned long long)shared_pa,
                (unsigned long long)shared_va);
            return FALSE;
        }
    }

    Arm64SharedUserDataPage = shared_page;

    TRACE("ARM64: SharedUserData mapped VA=0x%llx -> PA=0x%llx\n",
          (unsigned long long)shared_va,
          (unsigned long long)shared_pa);

    return TRUE;
}

VOID
Arm64LoaderZeroSharedUserData(VOID)
{
    if (!Arm64EnsureSharedUserDataMapped())
        return;

    if (Arm64SharedUserDataPage)
        RtlZeroMemory(Arm64SharedUserDataPage, MM_PAGE_SIZE);
}

/* -------------------------------------------------------------------------- */
/* Range mapping (map_region_hierarchical handles block selection)            */
/* -------------------------------------------------------------------------- */
static BOOLEAN
Arm64MapRangeHierarchical(ULONGLONG Va, ULONGLONG Pa, ULONGLONG Size, ULONG Attrs)
{
    if (Size == 0)
        return TRUE;

    return Arm64MapVirtualMemory(Va, Pa, Size, Attrs);
}

/* -------------------------------------------------------------------------- */
/* Paging helpers (public API used by the loader)                              */
/* -------------------------------------------------------------------------- */

BOOLEAN
MempSetupPaging(
    IN PFN_NUMBER StartPage,
    IN PFN_NUMBER NumberOfPages,
    IN BOOLEAN KernelMapping)
{
    if (NumberOfPages == 0)
        return TRUE;

    ULONGLONG phys_start = ((ULONGLONG)StartPage) << MM_PAGE_SHIFT;
    ULONGLONG phys_end   = phys_start + (((ULONGLONG)NumberOfPages) << MM_PAGE_SHIFT);
    ULONGLONG length     = phys_end - phys_start;

    /* Prefer non-exec identity mappings; keep the loader's own range executable. */
    const ULONG attrs_id_exec = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_EXECUTE;
    const ULONG attrs_id_nx = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_UXN | ARM64_MAP_ATTR_PXN;
    ULONG attrs_id = attrs_id_nx;
    const ULONG attrs_kv = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_EXECUTE;

    if (!KernelMapping)
    {
        ULONGLONG current_pc;
        __asm__ volatile("adr %0, ." : "=r"(current_pc));
        if (current_pc >= phys_start && current_pc < phys_end)
        {
            attrs_id = attrs_id_exec;
            TRACE("ARM64: Exec identity map for loader range 0x%llx..0x%llx\n",
                  (unsigned long long)phys_start,
                  (unsigned long long)phys_end);
        }
    }

    TRACE("ARM64: MempSetupPaging StartPage=0x%lx, Pages=0x%lx, KernelMapping=%d\n",
          (ULONG)StartPage, (ULONG)NumberOfPages, KernelMapping);
    TRACE("ARM64:   range PA [0x%llx, 0x%llx) len=0x%llx\n",
          (unsigned long long)phys_start,
          (unsigned long long)phys_end,
          (unsigned long long)length);

    /* TTBR0: identity map the exact range, using hierarchical granularity */
    if (!Arm64MapRangeHierarchical(phys_start, phys_start, length, attrs_id))
    {
        ERR("ARM64: Identity mapping failed for PA range 0x%llx..0x%llx\n",
            (unsigned long long)phys_start, (unsigned long long)phys_end);
        return FALSE;
    }

    /* TTBR1: KSEG0 mirror for the same range, executable (code fetch) if requested */
    if (KernelMapping)
    {
        ULONGLONG kva = ARM64_KSEG0_BASE + phys_start;
        if (!Arm64MapRangeHierarchical(kva, phys_start, length, attrs_kv))
        {
            ERR("ARM64: KSEG0 mapping failed for VA 0x%llx len 0x%llx\n",
                (unsigned long long)kva, (unsigned long long)length);
            return FALSE;
        }
    }

    /* Ensure page table updates are visible before returning */
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb");

    return TRUE;
}

VOID
MempUnmapPage(
    PFN_NUMBER Page)
{
    /* ARM64 page unmapping - not typically used by the bootloader */
    TRACE("ARM64: Unmapping page 0x%lx (not implemented in bootloader)\n", (ULONG)Page);
    UNIMPLEMENTED;
}

VOID
MempDump(VOID)
{
    TRACE("ARM64: Memory dump requested (no-op placeholder)\n");
}

/* -------------------------------------------------------------------------- */
/* NT handoff preparation                                                     */
/* -------------------------------------------------------------------------- */

static VOID
Arm64FillCacheInfoFromCtr(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /*
     * CTR_EL0:
     *  - IminLine[3:0]   : log2(Number of words in smallest I-line)
     *  - DminLine[19:16] : log2(Number of words in smallest D-line)
     *  LineBytes = 4 * 2^(field)
     */
    UINT64 ctr_el0 = 0;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr_el0));

    UINT64 IminLine = (ctr_el0 & 0xF);
    UINT64 DminLine = ((ctr_el0 >> 16) & 0xF);

    ULONG icache_line_bytes = (ULONG)(4ULL << IminLine);
    ULONG dcache_line_bytes = (ULONG)(4ULL << DminLine);

    /* Keep conservative capacities; set fill sizes accurately from hardware */
    LoaderBlock->u.Arm64.FirstLevelDcacheFillSize  = dcache_line_bytes;
    LoaderBlock->u.Arm64.FirstLevelIcacheFillSize  = icache_line_bytes;
    LoaderBlock->u.Arm64.SecondLevelDcacheFillSize = dcache_line_bytes; /* heuristic */
    LoaderBlock->u.Arm64.SecondLevelIcacheFillSize = icache_line_bytes; /* heuristic */

    TRACE("ARM64: Cache line sizes from CTR_EL0: I=%lu B, D=%lu B\n",
          icache_line_bytes, dcache_line_bytes);
}

#ifdef UEFIBOOT
static
PRSDP
Arm64LocateRsdp(VOID)
{
    if (!GlobalSystemTable)
        return NULL;

    EFI_GUID Acpi20 = EFI_ACPI_20_TABLE_GUID;
    EFI_GUID Acpi10 = ACPI_10_TABLE_GUID;

    for (UINTN Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &Acpi20, sizeof(EFI_GUID)) ||
            !memcmp(&Entry->VendorGuid, &Acpi10, sizeof(EFI_GUID)))
        {
            return (PRSDP)Entry->VendorTable;
        }
    }

    return NULL;
}

static
PFADT
Arm64LocateFadt(VOID)
{
    PRSDP Rsdp = Arm64LocateRsdp();
    if (!Rsdp)
        return NULL;

    /* Prefer XSDT if available (ACPI 2.0+) */
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress.QuadPart != 0)
    {
        PXSDT Xsdt = (PXSDT)(ULONG_PTR)Rsdp->XsdtAddress.QuadPart;
        if (!Xsdt)
            return NULL;

        ULONG EntryCount = 0;
        if (Xsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
            EntryCount = (Xsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(PHYSICAL_ADDRESS);

        for (ULONG i = 0; i < EntryCount; ++i)
        {
            ULONGLONG TablePa = Xsdt->Tables[i].QuadPart;
            if (TablePa == 0)
                continue;

            PFADT Fadt = (PFADT)(ULONG_PTR)TablePa;
            if (!Fadt)
                continue;

            if (Fadt->Header.Signature == FADT_SIGNATURE)
            {
                if (Fadt->Header.Length >= FIELD_OFFSET(FADT, minor_revision) + sizeof(Fadt->minor_revision))
                    return Fadt;
            }
        }
    }

    if (Rsdp->RsdtAddress != 0)
    {
        PRSDT Rsdt = (PRSDT)(ULONG_PTR)Rsdp->RsdtAddress;
        if (!Rsdt)
            return NULL;

        ULONG EntryCount = 0;
        if (Rsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
            EntryCount = (Rsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONG);

        for (ULONG i = 0; i < EntryCount; ++i)
        {
            ULONG TablePa = Rsdt->Tables[i];
            if (TablePa == 0)
                continue;

            PFADT Fadt = (PFADT)(ULONG_PTR)TablePa;
            if (!Fadt)
                continue;

            if (Fadt->Header.Signature == FADT_SIGNATURE)
            {
                if (Fadt->Header.Length >= FIELD_OFFSET(FADT, minor_revision) + sizeof(Fadt->minor_revision))
                    return Fadt;
            }
        }
    }

    return NULL;
}

static
VOID
Arm64PopulatePsciConfiguration(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (!LoaderBlock)
        return;

    PFADT Fadt = Arm64LocateFadt();
    if (!Fadt)
    {
        TRACE("ARM64: PSCI detection: FADT not found, defaulting to SMC\n");
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
        LoaderBlock->u.Arm64.PsciFlags = 0;
        return;
    }

    if (Fadt->Header.Length < FIELD_OFFSET(FADT, arm_boot_arch) + sizeof(Fadt->arm_boot_arch))
    {
        TRACE("ARM64: PSCI detection: FADT lacks ArmBootArch field, defaulting to SMC\n");
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
        LoaderBlock->u.Arm64.PsciFlags = 0;
        return;
    }

    USHORT ArmBootFlags = Fadt->arm_boot_arch;
    LoaderBlock->u.Arm64.PsciFlags = ArmBootFlags;

    if (!(ArmBootFlags & ARM64_FADT_PSCI_COMPLIANT))
    {
        TRACE("ARM64: PSCI detection: ArmBootArch does not advertise PSCI, defaulting to SMC\n");
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
        return;
    }

    if (ArmBootFlags & ARM64_FADT_PSCI_USE_HVC)
    {
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_HVC;
        TRACE("ARM64: PSCI detection: ACPI indicates HVC conduit\n");
    }
    else
    {
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
        TRACE("ARM64: PSCI detection: ACPI indicates SMC conduit\n");
    }
}
#endif /* UEFIBOOT */

BOOLEAN
Arm64SetupForNt(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PVOID *GdtIdt,
    IN ULONG *PcrBasePage,
    IN ULONG *TssBasePage)
{
    TRACE("ARM64: Setting up for NT kernel\n");

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] Arm64SetupForNt entered\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /* ARM64 doesn't use GDT/IDT or TSS like x86 */
    *GdtIdt = NULL;
    *PcrBasePage = 0;
    *TssBasePage = 0;

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] About to allocate kernel data structures\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /* Allocate kernel data structures including stacks */
    if (!Arm64AllocateKernelDataStructures())
    {
        ERR("ARM64: Failed to allocate kernel data structures\n");
#if defined(_M_ARM64) || defined(__aarch64__)
        {
            volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
            const char *msg = "[PL011] ERROR: Arm64AllocateKernelDataStructures FAILED\r\n";
            while (*msg) {
                while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
                Uart[0] = *msg++;
            }
        }
#endif
        return FALSE;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] Kernel data structures allocated successfully\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /*
     * Populate LoaderBlock with stack and structure pointers.
     * MmAllocateMemoryWithType returns a physical allocation; convert to VA.
     */
    ULONG_PTR KernelStackPA     = (ULONG_PTR)KernelDataBlock->KernelStack + KERNEL_STACK_SIZE;
    ULONG_PTR PanicStackPA      = (ULONG_PTR)KernelDataBlock->PanicStack + KERNEL_STACK_SIZE;
    ULONG_PTR InterruptStackPA  = (ULONG_PTR)KernelDataBlock->InterruptStack + KERNEL_STACK_SIZE;
    ULONG_PTR PcrPA             = (ULONG_PTR)KernelDataBlock->Pcr;
    ULONG_PTR PrcbPA            = (ULONG_PTR)KernelDataBlock->Prcb;
    ULONG_PTR ProcessPA         = (ULONG_PTR)KernelDataBlock->InitialProcess;
    ULONG_PTR ThreadPA          = (ULONG_PTR)KernelDataBlock->InitialThread;

    /* Ensure 16-byte alignment for SP as per AArch64 ABI */
    KernelStackPA    = ALIGN_DOWN_16(KernelStackPA);
    PanicStackPA     = ALIGN_DOWN_16(PanicStackPA);
    InterruptStackPA = ALIGN_DOWN_16(InterruptStackPA);

    /* Convert to kernel VA (KSEG0) if still physical */
    LoaderBlock->KernelStack             = (KernelStackPA     < ARM64_KSEG0_BASE) ? (KernelStackPA     + ARM64_KSEG0_BASE) : KernelStackPA;
    LoaderBlock->u.Arm64.PanicStack      = (PanicStackPA      < ARM64_KSEG0_BASE) ? (PanicStackPA      + ARM64_KSEG0_BASE) : PanicStackPA;
    LoaderBlock->u.Arm64.InterruptStack  = (InterruptStackPA  < ARM64_KSEG0_BASE) ? (InterruptStackPA  + ARM64_KSEG0_BASE) : InterruptStackPA;

    TRACE("ARM64: LoaderBlock stack pointers - Kernel=0x%llx Panic=0x%llx Interrupt=0x%llx\n",
          (unsigned long long)LoaderBlock->KernelStack,
          (unsigned long long)LoaderBlock->u.Arm64.PanicStack,
          (unsigned long long)LoaderBlock->u.Arm64.InterruptStack);
    LoaderBlock->u.Arm64.PcrPage         = (PcrPA             < ARM64_KSEG0_BASE) ? (PcrPA             + ARM64_KSEG0_BASE) : PcrPA;
    LoaderBlock->u.Arm64.PdrPage         = 0; /* Not used on ARM64 */
    LoaderBlock->Prcb                     = (PrcbPA           < ARM64_KSEG0_BASE) ? (PrcbPA            + ARM64_KSEG0_BASE) : PrcbPA;
    LoaderBlock->Process                  = (ProcessPA        < ARM64_KSEG0_BASE) ? (ProcessPA         + ARM64_KSEG0_BASE) : ProcessPA;
    LoaderBlock->Thread                   = (ThreadPA         < ARM64_KSEG0_BASE) ? (ThreadPA          + ARM64_KSEG0_BASE) : ThreadPA;

    TRACE("ARM64: Populated LoaderBlock - KernelStack=0x%llx (VA), PanicStack=0x%llx (VA), InterruptStack=0x%llx (VA)\n",
          (unsigned long long)LoaderBlock->KernelStack,
          (unsigned long long)LoaderBlock->u.Arm64.PanicStack,
          (unsigned long long)LoaderBlock->u.Arm64.InterruptStack);
    TRACE("ARM64: Physical addresses - KernelStack=0x%llx, PanicStack=0x%llx, InterruptStack=0x%llx\n",
          (unsigned long long)KernelStackPA,
          (unsigned long long)PanicStackPA,
          (unsigned long long)InterruptStackPA);
    TRACE("ARM64: VA conversion check - KSEG0_BASE=0x%llx\n",
          (unsigned long long)ARM64_KSEG0_BASE);

    /* Initialize ARM64 cache configuration information */
    /* Conservative capacities; fill sizes from hardware (CTR_EL0) */
    LoaderBlock->u.Arm64.FirstLevelDcacheSize       = 32768;   /* 32KB default */
    LoaderBlock->u.Arm64.FirstLevelIcacheSize       = 32768;   /* 32KB default */
    LoaderBlock->u.Arm64.SecondLevelDcacheSize      = 262144;  /* 256KB default */
   LoaderBlock->u.Arm64.SecondLevelIcacheSize      = 262144;  /* 256KB default */
    Arm64FillCacheInfoFromCtr(LoaderBlock);

    LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
    LoaderBlock->u.Arm64.PsciFlags   = 0;
#ifdef UEFIBOOT
    Arm64PopulatePsciConfiguration(LoaderBlock);
#endif
    TRACE("ARM64: PSCI configuration - conduit=%lu flags=0x%04lx\n",
          LoaderBlock->u.Arm64.PsciConduit,
          LoaderBlock->u.Arm64.PsciFlags);

    /* Any additional exception vector setup is expected to be done by the kernel */

    /* Ensure memory is properly prepared */
    if (!Arm64InitializeMemory(LoaderBlock))
    {
        ERR("ARM64: Failed to initialize memory for NT\n");
        return FALSE;
    }

    TRACE("ARM64: Successfully set up for NT kernel\n");
    return TRUE;
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
#if defined(_M_ARM64) || defined(__aarch64__)
    /* Direct PL011 UART output - works after ExitBootServices */
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "\r\n[PL011] WinLdrSetProcessorContext entered\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
    UartPuts("ARM64: WinLdrSetProcessorContext called\n");
#endif

    Arm64ConfigureProcessorContext(OperatingSystemVersion);

#if defined(_M_ARM64) || defined(__aarch64__)
    /* Direct PL011 UART output after page table enable */
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] WinLdrSetProcessorContext completed\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
    UartPuts("ARM64: WinLdrSetProcessorContext completed\n");
#endif
}

/* Provide the machine-dependent setup entry used by the generic NT loader path */
VOID
WinLdrSetupMachineDependent(
    PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVOID GdtIdt = NULL;
    ULONG PcrBasePage = 0;
    ULONG TssBasePage = 0;

#if defined(_M_ARM64) || defined(__aarch64__)
    /* Direct PL011 UART output - works after ExitBootServices */
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] WinLdrSetupMachineDependent entered\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /* Delegate to the ARM64-specific setup; GDT/IDT/TSS are not used on AArch64 */
    (void)Arm64SetupForNt(LoaderBlock, &GdtIdt, &PcrBasePage, &TssBasePage);

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] WinLdrSetupMachineDependent completed\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif
}

/* -------------------------------------------------------------------------- */
/* ARM64 specific memory & CPU setup                                          */
/* -------------------------------------------------------------------------- */

BOOLEAN
Arm64InitializeMemory(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    TRACE("ARM64: Initializing memory management structures\n");

    if (!LoaderBlock)
    {
        ERR("ARM64: Invalid LoaderBlock\n");
        return FALSE;
    }

    Arm64ApplyDeferredPageTableMemoryTypes();

    /* Memory descriptors are built later by WinLdrSetupMemoryLayout(). */
    TRACE("ARM64: Memory management structures initialized\n");
    return TRUE;
}

extern VOID Arm64EnablePageTables(VOID);

static VOID
Arm64ConfigureProcessorContext(USHORT OperatingSystemVersion)
{
    UNREFERENCED_PARAMETER(OperatingSystemVersion);

    TRACE("ARM64: WinLdrSetProcessorContext\n");

    /*
     * UEFI typically leaves us in EL1 with MMU on. We now switch to our
     * own page tables (TTBR0 identity, TTBR1 kernel) to ensure KSEG0
     * is accessible for the jump to the kernel.
     */
#ifdef UEFIBOOT
    /* Avoid firmware serial callbacks once we swap translation tables. */
    UefiSerialDisableFirmware();
#endif
    Arm64EnablePageTables();
}

/* WinLdrpDumpMemoryDescriptors and WinLdrLoadModule are defined in the main winldr.c */

/* Other architecture-specific functions can be added here as needed */

BOOLEAN
WinLdrCheckForLoadedDll(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PCH DllName,
    OUT PLDR_DATA_TABLE_ENTRY *LoadedEntry)
{
    /* Delegate to the generic PE loader helper which performs
       proper case-insensitive comparison against UNICODE names. */
    return PeLdrCheckForLoadedDll(&LoaderBlock->LoadOrderListHead,
                                  DllName,
                                  LoadedEntry);
}

/* ARM64 specific allocation of kernel data structures */
static BOOLEAN
Arm64AllocateKernelDataStructures(VOID)
{
    TRACE("ARM64: Allocating kernel data structures\n");

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] Arm64AllocateKernelDataStructures entered\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /* Allocate the ARM64 kernel data block which contains all stacks and structures.
       This returns a physical allocation in the loader's address space. */
    KernelDataBlock = MmAllocateMemoryWithType(sizeof(ARM64_KERNEL_DATA), LoaderMemoryData);
    if (!KernelDataBlock)
    {
        ERR("ARM64: Failed to allocate kernel data block of size %zu bytes\n",
            sizeof(ARM64_KERNEL_DATA));
#if defined(_M_ARM64) || defined(__aarch64__)
        {
            volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
            const char *msg = "[PL011] ERROR: MmAllocateMemoryWithType FAILED\r\n";
            while (*msg) {
                while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
                Uart[0] = *msg++;
            }
        }
#endif
        return FALSE;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] MmAllocateMemoryWithType succeeded\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /* Zero out the entire data block for clean initialization */
    RtlZeroMemory(KernelDataBlock, sizeof(ARM64_KERNEL_DATA));

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] RtlZeroMemory completed\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /*
     * Initialize the KTHREAD structure's stack fields.
     * The kernel expects these to be set up by the loader.
     * KTHREAD layout (ARM64/x64):
     *   +0x28: InitialStack  (top of stack)
     *   +0x30: StackLimit    (bottom of stack / guard)
     *   +0x38: StackBase     (same as InitialStack)
     *   +0x58: KernelStack   (current stack pointer)
     */
    {
        PULONG_PTR ThreadPtr = (PULONG_PTR)KernelDataBlock->InitialThread;
        ULONG_PTR StackBase = (ULONG_PTR)KernelDataBlock->KernelStack;
        ULONG_PTR StackTop = StackBase + KERNEL_STACK_SIZE;
        ULONG_PTR StackLimit = StackBase;

        /* Ensure 16-byte alignment for stack top */
        StackTop = StackTop & ~(ULONG_PTR)0xF;

        /* Set KTHREAD stack fields using raw offsets */
        ThreadPtr[0x28 / sizeof(ULONG_PTR)] = StackTop;     /* InitialStack */
        ThreadPtr[0x30 / sizeof(ULONG_PTR)] = StackLimit;   /* StackLimit */
        ThreadPtr[0x38 / sizeof(ULONG_PTR)] = StackTop;     /* StackBase */
        ThreadPtr[0x58 / sizeof(ULONG_PTR)] = StackTop;     /* KernelStack */

        TRACE("ARM64: InitialThread stack initialized: InitialStack=0x%llx StackLimit=0x%llx KernelStack=0x%llx\n",
              (unsigned long long)StackTop,
              (unsigned long long)StackLimit,
              (unsigned long long)StackTop);
    }

    TRACE("ARM64: Successfully allocated kernel data structures at %p\n", KernelDataBlock);
    TRACE("ARM64: KernelStack at %p, PanicStack at %p, InterruptStack at %p\n",
          KernelDataBlock->KernelStack, KernelDataBlock->PanicStack, KernelDataBlock->InterruptStack);
    TRACE("ARM64: Prcb at %p, Process at %p, Thread at %p\n",
          KernelDataBlock->Prcb, KernelDataBlock->InitialProcess, KernelDataBlock->InitialThread);

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] About to map kernel data block\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    /* Mirror the kernel data block into KSEG0 so the kernel stack & PCR are accessible */
    {
        ULONGLONG block_pa = (ULONGLONG)(ULONG_PTR)KernelDataBlock;
        ULONGLONG block_va = block_pa;
        ULONGLONG map_size = (ULONGLONG)((sizeof(ARM64_KERNEL_DATA) + MM_PAGE_SIZE - 1) & ~(MM_PAGE_SIZE - 1));
        ULONG map_attrs = ARM64_MAP_ATTR_NORMAL | ARM64_MAP_ATTR_UXN | ARM64_MAP_ATTR_PXN;

        if (block_pa < ARM64_KSEG0_BASE)
            block_va = ARM64_KSEG0_BASE + block_pa;

        if (!Arm64MapVirtualMemory(block_va, block_pa, map_size, map_attrs))
        {
            ERR("ARM64: Failed to map kernel data block (PA=0x%llx VA=0x%llx size=0x%llx)\n",
                (unsigned long long)block_pa,
                (unsigned long long)block_va,
                (unsigned long long)map_size);
#if defined(_M_ARM64) || defined(__aarch64__)
            {
                volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
                const char *msg = "[PL011] ERROR: Arm64MapVirtualMemory FAILED\r\n";
                while (*msg) {
                    while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
                    Uart[0] = *msg++;
                }
            }
#endif
            return FALSE;
        }

        TRACE("ARM64: Kernel data block mapped VA=0x%llx size=0x%llx\n",
              (unsigned long long)block_va,
              (unsigned long long)map_size);
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] Kernel data block mapped, checking shared user data\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    if (!Arm64EnsureSharedUserDataMapped())
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        {
            volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
            const char *msg = "[PL011] ERROR: Arm64EnsureSharedUserDataMapped FAILED\r\n";
            while (*msg) {
                while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
                Uart[0] = *msg++;
            }
        }
#endif
        return FALSE;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        volatile ULONG *Uart = (volatile ULONG *)0x09000000UL;
        const char *msg = "[PL011] Arm64AllocateKernelDataStructures completed successfully\r\n";
        while (*msg) {
            while (Uart[0x18 / sizeof(ULONG)] & (1 << 5)) {}
            Uart[0] = *msg++;
        }
    }
#endif

    return TRUE;
}
