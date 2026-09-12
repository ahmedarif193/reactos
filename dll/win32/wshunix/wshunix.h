/*
 * PROJECT:     ReactOS WinSock Helper DLL for AF_UNIX
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#ifndef __WSHUNIX_H
#define __WSHUNIX_H

#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <stdarg.h>

#include <windef.h>
#include <winbase.h>
#include <wsahelp.h>
#include <afunix.h>
#include <rtlfuncs.h>

#define EXPORT WINAPI

#define UNIX_DEVICE_NAME L"\\Device\\Afunix"

typedef struct _UNIX_SOCKET_CONTEXT {
    INT AddressFamily;
    INT SocketType;
    INT Protocol;
    DWORD Flags;
} UNIX_SOCKET_CONTEXT, *PUNIX_SOCKET_CONTEXT;

#endif /* __WSHUNIX_H */
