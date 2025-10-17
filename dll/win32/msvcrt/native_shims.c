/*
 * PROJECT:         ReactOS CRT
 * LICENSE:         MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:         Native MinGW CRT bridge shims
 * COPYRIGHT:       2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <windef.h>

#ifdef USE_NATIVE_MINGW_CRT

#include <ctype.h>
#include <errno.h>
#include <io.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include <excpt.h>
#include <setjmp.h>

#include <debug.h>

#ifndef _SE_TRANSLATOR_FUNCTION_DEFINED
typedef void (__cdecl *_se_translator_function)(unsigned int, struct _EXCEPTION_POINTERS *);
#endif

#ifdef _fstat32
#undef _fstat32
#endif
#ifdef _fstat32i64
#undef _fstat32i64
#endif
#ifdef _fstat64i32
#undef _fstat64i32
#endif
#ifdef _stat32
#undef _stat32
#endif
#ifdef _stat32i64
#undef _stat32i64
#endif
#ifdef _stat64i32
#undef _stat64i32
#endif
#ifdef _wstat32
#undef _wstat32
#endif
#ifdef _wstat32i64
#undef _wstat32i64
#endif
#ifdef _wstat64i32
#undef _wstat64i32
#endif

/* ------------------------------------------------------------------------- */
/* Legacy data exports required by the MinGW CRT.                            */
/* ------------------------------------------------------------------------- */

char **__dcrt_initial_narrow_environment = NULL;

void (*_aexit_rtn)(int) = NULL;

char *_acmdln = NULL;
char *_pgmptr = NULL;
char **_argv = NULL;
char **__argv = NULL;
char **__initenv = NULL;
char **_environ = NULL;

wchar_t *_wcmdln = NULL;
wchar_t *_wpgmptr = NULL;
wchar_t **_wenviron = NULL;
wchar_t **__winitenv = NULL;
wchar_t **__wargv = NULL;

unsigned char _mbcasemap[257] = {0};
unsigned char _mbctype[257] = {0};
unsigned short const _wctype[257] = {0};

long _timezone = 0;
char *_tzname[2] = {NULL, NULL};
static char *__tzname_store[2] = {NULL, NULL};
int _daylight = 0;
long _dstbias = 0;

static int current_app_type = 0;

/* Additional runtime state variables */
unsigned int _amblksiz = 16384;
int _commode = 0;
int _fileinfo = 0;
int _fmode = 0;

int __argc = 0;
int __mb_cur_max = 1;
unsigned int __lc_codepage = 0;
unsigned int MSVCRT___lc_collate_cp = 0;
int MSVCRT___lc_handle[6] = {0};
int __setlc_active = 0;
int __unguarded_readlc_active = 0;

unsigned int _osplatform = 0;
unsigned int _osver = 0;
unsigned int _winmajor = 0;
unsigned int _winminor = 0;
unsigned int _winver = 0;

const unsigned short *_ctype = NULL;
const unsigned short *_pctype = NULL;
const unsigned short *_pwctype = NULL;

char *_sys_errlist[1] = {NULL};
char **__sys_errlist = _sys_errlist;
int _sys_nerr = 0;
int *__sys_nerr = &_sys_nerr;
void *__badioinfo = NULL;
void *__pioinfo[64] = {0};
FILE _iob[20] = {0};

#define MSVCRT_IOB_COUNT (sizeof(_iob) / sizeof(_iob[0]))

FILE *__cdecl __iob_func(void)
{
    return _iob;
}

#ifdef __GNUC__
const double _HUGE = __builtin_huge_val();
#else
const double _HUGE = 1.7976931348623158e+308;
#endif

static unsigned __int64 __msvcrt_printf_options = 0;

static unsigned __int64 *__cdecl __local_stdio_printf_options(void)
{
    return &__msvcrt_printf_options;
}

/* ------------------------------------------------------------------------- */
/* Imported helpers from the toolchain CRT.                                  */
/* ------------------------------------------------------------------------- */

__declspec(dllimport) unsigned char *__cdecl __p__mbcasemap(void);
__declspec(dllimport) unsigned char *__cdecl __p__mbctype(void);
__declspec(dllimport) int *__cdecl __p__commode(void);
__declspec(dllimport) int *__cdecl __p__fmode(void);
unsigned int *__cdecl __p__amblksiz(void);
int *__cdecl __p__fileinfo(void);
const unsigned short **__cdecl __p__pctype(void);
__declspec(dllimport) const unsigned short *__cdecl __pwctype_func(void);
__declspec(dllimport) int *__cdecl __p___argc(void);
__declspec(dllimport) char ***__cdecl __p___argv(void);
__declspec(dllimport) char ***__cdecl __p___initenv(void);
__declspec(dllimport) char **__cdecl __p__acmdln(void);
__declspec(dllimport) char **__cdecl __p__pgmptr(void);
__declspec(dllimport) wchar_t **__cdecl __p__wcmdln(void);
__declspec(dllimport) wchar_t **__cdecl __p__wpgmptr(void);
__declspec(dllimport) wchar_t ***__cdecl __p___winitenv(void);
__declspec(dllimport) wchar_t ***__cdecl __p___wargv(void);
int *__cdecl __p___mb_cur_max(void);
__declspec(dllimport) char ***__cdecl __p__environ(void);
__declspec(dllimport) wchar_t ***__cdecl __p__wenviron(void);
__declspec(dllimport) unsigned int *__cdecl __p__winmajor(void);
__declspec(dllimport) unsigned int *__cdecl __p__winminor(void);
__declspec(dllimport) unsigned int *__cdecl __p__winver(void);
__declspec(dllimport) unsigned int *__cdecl __p__osplatform(void);
__declspec(dllimport) unsigned int *__cdecl __p__osver(void);
__declspec(dllimport) long *__cdecl __p__timezone(void);
__declspec(dllimport) long *__cdecl __dstbias(void);
__declspec(dllimport) int *__cdecl __daylight(void);
__declspec(dllimport) char **__cdecl __p__tzname(void);
__declspec(dllimport) intptr_t __cdecl _findfirst64(const char *, struct __finddata64_t *);
__declspec(dllimport) int __cdecl _findnext64(intptr_t, struct __finddata64_t *);
__declspec(dllimport) intptr_t __cdecl _wfindfirst64(const wchar_t *, struct _wfinddata64_t *);
__declspec(dllimport) int __cdecl _wfindnext64(intptr_t, struct _wfinddata64_t *);
__declspec(dllimport) void __cdecl _invoke_watson(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t);

