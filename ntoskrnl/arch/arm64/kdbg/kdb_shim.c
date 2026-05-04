/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/kdbg/kdb_shim.c
 * PURPOSE:         ARM64-specific KDBG helper functions
 *
 * This file provides ARM64-specific helper functions for KDBG.
 * The main KDBG infrastructure is provided by the common kdb*.c files.
 * This file only contains ARM64-specific extensions that supplement
 * the common implementation, not replace it.
 */

#include <ntoskrnl.h>
#include <kdbg/kdb.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/*
 * Note: All globals are provided by the common kdb.c.
 * This file only provides ARM64-specific helper functions.
 */

/* PUBLIC FUNCTIONS **********************************************************/

KIRQL
NTAPI
KxKdbRaiseIrql(
    VOID)
{
    KIRQL OldIrql;

    /*
     * Keep KDB at HIGH_LEVEL on ARM64 so both DAIF and GIC PMR fully block
     * interrupts while the common debugger code holds DPC-level spinlocks.
     */
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < HIGH_LEVEL)
    {
        KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    }

    return OldIrql;
}

VOID
NTAPI
KxKdbLowerIrql(
    _In_ KIRQL OldIrql)
{
    if (OldIrql < HIGH_LEVEL)
    {
        KeLowerIrql(OldIrql);
    }
}

BOOLEAN
NTAPI
KxKdbFilterLoadSymbols(
    _In_ BOOLEAN LoadSymbols)
{
    UNREFERENCED_PARAMETER(LoadSymbols);

    /*
     * ARM64 bring-up keeps symbol loading disabled during early KDB init to
     * avoid the current symbol-loader cost and noise while debugging boot.
     */
    return FALSE;
}

/*
 * ARM64-specific disassembly and instruction length functions are provided
 * by arm64-dis.c (KdbpGetInstLength, KdbpDisassemble).
 *
 * All other KDBG functions use the common implementations from:
 * - kdb.c (KdbEnterDebuggerException, KdbpGetCommandLineSettings, etc.)
 * - kdb_cli.c (KdbRegisterCliCallback, command handling)
 * - kdb_print.c (KdbPrintf, KdbPuts, KdbPrompt, KdbPromptString)
 * - kdb_symbols.c (KdbpSymFindModule, KdbSymPrintAddress, KdbSymInit, etc.)
 * - kdb_expr.c (expression evaluation)
 */

/* EOF */
