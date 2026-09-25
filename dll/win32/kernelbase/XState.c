/*
 * Extended processor-state context APIs, adapted from Wine kernelbase/memory.c.
 * Copyright 1997 Alexandre Julliard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdarg.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wine/kernelbase.h"

/* The corresponding native RTL context helpers are available on AMD64. */
#ifdef __x86_64__

BOOL WINAPI InitializeContext2(void *buffer, DWORD flags, CONTEXT **context,
                              DWORD *length, ULONG64 compaction_mask)
{
    ULONG original_length = *length;
    CONTEXT_EX *extended;
    NTSTATUS status;

    status = RtlGetExtendedContextLength2(flags, length, compaction_mask);
    if (status == STATUS_NOT_SUPPORTED && (flags & 0x40))
    {
        flags &= ~0x40;
        status = RtlGetExtendedContextLength2(flags, length, compaction_mask);
    }
    if (status) return set_ntstatus(status);
    if (!buffer || original_length < *length)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }

    status = RtlInitializeExtendedContext2(buffer, flags, &extended, compaction_mask);
    if (status) return set_ntstatus(status);
    *context = (CONTEXT *)((BYTE *)extended + extended->Legacy.Offset);
    return TRUE;
}

BOOL WINAPI InitializeContext(void *buffer, DWORD flags, CONTEXT **context, DWORD *length)
{
    return InitializeContext2(buffer, flags, context, length, ~(ULONG64)0);
}

BOOL WINAPI GetXStateFeaturesMask(CONTEXT *context, DWORD64 *mask)
{
    if (!(context->ContextFlags & CONTEXT_AMD64))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    *mask = (context->ContextFlags & CONTEXT_FLOATING_POINT) == CONTEXT_FLOATING_POINT ? 3 : 0;
    if ((context->ContextFlags & CONTEXT_XSTATE) == CONTEXT_XSTATE)
        *mask |= RtlGetExtendedFeaturesMask((CONTEXT_EX *)(context + 1));
    return TRUE;
}

BOOL WINAPI SetXStateFeaturesMask(CONTEXT *context, DWORD64 mask)
{
    if (!(context->ContextFlags & CONTEXT_AMD64)) return FALSE;
    if (mask & 3) context->ContextFlags |= CONTEXT_FLOATING_POINT;
    if ((context->ContextFlags & CONTEXT_XSTATE) != CONTEXT_XSTATE)
        return !(mask & ~(DWORD64)3);
    RtlSetExtendedFeaturesMask((CONTEXT_EX *)(context + 1), mask);
    return TRUE;
}

void *WINAPI LocateXStateFeature(CONTEXT *context, DWORD feature, DWORD *length)
{
    if (!(context->ContextFlags & CONTEXT_AMD64)) return NULL;
    if (feature >= 2)
        return (context->ContextFlags & CONTEXT_XSTATE) == CONTEXT_XSTATE
            ? RtlLocateExtendedFeature((CONTEXT_EX *)(context + 1), feature, length) : NULL;
    if (feature == 1)
    {
        if (length) *length = sizeof(context->FltSave.XmmRegisters);
        return context->FltSave.XmmRegisters;
    }
    if (length) *length = FIELD_OFFSET(XSAVE_FORMAT, XmmRegisters);
    return &context->FltSave;
}

#endif
