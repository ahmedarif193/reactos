/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB function class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>
#include <initguid.h>
#include <usbspec.h>
#include <usbfnbase.h>
#include <usbfnattach.h>
#include <usbfnioctl.h>
#include <ufxproprietarycharger.h>

#define WDFCX_STRING2(_Text) #_Text
#define WDFCX_STRING(_Text) WDFCX_STRING2(_Text)
#define WDFCX_SYMBOL_PREFIX WDFCX_STRING(__USER_LABEL_PREFIX__)

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:" WDFCX_SYMBOL_PREFIX "_UFX_BIND_INFO")

PVOID UfxGlobals;
PVOID UfxClassFunctions[21];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _UFX_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"UFX",
    {1, 1, 0},
    (VOID (NTAPI **)(VOID))UfxClassFunctions,
    RTL_NUMBER_OF(UfxClassFunctions),
    &UfxGlobals,
    NULL,
    NULL,
    NULL
};
