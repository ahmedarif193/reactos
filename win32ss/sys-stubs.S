
#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_U name

#include "w32ksvc32.h"

    END

#elif SYSCALLS_ARM64_STUBS

    .text
    .set SyscallId, 0x1000

#define SVC_(name, argcount) STUB_U name, argcount

#include "w32ksvc64.h"

#else

.code

SyscallId = HEX(1000)
#define SVC_(name, argcount) STUB_U name, argcount

#ifdef _WIN64
#include "w32ksvc64.h"
#else
#include "w32ksvc32.h"
#endif

#if !SYSCALLS_ARM64_STUBS
    END
#endif
#endif
