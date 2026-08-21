/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Native thread-pool ALPC lifecycle and callback tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"
#include <pseh/pseh2.h>

#define TP_ALPC_LIST_SIZE (16 * PAGE_SIZE)
#define TP_ALPC_ACTION_NONE 0
#define TP_ALPC_ACTION_ON_COMPLETION 1
#define TP_ALPC_ACTION_PENDING 2
#define TP_ALPC_LIST_EMPTY 0x00FFFFFFUL
#define TP_ALPC_LIST_GRANULARITY 64
#define TP_ALPC_LIST_START_MAGIC 0xDEADBEEFBAADF00DULL
#define TP_ALPC_LIST_END_MAGIC 0xBAADF00DDEADBEEFULL

typedef VOID (WINAPI *PTEST_TP_ALPC_CALLBACK)(PVOID, PVOID, PVOID);
typedef NTSTATUS (WINAPI *PFN_TP_ALLOC_ALPC_COMPLETION)(PVOID *, HANDLE, PTEST_TP_ALPC_CALLBACK, PVOID, PVOID);
typedef VOID (WINAPI *PFN_TP_ALPC_OBJECT_ROUTINE)(PVOID);
typedef NTSTATUS (WINAPI *PFN_TP_CALLBACK_SEND_ALPC_MESSAGE_ON_COMPLETION)(PVOID, HANDLE, ULONG, PPORT_MESSAGE);
typedef NTSTATUS (WINAPI *PFN_TP_CALLBACK_SEND_PENDING_ALPC_MESSAGE)(PVOID);
typedef VOID (WINAPI *PFN_TP_CALLBACK_SET_EVENT_ON_COMPLETION)(PVOID, HANDLE);

typedef struct _TEST_TP_ALPC_FUNCTIONS
{
    PFN_TP_ALLOC_ALPC_COMPLETION Alloc;
    PFN_TP_ALLOC_ALPC_COMPLETION AllocEx;
    PFN_TP_ALPC_OBJECT_ROUTINE RegisterCompletionList;
    PFN_TP_ALPC_OBJECT_ROUTINE UnregisterCompletionList;
    PFN_TP_ALPC_OBJECT_ROUTINE Release;
    PFN_TP_ALPC_OBJECT_ROUTINE Wait;
    PFN_TP_CALLBACK_SEND_ALPC_MESSAGE_ON_COMPLETION SendOnCompletion;
    PFN_TP_CALLBACK_SEND_PENDING_ALPC_MESSAGE SendPending;
    PFN_TP_CALLBACK_SET_EVENT_ON_COMPLETION SetEventOnCompletion;
} TEST_TP_ALPC_FUNCTIONS, *PTEST_TP_ALPC_FUNCTIONS;

typedef struct _TEST_TP_ALPC_CONTEXT
{
    HANDLE ReceivePort;
    HANDLE ServerPort;
    HANDLE CallbackEvent;
    PVOID CompletionList;
    volatile LONG CallbackCount;
    volatile LONG Action;
    PVOID SeenInstance;
    PVOID SeenContext;
    PVOID SeenAlpc;
    NTSTATUS ReceiveStatus;
    NTSTATUS NullMessageStatus;
    NTSTATUS NullMessageException;
    NTSTATUS EmptyPendingStatus;
    NTSTATUS EmptyPendingException;
    NTSTATUS SendOnCompletionStatus;
    NTSTATUS SendPendingStatus;
    ULONG ReceivedCookie;
    ULONG ReceivedValue;
    ULONG ReceivedMessageId;
    ULONG ReceivedCallbackId;
    BOOLEAN WaitTimedOut;
} TEST_TP_ALPC_CONTEXT, *PTEST_TP_ALPC_CONTEXT;

typedef struct _TEST_TP_ALPC_WAIT_CONTEXT
{
    PVOID Alpc;
    NTSTATUS ExceptionStatus;
} TEST_TP_ALPC_WAIT_CONTEXT, *PTEST_TP_ALPC_WAIT_CONTEXT;

typedef struct _TEST_TP_COMPLETION_LIST_HEADER
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
    RTL_SRWLOCK UserLock;
#ifndef _WIN64
    ULONG UserLockPadding;
#endif
    ULONGLONG EndMagic;
    UCHAR Reserved6[112];
} TEST_TP_COMPLETION_LIST_HEADER, *PTEST_TP_COMPLETION_LIST_HEADER;

C_ASSERT(FIELD_OFFSET(TEST_TP_COMPLETION_LIST_HEADER, State) == 0x80);
C_ASSERT(FIELD_OFFSET(TEST_TP_COMPLETION_LIST_HEADER, UserLock) == 0x280);
C_ASSERT(sizeof(TEST_TP_COMPLETION_LIST_HEADER) == 0x300);

static TEST_TP_ALPC_FUNCTIONS TpAlpcFunctions;

static
BOOLEAN
TpAlpcRangeValid(
    _In_ ULONG Offset,
    _In_ ULONG Size,
    _In_ ULONG TotalSize)
{
    return Offset <= TotalSize && Size <= TotalSize - Offset;
}

