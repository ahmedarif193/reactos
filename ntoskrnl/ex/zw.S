
#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_K name
#define SVC_WRAP_(name, argcount) STUB_K name

#include <sysfuncs.h>

#elif defined(SYSCALLS_GNU_AS_STUBS)

    .text

#define SVC_(name, argcount) STUB_K name, argcount
#define SVC_WRAP_(name, argcount) STUB_K name, argcount

#include <sysfuncs.h>

#else

#ifdef _M_IX86
EXTERN _KiSystemService:PROC
#elif defined(_M_AMD64)
#include <ksamd64.inc>
EXTERN KiSystemService:PROC
EXTERN KiZwSystemService:PROC
#endif

.code

#define SVC_(name, argcount) STUB_K name, argcount
#define SVC_WRAP_(name, argcount) STUB_K name, argcount

#include <sysfuncs.h>

#endif

#ifndef SYSCALLS_GNU_AS_STUBS
    END
#endif
