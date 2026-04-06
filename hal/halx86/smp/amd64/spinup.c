/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup setup
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2025 ReactOS Team
 *
 * DESCRIPTION:
 *     Prepares the environment for AP startup and triggers INIT/SIPI.
 *
 *     The low stub (5 pages below 1MB) is laid out as:
 *       Page 0: Trampoline code (from apentry.S) + data + 64-bit bridge
 *       Page 1: Temporary PML4
 *       Page 2: Temporary PDPT
 *       Page 3: Temporary PD (identity maps first 1GB with 2MB pages)
 *       Page 4: Reserved
 *
 *     The trampoline transitions from real mode to long mode using the
 *     temporary page tables, then the bridge switches to the kernel's
 *     page tables and jumps to HalpAPEntry64 (in the HAL's .text section).
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

/*
 * Assembly symbols from apentry.S.
 * These are link-time addresses of the trampoline code and data fields.
 */
extern PVOID HalpAPEntry16;
extern PVOID HalpAPEntryData;
extern PVOID HalpAPEntry64;
extern PVOID HalpAPEntry16End;

extern HALP_APIC_INFO_TABLE HalpApicInfoTable;

ULONG HalpStartedProcessorCount = 1;

#ifndef Add2Ptr
#define Add2Ptr(P,I) ((PVOID)((PUCHAR)(P) + (I)))
#endif

/*
 * AP_ENTRY_DATA: Data block layout within the low stub.
 * Must match the HalpAPEntryData section of apentry.S exactly.
 * Written by spinup.c, read by the 64-bit bridge and HalpAPEntry64.
 *
 * Located at the offset of HalpAPEntryData relative to HalpAPEntry16.
 */
typedef struct _AP_ENTRY_DATA
{
    PVOID ProcessorState;           /* +0x00: PKPROCESSOR_STATE (kernel VA) */
    ULONG64 Entry64VA;              /* +0x08: Kernel VA of HalpAPEntry64 */
    ULONG64 KernelCr3;             /* +0x10: Kernel CR3 value */
} AP_ENTRY_DATA, *PAP_ENTRY_DATA;

/*
 * AMD64 hardware page table entry for building temporary identity mappings.
 * Uses 2MB large pages to minimize the number of page table levels.
 */
typedef struct _HW_PTE64
{
    ULONG64 Valid       : 1;
    ULONG64 Write       : 1;
    ULONG64 User        : 1;
    ULONG64 WriteThru   : 1;
    ULONG64 CacheDisable: 1;
    ULONG64 Accessed    : 1;
    ULONG64 Dirty       : 1;
    ULONG64 LargePage   : 1;
    ULONG64 Global      : 1;
    ULONG64 Available   : 3;
    ULONG64 PageFrame   : 40;
    ULONG64 Reserved    : 11;
    ULONG64 NoExecute   : 1;
} HW_PTE64, *PHW_PTE64;

/* FUNCTIONS *****************************************************************/

/*
 * Builds temporary 4-level page tables in the low stub.
 * Identity maps the first 1GB using 2MB large pages so the AP can
 * execute with paging enabled before switching to the kernel's page tables.
 *
 * Returns the physical address of the PML4.
 */
static
ULONG
HalpSetupTemporaryMappings(VOID)
{
    PHW_PTE64 Pml4, Pdpt, Pd;
    ULONG64 LowStubPhys;
    ULONG PdptPhys, PdPhys;

    Pml4 = (PHW_PTE64)Add2Ptr(HalpLowStub, 1 * PAGE_SIZE);
    Pdpt = (PHW_PTE64)Add2Ptr(HalpLowStub, 2 * PAGE_SIZE);
    Pd   = (PHW_PTE64)Add2Ptr(HalpLowStub, 3 * PAGE_SIZE);

    /*
     * Copy the kernel's PML4 so that the temp tables include both
     * identity-mapped low memory (PML4[0]) AND the kernel higher-half
     * mappings (PML4[256-511]).  This mirrors the i386 approach in
     * hal/halx86/smp/i386/spinup.c which copies MiAddressToPde(NULL).
     */
    RtlCopyMemory(Pml4, (PVOID)PXE_BASE, PAGE_SIZE);
    RtlZeroMemory(Pdpt, PAGE_SIZE);
    RtlZeroMemory(Pd, PAGE_SIZE);

    LowStubPhys = HalpLowStubPhysicalAddress.QuadPart;
    PdptPhys = (ULONG)(LowStubPhys + 2 * PAGE_SIZE);
    PdPhys   = (ULONG)(LowStubPhys + 3 * PAGE_SIZE);

    /* PML4[0] -> PDPT (covers VA 0 - 512GB) */
    Pml4[0].Valid = 1;
    Pml4[0].Write = 1;
    Pml4[0].PageFrame = PdptPhys >> PAGE_SHIFT;

    /* PDPT[0] -> PD (covers VA 0 - 1GB) */
    Pdpt[0].Valid = 1;
    Pdpt[0].Write = 1;
    Pdpt[0].PageFrame = PdPhys >> PAGE_SHIFT;

    /* PD: 512 entries * 2MB = 1GB identity map */
    for (ULONG i = 0; i < 512; i++)
    {
        Pd[i].Valid = 1;
        Pd[i].Write = 1;
        Pd[i].LargePage = 1;
        Pd[i].PageFrame = i * (0x200000 >> PAGE_SHIFT);
    }

    ASSERT(LowStubPhys + PAGE_SIZE < 0x100000000ULL);
    return (ULONG)(LowStubPhys + PAGE_SIZE);
}

/*
 * Generates a small 64-bit machine code stub ("bridge") that runs from the
 * identity-mapped low memory region after the real-to-long mode transition.
 *
 * The bridge:
 *   1. Sets data segment registers
 *   2. Loads the kernel CR3 (switching to kernel page tables)
 *   3. Loads RSI with the ProcessorState pointer
 *   4. Jumps to HalpAPEntry64 (at its kernel virtual address)
 *
 * After this jump, execution continues in kernel address space.
 *
 * Returns the number of bytes written.
 */
static
ULONG
HalpBuildBridgeStub(
    _Out_writes_(MaxSize) PUCHAR Buffer,
    _In_ ULONG MaxSize,
    _In_ ULONG64 ProcessorStatePtr,
    _In_ ULONG64 Entry64VirtualAddress)
{
    ULONG Idx = 0;

    ASSERT(MaxSize >= 40);

    /*
     * Set data segment registers to the temp GDT data selector (0x10).
     * CR3/CR4 are NOT switched here -- the temp page tables already
     * include both identity-mapped low memory and kernel VA mappings.
     * HalpAPEntry64 will switch to the real kernel page tables.
     */

    /* mov ax, 0x10  (66 B8 10 00) */
    Buffer[Idx++] = 0x66;
    Buffer[Idx++] = 0xB8;
    Buffer[Idx++] = 0x10;
    Buffer[Idx++] = 0x00;

    /* mov ds, ax  (8E D8) */
    Buffer[Idx++] = 0x8E;
    Buffer[Idx++] = 0xD8;

    /* mov es, ax  (8E C0) */
    Buffer[Idx++] = 0x8E;
    Buffer[Idx++] = 0xC0;

    /* mov ss, ax  (8E D0) */
    Buffer[Idx++] = 0x8E;
    Buffer[Idx++] = 0xD0;

    /* xor eax, eax  (31 C0) */
    Buffer[Idx++] = 0x31;
    Buffer[Idx++] = 0xC0;

    /* mov fs, ax  (8E E0) */
    Buffer[Idx++] = 0x8E;
    Buffer[Idx++] = 0xE0;

    /* mov gs, ax  (8E E8) */
    Buffer[Idx++] = 0x8E;
    Buffer[Idx++] = 0xE8;

    /* mov rsi, imm64 (ProcessorState pointer)  ->  48 BE <8 bytes> */
    Buffer[Idx++] = 0x48;
    Buffer[Idx++] = 0xBE;
    *(PULONG64)(Buffer + Idx) = ProcessorStatePtr;
    Idx += 8;

    /* mov rax, imm64 (Entry64 VA)  ->  48 B8 <8 bytes> */
    Buffer[Idx++] = 0x48;
    Buffer[Idx++] = 0xB8;
    *(PULONG64)(Buffer + Idx) = Entry64VirtualAddress;
    Idx += 8;

    /* jmp rax  (FF E0) */
    Buffer[Idx++] = 0xFF;
    Buffer[Idx++] = 0xE0;

    return Idx;
}

BOOLEAN
NTAPI
HalStartNextProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PKPROCESSOR_STATE ProcessorState)
{
    PUCHAR LowStub;
    PAP_ENTRY_DATA ApEntryData;
    ULONG TrampolineSize;
    ULONG BridgeOffset;
    ULONG BridgeSize;
    ULONG Pml4Phys;
    ULONG64 LowStubPhys;
    PULONG TempCr3Ptr;

    /* Bail out if we only use the boot CPU */
    if (HalpOnlyBootProcessor)
        return FALSE;

    /* Bail out if we have started all available CPUs */
    if (HalpStartedProcessorCount == HalpApicInfoTable.ProcessorCount)
        return FALSE;

    DPRINT("HalStartNextProcessor: Starting processor %lu\n",
           HalpStartedProcessorCount);

    LowStub = (PUCHAR)HalpLowStub;
    LowStubPhys = HalpLowStubPhysicalAddress.QuadPart;

    /* Build temporary page tables for the AP mode transition */
    Pml4Phys = HalpSetupTemporaryMappings();
    if (!Pml4Phys)
        return FALSE;

    /*
     * Calculate sizes and offsets from the assembly symbols.
     * All the APData_* fields are within the range HalpAPEntry16..HalpAPEntry16End,
     * so they get copied along with the trampoline.
     */
    TrampolineSize = (ULONG)((ULONG_PTR)&HalpAPEntry16End - (ULONG_PTR)&HalpAPEntry16);

    /* Copy the 16-bit trampoline + data into page 0 of the low stub */
    RtlCopyMemory(LowStub, &HalpAPEntry16, TrampolineSize);

    /*
     * Macro to convert an assembly symbol address to the corresponding
     * address in the low stub copy.
     */
    #define STUB_ADDR(sym) \
        (LowStub + ((ULONG_PTR)&(sym) - (ULONG_PTR)&HalpAPEntry16))

    /*
     * Build the 64-bit bridge stub right after the trampoline.
     * Align to 16 bytes for code alignment.
     */
    BridgeOffset = ALIGN_UP_BY(TrampolineSize, 16);
    BridgeSize = HalpBuildBridgeStub(
        LowStub + BridgeOffset,
        PAGE_SIZE - BridgeOffset,
        (ULONG64)ProcessorState,
        (ULONG64)&HalpAPEntry64);

    ASSERT(BridgeOffset + BridgeSize < PAGE_SIZE);

    /*
     * Patch the data fields in the low stub copy.
     * We use the STUB_ADDR macro to find each field's location.
     */

    /*
     * 1. Patch the far jump target to the bridge's physical address.
     * APData_FarJmp32Target is the .long 0 right after the 0x66 0xEA bytes.
     * It is not an exported symbol, so we scan for the known pattern:
     * 0x66 0xEA <4 bytes=0> <2 bytes selector=0x0008>.
     */
    {
        PUCHAR Scan = LowStub;
        PUCHAR ScanEnd = LowStub + TrampolineSize - 8;
        BOOLEAN Found = FALSE;

        for (; Scan < ScanEnd; Scan++)
        {
            /* Look for: 0x66 0xEA <4 bytes=0> <2 bytes selector=0x0008> */
            if (Scan[0] == 0x66 && Scan[1] == 0xEA &&
                *(PULONG)(Scan + 2) == 0 &&
                *(PUSHORT)(Scan + 6) == 0x0008)
            {
                *(PULONG)(Scan + 2) = (ULONG)(LowStubPhys + BridgeOffset);
                Found = TRUE;
                break;
            }
        }
        ASSERT(Found);
    }

    /* 2. Patch the temporary GDT base address.
     *    The GDT limit is (3*8 - 1) = 0x17. The base is the 4 bytes after.
     *    We search for the limit value 0x0017 followed by zeroed base. */
    {
        PUCHAR Scan = LowStub;
        PUCHAR ScanEnd = LowStub + TrampolineSize - 6;
        BOOLEAN Found = FALSE;

        for (; Scan < ScanEnd; Scan++)
        {
            if (*(PUSHORT)Scan == 0x0017 &&
                *(PULONG)(Scan + 2) == 0)
            {
                /* GDT is right after this 6-byte GDTR (2 limit + 4 base) */
                ULONG GdtOffset = (ULONG)(Scan + 6 - LowStub);
                *(PULONG)(Scan + 2) = (ULONG)(LowStubPhys + GdtOffset);
                Found = TRUE;
                break;
            }
        }
        ASSERT(Found);
    }

    /* 3. Patch TempCr3 (the .long 0 right before HalpAPEntryData).
     *    It is at offset (HalpAPEntryData - HalpAPEntry16 - 4) in the stub. */
    {
        ULONG EntryDataOffset = (ULONG)((ULONG_PTR)&HalpAPEntryData - (ULONG_PTR)&HalpAPEntry16);
        TempCr3Ptr = (PULONG)(LowStub + EntryDataOffset - sizeof(ULONG));
        *TempCr3Ptr = Pml4Phys;
    }

    /* 4. Fill in the AP_ENTRY_DATA fields */
    ApEntryData = (PAP_ENTRY_DATA)STUB_ADDR(HalpAPEntryData);
    ApEntryData->ProcessorState = ProcessorState;
    ApEntryData->Entry64VA = (ULONG64)&HalpAPEntry64;
    ApEntryData->KernelCr3 = ProcessorState->SpecialRegisters.Cr3;

    #undef STUB_ADDR

    DPRINT("HalStartNextProcessor: Trampoline phys 0x%I64x, PML4 phys 0x%x, bridge at +0x%x\n",
           LowStubPhys, Pml4Phys, BridgeOffset);

    /* Send INIT/SIPI to the target AP */
    ApicStartApplicationProcessor(HalpStartedProcessorCount, HalpLowStubPhysicalAddress);

    HalpStartedProcessorCount++;

    return TRUE;
}
