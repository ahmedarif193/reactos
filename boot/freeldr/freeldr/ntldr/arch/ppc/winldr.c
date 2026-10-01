/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC loader handoff: page tables, HTAB and BATs
 *
 * See the PPC_LOADER_BLOCK contract in sdk/include/reactos/arc/arc.h. While
 * the loader runs, Open Firmware owns the MMU and maps memory 1:1, so the
 * tables below are built with physical addresses and only activated by
 * PpcJumpToKernel, in real mode, right before entering the kernel.
 */

#include <freeldr.h>
#include <ntldr/winldr.h>
#include <ndk/ketypes.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

/* 64-bit PAE-format software page table entries. */
#define PPC_PTE_VALID          0x001ULL
#define PPC_PTE_WRITE          0x002ULL
#define PPC_PTE_OWNER          0x004ULL
#define PPC_PTE_WRITE_THROUGH  0x008ULL
#define PPC_PTE_CACHE_DISABLE  0x010ULL
#define PPC_PTE_ACCESSED       0x020ULL
#define PPC_PTE_DIRTY          0x040ULL
#define PPC_PTE_GLOBAL         0x100ULL
#define PPC_PTE_FRAME_MASK     0x000000FFFFFFF000ULL

/* Non-leaf entries: Valid | Write; supervisor leaves: cached, A and D set. */
#define PPC_PTE_TABLE          (PPC_PTE_VALID | PPC_PTE_WRITE)
#define PPC_PTE_KERNEL_LEAF    (PPC_PTE_VALID | PPC_PTE_WRITE | PPC_PTE_ACCESSED | PPC_PTE_DIRTY)

#define PPC_ROOT_ENTRIES       4
#define PPC_TABLE_ENTRIES      512
#define PPC_ROOT_SHIFT         30
#define PPC_L1_SHIFT           21
#define PPC_L0_SHIFT           12
#define PPC_BOOT_ROOT_INDEX    (PPC_LOADER_KSEG0_BASE >> PPC_ROOT_SHIFT)

/* A 256 KiB HTAB: 4096 PTEGs, HTABMASK 3. */
#define PPC_HTAB_SIZE          0x40000UL
#define PPC_HTAB_MASK          ((PPC_HTAB_SIZE >> 16) - 1)

/* Segment register: Kp = 1 so user-mode protection keys apply. */
#define PPC_SR_KP              0x20000000UL

/* BAT encodings (604): upper = BEPI | BL | Vs; lower = BRPN | WIMG | PP. */
#define PPC_BAT_VS             0x2UL
#define PPC_BAT_PP_RW          0x2UL
#define PPC_BAT_I              0x20UL
#define PPC_BAT_G              0x08UL
#define PPC_BAT_BL_256M        (0x7FFUL << 2)
#define PPC_BAT_BL_16M         (0x07FUL << 2)

#define PPC_KERNEL_MSR         (PPC_MSR_ME | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_ILE | PPC_MSR_LE | PPC_MSR_FP | PPC_MSR_RI)

/* Read by PpcEnterKernel (ppcjump.S) in real mode; keep the layout in sync. */
typedef struct _PPC_HANDOFF
{
    ULONG Sdr1;
    ULONG Sr[16];
    ULONG Ibat[8];      /* IBAT0U, IBAT0L, ... IBAT3L */
    ULONG Dbat[8];      /* DBAT0U, DBAT0L, ... DBAT3L */
    ULONG Msr;
    ULONG Entry;
    ULONG Toc;
    ULONG Stack;
    ULONG Argument;
} PPC_HANDOFF, *PPPC_HANDOFF;

C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Sr) == 4);
C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Ibat) == 68);
C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Dbat) == 100);
C_ASSERT(FIELD_OFFSET(PPC_HANDOFF, Msr) == 132);
C_ASSERT(sizeof(PPC_HANDOFF) == 152);

DECLSPEC_NORETURN VOID PpcEnterKernel(_In_ PPPC_HANDOFF Handoff);

typedef struct _PPC_LOADER_STATE
{
    PULONGLONG RootTable;
    PULONGLONG BootL1Table;
    PUCHAR TablePool;
    ULONG TablesAvailable;
    ULONG TablesUsed;
    PVOID Htab;
    PVOID KernelStack;
    PVOID DpcStack;
    PVOID PanicStack;
    PVOID Pcr;
    PVOID Process;
    PVOID Thread;
    PVOID SharedUserDataPage;
    ULONG HighestMappedPhysicalAddress;
    BOOLEAN SetupSucceeded;
    BOOLEAN TablesFinalized;
    PPC_HANDOFF Handoff;
} PPC_LOADER_STATE;

