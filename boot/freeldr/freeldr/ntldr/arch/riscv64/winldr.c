/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 NT loader handoff and Sv39 bootstrap mappings
 */

#include <freeldr.h>
#include <ntldr/winldr.h>
#include <uefildr.h>
#include <Fdt.h>
#include <RiscVBoot.h>
#include <ndk/ketypes.h>
#include <reactos/riscv64/fdt.h>
#include <reactos/riscv64/fdtlib.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

#define RISCV64_PT_ENTRIES             512
#define RISCV64_PT_POOL_MARGIN         64
#define RISCV64_LEVEL0_SIZE            (1ULL << 12)
#define RISCV64_LEVEL1_SIZE            (1ULL << 21)
#define RISCV64_LEVEL2_SIZE            (1ULL << 30)
#define RISCV64_LEVEL0_SPAN_PAGES      (1ULL << 9)
#define RISCV64_LEVEL1_SPAN_PAGES      (1ULL << 18)

#define RISCV64_PTE_VALID              (1ULL << 0)
#define RISCV64_PTE_READ               (1ULL << 1)
#define RISCV64_PTE_WRITE              (1ULL << 2)
#define RISCV64_PTE_EXECUTE            (1ULL << 3)
#define RISCV64_PTE_USER               (1ULL << 4)
#define RISCV64_PTE_GLOBAL             (1ULL << 5)
#define RISCV64_PTE_ACCESSED           (1ULL << 6)
#define RISCV64_PTE_DIRTY              (1ULL << 7)
#define RISCV64_PTE_FLAGS_MASK         0x3FFULL
#define RISCV64_PTE_PPN_MASK           0x003FFFFFFFFFFC00ULL
#define RISCV64_PTE_PERMISSION_MASK    \
    (RISCV64_PTE_READ | RISCV64_PTE_WRITE | RISCV64_PTE_EXECUTE)
/* The loader itself runs from the identity map. */
#define RISCV64_PTE_BOOT_LEAF          \
    (RISCV64_PTE_VALID | RISCV64_PTE_READ | RISCV64_PTE_WRITE | \
     RISCV64_PTE_EXECUTE | RISCV64_PTE_ACCESSED | RISCV64_PTE_DIRTY)
/* Kernel data, the direct map and device windows are never executable. */
#define RISCV64_PTE_DATA_LEAF          \
    (RISCV64_PTE_VALID | RISCV64_PTE_READ | RISCV64_PTE_WRITE | \
     RISCV64_PTE_ACCESSED | RISCV64_PTE_DIRTY | RISCV64_PTE_GLOBAL)
#define RISCV64_PTE_DEVICE_LEAF        RISCV64_PTE_DATA_LEAF

/* Maximum leaf level accepted by RiscvMapRange. The identity map and the
 * direct map may use large leaves; KSEG0 must consist of level-0 leaves only
 * (RISCV64_LOADER_FLAG_KSEG0_4K). */
#define RISCV64_MAP_LEVEL_ANY          2
#define RISCV64_MAP_LEVEL_4K           0

typedef enum _RISCV64_MAP_RESULT
{
    RiscvMapOk,
    RiscvMapSmaller,
    RiscvMapConflict,
    RiscvMapNoTables
} RISCV64_MAP_RESULT;

typedef struct _RISCV64_LOADER_STATE
{
    PULONGLONG PageTablePool;
    PULONGLONG RootTable;
    ULONG TablesUsed;
    ULONG TablesAvailable;
    PVOID KernelStack;
    PVOID DpcStack;
    PVOID Pcr;
    PVOID Process;
    PVOID Thread;
    PVOID SharedUserDataPage;
    PVOID DeviceTree;
    ULONG DeviceTreeSize;
    ULONGLONG BootHartId;
    ULONGLONG BootProtocolRevision;
    ULONGLONG HighestMappedPhysicalAddress;
    ULONG EarlyConsoleInterface;
    ULONGLONG EarlyConsoleAddress;
    ULONGLONG EarlyConsoleLength;
    ULONG EarlyConsoleRegisterShift;
    ULONG EarlyConsoleRegisterWidth;
    ULONG EarlyDeviceRangeCount;
    RISCV64_EARLY_DEVICE_RANGE EarlyDeviceRanges[RISCV64_MAX_EARLY_DEVICE_RANGES];
    BOOLEAN SetupSucceeded;
    BOOLEAN TablesFinalized;
    BOOLEAN PagingEnabled;
} RISCV64_LOADER_STATE;

static RISCV64_LOADER_STATE RiscvLoaderState;
extern EFI_SYSTEM_TABLE *GlobalSystemTable;

/* 8250-class register files handled by the kernel's NS16550 driver. */
static const CHAR *const RiscvConsoleCompatibles[] =
{
    "ns16550a",
    "ns16550",
    "ns8250",
    "ns16450",
    "snps,dw-apb-uart"
};

static
ULONGLONG
RiscvLevelSize(
    _In_ ULONG Level)
{
    return 1ULL << (RISCV64_PAGE_SHIFT + 9 * Level);
}

static
BOOLEAN
RiscvPteIsLeaf(
    _In_ ULONGLONG Entry)
{
    return !!(Entry & (RISCV64_PTE_READ | RISCV64_PTE_WRITE | RISCV64_PTE_EXECUTE));
}

static
ULONGLONG
RiscvPteToPhysical(
    _In_ ULONGLONG Entry)
{
    return (Entry & RISCV64_PTE_PPN_MASK) << 2;
}

static
ULONGLONG
RiscvPhysicalToPte(
    _In_ ULONGLONG PhysicalAddress)
{
    return (PhysicalAddress >> RISCV64_PAGE_SHIFT) << 10;
}

static
BOOLEAN
RiscvPhysicalRangeSupported(
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONGLONG Size)
{
    return (Size != 0) &&
           (PhysicalAddress < RISCV64_MAX_PHYSICAL_ADDRESS) &&
           (Size <= RISCV64_MAX_PHYSICAL_ADDRESS - PhysicalAddress);
}

static
PVOID
RiscvAllocateResidentMemory(
    _In_ SIZE_T Size,
    _In_ TYPE_OF_MEMORY MemoryType,
    _In_z_ PCSTR Name)
{
    SIZE_T AllocationSize;
    PVOID Address;

    AllocationSize = ROUND_UP(Size, RISCV64_PAGE_SIZE);
    Address = MmAllocateMemoryWithType(AllocationSize, MemoryType);
    if (!Address)
    {
        ERR("RISC-V: unable to allocate %s (%Iu bytes)\n", Name, AllocationSize);
        return NULL;
    }

    if (((ULONG_PTR)Address & (RISCV64_PAGE_SIZE - 1)) ||
        !RiscvPhysicalRangeSupported((ULONG_PTR)Address, AllocationSize))
    {
        ERR("RISC-V: %s allocation %p..%p is outside the initial physical map\n",
            Name,
            Address,
            (PVOID)((ULONG_PTR)Address + AllocationSize - 1));
        return NULL;
    }

    RtlZeroMemory(Address, AllocationSize);
    return Address;
}

