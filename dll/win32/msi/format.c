/*
 * Implementation of the Microsoft Installer (msi.dll)
 *
 * Copyright 2005 Mike McCormack for CodeWeavers
 * Copyright 2005 Aric Stewart for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <stdio.h>
#ifdef __REACTOS__
#include <limits.h>
#endif

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wine/debug.h"
#include "msi.h"
#include "winnls.h"
#include "objbase.h"
#include "oleauto.h"

#include "msipriv.h"
#ifdef __REACTOS__
#include "winemsi_s.h"
#else
#include "winemsi.h"
#endif
#include "wine/exception.h"

WINE_DEFAULT_DEBUG_CHANNEL(msi);

/* types arranged by precedence */
#define FORMAT_NULL         0x0001
#define FORMAT_LITERAL      0x0002
#define FORMAT_NUMBER       0x0004
#define FORMAT_LBRACK       0x0010
#define FORMAT_LBRACE       0x0020
#define FORMAT_RBRACK       0x0011
#define FORMAT_RBRACE       0x0021
#define FORMAT_ESCAPE       0x0040
#define FORMAT_PROPNULL     0x0080
#define FORMAT_ERROR        0x1000
#define FORMAT_FAIL         0x2000

#define left_type(x) (x & 0xF0)

struct format
{
    MSIPACKAGE *package;
    MSIRECORD *record;
    LPWSTR deformatted;
    int len;
    int n;
    BOOL propfailed;
    BOOL groupfailed;
    int groups;
#ifdef __REACTOS__
    UINT error;
#endif
};

struct form_str
{
    struct list entry;
    int n;
    int len;
    int type;
    BOOL propfound;
    BOOL nonprop;
};

struct stack
{
    struct list items;
};

static struct stack *create_stack(void)
{
    struct stack *stack = malloc(sizeof(*stack));
#ifdef __REACTOS__
    if (!stack) return NULL;
#endif
    list_init(&stack->items);
    return stack;
}

static void free_stack(struct stack *stack)
{
#ifdef __REACTOS__
    if (!stack) return;
#endif
    while (!list_empty(&stack->items))
    {
        struct form_str *str = LIST_ENTRY(list_head(&stack->items), struct form_str, entry);
        list_remove(&str->entry);
        free(str);
    }

    free(stack);
}

static void stack_push(struct stack *stack, struct form_str *str)
{
    list_add_head(&stack->items, &str->entry);
}

static struct form_str *stack_pop(struct stack *stack)
{
    struct form_str *ret;

    if (list_empty(&stack->items))
        return NULL;

    ret = LIST_ENTRY(list_head(&stack->items), struct form_str, entry);
    list_remove(&ret->entry);
    return ret;
}

static struct form_str *stack_find(struct stack *stack, int type)
{
    struct form_str *str;

    LIST_FOR_EACH_ENTRY(str, &stack->items, struct form_str, entry)
    {
        if (str->type == type)
            return str;
    }

    return NULL;
}

static struct form_str *stack_peek(struct stack *stack)
{
    return LIST_ENTRY(list_head(&stack->items), struct form_str, entry);
}

static const WCHAR *get_formstr_data(struct format *format, struct form_str *str)
{
    return &format->deformatted[str->n];
}

#ifdef __REACTOS__
static WCHAR *format_alloc( struct format *format, size_t len )
{
    WCHAR *ret;

    if (len >= INT_MAX || len >= (size_t)-1 / sizeof(WCHAR))
    {
        format->error = ERROR_OUTOFMEMORY;
        return NULL;
    }
    ret = malloc( (len + 1) * sizeof(WCHAR) );
    if (!ret) format->error = ERROR_OUTOFMEMORY;
    return ret;
}

static WCHAR *format_strdup( struct format *format, const WCHAR *str )
{
    WCHAR *ret;
    size_t len;

    if (!str) return NULL;
    len = wcslen(str);
    if ((ret = format_alloc( format, len )))
        memcpy( ret, str, (len + 1) * sizeof(WCHAR) );
    return ret;
}

#endif
static WCHAR *dup_formstr( struct format *format, struct form_str *str, int *ret_len )
{
    WCHAR *val;

    if (!str->len) return NULL;
#ifdef __REACTOS__
    if ((val = format_alloc( format, str->len )))
#else
    if ((val = malloc( (str->len + 1) * sizeof(WCHAR) )))
#endif
    {
        memcpy( val, get_formstr_data(format, str), str->len * sizeof(WCHAR) );
        val[str->len] = 0;
        *ret_len = str->len;
    }
    return val;
}

