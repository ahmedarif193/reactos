/*
 * Network address control
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <wine/config.h>

#define WIN32_NO_STATUS
#define _INC_WINDOWS

#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <winuser.h>
#include <ws2tcpip.h>
#include <windns.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <commctrl.h>

#include "wine/shell32_main.h"

typedef struct _NETWORK_ADDRESS_STATE
{
    DWORD ParseError;
    DWORD AllowType;
    HWND Hwnd;
} NETWORK_ADDRESS_STATE, *PNETWORK_ADDRESS_STATE;

static WNDPROC NetworkAddressEditWndProc;
static INT NetworkAddressExtraOffset;

static HRESULT
NetworkAddressGetAddress(PNETWORK_ADDRESS_STATE State, PNC_ADDRESS Address)
{
    WCHAR *Text;
    INT Length;
    DWORD Error;
    HRESULT Result;

    if (!Address || !Address->pAddrInfo)
        return E_INVALIDARG;

    Length = GetWindowTextLengthW(State->Hwnd);
    if (!Length)
        return S_FALSE;

    Text = HeapAlloc(GetProcessHeap(), 0, (Length + 1) * sizeof(*Text));
    if (!Text)
        return E_OUTOFMEMORY;

    if (!GetWindowTextW(State->Hwnd, Text, Length + 1))
    {
        Result = GetLastError();
    }
    else if (!Text[0])
    {
        Result = S_FALSE;
    }
    else
    {
        Address->pAddrInfo->Format = NET_ADDRESS_FORMAT_UNSPECIFIED;
        Error = ParseNetworkString(Text,
                                   State->AllowType,
                                   Address->pAddrInfo,
                                   &Address->PortNumber,
                                   &Address->PrefixLength);
        State->ParseError = Error;
        Result = Error ? HRESULT_FROM_WIN32(Error) : S_OK;
    }

    HeapFree(GetProcessHeap(), 0, Text);
    return Result;
}

static LRESULT
NetworkAddressMessage(PNETWORK_ADDRESS_STATE State,
                      HWND Hwnd,
                      UINT Message,
                      WPARAM WParam,
                      LPARAM LParam)
{
    EDITBALLOONTIP Tip;
    WCHAR ErrorText[256];

    switch (Message)
    {
        case NCM_GETADDRESS:
            return NetworkAddressGetAddress(State, (PNC_ADDRESS)LParam);

        case NCM_SETALLOWTYPE:
            State->AllowType = WParam;
            return TRUE;

        case NCM_GETALLOWTYPE:
            return State->AllowType;

        case NCM_DISPLAYERRORTIP:
            if (!State->ParseError)
                return S_OK;

            if (!FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL,
                                State->ParseError,
                                0,
                                ErrorText,
                                ARRAY_SIZE(ErrorText),
                                NULL))
            {
                return HRESULT_FROM_WIN32(GetLastError());
            }

            Tip.cbStruct = sizeof(Tip);
            Tip.pszTitle = NULL;
            Tip.pszText = ErrorText;
            Tip.ttiIcon = 0;
            return SendMessageW(Hwnd, EM_SHOWBALLOONTIP, 0, (LPARAM)&Tip) ?
                   S_OK : E_FAIL;

        default:
            return CallWindowProcW(NetworkAddressEditWndProc,
                                   Hwnd,
                                   Message,
                                   WParam,
                                   LParam);
    }
}

static LRESULT CALLBACK
NetworkAddressWndProc(HWND Hwnd, UINT Message, WPARAM WParam, LPARAM LParam)
{
    PNETWORK_ADDRESS_STATE State;
    LRESULT Result;

    State = (PNETWORK_ADDRESS_STATE)GetWindowLongPtrW(Hwnd,
                                                      NetworkAddressExtraOffset);
    if (State)
    {
        Result = NetworkAddressMessage(State, Hwnd, Message, WParam, LParam);
        if (Message == WM_NCDESTROY)
        {
            SetWindowLongPtrW(Hwnd, NetworkAddressExtraOffset, 0);
            HeapFree(GetProcessHeap(), 0, State);
        }
        return Result;
    }

    if (Message == WM_NCCREATE)
    {
        State = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*State));
        if (!State)
            return FALSE;

        State->AllowType = NET_STRING_ANY_ADDRESS;
        State->Hwnd = Hwnd;
        SetWindowLongPtrW(Hwnd, NetworkAddressExtraOffset, (LONG_PTR)State);
        return NetworkAddressMessage(State, Hwnd, Message, WParam, LParam);
    }

    return CallWindowProcW(NetworkAddressEditWndProc,
                           Hwnd,
                           Message,
                           WParam,
                           LParam);
}

BOOL WINAPI
InitNetworkAddressControl(VOID)
{
    WNDCLASSEXW Class = { sizeof(Class) };
    ATOM Atom;

    if (NetworkAddressEditWndProc)
        return TRUE;

    if (!GetClassInfoExW(NULL, L"EDIT", &Class))
        return FALSE;

    NetworkAddressEditWndProc = Class.lpfnWndProc;
    NetworkAddressExtraOffset = Class.cbWndExtra;

    Class.style |= CS_GLOBALCLASS;
    Class.lpfnWndProc = NetworkAddressWndProc;
    Class.cbWndExtra += sizeof(PVOID);
    Class.hInstance = shell32_hInstance;
    Class.lpszMenuName = NULL;
    Class.lpszClassName = WC_NETADDRESS;

    Atom = RegisterClassExW(&Class);
    if (Atom)
        return TRUE;

    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
