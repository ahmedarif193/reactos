/*
 * ARM64 Context Compatibility Header
 * Provides compatibility macros to map AMD64 context register names to ARM64
 */

#ifndef _ARM64_CONTEXT_COMPAT_H
#define _ARM64_CONTEXT_COMPAT_H

#ifdef _M_ARM64

/* Map AMD64 register names to ARM64 equivalents in CONTEXT structure */
#define Rip Pc
#define Rsp Sp
#define Rbp Fp
#define Rax X0
#define Rcx X0
#define Rdx X1
#define Rbx X19
#define Rsi X4
#define Rdi X5
#define R8 X2
#define R9 X3
#define R10 X6
#define R11 X7
#define R12 X8
#define R13 X9
#define R14 X10
#define R15 X11

/* For compatibility with x86 32-bit code */
#define Eip Pc
#define Esp Sp
#define Ebp Fp
#define Eax X0
#define Ecx X0
#define Edx X1
#define Ebx X19
#define Esi X4
#define Edi X5

#endif /* _M_ARM64 */

#endif /* _ARM64_CONTEXT_COMPAT_H */