
#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_K name

#include <sysfuncs.h>

#elif defined(_M_ARM64) || defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)

    .text

#define SVC_(name, argcount) STUB_K name, argcount

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

#include <sysfuncs.h>

#endif

#ifndef _M_ARM64
    END
#endif
