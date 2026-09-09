/*
 * PROJECT:     ReactOS DirectComposition
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel batch ownership shared by win32k and dxgkrnl
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

typedef struct _DXGK_COMPOSITION_BATCH_LIST DXGK_COMPOSITION_BATCH_LIST;

/* IBatchList transfers ownership. It has no IUnknown prefix. A caller must
 * fetch the next item before returning this one, which can destroy it. */
typedef struct _DXGK_COMPOSITION_BATCH_LIST_VTABLE
{
    DXGK_COMPOSITION_BATCH_LIST *(NTAPI *GetNext)(DXGK_COMPOSITION_BATCH_LIST *Batch);
    VOID (NTAPI *ReturnToApplication)(DXGK_COMPOSITION_BATCH_LIST *Batch, BOOLEAN Discard);
} DXGK_COMPOSITION_BATCH_LIST_VTABLE;

struct _DXGK_COMPOSITION_BATCH_LIST
{
    const DXGK_COMPOSITION_BATCH_LIST_VTABLE *Vtbl;
};

C_ASSERT(sizeof(DXGK_COMPOSITION_BATCH_LIST_VTABLE) == 2 * sizeof(PVOID));
C_ASSERT(FIELD_OFFSET(DXGK_COMPOSITION_BATCH_LIST_VTABLE, ReturnToApplication) == sizeof(PVOID));