static
PULONGLONG
RiscvAllocateTable(VOID)
{
    PULONGLONG Table;

    if (!RiscvLoaderState.PageTablePool ||
        RiscvLoaderState.TablesUsed >= RiscvLoaderState.TablesAvailable)
    {
        return NULL;
    }

    Table = RiscvLoaderState.PageTablePool +
            RiscvLoaderState.TablesUsed * RISCV64_PT_ENTRIES;
    ++RiscvLoaderState.TablesUsed;
    RtlZeroMemory(Table, RISCV64_PAGE_SIZE);
    return Table;
}

/*
 * Size the page-table pool from the memory map. KSEG0 maps loader memory
 * with level-0 leaves only, so it needs at most one level-0 table per 512
 * pages and one level-1 table per 262144 pages (plus one for a partial
 * gigabyte). The direct map uses the largest leaves each run allows: it
 * needs the same level-1 tables and at most two level-0 tables per run for
 * the unaligned edges. The identity map only needs level-0 tables at the
 * 2 MiB windows containing a region boundary, bounded by the number of type
 * runs in the lookup table. The margin covers the root, the shared user data
 * page and device windows. The pool is one contiguous LoaderMemoryData block:
 * the kernel adopts the tables in use as live page tables and never frees it.
 */
static
BOOLEAN
RiscvAllocatePageTablePool(VOID)
{
    const ULONGLONG MaximumPages =
        RISCV64_MAX_PHYSICAL_ADDRESS >> RISCV64_PAGE_SHIFT;
    ULONGLONG PageCount = (ULONGLONG)MmGetHighestPhysicalPage() + 1;
    ULONGLONG Level0Tables;
    ULONGLONG Level1Tables;
    ULONGLONG IdentityTables;
    ULONGLONG PoolPages;
    ULONGLONG Runs = 1;
    PPAGE_LOOKUP_TABLE_ITEM MemoryMap;
    PFN_NUMBER NoEntries = 0;
    PFN_NUMBER Index;

    if (PageCount > MaximumPages)
        PageCount = MaximumPages;

    Level0Tables = (PageCount + RISCV64_LEVEL0_SPAN_PAGES - 1) /
                   RISCV64_LEVEL0_SPAN_PAGES;
    Level1Tables = (PageCount + RISCV64_LEVEL1_SPAN_PAGES - 1) /
                   RISCV64_LEVEL1_SPAN_PAGES + 1;

    MemoryMap = MmGetMemoryMap(&NoEntries);
    for (Index = 1; MemoryMap && Index < NoEntries; ++Index)
    {
        if (MemoryMap[Index].PageAllocated != MemoryMap[Index - 1].PageAllocated)
            ++Runs;
    }
    IdentityTables = (Runs + 1 < Level0Tables) ? Runs + 1 : Level0Tables;

    PoolPages = Level0Tables + 2 * Level1Tables + IdentityTables +
                2 * (Runs + 1) + RISCV64_PT_POOL_MARGIN;
    if (PoolPages > MAXULONG)
    {
        ERR("RISC-V: Sv39 page-table pool of 0x%llx pages is not representable\n",
            PoolPages);
        return FALSE;
    }

    RiscvLoaderState.PageTablePool = RiscvAllocateResidentMemory(
        (SIZE_T)(PoolPages << RISCV64_PAGE_SHIFT),
        LoaderMemoryData,
        "Sv39 page-table pool");
    if (!RiscvLoaderState.PageTablePool)
        return FALSE;

    RiscvLoaderState.TablesAvailable = (ULONG)PoolPages;
    TRACE("RISC-V: Sv39 page-table pool %p, %llu pages (RAM pages 0x%llx: "
          "L0 %llu, L1 %llu, identity %llu, margin %u)\n",
          RiscvLoaderState.PageTablePool,
          PoolPages,
          PageCount,
          Level0Tables,
          Level1Tables,
          IdentityTables,
          RISCV64_PT_POOL_MARGIN);
    return TRUE;
}

static
BOOLEAN
RiscvLeafMatches(
    _In_ ULONGLONG Entry,
    _In_ ULONGLONG VirtualAddress,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG Level,
    _In_ ULONGLONG Flags)
{
    ULONGLONG Size = RiscvLevelSize(Level);
    ULONGLONG ExistingPhysical;

    if (!(Entry & RISCV64_PTE_VALID) || !RiscvPteIsLeaf(Entry))
        return FALSE;

    ExistingPhysical = RiscvPteToPhysical(Entry) + (VirtualAddress & (Size - 1));
    return (ExistingPhysical == PhysicalAddress) &&
           ((Entry & RISCV64_PTE_FLAGS_MASK) == Flags);
}

static
RISCV64_MAP_RESULT
RiscvInstallLeaf(
    _In_ ULONGLONG VirtualAddress,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONG TargetLevel,
    _In_ ULONGLONG Flags)
{
    PULONGLONG Table = RiscvLoaderState.RootTable;
    LONG Level;

    for (Level = 2; Level > (LONG)TargetLevel; --Level)
    {
        ULONG Index = (ULONG)((VirtualAddress >>
                      (RISCV64_PAGE_SHIFT + 9 * Level)) & 0x1FF);
        ULONGLONG Entry = __atomic_load_n(&Table[Index], __ATOMIC_ACQUIRE);

        if (!(Entry & RISCV64_PTE_VALID))
        {
            PULONGLONG Child = RiscvAllocateTable();

            if (!Child)
                return RiscvMapNoTables;
            Entry = RiscvPhysicalToPte((ULONG_PTR)Child) | RISCV64_PTE_VALID;
            __atomic_store_n(&Table[Index], Entry, __ATOMIC_RELEASE);
        }
        else if (RiscvPteIsLeaf(Entry))
        {
            return RiscvLeafMatches(Entry,
                                    VirtualAddress,
                                    PhysicalAddress,
                                    Level,
                                    Flags) ? RiscvMapOk : RiscvMapConflict;
        }

        if ((Entry & (RISCV64_PTE_USER | RISCV64_PTE_GLOBAL |
                      RISCV64_PTE_ACCESSED | RISCV64_PTE_DIRTY)) ||
            !RiscvPhysicalRangeSupported(RiscvPteToPhysical(Entry),
                                         RISCV64_PAGE_SIZE))
        {
            return RiscvMapConflict;
        }
        Table = (PULONGLONG)(ULONG_PTR)RiscvPteToPhysical(Entry);
    }

    {
        ULONG Index = (ULONG)((VirtualAddress >>
                      (RISCV64_PAGE_SHIFT + 9 * TargetLevel)) & 0x1FF);
        ULONGLONG Entry = __atomic_load_n(&Table[Index], __ATOMIC_ACQUIRE);
        ULONGLONG NewEntry = RiscvPhysicalToPte(PhysicalAddress) | Flags;

        if (!(Entry & RISCV64_PTE_VALID))
        {
            __atomic_store_n(&Table[Index], NewEntry, __ATOMIC_RELEASE);
            return RiscvMapOk;
        }
        if (RiscvPteIsLeaf(Entry))
        {
            return RiscvLeafMatches(Entry,
                                    VirtualAddress,
                                    PhysicalAddress,
                                    TargetLevel,
                                    Flags) ? RiscvMapOk : RiscvMapConflict;
        }
        return RiscvMapSmaller;
    }
}

