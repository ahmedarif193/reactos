/*
 * PROJECT:         ReactOS winsta.dll
 * FILE:            dll/win32/winsta/winsta.h
 * PURPOSE:         WinStation
 * PROGRAMMER:      Aleksey Bragin
 * NOTES:           This file contains exported functions relevant to
 *                  userinit, winlogon, lsass and friends.
 */

#ifndef _WINSTA_H
#define _WINSTA_H

#include <stdarg.h>

#include <windef.h>
#include <winbase.h>
#include <winternl.h>

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(winsta);

/* WinSta calling convention */
#define WINSTAAPI WINAPI

/*
 * Exported WinStation APIs that are actually implemented in this module.
 */

BOOLEAN
WINSTAAPI
WinStationGetProcessSid(
    _In_opt_ HANDLE hServer,
    _In_ ULONG ProcessId,
    _In_ LARGE_INTEGER ProcessStartTime,
    _Out_writes_bytes_opt_(*SidLength) PSID ProcessUserSid,
    _Inout_ PULONG SidLength);

#endif /* _WINSTA_H */
