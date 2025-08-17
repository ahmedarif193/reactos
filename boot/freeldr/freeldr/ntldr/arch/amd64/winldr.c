/*
 * PROJECT:         EFI Windows Loader
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            boot/freeldr/freeldr/arch/amd64/winldr.c
 * PURPOSE:         Memory related routines
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ***************************************************************/

#include <freeldr.h>
#include <ndk/asm.h>
#include <internal/amd64/intrin_i.h>
#include "../../winldr.h"

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

//extern ULONG LoaderPagesSpanned;

/* GLOBALS ***************************************************************/

PHARDWARE_PTE PxeBase;
//PHARDWARE_PTE HalPageTable;

PVOID GdtIdt;
PFN_NUMBER SharedUserDataPfn;
ULONG_PTR TssBasePage;

/* FUNCTIONS **************************************************************/

static
BOOLEAN
MempAllocatePageTables(VOID)
{
    // TRACE(">>> MempAllocatePageTables\n");
    ERR("MempAllocatePageTables: Entry\n");

    /* Allocate a page for the PML4 */
    PxeBase = MmAllocateMemoryWithType(PAGE_SIZE, LoaderMemoryData);
    ERR("MempAllocatePageTables: Allocated PxeBase=%p\n", PxeBase);
    if (!PxeBase)
    {
        ERR("failed to allocate PML4\n");
        return FALSE;
    }

    // FIXME: Physical PTEs = FirmwareTemporary ?

    /* Zero the PML4 */
    RtlZeroMemory(PxeBase, PAGE_SIZE);
    ERR("MempAllocatePageTables: Zeroed PML4\n");

    /* The page tables are located at 0xfffff68000000000
     * We create a recursive self mapping through all 4 levels at
     * virtual address 0xfffff6fb7dbedf68 */
    PxeBase[VAtoPXI(PXE_BASE)].Valid = 1;
    PxeBase[VAtoPXI(PXE_BASE)].Write = 1;
    PxeBase[VAtoPXI(PXE_BASE)].PageFrameNumber = PtrToPfn(PxeBase);
    ERR("MempAllocatePageTables: Set recursive mapping\n");

#ifdef UEFIBOOT
    /* In UEFI mode, we need identity mapping for the first 4GB to ensure code execution continues */
    ERR("MempAllocatePageTables: UEFI - Setting up identity mapping\n");
    
    /* Allocate a PDPT (Page Directory Pointer Table) */
    PHARDWARE_PTE PdptBase = MmAllocateMemoryWithType(PAGE_SIZE, LoaderMemoryData);
    if (PdptBase)
    {
        RtlZeroMemory(PdptBase, PAGE_SIZE);
        
        /* Map first PML4 entry to PDPT */
        PxeBase[0].Valid = 1;
        PxeBase[0].Write = 1;
        PxeBase[0].PageFrameNumber = PtrToPfn(PdptBase);
        
        /* Allocate PDs (Page Directories) for first 2GB */
        PHARDWARE_PTE PdBase0 = MmAllocateMemoryWithType(PAGE_SIZE, LoaderMemoryData);
        PHARDWARE_PTE PdBase1 = MmAllocateMemoryWithType(PAGE_SIZE, LoaderMemoryData);
        if (PdBase0 && PdBase1)
        {
            RtlZeroMemory(PdBase0, PAGE_SIZE);
            RtlZeroMemory(PdBase1, PAGE_SIZE);
            
            /* Map first two PDPT entries to PDs */
            PdptBase[0].Valid = 1;
            PdptBase[0].Write = 1;
            PdptBase[0].PageFrameNumber = PtrToPfn(PdBase0);
            
            PdptBase[1].Valid = 1;
            PdptBase[1].Write = 1;
            PdptBase[1].PageFrameNumber = PtrToPfn(PdBase1);
            
            /* Create identity mapping using 2MB pages for first 1GB */
            for (int i = 0; i < 512; i++)
            {
                PdBase0[i].Valid = 1;
                PdBase0[i].Write = 1;
                PdBase0[i].LargePage = 1;  /* 2MB page */
                PdBase0[i].PageFrameNumber = i * 512;  /* Each 2MB page = 512 * 4KB */
            }
            
            /* Create identity mapping using 2MB pages for second 1GB */
            for (int i = 0; i < 512; i++)
            {
                PdBase1[i].Valid = 1;
                PdBase1[i].Write = 1;
                PdBase1[i].LargePage = 1;  /* 2MB page */
                PdBase1[i].PageFrameNumber = (512 + i) * 512;  /* Continue from 1GB */
            }
            
            ERR("MempAllocatePageTables: UEFI - Identity mapped first 2GB with 2MB pages\n");
        }
    }
#endif

    // FIXME: map PDE's for hals memory mapping

    TRACE(">>> leave MempAllocatePageTables\n");
    ERR("MempAllocatePageTables: Success\n");

    return TRUE;
}

