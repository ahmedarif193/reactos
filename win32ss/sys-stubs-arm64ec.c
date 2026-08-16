/*
 * PROJECT:     ReactOS Win32 subsystem
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ARM64EC user-mode Win32 system-call stubs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * ARM64EC functions need compiler-generated hybrid metadata and x64 entry
 * thunks.  Plain ARM64 assembly stubs do not carry that metadata, so an x64
 * caller cannot safely enter them through the ARM64EC fast-forward path.
 *
 * Every win32k service argument occupies one integer/pointer-sized ABI slot.
 * Describing the complete signature lets Clang generate the correct entry
 * thunk, including the stack-argument bridge for services with more than four
 * arguments.
 */

typedef unsigned long long ARM64EC_SYSCALL_ARGUMENT;

#define ARM64EC_SYSCALL_ARGS_0 void
#define ARM64EC_SYSCALL_ARGS_1 ARM64EC_SYSCALL_ARGUMENT Argument1
#define ARM64EC_SYSCALL_ARGS_2 ARM64EC_SYSCALL_ARGS_1, ARM64EC_SYSCALL_ARGUMENT Argument2
#define ARM64EC_SYSCALL_ARGS_3 ARM64EC_SYSCALL_ARGS_2, ARM64EC_SYSCALL_ARGUMENT Argument3
#define ARM64EC_SYSCALL_ARGS_4 ARM64EC_SYSCALL_ARGS_3, ARM64EC_SYSCALL_ARGUMENT Argument4
#define ARM64EC_SYSCALL_ARGS_5 ARM64EC_SYSCALL_ARGS_4, ARM64EC_SYSCALL_ARGUMENT Argument5
#define ARM64EC_SYSCALL_ARGS_6 ARM64EC_SYSCALL_ARGS_5, ARM64EC_SYSCALL_ARGUMENT Argument6
#define ARM64EC_SYSCALL_ARGS_7 ARM64EC_SYSCALL_ARGS_6, ARM64EC_SYSCALL_ARGUMENT Argument7
#define ARM64EC_SYSCALL_ARGS_8 ARM64EC_SYSCALL_ARGS_7, ARM64EC_SYSCALL_ARGUMENT Argument8
#define ARM64EC_SYSCALL_ARGS_9 ARM64EC_SYSCALL_ARGS_8, ARM64EC_SYSCALL_ARGUMENT Argument9
#define ARM64EC_SYSCALL_ARGS_10 ARM64EC_SYSCALL_ARGS_9, ARM64EC_SYSCALL_ARGUMENT Argument10
#define ARM64EC_SYSCALL_ARGS_11 ARM64EC_SYSCALL_ARGS_10, ARM64EC_SYSCALL_ARGUMENT Argument11
#define ARM64EC_SYSCALL_ARGS_12 ARM64EC_SYSCALL_ARGS_11, ARM64EC_SYSCALL_ARGUMENT Argument12
#define ARM64EC_SYSCALL_ARGS_13 ARM64EC_SYSCALL_ARGS_12, ARM64EC_SYSCALL_ARGUMENT Argument13
#define ARM64EC_SYSCALL_ARGS_14 ARM64EC_SYSCALL_ARGS_13, ARM64EC_SYSCALL_ARGUMENT Argument14
#define ARM64EC_SYSCALL_ARGS_15 ARM64EC_SYSCALL_ARGS_14, ARM64EC_SYSCALL_ARGUMENT Argument15
#define ARM64EC_SYSCALL_ARGS_16 ARM64EC_SYSCALL_ARGS_15, ARM64EC_SYSCALL_ARGUMENT Argument16

#define ARM64EC_DECLARE_SYSCALL(Name, ArgCount, ServiceId) \
    __attribute__((naked)) ARM64EC_SYSCALL_ARGUMENT Nt##Name(ARM64EC_SYSCALL_ARGS_##ArgCount) \
    { \
        __asm__ volatile("mov x8, %0\nsvc #0\nret" : : "i" (ServiceId)); \
    } \
    extern __typeof(Nt##Name) Zw##Name __attribute__((alias("Nt" #Name)));

enum
{
    Arm64EcSyscallCounterBase = __COUNTER__
};

#define SVC_(Name, ArgCount) ARM64EC_DECLARE_SYSCALL(Name, ArgCount, (0x1000 + __COUNTER__ - Arm64EcSyscallCounterBase - 1))
#include "w32ksvc64.h"
#undef SVC_
