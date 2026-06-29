/*
 * PROJECT:     ReactOS DXGI API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Precompiled header
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#ifndef _DXGI_APITEST_PRECOMP_H_
#define _DXGI_APITEST_PRECOMP_H_

#include <stdarg.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#define COBJMACROS
#define CONST_VTABLE

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>
#include <objbase.h>
#include <dxgi.h>

#include <apitest.h>

#endif /* _DXGI_APITEST_PRECOMP_H_ */
