/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ALPC syscall validation-order and error-path coverage
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

static
VOID
AlpcValidationCreatePortMatrix(VOID)
{
    static const SIZE_T MaximumMessageLengths[] = {0, sizeof(PORT_MESSAGE) - 1, sizeof(PORT_MESSAGE), 0xffff, 0x10000};
    ALPC_PORT_ATTRIBUTES Attributes;
    SECURITY_QUALITY_OF_SERVICE Qos;
    NTSTATUS Status;
    HANDLE Output;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(MaximumMessageLengths); ++Index)
    {
        AlpcTestInitializePortAttributes(&Attributes, 0);
        Attributes.MaxMessageLength = MaximumMessageLengths[Index];
        Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
        alpc_observe_scalar_output("CreatePort.max_message", Output, NtAlpcCreatePort(&Output, NULL, &Attributes));
        trace("ALPC_OBSERVE input CreatePort.max_message=%Iu\n", MaximumMessageLengths[Index]);
        if (NT_SUCCESS(Status))
            NtClose(Output);
    }

    AlpcTestInitializePortAttributes(&Attributes, 0x80000000);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("CreatePort.invalid_flag", Output, NtAlpcCreatePort(&Output, NULL, &Attributes));
    if (NT_SUCCESS(Status))
        NtClose(Output);

    AlpcTestInitializePortAttributes(&Attributes, 0);
    Attributes.SecurityQos.Length = sizeof(Attributes.SecurityQos) - 1;
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("CreatePort.short_qos", Output, NtAlpcCreatePort(&Output, NULL, &Attributes));
    if (NT_SUCCESS(Status))
        NtClose(Output);

    AlpcTestInitializePortAttributes(&Attributes, 0);
    Qos = Attributes.SecurityQos;
    Qos.ImpersonationLevel = (SECURITY_IMPERSONATION_LEVEL)4;
    Attributes.SecurityQos = Qos;
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("CreatePort.invalid_impersonation_level", Output, NtAlpcCreatePort(&Output, NULL, &Attributes));
    if (NT_SUCCESS(Status))
        NtClose(Output);
}

