/*
 * PROJECT:     ReactOS ARM64EC runtime
 * PURPOSE:     AMD64 entry helpers which cannot use generated ARM64EC thunks
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <sdkddkver.h>
#include <wine/asm.h>

#define CHPE_X64_SYSCALL(Name, Id) \
    __ASM_DEFINE_FUNC("\"EXP+#" #Name "\"", \
                     __ASM_SEH(".seh_endprologue\n\t") \
                     ".byte 0x4c,0x8b,0xd1,0xb8\n\t" \
                     ".long " #Id "\n\t" \
                     "syscall\n\tret\n\t.fill 21,1,0x90\n\t") \
    __asm__(".globl " #Name "\n.set " #Name ",\"EXP+#" #Name "\"\n");

__asm__(".set ChpeServiceId, 0\n");

#define SYSFUNCS_TARGET_ARM64 1
#define SVC_(name, argcount) \
    __asm__(".set ChpeServiceId_" #name ", ChpeServiceId\n" \
            ".set ChpeServiceId, ChpeServiceId + 1\n"); \
    CHPE_X64_SYSCALL(ChpeAutoNt##name, ChpeServiceId_##name) \
    CHPE_X64_SYSCALL(ChpeAutoZw##name, ChpeServiceId_##name)
#define SVC_WRAP_(name, argcount) SVC_(name, argcount)
#include <sysfuncs.h>
#undef SVC_WRAP_
#undef SVC_
#undef SYSFUNCS_TARGET_ARM64
#undef CHPE_X64_SYSCALL

__ASM_GLOBAL_FUNC(ChpeInvokeSyscall,
                  "movq %r10,8(%rsp)\n\t"
                  "popq %r10\n\t"
                  "movq %r10,8(%rsp)\n\t"
                  "leaq ChpeSyscallServices(%rip),%r10\n\t"
                  "callq *(%r10,%rax,8)\n\t"
                  "movq (%rsp),%r10\n\t"
                  "pushq 8(%rsp)\n\t"
                  "pushq %r10\n\t"
                  "ret")

/*
 * The x64 export has the same split shape as the Windows ARM64X ntdll: keep
 * the optional WoW64 hook in x64 code, then enter the native exception worker.
 */
__ASM_GLOBAL_POINTER("ChpeLdrpWow64PrepareForExceptionX64", "0")

__ASM_GLOBAL_FUNC(ChpeKiUserExceptionDispatcher,
                  __ASM_SEH(".seh_endprologue\n\t")
                  "cld\n\t"
                  "movq ChpeLdrpWow64PrepareForExceptionX64(%rip),%rax\n\t"
                  "testq %rax,%rax\n\t"
                  "jz 1f\n\t"
                  "subq $0x28,%rsp\n\t"
                  "call *%rax\n\t"
                  "addq $0x28,%rsp\n"
                  "1:\tjmp ChpeKiUserExceptionDispatcherNative")

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

/*
 * Capture guest state before any ARM64EC thunk can clobber RAX, R10 or flags.
 * This is the user-mode path of sdk/lib/rtl/amd64/except_asm.S, using the AMD64
 * CONTEXT offsets. Native bridge-internal captures still use the EC helper.
 */