/* MinGW CRT exit handling - use available MinGW symbol _onexit. */
typedef int (__cdecl *_onexit_t)(void);
__declspec(dllimport) _onexit_t __cdecl _onexit(_onexit_t func);

__declspec(dllimport) int __cdecl __stdio_common_vsnprintf_s(unsigned __int64, char *, size_t, size_t, const char *, _locale_t, va_list);

/* ------------------------------------------------------------------------- */
/* Synchronise exported state with the MinGW CRT.                            */
/* ------------------------------------------------------------------------- */

static void sync_native_globals(void)
{
    unsigned char *mbmap = __p__mbcasemap();
    if (mbmap)
        memcpy(_mbcasemap, mbmap, sizeof(_mbcasemap));

    unsigned char *mbct = __p__mbctype();
    if (mbct)
        memcpy(_mbctype, mbct, sizeof(_mbctype));

    long *tz = __p__timezone();
    if (tz)
        _timezone = *tz;

    char **tzn = __p__tzname();
    if (tzn)
    {
        _tzname[0] = tzn[0];
        _tzname[1] = tzn[1];
        __tzname_store[0] = tzn[0];
        __tzname_store[1] = tzn[1];
    }

    int *dl = __daylight();
    if (dl)
        _daylight = *dl;

    long *dst = __dstbias();
    if (dst)
        _dstbias = *dst;

    char **acmd = __p__acmdln();
    if (acmd)
        _acmdln = *acmd;

    char **pgm = __p__pgmptr();
    if (pgm)
        _pgmptr = *pgm;

    char ***env = __p__environ();
    if (env)
        _environ = *env;

    char ***initenv = __p___initenv();
    if (initenv)
        __initenv = *initenv;

    char ***argv = __p___argv();
    if (argv)
        __argv = _argv = *argv;

    wchar_t **wcmd = __p__wcmdln();
    if (wcmd)
        _wcmdln = *wcmd;

    wchar_t **wpgm = __p__wpgmptr();
    if (wpgm)
        _wpgmptr = *wpgm;

    wchar_t ***wenv = __p__wenviron();
    if (wenv)
        _wenviron = *wenv;

    wchar_t ***winit = __p___winitenv();
    if (winit)
        __winitenv = *winit;

    wchar_t ***warg = __p___wargv();
    if (warg)
        __wargv = *warg;

    int *argc = __p___argc();
    if (argc)
        __argc = *argc;

    int *mbmax = __p___mb_cur_max();
    if (mbmax)
        __mb_cur_max = *mbmax;

    unsigned int *major = __p__winmajor();
    if (major)
        _winmajor = *major;

    unsigned int *minor = __p__winminor();
    if (minor)
        _winminor = *minor;

    unsigned int *ver = __p__winver();
    if (ver)
        _winver = *ver;

    unsigned int *plat = __p__osplatform();
    if (plat)
        _osplatform = *plat;

    unsigned int *osv = __p__osver();
    if (osv)
        _osver = *osv;

    const unsigned short *pwct = __pwctype_func();
    if (pwct)
    {
        _pwctype = pwct;
        _pctype = pwct;
        _ctype = pwct;
    }

    const unsigned short **pct = __p__pctype();
    if (pct && *pct)
        _pctype = *pct;

    int *commode = __p__commode();
    if (commode)
        _commode = *commode;

#ifndef _WIN64
    unsigned int *amblk = __p__amblksiz();
    if (amblk)
        _amblksiz = *amblk;

    int *fileinfo = __p__fileinfo();
    if (fileinfo)
        _fileinfo = *fileinfo;
#endif

    int *fmode = __p__fmode();
    if (fmode)
        _fmode = *fmode;

    FILE *iob = __iob_func();
    if (iob)
        memcpy(_iob, iob, sizeof(_iob));

    __dcrt_initial_narrow_environment = _environ;
}

unsigned int *__cdecl __p__amblksiz(void)
{
    return &_amblksiz;
}

int *__cdecl __p__fileinfo(void)
{
    return &_fileinfo;
}

const unsigned short **__cdecl __p__pctype(void)
{
    return (const unsigned short **)&_pctype;
}

int *__cdecl __p___mb_cur_max(void)
{
    return &__mb_cur_max;
}

int __cdecl __get_app_type(void)
{
    return current_app_type;
}

void __cdecl __set_app_type(int app_type)
{
    current_app_type = app_type;
}

void __cdecl _set_app_type(int app_type)
{
    __set_app_type(app_type);
}

/* ------------------------------------------------------------------------- */
/* Environment & startup initialization API forwarders.                      */
/* These functions configure the runtime environment during process startup. */
/* ------------------------------------------------------------------------- */

int __cdecl _initialize_narrow_environment(void)
{
    /* Sync the environment from MinGW CRT to our exported globals.
     * The actual initialization is done by MinGW CRT's __getmainargs or
     * its internal startup code. We just need to synchronize the state. */
    char ***env = __p__environ();
    if (env && *env)
    {
        _environ = *env;
        __initenv = *env;
        __dcrt_initial_narrow_environment = *env;
    }
    return 0;
}

int __cdecl _initialize_wide_environment(void)
{
    /* Sync the wide environment from MinGW CRT to our exported globals. */
    wchar_t ***wenv = __p__wenviron();
    if (wenv && *wenv)
    {
        _wenviron = *wenv;
        __winitenv = *wenv;
    }
    return 0;
}

int __cdecl _configure_narrow_argv(unsigned int mode)
{
    /* Configure command-line argument parsing mode.
     * Mode values:
     *   0 = no processing
     *   1 = no wildcard expansion
     *   2 = wildcard expansion enabled
     *
     * MinGW CRT handles argv parsing internally in __getmainargs.
     * For compatibility, we synchronize the already-parsed arguments. */
    (void)mode;

    char ***argv = __p___argv();
    if (argv && *argv)
    {
        __argv = _argv = *argv;
    }

    int *argc = __p___argc();
    if (argc)
    {
        __argc = *argc;
    }

    return 0;
}

int __cdecl _configure_wide_argv(unsigned int mode)
{
    /* Configure wide command-line argument parsing mode. */
    (void)mode;

    wchar_t ***wargv = __p___wargv();
    if (wargv && *wargv)
    {
        __wargv = *wargv;
    }

    return 0;
}

