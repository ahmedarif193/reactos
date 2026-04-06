/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) entry trampoline
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2025 ReactOS Team
 *
 * DESCRIPTION:
 *     Trampoline code executed by APs after receiving a SIPI from the BSP.
 *     Performs: 16-bit real mode -> 64-bit long mode transition.
 *
 *     The 16-bit section (HalpAPEntry16 .. HalpAPEntry16End) is copied
 *     to a low-memory stub by HalStartNextProcessor() in spinup.c.
 *     The data fields within that section are patched before the AP is
 *     started.
 *
 *     After the far jump to 64-bit mode, the AP switches to the kernel
 *     page tables using ProcessorState->SpecialRegisters.Cr3, then jumps
 *     to the kernel-mapped HalpAPEntry64 which loads the final GDT/IDT/TSS
 *     and jumps to KiSystemStartup.
 *
 *     Low stub layout (5 pages):
 *       Page 0 (0x0000): 16-bit trampoline + data + 64-bit bridge
 *       Page 1 (0x1000): Temporary PML4
 *       Page 2 (0x2000): Temporary PDPT
 *       Page 3 (0x3000): Temporary PD (identity maps low 1GB with 2MB pages)
 *       Page 4 (0x4000): Reserved
 */

#include <asm.inc>
#include <ksamd64.inc>

/*
 * Offset of a symbol relative to HalpAPEntry16 (the trampoline base).
 * Used in 16-bit mode for CS-relative addressing.
 */
#define ZERO_OFFSET(f) (f - HalpAPEntry16)

PUBLIC HalpAPEntry16
PUBLIC HalpAPEntryData
PUBLIC HalpAPEntry64
PUBLIC HalpAPEntry16End

/* =========================================================================
 * 16-BIT REAL MODE ENTRY POINT
 *
 * AP starts here after SIPI. CS:IP = (phys_page >> 4) : 0.
 * Transitions directly from real mode to long mode.
 * ========================================================================= */

.code16
HalpAPEntry16:
    cli

    /* Compute flat physical base: CS << 4 */
    mov ebp, cs
    shl ebp, 4

    /* Flat DS = 0 for absolute addressing */
    xor eax, eax
    mov ds, eax

    /* Load the temporary GDT (32-bit base in 16-bit mode) */
#ifdef _USE_ML
    data32 lgdt fword ptr cs:[ZERO_OFFSET(TempGdtr)]
#else
    data32 lgdt cs:[ZERO_OFFSET(TempGdtr)]
#endif

    /* Enable PAE in CR4 (required for long mode) */
    mov eax, cr4
    or eax, HEX(20)            /* CR4_PAE */
    mov cr4, eax

    /* Load temporary PML4 into CR3 */
    mov eax, cs:[ZERO_OFFSET(APData_TempCr3)]
    mov cr3, eax

    /* Enable Long Mode (LME) and NX in EFER */
    mov ecx, HEX(C0000080)     /* MSR_EFER */
    rdmsr
    or eax, HEX(900)           /* EFER_LME | EFER_NXE */
    wrmsr

    /* Enable paging and protection -> activates long mode */
    mov eax, cr0
    or eax, HEX(80000001)      /* CR0_PG | CR0_PE */
    mov cr0, eax

    /*
     * Far jump to 64-bit code segment (selector 0x08 in TempGdt).
     * The 32-bit offset at APData_FarJmp32Target is patched by spinup.c
     * to the physical address of the 64-bit bridge code in the low stub.
     */
.align 4
    .byte HEX(66)              /* Operand size override */
    .byte HEX(EA)              /* Far jump opcode */
APData_FarJmp32Target:
    .long 0                    /* 32-bit offset - patched by spinup.c */
    .short HEX(08)             /* Segment selector: 64-bit code */

/*
 * Temporary GDT for the mode transition.
 * Entry 0 (0x00): Null descriptor
 * Entry 1 (0x08): 64-bit code (L=1, D=0, P=1, DPL=0, Type=Execute/Read)
 * Entry 2 (0x10): Data (G=1, D/B=1, P=1, DPL=0, Type=Read/Write)
 */
.align 4
TempGdtr:
    .short (TempGdtEnd - TempGdt - 1)  /* Limit */
APData_TempGdtrBase:
    .long 0                             /* Base - patched by spinup.c */

