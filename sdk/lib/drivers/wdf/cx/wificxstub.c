/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WiFiCx 1.2 class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_WifiCx_BIND_INFO")

PVOID WifiDriverGlobals;
PVOID WifiFunctions[40];
BOOLEAN WifiClientVersionHigherThanFramework;
ULONG WifiFunctionCount = 40;
ULONG WifiStructureCount = 17;
size_t *WifiStructures;
PCWSTR WifiFrameworkExtensionName = L"WifiCx";
DECLSPEC_SELECTANY ULONG WifiMinimumVersionRequired = 2;

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO2 _WifiCx_BIND_INFO =
{
    {
        sizeof(WDF_CLASS_BIND_INFO2),
        L"WifiCx",
        {1, 2, 0},
        (VOID (NTAPI **)(VOID))WifiFunctions,
        RTL_NUMBER_OF(WifiFunctions),
        &WifiDriverGlobals,
        NULL,
        NULL,
        NULL
    },
    &WifiMinimumVersionRequired,
    &WifiClientVersionHigherThanFramework,
    &WifiFunctionCount,
    &WifiStructureCount,
    (size_t *)&WifiStructures
};
