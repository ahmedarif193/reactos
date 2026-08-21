/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         End-to-end ALPC message attribute and sender identity tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

#define ALPC_ATTRIBUTE_TEST_COOKIE 0x41545452
#define ALPC_ATTRIBUTE_TEST_CONTEXT ((PVOID)(ULONG_PTR)0x4154545243545801ULL)

static BOOLEAN AlpcAttributeQueueHealthy;

static
PALPC_MESSAGE_ATTRIBUTES
AlpcTestInitializeAttributes(
    _Out_writes_bytes_(BufferSize) UCHAR *Buffer,
    _In_ SIZE_T BufferSize,
    _In_ ULONG Flags)
{
    PALPC_MESSAGE_ATTRIBUTES Attributes = (PALPC_MESSAGE_ATTRIBUTES)Buffer;
    SIZE_T RequiredSize = 0;
    NTSTATUS Status;

    RtlFillMemory(Buffer, BufferSize, 0x55);
    Status = AlpcInitializeMessageAttribute(Flags, Attributes, BufferSize, &RequiredSize);
    ok_hex(Status, STATUS_SUCCESS);
    ok(RequiredSize <= BufferSize, "attribute size %Iu exceeds buffer %Iu\n", RequiredSize, BufferSize);
    return NT_SUCCESS(Status) ? Attributes : NULL;
}

static
NTSTATUS
AlpcTestSendRequest(
    _In_ HANDLE ClientPort,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES SendAttributes,
    _In_ ULONG Value)
{
    ALPC_TEST_MESSAGE Message;

    AlpcTestInitializeMessage(&Message, ALPC_ATTRIBUTE_TEST_COOKIE, Value);
    return NtAlpcSendWaitReceivePort(ClientPort, 0, &Message.Header, SendAttributes, NULL, NULL, NULL, NULL);
}

static
NTSTATUS
AlpcTestReceiveRequest(
    _In_ HANDLE ConnectionPort,
    _Out_ PALPC_TEST_MESSAGE Message,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes)
{
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    SIZE_T MessageLength = sizeof(*Message);

    RtlZeroMemory(Message, sizeof(*Message));
    return NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Message->Header, &MessageLength, ReceiveAttributes, &Timeout);
}

static
NTSTATUS
AlpcTestReplyRequest(
    _In_ HANDLE ServerPort,
    _Inout_ PALPC_TEST_MESSAGE Message)
{
    Message->Value++;
    return NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &Message->Header, NULL, NULL, NULL, NULL, NULL);
}

static
BOOLEAN
AlpcTestDrainUnexpectedRequest(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ ULONG ExpectedValue,
    _In_ PCSTR Label)
{
    UCHAR ReceiveBuffer[1024];
    ALPC_MESSAGE_HANDLE_INFORMATION Information[512];
    PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes;
    PALPC_HANDLE_ATTR HandleAttribute;
    ALPC_TEST_MESSAGE Message;
    LARGE_INTEGER Timeout;
    SIZE_T MessageLength;
    NTSTATUS Status;
    HANDLE ReceivedHandle;
    DWORD HandleFlags;
    ULONG Index;
    ULONG Count;
    BOOLEAN ExpectedMessage;

    ReceiveAttributes = AlpcTestInitializeAttributes(ReceiveBuffer, sizeof(ReceiveBuffer), ALPC_MESSAGE_HANDLE_ATTRIBUTE | ALPC_MESSAGE_CONTEXT_ATTRIBUTE | ALPC_MESSAGE_TOKEN_ATTRIBUTE | ALPC_MESSAGE_DIRECT_ATTRIBUTE | ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE | ALPC_MESSAGE_VIEW_ATTRIBUTE | ALPC_MESSAGE_SECURITY_ATTRIBUTE);
    if (!ReceiveAttributes)
    {
        AlpcAttributeQueueHealthy = FALSE;
        return FALSE;
    }
    RtlZeroMemory(Information, sizeof(Information));
    HandleAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    ok(HandleAttribute != NULL, "%s drain omitted the allocated handle attribute\n", Label);
    if (!HandleAttribute)
    {
        AlpcAttributeQueueHealthy = FALSE;
        return FALSE;
    }
    HandleAttribute->Flags = ALPC_HANDLEFLG_INDIRECT;
    HandleAttribute->HandleAttrArray = Information;
    HandleAttribute->HandleCount = RTL_NUMBER_OF(Information);

    RtlZeroMemory(&Message, sizeof(Message));
    MessageLength = sizeof(Message);
    Timeout = AlpcTestRelativeTimeout(1000);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Message.Header, &MessageLength, ReceiveAttributes, &Timeout);
    trace("ALPC_OBSERVE status %s.drain=%08lx cookie=%08lx value=%lu valid_attributes=%08lx\n", Label, Status, Message.Cookie, Message.Value, ReceiveAttributes->ValidAttributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        AlpcAttributeQueueHealthy = FALSE;
        return FALSE;
    }

    ok_eq_ulong(Message.Cookie, ALPC_ATTRIBUTE_TEST_COOKIE);
    ok_eq_ulong(Message.Value, ExpectedValue);
    ExpectedMessage = Message.Cookie == ALPC_ATTRIBUTE_TEST_COOKIE && Message.Value == ExpectedValue;
    if (ReceiveAttributes->ValidAttributes & ALPC_MESSAGE_HANDLE_ATTRIBUTE)
    {
        HandleAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
        if (HandleAttribute->Flags & ALPC_HANDLEFLG_INDIRECT)
        {
            Count = HandleAttribute->HandleCount;
            if (Count > RTL_NUMBER_OF(Information))
                Count = RTL_NUMBER_OF(Information);
            for (Index = 0; Index < Count; ++Index)
            {
                ReceivedHandle = UlongToHandle(Information[Index].Handle);
                if (ReceivedHandle && GetHandleInformation(ReceivedHandle, &HandleFlags))
                    NtClose(ReceivedHandle);
            }
        }
        else
        {
            ReceivedHandle = HandleAttribute->Handle;
            if (ReceivedHandle && GetHandleInformation(ReceivedHandle, &HandleFlags))
                NtClose(ReceivedHandle);
        }
    }
    Status = AlpcTestReplyRequest(ServerPort, &Message);
    alpc_expect_status("Attributes.drain_reply", Status, STATUS_SUCCESS);
    if (!ExpectedMessage || !NT_SUCCESS(Status))
    {
        AlpcAttributeQueueHealthy = FALSE;
        return FALSE;
    }
    return TRUE;
}

