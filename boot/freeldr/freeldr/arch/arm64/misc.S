/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Miscellaneous Assembly Functions
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* ARM64 Assembly - GNU AS syntax */
.text
.align 2

/* VOID __cdecl DebugBreakPoint(VOID) */
.global DebugBreakPoint
/* .type DebugBreakPoint, %function */
DebugBreakPoint:
    brk #0
    ret

/* VOID __cdecl ChainLoadBiosBootSectorCode(IN ULONG BootPartition OPTIONAL) */
.global ChainLoadBiosBootSectorCode
/* .type ChainLoadBiosBootSectorCode, %function */
ChainLoadBiosBootSectorCode:
    /* Not implemented for ARM64 - UEFI only */
    ret

/* VOID __cdecl Reboot(VOID) */
.global Reboot
/* .type Reboot, %function */
Reboot:
    /* Issue system reset via PSCI */
    movz x0, #0x8400, lsl #16
    movk x0, #0x0009
    smc #0
    /* Should not return */
1:  b 1b

/* VOID __cdecl EnableA20(VOID) */
.global EnableA20
/* .type EnableA20, %function */
EnableA20:
    /* No A20 gate on ARM64 */
    ret