TempGdt:
    .quad 0
    .short HEX(FFFF)
    .short 0
    .byte 0
    .byte HEX(9A)
    .byte HEX(AF)
    .byte 0
    .short HEX(FFFF)
    .short 0
    .byte 0
    .byte HEX(92)
    .byte HEX(CF)
    .byte 0
TempGdtEnd:

/*
 * AP_ENTRY_DATA: Data fields filled by spinup.c.
 * Exported as HalpAPEntryData so spinup.c can find the base.
 * Field offsets from HalpAPEntryData must match AP_ENTRY_DATA in spinup.c.
 */

/* Temporary CR3: physical address of PML4, patched by spinup.c */
APData_TempCr3:
    .long 0

HalpAPEntryData:
/* +0x00: ProcessorState pointer (PKPROCESSOR_STATE, kernel VA) */
APData_ProcessorState:
    .quad 0
/* +0x08: Entry64 virtual address (kernel VA of HalpAPEntry64) */
APData_Entry64VA:
    .quad 0
/* +0x10: Kernel CR3 */
APData_KernelCr3:
    .quad 0

HalpAPEntry16End:
.endcode16

/* =========================================================================
 * 64-BIT LONG MODE ENTRY POINT
 *
 * Reached from the kernel address space after the bridge switches CR3.
 * RSI = ProcessorState pointer (set by bridge).
 *
 * Loads the kernel GDT, IDT, TSS, debug registers, stack, and jumps
 * to KiSystemStartup.
 * ========================================================================= */

.code64

HalpAPEntry64:
    /*
     * At this point we are in 64-bit mode with temporary page tables
     * that include both identity-mapped low memory and kernel VA mappings.
     * RSI holds the ProcessorState pointer (set by bridge stub).
     * First, switch to the kernel's real page tables.
     */

    /* Load CR3 from ProcessorState - switch to kernel page tables.
     * Safe because we are executing at kernel VA, which is mapped
     * in both the temporary and kernel page tables. */
    mov rax, [rsi + PsCr3]
    mov cr3, rax

    /* Load CR4 from ProcessorState */
    mov rax, [rsi + PsCr4]
    mov cr4, rax

    /* Load the kernel's GDT and IDT.
     * KDESCRIPTOR has: Pad[3] (6 bytes), Limit (2 bytes), Base (8 bytes).
     * lgdt/lidt expect the Limit:Base pair starting at Pad[3]+Limit,
     * which is at offset PsGdtr + 6. */
    lgdt [rsi + PsGdtr + 6]
    lidt [rsi + PsIdtr + 6]

    /* Load debug registers */
    mov rax, [rsi + PsKernelDr0]
    mov dr0, rax
    mov rax, [rsi + PsKernelDr1]
    mov dr1, rax
    mov rax, [rsi + PsKernelDr2]
    mov dr2, rax
    mov rax, [rsi + PsKernelDr3]
    mov dr3, rax
    mov rax, [rsi + PsKernelDr6]
    mov dr6, rax
    mov rax, [rsi + PsKernelDr7]
    mov dr7, rax

    /* Load TSS selector */
    ltr word ptr [rsi + PsTr]

    /* Set data segments to kernel data selector */
    mov ax, HEX(18)            /* KGDT64_R0_DATA */
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    /* Load the kernel stack */
    mov rsp, [rsi + PsContextFrame + CxRsp]

    /* Load the entry point (KiSystemStartup) */
    mov rax, [rsi + PsContextFrame + CxRip]

    /*
     * Load RCX with the first argument for KiSystemStartup.
     * On AMD64, the first parameter is passed in RCX.
     * mproc.c stores KeLoaderBlock in ContextFrame.Rcx.
     */
    mov rcx, [rsi + PsContextFrame + CxRcx]

    /* Clear remaining GP registers for a clean entry state */
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor rdi, rdi
    xor rbp, rbp
    xor rbx, rbx
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    xor rsi, rsi

    /*
     * The x64 calling convention requires that on function entry the stack
     * is aligned to 16 bytes minus 8 (i.e., RSP mod 16 == 8) to account
     * for the return address push. We subtract 8 for the shadow space
     * alignment, then push a dummy return address.
     */
    sub rsp, HEX(28)          /* Shadow space (32 bytes) + alignment */
    mov [rsp], rax             /* Store target as "return address" at [rsp] */
    jmp rax                    /* Jump to KiSystemStartup(KeLoaderBlock) */

END
