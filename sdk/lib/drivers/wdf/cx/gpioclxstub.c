/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GPIO class-extension client binding record
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include "wdf.h"
#include <fxldr.h>

#pragma section(".kmdfclassbind$b", read, write)
#pragma comment(linker, "/include:_GPIOClx_BIND_INFO")

PVOID GpioClxExportedInterfaces[6];

__declspec(allocate(".kmdfclassbind$b"))
WDF_CLASS_BIND_INFO _GPIOClx_BIND_INFO =
{
    sizeof(WDF_CLASS_BIND_INFO),
    L"GPIOClx",
    {1, 0, 0},
    (VOID (NTAPI **)(VOID))GpioClxExportedInterfaces,
    RTL_NUMBER_OF(GpioClxExportedInterfaces),
    NULL,
    NULL,
    NULL,
    NULL
};
