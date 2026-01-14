/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     x64 assembly UEFI escape helper functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justinmiller100@gmail.com>
 */

#include <asm.inc>
#include <arch/pc/x86common.h>
#include <arch/pc/pcbios.h>

EXTERN UefiServiceStack:QWORD
EXTERN BasicStack:QWORD
EXTERN ExecuteLoaderCleanly:PROC
EXTERN UefiExitBootServices:PROC
/* If needed: */
/* EXTERN EfiImageHandle:QWORD */
/* EXTERN EfiMapKey:QWORD */

.code64

/*
 * void UefiClearDebugState(VOID)
 *
 * Clear all x64 debug registers and sanitize CPU debug state before
 * calling ExitBootServices. Some UEFI firmware implementations (notably
 * VirtualBox) may have internal debug state that causes #DB exceptions
 * if not properly cleared.
 *
 * This clears:
 *   - DR0-DR3: Hardware breakpoint addresses
 *   - DR6: Debug status register (set to default 0xFFFF0FF0)
 *   - DR7: Debug control register (disable all breakpoints)
 *   - RFLAGS.TF: Trap flag (single-step)
 *   - RFLAGS.RF: Resume flag (suppress debug exceptions)
 *
 * Must be called from Ring 0 with interrupts enabled or disabled.
 */
PUBLIC UefiClearDebugState
UefiClearDebugState:
    push    rbp
    mov     rbp, rsp
    push    rbx

    /*
     * Clear hardware breakpoint address registers (DR0-DR3).
     * These specify addresses that trigger debug exceptions.
     */
    xor     rax, rax
    mov     dr0, rax
    mov     dr1, rax
    mov     dr2, rax
    mov     dr3, rax

    /*
     * Clear DR7 (Debug Control Register) FIRST.
     * Bit 10 must always be 1 (reserved, required).
     * All other bits = 0 disables all hardware breakpoints.
     * This prevents any debug exceptions from being generated
     * while we clean up the rest of the debug state.
     */
    mov     rax, HEX(400)              /* Bit 10 = 1, all else = 0 */
    mov     dr7, rax

    /*
     * Reset DR6 (Debug Status Register) to its default value.
     * Intel SDM specifies that bits 0-3 should be cleared by software,
     * and certain upper bits have fixed values.
     * 0xFFFF0FF0 is the default/reset value:
     *   - Bits 0-3 (B0-B3): 0 (no breakpoint triggered)
     *   - Bits 4-11: Reserved, set to 1
     *   - Bit 12: Reserved, 0
     *   - Bit 13 (BD): 0 (no debug register access)
     *   - Bit 14 (BS): 0 (no single-step)
     *   - Bit 15 (BT): 0 (no task switch)
     *   - Bits 16-31: Reserved, set to 1
     */
    mov     rax, HEX(FFFF0FF0)
    mov     dr6, rax

    /*
     * Clear RFLAGS.TF (Trap Flag) and RFLAGS.RF (Resume Flag).
     * TF enables single-step mode which generates #DB after each instruction.
     * RF suppresses debug exceptions for one instruction.
     * We want both cleared to ensure normal execution.
     *
     * RFLAGS bit positions:
     *   TF = bit 8 (0x100)
     *   RF = bit 16 (0x10000)
     */
    pushfq
    pop     rax
    and     rax, HEX(FFFFFFFFFFFEFEFF)  /* Clear TF (bit 8) and RF (bit 16) */
    push    rax
    popfq

    pop     rbx
    pop     rbp
    ret

// void _exituefi(VOID)
PUBLIC _exituefi
_exituefi:
    /* Save non-volatile registers */
    push    rbp
    push    rsi
    push    rdi
    push    rbx

    /* Save the old stack */
    mov     rbx, rsp

    /* Load the new UEFI service stack */
    xor     rbp, rbp
    mov     rsp, qword ptr [rip + UefiServiceStack]

    /* Ensure 16-byte alignment (defensive programming) */
    and     rsp, -16

    /* Call UefiExitBootServices (wrapper) with proper shadow space + alignment */
    /* If calling firmware directly, set RCX=ImageHandle, RDX=MapKey first. */
    /* mov   rcx, qword ptr [rip + EfiImageHandle] */
    /* mov   rdx, qword ptr [rip + EfiMapKey]      */

    sub     rsp, 0x20                /* 32-byte shadow space for Win x64 ABI */
    call    UefiExitBootServices

    /* Check return value (EFI_SUCCESS = 0) */
    test    rax, rax
    jz      .exit_success

    /* Error case: you may want to handle this differently */
    /* For now, continue anyway, but could retry or halt */

