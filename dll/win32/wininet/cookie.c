/*
 * Wininet - cookie handling stuff
 *
 * Copyright 2002 TransGaming Technologies Inc.
 *
 * David Hammerton
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

#include "ws2tcpip.h"

#include <stdarg.h>
#ifdef __REACTOS__
#include <ctype.h>
#include <errno.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wchar.h>

#include "windef.h"
#include "winbase.h"
#include "wininet.h"
#include "lmcons.h"
#include "winerror.h"

#include "wine/debug.h"
#include "internet.h"

#define RESPONSE_TIMEOUT        30            /* FROM internet.c */


WINE_DEFAULT_DEBUG_CHANNEL(wininet);

/* FIXME
 *     Cookies could use A LOT OF MEMORY. We need some kind of memory management here!
 */

struct _cookie_domain_t;
struct _cookie_container_t;

typedef struct _cookie_t {
    struct list entry;

    struct _cookie_container_t *container;

    WCHAR *name;
    WCHAR *data;
    DWORD flags;
    FILETIME expiry;
    FILETIME create;
} cookie_t;

typedef struct _cookie_container_t {
    struct list entry;

    WCHAR *cookie_url;
    substr_t path;
    struct _cookie_domain_t *domain;

    struct list cookie_list;
} cookie_container_t;

typedef struct _cookie_domain_t {
    struct list entry;

    WCHAR *domain;
    unsigned subdomain_len;

    struct _cookie_domain_t *parent;
    struct list subdomain_list;

    /* List of stored paths sorted by length of the path. */
    struct list path_list;
} cookie_domain_t;

static CRITICAL_SECTION cookie_cs;
static CRITICAL_SECTION_DEBUG cookie_cs_debug =
{
    0, 0, &cookie_cs,
    { &cookie_cs_debug.ProcessLocksList, &cookie_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": cookie_cs") }
};
static CRITICAL_SECTION cookie_cs = { &cookie_cs_debug, -1, 0, 0, 0, 0 };
static struct list domain_list = LIST_INIT(domain_list);

static cookie_domain_t *get_cookie_domain(substr_t domain, BOOL create)
{
#ifdef __REACTOS__
    const WCHAR *ptr, *ptr_end, *subdomain_ptr;
#else
    const WCHAR *ptr = domain.str + domain.len, *ptr_end, *subdomain_ptr;
#endif
    cookie_domain_t *iter, *current_domain, *prev_domain = NULL;
    struct list *current_list = &domain_list;

#ifdef __REACTOS__
    if(!domain.len) {
        SetLastError(ERROR_INVALID_NAME);
        return NULL;
    }
    ptr = domain.str + domain.len;

#endif
    while(1) {
        for(ptr_end = ptr--; ptr > domain.str && *ptr != '.'; ptr--);
        subdomain_ptr = *ptr == '.' ? ptr+1 : ptr;

        current_domain = NULL;
        LIST_FOR_EACH_ENTRY(iter, current_list, cookie_domain_t, entry) {
            if(ptr_end-subdomain_ptr == iter->subdomain_len
                    && !memcmp(subdomain_ptr, iter->domain, iter->subdomain_len*sizeof(WCHAR))) {
                current_domain = iter;
                break;
            }
        }

        if(!current_domain) {
            if(!create)
                return prev_domain;

            current_domain = malloc(sizeof(*current_domain));
#ifdef __REACTOS__
            if(!current_domain) {
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
#else
            if(!current_domain)
#endif
                return NULL;
#ifdef __REACTOS__
            }
#endif

            current_domain->domain = strndupW(subdomain_ptr, domain.str + domain.len - subdomain_ptr);
            if(!current_domain->domain) {
#ifdef __REACTOS__
                DWORD error = GetLastError();
#endif
                free(current_domain);
#ifdef __REACTOS__
                SetLastError(error);
#endif
                return NULL;
            }

            current_domain->subdomain_len = ptr_end-subdomain_ptr;

            current_domain->parent = prev_domain;
            list_init(&current_domain->path_list);
            list_init(&current_domain->subdomain_list);

            list_add_tail(current_list, &current_domain->entry);
        }

        if(ptr == domain.str)
            return current_domain;

        prev_domain = current_domain;
        current_list = &current_domain->subdomain_list;
    }
}

static WCHAR *create_cookie_url(substr_t domain, substr_t path, substr_t *ret_path)
{
    WCHAR *p, *url;
#ifdef __REACTOS__
    DWORD user_len;
    size_t len, i;
#else
    DWORD len, user_len, i;
#endif

    static const WCHAR cookie_prefix[] = {'C','o','o','k','i','e',':'};

    user_len = 0;
    if(GetUserNameW(NULL, &user_len) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return NULL;

#ifdef __REACTOS__
    len = (size_t)-1 / sizeof(WCHAR) - ARRAY_SIZE(cookie_prefix) - 1;
    if(user_len > len || domain.len > len - user_len || path.len > len - user_len - domain.len) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }

#endif
    /* user_len already accounts for terminating NULL */
    len = ARRAY_SIZE(cookie_prefix) + user_len + 1 /* @ */ + domain.len + path.len;
    url = malloc(len * sizeof(WCHAR));
#ifdef __REACTOS__
    if(!url) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
#else
    if(!url)
#endif
        return NULL;
#ifdef __REACTOS__
    }
#endif

    memcpy(url, cookie_prefix, sizeof(cookie_prefix));
    p = url + ARRAY_SIZE(cookie_prefix);

    if(!GetUserNameW(p, &user_len)) {
#ifdef __REACTOS__
        DWORD error = GetLastError();
#endif
        free(url);
#ifdef __REACTOS__
        SetLastError(error);
#endif
        return NULL;
    }
    p += user_len;

    *(p - 1) = '@';

    memcpy(p, domain.str, domain.len*sizeof(WCHAR));
    p += domain.len;

    for(i=0; i < path.len; i++)
        p[i] = towlower(path.str[i]);
    p[path.len] = 0;

    ret_path->str = p;
    ret_path->len = path.len;
    return url;
}

static cookie_container_t *get_cookie_container(substr_t domain, substr_t path, BOOL create)
{
    cookie_domain_t *cookie_domain;
    cookie_container_t *cookie_container, *iter;

    cookie_domain = get_cookie_domain(domain, create);
    if(!cookie_domain)
        return NULL;

    LIST_FOR_EACH_ENTRY(cookie_container, &cookie_domain->path_list, cookie_container_t, entry) {
        if(cookie_container->path.len < path.len)
            break;

        if(path.len == cookie_container->path.len && !wcsnicmp(cookie_container->path.str, path.str, path.len))
            return cookie_container;
    }

    if(!create)
        return NULL;

    cookie_container = malloc(sizeof(*cookie_container));
#ifdef __REACTOS__
    if(!cookie_container) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
#else
    if(!cookie_container)
#endif
        return NULL;
#ifdef __REACTOS__
    }
