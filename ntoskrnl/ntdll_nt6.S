
#include <syscalls.inc>

/* Skip NT5 syscalls, so we get the right IDs for the NT6 ones */
#define SYSFUNCS_NT5_ONLY
#define SVC_(name, argcount) SyscallId = SyscallId + 1
#define SVC_WRAP_(name, argcount) SyscallId = SyscallId + 1
#include <sysfuncs.h>
#undef SVC_WRAP_
#undef SVC_
#undef SYSFUNCS_NT5_ONLY

/* Now do the NT6 ones proper */
#define SYSFUNCS_NT6_ONLY

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_U name
#define SVC_WRAP_(name, argcount) STUB_U name

#include <sysfuncs.h>

    END

#elif defined(_M_ARM64) || defined(_M_ARM64EC) || defined(_ARM64_) || defined(_ARM64EC_) || defined(__aarch64__) || defined(__arm64__) || defined(__arm64ec__)

    .text

#define SVC_(name, argcount) STUB_U name, argcount
#define SVC_WRAP_(name, argcount) STUB_WRAP name, argcount

#include <sysfuncs.h>

#else

.code

#define SVC_(name, argcount) STUB_U name, argcount
#define SVC_WRAP_(name, argcount) STUB_U name, argcount

#include <sysfuncs.h>

END
#endif