static
BOOLEAN
TpAlpcCompletionHeaderValid(
    _In_ PVOID CompletionList)
{
    PTEST_TP_COMPLETION_LIST_HEADER Header = CompletionList;
    ULONGLONG State;

    if (!Header || Header->StartMagic != TP_ALPC_LIST_START_MAGIC || Header->EndMagic != TP_ALPC_LIST_END_MAGIC)
        return FALSE;
    if (Header->TotalSize != TP_ALPC_LIST_SIZE || Header->TotalSize < sizeof(*Header))
        return FALSE;
    if (Header->ListOffset < sizeof(*Header) || (Header->ListOffset & (sizeof(ULONG) - 1)) != 0 || Header->ListSize < sizeof(ULONG) || (Header->ListSize & (sizeof(ULONG) - 1)) != 0 || !TpAlpcRangeValid(Header->ListOffset, Header->ListSize, Header->TotalSize))
        return FALSE;
    if (!TpAlpcRangeValid(Header->BitmapOffset, Header->BitmapSize, Header->TotalSize) || Header->BitmapOffset < Header->ListOffset + Header->ListSize || (Header->BitmapOffset & (sizeof(ULONG) - 1)) != 0 || Header->BitmapSize < sizeof(ULONG) || (Header->BitmapSize & (sizeof(ULONG) - 1)) != 0)
        return FALSE;
    if (!TpAlpcRangeValid(Header->DataOffset, Header->DataSize, Header->TotalSize) || Header->DataOffset < Header->BitmapOffset + Header->BitmapSize || (Header->DataOffset & (TP_ALPC_LIST_GRANULARITY - 1)) != 0 || Header->DataSize < TP_ALPC_LIST_GRANULARITY || (Header->DataSize & (TP_ALPC_LIST_GRANULARITY - 1)) != 0)
        return FALSE;
    State = (ULONGLONG)Header->State;
    if ((State & TP_ALPC_LIST_EMPTY) != TP_ALPC_LIST_EMPTY || ((State >> 24) & TP_ALPC_LIST_EMPTY) != TP_ALPC_LIST_EMPTY || (State >> 48) != 0)
        return FALSE;
    if (Header->PostCount != 0 || Header->ReturnCount != 0 || Header->UserLock.Ptr != NULL)
        return FALSE;
    if (Header->AttributeFlags != ALPC_MESSAGE_CONTEXT_ATTRIBUTE || Header->AttributeSize < sizeof(ALPC_MESSAGE_ATTRIBUTES))
        return FALSE;
    return TRUE;
}

static
BOOLEAN
TpAlpcCompletionMessageValid(
    _In_ PVOID CompletionList,
    _In_ PPORT_MESSAGE Message,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES Attributes)
{
    PTEST_TP_COMPLETION_LIST_HEADER Header = CompletionList;
    ULONG_PTR Base = (ULONG_PTR)Header;
    ULONG_PTR DataStart = Base + Header->DataOffset;
    ULONG_PTR DataEnd = DataStart + Header->DataSize;
    ULONG_PTR MessageAddress = (ULONG_PTR)Message;
    ULONG_PTR AttributeAddress;
    ULONG_PTR ExpectedAttributes;
    USHORT TotalLength;

    if (!Message || MessageAddress < DataStart || MessageAddress > DataEnd - sizeof(ALPC_TEST_MESSAGE) || ((MessageAddress - DataStart) & 7) != 0)
        return FALSE;
    TotalLength = Message->u1.s1.TotalLength;
    if (TotalLength < sizeof(ALPC_TEST_MESSAGE) || TotalLength > DataEnd - MessageAddress)
        return FALSE;
    if (!Attributes)
        return Header->AttributeFlags == 0;
    ExpectedAttributes = (MessageAddress + TotalLength + 7) & ~(ULONG_PTR)7;
    AttributeAddress = (ULONG_PTR)Attributes;
    if (AttributeAddress != ExpectedAttributes || AttributeAddress < DataStart || AttributeAddress > DataEnd - sizeof(ALPC_MESSAGE_ATTRIBUTES))
        return FALSE;
    if (Header->AttributeSize < sizeof(ALPC_MESSAGE_ATTRIBUTES) || Header->AttributeSize > DataEnd - AttributeAddress)
        return FALSE;
    return TRUE;
}

static
DWORD
WINAPI
TpAlpcWaitThread(
    _In_ PVOID Parameter)
{
    PTEST_TP_ALPC_WAIT_CONTEXT Context = Parameter;

    Context->ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        TpAlpcFunctions.Wait(Context->Alpc);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Context->ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    return 0;
}

