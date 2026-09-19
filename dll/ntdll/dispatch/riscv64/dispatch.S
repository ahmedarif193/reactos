/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Private RISC-V64 loader and user APC transitions
 */

    .text

    /* SP is the kernel-constructed KUSER_EXCEPTION_STACK. Its interrupted
     * PC and ra are distinct. RTL recognizes this exact architecture-owned
     * range and restores the saved CONTEXT instead of inventing a leaf ra. */
    .p2align 1
    .globl KiUserExceptionDispatcher
    .seh_proc KiUserExceptionDispatcher
KiUserExceptionDispatcher:
    .seh_endprologue
    call KiUserExceptionDispatcherWorker
    .globl KiRiscvUserExceptionDispatcherEnd
KiRiscvUserExceptionDispatcherEnd:
    .word 0
2:
    j 2b
    .seh_endproc

    .p2align 2
    .globl LdrInitializeThunk
LdrInitializeThunk:
    /* The APC dispatcher passes the interrupted CONTEXT in a3. */
    mv a0, a3
    tail LdrpInit

    .p2align 2
    .globl KiUserApcDispatcher
KiUserApcDispatcher:
    /* sp points at the saved CONTEXT; keep it across the normal routine. */
    mv s1, sp
    mv t0, a3
    mv a3, s1
    jalr t0
    mv a0, s1
    li a1, 1
    call NtContinue
    /* A failed continuation must not return through the interrupted ra. */
    .word 0
1:
    j 1b
