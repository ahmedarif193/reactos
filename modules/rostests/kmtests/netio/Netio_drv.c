/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:         Kernel-Mode Test Suite for netio WSK listen/accept paths
 */

#include <kmt_test.h>

#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0600)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#if !defined(WINVER) || (WINVER < 0x0600)
#undef WINVER
#define WINVER 0x0600
#endif

#ifndef _MINWINDEF_
typedef ULONG DWORD;
#endif

#ifndef _BSDTYPES_DEFINED
typedef unsigned short u_short;
#define _BSDTYPES_DEFINED
#endif

#include <ws2def.h>
#include <wsk.h>

#include "Netio.h"

#define NETIO_TEST_TIMEOUT_MS 5000
#define NETIO_SETUP_DELAY_MS  100
#define NETIO_LOOPBACK_ADDR   0x7f000001UL

typedef enum _NETIO_SCENARIO
{
    NetioScenarioNone,
    NetioScenarioEvent,
    NetioScenarioPrecedence,
    NetioScenarioDisable,
    NetioScenarioCloseAccept,
} NETIO_SCENARIO;

typedef struct _NETIO_EVENT_CONTEXT
{
    KEVENT CompletionEvent;
    volatile LONG AcceptEventCount;
    PWSK_SOCKET AcceptSocket;
    SOCKADDR_IN CallbackLocalAddress;
    SOCKADDR_IN CallbackRemoteAddress;
} NETIO_EVENT_CONTEXT, *PNETIO_EVENT_CONTEXT;

typedef struct _NETIO_TEST_STATE
{
    NETIO_SCENARIO Scenario;
    BOOLEAN Registered;
    BOOLEAN ProviderCaptured;
    WSK_REGISTRATION Registration;
    WSK_PROVIDER_NPI ProviderNpi;
    PWSK_SOCKET ListenSocket;
    PIRP AcceptIrp;
    KEVENT AcceptIrpEvent;
    SOCKADDR_IN AcceptLocalAddress;
    SOCKADDR_IN AcceptRemoteAddress;
    PWSK_SOCKET AcceptedSocket;
    NETIO_EVENT_CONTEXT EventContext;
} NETIO_TEST_STATE, *PNETIO_TEST_STATE;

static NETIO_TEST_STATE NetioState;

static const WSK_CLIENT_DISPATCH NetioClientDispatch =
{
    MAKE_WSK_VERSION(1, 0),
    0,
    NULL,
};

static USHORT
NetioHtons(
    _In_ USHORT Value)
{
    return RtlUshortByteSwap(Value);
}

static ULONG
NetioHtonl(
    _In_ ULONG Value)
{
    return RtlUlongByteSwap(Value);
}

static VOID
NetioInitializeLoopbackAddress(
    _Out_ PSOCKADDR_IN Address,
    _In_ USHORT Port)
{
    RtlZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_port = NetioHtons(Port);
    Address->sin_addr.S_un.S_addr = NetioHtonl(NETIO_LOOPBACK_ADDR);
}

