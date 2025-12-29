
#include <stddef.h>
#include <tchar.h>

#ifdef _MSC_VER
#pragma function(_tcslen)
#endif /* _MSC_VER */

#ifndef CRT_WEAK_ATTR
#define CRT_WEAK_ATTR
#endif

CRT_WEAK_ATTR size_t __cdecl _tcslen(const _TCHAR * str)
{
 const _TCHAR * s;

 for(s = str; *s; ++ s);

 return s - str;
}

/* EOF */