static
PHARDWARE_PTE
MempGetOrCreatePageDir(PHARDWARE_PTE PdeBase, ULONG Index)
{
    PHARDWARE_PTE SubDir;

    if (!PdeBase)
        return NULL;

    if (!PdeBase[Index].Valid)
    {
        SubDir = MmAllocateMemoryWithType(PAGE_SIZE, LoaderMemoryData);
        if (!SubDir)
            return NULL;
        RtlZeroMemory(SubDir, PAGE_SIZE);
        PdeBase[Index].PageFrameNumber = PtrToPfn(SubDir);
        PdeBase[Index].Valid = 1;
        PdeBase[Index].Write = 1;
    }
    else
    {
        SubDir = (PVOID)((ULONG64)(PdeBase[Index].PageFrameNumber) * PAGE_SIZE);
    }
    return SubDir;
}

static
BOOLEAN
MempMapSinglePage(ULONG64 VirtualAddress, ULONG64 PhysicalAddress)
{
    PHARDWARE_PTE PpeBase, PdeBase, PteBase;
    ULONG Index;

    PpeBase = MempGetOrCreatePageDir(PxeBase, VAtoPXI(VirtualAddress));
    PdeBase = MempGetOrCreatePageDir(PpeBase, VAtoPPI(VirtualAddress));
    PteBase = MempGetOrCreatePageDir(PdeBase, VAtoPDI(VirtualAddress));

    if (!PteBase)
    {
        ERR("!!!No Dir %p, %p, %p, %p\n", PxeBase, PpeBase, PdeBase, PteBase);
        return FALSE;
    }

    Index = VAtoPTI(VirtualAddress);
    if (PteBase[Index].Valid)
    {
        ERR("!!!Already mapped %ld\n", Index);
        return FALSE;
    }

    PteBase[Index].Valid = 1;
    PteBase[Index].Write = 1;
    PteBase[Index].PageFrameNumber = PhysicalAddress / PAGE_SIZE;

    return TRUE;
}

BOOLEAN
MempIsPageMapped(PVOID VirtualAddress)
{
    PHARDWARE_PTE PpeBase, PdeBase, PteBase;
    ULONG Index;

    Index = VAtoPXI(VirtualAddress);
    if (!PxeBase[Index].Valid)
        return FALSE;

    PpeBase = (PVOID)((ULONG64)(PxeBase[Index].PageFrameNumber) * PAGE_SIZE);
    Index = VAtoPPI(VirtualAddress);
    if (!PpeBase[Index].Valid)
        return FALSE;

    PdeBase = (PVOID)((ULONG64)(PpeBase[Index].PageFrameNumber) * PAGE_SIZE);
    Index = VAtoPDI(VirtualAddress);
    if (!PdeBase[Index].Valid)
        return FALSE;

    /* Check if this is a large page (2MB) */
    if (PdeBase[Index].LargePage)
    {
        /* It's a 2MB page, so it's mapped */
        return TRUE;
    }

    /* Otherwise check the PTE level for 4KB pages */
    PteBase = (PVOID)((ULONG64)(PdeBase[Index].PageFrameNumber) * PAGE_SIZE);
    Index = VAtoPTI(VirtualAddress);
    if (!PteBase[Index].Valid)
        return FALSE;

    return TRUE;
}

