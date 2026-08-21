/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ALPC completion-list helper and end-to-end tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"
#include <pseh/pseh2.h>

#define TEST_COMPLETION_LIST_EMPTY          0x00FFFFFFUL
#define TEST_COMPLETION_LIST_TAIL_SHIFT     24
#define TEST_COMPLETION_LIST_ACTIVE_SHIFT   48
#define TEST_COMPLETION_LIST_GRANULARITY    64
#define TEST_COMPLETION_LIST_START_MAGIC    0xDEADBEEFBAADF00DULL
#define TEST_COMPLETION_LIST_END_MAGIC      0xBAADF00DDEADBEEFULL
#define TEST_LOCAL_LIST_SIZE                0x1000
#define TEST_LOCAL_LIST_OFFSET              0x300
#define TEST_LOCAL_LIST_CAPACITY            4
#define TEST_LOCAL_BITMAP_OFFSET            0x340
#define TEST_LOCAL_BITMAP_SIZE              0x20
#define TEST_LOCAL_DATA_OFFSET              0x400
#define TEST_LOCAL_DATA_SIZE                0x400
#define TEST_KERNEL_LIST_SIZE               (4 * PAGE_SIZE)
#define TEST_WORKER_COUNT                   8

typedef struct _TEST_COMPLETION_LIST_HEADER
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
} TEST_COMPLETION_LIST_HEADER, *PTEST_COMPLETION_LIST_HEADER;

typedef struct _TEST_COMPLETION_WORKER_CONTEXT
{
    PVOID CompletionList;
    HANDLE Gate;
    BOOLEAN Register;
    BOOLEAN Result;
} TEST_COMPLETION_WORKER_CONTEXT, *PTEST_COMPLETION_WORKER_CONTEXT;

C_ASSERT(FIELD_OFFSET(TEST_COMPLETION_LIST_HEADER, State) == 0x80);
C_ASSERT(FIELD_OFFSET(TEST_COMPLETION_LIST_HEADER, PostCount) == 0x100);
C_ASSERT(FIELD_OFFSET(TEST_COMPLETION_LIST_HEADER, ReturnCount) == 0x180);
C_ASSERT(FIELD_OFFSET(TEST_COMPLETION_LIST_HEADER, UserLock) == 0x280);
C_ASSERT(sizeof(TEST_COMPLETION_LIST_HEADER) == 0x300);

static
ULONGLONG
TestCompletionState(
    _In_ ULONG Head,
    _In_ ULONG Tail,
    _In_ ULONG Active)
{
    return ((ULONGLONG)Active << TEST_COMPLETION_LIST_ACTIVE_SHIFT) | ((ULONGLONG)Tail << TEST_COMPLETION_LIST_TAIL_SHIFT) | Head;
}

static
ULONG
TestCompletionHead(
    _In_ ULONGLONG State)
{
    return (ULONG)(State & TEST_COMPLETION_LIST_EMPTY);
}

static
ULONG
TestCompletionTail(
    _In_ ULONGLONG State)
{
    return (ULONG)((State >> TEST_COMPLETION_LIST_TAIL_SHIFT) & TEST_COMPLETION_LIST_EMPTY);
}

static
ULONG
TestCompletionActive(
    _In_ ULONGLONG State)
{
    return (ULONG)(State >> TEST_COMPLETION_LIST_ACTIVE_SHIFT);
}