__ASM_GLOBAL_FUNC(ChpeRtlCaptureContextX64,
                  "pushfq\n\t"
                  __ASM_SEH(".seh_stackalloc 8\n\t")
                  __ASM_SEH(".seh_endprologue\n\t")
                  "movq %rax,0x78(%rcx)\n\t"
                  "movl $0x10000f,0x30(%rcx)\n\t"
                  "movq %rcx,0x80(%rcx)\n\t"
                  "movq %rdx,0x88(%rcx)\n\t"
                  "movq %rbx,0x90(%rcx)\n\t"
                  "movq %rbp,0xa0(%rcx)\n\t"
                  "movq %rsi,0xa8(%rcx)\n\t"
                  "movq %rdi,0xb0(%rcx)\n\t"
                  "movq %r8,0xb8(%rcx)\n\t"
                  "movq %r9,0xc0(%rcx)\n\t"
                  "movq %r10,0xc8(%rcx)\n\t"
                  "movq %r11,0xd0(%rcx)\n\t"
                  "movq %r12,0xd8(%rcx)\n\t"
                  "movq %r13,0xe0(%rcx)\n\t"
                  "movq %r14,0xe8(%rcx)\n\t"
                  "movq %r15,0xf0(%rcx)\n\t"
                  "movq 8(%rsp),%rax\n\t"
                  "movq %rax,0xf8(%rcx)\n\t"
                  "leaq 16(%rsp),%rax\n\t"
                  "movq %rax,0x98(%rcx)\n\t"
                  "movw %cs,0x38(%rcx)\n\t"
                  "movw %ds,0x3a(%rcx)\n\t"
                  "movw %es,0x3c(%rcx)\n\t"
                  "movw %fs,0x3e(%rcx)\n\t"
                  "movw %gs,0x40(%rcx)\n\t"
                  "movw %ss,0x42(%rcx)\n\t"
                  "movl (%rsp),%eax\n\t"
                  "movl %eax,0x44(%rcx)\n\t"
                  "stmxcsr 0x34(%rcx)\n\t"
                  "fxsave 0x100(%rcx)\n\t"
                  "movb $0,0x105(%rcx)\n\t"
                  "movw $0,0x106(%rcx)\n\t"
                  "movq $0,0x108(%rcx)\n\t"
                  "movq $0,0x110(%rcx)\n\t"
                  "movl $0x2ffff,0x11c(%rcx)\n\t"
                  ".irp offset,0x12a,0x13a,0x14a,0x15a,0x16a,0x17a,0x18a,0x19a\n\t"
                  "movw $0,\\offset(%rcx)\n\t"
                  "movl $0,\\offset+2(%rcx)\n\t"
                  ".endr\n\t"
                  "addq $8,%rsp\n\t"
                  "ret")

/* setjmp must capture the guest's registers before the ARM64EC entry thunk. */
__ASM_GLOBAL_FUNC(ChpeSetJmpX64,
                  __ASM_SEH(".seh_endprologue\n\t")
                  "leaq 8(%rsp),%rax\n\t"
                  "movq (%rsp),%r8\n\t"
                  "movq %rdx,0x00(%rcx)\n\t"
                  "movq %rbx,0x08(%rcx)\n\t"
                  "movq %rax,0x10(%rcx)\n\t"
                  "movq %rbp,0x18(%rcx)\n\t"
                  "movq %rsi,0x20(%rcx)\n\t"
                  "movq %rdi,0x28(%rcx)\n\t"
                  "movq %r12,0x30(%rcx)\n\t"
                  "movq %r13,0x38(%rcx)\n\t"
                  "movq %r14,0x40(%rcx)\n\t"
                  "movq %r15,0x48(%rcx)\n\t"
                  "movq %r8,0x50(%rcx)\n\t"
                  "stmxcsr 0x58(%rcx)\n\t"
                  "fnstcw 0x5c(%rcx)\n\t"
                  "movdqu %xmm6,0x60(%rcx)\n\t"
                  "movdqu %xmm7,0x70(%rcx)\n\t"
                  "movdqu %xmm8,0x80(%rcx)\n\t"
                  "movdqu %xmm9,0x90(%rcx)\n\t"
                  "movdqu %xmm10,0xa0(%rcx)\n\t"
                  "movdqu %xmm11,0xb0(%rcx)\n\t"
                  "movdqu %xmm12,0xc0(%rcx)\n\t"
                  "movdqu %xmm13,0xd0(%rcx)\n\t"
                  "movdqu %xmm14,0xe0(%rcx)\n\t"
                  "movdqu %xmm15,0xf0(%rcx)\n\t"
                  "xorl %eax,%eax\n\t"
                  "ret")
