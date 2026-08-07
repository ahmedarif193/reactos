/*
 * PROJECT:     ReactOS WinSock 2 API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Extended Protocol-Independent Address Resolution
 * COPYRIGHT:   Adapted from Wine dlls/ws2_32/protocol.c
 */

#include <ws2_32.h>

#include <ws2tcpip.h>

#define NDEBUG
#include <debug.h>

static
INT
WSAAPI
ConvertAddrInfoToAddrInfoEx(IN PADDRINFOW AddrInfo,
                            OUT PADDRINFOEXW *ppResult)
{
    PADDRINFOEXW New;
    PADDRINFOEXW *Next = ppResult;
    SIZE_T NameSize;

    *ppResult = NULL;

    for (; AddrInfo; AddrInfo = AddrInfo->ai_next)
    {
        New = HeapAlloc(WsSockHeap, HEAP_ZERO_MEMORY, sizeof(ADDRINFOEXW));
        if (!New) goto Failure;

        *Next = New;

        New->ai_flags = AddrInfo->ai_flags;
        New->ai_family = AddrInfo->ai_family;
        New->ai_socktype = AddrInfo->ai_socktype;
        New->ai_protocol = AddrInfo->ai_protocol;

        if (AddrInfo->ai_addr)
        {
            New->ai_addr = HeapAlloc(WsSockHeap, 0, AddrInfo->ai_addrlen);
            if (!New->ai_addr) goto Failure;

            New->ai_addrlen = AddrInfo->ai_addrlen;
            RtlMoveMemory(New->ai_addr, AddrInfo->ai_addr, AddrInfo->ai_addrlen);
        }

        if (AddrInfo->ai_canonname)
        {
            NameSize = (wcslen(AddrInfo->ai_canonname) + 1) * sizeof(WCHAR);
            New->ai_canonname = HeapAlloc(WsSockHeap, 0, NameSize);
            if (!New->ai_canonname) goto Failure;

            RtlMoveMemory(New->ai_canonname, AddrInfo->ai_canonname, NameSize);
        }

        Next = &New->ai_next;
    }

    return ERROR_SUCCESS;

Failure:
    FreeAddrInfoExW(*ppResult);
    *ppResult = NULL;
    return WSA_NOT_ENOUGH_MEMORY;
}

INT
WSAAPI
GetAddrInfoExW(IN PCWSTR pName,
               IN PCWSTR pServiceName,
               IN DWORD dwNameSpace,
               IN LPGUID lpNspId,
               IN const ADDRINFOEXW *hints,
               OUT PADDRINFOEXW *ppResult,
               IN struct timeval *timeout,
               IN LPOVERLAPPED lpOverlapped,
               IN LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
               OUT LPHANDLE lpHandle)
{
    INT ErrorCode;
    ADDRINFOW Hints;
    PADDRINFOW HintsW = NULL;
    PADDRINFOW Result = NULL;

    DPRINT("GetAddrInfoExW: %S, %S, %lu, %p\n", pName, pServiceName, dwNameSpace, ppResult);

    if (lpHandle) *lpHandle = NULL;

    if ((ErrorCode = WsQuickProlog()) != ERROR_SUCCESS)
    {
        SetLastError(ErrorCode);
        return ErrorCode;
    }

    if (!ppResult)
    {
        SetLastError(WSAEFAULT);
        return WSAEFAULT;
    }

    *ppResult = NULL;

    if ((lpOverlapped) || (lpCompletionRoutine))
    {
        SetLastError(WSAEOPNOTSUPP);
        return WSAEOPNOTSUPP;
    }

    if (((dwNameSpace != NS_ALL) && (dwNameSpace != NS_DNS)) || (lpNspId))
    {
        SetLastError(WSAEINVAL);
        return WSAEINVAL;
    }

    if (!(pName) && !(pServiceName))
    {
        SetLastError(WSAHOST_NOT_FOUND);
        return WSAHOST_NOT_FOUND;
    }

    if (hints)
    {
        RtlZeroMemory(&Hints, sizeof(Hints));
        Hints.ai_flags = hints->ai_flags;
        Hints.ai_family = hints->ai_family;
        Hints.ai_socktype = hints->ai_socktype;
        Hints.ai_protocol = hints->ai_protocol;
        HintsW = &Hints;
    }

    ErrorCode = GetAddrInfoW(pName, pServiceName, HintsW, &Result);
    if (ErrorCode != ERROR_SUCCESS)
    {
        SetLastError(ErrorCode);
        return ErrorCode;
    }

    ErrorCode = ConvertAddrInfoToAddrInfoEx(Result, ppResult);

    freeaddrinfo((LPADDRINFO)Result);

    SetLastError(ErrorCode);
    return ErrorCode;
}

VOID
WSAAPI
FreeAddrInfoExW(IN PADDRINFOEXW pAddrInfoEx)
{
    PADDRINFOEXW NextInfo;

    for (NextInfo = pAddrInfoEx; NextInfo; NextInfo = pAddrInfoEx)
    {
        if (NextInfo->ai_canonname)
        {
            HeapFree(WsSockHeap, 0, NextInfo->ai_canonname);
        }

        if (NextInfo->ai_addr)
        {
            HeapFree(WsSockHeap, 0, NextInfo->ai_addr);
        }

        pAddrInfoEx = NextInfo->ai_next;

        HeapFree(WsSockHeap, 0, NextInfo);
    }
}

INT
WSAAPI
GetAddrInfoExCancel(IN LPHANDLE lpHandle)
{
    DPRINT("GetAddrInfoExCancel: %p\n", lpHandle);

    SetLastError(WSA_INVALID_HANDLE);
    return WSA_INVALID_HANDLE;
}