.exit_success:
    add     rsp, 0x20

    /* Restore old stack */
    mov     rsp, rbx

    /* Restore non-volatiles */
    pop     rbx
    pop     rdi
    pop     rsi
    pop     rbp

    /* Save return address before GDT switch - we need it for far return */
    pop     rax                      /* Pop return address into RAX */

    /*
     * Disable interrupts before loading new GDT and keep them disabled.
     *
     * Rationale:
     * - UEFI Boot Services have been terminated, so the UEFI IDT is no longer valid
     * - We're switching from UEFI's GDT to our own minimal GDT
     * - ReactOS kernel will set up its own IDT during early initialization
     * - Interrupts must remain disabled until that IDT is ready
     */
    cli

#ifdef _USE_ML
    lgdt    _gdtptr                  /* ml64: 10-byte descriptor inferred */
#else
    lgdt    [rip + _gdtptr]          /* GAS: RIP-relative load of 10-byte GDTR */
#endif

    /*
     * CRITICAL: After loading a new GDT, we MUST reload the segment registers.
     * The old UEFI CS selector is no longer valid with our new GDT.
     * Use a far return (lretq) to atomically reload CS with our new selector.
     *
     * Stack frame for lretq: [RSP+0]=RIP, [RSP+8]=CS
     * CS selector 0x10 = long mode code segment in our GDT
     *
     * We push CS first, then RIP, so that after both pushes:
     *   [RSP+0] = RIP (return address) - popped first by lretq
     *   [RSP+8] = CS (selector 0x10)   - popped second by lretq
     */
    mov     rcx, HEX(10)             /* LMODE_CS selector */
    push    rcx                      /* Push CS selector FIRST (will be at RSP+8) */
    push    rax                      /* Push return address SECOND (will be at RSP+0) */

    /* Reload data segment registers with new DS selector (0x18) */
    mov     ax, HEX(18)              /* LMODE_DS */
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    xor     ax, ax                   /* FS and GS should be NULL in long mode */
    mov     fs, ax
    mov     gs, ax

    /* Far return: pops RIP then CS, reloading CS with our new GDT selector */
    lretq

// void _changestack(VOID)
PUBLIC _changestack
_changestack:
    /* Save current stack pointer and switch to BasicStack */
    mov     rax, rsp
    mov     rsp, qword ptr [rip + BasicStack]
    
    /* Ensure 16-byte alignment (defensive programming) */
    and     rsp, -16

    /* x64 ABI: shadow space + alignment before call. */
    /* Pass old RSP in RCX if ExecuteLoaderCleanly expects it (common pattern). */
    sub     rsp, 0x20                /* 32-byte shadow space */
    mov     rcx, rax                 /* RCX = previous RSP (argument 1) */
    call    ExecuteLoaderCleanly
    add     rsp, 0x20
    
    /* Note: ExecuteLoaderCleanly should not return in normal operation */
    /* If it does return, this is likely an error condition */
    
    ret

.align 8
gdt:
    .word HEX(0000), HEX(0000), HEX(0000), HEX(0000) /* 00: NULL descriptor */
    .word HEX(0000), HEX(0000), HEX(0000), HEX(0000) /* 08: unused/reserved */
    .word HEX(0000), HEX(0000), HEX(9A00), HEX(0020) /* 10: long mode CS (access=0x9A, L-bit=1) */
    .word HEX(FFFF), HEX(0000), HEX(9200), HEX(0000) /* 18: long mode DS (RW, DPL=0) */
    .word HEX(FFFF), HEX(0000), HEX(9E00), HEX(0000) /* 20: 16-bit real mode CS */
    .word HEX(FFFF), HEX(0000), HEX(9200), HEX(0000) /* 28: 16-bit real mode DS */
    .word HEX(FFFF), HEX(0000), HEX(9B00), HEX(00CF) /* 30: 32-bit compat mode CS */

    /*
     * GDT table pointer: 10 bytes (limit:2 + base:8)
     * Align to 2-byte boundary (minimum required for GDTR)
     * The structure is naturally packed: 2-byte limit followed by 8-byte base
     */
    .align 2
_gdtptr:
    .word HEX(0037)                  /* Limit = 7 entries * 8 - 1 = 0x37 */
    .quad gdt                        /* Base Address */

END