static
VOID
AlpcTestQuerySender(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ PALPC_TEST_MESSAGE Message)
{
    UCHAR SidBuffer[SECURITY_MAX_SID_SIZE];
    ALPC_MESSAGE_HANDLE_INFORMATION HandleInformation;
    OBJECT_ATTRIBUTES ObjectAttributes;
    LUID ModifiedId;
    NTSTATUS DirectStatus;
    ULONG ReturnLength;
    NTSTATUS Status;
    HANDLE ProcessHandle = NULL;
    HANDLE ThreadHandle = NULL;

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);

    ReturnLength = 0x55555555;
    RtlFillMemory(SidBuffer, sizeof(SidBuffer), 0x55);
    alpc_observe_status("QueryMessage.sid", NtAlpcQueryInformationMessage(ConnectionPort, &Message->Header, AlpcMessageSidInformation, SidBuffer, sizeof(SidBuffer), &ReturnLength));
    trace("ALPC_OBSERVE value QueryMessage.sid_length=%lu sid_valid=%u\n", ReturnLength, NT_SUCCESS(Status) ? RtlValidSid((PSID)SidBuffer) : FALSE);

    ReturnLength = 0x55555555;
    RtlFillMemory(&ModifiedId, sizeof(ModifiedId), 0x55);
    alpc_observe_status("QueryMessage.modified_id", NtAlpcQueryInformationMessage(ConnectionPort, &Message->Header, AlpcMessageTokenModifiedIdInformation, &ModifiedId, sizeof(ModifiedId), &ReturnLength));
    trace("ALPC_OBSERVE value QueryMessage.modified_id=%08lx:%08lx length=%lu\n", ModifiedId.HighPart, ModifiedId.LowPart, ReturnLength);

    alpc_observe_status("QueryMessage.direct_status", NtAlpcQueryInformationMessage(ConnectionPort, &Message->Header, AlpcMessageDirectStatusInformation, NULL, 0, NULL));
    trace("ALPC_OBSERVE value QueryMessage.direct_status_return=%08lx\n", Status);
    ReturnLength = 0x55555555;
    DirectStatus = (NTSTATUS)0x55555555;
    alpc_observe_status("QueryMessage.direct_status_invalid_output", NtAlpcQueryInformationMessage(ConnectionPort, &Message->Header, AlpcMessageDirectStatusInformation, &DirectStatus, sizeof(DirectStatus), &ReturnLength));
    trace("ALPC_OBSERVE value QueryMessage.direct_status_invalid_output=%08lx length=%lu\n", DirectStatus, ReturnLength);

    RtlFillMemory(&HandleInformation, sizeof(HandleInformation), 0x55);
    HandleInformation.Index = 0;
    ReturnLength = 0x55555555;
    alpc_observe_status("QueryMessage.handle_absent", NtAlpcQueryInformationMessage(ConnectionPort, &Message->Header, AlpcMessageHandleInformation, &HandleInformation, sizeof(HandleInformation), &ReturnLength));
    trace("ALPC_OBSERVE value QueryMessage.handle_absent index=%lu flags=%08lx handle=%08lx type=%08lx access=%08lx length=%lu\n", HandleInformation.Index, HandleInformation.Flags, HandleInformation.Handle, HandleInformation.ObjectType, HandleInformation.GrantedAccess, ReturnLength);

    alpc_observe_status("OpenSenderProcess.valid", NtAlpcOpenSenderProcess(&ProcessHandle, ServerPort, &Message->Header, 0, PROCESS_QUERY_LIMITED_INFORMATION, &ObjectAttributes));
    trace("ALPC_OBSERVE value OpenSenderProcess.handle=%p pid=%lu expected_pid=%lu\n", ProcessHandle, ProcessHandle ? GetProcessId(ProcessHandle) : 0, GetCurrentProcessId());
    if (ProcessHandle)
        NtClose(ProcessHandle);

    alpc_observe_status("OpenSenderThread.valid", NtAlpcOpenSenderThread(&ThreadHandle, ServerPort, &Message->Header, 0, THREAD_QUERY_LIMITED_INFORMATION, &ObjectAttributes));
    trace("ALPC_OBSERVE value OpenSenderThread.handle=%p tid=%lu message_tid=%Iu\n", ThreadHandle, ThreadHandle ? GetThreadId(ThreadHandle) : 0, (ULONG_PTR)Message->Header.ClientId.UniqueThread);
    if (ThreadHandle)
        NtClose(ThreadHandle);

    alpc_observe_status("ImpersonateClient.valid", NtAlpcImpersonateClientOfPort(ServerPort, &Message->Header, NULL));
    if (NT_SUCCESS(Status))
        ok(RevertToSelf(), "RevertToSelf after client impersonation failed: %lu\n", GetLastError());

    alpc_observe_status("ImpersonateContainer.valid_message", NtAlpcImpersonateClientContainerOfPort(ServerPort, &Message->Header, 0));
    if (NT_SUCCESS(Status))
        ok(RevertToSelf(), "RevertToSelf after container impersonation failed: %lu\n", GetLastError());
}