static PPC_LOADER_STATE PpcLoaderState;

static
PVOID
PpcAllocateResident(_In_ SIZE_T Size, _In_ TYPE_OF_MEMORY Type, _In_z_ PCSTR What)
{
    PVOID Memory = MmAllocateMemoryWithType(Size, Type);

    if (!Memory)
    {
        ERR("PPC: failed to allocate %s\n", What);
        return NULL;
    }
    if ((ULONG_PTR)Memory + Size > PPC_LOADER_KSEG0_SIZE)
    {
        ERR("PPC: %s at %p is outside KSEG0\n", What, Memory);
        return NULL;
    }
    RtlZeroMemory(Memory, Size);
    return Memory;
}

static
PULONGLONG
PpcAllocateTable(VOID)
{
    PULONGLONG Table;

    if (PpcLoaderState.TablesUsed >= PpcLoaderState.TablesAvailable)
    {
        ERR("PPC: page table pool exhausted (%lu tables)\n", PpcLoaderState.TablesAvailable);
        return NULL;
    }

    Table = (PULONGLONG)(PpcLoaderState.TablePool + PpcLoaderState.TablesUsed * PAGE_SIZE);
    PpcLoaderState.TablesUsed++;
    RtlZeroMemory(Table, PAGE_SIZE);
    return Table;
}

static
PULONGLONG
PpcGetLeafSlot(_In_ ULONG VirtualAddress, _In_ BOOLEAN Create)
{
    ULONG RootIndex = VirtualAddress >> PPC_ROOT_SHIFT;
    ULONG L1Index = (VirtualAddress >> PPC_L1_SHIFT) & (PPC_TABLE_ENTRIES - 1);
    ULONG L0Index = (VirtualAddress >> PPC_L0_SHIFT) & (PPC_TABLE_ENTRIES - 1);
    PULONGLONG L0Table;

    /* The contract lets the loader populate the boot slot only. */
    if (RootIndex != PPC_BOOT_ROOT_INDEX || !PpcLoaderState.BootL1Table)
        return NULL;

    if (!(PpcLoaderState.BootL1Table[L1Index] & PPC_PTE_VALID))
    {
        if (!Create)
            return NULL;
        L0Table = PpcAllocateTable();
        if (!L0Table)
            return NULL;
        PpcLoaderState.BootL1Table[L1Index] = ((ULONG_PTR)L0Table & PPC_PTE_FRAME_MASK) | PPC_PTE_TABLE;
    }

    L0Table = (PULONGLONG)(ULONG_PTR)(PpcLoaderState.BootL1Table[L1Index] & PPC_PTE_FRAME_MASK);
    return &L0Table[L0Index];
}

static
BOOLEAN
PpcMapPage(_In_ ULONG VirtualAddress, _In_ ULONG PhysicalAddress, _In_ ULONGLONG Flags)
{
    PULONGLONG Slot = PpcGetLeafSlot(VirtualAddress, TRUE);

    if (!Slot)
    {
        ERR("PPC: cannot map VA 0x%lx\n", VirtualAddress);
        return FALSE;
    }
    *Slot = ((ULONGLONG)PhysicalAddress & PPC_PTE_FRAME_MASK) | Flags;
    return TRUE;
}

BOOLEAN
MempSetupPaging(
    _In_ PFN_NUMBER StartPage,
    _In_ PFN_NUMBER NumberOfPages,
    _In_ BOOLEAN KernelMapping)
{
    PFN_NUMBER Page;

    /* The loader runs 1:1 under Open Firmware and needs no identity map. */
    if (!KernelMapping)
        return TRUE;

    if (!PpcLoaderState.SetupSucceeded)
        return FALSE;

    for (Page = StartPage; Page < StartPage + NumberOfPages; Page++)
    {
        ULONG Physical = Page << PAGE_SHIFT;

        if (Physical >= PPC_LOADER_KSEG0_SIZE)
        {
            ERR("PPC: page 0x%lx is outside KSEG0\n", Page);
            return FALSE;
        }
        if (!PpcMapPage(PPC_LOADER_KSEG0_BASE + Physical, Physical, PPC_PTE_KERNEL_LEAF))
            return FALSE;
        if (Physical + PAGE_SIZE - 1 > PpcLoaderState.HighestMappedPhysicalAddress)
            PpcLoaderState.HighestMappedPhysicalAddress = Physical + PAGE_SIZE - 1;
    }
    return TRUE;
}

