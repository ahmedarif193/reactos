/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of _setjmp/_setjmpex for x64.
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

#include <asm.inc>

.code64

//
// _setjmp and _setjmpex are thin wrappers that tail-call __intrinsic_setjmpex.
// They are in a separate object file so the linker only pulls them in when
// explicitly needed, avoiding conflicts with libmsvcrt's import stubs.
//
EXTERN __intrinsic_setjmpex:PROC

PUBLIC _setjmp
_setjmp:
PUBLIC _setjmpex
_setjmpex:
    jmp __intrinsic_setjmpex

END
