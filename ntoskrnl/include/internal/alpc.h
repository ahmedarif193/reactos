/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Internal Advanced Local Procedure Call definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#pragma once

#define ALPC_PORT_TYPE_MASK                 0x00000003
#define ALPC_PORT_TYPE_CONNECTION           0x00000001
#define ALPC_PORT_TYPE_SERVER               0x00000002
#define ALPC_PORT_TYPE_CLIENT               0x00000003
#define ALPC_PORT_FLAG_LPC                  0x00000010
#define ALPC_PORT_FLAG_WAITABLE             0x00000020
#define ALPC_PORT_FLAG_NAME_DELETED         0x00000040
#define ALPC_PORT_FLAG_CLOSED               0x00000080
#define ALPC_PORT_FLAG_DISCONNECTED         0x00000100
#define ALPC_PORT_FLAG_DYNAMIC_SECURITY     0x00000200
#define ALPC_PORT_FLAG_NO_FLUSH_ON_CLOSE    0x00002000
#define ALPC_PORT_FLAG_ALLOW_LPC            0x00004000
#define ALPC_PORT_FLAG_HAS_COMPLETION_LIST  0x00008000
#define ALPC_PORT_FLAG_COMPLETION_RUNDOWN   0x00010000

#define ALPC_MSG_STATE_QUEUED               0x00000001
#define ALPC_MSG_STATE_PENDING              0x00000002
#define ALPC_MSG_STATE_REPLIED              0x00000004
#define ALPC_MSG_STATE_CANCELED             0x00000008
#define ALPC_MSG_STATE_SYNC                 0x00000010
#define ALPC_MSG_STATE_LPC_MODE             0x00000020
#define ALPC_MSG_STATE_KERNEL               0x00000040
#define ALPC_MSG_STATE_CONNECTION           0x00000080
#define ALPC_MSG_STATE_REFUSED              0x00000100
#define ALPC_MSG_STATE_ACCEPTED             0x00000200
#define ALPC_MSG_STATE_DISCONNECTED         0x00000400
#define ALPC_MSG_STATE_DATA_INFO            0x00000800
#define ALPC_MSG_STATE_ASYNC_REPLY          0x00001000
#define ALPC_MSG_STATE_ACCEPT_IN_PROGRESS   0x00002000
#define ALPC_MSG_STATE_REPLY_IN_PROGRESS    0x00004000
#define ALPC_MSGFLG_LPC_MODE_INTERNAL       0x00000002
#define ALPC_MSG_STATE_IMPERSONATING        0x00008000
#define ALPC_MSG_STATE_CALLBACK             0x00020000
#define ALPC_MSG_STATE_SENDER_DISCONNECTED  0x00040000
#define ALPC_MSG_STATE_IN_CANCELED_QUEUE     0x00080000

#define ALPC_SMALL_MESSAGE_SIZE             1024
#define ALPC_MAX_PORT_MESSAGE_LENGTH        0x10000
#define ALPC_MAX_RESERVE_MESSAGE_LENGTH     (ALPC_MAX_PORT_MESSAGE_LENGTH - sizeof(PORT_MESSAGE) - 1)
#define ALPC_RESOURCE_RESERVE_ID_BIT         0x80000000UL
#define ALPC_SECURITY_CONTEXT_ID_BIAS        0x10UL

typedef struct _ALPC_PORT ALPC_PORT, *PALPC_PORT;
typedef struct _KALPC_MESSAGE KALPC_MESSAGE, *PKALPC_MESSAGE;
typedef struct _KALPC_COMPLETION_LIST KALPC_COMPLETION_LIST, *PKALPC_COMPLETION_LIST;

#define ALPC_COMPLETION_LIST_EMPTY          0x00FFFFFFULL
#define ALPC_COMPLETION_LIST_INDEX_MASK     0x00FFFFFFULL
#define ALPC_COMPLETION_LIST_TAIL_SHIFT     24
#define ALPC_COMPLETION_LIST_ACTIVE_SHIFT   48
#define ALPC_COMPLETION_LIST_GRANULARITY    64
#define ALPC_COMPLETION_LIST_START_MAGIC    0xDEADBEEFBAADF00DULL
#define ALPC_COMPLETION_LIST_END_MAGIC      0xBAADF00DDEADBEEFULL

