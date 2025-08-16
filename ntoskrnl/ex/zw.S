
#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_K name

#include <sysfuncs.h>

#elif defined(_M_ARM64) || defined(__aarch64__)

/* ARM64 system call stubs */
.text

#define SVC_(name, argcount) \
.global Zw##name; \
Zw##name: \
    mov w8, #0; \
    svc #0; \
    ret

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

    END
