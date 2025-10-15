/*
 * ReactOS CRT - mbsrtowcs and mbsrtowcs_s implementations
 *
 * These functions convert multibyte character sequences to wide characters
 * using the current locale state, providing the minimal behaviour required
 * by libstdc++/libwinpthread when targeting newer toolchains.
 */

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static size_t __cdecl ros_mbsrtowcs_internal(
    wchar_t *      dst,
    const char **  src,
    size_t         len,
    mbstate_t *    state)
{
    const char *current;
    mbstate_t local_state;
    wchar_t *out;
    size_t converted = 0;

    if (!src)
    {
        errno = EINVAL;
        return (size_t)-1;
    }

    current = *src;
    if (!state)
    {
        memset(&local_state, 0, sizeof(local_state));
        state = &local_state;
    }

    out = dst;

    while (converted < len)
    {
        wchar_t wc;
        size_t consumed;

        consumed = mbrtowc(&wc, current, MB_CUR_MAX, state);
        if (consumed == (size_t)-1)
        {
            *src = current;
            return (size_t)-1;
        }
        if (consumed == (size_t)-2)
        {
            *src = current;
            return (size_t)-2;
        }

        if (wc == L'\0')
        {
            if (out)
            {
                out[converted] = L'\0';
            }
            *src = NULL;
            return converted;
        }

        if (out)
        {
            out[converted] = wc;
        }

        current += consumed;
        ++converted;
    }

    *src = current;
    return converted;
}

size_t __cdecl mbsrtowcs(
    wchar_t *      dst,
    const char **  src,
    size_t         len,
    mbstate_t *    state)
{
    return ros_mbsrtowcs_internal(dst, src, len, state);
}

errno_t __cdecl mbsrtowcs_s(
    size_t *       count,
    wchar_t *      dst,
    size_t         size_in_words,
    const char **  src,
    size_t         len,
    mbstate_t *    state)
{
    const char *local_src;
    mbstate_t local_state;
    size_t max_convert;
    size_t converted;
    errno_t status = 0;

    if (count)
    {
        *count = (size_t)-1;
    }

    if (!src)
    {
        return EINVAL;
    }

    if (dst == NULL)
    {
        if (size_in_words != 0)
        {
            return EINVAL;
        }
    }
    else if (size_in_words == 0)
    {
        return EINVAL;
    }

    local_src = *src;

    if (!state)
    {
        memset(&local_state, 0, sizeof(local_state));
        state = &local_state;
    }
    else
    {
        local_state = *state;
    }

    if (dst == NULL)
    {
        converted = ros_mbsrtowcs_internal(NULL, &local_src, len, &local_state);
    }
    else
    {
        if (size_in_words <= 1)
        {
            max_convert = 0;
        }
        else
        {
            max_convert = size_in_words - 1;
        }
        if (len < max_convert)
        {
            max_convert = len;
        }

        converted = ros_mbsrtowcs_internal(dst, &local_src, max_convert, &local_state);

        if (converted != (size_t)-1 && converted != (size_t)-2)
        {
            if (local_src != NULL)
            {
                dst[0] = L'\0';
                *src = local_src;
                return ERANGE;
            }

            if (converted >= size_in_words)
            {
                dst[0] = L'\0';
                *src = local_src;
                return ERANGE;
            }

            dst[converted] = L'\0';
        }
    }

    if (converted == (size_t)-1)
    {
        status = errno ? errno : EILSEQ;
    }
    else if (converted == (size_t)-2)
    {
        status = EILSEQ;
        converted = (size_t)-1;
    }
    else
    {
        if (state != &local_state && state)
        {
            *state = local_state;
        }
        if (count)
        {
            *count = converted + 1;
        }
    }

    *src = local_src;

    return status;
}