static
PFN_NUMBER
MempMapRangeOfPages(ULONG64 VirtualAddress, ULONG64 PhysicalAddress, PFN_NUMBER cPages)
{
    PFN_NUMBER i;

    for (i = 0; i < cPages; i++)
    {
        if (!MempMapSinglePage(VirtualAddress, PhysicalAddress))
        {
            ERR("Failed to map page %ld from %p to %p\n",
                    i, (PVOID)VirtualAddress, (PVOID)PhysicalAddress);
            return i;
        }
        VirtualAddress += PAGE_SIZE;
        PhysicalAddress += PAGE_SIZE;
    }
    return i;
}

BOOLEAN
MempSetupPaging(IN PFN_NUMBER StartPage,
                IN PFN_NUMBER NumberOfPages,
                IN BOOLEAN KernelMapping)
{
    TRACE(">>> MempSetupPaging(0x%lx, %ld, %p)\n",
            StartPage, NumberOfPages, StartPage * PAGE_SIZE + KSEG0_BASE);

#ifdef UEFIBOOT
    /* In UEFI mode, check if pages are already mapped (e.g., by 2MB pages) */
    BOOLEAN allMapped = TRUE;
    for (PFN_NUMBER i = 0; i < NumberOfPages; i++)
    {
        if (!MempIsPageMapped((PVOID)((StartPage + i) * PAGE_SIZE)))
        {
            allMapped = FALSE;
            break;
        }
    }
    
    if (allMapped)
    {
        TRACE("Pages already mapped, skipping identity mapping\n");
    }
    else
#endif
    {
        /* Identity mapping */
        if (MempMapRangeOfPages(StartPage * PAGE_SIZE,
                                StartPage * PAGE_SIZE,
                                NumberOfPages) != NumberOfPages)
        {
            ERR("Failed to map pages %ld, %ld\n",
                    StartPage, NumberOfPages);
            return FALSE;
        }
    }

    /* Kernel mapping */
    if (KernelMapping)
    {
        if (MempMapRangeOfPages(StartPage * PAGE_SIZE + KSEG0_BASE,
                                StartPage * PAGE_SIZE,
                                NumberOfPages) != NumberOfPages)
        {
            ERR("Failed to map pages %ld, %ld\n",
                    StartPage, NumberOfPages);
            return FALSE;
        }
    }

    return TRUE;
}

VOID
MempUnmapPage(PFN_NUMBER Page)
{
   // TRACE(">>> MempUnmapPage\n");
}

static
VOID
WinLdrpMapApic(VOID)
{
    BOOLEAN LocalAPIC;
    LARGE_INTEGER MsrValue;
    ULONG CpuInfo[4];
    ULONG64 APICAddress;

    TRACE(">>> WinLdrpMapApic\n");

    /* Check if we have a local APIC */
    __cpuid((int*)CpuInfo, 1);
    LocalAPIC = (((CpuInfo[3] >> 9) & 1) != 0);

    /* If there is no APIC, just return */
    if (!LocalAPIC)
    {
        WARN("No APIC found.\n");
        return;
    }

    /* Read the APIC Address */
    MsrValue.QuadPart = __readmsr(0x1B);
    APICAddress = (MsrValue.LowPart & 0xFFFFF000);

    TRACE("Local APIC detected at address 0x%x\n",
        APICAddress);

    /* Map it */
    MempMapSinglePage(APIC_BASE, APICAddress);
}

