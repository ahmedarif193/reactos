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