static VOID
NetioDelayMilliseconds(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -((LONGLONG)Milliseconds * 10000);
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

static NTSTATUS
NetioWaitForEvent(
    _In_ PKEVENT Event,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -((LONGLONG)TimeoutMs * 10000);
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
}

static NTSTATUS
NTAPI
NetioIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
NetioAllocateSyncIrp(
    _Out_ PIRP *IrpOut,
    _Out_ PKEVENT EventOut)
{
    PIRP Irp;

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(EventOut, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, NetioIrpCompletion, EventOut, TRUE, TRUE, TRUE);
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    *IrpOut = Irp;
    return STATUS_SUCCESS;
}

static NTSTATUS
NetioWaitSyncIrp(
    _Inout_ PIRP Irp,
    _Inout_ PKEVENT Event,
    _In_ NTSTATUS Status,
    _Out_opt_ PULONG_PTR Information)
{
    NTSTATUS FinalStatus;
    NTSTATUS WaitStatus;

    FinalStatus = Status;
    if (FinalStatus == STATUS_PENDING)
    {
        WaitStatus = NetioWaitForEvent(Event, NETIO_TEST_TIMEOUT_MS);
        if (WaitStatus != STATUS_SUCCESS)
            return WaitStatus;
        FinalStatus = Irp->IoStatus.Status;
    }

    if (Information != NULL)
        *Information = Irp->IoStatus.Information;

    IoFreeIrp(Irp);
    return FinalStatus;
}

static NTSTATUS
NetioProviderInit(
    _Inout_ PNETIO_TEST_STATE State)
{
    WSK_CLIENT_NPI ClientNpi;
    NTSTATUS Status;

    ClientNpi.ClientContext = NULL;
    ClientNpi.Dispatch = &NetioClientDispatch;

    Status = WskRegister(&ClientNpi, &State->Registration);
    if (!NT_SUCCESS(Status))
        return Status;

    State->Registered = TRUE;
    Status = WskCaptureProviderNPI(&State->Registration,
                                   WSK_INFINITE_WAIT,
                                   &State->ProviderNpi);
    if (!NT_SUCCESS(Status))
    {
        WskDeregister(&State->Registration);
        State->Registered = FALSE;
        return Status;
    }

    State->ProviderCaptured = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS
NetioSocketCreate(
    _In_ PNETIO_TEST_STATE State,
    _In_ ULONG Flags,
    _In_opt_ PVOID SocketContext,
    _In_opt_ const VOID *Dispatch,
    _Out_ PWSK_SOCKET *SocketOut)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    ULONG_PTR Information;

    Status = NetioAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    Information = 0;
    Status = State->ProviderNpi.Dispatch->WskSocket(State->ProviderNpi.Client,
                                                    AF_INET,
                                                    SOCK_STREAM,
                                                    IPPROTO_TCP,
                                                    Flags,
                                                    SocketContext,
                                                    Dispatch,
                                                    NULL,
                                                    NULL,
                                                    NULL,
                                                    Irp);
    Status = NetioWaitSyncIrp(Irp, &Event, Status, &Information);
    if (!NT_SUCCESS(Status))
        return Status;

    *SocketOut = (PWSK_SOCKET)Information;
    return STATUS_SUCCESS;
}

static NTSTATUS
NetioSocketBind(
    _In_ PWSK_SOCKET Socket,
    _In_ PSOCKADDR LocalAddress)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_LISTEN_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_LISTEN_DISPATCH)Socket->Dispatch;
    Status = NetioAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Dispatch->WskBind(Socket, LocalAddress, 0, Irp);
    return NetioWaitSyncIrp(Irp, &Event, Status, NULL);
}

static NTSTATUS
NetioSocketClose(
    _In_ PWSK_SOCKET Socket)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_BASIC_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_BASIC_DISPATCH)Socket->Dispatch;
    Status = NetioAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Dispatch->WskCloseSocket(Socket, Irp);
    return NetioWaitSyncIrp(Irp, &Event, Status, NULL);
}

static NTSTATUS
NetioSocketGetLocalAddress(
    _In_ PWSK_SOCKET Socket,
    _Out_ PSOCKADDR_IN LocalAddress)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Status = NetioAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(LocalAddress, sizeof(*LocalAddress));
    Status = Dispatch->WskGetLocalAddress(Socket, (PSOCKADDR)LocalAddress, Irp);
    return NetioWaitSyncIrp(Irp, &Event, Status, NULL);
}

static NTSTATUS
NetioSocketGetRemoteAddress(
    _In_ PWSK_SOCKET Socket,
    _Out_ PSOCKADDR_IN RemoteAddress)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Status = NetioAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(RemoteAddress, sizeof(*RemoteAddress));
    Status = Dispatch->WskGetRemoteAddress(Socket, (PSOCKADDR)RemoteAddress, Irp);
    return NetioWaitSyncIrp(Irp, &Event, Status, NULL);
}

static NTSTATUS
NetioConfigureAcceptEvent(
    _In_ PWSK_SOCKET ListenSocket,
    _In_ BOOLEAN Enable)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    WSK_EVENT_CALLBACK_CONTROL Control;
    PWSK_PROVIDER_LISTEN_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_LISTEN_DISPATCH)ListenSocket->Dispatch;
    Status = NetioAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The standalone kmtest build does not export NPI_WSK_INTERFACE_ID. */
    Control.NpiId = NULL;
    Control.EventMask = WSK_EVENT_ACCEPT | (Enable ? 0 : WSK_EVENT_DISABLE);
    Status = Dispatch->WskControlSocket(ListenSocket,
                                        WskSetOption,
                                        SO_WSK_EVENT_CALLBACK,
                                        SOL_SOCKET,
                                        sizeof(Control),
                                        &Control,
                                        0,
                                        NULL,
                                        NULL,
                                        Irp);
    return NetioWaitSyncIrp(Irp, &Event, Status, NULL);
}