int __cdecl __crt_atexit(void (__cdecl *func)(void))
{
    /* Use MinGW's _onexit for exit registration.
     * MinGW handles the exit list internally through _onexit. */
    if (!func)
        return -1;

    return (_onexit((_onexit_t)func) == NULL) ? -1 : 0;
}

int __cdecl __crt_at_quick_exit(void (__cdecl *func)(void))
{
    /* MinGW doesn't provide at_quick_exit/quick_exit in msvcrt.
     * This is a C11 feature that MinGW doesn't fully support.
     * Fall back to regular atexit registration.
     * Note: This means quick_exit will behave like exit(). */
    if (!func)
        return -1;

    /* Use the same mechanism as __crt_atexit */
    return __crt_atexit(func);
}

/* ------------------------------------------------------------------------- */
/* Basic entry points.                                                       */
/* ------------------------------------------------------------------------- */

void __cdecl __msvcrt_native_attach(void)
{
    sync_native_globals();
}

int __cdecl __mingw_module_is_dll(void)
{
    return 1;
}

FILE *__cdecl __acrt_iob_func(unsigned int index)
{
    FILE *table = __iob_func();

    if (!table)
        table = _iob;

    if (!table || index >= MSVCRT_IOB_COUNT)
        return NULL;

    return &table[index];
}

long *__cdecl __timezone(void)
{
    sync_native_globals();

    long *tz = __p__timezone();
    return tz ? tz : &_timezone;
}

char **__cdecl __tzname(void)
{
    sync_native_globals();

    char **tzn = __p__tzname();
    if (tzn)
        return tzn;

    return __tzname_store;
}

int __cdecl _get_osplatform(unsigned int *value)
{
    if (!value)
    {
        errno = EINVAL;
        return EINVAL;
    }

    sync_native_globals();
    *value = _osplatform;
    return 0;
}

unsigned char *__cdecl reactos_p__mbcasemap(void)
{
    sync_native_globals();

    unsigned char *map = __p__mbcasemap();
    return map ? map : _mbcasemap;
}

unsigned char *__cdecl reactos_p__mbctype(void)
{
    sync_native_globals();

    unsigned char *map = __p__mbctype();
    return map ? map : _mbctype;
}

int *__cdecl reactos_p__commode(void)
{
    sync_native_globals();

    int *ptr = __p__commode();
    return ptr ? ptr : &_commode;
}

int *__cdecl reactos_p__fmode(void)
{
    sync_native_globals();

    int *ptr = __p__fmode();
    return ptr ? ptr : &_fmode;
}
#ifndef _WIN64

unsigned int *__cdecl reactos_p__amblksiz(void)
{
    sync_native_globals();

    unsigned int *ptr = __p__amblksiz();
    return ptr ? ptr : &_amblksiz;
}

int *__cdecl reactos_p__fileinfo(void)
{
    sync_native_globals();

    int *ptr = __p__fileinfo();
    return ptr ? ptr : &_fileinfo;
}

#endif /* !_WIN64 */

char ***__cdecl reactos_p__environ(void)
{
    sync_native_globals();

    char ***env = __p__environ();
    return env ? env : &_environ;
}

wchar_t ***__cdecl reactos_p__wenviron(void)
{
    sync_native_globals();

    wchar_t ***env = __p__wenviron();
    return env ? env : &_wenviron;
}

int *__cdecl reactos_p___argc(void)
{
    sync_native_globals();

    int *ptr = __p___argc();
    return ptr ? ptr : &__argc;
}

char ***__cdecl reactos_p___argv(void)
{
    sync_native_globals();

    char ***ptr = __p___argv();
    return ptr ? ptr : &__argv;
}

char ***__cdecl reactos_p___initenv(void)
{
    sync_native_globals();

    char ***ptr = __p___initenv();
    return ptr ? ptr : &__initenv;
}

int *__cdecl reactos_p___mb_cur_max(void)
{
    sync_native_globals();

    int *ptr = __p___mb_cur_max();
    return ptr ? ptr : &__mb_cur_max;
}

wchar_t ***__cdecl reactos_p___wargv(void)
{
    sync_native_globals();

    wchar_t ***ptr = __p___wargv();
    return ptr ? ptr : &__wargv;
}

wchar_t ***__cdecl reactos_p___winitenv(void)
{
    sync_native_globals();

    wchar_t ***ptr = __p___winitenv();
    return ptr ? ptr : &__winitenv;
}

char **__cdecl reactos_p__acmdln(void)
{
    sync_native_globals();

    char **ptr = __p__acmdln();
    return ptr ? ptr : &_acmdln;
}

char **__cdecl reactos_p__pgmptr(void)
{
    sync_native_globals();

    char **ptr = __p__pgmptr();
    return ptr ? ptr : &_pgmptr;
}

wchar_t **__cdecl reactos_p__wcmdln(void)
{
    sync_native_globals();

    wchar_t **ptr = __p__wcmdln();
    return ptr ? ptr : &_wcmdln;
}

wchar_t **__cdecl reactos_p__wpgmptr(void)
{
    sync_native_globals();

    wchar_t **ptr = __p__wpgmptr();
    return ptr ? ptr : &_wpgmptr;
}

#if defined(_M_IX86) || defined(__i386__)

void __cdecl _adj_fpatan(void) {}
void __cdecl _adj_fprem(void) {}
void __cdecl _adj_fprem1(void) {}
void __cdecl _adj_fptan(void) {}
void __cdecl _safe_fdiv(void) {}
void __cdecl _safe_fdivr(void) {}
void __cdecl _safe_fprem(void) {}
void __cdecl _safe_fprem1(void) {}

long __cdecl _ftol2(void)
{
    long result;
    __asm__ __volatile__("fistpl %0" : "=m"(result));
    return result;
}

long __cdecl _ftol2_sse(void)
{
    long result;
    __asm__ __volatile__("fistpl %0" : "=m"(result));
    return result;
}

#endif /* _M_IX86 || __i386__ */

#ifndef _abnormal_termination
int __cdecl _abnormal_termination(void)
{
    return 0;
}
#endif

#if defined(USE_MINGW_SETJMP_TWO_ARGS)
int __cdecl _setjmp(jmp_buf env, void *ctx)
{
    (void)ctx;
    return __builtin_setjmp(env);
}
#else
int __cdecl _setjmp(jmp_buf env)
{
    return __builtin_setjmp(env);
}
#endif

/* ------------------------------------------------------------------------- */
/* Secure printf wrappers backed by UCRT helpers.                            */
/* ------------------------------------------------------------------------- */

