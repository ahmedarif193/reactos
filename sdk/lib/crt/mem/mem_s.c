/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Bounds-checked (secure) memory copy functions.
 */

typedef __SIZE_TYPE__ crt_size_t;
typedef int crt_errno_t;

#define CRT_EINVAL 22
#define CRT_ERANGE 34

extern void * __cdecl memmove(void *dst, const void *src, crt_size_t n);
extern void * __cdecl memset(void *dst, int c, crt_size_t n);

crt_errno_t __cdecl memcpy_s(void *dest, crt_size_t destsz, const void *src, crt_size_t count)
{
    if (count == 0) return 0;
    if (dest == 0) return CRT_EINVAL;
    if (src == 0 || destsz < count) { memset(dest, 0, destsz); return (src == 0) ? CRT_EINVAL : CRT_ERANGE; }
    memmove(dest, src, count);
    return 0;
}

crt_errno_t __cdecl memmove_s(void *dest, crt_size_t destsz, const void *src, crt_size_t count)
{
    if (count == 0) return 0;
    if (dest == 0) return CRT_EINVAL;
    if (src == 0 || destsz < count) { memset(dest, 0, destsz); return (src == 0) ? CRT_EINVAL : CRT_ERANGE; }
    memmove(dest, src, count);
    return 0;
}