static
VOID
AlpcTestBasicAttributes(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ HANDLE ClientPort)
{
    UCHAR SendBuffer[256];
    UCHAR ReceiveBuffer[256];
    UCHAR ReceiveBefore[256];
    PALPC_MESSAGE_ATTRIBUTES SendAttributes;
    PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes;
    PALPC_CONTEXT_ATTR ContextAttribute;
    PALPC_DIRECT_ATTR DirectAttribute;
    PALPC_WORK_ON_BEHALF_ATTR WorkAttribute;
    PALPC_TOKEN_ATTR TokenAttribute;
    ALPC_TEST_MESSAGE Message;
    ULONG Flags = ALPC_MESSAGE_CONTEXT_ATTRIBUTE | ALPC_MESSAGE_TOKEN_ATTRIBUTE | ALPC_MESSAGE_DIRECT_ATTRIBUTE | ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE;
    NTSTATUS Status;
    HANDLE Event;
    DWORD WaitStatus;

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Event)
        return;

    SendAttributes = AlpcTestInitializeAttributes(SendBuffer, sizeof(SendBuffer), Flags);
    ReceiveAttributes = AlpcTestInitializeAttributes(ReceiveBuffer, sizeof(ReceiveBuffer), Flags);
    if (!SendAttributes || !ReceiveAttributes)
    {
        CloseHandle(Event);
        return;
    }

    SendAttributes->ValidAttributes = Flags;
    ContextAttribute = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    ContextAttribute->PortContext = (PVOID)(ULONG_PTR)0x11111111;
    ContextAttribute->MessageContext = ALPC_ATTRIBUTE_TEST_CONTEXT;
    ContextAttribute->Sequence = 0x22222222;
    ContextAttribute->MessageId = 0x33333333;
    ContextAttribute->CallbackId = 0x44444444;
    DirectAttribute = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_DIRECT_ATTRIBUTE);
    DirectAttribute->Event = Event;
    WorkAttribute = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE);
    WorkAttribute->Ticket = 0x1122334455667788ULL;

    RtlCopyMemory(ReceiveBefore, ReceiveBuffer, sizeof(ReceiveBefore));
    alpc_expect_status("Attributes.send", AlpcTestSendRequest(ClientPort, SendAttributes, 1), STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    alpc_expect_status("Attributes.receive", AlpcTestReceiveRequest(ConnectionPort, &Message, ReceiveAttributes), STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, 1, "Attributes.receive_failure");
        goto Cleanup;
    }
    AlpcTestTraceBufferMutation("Attributes.receive", ReceiveBefore, ReceiveBuffer, sizeof(ReceiveBuffer));
    ok_eq_ulong(Message.Cookie, ALPC_ATTRIBUTE_TEST_COOKIE);
    ok_eq_ulong(Message.Value, 1);
    trace("ALPC_OBSERVE value Attributes.valid=%08lx allocated=%08lx\n", ReceiveAttributes->ValidAttributes, ReceiveAttributes->AllocatedAttributes);

    ContextAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    trace("ALPC_OBSERVE value Attributes.context port=%p message=%p sequence=%lu message_id=%lu callback_id=%lu\n", ContextAttribute->PortContext, ContextAttribute->MessageContext, ContextAttribute->Sequence, ContextAttribute->MessageId, ContextAttribute->CallbackId);
    if (ReceiveAttributes->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
    {
        ok(ContextAttribute->PortContext == ALPC_TEST_PORT_CONTEXT, "port context %p expected %p\n", ContextAttribute->PortContext, ALPC_TEST_PORT_CONTEXT);
        ok(ContextAttribute->MessageContext == NULL, "message context %p expected NULL\n", ContextAttribute->MessageContext);
    }

    TokenAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_TOKEN_ATTRIBUTE);
    trace("ALPC_OBSERVE value Attributes.token token=%I64x auth=%I64x modified=%I64x\n", TokenAttribute->TokenId, TokenAttribute->AuthenticationId, TokenAttribute->ModifiedId);
    WorkAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE);
    trace("ALPC_OBSERVE value Attributes.work_ticket=%I64x\n", WorkAttribute->Ticket);
    DirectAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_DIRECT_ATTRIBUTE);
    trace("ALPC_OBSERVE value Attributes.direct_event=%p\n", DirectAttribute->Event);

    AlpcTestQuerySender(ConnectionPort, ServerPort, &Message);
    alpc_expect_status("Attributes.reply", AlpcTestReplyRequest(ServerPort, &Message), STATUS_SUCCESS);
    alpc_observe_status("QueryMessage.direct_status_after_reply", NtAlpcQueryInformationMessage(ConnectionPort, &Message.Header, AlpcMessageDirectStatusInformation, NULL, 0, NULL));
    WaitStatus = WaitForSingleObject(Event, 0);
    trace("ALPC_OBSERVE value Attributes.direct_event_wait=%lu\n", WaitStatus);