static WCHAR *deformat_index( struct format *format, struct form_str *str, int *ret_len )
{
    WCHAR *val, *ret;
    DWORD len;
    int field;

#ifdef __REACTOS__
    if (!(val = format_alloc( format, str->len ))) return NULL;
#else
    if (!(val = malloc( (str->len + 1) * sizeof(WCHAR) ))) return NULL;
#endif
    lstrcpynW(val, get_formstr_data(format, str), str->len + 1);
    field = wcstol( val, NULL, 10 );
    free( val );

    if (MSI_RecordIsNull( format->record, field ) ||
        MSI_RecordGetStringW( format->record, field, NULL, &len )) return NULL;

#ifdef __REACTOS__
    if (!(ret = format_alloc( format, len ))) return NULL;
#endif
    len++;
#ifndef __REACTOS__
    if (!(ret = malloc( len * sizeof(WCHAR) ))) return NULL;
#endif
    ret[0] = 0;
    if (MSI_RecordGetStringW( format->record, field, ret, &len ))
    {
        free( ret );
        return NULL;
    }
    *ret_len = len;
    return ret;
}

static WCHAR *deformat_property( struct format *format, struct form_str *str, int *ret_len )
{
    WCHAR *prop, *ret;
    DWORD len = 0;
    UINT r;

#ifdef __REACTOS__
    if (!(prop = format_alloc( format, str->len ))) return NULL;
#else
    if (!(prop = malloc( (str->len + 1) * sizeof(WCHAR) ))) return NULL;
#endif
    lstrcpynW( prop, get_formstr_data(format, str), str->len + 1 );

    r = msi_get_property( format->package->db, prop, NULL, &len );
    if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA)
    {
        free( prop );
#ifdef __REACTOS__
        if (r == ERROR_OUTOFMEMORY) format->error = r;
#endif
        return NULL;
    }
#ifdef __REACTOS__
    if ((ret = format_alloc( format, len )))
    {
        len++;
        r = msi_get_property( format->package->db, prop, ret, &len );
        if (r != ERROR_SUCCESS)
        {
            free( ret );
            ret = NULL;
            format->error = r;
        }
    }
#else
    len++;
    if ((ret = malloc( len * sizeof(WCHAR) )))
        msi_get_property( format->package->db, prop, ret, &len );
#endif
    free( prop );
#ifdef __REACTOS__
    *ret_len = ret ? len : 0;
#else
    *ret_len = len;
#endif
    return ret;
}

static WCHAR *deformat_component( struct format *format, struct form_str *str, int *ret_len )
{
    WCHAR *key, *ret;
    MSICOMPONENT *comp;

#ifdef __REACTOS__
    if (!(key = format_alloc( format, str->len ))) return NULL;
#else
    if (!(key = malloc( (str->len + 1) * sizeof(WCHAR) ))) return NULL;
#endif
    lstrcpynW(key, get_formstr_data(format, str), str->len + 1);

    if (!(comp = msi_get_loaded_component( format->package, key )))
    {
        free( key );
        return NULL;
    }
    if (comp->Action == INSTALLSTATE_SOURCE)
        ret = msi_resolve_source_folder( format->package, comp->Directory, NULL );
    else
#ifdef __REACTOS__
        ret = format_strdup( format, msi_get_target_folder( format->package, comp->Directory ) );
#else
        ret = wcsdup( msi_get_target_folder( format->package, comp->Directory ) );
#endif

#ifdef __REACTOS__
    if (ret)
    {
        size_t len = wcslen(ret);
        if (len >= INT_MAX)
        {
            free(ret);
            ret = NULL;
            format->error = ERROR_OUTOFMEMORY;
            *ret_len = 0;
        }
        else *ret_len = len;
    }
#else
    if (ret) *ret_len = lstrlenW( ret );
#endif
    else *ret_len = 0;
    free( key );
    return ret;
}