static
BOOLEAN
RiscvMapRange(
    _In_ ULONGLONG VirtualAddress,
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONGLONG Size,
    _In_ ULONGLONG Flags,
    _In_ ULONG MaximumLevel)
{
    while (Size != 0)
    {
        RISCV64_MAP_RESULT Result = RiscvMapSmaller;
        ULONGLONG ChunkSize = RISCV64_LEVEL0_SIZE;

        if (MaximumLevel >= 2 &&
            !(VirtualAddress & (RISCV64_LEVEL2_SIZE - 1)) &&
            !(PhysicalAddress & (RISCV64_LEVEL2_SIZE - 1)) &&
            Size >= RISCV64_LEVEL2_SIZE)
        {
            Result = RiscvInstallLeaf(VirtualAddress,
                                      PhysicalAddress,
                                      2,
                                      Flags);
            if (Result == RiscvMapOk)
                ChunkSize = RISCV64_LEVEL2_SIZE;
        }

        if (Result == RiscvMapSmaller &&
            MaximumLevel >= 1 &&
            !(VirtualAddress & (RISCV64_LEVEL1_SIZE - 1)) &&
            !(PhysicalAddress & (RISCV64_LEVEL1_SIZE - 1)) &&
            Size >= RISCV64_LEVEL1_SIZE)
        {
            Result = RiscvInstallLeaf(VirtualAddress,
                                      PhysicalAddress,
                                      1,
                                      Flags);
            if (Result == RiscvMapOk)
                ChunkSize = RISCV64_LEVEL1_SIZE;
        }

        if (Result == RiscvMapSmaller)
        {
            Result = RiscvInstallLeaf(VirtualAddress,
                                      PhysicalAddress,
                                      0,
                                      Flags);
            ChunkSize = RISCV64_LEVEL0_SIZE;
        }

        if (Result != RiscvMapOk)
        {
            ERR("RISC-V: Sv39 mapping failed (%u) VA 0x%llx PA 0x%llx size 0x%llx\n",
                Result,
                VirtualAddress,
                PhysicalAddress,
                Size);
            return FALSE;
        }

        VirtualAddress += ChunkSize;
        PhysicalAddress += ChunkSize;
        Size -= ChunkSize;
    }

    return TRUE;
}

static
BOOLEAN
RiscvTranslateAddress(
    _In_ ULONGLONG VirtualAddress,
    _Out_ PULONGLONG PhysicalAddress)
{
    PULONGLONG Table = RiscvLoaderState.RootTable;
    LONG Level;

    for (Level = 2; Level >= 0; --Level)
    {
        ULONG Index = (ULONG)((VirtualAddress >>
                      (RISCV64_PAGE_SHIFT + 9 * Level)) & 0x1FF);
        ULONGLONG Entry = __atomic_load_n(&Table[Index], __ATOMIC_ACQUIRE);

        if (!(Entry & RISCV64_PTE_VALID))
            return FALSE;
        if (RiscvPteIsLeaf(Entry))
        {
            ULONGLONG LevelSize = RiscvLevelSize(Level);

            *PhysicalAddress = RiscvPteToPhysical(Entry) +
                               (VirtualAddress & (LevelSize - 1));
            return TRUE;
        }
        if (Level == 0)
            return FALSE;
        Table = (PULONGLONG)(ULONG_PTR)RiscvPteToPhysical(Entry);
    }

    return FALSE;
}

