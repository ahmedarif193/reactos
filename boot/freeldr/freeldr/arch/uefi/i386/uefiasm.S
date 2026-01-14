/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     i386 assembly UEFI escape helper functions
 * COPYRIGHT:   Copyright 2023 Justin Miller <justinmiller100@gmail.com>
 */

#include <asm.inc>
#include <arch/pc/x86common.h>
#include <arch/pc/pcbios.h>

PUBLIC _gdtptr
PUBLIC _i386idtptr
PUBLIC __changestack
EXTERN _UefiServiceStack:DWORD
EXTERN _i386Idt:DWORD
EXTERN _ExecuteLoaderCleanly:PROC
EXTERN _UefiExitBootServices:PROC
EXTERN _BasicStack:DWORD
.code32

// void __exituefi(VOID)
PUBLIC __exituefi
__exituefi:
    push ebp
    push esi
    push edi
    push ebx

    /* Save the old stack */
    mov ebx, esp

    /* Load the new stack */
    xor ebp, ebp
    mov esp, _UefiServiceStack

    /* Call the entry routine, passing the parameters */
    call _UefiExitBootServices

    /* Restore old stack */
    mov esp, ebx

    /* Restore non-volatiles */
    pop ebx
    pop edi
    pop esi
    pop ebp

    /* Save return address before GDT switch - we need it for far return */
    pop eax                          /* Pop return address into EAX */

    /*
     * Disable interrupts before loading new GDT/IDT and keep them disabled.
     *
     * Rationale:
     * - UEFI Boot Services have been terminated, so the UEFI IDT is no longer valid
     * - We're switching from UEFI's GDT/IDT to our own minimal GDT/IDT
     * - ReactOS kernel will set up its own IDT during early initialization
     * - Interrupts must remain disabled until that IDT is ready
     */
    cli

#ifdef _USE_ML
    lgdt fword ptr [_gdtptr]
    lidt fword ptr ds:[_i386idtptr]
#else
    lgdt cs:[_gdtptr]
    lidt _i386idtptr
#endif

    /*
     * CRITICAL: After loading a new GDT, we MUST reload the segment registers.
     * The old UEFI CS selector is no longer valid with our new GDT.
     * Use a far return (retf) to atomically reload CS with our new selector.
     *
     * Stack frame for retf: [ESP+0]=EIP, [ESP+4]=CS
     * CS selector 0x08 = 32-bit flat code segment in our GDT
     *
     * We push CS first, then EIP, so that after both pushes:
     *   [ESP+0] = EIP (return address) - popped first by retf
     *   [ESP+4] = CS (selector 0x08)   - popped second by retf
     */
    push HEX(08)                     /* Push CS selector FIRST (will be at ESP+4) */
    push eax                         /* Push return address SECOND (will be at ESP+0) */

    /* Reload data segment registers with new DS selector (0x10) */
    mov ax, HEX(10)
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    /* Far return: pops EIP then CS, reloading CS with our new GDT selector */
    retf

// void __reloadsegment(VOID)
PUBLIC __changestack
__changestack:
    mov eax, esp
    mov esp, _BasicStack
    push eax
    call _ExecuteLoaderCleanly
    ret

    .align 4    /* force 4-byte alignment */
gdt:
    /* NULL Descriptor */
   .word HEX(0000)
   .word HEX(0000)
   .word HEX(0000)
   .word HEX(0000)

    /* 32-bit flat CS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9A00)
    .word HEX(00CF)

    /* 32-bit flat DS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9200)
    .word HEX(00CF)

    /* 16-bit real mode CS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9E00)
    .word HEX(0000)

    /* 16-bit real mode DS */
    .word HEX(FFFF)
    .word HEX(0000)
    .word HEX(9200)
    .word HEX(0000)

/* GDT table pointer */
_gdtptr:
    .word HEX(27)  /* Limit */
    .long gdt, 0   /* Base Address */

_i386idtptr:
    .word 255      /* Limit */
    .long _i386Idt /* Base Address */
END
