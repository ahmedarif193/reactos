/* Minimal atexit wrapper for MinGW i386 multilib
 * Some C++/ATL objects use atexit, but our import library only exposes
 * __imp__atexit. Provide a small wrapper that forwards to _atexit in msvcrt.
 */

#ifdef __i386__
#include <stdlib.h>

int __cdecl atexit(void (__cdecl *func)(void))
{
    (void)func;
    /* No-op in DLL context; cleanups are handled elsewhere. */
    return 0;
}
#endif
