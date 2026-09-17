/*
 * FILE:            ntoskrnl/include/internal/napi.h
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PURPOSE:         System Call Table for Native API
 * PROGRAMMER:      Timo Kreuzer
 */

#if defined(_M_RISCV64)
static
NTSTATUS
NTAPI
KiUnsupportedSystemService(VOID)
{
    return STATUS_NOT_IMPLEMENTED;
}
#endif

#define SVC_(name, argcount) (ULONG_PTR)Nt##name,
#define SVC_WRAP_(name, argcount) (ULONG_PTR)Nt##name,
#define SVC_UNSUPPORTED_(name, argcount) (ULONG_PTR)KiUnsupportedSystemService,
ULONG_PTR MainSSDT[] = {
#include "sysfuncs.h"
};
#undef SVC_
#undef SVC_WRAP_
#undef SVC_UNSUPPORTED_

#define SVC_(name, argcount) argcount * sizeof(void *),
#define SVC_WRAP_(name, argcount) argcount * sizeof(void *),
/* Unsupported slots never need to capture arguments from a caller's stack. */
#define SVC_UNSUPPORTED_(name, argcount) 0,
UCHAR MainSSPT[] = {
#include "sysfuncs.h"
};
#undef SVC_
#undef SVC_WRAP_
#undef SVC_UNSUPPORTED_

#define MIN_SYSCALL_NUMBER    0
#define NUMBER_OF_SYSCALLS    (sizeof(MainSSPT) / sizeof(MainSSPT[0]))
#define MAX_SYSCALL_NUMBER    (NUMBER_OF_SYSCALLS - 1)
ULONG MainNumberOfSysCalls = NUMBER_OF_SYSCALLS;
