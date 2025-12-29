/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 entry point for FreeLoader
 * COPYRIGHT:   Copyright 2024 Ahmed ARIF (contact@eotics.com)
 */

/* No x86 asm includes on ARM64 */

.extern EfiEntry
.extern FrldrBootDrive
.extern FrldrBootPartition
.extern GlobalImageHandle
.extern GlobalSystemTable
.extern Arm64InitializeMMU
/* Exception trampolines implemented in except.S */
.extern Arm64SyncExceptionHandler
.extern Arm64IrqHandler
.extern Arm64FiqHandler
.extern Arm64SerrorHandler

.text

/* PSTATE/PSR bits used when transitioning from EL2 */
.equ PSR_MODE_EL1H, 0x00000005
.equ PSR_F_BIT,     0x00000040
.equ PSR_I_BIT,     0x00000080
.equ PSR_A_BIT,     0x00000100
.equ PSR_D_BIT,     0x00000200
.equ FRLDR_BOOT_DRIVE_UEFI,     0xFF
.equ FRLDR_BOOT_PARTITION_UEFI, 0xFF

.globl Arm64EnsureEl1EntryState
Arm64EnsureEl1EntryState:
    /* Determine current exception level */
    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #1
    b.eq 2f
    cmp x0, #2
    b.ne 1f

    /* Drop from EL2 to EL1 following Windows loader expectations */
    mrs x1, hcr_el2
    mov x2, #1
    lsl x2, x2, #31       /* HCR_RW */
    orr x1, x1, x2
    mov x2, #1
    lsl x2, x2, #34       /* Clear HCR_E2H */
    bic x1, x1, x2
    mov x2, #1
    lsl x2, x2, #27       /* Clear HCR_TGE */
    bic x1, x1, x2
    msr hcr_el2, x1

    /* Allow EL1 access to timers and system registers */
    mrs x1, cnthctl_el2
    mov x2, #3
    orr x1, x1, x2        /* Enable EL1 PCT/PC */
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr

    /* Permit SIMD/FPU and system instructions at EL1 */
    mov x1, xzr
    msr cptr_el2, x1
    msr hstr_el2, x1
    msr mdcr_el2, x1

    /* Propagate current stack pointer to EL1 */
    mov x1, sp
    msr sp_el1, x1

    /* Mask interrupts during transition */
    mov x1, #(PSR_MODE_EL1H | PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT)
    msr spsr_el2, x1

    /* Resume execution in EL1 right after this block */
    adr x1, 2f
    msr elr_el2, x1
    isb
    eret

1:
    /* Unsupported EL (e.g. EL3) – just align stack and return */

2:
    /* Ensure 16-byte aligned stack for C ABI compliance */
    mov x1, sp
    mov x2, #0xF
    and x1, x1, x2
    cbz x1, 3f
    sub sp, sp, x1
3:
    ret

