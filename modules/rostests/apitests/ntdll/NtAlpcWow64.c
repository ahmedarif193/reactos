/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exercise ALPC pointer-sized outputs and WoW64 argument conversion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include "alpc_test_utils.h"
#include <pseh/pseh2.h>

#define ALPC_GUARD 0x76543210

typedef struct
{
    PORT_MESSAGE Header;
    ULONG Cookie;
} THUNK_MESSAGE;

typedef struct
{
    UNICODE_STRING Name;
    NTSTATUS ConnectStatus;
    NTSTATUS SendStatus;
    ULONG Reply;
} THUNK_CLIENT;

static VOID InitMessage(THUNK_MESSAGE *Message, ULONG Cookie)
{
    RtlZeroMemory(Message, sizeof(*Message));
    Message->Header.u1.s1.DataLength = sizeof(*Message) - sizeof(Message->Header);
    Message->Header.u1.s1.TotalLength = sizeof(*Message);
    Message->Cookie = Cookie;
}

static VOID TestResources(VOID)
{
    HANDLE Port, Completion;
    NTSTATUS Status;
    ALPC_PORT_ATTRIBUTES Attributes = {0};
    ALPC_PORT_ASSOCIATE_COMPLETION_PORT Associate;
    ALPC_PORT_MESSAGE_ZONE_INFORMATION Zone;
    ALPC_CONTEXT_ATTR Context = {0};
    ALPC_SECURITY_ATTR Security = {0};
    SECURITY_QUALITY_OF_SERVICE Qos = {sizeof(Qos), SecurityImpersonation, SECURITY_DYNAMIC_TRACKING, FALSE};
    struct { ALPC_BASIC_INFORMATION Value; ULONG Guard; } Basic;
    struct { HANDLE Value; ULONG Guard; } Section;
    struct { SIZE_T Value; ULONG Guard; } Size;
    struct { ULONG Value; ULONG Guard; } Reserve;
    struct { ALPC_DATA_VIEW_ATTR Value; ULONG Guard; } View;
    ULONG Length, Index;

    AlpcTestInitializePortAttributes(&Attributes, 0);
    Attributes.MaxMessageLength = sizeof(THUNK_MESSAGE);
    Status = NtCreateIoCompletion(&Completion, IO_COMPLETION_ALL_ACCESS, NULL, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    for (Index = 0; Index < 32; ++Index)
    {
        Port = NULL;
        Status = NtAlpcCreatePort(&Port, NULL, &Attributes);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) break;

        RtlFillMemory(&Basic, sizeof(Basic), 0x55);
        Basic.Guard = ALPC_GUARD;
        Length = 0x55555555;
        Status = NtAlpcQueryInformation(Port, AlpcBasicInformation, &Basic.Value, sizeof(Basic.Value) - 1, &Length);
        ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
        ok_eq_ulong(Length, sizeof(Basic.Value));
        ok_eq_hex(Basic.Value.Flags, 0x55555555);
        ok_eq_hex(Basic.Guard, ALPC_GUARD);
        Status = NtAlpcQueryInformation(Port, AlpcBasicInformation, &Basic.Value, sizeof(Basic.Value), &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Length, sizeof(Basic.Value));
        ok(Basic.Value.PortContext == NULL, "Unexpected port context %p\n", Basic.Value.PortContext);
        ok_eq_hex(Basic.Guard, ALPC_GUARD);

        Attributes.Flags = ALPC_PORFLG_ALLOW_LPC_REQUESTS;
        Status = NtAlpcSetInformation(Port, AlpcPortInformation, &Attributes, sizeof(Attributes));
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = NtAlpcQueryInformation(Port, AlpcBasicInformation, &Basic.Value, sizeof(Basic.Value), NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok((Basic.Value.Flags & ALPC_PORFLG_ALLOW_LPC_REQUESTS) != 0, "Port flag was not set\n");
        Attributes.Flags = 0;

        Associate.CompletionKey = (PVOID)(ULONG_PTR)0x12345678;
        Associate.CompletionPort = Completion;
        Status = NtAlpcSetInformation(Port, AlpcAssociateCompletionPortInformation, &Associate, sizeof(Associate));
        ok_eq_hex(Status, STATUS_SUCCESS);
        Zone.Buffer = NULL;
        Zone.Size = 0;
        Status = NtAlpcSetInformation(Port, AlpcMessageZoneInformation, &Zone, sizeof(Zone));
        ok_eq_hex(Status, STATUS_SUCCESS);

        Reserve.Value = 0;
        Reserve.Guard = ALPC_GUARD;
        Status = NtAlpcCreateResourceReserve(Port, 0, sizeof(PORT_MESSAGE), &Reserve.Value);
        /* WoW64 preserves this allocation size, including the native header minimum. */
        ok_eq_hex(Status, sizeof(PORT_MESSAGE) < 40 ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS);
        ok_eq_hex(Reserve.Guard, ALPC_GUARD);
        if (NT_SUCCESS(Status))
        {
            Status = NtAlpcDeleteResourceReserve(Port, 0, Reserve.Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }
        Status = NtAlpcCreateResourceReserve(Port, 0, 0x100, &Reserve.Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Reserve.Guard, ALPC_GUARD);
        if (NT_SUCCESS(Status))
        {
            Status = NtAlpcDeleteResourceReserve(Port, 0, Reserve.Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }

        Section.Value = NULL;
        Section.Guard = ALPC_GUARD;
        Size.Value = 0;
        Size.Guard = ALPC_GUARD;
        Status = NtAlpcCreatePortSection(Port, 0, NULL, 0x1001, &Section.Value, &Size.Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Section.Guard, ALPC_GUARD);
        ok_eq_hex(Size.Guard, ALPC_GUARD);
        if (NT_SUCCESS(Status))
        {
            ok(Size.Value >= 0x1001, "Section is too small: %I64u\n", (ULONGLONG)Size.Value);
            RtlZeroMemory(&View, sizeof(View));
            View.Guard = ALPC_GUARD;
            View.Value.SectionHandle = Section.Value;
            View.Value.ViewSize = Size.Value;
            Status = NtAlpcCreateSectionView(Port, 0, &View.Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_hex(View.Guard, ALPC_GUARD);
            if (NT_SUCCESS(Status))
            {
                ok(View.Value.ViewBase != NULL, "No section mapping\n");
                ok_eq_ulong(View.Value.ViewSize, Size.Value);
                RtlFillMemory(View.Value.ViewBase, View.Value.ViewSize, 0x5a);
                ok_eq_hex(((PUCHAR)View.Value.ViewBase)[View.Value.ViewSize - 1], 0x5a);
                Status = NtAlpcDeleteSectionView(Port, 0, View.Value.ViewBase);
                ok_eq_hex(Status, STATUS_SUCCESS);
            }
            Status = NtAlpcDeletePortSection(Port, 0, Section.Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }

        Security.Flags = ALPC_SECFLG_CREATE_HANDLE;
        Security.QoS = &Qos;
        Security.ContextHandle = NULL;
        Status = NtAlpcCreateSecurityContext(Port, 0, &Security);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok(Security.ContextHandle != NULL, "No security context handle\n");
            Status = NtAlpcRevokeSecurityContext(Port, 0, Security.ContextHandle);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = NtAlpcDeleteSecurityContext(Port, 0, Security.ContextHandle);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }

        Status = NtAlpcCancelMessage(Port, 0x80000000, &Context);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        NtClose(Port);
    }
    NtClose(Completion);
}

static DWORD WINAPI ClientThread(PVOID Parameter)
{
    THUNK_CLIENT *Client = Parameter;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_PORT_ATTRIBUTES Attributes = {0};
    THUNK_MESSAGE Message, Reply;
    HANDLE Port = NULL;
    SIZE_T Length = sizeof(Message);
    LARGE_INTEGER Timeout;

    InitializeObjectAttributes(&ObjectAttributes, &Client->Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    AlpcTestInitializePortAttributes(&Attributes, 0);
    Attributes.MaxMessageLength = sizeof(THUNK_MESSAGE);
    InitMessage(&Message, 0x11223344);
    Timeout.QuadPart = -10000000;
    Client->ConnectStatus = NtAlpcConnectPortEx(&Port, &ObjectAttributes, NULL, &Attributes, ALPC_SYNC_CONNECTION, NULL, &Message.Header, &Length, NULL, NULL, &Timeout);
    if (Client->ConnectStatus != STATUS_SUCCESS) return 0;
    InitMessage(&Message, 0xaabbccdd);
    RtlZeroMemory(&Reply, sizeof(Reply));
    Length = sizeof(Reply);
    Client->SendStatus = NtAlpcSendWaitReceivePort(Port, ALPC_MSGFLG_SYNC_REQUEST, &Message.Header, NULL, &Reply.Header, &Length, NULL, &Timeout);
    Client->Reply = Reply.Cookie;
    NtClose(Port);
    return 0;
}

static VOID TestConnection(VOID)
{
    THUNK_CLIENT Client;
    WCHAR Name[100];
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_PORT_ATTRIBUTES Attributes = {0};
    THUNK_MESSAGE Message;
    LARGE_INTEGER Timeout;
    HANDLE Port = NULL, Communication = NULL, Thread, Sender;
    NTSTATUS Status;
    SIZE_T Length;
    ULONG ReturnLength, RequiredLength, ThreadId, WaitStatus;
    struct { LUID Value; ULONG Guard; } Token;
    union { ALPC_SERVER_INFORMATION Info; UCHAR Buffer[512]; } Server, Before;
    ULONG_PTR Cookie = 0x12345678;

    RtlZeroMemory(&Client, sizeof(Client));
    StringCchPrintfW(Name, RTL_NUMBER_OF(Name), L"\\RPC Control\\AlpcThunk_%lu_%lu_%lu", GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount());
    RtlInitUnicodeString(&Client.Name, Name);
    InitializeObjectAttributes(&ObjectAttributes, &Client.Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    AlpcTestInitializePortAttributes(&Attributes, 0);
    Attributes.MaxMessageLength = sizeof(THUNK_MESSAGE);
    Status = NtAlpcCreatePort(&Port, &ObjectAttributes, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Thread = CreateThread(NULL, 0, ClientThread, &Client, 0, &ThreadId);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread) goto Cleanup;

    Timeout.QuadPart = -10000000;
    Length = sizeof(Message);
    Status = NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, &Message.Header, &Length, NULL, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Wait;
    ok_eq_hex(Message.Cookie, 0x11223344);
    ok_eq_hex(Message.Header.u2.s2.Type & 0x1000, sizeof(void *) == 4 ? 0x1000 : 0);
    Status = NtAlpcAcceptConnectPort(&Communication, Port, 0, NULL, &Attributes, (PVOID)Cookie, &Message.Header, NULL, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Wait;

    Length = sizeof(Message);
    Status = NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, &Message.Header, &Length, NULL, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Wait;
    ok_eq_hex(Message.Cookie, 0xaabbccdd);
    ok_eq_hex(Message.Header.u2.s2.Type & 0x1000, sizeof(void *) == 4 ? 0x1000 : 0);
    ok_eq_ulong((ULONG_PTR)Message.Header.ClientId.UniqueProcess, GetCurrentProcessId());
    ok_eq_ulong((ULONG_PTR)Message.Header.ClientId.UniqueThread, ThreadId);

    RtlFillMemory(&Server, sizeof(Server), 0x55);
    Server.Info.In.ThreadHandle = Thread;
    Before = Server;
    ReturnLength = 0x55555555;
    Status = NtAlpcQueryInformation(NULL, AlpcServerInformation, &Server, sizeof(HANDLE), &ReturnLength);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(!memcmp(&Before, &Server, sizeof(Server)), "Short query modified the caller buffer\n");
    ok(ReturnLength > sizeof(Server.Info) && ReturnLength < sizeof(Server), "Invalid required length %lu\n", ReturnLength);
    RequiredLength = ReturnLength;

    RtlFillMemory(&Server, sizeof(Server), 0x55);
    Server.Info.In.ThreadHandle = Thread;
    Status = NtAlpcQueryInformation(NULL, AlpcServerInformation, &Server, min(RequiredLength, sizeof(Server)), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok(Server.Info.Out.ThreadBlocked, "Client was not reported blocked\n");
        ok_eq_ulong((ULONG_PTR)Server.Info.Out.ConnectedProcessId, GetCurrentProcessId());
        ok(ReturnLength >= sizeof(Server.Info) && ReturnLength <= sizeof(Server), "Invalid return length %lu\n", ReturnLength);
        ok_eq_ulong(ReturnLength, RequiredLength);
        if (RequiredLength < sizeof(Server)) ok_eq_hex(Server.Buffer[RequiredLength], 0x55);
        ok(Server.Info.Out.ConnectionPortName.Buffer >= (PWSTR)Server.Buffer && (PUCHAR)Server.Info.Out.ConnectionPortName.Buffer + Server.Info.Out.ConnectionPortName.MaximumLength <= Server.Buffer + sizeof(Server), "Name pointer %p lies outside caller buffer\n", Server.Info.Out.ConnectionPortName.Buffer);
        ok_eq_ulong(Server.Info.Out.ConnectionPortName.Length, Client.Name.Length);
        if (Server.Info.Out.ConnectionPortName.Length == Client.Name.Length)
            ok(!memcmp(Server.Info.Out.ConnectionPortName.Buffer, Client.Name.Buffer, Client.Name.Length), "Wrong connection port name\n");
    }

    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);
    Status = NtAlpcOpenSenderProcess(&Sender, Communication, &Message.Header, 0, PROCESS_QUERY_INFORMATION, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok_eq_ulong(GetProcessId(Sender), GetCurrentProcessId());
        NtClose(Sender);
    }
    Status = NtAlpcOpenSenderThread(&Sender, Communication, &Message.Header, 0, THREAD_QUERY_INFORMATION, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status == STATUS_SUCCESS)
    {
        ok_eq_ulong(GetThreadId(Sender), ThreadId);
        NtClose(Sender);
    }
    RtlFillMemory(&Token, sizeof(Token), 0x55);
    Token.Guard = ALPC_GUARD;
    ReturnLength = ALPC_GUARD;
    /* Message information is queried on the listening port. */
    Status = NtAlpcQueryInformationMessage(Port, &Message.Header, AlpcMessageTokenModifiedIdInformation, &Token.Value, sizeof(Token.Value), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(Token.Value));
    ok_eq_hex(Token.Guard, ALPC_GUARD);
    Status = NtAlpcImpersonateClientContainerOfPort(Communication, &Message.Header, 1);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Message.Cookie = 0xdeadbeef;
    Status = NtAlpcSendWaitReceivePort(Communication, ALPC_MSGFLG_REPLY_MESSAGE, &Message.Header, NULL, NULL, NULL, NULL, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

Wait:
    WaitStatus = WaitForSingleObject(Thread, 3000);
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus != WAIT_OBJECT_0)
    {
        /* Never let a failed worker outlive the stack containing its context. */
        TerminateProcess(GetCurrentProcess(), 1);
        return;
    }
    ok_eq_hex(Client.ConnectStatus, STATUS_SUCCESS);
    ok_eq_hex(Client.SendStatus, STATUS_SUCCESS);
    ok_eq_hex(Client.Reply, 0xdeadbeef);
    NtClose(Thread);
Cleanup:
    if (Communication) NtClose(Communication);
    NtClose(Port);
}


/* The boundary expectations were measured on Windows 11 26100.1742, including
 * its different native and WoW64 information-buffer length rules. */
static VOID TestBoundaries(VOID)
{
    static const SIZE_T ReserveSizes[] = {0, 1, 23, 24, 39, 40, 41, 0xffd7, 0xffd8, MAXULONG};
    ALPC_PORT_ATTRIBUTES Attributes;
    ALPC_PORT_MESSAGE_ZONE_INFORMATION Zone = {0};
    union { ALPC_BASIC_INFORMATION Basic; UCHAR Bytes[80]; } Buffer, Before;
    struct { ULONG Value; ULONG Guard; } Reserve;
    HANDLE Port;
    ULONG Length, Returned, Index;
    NTSTATUS Status, Expected;

    AlpcTestInitializePortAttributes(&Attributes, 0);
    Status = NtAlpcCreatePort(&Port, NULL, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) return;

    for (Length = 0; Length <= sizeof(Buffer.Basic) + 16; ++Length)
    {
        RtlFillMemory(&Buffer, sizeof(Buffer), 0x55);
        Before = Buffer;
        Returned = ALPC_GUARD;
        Status = NtAlpcQueryInformation(Port, AlpcBasicInformation, &Buffer, Length, &Returned);
        Expected = Length < sizeof(Buffer.Basic) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
        ok(Status == Expected, "Basic length=%lu status=%08lx expected=%08lx\n", Length, Status, Expected);
        ok_eq_ulong(Returned, sizeof(Buffer.Basic));
        if (Status != STATUS_SUCCESS)
            ok(!memcmp(&Buffer, &Before, sizeof(Buffer)), "Basic short length %lu modified output\n", Length);
        else
            ok(!memcmp(Buffer.Bytes + sizeof(Buffer.Basic), Before.Bytes + sizeof(Buffer.Basic), sizeof(Buffer) - sizeof(Buffer.Basic)), "Basic length %lu wrote beyond structure\n", Length);
    }

    for (Length = 0; Length <= sizeof(Attributes) + 8; ++Length)
    {
        Expected = (Length == sizeof(Attributes) || (sizeof(void *) == 4 && Length > sizeof(Attributes))) ? STATUS_SUCCESS : STATUS_INFO_LENGTH_MISMATCH;
        Status = NtAlpcSetInformation(Port, AlpcPortInformation, &Attributes, Length);
        ok(Status == Expected, "Set port length=%lu status=%08lx expected=%08lx\n", Length, Status, Expected);
    }
    for (Length = 0; Length <= sizeof(Zone) + 8; ++Length)
    {
        if (sizeof(void *) == 4)
            Expected = Length < sizeof(Zone) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
        else
            Expected = Length == sizeof(Zone) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
        Status = NtAlpcSetInformation(Port, AlpcMessageZoneInformation, &Zone, Length);
        ok(Status == Expected, "Set zone length=%lu status=%08lx expected=%08lx\n", Length, Status, Expected);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(ReserveSizes); ++Index)
    {
        Reserve.Value = Reserve.Guard = ALPC_GUARD;
        Expected = ReserveSizes[Index] < 40 ? STATUS_INVALID_PARAMETER : (ReserveSizes[Index] > 0xffd7 ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS);
        Status = NtAlpcCreateResourceReserve(Port, 0, ReserveSizes[Index], &Reserve.Value);
        ok(Status == Expected, "Reserve size=%I64u status=%08lx expected=%08lx\n", (ULONGLONG)ReserveSizes[Index], Status, Expected);
        ok_eq_hex(Reserve.Guard, ALPC_GUARD);
        if (Status == STATUS_SUCCESS)
        {
            Status = NtAlpcDeleteResourceReserve(Port, 0, Reserve.Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = NtAlpcDeleteResourceReserve(Port, 0, Reserve.Value);
            ok_eq_hex(Status, STATUS_INVALID_HANDLE);
        }
        else
            ok_eq_hex(Reserve.Value, ALPC_GUARD);
    }
    NtClose(Port);
}

static VOID TestInvalidHandles(VOID)
{
    HANDLE Event, Closed, Handles[2];
    HANDLE Output;
    SIZE_T Actual;
    ULONG Index, Reserve, Returned;
    NTSTATUS Status, Expected;
    ALPC_PORT_ATTRIBUTES Attributes;
    ALPC_BASIC_INFORMATION Basic;
    ALPC_CONTEXT_ATTR Context = {0};
    ALPC_DATA_VIEW_ATTR View = {0};
    ALPC_SECURITY_ATTR Security = {0};
    SECURITY_QUALITY_OF_SERVICE Qos = {sizeof(Qos), SecurityImpersonation, SECURITY_DYNAMIC_TRACKING, FALSE};
    OBJECT_ATTRIBUTES ObjectAttributes;
    THUNK_MESSAGE Message;
    LUID Token;

    Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    Closed = CreateEventW(NULL, FALSE, FALSE, NULL);
    ok(Event != NULL && Closed != NULL, "Event creation failed\n");
    if (!Event || !Closed)
    {
        if (Event) NtClose(Event);
        if (Closed) NtClose(Closed);
        return;
    }
    NtClose(Closed);
    Handles[0] = Event;
    Handles[1] = Closed;
    AlpcTestInitializePortAttributes(&Attributes, 0);
    Security.QoS = &Qos;
    Security.Flags = ALPC_SECFLG_CREATE_HANDLE;
    View.ViewSize = PAGE_SIZE;
    InitMessage(&Message, 0);
    Message.Header.MessageId = 1;
    Context.MessageId = 1;
    InitializeObjectAttributes(&ObjectAttributes, NULL, 0, NULL, NULL);

#define CHECK_BAD_HANDLE(call) do { Status = (call); ok(Status == Expected, "%s row=%lu status=%08lx expected=%08lx\n", #call, Index, Status, Expected); } while (0)
    for (Index = 0; Index < RTL_NUMBER_OF(Handles); ++Index)
    {
        Expected = Index == 0 ? STATUS_OBJECT_TYPE_MISMATCH : STATUS_INVALID_HANDLE;
        CHECK_BAD_HANDLE(NtAlpcCreatePortSection(Handles[Index], 0, NULL, PAGE_SIZE, &Output, &Actual));
        CHECK_BAD_HANDLE(NtAlpcDeletePortSection(Handles[Index], 0, NULL));
        CHECK_BAD_HANDLE(NtAlpcCreateResourceReserve(Handles[Index], 0, 0x100, &Reserve));
        CHECK_BAD_HANDLE(NtAlpcDeleteResourceReserve(Handles[Index], 0, 0x80000010));
        CHECK_BAD_HANDLE(NtAlpcCreateSectionView(Handles[Index], 0, &View));
        CHECK_BAD_HANDLE(NtAlpcDeleteSectionView(Handles[Index], 0, NULL));
        CHECK_BAD_HANDLE(NtAlpcCreateSecurityContext(Handles[Index], 0, &Security));
        CHECK_BAD_HANDLE(NtAlpcDeleteSecurityContext(Handles[Index], 0, NULL));
        CHECK_BAD_HANDLE(NtAlpcRevokeSecurityContext(Handles[Index], 0, NULL));
        CHECK_BAD_HANDLE(NtAlpcCancelMessage(Handles[Index], 0, &Context));
        CHECK_BAD_HANDLE(NtAlpcQueryInformation(Handles[Index], AlpcBasicInformation, &Basic, sizeof(Basic), &Returned));
        CHECK_BAD_HANDLE(NtAlpcSetInformation(Handles[Index], AlpcPortInformation, &Attributes, sizeof(Attributes)));
        /* An untagged synthetic 32-bit header is rejected before handle lookup. */
        Status = NtAlpcQueryInformationMessage(Handles[Index], &Message.Header, AlpcMessageTokenModifiedIdInformation, &Token, sizeof(Token), &Returned);
        ok_eq_hex(Status, sizeof(void *) == 4 ? STATUS_INVALID_PARAMETER : Expected);
        CHECK_BAD_HANDLE(NtAlpcOpenSenderProcess(&Output, Handles[Index], &Message.Header, 0, PROCESS_QUERY_INFORMATION, &ObjectAttributes));
        CHECK_BAD_HANDLE(NtAlpcOpenSenderThread(&Output, Handles[Index], &Message.Header, 0, THREAD_QUERY_INFORMATION, &ObjectAttributes));
    }
#undef CHECK_BAD_HANDLE
    NtClose(Event);
}

static __declspec(noinline) VOID WriteProtectedWord(PVOID Address)
{
    *(volatile ULONG *)Address = 0x12345678;
}

static VOID TestGuardPages(VOID)
{
    PUCHAR Pages;
    PALPC_BASIC_INFORMATION Basic;
    ALPC_PORT_ATTRIBUTES Attributes;
    HANDLE Port;
    ULONG Returned;
    DWORD OldProtect;
    MEMORY_BASIC_INFORMATION Memory;
    NTSTATUS Status;
    BOOLEAN Exception;

    AlpcTestInitializePortAttributes(&Attributes, 0);
    Status = NtAlpcCreatePort(&Port, NULL, &Attributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) return;
    Pages = VirtualAlloc(NULL, 2 * PAGE_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok(Pages != NULL, "Guard allocation failed\n");
    if (!Pages) goto Cleanup;
    if (!VirtualProtect(Pages + PAGE_SIZE, PAGE_SIZE, PAGE_NOACCESS, &OldProtect))
    {
        ok(0, "Cannot protect guard page: %lu\n", GetLastError());
        goto Cleanup;
    }
    Basic = (PALPC_BASIC_INFORMATION)(Pages + PAGE_SIZE - sizeof(*Basic));
    RtlFillMemory(Pages, PAGE_SIZE, 0x55);
    Status = NtAlpcQueryInformation(Port, AlpcBasicInformation, Basic, sizeof(*Basic), &Returned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Returned, sizeof(*Basic));
    ok_eq_hex(((PUCHAR)Basic)[-1], 0x55);

    Exception = FALSE;
    _SEH2_TRY
    {
        Status = NtAlpcQueryInformation(Port, AlpcBasicInformation, Pages + PAGE_SIZE, sizeof(*Basic), &Returned);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Exception = TRUE;
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    ok(!Exception, "Query raised an exception instead of returning status\n");

    Exception = FALSE;
    _SEH2_TRY
    {
        Status = NtAlpcSetInformation(Port, AlpcPortInformation, Pages + PAGE_SIZE, sizeof(Attributes));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Exception = TRUE;
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    ok(!Exception, "Set information raised an exception instead of returning status\n");

    if (VirtualProtect(Pages, PAGE_SIZE, PAGE_READONLY, &OldProtect))
    {
        ok_eq_size(VirtualQuery(Pages, &Memory, sizeof(Memory)), sizeof(Memory));
        ok_eq_hex(Memory.Protect, PAGE_READONLY);
        Exception = FALSE;
        _SEH2_TRY
        {
            Status = NtAlpcQueryInformation(Port, AlpcBasicInformation, Basic, sizeof(*Basic), &Returned);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Exception = TRUE;
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        ok(!Exception, "Read-only output raised an exception instead of returning status\n");
        Exception = FALSE;
        _SEH2_TRY
        {
            WriteProtectedWord(Pages);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Exception = TRUE;
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        ok(Exception, "Direct write to read-only page succeeded\n");
        if (Exception) ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
        ok_eq_hex(*(volatile ULONG *)Pages, 0x55555555);
        ok_eq_size(VirtualQuery(Pages, &Memory, sizeof(Memory)), sizeof(Memory));
        ok_eq_hex(Memory.Protect, PAGE_READONLY);
    }
    else
        ok(0, "Cannot make output read-only: %lu\n", GetLastError());
Cleanup:
    if (Pages) VirtualFree(Pages, 0, MEM_RELEASE);
    NtClose(Port);
}

static VOID TestMessageLayouts(VOID)
{
    static const USHORT Types[] = {0, 1, 0x1000, 0x1001, 0x2000, 0x8000};
    union { PORT_MESSAGE Header; BYTE Bytes[64]; } Message;
    HANDLE Event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ULONG Index, Id, Tail, Returned;
    NTSTATUS Status, Expected;
    LUID Token;

    ok(Event != NULL, "Event creation failed\n");
    if (!Event) return;
    for (Index = 0; Index < RTL_NUMBER_OF(Types); ++Index)
    for (Id = 0; Id < 2; ++Id)
    for (Tail = 0; Tail < 2; ++Tail)
    {
        RtlZeroMemory(&Message, sizeof(Message));
        Message.Header.u1.s1.TotalLength = sizeof(Message.Header);
        Message.Header.u2.s2.Type = Types[Index];
        Message.Header.MessageId = Id;
        if (sizeof(void *) == 4) *(ULONG *)(Message.Bytes + 24) = Tail;
        Status = NtAlpcQueryInformationMessage(Event, &Message.Header, AlpcMessageTokenModifiedIdInformation, &Token, sizeof(Token), &Returned);
        /* On 64-bit Windows, bit 0x1000 selects the message ID at offset 16;
         * otherwise the query reads it at offset 24, even from a WoW64 caller. */
        if (sizeof(void *) == 4)
            Expected = (Types[Index] & 0x1000 ? Id : Tail) ? STATUS_OBJECT_TYPE_MISMATCH : STATUS_INVALID_PARAMETER;
        else
            Expected = !(Types[Index] & 0x1000) && Id ? STATUS_OBJECT_TYPE_MISMATCH : STATUS_INVALID_PARAMETER;
        ok(Status == Expected, "Header type=%04x id=%lu tail=%lu status=%08lx expected=%08lx\n", Types[Index], Id, Tail, Status, Expected);
    }
    NtClose(Event);
}

static DWORD WINAPI StressThread(PVOID Parameter)
{
    ULONG Iteration;
    UNREFERENCED_PARAMETER(Parameter);
    TestResources();
    for (Iteration = 0; Iteration < 8; ++Iteration) TestConnection();
    return 0;
}

static VOID TestConcurrent(VOID)
{
    HANDLE Threads[4];
    ULONG Count, Index;
    DWORD WaitStatus;

    for (Count = 0; Count < RTL_NUMBER_OF(Threads); ++Count)
    {
        Threads[Count] = CreateThread(NULL, 0, StressThread, NULL, 0, NULL);
        ok(Threads[Count] != NULL, "Stress worker %lu failed: %lu\n", Count, GetLastError());
        if (!Threads[Count]) break;
    }
    if (!Count) return;
    WaitStatus = WaitForMultipleObjects(Count, Threads, TRUE, 30000);
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus != WAIT_OBJECT_0)
    {
        TerminateProcess(GetCurrentProcess(), 1);
        return;
    }
    for (Index = 0; Index < Count; ++Index) NtClose(Threads[Index]);
}

START_TEST(NtAlpcWow64)
{
    if (AlpcTestIsChildMode("probe"))
    {
        TestGuardPages();
        TestMessageLayouts();
        return;
    }
    if (AlpcTestIsChildMode("stress"))
    {
        TestConcurrent();
        return;
    }
    if (AlpcTestIsChildMode("boundaries"))
    {
        TestBoundaries();
        TestInvalidHandles();
        TestGuardPages();
        TestMessageLayouts();
        return;
    }
    TestResources();
    TestConnection();
    TestBoundaries();
    TestInvalidHandles();
    TestGuardPages();
    TestMessageLayouts();
}
