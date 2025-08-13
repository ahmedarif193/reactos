
#include <stddef.h>
#include <tchar.h>

size_t __cdecl _tcsnlen(const _TCHAR * str, size_t count)
{
 const _TCHAR * s;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnonnull-compare"
 if(str == 0) return 0;
#pragma GCC diagnostic pop

 for(s = str; *s && count; ++ s, -- count);

 return s - str;
}

/* EOF */
