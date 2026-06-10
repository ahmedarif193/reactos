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
#include <uefildr.h>
#include <drivers/acpi/acpi.h>
#include <reactos/arm64/acpi.h>
#include <reactos/arm64/early_uart.h>
extern EFI_SYSTEM_TABLE *GlobalSystemTable;
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

static PRSDP Arm64LocateRsdp(VOID);
static PDESCRIPTION_HEADER Arm64LocateAcpiTable(ULONG Signature, ULONG MinimumLength);
static PFADT Arm64LocateFadt(VOID);
static VOID Arm64PopulateEarlyDeviceRanges(PLOADER_PARAMETER_BLOCK LoaderBlock);
static VOID Arm64PopulatePsciConfiguration(PLOADER_PARAMETER_BLOCK LoaderBlock);

/* -------------------------------------------------------------------------- */
/* ARM64-specific data structures for kernel initialization                   */
/* -------------------------------------------------------------------------- */
/*
 * PCR allocation size.
 * The KIPCR structure on ARM64 follows the Win11 26100 layout: 0xA980 bytes
 * (~42.4 KB) with the 0xA000-byte KPRCB embedded at offset 0x980. We allocate
 * 12 pages (48 KB) so the mapped kernel-data block fully covers the kernel's
 * RtlZeroMemory(Pcr, sizeof(*Pcr)) at KiInitializePcr entry, leaving slack
 * for future struct growth. Undersizing this writes past the end of the
 * KSEG0-mapped KernelDataBlock and faults the kernel before its first PCR
 * field is set up.
 */
#define PCR_ALLOCATION_SIZE     (12 * PAGE_SIZE)

typedef struct _ARM64_KERNEL_DATA
{
    CHAR KernelStack[KERNEL_STACK_SIZE];    /* Main kernel stack */
    CHAR PanicStack[KERNEL_STACK_SIZE];     /* Panic/emergency stack */
    CHAR InterruptStack[KERNEL_STACK_SIZE]; /* Interrupt handling stack */
    CHAR InitialProcess[PAGE_SIZE];         /* Initial system process */
    CHAR InitialThread[PAGE_SIZE];          /* Initial system thread */
    CHAR Prcb[PAGE_SIZE];                   /* Processor Control Block (unused, PCR embeds PRCB) */
    CHAR Pcr[PCR_ALLOCATION_SIZE];          /* Processor Control Region (0xA980 with embedded PRCB) */
} ARM64_KERNEL_DATA, *PARM64_KERNEL_DATA;

static PARM64_KERNEL_DATA KernelDataBlock = NULL;
static PVOID Arm64SharedUserDataPage = NULL;

static BOOLEAN Arm64InitializeMemory(IN PLOADER_PARAMETER_BLOCK LoaderBlock);
static VOID    Arm64ConfigureProcessorContext(USHORT OperatingSystemVersion);
static BOOLEAN Arm64AllocateKernelDataStructures(VOID);

/* Low-level mapper provided by the platform */
extern BOOLEAN Arm64MapVirtualMemory(ULONGLONG Va, ULONGLONG Pa, ULONGLONG Size, ULONG Attrs);

#define ARM64_EARLY_DEVICE_GICD_LENGTH      0x10000ULL
#define ARM64_EARLY_DEVICE_GICC_LENGTH      0x10000ULL
#define ARM64_EARLY_DEVICE_GICR_LENGTH      0x20000ULL
#define ARM64_EARLY_DEVICE_GIC_MSI_LENGTH   0x10000ULL
#define ARM64_EARLY_DEVICE_GIC_ITS_LENGTH   0x20000ULL

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
        }
    }

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
    UNIMPLEMENTED;
}

VOID
MempDump(VOID)
{
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
}
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
PDESCRIPTION_HEADER
Arm64LocateAcpiTable(ULONG Signature, ULONG MinimumLength)
{
    PRSDP Rsdp = Arm64LocateRsdp();
    if (!Rsdp)
        return NULL;

    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress.QuadPart != 0)
    {
        PXSDT Xsdt = (PXSDT)(ULONG_PTR)Rsdp->XsdtAddress.QuadPart;
        ULONG EntryCount = 0;

        if (Xsdt && Xsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
            EntryCount = (Xsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) /
                         sizeof(PHYSICAL_ADDRESS);

        for (ULONG i = 0; i < EntryCount; ++i)
        {
            ULONGLONG TablePa = Xsdt->Tables[i].QuadPart;
            PDESCRIPTION_HEADER Header;

            if (TablePa == 0)
                continue;

            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)TablePa;
            if (!Header)
                continue;

            if ((Header->Signature == Signature) &&
                (Header->Length >= MinimumLength))
            {
                return Header;
            }
        }
    }

    if (Rsdp->RsdtAddress != 0)
    {
        PRSDT Rsdt = (PRSDT)(ULONG_PTR)Rsdp->RsdtAddress;
        ULONG EntryCount = 0;

        if (Rsdt && Rsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
            EntryCount = (Rsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) /
                         sizeof(ULONG);

        for (ULONG i = 0; i < EntryCount; ++i)
        {
            ULONG TablePa = Rsdt->Tables[i];
            PDESCRIPTION_HEADER Header;

            if (TablePa == 0)
                continue;

            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)TablePa;
            if (!Header)
                continue;

            if ((Header->Signature == Signature) &&
                (Header->Length >= MinimumLength))
            {
                return Header;
            }
        }
    }

    return NULL;
}