#ifdef UEFIBOOT
/* Simple wide string comparison for UEFI boot */
static BOOLEAN
WinLdrWideStringStartsWith(PCWSTR String, PCWSTR Prefix, SIZE_T PrefixLen)
{
    SIZE_T i;
    if (!String || !Prefix) return FALSE;
    for (i = 0; i < PrefixLen; i++)
    {
        if (String[i] == 0 || String[i] != Prefix[i])
            return FALSE;
    }
    return TRUE;
}

BOOLEAN
WinLdrSetupKernelVirtualMapping(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLDR_DATA_TABLE_ENTRY KernelDTE, HalDTE, CurrentDTE;
    PLIST_ENTRY NextEntry;
    ULONG64 PhysicalBase, VirtualBase;
    PFN_NUMBER NumPages;
    
    ERR("WinLdrSetupKernelVirtualMapping: Setting up kernel virtual mappings\n");
    
    /* Find kernel and HAL entries */
    KernelDTE = NULL;
    HalDTE = NULL;
    
    ERR("WinLdrSetupKernelVirtualMapping: LoaderBlock=%p\n", LoaderBlock);
    ERR("WinLdrSetupKernelVirtualMapping: LoadOrderListHead at %p\n", &LoaderBlock->LoadOrderListHead);
    
    NextEntry = LoaderBlock->LoadOrderListHead.Flink;
    ERR("WinLdrSetupKernelVirtualMapping: First entry at %p\n", NextEntry);
    
    /* Convert virtual to physical if needed */
    if ((ULONG64)NextEntry >= KSEG0_BASE)
    {
        NextEntry = (PLIST_ENTRY)((ULONG64)NextEntry - KSEG0_BASE);
        ERR("WinLdrSetupKernelVirtualMapping: Converted to physical: %p\n", NextEntry);
    }
    
    while (NextEntry != &LoaderBlock->LoadOrderListHead)
    {
        CurrentDTE = CONTAINING_RECORD(NextEntry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        ERR("WinLdrSetupKernelVirtualMapping: Checking DTE at %p\n", CurrentDTE);
        
        /* For UEFI, just use the first two DTEs as kernel and HAL */
        /* This is a simplification to avoid string comparison issues */
        if (!KernelDTE)
        {
            KernelDTE = CurrentDTE;
            ERR("WinLdrSetupKernelVirtualMapping: Using first DTE as kernel\n");
            ERR("  DllBase=%p, SizeOfImage=%x\n", CurrentDTE->DllBase, CurrentDTE->SizeOfImage);
        }
        else if (!HalDTE)
        {
            HalDTE = CurrentDTE;
            ERR("WinLdrSetupKernelVirtualMapping: Using second DTE as HAL\n");
            ERR("  DllBase=%p, SizeOfImage=%x\n", CurrentDTE->DllBase, CurrentDTE->SizeOfImage);
        }
        
        NextEntry = NextEntry->Flink;
        
        /* Convert virtual to physical if needed */
        if ((ULONG64)NextEntry >= KSEG0_BASE)
        {
            NextEntry = (PLIST_ENTRY)((ULONG64)NextEntry - KSEG0_BASE);
        }
        
        ERR("WinLdrSetupKernelVirtualMapping: Next entry at %p\n", NextEntry);
    }
    
    if (!KernelDTE)
    {
        ERR("WinLdrSetupKernelVirtualMapping: ERROR - Kernel DTE not found!\n");
        return FALSE;
    }
    
    /* Map the kernel at its virtual address */
    /* Kernel is loaded physically at 0x400000 but expects to run at 0xFFFFF80000400000 */
    /* Check if DllBase is already virtual (starts with 0xFFFFF) */
    if ((ULONG64)KernelDTE->DllBase >= KSEG0_BASE)
    {
        /* DllBase is already virtual, extract physical */
        VirtualBase = (ULONG64)KernelDTE->DllBase & ~(PAGE_SIZE - 1);
        PhysicalBase = VirtualBase - KSEG0_BASE;
    }
    else
    {
        /* DllBase is physical */
        PhysicalBase = (ULONG64)KernelDTE->DllBase & ~(PAGE_SIZE - 1);
        VirtualBase = KSEG0_BASE + PhysicalBase;
    }
    NumPages = (KernelDTE->SizeOfImage + PAGE_SIZE - 1) / PAGE_SIZE;
    
    ERR("WinLdrSetupKernelVirtualMapping: Mapping kernel:\n");
    ERR("  Physical: %llx, Virtual: %llx, Pages: %ld\n", PhysicalBase, VirtualBase, NumPages);
    
    if (MempMapRangeOfPages(VirtualBase, PhysicalBase, NumPages) != NumPages)
    {
        ERR("WinLdrSetupKernelVirtualMapping: Failed to map kernel!\n");
        return FALSE;
    }
    
    /* Map HAL if found */
    if (HalDTE)
    {
        /* Check if DllBase is already virtual */
        if ((ULONG64)HalDTE->DllBase >= KSEG0_BASE)
        {
            VirtualBase = (ULONG64)HalDTE->DllBase & ~(PAGE_SIZE - 1);
            PhysicalBase = VirtualBase - KSEG0_BASE;
        }
        else
        {
            PhysicalBase = (ULONG64)HalDTE->DllBase & ~(PAGE_SIZE - 1);
            VirtualBase = KSEG0_BASE + PhysicalBase;
        }
        NumPages = (HalDTE->SizeOfImage + PAGE_SIZE - 1) / PAGE_SIZE;
        
        ERR("WinLdrSetupKernelVirtualMapping: Mapping HAL:\n");
        ERR("  Physical: %llx, Virtual: %llx, Pages: %ld\n", PhysicalBase, VirtualBase, NumPages);
        
        if (MempMapRangeOfPages(VirtualBase, PhysicalBase, NumPages) != NumPages)
        {
            ERR("WinLdrSetupKernelVirtualMapping: Failed to map HAL!\n");
            return FALSE;
        }
    }
    
    /* Map all boot drivers */
    NextEntry = LoaderBlock->BootDriverListHead.Flink;
    while (NextEntry != &LoaderBlock->BootDriverListHead)
    {
        PBOOT_DRIVER_LIST_ENTRY BootDriver = CONTAINING_RECORD(NextEntry, BOOT_DRIVER_LIST_ENTRY, Link);
        CurrentDTE = BootDriver->LdrEntry;
        
        if (CurrentDTE && CurrentDTE->DllBase)
        {
            /* Check if DllBase is already virtual */
            if ((ULONG64)CurrentDTE->DllBase >= KSEG0_BASE)
            {
                VirtualBase = (ULONG64)CurrentDTE->DllBase & ~(PAGE_SIZE - 1);
                PhysicalBase = VirtualBase - KSEG0_BASE;
            }
            else
            {
                PhysicalBase = (ULONG64)CurrentDTE->DllBase & ~(PAGE_SIZE - 1);
                VirtualBase = KSEG0_BASE + PhysicalBase;
            }
            NumPages = (CurrentDTE->SizeOfImage + PAGE_SIZE - 1) / PAGE_SIZE;
            
            ERR("WinLdrSetupKernelVirtualMapping: Mapping driver %S\n", 
                CurrentDTE->BaseDllName.Buffer ? CurrentDTE->BaseDllName.Buffer : L"<unknown>");
            
            if (MempMapRangeOfPages(VirtualBase, PhysicalBase, NumPages) != NumPages)
            {
                ERR("WinLdrSetupKernelVirtualMapping: Failed to map driver!\n");
            }
        }
        
        NextEntry = NextEntry->Flink;
    }
    
    /* Map loader block at both physical and virtual addresses */
    PhysicalBase = (ULONG64)LoaderBlock & ~(PAGE_SIZE - 1);
    VirtualBase = KSEG0_BASE + PhysicalBase;
    NumPages = (sizeof(LOADER_PARAMETER_BLOCK) + PAGE_SIZE - 1) / PAGE_SIZE;
    
    ERR("WinLdrSetupKernelVirtualMapping: Mapping LoaderBlock:\n");
    ERR("  Physical: %llx, Virtual: %llx, Pages: %ld\n", PhysicalBase, VirtualBase, NumPages);
    
    if (MempMapRangeOfPages(VirtualBase, PhysicalBase, NumPages) != NumPages)
    {
        ERR("WinLdrSetupKernelVirtualMapping: Failed to map LoaderBlock!\n");
    }
    
    /* Ensure freeldr itself is identity-mapped - map more aggressively */
    ERR("WinLdrSetupKernelVirtualMapping: Ensuring comprehensive identity mapping\n");
    
    /* Map first 256MB to ensure all of freeldr and low memory is accessible */
    for (ULONG64 addr = 0; addr < 0x10000000; addr += PAGE_SIZE)
    {
        if (!MempIsPageMapped((PVOID)addr))
        {
            /* Map it identity */
            if (!MempMapSinglePage(addr, addr))
            {
                /* Only warn for critical regions */
                if (addr < 0x8000000)
                {
                    ERR("WinLdrSetupKernelVirtualMapping: WARNING - Failed to identity map %llx\n", addr);
                }
            }
        }
    }
    
    ERR("WinLdrSetupKernelVirtualMapping: Virtual mappings complete\n");
    return TRUE;
}
#endif

static
BOOLEAN
WinLdrMapSpecialPages(VOID)
{
    PHARDWARE_PTE PpeBase, PdeBase;

    /* Map KI_USER_SHARED_DATA */
    if (!MempMapSinglePage(KI_USER_SHARED_DATA, SharedUserDataPfn * PAGE_SIZE))
    {
        ERR("Could not map KI_USER_SHARED_DATA\n");
        return FALSE;
    }

    /* Map the APIC page */
    WinLdrpMapApic();

    /* Map the page tables for 4 MB HAL address space. */
    PpeBase = MempGetOrCreatePageDir(PxeBase, VAtoPXI(MM_HAL_VA_START));
    PdeBase = MempGetOrCreatePageDir(PpeBase, VAtoPPI(MM_HAL_VA_START));
    MempGetOrCreatePageDir(PdeBase, VAtoPDI(MM_HAL_VA_START));
    MempGetOrCreatePageDir(PdeBase, VAtoPDI(MM_HAL_VA_START + 2 * 1024 * 1024));

    return TRUE;
}

static
VOID
Amd64SetupGdt(PVOID GdtBase, ULONG64 TssBase)
{
    PKGDTENTRY64 Entry;
    KDESCRIPTOR GdtDesc;
    TRACE("Amd64SetupGdt(GdtBase = %p, TssBase = %p)\n", GdtBase, TssBase);

    /* Setup KGDT64_NULL */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_NULL);
    *(PULONG64)Entry = 0x0000000000000000ULL;

    /* Setup KGDT64_R0_CODE */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_R0_CODE);
    *(PULONG64)Entry = 0x00209b0000000000ULL;

    /* Setup KGDT64_R0_DATA */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_R0_DATA);
    *(PULONG64)Entry = 0x00cf93000000ffffULL;

    /* Setup KGDT64_R3_CMCODE */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_R3_CMCODE);
    *(PULONG64)Entry = 0x00cffb000000ffffULL;

    /* Setup KGDT64_R3_DATA */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_R3_DATA);
    *(PULONG64)Entry = 0x00cff3000000ffffULL;

    /* Setup KGDT64_R3_CODE */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_R3_CODE);
    *(PULONG64)Entry = 0x0020fb0000000000ULL;

    /* Setup KGDT64_R3_CMTEB */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_R3_CMTEB);
    *(PULONG64)Entry = 0xff40f3fd50003c00ULL;

    /* Setup TSS entry */
    Entry = KiGetGdtEntry(GdtBase, KGDT64_SYS_TSS);
    KiInitGdtEntry(Entry, TssBase, sizeof(KTSS), I386_TSS, 0);

    /* Setup GDT descriptor */
    GdtDesc.Base  = GdtBase;
    GdtDesc.Limit = NUM_GDT * sizeof(KGDTENTRY) - 1;

    /* Set the new Gdt */
    __lgdt(&GdtDesc.Limit);
    TRACE("Leave Amd64SetupGdt()\n");
}

