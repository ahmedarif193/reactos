
/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Stack Probing
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* ARM64 assembly for GAS */
.text

.global __chkstk
__chkstk:
    /* Stack probing for ARM64 */
    /* X15 contains the requested size */
    /* For now, just allocate without probing */
    sub     sp, sp, x15
    ret

.global _chkstk
_chkstk:
    /* Alias for __chkstk */
    sub     sp, sp, x15
    ret

.global __alloca_probe
__alloca_probe:
    /* Alloca probe for ARM64 */
    /* X0 contains the requested size */
    /* Round up to 16-byte alignment */
    add     x0, x0, #15
    and     x0, x0, #~15
    /* Allocate space */
    sub     sp, sp, x0
    /* Return the new stack pointer */
    mov     x0, sp
    ret

.global _alloca_probe
_alloca_probe:
    /* Alias for __alloca_probe */
    add     x0, x0, #15
    and     x0, x0, #~15
    sub     sp, sp, x0
    mov     x0, sp
    ret
