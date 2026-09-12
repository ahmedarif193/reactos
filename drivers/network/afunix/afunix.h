/*
 * PROJECT:     ReactOS AF_UNIX local stream transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#ifndef _AFUNIX_PCH_
#define _AFUNIX_PCH_

#include <ntifs.h>
#include <ndk/obtypes.h>
#include <tdikrnl.h>
#include <reactos/tdiunix.h>

#define TAG_AFUNIX_ENDPOINT 'eufA'
#define TAG_AFUNIX_CONN     'cufA'
#define TAG_AFUNIX_PIPE     'pufA'
#define TAG_AFUNIX_BUFFER   'bufA'
#define TAG_AFUNIX_REQ      'rufA'

#define AFUNIX_PIPE_SIZE   0x10000
#define AFUNIX_MAX_BACKLOG 200

typedef struct _AFUNIX_PIPE
{
    LONG RefCount;
    KSPIN_LOCK Lock;
    PUCHAR Buffer;
    ULONG Size;
    ULONG Head;
    ULONG Count;
    BOOLEAN WriteClosed;
    BOOLEAN ReadClosed;
    LIST_ENTRY ReceiveQueue;
    LIST_ENTRY SendQueue;
} AFUNIX_PIPE, *PAFUNIX_PIPE;

typedef enum _AFUNIX_FILE_TYPE
{
    AfunixFileControl = 0,
    AfunixFileAddress,
    AfunixFileConnection
} AFUNIX_FILE_TYPE;

typedef struct _AFUNIX_ENDPOINT
{
    LIST_ENTRY ListEntry;
    LONG RefCount;
    BOOLEAN Registered;
    BOOLEAN HasListened;
    ULONG PathLength;
    CHAR Path[AFUNIX_PATH_LENGTH];
    LIST_ENTRY ListenQueue;
    LIST_ENTRY ConnectQueue;
    ULONG ConnectQueueCount;
    HANDLE MarkerFile;
    UNICODE_STRING MarkerPath;
} AFUNIX_ENDPOINT, *PAFUNIX_ENDPOINT;

typedef struct _AFUNIX_CONNECTION
{
    LONG RefCount;
    LIST_ENTRY ListenEntry;
    PAFUNIX_ENDPOINT Endpoint;
    PAFUNIX_PIPE Rx;
    PAFUNIX_PIPE Tx;
    BOOLEAN Connected;
    BOOLEAN Disconnected;
    PIRP PendingListen;
    PIRP PendingConnect;
    ULONG PeerPathLength;
    CHAR PeerPath[AFUNIX_PATH_LENGTH];
} AFUNIX_CONNECTION, *PAFUNIX_CONNECTION;

typedef struct _AFUNIX_CONNREQ
{
    LIST_ENTRY ListEntry;
    PAFUNIX_PIPE ToServer;
    PAFUNIX_PIPE ToClient;
    PAFUNIX_CONNECTION Client;
    ULONG PathLength;
    CHAR Path[AFUNIX_PATH_LENGTH];
} AFUNIX_CONNREQ, *PAFUNIX_CONNREQ;

typedef struct _AFUNIX_FCB
{
    AFUNIX_FILE_TYPE Type;
    PAFUNIX_ENDPOINT Endpoint;
    PAFUNIX_CONNECTION Connection;
} AFUNIX_FCB, *PAFUNIX_FCB;

#endif /* _AFUNIX_PCH_ */
