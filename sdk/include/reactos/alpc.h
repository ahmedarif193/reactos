/*
 * Advanced Local Procedure Call definitions
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef _REACTOS_ALPC_H_
#define _REACTOS_ALPC_H_

/* Connection flags */
#define ALPC_SYNC_CONNECTION                    0x00020000

/* Port attribute flags */
#define ALPC_PORTFLG_ALLOWIMPERSONATION         0x00010000
#define ALPC_PORTFLG_NON_BLOCKING_RECEIVE       0x00040000
#define ALPC_PORTFLG_ALLOW_DUP_OBJECT           0x00080000

/* Message flags */
#define ALPC_MSGFLG_NONE                        0x00000000
#define ALPC_MSGFLG_REPLY_MESSAGE               0x00010000
#define ALPC_MSGFLG_SYNC_REQUEST                0x00020000

/* Message attribute flags */
#define ALPC_MESSAGE_SECURITY_ATTRIBUTE         0x80000000
#define ALPC_MESSAGE_VIEW_ATTRIBUTE             0x40000000
#define ALPC_MESSAGE_CONTEXT_ATTRIBUTE          0x20000000
#define ALPC_MESSAGE_HANDLE_ATTRIBUTE           0x10000000
#define ALPC_MESSAGE_TOKEN_ATTRIBUTE            0x08000000
#define ALPC_MESSAGE_DIRECT_ATTRIBUTE           0x04000000
#define ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE   0x02000000
#define ALPC_MESSAGE_ATTRIBUTE_ALL              0xfe000000

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
    HANDLE ContextHandle;
} ALPC_SECURITY_ATTR, *PALPC_SECURITY_ATTR;

typedef struct _ALPC_VIEW_ATTR
{
    ULONG Flags;
    HANDLE SectionHandle;
    void *ViewBase;
    SIZE_T ViewSize;
} ALPC_VIEW_ATTR, *PALPC_VIEW_ATTR;

typedef struct _ALPC_CONTEXT_ATTR
{
    void *PortContext;
    void *MessageContext;
    ULONG Sequence;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_CONTEXT_ATTR, *PALPC_CONTEXT_ATTR;

typedef struct _ALPC_HANDLE_ATTR
{
    ULONG Flags;
    HANDLE Handle;
    ULONG ObjectType;
    ACCESS_MASK DesiredAccess;
} ALPC_HANDLE_ATTR, *PALPC_HANDLE_ATTR;

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

typedef struct _ALPC_BASIC_INFORMATION
{
    ULONG Flags;
    ULONG SequenceNo;
    PVOID PortContext;
} ALPC_BASIC_INFORMATION, *PALPC_BASIC_INFORMATION;

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
