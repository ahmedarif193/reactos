/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V kernel handoff entry
 */

#include <asm/ksriscv64.h>

    .section .text$KiSystemStartup, "xr"
    .balign 4
    .global KiSystemStartup
KiSystemStartup:
    /* FreeLdr supplies a0 and the aligned resident boot stack. Keep every
     * supervisor interrupt source masked until the C consumer validates and
     * publishes the PCR and trap-vector contract. */
    csrci sstatus, 2
    csrw sie, zero
    tail KiRiscvSystemStartup

    /* The C consumer is non-returning. Retain a fail-stop if a bad compiler
     * or calling-convention change violates that contract. */
.Lstartup_returned:
    wfi
    j .Lstartup_returned
