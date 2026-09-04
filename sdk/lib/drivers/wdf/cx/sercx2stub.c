/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SerCx2 class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_SerCx_BIND_INFO")

PVOID SercxDriverGlobals;
PVOID SercxFunctions[35];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _SerCx_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"SerCx",
    {2, 0, 0},
    (VOID (NTAPI **)(VOID))SercxFunctions,
    RTL_NUMBER_OF(SercxFunctions),
    &SercxDriverGlobals,
    NULL,
    NULL,
    NULL
};
