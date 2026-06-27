
#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_U name
#define SVC_WRAP_(name, argcount) STUB_U name

#include <sysfuncs.h>

    END

#elif defined(_M_ARM64) || defined(_M_ARM64EC) || defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__) || defined(__arm64ec__)

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