static NTSTATUS
NetioStartExplicitAccept(
    _Inout_ PNETIO_TEST_STATE State)
{
    NTSTATUS Status;
    PWSK_PROVIDER_LISTEN_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_LISTEN_DISPATCH)State->ListenSocket->Dispatch;
    Status = NetioAllocateSyncIrp(&State->AcceptIrp, &State->AcceptIrpEvent);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&State->AcceptLocalAddress, sizeof(State->AcceptLocalAddress));
    RtlZeroMemory(&State->AcceptRemoteAddress, sizeof(State->AcceptRemoteAddress));

    Status = Dispatch->WskAccept(State->ListenSocket,
                                 0,
                                 NULL,
                                 NULL,
                                 (PSOCKADDR)&State->AcceptLocalAddress,
                                 (PSOCKADDR)&State->AcceptRemoteAddress,
                                 State->AcceptIrp);
    if (Status == STATUS_PENDING || NT_SUCCESS(Status))
        return STATUS_SUCCESS;

    IoFreeIrp(State->AcceptIrp);
    State->AcceptIrp = NULL;
    return Status;
}

static NTSTATUS
WSKAPI
NetioAcceptEvent(
    _In_opt_ PVOID SocketContext,
    _In_ ULONG Flags,
    _In_ PSOCKADDR LocalAddress,
    _In_ PSOCKADDR RemoteAddress,
    _In_opt_ PWSK_SOCKET AcceptSocket,
    _Outptr_result_maybenull_ PVOID *AcceptSocketContext,
    _Outptr_result_maybenull_ const WSK_CLIENT_CONNECTION_DISPATCH **AcceptSocketDispatch)
{
    PNETIO_EVENT_CONTEXT EventContext;

    UNREFERENCED_PARAMETER(Flags);

    EventContext = (PNETIO_EVENT_CONTEXT)SocketContext;
    InterlockedIncrement(&EventContext->AcceptEventCount);
    EventContext->AcceptSocket = AcceptSocket;
    RtlCopyMemory(&EventContext->CallbackLocalAddress,
                  LocalAddress,
                  sizeof(EventContext->CallbackLocalAddress));
    RtlCopyMemory(&EventContext->CallbackRemoteAddress,
                  RemoteAddress,
                  sizeof(EventContext->CallbackRemoteAddress));

    if (AcceptSocketContext != NULL)
        *AcceptSocketContext = NULL;
    if (AcceptSocketDispatch != NULL)
        *AcceptSocketDispatch = NULL;

    KeSetEvent(&EventContext->CompletionEvent, IO_NO_INCREMENT, FALSE);
    return STATUS_SUCCESS;
}

static const WSK_CLIENT_LISTEN_DISPATCH NetioListenDispatch =
{
    NetioAcceptEvent,
    NULL,
    NULL,
};

static VOID
NetioValidateLocalAddress(
    _In_ const SOCKADDR_IN *LocalAddress,
    _In_ USHORT Port)
{
    ok_eq_int(LocalAddress->sin_family, AF_INET);
    ok_eq_hex(LocalAddress->sin_addr.S_un.S_addr, NetioHtonl(NETIO_LOOPBACK_ADDR));
    ok_eq_hex(LocalAddress->sin_port, NetioHtons(Port));
}

static VOID
NetioValidateRemoteAddress(
    _In_ const SOCKADDR_IN *RemoteAddress)
{
    ok_eq_int(RemoteAddress->sin_family, AF_INET);
    ok_eq_hex(RemoteAddress->sin_addr.S_un.S_addr, NetioHtonl(NETIO_LOOPBACK_ADDR));
    ok(RemoteAddress->sin_port != 0, "Remote port should not be 0\n");
}

