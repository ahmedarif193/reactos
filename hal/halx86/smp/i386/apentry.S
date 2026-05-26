/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     i386 Application Processor (AP) spinup setup
 * COPYRIGHT:   Copyright 2021 Victor Perevertkin <victor.perevertkin@reactos.org>
 *              Copyright 2021-2023 Justin Miller <justin.miller@reactos.org>
 */

#include <asm.inc>
#include <ks386.inc>

#define PS(f) (f - _HalpAPEntryData)

#ifdef _USE_ML
#define OFFSET_GDTR (Gdtr - _HalpAPEntry16)
#define OFFSET_IDTR (Idtr - _HalpAPEntry16)
#define OFFSET_PAGE_TABLE_ROOT (PageTableRoot - _HalpAPEntry16)
#define OFFSET_PROCESSOR_STATE (ProcessorState - _HalpAPEntry16)
#else
#define OFFSET_GDTR GdtrOffset
#define OFFSET_IDTR IdtrOffset
#define OFFSET_PAGE_TABLE_ROOT PageTableRootOffset
#define OFFSET_PROCESSOR_STATE ProcessorStateOffset

.equ GdtrOffset, Gdtr - _HalpAPEntry16
.equ IdtrOffset, Idtr - _HalpAPEntry16
.equ PageTableRootOffset, PageTableRoot - _HalpAPEntry16
.equ ProcessorStateOffset, ProcessorState - _HalpAPEntry16
#endif

PUBLIC _HalpAPEntry16
PUBLIC _HalpAPEntryData
PUBLIC _HalpAPEntry32
PUBLIC _HalpAPEntry16End

.code16
_HalpAPEntry16:
    cli

    /* Calculate the flat base address */
    mov ebp, cs
    shl ebp, 4

    /* Use flat addressing */
    xor eax, eax
    mov ds, eax

#ifdef _USE_ML
    data32 lgdt fword ptr cs:[OFFSET_GDTR]
    data32 lidt fword ptr cs:[OFFSET_IDTR]
#else
    data32 lgdt cs:[OFFSET_GDTR]
    data32 lidt cs:[OFFSET_IDTR]
#endif

    /* Load temp page table */
    mov eax, cs:[OFFSET_PAGE_TABLE_ROOT]
    mov cr3, eax

    mov eax, cr0
    or eax, HEX(80000001) /* CR0_PG | CR0_PE */
    mov cr0, eax

.align 4
    /* Long jump, 32bit address */
    .byte HEX(66)
    .byte HEX(EA)
_HalpAPEntryData:
_APEntryJump32Offset:
    .long 0
_APEntryJump32Segment:
    .long 8
SelfPtr:
    .long 0
PageTableRoot:
    .long 0
ProcessorState:
    .long 0
Gdtr_Pad:
    .short 0 // Pad
Gdtr:
    .short 0 // Limit
    .long 0 // Base
Idtr_Pad:
    .short 0 // Pad
Idtr:
    .short 0 // Limit
    .long 0 // Base
_HalpAPEntry16End:
.endcode16

.code32
_HalpAPEntry32:
    /* Set the Ring 0 DS/ES/SS Segment */
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax

    /* Load ProcessorState pointer */
    mov esi, [ebp + OFFSET_PROCESSOR_STATE]

    mov eax, [esi + PsContextFrame + CsSegDs]
    mov ds, eax
    mov eax, [esi + PsContextFrame + CsSegEs]
    mov es, eax
    mov eax, [esi + PsContextFrame + CsSegSs]
    mov ss, eax
    mov eax, [esi + PsContextFrame + CsSegFs]
    mov fs, eax
    mov eax, [esi + PsContextFrame + CsSegGs]
    mov gs, eax

    /* Write CR registers with ProcessorState values */
    mov eax, [esi + PsSpecialRegisters + SrCr3]
    mov cr3, eax
    mov eax, [esi + PsSpecialRegisters + SrCr4]
    mov cr4, eax

    /* Load debug registers */
    mov eax, [esi + PsSpecialRegisters + SrKernelDr0]
    mov dr0, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr1]
    mov dr1, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr2]
    mov dr2, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr3]
    mov dr3, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr6]
    mov dr6, eax
    mov eax, [esi + PsSpecialRegisters + SrKernelDr7]
    mov dr7, eax

    /* Load TSS */
    ltr word ptr [esi + PsSpecialRegisters + SrTr]

    /* Load AP Stack */
    mov esp, [esi + PsContextFrame + CsEsp]

    /* Load Eip and push it as a "return" address */
    mov eax, [esi + PsContextFrame + CsEip]
    push eax

    /* Load flags */
    mov eax, [esi + PsContextFrame + CsEflags]
    sahf

    /* Set up all GP registers */
    xor edi, edi
    xor esi, esi
    xor ebp, ebp
    xor ebx, ebx
    xor edx, edx
    xor ecx, ecx
    xor eax, eax

    /* Jump into the kernel */
    ret
END