Cleanup:
    CloseHandle(Event);
}

static
VOID
AlpcTestSingleHandle(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ HANDLE ClientPort)
{
    UCHAR SendBuffer[128];
    UCHAR ReceiveBuffer[128];
    UCHAR ReceiveBefore[128];
    PALPC_MESSAGE_ATTRIBUTES SendAttributes;
    PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes;
    PALPC_HANDLE_ATTR SendHandleAttribute;
    PALPC_HANDLE_ATTR ReceiveHandleAttribute;
    ALPC_MESSAGE_HANDLE_INFORMATION Information;
    ALPC_TEST_MESSAGE Message;
    ULONG ReturnLength;
    NTSTATUS Status;
    HANDLE Event;
    HANDLE ReceivedHandle = NULL;

    Event = CreateEventW(NULL, TRUE, TRUE, NULL);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Event)
        return;

    SendAttributes = AlpcTestInitializeAttributes(SendBuffer, sizeof(SendBuffer), ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    ReceiveAttributes = AlpcTestInitializeAttributes(ReceiveBuffer, sizeof(ReceiveBuffer), ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    if (!SendAttributes || !ReceiveAttributes)
        goto Cleanup;

    SendAttributes->ValidAttributes = ALPC_MESSAGE_HANDLE_ATTRIBUTE;
    SendHandleAttribute = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    SendHandleAttribute->Flags = ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS;
    SendHandleAttribute->Handle = Event;
    SendHandleAttribute->ObjectType = ALPC_PORFLG_OBJECT_TYPE_EVENT;
    SendHandleAttribute->DesiredAccess = 0;

    RtlCopyMemory(ReceiveBefore, ReceiveBuffer, sizeof(ReceiveBefore));
    alpc_expect_status("Handle.single_send", AlpcTestSendRequest(ClientPort, SendAttributes, 2), STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    alpc_expect_status("Handle.single_receive", AlpcTestReceiveRequest(ConnectionPort, &Message, ReceiveAttributes), STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, 2, "Handle.single_receive_failure");
        goto Cleanup;
    }
    AlpcTestTraceBufferMutation("Handle.single_receive", ReceiveBefore, ReceiveBuffer, sizeof(ReceiveBuffer));
    ok_eq_ulong(Message.Cookie, ALPC_ATTRIBUTE_TEST_COOKIE);
    ok_eq_ulong(Message.Value, 2);
    ReceiveHandleAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    trace("ALPC_OBSERVE value Handle.single valid=%08lx flags=%08lx handle=%p type=%08lx access=%08lx\n", ReceiveAttributes->ValidAttributes, ReceiveHandleAttribute->Flags, ReceiveHandleAttribute->Handle, ReceiveHandleAttribute->ObjectType, ReceiveHandleAttribute->DesiredAccess);
    if (ReceiveAttributes->ValidAttributes & ALPC_MESSAGE_HANDLE_ATTRIBUTE)
    {
        ok(ReceiveHandleAttribute->Handle == NULL, "single handle attribute exposed %p instead of an indexed handle\n", ReceiveHandleAttribute->Handle);
        ok_eq_ulong(ReceiveHandleAttribute->Flags, ALPC_HANDLEFLG_INDIRECT);
        ok_eq_ulong(ReceiveHandleAttribute->HandleCount, 1);
        ok_eq_ulong(ReceiveHandleAttribute->DesiredAccess, 0);
    }

    RtlFillMemory(&Information, sizeof(Information), 0x55);
    Information.Index = 0;
    ReturnLength = 0x55555555;
    alpc_expect_status("QueryMessage.handle_index_0", NtAlpcQueryInformationMessage(ConnectionPort, &Message.Header, AlpcMessageHandleInformation, &Information, sizeof(Information), &ReturnLength), STATUS_SUCCESS);
    trace("ALPC_OBSERVE value QueryMessage.handle_0 index=%lu flags=%08lx handle=%08lx type=%08lx access=%08lx length=%lu\n", Information.Index, Information.Flags, Information.Handle, Information.ObjectType, Information.GrantedAccess, ReturnLength);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong(Information.Index, 0);
        ok_eq_ulong(ReturnLength, sizeof(Information));
        ReceivedHandle = UlongToHandle(Information.Handle);
        ok(ReceivedHandle != NULL, "indexed handle query returned NULL\n");
        if (ReceivedHandle)
            ok_eq_ulong(WaitForSingleObject(ReceivedHandle, 0), WAIT_OBJECT_0);
    }

    RtlFillMemory(&Information, sizeof(Information), 0x55);
    Information.Index = 1;
    ReturnLength = 0x55555555;
    alpc_expect_status("QueryMessage.handle_index_1", NtAlpcQueryInformationMessage(ConnectionPort, &Message.Header, AlpcMessageHandleInformation, &Information, sizeof(Information), &ReturnLength), STATUS_INVALID_HANDLE);
    trace("ALPC_OBSERVE value QueryMessage.handle_1 index=%lu flags=%08lx handle=%08lx type=%08lx access=%08lx length=%lu\n", Information.Index, Information.Flags, Information.Handle, Information.ObjectType, Information.GrantedAccess, ReturnLength);

    alpc_expect_status("Handle.single_reply", AlpcTestReplyRequest(ServerPort, &Message), STATUS_SUCCESS);
    if (ReceivedHandle && ReceivedHandle != Event)
        NtClose(ReceivedHandle);

Cleanup:
    CloseHandle(Event);
}