static VOID
NetioCaptureFailure(
    _Inout_ PNTSTATUS FinalStatus,
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(*FinalStatus) && !NT_SUCCESS(Status))
        *FinalStatus = Status;
}

static NTSTATUS
NetioCleanupState(
    _Inout_ PNETIO_TEST_STATE State)
{
    NTSTATUS CleanupStatus = STATUS_SUCCESS;
    NTSTATUS Status;
    PWSK_SOCKET Socket;

    if (State->AcceptIrp != NULL && State->ListenSocket != NULL)
    {
        Status = NetioSocketClose(State->ListenSocket);
        ok_eq_hex(Status, STATUS_SUCCESS);
        NetioCaptureFailure(&CleanupStatus, Status);
        State->ListenSocket = NULL;
    }

    if (State->AcceptIrp != NULL)
    {
        Status = NetioWaitForEvent(&State->AcceptIrpEvent, NETIO_TEST_TIMEOUT_MS);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            return Status;

        IoFreeIrp(State->AcceptIrp);
        State->AcceptIrp = NULL;
    }

    if (State->AcceptedSocket != NULL)
    {
        Socket = State->AcceptedSocket;
        State->AcceptedSocket = NULL;
        if (State->EventContext.AcceptSocket == Socket)
            State->EventContext.AcceptSocket = NULL;

        Status = NetioSocketClose(Socket);
        ok_eq_hex(Status, STATUS_SUCCESS);
        NetioCaptureFailure(&CleanupStatus, Status);
    }

    if (State->EventContext.AcceptSocket != NULL)
    {
        Socket = State->EventContext.AcceptSocket;
        State->EventContext.AcceptSocket = NULL;

        Status = NetioSocketClose(Socket);
        ok_eq_hex(Status, STATUS_SUCCESS);
        NetioCaptureFailure(&CleanupStatus, Status);
    }

    if (State->ListenSocket != NULL)
    {
        Status = NetioSocketClose(State->ListenSocket);
        ok_eq_hex(Status, STATUS_SUCCESS);
        NetioCaptureFailure(&CleanupStatus, Status);
        State->ListenSocket = NULL;
    }

    if (State->ProviderCaptured)
    {
        WskReleaseProviderNPI(&State->Registration);
        State->ProviderCaptured = FALSE;
    }

    if (State->Registered)
    {
        WskDeregister(&State->Registration);
        State->Registered = FALSE;
    }

    RtlZeroMemory(State, sizeof(*State));
    return CleanupStatus;
}

static NTSTATUS
NetioStartScenario(
    _In_ NETIO_SCENARIO Scenario,
    _In_ USHORT Port)
{
    NTSTATUS Status;
    NTSTATUS CleanupStatus;
    SOCKADDR_IN ListenAddress;

    CleanupStatus = NetioCleanupState(&NetioState);
    ok_eq_hex(CleanupStatus, STATUS_SUCCESS);
    if (!NT_SUCCESS(CleanupStatus))
        return CleanupStatus;

    KeInitializeEvent(&NetioState.EventContext.CompletionEvent, NotificationEvent, FALSE);

    Status = NetioProviderInit(&NetioState);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    Status = NetioSocketCreate(&NetioState,
                               WSK_FLAG_LISTEN_SOCKET,
                               &NetioState.EventContext,
                               &NetioListenDispatch,
                               &NetioState.ListenSocket);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    NetioInitializeLoopbackAddress(&ListenAddress, Port);
    Status = NetioSocketBind(NetioState.ListenSocket, (PSOCKADDR)&ListenAddress);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    if (Scenario == NetioScenarioEvent ||
        Scenario == NetioScenarioPrecedence ||
        Scenario == NetioScenarioDisable)
    {
        Status = NetioConfigureAcceptEvent(NetioState.ListenSocket, TRUE);
        if (!NT_SUCCESS(Status))
            goto cleanup;

        NetioDelayMilliseconds(NETIO_SETUP_DELAY_MS);
    }

    if (Scenario == NetioScenarioDisable)
    {
        Status = NetioConfigureAcceptEvent(NetioState.ListenSocket, FALSE);
        if (!NT_SUCCESS(Status))
            goto cleanup;
    }

    if (Scenario == NetioScenarioPrecedence ||
        Scenario == NetioScenarioDisable ||
        Scenario == NetioScenarioCloseAccept)
    {
        Status = NetioStartExplicitAccept(&NetioState);
        if (!NT_SUCCESS(Status))
            goto cleanup;

        if (Scenario == NetioScenarioCloseAccept)
            NetioDelayMilliseconds(NETIO_SETUP_DELAY_MS);
    }

    NetioState.Scenario = Scenario;
    return STATUS_SUCCESS;

cleanup:
    CleanupStatus = NetioCleanupState(&NetioState);
    if (NT_SUCCESS(Status))
        Status = CleanupStatus;
    return Status;
}