static
BOOLEAN
RiscvValidateMapping(
    _In_ PVOID VirtualAddress,
    _In_ PVOID PhysicalAddress,
    _In_z_ PCSTR Name)
{
    ULONGLONG Translated;

    if (!RiscvTranslateAddress((ULONG_PTR)VirtualAddress, &Translated) ||
        Translated != (ULONG_PTR)PhysicalAddress)
    {
        ERR("RISC-V: %s mapping mismatch VA %p, expected PA %p\n",
            Name,
            VirtualAddress,
            PhysicalAddress);
        return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
RiscvDiscoverBootHart(VOID)
{
    EFI_GUID BootProtocolGuid = RISCV_EFI_BOOT_PROTOCOL_GUID;
    RISCV_EFI_BOOT_PROTOCOL *BootProtocol = NULL;
    EFI_STATUS Status;
    UINTN BootHartId;

    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
    {
        ERR("RISC-V: UEFI boot services are unavailable before hart discovery\n");
        return FALSE;
    }

    Status = GlobalSystemTable->BootServices->LocateProtocol(&BootProtocolGuid,
                                                              NULL,
                                                              (VOID **)&BootProtocol);
    if (EFI_ERROR(Status) || !BootProtocol || !BootProtocol->GetBootHartId ||
        BootProtocol->Revision < RISCV_EFI_BOOT_PROTOCOL_REVISION)
    {
        ERR("RISC-V: RISCV_EFI_BOOT_PROTOCOL is unavailable or too old (0x%llx)\n",
            (ULONGLONG)Status);
        return FALSE;
    }

    Status = BootProtocol->GetBootHartId(BootProtocol, &BootHartId);
    if (EFI_ERROR(Status))
    {
        ERR("RISC-V: GetBootHartId failed (0x%llx)\n", (ULONGLONG)Status);
        return FALSE;
    }

    RiscvLoaderState.BootHartId = BootHartId;
    RiscvLoaderState.BootProtocolRevision = BootProtocol->Revision;
    return TRUE;
}

static
BOOLEAN
RiscvCaptureDeviceTree(VOID)
{
    EFI_GUID DeviceTreeGuid = FDT_TABLE_GUID;
    const VOID *FirmwareDeviceTree = NULL;
    ULONG DeviceTreeSize;
    UINTN Index;

    if (!GlobalSystemTable || !GlobalSystemTable->ConfigurationTable)
    {
        ERR("RISC-V: UEFI configuration tables are unavailable\n");
        return FALSE;
    }

    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry =
            &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid,
                    &DeviceTreeGuid,
                    sizeof(DeviceTreeGuid)))
        {
            if (FirmwareDeviceTree != NULL)
            {
                ERR("RISC-V: multiple UEFI device-tree tables are ambiguous\n");
                return FALSE;
            }
            FirmwareDeviceTree = Entry->VendorTable;
        }
    }

    if (!RiscvFdtValidateHeader(FirmwareDeviceTree,
                                RISCV_FDT_MAXIMUM_SIZE,
                                &DeviceTreeSize))
    {
        ERR("RISC-V: UEFI did not provide a supported flattened device tree\n");
        return FALSE;
    }

    RiscvLoaderState.DeviceTree = RiscvAllocateResidentMemory(
        DeviceTreeSize,
        LoaderFirmwarePermanent,
        "flattened device tree");
    if (!RiscvLoaderState.DeviceTree)
        return FALSE;

    RtlCopyMemory(RiscvLoaderState.DeviceTree,
                  FirmwareDeviceTree,
                  DeviceTreeSize);
    if (!RiscvFdtValidateHeader(RiscvLoaderState.DeviceTree,
                                DeviceTreeSize,
                                NULL))
    {
        ERR("RISC-V: retained device-tree copy failed validation\n");
        return FALSE;
    }

    RiscvLoaderState.DeviceTreeSize = DeviceTreeSize;
    return TRUE;
}

static
BOOLEAN
RiscvAddEarlyDeviceRange(
    _In_ ULONGLONG PhysicalAddress,
    _In_ ULONGLONG Length)
{
    ULONGLONG Base = ROUND_DOWN(PhysicalAddress, RISCV64_PAGE_SIZE);
    ULONGLONG End = ROUND_UP(PhysicalAddress + Length, RISCV64_PAGE_SIZE);
    PRISCV64_EARLY_DEVICE_RANGE Range;

    if (RiscvLoaderState.EarlyDeviceRangeCount >=
        RISCV64_MAX_EARLY_DEVICE_RANGES)
    {
        return FALSE;
    }
    if (End <= Base || !RiscvPhysicalRangeSupported(Base, End - Base))
        return FALSE;

    Range = &RiscvLoaderState.EarlyDeviceRanges[RiscvLoaderState.EarlyDeviceRangeCount];
    Range->BaseAddress = Base;
    Range->Length = End - Base;
    ++RiscvLoaderState.EarlyDeviceRangeCount;
    return TRUE;
}

/*
 * Resolve /chosen/stdout-path from the retained device tree and
 * describe the console register file for the kernel. The UART is never
 * touched here; firmware owns its configuration. Absence of a usable
 * console is not an error: the kernel falls back to the SBI debug console.
 */
static
VOID
RiscvDiscoverEarlyConsole(VOID)
{
    RISCV_FDT Fdt;
    ULONG Chosen;
    ULONG Node;
    ULONG Parent;
    ULONG Length;
    ULONG Index;
    ULONG Value;
    ULONGLONG Address;
    ULONGLONG Size;
    const CHAR *Path;
    const VOID *Compatible;

    RiscvLoaderState.EarlyConsoleInterface = RISCV64_EARLY_CONSOLE_NONE;
    RiscvLoaderState.EarlyDeviceRangeCount = 0;

    if (!RiscvFdtOpen(RiscvLoaderState.DeviceTree,
                      RiscvLoaderState.DeviceTreeSize,
                      &Fdt))
    {
        TRACE("RISC-V: early console: device tree cannot be opened\n");
        return;
    }

    if (!RiscvFdtFindNode(&Fdt, "/chosen", &Chosen, NULL))
    {
        TRACE("RISC-V: early console: no /chosen node\n");
        return;
    }

    Path = RiscvFdtGetProperty(&Fdt, Chosen, "stdout-path", &Length);
    if (!Path || Length == 0 || Path[Length - 1] != '\0')
    {
        TRACE("RISC-V: early console: /chosen/stdout-path is absent\n");
        return;
    }

    if (!RiscvFdtResolvePath(&Fdt, Path, &Node, &Parent, NULL) ||
        Parent == RISCV_FDT_NO_NODE)
    {
        TRACE("RISC-V: early console: stdout-path '%s' does not resolve\n", Path);
        return;
    }

    Compatible = RiscvFdtGetProperty(&Fdt, Node, "compatible", &Length);
    for (Index = 0; Index < RTL_NUMBER_OF(RiscvConsoleCompatibles); ++Index)
    {
        if (RiscvFdtStringListContains(Compatible,
                                       Length,
                                       RiscvConsoleCompatibles[Index]))
        {
            break;
        }
    }
    if (Index == RTL_NUMBER_OF(RiscvConsoleCompatibles))
    {
        TRACE("RISC-V: early console: '%s' has no supported compatible\n", Path);
        return;
    }

    if (!RiscvFdtReadReg(&Fdt, Node, Parent, 0, &Address, &Size) || Size == 0)
    {
        TRACE("RISC-V: early console: '%s' has no usable reg\n", Path);
        return;
    }
    if (!RiscvPhysicalRangeSupported(Address, Size))
    {
        TRACE("RISC-V: early console: reg 0x%llx+0x%llx is outside the direct map\n",
              Address,
              Size);
        return;
    }

    RiscvLoaderState.EarlyConsoleRegisterShift = 0;
    if (RiscvFdtReadU32(&Fdt, Node, "reg-shift", &Value))
        RiscvLoaderState.EarlyConsoleRegisterShift = Value;

    RiscvLoaderState.EarlyConsoleRegisterWidth = 1;
    if (RiscvFdtReadU32(&Fdt, Node, "reg-io-width", &Value))
    {
        if (Value != 1 && Value != 2 && Value != 4)
        {
            TRACE("RISC-V: early console: unsupported reg-io-width %lu\n", Value);
            return;
        }
        RiscvLoaderState.EarlyConsoleRegisterWidth = Value;
    }

    if (!RiscvAddEarlyDeviceRange(Address, Size))
    {
        TRACE("RISC-V: early console: cannot record device range 0x%llx+0x%llx\n",
              Address,
              Size);
        return;
    }

    RiscvLoaderState.EarlyConsoleAddress = Address;
    RiscvLoaderState.EarlyConsoleLength = Size;
    RiscvLoaderState.EarlyConsoleInterface = RISCV64_EARLY_CONSOLE_NS16550;
    TRACE("RISC-V: early console: NS16550 '%s' PA 0x%llx+0x%llx shift %lu width %lu\n",
          Path,
          Address,
          Size,
          RiscvLoaderState.EarlyConsoleRegisterShift,
          RiscvLoaderState.EarlyConsoleRegisterWidth);
}

