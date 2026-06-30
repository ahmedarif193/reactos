/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Debug and tracing helpers for dxgkrnl
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 */

#pragma once

/*
 * Pool allocation tag for all dxgkrnl allocations.
 *
 * Tag bytes in little-endian (as stored in the pool): 'D', 'x', 'g', 'X'
 * Displayed in tools like !poolfind as 'XgxD'.
 */
#define DXGKRNL_POOL_TAG    'XgxD'

/*
 * DXGKRNL_TRACE(fmt, ...)
 *
 * Lightweight init/flow tracing. There are ~235 call sites; mapping them to
 * the always-on DPRINT1 floods the synchronous COM1 debug port (115200 baud)
 * and is the dominant cost of a WDDM boot (hundreds of seconds of serial I/O).
 * So it is OFF by default and re-enabled with -DDXGKRNL_DEBUG_TRACE=1 (or by
 * defining it before this header). DXGKRNL_WARN/ERR remain always-on, so real
 * problems still surface. Do not gate on architecture — this is common code.
 */
#ifndef DXGKRNL_DEBUG_TRACE
#define DXGKRNL_DEBUG_TRACE 0
#endif
#if DXGKRNL_DEBUG_TRACE
#define DXGKRNL_TRACE(fmt, ...) \
    DPRINT1("DXGKRNL: " fmt, ##__VA_ARGS__)
#else
#define DXGKRNL_TRACE(fmt, ...) ((void)0)
#endif

/*
 * DXGKRNL_VERBOSE(fmt, ...)
 *
 * High-frequency hot-path traces (IOCTL dispatch, lock/unlock, escape,
 * aperture map/unmap).  Compiled out unless DXGKRNL_DEBUG_VERBOSE is
 * explicitly set to 1 before including this header or on the compiler
 * command line (-DDXGKRNL_DEBUG_VERBOSE=1).
 */
// #define DXGKRNL_DEBUG_VERBOSE 1
#ifndef DXGKRNL_DEBUG_VERBOSE
#define DXGKRNL_DEBUG_VERBOSE 0
#endif

#if DXGKRNL_DEBUG_VERBOSE
#define DXGKRNL_VERBOSE(fmt, ...) \
    DPRINT1("DXGKRNL: " fmt, ##__VA_ARGS__)
#else
#define DXGKRNL_VERBOSE(fmt, ...) ((void)0)
#endif

/*
 * DXGKRNL_WARN(fmt, ...)
 *
 * Non-fatal warning that always prints in checked builds.
 */
#define DXGKRNL_WARN(fmt, ...) \
    DPRINT1("DXGKRNL: WARNING: " fmt, ##__VA_ARGS__)

/*
 * DXGKRNL_ERR(fmt, ...)
 *
 * Error-level print — always active (uses DPRINT1 which maps to DbgPrint
 * regardless of NDEBUG).
 */
#define DXGKRNL_ERR(fmt, ...) \
    DPRINT1("DXGKRNL: ERROR: " fmt, ##__VA_ARGS__)

/*
 * DxgkRosAssert
 *
 * Internal assertion helper used during development. In a free build this
 * compiles to nothing; in a checked build it will bugcheck with a unique
 * code so we can identify the source site in crash dumps.
 *
 * Parameters:
 *   Expr    - Boolean expression that must be TRUE.
 *   BugCode - A driver-defined bugcheck subcode (use DXGKRNL_BUGCHECK_*).
 */
#if DBG
FORCEINLINE VOID
DxgkRosAssert(
    _In_ BOOLEAN Expr,
    _In_ ULONG   BugCode)
{
    if (!Expr)
    {
        /*
         * Use KeBugCheckEx with a ReactOS-reserved bugcheck code so that
         * crash dumps are clearly labelled. Parameters:
         *   P1 = DXGKRNL_POOL_TAG ('XgxD') as a breadcrumb
         *   P2 = caller-supplied subcode
         *   P3 = 0 (reserved)
         *   P4 = 0 (reserved)
         *
         * 0x00000170 is not yet assigned to any public ReactOS or Windows
         * driver bugcheck; treat it as a private dxgkrnl assertion stop.
         */
        KeBugCheckEx(0x00000170,
                     (ULONG_PTR)DXGKRNL_POOL_TAG,
                     (ULONG_PTR)BugCode,
                     0,
                     0);
    }
}
#else
#define DxgkRosAssert(Expr, BugCode) ((void)0)
#endif /* DBG */

/* Sub-codes for DxgkRosAssert */
#define DXGKRNL_BUGCHECK_INIT_FAILURE       0x0001
#define DXGKRNL_BUGCHECK_NULL_ADAPTER       0x0002
#define DXGKRNL_BUGCHECK_BAD_DEVICE_EXT     0x0003
#define DXGKRNL_BUGCHECK_INVALID_HANDLE     0x0004