static
BOOLEAN
TpAlpcWaitBounded(
    _In_ PVOID Alpc,
    _In_ PCSTR Label,
    _In_ BOOLEAN AssertNoException)
{
    PTEST_TP_ALPC_WAIT_CONTEXT Context;
    HANDLE Thread;
    DWORD WaitStatus;
    NTSTATUS ExceptionStatus;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    ok(Context != NULL, "%s wait-context allocation failed\n", Label);
    if (!Context)
        return FALSE;
    Context->Alpc = Alpc;
    Context->ExceptionStatus = STATUS_UNSUCCESSFUL;
    Thread = CreateThread(NULL, 0, TpAlpcWaitThread, Context, 0, NULL);
    ok(Thread != NULL, "%s CreateThread failed: %lu\n", Label, GetLastError());
    if (!Thread)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
        return FALSE;
    }

    WaitStatus = WaitForSingleObject(Thread, ALPC_TEST_TIMEOUT_MS);
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    CloseHandle(Thread);
    if (WaitStatus != WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE tp_alpc %s wait_context_quarantined=%p object=%p\n", Label, Context, Alpc);
        return FALSE;
    }

    ExceptionStatus = Context->ExceptionStatus;
    trace("ALPC_OBSERVE tp_alpc %s wait_exception=%08lx\n", Label, ExceptionStatus);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    if (AssertNoException)
        ok_hex(ExceptionStatus, STATUS_SUCCESS);
    return ExceptionStatus == STATUS_SUCCESS;
}

static
PVOID
TpAlpcAllocatePages(
    _In_ SIZE_T RequestedSize)
{
    PVOID BaseAddress = NULL;
    SIZE_T RegionSize = RequestedSize;
    NTSTATUS Status;

    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &BaseAddress, 0, &RegionSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!NT_SUCCESS(Status))
        return NULL;
    return BaseAddress;
}

static
VOID
TpAlpcFreePages(
    _In_opt_ PVOID BaseAddress)
{
    SIZE_T RegionSize = 0;
    NTSTATUS Status;

    if (!BaseAddress)
        return;
    Status = NtFreeVirtualMemory(NtCurrentProcess(), &BaseAddress, &RegionSize, MEM_RELEASE);
    ok_hex(Status, STATUS_SUCCESS);
}

static
BOOLEAN
TpAlpcResolveFunctions(VOID)
{
    HMODULE Ntdll;

    Ntdll = GetModuleHandleW(L"ntdll.dll");
    ok(Ntdll != NULL, "GetModuleHandleW failed: %lu\n", GetLastError());
    if (!Ntdll)
        return FALSE;

    TpAlpcFunctions.Alloc = (PFN_TP_ALLOC_ALPC_COMPLETION)GetProcAddress(Ntdll, "TpAllocAlpcCompletion");
    TpAlpcFunctions.AllocEx = (PFN_TP_ALLOC_ALPC_COMPLETION)GetProcAddress(Ntdll, "TpAllocAlpcCompletionEx");
    TpAlpcFunctions.RegisterCompletionList = (PFN_TP_ALPC_OBJECT_ROUTINE)GetProcAddress(Ntdll, "TpAlpcRegisterCompletionList");
    TpAlpcFunctions.UnregisterCompletionList = (PFN_TP_ALPC_OBJECT_ROUTINE)GetProcAddress(Ntdll, "TpAlpcUnregisterCompletionList");
    TpAlpcFunctions.Release = (PFN_TP_ALPC_OBJECT_ROUTINE)GetProcAddress(Ntdll, "TpReleaseAlpcCompletion");
    TpAlpcFunctions.Wait = (PFN_TP_ALPC_OBJECT_ROUTINE)GetProcAddress(Ntdll, "TpWaitForAlpcCompletion");
    TpAlpcFunctions.SendOnCompletion = (PFN_TP_CALLBACK_SEND_ALPC_MESSAGE_ON_COMPLETION)GetProcAddress(Ntdll, "TpCallbackSendAlpcMessageOnCompletion");
    TpAlpcFunctions.SendPending = (PFN_TP_CALLBACK_SEND_PENDING_ALPC_MESSAGE)GetProcAddress(Ntdll, "TpCallbackSendPendingAlpcMessage");
    TpAlpcFunctions.SetEventOnCompletion = (PFN_TP_CALLBACK_SET_EVENT_ON_COMPLETION)GetProcAddress(Ntdll, "TpCallbackSetEventOnCompletion");

    ok(TpAlpcFunctions.Alloc != NULL, "TpAllocAlpcCompletion is not exported\n");
    ok(TpAlpcFunctions.AllocEx != NULL, "TpAllocAlpcCompletionEx is not exported\n");
    ok(TpAlpcFunctions.RegisterCompletionList != NULL, "TpAlpcRegisterCompletionList is not exported\n");
    ok(TpAlpcFunctions.UnregisterCompletionList != NULL, "TpAlpcUnregisterCompletionList is not exported\n");
    ok(TpAlpcFunctions.Release != NULL, "TpReleaseAlpcCompletion is not exported\n");
    ok(TpAlpcFunctions.Wait != NULL, "TpWaitForAlpcCompletion is not exported\n");
    ok(TpAlpcFunctions.SendOnCompletion != NULL, "TpCallbackSendAlpcMessageOnCompletion is not exported\n");
    ok(TpAlpcFunctions.SendPending != NULL, "TpCallbackSendPendingAlpcMessage is not exported\n");
    ok(TpAlpcFunctions.SetEventOnCompletion != NULL, "TpCallbackSetEventOnCompletion is not exported\n");

    return TpAlpcFunctions.Alloc && TpAlpcFunctions.AllocEx && TpAlpcFunctions.RegisterCompletionList && TpAlpcFunctions.UnregisterCompletionList && TpAlpcFunctions.Release && TpAlpcFunctions.Wait && TpAlpcFunctions.SendOnCompletion && TpAlpcFunctions.SendPending && TpAlpcFunctions.SetEventOnCompletion;
}