typedef struct _KALPC_COMPLETION_LIST_HEADER
{
    ULONGLONG StartMagic;
    ULONG TotalSize;
    ULONG ListOffset;
    ULONG ListSize;
    ULONG BitmapOffset;
    ULONG BitmapSize;
    ULONG DataOffset;
    ULONG DataSize;
    ULONG AttributeFlags;
    ULONG AttributeSize;
    UCHAR Reserved1[84];
    volatile LONGLONG State;
    ULONG LastMessageId;
    ULONG LastCallbackId;
    UCHAR Reserved2[112];
    volatile LONG PostCount;
    UCHAR Reserved3[124];
    volatile LONG ReturnCount;
    UCHAR Reserved4[124];
    volatile LONG LogSequenceNumber;
    UCHAR Reserved5[124];
    PVOID UserLock;
#ifndef _WIN64
    ULONG UserLockPadding;
#endif
    ULONGLONG EndMagic;
    UCHAR Reserved6[112];
} KALPC_COMPLETION_LIST_HEADER, *PKALPC_COMPLETION_LIST_HEADER;

C_ASSERT(FIELD_OFFSET(KALPC_COMPLETION_LIST_HEADER, State) == 0x80);
C_ASSERT(FIELD_OFFSET(KALPC_COMPLETION_LIST_HEADER, PostCount) == 0x100);
C_ASSERT(FIELD_OFFSET(KALPC_COMPLETION_LIST_HEADER, ReturnCount) == 0x180);
C_ASSERT(FIELD_OFFSET(KALPC_COMPLETION_LIST_HEADER, UserLock) == 0x280);
C_ASSERT(sizeof(KALPC_COMPLETION_LIST_HEADER) == 0x300);

struct _KALPC_COMPLETION_LIST
{
    PEPROCESS OwnerProcess;
    PMDL Mdl;
    PVOID UserVa;
    PVOID UserLimit;
    PVOID SystemVa;
    ULONG TotalSize;
    PKALPC_COMPLETION_LIST_HEADER Header;
    PULONG List;
    ULONG ListSize;
    volatile LONG *Bitmap;
    ULONG BitmapSize;
    PUCHAR Data;
    ULONG DataSize;
    ULONG BitmapLimit;
    ULONG ConcurrencyCount;
    ULONG AttributeFlags;
    ULONG AttributeSize;
};

typedef struct _ALPC_COMMUNICATION_INFO
{
    PALPC_PORT ConnectionPort;
    PALPC_PORT ServerCommunicationPort;
    PALPC_PORT ClientCommunicationPort;
    LIST_ENTRY CommunicationList;
    PKALPC_MESSAGE CloseMessage;
    LONG ReferenceCount;
} ALPC_COMMUNICATION_INFO, *PALPC_COMMUNICATION_INFO;

typedef struct _KALPC_SECTION
{
    LIST_ENTRY Entry;
    ULONG Handle;
    ULONG Flags;
    PVOID SectionObject;
    SIZE_T Size;
    LONG ViewCount;
    LONG ReferenceCount;
    PALPC_PORT OwnerPort;
} KALPC_SECTION, *PKALPC_SECTION;

typedef struct _KALPC_VIEW
{
    LIST_ENTRY Entry;
    ULONG Flags;
    PKALPC_SECTION Section;
    PALPC_PORT OwnerPort;
    PEPROCESS Process;
    PVOID Address;
    SIZE_T Size;
    LONG ReferenceCount;
    BOOLEAN Secure;
    BOOLEAN DeletePending;
} KALPC_VIEW, *PKALPC_VIEW;

typedef struct _KALPC_SECURITY_DATA
{
    LIST_ENTRY Entry;
    ULONG Handle;
    ULONG Flags;
    PALPC_PORT OwnerPort;
    SECURITY_CLIENT_CONTEXT ClientContext;
    BOOLEAN Revoked;
    LONG ReferenceCount;
} KALPC_SECURITY_DATA, *PKALPC_SECURITY_DATA;

typedef struct _KALPC_RESERVE
{
    LIST_ENTRY Entry;
    ULONG Handle;
    SIZE_T Size;
    PALPC_PORT OwnerPort;
    PKALPC_MESSAGE Message;
} KALPC_RESERVE, *PKALPC_RESERVE;