static NTSTATUS
NetioFinishEventScenario(
    _In_ USHORT Port)
{
    NTSTATUS CleanupStatus;
    NTSTATUS Status;
    SOCKADDR_IN LocalAddress;
    SOCKADDR_IN RemoteAddress;

    if (NetioState.Scenario != NetioScenarioEvent)
        return STATUS_INVALID_DEVICE_STATE;

    Status = NetioWaitForEvent(&NetioState.EventContext.CompletionEvent, NETIO_TEST_TIMEOUT_MS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    ok_eq_long(NetioState.EventContext.AcceptEventCount, 1);
    ok(NetioState.EventContext.AcceptSocket != NULL, "Accept event did not return a socket\n");

    NetioValidateLocalAddress(&NetioState.EventContext.CallbackLocalAddress, Port);
    NetioValidateRemoteAddress(&NetioState.EventContext.CallbackRemoteAddress);

    if (NetioState.EventContext.AcceptSocket != NULL)
    {
        Status = NetioSocketGetLocalAddress(NetioState.EventContext.AcceptSocket, &LocalAddress);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            NetioValidateLocalAddress(&LocalAddress, Port);

        Status = NetioSocketGetRemoteAddress(NetioState.EventContext.AcceptSocket, &RemoteAddress);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            NetioValidateRemoteAddress(&RemoteAddress);
    }

    Status = STATUS_SUCCESS;

cleanup:
    CleanupStatus = NetioCleanupState(&NetioState);
    if (NT_SUCCESS(Status))
        Status = CleanupStatus;
    return Status;
}

static NTSTATUS
NetioFinishExplicitScenario(
    _In_ NETIO_SCENARIO Scenario,
    _In_ USHORT Port)
{
    NTSTATUS CleanupStatus;
    NTSTATUS Status;
    SOCKADDR_IN LocalAddress;
    SOCKADDR_IN RemoteAddress;

    if (NetioState.Scenario != Scenario || NetioState.AcceptIrp == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    Status = NetioWaitForEvent(&NetioState.AcceptIrpEvent, NETIO_TEST_TIMEOUT_MS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    Status = NetioState.AcceptIrp->IoStatus.Status;
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    NetioState.AcceptedSocket = (PWSK_SOCKET)NetioState.AcceptIrp->IoStatus.Information;
    IoFreeIrp(NetioState.AcceptIrp);
    NetioState.AcceptIrp = NULL;

    ok(NetioState.AcceptedSocket != NULL, "WskAccept did not return a socket\n");
    ok_eq_long(NetioState.EventContext.AcceptEventCount, 0);

    NetioValidateLocalAddress(&NetioState.AcceptLocalAddress, Port);
    NetioValidateRemoteAddress(&NetioState.AcceptRemoteAddress);

    if (NetioState.AcceptedSocket != NULL)
    {
        Status = NetioSocketGetLocalAddress(NetioState.AcceptedSocket, &LocalAddress);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            NetioValidateLocalAddress(&LocalAddress, Port);
            ok_eq_hex(LocalAddress.sin_port, NetioState.AcceptLocalAddress.sin_port);
        }

        Status = NetioSocketGetRemoteAddress(NetioState.AcceptedSocket, &RemoteAddress);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            NetioValidateRemoteAddress(&RemoteAddress);
            ok_eq_hex(RemoteAddress.sin_port, NetioState.AcceptRemoteAddress.sin_port);
        }
    }

    Status = STATUS_SUCCESS;

cleanup:
    CleanupStatus = NetioCleanupState(&NetioState);
    if (NT_SUCCESS(Status))
        Status = CleanupStatus;
    return Status;
}

static NTSTATUS
NetioFinishCloseAcceptScenario(VOID)
{
    NTSTATUS AcceptStatus;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (NetioState.Scenario != NetioScenarioCloseAccept || NetioState.AcceptIrp == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    ok_eq_long(NetioState.EventContext.AcceptEventCount, 0);
    ok_eq_pointer(NetioState.EventContext.AcceptSocket, NULL);

    Status = NetioSocketClose(NetioState.ListenSocket);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto cleanup;
    NetioState.ListenSocket = NULL;

    Status = NetioWaitForEvent(&NetioState.AcceptIrpEvent, NETIO_TEST_TIMEOUT_MS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto cleanup;

    AcceptStatus = NetioState.AcceptIrp->IoStatus.Status;
    ok(AcceptStatus != STATUS_SUCCESS,
       "Pending WskAccept unexpectedly succeeded after listener close\n");
    ok_eq_ulongptr(NetioState.AcceptIrp->IoStatus.Information, 0);

    IoFreeIrp(NetioState.AcceptIrp);
    NetioState.AcceptIrp = NULL;

    ok_eq_pointer(NetioState.AcceptedSocket, NULL);
    ok_eq_pointer(NetioState.EventContext.AcceptSocket, NULL);

    Status = STATUS_SUCCESS;

cleanup:
    CleanupStatus = NetioCleanupState(&NetioState);
    if (NT_SUCCESS(Status))
        Status = CleanupStatus;
    return Status;
}

static NTSTATUS
NetioMessageHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_ PVOID Buffer,
    _In_ SIZE_T InLength,
    _Inout_ PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    switch (ControlCode)
    {
        case IOCTL_NETIO_START_EVENT_TEST:
            return NetioStartScenario(NetioScenarioEvent, NETIO_TEST_EVENT_PORT);

        case IOCTL_NETIO_FINISH_EVENT_TEST:
            return NetioFinishEventScenario(NETIO_TEST_EVENT_PORT);

        case IOCTL_NETIO_START_PRECEDENCE_TEST:
            return NetioStartScenario(NetioScenarioPrecedence, NETIO_TEST_PRECEDENCE_PORT);

        case IOCTL_NETIO_FINISH_PRECEDENCE_TEST:
            return NetioFinishExplicitScenario(NetioScenarioPrecedence, NETIO_TEST_PRECEDENCE_PORT);

        case IOCTL_NETIO_START_DISABLE_TEST:
            return NetioStartScenario(NetioScenarioDisable, NETIO_TEST_DISABLE_PORT);

        case IOCTL_NETIO_FINISH_DISABLE_TEST:
            return NetioFinishExplicitScenario(NetioScenarioDisable, NETIO_TEST_DISABLE_PORT);

        case IOCTL_NETIO_START_CLOSE_ACCEPT_TEST:
            return NetioStartScenario(NetioScenarioCloseAccept, NETIO_TEST_CLOSE_ACCEPT_PORT);

        case IOCTL_NETIO_FINISH_CLOSE_ACCEPT_TEST:
            return NetioFinishCloseAcceptScenario();

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

NTSTATUS
TestEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PCUNICODE_STRING RegistryPath,
    _Out_ PCWSTR *DeviceName,
    _Inout_ INT *Flags)
{
    static const ULONG ControlCodes[] =
    {
        IOCTL_NETIO_START_EVENT_TEST,
        IOCTL_NETIO_FINISH_EVENT_TEST,
        IOCTL_NETIO_START_PRECEDENCE_TEST,
        IOCTL_NETIO_FINISH_PRECEDENCE_TEST,
        IOCTL_NETIO_START_DISABLE_TEST,
        IOCTL_NETIO_FINISH_DISABLE_TEST,
        IOCTL_NETIO_START_CLOSE_ACCEPT_TEST,
        IOCTL_NETIO_FINISH_CLOSE_ACCEPT_TEST,
    };
    ULONG Index;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(Flags);

    *DeviceName = L"Netio";
    for (Index = 0; Index < RTL_NUMBER_OF(ControlCodes); Index++)
        KmtRegisterMessageHandler(ControlCodes[Index], NULL, NetioMessageHandler);

    return STATUS_SUCCESS;
}

VOID
TestUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();
    NetioCleanupState(&NetioState);
}