VOID
MempUnmapPage(_In_ PFN_NUMBER Page)
{
    PULONGLONG Slot;

    if ((Page << PAGE_SHIFT) >= PPC_LOADER_KSEG0_SIZE)
        return;
    Slot = PpcGetLeafSlot(PPC_LOADER_KSEG0_BASE + (Page << PAGE_SHIFT), FALSE);
    if (Slot)
        *Slot = 0;
}

VOID
MempDump(VOID)
{
    TRACE("PPC: root %p, boot L1 %p, tables %lu/%lu, HTAB %p, highest PA 0x%lx\n", PpcLoaderState.RootTable, PpcLoaderState.BootL1Table, PpcLoaderState.TablesUsed, PpcLoaderState.TablesAvailable, PpcLoaderState.Htab, PpcLoaderState.HighestMappedPhysicalAddress);
}

BOOLEAN
PpcLoaderSetupSucceeded(VOID)
{
    return PpcLoaderState.SetupSucceeded;
}

static
VOID
PpcFillLoaderBlock(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPPC_LOADER_BLOCK Block = &LoaderBlock->u.PowerPC;
    ULONG Pvr;

    __asm__ __volatile__("mfpvr %0" : "=r"(Pvr));

    RtlZeroMemory(Block, sizeof(*Block));
    Block->Version = PPC_LOADER_BLOCK_VERSION;
    Block->Size = sizeof(*Block);
    Block->Flags = PPC_LOADER_FLAG_HASH_TABLE;
    Block->MachineType = OfwMachine.MachineType;
    Block->ProcessorVersion = Pvr;
    Block->ProcessorFrequency = OfwMachine.ProcessorFrequency;
    Block->BusFrequency = OfwMachine.BusFrequency;
    Block->TimebaseFrequency = OfwMachine.TimebaseFrequency;
    Block->DcacheLineSize = OfwMachine.DcacheLineSize;
    Block->IcacheLineSize = OfwMachine.IcacheLineSize;
    Block->DcacheSize = OfwMachine.DcacheSize;
    Block->IcacheSize = OfwMachine.IcacheSize;
    Block->HashTable = (ULONG_PTR)PpcLoaderState.Htab;
    Block->HashTableSize = PPC_HTAB_SIZE;
    Block->PageTableRoot = (ULONG_PTR)PpcLoaderState.RootTable;
    Block->Kseg0Base = PPC_LOADER_KSEG0_BASE;
    Block->Kseg0Size = PPC_LOADER_KSEG0_SIZE;
    Block->PcrPage = (ULONG_PTR)PpcLoaderState.Pcr;
    Block->PanicStack = (ULONG_PTR)PaToVa((PUCHAR)PpcLoaderState.PanicStack + KERNEL_STACK_SIZE);
    Block->DpcStack = (ULONG_PTR)PaToVa((PUCHAR)PpcLoaderState.DpcStack + KERNEL_STACK_SIZE);
    Block->SharedUserDataPage = (ULONG_PTR)PpcLoaderState.SharedUserDataPage;
    Block->IsaIoPhysicalBase = OfwMachine.IsaIoPhysicalBase;
    Block->IsaIoVirtualBase = OfwMachine.IsaIoPhysicalBase ? PPC_LOADER_IO_WINDOW_BASE : 0;
    Block->PciMemoryPhysicalBase = OfwMachine.PciMemoryPhysicalBase;
    Block->PciDmaOffset = OfwMachine.PciDmaOffset;
    if (OfwMachine.ConsolePort)
    {
        Block->Flags |= PPC_LOADER_FLAG_EARLY_CONSOLE;
        Block->EarlyConsolePort = OfwMachine.ConsolePort;
        Block->EarlyConsoleBaud = 115200;
    }
}