typedef struct _KALPC_HANDLE_DATA
{
    ULONG Flags;
    ULONG Count;
    struct
    {
        PVOID Object;
        ULONG Index;
        ULONG ObjectType;
        ACCESS_MASK DesiredAccess;
        ULONG Flags;
        ULONG HandleAttributes;
    } Entries[1];
} KALPC_HANDLE_DATA, *PKALPC_HANDLE_DATA;

typedef struct _KALPC_MESSAGE_ATTRIBUTES
{
    PVOID ClientContext;
    PVOID ServerContext;
    PVOID PortContext;
    PVOID CancelPortContext;
    PKALPC_SECURITY_DATA SecurityData;
    PKALPC_VIEW View;
    ULONG ViewFlags;
    PKALPC_HANDLE_DATA HandleData;
    LUID TokenId;
    LUID AuthenticationId;
    LUID ModifiedId;
    ULONG_PTR DirectEvent;
    ULONGLONG WorkOnBehalfTicket;
    ULONG ValidAttributes;
} KALPC_MESSAGE_ATTRIBUTES, *PKALPC_MESSAGE_ATTRIBUTES;

typedef struct _KALPC_CONNECTION_DATA
{
    PALPC_PORT ClientPort;
    PVOID SectionToMap;
    PORT_VIEW ClientView;
    REMOTE_PORT_VIEW ServerView;
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
} KALPC_CONNECTION_DATA, *PKALPC_CONNECTION_DATA;

struct _KALPC_MESSAGE
{
    LIST_ENTRY Entry;
    LIST_ENTRY CanceledEntry;
    LIST_ENTRY DeferredFreeEntry;
    PALPC_PORT QueuePort;
    PALPC_PORT CancelQueuePort;
    PALPC_PORT OwnerPort;
    PALPC_PORT SenderPort;
    PEPROCESS SenderProcess;
    PALPC_PORT ReplyPort;
    PETHREAD WaitingThread;
    PETHREAD ServerThread;
    PKALPC_MESSAGE CallbackParent;
    PKALPC_MESSAGE ActiveCallback;
    PALPC_COMMUNICATION_INFO CommunicationInfo;
    PVOID PortContext;
    ULONG State;
    ULONG Sequence;
    ULONG AllocatedLength;
    NTSTATUS CompletionStatus;
    KALPC_MESSAGE_ATTRIBUTES Attributes;
    KALPC_CONNECTION_DATA Connection;
    PORT_MESSAGE PortMessage;
};

struct _ALPC_PORT
{
    KEVENT WaitEvent;
    LIST_ENTRY PortListEntry;
    PALPC_COMMUNICATION_INFO CommunicationInfo;
    PEPROCESS OwnerProcess;
    PVOID CompletionPort;
    PVOID CompletionKey;
    PKALPC_COMPLETION_LIST CompletionList;
    PVOID CallbackObject;
    PVOID MessageZoneBuffer;
    ULONG MessageZoneSize;
    PVOID PortContext;
    SECURITY_CLIENT_CONTEXT StaticSecurity;
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
    LIST_ENTRY MainQueue;
    LIST_ENTRY PendingQueue;
    LIST_ENTRY CanceledQueue;
    LIST_ENTRY Waiters;
    LIST_ENTRY CommunicationPorts;
    LIST_ENTRY SectionList;
    LIST_ENTRY ViewList;
    LIST_ENTRY SecurityList;
    LIST_ENTRY ReserveList;
    ALPC_PORT_ATTRIBUTES PortAttributes;
    CLIENT_ID Creator;
    PVOID ClientSectionBase;
    PVOID ServerSectionBase;
    ULONG Flags;
    ULONG SequenceNo;
    ULONG MaxMessageLength;
    ULONG MaxConnectionInfoLength;
    ULONG NextResourceHandle;
    ULONG MainQueueLength;
    ULONG PendingQueueLength;
    ULONG CanceledQueueLength;
    ULONG Waiting;
    ULONG ReferenceCount;
    ULONG ReferenceWaitTarget;
    PKEVENT ReferenceWaiter;
};

extern POBJECT_TYPE AlpcPortObjectType;
extern KGUARDED_MUTEX AlpcpLock;
extern ULONG AlpcpNextMessageId;
extern ULONG AlpcpNextCallbackId;
extern LIST_ENTRY AlpcpPortList;

