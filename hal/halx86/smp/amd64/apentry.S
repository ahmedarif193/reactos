/*
 * PROJECT:     ReactOS HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 Application Processor (AP) spinup trampoline
 * COPYRIGHT:   Copyright 2023 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Clean-room AP bring-up modelled on the Linux real-mode trampoline
 * (arch/x86/realmode/rm/trampoline_64.S): the AP starts in 16-bit real
 * mode at the SIPI vector, switches to 32-bit protected mode, enables
 * PAE + EFER.LME + paging to reach 64-bit long mode, then loads the real
 * kernel CR3/GDT/IDT from the KPROCESSOR_STATE and enters KiSystemStartup.
 *
 * The low part (HalpAPEntry16 .. HalpAPEntry16End) is copied to a page
 * below 1 MB; HalpAPEntry64 runs in place at its kernel address. The far
 * jump immediates, the temporary PML4 root, the ProcessorState pointer and
 * the trampoline GDTR base are patched in by HalStartNextProcessor.
 */

#include <asm.inc>
#include <ksamd64.inc>

PUBLIC HalpAPEntry16
PUBLIC HalpAPEntryProt32
PUBLIC HalpAPEntryLong64
PUBLIC HalpAPEntryJump32
PUBLIC HalpAPEntryJump64
PUBLIC HalpAPEntryProcState
PUBLIC HalpAPEntryHigh
PUBLIC HalpAPEntryPmlRoot
PUBLIC HalpAPEntryGdtr
PUBLIC HalpAPEntryGdt
PUBLIC HalpAPEntry16End
PUBLIC HalpAPEntry64

.equ OFF_PROCSTATE, HalpAPEntryProcState - HalpAPEntry16
.equ OFF_HIGH, HalpAPEntryHigh - HalpAPEntry16
.equ OFF_PMLROOT, HalpAPEntryPmlRoot - HalpAPEntry16
.equ OFF_GDTR, HalpAPEntryGdtr - HalpAPEntry16

.code16
HalpAPEntry16:
    cli
    cld

    /* ebp = physical base of this stub (CS << 4) */
    xor eax, eax
    mov ax, cs
    shl eax, 4
    mov ebp, eax

    /* Flat segments so cs-relative data access works */
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    /* Load the trampoline GDT (limit/base patched in by C) */
    data32 lgdt cs:[OFF_GDTR]

    /*
     * INIT leaves the AP with caching disabled. Flush any firmware cache
     * state, then clear CR0.NW/CD before executing the kernel trampoline.
     */
    mov eax, cr0
    and eax, HEX(DFFFFFFF)
    mov cr0, eax
    wbinvd
    and eax, HEX(BFFFFFFF)
    or eax, 1
    mov cr0, eax

    /* Far jump into 32-bit protected mode (offset patched in by C) */
    .byte HEX(66)
    .byte HEX(EA)
HalpAPEntryJump32:
    .long 0
    .word HEX(08)
.endcode16

.code32
HalpAPEntryProt32:
    /* Reload the 32-bit data segments */
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    /* Enable PAE */
    mov eax, cr4
    or eax, HEX(20)
    mov cr4, eax

    /* Load the temporary PML4 (patched in by C) */
    mov eax, [ebp + OFF_PMLROOT]
    mov cr3, eax

    /* Set EFER.LME and EFER.NXE (the kernel page tables use the NX bit, so
       NXE must be enabled before loading a kernel CR3 or the NX bit reads as
       a reserved bit and every access faults) */
    mov ecx, HEX(C0000080)
    rdmsr
    or eax, HEX(900)
    wrmsr

    /* Enable paging -> IA-32e compatibility mode */
    mov eax, cr0
    or eax, HEX(80000001)
    mov cr0, eax

    /* Far jump into 64-bit long mode (offset patched in by C) */
    .byte HEX(EA)
HalpAPEntryJump64:
    .long 0
    .word HEX(18)

.code64
HalpAPEntryLong64:
    /* rbp still holds the stub base. Fetch the ProcessorState pointer. */
    mov rcx, [rbp + OFF_PROCSTATE]

    /* Switch to the AP kernel stack (mapped by the temporary PML4) */
    mov rsp, [rcx + PsContextFrame + CxRsp]

    /* Jump to the in-place restore code (high kernel VA) */
    mov rax, [rbp + OFF_HIGH]
    jmp rax

/* Data, copied with the stub and patched in by HalStartNextProcessor */
.align 8
HalpAPEntryProcState:
    .quad 0
HalpAPEntryHigh:
    .quad 0
HalpAPEntryPmlRoot:
    .long 0
    .long 0
HalpAPEntryGdtr:
    .word 0
    .long 0
    .word 0
.align 8
HalpAPEntryGdt:
    .quad HEX(0000000000000000)
    .quad HEX(00CF9A000000FFFF)
    .quad HEX(00CF92000000FFFF)
    .quad HEX(00209A0000000000)
HalpAPEntry16End:

.code64
HalpAPEntry64:
    /* rcx = ProcessorState. Switch to the real kernel address space. */
    mov rax, [rcx + PsSpecialRegisters + SrCr3]
    mov cr3, rax
    mov rax, [rcx + PsSpecialRegisters + SrCr4]
    mov cr4, rax
    mov rax, [rcx + PsSpecialRegisters + SrCr0]
    mov cr0, rax

    /* Load the real GDT/IDT (KDESCRIPTOR: +6 skips Pad[3] to reach Limit) */
    lgdt fword ptr [rcx + PsSpecialRegisters + SrGdtr + 6]
    lidt fword ptr [rcx + PsSpecialRegisters + SrIdtr + 6]

    /* Reload the segment registers */
    mov ax, KGDT64_R0_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov gs, ax
    xor ax, ax
    mov fs, ax

    /* Load the task register */
    ltr word ptr [rcx + PsSpecialRegisters + SrTr]

    /* Restore the debug registers */
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr0]
    mov dr0, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr1]
    mov dr1, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr2]
    mov dr2, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr3]
    mov dr3, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr6]
    mov dr6, rax
    mov rax, [rcx + PsSpecialRegisters + SrKernelDr7]
    mov dr7, rax

    /* Reload the stack pointer from the context */
    mov rsp, [rcx + PsContextFrame + CxRsp]

    /* Restore flags (interrupts stay masked) */
    mov eax, [rcx + PsContextFrame + CxEFlags]
    push rax
    popfq

    /* Build a far return frame: CS = KGDT64_R0_CODE, RIP = KiSystemStartup.
       A far return is required to reload CS away from the trampoline GDT. */
    mov rax, [rcx + PsContextFrame + CxRip]
    mov r9, [rcx + PsContextFrame + CxRcx]
    push KGDT64_R0_CODE
    push rax

    /* rcx = first argument (LoaderBlock) */
    mov rcx, r9

    /* Clear the remaining general purpose registers */
    xor eax, eax
    xor ebx, ebx
    xor edx, edx
    xor esi, esi
    xor edi, edi
    xor ebp, ebp
    xor r8d, r8d
    xor r9d, r9d
    xor r10d, r10d
    xor r11d, r11d
    xor r12d, r12d
    xor r13d, r13d
    xor r14d, r14d
    xor r15d, r15d

    /* retfq (REX.W RETF) */
    .byte HEX(48)
    .byte HEX(CB)
END
