
#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_U name

#include <internal/syscalls_body.h>

    END

#elif defined(_M_ARM64) || defined(_ARM64_) || defined(__aarch64__) || defined(__arm64__)

    .text

#define SVC_(name, argcount) STUB_U name, argcount

#include <internal/syscalls_body.h>

#else

.code

#define SVC_(name, argcount) STUB_U name, argcount

#include <internal/syscalls_body.h>

END
#endif