static
PFADT
Arm64LocateFadt(VOID)
{
    return (PFADT)Arm64LocateAcpiTable(FADT_SIGNATURE,
                                       FIELD_OFFSET(FADT, minor_revision) +
                                       sizeof(((PFADT)0)->minor_revision));
}

static
VOID
Arm64ResetEarlyDeviceRanges(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (!LoaderBlock)
        return;

    LoaderBlock->u.Arm64.EarlyDeviceRangeCount = 0;
    RtlZeroMemory(LoaderBlock->u.Arm64.EarlyDeviceRanges,
                  sizeof(LoaderBlock->u.Arm64.EarlyDeviceRanges));
}

static
VOID
Arm64AddEarlyDeviceRange(
    PLOADER_PARAMETER_BLOCK LoaderBlock,
    ULONGLONG BaseAddress,
    ULONGLONG Length)
{
    ULONG Count;

    if (!LoaderBlock || (BaseAddress == 0) || (Length == 0))
        return;

    Count = LoaderBlock->u.Arm64.EarlyDeviceRangeCount;
    if (Count > ARM64_LOADER_MAX_EARLY_DEVICE_RANGES)
        Count = ARM64_LOADER_MAX_EARLY_DEVICE_RANGES;

    for (ULONG Index = 0; Index < Count; ++Index)
    {
        PARM64_LOADER_EARLY_DEVICE_RANGE Range;

        Range = &LoaderBlock->u.Arm64.EarlyDeviceRanges[Index];
        if ((Range->BaseAddress == BaseAddress) &&
            (Range->Length == Length))
        {
            return;
        }
    }

    if (Count == ARM64_LOADER_MAX_EARLY_DEVICE_RANGES)
        return;

    LoaderBlock->u.Arm64.EarlyDeviceRanges[Count].BaseAddress = BaseAddress;
    LoaderBlock->u.Arm64.EarlyDeviceRanges[Count].Length = Length;
    LoaderBlock->u.Arm64.EarlyDeviceRangeCount = Count + 1;
}

