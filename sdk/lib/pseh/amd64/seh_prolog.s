/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS CRT
 * FILE:            lib/pseh/amd64/seh_prolog.S
 * PROGRAMMERS:     Ahmed ARIF
 *                  Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

/* INCLUDES ******************************************************************/

#include <asm.inc>

EXTERN _except_handler3:PROC

.code

PUBLIC _SEH_prolog
_SEH_prolog:


PUBLIC _SEH_epilog
_SEH_epilog:
    ret

END