static
VOID
WINAPI
TpAlpcCallback(
    _In_ PVOID Instance,
    _In_ PVOID Parameter,
    _In_ PVOID Alpc)
{
    PTEST_TP_ALPC_CONTEXT Context = Parameter;
    ALPC_TEST_MESSAGE ReceivedMessage;
    ALPC_TEST_MESSAGE OutgoingMessage;
    PORT_MESSAGE ReplyHeader;
    PALPC_MESSAGE_ATTRIBUTES Attributes = NULL;
    PPORT_MESSAGE CompletionMessage;
    LARGE_INTEGER Timeout;
    SIZE_T BufferLength;
    NTSTATUS CompletionException;
    NTSTATUS ReceiveStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS NullMessageStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS NullMessageException = STATUS_SUCCESS;
    NTSTATUS EmptyPendingStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS EmptyPendingException = STATUS_SUCCESS;
    NTSTATUS SendOnCompletionStatus = STATUS_NOT_SUPPORTED;
    NTSTATUS SendPendingStatus = STATUS_NOT_SUPPORTED;
    ULONG ReceivedCookie = 0;
    ULONG ReceivedValue = 0;
    ULONG ReceivedMessageId = 0;
    ULONG ReceivedCallbackId = 0;
    BOOLEAN MessageValid = FALSE;
    BOOLEAN HaveReplyHeader = FALSE;
    LONG Action;

    _SEH2_TRY
    {
        NullMessageStatus = TpAlpcFunctions.SendOnCompletion(Instance, Context->ReceivePort, 0, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        NullMessageException = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    _SEH2_TRY
    {
        EmptyPendingStatus = TpAlpcFunctions.SendPending(Instance);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        EmptyPendingException = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    RtlZeroMemory(&ReplyHeader, sizeof(ReplyHeader));

    if (Context->CompletionList)
    {
        CompletionMessage = NULL;
        CompletionException = STATUS_SUCCESS;
        _SEH2_TRY
        {
            CompletionMessage = AlpcGetMessageFromCompletionList(Context->CompletionList, &Attributes);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            CompletionException = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (CompletionException != STATUS_SUCCESS)
            ReceiveStatus = CompletionException;
        if (CompletionMessage)
        {
            MessageValid = TpAlpcCompletionMessageValid(Context->CompletionList, CompletionMessage, Attributes);
            trace("ALPC_OBSERVE tp_callback completion_message=%p attributes=%p safe=%u\n", CompletionMessage, Attributes, MessageValid);
            if (MessageValid)
            {
                PALPC_TEST_MESSAGE Message = CONTAINING_RECORD(CompletionMessage, ALPC_TEST_MESSAGE, Header);

                ReceiveStatus = STATUS_SUCCESS;
                ReceivedCookie = Message->Cookie;
                ReceivedValue = Message->Value;
                ReceivedMessageId = Message->Header.MessageId;
                ReceivedCallbackId = Message->Header.CallbackId;
                ReplyHeader = Message->Header;
                HaveReplyHeader = TRUE;
                trace("ALPC_OBSERVE tp_callback completion_attributes=%p allocated=%08lx valid=%08lx\n", Attributes, Attributes->AllocatedAttributes, Attributes->ValidAttributes);
                _SEH2_TRY
                {
                    AlpcFreeCompletionListMessage(Context->CompletionList, CompletionMessage);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    ReceiveStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
            }
            else
            {
                ReceiveStatus = STATUS_DATA_ERROR;
            }
        }
        else if (CompletionException == STATUS_SUCCESS)
        {
            ReceiveStatus = STATUS_NO_MORE_ENTRIES;
        }
    }
    else
    {
        RtlZeroMemory(&ReceivedMessage, sizeof(ReceivedMessage));
        BufferLength = sizeof(ReceivedMessage);
        Timeout.QuadPart = 0;
        ReceiveStatus = NtAlpcSendWaitReceivePort(Context->ReceivePort, 0, NULL, NULL, &ReceivedMessage.Header, &BufferLength, NULL, &Timeout);
        if (NT_SUCCESS(ReceiveStatus))
        {
            ReceivedCookie = ReceivedMessage.Cookie;
            ReceivedValue = ReceivedMessage.Value;
            ReceivedMessageId = ReceivedMessage.Header.MessageId;
            ReceivedCallbackId = ReceivedMessage.Header.CallbackId;
            ReplyHeader = ReceivedMessage.Header;
            HaveReplyHeader = TRUE;
        }
    }

    if (!HaveReplyHeader)
        return;

    Action = InterlockedExchange(&Context->Action, TP_ALPC_ACTION_NONE);
    if (Action != TP_ALPC_ACTION_NONE)
    {
        AlpcTestInitializeMessage(&OutgoingMessage, 0x54504F00 | (ULONG)Action, 900 + (ULONG)Action);
        if (HaveReplyHeader)
        {
            OutgoingMessage.Header.ClientId = ReplyHeader.ClientId;
            OutgoingMessage.Header.MessageId = ReplyHeader.MessageId;
            OutgoingMessage.Header.CallbackId = ReplyHeader.CallbackId;
        }
        SendOnCompletionStatus = TpAlpcFunctions.SendOnCompletion(Instance, Context->ReceivePort, ALPC_MSGFLG_REPLY_MESSAGE, &OutgoingMessage.Header);
        if (Action == TP_ALPC_ACTION_PENDING && NT_SUCCESS(SendOnCompletionStatus))
            SendPendingStatus = TpAlpcFunctions.SendPending(Instance);
    }

    Context->SeenInstance = Instance;
    Context->SeenContext = Parameter;
    Context->SeenAlpc = Alpc;
    Context->ReceiveStatus = ReceiveStatus;
    Context->NullMessageStatus = NullMessageStatus;
    Context->NullMessageException = NullMessageException;
    Context->EmptyPendingStatus = EmptyPendingStatus;
    Context->EmptyPendingException = EmptyPendingException;
    Context->SendOnCompletionStatus = SendOnCompletionStatus;
    Context->SendPendingStatus = SendPendingStatus;
    Context->ReceivedCookie = ReceivedCookie;
    Context->ReceivedValue = ReceivedValue;
    Context->ReceivedMessageId = ReceivedMessageId;
    Context->ReceivedCallbackId = ReceivedCallbackId;
    InterlockedIncrement(&Context->CallbackCount);
    TpAlpcFunctions.SetEventOnCompletion(Instance, Context->CallbackEvent);
}

static
VOID
TpAlpcObserveStatus(
    _In_ PCSTR Label,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Output,
    _In_ NTSTATUS ExceptionStatus)
{
    trace("ALPC_OBSERVE tp_alpc %s status=%08lx output=%p exception=%08lx\n", Label, Status, Output, ExceptionStatus);
    if (ExceptionStatus == STATUS_SUCCESS)
        ok(Status != STATUS_NOT_IMPLEMENTED, "%s is still unimplemented\n", Label);
}

static
NTSTATUS
TpAlpcCallObjectRoutine(
    _In_ PCSTR Label,
    _In_ PFN_TP_ALPC_OBJECT_ROUTINE Routine,
    _In_opt_ PVOID Object)
{
    NTSTATUS ExceptionStatus = STATUS_SUCCESS;

    _SEH2_TRY
    {
        Routine(Object);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE tp_alpc %s object=%p exception=%08lx\n", Label, Object, ExceptionStatus);
    return ExceptionStatus;
}

static
VOID
TpAlpcTestInvalidInputs(
    _In_ HANDLE ValidPort)
{
    ALPC_TEST_MESSAGE Message;
    PVOID Object;
    NTSTATUS Status;
    NTSTATUS ExceptionStatus;

    Object = (PVOID)(ULONG_PTR)0x55555555;
    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.Alloc(NULL, ValidPort, TpAlpcCallback, NULL, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("alloc_null_output", Status, Object, ExceptionStatus);

    Object = (PVOID)(ULONG_PTR)0x55555555;
    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.Alloc(&Object, NULL, TpAlpcCallback, NULL, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("alloc_null_port", Status, Object, ExceptionStatus);
    if (ExceptionStatus == STATUS_SUCCESS && NT_SUCCESS(Status) && Object && Object != (PVOID)(ULONG_PTR)0x55555555)
        TpAlpcFunctions.Release(Object);

    Object = (PVOID)(ULONG_PTR)0x55555555;
    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.Alloc(&Object, (HANDLE)(ULONG_PTR)0xdeadbeef, TpAlpcCallback, NULL, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("alloc_invalid_port", Status, Object, ExceptionStatus);
    if (ExceptionStatus == STATUS_SUCCESS && NT_SUCCESS(Status) && Object && Object != (PVOID)(ULONG_PTR)0x55555555)
        TpAlpcFunctions.Release(Object);

    Object = (PVOID)(ULONG_PTR)0x55555555;
    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.Alloc(&Object, ValidPort, NULL, NULL, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("alloc_null_callback", Status, Object, ExceptionStatus);
    if (ExceptionStatus == STATUS_SUCCESS && NT_SUCCESS(Status) && Object && Object != (PVOID)(ULONG_PTR)0x55555555)
        TpAlpcFunctions.Release(Object);

    Object = (PVOID)(ULONG_PTR)0x55555555;
    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.AllocEx(&Object, NULL, TpAlpcCallback, NULL, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("alloc_ex_null_port", Status, Object, ExceptionStatus);
    if (ExceptionStatus == STATUS_SUCCESS && NT_SUCCESS(Status) && Object && Object != (PVOID)(ULONG_PTR)0x55555555)
        TpAlpcFunctions.Release(Object);

    AlpcTestInitializeMessage(&Message, 0x494E5641, 1);
    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.SendOnCompletion(NULL, ValidPort, 0, &Message.Header);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("send_on_completion_null_instance", Status, NULL, ExceptionStatus);

    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.SendOnCompletion(NULL, ValidPort, 0, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("send_on_completion_null_message", Status, NULL, ExceptionStatus);

    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = TpAlpcFunctions.SendPending(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    TpAlpcObserveStatus("send_pending_null_instance", Status, NULL, ExceptionStatus);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        TpAlpcFunctions.RegisterCompletionList(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE tp_alpc register_null exception=%08lx\n", ExceptionStatus);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        TpAlpcFunctions.UnregisterCompletionList(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE tp_alpc unregister_null exception=%08lx\n", ExceptionStatus);

    TpAlpcWaitBounded(NULL, "wait_null", FALSE);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        TpAlpcFunctions.Release(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE tp_alpc release_null exception=%08lx\n", ExceptionStatus);
}

static
VOID
TpAlpcTestInvalidInputsIsolated(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestTpAlpcInvalid");
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        TpAlpcTestInvalidInputs(ServerPort);
        AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
    }
}

static
NTSTATUS
TpAlpcReceiveClientMessage(
    _In_ HANDLE ClientPort,
    _Out_ PALPC_TEST_MESSAGE Message)
{
    LARGE_INTEGER Timeout;
    SIZE_T BufferLength;

    RtlZeroMemory(Message, sizeof(*Message));
    BufferLength = sizeof(*Message);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    return NtAlpcSendWaitReceivePort(ClientPort, 0, NULL, NULL, &Message->Header, &BufferLength, NULL, &Timeout);
}

static
BOOLEAN
TpAlpcSendAndWaitForCallback(
    _In_ PVOID Alpc,
    _In_ HANDLE ClientPort,
    _Inout_ PTEST_TP_ALPC_CONTEXT Context,
    _In_ LONG Action,
    _In_ ULONG Cookie,
    _In_ ULONG Value,
    _In_ LONG ExpectedCallbackCount)
{
    ALPC_TEST_MESSAGE SendMessage;
    ALPC_TEST_MESSAGE OutgoingMessage;
    NTSTATUS Status;
    DWORD WaitStatus;
    BOOLEAN Success = FALSE;

    ResetEvent(Context->CallbackEvent);
    Context->SeenInstance = NULL;
    Context->SeenContext = NULL;
    Context->SeenAlpc = NULL;
    Context->ReceiveStatus = STATUS_UNSUCCESSFUL;
    Context->NullMessageStatus = STATUS_UNSUCCESSFUL;
    Context->NullMessageException = STATUS_UNSUCCESSFUL;
    Context->EmptyPendingStatus = STATUS_UNSUCCESSFUL;
    Context->EmptyPendingException = STATUS_UNSUCCESSFUL;
    Context->SendOnCompletionStatus = STATUS_NOT_SUPPORTED;
    Context->SendPendingStatus = STATUS_NOT_SUPPORTED;
    Context->WaitTimedOut = FALSE;
    InterlockedExchange(&Context->Action, Action);

    AlpcTestInitializeMessage(&SendMessage, Cookie, Value);
    Status = NtAlpcSendWaitReceivePort(ClientPort, 0, &SendMessage.Header, NULL, NULL, NULL, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitForCallbacks;

    WaitStatus = WaitForSingleObject(Context->CallbackEvent, ALPC_TEST_TIMEOUT_MS);
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus != WAIT_OBJECT_0)
        goto WaitForCallbacks;

    trace("ALPC_OBSERVE tp_callback action=%ld count=%ld receive=%08lx null_message=%08lx/%08lx empty_pending=%08lx/%08lx send_on_completion=%08lx send_pending=%08lx instance=%p context=%p alpc=%p cookie=%08lx value=%lu message_id=%lu callback_id=%lu\n", Action, Context->CallbackCount, Context->ReceiveStatus, Context->NullMessageStatus, Context->NullMessageException, Context->EmptyPendingStatus, Context->EmptyPendingException, Context->SendOnCompletionStatus, Context->SendPendingStatus, Context->SeenInstance, Context->SeenContext, Context->SeenAlpc, Context->ReceivedCookie, Context->ReceivedValue, Context->ReceivedMessageId, Context->ReceivedCallbackId);
    ok_eq_long(Context->CallbackCount, ExpectedCallbackCount);
    ok(Context->SeenInstance != NULL, "callback instance is NULL\n");
    ok(Context->SeenContext == Context, "callback context is %p, expected %p\n", Context->SeenContext, Context);
    ok(Context->SeenAlpc == Alpc, "callback ALPC object is %p, expected %p\n", Context->SeenAlpc, Alpc);
    ok_hex(Context->ReceiveStatus, STATUS_SUCCESS);
    if (!NT_SUCCESS(Context->ReceiveStatus))
        goto WaitForCallbacks;
    ok_eq_ulong(Context->ReceivedCookie, Cookie);
    ok_eq_ulong(Context->ReceivedValue, Value);

    if (Action != TP_ALPC_ACTION_NONE)
    {
        ok_hex(Context->SendOnCompletionStatus, STATUS_SUCCESS);
        if (!NT_SUCCESS(Context->SendOnCompletionStatus))
            goto WaitForCallbacks;
        if (Action == TP_ALPC_ACTION_PENDING)
        {
            ok_hex(Context->SendPendingStatus, STATUS_SUCCESS);
            if (!NT_SUCCESS(Context->SendPendingStatus))
                goto WaitForCallbacks;
        }
        Status = TpAlpcReceiveClientMessage(ClientPort, &OutgoingMessage);
        ok_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            goto WaitForCallbacks;
        ok_eq_ulong(OutgoingMessage.Cookie, 0x54504F00 | (ULONG)Action);
        ok_eq_ulong(OutgoingMessage.Value, 900 + (ULONG)Action);
    }

    Success = TRUE;

WaitForCallbacks:
    if (!TpAlpcWaitBounded(Alpc, "callback completion", TRUE))
    {
        Context->WaitTimedOut = TRUE;
        return FALSE;
    }
    return Success;
}

static
VOID
TpAlpcTestBasicLifecycle(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestTpAlpcBasic");
    PTEST_TP_ALPC_CONTEXT Context = NULL;
    PVOID Alpc = (PVOID)(ULONG_PTR)0x55555555;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;
    BOOLEAN Quarantine = FALSE;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    ok(Context != NULL, "basic TP context allocation failed\n");
    if (!Context)
        goto Cleanup;
    Context->ReceivePort = ConnectionPort;
    Context->ServerPort = ServerPort;
    Context->CallbackEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Context->CallbackEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Context->CallbackEvent)
        goto Cleanup;

    Status = TpAlpcFunctions.Alloc(&Alpc, ConnectionPort, TpAlpcCallback, Context, NULL);
    trace("ALPC_OBSERVE tp_alpc alloc_valid status=%08lx output=%p\n", Status, Alpc);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Alpc != NULL, "TpAllocAlpcCompletion returned NULL\n");
    if (!NT_SUCCESS(Status) || !Alpc)
        goto Cleanup;

    if (!TpAlpcWaitBounded(Alpc, "base initial", TRUE))
    {
        Quarantine = TRUE;
        goto Cleanup;
    }
    TpAlpcCallObjectRoutine("base_register", TpAlpcFunctions.RegisterCompletionList, Alpc);
    TpAlpcCallObjectRoutine("base_register_repeated", TpAlpcFunctions.RegisterCompletionList, Alpc);
    TpAlpcCallObjectRoutine("base_unregister", TpAlpcFunctions.UnregisterCompletionList, Alpc);
    TpAlpcCallObjectRoutine("base_unregister_repeated", TpAlpcFunctions.UnregisterCompletionList, Alpc);
    if (!TpAlpcSendAndWaitForCallback(Alpc, ClientPort, Context, TP_ALPC_ACTION_NONE, 0x54504231, 111, 1))
    {
        Quarantine = Context->WaitTimedOut;
        goto Cleanup;
    }

Cleanup:
    if (Quarantine)
    {
        trace("ALPC_OBSERVE tp_alpc base_quarantine object=%p context=%p event=%p ports=%p/%p/%p\n", Alpc, Context, Context ? Context->CallbackEvent : NULL, ConnectionPort, ServerPort, ClientPort);
        return;
    }
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
    ConnectionPort = NULL;
    ServerPort = NULL;
    ClientPort = NULL;
    if (Alpc && Alpc != (PVOID)(ULONG_PTR)0x55555555)
    {
        if (!TpAlpcWaitBounded(Alpc, "base port-close cleanup", TRUE))
        {
            trace("ALPC_OBSERVE tp_alpc base_cleanup_quarantine object=%p context=%p event=%p ports=%p/%p/%p\n", Alpc, Context, Context ? Context->CallbackEvent : NULL, ConnectionPort, ServerPort, ClientPort);
            return;
        }
        TpAlpcFunctions.Release(Alpc);
    }
    if (Context)
    {
        if (Context->CallbackEvent)
            CloseHandle(Context->CallbackEvent);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    }
}

static
VOID
TpAlpcTestExtendedLifecycle(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestTpAlpcExtended");
    PTEST_TP_ALPC_CONTEXT Context = NULL;
    PVOID CompletionList = NULL;
    PVOID Alpc = (PVOID)(ULONG_PTR)0x55555555;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;
    BOOLEAN CompletionListRegistered = FALSE;
    BOOLEAN HeaderValid;
    BOOLEAN Quarantine = FALSE;

    CompletionList = TpAlpcAllocatePages(TP_ALPC_LIST_SIZE);
    ok(CompletionList != NULL, "extended completion-list allocation failed\n");
    if (!CompletionList)
        return;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    ok(Context != NULL, "extended TP context allocation failed\n");
    if (!Context)
        goto Cleanup;
    Context->ReceivePort = ConnectionPort;
    Context->ServerPort = ServerPort;
    Context->CompletionList = CompletionList;
    Context->CallbackEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Context->CallbackEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Context->CallbackEvent)
        goto Cleanup;

    Status = TpAlpcFunctions.AllocEx(&Alpc, ConnectionPort, TpAlpcCallback, Context, NULL);
    trace("ALPC_OBSERVE tp_alpc alloc_ex_valid status=%08lx output=%p\n", Status, Alpc);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Alpc != NULL, "TpAllocAlpcCompletionEx returned NULL\n");
    if (!NT_SUCCESS(Status) || !Alpc)
        goto Cleanup;

    if (!TpAlpcWaitBounded(Alpc, "extended initial", TRUE))
    {
        Quarantine = TRUE;
        goto Cleanup;
    }

    Status = AlpcRegisterCompletionList(ConnectionPort, CompletionList, TP_ALPC_LIST_SIZE, 1, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    trace("ALPC_OBSERVE tp_alpc completion_list_register status=%08lx buffer=%p\n", Status, CompletionList);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    CompletionListRegistered = TRUE;
    HeaderValid = TpAlpcCompletionHeaderValid(CompletionList);
    ok(HeaderValid, "kernel initialized an unsafe TP completion-list header\n");
    if (!HeaderValid)
    {
        Status = NtAlpcSetInformation(ConnectionPort, AlpcUnregisterCompletionListInformation, NULL, 0);
        trace("ALPC_OBSERVE tp_alpc unsafe_header_unregister status=%08lx\n", Status);
        if (NT_SUCCESS(Status))
            CompletionListRegistered = FALSE;
        else
            Quarantine = TRUE;
        goto Cleanup;
    }
    ok_hex(TpAlpcCallObjectRoutine("extended_register", TpAlpcFunctions.RegisterCompletionList, Alpc), STATUS_SUCCESS);
    ok_hex(TpAlpcCallObjectRoutine("extended_register_repeated", TpAlpcFunctions.RegisterCompletionList, Alpc), STATUS_SUCCESS);

    if (!TpAlpcSendAndWaitForCallback(Alpc, ClientPort, Context, TP_ALPC_ACTION_ON_COMPLETION, 0x54504531, 301, 1))
    {
        Quarantine = Context->WaitTimedOut;
        goto Cleanup;
    }
    if (!TpAlpcSendAndWaitForCallback(Alpc, ClientPort, Context, TP_ALPC_ACTION_PENDING, 0x54504532, 302, 2))
    {
        Quarantine = Context->WaitTimedOut;
        goto Cleanup;
    }

    ok_hex(TpAlpcCallObjectRoutine("extended_unregister", TpAlpcFunctions.UnregisterCompletionList, Alpc), STATUS_SUCCESS);
    ok_hex(TpAlpcCallObjectRoutine("extended_unregister_repeated", TpAlpcFunctions.UnregisterCompletionList, Alpc), STATUS_SUCCESS);
    Status = AlpcRundownCompletionList(ConnectionPort);
    trace("ALPC_OBSERVE tp_alpc completion_list_rundown status=%08lx outstanding=%lu\n", Status, AlpcGetOutstandingCompletionListMessageCount(CompletionList));
    ok_hex(Status, STATUS_SUCCESS);
    Status = AlpcUnregisterCompletionList(ConnectionPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        CompletionListRegistered = FALSE;

Cleanup:
    if (Quarantine)
    {
        trace("ALPC_OBSERVE tp_alpc extended_quarantine object=%p context=%p event=%p list=%p ports=%p/%p/%p\n", Alpc, Context, Context ? Context->CallbackEvent : NULL, CompletionList, ConnectionPort, ServerPort, ClientPort);
        return;
    }
    if (CompletionListRegistered && ConnectionPort)
    {
        AlpcRundownCompletionList(ConnectionPort);
        AlpcUnregisterCompletionList(ConnectionPort);
    }
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
    ConnectionPort = NULL;
    ServerPort = NULL;
    ClientPort = NULL;
    if (Alpc && Alpc != (PVOID)(ULONG_PTR)0x55555555)
    {
        if (!TpAlpcWaitBounded(Alpc, "extended port-close cleanup", TRUE))
        {
            trace("ALPC_OBSERVE tp_alpc extended_cleanup_quarantine object=%p context=%p event=%p list=%p ports=%p/%p/%p\n", Alpc, Context, Context ? Context->CallbackEvent : NULL, CompletionList, ConnectionPort, ServerPort, ClientPort);
            return;
        }
        TpAlpcFunctions.Release(Alpc);
    }
    if (Context)
    {
        if (Context->CallbackEvent)
            CloseHandle(Context->CallbackEvent);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    }
    TpAlpcFreePages(CompletionList);
}

START_TEST(TpAlpc)
{
    if (!TpAlpcResolveFunctions())
    {
        skip("One or more native TP ALPC exports are unavailable\n");
        return;
    }

    if (AlpcTestIsChildMode("tp-invalid-inputs"))
    {
        TpAlpcTestInvalidInputsIsolated();
        return;
    }
    if (AlpcTestIsChildMode("tp-basic-lifecycle"))
    {
        TpAlpcTestBasicLifecycle();
        return;
    }
    if (AlpcTestIsChildMode("tp-extended-lifecycle"))
    {
        TpAlpcTestExtendedLifecycle();
        return;
    }

    AlpcTestRunIsolatedCase(L"TpAlpc", L"tp-invalid-inputs", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestRunIsolatedCase(L"TpAlpc", L"tp-basic-lifecycle", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestRunIsolatedCase(L"TpAlpc", L"tp-extended-lifecycle", ALPC_TEST_CHILD_TIMEOUT_MS);
}