#define AlpcpAcquireLock()   KeAcquireGuardedMutex(&AlpcpLock)

VOID
NTAPI
AlpcpReleaseLock(VOID);

#define AlpcpPortType(Port)  ((Port)->Flags & ALPC_PORT_TYPE_MASK)

CODE_SEG("INIT")
BOOLEAN
NTAPI
LpcInitSystem(VOID);

VOID
NTAPI
LpcExitThread(
    _In_ PETHREAD Thread);

VOID
NTAPI
AlpcpClosePort(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount);

VOID
NTAPI
AlpcpDeletePort(
    _In_ PVOID ObjectBody);

NTSTATUS
NTAPI
AlpcpCreatePort(
    _Out_ PALPC_PORT *NewPort,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PALPC_PORT_ATTRIBUTES PortAttributes,
    _In_ ULONG Type,
    _In_ ULONG Flags,
    _In_ ULONG MaxConnectionInfoLength,
    _In_ ULONG MaxMessageLength);

NTSTATUS
NTAPI
AlpcCreateSecurityContext(
    _In_ HANDLE PortHandle,
    _In_ PETHREAD Thread,
    _Reserved_ ULONG Flags,
    _Inout_ PALPC_SECURITY_ATTR SecurityAttribute);

PKALPC_MESSAGE
NTAPI
AlpcpAllocateMessage(
    _In_ ULONG Capacity,
    _In_ PALPC_PORT Port);

VOID
NTAPI
AlpcpSetMessageSenderPort(
    _Inout_ PKALPC_MESSAGE Message,
    _In_ PALPC_PORT Port);

VOID
NTAPI
AlpcpFreeMessage(
    _In_ PKALPC_MESSAGE Message);

VOID
NTAPI
AlpcpCopyMessage(
    _Out_ PPORT_MESSAGE Destination,
    _In_ PPORT_MESSAGE Origin,
    _In_ PVOID Data,
    _In_ ULONG MessageType,
    _In_opt_ PCLIENT_ID ClientId);

NTSTATUS
NTAPI
AlpcpReferencePortByHandle(
    _In_ HANDLE PortHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PALPC_PORT *Port);

NTSTATUS
NTAPI
AlpcpSendWaitReceivePort(
    _In_ PALPC_PORT Port,
    _In_ ULONG Flags,
    _In_opt_ PPORT_MESSAGE SendMessage,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES SendMessageAttributes,
    _Out_opt_ PPORT_MESSAGE ReceiveMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES ReceiveMessageAttributes,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ KPROCESSOR_MODE PreviousMode);

PALPC_PORT
NTAPI
AlpcpGetQueuePort(
    _In_ PALPC_PORT Port,
    _Out_opt_ PVOID *PortContext);

VOID
NTAPI
AlpcpSignalWaiter(
    _In_ PETHREAD Thread);

PALPC_PORT
NTAPI
AlpcpGetReceivePort(
    _In_ PALPC_PORT Port);

VOID
NTAPI
AlpcpWakeWaiter(
    _In_ PALPC_PORT Port);

VOID
NTAPI
AlpcpQueueMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message);

BOOLEAN
NTAPI
AlpcpQueueCompletionListMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message);

