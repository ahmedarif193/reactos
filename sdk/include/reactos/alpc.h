/*
 * Advanced Local Procedure Call definitions
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
 * Copyright 2026 Ahmed ARIF
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef _REACTOS_ALPC_H_
#define _REACTOS_ALPC_H_

typedef HANDLE ALPC_HANDLE, *PALPC_HANDLE;

#define ALPC_PORFLG_ALLOW_IMPERSONATION         0x00010000
#define ALPC_PORFLG_ALLOW_LPC_REQUESTS          0x00020000
#define ALPC_PORFLG_WAITABLE_PORT               0x00040000
#define ALPC_PORFLG_ALLOW_DUP_OBJECT            0x00080000
#define ALPC_PORFLG_SYSTEM_PROCESS              0x00100000
#define ALPC_PORFLG_LRPC_WAKE_POLICY1           0x00200000
#define ALPC_PORFLG_LRPC_WAKE_POLICY2           0x00400000
#define ALPC_PORFLG_LRPC_WAKE_POLICY3           0x00800000
#define ALPC_PORFLG_DIRECT_MESSAGE              0x01000000
#define ALPC_PORFLG_ALLOW_MULTIHANDLE_ATTRIBUTE 0x02000000

#define ALPC_PORTFLG_ALLOWIMPERSONATION         ALPC_PORFLG_ALLOW_IMPERSONATION
#define ALPC_PORTFLG_NON_BLOCKING_RECEIVE       ALPC_PORFLG_WAITABLE_PORT
#define ALPC_PORTFLG_ALLOW_DUP_OBJECT           ALPC_PORFLG_ALLOW_DUP_OBJECT

#define ALPC_PORFLG_OBJECT_TYPE_FILE            0x00000001
#define ALPC_PORFLG_OBJECT_TYPE_INVALID         0x00000002
#define ALPC_PORFLG_OBJECT_TYPE_THREAD          0x00000004
#define ALPC_PORFLG_OBJECT_TYPE_SEMAPHORE       0x00000008
#define ALPC_PORFLG_OBJECT_TYPE_EVENT           0x00000010
#define ALPC_PORFLG_OBJECT_TYPE_PROCESS         0x00000020
#define ALPC_PORFLG_OBJECT_TYPE_MUTEX           0x00000040
#define ALPC_PORFLG_OBJECT_TYPE_SECTION         0x00000080
#define ALPC_PORFLG_OBJECT_TYPE_REGKEY          0x00000100
#define ALPC_PORFLG_OBJECT_TYPE_TOKEN           0x00000200
#define ALPC_PORFLG_OBJECT_TYPE_COMPOSITION     0x00000400
#define ALPC_PORFLG_OBJECT_TYPE_JOB             0x00000800
#define ALPC_PORFLG_OBJECT_TYPE_ALL_OBJECTS     0x00000FFD

#define ALPC_SYNC_CONNECTION                    0x00020000
#define ALPC_USER_WAIT_MODE                     0x00100000
#define ALPC_WAIT_IS_ALERTABLE                  0x00200000

#define ALPC_MSGFLG_NONE                        0x00000000
#define ALPC_MSGFLG_REPLY_MESSAGE               0x00010000
#define ALPC_MSGFLG_SYNC_REQUEST                0x00020000
#define ALPC_MSGFLG_TRACK_PORT_REFERENCES       0x00040000
#define ALPC_MSGFLG_WAIT_USER_MODE              0x00100000
#define ALPC_MSGFLG_WAIT_ALERTABLE              0x00200000
#define ALPC_MSGFLG_WOW64_CALL                  0x80000000

#define ALPC_CANCELFLG_TRY_CANCEL               0x00000001
#define ALPC_CANCELFLG_NO_CONTEXT_CHECK         0x00000008
#define ALPC_CANCELFLGP_FLUSH                   0x00010000

#define ALPC_DISCONNECT_NO_FLUSH_ON_CLOSE       0x00000001

#define ALPC_IMPERSONATEFLG_ANONYMOUS_FALLBACK  0x00000001
#define ALPC_IMPERSONATEFLG_REQUIRE_IMPERSONATION_LEVEL 0x00000002

#define ALPC_SECFLG_CREATE_HANDLE               0x00020000
#define ALPC_SECFLG_NOSECTIONHANDLE             0x00040000

#define ALPC_VIEWFLG_UNMAP_EXISTING             0x00010000
#define ALPC_VIEWFLG_AUTO_RELEASE               0x00020000
#define ALPC_VIEWFLG_NOT_SECURE                 0x00040000

#define ALPC_PORTSECTIONFLG_SECURE              0x00040000

#define ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS    0x00010000
#define ALPC_HANDLEFLG_DUPLICATE_SAME_ATTRIBUTES 0x00020000
#define ALPC_HANDLEFLG_INDIRECT                 0x00040000
#define ALPC_HANDLEFLG_DUPLICATE_INHERIT        0x00080000

#define ALPC_MESSAGE_SECURITY_ATTRIBUTE         0x80000000
#define ALPC_MESSAGE_VIEW_ATTRIBUTE             0x40000000
#define ALPC_MESSAGE_CONTEXT_ATTRIBUTE          0x20000000
#define ALPC_MESSAGE_HANDLE_ATTRIBUTE           0x10000000
#define ALPC_MESSAGE_TOKEN_ATTRIBUTE            0x08000000
#define ALPC_MESSAGE_DIRECT_ATTRIBUTE           0x04000000
#define ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE   0x02000000
#define ALPC_MESSAGE_ATTRIBUTE_ALL              0xfe000000

#define ALPC_MAX_ALLOWED_MESSAGE_LENGTH         0x0000FFFF

typedef enum _ALPC_MESSAGE_TYPE
{
    ALPC_MESSAGE_TYPE_REQUEST = 1,
    ALPC_MESSAGE_TYPE_REPLY,
    ALPC_MESSAGE_TYPE_DATAGRAM,
    ALPC_MESSAGE_TYPE_LOST_REPLY,
    ALPC_MESSAGE_TYPE_PORT_CLOSED,
    ALPC_MESSAGE_TYPE_CLIENT_DIED,
    ALPC_MESSAGE_TYPE_EXCEPTION,
    ALPC_MESSAGE_TYPE_DEBUG_EVENT,
    ALPC_MESSAGE_TYPE_ERROR_EVENT,
    ALPC_MESSAGE_TYPE_CONNECTION_REQUEST,
    ALPC_MESSAGE_TYPE_CONNECTION_REPLY,
    ALPC_MESSAGE_TYPE_CANCELED,
    ALPC_MESSAGE_TYPE_UNREGISTER_PROCESS
} ALPC_MESSAGE_TYPE;

typedef struct _ALPC_PORT_MESSAGE
{
    union
    {
        struct
        {
            USHORT DataLength;
            USHORT TotalLength;
        } DUMMYSTRUCTNAME1;
        ULONG Length;
    } DUMMYUNIONNAME1;
    union
    {
        struct
        {
            USHORT Type;
            USHORT DataInfoOffset;
        } DUMMYSTRUCTNAME2;
        ULONG ZeroInit;
    } DUMMYUNIONNAME2;
    union
    {
        CLIENT_ID ClientId;
        double DoNotUseThisField;
    } DUMMYUNIONNAME3;
    ULONG MessageId;
    union
    {
        SIZE_T ClientViewSize;
        ULONG CallbackId;
    } DUMMYUNIONNAME4;
} ALPC_PORT_MESSAGE, *PALPC_PORT_MESSAGE, ALPC_PORT_MESSAGE_HEADER, *PALPC_PORT_MESSAGE_HEADER;

typedef struct _ALPC_MESSAGE_ATTRIBUTES
{
    ULONG AllocatedAttributes;
    ULONG ValidAttributes;
} ALPC_MESSAGE_ATTRIBUTES, *PALPC_MESSAGE_ATTRIBUTES;

typedef struct _ALPC_SECURITY_ATTR
{
    ULONG Flags;
    SECURITY_QUALITY_OF_SERVICE *QoS;
    ALPC_HANDLE ContextHandle;
} ALPC_SECURITY_ATTR, *PALPC_SECURITY_ATTR;

typedef struct _ALPC_VIEW_ATTR
{
    ULONG Flags;
    ALPC_HANDLE SectionHandle;
    void *ViewBase;
    SIZE_T ViewSize;
} ALPC_VIEW_ATTR, *PALPC_VIEW_ATTR, ALPC_DATA_VIEW_ATTR, *PALPC_DATA_VIEW_ATTR;

typedef struct _ALPC_CONTEXT_ATTR
{
    void *PortContext;
    void *MessageContext;
    ULONG Sequence;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_CONTEXT_ATTR, *PALPC_CONTEXT_ATTR;

typedef struct _ALPC_MESSAGE_HANDLE_INFORMATION
{
    ULONG Index;
    ULONG Flags;
    ULONG Handle;
    ULONG ObjectType;
    ACCESS_MASK GrantedAccess;
} ALPC_MESSAGE_HANDLE_INFORMATION, *PALPC_MESSAGE_HANDLE_INFORMATION;

typedef struct _ALPC_HANDLE_ATTR
{
    ULONG Flags;
    union
    {
        HANDLE Handle;
        PALPC_MESSAGE_HANDLE_INFORMATION HandleAttrArray;
    };
    union
    {
        ULONG ObjectType;
        ULONG HandleCount;
    };
    ACCESS_MASK DesiredAccess;
} ALPC_HANDLE_ATTR, *PALPC_HANDLE_ATTR;

typedef struct _ALPC_HANDLE_ATTR32
{
    ULONG Flags;
    ULONG Handle;
    ULONG ObjectType;
    ACCESS_MASK DesiredAccess;
} ALPC_HANDLE_ATTR32, *PALPC_HANDLE_ATTR32;

typedef struct _ALPC_TOKEN_ATTR
{
    ULONGLONG TokenId;
    ULONGLONG AuthenticationId;
    ULONGLONG ModifiedId;
} ALPC_TOKEN_ATTR, *PALPC_TOKEN_ATTR;

typedef struct _ALPC_DIRECT_ATTR
{
    HANDLE Event;
} ALPC_DIRECT_ATTR, *PALPC_DIRECT_ATTR;

typedef struct _ALPC_WORK_ON_BEHALF_ATTR
{
    ULONGLONG Ticket;
} ALPC_WORK_ON_BEHALF_ATTR, *PALPC_WORK_ON_BEHALF_ATTR;

typedef enum _ALPC_PORT_INFORMATION_CLASS
{
    AlpcBasicInformation,
    AlpcPortInformation,
    AlpcAssociateCompletionPortInformation,
    AlpcConnectedSIDInformation,
    AlpcServerInformation,
    AlpcMessageZoneInformation,
    AlpcRegisterCompletionListInformation,
    AlpcUnregisterCompletionListInformation,
    AlpcAdjustCompletionListConcurrencyCountInformation,
    AlpcRegisterCallbackInformation,
    AlpcCompletionListRundownInformation,
    AlpcWaitForPortReferences,
    AlpcServerSessionInformation,
    MaxAlpcPortInfoClass
} ALPC_PORT_INFORMATION_CLASS;

typedef struct _ALPC_BASIC_INFORMATION
{
    ULONG Flags;
    ULONG SequenceNo;
    PVOID PortContext;
} ALPC_BASIC_INFORMATION, *PALPC_BASIC_INFORMATION;

typedef struct _ALPC_PORT_ASSOCIATE_COMPLETION_PORT
{
    PVOID CompletionKey;
    HANDLE CompletionPort;
} ALPC_PORT_ASSOCIATE_COMPLETION_PORT, *PALPC_PORT_ASSOCIATE_COMPLETION_PORT;

typedef struct _ALPC_SERVER_INFORMATION
{
    union
    {
        struct
        {
            HANDLE ThreadHandle;
        } In;
        struct
        {
            BOOLEAN ThreadBlocked;
            HANDLE ConnectedProcessId;
            UNICODE_STRING ConnectionPortName;
        } Out;
    };
} ALPC_SERVER_INFORMATION, *PALPC_SERVER_INFORMATION;

typedef struct _ALPC_PORT_MESSAGE_ZONE_INFORMATION
{
    PVOID Buffer;
    ULONG Size;
} ALPC_PORT_MESSAGE_ZONE_INFORMATION, *PALPC_PORT_MESSAGE_ZONE_INFORMATION;

typedef struct _ALPC_PORT_COMPLETION_LIST_INFORMATION
{
    PVOID Buffer;
    ULONG Size;
    ULONG ConcurrencyCount;
    ULONG AttributeFlags;
} ALPC_PORT_COMPLETION_LIST_INFORMATION, *PALPC_PORT_COMPLETION_LIST_INFORMATION;

typedef struct _ALPC_SERVER_SESSION_INFORMATION
{
    ULONG SessionId;
    ULONG ProcessId;
} ALPC_SERVER_SESSION_INFORMATION, *PALPC_SERVER_SESSION_INFORMATION;

typedef enum _ALPC_MESSAGE_INFORMATION_CLASS
{
    AlpcMessageSidInformation,
    AlpcMessageTokenModifiedIdInformation,
    AlpcMessageDirectStatusInformation,
    AlpcMessageHandleInformation,
    MaxAlpcMessageInfoClass
} ALPC_MESSAGE_INFORMATION_CLASS;

typedef struct _ALPC_PORT_ATTRIBUTES
{
    ULONG Flags;
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
    SIZE_T MaxMessageLength;
    SIZE_T MemoryBandwidth;
    SIZE_T MaxPoolUsage;
    SIZE_T MaxSectionSize;
    SIZE_T MaxViewSize;
    SIZE_T MaxTotalSectionSize;
    ULONG DupObjectTypes;
#ifdef _WIN64
    ULONG Reserved;
#endif
} ALPC_PORT_ATTRIBUTES, *PALPC_PORT_ATTRIBUTES;

#endif /* _REACTOS_ALPC_H_ */