static
PVOID
TestAllocatePages(
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
TestFreePages(
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
PTEST_COMPLETION_LIST_HEADER
TestInitializeLocalCompletionList(
    _Out_writes_bytes_(TEST_LOCAL_LIST_SIZE) PVOID Buffer)
{
    PTEST_COMPLETION_LIST_HEADER Header = Buffer;

    RtlZeroMemory(Buffer, TEST_LOCAL_LIST_SIZE);
    Header->StartMagic = TEST_COMPLETION_LIST_START_MAGIC;
    Header->TotalSize = TEST_LOCAL_LIST_SIZE;
    Header->ListOffset = TEST_LOCAL_LIST_OFFSET;
    Header->ListSize = TEST_LOCAL_LIST_CAPACITY * sizeof(ULONG);
    Header->BitmapOffset = TEST_LOCAL_BITMAP_OFFSET;
    Header->BitmapSize = TEST_LOCAL_BITMAP_SIZE;
    Header->DataOffset = TEST_LOCAL_DATA_OFFSET;
    Header->DataSize = TEST_LOCAL_DATA_SIZE;
    Header->State = (LONGLONG)TestCompletionState(TEST_COMPLETION_LIST_EMPTY, TEST_COMPLETION_LIST_EMPTY, 0);
    Header->EndMagic = TEST_COMPLETION_LIST_END_MAGIC;
    return Header;
}

static
PULONG
TestCompletionEntries(
    _In_ PTEST_COMPLETION_LIST_HEADER Header)
{
    return (PULONG)((PUCHAR)Header + Header->ListOffset);
}

static
volatile LONG *
TestCompletionBitmap(
    _In_ PTEST_COMPLETION_LIST_HEADER Header)
{
    return (volatile LONG *)((PUCHAR)Header + Header->BitmapOffset);
}

static
PALPC_TEST_MESSAGE
TestCompletionMessage(
    _In_ PTEST_COMPLETION_LIST_HEADER Header,
    _In_ ULONG Offset)
{
    return (PALPC_TEST_MESSAGE)((PUCHAR)Header + Header->DataOffset + Offset);
}

static
BOOLEAN
TestCompletionRangeValid(
    _In_ ULONG Offset,
    _In_ ULONG Size,
    _In_ ULONG TotalSize)
{
    return Offset <= TotalSize && Size <= TotalSize - Offset;
}

static
BOOLEAN
TestKernelCompletionHeaderValid(
    _In_ PTEST_COMPLETION_LIST_HEADER Header,
    _In_ ULONG AllocationSize)
{
    ULONGLONG State;

    if (Header->StartMagic != TEST_COMPLETION_LIST_START_MAGIC || Header->EndMagic != TEST_COMPLETION_LIST_END_MAGIC)
        return FALSE;
    if (Header->TotalSize != AllocationSize || Header->TotalSize < sizeof(*Header))
        return FALSE;
    if (Header->ListOffset < sizeof(*Header) || (Header->ListOffset & (sizeof(ULONG) - 1)) != 0 || Header->ListSize < sizeof(ULONG) || (Header->ListSize & (sizeof(ULONG) - 1)) != 0 || !TestCompletionRangeValid(Header->ListOffset, Header->ListSize, Header->TotalSize))
        return FALSE;
    if (!TestCompletionRangeValid(Header->BitmapOffset, Header->BitmapSize, Header->TotalSize) || Header->BitmapOffset < Header->ListOffset + Header->ListSize || (Header->BitmapOffset & (sizeof(ULONG) - 1)) != 0 || Header->BitmapSize < sizeof(ULONG) || (Header->BitmapSize & (sizeof(ULONG) - 1)) != 0)
        return FALSE;
    if (!TestCompletionRangeValid(Header->DataOffset, Header->DataSize, Header->TotalSize) || Header->DataOffset < Header->BitmapOffset + Header->BitmapSize || (Header->DataOffset & (TEST_COMPLETION_LIST_GRANULARITY - 1)) != 0 || Header->DataSize < TEST_COMPLETION_LIST_GRANULARITY || (Header->DataSize & (TEST_COMPLETION_LIST_GRANULARITY - 1)) != 0)
        return FALSE;
    State = (ULONGLONG)Header->State;
    if (TestCompletionHead(State) != TEST_COMPLETION_LIST_EMPTY || TestCompletionTail(State) != TEST_COMPLETION_LIST_EMPTY || TestCompletionActive(State) != 0)
        return FALSE;
    if (Header->PostCount != 0 || Header->ReturnCount != 0 || Header->UserLock.Ptr != NULL)
        return FALSE;
    if (Header->AttributeFlags != ALPC_MESSAGE_CONTEXT_ATTRIBUTE || Header->AttributeSize < sizeof(ALPC_MESSAGE_ATTRIBUTES))
        return FALSE;
    return TRUE;
}

static
BOOLEAN
TestKernelCompletionMessageValid(
    _In_ PTEST_COMPLETION_LIST_HEADER Header,
    _In_ PPORT_MESSAGE Message,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES Attributes)
{
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
TestCompletionWorkerThread(
    _In_ PVOID Parameter)
{
    PTEST_COMPLETION_WORKER_CONTEXT Context = Parameter;

    WaitForSingleObject(Context->Gate, ALPC_TEST_TIMEOUT_MS);
    if (Context->Register)
        Context->Result = AlpcRegisterCompletionListWorkerThread(Context->CompletionList);
    else
        Context->Result = AlpcUnregisterCompletionListWorkerThread(Context->CompletionList);
    return 0;
}

static
VOID
TestCompletionMetadata(
    _Inout_updates_bytes_(TEST_LOCAL_LIST_SIZE) PVOID Buffer)
{
    PTEST_COMPLETION_LIST_HEADER Header;
    PALPC_TEST_MESSAGE Message;
    PALPC_MESSAGE_ATTRIBUTES Attributes;
    ULONG LastMessageId;
    ULONG LastCallbackId;

    Header = TestInitializeLocalCompletionList(Buffer);
    Header->LastMessageId = 0x12345678;
    Header->LastCallbackId = 0x87654321;
    LastMessageId = 0;
    LastCallbackId = 0;
    AlpcGetCompletionListLastMessageInformation(Header, &LastMessageId, &LastCallbackId);
    ok_eq_ulong(LastMessageId, 0x12345678);
    ok_eq_ulong(LastCallbackId, 0x87654321);

    Header->PostCount = 9;
    Header->ReturnCount = 4;
    ok_eq_ulong(AlpcGetOutstandingCompletionListMessageCount(Header), 5);
    Header->ReturnCount = 9;
    ok_eq_ulong(AlpcGetOutstandingCompletionListMessageCount(Header), 0);
    Header->PostCount = 0;
    Header->ReturnCount = 1;
    trace("ALPC_OBSERVE completion_outstanding underflow=%lu post=%ld return=%ld\n", AlpcGetOutstandingCompletionListMessageCount(Header), Header->PostCount, Header->ReturnCount);

    Message = TestCompletionMessage(Header, 0);
    AlpcTestInitializeMessage(Message, 0x4D455441, 1);
    Attributes = AlpcGetCompletionListMessageAttributes(Header, &Message->Header);
    ok(Attributes == NULL, "attribute pointer is %p without requested attributes\n", Attributes);

    Header->AttributeFlags = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    Header->AttributeSize = sizeof(ALPC_MESSAGE_ATTRIBUTES) + sizeof(ALPC_CONTEXT_ATTR);
    Message->Header.u1.s1.TotalLength = sizeof(PORT_MESSAGE);
    Attributes = AlpcGetCompletionListMessageAttributes(Header, &Message->Header);
    ok(Attributes == (PALPC_MESSAGE_ATTRIBUTES)((PUCHAR)Message + sizeof(PORT_MESSAGE)), "aligned attribute pointer is %p, expected %p\n", Attributes, (PUCHAR)Message + sizeof(PORT_MESSAGE));
    Message->Header.u1.s1.TotalLength = sizeof(PORT_MESSAGE) + 1;
    Attributes = AlpcGetCompletionListMessageAttributes(Header, &Message->Header);
    ok(Attributes == (PALPC_MESSAGE_ATTRIBUTES)(((ULONG_PTR)Message + sizeof(PORT_MESSAGE) + 8) & ~(ULONG_PTR)7), "unaligned attribute pointer is %p\n", Attributes);
    Message->Header.u1.s1.TotalLength = 0;
    Attributes = AlpcGetCompletionListMessageAttributes(Header, &Message->Header);
    trace("ALPC_OBSERVE completion_attributes zero_total_length pointer=%p message=%p\n", Attributes, Message);
}

static
VOID
TestCompletionDequeue(
    _Inout_updates_bytes_(TEST_LOCAL_LIST_SIZE) PVOID Buffer)
{
    PTEST_COMPLETION_LIST_HEADER Header;
    PALPC_TEST_MESSAGE Message0;
    PALPC_TEST_MESSAGE Message1;
    PALPC_MESSAGE_ATTRIBUTES Attributes;
    PPORT_MESSAGE Result;
    PULONG Entries;
    ULONGLONG State;

    Header = TestInitializeLocalCompletionList(Buffer);
    Attributes = (PALPC_MESSAGE_ATTRIBUTES)(ULONG_PTR)0x55555555;
    State = (ULONGLONG)Header->State;
    Result = AlpcGetMessageFromCompletionList(Header, &Attributes);
    ok(Result == NULL, "empty list returned %p\n", Result);
    ok(Attributes == NULL, "empty list attribute pointer is %p\n", Attributes);
    ok_eq_ulonglong((ULONGLONG)Header->State, State);

    Header = TestInitializeLocalCompletionList(Buffer);
    Entries = TestCompletionEntries(Header);
    Message0 = TestCompletionMessage(Header, 0);
    AlpcTestInitializeMessage(Message0, 0x4F4E4530, 10);
    Entries[0] = 0;
    Header->State = (LONGLONG)TestCompletionState(0, 0, 7);
    Attributes = (PALPC_MESSAGE_ATTRIBUTES)(ULONG_PTR)0x55555555;
    Result = AlpcGetMessageFromCompletionList(Header, &Attributes);
    ok(Result == &Message0->Header, "single entry returned %p, expected %p\n", Result, &Message0->Header);
    ok(Attributes == NULL, "single entry attributes are %p\n", Attributes);
    ok_eq_ulong(TestCompletionHead((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);
    ok_eq_ulong(TestCompletionTail((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 7);
    Result = AlpcGetMessageFromCompletionList(Header, NULL);
    ok(Result == NULL, "repeated dequeue returned %p\n", Result);

    Header = TestInitializeLocalCompletionList(Buffer);
    Entries = TestCompletionEntries(Header);
    Message0 = TestCompletionMessage(Header, TEST_COMPLETION_LIST_GRANULARITY);
    Message1 = TestCompletionMessage(Header, 2 * TEST_COMPLETION_LIST_GRANULARITY);
    AlpcTestInitializeMessage(Message0, 0x57524150, 30);
    AlpcTestInitializeMessage(Message1, 0x57524150, 40);
    Entries[3] = TEST_COMPLETION_LIST_GRANULARITY;
    Entries[0] = 2 * TEST_COMPLETION_LIST_GRANULARITY;
    Header->State = (LONGLONG)TestCompletionState(3, 0, 2);
    Result = AlpcGetMessageFromCompletionList(Header, NULL);
    ok(Result == &Message0->Header, "wrapped first entry returned %p, expected %p\n", Result, &Message0->Header);
    ok_eq_ulong(TestCompletionHead((ULONGLONG)Header->State), 0);
    ok_eq_ulong(TestCompletionTail((ULONGLONG)Header->State), 0);
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 2);
    Result = AlpcGetMessageFromCompletionList(Header, NULL);
    ok(Result == &Message1->Header, "wrapped second entry returned %p, expected %p\n", Result, &Message1->Header);
    ok_eq_ulong(TestCompletionHead((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);
    ok_eq_ulong(TestCompletionTail((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);

    Header = TestInitializeLocalCompletionList(Buffer);
    Header->ListSize = 0;
    Header->State = (LONGLONG)TestCompletionState(0, 0, 0);
    Attributes = (PALPC_MESSAGE_ATTRIBUTES)(ULONG_PTR)0x55555555;
    Result = AlpcGetMessageFromCompletionList(Header, &Attributes);
    trace("ALPC_OBSERVE completion_dequeue zero_capacity result=%p attributes=%p state=%I64x\n", Result, Attributes, (ULONGLONG)Header->State);

    Header = TestInitializeLocalCompletionList(Buffer);
    Header->State = (LONGLONG)TestCompletionState(TEST_LOCAL_LIST_CAPACITY, 0, 0);
    Result = AlpcGetMessageFromCompletionList(Header, NULL);
    trace("ALPC_OBSERVE completion_dequeue out_of_range_head result=%p state=%I64x\n", Result, (ULONGLONG)Header->State);

    Header = TestInitializeLocalCompletionList(Buffer);
    Header->State = (LONGLONG)TestCompletionState(0, TEST_LOCAL_LIST_CAPACITY, 0);
    Result = AlpcGetMessageFromCompletionList(Header, NULL);
    trace("ALPC_OBSERVE completion_dequeue out_of_range_tail result=%p state=%I64x\n", Result, (ULONGLONG)Header->State);

    Header = TestInitializeLocalCompletionList(Buffer);
    Entries = TestCompletionEntries(Header);
    Entries[0] = Header->DataSize;
    Header->State = (LONGLONG)TestCompletionState(0, 0, 0);
    Attributes = (PALPC_MESSAGE_ATTRIBUTES)(ULONG_PTR)0x55555555;
    Result = AlpcGetMessageFromCompletionList(Header, &Attributes);
    trace("ALPC_OBSERVE completion_dequeue offset_at_limit result=%p attributes=%p state=%I64x\n", Result, Attributes, (ULONGLONG)Header->State);

    Header = TestInitializeLocalCompletionList(Buffer);
    Entries = TestCompletionEntries(Header);
    Entries[0] = Header->DataSize - 1;
    Header->State = (LONGLONG)TestCompletionState(0, 0, 0);
    Result = AlpcGetMessageFromCompletionList(Header, NULL);
    trace("ALPC_OBSERVE completion_dequeue offset_last_byte result=%p expected_address=%p state=%I64x\n", Result, (PUCHAR)Header + Header->DataOffset + Header->DataSize - 1, (ULONGLONG)Header->State);
}

static
VOID
TestCompletionFree(
    _Inout_updates_bytes_(TEST_LOCAL_LIST_SIZE) PVOID Buffer)
{
    PTEST_COMPLETION_LIST_HEADER Header;
    PALPC_TEST_MESSAGE Message;
    volatile LONG *Bitmap;
    LONG Before;

    Header = TestInitializeLocalCompletionList(Buffer);
    Bitmap = TestCompletionBitmap(Header);
    Bitmap[0] = -1;
    Message = TestCompletionMessage(Header, 0);
    AlpcTestInitializeMessage(Message, 0x46524545, 1);
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    ok_eq_long(Bitmap[0], (LONG)0xfffffffe);
    ok_eq_long(Header->ReturnCount, 1);
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    trace("ALPC_OBSERVE completion_free repeated bitmap=%08lx return_count=%ld\n", (ULONG)Bitmap[0], Header->ReturnCount);

    Header = TestInitializeLocalCompletionList(Buffer);
    Bitmap = TestCompletionBitmap(Header);
    Bitmap[0] = -1;
    Message = TestCompletionMessage(Header, 2 * TEST_COMPLETION_LIST_GRANULARITY);
    RtlZeroMemory(Message, 2 * TEST_COMPLETION_LIST_GRANULARITY);
    Message->Header.u1.s1.TotalLength = 96;
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    ok_eq_long(Bitmap[0], (LONG)0xfffffff3);
    ok_eq_long(Header->ReturnCount, 1);

    Header = TestInitializeLocalCompletionList(Buffer);
    Header->AttributeFlags = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    Header->AttributeSize = TEST_COMPLETION_LIST_GRANULARITY;
    Bitmap = TestCompletionBitmap(Header);
    Bitmap[0] = -1;
    Message = TestCompletionMessage(Header, 4 * TEST_COMPLETION_LIST_GRANULARITY);
    RtlZeroMemory(Message, 2 * TEST_COMPLETION_LIST_GRANULARITY);
    Message->Header.u1.s1.TotalLength = sizeof(ALPC_TEST_MESSAGE);
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    ok((Bitmap[0] & (1 << 4)) == 0, "message bit was not cleared: %08lx\n", (ULONG)Bitmap[0]);
    ok((Bitmap[0] & (1 << 5)) == 0, "attribute spill bit was not cleared: %08lx\n", (ULONG)Bitmap[0]);
    ok_eq_long(Header->ReturnCount, 1);

    Header = TestInitializeLocalCompletionList(Buffer);
    Bitmap = TestCompletionBitmap(Header);
    Bitmap[0] = -1;
    Message = TestCompletionMessage(Header, Header->DataSize - TEST_COMPLETION_LIST_GRANULARITY);
    RtlZeroMemory(Message, TEST_COMPLETION_LIST_GRANULARITY);
    Message->Header.u1.s1.TotalLength = TEST_COMPLETION_LIST_GRANULARITY;
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    ok((Bitmap[0] & (1 << 15)) == 0, "last data bit was not cleared: %08lx\n", (ULONG)Bitmap[0]);
    ok_eq_long(Header->ReturnCount, 1);

    Header = TestInitializeLocalCompletionList(Buffer);
    Bitmap = TestCompletionBitmap(Header);
    Bitmap[0] = -1;
    Before = Bitmap[0];
    Message = TestCompletionMessage(Header, 0);
    AlpcTestInitializeMessage(Message, 0x42414430, 1);
    AlpcFreeCompletionListMessage(Header, (PPORT_MESSAGE)((PUCHAR)Message + 1));
    ok_eq_long(Bitmap[0], Before);
    ok_eq_long(Header->ReturnCount, 0);
    AlpcFreeCompletionListMessage(Header, (PPORT_MESSAGE)((PUCHAR)Header + Header->DataOffset - TEST_COMPLETION_LIST_GRANULARITY));
    ok_eq_long(Bitmap[0], Before);
    ok_eq_long(Header->ReturnCount, 0);
    AlpcFreeCompletionListMessage(Header, (PPORT_MESSAGE)((PUCHAR)Header + Header->DataOffset + Header->DataSize));
    ok_eq_long(Bitmap[0], Before);
    ok_eq_long(Header->ReturnCount, 0);

    Message = TestCompletionMessage(Header, 0);
    Message->Header.u1.s1.TotalLength = 0;
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    ok_eq_long(Bitmap[0], Before);
    ok_eq_long(Header->ReturnCount, 0);

    Message = TestCompletionMessage(Header, Header->DataSize - TEST_COMPLETION_LIST_GRANULARITY);
    Message->Header.u1.s1.TotalLength = TEST_COMPLETION_LIST_GRANULARITY + 1;
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    ok_eq_long(Bitmap[0], Before);
    ok_eq_long(Header->ReturnCount, 0);

    Header->BitmapSize = 0;
    Message = TestCompletionMessage(Header, 0);
    Message->Header.u1.s1.TotalLength = sizeof(PORT_MESSAGE);
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    ok_eq_long(Bitmap[0], Before);
    ok_eq_long(Header->ReturnCount, 0);

    Header = TestInitializeLocalCompletionList(Buffer);
    Bitmap = TestCompletionBitmap(Header);
    Bitmap[0] = -1;
    Message = TestCompletionMessage(Header, 0);
    Message->Header.u1.s1.TotalLength = 1;
    AlpcFreeCompletionListMessage(Header, &Message->Header);
    trace("ALPC_OBSERVE completion_free short_total_length bitmap=%08lx return_count=%ld\n", (ULONG)Bitmap[0], Header->ReturnCount);
}

static
BOOLEAN
TestCompletionWorkers(
    _Inout_updates_bytes_(TEST_LOCAL_LIST_SIZE) PVOID Buffer)
{
    PTEST_COMPLETION_WORKER_CONTEXT Contexts;
    HANDLE Threads[TEST_WORKER_COUNT];
    PTEST_COMPLETION_LIST_HEADER Header;
    HANDLE Gate;
    DWORD WaitStatus = WAIT_OBJECT_0;
    DWORD JoinStatus;
    ULONG Created;
    ULONG Registered;
    ULONG Index;
    BOOLEAN Result;
    BOOLEAN Safe = TRUE;

    Contexts = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Contexts) * TEST_WORKER_COUNT);
    ok(Contexts != NULL, "completion worker context allocation failed\n");
    if (!Contexts)
        return TRUE;

    Header = TestInitializeLocalCompletionList(Buffer);
    Result = AlpcRegisterCompletionListWorkerThread(Header);
    ok_bool_true(Result, "first worker registration");
    Result = AlpcRegisterCompletionListWorkerThread(Header);
    ok_bool_true(Result, "repeated worker registration");
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 2);
    Result = AlpcUnregisterCompletionListWorkerThread(Header);
    ok_bool_true(Result, "first worker unregistration");
    Result = AlpcUnregisterCompletionListWorkerThread(Header);
    ok_bool_true(Result, "second worker unregistration");
    Result = AlpcUnregisterCompletionListWorkerThread(Header);
    ok_bool_false(Result, "worker count underflow prevention");
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 0);

    Header->State = (LONGLONG)TestCompletionState(TEST_COMPLETION_LIST_EMPTY, TEST_COMPLETION_LIST_EMPTY, 0xfffe);
    Result = AlpcRegisterCompletionListWorkerThread(Header);
    ok_bool_true(Result, "worker maximum boundary registration");
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 0xffff);
    Result = AlpcRegisterCompletionListWorkerThread(Header);
    ok_bool_false(Result, "worker maximum rejection");
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 0xffff);
    Result = AlpcUnregisterCompletionListWorkerThread(Header);
    ok_bool_true(Result, "worker maximum boundary unregistration");
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 0xfffe);

    Header->State = (LONGLONG)TestCompletionState(0, 0, 1);
    Result = AlpcUnregisterCompletionListWorkerThread(Header);
    ok_bool_false(Result, "worker unregistration with pending message");
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 1);

    Header->State = (LONGLONG)TestCompletionState(TEST_COMPLETION_LIST_EMPTY, 0, 1);
    Result = AlpcUnregisterCompletionListWorkerThread(Header);
    trace("ALPC_OBSERVE completion_worker corrupt_empty_head result=%u state=%I64x\n", Result, (ULONGLONG)Header->State);

    Header = TestInitializeLocalCompletionList(Buffer);
    Gate = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Gate != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Gate)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Contexts);
        return TRUE;
    }

    Created = 0;
    for (Index = 0; Index < TEST_WORKER_COUNT; ++Index)
    {
        Contexts[Index].CompletionList = Header;
        Contexts[Index].Gate = Gate;
        Contexts[Index].Register = TRUE;
        Contexts[Index].Result = FALSE;
        Threads[Index] = CreateThread(NULL, 0, TestCompletionWorkerThread, &Contexts[Index], 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread %lu failed: %lu\n", Index, GetLastError());
        if (!Threads[Index])
            break;
        ++Created;
    }
    SetEvent(Gate);
    if (Created)
    {
        WaitStatus = WaitForMultipleObjects(Created, Threads, TRUE, ALPC_TEST_TIMEOUT_MS);
        ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    }
    for (Index = 0; Index < Created; ++Index)
    {
        if (WaitStatus != WAIT_OBJECT_0)
        {
            JoinStatus = AlpcTestJoinThread(Threads[Index], NULL, NULL, "completion-list register worker");
            if (JoinStatus != WAIT_OBJECT_0)
                Safe = FALSE;
        }
        if (Safe)
            ok_bool_true(Contexts[Index].Result, "concurrent worker registration");
        CloseHandle(Threads[Index]);
    }
    if (!Safe)
    {
        trace("ALPC_OBSERVE thread completion register contexts_quarantined=%p gate=%p buffer=%p\n", Contexts, Gate, Buffer);
        return FALSE;
    }
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), Created);

    Registered = Created;
    ResetEvent(Gate);
    for (Index = 0; Index < Registered; ++Index)
    {
        Contexts[Index].Register = FALSE;
        Contexts[Index].Result = FALSE;
        Threads[Index] = CreateThread(NULL, 0, TestCompletionWorkerThread, &Contexts[Index], 0, NULL);
        ok(Threads[Index] != NULL, "CreateThread unregister %lu failed: %lu\n", Index, GetLastError());
        if (!Threads[Index])
            break;
    }
    Created = Index;
    SetEvent(Gate);
    if (Created)
    {
        WaitStatus = WaitForMultipleObjects(Created, Threads, TRUE, ALPC_TEST_TIMEOUT_MS);
        ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    }
    for (Index = 0; Index < Created; ++Index)
    {
        if (WaitStatus != WAIT_OBJECT_0)
        {
            JoinStatus = AlpcTestJoinThread(Threads[Index], NULL, NULL, "completion-list unregister worker");
            if (JoinStatus != WAIT_OBJECT_0)
                Safe = FALSE;
        }
        if (Safe)
            ok_bool_true(Contexts[Index].Result, "concurrent worker unregistration");
        CloseHandle(Threads[Index]);
    }
    if (!Safe)
    {
        trace("ALPC_OBSERVE thread completion unregister contexts_quarantined=%p gate=%p buffer=%p\n", Contexts, Gate, Buffer);
        return FALSE;
    }
    for (Index = Created; Index < Registered; ++Index)
    {
        Result = AlpcUnregisterCompletionListWorkerThread(Header);
        ok_bool_true(Result, "fallback worker unregistration");
    }
    ok_eq_ulong(TestCompletionActive((ULONGLONG)Header->State), 0);
    CloseHandle(Gate);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Contexts);
    return TRUE;
}

