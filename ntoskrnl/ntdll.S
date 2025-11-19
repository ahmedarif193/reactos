
#include <syscalls.inc>

#ifdef _M_ARM

    TEXTAREA

#define SVC_(name, argcount) STUB_U name

#include <internal/syscalls_body.h>

    END

#else

.code

#define SVC_(name, argcount) STUB_U name, argcount

#include <internal/syscalls_body.h>

END
#endif
