/*
 * WS2_32 Windows 10 API Stubs
 * Copyright 2024 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "../inc/ws2_32.h"

#define NDEBUG
#include <debug.h>

/* Windows 10 UDP Message Size APIs */

/*
 * @unimplemented
 */
INT WSAAPI
WSASetUdpSendMessageSize(
    _In_ SOCKET Socket,
    _In_ DWORD MsgSize)
{
    DPRINT1("WSASetUdpSendMessageSize: stub\n");
    WSASetLastError(WSAEOPNOTSUPP);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT WSAAPI
WSAGetUdpSendMessageSize(
    _In_ SOCKET Socket,
    _Out_ PDWORD MsgSize)
{
    DPRINT1("WSAGetUdpSendMessageSize: stub\n");
    WSASetLastError(WSAEOPNOTSUPP);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT WSAAPI
WSASetUdpReceiveMessageSize(
    _In_ SOCKET Socket,
    _In_ DWORD MsgSize)
{
    DPRINT1("WSASetUdpReceiveMessageSize: stub\n");
    WSASetLastError(WSAEOPNOTSUPP);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT WSAAPI
WSAGetUdpReceiveMessageSize(
    _In_ SOCKET Socket,
    _Out_ PDWORD MsgSize)
{
    DPRINT1("WSAGetUdpReceiveMessageSize: stub\n");
    WSASetLastError(WSAEOPNOTSUPP);
    return SOCKET_ERROR;
}

/* EOF */