static int __cdecl msvcrt_vsnprintf_s_internal(char *buffer,
                                               size_t size_of_buffer,
                                               size_t count,
                                               const char *format,
                                               _locale_t locale,
                                               va_list argptr)
{
    int const result = __stdio_common_vsnprintf_s(
        *__local_stdio_printf_options(),
        buffer,
        size_of_buffer,
        count,
        format,
        locale,
        argptr);

    return (result < 0) ? -1 : result;
}

int __cdecl _vsnprintf_s_l(char *buffer,
                           size_t size_of_buffer,
                           size_t count,
                           const char *format,
                           _locale_t locale,
                           va_list argptr)
{
    return msvcrt_vsnprintf_s_internal(buffer, size_of_buffer, count, format, locale, argptr);
}

int __cdecl _vsnprintf_s(char *buffer,
                         size_t size_of_buffer,
                         size_t count,
                         const char *format,
                         va_list argptr)
{
    return _vsnprintf_s_l(buffer, size_of_buffer, count, format, NULL, argptr);
}

int __cdecl _snprintf_s_l(char *buffer,
                          size_t size_of_buffer,
                          size_t count,
                          const char *format,
                          _locale_t locale,
                          ...)
{
    int result;
    va_list args;

    va_start(args, locale);
    result = _vsnprintf_s_l(buffer, size_of_buffer, count, format, locale, args);
    va_end(args);
    return result;
}

int __cdecl _snprintf_s(char *buffer,
                        size_t size_of_buffer,
                        size_t count,
                        const char *format,
                        ...)
{
    int result;
    va_list args;

    va_start(args, format);
    result = _vsnprintf_s_l(buffer, size_of_buffer, count, format, NULL, args);
    va_end(args);
    return result;
}

/* ------------------------------------------------------------------------- */
/* Unsigned 64-bit string conversion helpers.                                */
/* ------------------------------------------------------------------------- */

static unsigned __int64
msvcrt_parse_u64(const char *nptr, char **endptr, int base)
{
    const char *start = nptr;
    const char *s;
    unsigned __int64 result = 0;
    unsigned __int64 threshold;
    unsigned __int64 remainder_limit;
    int negative = 0;
    int any = 0;
    int overflow = 0;

    if (!nptr)
    {
        if (endptr)
            *endptr = NULL;
        return 0;
    }

    s = nptr;
    while (isspace((unsigned char)*s))
        ++s;

    if (base != 0 && (base < 2 || base > 36))
    {
        if (endptr)
            *endptr = (char *)start;
        return 0;
    }

    if (*s == '+' || *s == '-')
    {
        negative = (*s == '-');
        ++s;
    }

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    {
        base = 16;
        s += 2;
    }

    if (base == 0)
        base = (*s == '0') ? 8 : 10;

    threshold = ULLONG_MAX / (unsigned)base;
    remainder_limit = ULLONG_MAX % (unsigned)base;

    for (;; ++s)
    {
        unsigned char c = (unsigned char)*s;
        unsigned digit;

        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'A' && c <= 'Z')
            digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z')
            digit = c - 'a' + 10;
        else
            break;

        if (digit >= (unsigned)base)
            break;

        if (!overflow)
        {
            if (result > threshold || (result == threshold && digit > remainder_limit))
            {
                result = ULLONG_MAX;
                overflow = 1;
                errno = ERANGE;
            }
            else
            {
                result = result * (unsigned)base + digit;
            }
        }

        any = 1;
    }

    if (!any)
    {
        if (endptr)
            *endptr = (char *)start;
        return 0;
    }

    if (endptr)
        *endptr = (char *)s;

    if (!overflow && negative)
        result = (unsigned __int64)(-(long long)result);

    return result;
}

unsigned __int64 __cdecl _strtoui64_l(const char *nptr,
                                      char **endptr,
                                      int base,
                                      _locale_t locale)
{
    (void)locale;
    return msvcrt_parse_u64(nptr, endptr, base);
}

unsigned __int64 __cdecl _strtoui64(const char *nptr,
                                    char **endptr,
                                    int base)
{
    return _strtoui64_l(nptr, endptr, base, NULL);
}

unsigned __int64 __cdecl strtoull(const char *nptr,
                                   char **endptr,
                                   int base)
{
    return _strtoui64(nptr, endptr, base);
}

/* ------------------------------------------------------------------------- */
/* Filesystem enumeration compatibility (_findfirst64i32/_findnext64i32).    */
/* ------------------------------------------------------------------------- */

static void
msvcrt_copy_finddata64_to_finddata64i32(const struct __finddata64_t *src,
                                        struct _finddata64i32_t *dst)
{
    dst->attrib       = src->attrib;
    dst->time_create  = src->time_create;
    dst->time_access  = src->time_access;
    dst->time_write   = src->time_write;
    dst->size         = (_fsize_t)src->size;
    memcpy(dst->name, src->name, sizeof(dst->name));
}

static void
msvcrt_copy_wfinddata64_to_wfinddata64i32(const struct _wfinddata64_t *src,
                                          struct _wfinddata64i32_t *dst)
{
    dst->attrib       = src->attrib;
    dst->time_create  = src->time_create;
    dst->time_access  = src->time_access;
    dst->time_write   = src->time_write;
    dst->size         = (_fsize_t)src->size;
    memcpy(dst->name, src->name, sizeof(dst->name));
}

intptr_t __cdecl _findfirst64i32(const char *pattern, struct _finddata64i32_t *result)
{
    struct __finddata64_t temp;

    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    intptr_t handle = _findfirst64(pattern, &temp);
    if (handle == -1)
        return -1;

    msvcrt_copy_finddata64_to_finddata64i32(&temp, result);
    return handle;
}

int __cdecl _findnext64i32(intptr_t handle, struct _finddata64i32_t *result)
{
    struct __finddata64_t temp;

    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_findnext64(handle, &temp) == -1)
        return -1;

    msvcrt_copy_finddata64_to_finddata64i32(&temp, result);
    return 0;
}

intptr_t __cdecl _wfindfirst64i32(const wchar_t *pattern, struct _wfinddata64i32_t *result)
{
    struct _wfinddata64_t temp;

    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    intptr_t handle = _wfindfirst64(pattern, &temp);
    if (handle == -1)
        return -1;

    msvcrt_copy_wfinddata64_to_wfinddata64i32(&temp, result);
    return handle;
}