static
VOID
Amd64SetupIdt(PVOID IdtBase)
{
    KDESCRIPTOR IdtDesc, OldIdt;
    //ULONG Size;
    TRACE("Amd64SetupIdt(IdtBase = %p)\n", IdtBase);

    /* Get old IDT */
    __sidt(&OldIdt.Limit);

    /* Copy the old IDT */
    //Size =  min(OldIdt.Limit + 1, NUM_IDT * sizeof(KIDTENTRY));
    //RtlCopyMemory(IdtBase, (PVOID)OldIdt.Base, Size);

    /* Setup the new IDT descriptor */
    IdtDesc.Base = IdtBase;
    IdtDesc.Limit = NUM_IDT * sizeof(KIDTENTRY) - 1;

    /* Set the new IDT */
    __lidt(&IdtDesc.Limit);
    TRACE("Leave Amd64SetupIdt()\n");
}

VOID
WinLdrSetProcessorContext(
    _In_ USHORT OperatingSystemVersion)
{
    TRACE("WinLdrSetProcessorContext\n");
    ERR("WinLdrSetProcessorContext: Entry\n");

    /* Disable Interrupts */
    ERR("WinLdrSetProcessorContext: Disabling interrupts\n");
    _disable();

    /* Re-initialize EFLAGS */
    ERR("WinLdrSetProcessorContext: Reset EFLAGS\n");
    __writeeflags(0);

    /* Set the new PML4 */
    ERR("WinLdrSetProcessorContext: PxeBase=%p\n", PxeBase);
    if (!PxeBase)
    {
        ERR("WinLdrSetProcessorContext: ERROR - PxeBase is NULL!\n");
        return;
    }
    
#ifdef UEFIBOOT
    /* In UEFI mode, we might need to ensure the page tables are identity mapped first */
    ERR("WinLdrSetProcessorContext: UEFI - Checking if PxeBase is valid\n");
    
    /* Try to read from PxeBase to verify it's accessible */
    volatile ULONG64 test = *(ULONG64*)PxeBase;
    ERR("WinLdrSetProcessorContext: PxeBase first entry = %llx\n", test);
    
    /* Get current CR3 for comparison */
    ULONG64 oldCr3 = __readcr3();
    ERR("WinLdrSetProcessorContext: Current CR3 = %llx\n", oldCr3);
    
    /* Get current RIP to see where we're executing from */
    void* currentAddr = WinLdrSetProcessorContext;
    ERR("WinLdrSetProcessorContext: Current function at %p\n", currentAddr);
    
    /* Check if we're in low memory that should be identity mapped */
    if ((ULONG_PTR)currentAddr > 0x40000000)  /* 1GB */
    {
        ERR("WinLdrSetProcessorContext: WARNING - Code at %p is not in identity mapped region!\n", currentAddr);
        /* For now, continue without changing CR3 in UEFI mode */
    }
#endif
    
#ifdef UEFIBOOT
    /* In UEFI mode, we don't switch CR3 here - we'll do it later after setting up virtual mappings */
    ERR("WinLdrSetProcessorContext: UEFI - Deferring CR3 change until after virtual mapping setup\n");
#else
    ERR("WinLdrSetProcessorContext: Setting CR3 to %llx\n", (ULONG64)PxeBase);
    __writecr3((ULONG64)PxeBase);
    ERR("WinLdrSetProcessorContext: CR3 set\n");
#endif

    /* Get kernel mode address of gdt / idt */
    ERR("WinLdrSetProcessorContext: GdtIdt=%p\n", GdtIdt);
#ifdef UEFIBOOT
    /* In UEFI mode, don't add virtual base since we're not using virtual addresses yet */
    ERR("WinLdrSetProcessorContext: UEFI - Using physical GdtIdt\n");
#else
    GdtIdt = (PVOID)((ULONG64)GdtIdt + KSEG0_BASE);
    ERR("WinLdrSetProcessorContext: GdtIdt(virtual)=%p\n", GdtIdt);
#endif

    /* Create gdt entries and load gdtr */
    ERR("WinLdrSetProcessorContext: Setting up GDT\n");
#ifdef UEFIBOOT
    /* Use physical address for TSS in UEFI mode */
    Amd64SetupGdt(GdtIdt, (TssBasePage << MM_PAGE_SHIFT));
#else
    Amd64SetupGdt(GdtIdt, KSEG0_BASE | (TssBasePage << MM_PAGE_SHIFT));
#endif
    ERR("WinLdrSetProcessorContext: GDT set\n");

    /* Copy old Idt and set idtr */
    ERR("WinLdrSetProcessorContext: Setting up IDT\n");
    Amd64SetupIdt((PVOID)((ULONG64)GdtIdt + NUM_GDT * sizeof(KGDTENTRY)));
    ERR("WinLdrSetProcessorContext: IDT set\n");

    /* LDT is unused */
//    __lldt(0);

    /* Load TSR */
    ERR("WinLdrSetProcessorContext: Loading TSS\n");
    __ltr(KGDT64_SYS_TSS);
    ERR("WinLdrSetProcessorContext: TSS loaded\n");

    TRACE("leave WinLdrSetProcessorContext\n");
    ERR("WinLdrSetProcessorContext: Exit\n");
}

