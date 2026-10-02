/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx 2.3 class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_NetAdapterCx_BIND_INFO")

PVOID NetDriverGlobals;
PVOID NetFunctions[125];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _NetAdapterCx_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"NetAdapterCx",
    {2, 3, 0},
    (VOID (NTAPI **)(VOID))NetFunctions,
    RTL_NUMBER_OF(NetFunctions),
    &NetDriverGlobals,
    NULL,
    NULL,
    NULL
};
