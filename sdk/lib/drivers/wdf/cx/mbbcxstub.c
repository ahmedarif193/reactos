/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     MBBCx 1.0 class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_MbbCx_BIND_INFO")

PVOID MbbDriverGlobals;
PVOID MbbFunctions[15];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _MbbCx_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"MbbCx",
    {1, 0, 0},
    (VOID (NTAPI **)(VOID))MbbFunctions,
    RTL_NUMBER_OF(MbbFunctions),
    &MbbDriverGlobals,
    NULL,
    NULL,
    NULL
};
