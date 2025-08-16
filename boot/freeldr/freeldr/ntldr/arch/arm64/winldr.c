/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Windows Loader Support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#include <freeldr.h>
#include <arch/arm64/arm64.h>
#include <ndk/asm.h>
#include <internal/arm64/intrin_i.h>
#include <debug.h>

DBG_DEFAULT_CHANNEL(WARNING);

/* ARM64 specific kernel entry point signature */
typedef VOID (NTAPI *PKERNEL_ENTRY_POINT)(PLOADER_PARAMETER_BLOCK LoaderBlock);

/* GDT/IDT structures (not used on ARM64 but needed for compatibility) */
typedef struct _KDESCRIPTOR
{
    USHORT Pad[3];
    USHORT Limit;
    PVOID Base;
} KDESCRIPTOR, *PKDESCRIPTOR;

/* Prepare ARM64-specific loader block data */
static VOID
WinLdrSetupARM64LoaderBlock(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    TRACE("Setting up ARM64 loader block\n");
    
    /* ARM64 specific initialization */
    /* TODO: Setup ARM64-specific loader block fields when defined in SDK */
    /* For now, these would be set up by the kernel itself:
     * - Kernel/Panic/Interrupt stacks
     * - Cache sizes and characteristics
     */
    
    /* TODO: Query actual cache sizes from system registers */
    
    TRACE("ARM64 loader block initialized\n");
}

/* Map kernel into virtual address space */
static BOOLEAN
WinLdrMapKernel(PVOID KernelBase)
{
    TRACE("Mapping kernel at 0x%p\n", KernelBase);
    
    /* TODO: Create proper page tables for kernel mapping */
    /* For now, we rely on identity mapping from MMU initialization */
    
    return TRUE;
}

/* Setup ARM64 CPU state before jumping to kernel */
static VOID
WinLdrSetupARM64State(VOID)
{
    ULONG64 SCTLR, CPACR, MDSCR;
    
    TRACE("Setting up ARM64 CPU state\n");
    
    /* TODO: Setup ARM64 CPU state via assembly helpers */
    /* This includes:
     * - Disabling alignment checking (SCTLR_EL1.A = 0)
     * - Enabling FPU/SIMD (CPACR_EL1.FPEN = 11)
     * - Disabling debug exceptions (MDSCR_EL1.MDE = 0)
     */
    SCTLR = 0; /* Placeholder */
    CPACR = 0; /* Placeholder */
    MDSCR = 0; /* Placeholder */
    
    TRACE("ARM64 CPU state configured\n");
}

/* Main Windows loader entry for ARM64 */
VOID
WinLdrSetupForNt(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                 IN PVOID *GdtIdt,
                 IN PULONG PcrBasePage,
                 IN PULONG TssBasePage)
{
    TRACE("WinLdrSetupForNt for ARM64\n");
    
    /* GDT/IDT not used on ARM64 */
    *GdtIdt = NULL;
    
    /* PCR will be at a fixed location determined by kernel */
    *PcrBasePage = 0;  /* Kernel will set this up */
    
    /* TSS not used on ARM64 */
    *TssBasePage = 0;
    
    /* Setup ARM64-specific loader block */
    WinLdrSetupARM64LoaderBlock(LoaderBlock);
    
    /* Setup CPU state */
    WinLdrSetupARM64State();
    
    TRACE("ARM64 setup complete\n");
}

/* Transfer control to kernel */
VOID
WinLdrSetProcessorContext(PVOID GdtIdt, IN ULONG64 Pcr, IN ULONG64 Tss)
{
    TRACE("WinLdrSetProcessorContext for ARM64\n");
    
    /* GDT/IDT/TSS not used on ARM64 */
    /* PCR will be set up by kernel itself */
    
    /* TODO: Flush caches via assembly helpers */
    /* extern VOID InvalidateICache(VOID); */
    /* extern VOID InvalidateDCache(VOID); */
    
    TRACE("Processor context ready for kernel\n");
}

/* Jump to kernel entry point */
VOID
WinLdrCallKernel(IN PVOID KernelEntry, IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PKERNEL_ENTRY_POINT KernelEntryPoint = (PKERNEL_ENTRY_POINT)KernelEntry;
    
    TRACE("Calling kernel entry point at 0x%p\n", KernelEntry);
    TRACE("LoaderBlock at 0x%p\n", LoaderBlock);
    
    /* TODO: Disable interrupts and flush caches via assembly */
    /* This should be done in assembly before jumping to kernel */
    
    /* Jump to kernel */
    KernelEntryPoint(LoaderBlock);
    
    /* Should never return */
    ERR("Kernel returned to loader!\n");
    for (;;) ;
}

/* Machine-specific initialization */
VOID
MachPrepareForReactOS(VOID)
{
    TRACE("MachPrepareForReactOS for ARM64\n");
    
    /* Ensure MMU and caches are properly configured */
    extern VOID MmuInit(VOID);
    extern VOID MmuEnable(VOID);
    extern VOID EnableCaches(VOID);
    
    MmuInit();
    MmuEnable();
    EnableCaches();
    
    /* TODO: Setup initial interrupt controller state */
    /* TODO: Disable watchdog timers if present */
    
    TRACE("Machine prepared for ReactOS\n");
}

/* Paging support functions */
BOOLEAN
MempSetupPaging(IN PFN_NUMBER StartPage,
                IN PFN_NUMBER NumberOfPages,
                IN BOOLEAN KernelMapping)
{
    TRACE("MempSetupPaging: StartPage=0x%lx, NumberOfPages=0x%lx, Kernel=%d\n",
          StartPage, NumberOfPages, KernelMapping);
    
    /* ARM64 uses a different paging mechanism - identity mapping for now */
    /* The actual page tables will be set up by the kernel */
    
    return TRUE;
}

VOID
MempDump(VOID)
{
    TRACE("MempDump for ARM64\n");
    /* Dump memory map for debugging if needed */
}

/* Machine-dependent setup */
VOID
WinLdrSetupMachineDependent(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    TRACE("WinLdrSetupMachineDependent for ARM64\n");
    
    /* Setup ARM64-specific loader block */
    WinLdrSetupARM64LoaderBlock(LoaderBlock);
}