static
NTSTATUS
AlpcTestSendIndirectHandles(
    _In_ HANDLE ClientPort,
    _In_ HANDLE Event,
    _In_ ULONG Count,
    _In_opt_ PALPC_HANDLE_ATTR32 Entries)
{
    UCHAR SendBuffer[128];
    PALPC_MESSAGE_ATTRIBUTES SendAttributes;
    PALPC_HANDLE_ATTR HandleAttribute;

    SendAttributes = AlpcTestInitializeAttributes(SendBuffer, sizeof(SendBuffer), ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    if (!SendAttributes)
        return STATUS_UNSUCCESSFUL;

    SendAttributes->ValidAttributes = ALPC_MESSAGE_HANDLE_ATTRIBUTE;
    HandleAttribute = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    HandleAttribute->Flags = ALPC_HANDLEFLG_INDIRECT;
    HandleAttribute->HandleAttrArray = (PALPC_MESSAGE_HANDLE_INFORMATION)Entries;
    HandleAttribute->HandleCount = Count;
    HandleAttribute->DesiredAccess = 0;
    UNREFERENCED_PARAMETER(Event);
    return AlpcTestSendRequest(ClientPort, SendAttributes, Count);
}

static
VOID
AlpcTestIndirectHandles(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ HANDLE ClientPort)
{
    static const ULONG Counts[] = {0, 1, 2, 511, 512, 513};
    PALPC_HANDLE_ATTR32 Entries;
    ALPC_MESSAGE_HANDLE_INFORMATION Information;
    ALPC_TEST_MESSAGE Message;
    ULONG ReturnLength;
    ULONG Index;
    ULONG EntryIndex;
    NTSTATUS Status;
    HANDLE Event;
    SIZE_T AllocationSize = 513 * sizeof(*Entries);

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Event)
        return;

    Entries = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, AllocationSize);
    ok(Entries != NULL, "failed to allocate indirect handle entries\n");
    if (!Entries)
    {
        CloseHandle(Event);
        return;
    }

    for (EntryIndex = 0; EntryIndex < 513; ++EntryIndex)
    {
        Entries[EntryIndex].Flags = ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS;
        Entries[EntryIndex].Handle = HandleToUlong(Event);
        Entries[EntryIndex].ObjectType = ALPC_PORFLG_OBJECT_TYPE_EVENT;
        Entries[EntryIndex].DesiredAccess = 0;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Counts); ++Index)
    {
        Status = AlpcTestSendIndirectHandles(ClientPort, Event, Counts[Index], Counts[Index] ? Entries : NULL);
        trace("ALPC_OBSERVE status Handle.indirect_count_%lu=%08lx\n", Counts[Index], Status);
        ok(Status != STATUS_NOT_IMPLEMENTED, "indirect handle count %lu reached a stub\n", Counts[Index]);
        if (Counts[Index] < 2)
            ok_hex(Status, STATUS_INVALID_PARAMETER);
        if (Counts[Index] > 512)
            ok_hex(Status, STATUS_LPC_HANDLE_COUNT_EXCEEDED);
        if (!NT_SUCCESS(Status))
            continue;

        alpc_expect_status("Handle.indirect_receive", AlpcTestReceiveRequest(ConnectionPort, &Message, NULL), STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
        {
            if (!AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, Counts[Index], "Handle.indirect_receive_failure"))
                break;
            continue;
        }
        ok_eq_ulong(Message.Cookie, ALPC_ATTRIBUTE_TEST_COOKIE);
        ok_eq_ulong(Message.Value, Counts[Index]);
        RtlZeroMemory(&Information, sizeof(Information));
        Information.Index = Counts[Index] - 1;
        ReturnLength = 0x55555555;
        alpc_observe_status("Handle.indirect_query_last", NtAlpcQueryInformationMessage(ConnectionPort, &Message.Header, AlpcMessageHandleInformation, &Information, sizeof(Information), &ReturnLength));
        trace("ALPC_OBSERVE value Handle.indirect_count=%lu last_index=%lu returned_index=%lu flags=%08lx type=%08lx access=%08lx length=%lu\n", Counts[Index], Counts[Index] - 1, Information.Index, Information.Flags, Information.ObjectType, Information.GrantedAccess, ReturnLength);
        alpc_expect_status("Handle.indirect_reply", AlpcTestReplyRequest(ServerPort, &Message), STATUS_SUCCESS);
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, Entries);
    CloseHandle(Event);
}

