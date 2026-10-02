/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Trusted runtime class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_WindowsTrustedRT_BIND_INFO")

PVOID TrBindContext;
PVOID TrFunctions[6];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _WindowsTrustedRT_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"WindowsTrustedRT",
    {1, 0, 0},
    (VOID (NTAPI **)(VOID))TrFunctions,
    RTL_NUMBER_OF(TrFunctions),
    &TrBindContext,
    NULL,
    NULL,
    NULL
};