static WCHAR *deformat_file( struct format *format, struct form_str *str, BOOL shortname, int *ret_len )
{
    WCHAR *key, *ret = NULL;
    const MSIFILE *file;
    DWORD len = 0;

#ifdef __REACTOS__
    if (!(key = format_alloc( format, str->len ))) return NULL;
#else
    if (!(key = malloc( (str->len + 1) * sizeof(WCHAR) ))) return NULL;
#endif
    lstrcpynW(key, get_formstr_data(format, str), str->len + 1);

    if (!(file = msi_get_loaded_file( format->package, key ))) goto done;
    if (!shortname)
    {
#ifdef __REACTOS__
        if ((ret = format_strdup( format, file->TargetPath ))) len = wcslen( ret );
#else
        if ((ret = wcsdup( file->TargetPath ))) len = lstrlenW( ret );
#endif
        goto done;
    }
    if (!(len = GetShortPathNameW(file->TargetPath, NULL, 0)))
    {
#ifdef __REACTOS__
        if ((ret = format_strdup( format, file->TargetPath ))) len = wcslen( ret );
#else
        if ((ret = wcsdup( file->TargetPath ))) len = lstrlenW( ret );
#endif
        goto done;
    }
#ifdef __REACTOS__
    if ((ret = format_alloc( format, len )))
    {
        DWORD size = len + 1;
        len = GetShortPathNameW( file->TargetPath, ret, size );
        if (!len || len >= size)
        {
            free( ret );
            ret = NULL;
        }
    }
#else
    len++;
    if ((ret = malloc( len * sizeof(WCHAR) )))
        len = GetShortPathNameW( file->TargetPath, ret, len );
#endif

done:
    free( key );
#ifdef __REACTOS__
    *ret_len = ret ? len : 0;
#else
    *ret_len = len;
#endif
    return ret;
}

static WCHAR *deformat_environment( struct format *format, struct form_str *str, int *ret_len )
{
    WCHAR *key, *ret = NULL;
    DWORD len;

#ifdef __REACTOS__
    if (!(key = format_alloc( format, str->len ))) return NULL;
#else
    if (!(key = malloc((str->len + 1) * sizeof(WCHAR)))) return NULL;
#endif
    lstrcpynW(key, get_formstr_data(format, str), str->len + 1);

    if ((len = GetEnvironmentVariableW( key, NULL, 0 )))
    {
#ifdef __REACTOS__
        if ((ret = format_alloc( format, len )))
        {
            DWORD size = len + 1;
            len = GetEnvironmentVariableW( key, ret, size );
            if (!len || len >= size)
            {
                free( ret );
                ret = NULL;
            }
            else *ret_len = len;
        }
#else
        len++;
        if ((ret = malloc( len * sizeof(WCHAR) )))
            *ret_len = GetEnvironmentVariableW( key, ret, len );
#endif
    }
    free( key );
    return ret;
}

static WCHAR *deformat_literal( struct format *format, struct form_str *str, BOOL *propfound,
                                int *type, int *len )
{
    LPCWSTR data = get_formstr_data(format, str);
    WCHAR *replaced = NULL;
    char ch = data[0];

    if (ch == '\\')
    {
        str->n++;
        if (str->len == 1)
        {
            str->len = 0;
            replaced = NULL;
        }
        else
        {
            str->len = 1;
            replaced = dup_formstr( format, str, len );
        }
    }
    else if (ch == '~')
    {
        if (str->len != 1)
            replaced = NULL;
#ifdef __REACTOS__
        else if ((replaced = format_alloc( format, 0 )))
#else
        else if ((replaced = malloc( sizeof(WCHAR) )))
#endif
        {
            *replaced = 0;
            *len = 0;
        }
    }
    else if (ch == '%' || ch == '#' || ch == '!' || ch == '$')
    {
        str->n++;
        str->len--;

        switch (ch)
        {
        case '%':
            replaced = deformat_environment( format, str, len ); break;
        case '#':
            replaced = deformat_file( format, str, FALSE, len ); break;
        case '!':
            replaced = deformat_file( format, str, TRUE, len ); break;
        case '$':
            replaced = deformat_component( format, str, len ); break;
        }

        *type = FORMAT_LITERAL;
    }
    else
    {
        replaced = deformat_property( format, str, len );
        *type = FORMAT_LITERAL;

        if (replaced)
            *propfound = TRUE;
        else
            format->propfailed = TRUE;
    }

    return replaced;
}

static WCHAR *build_default_format( const MSIRECORD *record )
{
    int i, count = MSI_RecordGetFieldCount( record );
    WCHAR *ret, *tmp, buf[26];
    DWORD size = 1;

    if (!(ret = malloc( sizeof(*ret) ))) return NULL;
    ret[0] = 0;

    for (i = 1; i <= count; i++)
    {
        size += swprintf( buf, ARRAY_SIZE(buf), L"%d: [%d] ", i, i );
        if (!(tmp = realloc( ret, size * sizeof(*ret) )))
        {
            free( ret );
            return NULL;
        }
        ret = tmp;
        lstrcatW( ret, buf );
    }
    return ret;
}

static BOOL format_is_number(WCHAR x)
{
    return ((x >= '0') && (x <= '9'));
}

static BOOL format_str_is_number(LPWSTR str)
{
    LPWSTR ptr;

    for (ptr = str; *ptr; ptr++)
        if (!format_is_number(*ptr))
            return FALSE;

    return TRUE;
}