int __cdecl _wfindnext64i32(intptr_t handle, struct _wfinddata64i32_t *result)
{
    struct _wfinddata64_t temp;

    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_wfindnext64(handle, &temp) == -1)
        return -1;

    msvcrt_copy_wfinddata64_to_wfinddata64i32(&temp, result);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Filesystem compatibility layers (_stat32/_wstat32 variants).              */
/* ------------------------------------------------------------------------- */

static void
msvcrt_copy_stat64_to_stat32(const struct _stat64 *src, struct _stat32 *dst)
{
    dst->st_dev   = src->st_dev;
    dst->st_ino   = src->st_ino;
    dst->st_mode  = src->st_mode;
    dst->st_nlink = src->st_nlink;
    dst->st_uid   = src->st_uid;
    dst->st_gid   = src->st_gid;
    dst->st_rdev  = src->st_rdev;
    dst->st_size  = (_off_t)src->st_size;
    dst->st_atime = (time_t)src->st_atime;
    dst->st_mtime = (time_t)src->st_mtime;
    dst->st_ctime = (time_t)src->st_ctime;
}

static void
msvcrt_copy_stat64_to_stat32i64(const struct _stat64 *src, struct _stat32i64 *dst)
{
    dst->st_dev   = src->st_dev;
    dst->st_ino   = src->st_ino;
    dst->st_mode  = src->st_mode;
    dst->st_nlink = src->st_nlink;
    dst->st_uid   = src->st_uid;
    dst->st_gid   = src->st_gid;
    dst->st_rdev  = src->st_rdev;
    dst->st_size  = src->st_size;
    dst->st_atime = (__time32_t)src->st_atime;
    dst->st_mtime = (__time32_t)src->st_mtime;
    dst->st_ctime = (__time32_t)src->st_ctime;
}

static void
msvcrt_copy_stat64_to_stat64i32(const struct _stat64 *src, struct _stat64i32 *dst)
{
    dst->st_dev   = src->st_dev;
    dst->st_ino   = src->st_ino;
    dst->st_mode  = src->st_mode;
    dst->st_nlink = src->st_nlink;
    dst->st_uid   = src->st_uid;
    dst->st_gid   = src->st_gid;
    dst->st_rdev  = src->st_rdev;
    dst->st_size  = (_off_t)src->st_size;
    dst->st_atime = src->st_atime;
    dst->st_mtime = src->st_mtime;
    dst->st_ctime = src->st_ctime;
}

