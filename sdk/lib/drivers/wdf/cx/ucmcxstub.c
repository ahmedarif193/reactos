/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB connector manager class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#define WDFCX_STRING2(_Text) #_Text
#define WDFCX_STRING(_Text) WDFCX_STRING2(_Text)
#define WDFCX_SYMBOL_PREFIX WDFCX_STRING(__USER_LABEL_PREFIX__)

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:" WDFCX_SYMBOL_PREFIX "_UcmCx_BIND_INFO")

PVOID UcmDriverGlobals;
PVOID UcmFunctions[11];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _UcmCx_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"UcmCx",
    {1, 0, 0},
    (VOID (NTAPI **)(VOID))UcmFunctions,
    RTL_NUMBER_OF(UcmFunctions),
    &UcmDriverGlobals,
    NULL,
    NULL,
    NULL
};