static
BOOLEAN
PpcInitializeHandoff(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG TablesNeeded, HtabRaw;
    ULONG_PTR KernelStackTop;

    RtlZeroMemory(&PpcLoaderState, sizeof(PpcLoaderState));

    /* One level-0 table per 2 MiB of KSEG0, plus the shared user data page
     * and some slack, all allocated up front so the pool itself is inside
     * the memory map before the final layout is built. */
    TablesNeeded = (MmGetHighestPhysicalPage() + PPC_TABLE_ENTRIES - 1) / PPC_TABLE_ENTRIES;
    if (TablesNeeded > PPC_LOADER_KSEG0_SIZE >> PPC_L1_SHIFT)
        TablesNeeded = PPC_LOADER_KSEG0_SIZE >> PPC_L1_SHIFT;
    TablesNeeded += 2 + 4;

    PpcLoaderState.TablePool = PpcAllocateResident(TablesNeeded * PAGE_SIZE, LoaderMemoryData, "page table pool");
    if (!PpcLoaderState.TablePool)
        return FALSE;
    PpcLoaderState.TablesAvailable = TablesNeeded;

    PpcLoaderState.RootTable = PpcAllocateTable();
    PpcLoaderState.BootL1Table = PpcAllocateTable();
    if (!PpcLoaderState.RootTable || !PpcLoaderState.BootL1Table)
        return FALSE;
    PpcLoaderState.RootTable[PPC_BOOT_ROOT_INDEX] = ((ULONG_PTR)PpcLoaderState.BootL1Table & PPC_PTE_FRAME_MASK) | PPC_PTE_TABLE;

    /* SDR1 requires the HTAB aligned to its size. */
    HtabRaw = (ULONG_PTR)PpcAllocateResident(2 * PPC_HTAB_SIZE, LoaderMemoryData, "hashed page table");
    if (!HtabRaw)
        return FALSE;
    PpcLoaderState.Htab = (PVOID)ALIGN_UP_BY(HtabRaw, PPC_HTAB_SIZE);

    PpcLoaderState.KernelStack = PpcAllocateResident(KERNEL_STACK_SIZE, LoaderStartupKernelStack, "kernel stack");
    PpcLoaderState.DpcStack = PpcAllocateResident(KERNEL_STACK_SIZE, LoaderStartupDpcStack, "DPC stack");
    PpcLoaderState.PanicStack = PpcAllocateResident(KERNEL_STACK_SIZE, LoaderStartupPanicStack, "panic stack");
    PpcLoaderState.Pcr = PpcAllocateResident(ROUND_UP(sizeof(KPCR), PAGE_SIZE), LoaderStartupPcrPage, "PCR");
    PpcLoaderState.Process = PpcAllocateResident(PAGE_SIZE, LoaderMemoryData, "initial process");
    PpcLoaderState.Thread = PpcAllocateResident(PAGE_SIZE, LoaderMemoryData, "initial thread");
    PpcLoaderState.SharedUserDataPage = PpcAllocateResident(PAGE_SIZE, LoaderStartupPcrPage, "shared user data");
    if (!PpcLoaderState.KernelStack || !PpcLoaderState.DpcStack || !PpcLoaderState.PanicStack || !PpcLoaderState.Pcr || !PpcLoaderState.Process || !PpcLoaderState.Thread || !PpcLoaderState.SharedUserDataPage)
        return FALSE;

    /* r1 and LoaderBlock->KernelStack are the same 16-aligned address; the
     * 64 bytes above it hold the linkage area the first callee writes to. */
    KernelStackTop = ROUND_DOWN((ULONG_PTR)PpcLoaderState.KernelStack + KERNEL_STACK_SIZE - 64, 16);
    LoaderBlock->KernelStack = (ULONG_PTR)PaToVa((PVOID)KernelStackTop);
    LoaderBlock->Prcb = (ULONG_PTR)PaToVa((PUCHAR)PpcLoaderState.Pcr + FIELD_OFFSET(KPCR, Prcb));
    LoaderBlock->Process = (ULONG_PTR)PaToVa(PpcLoaderState.Process);
    LoaderBlock->Thread = (ULONG_PTR)PaToVa(PpcLoaderState.Thread);
#if (NTDDI_VERSION >= NTDDI_WIN8)
    LoaderBlock->KernelStackSize = KERNEL_STACK_SIZE;
#endif

    PpcLoaderState.SetupSucceeded = TRUE;
    PpcFillLoaderBlock(LoaderBlock);
    return TRUE;
}

VOID
WinLdrSetupMachineDependent(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (!PpcInitializeHandoff(LoaderBlock))
        UiMessageBox("PowerPC loader handoff initialization failed.");
}

