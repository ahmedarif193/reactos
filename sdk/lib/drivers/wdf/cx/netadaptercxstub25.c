/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx 2.5 class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#define WDFCX_STRING2(_Text) #_Text
#define WDFCX_STRING(_Text) WDFCX_STRING2(_Text)
#define WDFCX_SYMBOL_PREFIX WDFCX_STRING(__USER_LABEL_PREFIX__)

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:" WDFCX_SYMBOL_PREFIX "_NetAdapterCx_BIND_INFO")

PVOID NetDriverGlobals;
PVOID NetFunctions[132];
BOOLEAN NetClientVersionHigherThanFramework;
ULONG NetFunctionCount = 132;
ULONG NetStructureCount = 46;
size_t *NetStructures;
PCWSTR NetFrameworkExtensionName = L"NetAdapterCx";
DECLSPEC_SELECTANY ULONG NetMinimumVersionRequired = 5;

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO2 _NetAdapterCx_BIND_INFO =
{
    {
        sizeof(WDF_CLASS_BIND_INFO2),
        L"NetAdapterCx",
        {2, 5, 0},
        (VOID (NTAPI **)(VOID))NetFunctions,
        RTL_NUMBER_OF(NetFunctions),
        &NetDriverGlobals,
        NULL,
        NULL,
        NULL
    },
    &NetMinimumVersionRequired,
    &NetClientVersionHigherThanFramework,
    &NetFunctionCount,
    &NetStructureCount,
    (size_t *)&NetStructures
};
