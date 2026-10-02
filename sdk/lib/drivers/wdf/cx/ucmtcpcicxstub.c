/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C port controller interface class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_UcmTcpciCx_BIND_INFO")

PVOID UcmtcpciDriverGlobals;
PVOID UcmtcpciFunctions[7];

DATA_SEG(".kmdfclassbind$b")
WDF_CLASS_BIND_INFO _UcmTcpciCx_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"UcmTcpciCx",
    {1, 0, 0},
    (VOID (NTAPI **)(VOID))UcmtcpciFunctions,
    RTL_NUMBER_OF(UcmtcpciFunctions),
    &UcmtcpciDriverGlobals,
    NULL,
    NULL,
    NULL
};