/* ARM64 UEFI entry point */
.globl RealEntryPoint
RealEntryPoint:
    /* x0 = ImageHandle, x1 = SystemTable (UEFI parameters) */

    /* Preserve UEFI parameters prior to EL transition */
    mov x19, x0
    mov x20, x1

    /* Match Windows loader by ensuring we execute in EL1 with aligned SP */
    bl Arm64EnsureEl1EntryState

    /* Save UEFI parameters on stack for later use */
    stp x29, x30, [sp, #-32]!
    stp x19, x20, [sp, #16]
    mov x29, sp

    /* UEFI parameters already preserved in x19/x20 */

    /* Disable interrupts during initialization */
    msr daifset, #0xF
    
    /* Initialize exception vectors */
    bl Arm64SetupExceptionVectors
    
    /* Initialize BSS section */
    ldr x0, =__bss_start__
    ldr x1, =__bss_end__
    sub x2, x1, x0
    cbz x2, bss_done
    
clear_bss:
    str xzr, [x0], #8
    subs x2, x2, #8
    b.ne clear_bss
    
bss_done:
    /* Publish UEFI handles before we touch MM services */
    ldr x0, =GlobalImageHandle
    str x19, [x0]

    ldr x0, =GlobalSystemTable
    str x20, [x0]

    /* Store BootDrive and BootPartition (UEFI sentinel values) */
    mov w0, #FRLDR_BOOT_DRIVE_UEFI
    ldr x1, =FrldrBootDrive
    strb w0, [x1]
    
    mov w0, #FRLDR_BOOT_PARTITION_UEFI
    ldr x1, =FrldrBootPartition
    str w0, [x1]
    
    /* Initialize cache and memory management */
    bl Arm64InitializeCache
    bl Arm64InitializeMMU

    /* Restore UEFI parameters for EfiEntry */
    mov x0, x19  /* ImageHandle */
    mov x1, x20  /* SystemTable */

    /* Keep interrupts masked; the OS/loader enables them when ready */

    /* Call EfiEntry with UEFI parameters */
    bl EfiEntry
    
    /* Should never return, but if it does, loop */
stop:
    b stop

/* ARM64 cache and MMU initialization */
.globl Arm64InitializeCache
Arm64InitializeCache:
    /* Ensure all previous memory operations complete before cache maintenance */
    dsb sy
    isb

    /* Invalidate entire instruction cache to the point of unification */
    ic iallu
    dsb sy
    isb

    /* Optional D-cache maintenance (clean+invalidate by set/way) can be added here if needed */
    ret

/* Setup ARM64 exception vectors */
.globl Arm64SetupExceptionVectors
Arm64SetupExceptionVectors:
    /* Install exception vector table for current EL */
    ldr x0, =arm64_exception_vectors
    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #3
    b.eq 1f
    cmp x1, #2
    b.eq 2f
    /* EL1 */
    msr vbar_el1, x0
    isb
    ret
1:  /* EL3 */
    msr vbar_el3, x0
    isb
    ret
2:  /* EL2 */
    msr vbar_el2, x0
    isb
    ret


/* ARM64 equivalent functions - not applicable but needed for compatibility */
.globl Relocator16Boot
Relocator16Boot:
    /* Not supported on ARM64 UEFI */
    b stop

.globl PxeCallApi
PxeCallApi:
    /* PXE not supported on ARM64 for now */
    mov x0, #0
    ret

.globl CallRealMode
CallRealMode:
    /* Real mode not applicable on ARM64 */
    ret

/* Exception vector table for ARM64 */
.align 11
.globl arm64_exception_vectors
arm64_exception_vectors:
    /* Current EL with SP0 */
    .align 7
    b Arm64SyncExceptionHandler     /* Synchronous */
    .align 7
    b Arm64IrqHandler               /* IRQ */
    .align 7
    b Arm64FiqHandler               /* FIQ */
    .align 7
    b Arm64SerrorHandler            /* SError */

    /* Current EL with SPx */
    .align 7
    b Arm64SyncExceptionHandler     /* Synchronous */
    .align 7
    b Arm64IrqHandler               /* IRQ */
    .align 7
    b Arm64FiqHandler               /* FIQ */
    .align 7
    b Arm64SerrorHandler            /* SError */

    /* Lower EL using AArch64 */
    .align 7
    b Arm64SyncExceptionHandler     /* Synchronous */
    .align 7
    b Arm64IrqHandler               /* IRQ */
    .align 7
    b Arm64FiqHandler               /* FIQ */
    .align 7
    b Arm64SerrorHandler            /* SError */

    /* Lower EL using AArch32 */
    .align 7
    b Arm64SyncExceptionHandler     /* Synchronous */
    .align 7
    b Arm64IrqHandler               /* IRQ */
    .align 7
    b Arm64FiqHandler               /* FIQ */
    .align 7
    b Arm64SerrorHandler            /* SError */

/* Boot drive and partition are defined in UEFI disk layer */

/* End of file */
