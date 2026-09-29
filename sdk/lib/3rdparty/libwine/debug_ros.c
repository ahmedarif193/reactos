/* The use of these four functions was creating unwanted imports
 * from msvcrt.dll in kernel32.dll. */

#define malloc libwine_malloc
#define free libwine_free
#define realloc libwine_realloc
#define _strdup libwine__strdup

#include "debug.c"

__MINGW_ATTRIB_MALLOC
void * __cdecl malloc(size_t size)
{
    return RtlAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, size);
}

void __cdecl free(void *ptr)
{
    if (ptr) RtlFreeHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, ptr);
}

void * __cdecl realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return malloc(size);
    return RtlReAllocateHeap(NtCurrentTeb()->Peb->ProcessHeap, 0, ptr, size);
}

__MINGW_ATTRIB_MALLOC
char * __cdecl _strdup(const char *str)
{
    char *newstr = malloc(strlen(str) + 1);
    if (newstr) strcpy(newstr, str);
    return newstr;
}
