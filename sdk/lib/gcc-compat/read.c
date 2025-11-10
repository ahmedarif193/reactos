/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS CRT compatibility helpers
 * LICENSE:     GPL-2.0-or-later (compatible with GCC Runtime Library Exception)
 * PURPOSE:     Provide a POSIX-style read() symbol for libstdc++
 */

#if defined(_CRTIMP)
#define HAVE_ORIGINAL_CRTIMP 1
#pragma push_macro("_CRTIMP")
#undef _CRTIMP
#define _CRTIMP
#else
#define UNDEFINED_CRTIMP 1
#define _CRTIMP
#endif
#include <io.h>
#ifdef HAVE_ORIGINAL_CRTIMP
#pragma pop_macro("_CRTIMP")
#undef HAVE_ORIGINAL_CRTIMP
#elif defined(UNDEFINED_CRTIMP)
#undef _CRTIMP
#undef UNDEFINED_CRTIMP
#endif

/*
 * MinGW-built libstdc++ assumes the presence of a POSIX read() function
 * when _GLIBCXX_HAVE_UNISTD_H is defined. ReactOS links against msvcrt,
 * which only exposes the _read() variant, so we provide the unmangled
 * synonym here.
 */
int read(int fd, void *buffer, unsigned int count)
{
    return _read(fd, buffer, count);
}