static
VOID
AlpcTestHandleRollback(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ HANDLE ClientPort)
{
    WCHAR EventName[96];
    ALPC_HANDLE_ATTR32 Entries[2];
    NTSTATUS Status;
    HANDLE Event;
    HANDLE Reopened;

    StringCchPrintfW(EventName, RTL_NUMBER_OF(EventName), L"Local\\NtAlpcRollback_%lu_%lu", GetCurrentProcessId(), GetTickCount());
    Event = CreateEventW(NULL, TRUE, FALSE, EventName);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Event)
        return;

    RtlZeroMemory(Entries, sizeof(Entries));
    Entries[0].Flags = ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS;
    Entries[0].Handle = HandleToUlong(Event);
    Entries[0].ObjectType = ALPC_PORFLG_OBJECT_TYPE_EVENT;
    Entries[1].Flags = ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS;
    Entries[1].Handle = HandleToUlong((HANDLE)(LONG_PTR)-1);
    Entries[1].ObjectType = ALPC_PORFLG_OBJECT_TYPE_EVENT;
    Status = AlpcTestSendIndirectHandles(ClientPort, Event, 2, Entries);
    trace("ALPC_OBSERVE status Handle.partial_rollback=%08lx\n", Status);
    ok(!NT_SUCCESS(Status), "partially invalid indirect list unexpectedly succeeded\n");
    if (NT_SUCCESS(Status))
        AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, 2, "Handle.partial_rollback");
    CloseHandle(Event);

    Reopened = OpenEventW(EVENT_MODIFY_STATE, FALSE, EventName);
    ok(Reopened == NULL, "named event survived failed capture, possible object reference leak: %p\n", Reopened);
    if (Reopened)
        CloseHandle(Reopened);
}

static
VOID
AlpcTestIndirectReceiveCapacities(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ HANDLE ClientPort)
{
    static const ULONG Capacities[] = {0, 1, 2, 3};
    UCHAR ReceiveBuffer[128];
    UCHAR ReceiveBefore[128];
    ALPC_MESSAGE_HANDLE_INFORMATION Information[3];
    ALPC_HANDLE_ATTR32 Entries[2];
    PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes;
    PALPC_HANDLE_ATTR HandleAttribute;
    ALPC_TEST_MESSAGE Message;
    NTSTATUS Status;
    HANDLE Event;
    HANDLE ReceivedHandle;
    DWORD HandleFlags;
    ULONG CapacityIndex;
    ULONG HandleIndex;

    Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Event != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Event)
        return;

    RtlZeroMemory(Entries, sizeof(Entries));
    for (HandleIndex = 0; HandleIndex < RTL_NUMBER_OF(Entries); ++HandleIndex)
    {
        Entries[HandleIndex].Flags = ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS;
        Entries[HandleIndex].Handle = HandleToUlong(Event);
        Entries[HandleIndex].ObjectType = ALPC_PORFLG_OBJECT_TYPE_EVENT;
    }

    for (CapacityIndex = 0; CapacityIndex < RTL_NUMBER_OF(Capacities); ++CapacityIndex)
    {
        Status = AlpcTestSendIndirectHandles(ClientPort, Event, RTL_NUMBER_OF(Entries), Entries);
        ok_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            continue;

        ReceiveAttributes = AlpcTestInitializeAttributes(ReceiveBuffer, sizeof(ReceiveBuffer), ALPC_MESSAGE_HANDLE_ATTRIBUTE);
        if (!ReceiveAttributes)
            break;
        RtlZeroMemory(Information, sizeof(Information));
        HandleAttribute = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
        HandleAttribute->Flags = 0;
        HandleAttribute->HandleAttrArray = Capacities[CapacityIndex] ? Information : NULL;
        HandleAttribute->HandleCount = Capacities[CapacityIndex];
        RtlCopyMemory(ReceiveBefore, ReceiveBuffer, sizeof(ReceiveBefore));

        Status = AlpcTestReceiveRequest(ConnectionPort, &Message, ReceiveAttributes);
        trace("ALPC_OBSERVE status Handle.indirect_receive capacity=%lu status=%08lx valid=%08lx flags=%08lx returned_count=%lu pointer=%p\n", Capacities[CapacityIndex], Status, ReceiveAttributes->ValidAttributes, HandleAttribute->Flags, HandleAttribute->HandleCount, HandleAttribute->HandleAttrArray);
        AlpcTestTraceBufferMutation("Handle.indirect_receive_attributes", ReceiveBefore, ReceiveBuffer, sizeof(ReceiveBuffer));
        ok(Status != STATUS_NOT_IMPLEMENTED, "indirect receive capacity %lu reached a stub\n", Capacities[CapacityIndex]);
        if (NT_SUCCESS(Status))
        {
            ok_eq_ulong(Message.Cookie, ALPC_ATTRIBUTE_TEST_COOKIE);
            ok_eq_ulong(Message.Value, RTL_NUMBER_OF(Entries));
        }

        for (HandleIndex = 0; HandleIndex < RTL_NUMBER_OF(Information); ++HandleIndex)
        {
            trace("ALPC_OBSERVE value Handle.indirect_receive capacity=%lu slot=%lu index=%lu flags=%08lx handle=%08lx type=%08lx access=%08lx\n", Capacities[CapacityIndex], HandleIndex, Information[HandleIndex].Index, Information[HandleIndex].Flags, Information[HandleIndex].Handle, Information[HandleIndex].ObjectType, Information[HandleIndex].GrantedAccess);
            ReceivedHandle = UlongToHandle(Information[HandleIndex].Handle);
            if (ReceivedHandle && ReceivedHandle != Event && GetHandleInformation(ReceivedHandle, &HandleFlags))
                NtClose(ReceivedHandle);
        }

        if (NT_SUCCESS(Status))
            alpc_expect_status("Handle.indirect_receive_reply", AlpcTestReplyRequest(ServerPort, &Message), STATUS_SUCCESS);
        else if (!AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, RTL_NUMBER_OF(Entries), "Handle.indirect_receive_capacity"))
            break;
    }

    CloseHandle(Event);
}

