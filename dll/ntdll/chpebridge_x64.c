/*
 * PROJECT:     ReactOS ARM64EC runtime
 * PURPOSE:     AMD64 entry helpers which cannot use generated ARM64EC thunks
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <wine/asm.h>

/*
 * AMD64 callers pass the stack allocation size in RAX. This helper must stay
 * as genuine x64 code: a generated ARM64EC entry thunk uses RAX as scratch
 * before entering native code and would destroy the size before it is probed.
 */
__ASM_GLOBAL_FUNC(ChpeChkStk,
                  "pushq %rcx\n\t"
                  __ASM_SEH(".seh_pushreg %rcx\n\t")
                  "pushq %rax\n\t"
                  __ASM_SEH(".seh_pushreg %rax\n\t")
                  __ASM_SEH(".seh_endprologue\n\t")
                  "cmpq $0x1000,%rax\n\t"
                  "leaq 24(%rsp),%rcx\n\t"
                  "jb 2f\n"
                  "1:\tsubq $0x1000,%rcx\n\t"
                  "orb $0,(%rcx)\n\t"
                  "subq $0x1000,%rax\n\t"
                  "cmpq $0x1000,%rax\n\t"
                  "ja 1b\n"
                  "2:\tsubq %rax,%rcx\n\t"
                  "orb $0,(%rcx)\n\t"
                  "popq %rax\n\t"
                  "popq %rcx\n\t"
                  "ret")
