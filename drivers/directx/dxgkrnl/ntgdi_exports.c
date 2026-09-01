/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows 11 public NtGdi D3DKMT export entry points
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 */

#include "dxgkrnl_private.h"
#include <reactos/rddm/rxgkntgdi.h>

/*
 * Windows keeps these syscall entry points in dxgkrnl.  ReactOS keeps user
 * capture and nested-buffer marshalling in win32k, so each exported entry
 * returns through the registered win32k dispatcher before it reaches the
 * existing dxgkrnl core.  That preserves the native public ABI without
 * exposing the kernel-captured core functions directly to user pointers.
 */

#define DXGKP_DEFINE_NTGDI_0(Ordinal, Name) \
    BOOLEAN NTAPI DxgkNtGdiDdDDI##Name(VOID) \
    { \
        return DxgkDispatchWin32kNtGdi(Ordinal, 0, 0, 0, 0, 0) == TRUE; \
    }
RXGK_NTGDI_EXPORTS_0(DXGKP_DEFINE_NTGDI_0)
#undef DXGKP_DEFINE_NTGDI_0

#define DXGKP_DEFINE_NTGDI_1(Ordinal, Name) \
    NTSTATUS NTAPI DxgkNtGdiDdDDI##Name(_In_opt_ PVOID Argument) \
    { \
        return (NTSTATUS)DxgkDispatchWin32kNtGdi(Ordinal, (ULONG_PTR)Argument, 0, 0, 0, 0); \
    }
RXGK_NTGDI_EXPORTS_1(DXGKP_DEFINE_NTGDI_1)
#undef DXGKP_DEFINE_NTGDI_1

NTSTATUS
NTAPI
DxgkNtGdiDdDDIGetProcessSchedulingPriorityClass(
    _In_opt_ HANDLE Process,
    _Out_opt_ D3DKMT_SCHEDULINGPRIORITYCLASS *PriorityClass)
{
    return (NTSTATUS)DxgkDispatchWin32kNtGdi(183, (ULONG_PTR)Process, (ULONG_PTR)PriorityClass, 0, 0, 0);
}

NTSTATUS
NTAPI
DxgkNtGdiDdDDISetProcessSchedulingPriorityClass(
    _In_opt_ HANDLE Process,
    _In_ D3DKMT_SCHEDULINGPRIORITYCLASS PriorityClass)
{
    return (NTSTATUS)DxgkDispatchWin32kNtGdi(267, (ULONG_PTR)Process, (ULONG_PTR)PriorityClass, 0, 0, 0);
}

NTSTATUS
NTAPI
DxgkNtGdiDdDDIShareObjects(
    _In_ UINT ObjectCount,
    _In_reads_opt_(ObjectCount) const D3DKMT_HANDLE *Objects,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ DWORD DesiredAccess,
    _Out_opt_ HANDLE *SharedNtHandle)
{
    return (NTSTATUS)DxgkDispatchWin32kNtGdi(275, ObjectCount, (ULONG_PTR)Objects, (ULONG_PTR)ObjectAttributes, DesiredAccess, (ULONG_PTR)SharedNtHandle);
}
