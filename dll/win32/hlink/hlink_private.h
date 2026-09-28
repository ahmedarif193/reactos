/*
 * Copyright 2007 Jacek Caban for CodeWeavers
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
#ifdef __REACTOS__
#include <wchar.h>
#endif

#define COBJMACROS

#include "winerror.h"
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "ole2.h"
#include "hlink.h"

extern HRESULT HLink_Constructor(IUnknown*,REFIID,void**);
extern HRESULT HLinkBrowseContext_Constructor(IUnknown*,REFIID,void**);

static inline LPWSTR hlink_co_strdupW(LPCWSTR str)
{
    LPWSTR ret = NULL;

    if(str) {
#ifdef __REACTOS__
        SIZE_T size;
#else
        DWORD size;
#endif

#ifdef __REACTOS__
        size = wcslen(str);
        if (size >= ~(SIZE_T)0 / sizeof(WCHAR)) return NULL;
        size = (size + 1) * sizeof(WCHAR);
#else
        size = (lstrlenW(str)+1)*sizeof(WCHAR);
#endif
        ret = CoTaskMemAlloc(size);
#ifdef __REACTOS__
        if (ret) memcpy(ret, str, size);
#else
        memcpy(ret, str, size);
#endif
    }

    return ret;
}