/* Map the early device windows into the direct map with 4 KiB leaves. They
 * are not RAM, so HighestMappedPhysicalAddress is deliberately untouched. */
static
BOOLEAN
RiscvMapEarlyDeviceRanges(VOID)
{
    ULONG Index;

    for (Index = 0; Index < RiscvLoaderState.EarlyDeviceRangeCount; ++Index)
    {
        PRISCV64_EARLY_DEVICE_RANGE Range =
            &RiscvLoaderState.EarlyDeviceRanges[Index];

        if (!RiscvMapRange(RISCV64_DIRECT_MAP_BASE + Range->BaseAddress,
                           Range->BaseAddress,
                           Range->Length,
                           RISCV64_PTE_DEVICE_LEAF,
                           RISCV64_MAP_LEVEL_4K))
        {
            ERR("RISC-V: early device range %lu (PA 0x%llx+0x%llx) not mapped\n",
                Index,
                Range->BaseAddress,
                Range->Length);
            return FALSE;
        }
        TRACE("RISC-V: early device range %lu PA 0x%llx+0x%llx -> VA 0x%llx\n",
              Index,
              Range->BaseAddress,
              Range->Length,
              RISCV64_DIRECT_MAP_BASE + Range->BaseAddress);
    }
    return TRUE;
}

static
VOID
RiscvFillLoaderBlock(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRISCV64_LOADER_BLOCK Payload = &LoaderBlock->u.Riscv64;

    RtlZeroMemory(Payload, sizeof(*Payload));
    Payload->Version = RISCV64_LOADER_BLOCK_VERSION;
    Payload->Size = sizeof(*Payload);
    Payload->Flags =
        RISCV64_LOADER_FLAG_BOOT_HART_VALID |
        RISCV64_LOADER_FLAG_SV39 |
        RISCV64_LOADER_FLAG_IDENTITY_MAP_ACTIVE |
        RISCV64_LOADER_FLAG_IMAGE_PROTECTIONS |
        RISCV64_LOADER_FLAG_DEVICE_TREE_VALID |
        RISCV64_LOADER_FLAG_KSEG0_4K;
    Payload->FirmwareBootProtocolRevision =
        RiscvLoaderState.BootProtocolRevision;
    Payload->BootHartId = RiscvLoaderState.BootHartId;
    Payload->PageTableRoot = (ULONG_PTR)RiscvLoaderState.RootTable;
    Payload->Satp =
        RISCV64_SV39_MODE |
        ((ULONG_PTR)RiscvLoaderState.RootTable >> RISCV64_PAGE_SHIFT);
    Payload->Kseg0Base = RISCV64_LOADER_KSEG0_BASE;
    Payload->DirectMapBase = RISCV64_DIRECT_MAP_BASE;
    Payload->PhysicalLimit = RISCV64_MAX_PHYSICAL_ADDRESS;
    Payload->PcrPage = (ULONG_PTR)PaToVa(RiscvLoaderState.Pcr);
    Payload->DpcStack = (ULONG_PTR)PaToVa(
        (PUCHAR)RiscvLoaderState.DpcStack + KERNEL_STACK_SIZE);
    Payload->SharedUserDataPage = (ULONG_PTR)RiscvLoaderState.SharedUserDataPage;
    Payload->DeviceTree = (ULONG_PTR)PaToVa(RiscvLoaderState.DeviceTree);
    Payload->DeviceTreeSize = RiscvLoaderState.DeviceTreeSize;

    Payload->EarlyConsoleInterface = RISCV64_EARLY_CONSOLE_NONE;
    if (RiscvLoaderState.EarlyConsoleInterface != RISCV64_EARLY_CONSOLE_NONE)
    {
        Payload->Flags |= RISCV64_LOADER_FLAG_EARLY_CONSOLE_VALID;
        Payload->EarlyConsoleInterface = RiscvLoaderState.EarlyConsoleInterface;
        Payload->EarlyConsoleAddress = RiscvLoaderState.EarlyConsoleAddress;
        Payload->EarlyConsoleLength = RiscvLoaderState.EarlyConsoleLength;
        Payload->EarlyConsoleRegisterShift =
            RiscvLoaderState.EarlyConsoleRegisterShift;
        Payload->EarlyConsoleRegisterWidth =
            RiscvLoaderState.EarlyConsoleRegisterWidth;
    }
}