static BOOL format_is_alpha(WCHAR x)
{
    return (!format_is_number(x) && x != '\0' &&
            x != '[' && x != ']' && x != '{' && x != '}');
}

static BOOL format_is_literal(WCHAR x)
{
    return (format_is_alpha(x) || format_is_number(x));
}

static int format_lex(struct format *format, struct form_str **out)
{
    int type, len = 1;
    struct form_str *str;
    LPCWSTR data;
    WCHAR ch;

    *out = NULL;

    if (!format->deformatted)
        return FORMAT_NULL;

    *out = calloc(1, sizeof(**out));
    if (!*out)
        return FORMAT_FAIL;

    str = *out;
    str->n = format->n;
    str->len = 1;
    data = get_formstr_data(format, str);

    ch = data[0];
    switch (ch)
    {
        case '{': type = FORMAT_LBRACE; break;
        case '}': type = FORMAT_RBRACE; break;
        case '[': type = FORMAT_LBRACK; break;
        case ']': type = FORMAT_RBRACK; break;
        case '~': type = FORMAT_PROPNULL; break;
        case '\0': type = FORMAT_NULL; break;

        default:
            type = 0;
    }

    if (type)
    {
        str->type = type;
        format->n++;
        return type;
    }

    if (ch == '\\')
    {
        while (data[len] && data[len] != ']')
            len++;

        type = FORMAT_ESCAPE;
    }
    else if (format_is_alpha(ch))
    {
        while (format_is_literal(data[len]))
            len++;

        type = FORMAT_LITERAL;
    }
    else if (format_is_number(ch))
    {
        while (format_is_number(data[len]))
            len++;

        type = FORMAT_NUMBER;

        if (data[len] != ']')
        {
            while (format_is_literal(data[len]))
                len++;

            type = FORMAT_LITERAL;
        }
    }
    else
    {
        ERR("Got unknown character %c(%x)\n", ch, ch);
        return FORMAT_ERROR;
    }

    format->n += len;
    str->len = len;
    str->type = type;

    return type;
}

static struct form_str *format_replace( struct format *format, BOOL propfound, BOOL nonprop,
                                        int oldsize, int type, WCHAR *replace, int len )
{
    struct form_str *ret;
    LPWSTR str, ptr;
#ifdef __REACTOS__
    size_t size = 0;
#else
    DWORD size = 0;
#endif
    int n;

#ifdef __REACTOS__
    if (format->n < 0 || format->n > format->len || oldsize < 0 ||
        oldsize > format->len - format->n || len < 0)
    {
        format->error = ERROR_INVALID_PARAMETER;
        return NULL;
    }

#endif
    if (replace)
    {
        if (!len)
            size = 1;
        else
            size = len;
    }

#ifdef __REACTOS__
    size += format->len - oldsize + 1;
#else
    size -= oldsize;
    size = format->len + size + 1;
#endif

    if (size <= 1)
    {
        free(format->deformatted);
        format->deformatted = NULL;
        format->len = 0;
        return NULL;
    }

#ifdef __REACTOS__
    str = format_alloc( format, size - 1 );
#else
    str = malloc(size * sizeof(WCHAR));
#endif
    if (!str)
        return NULL;

    str[0] = '\0';
    memcpy(str, format->deformatted, format->n * sizeof(WCHAR));
    n = format->n;

    if (replace)
    {
        if (!len) str[n++] = 0;
        else
        {
            memcpy( str + n, replace, len * sizeof(WCHAR) );
            n += len;
            str[n] = 0;
        }
    }

    ptr = &format->deformatted[format->n + oldsize];
    memcpy(&str[n], ptr, (lstrlenW(ptr) + 1) * sizeof(WCHAR));

    free(format->deformatted);
    format->deformatted = str;
    format->len = size - 1;

    /* don't reformat the NULL */
    if (replace && !len)
        format->n++;

    if (!replace)
        return NULL;

    ret = calloc(1, sizeof(*ret));
    if (!ret)
#ifdef __REACTOS__
    {
        format->error = ERROR_OUTOFMEMORY;
#endif
        return NULL;
#ifdef __REACTOS__
    }
#endif

    ret->len = len;
    ret->type = type;
    ret->n = format->n;
    ret->propfound = propfound;
    ret->nonprop = nonprop;

    return ret;
}