BOOLEAN
PpcFinalizePageTables(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    ULONG i;

    if (!PpcLoaderState.SetupSucceeded)
        return FALSE;

    /* The kernel view of KUSER_SHARED_DATA is inside the boot slot. */
    if (!PpcMapPage(KI_USER_SHARED_DATA, (ULONG_PTR)PpcLoaderState.SharedUserDataPage, PPC_PTE_KERNEL_LEAF))
        return FALSE;
    RtlZeroMemory(PpcLoaderState.SharedUserDataPage, PAGE_SIZE);

    LoaderBlock->u.PowerPC.HighestMappedPhysicalAddress = PpcLoaderState.HighestMappedPhysicalAddress;
    RtlZeroMemory(PpcLoaderState.Htab, PPC_HTAB_SIZE);

    /* MMU state loaded by PpcEnterKernel. */
    PpcLoaderState.Handoff.Sdr1 = (ULONG_PTR)PpcLoaderState.Htab | PPC_HTAB_MASK;
    for (i = 0; i < 8; i++)
        PpcLoaderState.Handoff.Sr[i] = PPC_SR_KP | PPC_BOOT_USER_VSID(i);
    for (i = 8; i < 16; i++)
        PpcLoaderState.Handoff.Sr[i] = PPC_SR_KP | PPC_KERNEL_VSID(i);

    RtlZeroMemory(PpcLoaderState.Handoff.Ibat, sizeof(PpcLoaderState.Handoff.Ibat));
    RtlZeroMemory(PpcLoaderState.Handoff.Dbat, sizeof(PpcLoaderState.Handoff.Dbat));
    /* KSEG0: VA 0x80000000 -> PA 0, 256 MiB, cacheable, supervisor RW. */
    PpcLoaderState.Handoff.Ibat[0] = PPC_LOADER_KSEG0_BASE | PPC_BAT_BL_256M | PPC_BAT_VS;
    PpcLoaderState.Handoff.Ibat[1] = 0 | PPC_BAT_PP_RW;
    PpcLoaderState.Handoff.Dbat[0] = PPC_LOADER_KSEG0_BASE | PPC_BAT_BL_256M | PPC_BAT_VS;
    PpcLoaderState.Handoff.Dbat[1] = 0 | PPC_BAT_PP_RW;
    /* I/O window: cache-inhibited and guarded. */
    if (OfwMachine.IsaIoPhysicalBase)
    {
        PpcLoaderState.Handoff.Dbat[2] = PPC_LOADER_IO_WINDOW_BASE | PPC_BAT_BL_16M | PPC_BAT_VS;
        PpcLoaderState.Handoff.Dbat[3] = OfwMachine.IsaIoPhysicalBase | PPC_BAT_I | PPC_BAT_G | PPC_BAT_PP_RW;
    }
    PpcLoaderState.Handoff.Msr = PPC_KERNEL_MSR;

    PpcLoaderState.TablesFinalized = TRUE;
    TRACE("PPC: tables finalized, %lu/%lu used, highest PA 0x%lx, SDR1 0x%lx\n", PpcLoaderState.TablesUsed, PpcLoaderState.TablesAvailable, PpcLoaderState.HighestMappedPhysicalAddress, PpcLoaderState.Handoff.Sdr1);
    return TRUE;
}

VOID
WinLdrSetProcessorContext(_In_ USHORT OperatingSystemVersion)
{
    /* The MMU switch happens in PpcJumpToKernel, after the last firmware call. */
    UNREFERENCED_PARAMETER(OperatingSystemVersion);
}

DECLSPEC_NORETURN
VOID
PpcJumpToKernel(
    _In_ ULONG_PTR KernelEntry,
    _In_ ULONG_PTR LoaderBlock,
    _In_ ULONG_PTR KernelStack)
{
    PLOADER_PARAMETER_BLOCK Block = VaToPa((PVOID)LoaderBlock);
    PULONG Descriptor = VaToPa((PVOID)KernelEntry);
    PLIST_ENTRY Entry;

    if (!PpcLoaderState.TablesFinalized)
    {
        ERR("PPC: page tables were not finalized\n");
        Reboot();
    }

    /* Entry is a {code, TOC} descriptor, both KSEG0 addresses. */
    PpcLoaderState.Handoff.Entry = Descriptor[0];
    PpcLoaderState.Handoff.Toc = Descriptor[1];
    PpcLoaderState.Handoff.Stack = KernelStack;
    PpcLoaderState.Handoff.Argument = LoaderBlock;

    TRACE("PPC: entering kernel at 0x%lx (TOC 0x%lx), loader block 0x%lx, stack 0x%lx\n", PpcLoaderState.Handoff.Entry, PpcLoaderState.Handoff.Toc, PpcLoaderState.Handoff.Argument, PpcLoaderState.Handoff.Stack);

    /* Make every loaded image visible to instruction fetch. */
    for (Entry = ((PLIST_ENTRY)VaToPa(Block->LoadOrderListHead.Flink)); Entry != &Block->LoadOrderListHead; Entry = VaToPa(Entry->Flink))
    {
        PLDR_DATA_TABLE_ENTRY Ldr = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        PpcFlushCacheRange(VaToPa(Ldr->DllBase), Ldr->SizeOfImage);
    }

    /* No firmware call is possible from here on. */
    OfwAvailable = FALSE;
    PpcFlushCacheRange(&PpcLoaderState, sizeof(PpcLoaderState));
    PpcEnterKernel(&PpcLoaderState.Handoff);
}
