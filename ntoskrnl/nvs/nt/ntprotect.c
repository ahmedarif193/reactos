/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntprotect.c
 * PURPOSE:     NT virtual memory protection conversion and validation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/core/ntprotectshim.h>
#else
#include <nvs/nt/mint.h>
#endif

BOOLEAN
MiProtectionFromWin32(
    _In_ ULONG Win32Protect,
    _Out_ PULONG Protection)
{
    ULONG Base;

    *Protection = 0;

    switch (Win32Protect & 0xFF)
    {
        case PAGE_NOACCESS:          Base = MI_PROT_NOACCESS; break;
        case PAGE_READONLY:          Base = MI_PROT_READONLY; break;
        case PAGE_READWRITE:         Base = MI_PROT_READWRITE; break;
        case PAGE_WRITECOPY:         Base = MI_PROT_WRITECOPY; break;
        case PAGE_EXECUTE:           Base = MI_PROT_EXECUTE; break;
        case PAGE_EXECUTE_READ:      Base = MI_PROT_EXECUTE_READ; break;
        case PAGE_EXECUTE_READWRITE: Base = MI_PROT_EXECUTE_READWRITE; break;
        case PAGE_EXECUTE_WRITECOPY: Base = MI_PROT_EXECUTE_WRITECOPY; break;
        default:
            return FALSE;
    }

    if (Win32Protect & ~(0xFFUL | PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE))
        return FALSE;

    if (Win32Protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE))
    {
        if (Base == MI_PROT_NOACCESS)
            return FALSE;

        if ((Win32Protect & PAGE_GUARD) && (Win32Protect & (PAGE_NOCACHE | PAGE_WRITECOMBINE)))
            return FALSE;

        if (Win32Protect & PAGE_GUARD)
            Base |= MI_PROT_GUARD;

        if (Win32Protect & PAGE_NOCACHE)
            Base |= MI_PROT_NOCACHE;
    }

    *Protection = Base;
    return TRUE;
}

ULONG
MiProtectionToWin32(
    _In_ ULONG Protection)
{
    static const ULONG Access[8] =
    {
        PAGE_NOACCESS, PAGE_READONLY, PAGE_EXECUTE, PAGE_EXECUTE_READ,
        PAGE_READWRITE, PAGE_WRITECOPY, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY
    };
    ULONG Win32;

    if ((Protection & MI_PROT_NOACCESS) == MI_PROT_NOACCESS || (Protection & MI_PROT_ACCESS_MASK) == 0)
        return PAGE_NOACCESS;

    Win32 = Access[Protection & MI_PROT_ACCESS_MASK];

    if (Protection & MI_PROT_GUARD)
        Win32 |= PAGE_GUARD;

    if (Protection & MI_PROT_NOCACHE)
        Win32 |= PAGE_NOCACHE;

    return Win32;
}

BOOLEAN
MiAllocationProtectionFromWin32(
    _In_ ULONG Win32Protect,
    _Out_ PULONG Protection)
{
    ULONG Base;

    *Protection = 0;
    if (!MiProtectionFromWin32(Win32Protect & ~PAGE_TARGETS_INVALID, &Base) ||
        ((Win32Protect & PAGE_TARGETS_INVALID) && !MI_PROT_IS_EXECUTE(Base)))
    {
        return FALSE;
    }
    *Protection = Base;
    return TRUE;
}