static WCHAR *replace_stack_group( struct format *format, struct stack *values,
                                   BOOL *propfound, BOOL *nonprop,
                                   int *oldsize, int *type, int *len )
{
    WCHAR *replaced;
#ifdef __REACTOS__
    struct form_str content = {0}, *node;
#else
    struct form_str *content, *node;
#endif
    int n;

    *nonprop = FALSE;
    *propfound = FALSE;

    node = stack_pop(values);
    n = node->n;
    *oldsize = node->len;
    free(node);

    while ((node = stack_pop(values)))
    {
        *oldsize += node->len;

        if (node->nonprop)
            *nonprop = TRUE;

        if (node->propfound)
            *propfound = TRUE;

        free(node);
    }

#ifdef __REACTOS__
    content.n = n;
    content.len = *oldsize;
    content.type = FORMAT_LITERAL;
#else
    content = calloc(1, sizeof(*content));
    content->n = n;
    content->len = *oldsize;
    content->type = FORMAT_LITERAL;
#endif

    if (!format->groupfailed && (*oldsize == 2 ||
        (format->propfailed && !*nonprop)))
    {
#ifndef __REACTOS__
        free(content);
#endif
        return NULL;
    }
#ifdef __REACTOS__
    else if (format->deformatted[content.n + 1] == '{' &&
             format->deformatted[content.n + content.len - 2] == '}')
#else
    else if (format->deformatted[content->n + 1] == '{' &&
             format->deformatted[content->n + content->len - 2] == '}')
#endif
    {
        format->groupfailed = FALSE;
#ifdef __REACTOS__
        content.len = 0;
#else
        content->len = 0;
#endif
    }
    else if (*propfound && !*nonprop &&
             !format->groupfailed && format->groups == 0)
    {
#ifdef __REACTOS__
        content.n++;
        content.len -= 2;
#else
        content->n++;
        content->len -= 2;
#endif
    }
    else
    {
        if (format->groups != 0)
            format->groupfailed = TRUE;

        *nonprop = TRUE;
    }

#ifdef __REACTOS__
    replaced = dup_formstr( format, &content, len );
    *type = content.type;
#else
    replaced = dup_formstr( format, content, len );
    *type = content->type;
    free(content);
#endif

    if (format->groups == 0)
        format->propfailed = FALSE;

    return replaced;
}

static WCHAR *replace_stack_prop( struct format *format, struct stack *values,
                                  BOOL *propfound, BOOL *nonprop,
                                  int *oldsize, int *type, int *len )
{
    WCHAR *replaced;
#ifdef __REACTOS__
    struct form_str content = {0}, *node;
#else
    struct form_str *content, *node;
#endif
    int n;

    *propfound = FALSE;
    *nonprop = FALSE;

    node = stack_pop(values);
    n = node->n;
    *oldsize = node->len;
    *type = stack_peek(values)->type;
    free(node);

    while ((node = stack_pop(values)))
    {
        *oldsize += node->len;

        if (*type != FORMAT_ESCAPE &&
            stack_peek(values) && node->type != *type)
            *type = FORMAT_LITERAL;

        free(node);
    }

#ifdef __REACTOS__
    content.n = n + 1;
    content.len = *oldsize - 2;
    content.type = *type;
#else
    content = calloc(1, sizeof(*content));
    content->n = n + 1;
    content->len = *oldsize - 2;
    content->type = *type;
#endif

    if (*type == FORMAT_NUMBER && format->record)
    {
#ifdef __REACTOS__
        replaced = deformat_index( format, &content, len );
#else
        replaced = deformat_index( format, content, len );
#endif
        if (replaced)
            *propfound = TRUE;
        else
            format->propfailed = TRUE;

        if (replaced)
            *type = format_str_is_number(replaced) ?
                FORMAT_NUMBER : FORMAT_LITERAL;
    }
    else if (format->package)
    {
#ifdef __REACTOS__
        replaced = deformat_literal( format, &content, propfound, type, len );
#else
        replaced = deformat_literal( format, content, propfound, type, len );
#endif
    }
    else
    {
        *nonprop = TRUE;
#ifdef __REACTOS__
        content.n--;
        content.len += 2;
        replaced = dup_formstr( format, &content, len );
#else
        content->n--;
        content->len += 2;
        replaced = dup_formstr( format, content, len );
#endif
    }
#ifndef __REACTOS__
    free(content);
#endif
    return replaced;
}