void WinLdrSetupMachineDependent(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PVOID SharedUserDataAddress = NULL;
    ULONG_PTR Tss = 0;
    ULONG BlockSize, NumPages;

    LoaderBlock->u.I386.CommonDataArea = (PVOID)DbgPrint; // HACK
    LoaderBlock->u.I386.MachineType = MACHINE_TYPE_ISA;

    /* Allocate 1 page for SharedUserData */
    SharedUserDataAddress = MmAllocateMemoryWithType(MM_PAGE_SIZE, LoaderStartupPcrPage);
    SharedUserDataPfn = (ULONG_PTR)SharedUserDataAddress >> MM_PAGE_SHIFT;
    if (SharedUserDataAddress == NULL)
    {
        UiMessageBox("Can't allocate SharedUserData page.");
        return;
    }
    RtlZeroMemory(SharedUserDataAddress, MM_PAGE_SIZE);

    /* Allocate TSS */
    BlockSize = (sizeof(KTSS) + MM_PAGE_SIZE) & ~(MM_PAGE_SIZE - 1);
    Tss = (ULONG_PTR)MmAllocateMemoryWithType(BlockSize, LoaderMemoryData);
    TssBasePage = Tss >> MM_PAGE_SHIFT;

    /* Allocate space for new GDT + IDT */
    BlockSize = NUM_GDT * sizeof(KGDTENTRY) + NUM_IDT * sizeof(KIDTENTRY);
    NumPages = (BlockSize + MM_PAGE_SIZE - 1) >> MM_PAGE_SHIFT;
    GdtIdt = (PKGDTENTRY)MmAllocateMemoryWithType(NumPages * MM_PAGE_SIZE, LoaderMemoryData);
    if (GdtIdt == NULL)
    {
        UiMessageBox("Can't allocate pages for GDT+IDT!");
        return;
    }

    /* Zero newly prepared GDT+IDT */
    RtlZeroMemory(GdtIdt, NumPages << MM_PAGE_SHIFT);

    // Before we start mapping pages, create a block of memory, which will contain
    // PDE and PTEs
    if (MempAllocatePageTables() == FALSE)
    {
        // FIXME: bugcheck
    }

    /* Map KI_USER_SHARED_DATA, Apic and HAL space */
    WinLdrMapSpecialPages();
}


VOID
MempDump(VOID)
{
}