#endif

    cookie_container->cookie_url = create_cookie_url(substrz(cookie_domain->domain), path, &cookie_container->path);
    if(!cookie_container->cookie_url) {
#ifdef __REACTOS__
        DWORD error = GetLastError();
#endif
        free(cookie_container);
#ifdef __REACTOS__
        SetLastError(error);
#endif
        return NULL;
    }

    cookie_container->domain = cookie_domain;
    list_init(&cookie_container->cookie_list);

    LIST_FOR_EACH_ENTRY(iter, &cookie_domain->path_list, cookie_container_t, entry) {
        if(iter->path.len <= path.len) {
            list_add_before(&iter->entry, &cookie_container->entry);
            return cookie_container;
        }
    }

    list_add_tail(&cookie_domain->path_list, &cookie_container->entry);
    return cookie_container;
}

static void delete_cookie(cookie_t *cookie)
{
    list_remove(&cookie->entry);

    free(cookie->name);
    free(cookie->data);
    free(cookie);
}

static cookie_t *alloc_cookie(substr_t name, substr_t data, FILETIME expiry, FILETIME create_time, DWORD flags)
{
    cookie_t *new_cookie;

    new_cookie = calloc(1, sizeof(*new_cookie));
#ifdef __REACTOS__
    if(!new_cookie) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
#else
    if(!new_cookie)
#endif
        return NULL;
#ifdef __REACTOS__
    }
#endif

    new_cookie->expiry = expiry;
    new_cookie->create = create_time;
    new_cookie->flags = flags;
    list_init(&new_cookie->entry);

    if(name.str && !(new_cookie->name = strndupW(name.str, name.len))) {
#ifdef __REACTOS__
        DWORD error = GetLastError();
#endif
        delete_cookie(new_cookie);
#ifdef __REACTOS__
        SetLastError(error);
#endif
        return NULL;
    }

    if(data.str && !(new_cookie->data = strndupW(data.str, data.len))) {
#ifdef __REACTOS__
        DWORD error = GetLastError();
#endif
        delete_cookie(new_cookie);
#ifdef __REACTOS__
        SetLastError(error);
#endif
        return NULL;
    }

    return new_cookie;
}

static cookie_t *find_cookie(cookie_container_t *container, substr_t name)
{
    cookie_t *iter;

    LIST_FOR_EACH_ENTRY(iter, &container->cookie_list, cookie_t, entry) {
        if(lstrlenW(iter->name) == name.len && !wcsnicmp(iter->name, name.str, name.len))
            return iter;
    }

    return NULL;
}

static void add_cookie(cookie_container_t *container, cookie_t *new_cookie)
{
    TRACE("Adding %s=%s to %s\n", debugstr_w(new_cookie->name), debugstr_w(new_cookie->data),
          debugstr_w(container->cookie_url));

    list_add_tail(&container->cookie_list, &new_cookie->entry);
    new_cookie->container = container;
}

static void replace_cookie(cookie_container_t *container, cookie_t *new_cookie)
{
    cookie_t *old_cookie;

    old_cookie = find_cookie(container, substrz(new_cookie->name));
    if(old_cookie)
        delete_cookie(old_cookie);

    add_cookie(container, new_cookie);
}

static BOOL cookie_match_path(cookie_container_t *container, substr_t path)
{
    return path.len >= container->path.len && !wcsnicmp(container->path.str, path.str, container->path.len);
}

#ifdef __REACTOS__
static BOOL read_cookie_dword(char **cursor, DWORD *value)
{
    char *ptr = *cursor, *end;
    unsigned long number;

    while (isspace((unsigned char)*ptr)) ptr++;
    if (!isdigit((unsigned char)*ptr)) return FALSE;
    errno = 0;
    number = strtoul(ptr, &end, 10);
    if (errno == ERANGE || number > MAXDWORD || (*end && !isspace((unsigned char)*end)))
        return FALSE;
    *cursor = end;
    *value = number;
    return TRUE;
}

