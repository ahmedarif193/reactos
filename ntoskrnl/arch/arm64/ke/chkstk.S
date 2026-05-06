/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/chkstk.S
 * PURPOSE:         ARM64 kernel stack probe entry points
 */

    .text
    .align 2

    .global __chkstk
__chkstk:
    ret

    .global __alloca_probe
__alloca_probe:
    ret