static
VOID
Arm64PopulateEarlyDeviceRanges(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PARM64_ACPI_MADT Madt;
    PUCHAR Current;
    PUCHAR End;

    if (!LoaderBlock)
        return;

    Arm64ResetEarlyDeviceRanges(LoaderBlock);

    Arm64AddEarlyDeviceRange(LoaderBlock,
                             LoaderBlock->u.Arm64.EarlyUartAddress,
                             PAGE_SIZE);

    Madt = (PARM64_ACPI_MADT)Arm64LocateAcpiTable(APIC_SIGNATURE,
                                                  sizeof(*Madt));
    if (!Madt)
        return;

    Current = (PUCHAR)(Madt + 1);
    End = (PUCHAR)Madt + Madt->Header.Length;

    while (Current + sizeof(ARM64_ACPI_MADT_SUBTABLE) <= End)
    {
        PARM64_ACPI_MADT_SUBTABLE Header;

        Header = (PARM64_ACPI_MADT_SUBTABLE)Current;

        if ((Header->Length < sizeof(*Header)) ||
            (Current + Header->Length > End))
        {
            break;
        }

        switch (Header->Type)
        {
            case ARM64_ACPI_MADT_TYPE_GENERIC_INTERRUPT:
            {
                PARM64_ACPI_MADT_GENERIC_INTERRUPT Gicc;

                Gicc = (PARM64_ACPI_MADT_GENERIC_INTERRUPT)Header;

                if (Header->Length >= FIELD_OFFSET(ARM64_ACPI_MADT_GENERIC_INTERRUPT,
                                                   BaseAddress) +
                                      sizeof(Gicc->BaseAddress))
                {
                    Arm64AddEarlyDeviceRange(LoaderBlock,
                                             Gicc->BaseAddress,
                                             ARM64_EARLY_DEVICE_GICC_LENGTH);
                }

                if (Header->Length >= FIELD_OFFSET(ARM64_ACPI_MADT_GENERIC_INTERRUPT,
                                                   GicrBaseAddress) +
                                      sizeof(Gicc->GicrBaseAddress))
                {
                    Arm64AddEarlyDeviceRange(LoaderBlock,
                                             Gicc->GicrBaseAddress,
                                             ARM64_EARLY_DEVICE_GICR_LENGTH);
                }
                break;
            }

            case ARM64_ACPI_MADT_TYPE_GENERIC_DISTRIBUTOR:
            {
                PARM64_ACPI_MADT_GENERIC_DISTRIBUTOR Gicd;

                if (Header->Length >= sizeof(*Gicd))
                {
                    Gicd = (PARM64_ACPI_MADT_GENERIC_DISTRIBUTOR)Header;
                    Arm64AddEarlyDeviceRange(LoaderBlock,
                                             Gicd->BaseAddress,
                                             ARM64_EARLY_DEVICE_GICD_LENGTH);
                }
                break;
            }

            case ARM64_ACPI_MADT_TYPE_GENERIC_MSI_FRAME:
            {
                PARM64_ACPI_MADT_GENERIC_MSI_FRAME Frame;

                if (Header->Length >= sizeof(*Frame))
                {
                    Frame = (PARM64_ACPI_MADT_GENERIC_MSI_FRAME)Header;
                    Arm64AddEarlyDeviceRange(LoaderBlock,
                                             Frame->BaseAddress,
                                             ARM64_EARLY_DEVICE_GIC_MSI_LENGTH);
                }
                break;
            }

            case ARM64_ACPI_MADT_TYPE_GENERIC_REDISTRIBUTOR:
            {
                PARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR Gicr;

                if (Header->Length >= sizeof(*Gicr))
                {
                    Gicr = (PARM64_ACPI_MADT_GENERIC_REDISTRIBUTOR)Header;
                    Arm64AddEarlyDeviceRange(LoaderBlock,
                                             Gicr->BaseAddress,
                                             Gicr->Length ? Gicr->Length :
                                             ARM64_EARLY_DEVICE_GICR_LENGTH);
                }
                break;
            }

            case ARM64_ACPI_MADT_TYPE_GENERIC_TRANSLATOR:
            {
                PARM64_ACPI_MADT_GENERIC_TRANSLATOR Its;

                if (Header->Length >= sizeof(*Its))
                {
                    Its = (PARM64_ACPI_MADT_GENERIC_TRANSLATOR)Header;
                    Arm64AddEarlyDeviceRange(LoaderBlock,
                                             Its->BaseAddress,
                                             ARM64_EARLY_DEVICE_GIC_ITS_LENGTH);
                }
                break;
            }
        }

        Current += Header->Length;
    }
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
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
        LoaderBlock->u.Arm64.PsciFlags = 0;
        return;
    }

    if (Fadt->Header.Length < FIELD_OFFSET(FADT, arm_boot_arch) + sizeof(Fadt->arm_boot_arch))
    {
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
        LoaderBlock->u.Arm64.PsciFlags = 0;
        return;
    }

    USHORT ArmBootFlags = Fadt->arm_boot_arch;
    LoaderBlock->u.Arm64.PsciFlags = ArmBootFlags;

    if (!(ArmBootFlags & ARM64_FADT_PSCI_COMPLIANT))
    {
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
        return;
    }

    if (ArmBootFlags & ARM64_FADT_PSCI_USE_HVC)
    {
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_HVC;
    }
    else
    {
        LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
    }
}