static UINT replace_stack(struct format *format, struct stack *stack, struct stack *values)
{
    WCHAR *replaced = NULL;
    struct form_str *beg, *top, *node;
    BOOL propfound = FALSE, nonprop = FALSE, group = FALSE;
    int type, n, len = 0, oldsize = 0;

    node = stack_peek(values);
    type = node->type;
    n = node->n;

    if (type == FORMAT_LBRACK)
        replaced = replace_stack_prop( format, values, &propfound,
                                       &nonprop, &oldsize, &type, &len );
    else if (type == FORMAT_LBRACE)
    {
        replaced = replace_stack_group( format, values, &propfound,
                                        &nonprop, &oldsize, &type, &len );
        group = TRUE;
    }
#ifdef __REACTOS__
    if (format->error)
    {
        free(replaced);
        return format->error;
    }
#endif

    format->n = n;
    beg = format_replace( format, propfound, nonprop, oldsize, type, replaced, len );
    free(replaced);
#ifdef __REACTOS__
    if (format->error)
    {
        free(beg);
        return format->error;
    }
#endif
    if (!beg)
        return ERROR_SUCCESS;

    format->n = beg->n + beg->len;

    top = stack_peek(stack);
    if (top)
    {
        type = top->type;

        if ((type == FORMAT_LITERAL || type == FORMAT_NUMBER) &&
            type == beg->type)
        {
            top->len += beg->len;

            if (group)
                top->nonprop = FALSE;

            if (type == FORMAT_LITERAL)
                top->nonprop = beg->nonprop;

            if (beg->propfound)
                top->propfound = TRUE;

            free(beg);
            return ERROR_SUCCESS;
        }
    }

    stack_push(stack, beg);
    return ERROR_SUCCESS;
}

static BOOL verify_format(LPWSTR data)
{
    int count = 0;

    while (*data)
    {
        if (*data == '\\' && *(data + 1) == '[')
        {
            data += 2;
            continue;
        }

        if (*data == '[')
            count++;
        else if (*data == ']')
            count--;

        data++;
    }

    if (count > 0)
        return FALSE;

    return TRUE;
}

static DWORD deformat_string_internal(MSIPACKAGE *package, LPCWSTR ptr,
                                      WCHAR** data, DWORD *len,
                                      MSIRECORD* record)
{
    struct format format;
    struct form_str *str = NULL;
    struct stack *stack, *temp;
    struct form_str *node;
    int type;
#ifdef __REACTOS__
    size_t length;
    UINT r = ERROR_SUCCESS;
#endif

#ifdef __REACTOS__
    *data = NULL;
    *len = 0;
#endif
    if (!ptr)
    {
#ifndef __REACTOS__
        *data = NULL;
        *len = 0;
#endif
        return ERROR_SUCCESS;
    }

#ifdef __REACTOS__
    length = wcslen(ptr);
    if (length >= INT_MAX || length >= (size_t)-1 / sizeof(WCHAR))
        return ERROR_OUTOFMEMORY;
    if (!(*data = wcsdup(ptr))) return ERROR_OUTOFMEMORY;
    *len = length;
#else
    *data = wcsdup(ptr);
    *len = lstrlenW(ptr);
#endif

    ZeroMemory(&format, sizeof(format));
    format.package = package;
    format.record = record;
    format.deformatted = *data;
    format.len = *len;

    if (!verify_format(*data))
        return ERROR_SUCCESS;

    stack = create_stack();
    temp = create_stack();
#ifdef __REACTOS__
    if (!stack || !temp)
    {
        r = ERROR_OUTOFMEMORY;
        goto done;
    }
#endif

    while ((type = format_lex(&format, &str)) != FORMAT_NULL)
    {
        if (type == FORMAT_LBRACK || type == FORMAT_LBRACE ||
            type == FORMAT_LITERAL || type == FORMAT_NUMBER ||
            type == FORMAT_ESCAPE || type == FORMAT_PROPNULL)
        {
            if (type == FORMAT_LBRACE)
            {
                format.propfailed = FALSE;
                format.groups++;
            }
            else if (type == FORMAT_ESCAPE &&
                     !stack_find(stack, FORMAT_LBRACK))
            {
                format.n -= str->len - 1;
                str->len = 1;
            }

            stack_push(stack, str);
#ifdef __REACTOS__
            str = NULL;
#endif
        }
        else if (type == FORMAT_RBRACK || type == FORMAT_RBRACE)
        {
            if (type == FORMAT_RBRACE)
                format.groups--;

            stack_push(stack, str);
#ifdef __REACTOS__
            str = NULL;
#endif

            if (stack_find(stack, left_type(type)))
            {
                do
                {
                    node = stack_pop(stack);
                    stack_push(temp, node);
                } while (node->type != left_type(type));

#ifdef __REACTOS__
                r = replace_stack(&format, stack, temp);
                if (r != ERROR_SUCCESS) break;
#else
                replace_stack(&format, stack, temp);
#endif
            }
        }
#ifdef __REACTOS__
        else
        {
            r = type == FORMAT_FAIL ? ERROR_OUTOFMEMORY : ERROR_INVALID_PARAMETER;
            break;
        }
#endif
    }

#ifdef __REACTOS__
done:
    if (r != ERROR_SUCCESS)
    {
        free(format.deformatted);
        format.deformatted = NULL;
        format.len = 0;
    }
#endif
    *data = format.deformatted;
    *len = format.len;

    free(str);
    free_stack(stack);
    free_stack(temp);

#ifdef __REACTOS__
    return r;
#else
    return ERROR_SUCCESS;
#endif
}