int __cdecl _stat32(const char *path, struct _stat32 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_stat64(path, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat32(&temp, result);
    return 0;
}

int __cdecl _stat32i64(const char *path, struct _stat32i64 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_stat64(path, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat32i64(&temp, result);
    return 0;
}

int __cdecl _stat64i32(const char *path, struct _stat64i32 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_stat64(path, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat64i32(&temp, result);
    return 0;
}

int __cdecl _wstat32(const wchar_t *path, struct _stat32 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_wstat64(path, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat32(&temp, result);
    return 0;
}

int __cdecl _wstat32i64(const wchar_t *path, struct _stat32i64 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_wstat64(path, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat32i64(&temp, result);
    return 0;
}

int __cdecl _wstat64i32(const wchar_t *path, struct _stat64i32 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_wstat64(path, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat64i32(&temp, result);
    return 0;
}

int __cdecl _fstat32(int fd, struct _stat32 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_fstat64(fd, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat32(&temp, result);
    return 0;
}

int __cdecl _fstat32i64(int fd, struct _stat32i64 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_fstat64(fd, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat32i64(&temp, result);
    return 0;
}

int __cdecl _fstat64i32(int fd, struct _stat64i32 *result)
{
    struct _stat64 temp;
    if (!result)
    {
        errno = EINVAL;
        return -1;
    }

    if (_fstat64(fd, &temp) != 0)
        return -1;

    msvcrt_copy_stat64_to_stat64i32(&temp, result);
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Long double conversion helper ($I10_OUTPUT) copied from Wine.             */
/* ------------------------------------------------------------------------- */

#if defined(_M_IX86) || defined(__i386__)

typedef struct { ULONG x80[3]; } MSVCRT__LDOUBLE;

enum fpmod {
    FP_ROUND_ZERO,
    FP_ROUND_DOWN,
    FP_ROUND_EVEN,
    FP_ROUND_UP,
    FP_VAL_INFINITY,
    FP_VAL_NAN
};

struct fpnum
{
    int sign;
    int exp;
    unsigned __int64 m;
    enum fpmod mod;
};

#define EXP_BITS 11
#define MANT_BITS 53

static int fpnum_double(struct fpnum *fp, double *d)
{
    unsigned __int64 bits = 0;

    if (fp->mod == FP_VAL_INFINITY)
    {
        *d = fp->sign * __builtin_inf();
        return 0;
    }

    if (fp->mod == FP_VAL_NAN)
    {
        bits = ~0ULL;
        if (fp->sign == 1)
            bits &= ~((unsigned __int64)1 << (MANT_BITS + EXP_BITS - 1));
        *d = *(double *)&bits;
        return 0;
    }

    if (!fp->m)
    {
        *d = fp->sign * 0.0;
        return 0;
    }

    if (fp->exp > 1 << EXP_BITS)
    {
        *d = fp->sign * __builtin_inf();
        return ERANGE;
    }
    if (fp->exp < -(1 << EXP_BITS))
    {
        *d = fp->sign * 0.0;
        return ERANGE;
    }
    fp->exp += MANT_BITS - 1;

    while (fp->m < (unsigned __int64)1 << (MANT_BITS - 1))
    {
        fp->m <<= 1;
        fp->exp--;
    }
    while (fp->m >= (unsigned __int64)1 << MANT_BITS)
    {
        if (fp->m & 1 || fp->mod != FP_ROUND_ZERO)
        {
            if (!(fp->m & 1)) fp->mod = FP_ROUND_DOWN;
            else if (fp->mod == FP_ROUND_ZERO) fp->mod = FP_ROUND_EVEN;
            else fp->mod = FP_ROUND_UP;
        }
        fp->m >>= 1;
        fp->exp++;
    }
    fp->exp += (1 << (EXP_BITS - 1)) - 1;

    if (fp->exp <= 0)
    {
        if (fp->m & 1 && fp->mod == FP_ROUND_ZERO) fp->mod = FP_ROUND_EVEN;
        else if (fp->m & 1) fp->mod = FP_ROUND_UP;
        else if (fp->mod != FP_ROUND_ZERO) fp->mod = FP_ROUND_DOWN;
        fp->m >>= 1;
    }
    while (fp->m && fp->exp < 0)
    {
        if (fp->m & 1 && fp->mod == FP_ROUND_ZERO) fp->mod = FP_ROUND_EVEN;
        else if (fp->m & 1) fp->mod = FP_ROUND_UP;
        else if (fp->mod != FP_ROUND_ZERO) fp->mod = FP_ROUND_DOWN;
        fp->m >>= 1;
        fp->exp++;
    }

    if (fp->mod == FP_ROUND_UP || (fp->mod == FP_ROUND_EVEN && (fp->m & 1)))
    {
        fp->m++;

        if (fp->m == (unsigned __int64)1 << (MANT_BITS - 1))
        {
            fp->exp++;
        }
        else if (fp->m >= (unsigned __int64)1 << MANT_BITS)
        {
            fp->exp++;
            fp->m >>= 1;
        }
    }

    if (fp->exp >= (1 << EXP_BITS) - 1)
    {
        *d = fp->sign * __builtin_inf();
        return ERANGE;
    }
    if (!fp->m || fp->exp < 0)
    {
        *d = fp->sign * 0.0;
        return ERANGE;
    }

    if (fp->sign == -1)
        bits |= (unsigned __int64)1 << (MANT_BITS + EXP_BITS - 1);
    bits |= (unsigned __int64)fp->exp << (MANT_BITS - 1);
    bits |= fp->m & (((unsigned __int64)1 << (MANT_BITS - 1)) - 1);

    *d = *(double *)&bits;
    return 0;
}

#define I10_OUTPUT_MAX_PREC 21

struct _I10_OUTPUT_DATA
{
    short pos;
    char sign;
    unsigned char len;
    char str[I10_OUTPUT_MAX_PREC + 1];
};

int __cdecl I10_OUTPUT(MSVCRT__LDOUBLE ld80, int prec, int flag, struct _I10_OUTPUT_DATA *data)
{
    struct fpnum num;
    double d;
    char format[8];
    char buf[I10_OUTPUT_MAX_PREC + 9];
    char *p;

    if (!data)
        return 0;

    if ((ld80.x80[2] & 0x7fff) == 0x7fff)
    {
        if (ld80.x80[0] == 0 && ld80.x80[1] == 0x80000000)
            strcpy(data->str, "1#INF");
        else
            strcpy(data->str, (ld80.x80[1] & 0x40000000) ? "1#QNAN" : "1#SNAN");
        data->pos = 1;
        data->sign = (ld80.x80[2] & 0x8000) ? '-' : ' ';
        data->len = (unsigned char)strlen(data->str);
        return 0;
    }

    num.sign = (ld80.x80[2] & 0x8000) ? -1 : 1;
    num.exp  = (ld80.x80[2] & 0x7fff) - 0x3fff - 63;
    num.m    = ld80.x80[0] | ((unsigned __int64)ld80.x80[1] << 32);
    num.mod  = FP_ROUND_EVEN;
    fpnum_double(&num, &d);

    if (d < 0.0)
    {
        data->sign = '-';
        d = -d;
    }
    else
    {
        data->sign = ' ';
    }

    if (flag & 1)
    {
        int exp = (d > 0.0) ? (int)__builtin_floor(__builtin_log10(d)) + 1 : 1;
        prec += exp;
        if (exp < 0)
            prec--;
    }
    prec--;

    if (prec + 1 > I10_OUTPUT_MAX_PREC)
        prec = I10_OUTPUT_MAX_PREC - 1;
    else if (prec < 0)
    {
        d = 0.0;
        prec = 0;
    }

    sprintf(format, "%%.%dle", prec);
    sprintf(buf, format, d);

    buf[1] = buf[0];
    data->pos = atoi(buf + prec + 3);
    if (buf[1] != '0')
        data->pos++;

    for (p = buf + prec + 1; p > buf + 1 && *p == '0'; p--)
        ;
    data->len = (unsigned char)(p - buf);

    memmove(data->str, buf + 1, data->len);
    data->str[data->len] = '\0';

    return 1;
}

#undef I10_OUTPUT_MAX_PREC
#undef EXP_BITS
#undef MANT_BITS

#endif /* defined(_M_IX86) || defined(__i386__) */

/* ------------------------------------------------------------------------- */
/* Minimal C++ runtime glue.                                                */
/* ------------------------------------------------------------------------- */

typedef void (__cdecl *new_handler_func)(void);
typedef int (__cdecl *new_handler_func_int)(size_t);
typedef void (__cdecl *unexpected_handler)(void);
typedef void (__cdecl *terminate_handler)(void);

static new_handler_func current_new_handler = NULL;
static new_handler_func_int current_new_handler_int = NULL;
static int current_new_mode = 0;
static _se_translator_function current_se_translator = NULL;
static unexpected_handler current_unexpected_handler = NULL;
static terminate_handler current_terminate_handler = NULL;

static int __cdecl call_void_new_handler_wrapper(size_t size)
{
    (void)size;
    if (current_new_handler)
    {
        current_new_handler();
        return 1;
    }
    return 0;
}

new_handler_func __cdecl set_new_handler(new_handler_func handler)
{
    new_handler_func old = current_new_handler;
    current_new_handler = handler;
    if (handler)
        current_new_handler_int = call_void_new_handler_wrapper;
    return old;
}

new_handler_func_int __cdecl _set_new_handler(new_handler_func_int handler)
{
    new_handler_func_int old = current_new_handler_int;
    current_new_handler_int = handler;
    if (handler)
        current_new_handler = NULL;
    return old;
}

_se_translator_function __cdecl _set_se_translator(_se_translator_function func)
{
    _se_translator_function previous = current_se_translator;
    current_se_translator = func;
    return previous;
}

unexpected_handler __cdecl set_unexpected(unexpected_handler handler)
{
    unexpected_handler previous = current_unexpected_handler;
    current_unexpected_handler = handler;
    return previous;
}

terminate_handler __cdecl set_terminate(terminate_handler handler)
{
    terminate_handler previous = current_terminate_handler;
    current_terminate_handler = handler;
    return previous;
}

void __cdecl terminate(void)
{
    if (current_terminate_handler)
        current_terminate_handler();

    abort();
}

void __cdecl unexpected(void)
{
    if (current_unexpected_handler)
        current_unexpected_handler();

    terminate();
}

static void *allocate_with_handlers(size_t size)
{
    size_t actual = size ? size : 1;
    void *ptr = malloc(actual);

    while (!ptr)
    {
        if (current_new_handler)
        {
            current_new_handler();
        }
        else if (current_new_handler_int && current_new_handler_int((unsigned int)actual))
        {
            /* handler handled the failure */
        }
        else
        {
            break;
        }

        ptr = malloc(actual);
    }

    if (!ptr)
        abort();

    return ptr;
}

new_handler_func_int __cdecl _query_new_handler(void)
{
    return current_new_handler_int;
}

int __cdecl _set_new_mode(int mode)
{
    int old = current_new_mode;
    current_new_mode = mode ? 1 : 0;
    return old;
}

int __cdecl _query_new_mode(void)
{
    return current_new_mode;
}

int __cdecl __initialize_lconv_for_unsigned_char(void)
{
    struct lconv *lc = localeconv();
    if (!lc)
        return 0;

    lc->int_frac_digits = (char)UCHAR_MAX;
    lc->frac_digits     = (char)UCHAR_MAX;
    lc->p_cs_precedes   = (char)UCHAR_MAX;
    lc->p_sep_by_space  = (char)UCHAR_MAX;
    lc->n_cs_precedes   = (char)UCHAR_MAX;
    lc->n_sep_by_space  = (char)UCHAR_MAX;
    lc->p_sign_posn     = (char)UCHAR_MAX;
    lc->n_sign_posn     = (char)UCHAR_MAX;
    return 0;
}

void *__cdecl operator_new(size_t size)
{
    return allocate_with_handlers(size);
}

void __cdecl operator_delete(void *ptr)
{
    free(ptr);
}

void *__cdecl operator_new_dbg(size_t size, int block_use, const char *file, int line)
{
    (void)block_use;
    (void)file;
    (void)line;
    return operator_new(size);
}

void *__cdecl operator_new_array_dbg(size_t size, int block_use, const char *file, int line)
{
    (void)block_use;
    (void)file;
    (void)line;
    return operator_new(size);
}

int __cdecl _heapadd(void *memory, size_t size)
{
    (void)memory;
    (void)size;
    return 0;
}

typedef struct exception
{
    void *vtable;
    char *name;
    int do_free;
} exception;

typedef exception bad_cast;
typedef exception bad_typeid;
typedef exception __non_rtti_object;

static exception *exception_init(exception *self, const char *name)
{
    if (!self)
        return NULL;
    self->vtable = NULL;
    self->name = (char *)(name ? name : "");
    self->do_free = 0;
    return self;
}

void *__cdecl exception_ctor(exception *self, const char * const *name)
{
    return exception_init(self, name ? *name : "");
}

void *__cdecl exception_ctor_noalloc(exception *self, const char * const *name, int noalloc)
{
    (void)noalloc;
    return exception_ctor(self, name);
}

void *__cdecl exception_copy_ctor(exception *self, const exception *other)
{
    if (self && other)
    {
        self->vtable = other->vtable;
        self->name = other->name;
        self->do_free = 0;
    }
    return self;
}

void *__cdecl exception_default_ctor(exception *self)
{
    return exception_init(self, "");
}

void *__cdecl exception_dtor(exception *self)
{
    (void)self;
    return NULL;
}

exception *__cdecl exception_opequals(exception *self, const exception *other)
{
    exception_copy_ctor(self, other);
    return self;
}

const char *__cdecl exception_what(const exception *self)
{
    return (self && self->name) ? self->name : "Unknown exception";
}

void *__cdecl exception_vector_dtor(exception *self, unsigned int flags)
{
    (void)flags;
    return exception_dtor(self);
}

void *__cdecl exception_scalar_dtor(exception *self, unsigned int flags)
{
    (void)flags;
    return exception_dtor(self);
}

void *__cdecl bad_cast_ctor(bad_cast *self, const char * const *name)
{
    return exception_ctor(self, name);
}

void *__cdecl bad_cast_copy_ctor(bad_cast *self, const bad_cast *other)
{
    return exception_copy_ctor(self, other);
}

void *__cdecl bad_cast_ctor_charptr(bad_cast *self, const char *name)
{
    return exception_init(self, name);
}

void *__cdecl bad_cast_dtor(bad_cast *self)
{
    return exception_dtor(self);
}

bad_cast *__cdecl bad_cast_opequals(bad_cast *self, const bad_cast *other)
{
    return (bad_cast *)exception_opequals(self, other);
}

void *__cdecl bad_cast_default_ctor(bad_cast *self)
{
    return exception_default_ctor(self);
}

void *__cdecl bad_cast_vector_dtor(bad_cast *self, unsigned int flags)
{
    return exception_vector_dtor(self, flags);
}

void *__cdecl bad_cast_scalar_dtor(bad_cast *self, unsigned int flags)
{
    return exception_scalar_dtor(self, flags);
}

void *__cdecl bad_typeid_ctor(bad_typeid *self, const char *name)
{
    return exception_init(self, name);
}

void *__cdecl bad_typeid_copy_ctor(bad_typeid *self, const bad_typeid *other)
{
    return exception_copy_ctor(self, other);
}

void *__cdecl bad_typeid_dtor(bad_typeid *self)
{
    return exception_dtor(self);
}

bad_typeid *__cdecl bad_typeid_opequals(bad_typeid *self, const bad_typeid *other)
{
    return (bad_typeid *)exception_opequals(self, other);
}

void *__cdecl bad_typeid_default_ctor(bad_typeid *self)
{
    return exception_default_ctor(self);
}

void *__cdecl bad_typeid_vector_dtor(bad_typeid *self, unsigned int flags)
{
    return exception_vector_dtor(self, flags);
}

void *__cdecl bad_typeid_scalar_dtor(bad_typeid *self, unsigned int flags)
{
    return exception_scalar_dtor(self, flags);
}

void *__cdecl __non_rtti_object_ctor(__non_rtti_object *self, const char *name)
{
    return exception_init(self, name);
}

void *__cdecl __non_rtti_object_copy_ctor(__non_rtti_object *self, const __non_rtti_object *other)
{
    return exception_copy_ctor(self, other);
}

void *__cdecl __non_rtti_object_dtor(__non_rtti_object *self)
{
    return exception_dtor(self);
}

__non_rtti_object *__cdecl __non_rtti_object_opequals(__non_rtti_object *self, const __non_rtti_object *other)
{
    return (__non_rtti_object *)exception_opequals(self, other);
}

void *__cdecl __non_rtti_object_vector_dtor(__non_rtti_object *self, unsigned int flags)
{
    return exception_vector_dtor(self, flags);
}

void *__cdecl __non_rtti_object_scalar_dtor(__non_rtti_object *self, unsigned int flags)
{
    return exception_scalar_dtor(self, flags);
}

int __cdecl __uncaught_exception(void *unused)
{
    (void)unused;
    return 0;
}

void *exception_vtable[4] = {0};
void *bad_cast_vtable[4] = {0};
void *bad_typeid_vtable[4] = {0};
void *__non_rtti_object_vtable[4] = {0};

typedef struct type_info
{
    void *vfptr;
    char *name;
} type_info;

void __cdecl type_info_dtor(type_info *self)
{
    (void)self;
}

int __cdecl type_info_before(const type_info *self, const type_info *other)
{
    if (!self || !other)
        return 0;
    return strcmp(self->name ? self->name : "", other->name ? other->name : "") < 0;
}

const char *__cdecl type_info_name(const type_info *self)
{
    return (self && self->name) ? self->name : "";
}

const char *__cdecl type_info_raw_name(const type_info *self)
{
    return type_info_name(self);
}

int __cdecl type_info_opequals_equals(const type_info *self, const type_info *other)
{
    return strcmp(type_info_name(self), type_info_name(other)) == 0;
}

int __cdecl type_info_opnot_equals(const type_info *self, const type_info *other)
{
    return !type_info_opequals_equals(self, other);
}

void __cdecl _adj_fdiv_r(void)
{
}

void __stdcall _adj_fdiv_m16i(long value)
{
    (void)value;
}

void __stdcall _adj_fdiv_m32(long value)
{
    (void)value;
}

void __stdcall _adj_fdiv_m32i(long value)
{
    (void)value;
}

void __stdcall _adj_fdiv_m64(double value)
{
    (void)value;
}

void __stdcall _adj_fdivr_m16i(long value)
{
    (void)value;
}

void __stdcall _adj_fdivr_m32(long value)
{
    (void)value;
}

void __stdcall _adj_fdivr_m32i(long value)
{
    (void)value;
}

void __stdcall _adj_fdivr_m64(double value)
{
    (void)value;
}

void __cdecl _adjust_fdiv(void)
{
}

/* ------------------------------------------------------------------------- */
/* Additional __p__* accessor functions for i386-specific runtime state.     */
/* Note: __p__amblksiz, __p__commode, __p__fileinfo, and __p__fmode are      */
/* already provided by MinGW's libmsvcrt.a, so we don't define them here.    */
/* We synchronize their state in sync_native_globals() via the aliased       */
/* additional state synchronized via __p__ helpers defined above.            */
/* ------------------------------------------------------------------------- */

int *__cdecl __p__daylight(void)
{
    int *dl = __daylight();
    if (dl)
        return dl;
    return &_daylight;
}

long *__cdecl __p__dstbias(void)
{
    long *dst = __dstbias();
    if (dst)
        return dst;
    return &_dstbias;
}

const unsigned short *__cdecl __p__pwctype(void)
{
    return _pwctype;
}

/* ------------------------------------------------------------------------- */
/* Minimal invalid-parameter handler stub.                                   */
/* ------------------------------------------------------------------------- */

void __cdecl _invalid_parameter(const wchar_t *expression,
                                const wchar_t *function,
                                const wchar_t *file,
                                unsigned int line,
                                uintptr_t reserved)
{
    DPRINT1("MSVCRT: _invalid_parameter(expr=%ls, func=%ls, file=%ls, line=%u, reserved=%p)\n",
            expression ? expression : L"(null)",
            function   ? function   : L"(null)",
            file       ? file       : L"(null)",
            line,
            (PVOID)reserved);

    _invoke_watson(expression, function, file, line, reserved);
}

#else /* !USE_NATIVE_MINGW_CRT */

unsigned char *__cdecl __p__mbcasemap(void);
unsigned char *__cdecl __p__mbctype(void);
int *__cdecl __p__commode(void);
int *__cdecl __p__fmode(void);
#ifndef _WIN64
unsigned int *__cdecl __p__amblksiz(void);
int *__cdecl __p__fileinfo(void);
#endif
char ***__cdecl __p__environ(void);
wchar_t ***__cdecl __p__wenviron(void);
int *__cdecl __p___argc(void);
char ***__cdecl __p___argv(void);
char ***__cdecl __p___initenv(void);
int *__cdecl __p___mb_cur_max(void);
wchar_t ***__cdecl __p___wargv(void);
wchar_t ***__cdecl __p___winitenv(void);
char **__cdecl __p__acmdln(void);
char **__cdecl __p__pgmptr(void);
wchar_t **__cdecl __p__wcmdln(void);
wchar_t **__cdecl __p__wpgmptr(void);

unsigned char *__cdecl reactos_p__mbcasemap(void)
{
    return __p__mbcasemap();
}

unsigned char *__cdecl reactos_p__mbctype(void)
{
    return __p__mbctype();
}

int *__cdecl reactos_p__commode(void)
{
    return __p__commode();
}

int *__cdecl reactos_p__fmode(void)
{
    return __p__fmode();
}

#ifndef _WIN64
unsigned int *__cdecl reactos_p__amblksiz(void)
{
    return __p__amblksiz();
}

int *__cdecl reactos_p__fileinfo(void)
{
    return __p__fileinfo();
}
#endif /* !_WIN64 */

char ***__cdecl reactos_p__environ(void)
{
    return __p__environ();
}

wchar_t ***__cdecl reactos_p__wenviron(void)
{
    return __p__wenviron();
}

int *__cdecl reactos_p___argc(void)
{
    return __p___argc();
}

char ***__cdecl reactos_p___argv(void)
{
    return __p___argv();
}

char ***__cdecl reactos_p___initenv(void)
{
    return __p___initenv();
}

int *__cdecl reactos_p___mb_cur_max(void)
{
    return __p___mb_cur_max();
}

wchar_t ***__cdecl reactos_p___wargv(void)
{
    return __p___wargv();
}

wchar_t ***__cdecl reactos_p___winitenv(void)
{
    return __p___winitenv();
}

char **__cdecl reactos_p__acmdln(void)
{
    return __p__acmdln();
}

char **__cdecl reactos_p__pgmptr(void)
{
    return __p__pgmptr();
}

wchar_t **__cdecl reactos_p__wcmdln(void)
{
    return __p__wcmdln();
}

wchar_t **__cdecl reactos_p__wpgmptr(void)
{
    return __p__wpgmptr();
}

void __cdecl __msvcrt_native_attach(void) {}

#endif /* USE_NATIVE_MINGW_CRT */
