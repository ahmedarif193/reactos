/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup setup
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include <smp.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern BOOLEAN HalpOnlyBootProcessor;
extern PPROCESSOR_IDENTITY HalpProcessorIdentity;
extern PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
extern PVOID HalpLowStub;
extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

/* Trampoline symbols (hal/halx86/smp/amd64/apentry.S) */
extern UCHAR HalpAPEntry16[];
extern UCHAR HalpAPEntry16End[];
extern UCHAR HalpAPEntryProt32[];
extern UCHAR HalpAPEntryLong64[];
extern UCHAR HalpAPEntryJump32[];
extern UCHAR HalpAPEntryJump64[];
extern UCHAR HalpAPEntryProcState[];
extern UCHAR HalpAPEntryHigh[];
extern UCHAR HalpAPEntryPmlRoot[];
extern UCHAR HalpAPEntryGdtr[];
extern UCHAR HalpAPEntryGdt[];
extern UCHAR HalpAPEntry64[];

ULONG HalpStartedProcessorCount = 1;

#define PTE_PRESENT_WRITE   0x003
#define PTE_LARGE_IDENTITY  0x083 /* Present | Write | PageSize, PFN 0 */

#define StubOffset(sym)     ((ULONG_PTR)(sym) - (ULONG_PTR)HalpAPEntry16)
#define StubVa(sym)         ((PUCHAR)HalpLowStub + StubOffset(sym))
#define StubPa(sym)         ((ULONG)(HalpLowStubPhysicalAddress.QuadPart + StubOffset(sym)))

/* FUNCTIONS *****************************************************************/

static
ULONG
HalpSetupTemporaryMappings(VOID)
{
    PULONG64 Pml4 = (PULONG64)((PUCHAR)HalpLowStub + 1 * PAGE_SIZE);
    PULONG64 Pdpt = (PULONG64)((PUCHAR)HalpLowStub + 2 * PAGE_SIZE);
    PULONG64 Pd = (PULONG64)((PUCHAR)HalpLowStub + 3 * PAGE_SIZE);
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Start from a copy of the running address space (kernel higher half) */
    RtlCopyMemory(Pml4, MiAddressToPxe((PVOID)0), PAGE_SIZE);

    /* Identity-map the low 2 MB so the real-mode/protected-mode stub runs */
    PhysicalAddress = MmGetPhysicalAddress(Pdpt);
    Pml4[0] = (PhysicalAddress.QuadPart & ~0xFFFULL) | PTE_PRESENT_WRITE;

    PhysicalAddress = MmGetPhysicalAddress(Pd);
    Pdpt[0] = (PhysicalAddress.QuadPart & ~0xFFFULL) | PTE_PRESENT_WRITE;

    Pd[0] = PTE_LARGE_IDENTITY;

    PhysicalAddress = MmGetPhysicalAddress(Pml4);
    ASSERT(PhysicalAddress.QuadPart < 0x100000000);
    return (ULONG)PhysicalAddress.QuadPart;
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    ULONG TemporaryCr3;

    UNREFERENCED_PARAMETER(LoaderBlock);

    /* Bail out if we only use the boot CPU */
    if (HalpOnlyBootProcessor)
        return FALSE;

    /* Bail out if we have started all available CPUs */
    if (HalpStartedProcessorCount == HalpApicInfoTable.ProcessorCount)
        return FALSE;

    /* Build the temporary page tables used during the mode transition */
    TemporaryCr3 = HalpSetupTemporaryMappings();
    if (!TemporaryCr3)
        return FALSE;

    /* Copy the trampoline into low memory */
    RtlCopyMemory(HalpLowStub, HalpAPEntry16, StubOffset(HalpAPEntry16End));

    /* Patch the far-jump targets (selectors are already baked in) */
    *(PULONG)StubVa(HalpAPEntryJump32) = StubPa(HalpAPEntryProt32);
    *(PULONG)StubVa(HalpAPEntryJump64) = StubPa(HalpAPEntryLong64);

    /* Patch the data consumed by the trampoline */
    *(PULONG64)StubVa(HalpAPEntryProcState) = (ULONG64)(ULONG_PTR)ProcessorState;
    *(PULONG64)StubVa(HalpAPEntryHigh) = (ULONG64)(ULONG_PTR)HalpAPEntry64;
    *(PULONG)StubVa(HalpAPEntryPmlRoot) = TemporaryCr3;

    /* Trampoline GDTR: 4 entries, base points at the copied GDT */
    *(PUSHORT)StubVa(HalpAPEntryGdtr) = (4 * sizeof(ULONG64)) - 1;
    *(PULONG)(StubVa(HalpAPEntryGdtr) + 2) = StubPa(HalpAPEntryGdt);

    /* Fire INIT-SIPI-SIPI */
    ApicStartApplicationProcessor(HalpStartedProcessorCount, HalpLowStubPhysicalAddress);

    HalpStartedProcessorCount++;

    return TRUE;
}