UINT MSI_FormatRecordW( MSIPACKAGE* package, MSIRECORD* record, LPWSTR buffer,
                        LPDWORD size )
{
    WCHAR *format, *deformated = NULL;
    UINT rc = ERROR_INVALID_PARAMETER;
    DWORD len;
#ifdef __REACTOS__
    MSIRECORD *record_deformated = NULL;
#else
    MSIRECORD *record_deformated;
#endif
    int field_count, i;

    dump_record(record);

    if (!(format = msi_dup_record_field( record, 0 )))
        format = build_default_format( record );
#ifdef __REACTOS__
    if (!format) return ERROR_OUTOFMEMORY;
#endif

    field_count = MSI_RecordGetFieldCount(record);
    record_deformated = MSI_CloneRecord(record);
    if (!record_deformated)
    {
        rc = ERROR_OUTOFMEMORY;
        goto end;
    }
#ifdef __REACTOS__
    rc = MSI_RecordSetStringW(record_deformated, 0, format);
    if (rc != ERROR_SUCCESS) goto end;
#else
    MSI_RecordSetStringW(record_deformated, 0, format);
#endif
    for (i = 1; i <= field_count; i++)
    {
        if (MSI_RecordGetString(record, i))
        {
#ifdef __REACTOS__
            rc = deformat_string_internal(package, MSI_RecordGetString(record, i), &deformated, &len, NULL);
            if (rc != ERROR_SUCCESS) goto end;
            rc = MSI_RecordSetStringW(record_deformated, i, deformated);
#else
            deformat_string_internal(package, MSI_RecordGetString(record, i), &deformated, &len, NULL);
            MSI_RecordSetStringW(record_deformated, i, deformated);
#endif
            free(deformated);
#ifdef __REACTOS__
            deformated = NULL;
            if (rc != ERROR_SUCCESS) goto end;
#endif
        }
    }

#ifdef __REACTOS__
    rc = deformat_string_internal(package, format, &deformated, &len, record_deformated);
    if (rc != ERROR_SUCCESS) goto end;
#else
    deformat_string_internal(package, format, &deformated, &len, record_deformated);
#endif
    if (buffer)
    {
        if (*size>len)
        {
#ifdef __REACTOS__
            if (len) memcpy(buffer,deformated,len*sizeof(WCHAR));
#else
            memcpy(buffer,deformated,len*sizeof(WCHAR));
#endif
            rc = ERROR_SUCCESS;
            buffer[len] = 0;
        }
        else
        {
            if (*size > 0)
            {
                memcpy(buffer,deformated,(*size)*sizeof(WCHAR));
                buffer[(*size)-1] = 0;
            }
            rc = ERROR_MORE_DATA;
        }
    }
    else rc = ERROR_SUCCESS;

    *size = len;
#ifndef __REACTOS__
    msiobj_release(&record_deformated->hdr);
#endif
end:
#ifdef __REACTOS__
    if (record_deformated) msiobj_release(&record_deformated->hdr);
#endif
    free( format );
    free( deformated );
    return rc;
}

UINT WINAPI MsiFormatRecordW( MSIHANDLE hInstall, MSIHANDLE hRecord, WCHAR *szResult, DWORD *sz )
{
    UINT r = ERROR_INVALID_HANDLE;
    MSIPACKAGE *package;
    MSIRECORD *record;

    TRACE( "%lu, %lu, %p, %p\n", hInstall, hRecord, szResult, sz );

    record = msihandle2msiinfo(hRecord, MSIHANDLETYPE_RECORD);
    if (!record)
        return ERROR_INVALID_HANDLE;

    package = msihandle2msiinfo( hInstall, MSIHANDLETYPE_PACKAGE );
    if (!package)
    {
        LPWSTR value = NULL;
        MSIHANDLE remote;

        if ((remote = msi_get_remote(hInstall)))
        {
            __TRY
            {
                r = remote_FormatRecord(remote, (struct wire_record *)&record->count, &value);
            }
            __EXCEPT(rpc_filter)
            {
                r = GetExceptionCode();
            }
            __ENDTRY

            if (!r)
                r = msi_strncpyW(value, -1, szResult, sz);

            midl_user_free(value);
            msiobj_release(&record->hdr);
            return r;
        }
    }

    if (!sz)
    {
        msiobj_release( &record->hdr );
        if (szResult)
            return ERROR_INVALID_PARAMETER;
        else
            return ERROR_SUCCESS;
    }

    r = MSI_FormatRecordW( package, record, szResult, sz );
    msiobj_release( &record->hdr );
    if (package)
        msiobj_release( &package->hdr );
    return r;
}