static
BOOLEAN
RiscvInitializeHandoff(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    SIZE_T PcrSize;
    ULONG_PTR KernelStackTop;
    ULONG_PTR PcrAddress;

    RtlZeroMemory(&RiscvLoaderState, sizeof(RiscvLoaderState));
    if (!LoaderBlock || !RiscvDiscoverBootHart() ||
        !RiscvCaptureDeviceTree())
        return FALSE;

    if (!RiscvAllocatePageTablePool())
        return FALSE;

    RiscvLoaderState.RootTable = RiscvAllocateTable();
    if (!RiscvLoaderState.RootTable)
        return FALSE;

    RiscvDiscoverEarlyConsole();
    if (!RiscvMapEarlyDeviceRanges())
        return FALSE;

    RiscvLoaderState.KernelStack = RiscvAllocateResidentMemory(
        KERNEL_STACK_SIZE,
        LoaderStartupKernelStack,
        "kernel stack");
    RiscvLoaderState.DpcStack = RiscvAllocateResidentMemory(
        KERNEL_STACK_SIZE,
        LoaderStartupDpcStack,
        "DPC/interrupt stack");
    PcrSize = ROUND_UP(sizeof(KPCR), RISCV64_PAGE_SIZE);
    RiscvLoaderState.Pcr = RiscvAllocateResidentMemory(
        PcrSize,
        LoaderStartupPcrPage,
        "PCR");
    RiscvLoaderState.Process = RiscvAllocateResidentMemory(
        RISCV64_PAGE_SIZE,
        LoaderMemoryData,
        "initial process");
    RiscvLoaderState.Thread = RiscvAllocateResidentMemory(
        RISCV64_PAGE_SIZE,
        LoaderMemoryData,
        "initial thread");
    RiscvLoaderState.SharedUserDataPage = RiscvAllocateResidentMemory(
        RISCV64_PAGE_SIZE,
        LoaderStartupPcrPage,
        "shared user data");
    if (!RiscvLoaderState.KernelStack || !RiscvLoaderState.DpcStack ||
        !RiscvLoaderState.Pcr ||
        !RiscvLoaderState.Process || !RiscvLoaderState.Thread ||
        !RiscvLoaderState.SharedUserDataPage)
    {
        return FALSE;
    }

    /* The kernel view of the shared user data page lives at its fixed
     * address; the memory manager later maps it into every process. */
    if (!RiscvMapRange(KI_USER_SHARED_DATA,
                       (ULONG_PTR)RiscvLoaderState.SharedUserDataPage,
                       RISCV64_PAGE_SIZE,
                       RISCV64_PTE_DATA_LEAF,
                       RISCV64_MAP_LEVEL_4K))
    {
        return FALSE;
    }

    KernelStackTop = ROUND_DOWN((ULONG_PTR)RiscvLoaderState.KernelStack +
                                KERNEL_STACK_SIZE,
                                16);
    PcrAddress = (ULONG_PTR)RiscvLoaderState.Pcr;

    LoaderBlock->KernelStack = (ULONG_PTR)PaToVa((PVOID)KernelStackTop);
    LoaderBlock->Prcb = (ULONG_PTR)PaToVa((PVOID)(PcrAddress +
                                                   FIELD_OFFSET(KPCR, Prcb)));
    LoaderBlock->Process = (ULONG_PTR)PaToVa(RiscvLoaderState.Process);
    LoaderBlock->Thread = (ULONG_PTR)PaToVa(RiscvLoaderState.Thread);
#if (NTDDI_VERSION >= NTDDI_WIN8)
    LoaderBlock->KernelStackSize = KERNEL_STACK_SIZE;
#endif

    RiscvFillLoaderBlock(LoaderBlock);

    RiscvLoaderState.SetupSucceeded = TRUE;
    return TRUE;
}

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping)
{
    const ULONGLONG MaximumPage =
        RISCV64_MAX_PHYSICAL_ADDRESS >> RISCV64_PAGE_SHIFT;
    ULONGLONG PhysicalAddress;
    ULONGLONG Size;

    if (NumberOfPages == 0)
        return TRUE;
    if (!RiscvLoaderState.SetupSucceeded ||
        StartPage >= MaximumPage ||
        NumberOfPages > MaximumPage - StartPage)
    {
        ERR("RISC-V: page range 0x%llx + 0x%llx is not representable\n",
            (ULONGLONG)StartPage,
            (ULONGLONG)NumberOfPages);
        return FALSE;
    }

    PhysicalAddress = (ULONGLONG)StartPage << RISCV64_PAGE_SHIFT;
    Size = (ULONGLONG)NumberOfPages << RISCV64_PAGE_SHIFT;

    /* Keep virtual page zero invalid while satisfying the shared loader's
     * request to account for its descriptor. */
    if (PhysicalAddress == 0)
    {
        if (Size == RISCV64_PAGE_SIZE)
            return TRUE;
        PhysicalAddress += RISCV64_PAGE_SIZE;
        Size -= RISCV64_PAGE_SIZE;
    }

    /* The identity map is discarded by the kernel; large leaves are fine. */
    if (!RiscvMapRange(PhysicalAddress,
                       PhysicalAddress,
                       Size,
                       RISCV64_PTE_BOOT_LEAF,
                       RISCV64_MAP_LEVEL_ANY))
    {
        return FALSE;
    }

    /* KSEG0 is adopted by the kernel and must stay 4 KiB-granular. Boot
     * images receive their section protections before the handoff. */
    if (KernelMapping &&
        !RiscvMapRange(RISCV64_LOADER_KSEG0_BASE + PhysicalAddress,
                       PhysicalAddress,
                       Size,
                       RISCV64_PTE_DATA_LEAF,
                       RISCV64_MAP_LEVEL_4K))
    {
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
RiscvSplitLeaf(
    _Inout_ PULONGLONG EntryPointer,
    _In_ ULONG Level)
{
    ULONGLONG Entry = __atomic_load_n(EntryPointer, __ATOMIC_ACQUIRE);
    ULONGLONG ChildSize;
    ULONGLONG PhysicalAddress;
    ULONGLONG Flags;
    PULONGLONG Child;
    ULONG Index;

    if (Level == 0 || !RiscvPteIsLeaf(Entry))
        return FALSE;

    Child = RiscvAllocateTable();
    if (!Child)
        return FALSE;

    ChildSize = RiscvLevelSize(Level - 1);
    PhysicalAddress = RiscvPteToPhysical(Entry);
    Flags = Entry & RISCV64_PTE_FLAGS_MASK;
    for (Index = 0; Index < RISCV64_PT_ENTRIES; ++Index)
    {
        Child[Index] = RiscvPhysicalToPte(PhysicalAddress +
                                          Index * ChildSize) | Flags;
    }

    __atomic_store_n(EntryPointer,
                     RiscvPhysicalToPte((ULONG_PTR)Child) |
                     RISCV64_PTE_VALID,
                     __ATOMIC_RELEASE);
    return TRUE;
}

VOID
MempUnmapPage(
    _In_ PFN_NUMBER Page)
{
    ULONGLONG VirtualAddress = RISCV64_LOADER_KSEG0_BASE +
                               ((ULONGLONG)Page << RISCV64_PAGE_SHIFT);
    PULONGLONG Table = RiscvLoaderState.RootTable;
    LONG Level;

    if (!Table || Page >=
        (RISCV64_MAX_PHYSICAL_ADDRESS >> RISCV64_PAGE_SHIFT))
    {
        return;
    }

    for (Level = 2; Level >= 0; --Level)
    {
        ULONG Index = (ULONG)((VirtualAddress >>
                      (RISCV64_PAGE_SHIFT + 9 * Level)) & 0x1FF);
        ULONGLONG Entry = __atomic_load_n(&Table[Index], __ATOMIC_ACQUIRE);

        if (!(Entry & RISCV64_PTE_VALID))
            return;
        if (RiscvPteIsLeaf(Entry))
        {
            /* KSEG0 is 4 KiB-granular; the split is kept for safety. */
            if (Level != 0)
            {
                if (!RiscvSplitLeaf(&Table[Index], Level))
                    return;
                Entry = __atomic_load_n(&Table[Index], __ATOMIC_ACQUIRE);
                Table = (PULONGLONG)(ULONG_PTR)RiscvPteToPhysical(Entry);
                continue;
            }

            __atomic_store_n(&Table[Index], 0, __ATOMIC_RELEASE);
            if (RiscvLoaderState.PagingEnabled)
            {
                __asm__ __volatile__("sfence.vma %0, zero"
                                     :: "r"(VirtualAddress) : "memory");
            }
            return;
        }
        if (Level == 0)
            return;
        Table = (PULONGLONG)(ULONG_PTR)RiscvPteToPhysical(Entry);
    }
}

VOID
MempDump(VOID)
{
    TRACE("RISC-V: Sv39 root %p, tables %lu/%lu, highest PA 0x%llx\n",
          RiscvLoaderState.RootTable,
          RiscvLoaderState.TablesUsed,
          RiscvLoaderState.TablesAvailable,
          RiscvLoaderState.HighestMappedPhysicalAddress);
}

BOOLEAN
RiscvLoaderSetupSucceeded(VOID)
{
    return RiscvLoaderState.SetupSucceeded;
}

/* The memory manager tracks every RAM page except these and reaches each
 * tracked page, including its page tables, through the direct map. */
static
BOOLEAN
RiscvIsInvisibleType(
    _In_ TYPE_OF_MEMORY Type)
{
    return (Type == LoaderFirmwarePermanent) || (Type == LoaderSpecialMemory) ||
           (Type == LoaderHALCachedMemory) || (Type == LoaderBBTMemory) ||
           (Type == LoaderBad);
}

static
BOOLEAN
RiscvMapDirectMapRun(
    _In_ ULONGLONG Start,
    _In_ ULONGLONG Count)
{
    /* Physical page zero is not RAM the memory manager can use. */
    if (Start == 0)
    {
        ++Start;
        if (--Count == 0)
            return TRUE;
    }

    if (!RiscvMapRange(RISCV64_DIRECT_MAP_BASE + (Start << RISCV64_PAGE_SHIFT),
                       Start << RISCV64_PAGE_SHIFT,
                       Count << RISCV64_PAGE_SHIFT,
                       RISCV64_PTE_DATA_LEAF,
                       RISCV64_MAP_LEVEL_ANY))
    {
        return FALSE;
    }

    if (((Start + Count) << RISCV64_PAGE_SHIFT) - 1 >
        RiscvLoaderState.HighestMappedPhysicalAddress)
    {
        RiscvLoaderState.HighestMappedPhysicalAddress =
            ((Start + Count) << RISCV64_PAGE_SHIFT) - 1;
    }
    return TRUE;
}

/* Map every visible RAM page, including firmware-temporary ranges above the
 * loader allocation lookup table. Contiguous descriptors are coalesced so
 * the largest leaves can be used. WinLdrSetupMemoryLayout has already
 * converted the descriptor links to kernel virtual addresses; access their
 * physical backing until the new address space is installed. */
static
BOOLEAN
RiscvMapDirectMap(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY Head = &LoaderBlock->MemoryDescriptorListHead;
    PLIST_ENTRY Link = VaToPa(Head->Flink);
    const ULONGLONG MaximumPage = RISCV64_MAX_PHYSICAL_ADDRESS >> RISCV64_PAGE_SHIFT;
    ULONGLONG RunStart = 0, RunCount = 0, Mapped = 0;

    while (Link != Head)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Descriptor = CONTAINING_RECORD(Link, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);
        ULONGLONG Start = Descriptor->BasePage;
        ULONGLONG Count = Descriptor->PageCount;

        Link = VaToPa(Descriptor->ListEntry.Flink);
        if (RiscvIsInvisibleType(Descriptor->MemoryType) || !Count)
            continue;
        if (Start >= MaximumPage || Count > MaximumPage - Start)
        {
            ERR("RISC-V: descriptor 0x%llx + 0x%llx exceeds the direct map\n", Start, Count);
            return FALSE;
        }

        if (RunCount != 0 && RunStart + RunCount == Start)
        {
            RunCount += Count;
            continue;
        }
        if (RunCount != 0 && !RiscvMapDirectMapRun(RunStart, RunCount))
            return FALSE;
        Mapped += RunCount;
        RunStart = Start;
        RunCount = Count;
    }

    if (RunCount != 0 && !RiscvMapDirectMapRun(RunStart, RunCount))
        return FALSE;
    Mapped += RunCount;

    TRACE("RISC-V: direct-mapped 0x%llx RAM pages, highest PA 0x%llx\n",
          Mapped,
          RiscvLoaderState.HighestMappedPhysicalAddress);
    return TRUE;
}

static
BOOLEAN
RiscvSetKseg0Permissions(
    _In_ ULONGLONG VirtualAddress,
    _In_ ULONGLONG Permissions)
{
    PULONGLONG Table = RiscvLoaderState.RootTable;
    LONG Level;

    for (Level = 2; Level >= 0; --Level)
    {
        ULONG Index = (ULONG)((VirtualAddress >>
                      (RISCV64_PAGE_SHIFT + 9 * Level)) & 0x1FF);
        ULONGLONG Entry = __atomic_load_n(&Table[Index], __ATOMIC_ACQUIRE);

        if (!(Entry & RISCV64_PTE_VALID))
            return FALSE;
        if (RiscvPteIsLeaf(Entry))
        {
            if (Level != 0)
                return FALSE;
            Entry = (Entry & ~RISCV64_PTE_PERMISSION_MASK) | Permissions;
            __atomic_store_n(&Table[Index], Entry, __ATOMIC_RELEASE);
            return TRUE;
        }
        if (Level == 0)
            return FALSE;
        Table = (PULONGLONG)(ULONG_PTR)RiscvPteToPhysical(Entry);
    }

    return FALSE;
}

/* Give every boot image the protections of its sections: headers and pages
 * outside any section are read-only, and only code is executable. The
 * relocations and import bindings are complete at this point. An image
 * whose sections are not page aligned stays read/write/execute. */
static
BOOLEAN
RiscvProtectBootImages(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY Head = &LoaderBlock->LoadOrderListHead;
    PLIST_ENTRY Link = VaToPa(Head->Flink);
    ULONG Images = 0;

    while (Link != Head)
    {
        PLDR_DATA_TABLE_ENTRY Entry = CONTAINING_RECORD(Link, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        ULONGLONG ImageBase = (ULONG_PTR)Entry->DllBase;
        ULONGLONG Pages = ((ULONGLONG)Entry->SizeOfImage + RISCV64_PAGE_SIZE - 1) >> RISCV64_PAGE_SHIFT;
        PIMAGE_NT_HEADERS NtHeaders;
        PIMAGE_SECTION_HEADER Section;
        BOOLEAN Protect;
        ULONGLONG Page;
        ULONG Index;

        Link = VaToPa(Entry->InLoadOrderLinks.Flink);
        if (ImageBase < RISCV64_LOADER_KSEG0_BASE || Pages == 0 ||
            ImageBase - RISCV64_LOADER_KSEG0_BASE >= RISCV64_MAX_PHYSICAL_ADDRESS ||
            (ImageBase & (RISCV64_PAGE_SIZE - 1)))
        {
            continue;
        }

        NtHeaders = RtlImageNtHeader(VaToPa((PVOID)(ULONG_PTR)ImageBase));
        if (!NtHeaders)
            continue;
        Protect = NtHeaders->OptionalHeader.SectionAlignment >= RISCV64_PAGE_SIZE;

        for (Page = 0; Page < Pages; ++Page)
        {
            ULONGLONG PageStart = Page << RISCV64_PAGE_SHIFT;
            ULONGLONG Permissions = RISCV64_PTE_READ;

            if (!Protect)
            {
                Permissions = RISCV64_PTE_PERMISSION_MASK;
            }
            else
            {
                Section = IMAGE_FIRST_SECTION(NtHeaders);
                for (Index = 0; Index < NtHeaders->FileHeader.NumberOfSections; ++Index, ++Section)
                {
                    ULONGLONG Size = max(Section->SizeOfRawData, Section->Misc.VirtualSize);
                    ULONGLONG First = Section->VirtualAddress & ~(RISCV64_PAGE_SIZE - 1);
                    ULONGLONG Last = Section->VirtualAddress + Size;

                    if (Size == 0 || PageStart < First || PageStart >= Last)
                        continue;
                    if (Section->Characteristics & IMAGE_SCN_MEM_WRITE)
                        Permissions |= RISCV64_PTE_WRITE;
                    if (Section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
                        Permissions |= RISCV64_PTE_EXECUTE;
                }
            }

            if (!RiscvSetKseg0Permissions(ImageBase + PageStart, Permissions))
            {
                ERR("RISC-V: image page %llx is not a KSEG0 leaf\n", ImageBase + PageStart);
                return FALSE;
            }
        }
        ++Images;
    }

    TRACE("RISC-V: applied section protections to %lu boot images\n", Images);
    return TRUE;
}

BOOLEAN
RiscvFinalizePageTables(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG_PTR KernelStackPhysical;

    if (!RiscvLoaderState.SetupSucceeded || !LoaderBlock)
        return FALSE;
    if (!RiscvMapDirectMap(LoaderBlock) || !RiscvProtectBootImages(LoaderBlock))
        return FALSE;

    KernelStackPhysical = (ULONG_PTR)RiscvLoaderState.KernelStack +
                          KERNEL_STACK_SIZE - 16;
    if (!RiscvValidateMapping(PaToVa((PVOID)KernelStackPhysical),
                              (PVOID)KernelStackPhysical,
                              "kernel stack") ||
        !RiscvValidateMapping(
            PaToVa((PUCHAR)RiscvLoaderState.DpcStack + KERNEL_STACK_SIZE - 16),
            (PUCHAR)RiscvLoaderState.DpcStack + KERNEL_STACK_SIZE - 16,
            "DPC/interrupt stack") ||
        !RiscvValidateMapping(PaToVa(RiscvLoaderState.Pcr),
                              RiscvLoaderState.Pcr,
                              "PCR") ||
        !RiscvValidateMapping((PVOID)(ULONG_PTR)KI_USER_SHARED_DATA,
                              RiscvLoaderState.SharedUserDataPage,
                              "shared user data") ||
        !RiscvValidateMapping(PaToVa(RiscvLoaderState.DeviceTree),
                              RiscvLoaderState.DeviceTree,
                              "flattened device tree") ||
        !RiscvValidateMapping((PVOID)(ULONG_PTR)(RISCV64_DIRECT_MAP_BASE +
                                                 (ULONG_PTR)RiscvLoaderState.RootTable),
                              RiscvLoaderState.RootTable,
                              "page-table root") ||
        !RiscvValidateMapping(PaToVa(LoaderBlock),
                              LoaderBlock,
                              "loader block"))
    {
        return FALSE;
    }

    if (RiscvLoaderState.EarlyConsoleInterface != RISCV64_EARLY_CONSOLE_NONE &&
        !RiscvValidateMapping((PVOID)(ULONG_PTR)(RISCV64_DIRECT_MAP_BASE +
                                                 RiscvLoaderState.EarlyConsoleAddress),
                              (PVOID)(ULONG_PTR)RiscvLoaderState.EarlyConsoleAddress,
                              "early console"))
    {
        return FALSE;
    }

    LoaderBlock->u.Riscv64.HighestMappedPhysicalAddress =
        RiscvLoaderState.HighestMappedPhysicalAddress;
    __asm__ __volatile__("fence rw, rw" ::: "memory");
    RiscvLoaderState.TablesFinalized = TRUE;
    TRACE("RISC-V: Sv39 tables finalized, %lu/%lu used, highest RAM PA 0x%llx\n",
          RiscvLoaderState.TablesUsed,
          RiscvLoaderState.TablesAvailable,
          RiscvLoaderState.HighestMappedPhysicalAddress);
    return TRUE;
}

VOID
WinLdrSetupMachineDependent(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (!RiscvInitializeHandoff(LoaderBlock))
    {
        UiMessageBox("RISC-V loader handoff initialization failed.");
    }
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    ULONGLONG Satp;

    UNREFERENCED_PARAMETER(OperatingSystemVersion);
    if (!RiscvLoaderState.TablesFinalized)
    {
        for (;;)
            __asm__ __volatile__("wfi");
    }

    Satp = RISCV64_SV39_MODE |
           ((ULONG_PTR)RiscvLoaderState.RootTable >> RISCV64_PAGE_SHIFT);
    __asm__ __volatile__("csrci sstatus, 2\n\t"
                         "csrw sie, zero\n\t"
                         "sfence.vma zero, zero\n\t"
                         "csrw satp, %0\n\t"
                         "sfence.vma zero, zero\n\t"
                         "fence.i"
                         :: "r"(Satp) : "memory");
    RiscvLoaderState.PagingEnabled = TRUE;
}