BOOLEAN
Arm64SetupForNt(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PVOID *GdtIdt,
    IN ULONG *PcrBasePage,
    IN ULONG *TssBasePage)
{
    /* ARM64 doesn't use GDT/IDT or TSS like x86 */
    *GdtIdt = NULL;
    *PcrBasePage = 0;
    *TssBasePage = 0;

    /* Allocate kernel data structures including stacks */
    if (!Arm64AllocateKernelDataStructures())
    {
        ERR("ARM64: Failed to allocate kernel data structures\n");
        return FALSE;
    }

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

    LoaderBlock->u.Arm64.PcrPage         = (PcrPA             < ARM64_KSEG0_BASE) ? (PcrPA             + ARM64_KSEG0_BASE) : PcrPA;
    LoaderBlock->u.Arm64.PdrPage         = 0; /* Not used on ARM64 */
    LoaderBlock->Prcb                     = (PrcbPA           < ARM64_KSEG0_BASE) ? (PrcbPA            + ARM64_KSEG0_BASE) : PrcbPA;
    LoaderBlock->Process                  = (ProcessPA        < ARM64_KSEG0_BASE) ? (ProcessPA         + ARM64_KSEG0_BASE) : ProcessPA;
    LoaderBlock->Thread                   = (ThreadPA         < ARM64_KSEG0_BASE) ? (ThreadPA          + ARM64_KSEG0_BASE) : ThreadPA;

    /* Initialize ARM64 cache configuration information */
    /* Conservative capacities; fill sizes from hardware (CTR_EL0) */
    LoaderBlock->u.Arm64.FirstLevelDcacheSize       = 32768;   /* 32KB default */
    LoaderBlock->u.Arm64.FirstLevelIcacheSize       = 32768;   /* 32KB default */
    LoaderBlock->u.Arm64.SecondLevelDcacheSize      = 262144;  /* 256KB default */
   LoaderBlock->u.Arm64.SecondLevelIcacheSize      = 262144;  /* 256KB default */
    Arm64FillCacheInfoFromCtr(LoaderBlock);

    LoaderBlock->u.Arm64.PsciConduit = ARM64_PSCI_METHOD_SMC;
    LoaderBlock->u.Arm64.PsciFlags   = 0;
    Arm64PopulatePsciConfiguration(LoaderBlock);

    /* Pass the detected UART address to the kernel */
    LoaderBlock->u.Arm64.EarlyUartAddress = EarlyUartBaseAddress;
    LoaderBlock->u.Arm64.EarlyUartInterface = (ULONG)EarlyUartInterface;
    Arm64PopulateEarlyDeviceRanges(LoaderBlock);

    /* Ensure memory is properly prepared */
    if (!Arm64InitializeMemory(LoaderBlock))
    {
        ERR("failed to initialize memory for NT\n");
        return FALSE;
    }

    return TRUE;
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    Arm64ConfigureProcessorContext(OperatingSystemVersion);
}

/* Provide the machine-dependent setup entry used by the generic NT loader path */
VOID
WinLdrSetupMachineDependent(
    PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVOID GdtIdt = NULL;
    ULONG PcrBasePage = 0;
    ULONG TssBasePage = 0;

    /* Delegate to the ARM64-specific setup; GDT/IDT/TSS are not used on AArch64 */
    (void)Arm64SetupForNt(LoaderBlock, &GdtIdt, &PcrBasePage, &TssBasePage);
}

/* -------------------------------------------------------------------------- */
/* ARM64 specific memory & CPU setup                                          */
/* -------------------------------------------------------------------------- */

extern VOID Arm64PreparePageTables(VOID);
extern VOID Arm64EnablePageTables(VOID);
extern VOID Arm64ClearIdentityMappings(VOID);

BOOLEAN
Arm64InitializeMemory(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (!LoaderBlock)
        return FALSE;

    Arm64ApplyDeferredPageTableMemoryTypes();
    return TRUE;
}

static VOID
Arm64ConfigureProcessorContext(USHORT OperatingSystemVersion)
{
    UNREFERENCED_PARAMETER(OperatingSystemVersion);

    /*
     * UEFI typically leaves us in EL1 with MMU on. We now switch to our
     * own page tables (TTBR0 identity, TTBR1 kernel) to ensure KSEG0
     * is accessible for the jump to the kernel.
     */
    /* Avoid firmware serial callbacks once we swap translation tables. */
    UefiSerialDisableFirmware();
    Arm64EnablePageTables();

    /*
     * NOTE: We do NOT clear TTBR0 identity mappings here because:
     * 1. FreeLoader is still executing from identity-mapped addresses
     * 2. Clearing TTBR0 now would cause translation faults when returning
     * 3. The kernel will clear these mappings itself after initialization
     *
     * The identity mappings in TTBR0 are harmless during kernel startup
     * because the kernel runs from TTBR1 (kernel space 0xFFFF...). The
     * kernel's memory manager will clear TTBR0 when setting up the first
     * user process.
     */
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
    /* Allocate the ARM64 kernel data block which contains all stacks and structures. */
    KernelDataBlock = MmAllocateMemoryWithType(sizeof(ARM64_KERNEL_DATA), LoaderMemoryData);
    if (!KernelDataBlock)
    {
        ERR("failed to allocate kernel data block\n");
        return FALSE;
    }

    /* Zero out the entire data block for clean initialization */
    RtlZeroMemory(KernelDataBlock, sizeof(ARM64_KERNEL_DATA));

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
    }

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
            return FALSE;
        }
    }

    if (!Arm64EnsureSharedUserDataMapped())
    {
        return FALSE;
    }

    return TRUE;
}
