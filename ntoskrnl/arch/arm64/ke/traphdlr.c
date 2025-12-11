/*
 * PROJECT:         ReactOS Kernel – ARM64 bring-up scaffolding
 * LICENSE:         GPL-2.0-or-later (see COPYING in the top level directory)
 * FILE:            ntoskrnl/arch/arm64/ke/traphdlr.c
 * PURPOSE:         Placeholder trap/exception handler glue for ARM64
 * PROGRAMMERS:     ReactOS Contributors
 *
 * NOTE:
 *  This file exists as a “vitrine” so that the amd64/i386 KiHandleKernelSListFault
 *  logic can be mirrored for ARM64 once the architecture actually gains full
 *  trap/exception plumbing. ARM64 currently has no trap stubs, no Ki* handlers,
 *  and no SLIST retry hook, so the code below is intentionally minimal and is
 *  never built. When the real ARM64 dispatcher is implemented, port the logic
 *  from ntoskrnl/arch/amd64/ke/except.c (KiHandleKernelSListFault) into the
 *  equivalent handler here so all architectures share the same SLIST recovery
 *  semantics.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

/*
 * Stub exported symbol so the file links once ARM64 trap handling is wired up.
 * The real implementation must:
 *  - Decode the ARM64 trap frame to detect GP faults at ExpInterlockedPopEntrySListFault16.
 *  - Re-read the SLIST header (alignment/region) pointed to by the miniport stub.
 *  - Canonicalise the next-entry pointer and retry the pop, bumping SListFaultCount,
 *    just like KiHandleKernelSListFault on amd64.
 */
BOOLEAN
KiHandleKernelSListFaultArm64(
    _Inout_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_WARNING_LEVEL,
               "KiHandleKernelSListFaultArm64: not implemented yet.\n");

    return FALSE;
}

#else

/* Keep the compiler quiet when this placeholder is included in non-ARM64 builds. */
typedef int __ke_traphdlr_c_requires_arm64_host;

#endif /* _M_ARM64 */

