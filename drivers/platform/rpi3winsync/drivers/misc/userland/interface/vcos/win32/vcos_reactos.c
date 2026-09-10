/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Initialize the VCOS runtime when its kernel DLL is loaded
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "interface/vcos/vcos.h"

/* Kernel DLL loading does not run the user-mode CRT constructors. */
__declspec(dllexport)
NTSTATUS NTAPI
DllInitialize(PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    return vcos_init() == VCOS_SUCCESS ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

__declspec(dllexport)
NTSTATUS NTAPI
DllUnload(VOID)
{
    vcos_deinit();
    return STATUS_SUCCESS;
}
