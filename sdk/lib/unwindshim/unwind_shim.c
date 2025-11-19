#include <stddef.h>

#if defined(_M_IX86) || defined(__i386__)
/*
 * Provide a DWARF-style _Unwind_Resume that forwards to the SJLJ
 * unwinder exported by the MinGW i686 runtime. This matches the
 * semantics expected by Rust's libstd/backtrace on i686-gnu when
 * linking against a SJLJ-based toolchain.
 */
void _Unwind_SjLj_Resume(void *exception_object);

void _Unwind_Resume(void *exception_object)
{
    _Unwind_SjLj_Resume(exception_object);
}
#endif
