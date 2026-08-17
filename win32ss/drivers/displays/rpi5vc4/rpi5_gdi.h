/*
 * PROJECT:     ReactOS Raspberry Pi 5 XPDM graphics stack
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RPi5-specific XPDM GDI publication declarations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _RPI5VC4_GDI_H_
#define _RPI5VC4_GDI_H_

#include "framebuf.h"
#include <reactos/rpi5vc4_xpdm.h>

ULONG APIENTRY
DrvEscape(
    _In_ SURFOBJ *Surface,
    _In_ ULONG Escape,
    _In_ ULONG InputSize,
    _In_reads_bytes_(InputSize) PVOID Input,
    _In_ ULONG OutputSize,
    _Out_writes_bytes_(OutputSize) PVOID Output);

VOID
Rpi5InitializeGdiPdev(
    _Inout_ PPDEV Device);

#endif /* _RPI5VC4_GDI_H_ */
