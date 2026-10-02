/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C connector system software interface class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#define WDFCX_STRING2(_Text) #_Text
#define WDFCX_STRING(_Text) WDFCX_STRING2(_Text)
#define WDFCX_SYMBOL_PREFIX WDFCX_STRING(__USER_LABEL_PREFIX__)

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:" WDFCX_SYMBOL_PREFIX "_UcmUcsiCx_BIND_INFO")

PVOID UcmucsiDriverGlobals;
PVOID UcmucsiFunctions[9];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _UcmUcsiCx_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"UcmUcsiCx",
    {1, 0, 0},
    (VOID (NTAPI **)(VOID))UcmucsiFunctions,
    RTL_NUMBER_OF(UcmucsiFunctions),
    &UcmucsiDriverGlobals,
    NULL,
    NULL,
    NULL
};
