
#include <syscalls.inc>

/* This file only has the NT5 syscalls */
#define SYSFUNCS_NT5_ONLY

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_U name
#define SVC_WRAP_(name, argcount) STUB_U name

#include <sysfuncs.h>

    END

#elif defined(SYSCALLS_GNU_AS_STUBS)

    .text

#define SVC_(name, argcount) STUB_U name, argcount
#define SVC_WRAP_(name, argcount) STUB_WRAP name, argcount

#include <sysfuncs.h>

#else

.code

#define SVC_(name, argcount) STUB_U name, argcount
#define SVC_WRAP_(name, argcount) STUB_U name, argcount

#include <sysfuncs.h>

#ifndef _M_ARM64
    END
#endif
#endif