NTSTATUS
NTAPI
AlpcpRegisterCompletionList(
    _In_ PALPC_PORT Port,
    _In_ PALPC_PORT_COMPLETION_LIST_INFORMATION Information,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
AlpcpAdjustCompletionListConcurrencyCount(
    _In_ PALPC_PORT Port,
    _In_ ULONG ConcurrencyCount);

NTSTATUS
NTAPI
AlpcpRundownCompletionList(
    _In_ PALPC_PORT Port);

NTSTATUS
NTAPI
AlpcpUnregisterCompletionList(
    _In_ PALPC_PORT Port,
    _In_ BOOLEAN Force);

VOID
NTAPI
AlpcpFreeCompletionList(
    _In_ PKALPC_COMPLETION_LIST CompletionList);

NTSTATUS
NTAPI
AlpcpWaitForPortReferences(
    _In_ PALPC_PORT Port,
    _In_ ULONG ReferenceCount);

VOID
NTAPI
AlpcpSignalPortReferenceWaiter(
    _In_ PALPC_PORT Port);

VOID
NTAPI
AlpcpTrackPortReferences(
    _In_ PALPC_PORT Port);

NTSTATUS
NTAPI
AlpcpWaitForMessage(
    _In_ PALPC_PORT Port,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_ PKALPC_MESSAGE *Message);

NTSTATUS
NTAPI
AlpcpWaitForReply(
    _In_ PKALPC_MESSAGE Message,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_opt_ PKALPC_MESSAGE *ReceivedMessage);

VOID
NTAPI
AlpcpDetachWaiter(
    _In_ PKALPC_MESSAGE Message);

PKALPC_MESSAGE
NTAPI
AlpcpFindPendingMessage(
    _In_ PALPC_PORT Port,
    _In_ ULONG MessageId,
    _In_opt_ PCLIENT_ID ClientId);

PKALPC_MESSAGE
NTAPI
AlpcpFindPendingMessageForReply(
    _In_ PALPC_PORT Port,
    _In_ ULONG MessageId,
    _In_opt_ PCLIENT_ID ClientId);

PKALPC_MESSAGE
NTAPI
AlpcpFindCanceledMessage(
    _In_ PALPC_PORT Port,
    _In_ ULONG MessageId,
    _In_ ULONG CallbackId);

VOID
NTAPI
AlpcpRemovePending(
    _In_ PKALPC_MESSAGE Message);

VOID
NTAPI
AlpcpMakePending(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message);

VOID
NTAPI
AlpcpCompleteReply(
    _In_ PKALPC_MESSAGE Message);

VOID
NTAPI
AlpcpCompleteWithStatus(
    _In_ PKALPC_MESSAGE Message,
    _In_ ULONG StateBits,
    _In_ NTSTATUS Status);

VOID
NTAPI
AlpcpRundownQueues(
    _In_ PALPC_PORT Port);

VOID
NTAPI
AlpcpSendPortClosed(
    _In_ PALPC_PORT Port);

VOID
NTAPI
AlpcpDereferenceCommunicationInfo(
    _In_ PALPC_COMMUNICATION_INFO Info);

NTSTATUS
NTAPI
AlpcpDisconnectPort(
    _In_ PALPC_PORT Port,
    _In_ BOOLEAN Flush);

NTSTATUS
NTAPI
AlpcpCheckServerSid(
    _In_ PALPC_PORT Port,
    _In_ PSID ServerSid);

ULONG
NTAPI
AlpcpAttributesSize(
    _In_ ULONG AllocatedAttributes);

NTSTATUS
NTAPI
AlpcpCaptureAttributes(
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES UserAttributes,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PALPC_MESSAGE_ATTRIBUTES *Captured);

NTSTATUS
NTAPI
AlpcpWriteAttributes(
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES UserAttributes,
    _In_ PALPC_MESSAGE_ATTRIBUTES Captured,
    _In_ KPROCESSOR_MODE PreviousMode);

PKALPC_SECTION
NTAPI
AlpcpLookupSection(
    _In_ PALPC_PORT Port,
    _In_ ULONG Handle);

PKALPC_VIEW
NTAPI
AlpcpLookupView(
    _In_ PALPC_PORT Port,
    _In_ PVOID Address);

PKALPC_SECURITY_DATA
NTAPI
AlpcpLookupSecurityData(
    _In_ PALPC_PORT Port,
    _In_ ULONG Handle);

VOID
NTAPI
AlpcpDereferenceSection(
    _In_ PKALPC_SECTION Section);

VOID
NTAPI
AlpcpDereferenceView(
    _In_ PKALPC_VIEW View);

VOID
NTAPI
AlpcpDeleteView(
    _In_ PKALPC_VIEW View);

VOID
NTAPI
AlpcpDereferenceSecurityData(
    _In_ PKALPC_SECURITY_DATA Data);

VOID
NTAPI
AlpcpReleaseMessageAttributes(
    _In_ PKALPC_MESSAGE Message);

VOID
NTAPI
AlpcpRundownResources(
    _In_ PALPC_PORT Port);

NTSTATUS
NTAPI
AlpcpCaptureSendAttributes(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES Attributes,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
AlpcpExposeReceiveAttributes(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES Attributes,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES UserAttributes,
    _In_ KPROCESSOR_MODE PreviousMode);

NTSTATUS
NTAPI
AlpcpImpersonateMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_ ULONG Flags);