static
VOID
AlpcValidationCloseUnexpectedHandle(
    _In_ NTSTATUS Status,
    _In_opt_ HANDLE Output)
{
    if (NT_SUCCESS(Status) && Output && Output != (HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtClose(Output);
}

static
VOID
AlpcValidationInformationMatrix(
    _In_ HANDLE Port,
    _In_ HANDLE WrongType,
    _In_ HANDLE ClosedHandle,
    _In_opt_ HANDLE NoAccessPort)
{
    static ULONG_PTR ZoneStorage[PAGE_SIZE / sizeof(ULONG_PTR)];
    UCHAR Buffer[sizeof(ALPC_PORT_ATTRIBUTES) + 16];
    UCHAR Before[sizeof(Buffer)];
    PALPC_BASIC_INFORMATION Basic = (PALPC_BASIC_INFORMATION)Buffer;
    PALPC_PORT_ATTRIBUTES Attributes = (PALPC_PORT_ATTRIBUTES)Buffer;
    ALPC_PORT_ASSOCIATE_COMPLETION_PORT Associate;
    ALPC_PORT_MESSAGE_ZONE_INFORMATION Zone;
    ULONG Concurrency;
    ULONG ReturnLength;
    NTSTATUS Status;

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    RtlCopyMemory(Before, Buffer, sizeof(Buffer));
    ReturnLength = 0x55555555;
    alpc_observe_status("QueryInformation.null_information", NtAlpcQueryInformation(Port, AlpcBasicInformation, NULL, sizeof(*Basic), &ReturnLength));
    alpc_trace_scalar_mutation("QueryInformation.null_information", "return_length", 0x55555555, ReturnLength);

    ReturnLength = 0x55555555;
    alpc_observe_status("QueryInformation.zero_length", NtAlpcQueryInformation(Port, AlpcBasicInformation, Basic, 0, &ReturnLength));
    AlpcTestTraceBufferMutation("QueryInformation.zero_length", Before, Buffer, sizeof(Buffer));
    alpc_trace_scalar_mutation("QueryInformation.zero_length", "return_length", 0x55555555, ReturnLength);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    RtlCopyMemory(Before, Buffer, sizeof(Buffer));
    ReturnLength = 0x55555555;
    alpc_observe_status("QueryInformation.oversized", NtAlpcQueryInformation(Port, AlpcBasicInformation, Basic, sizeof(Buffer), &ReturnLength));
    AlpcTestTraceBufferMutation("QueryInformation.oversized", Before, Buffer, sizeof(Buffer));
    alpc_trace_scalar_mutation("QueryInformation.oversized", "return_length", 0x55555555, ReturnLength);

    ReturnLength = 0x55555555;
    alpc_observe_status("QueryInformation.wrong_type_handle", NtAlpcQueryInformation(WrongType, AlpcBasicInformation, Basic, sizeof(*Basic), &ReturnLength));
    alpc_observe_status("QueryInformation.closed_handle", NtAlpcQueryInformation(ClosedHandle, AlpcBasicInformation, Basic, sizeof(*Basic), &ReturnLength));
    if (NoAccessPort)
        alpc_observe_status("QueryInformation.access_denied", NtAlpcQueryInformation(NoAccessPort, AlpcBasicInformation, Basic, sizeof(*Basic), &ReturnLength));

    AlpcTestInitializePortAttributes(Attributes, 0);
    alpc_observe_status("SetInformation.port_short", NtAlpcSetInformation(Port, AlpcPortInformation, Attributes, sizeof(*Attributes) - 1));
    alpc_observe_status("SetInformation.port_exact", NtAlpcSetInformation(Port, AlpcPortInformation, Attributes, sizeof(*Attributes)));
    alpc_observe_status("SetInformation.port_oversized", NtAlpcSetInformation(Port, AlpcPortInformation, Attributes, sizeof(*Attributes) + 1));
    alpc_observe_status("SetInformation.port_null_exact", NtAlpcSetInformation(Port, AlpcPortInformation, NULL, sizeof(*Attributes)));
    alpc_observe_status("SetInformation.wrong_type_handle", NtAlpcSetInformation(WrongType, AlpcPortInformation, Attributes, sizeof(*Attributes)));
    alpc_observe_status("SetInformation.closed_handle", NtAlpcSetInformation(ClosedHandle, AlpcPortInformation, Attributes, sizeof(*Attributes)));
    if (NoAccessPort)
        alpc_observe_status("SetInformation.access_denied", NtAlpcSetInformation(NoAccessPort, AlpcPortInformation, Attributes, sizeof(*Attributes)));

    RtlZeroMemory(&Associate, sizeof(Associate));
    Associate.CompletionPort = WrongType;
    alpc_observe_status("SetInformation.associate_wrong_type", NtAlpcSetInformation(Port, AlpcAssociateCompletionPortInformation, &Associate, sizeof(Associate)));
    alpc_observe_status("SetInformation.associate_oversized", NtAlpcSetInformation(Port, AlpcAssociateCompletionPortInformation, &Associate, sizeof(Associate) + 1));

    Zone.Buffer = ZoneStorage;
    Zone.Size = sizeof(ZoneStorage);
    alpc_observe_status("SetInformation.zone_short", NtAlpcSetInformation(Port, AlpcMessageZoneInformation, &Zone, sizeof(Zone) - 1));
    alpc_observe_status("SetInformation.zone_exact", NtAlpcSetInformation(Port, AlpcMessageZoneInformation, &Zone, sizeof(Zone)));
    alpc_observe_status("SetInformation.zone_oversized", NtAlpcSetInformation(Port, AlpcMessageZoneInformation, &Zone, sizeof(Zone) + 1));

    Concurrency = 0;
    alpc_observe_status("SetInformation.concurrency_zero", NtAlpcSetInformation(Port, AlpcAdjustCompletionListConcurrencyCountInformation, &Concurrency, sizeof(Concurrency)));
    alpc_observe_status("SetInformation.concurrency_short", NtAlpcSetInformation(Port, AlpcAdjustCompletionListConcurrencyCountInformation, &Concurrency, sizeof(Concurrency) - 1));
    alpc_observe_status("SetInformation.unregister_null_zero", NtAlpcSetInformation(Port, AlpcUnregisterCompletionListInformation, NULL, 0));
    alpc_observe_status("SetInformation.unregister_nonzero", NtAlpcSetInformation(Port, AlpcUnregisterCompletionListInformation, NULL, 1));
    alpc_observe_status("SetInformation.rundown_null_zero", NtAlpcSetInformation(Port, AlpcCompletionListRundownInformation, NULL, 0));

    if (AlpcTestNativeObservationEnabled())
    {
        Zone.Buffer = (PVOID)(ULONG_PTR)0x5555555555555555ULL;
        Zone.Size = 0x55555555;
        alpc_observe_status("SetInformation.zone_invalid_pointer", NtAlpcSetInformation(Port, AlpcMessageZoneInformation, &Zone, sizeof(Zone)));
        ReturnLength = 0x55555555;
        alpc_observe_status("QueryInformation.misaligned_plus_1", NtAlpcQueryInformation(Port, AlpcBasicInformation, Buffer + 1, sizeof(ALPC_BASIC_INFORMATION), &ReturnLength));
        alpc_observe_status("SetInformation.misaligned_plus_1", NtAlpcSetInformation(Port, AlpcPortInformation, Buffer + 1, sizeof(ALPC_PORT_ATTRIBUTES)));
    }
    else
    {
        skip("set ALPC_TEST_NATIVE_OBSERVE=1 to run information misalignment rows\n");
    }
}

static
VOID
AlpcValidationResourceMatrix(
    _In_ HANDLE Port,
    _In_ HANDLE WrongType,
    _In_ HANDLE ClosedHandle)
{
    UCHAR Buffer[sizeof(ALPC_DATA_VIEW_ATTR) + 8];
    ALPC_DATA_VIEW_ATTR View;
    ALPC_SECURITY_ATTR Security;
    SECURITY_QUALITY_OF_SERVICE Qos;
    ALPC_TEST_RESERVE_OUTPUT ReserveOutput;
    ALPC_HANDLE Resource;
    SIZE_T ActualSize;
    NTSTATUS Status;

    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    ActualSize = (SIZE_T)0x5555555555555555ULL;
    alpc_observe_status("CreatePortSection.null_section_output", NtAlpcCreatePortSection(Port, 0, NULL, PAGE_SIZE, NULL, &ActualSize));
    alpc_trace_scalar_mutation("CreatePortSection.null_section_output", "actual_size", (SIZE_T)0x5555555555555555ULL, ActualSize);
    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("CreatePortSection.null_size_output", Resource, NtAlpcCreatePortSection(Port, 0, NULL, PAGE_SIZE, &Resource, NULL));
    if (NT_SUCCESS(Status) && Resource != (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtAlpcDeletePortSection(Port, 0, Resource);
    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    ActualSize = (SIZE_T)0x5555555555555555ULL;
    alpc_observe_status("CreatePortSection.invalid_flags", NtAlpcCreatePortSection(Port, 1, NULL, PAGE_SIZE, &Resource, &ActualSize));
    alpc_trace_scalar_mutation("CreatePortSection.invalid_flags", "section", (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL, Resource);
    alpc_trace_scalar_mutation("CreatePortSection.invalid_flags", "actual_size", (SIZE_T)0x5555555555555555ULL, ActualSize);
    if (NT_SUCCESS(Status) && Resource != (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtAlpcDeletePortSection(Port, 0, Resource);
    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    ActualSize = (SIZE_T)0x5555555555555555ULL;
    alpc_observe_status("CreatePortSection.wrong_type_port", NtAlpcCreatePortSection(WrongType, 0, NULL, PAGE_SIZE, &Resource, &ActualSize));
    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    ActualSize = (SIZE_T)0x5555555555555555ULL;
    alpc_observe_status("CreatePortSection.closed_port", NtAlpcCreatePortSection(ClosedHandle, 0, NULL, PAGE_SIZE, &Resource, &ActualSize));
    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    ActualSize = (SIZE_T)0x5555555555555555ULL;
    alpc_observe_status("CreatePortSection.wrong_type_section", NtAlpcCreatePortSection(Port, 0, WrongType, PAGE_SIZE, &Resource, &ActualSize));
    if (NT_SUCCESS(Status) && Resource != (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtAlpcDeletePortSection(Port, 0, Resource);

    alpc_observe_status("DeletePortSection.invalid_flags", NtAlpcDeletePortSection(Port, 1, (ALPC_HANDLE)(ULONG_PTR)1));
    alpc_observe_status("DeletePortSection.wrong_type_port", NtAlpcDeletePortSection(WrongType, 0, (ALPC_HANDLE)(ULONG_PTR)1));
    alpc_observe_status("DeletePortSection.closed_port", NtAlpcDeletePortSection(ClosedHandle, 0, (ALPC_HANDLE)(ULONG_PTR)1));

    alpc_observe_status("CreateResourceReserve.null_output", NtAlpcCreateResourceReserve(Port, 0, sizeof(PORT_MESSAGE), NULL));
    ReserveOutput.ResourceId = 0x55555555;
    ReserveOutput.Guard = 0xa5a5a5a5;
    alpc_observe_status("CreateResourceReserve.invalid_flags", NtAlpcCreateResourceReserve(Port, 1, sizeof(PORT_MESSAGE), &ReserveOutput.ResourceId));
    alpc_trace_scalar_mutation("CreateResourceReserve.invalid_flags", "output", 0x55555555, ReserveOutput.ResourceId);
    ok_eq_hex(ReserveOutput.Guard, 0xa5a5a5a5);
    if (NT_SUCCESS(Status))
        NtAlpcDeleteResourceReserve(Port, 0, ReserveOutput.ResourceId);
    ReserveOutput.ResourceId = 0x55555555;
    ReserveOutput.Guard = 0xa5a5a5a5;
    alpc_observe_status("CreateResourceReserve.wrong_type_port", NtAlpcCreateResourceReserve(WrongType, 0, sizeof(PORT_MESSAGE), &ReserveOutput.ResourceId));
    ok_eq_hex(ReserveOutput.Guard, 0xa5a5a5a5);
    ReserveOutput.ResourceId = 0x55555555;
    ReserveOutput.Guard = 0xa5a5a5a5;
    alpc_observe_status("CreateResourceReserve.closed_port", NtAlpcCreateResourceReserve(ClosedHandle, 0, sizeof(PORT_MESSAGE), &ReserveOutput.ResourceId));
    ok_eq_hex(ReserveOutput.Guard, 0xa5a5a5a5);
    alpc_observe_status("DeleteResourceReserve.invalid_flags", NtAlpcDeleteResourceReserve(Port, 1, 1));
    alpc_observe_status("DeleteResourceReserve.wrong_type_port", NtAlpcDeleteResourceReserve(WrongType, 0, 1));

    RtlZeroMemory(&View, sizeof(View));
    View.SectionHandle = (ALPC_HANDLE)(ULONG_PTR)1;
    View.ViewSize = PAGE_SIZE;
    alpc_observe_status("CreateSectionView.null_attributes", NtAlpcCreateSectionView(Port, 0, NULL));
    View.ViewBase = NULL;
    alpc_observe_status("CreateSectionView.invalid_flags", NtAlpcCreateSectionView(Port, 1, &View));
    if (NT_SUCCESS(Status) && View.ViewBase)
        NtAlpcDeleteSectionView(Port, 0, View.ViewBase);
    View.ViewBase = NULL;
    alpc_observe_status("CreateSectionView.wrong_type_port", NtAlpcCreateSectionView(WrongType, 0, &View));
    View.ViewBase = NULL;
    alpc_observe_status("CreateSectionView.closed_port", NtAlpcCreateSectionView(ClosedHandle, 0, &View));
    alpc_observe_status("DeleteSectionView.invalid_flags", NtAlpcDeleteSectionView(Port, 1, (PVOID)(ULONG_PTR)1));
    alpc_observe_status("DeleteSectionView.wrong_type_port", NtAlpcDeleteSectionView(WrongType, 0, (PVOID)(ULONG_PTR)1));

    RtlZeroMemory(&Qos, sizeof(Qos));
    Qos.Length = sizeof(Qos);
    Qos.ImpersonationLevel = SecurityImpersonation;
    RtlZeroMemory(&Security, sizeof(Security));
    Security.QoS = &Qos;
    Security.ContextHandle = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_status("CreateSecurityContext.null_attribute", NtAlpcCreateSecurityContext(Port, 0, NULL));
    Security.ContextHandle = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_status("CreateSecurityContext.invalid_flags", NtAlpcCreateSecurityContext(Port, 1, &Security));
    if (NT_SUCCESS(Status) && Security.ContextHandle != (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtAlpcDeleteSecurityContext(Port, 0, Security.ContextHandle);
    Security.ContextHandle = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_status("CreateSecurityContext.wrong_type_port", NtAlpcCreateSecurityContext(WrongType, 0, &Security));
    Security.ContextHandle = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_status("CreateSecurityContext.closed_port", NtAlpcCreateSecurityContext(ClosedHandle, 0, &Security));
    Security.QoS = NULL;
    Security.ContextHandle = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_status("CreateSecurityContext.null_qos", NtAlpcCreateSecurityContext(Port, 0, &Security));
    if (NT_SUCCESS(Status) && Security.ContextHandle != (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtAlpcDeleteSecurityContext(Port, 0, Security.ContextHandle);
    alpc_observe_status("DeleteSecurityContext.invalid_flags", NtAlpcDeleteSecurityContext(Port, 1, (ALPC_HANDLE)(ULONG_PTR)1));
    alpc_observe_status("RevokeSecurityContext.invalid_flags", NtAlpcRevokeSecurityContext(Port, 1, (ALPC_HANDLE)(ULONG_PTR)1));

    if (AlpcTestNativeObservationEnabled())
    {
        RtlZeroMemory(Buffer, sizeof(Buffer));
        ((PALPC_DATA_VIEW_ATTR)(Buffer + 1))->SectionHandle = (ALPC_HANDLE)(ULONG_PTR)1;
        ((PALPC_DATA_VIEW_ATTR)(Buffer + 1))->ViewSize = PAGE_SIZE;
        alpc_observe_status("CreateSectionView.misaligned_plus_1", NtAlpcCreateSectionView(Port, 0, (PALPC_DATA_VIEW_ATTR)(Buffer + 1)));
        if (NT_SUCCESS(Status) && ((PALPC_DATA_VIEW_ATTR)(Buffer + 1))->ViewBase)
            NtAlpcDeleteSectionView(Port, 0, ((PALPC_DATA_VIEW_ATTR)(Buffer + 1))->ViewBase);
    }
    else
    {
        skip("set ALPC_TEST_NATIVE_OBSERVE=1 to run resource misalignment rows\n");
    }
}

static
VOID
AlpcValidationTransportMatrix(
    _In_ HANDLE Port,
    _In_ HANDLE WrongType,
    _In_ HANDLE ClosedHandle,
    _In_ PUNICODE_STRING MissingName,
    _In_ POBJECT_ATTRIBUTES MissingAttributes,
    _In_ PALPC_PORT_ATTRIBUTES ValidAttributes)
{
    UCHAR Buffer[sizeof(ALPC_TEST_MESSAGE) + 8];
    ALPC_TEST_MESSAGE Message;
    ALPC_CONTEXT_ATTR Context;
    ALPC_PORT_ATTRIBUTES BadAttributes;
    OBJECT_ATTRIBUTES BadObjectAttributes;
    LARGE_INTEGER Timeout;
    SIZE_T BufferLength;
    ULONG ReturnLength;
    NTSTATUS Status;
    HANDLE Output;

    AlpcTestInitializeMessage(&Message, 0x56414c49, 1);
    RtlZeroMemory(&Context, sizeof(Context));
    Timeout.QuadPart = 0;
    ReturnLength = 0x55555555;

    alpc_observe_status("QueryInformationMessage.null_message", NtAlpcQueryInformationMessage(Port, NULL, AlpcMessageSidInformation, Buffer, sizeof(Buffer), &ReturnLength));
    alpc_observe_status("QueryInformationMessage.invalid_class", NtAlpcQueryInformationMessage(Port, &Message.Header, MaxAlpcMessageInfoClass, Buffer, sizeof(Buffer), &ReturnLength));
    alpc_observe_status("QueryInformationMessage.wrong_type_port", NtAlpcQueryInformationMessage(WrongType, &Message.Header, AlpcMessageSidInformation, Buffer, sizeof(Buffer), &ReturnLength));
    alpc_observe_status("QueryInformationMessage.closed_port", NtAlpcQueryInformationMessage(ClosedHandle, &Message.Header, AlpcMessageSidInformation, Buffer, sizeof(Buffer), &ReturnLength));
    alpc_observe_status("QueryInformationMessage.null_output_nonzero", NtAlpcQueryInformationMessage(Port, &Message.Header, AlpcMessageSidInformation, NULL, sizeof(Buffer), &ReturnLength));

    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("ConnectPort.null_name", Output, NtAlpcConnectPort(&Output, NULL, NULL, ValidAttributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("ConnectPort.invalid_flags", Output, NtAlpcConnectPort(&Output, MissingName, NULL, ValidAttributes, 1, NULL, NULL, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    BadAttributes = *ValidAttributes;
    BadAttributes.Flags = 0x80000000;
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("ConnectPort.invalid_attributes", Output, NtAlpcConnectPort(&Output, MissingName, NULL, &BadAttributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    BufferLength = sizeof(Message);
    alpc_observe_scalar_output("ConnectPort.length_without_message", Output, NtAlpcConnectPort(&Output, MissingName, NULL, ValidAttributes, 0, NULL, NULL, &BufferLength, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("ConnectPort.message_without_length", Output, NtAlpcConnectPort(&Output, MissingName, NULL, ValidAttributes, 0, NULL, &Message.Header, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    alpc_observe_status("ConnectPort.null_output", NtAlpcConnectPort(NULL, MissingName, NULL, ValidAttributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));

    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("ConnectPortEx.null_object_attributes", Output, NtAlpcConnectPortEx(&Output, NULL, NULL, ValidAttributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    alpc_observe_status("ConnectPortEx.null_output", NtAlpcConnectPortEx(NULL, MissingAttributes, NULL, ValidAttributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    BadObjectAttributes = *MissingAttributes;
    BadObjectAttributes.Length = sizeof(BadObjectAttributes) - 1;
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("ConnectPortEx.short_object_attributes", Output, NtAlpcConnectPortEx(&Output, &BadObjectAttributes, NULL, ValidAttributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, Output);

    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("AcceptConnectPort.null_request", Output, NtAlpcAcceptConnectPort(&Output, Port, 0, NULL, NULL, NULL, NULL, NULL, FALSE));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    alpc_observe_status("AcceptConnectPort.null_output", NtAlpcAcceptConnectPort(NULL, Port, 0, NULL, NULL, NULL, &Message.Header, NULL, FALSE));
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("AcceptConnectPort.invalid_flags", Output, NtAlpcAcceptConnectPort(&Output, Port, 1, NULL, NULL, NULL, &Message.Header, NULL, FALSE));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("AcceptConnectPort.wrong_type_port", Output, NtAlpcAcceptConnectPort(&Output, WrongType, 0, NULL, NULL, NULL, &Message.Header, NULL, FALSE));
    AlpcValidationCloseUnexpectedHandle(Status, Output);

    BufferLength = sizeof(Message);
    alpc_observe_status("SendWaitReceive.receive_without_length", NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, &Message.Header, NULL, NULL, &Timeout));
    alpc_observe_status("SendWaitReceive.length_without_receive", NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, NULL, &BufferLength, NULL, &Timeout));
    BufferLength = sizeof(Message);
    alpc_expect_status("SendWaitReceive.low_bit_1_masked", NtAlpcSendWaitReceivePort(Port, 1, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout), STATUS_TIMEOUT);
    BufferLength = sizeof(Message);
    alpc_expect_status("SendWaitReceive.low_bit_2_masked", NtAlpcSendWaitReceivePort(Port, 2, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout), STATUS_TIMEOUT);
    BufferLength = sizeof(Message);
    alpc_expect_status("SendWaitReceive.reply_sync", NtAlpcSendWaitReceivePort(Port, ALPC_MSGFLG_REPLY_MESSAGE | ALPC_MSGFLG_SYNC_REQUEST, &Message.Header, NULL, &Message.Header, &BufferLength, NULL, &Timeout), STATUS_INVALID_PARAMETER_2);
    BufferLength = sizeof(Message);
    alpc_expect_status("SendWaitReceive.sync_without_send", NtAlpcSendWaitReceivePort(Port, ALPC_MSGFLG_SYNC_REQUEST, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout), STATUS_INVALID_PARAMETER_2);
    alpc_expect_status("SendWaitReceive.sync_without_receive", NtAlpcSendWaitReceivePort(Port, ALPC_MSGFLG_SYNC_REQUEST, &Message.Header, NULL, NULL, NULL, NULL, &Timeout), STATUS_LPC_RECEIVE_BUFFER_EXPECTED);
    BufferLength = sizeof(Message);
    alpc_observe_status("SendWaitReceive.invalid_high_flag", NtAlpcSendWaitReceivePort(Port, 0x08000000, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout));
    alpc_observe_status("SendWaitReceive.wrong_type_port", NtAlpcSendWaitReceivePort(WrongType, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    alpc_observe_status("SendWaitReceive.closed_port", NtAlpcSendWaitReceivePort(ClosedHandle, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));

    alpc_observe_status("CancelMessage.null_context", NtAlpcCancelMessage(Port, 0, NULL));
    alpc_observe_status("CancelMessage.wrong_type_port", NtAlpcCancelMessage(WrongType, 0, &Context));
    alpc_observe_status("CancelMessage.closed_port", NtAlpcCancelMessage(ClosedHandle, 0, &Context));
    alpc_expect_status("CancelMessage.zero_id_flags_0", NtAlpcCancelMessage(Port, 0, &Context), STATUS_MESSAGE_NOT_FOUND);
    alpc_expect_status("CancelMessage.zero_id_try", NtAlpcCancelMessage(Port, ALPC_CANCELFLG_TRY_CANCEL, &Context), STATUS_MESSAGE_NOT_FOUND);
    alpc_expect_status("CancelMessage.zero_id_no_context", NtAlpcCancelMessage(Port, ALPC_CANCELFLG_NO_CONTEXT_CHECK, &Context), STATUS_MESSAGE_NOT_FOUND);
    alpc_expect_status("CancelMessage.zero_id_both", NtAlpcCancelMessage(Port, ALPC_CANCELFLG_TRY_CANCEL | ALPC_CANCELFLG_NO_CONTEXT_CHECK, &Context), STATUS_MESSAGE_NOT_FOUND);
    alpc_expect_status("CancelMessage.invalid_flag_outside_low_nibble", NtAlpcCancelMessage(Port, 0x10, &Context), STATUS_INVALID_PARAMETER);

    alpc_observe_status("ImpersonateClient.null_message", NtAlpcImpersonateClientOfPort(Port, NULL, NULL));
    alpc_observe_status("ImpersonateClient.wrong_type_port", NtAlpcImpersonateClientOfPort(WrongType, &Message.Header, NULL));
    alpc_observe_status("ImpersonateContainer.null_message", NtAlpcImpersonateClientContainerOfPort(Port, NULL, 0));
    alpc_observe_status("ImpersonateContainer.invalid_flags", NtAlpcImpersonateClientContainerOfPort(Port, &Message.Header, 1));

    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("OpenSenderProcess.null_message", Output, NtAlpcOpenSenderProcess(&Output, Port, NULL, 0, PROCESS_QUERY_LIMITED_INFORMATION, NULL));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    alpc_observe_status("OpenSenderProcess.null_output", NtAlpcOpenSenderProcess(NULL, Port, &Message.Header, 0, PROCESS_QUERY_LIMITED_INFORMATION, NULL));
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("OpenSenderProcess.invalid_flags", Output, NtAlpcOpenSenderProcess(&Output, Port, &Message.Header, 1, PROCESS_QUERY_LIMITED_INFORMATION, NULL));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("OpenSenderThread.null_message", Output, NtAlpcOpenSenderThread(&Output, Port, NULL, 0, THREAD_QUERY_LIMITED_INFORMATION, NULL));
    AlpcValidationCloseUnexpectedHandle(Status, Output);
    alpc_observe_status("OpenSenderThread.null_output", NtAlpcOpenSenderThread(NULL, Port, &Message.Header, 0, THREAD_QUERY_LIMITED_INFORMATION, NULL));
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_scalar_output("OpenSenderThread.invalid_flags", Output, NtAlpcOpenSenderThread(&Output, Port, &Message.Header, 1, THREAD_QUERY_LIMITED_INFORMATION, NULL));
    AlpcValidationCloseUnexpectedHandle(Status, Output);

    if (AlpcTestNativeObservationEnabled())
    {
        RtlZeroMemory(Buffer, sizeof(Buffer));
        alpc_observe_status("QueryInformationMessage.misaligned_message", NtAlpcQueryInformationMessage(Port, (PPORT_MESSAGE)(Buffer + 1), AlpcMessageSidInformation, Buffer, sizeof(Buffer), &ReturnLength));
    }
    else
    {
        skip("set ALPC_TEST_NATIVE_OBSERVE=1 to run transport misalignment rows\n");
    }
}

START_TEST(NtAlpcValidation)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcValidation");
    static UNICODE_STRING MissingName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestMissingAlpcPort");
    ALPC_PORT_ASSOCIATE_COMPLETION_PORT Associate;
    ALPC_PORT_ATTRIBUTES Attributes;
    ALPC_SECURITY_ATTR SecurityAttribute;
    ALPC_DATA_VIEW_ATTR ViewAttribute;
    ALPC_CONTEXT_ATTR ContextAttribute;
    ALPC_BASIC_INFORMATION BasicInformation;
    ALPC_TEST_MESSAGE Message;
    OBJECT_ATTRIBUTES ObjectAttributes;
    OBJECT_ATTRIBUTES MissingAttributes;
    LARGE_INTEGER Timeout;
    ALPC_HANDLE Resource;
    ALPC_TEST_RESERVE_OUTPUT ReserveOutput;
    SIZE_T ActualSize;
    SIZE_T BufferLength;
    ULONG ReturnLength;
    NTSTATUS Status;
    HANDLE Port = NULL;
    HANDLE OutputHandle = NULL;
    HANDLE InvalidHandle = (HANDLE)(LONG_PTR)-1;
    HANDLE WrongTypeHandle = NULL;
    HANDLE ClosedHandle = InvalidHandle;
    HANDLE TemporaryHandle = NULL;
    HANDLE NoAccessPort = NULL;

    if (!AlpcTestIsChildMode("validation-matrix"))
    {
        AlpcTestRunIsolatedCase(L"NtAlpcValidation", L"validation-matrix", ALPC_TEST_CHILD_TIMEOUT_MS);
        return;
    }

    AlpcTestInitializePortAttributes(&Attributes, 0);
    InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    InitializeObjectAttributes(&MissingAttributes, &MissingName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    RtlZeroMemory(&Message, sizeof(Message));
    RtlZeroMemory(&ContextAttribute, sizeof(ContextAttribute));
    RtlZeroMemory(&ViewAttribute, sizeof(ViewAttribute));
    RtlZeroMemory(&SecurityAttribute, sizeof(SecurityAttribute));
    RtlZeroMemory(&Associate, sizeof(Associate));
    Timeout.QuadPart = 0;
    BufferLength = sizeof(Message);
    ReturnLength = 0;
    Resource = NULL;
    ActualSize = 0;

    AlpcValidationCreatePortMatrix();

    alpc_observe_status("CreatePort.null_output", NtAlpcCreatePort(NULL, NULL, NULL));
    alpc_observe_status("CreatePort.valid", NtAlpcCreatePort(&Port, &ObjectAttributes, &Attributes));
    if (!NT_SUCCESS(Status))
        return;

    WrongTypeHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(WrongTypeHandle != NULL, "CreateEventW failed: %lu\n", GetLastError());
    Status = NtDuplicateObject(NtCurrentProcess(), Port, NtCurrentProcess(), &NoAccessPort, 0, 0, 0);
    trace("ALPC_OBSERVE status Validation.duplicate_no_access=%08lx output=%p\n", Status, NoAccessPort);
    TemporaryHandle = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (TemporaryHandle)
    {
        ClosedHandle = TemporaryHandle;
        CloseHandle(TemporaryHandle);
        TemporaryHandle = NULL;
    }

    AlpcValidationInformationMatrix(Port, WrongTypeHandle ? WrongTypeHandle : InvalidHandle, ClosedHandle, NoAccessPort);
    AlpcValidationResourceMatrix(Port, WrongTypeHandle ? WrongTypeHandle : InvalidHandle, ClosedHandle);
    AlpcValidationTransportMatrix(Port, WrongTypeHandle ? WrongTypeHandle : InvalidHandle, ClosedHandle, &MissingName, &MissingAttributes, &Attributes);

    alpc_observe_status("Disconnect.invalid_handle", NtAlpcDisconnectPort(InvalidHandle, 0));
    alpc_observe_status("Disconnect.invalid_flags", NtAlpcDisconnectPort(Port, 2));

    alpc_observe_status("QueryInformation.null_handle", NtAlpcQueryInformation(NULL, AlpcBasicInformation, &BasicInformation, sizeof(BasicInformation), &ReturnLength));
    alpc_observe_status("QueryInformation.invalid_class", NtAlpcQueryInformation(Port, MaxAlpcPortInfoClass, &BasicInformation, sizeof(BasicInformation), &ReturnLength));
    alpc_observe_status("QueryInformation.short_buffer", NtAlpcQueryInformation(Port, AlpcBasicInformation, &BasicInformation, sizeof(BasicInformation) - 1, &ReturnLength));

    alpc_observe_status("SetInformation.invalid_handle", NtAlpcSetInformation(InvalidHandle, AlpcAssociateCompletionPortInformation, &Associate, sizeof(Associate)));
    alpc_observe_status("SetInformation.invalid_class", NtAlpcSetInformation(Port, MaxAlpcPortInfoClass, NULL, 0));
    alpc_observe_status("SetInformation.short_associate", NtAlpcSetInformation(Port, AlpcAssociateCompletionPortInformation, &Associate, sizeof(Associate) - 1));

    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    ActualSize = (SIZE_T)0x5555555555555555ULL;
    alpc_observe_status("CreatePortSection.invalid_handle", NtAlpcCreatePortSection(InvalidHandle, 0, NULL, 0x1000, &Resource, &ActualSize));
    Resource = (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    ActualSize = (SIZE_T)0x5555555555555555ULL;
    alpc_observe_status("CreatePortSection.zero_size", NtAlpcCreatePortSection(Port, 0, NULL, 0, &Resource, &ActualSize));
    if (NT_SUCCESS(Status) && Resource != (ALPC_HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtAlpcDeletePortSection(Port, 0, Resource);
    alpc_observe_status("DeletePortSection.invalid_handle", NtAlpcDeletePortSection(InvalidHandle, 0, (ALPC_HANDLE)(ULONG_PTR)1));
    alpc_observe_status("DeletePortSection.unknown_resource", NtAlpcDeletePortSection(Port, 0, (ALPC_HANDLE)(ULONG_PTR)1));

    ReserveOutput.ResourceId = 0x55555555;
    ReserveOutput.Guard = 0xa5a5a5a5;
    alpc_observe_status("CreateResourceReserve.invalid_handle", NtAlpcCreateResourceReserve(InvalidHandle, 0, 0x100, &ReserveOutput.ResourceId));
    ok_eq_hex(ReserveOutput.Guard, 0xa5a5a5a5);
    ReserveOutput.ResourceId = 0x55555555;
    ReserveOutput.Guard = 0xa5a5a5a5;
    alpc_observe_status("CreateResourceReserve.zero_size", NtAlpcCreateResourceReserve(Port, 0, 0, &ReserveOutput.ResourceId));
    ok_eq_hex(ReserveOutput.Guard, 0xa5a5a5a5);
    if (NT_SUCCESS(Status))
        NtAlpcDeleteResourceReserve(Port, 0, ReserveOutput.ResourceId);
    alpc_observe_status("DeleteResourceReserve.invalid_handle", NtAlpcDeleteResourceReserve(InvalidHandle, 0, 1));
    alpc_observe_status("DeleteResourceReserve.unknown_resource", NtAlpcDeleteResourceReserve(Port, 0, 1));

    alpc_observe_status("CreateSectionView.invalid_handle", NtAlpcCreateSectionView(InvalidHandle, 0, &ViewAttribute));
    alpc_observe_status("CreateSectionView.unknown_section", NtAlpcCreateSectionView(Port, 0, &ViewAttribute));
    if (NT_SUCCESS(Status) && ViewAttribute.ViewBase)
        NtAlpcDeleteSectionView(Port, 0, ViewAttribute.ViewBase);
    alpc_observe_status("DeleteSectionView.invalid_handle", NtAlpcDeleteSectionView(InvalidHandle, 0, NULL));
    alpc_observe_status("DeleteSectionView.unknown_view", NtAlpcDeleteSectionView(Port, 0, (PVOID)(ULONG_PTR)1));

    alpc_observe_status("CreateSecurityContext.invalid_handle", NtAlpcCreateSecurityContext(InvalidHandle, 0, &SecurityAttribute));
    alpc_observe_status("DeleteSecurityContext.invalid_handle", NtAlpcDeleteSecurityContext(InvalidHandle, 0, (ALPC_HANDLE)(ULONG_PTR)1));
    alpc_observe_status("DeleteSecurityContext.unknown_context", NtAlpcDeleteSecurityContext(Port, 0, (ALPC_HANDLE)(ULONG_PTR)1));
    alpc_observe_status("RevokeSecurityContext.invalid_handle", NtAlpcRevokeSecurityContext(InvalidHandle, 0, (ALPC_HANDLE)(ULONG_PTR)1));
    alpc_observe_status("RevokeSecurityContext.unknown_context", NtAlpcRevokeSecurityContext(Port, 0, (ALPC_HANDLE)(ULONG_PTR)1));

    alpc_observe_status("QueryInformationMessage.invalid_handle", NtAlpcQueryInformationMessage(InvalidHandle, &Message.Header, AlpcMessageSidInformation, NULL, 0, &ReturnLength));
    alpc_observe_status("QueryInformationMessage.unknown_message", NtAlpcQueryInformationMessage(Port, &Message.Header, AlpcMessageSidInformation, NULL, 0, &ReturnLength));

    OutputHandle = NULL;
    alpc_observe_status("ConnectPort.missing_name", NtAlpcConnectPort(&OutputHandle, &MissingName, NULL, &Attributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, OutputHandle);
    OutputHandle = NULL;
    alpc_observe_status("ConnectPortEx.missing_name", NtAlpcConnectPortEx(&OutputHandle, &MissingAttributes, NULL, &Attributes, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    AlpcValidationCloseUnexpectedHandle(Status, OutputHandle);

    OutputHandle = NULL;
    alpc_observe_status("AcceptConnectPort.invalid_handle", NtAlpcAcceptConnectPort(&OutputHandle, InvalidHandle, 0, NULL, NULL, NULL, &Message.Header, NULL, FALSE));
    AlpcValidationCloseUnexpectedHandle(Status, OutputHandle);
    alpc_observe_status("SendWaitReceive.invalid_handle", NtAlpcSendWaitReceivePort(InvalidHandle, 0, NULL, NULL, NULL, NULL, NULL, &Timeout));
    BufferLength = sizeof(Message);
    alpc_expect_status("SendWaitReceive.low_bit_4_masked", NtAlpcSendWaitReceivePort(Port, 4, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout), STATUS_TIMEOUT);
    alpc_observe_status("CancelMessage.invalid_handle", NtAlpcCancelMessage(InvalidHandle, 0, &ContextAttribute));
    alpc_expect_status("CancelMessage.invalid_flags", NtAlpcCancelMessage(Port, 0x10, &ContextAttribute), STATUS_INVALID_PARAMETER);
    alpc_observe_status("ImpersonateClient.invalid_handle", NtAlpcImpersonateClientOfPort(InvalidHandle, &Message.Header, NULL));
    alpc_observe_status("ImpersonateContainer.invalid_handle", NtAlpcImpersonateClientContainerOfPort(InvalidHandle, &Message.Header, 0));
    OutputHandle = NULL;
    alpc_observe_status("OpenSenderProcess.invalid_handle", NtAlpcOpenSenderProcess(&OutputHandle, InvalidHandle, &Message.Header, 0, PROCESS_QUERY_LIMITED_INFORMATION, NULL));
    AlpcValidationCloseUnexpectedHandle(Status, OutputHandle);
    OutputHandle = NULL;
    alpc_observe_status("OpenSenderThread.invalid_handle", NtAlpcOpenSenderThread(&OutputHandle, InvalidHandle, &Message.Header, 0, THREAD_QUERY_LIMITED_INFORMATION, NULL));
    AlpcValidationCloseUnexpectedHandle(Status, OutputHandle);

    if (NoAccessPort)
        NtClose(NoAccessPort);
    if (WrongTypeHandle)
        CloseHandle(WrongTypeHandle);
    NtClose(Port);
}