static
VOID
AlpcTestAttributeCaptureFailures(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ HANDLE ClientPort)
{
    UCHAR SendBuffer[128];
    PALPC_MESSAGE_ATTRIBUTES SendAttributes;
    PALPC_HANDLE_ATTR HandleAttribute;
    PALPC_DIRECT_ATTR DirectAttribute;
    WCHAR EventName[96];
    NTSTATUS Status;
    HANDLE OwnerEvent;
    HANDLE LimitedEvent;

    StringCchPrintfW(EventName, RTL_NUMBER_OF(EventName), L"Local\\NtAlpcAccess_%lu_%lu", GetCurrentProcessId(), GetTickCount());
    OwnerEvent = CreateEventW(NULL, TRUE, FALSE, EventName);
    ok(OwnerEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!OwnerEvent)
        return;
    LimitedEvent = OpenEventW(SYNCHRONIZE, FALSE, EventName);
    ok(LimitedEvent != NULL, "OpenEventW failed: %lu\n", GetLastError());
    if (!LimitedEvent)
    {
        CloseHandle(OwnerEvent);
        return;
    }

    SendAttributes = AlpcTestInitializeAttributes(SendBuffer, sizeof(SendBuffer), ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    SendAttributes->ValidAttributes = ALPC_MESSAGE_HANDLE_ATTRIBUTE;
    HandleAttribute = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE);
    HandleAttribute->Flags = 0;
    HandleAttribute->Handle = LimitedEvent;
    HandleAttribute->ObjectType = ALPC_PORFLG_OBJECT_TYPE_EVENT;
    HandleAttribute->DesiredAccess = EVENT_MODIFY_STATE;
    alpc_observe_status("Handle.desired_access_denied", AlpcTestSendRequest(ClientPort, SendAttributes, 4));
    if (NT_SUCCESS(Status) && !AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, 4, "Handle.desired_access_denied"))
        goto Cleanup;

    HandleAttribute->Flags = 1;
    HandleAttribute->Handle = OwnerEvent;
    HandleAttribute->DesiredAccess = 0;
    alpc_observe_status("Handle.invalid_low_flag", AlpcTestSendRequest(ClientPort, SendAttributes, 5));
    if (NT_SUCCESS(Status) && !AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, 5, "Handle.invalid_low_flag"))
        goto Cleanup;

    HandleAttribute->Flags = ALPC_HANDLEFLG_DUPLICATE_SAME_ACCESS;
    HandleAttribute->Handle = (HANDLE)(LONG_PTR)-1;
    alpc_observe_status("Handle.invalid_source_handle", AlpcTestSendRequest(ClientPort, SendAttributes, 6));
    if (NT_SUCCESS(Status) && !AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, 6, "Handle.invalid_source_handle"))
        goto Cleanup;

    SendAttributes = AlpcTestInitializeAttributes(SendBuffer, sizeof(SendBuffer), ALPC_MESSAGE_DIRECT_ATTRIBUTE);
    SendAttributes->ValidAttributes = ALPC_MESSAGE_DIRECT_ATTRIBUTE;
    DirectAttribute = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_DIRECT_ATTRIBUTE);
    DirectAttribute->Event = (HANDLE)(LONG_PTR)-1;
    alpc_observe_status("Direct.invalid_event", AlpcTestSendRequest(ClientPort, SendAttributes, 7));
    if (NT_SUCCESS(Status) && !AlpcTestDrainUnexpectedRequest(ConnectionPort, ServerPort, 7, "Direct.invalid_event"))
        goto Cleanup;

Cleanup:
    CloseHandle(LimitedEvent);
    CloseHandle(OwnerEvent);
}