UINT WINAPI MsiFormatRecordA(MSIHANDLE hinst, MSIHANDLE hrec, char *buf, DWORD *sz)
{
    MSIPACKAGE *package;
    MSIRECORD *rec;
    LPWSTR value;
    DWORD len;
    UINT r = 0;

    TRACE( "%lu, %lu, %p, %p\n", hinst, hrec, buf, sz );

    rec = msihandle2msiinfo(hrec, MSIHANDLETYPE_RECORD);
    if (!rec)
        return ERROR_INVALID_HANDLE;

    package = msihandle2msiinfo(hinst, MSIHANDLETYPE_PACKAGE);
    if (!package)
    {
        LPWSTR value = NULL;
        MSIHANDLE remote;

        if ((remote = msi_get_remote(hinst)))
        {
            __TRY
            {
                r = remote_FormatRecord(remote, (struct wire_record *)&rec->count, &value);
            }
            __EXCEPT(rpc_filter)
            {
                r = GetExceptionCode();
            }
            __ENDTRY

            if (!r)
                r = msi_strncpyWtoA(value, -1, buf, sz, TRUE);

            midl_user_free(value);
            msiobj_release(&rec->hdr);
            return r;
        }
    }

    r = MSI_FormatRecordW(package, rec, NULL, &len);
    if (r != ERROR_SUCCESS)
#ifdef __REACTOS__
        goto done;
#else
        return r;
#endif

#ifdef __REACTOS__
    if (len == MAXDWORD || (size_t)len >= (size_t)-1 / sizeof(WCHAR))
    {
        r = ERROR_OUTOFMEMORY;
        goto done;
    }
    value = malloc(((size_t)len + 1) * sizeof(WCHAR));
#else
    value = malloc(++len * sizeof(WCHAR));
#endif
    if (!value)
#ifdef __REACTOS__
    {
        r = ERROR_OUTOFMEMORY;
#endif
        goto done;
#ifdef __REACTOS__
    }
    len++;
#endif

    r = MSI_FormatRecordW(package, rec, value, &len);
    if (!r)
        r = msi_strncpyWtoA(value, len, buf, sz, FALSE);

    free(value);
done:
    msiobj_release(&rec->hdr);
    if (package) msiobj_release(&package->hdr);
    return r;
}

/* wrapper to resist a need for a full rewrite right now */
DWORD deformat_string( MSIPACKAGE *package, const WCHAR *fmt, WCHAR **data )
{
#ifdef __REACTOS__
    DWORD len = 0;
#else
    DWORD len;
#endif
    MSIRECORD *rec;

    *data = NULL;
    if (!fmt) return 0;
    if (!(rec = MSI_CreateRecord( 1 ))) return 0;

#ifdef __REACTOS__
    if (MSI_RecordSetStringW( rec, 0, fmt ) != ERROR_SUCCESS) goto done;
    if (MSI_FormatRecordW( package, rec, NULL, &len ) != ERROR_SUCCESS) goto done;
    if (len == MAXDWORD || (size_t)len >= (size_t)-1 / sizeof(WCHAR)) goto done;
    if (!(*data = malloc( ((size_t)len + 1) * sizeof(WCHAR) ))) goto done;
    len++;
    if (MSI_FormatRecordW( package, rec, *data, &len ) != ERROR_SUCCESS)
#else
    MSI_RecordSetStringW( rec, 0, fmt );
    MSI_FormatRecordW( package, rec, NULL, &len );
    if (!(*data = malloc( ++len * sizeof(WCHAR) )))
#endif
    {
#ifdef __REACTOS__
        free( *data );
        *data = NULL;
#else
        msiobj_release( &rec->hdr );
        return 0;
#endif
    }
#ifdef __REACTOS__
done:
#else
    MSI_FormatRecordW( package, rec, *data, &len );
#endif
    msiobj_release( &rec->hdr );
#ifdef __REACTOS__
    return *data ? len : 0;
#else
    return len;
#endif
}
