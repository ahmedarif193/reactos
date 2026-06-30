/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Debug and tracing helpers for dxgmms1.sys
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#pragma once

/*
 * Pool allocation tag for all dxgmms1 allocations.
 *
 * Bytes stored in little-endian (as written to pool memory):
 *   's', 'm', 'm', 'G'
 * Displayed in tools such as !poolfind / poolmon (byte-reversed):
 *   'Gmms'
 */
#define DXGMMS1_POOL_TAG    'smmG'

/*
 * DXGMMS_TRACE(fmt, ...)
 *
 * Lightweight tracing wrapper over DPRINT.  Compiled away in free builds
 * when NDEBUG is defined (DPRINT becomes a no-op).
 * Do NOT gate on architecture — this file is not architecture-specific.
 */
#define DXGMMS_TRACE(fmt, ...) \
    DPRINT("DXGMMS1: " fmt, ##__VA_ARGS__)

/*
 * DXGMMS_WARN(fmt, ...)
 *
 * Non-fatal warning.  Always emits output in checked builds via DPRINT1.
 */
#define DXGMMS_WARN(fmt, ...) \
    DPRINT1("DXGMMS1: WARNING: " fmt, ##__VA_ARGS__)

/*
 * DXGMMS_ERR(fmt, ...)
 *
 * Error-level message.  Always active — maps to DPRINT1 / DbgPrint
 * regardless of NDEBUG.
 */
#define DXGMMS_ERR(fmt, ...) \
    DPRINT1("DXGMMS1: ERROR: " fmt, ##__VA_ARGS__)

/*
 * DxgMmsAssert
 *
 * Internal assertion helper.  In free builds this is a no-op.  In
 * checked builds a failing assertion triggers KeBugCheckEx with a
 * driver-private stop code so crash dumps can be correlated to the
 * source location.
 *
 * Parameters:
 *   Expr    - Boolean expression that must be TRUE.
 *   BugCode - DXGMMS1_BUGCHECK_* sub-code identifying the assertion.
 *
 * Stop code 0x00000171 is not assigned to any public ReactOS or Windows
 * driver bugcheck; it serves as a private dxgmms1 assertion sentinel.
 */
#if DBG
FORCEINLINE VOID
DxgMmsAssert(
    _In_ BOOLEAN Expr,
    _In_ ULONG   BugCode)
{
    if (!Expr)
    {
        KeBugCheckEx(0x00000171,
                     (ULONG_PTR)DXGMMS1_POOL_TAG,
                     (ULONG_PTR)BugCode,
                     0,
                     0);
    }
}
#else
#define DxgMmsAssert(Expr, BugCode)  ((void)0)
#endif /* DBG */

/* Sub-codes for DxgMmsAssert */
#define DXGMMS1_BUGCHECK_INIT_FAILURE       0x0001  /* DriverEntry failure       */
#define DXGMMS1_BUGCHECK_NULL_CONTEXT       0x0002  /* NULL adapter/node context */
#define DXGMMS1_BUGCHECK_BAD_SIGNATURE      0x0003  /* Corrupted object signature*/
#define DXGMMS1_BUGCHECK_INVALID_SEGMENT    0x0004  /* Segment index out of range*/
#define DXGMMS1_BUGCHECK_INVALID_NODE       0x0005  /* Node ordinal out of range */
#define DXGMMS1_BUGCHECK_ALLOC_FAILURE      0x0006  /* Pool allocation failed    */
#define DXGMMS1_BUGCHECK_DOUBLE_REMOVE      0x0007  /* RemoveAdapter called twice*/
