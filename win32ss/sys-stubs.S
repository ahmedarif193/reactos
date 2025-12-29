
#if !defined(_M_ARM64) && (defined(__aarch64__) || defined(_ARM64_) || defined(__arm64__))
#define _M_ARM64 1
#endif

#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_U name

#include "w32ksvc32.h"

    END

#elif defined(_M_ARM64)

    .text
    .set SyscallId, 0x1000

#define SVC_(name, argcount) STUB_U name, argcount

#include "w32ksvc64.h"

#else

.code

SyscallId = 0x1000
#define SVC_(name, argcount) STUB_U name, argcount

#ifdef _WIN64
#include "w32ksvc64.h"
#else
#include "w32ksvc32.h"
#endif

    END
#endif