#endif
static BOOL load_persistent_cookie(substr_t domain, substr_t path)
{
    INTERNET_CACHE_ENTRY_INFOW *info;
    cookie_container_t *cookie_container;
    cookie_t *new_cookie;
    HANDLE cookie;
    char *str = NULL, *pbeg, *pend;
    DWORD size, flags;
#ifdef __REACTOS__
    WCHAR *name = NULL, *data = NULL;
#else
    WCHAR *name, *data;
#endif
    FILETIME expiry, create, time;

    cookie_container = get_cookie_container(domain, path, TRUE);
    if(!cookie_container)
        return FALSE;

    size = 0;
    RetrieveUrlCacheEntryStreamW(cookie_container->cookie_url, NULL, &size, FALSE, 0);
    if(GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return TRUE;
    info = malloc(size);
    if(!info)
        return FALSE;
    cookie = RetrieveUrlCacheEntryStreamW(cookie_container->cookie_url, info, &size, FALSE, 0);
#ifdef __REACTOS__
    if(!cookie) {
        DWORD error = GetLastError();
        free(info);
        SetLastError(error);
        return FALSE;
    }
    if(info->dwSizeHigh || (size_t)info->dwSizeLow >= (size_t)-1) {
        free(info);
        UnlockUrlCacheEntryStream(cookie, 0);
        SetLastError(ERROR_OUTOFMEMORY);
        return FALSE;
    }
#endif
    size = info->dwSizeLow;
    free(info);
#ifndef __REACTOS__
    if(!cookie)
        return FALSE;
#endif

#ifdef __REACTOS__
    if(!(str = malloc((size_t)size + 1)) || !ReadUrlCacheEntryStream(cookie, 0, str, &size, 0)) {
#else
    if(!(str = malloc(size + 1)) || !ReadUrlCacheEntryStream(cookie, 0, str, &size, 0)) {
#endif
        UnlockUrlCacheEntryStream(cookie, 0);
        free(str);
        return FALSE;
    }
    str[size] = 0;
    UnlockUrlCacheEntryStream(cookie, 0);

    GetSystemTimeAsFileTime(&time);
    for(pbeg=str; pbeg && *pbeg; name=data=NULL) {
        pend = strchr(pbeg, '\n');
        if(!pend)
            break;
        *pend = 0;
        name = strdupAtoW(pbeg);
#ifdef __REACTOS__
        if(!name)
            break;
#endif

        pbeg = pend+1;
        pend = strchr(pbeg, '\n');
        if(!pend)
            break;
        *pend = 0;
        data = strdupAtoW(pbeg);
#ifdef __REACTOS__
        if(!data)
            break;
#endif

        pbeg = strchr(pend+1, '\n');
        if(!pbeg)
            break;
#ifdef __REACTOS__
        if(!read_cookie_dword(&pbeg, &flags)
                || !read_cookie_dword(&pbeg, &expiry.dwLowDateTime)
                || !read_cookie_dword(&pbeg, &expiry.dwHighDateTime)
                || !read_cookie_dword(&pbeg, &create.dwLowDateTime)
                || !read_cookie_dword(&pbeg, &create.dwHighDateTime))
            break;
#else
        sscanf(pbeg, "%lu %lu %lu %lu %lu", &flags, &expiry.dwLowDateTime, &expiry.dwHighDateTime,
                &create.dwLowDateTime, &create.dwHighDateTime);
#endif

        /* skip "*\n" */
        pbeg = strchr(pbeg, '*');
        if(pbeg) {
            pbeg++;
            if(*pbeg)
                pbeg++;
        }

#ifndef __REACTOS__
        if(!name || !data)
            break;

#endif
        if(CompareFileTime(&time, &expiry) <= 0) {
            new_cookie = alloc_cookie(substr(NULL, 0), substr(NULL, 0), expiry, create, flags);
            if(!new_cookie)
                break;

            new_cookie->name = name;
            new_cookie->data = data;

            replace_cookie(cookie_container, new_cookie);
        }else {
            free(name);
            free(data);
        }
    }
    free(str);
    free(name);
    free(data);

    return TRUE;
}

#ifdef __REACTOS__
static BOOL write_cookie_data(HANDLE file, const char *data, size_t len)
{
    while(len) {
        DWORD written, count = len > MAXDWORD ? MAXDWORD : len;

        if(!WriteFile(file, data, count, &written, NULL)) return FALSE;
        if(!written) {
            SetLastError(ERROR_WRITE_FAULT);
            return FALSE;
        }
        data += written;
        len -= written;
    }
    return TRUE;
}

static BOOL write_cookie_string(HANDLE file, const WCHAR *str)
{
    char *data = strdupWtoA(str);
    DWORD error;
    BOOL ret;

    if(!data) return FALSE;
    ret = write_cookie_data(file, data, strlen(data));
    error = GetLastError();
    free(data);
    if(!ret) SetLastError(error);
    return ret;
}

#endif
static BOOL save_persistent_cookie(cookie_container_t *container)
{
    WCHAR cookie_file[MAX_PATH];
    HANDLE cookie_handle;
    cookie_t *cookie_container = NULL, *cookie_iter;
    BOOL do_save = FALSE;
#ifdef __REACTOS__
    char buf[64];
#else
    char buf[64], *dyn_buf;
#endif
    FILETIME time;
#ifdef __REACTOS__
    DWORD error = ERROR_SUCCESS;
#else
    DWORD bytes_written;
    size_t len;
#endif

    /* check if there's anything to save */
    GetSystemTimeAsFileTime(&time);
    LIST_FOR_EACH_ENTRY_SAFE(cookie_container, cookie_iter, &container->cookie_list, cookie_t, entry)
    {
        if((cookie_container->expiry.dwLowDateTime || cookie_container->expiry.dwHighDateTime)
                && CompareFileTime(&time, &cookie_container->expiry) > 0) {
            delete_cookie(cookie_container);
            continue;
        }

        if(!(cookie_container->flags & INTERNET_COOKIE_IS_SESSION)) {
            do_save = TRUE;
            break;
        }
    }

    if(!do_save) {
        DeleteUrlCacheEntryW(container->cookie_url);
        return TRUE;
    }

    if(!CreateUrlCacheEntryW(container->cookie_url, 0, L"txt", cookie_file, 0))
        return FALSE;

    cookie_handle = CreateFileW(cookie_file, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(cookie_handle == INVALID_HANDLE_VALUE) {
#ifdef __REACTOS__
        error = GetLastError();
#endif
        DeleteFileW(cookie_file);
#ifdef __REACTOS__
        SetLastError(error);
#endif
        return FALSE;
    }

    LIST_FOR_EACH_ENTRY(cookie_container, &container->cookie_list, cookie_t, entry)
    {
        if(cookie_container->flags & INTERNET_COOKIE_IS_SESSION)
            continue;

#ifdef __REACTOS__
        if(!write_cookie_string(cookie_handle, cookie_container->name)
                || !write_cookie_data(cookie_handle, "\n", 1)
                || !write_cookie_string(cookie_handle, cookie_container->data)
                || !write_cookie_data(cookie_handle, "\n", 1)
                || !write_cookie_string(cookie_handle, container->domain->domain)
                || !write_cookie_string(cookie_handle, container->path.str)) {
            error = GetLastError();
#else
        dyn_buf = strdupWtoA(cookie_container->name);
        if(!dyn_buf || !WriteFile(cookie_handle, dyn_buf, strlen(dyn_buf), &bytes_written, NULL)) {
            free(dyn_buf);
            do_save = FALSE;
            break;
        }
        free(dyn_buf);
        if(!WriteFile(cookie_handle, "\n", 1, &bytes_written, NULL)) {
#endif
            do_save = FALSE;
            break;
        }

#ifndef __REACTOS__
        dyn_buf = strdupWtoA(cookie_container->data);
        if(!dyn_buf || !WriteFile(cookie_handle, dyn_buf, strlen(dyn_buf), &bytes_written, NULL)) {
            free(dyn_buf);
            do_save = FALSE;
            break;
        }
        free(dyn_buf);
        if(!WriteFile(cookie_handle, "\n", 1, &bytes_written, NULL)) {
            do_save = FALSE;
            break;
        }

        dyn_buf = strdupWtoA(container->domain->domain);
        if(!dyn_buf || !WriteFile(cookie_handle, dyn_buf, strlen(dyn_buf), &bytes_written, NULL)) {
            free(dyn_buf);
            do_save = FALSE;
            break;
        }
        free(dyn_buf);

        len = WideCharToMultiByte(CP_ACP, 0, container->path.str, container->path.len, NULL, 0, NULL, NULL);
        dyn_buf = malloc(len + 1);
        if(dyn_buf) {
            WideCharToMultiByte(CP_ACP, 0, container->path.str, container->path.len, dyn_buf, len, NULL, NULL);
            dyn_buf[len] = 0;
        }
        if(!dyn_buf || !WriteFile(cookie_handle, dyn_buf, strlen(dyn_buf), &bytes_written, NULL)) {
            free(dyn_buf);
            do_save = FALSE;
            break;
        }
        free(dyn_buf);

#endif
        sprintf(buf, "\n%lu\n%lu\n%lu\n%lu\n%lu\n*\n", cookie_container->flags,
                cookie_container->expiry.dwLowDateTime, cookie_container->expiry.dwHighDateTime,
                cookie_container->create.dwLowDateTime, cookie_container->create.dwHighDateTime);
#ifdef __REACTOS__
        if(!write_cookie_data(cookie_handle, buf, strlen(buf))) {
            error = GetLastError();
#else
        if(!WriteFile(cookie_handle, buf, strlen(buf), &bytes_written, NULL)) {
#endif
            do_save = FALSE;
            break;
        }
    }

#ifdef __REACTOS__
    if(!CloseHandle(cookie_handle) && do_save) {
        error = GetLastError();
        do_save = FALSE;
    }
#else
    CloseHandle(cookie_handle);
#endif
    if(!do_save) {
        ERR("error saving cookie file\n");
        DeleteFileW(cookie_file);
#ifdef __REACTOS__
        SetLastError(error);
#endif
        return FALSE;
    }

    memset(&time, 0, sizeof(time));
#ifdef __REACTOS__
    if(!CommitUrlCacheEntryW(container->cookie_url, cookie_file, time, time, 0, NULL, 0, L"txt", 0)) {
        error = GetLastError();
        DeleteFileW(cookie_file);
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
#else
    return CommitUrlCacheEntryW(container->cookie_url, cookie_file, time, time, 0, NULL, 0, L"txt", 0);
#endif
}

static BOOL cookie_parse_url(const WCHAR *url, substr_t *host, substr_t *path)
{
    URL_COMPONENTSW comp = { sizeof(comp) };

    comp.dwHostNameLength = 1;
    comp.dwUrlPathLength = 1;

    if(!InternetCrackUrlW(url, 0, 0, &comp) || !comp.dwHostNameLength)
        return FALSE;

    /* discard the webpage off the end of the path */
    while(comp.dwUrlPathLength && comp.lpszUrlPath[comp.dwUrlPathLength-1] != '/')
        comp.dwUrlPathLength--;

    *host = substr(comp.lpszHostName, comp.dwHostNameLength);
    *path = comp.dwUrlPathLength ? substr(comp.lpszUrlPath, comp.dwUrlPathLength) : substr(L"/", 1);
    return TRUE;
}

typedef struct {
    cookie_t **cookies;
    unsigned cnt;
    unsigned size;

    unsigned string_len;
} cookie_set_t;

static DWORD get_cookie(substr_t host, substr_t path, DWORD flags, cookie_set_t *res)
{
    const WCHAR *p;
    cookie_domain_t *domain;
    cookie_container_t *container;
    FILETIME tm;

#ifdef __REACTOS__
    if(!host.len) return ERROR_NO_MORE_ITEMS;
    if(!path.len) path = substr(L"/", 1);

#endif
    GetSystemTimeAsFileTime(&tm);

    p = host.str + host.len;
    while(p > host.str && p[-1] != '.') p--;
    while(p != host.str) {
        p--;
        while(p > host.str && p[-1] != '.') p--;
        if(p == host.str) break;

        load_persistent_cookie(substr(p, host.str+host.len-p), substr(L"/", 1));
    }

    p = path.str + path.len;
    do {
        load_persistent_cookie(host, substr(path.str, p-path.str));

        p--;
        while(p > path.str && p[-1] != '/') p--;
    }while(p != path.str);

    domain = get_cookie_domain(host, FALSE);
    if(!domain) {
        TRACE("Unknown host %s\n", debugstr_wn(host.str, host.len));
        return ERROR_NO_MORE_ITEMS;
    }

    for(domain = get_cookie_domain(host, FALSE); domain; domain = domain->parent) {
        LIST_FOR_EACH_ENTRY(container, &domain->path_list, cookie_container_t, entry) {
            struct list *cursor, *cursor2;

            if(!cookie_match_path(container, path))
                continue;

            LIST_FOR_EACH_SAFE(cursor, cursor2, &container->cookie_list) {
                cookie_t *cookie_iter = LIST_ENTRY(cursor, cookie_t, entry);
#ifdef __REACTOS__
                size_t name_len, data_len, remaining;
#endif

                /* check for expiry */
                if((cookie_iter->expiry.dwLowDateTime != 0 || cookie_iter->expiry.dwHighDateTime != 0)
                    && CompareFileTime(&tm, &cookie_iter->expiry)  > 0) {
                    TRACE("Found expired cookie. deleting\n");
                    delete_cookie(cookie_iter);
                    continue;
                }

                if((cookie_iter->flags & INTERNET_COOKIE_HTTPONLY) && !(flags & INTERNET_COOKIE_HTTPONLY))
                    continue;

#ifdef __REACTOS__
                name_len = wcslen(cookie_iter->name);
                data_len = wcslen(cookie_iter->data);
                remaining = MAXDWORD / sizeof(WCHAR) - 1 - res->string_len;
                if(res->cnt) {
                    if(remaining < 2) goto failed;
                    remaining -= 2;
                }
                if(name_len > remaining) goto failed;
                remaining -= name_len;
                if(data_len && data_len >= remaining) goto failed;

#endif
                if(!res->size) {
                    res->cookies = malloc(4 * sizeof(*res->cookies));
                    if(!res->cookies)
#ifdef __REACTOS__
                        goto failed;
#else
                        continue;
#endif
                    res->size = 4;
                }else if(res->cnt == res->size) {
#ifdef __REACTOS__
                    cookie_t **new_cookies;

                    if(res->size > UINT_MAX / 2 || (size_t)res->size > (size_t)-1 / (2 * sizeof(*res->cookies)))
                        goto failed;
                    new_cookies = realloc(res->cookies, res->size * 2 * sizeof(*res->cookies));
#else
                    cookie_t **new_cookies = realloc(res->cookies, res->size * 2 * sizeof(*res->cookies));
#endif
                    if(!new_cookies)
#ifdef __REACTOS__
                        goto failed;
#else
                        continue;
#endif
                    res->cookies = new_cookies;
                    res->size *= 2;
                }

                TRACE("%s = %s domain %s path %s\n", debugstr_w(cookie_iter->name), debugstr_w(cookie_iter->data),
                      debugstr_w(domain->domain), debugstr_wn(container->path.str, container->path.len));

                if(res->cnt)
                    res->string_len += 2; /* '; ' */
                res->cookies[res->cnt++] = cookie_iter;

                res->string_len += lstrlenW(cookie_iter->name);
                if(*cookie_iter->data)
                    res->string_len += 1 /* = */ + lstrlenW(cookie_iter->data);
            }
        }
    }

    return ERROR_SUCCESS;
#ifdef __REACTOS__

failed:
    free(res->cookies);
    memset(res, 0, sizeof(*res));
    return ERROR_NOT_ENOUGH_MEMORY;
#endif
}

static void cookie_set_to_string(const cookie_set_t *cookie_set, WCHAR *str)
{
    WCHAR *ptr = str;
    unsigned i, len;

    for(i=0; i<cookie_set->cnt; i++) {
        if(i) {
            *ptr++ = ';';
            *ptr++ = ' ';
        }

        len = lstrlenW(cookie_set->cookies[i]->name);
        memcpy(ptr, cookie_set->cookies[i]->name, len*sizeof(WCHAR));
        ptr += len;

        if(*cookie_set->cookies[i]->data) {
            *ptr++ = '=';
            len = lstrlenW(cookie_set->cookies[i]->data);
            memcpy(ptr, cookie_set->cookies[i]->data, len*sizeof(WCHAR));
            ptr += len;
        }
    }

    assert(ptr-str == cookie_set->string_len);
    TRACE("%s\n", debugstr_wn(str, ptr-str));
}

DWORD get_cookie_header(const WCHAR *host, const WCHAR *path, WCHAR **ret)
{
    cookie_set_t cookie_set = {0};
    DWORD res;

    static const WCHAR cookieW[] = {'C','o','o','k','i','e',':',' '};

#ifdef __REACTOS__
    *ret = NULL;
#endif
    EnterCriticalSection(&cookie_cs);

    res = get_cookie(substrz(host), substrz(path), INTERNET_COOKIE_HTTPONLY, &cookie_set);
    if(res != ERROR_SUCCESS) {
        LeaveCriticalSection(&cookie_cs);
        return res;
    }

    if(cookie_set.cnt) {
        WCHAR *header, *ptr;

#ifdef __REACTOS__
        if(cookie_set.string_len > INT_MAX - ARRAY_SIZE(cookieW) - 3) {
            free(cookie_set.cookies);
            LeaveCriticalSection(&cookie_cs);
            return ERROR_NOT_ENOUGH_MEMORY;
        }

#endif
        ptr = header = malloc(sizeof(cookieW) + (cookie_set.string_len + 3 /* crlf0 */) * sizeof(WCHAR));
        if(header) {
            memcpy(ptr, cookieW, sizeof(cookieW));
            ptr += ARRAY_SIZE(cookieW);

            cookie_set_to_string(&cookie_set, ptr);
#ifndef __REACTOS__
            free(cookie_set.cookies);
#endif
            ptr += cookie_set.string_len;

            *ptr++ = '\r';
            *ptr++ = '\n';
            *ptr++ = 0;

            *ret = header;
        }else {
            res = ERROR_NOT_ENOUGH_MEMORY;
        }
    }else {
        *ret = NULL;
    }

#ifdef __REACTOS__
    free(cookie_set.cookies);
#endif
    LeaveCriticalSection(&cookie_cs);
    return res;
}

static void free_cookie_domain_list(struct list *list)
{
    cookie_container_t *container;
    cookie_domain_t *domain;

    while(!list_empty(list)) {
        domain = LIST_ENTRY(list_head(list), cookie_domain_t, entry);

        free_cookie_domain_list(&domain->subdomain_list);

        while(!list_empty(&domain->path_list)) {
            container = LIST_ENTRY(list_head(&domain->path_list), cookie_container_t, entry);

            while(!list_empty(&container->cookie_list))
                delete_cookie(LIST_ENTRY(list_head(&container->cookie_list), cookie_t, entry));

            free(container->cookie_url);
            list_remove(&container->entry);
            free(container);
        }

        free(domain->domain);
        list_remove(&domain->entry);
        free(domain);
    }
}

/***********************************************************************
 *           InternetGetCookieExW (WININET.@)
 *
 * Retrieve cookie from the specified url
 *
 *  It should be noted that on windows the lpszCookieName parameter is "not implemented".
 *    So it won't be implemented here.
 *
 * RETURNS
 *    TRUE  on success
 *    FALSE on failure
 *
 */
BOOL WINAPI InternetGetCookieExW(LPCWSTR lpszUrl, LPCWSTR lpszCookieName,
        LPWSTR lpCookieData, LPDWORD lpdwSize, DWORD flags, void *reserved)
{
    cookie_set_t cookie_set = {0};
    substr_t host, path;
    DWORD res;
    BOOL ret;

    TRACE("(%s, %s, %p, %p, %lx, %p)\n", debugstr_w(lpszUrl),debugstr_w(lpszCookieName), lpCookieData, lpdwSize, flags, reserved);

    if (flags & ~INTERNET_COOKIE_HTTPONLY)
        FIXME("flags 0x%08lx not supported\n", flags);

#ifdef __REACTOS__
    if (!lpszUrl || !lpdwSize)
#else
    if (!lpszUrl)
#endif
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    ret = cookie_parse_url(lpszUrl, &host, &path);
    if (!ret) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    EnterCriticalSection(&cookie_cs);

    res = get_cookie(host, path, flags, &cookie_set);
    if(res != ERROR_SUCCESS) {
        LeaveCriticalSection(&cookie_cs);
        SetLastError(res);
        return FALSE;
    }

    if(cookie_set.cnt) {
        if(!lpCookieData || cookie_set.string_len+1 > *lpdwSize) {
            *lpdwSize = (cookie_set.string_len + 1) * sizeof(WCHAR);
            TRACE("returning %lu\n", *lpdwSize);
            if(lpCookieData) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                ret = FALSE;
            }
        }else {
            *lpdwSize = cookie_set.string_len + 1;
            cookie_set_to_string(&cookie_set, lpCookieData);
            lpCookieData[cookie_set.string_len] = 0;
        }
    }else {
        TRACE("no cookies found for %s\n", debugstr_wn(host.str, host.len));
        SetLastError(ERROR_NO_MORE_ITEMS);
        ret = FALSE;
    }

    free(cookie_set.cookies);
    LeaveCriticalSection(&cookie_cs);
    return ret;
}

/***********************************************************************
 *           InternetGetCookieW (WININET.@)
 *
 * Retrieve cookie for the specified URL.
 */
BOOL WINAPI InternetGetCookieW(const WCHAR *url, const WCHAR *name, WCHAR *data, DWORD *size)
{
    TRACE("(%s, %s, %s, %p)\n", debugstr_w(url), debugstr_w(name), debugstr_w(data), size);

    return InternetGetCookieExW(url, name, data, size, 0, NULL);
}

/***********************************************************************
 *           InternetGetCookieExA (WININET.@)
 *
 * Retrieve cookie from the specified url
 *
 * RETURNS
 *    TRUE  on success
 *    FALSE on failure
 *
 */
BOOL WINAPI InternetGetCookieExA(LPCSTR lpszUrl, LPCSTR lpszCookieName,
        LPSTR lpCookieData, LPDWORD lpdwSize, DWORD flags, void *reserved)
{
#ifdef __REACTOS__
    WCHAR *url = NULL, *name = NULL;
    DWORD len = 0, size = 0, error;
    BOOL r = FALSE;
#else
    WCHAR *url, *name;
    DWORD len, size = 0;
    BOOL r;
#endif

    TRACE("(%s %s %p %p(%lu) %lx %p)\n", debugstr_a(lpszUrl), debugstr_a(lpszCookieName),
          lpCookieData, lpdwSize, lpdwSize ? *lpdwSize : 0, flags, reserved);

#ifdef __REACTOS__
    if (!lpszUrl || !lpdwSize)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!(url = strdupAtoW(lpszUrl))) goto done;
    if (lpszCookieName && !(name = strdupAtoW(lpszCookieName))) goto done;
#else
    url = strdupAtoW(lpszUrl);
    name = strdupAtoW(lpszCookieName);
#endif

    r = InternetGetCookieExW( url, name, NULL, &len, flags, reserved );
    if( r )
    {
        WCHAR *szCookieData;

#ifdef __REACTOS__
        szCookieData = malloc(len);
#else
        szCookieData = malloc(len * sizeof(WCHAR));
#endif
        if( !szCookieData )
        {
#ifdef __REACTOS__
            SetLastError(ERROR_OUTOFMEMORY);
#endif
            r = FALSE;
        }
        else
        {
#ifdef __REACTOS__
            len /= sizeof(WCHAR);
#endif
            r = InternetGetCookieExW( url, name, szCookieData, &len, flags, reserved );

            if(r) {
                size = WideCharToMultiByte( CP_ACP, 0, szCookieData, len, NULL, 0, NULL, NULL);
#ifdef __REACTOS__
                if (!size)
                    r = FALSE;
                else if(lpCookieData) {
#else
                if(lpCookieData) {
#endif
                    if(*lpdwSize >= size) {
#ifdef __REACTOS__
                        r = WideCharToMultiByte(CP_ACP, 0, szCookieData, len, lpCookieData, size, NULL, NULL) != 0;
#else
                        WideCharToMultiByte( CP_ACP, 0, szCookieData, len, lpCookieData, *lpdwSize, NULL, NULL);
#endif
                    }else {
                        SetLastError(ERROR_INSUFFICIENT_BUFFER);
                        r = FALSE;
                    }
                }
            }

#ifdef __REACTOS__
            error = GetLastError();
#endif
            free( szCookieData );
#ifdef __REACTOS__
            if (!r) SetLastError(error);
#endif
        }
    }
#ifdef __REACTOS__
done:
    error = GetLastError();
#endif
    *lpdwSize = size;
    free(name);
    free(url);
#ifdef __REACTOS__
    if (!r) SetLastError(error);
#endif
    return r;
}

/***********************************************************************
 *           InternetGetCookieA (WININET.@)
 *
 * See InternetGetCookieW.
 */
BOOL WINAPI InternetGetCookieA(const char *url, const char *name, char *data, DWORD *size)
{
    TRACE("(%s, %s, %p, %p)\n", debugstr_a(url), debugstr_a(name), data, size);

    return InternetGetCookieExA(url, name, data, size, 0, NULL);
}

static BOOL is_domain_legal_for_cookie(substr_t domain, substr_t full_domain)
{
    const WCHAR *ptr;

    if(!domain.len || *domain.str == '.' || !full_domain.len || *full_domain.str == '.') {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }

    if(domain.len > full_domain.len || !wmemchr(domain.str, '.', domain.len) || !wmemchr(full_domain.str, '.', full_domain.len))
        return FALSE;

    ptr = full_domain.str + full_domain.len - domain.len;
    if (wcsnicmp(domain.str, ptr, domain.len) || (full_domain.len > domain.len && ptr[-1] != '.')) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return TRUE;
}

/***********************************************************************
 *           IsDomainLegalCookieDomainW (WININET.@)
 */
BOOL WINAPI IsDomainLegalCookieDomainW(const WCHAR *domain, const WCHAR *full_domain)
{
    FIXME("(%s, %s) semi-stub\n", debugstr_w(domain), debugstr_w(full_domain));

    if (!domain || !full_domain) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    return is_domain_legal_for_cookie(substrz(domain), substrz(full_domain));
}

static void substr_skip(substr_t *str, size_t len)
{
    assert(str->len >= len);
    str->str += len;
    str->len -= len;
}

DWORD set_cookie(substr_t domain, substr_t path, substr_t name, substr_t data, DWORD flags)
{
    cookie_container_t *container;
    cookie_t *thisCookie;
    substr_t value;
    const WCHAR *end_ptr;
    FILETIME expiry, create;
    BOOL expired = FALSE, update_persistent = FALSE;
#ifdef __REACTOS__
    DWORD cookie_flags = 0, len, error;
#else
    DWORD cookie_flags = 0, len;
#endif

    TRACE("%s %s %s=%s %lx\n", debugstr_wn(domain.str, domain.len), debugstr_wn(path.str, path.len),
          debugstr_wn(name.str, name.len), debugstr_wn(data.str, data.len), flags);

    memset(&expiry,0,sizeof(expiry));
    GetSystemTimeAsFileTime(&create);

    /* lots of information can be parsed out of the cookie value */

    if(!(end_ptr = wmemchr(data.str, ';', data.len)))
       end_ptr = data.str + data.len;
    value = substr(data.str, end_ptr-data.str);
    data.str += value.len;
    data.len -= value.len;

    for(;;) {
        static const WCHAR szDomain[] = {'d','o','m','a','i','n','='};
        static const WCHAR szPath[] = {'p','a','t','h','='};
        static const WCHAR szExpires[] = {'e','x','p','i','r','e','s','='};
        static const WCHAR szSecure[] = {'s','e','c','u','r','e'};
        static const WCHAR szHttpOnly[] = {'h','t','t','p','o','n','l','y'};
        static const WCHAR szVersion[] = {'v','e','r','s','i','o','n','='};
        static const WCHAR max_ageW[] = {'m','a','x','-','a','g','e','='};

        /* Skip ';' */
        if(data.len)
            substr_skip(&data, 1);

        while(data.len && *data.str == ' ')
            substr_skip(&data, 1);

        if(!data.len)
            break;

        if(!(end_ptr = wmemchr(data.str, ';', data.len)))
            end_ptr = data.str + data.len;

        if(data.len >= (len = ARRAY_SIZE(szDomain)) && !wcsnicmp(data.str, szDomain, len)) {
            substr_skip(&data, len);

            if(data.len && *data.str == '.')
                substr_skip(&data, 1);

            if(!is_domain_legal_for_cookie(substr(data.str, end_ptr-data.str), domain))
                return COOKIE_STATE_UNKNOWN;

            domain = substr(data.str, end_ptr-data.str);
            TRACE("Parsing new domain %s\n", debugstr_wn(domain.str, domain.len));
        }else if(data.len >= (len = ARRAY_SIZE(szPath)) && !wcsnicmp(data.str, szPath, len)) {
            substr_skip(&data, len);
            path = substr(data.str, end_ptr - data.str);
            TRACE("Parsing new path %s\n", debugstr_wn(path.str, path.len));
        }else if(data.len >= (len = ARRAY_SIZE(szExpires)) && !wcsnicmp(data.str, szExpires, len)) {
            SYSTEMTIME st;
#ifdef __REACTOS__
            FILETIME new_expiry;
#endif
            WCHAR buf[128];
#ifdef __REACTOS__
            size_t expiry_len;
#endif

            substr_skip(&data, len);
#ifdef __REACTOS__
            expiry_len = end_ptr - data.str;
#endif

#ifdef __REACTOS__
            if(expiry_len && expiry_len < ARRAY_SIZE(buf)) {
                memcpy(buf, data.str, expiry_len*sizeof(WCHAR));
                buf[expiry_len] = 0;
#else
            if(end_ptr > data.str && (end_ptr - data.str < ARRAY_SIZE(buf) - 1)) {
                memcpy(buf, data.str, data.len*sizeof(WCHAR));
                buf[data.len] = 0;
#endif

#ifdef __REACTOS__
                if (InternetTimeToSystemTimeW(buf, &st, 0) && SystemTimeToFileTime(&st, &new_expiry)) {
                    expiry = new_expiry;
#else
                if (InternetTimeToSystemTimeW(data.str, &st, 0)) {
                    SystemTimeToFileTime(&st, &expiry);
#endif

                    if (CompareFileTime(&create,&expiry) > 0) {
                        TRACE("Cookie already expired.\n");
                        expired = TRUE;
                    }
                }
            }
        }else if(data.len >= (len = ARRAY_SIZE(szSecure)) && !wcsnicmp(data.str, szSecure, len)) {
            substr_skip(&data, len);
            FIXME("secure not handled\n");
        }else if(data.len >= (len = ARRAY_SIZE(szHttpOnly)) && !wcsnicmp(data.str, szHttpOnly, len)) {
            substr_skip(&data, len);

            if(!(flags & INTERNET_COOKIE_HTTPONLY)) {
                WARN("HTTP only cookie added without INTERNET_COOKIE_HTTPONLY flag\n");
                SetLastError(ERROR_INVALID_OPERATION);
                return COOKIE_STATE_REJECT;
            }

            cookie_flags |= INTERNET_COOKIE_HTTPONLY;
        }else if(data.len >= (len = ARRAY_SIZE(szVersion)) && !wcsnicmp(data.str, szVersion, len)) {
            substr_skip(&data, len);

            FIXME("version not handled (%s)\n",debugstr_wn(data.str, data.len));
        }else if(data.len >= (len = ARRAY_SIZE(max_ageW)) && !wcsnicmp(data.str, max_ageW, len)) {
            /* Native doesn't support Max-Age attribute. */
            WARN("Max-Age ignored\n");
        }else if(data.len) {
            FIXME("Unknown additional option %s\n", debugstr_wn(data.str, data.len));
        }

        substr_skip(&data, end_ptr - data.str);
    }

    EnterCriticalSection(&cookie_cs);

    load_persistent_cookie(domain, path);

    container = get_cookie_container(domain, path, !expired);
    if(!container) {
#ifdef __REACTOS__
        error = GetLastError();
#endif
        LeaveCriticalSection(&cookie_cs);
#ifdef __REACTOS__
        if(expired) return COOKIE_STATE_ACCEPT;
        SetLastError(error);
        return COOKIE_STATE_UNKNOWN;
#else
        return COOKIE_STATE_ACCEPT;
#endif
    }

    if(!expiry.dwLowDateTime && !expiry.dwHighDateTime)
        cookie_flags |= INTERNET_COOKIE_IS_SESSION;
    else
        update_persistent = TRUE;

    if ((thisCookie = find_cookie(container, name))) {
        if ((thisCookie->flags & INTERNET_COOKIE_HTTPONLY) && !(flags & INTERNET_COOKIE_HTTPONLY)) {
            WARN("An attempt to override httponly cookie\n");
            SetLastError(ERROR_INVALID_OPERATION);
            LeaveCriticalSection(&cookie_cs);
            return COOKIE_STATE_REJECT;
        }

        if (!(thisCookie->flags & INTERNET_COOKIE_IS_SESSION))
            update_persistent = TRUE;
#ifndef __REACTOS__
        delete_cookie(thisCookie);
#endif
    }

    TRACE("setting cookie %s=%s for domain %s path %s\n", debugstr_wn(name.str, name.len),
          debugstr_wn(value.str, value.len), debugstr_w(container->domain->domain),
          debugstr_wn(container->path.str, container->path.len));

    if (!expired) {
        cookie_t *new_cookie;

        new_cookie = alloc_cookie(name, value, expiry, create, cookie_flags);
        if(!new_cookie) {
#ifdef __REACTOS__
            error = GetLastError();
#endif
            LeaveCriticalSection(&cookie_cs);
#ifdef __REACTOS__
            SetLastError(error);
#endif
            return COOKIE_STATE_UNKNOWN;
        }

#ifdef __REACTOS__
        if(thisCookie) delete_cookie(thisCookie);
#endif
        add_cookie(container, new_cookie);
#ifdef __REACTOS__
    }else if(thisCookie) delete_cookie(thisCookie);
#else
    }
#endif

    if (!update_persistent || save_persistent_cookie(container))
    {
        LeaveCriticalSection(&cookie_cs);
        return COOKIE_STATE_ACCEPT;
    }
#ifdef __REACTOS__
    error = GetLastError();
#endif
    LeaveCriticalSection(&cookie_cs);
#ifdef __REACTOS__
    SetLastError(error);
#endif
    return COOKIE_STATE_UNKNOWN;
}

/***********************************************************************
 *           InternetSetCookieExW (WININET.@)
 *
 * Sets cookie for the specified url
 */
DWORD WINAPI InternetSetCookieExW(LPCWSTR lpszUrl, LPCWSTR lpszCookieName,
        LPCWSTR lpCookieData, DWORD flags, DWORD_PTR reserved)
{
    substr_t host, path, name, data;
    BOOL ret;
#ifdef __REACTOS__
    DWORD state;
#endif

    TRACE("(%s, %s, %s, %lx, %Ix)\n", debugstr_w(lpszUrl), debugstr_w(lpszCookieName),
          debugstr_w(lpCookieData), flags, reserved);

    if (flags & ~INTERNET_COOKIE_HTTPONLY)
        FIXME("flags %lx not supported\n", flags);

    if (!lpszUrl || !lpCookieData)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return COOKIE_STATE_UNKNOWN;
    }

    ret = cookie_parse_url(lpszUrl, &host, &path);
    if (!ret || !host.len) return COOKIE_STATE_UNKNOWN;

    if (!lpszCookieName) {
        const WCHAR *ptr;

        /* some apps (or is it us??) try to add a cookie with no cookie name, but
         * the cookie data in the form of name[=data].
         */
        if (!(ptr = wcschr(lpCookieData, '=')))
            ptr = lpCookieData + lstrlenW(lpCookieData);

        name = substr(lpCookieData, ptr - lpCookieData);
        data = substrz(*ptr == '=' ? ptr+1 : ptr);
    }else {
        name = substrz(lpszCookieName);
        data = substrz(lpCookieData);
    }

#ifdef __REACTOS__
    state = set_cookie(host, path, name, data, flags);
    if(state == COOKIE_STATE_UNKNOWN && GetLastError() == ERROR_OUTOFMEMORY)
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return state;
#else
    return set_cookie(host, path, name, data, flags);
#endif
}

/***********************************************************************
 *           InternetSetCookieW (WININET.@)
 *
 * Sets a cookie for the specified URL.
 */
BOOL WINAPI InternetSetCookieW(const WCHAR *url, const WCHAR *name, const WCHAR *data)
{
    TRACE("(%s, %s, %s)\n", debugstr_w(url), debugstr_w(name), debugstr_w(data));

    return InternetSetCookieExW(url, name, data, 0, 0) == COOKIE_STATE_ACCEPT;
}

/***********************************************************************
 *           InternetSetCookieA (WININET.@)
 *
 * Sets cookie for the specified url
 *
 * RETURNS
 *    TRUE  on success
 *    FALSE on failure
 *
 */
BOOL WINAPI InternetSetCookieA(LPCSTR lpszUrl, LPCSTR lpszCookieName,
    LPCSTR lpCookieData)
{
#ifndef __REACTOS__
    LPWSTR data, url, name;
    BOOL r;

#endif
    TRACE("(%s,%s,%s)\n", debugstr_a(lpszUrl),
        debugstr_a(lpszCookieName), debugstr_a(lpCookieData));

#ifdef __REACTOS__
    return InternetSetCookieExA(lpszUrl, lpszCookieName, lpCookieData, 0, 0) == COOKIE_STATE_ACCEPT;
#else
    url = strdupAtoW(lpszUrl);
    name = strdupAtoW(lpszCookieName);
    data = strdupAtoW(lpCookieData);

    r = InternetSetCookieW( url, name, data );

    free(data);
    free(name);
    free(url);
    return r;
#endif
}

/***********************************************************************
 *           InternetSetCookieExA (WININET.@)
 *
 * See InternetSetCookieExW.
 */
DWORD WINAPI InternetSetCookieExA( LPCSTR lpszURL, LPCSTR lpszCookieName, LPCSTR lpszCookieData,
                                   DWORD dwFlags, DWORD_PTR dwReserved)
{
#ifdef __REACTOS__
    WCHAR *data = NULL, *url = NULL, *name = NULL;
    DWORD r = COOKIE_STATE_UNKNOWN, error;
#else
    WCHAR *data, *url, *name;
    DWORD r;
#endif

    TRACE("(%s, %s, %s, %lx, %Ix)\n", debugstr_a(lpszURL), debugstr_a(lpszCookieName),
          debugstr_a(lpszCookieData), dwFlags, dwReserved);

#ifdef __REACTOS__
    if (lpszURL && !(url = strdupAtoW(lpszURL))) goto done;
    if (lpszCookieName && !(name = strdupAtoW(lpszCookieName))) goto done;
    if (lpszCookieData && !(data = strdupAtoW(lpszCookieData))) goto done;
#else
    url = strdupAtoW(lpszURL);
    name = strdupAtoW(lpszCookieName);
    data = strdupAtoW(lpszCookieData);
#endif

    r = InternetSetCookieExW(url, name, data, dwFlags, dwReserved);

#ifdef __REACTOS__
done:
    error = GetLastError();
#endif
    free(data);
    free(name);
    free(url);
#ifdef __REACTOS__
    if (r == COOKIE_STATE_UNKNOWN)
        SetLastError(error == ERROR_OUTOFMEMORY ? ERROR_NOT_ENOUGH_MEMORY : error);
#endif
    return r;
}

/***********************************************************************
 *           InternetClearAllPerSiteCookieDecisions (WININET.@)
 *
 * Clears all per-site decisions about cookies.
 *
 * RETURNS
 *    TRUE  on success
 *    FALSE on failure
 *
 */
BOOL WINAPI InternetClearAllPerSiteCookieDecisions( VOID )
{
    FIXME("stub\n");
    return TRUE;
}

/***********************************************************************
 *           InternetEnumPerSiteCookieDecisionA (WININET.@)
 *
 * See InternetEnumPerSiteCookieDecisionW.
 */
BOOL WINAPI InternetEnumPerSiteCookieDecisionA( LPSTR pszSiteName, ULONG *pcSiteNameSize,
                                                ULONG *pdwDecision, ULONG dwIndex )
{
    FIXME("(%s, %p, %p, 0x%08lx) stub\n",
          debugstr_a(pszSiteName), pcSiteNameSize, pdwDecision, dwIndex);
    return FALSE;
}

/***********************************************************************
 *           InternetEnumPerSiteCookieDecisionW (WININET.@)
 *
 * Enumerates all per-site decisions about cookies.
 *
 * RETURNS
 *    TRUE  on success
 *    FALSE on failure
 *
 */
BOOL WINAPI InternetEnumPerSiteCookieDecisionW( LPWSTR pszSiteName, ULONG *pcSiteNameSize,
                                                ULONG *pdwDecision, ULONG dwIndex )
{
    FIXME("(%s, %p, %p, 0x%08lx) stub\n",
          debugstr_w(pszSiteName), pcSiteNameSize, pdwDecision, dwIndex);
    return FALSE;
}

/***********************************************************************
 *           InternetGetPerSiteCookieDecisionA (WININET.@)
 */
BOOL WINAPI InternetGetPerSiteCookieDecisionA( LPCSTR pwchHostName, ULONG *pResult )
{
    FIXME("(%s, %p) stub\n", debugstr_a(pwchHostName), pResult);
    return FALSE;
}

/***********************************************************************
 *           InternetGetPerSiteCookieDecisionW (WININET.@)
 */
BOOL WINAPI InternetGetPerSiteCookieDecisionW( LPCWSTR pwchHostName, ULONG *pResult )
{
    FIXME("(%s, %p) stub\n", debugstr_w(pwchHostName), pResult);
    return FALSE;
}

/***********************************************************************
 *           InternetSetPerSiteCookieDecisionA (WININET.@)
 */
BOOL WINAPI InternetSetPerSiteCookieDecisionA( LPCSTR pchHostName, DWORD dwDecision )
{
    FIXME("(%s, 0x%08lx) stub\n", debugstr_a(pchHostName), dwDecision);
    return FALSE;
}

/***********************************************************************
 *           InternetSetPerSiteCookieDecisionW (WININET.@)
 */
BOOL WINAPI InternetSetPerSiteCookieDecisionW( LPCWSTR pchHostName, DWORD dwDecision )
{
    FIXME("(%s, 0x%08lx) stub\n", debugstr_w(pchHostName), dwDecision);
    return FALSE;
}

void free_cookie(void)
{
    EnterCriticalSection(&cookie_cs);

    free_cookie_domain_list(&domain_list);

    LeaveCriticalSection(&cookie_cs);
}
