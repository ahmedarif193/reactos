/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of at_quick_exit/quick_exit
 */

#include <precomp.h>
#include <internal.h>

typedef void (__cdecl *quick_exit_func)(void);

extern _onexit_t __cdecl __dllonexit(_onexit_t, _onexit_t **, _onexit_t **);

static quick_exit_func *g_quick_exit_begin;
static quick_exit_func *g_quick_exit_end;

static quick_exit_func *CDECL
crt_decode_pointer(quick_exit_func *ptr)
{
    return (quick_exit_func *)_decode_pointer(ptr);
}

static quick_exit_func *CDECL
crt_encode_pointer(quick_exit_func *ptr)
{
    return (quick_exit_func *)_encode_pointer(ptr);
}

int CDECL at_quick_exit(void (__cdecl *func)(void))
{
    quick_exit_func *table_begin;
    quick_exit_func *table_end;

    if (!func)
    {
        errno = EINVAL;
        return -1;
    }

    LOCK_EXIT;

    if (!g_quick_exit_begin)
    {
        table_begin = calloc(1, sizeof(*table_begin));
        if (!table_begin)
        {
            UNLOCK_EXIT;
            errno = ENOMEM;
            return -1;
        }
        table_end = table_begin;
    }
    else
    {
        table_begin = crt_decode_pointer(g_quick_exit_begin);
        table_end = crt_decode_pointer(g_quick_exit_end);
    }

    if (!__dllonexit((_onexit_t)func, (_onexit_t **)&table_begin, (_onexit_t **)&table_end))
    {
        if (!g_quick_exit_begin)
            free(table_begin);
        UNLOCK_EXIT;
        errno = ENOMEM;
        return -1;
    }

    g_quick_exit_begin = crt_encode_pointer(table_begin);
    g_quick_exit_end = crt_encode_pointer(table_end);

    UNLOCK_EXIT;
    return 0;
}

static void CDECL
crt_call_quick_exit_handlers(void)
{
    if (!g_quick_exit_begin)
        return;

    quick_exit_func *table_begin = crt_decode_pointer(g_quick_exit_begin);
    quick_exit_func *table_end = crt_decode_pointer(g_quick_exit_end);

    g_quick_exit_begin = g_quick_exit_end = NULL;

    while (table_end > table_begin)
    {
        quick_exit_func current = *--table_end;
        if (current)
            current();
    }

    free(table_begin);
}

void __cdecl quick_exit(int status)
{
    LOCK_EXIT;
    crt_call_quick_exit_handlers();
    UNLOCK_EXIT;

    _Exit(status);
}