static
VOID
AlpcTestViewAndSecurity(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ServerPort,
    _In_ HANDLE ClientPort)
{
    UCHAR SendBuffer[192];
    UCHAR ReceiveBuffer[192];
    PALPC_MESSAGE_ATTRIBUTES SendAttributes;
    PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes;
    PALPC_SECURITY_ATTR SendSecurity;
    PALPC_SECURITY_ATTR ReceiveSecurity;
    PALPC_DATA_VIEW_ATTR SendView;
    PALPC_DATA_VIEW_ATTR ReceiveView;
    SECURITY_QUALITY_OF_SERVICE SecurityQos;
    ALPC_SECURITY_ATTR SecurityAttribute;
    ALPC_DATA_VIEW_ATTR ViewAttribute;
    ALPC_TEST_MESSAGE Message;
    ALPC_HANDLE SectionId = NULL;
    SIZE_T ActualSize = 0;
    ULONG Flags = ALPC_MESSAGE_SECURITY_ATTRIBUTE | ALPC_MESSAGE_VIEW_ATTRIBUTE;
    NTSTATUS Status;

    alpc_observe_status("Attributes.section_create", NtAlpcCreatePortSection(ClientPort, 0, NULL, 0x3000, &SectionId, &ActualSize));
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(&ViewAttribute, sizeof(ViewAttribute));
    ViewAttribute.SectionHandle = SectionId;
    ViewAttribute.ViewSize = 0x1000;
    alpc_observe_status("Attributes.view_create", NtAlpcCreateSectionView(ClientPort, 0, &ViewAttribute));
    if (!NT_SUCCESS(Status))
        goto DeleteSection;

    RtlZeroMemory(&SecurityQos, sizeof(SecurityQos));
    SecurityQos.Length = sizeof(SecurityQos);
    SecurityQos.ImpersonationLevel = SecurityImpersonation;
    SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    RtlZeroMemory(&SecurityAttribute, sizeof(SecurityAttribute));
    SecurityAttribute.QoS = &SecurityQos;
    alpc_observe_status("Attributes.security_create", NtAlpcCreateSecurityContext(ClientPort, 0, &SecurityAttribute));
    if (!NT_SUCCESS(Status))
        goto DeleteView;

    SendAttributes = AlpcTestInitializeAttributes(SendBuffer, sizeof(SendBuffer), Flags);
    ReceiveAttributes = AlpcTestInitializeAttributes(ReceiveBuffer, sizeof(ReceiveBuffer), Flags);
    if (!SendAttributes || !ReceiveAttributes)
        goto DeleteSecurity;

    SendAttributes->ValidAttributes = Flags;
    SendSecurity = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_SECURITY_ATTRIBUTE);
    SendSecurity->Flags = 0;
    SendSecurity->QoS = NULL;
    SendSecurity->ContextHandle = SecurityAttribute.ContextHandle;
    SendView = AlpcGetMessageAttribute(SendAttributes, ALPC_MESSAGE_VIEW_ATTRIBUTE);
    SendView->Flags = 0;
    SendView->SectionHandle = SectionId;
    SendView->ViewBase = ViewAttribute.ViewBase;
    SendView->ViewSize = ViewAttribute.ViewSize;

    alpc_expect_status("Attributes.view_security_send", AlpcTestSendRequest(ClientPort, SendAttributes, 3), STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto DeleteSecurity;
    alpc_observe_status("Attributes.view_delete_inflight", NtAlpcDeleteSectionView(ClientPort, 0, ViewAttribute.ViewBase));
    if (NT_SUCCESS(Status))
        ViewAttribute.ViewBase = NULL;
    alpc_observe_status("Attributes.security_delete_inflight", NtAlpcDeleteSecurityContext(ClientPort, 0, SecurityAttribute.ContextHandle));
    if (NT_SUCCESS(Status))
        SecurityAttribute.ContextHandle = NULL;
    alpc_expect_status("Attributes.view_security_receive", AlpcTestReceiveRequest(ConnectionPort, &Message, ReceiveAttributes), STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto DeleteSecurity;
    ok_eq_ulong(Message.Cookie, ALPC_ATTRIBUTE_TEST_COOKIE);
    ok_eq_ulong(Message.Value, 3);

    ReceiveSecurity = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_SECURITY_ATTRIBUTE);
    ReceiveView = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_VIEW_ATTRIBUTE);
    trace("ALPC_OBSERVE value Attributes.view_security valid=%08lx security_flags=%08lx security_context=%p view_flags=%08lx section=%p base=%p size=%Iu\n", ReceiveAttributes->ValidAttributes, ReceiveSecurity->Flags, ReceiveSecurity->ContextHandle, ReceiveView->Flags, ReceiveView->SectionHandle, ReceiveView->ViewBase, ReceiveView->ViewSize);
    alpc_expect_status("Attributes.view_security_reply", AlpcTestReplyRequest(ServerPort, &Message), STATUS_SUCCESS);

DeleteSecurity:
    if (SecurityAttribute.ContextHandle)
        NtAlpcDeleteSecurityContext(ClientPort, 0, SecurityAttribute.ContextHandle);
DeleteView:
    if (ViewAttribute.ViewBase)
        NtAlpcDeleteSectionView(ClientPort, 0, ViewAttribute.ViewBase);
DeleteSection:
    NtAlpcDeletePortSection(ClientPort, 0, SectionId);
}

START_TEST(NtAlpcMessageAttributes)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcMessageAttributes");
    ULONG PortFlags = ALPC_PORFLG_ALLOW_IMPERSONATION | ALPC_PORFLG_ALLOW_DUP_OBJECT | ALPC_PORFLG_ALLOW_MULTIHANDLE_ATTRIBUTE | ALPC_PORFLG_DIRECT_MESSAGE;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;

    AlpcAttributeQueueHealthy = TRUE;
    Status = AlpcTestCreateConnectedPorts(&PortName, PortFlags, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    AlpcTestBasicAttributes(ConnectionPort, ServerPort, ClientPort);
    if (AlpcAttributeQueueHealthy)
        AlpcTestSingleHandle(ConnectionPort, ServerPort, ClientPort);
    if (AlpcAttributeQueueHealthy)
        AlpcTestIndirectHandles(ConnectionPort, ServerPort, ClientPort);
    if (AlpcAttributeQueueHealthy)
        AlpcTestHandleRollback(ConnectionPort, ServerPort, ClientPort);
    if (AlpcAttributeQueueHealthy)
        AlpcTestIndirectReceiveCapacities(ConnectionPort, ServerPort, ClientPort);
    if (AlpcAttributeQueueHealthy)
        AlpcTestAttributeCaptureFailures(ConnectionPort, ServerPort, ClientPort);
    if (AlpcAttributeQueueHealthy)
        AlpcTestViewAndSecurity(ConnectionPort, ServerPort, ClientPort);

    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}