static
VOID
TestCompletionNullObservations(
    _Inout_updates_bytes_(TEST_LOCAL_LIST_SIZE) PVOID Buffer)
{
    PTEST_COMPLETION_LIST_HEADER Header;
    PALPC_TEST_MESSAGE Message;
    PALPC_MESSAGE_ATTRIBUTES Attributes = (PALPC_MESSAGE_ATTRIBUTES)(ULONG_PTR)0x55555555;
    PPORT_MESSAGE PortMessage = (PPORT_MESSAGE)(ULONG_PTR)0x55555555;
    ULONG Value = 0x55555555;
    BOOLEAN BooleanValue = 0x55;
    NTSTATUS ExceptionStatus;

    Header = TestInitializeLocalCompletionList(Buffer);
    Message = TestCompletionMessage(Header, 0);
    AlpcTestInitializeMessage(Message, 0x4E554C4C, 1);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        AlpcFreeCompletionListMessage(NULL, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null free_null exception=%08lx\n", ExceptionStatus);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        AlpcFreeCompletionListMessage(Header, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null free_message exception=%08lx return_count=%ld\n", ExceptionStatus, Header->ReturnCount);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        AlpcGetCompletionListLastMessageInformation(NULL, &Value, &Value);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null last_info_list exception=%08lx output=%08lx\n", ExceptionStatus, Value);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        AlpcGetCompletionListLastMessageInformation(Header, NULL, &Value);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null last_info_output exception=%08lx callback=%08lx\n", ExceptionStatus, Value);

    Header->AttributeFlags = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Attributes = AlpcGetCompletionListMessageAttributes(Header, NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null message_attributes exception=%08lx output=%p\n", ExceptionStatus, Attributes);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Attributes = AlpcGetCompletionListMessageAttributes(NULL, &Message->Header);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null attributes_list exception=%08lx output=%p\n", ExceptionStatus, Attributes);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        PortMessage = AlpcGetMessageFromCompletionList(NULL, &Attributes);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null dequeue exception=%08lx message=%p attributes=%p\n", ExceptionStatus, PortMessage, Attributes);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Value = AlpcGetOutstandingCompletionListMessageCount(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null outstanding exception=%08lx output=%08lx\n", ExceptionStatus, Value);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        BooleanValue = AlpcRegisterCompletionListWorkerThread(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null register_worker exception=%08lx output=%u\n", ExceptionStatus, BooleanValue);

    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        BooleanValue = AlpcUnregisterCompletionListWorkerThread(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_null unregister_worker exception=%08lx output=%u\n", ExceptionStatus, BooleanValue);
}

static
VOID
TestObserveRegistrationStatus(
    _In_ PCSTR Label,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Buffer)
{
    ULONGLONG FirstValue = 0;

    if (Buffer)
        FirstValue = *(UNALIGNED ULONGLONG *)Buffer;
    trace("ALPC_OBSERVE completion_registration %s status=%08lx first_value=%I64x\n", Label, Status, FirstValue);
    ok(Status != STATUS_NOT_IMPLEMENTED, "%s is still unimplemented\n", Label);
}

static
VOID
TestCompletionKernelEndToEnd(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestAlpcCompletionList");
    PTEST_COMPLETION_LIST_HEADER Header;
    PALPC_MESSAGE_ATTRIBUTES Attributes;
    ALPC_TEST_MESSAGE SendMessage;
    PPORT_MESSAGE ReceivedMessage;
    PVOID Buffer = NULL;
    PVOID SecondBuffer = NULL;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;
    NTSTATUS ExceptionStatus;
    ULONG LastMessageId;
    ULONG LastCallbackId;
    ULONG Outstanding;
    BOOLEAN WorkerRegistered = FALSE;
    BOOLEAN HeaderValid;
    BOOLEAN MessageValid;
    BOOLEAN Quarantine = FALSE;
    BOOLEAN SkipWrapperUnregister = FALSE;
    BOOLEAN Result;

    Buffer = TestAllocatePages(TEST_KERNEL_LIST_SIZE);
    SecondBuffer = TestAllocatePages(TEST_KERNEL_LIST_SIZE);
    ok(Buffer != NULL, "completion-list allocation failed\n");
    ok(SecondBuffer != NULL, "second completion-list allocation failed\n");
    if (!Buffer || !SecondBuffer)
        goto Cleanup;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = AlpcAdjustCompletionListConcurrencyCount(ServerPort, 1);
    TestObserveRegistrationStatus("adjust_before_register", Status, Buffer);

    RtlFillMemory(Buffer, TEST_KERNEL_LIST_SIZE, 0xa5);
    Status = AlpcRegisterCompletionList(ServerPort, (PUCHAR)Buffer + 1, TEST_KERNEL_LIST_SIZE - 1, 1, 0);
    TestObserveRegistrationStatus("register_misaligned_buffer", Status, Buffer);
    if (NT_SUCCESS(Status))
    {
        Status = AlpcUnregisterCompletionList(ServerPort);
        TestObserveRegistrationStatus("unregister_after_misaligned_buffer", Status, Buffer);
        if (!NT_SUCCESS(Status))
        {
            Quarantine = TRUE;
            goto Cleanup;
        }
    }

    RtlFillMemory(Buffer, TEST_KERNEL_LIST_SIZE, 0xa5);
    Status = AlpcRegisterCompletionList(ServerPort, Buffer, TEST_KERNEL_LIST_SIZE - 1, 1, 0);
    TestObserveRegistrationStatus("register_unaligned_size", Status, Buffer);
    if (NT_SUCCESS(Status))
    {
        Status = AlpcUnregisterCompletionList(ServerPort);
        TestObserveRegistrationStatus("unregister_after_unaligned_size", Status, Buffer);
        if (!NT_SUCCESS(Status))
        {
            Quarantine = TRUE;
            goto Cleanup;
        }
    }

    RtlFillMemory(Buffer, TEST_KERNEL_LIST_SIZE, 0xa5);
    Status = AlpcRegisterCompletionList(ServerPort, Buffer, 3 * PAGE_SIZE, 1, 0);
    TestObserveRegistrationStatus("register_undersized", Status, Buffer);
    if (NT_SUCCESS(Status))
    {
        Status = AlpcUnregisterCompletionList(ServerPort);
        TestObserveRegistrationStatus("unregister_after_undersized", Status, Buffer);
        if (!NT_SUCCESS(Status))
        {
            Quarantine = TRUE;
            goto Cleanup;
        }
    }

    RtlFillMemory(Buffer, TEST_KERNEL_LIST_SIZE, 0xa5);
    Status = AlpcRegisterCompletionList(ServerPort, Buffer, TEST_KERNEL_LIST_SIZE, 0, 0);
    TestObserveRegistrationStatus("register_zero_concurrency", Status, Buffer);
    if (NT_SUCCESS(Status))
    {
        Status = AlpcUnregisterCompletionList(ServerPort);
        TestObserveRegistrationStatus("unregister_after_zero_concurrency", Status, Buffer);
        if (!NT_SUCCESS(Status))
        {
            Quarantine = TRUE;
            goto Cleanup;
        }
    }

    RtlFillMemory(Buffer, TEST_KERNEL_LIST_SIZE, 0xa5);
    Status = AlpcRegisterCompletionList(ServerPort, Buffer, TEST_KERNEL_LIST_SIZE, 1, 1);
    TestObserveRegistrationStatus("register_unknown_attribute", Status, Buffer);
    if (NT_SUCCESS(Status))
    {
        Status = AlpcUnregisterCompletionList(ServerPort);
        TestObserveRegistrationStatus("unregister_after_unknown_attribute", Status, Buffer);
        if (!NT_SUCCESS(Status))
        {
            Quarantine = TRUE;
            goto Cleanup;
        }
    }

    Status = AlpcRegisterCompletionList((HANDLE)(ULONG_PTR)0xdeadbeef, Buffer, TEST_KERNEL_LIST_SIZE, 1, 0);
    TestObserveRegistrationStatus("register_invalid_handle", Status, Buffer);
    Status = STATUS_UNSUCCESSFUL;
    ExceptionStatus = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Status = AlpcRegisterCompletionList(ServerPort, NULL, TEST_KERNEL_LIST_SIZE, 1, 0);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ExceptionStatus = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    trace("ALPC_OBSERVE completion_registration register_null_buffer exception=%08lx\n", ExceptionStatus);
    TestObserveRegistrationStatus("register_null_buffer", Status, NULL);
    if (ExceptionStatus == STATUS_SUCCESS && NT_SUCCESS(Status))
    {
        Status = NtAlpcSetInformation(ServerPort, AlpcUnregisterCompletionListInformation, NULL, 0);
        TestObserveRegistrationStatus("unregister_after_null_buffer", Status, NULL);
        if (!NT_SUCCESS(Status))
        {
            Quarantine = TRUE;
            goto Cleanup;
        }
    }

    RtlFillMemory(Buffer, TEST_KERNEL_LIST_SIZE, 0xcc);
    Status = AlpcRegisterCompletionList(ServerPort, Buffer, TEST_KERNEL_LIST_SIZE, 1, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Header = Buffer;
    trace("ALPC_OBSERVE completion_header start=%I64x total=%lx list=%lx/%lx bitmap=%lx/%lx data=%lx/%lx attributes=%08lx/%lx state=%I64x post=%ld return=%ld lock=%p end=%I64x\n", Header->StartMagic, Header->TotalSize, Header->ListOffset, Header->ListSize, Header->BitmapOffset, Header->BitmapSize, Header->DataOffset, Header->DataSize, Header->AttributeFlags, Header->AttributeSize, (ULONGLONG)Header->State, Header->PostCount, Header->ReturnCount, Header->UserLock.Ptr, Header->EndMagic);
    HeaderValid = TestKernelCompletionHeaderValid(Header, TEST_KERNEL_LIST_SIZE);
    ok(HeaderValid, "kernel initialized an unsafe completion-list header\n");
    if (!HeaderValid)
    {
        Status = NtAlpcSetInformation(ServerPort, AlpcUnregisterCompletionListInformation, NULL, 0);
        TestObserveRegistrationStatus("unregister_after_invalid_header", Status, Buffer);
        if (!NT_SUCCESS(Status))
            Quarantine = TRUE;
        else
            SkipWrapperUnregister = TRUE;
        goto Cleanup;
    }
    ok_eq_ulong(Header->TotalSize, TEST_KERNEL_LIST_SIZE);
    ok(Header->ListOffset >= sizeof(*Header) && Header->ListOffset < Header->TotalSize, "invalid list offset %#lx\n", Header->ListOffset);
    ok(Header->ListSize != 0 && Header->ListOffset + Header->ListSize <= Header->TotalSize, "invalid list range %#lx/%#lx\n", Header->ListOffset, Header->ListSize);
    ok(Header->BitmapOffset >= Header->ListOffset + Header->ListSize && Header->BitmapOffset < Header->TotalSize, "invalid bitmap offset %#lx\n", Header->BitmapOffset);
    ok(Header->BitmapSize != 0 && Header->BitmapOffset + Header->BitmapSize <= Header->TotalSize, "invalid bitmap range %#lx/%#lx\n", Header->BitmapOffset, Header->BitmapSize);
    ok(Header->DataOffset >= Header->BitmapOffset + Header->BitmapSize && Header->DataOffset < Header->TotalSize, "invalid data offset %#lx\n", Header->DataOffset);
    ok(Header->DataSize >= TEST_COMPLETION_LIST_GRANULARITY && Header->DataOffset + Header->DataSize <= Header->TotalSize, "invalid data range %#lx/%#lx\n", Header->DataOffset, Header->DataSize);
    ok_eq_ulong(Header->AttributeFlags, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    ok(Header->AttributeSize >= sizeof(ALPC_MESSAGE_ATTRIBUTES), "attribute size is %#lx\n", Header->AttributeSize);
    ok_eq_ulong(TestCompletionHead((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);
    ok_eq_ulong(TestCompletionTail((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);
    ok(Header->UserLock.Ptr == NULL, "user lock is %p\n", Header->UserLock.Ptr);

    Status = AlpcRegisterCompletionList(ServerPort, SecondBuffer, TEST_KERNEL_LIST_SIZE, 1, 0);
    TestObserveRegistrationStatus("register_repeated", Status, SecondBuffer);
    if (NT_SUCCESS(Status))
    {
        Status = AlpcUnregisterCompletionList(ServerPort);
        TestObserveRegistrationStatus("unregister_after_repeated_success", Status, SecondBuffer);
        if (!NT_SUCCESS(Status))
            Quarantine = TRUE;
        goto Cleanup;
    }

    Outstanding = AlpcGetOutstandingCompletionListMessageCount(Buffer);
    ok_eq_ulong(Outstanding, 0);
    Attributes = (PALPC_MESSAGE_ATTRIBUTES)(ULONG_PTR)0x55555555;
    ReceivedMessage = AlpcGetMessageFromCompletionList(Buffer, &Attributes);
    ok(ReceivedMessage == NULL, "fresh kernel list returned %p\n", ReceivedMessage);
    ok(Attributes == NULL, "fresh kernel list attributes are %p\n", Attributes);

    Status = AlpcAdjustCompletionListConcurrencyCount(ServerPort, 0);
    TestObserveRegistrationStatus("adjust_zero", Status, Buffer);
    Status = AlpcAdjustCompletionListConcurrencyCount(ServerPort, 2);
    ok_hex(Status, STATUS_SUCCESS);

    AlpcTestInitializeMessage(&SendMessage, 0x434C4531, 101);
    Status = NtAlpcSendWaitReceivePort(ClientPort, ALPC_MSGFLG_REPLY_MESSAGE, &SendMessage.Header, NULL, NULL, NULL, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Rundown;

    Outstanding = AlpcGetOutstandingCompletionListMessageCount(Buffer);
    ok_eq_ulong(Outstanding, 1);
    Attributes = (PALPC_MESSAGE_ATTRIBUTES)(ULONG_PTR)0x55555555;
    ReceivedMessage = AlpcGetMessageFromCompletionList(Buffer, &Attributes);
    ok(ReceivedMessage != NULL, "kernel completion list did not return the sent message\n");
    if (ReceivedMessage)
    {
        PALPC_TEST_MESSAGE Received = CONTAINING_RECORD(ReceivedMessage, ALPC_TEST_MESSAGE, Header);

        MessageValid = TestKernelCompletionMessageValid(Header, ReceivedMessage, Attributes);
        ok(MessageValid, "kernel completion list returned unsafe message=%p attributes=%p\n", ReceivedMessage, Attributes);
        if (!MessageValid)
            goto Rundown;

        ok_eq_ulong(Received->Cookie, 0x434C4531);
        ok_eq_ulong(Received->Value, 101);
        ok_eq_ulong(Received->Header.u1.s1.TotalLength, sizeof(*Received));
        ok(Attributes == AlpcGetCompletionListMessageAttributes(Buffer, ReceivedMessage), "returned attributes %p disagree with helper\n", Attributes);
        if (Attributes)
            trace("ALPC_OBSERVE completion_message attributes allocated=%08lx valid=%08lx address=%p\n", Attributes->AllocatedAttributes, Attributes->ValidAttributes, Attributes);
        LastMessageId = 0;
        LastCallbackId = 0;
        AlpcGetCompletionListLastMessageInformation(Buffer, &LastMessageId, &LastCallbackId);
        ok_eq_ulong(LastMessageId, Received->Header.MessageId);
        ok_eq_ulong(LastCallbackId, Received->Header.CallbackId);
        AlpcFreeCompletionListMessage(Buffer, ReceivedMessage);
        ok_eq_ulong(AlpcGetOutstandingCompletionListMessageCount(Buffer), 0);
    }

    Result = AlpcRegisterCompletionListWorkerThread(Buffer);
    ok_bool_true(Result, "kernel-list worker registration");
    WorkerRegistered = Result;
    Status = AlpcUnregisterCompletionList(ServerPort);
    TestObserveRegistrationStatus("unregister_with_worker", Status, Buffer);
    if (NT_SUCCESS(Status))
    {
        WorkerRegistered = FALSE;
        goto Cleanup;
    }

    AlpcTestInitializeMessage(&SendMessage, 0x434C4532, 202);
    Status = NtAlpcSendWaitReceivePort(ClientPort, ALPC_MSGFLG_REPLY_MESSAGE, &SendMessage.Header, NULL, NULL, NULL, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok_eq_ulong(AlpcGetOutstandingCompletionListMessageCount(Buffer), 1);

Rundown:
    Status = AlpcRundownCompletionList(ServerPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong(AlpcGetOutstandingCompletionListMessageCount(Buffer), 0);
        ReceivedMessage = AlpcGetMessageFromCompletionList(Buffer, NULL);
        ok(ReceivedMessage == NULL, "rundown list returned %p\n", ReceivedMessage);
        ok_eq_ulong(TestCompletionHead((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);
        ok_eq_ulong(TestCompletionTail((ULONGLONG)Header->State), TEST_COMPLETION_LIST_EMPTY);
    }
    Status = AlpcRundownCompletionList(ServerPort);
    TestObserveRegistrationStatus("rundown_repeated", Status, Buffer);

    if (WorkerRegistered)
    {
        Result = AlpcUnregisterCompletionListWorkerThread(Buffer);
        ok_bool_true(Result, "kernel-list worker unregistration");
        WorkerRegistered = FALSE;
    }
    Status = AlpcUnregisterCompletionList(ServerPort);
    ok_hex(Status, STATUS_SUCCESS);
    Status = AlpcUnregisterCompletionList(ServerPort);
    TestObserveRegistrationStatus("unregister_repeated", Status, Buffer);
    Status = AlpcRundownCompletionList(ServerPort);
    TestObserveRegistrationStatus("rundown_after_unregister", Status, Buffer);
    Status = AlpcAdjustCompletionListConcurrencyCount(ServerPort, 1);
    TestObserveRegistrationStatus("adjust_after_unregister", Status, Buffer);

Cleanup:
    if (Quarantine)
    {
        trace("ALPC_OBSERVE completion_kernel_quarantine list=%p second=%p ports=%p/%p/%p\n", Buffer, SecondBuffer, ConnectionPort, ServerPort, ClientPort);
        return;
    }
    if (WorkerRegistered)
        AlpcUnregisterCompletionListWorkerThread(Buffer);
    if (ServerPort && !SkipWrapperUnregister)
        AlpcUnregisterCompletionList(ServerPort);
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
    TestFreePages(SecondBuffer);
    TestFreePages(Buffer);
}

static
BOOLEAN
TestCompletionRunChildMode(VOID)
{
    PVOID Buffer;
    BOOLEAN BufferSafe = TRUE;
    enum
    {
        CompletionChildNone,
        CompletionChildDequeue,
        CompletionChildFree,
        CompletionChildWorkers,
        CompletionChildNulls,
        CompletionChildKernel
    } ChildMode = CompletionChildNone;

    if (AlpcTestIsChildMode("completion-dequeue"))
        ChildMode = CompletionChildDequeue;
    else if (AlpcTestIsChildMode("completion-free"))
        ChildMode = CompletionChildFree;
    else if (AlpcTestIsChildMode("completion-workers"))
        ChildMode = CompletionChildWorkers;
    else if (AlpcTestIsChildMode("completion-nulls"))
        ChildMode = CompletionChildNulls;
    else if (AlpcTestIsChildMode("completion-kernel"))
        ChildMode = CompletionChildKernel;
    if (ChildMode == CompletionChildNone)
        return FALSE;

    if (ChildMode == CompletionChildKernel)
    {
        TestCompletionKernelEndToEnd();
        return TRUE;
    }

    Buffer = TestAllocatePages(TEST_LOCAL_LIST_SIZE);
    ok(Buffer != NULL, "isolated local completion-list allocation failed\n");
    if (!Buffer)
        return TRUE;

    if (ChildMode == CompletionChildDequeue)
        TestCompletionDequeue(Buffer);
    else if (ChildMode == CompletionChildFree)
        TestCompletionFree(Buffer);
    else if (ChildMode == CompletionChildWorkers)
        BufferSafe = TestCompletionWorkers(Buffer);
    else
        TestCompletionNullObservations(Buffer);

    if (BufferSafe)
        TestFreePages(Buffer);
    else
        skip("isolated completion-list state quarantined after a hung worker\n");
    return TRUE;
}

START_TEST(AlpcCompletionList)
{
    PVOID Buffer;

    if (TestCompletionRunChildMode())
        return;

    Buffer = TestAllocatePages(TEST_LOCAL_LIST_SIZE);
    ok(Buffer != NULL, "local completion-list allocation failed\n");
    if (!Buffer)
        return;

    TestCompletionMetadata(Buffer);
    TestFreePages(Buffer);

    AlpcTestRunIsolatedCase(L"AlpcCompletionList", L"completion-dequeue", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestRunIsolatedCase(L"AlpcCompletionList", L"completion-free", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestRunIsolatedCase(L"AlpcCompletionList", L"completion-workers", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestRunIsolatedCase(L"AlpcCompletionList", L"completion-nulls", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestRunIsolatedCase(L"AlpcCompletionList", L"completion-kernel", ALPC_TEST_CHILD_TIMEOUT_MS);
}
