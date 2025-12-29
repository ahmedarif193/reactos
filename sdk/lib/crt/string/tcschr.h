
#include <tchar.h>

#ifndef CRT_WEAK_ATTR
#define CRT_WEAK_ATTR
#endif

CRT_WEAK_ATTR _TCHAR * _tcschr(const _TCHAR * s, _XINT c)
{
 _TCHAR cc = c;

 while(*s)
 {
  if(*s == cc) return (_TCHAR *)s;

  s++;
 }

 if(cc == 0) return (_TCHAR *)s;

 return 0;
}

/* EOF */
