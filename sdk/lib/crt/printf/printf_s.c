/*
 * PROJECT:     ReactOS CRT
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Bounds-checked (secure) printf family. These delegate to the
 *              existing _vsnprintf / _vsnwprintf engines and NUL-terminate.
 */

typedef __SIZE_TYPE__ crt_size_t;
typedef unsigned short crt_wchar_t;

#define CRT_TRUNCATE ((crt_size_t)-1)

extern int __cdecl _vsnprintf(char *buf, crt_size_t n, const char *fmt, __builtin_va_list ap);
extern int __cdecl _vsnwprintf(crt_wchar_t *buf, crt_size_t n, const crt_wchar_t *fmt, __builtin_va_list ap);

static int crt_vsnprintf_s(char *buf, crt_size_t size, crt_size_t count, const char *fmt, __builtin_va_list ap)
{
    crt_size_t maxchars; int r;
    if (buf == 0 || size == 0 || fmt == 0) { if (buf && size) buf[0] = 0; return -1; }
    maxchars = size - 1;
    if (count != CRT_TRUNCATE && count < maxchars) maxchars = count;
    r = _vsnprintf(buf, maxchars, fmt, ap);
    if (r < 0 || (crt_size_t)r > maxchars) { buf[maxchars] = 0; return -1; }
    buf[r] = 0;
    return r;
}

static int crt_vsnwprintf_s(crt_wchar_t *buf, crt_size_t size, crt_size_t count, const crt_wchar_t *fmt, __builtin_va_list ap)
{
    crt_size_t maxchars; int r;
    if (buf == 0 || size == 0 || fmt == 0) { if (buf && size) buf[0] = 0; return -1; }
    maxchars = size - 1;
    if (count != CRT_TRUNCATE && count < maxchars) maxchars = count;
    r = _vsnwprintf(buf, maxchars, fmt, ap);
    if (r < 0 || (crt_size_t)r > maxchars) { buf[maxchars] = 0; return -1; }
    buf[r] = 0;
    return r;
}

int __cdecl sprintf_s(char *buf, crt_size_t size, const char *fmt, ...)
{ __builtin_va_list ap; int r; __builtin_va_start(ap, fmt); r = crt_vsnprintf_s(buf, size, CRT_TRUNCATE, fmt, ap); __builtin_va_end(ap); return r; }

int __cdecl vsprintf_s(char *buf, crt_size_t size, const char *fmt, __builtin_va_list ap)
{ return crt_vsnprintf_s(buf, size, CRT_TRUNCATE, fmt, ap); }

int __cdecl _snprintf_s(char *buf, crt_size_t size, crt_size_t count, const char *fmt, ...)
{ __builtin_va_list ap; int r; __builtin_va_start(ap, fmt); r = crt_vsnprintf_s(buf, size, count, fmt, ap); __builtin_va_end(ap); return r; }

int __cdecl _vsnprintf_s(char *buf, crt_size_t size, crt_size_t count, const char *fmt, __builtin_va_list ap)
{ return crt_vsnprintf_s(buf, size, count, fmt, ap); }

int __cdecl swprintf_s(crt_wchar_t *buf, crt_size_t size, const crt_wchar_t *fmt, ...)
{ __builtin_va_list ap; int r; __builtin_va_start(ap, fmt); r = crt_vsnwprintf_s(buf, size, CRT_TRUNCATE, fmt, ap); __builtin_va_end(ap); return r; }

int __cdecl vswprintf_s(crt_wchar_t *buf, crt_size_t size, const crt_wchar_t *fmt, __builtin_va_list ap)
{ return crt_vsnwprintf_s(buf, size, CRT_TRUNCATE, fmt, ap); }

int __cdecl _snwprintf_s(crt_wchar_t *buf, crt_size_t size, crt_size_t count, const crt_wchar_t *fmt, ...)
{ __builtin_va_list ap; int r; __builtin_va_start(ap, fmt); r = crt_vsnwprintf_s(buf, size, count, fmt, ap); __builtin_va_end(ap); return r; }

int __cdecl _vsnwprintf_s(crt_wchar_t *buf, crt_size_t size, crt_size_t count, const crt_wchar_t *fmt, __builtin_va_list ap)
{ return crt_vsnwprintf_s(buf, size, count, fmt, ap); }
