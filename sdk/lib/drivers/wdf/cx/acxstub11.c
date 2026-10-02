/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio class extension 1.1 client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_Acx_BIND_INFO")

PVOID AcxDriverGlobals;
PVOID AcxFunctions[221];
BOOLEAN AcxClientVersionHigherThanFramework;
ULONG AcxFunctionCount = 221;
ULONG AcxStructureCount = 62;
size_t *AcxStructures;
PCWSTR AcxFrameworkExtensionName = L"Acx";
extern ULONG AcxMinimumVersionRequired;

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO2 _Acx_BIND_INFO =
{
    {
        sizeof(WDF_CLASS_BIND_INFO2),
        L"Acx",
        {1, 1, 0},
        (VOID (NTAPI **)(VOID))AcxFunctions,
        RTL_NUMBER_OF(AcxFunctions),
        &AcxDriverGlobals,
        NULL,
        NULL,
        NULL
    },
    &AcxMinimumVersionRequired,
    &AcxClientVersionHigherThanFramework,
    &AcxFunctionCount,
    &AcxStructureCount,
    (size_t *)&AcxStructures
};
