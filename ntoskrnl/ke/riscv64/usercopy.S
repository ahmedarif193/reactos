/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* Bounded user-copy leaves. Only the marked user-side accesses have fixups;
 * a fault in the kernel buffer remains a kernel error. No unwind substitute. */
    .text
    .balign 4
    .global KiRiscvCopyFromUserRaw
KiRiscvCopyFromUserRaw:
    beqz a2, .Lcopy_done
    .global KiRiscvCopyFromUserFault
KiRiscvCopyFromUserFault:
    lbu t0, 0(a1)
    sb t0, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    bnez a2, KiRiscvCopyFromUserFault
.Lcopy_done:
    li a0, 0
    ret

    .balign 4
    .global KiRiscvCopyToUserRaw
KiRiscvCopyToUserRaw:
    beqz a2, .Lcopy_done
.Lcopy_to:
    lbu t0, 0(a1)
    .global KiRiscvCopyToUserFault
KiRiscvCopyToUserFault:
    sb t0, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    bnez a2, .Lcopy_to
    j .Lcopy_done

    /* The trap handler supplies failure NTSTATUS in saved a0. */
    .global KiRiscvCopyUserFailure
KiRiscvCopyUserFailure:
    ret

    /* Resident kernel metadata reader. Only the source load is recoverable. */
    .balign 4
    .global KiRiscvReadKernelRaw
KiRiscvReadKernelRaw:
    beqz a2, .Lcopy_done
    .global KiRiscvReadKernelFault
KiRiscvReadKernelFault:
    lbu t0, 0(a1)
    sb t0, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi a2, a2, -1
    bnez a2, KiRiscvReadKernelFault
    j .Lcopy_done
