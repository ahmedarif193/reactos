/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/*
 * ReactOS-private RISC-V64 user TEB register (tp/x4). See
 * docs/riscv64/thread-environment-block.md.
 */

    .text
    .p2align 2

    .globl NtCurrentTeb
NtCurrentTeb:
    mv a0, tp
    ret
