/*
 * PROJECT:     LiberNT Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USB Type-C port controller interface KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "classlibrary.h"
#include <UcmTcpciCx.h>
#include <acpiioct.h>

#define TCPCI_TAG 'pcTU'
#define TCPCI_DEBOUNCE_MS 150
#define TCPCI_MAX_STEPS 8
#define TCPCI_MAX_EVENTS 32

#define TCPCI_POWER_VBUS 0x04
#define TCPCI_POWER_DETECTION 0x08

#define TCPCI_ROLE_DRP 0x40
#define TCPCI_ROLE_RP_RP 0x05
#define TCPCI_ROLE_RD_RD 0x0A

#define TCPCI_POWER_CONTROL_ATTACHED 0x70
#define TCPCI_POWER_CONTROL_DETACHED 0x60
#define TCPCI_POWER_CONTROL_VCONN 0x03

#define TCPCI_SEND_IDLE 0
#define TCPCI_SEND_SENDING 1
#define TCPCI_SEND_COMPLETED 2
#define TCPCI_SEND_PENDING 3

typedef enum _TCPCI_STATE
{
    TcpciStopped,
    TcpciStarting,
    TcpciWaitDetection,
    TcpciUnattached,
    TcpciAttachWait,
    TcpciUnsupported,
    TcpciSourceWaitVbus,
    TcpciAttachedSource,
    TcpciAttachedSink
} TCPCI_STATE;

typedef enum _TCPCI_EVENT_TYPE
{
    TcpciEventStart,
    TcpciEventCc,
    TcpciEventPower,
    TcpciEventFault,
    TcpciEventDebounce
} TCPCI_EVENT_TYPE;

typedef enum _TCPCI_PROGRAM
{
    TcpciProgramNone,
    TcpciProgramStart,
    TcpciProgramDetect,
    TcpciProgramUnattach,
    TcpciProgramRestart,
    TcpciProgramAttachSource,
    TcpciProgramAttachSink
} TCPCI_PROGRAM;

typedef enum _TCPCI_CANDIDATE
{
    TcpciCandidateLooking,
    TcpciCandidateOpen,
    TcpciCandidateSource,
    TcpciCandidateSink,
    TcpciCandidateUnsupported
} TCPCI_CANDIDATE;

typedef struct _TCPCI_EVENT
{
    TCPCI_EVENT_TYPE Type;
    ULONG Value;
} TCPCI_EVENT, *PTCPCI_EVENT;

typedef struct _TCPCI_STEP
{
    ULONG Code;
    ULONG Type;
    ULONG Value;
} TCPCI_STEP, *PTCPCI_STEP;

typedef struct _TCPCI_CONNECTOR
{
    ULONG Address;
    ULONG OperatingModes;
    ULONG Sourcing;
    ULONG AudioAccessory;
    ULONG PowerDelivery;
    ULONG PowerRoles;
    ULONG SourcePdoCount;
    ULONG SourcePdos[7];
    ULONG SinkPdoCount;
    ULONG SinkPdos[7];
    ULONG AlternateModeCount;
    ULONG AlternateModes[16];
    ULONG LocationLength;
    UCHAR Location[20];
} TCPCI_CONNECTOR, *PTCPCI_CONNECTOR;

typedef struct _TCPCI_PORT_CONTEXT
{
    WDFDEVICE Device;
    UCMTCPCIPORTCONTROLLER Handle;
    UCMTCPCI_PORT_CONTROLLER_IDENTIFICATION Identification;
    UCMTCPCI_PORT_CONTROLLER_CAPABILITIES Capabilities;
    WDFQUEUE Queue;
    KSPIN_LOCK Lock;
    TCPCI_STATE State;
    BOOLEAN Busy;
    BOOLEAN Starting;
    BOOLEAN Debounced;
    KEVENT IdleEvent;
    KEVENT StartEvent;
    TCPCI_EVENT Events[TCPCI_MAX_EVENTS];
    ULONG EventHead;
    ULONG EventCount;
    TCPCI_STEP Steps[TCPCI_MAX_STEPS];
    ULONG StepCount;
    ULONG StepNext;
    TCPCI_PROGRAM Program;
    LONG SendState;
    NTSTATUS StepStatus;
    PIRP Irp;
    PDEVICE_OBJECT Top;
    UCHAR Buffer[64];
    UCHAR Cc;
    UCHAR Power;
    UCHAR Fault;
    UCHAR RpValue;
    TCPCI_CANDIDATE Candidate;
    UCHAR CandidateCc;
    ULONGLONG ArmTime;
    KTIMER Timer;
    KDPC Dpc;
} TCPCI_PORT_CONTEXT, *PTCPCI_PORT_CONTEXT;

typedef struct _TCPCI_DEVICE_CONTEXT
{
    UCMTCPCIPORTCONTROLLER Port;
    TCPCI_CONNECTOR Connector;
} TCPCI_DEVICE_CONTEXT, *PTCPCI_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TCPCI_PORT_CONTEXT, TcpciGetPortContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TCPCI_DEVICE_CONTEXT, TcpciGetDeviceContext)

static const UCHAR TcpciConnectorUuid[16] =
{
    0x62, 0x6E, 0x85, 0x6B, 0xF4, 0x40, 0x88, 0x46, 0xBD, 0x46, 0x5E, 0x88, 0x8A, 0x22, 0x60, 0xDE
};

static VOID TcpciRun(_Inout_ PTCPCI_PORT_CONTEXT Port);

static
NTSTATUS
NTAPI
TcpciEvtWdmIrpDispatch(
    _In_ WDFDEVICE Device,
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ ULONG Code,
    _In_ WDFCONTEXT DriverContext,
    _Inout_ PIRP Irp,
    _In_ WDFCONTEXT DispatchContext)
{
    PTCPCI_DEVICE_CONTEXT DeviceContext = TcpciGetDeviceContext(Device);
    PTCPCI_PORT_CONTEXT Port;

    UNREFERENCED_PARAMETER(MajorFunction);
    UNREFERENCED_PARAMETER(MinorFunction);
    UNREFERENCED_PARAMETER(Code);
    UNREFERENCED_PARAMETER(DriverContext);

    if (DeviceContext != NULL && DeviceContext->Port != NULL)
    {
        Port = TcpciGetPortContext(DeviceContext->Port);
        if (Port->Queue != NULL && Irp == Port->Irp)
            return WdfDeviceWdmDispatchIrpToIoQueue(Device, Irp, Port->Queue, WDF_DISPATCH_IRP_TO_IO_QUEUE_NO_FLAGS);
    }

    return WdfDeviceWdmDispatchIrp(Device, Irp, DispatchContext);
}

static
NTSTATUS
TcpciAcpiRequest(
    _In_ WDFDEVICE Device,
    _In_ ULONG Code,
    _In_ PVOID Input,
    _In_ ULONG InputLength,
    _Out_ PVOID Output,
    _In_ ULONG OutputLength)
{
    PDEVICE_OBJECT Lower = WdfDeviceWdmGetAttachedDevice(Device);
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    if (Lower == NULL)
        return STATUS_INVALID_DEVICE_REQUEST;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(Code, Lower, Input, InputLength, Output, OutputLength, FALSE, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(Lower, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
TcpciAcpiEvaluate(
    _In_ WDFDEVICE Device,
    _In_ PCSTR Path,
    _In_ PCSTR Leaf,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *Result)
{
    ACPI_EVAL_INPUT_BUFFER_EX Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    ULONG Length = 512;
    NTSTATUS Status;

    *Result = NULL;
    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
    Status = RtlStringCbPrintfA(Input.MethodName, sizeof(Input.MethodName), "%s.%s", Path, Leaf);
    if (!NT_SUCCESS(Status))
        return Status;

    for (;;)
    {
        Output = ExAllocatePoolZero(NonPagedPoolNx, Length, TCPCI_TAG);
        if (Output == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = TcpciAcpiRequest(Device, IOCTL_ACPI_EVAL_METHOD_EX, &Input, sizeof(Input), Output, Length);
        if (Status != STATUS_BUFFER_OVERFLOW || Output->Length <= Length)
            break;

        Length = Output->Length;
        ExFreePoolWithTag(Output, TCPCI_TAG);
    }

    if (NT_SUCCESS(Status) &&
        (Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
         Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
         Output->Length > Length))
    {
        Status = STATUS_ACPI_INVALID_DATA;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Output, TCPCI_TAG);
        return Status;
    }

    *Result = Output;
    return STATUS_SUCCESS;
}

static
PACPI_METHOD_ARGUMENT
TcpciNextArgument(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _In_ PUCHAR End)
{
    PUCHAR Next;

    if ((PUCHAR)Argument + sizeof(ACPI_METHOD_ARGUMENT) > End)
        return NULL;

    Next = (PUCHAR)ACPI_METHOD_NEXT_ARGUMENT(Argument);
    if (Next > End)
        return NULL;

    return (PACPI_METHOD_ARGUMENT)Next;
}

static
BOOLEAN
TcpciArgumentValid(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _In_ PUCHAR End)
{
    return (PUCHAR)Argument + sizeof(ACPI_METHOD_ARGUMENT) <= End &&
           (PUCHAR)Argument + FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) + Argument->DataLength <= End;
}

static
BOOLEAN
TcpciArgumentInteger(
    _In_ PACPI_METHOD_ARGUMENT Argument,
    _Out_ PULONG Value)
{
    if (Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER || Argument->DataLength < sizeof(ULONG))
        return FALSE;

    RtlCopyMemory(Value, Argument->Data, sizeof(ULONG));
    return TRUE;
}

static
ULONG
TcpciReadList(
    _In_ PACPI_METHOD_ARGUMENT Package,
    _Out_writes_(Capacity) PULONG Values,
    _In_ ULONG Capacity)
{
    PUCHAR End = Package->Data + Package->DataLength;
    PACPI_METHOD_ARGUMENT Item = (PACPI_METHOD_ARGUMENT)Package->Data;
    ULONG Count = 0;

    if (Package->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        return 0;

    while (Item != NULL && (PUCHAR)Item < End && TcpciArgumentValid(Item, End) && Count < Capacity)
    {
        if (!TcpciArgumentInteger(Item, &Values[Count]))
            break;

        Count++;
        Item = TcpciNextArgument(Item, End);
    }

    return Count;
}

static
NTSTATUS
TcpciParseCapabilities(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _Out_ PTCPCI_CONNECTOR Connector)
{
    PUCHAR End = (PUCHAR)Output + Output->Length;
    PACPI_METHOD_ARGUMENT Uuid = Output->Argument;
    PACPI_METHOD_ARGUMENT List, Entry, Key, Data;
    PUCHAR ListEnd, EntryEnd;
    ULONG Id, Value;

    if (Output->Count < 2 || !TcpciArgumentValid(Uuid, End) ||
        Uuid->Type != ACPI_METHOD_ARGUMENT_BUFFER || Uuid->DataLength != sizeof(TcpciConnectorUuid) ||
        RtlCompareMemory(Uuid->Data, TcpciConnectorUuid, sizeof(TcpciConnectorUuid)) != sizeof(TcpciConnectorUuid))
    {
        return STATUS_ACPI_INVALID_DATA;
    }

    List = TcpciNextArgument(Uuid, End);
    if (List == NULL || !TcpciArgumentValid(List, End) || List->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        return STATUS_ACPI_INVALID_DATA;

    ListEnd = List->Data + List->DataLength;
    Entry = (PACPI_METHOD_ARGUMENT)List->Data;
    while (Entry != NULL && (PUCHAR)Entry < ListEnd && TcpciArgumentValid(Entry, ListEnd))
    {
        if (Entry->Type == ACPI_METHOD_ARGUMENT_PACKAGE)
        {
            EntryEnd = Entry->Data + Entry->DataLength;
            Key = (PACPI_METHOD_ARGUMENT)Entry->Data;
            Data = TcpciArgumentValid(Key, EntryEnd) ? TcpciNextArgument(Key, EntryEnd) : NULL;
            if (Data != NULL && (PUCHAR)Data < EntryEnd && TcpciArgumentValid(Data, EntryEnd) && TcpciArgumentInteger(Key, &Id))
            {
                Value = 0;
                switch (Id)
                {
                    case 1:
                        if (TcpciArgumentInteger(Data, &Value))
                            Connector->OperatingModes = Value;
                        break;
                    case 2:
                        if (TcpciArgumentInteger(Data, &Value))
                            Connector->Sourcing = Value;
                        break;
                    case 3:
                        if (TcpciArgumentInteger(Data, &Value))
                            Connector->AudioAccessory = Value;
                        break;
                    case 4:
                        if (TcpciArgumentInteger(Data, &Value))
                            Connector->PowerDelivery = Value;
                        break;
                    case 5:
                        if (TcpciArgumentInteger(Data, &Value))
                            Connector->PowerRoles = Value;
                        break;
                    case 6:
                        Connector->SourcePdoCount = TcpciReadList(Data, Connector->SourcePdos,
                                                                  RTL_NUMBER_OF(Connector->SourcePdos));
                        break;
                    case 7:
                        Connector->SinkPdoCount = TcpciReadList(Data, Connector->SinkPdos, RTL_NUMBER_OF(Connector->SinkPdos));
                        break;
                    case 8:
                        Connector->AlternateModeCount = TcpciReadList(Data, Connector->AlternateModes,
                                                                      RTL_NUMBER_OF(Connector->AlternateModes)) / 2;
                        break;
                }
            }
        }

        Entry = TcpciNextArgument(Entry, ListEnd);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
TcpciReadConnector(
    _In_ WDFDEVICE Device,
    _Out_ PTCPCI_CONNECTOR Connector)
{
    ACPI_ENUM_CHILDREN_OUTPUT_BUFFER Small;
    PACPI_ENUM_CHILDREN_OUTPUT_BUFFER Children;
    ACPI_ENUM_CHILDREN_INPUT_BUFFER Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_ENUM_CHILD Child;
    NTSTATUS Status, Found = STATUS_NOT_FOUND;
    ULONG Length, Index;
    PUCHAR End;

    RtlZeroMemory(Connector, sizeof(*Connector));
    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE;
    Input.Flags = ENUM_CHILDREN_IMMEDIATE_ONLY;
    RtlZeroMemory(&Small, sizeof(Small));
    Status = TcpciAcpiRequest(Device, IOCTL_ACPI_ENUM_CHILDREN, &Input, sizeof(Input), &Small, sizeof(Small));
    if (Status != STATUS_BUFFER_OVERFLOW)
        return NT_SUCCESS(Status) ? STATUS_NOT_FOUND : Status;

    Length = Small.NumberOfChildren;
    if (Small.Signature != ACPI_ENUM_CHILDREN_OUTPUT_BUFFER_SIGNATURE || Length < sizeof(Small) || Length > 0x10000)
        return STATUS_ACPI_INVALID_DATA;

    Children = ExAllocatePoolZero(NonPagedPoolNx, Length, TCPCI_TAG);
    if (Children == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = TcpciAcpiRequest(Device, IOCTL_ACPI_ENUM_CHILDREN, &Input, sizeof(Input), Children, Length);
    if (!NT_SUCCESS(Status) || Children->Signature != ACPI_ENUM_CHILDREN_OUTPUT_BUFFER_SIGNATURE)
    {
        ExFreePoolWithTag(Children, TCPCI_TAG);
        return NT_SUCCESS(Status) ? STATUS_ACPI_INVALID_DATA : Status;
    }

    End = (PUCHAR)Children + Length;
    Child = Children->Children;
    for (Index = 0; Index < Children->NumberOfChildren; Index++)
    {
        if ((PUCHAR)Child + FIELD_OFFSET(ACPI_ENUM_CHILD, Name) > End ||
            Child->NameLength == 0 ||
            Child->NameLength > (ULONG)(End - (PUCHAR)Child->Name) ||
            Child->Name[Child->NameLength - 1] != ANSI_NULL)
        {
            break;
        }

        if (Index != 0 && (Child->Flags & ACPI_OBJECT_HAS_CHILDREN) != 0)
        {
            Status = TcpciAcpiEvaluate(Device, Child->Name, "_ADR", &Output);
            if (NT_SUCCESS(Status))
            {
                if (Output->Count >= 1 &&
                    TcpciArgumentValid(Output->Argument, (PUCHAR)Output + Output->Length))
                {
                    TcpciArgumentInteger(Output->Argument, &Connector->Address);
                }
                ExFreePoolWithTag(Output, TCPCI_TAG);

                Status = TcpciAcpiEvaluate(Device, Child->Name, "_PLD", &Output);
                if (NT_SUCCESS(Status))
                {
                    if (Output->Count >= 1 &&
                        TcpciArgumentValid(Output->Argument, (PUCHAR)Output + Output->Length) &&
                        Output->Argument[0].Type == ACPI_METHOD_ARGUMENT_BUFFER)
                    {
                        Connector->LocationLength = min(Output->Argument[0].DataLength, sizeof(Connector->Location));
                        RtlCopyMemory(Connector->Location, Output->Argument[0].Data, Connector->LocationLength);
                    }
                    ExFreePoolWithTag(Output, TCPCI_TAG);
                }

                Status = TcpciAcpiEvaluate(Device, Child->Name, "_DSD", &Output);
                if (NT_SUCCESS(Status))
                {
                    Found = TcpciParseCapabilities(Output, Connector);
                    ExFreePoolWithTag(Output, TCPCI_TAG);
                    if (NT_SUCCESS(Found))
                        break;
                }
                else
                {
                    Found = Status;
                }
            }
        }

        Child = ACPI_ENUM_CHILD_NEXT(Child);
    }

    ExFreePoolWithTag(Children, TCPCI_TAG);
    return Found;
}

static
NTSTATUS
NTAPI
TcpciDeviceInitInitialize(
    _In_ PUCMTCPCI_DRIVER_GLOBALS DriverGlobals,
    _In_ PWDFDEVICE_INIT DeviceInit)
{
    UNREFERENCED_PARAMETER(DriverGlobals);

    if (DeviceInit == NULL)
        return STATUS_INVALID_PARAMETER;

    return WdfCxDeviceInitAllocate(WdfDriverGlobals, DeviceInit) != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

static
NTSTATUS
NTAPI
TcpciDeviceInitialize(
    _In_ PUCMTCPCI_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE WdfDevice,
    _In_ PUCMTCPCI_DEVICE_CONFIG Config)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    PTCPCI_DEVICE_CONTEXT Context;
    TCPCI_CONNECTOR Connector;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (WdfDevice == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;

    if (TcpciGetDeviceContext(WdfDevice) != NULL)
        return STATUS_SUCCESS;

    Status = TcpciReadConnector(WdfDevice, &Connector);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, TCPCI_DEVICE_CONTEXT);
    Status = WdfObjectAllocateContext(WdfDevice, &Attributes, (PVOID *)&Context);
    if (!NT_SUCCESS(Status))
        return Status;

    Context->Connector = Connector;
    return WdfDeviceConfigureWdmIrpDispatchCallback(WdfDevice,
                                                    WdfGetDriver(),
                                                    IRP_MJ_DEVICE_CONTROL,
                                                    TcpciEvtWdmIrpDispatch,
                                                    NULL);
}

static
VOID
TcpciAddStep(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ ULONG Code,
    _In_ ULONG Type,
    _In_ ULONG Value)
{
    if (Port->StepCount < TCPCI_MAX_STEPS)
    {
        Port->Steps[Port->StepCount].Code = Code;
        Port->Steps[Port->StepCount].Type = Type;
        Port->Steps[Port->StepCount].Value = Value;
        Port->StepCount++;
    }
}

static
VOID
TcpciBegin(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ TCPCI_PROGRAM Program)
{
    Port->StepCount = 0;
    Port->StepNext = 0;
    Port->Program = Program;
}

static
VOID
TcpciAddUnattach(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerRoleControl,
                 TCPCI_ROLE_DRP | TCPCI_ROLE_RD_RD | (Port->RpValue << 4));
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_COMMAND, 0, UcmTcpciPortControllerCommandLook4Connection);
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_GET_STATUS, 0, 0);
}

static
VOID
TcpciLoadUnattach(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    TcpciBegin(Port, TcpciProgramUnattach);
    TcpciAddUnattach(Port);
}

static
VOID
TcpciLoadRestart(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ UCHAR Cc)
{
    UCMTCPCI_PORT_CONTROLLER_CC_STATUS Status;

    Status.AsUInt8 = Cc;
    TcpciBegin(Port, TcpciProgramRestart);
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerRoleControl,
                 TCPCI_ROLE_DRP | (Port->RpValue << 4) |
                 (Status.ConnectResult == UcmTcpciPortControllerConnectResultPresentingRd ? TCPCI_ROLE_RP_RP : TCPCI_ROLE_RD_RD));
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_COMMAND, 0, UcmTcpciPortControllerCommandLook4Connection);
}

static
VOID
TcpciLoadAttach(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    UCMTCPCI_PORT_CONTROLLER_CC_STATUS Status;
    BOOLEAN Second, Vconn = FALSE;

    Status.AsUInt8 = Port->CandidateCc;
    if (Port->Candidate == TcpciCandidateSource)
    {
        Second = Status.CC2State == UcmTcpciPortControllerCCStateSrcRd;
        Vconn = (Second ? Status.CC1State : Status.CC2State) == UcmTcpciPortControllerCCStateSrcRa;
        TcpciBegin(Port, TcpciProgramAttachSource);
        TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerRoleControl,
                     TCPCI_ROLE_RP_RP | (Port->RpValue << 4));
    }
    else
    {
        Second = Status.CC1State == UcmTcpciPortControllerCCStateOpen;
        TcpciBegin(Port, TcpciProgramAttachSink);
        TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerRoleControl, TCPCI_ROLE_RD_RD);
    }

    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerTcpcControl, Second ? 1 : 0);
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerPowerControl,
                 TCPCI_POWER_CONTROL_ATTACHED | (Vconn ? TCPCI_POWER_CONTROL_VCONN : 0));
    if (Port->Candidate == TcpciCandidateSource)
    {
        TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_COMMAND, 0,
                     UcmTcpciPortControllerCommandSourceVbusDefaultVoltage);
    }
}

static
VOID
TcpciLoadDetach(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ BOOLEAN Source,
    _In_ BOOLEAN Pending)
{
    TcpciBegin(Port, TcpciProgramUnattach);
    if (Pending)
        TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_COMMAND, 0, UcmTcpciPortControllerCommandDisableSourceVbus);
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerPowerControl,
                 TCPCI_POWER_CONTROL_ATTACHED);
    if (Source)
        TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_COMMAND, 0, UcmTcpciPortControllerCommandDisableSourceVbus);
    TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL, UcmTcpciPortControllerPowerControl,
                 TCPCI_POWER_CONTROL_DETACHED);
    TcpciAddUnattach(Port);
}

static
TCPCI_CANDIDATE
TcpciClassify(
    _In_ UCHAR Cc)
{
    UCMTCPCI_PORT_CONTROLLER_CC_STATUS Status;

    Status.AsUInt8 = Cc;
    if (Status.Looking4Connection)
        return TcpciCandidateLooking;
    if (Status.CC1State == UcmTcpciPortControllerCCStateOpen && Status.CC2State == UcmTcpciPortControllerCCStateOpen)
        return TcpciCandidateOpen;

    if (Status.ConnectResult == UcmTcpciPortControllerConnectResultPresentingRd)
    {
        if (Status.CC1State != UcmTcpciPortControllerCCStateOpen && Status.CC2State != UcmTcpciPortControllerCCStateOpen)
            return TcpciCandidateUnsupported;
        return TcpciCandidateSink;
    }

    if ((Status.CC1State == UcmTcpciPortControllerCCStateSrcRd) != (Status.CC2State == UcmTcpciPortControllerCCStateSrcRd))
        return TcpciCandidateSource;

    return TcpciCandidateUnsupported;
}

static
VOID
TcpciArmDebounce(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    LARGE_INTEGER Due;

    Port->Debounced = FALSE;
    Port->ArmTime = KeQueryInterruptTime();
    Due.QuadPart = -10000LL * TCPCI_DEBOUNCE_MS;
    KeSetTimer(&Port->Timer, Due, &Port->Dpc);
}

static
VOID
TcpciDisarmDebounce(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    KeCancelTimer(&Port->Timer);
}

static
VOID
TcpciFinishStart(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    if (Port->Starting)
    {
        Port->Starting = FALSE;
        KeSetEvent(&Port->StartEvent, IO_NO_INCREMENT, FALSE);
    }
}

static
VOID
TcpciFail(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    KIRQL Irql;

    KeAcquireSpinLock(&Port->Lock, &Irql);
    Port->State = TcpciStopped;
    Port->EventCount = 0;
    KeReleaseSpinLock(&Port->Lock, Irql);
    KeCancelTimer(&Port->Timer);
    Port->StepCount = Port->StepNext = 0;
    Port->Program = TcpciProgramNone;
    TcpciFinishStart(Port);
    WdfDeviceSetFailed(Port->Device, WdfDeviceFailedNoRestart);
}

static
VOID
TcpciOnCc(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ UCHAR Cc)
{
    UCMTCPCI_PORT_CONTROLLER_CC_STATUS Status, Attached;
    TCPCI_CANDIDATE Candidate = TcpciClassify(Cc);

    Port->Cc = Cc;
    Status.AsUInt8 = Cc;
    switch (Port->State)
    {
        case TcpciUnattached:
        case TcpciUnsupported:
            if (Candidate == TcpciCandidateSource || Candidate == TcpciCandidateSink ||
                (Candidate == TcpciCandidateUnsupported && Port->State == TcpciUnattached))
            {
                Port->State = TcpciAttachWait;
                Port->Candidate = Candidate;
                Port->CandidateCc = Cc;
                TcpciArmDebounce(Port);
            }
            else if (Candidate == TcpciCandidateOpen && Port->State == TcpciUnsupported)
            {
                TcpciLoadUnattach(Port);
            }
            break;

        case TcpciAttachWait:
            if (Candidate == TcpciCandidateOpen)
            {
                TcpciDisarmDebounce(Port);
                Port->State = TcpciUnattached;
                TcpciLoadRestart(Port, Cc);
            }
            else if (Candidate != TcpciCandidateLooking)
            {
                Port->Candidate = Candidate;
                Port->CandidateCc = Cc;
                if (Port->Debounced)
                {
                    if (Candidate == TcpciCandidateUnsupported)
                        Port->State = TcpciUnsupported;
                    else if (Candidate == TcpciCandidateSource || (Port->Power & TCPCI_POWER_VBUS) != 0)
                        TcpciLoadAttach(Port);
                }
            }
            break;

        case TcpciSourceWaitVbus:
        case TcpciAttachedSource:
            Attached.AsUInt8 = Port->CandidateCc;
            if (Status.Looking4Connection ||
                (Attached.CC2State == UcmTcpciPortControllerCCStateSrcRd ? Status.CC2State : Status.CC1State) !=
                    UcmTcpciPortControllerCCStateSrcRd)
            {
                TcpciLoadDetach(Port, TRUE, Port->State == TcpciSourceWaitVbus);
                Port->State = TcpciUnattached;
            }
            break;

        default:
            break;
    }
}

static
VOID
TcpciOnPower(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ UCHAR Power)
{
    Port->Power = Power;
    switch (Port->State)
    {
        case TcpciWaitDetection:
            if ((Power & TCPCI_POWER_DETECTION) != 0)
            {
                Port->State = TcpciUnattached;
                TcpciLoadUnattach(Port);
            }
            break;

        case TcpciAttachWait:
            if (Port->Candidate == TcpciCandidateSink && Port->Debounced && (Power & TCPCI_POWER_VBUS) != 0)
                TcpciLoadAttach(Port);
            break;

        case TcpciSourceWaitVbus:
            if ((Power & TCPCI_POWER_VBUS) != 0)
                Port->State = TcpciAttachedSource;
            break;

        case TcpciAttachedSink:
            if ((Power & TCPCI_POWER_VBUS) == 0)
            {
                TcpciLoadDetach(Port, FALSE, FALSE);
                Port->State = TcpciUnattached;
            }
            break;

        default:
            break;
    }
}

static
VOID
TcpciHandleEvent(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ PTCPCI_EVENT Event)
{
    switch (Event->Type)
    {
        case TcpciEventStart:
            TcpciBegin(Port, TcpciProgramStart);
            TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_GET_STATUS, 0, 0);
            break;

        case TcpciEventCc:
            TcpciOnCc(Port, (UCHAR)Event->Value);
            break;

        case TcpciEventPower:
            TcpciOnPower(Port, (UCHAR)Event->Value);
            break;

        case TcpciEventFault:
            Port->Fault = (UCHAR)Event->Value;
            TcpciFail(Port);
            break;

        case TcpciEventDebounce:
            if (Port->State == TcpciAttachWait && !Port->Debounced &&
                KeQueryInterruptTime() - Port->ArmTime >= 10000ULL * (TCPCI_DEBOUNCE_MS - 1))
            {
                Port->Debounced = TRUE;
                if (Port->Candidate == TcpciCandidateUnsupported)
                    Port->State = TcpciUnsupported;
                else if (Port->Candidate == TcpciCandidateSource || (Port->Power & TCPCI_POWER_VBUS) != 0)
                    TcpciLoadAttach(Port);
            }
            break;
    }
}

static
VOID
TcpciProgramDone(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ TCPCI_PROGRAM Program)
{
    switch (Program)
    {
        case TcpciProgramStart:
            if ((Port->Power & TCPCI_POWER_DETECTION) == 0)
            {
                TcpciBegin(Port, TcpciProgramDetect);
                TcpciAddStep(Port, IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_COMMAND, 0,
                             UcmTcpciPortControllerCommandEnableVbusDetect);
            }
            else
            {
                TcpciLoadUnattach(Port);
            }
            break;

        case TcpciProgramDetect:
            Port->State = TcpciWaitDetection;
            TcpciFinishStart(Port);
            break;

        case TcpciProgramUnattach:
            Port->State = TcpciUnattached;
            TcpciFinishStart(Port);
            TcpciOnCc(Port, Port->Cc);
            break;

        case TcpciProgramRestart:
            Port->State = TcpciUnattached;
            break;

        case TcpciProgramAttachSource:
            Port->State = TcpciSourceWaitVbus;
            break;

        case TcpciProgramAttachSink:
            Port->State = TcpciAttachedSink;
            break;

        default:
            break;
    }
}

static
VOID
TcpciAfterStep(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    PUCMTCPCI_PORT_CONTROLLER_GET_STATUS_OUT_PARAMS Status = (PVOID)Port->Buffer;

    if (!NT_SUCCESS(Port->StepStatus))
    {
        if (Port->State != TcpciStopped)
            TcpciFail(Port);
        return;
    }

    if (Port->StepNext < Port->StepCount && Port->Steps[Port->StepNext].Code == IOCTL_UCMTCPCI_PORT_CONTROLLER_GET_STATUS)
    {
        Port->Cc = Status->CCStatus.AsUInt8;
        Port->Power = Status->PowerStatus.AsUInt8;
        Port->Fault = Status->FaultStatus.AsUInt8;
    }

    Port->StepNext++;
}

static
NTSTATUS
NTAPI
TcpciRequestCompleted(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PTCPCI_PORT_CONTEXT Port = Context;
    PDEVICE_OBJECT Top = Port->Top;

    UNREFERENCED_PARAMETER(DeviceObject);

    Port->StepStatus = Irp->IoStatus.Status;
    Port->Irp = NULL;
    Port->Top = NULL;
    IoFreeIrp(Irp);
    ObDereferenceObject(Top);
    if (InterlockedExchange(&Port->SendState, TCPCI_SEND_COMPLETED) == TCPCI_SEND_PENDING)
    {
        TcpciAfterStep(Port);
        TcpciRun(Port);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
BOOLEAN
TcpciSend(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    PUCMTCPCI_PORT_CONTROLLER_SET_CONTROL_IN_PARAMS Control = (PVOID)Port->Buffer;
    PUCMTCPCI_PORT_CONTROLLER_SET_COMMAND_IN_PARAMS Command = (PVOID)Port->Buffer;
    PUCMTCPCI_PORT_CONTROLLER_GET_STATUS_IN_PARAMS Status = (PVOID)Port->Buffer;
    PTCPCI_STEP Step = &Port->Steps[Port->StepNext];
    ULONG InputLength, OutputLength = 0;
    PIO_STACK_LOCATION Stack;
    PDEVICE_OBJECT Top;
    PIRP Irp;

    RtlZeroMemory(Port->Buffer, sizeof(Port->Buffer));
    switch (Step->Code)
    {
        case IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_CONTROL:
            Control->PortControllerObject = Port->Handle;
            Control->ControlType = Step->Type;
            Control->TCPCControl.AsUInt8 = (UINT8)Step->Value;
            InputLength = sizeof(*Control);
            break;

        case IOCTL_UCMTCPCI_PORT_CONTROLLER_SET_COMMAND:
            Command->PortControllerObject = Port->Handle;
            Command->Command = Step->Value;
            InputLength = sizeof(*Command);
            break;

        default:
            Status->PortControllerObject = Port->Handle;
            InputLength = sizeof(*Status);
            OutputLength = sizeof(UCMTCPCI_PORT_CONTROLLER_GET_STATUS_OUT_PARAMS);
            break;
    }

    Top = IoGetAttachedDeviceReference(WdfDeviceWdmGetDeviceObject(Port->Device));
    Irp = IoAllocateIrp(Top->StackSize, FALSE);
    if (Irp == NULL)
    {
        ObDereferenceObject(Top);
        Port->StepStatus = STATUS_INSUFFICIENT_RESOURCES;
        return TRUE;
    }

    Irp->AssociatedIrp.SystemBuffer = Port->Buffer;
    Irp->RequestorMode = KernelMode;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->Parameters.DeviceIoControl.IoControlCode = Step->Code;
    Stack->Parameters.DeviceIoControl.InputBufferLength = InputLength;
    Stack->Parameters.DeviceIoControl.OutputBufferLength = OutputLength;
    IoSetCompletionRoutine(Irp, TcpciRequestCompleted, Port, TRUE, TRUE, TRUE);
    Port->Irp = Irp;
    Port->Top = Top;
    InterlockedExchange(&Port->SendState, TCPCI_SEND_SENDING);
    IoCallDriver(Top, Irp);
    return InterlockedCompareExchange(&Port->SendState, TCPCI_SEND_PENDING, TCPCI_SEND_SENDING) == TCPCI_SEND_COMPLETED;
}

static
VOID
TcpciRun(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    TCPCI_PROGRAM Program;
    TCPCI_EVENT Event;
    KIRQL Irql;

    for (;;)
    {
        if (Port->State == TcpciStopped)
        {
            Port->StepCount = Port->StepNext = 0;
            Port->Program = TcpciProgramNone;
            TcpciFinishStart(Port);
        }

        if (Port->StepNext < Port->StepCount)
        {
            if (!TcpciSend(Port))
                return;

            TcpciAfterStep(Port);
            continue;
        }

        if (Port->Program != TcpciProgramNone)
        {
            Program = Port->Program;
            Port->Program = TcpciProgramNone;
            Port->StepCount = Port->StepNext = 0;
            TcpciProgramDone(Port, Program);
            continue;
        }

        KeAcquireSpinLock(&Port->Lock, &Irql);
        if (Port->EventCount == 0)
        {
            Port->Busy = FALSE;
            KeSetEvent(&Port->IdleEvent, IO_NO_INCREMENT, FALSE);
            KeReleaseSpinLock(&Port->Lock, Irql);
            return;
        }

        Event = Port->Events[Port->EventHead];
        Port->EventHead = (Port->EventHead + 1) % TCPCI_MAX_EVENTS;
        Port->EventCount--;
        KeReleaseSpinLock(&Port->Lock, Irql);
        TcpciHandleEvent(Port, &Event);
    }
}

static
VOID
TcpciPost(
    _Inout_ PTCPCI_PORT_CONTEXT Port,
    _In_ TCPCI_EVENT_TYPE Type,
    _In_ ULONG Value)
{
    BOOLEAN Run = FALSE;
    KIRQL Irql;

    KeAcquireSpinLock(&Port->Lock, &Irql);
    if (Port->State != TcpciStopped && Port->EventCount < TCPCI_MAX_EVENTS)
    {
        Port->Events[(Port->EventHead + Port->EventCount) % TCPCI_MAX_EVENTS].Type = Type;
        Port->Events[(Port->EventHead + Port->EventCount) % TCPCI_MAX_EVENTS].Value = Value;
        Port->EventCount++;
        if (!Port->Busy)
        {
            Port->Busy = TRUE;
            KeClearEvent(&Port->IdleEvent);
            Run = TRUE;
        }
    }
    KeReleaseSpinLock(&Port->Lock, Irql);
    if (Run)
        TcpciRun(Port);
}

static
VOID
NTAPI
TcpciDebounceDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PTCPCI_PORT_CONTEXT Port = Context;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    TcpciPost(Port, TcpciEventDebounce, 0);
}

static
VOID
TcpciHalt(
    _Inout_ PTCPCI_PORT_CONTEXT Port)
{
    KIRQL Irql;

    KeAcquireSpinLock(&Port->Lock, &Irql);
    Port->State = TcpciStopped;
    Port->EventCount = 0;
    KeReleaseSpinLock(&Port->Lock, Irql);
    KeCancelTimer(&Port->Timer);
    KeFlushQueuedDpcs();
    KeWaitForSingleObject(&Port->IdleEvent, Executive, KernelMode, FALSE, NULL);
}

static
VOID
NTAPI
TcpciEvtPortCleanup(
    _In_ WDFOBJECT Object)
{
    PTCPCI_PORT_CONTEXT Port = TcpciGetPortContext(Object);
    PTCPCI_DEVICE_CONTEXT DeviceContext = TcpciGetDeviceContext(Port->Device);

    TcpciHalt(Port);
    if (DeviceContext != NULL && DeviceContext->Port == Port->Handle)
        DeviceContext->Port = NULL;
}

static
NTSTATUS
NTAPI
TcpciPortControllerCreate(
    _In_ PUCMTCPCI_DRIVER_GLOBALS DriverGlobals,
    _In_ WDFDEVICE WdfDevice,
    _In_ PUCMTCPCI_PORT_CONTROLLER_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES Attributes,
    _Out_ UCMTCPCIPORTCONTROLLER *PortControllerObject)
{
    WDF_OBJECT_ATTRIBUTES ObjectAttributes, ContextAttributes;
    PTCPCI_DEVICE_CONTEXT DeviceContext;
    PTCPCI_PORT_CONTEXT Port;
    WDFOBJECT Object;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (WdfDevice == NULL || Config == NULL || PortControllerObject == NULL)
        return STATUS_INVALID_PARAMETER;

    *PortControllerObject = NULL;
    DeviceContext = TcpciGetDeviceContext(WdfDevice);
    if (DeviceContext == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    if (Attributes != NULL)
        ObjectAttributes = *Attributes;
    else
        WDF_OBJECT_ATTRIBUTES_INIT(&ObjectAttributes);
    if (ObjectAttributes.ParentObject == NULL)
        ObjectAttributes.ParentObject = WdfDevice;

    Status = WdfObjectCreate(&ObjectAttributes, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&ContextAttributes, TCPCI_PORT_CONTEXT);
    ContextAttributes.EvtCleanupCallback = TcpciEvtPortCleanup;
    Status = WdfObjectAllocateContext(Object, &ContextAttributes, (PVOID *)&Port);
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Object);
        return Status;
    }

    Port->Device = WdfDevice;
    Port->Handle = (UCMTCPCIPORTCONTROLLER)Object;
    Port->State = TcpciStopped;
    KeInitializeSpinLock(&Port->Lock);
    KeInitializeEvent(&Port->IdleEvent, NotificationEvent, TRUE);
    KeInitializeEvent(&Port->StartEvent, NotificationEvent, FALSE);
    KeInitializeTimer(&Port->Timer);
    KeInitializeDpc(&Port->Dpc, TcpciDebounceDpc, Port);
    if (Config->Identification != NULL)
        Port->Identification = *Config->Identification;
    if (Config->Capabilities != NULL)
        Port->Capabilities = *Config->Capabilities;
    if (DeviceContext->Connector.Sourcing == 4)
        Port->RpValue = UcmTcpciPortControllerRoleControlRp3000mA;
    else if (DeviceContext->Connector.Sourcing == 2)
        Port->RpValue = UcmTcpciPortControllerRoleControlRp1500mA;
    else
        Port->RpValue = UcmTcpciPortControllerRoleControlRpDefault;

    DeviceContext->Port = Port->Handle;
    *PortControllerObject = Port->Handle;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
TcpciPortControllerSetHardwareRequestQueue(
    _In_ PUCMTCPCI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMTCPCIPORTCONTROLLER PortControllerObject,
    _In_ WDFQUEUE HardwareRequestQueue)
{
    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PortControllerObject != NULL)
        TcpciGetPortContext(PortControllerObject)->Queue = HardwareRequestQueue;
}

static
NTSTATUS
NTAPI
TcpciPortControllerStart(
    _In_ PUCMTCPCI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMTCPCIPORTCONTROLLER PortControllerObject)
{
    PTCPCI_PORT_CONTEXT Port;
    KIRQL Irql;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PortControllerObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Port = TcpciGetPortContext(PortControllerObject);
    if (Port->Queue == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireSpinLock(&Port->Lock, &Irql);
    if (Port->State != TcpciStopped)
    {
        KeReleaseSpinLock(&Port->Lock, Irql);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Port->State = TcpciStarting;
    Port->Starting = TRUE;
    Port->EventHead = Port->EventCount = 0;
    KeClearEvent(&Port->StartEvent);
    KeReleaseSpinLock(&Port->Lock, Irql);
    TcpciPost(Port, TcpciEventStart, 0);
    KeWaitForSingleObject(&Port->StartEvent, Executive, KernelMode, FALSE, NULL);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
TcpciPortControllerStop(
    _In_ PUCMTCPCI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMTCPCIPORTCONTROLLER PortControllerObject)
{
    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PortControllerObject != NULL)
        TcpciHalt(TcpciGetPortContext(PortControllerObject));
}

static
VOID
NTAPI
TcpciPortControllerAlert(
    _In_ PUCMTCPCI_DRIVER_GLOBALS DriverGlobals,
    _In_ UCMTCPCIPORTCONTROLLER PortControllerObject,
    _In_reads_(NumberOfAlerts) PUCMTCPCI_PORT_CONTROLLER_ALERT_DATA AlertData,
    _In_ size_t NumberOfAlerts)
{
    PTCPCI_PORT_CONTEXT Port;
    size_t Index;

    UNREFERENCED_PARAMETER(DriverGlobals);

    if (PortControllerObject == NULL || AlertData == NULL)
        return;

    Port = TcpciGetPortContext(PortControllerObject);
    for (Index = 0; Index < NumberOfAlerts; Index++)
    {
        switch (AlertData[Index].AlertType)
        {
            case UcmTcpciPortControllerAlertCCStatus:
                TcpciPost(Port, TcpciEventCc, AlertData[Index].CCStatus.AsUInt8);
                break;

            case UcmTcpciPortControllerAlertPowerStatus:
                TcpciPost(Port, TcpciEventPower, AlertData[Index].PowerStatus.AsUInt8);
                break;

            case UcmTcpciPortControllerAlertFault:
                TcpciPost(Port, TcpciEventFault, AlertData[Index].FaultStatus.AsUInt8);
                break;

            default:
                break;
        }
    }
}

static PVOID TcpciFunctions[UcmtcpciFunctionTableNumEntries] =
{
    TcpciDeviceInitInitialize,
    TcpciDeviceInitialize,
    TcpciPortControllerCreate,
    TcpciPortControllerSetHardwareRequestQueue,
    TcpciPortControllerStart,
    TcpciPortControllerStop,
    TcpciPortControllerAlert
};

static
NTSTATUS
NTAPI
TcpciLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo, ClientGlobals, (PVOID const *)TcpciFunctions, RTL_NUMBER_OF(TcpciFunctions), 1);
}

static
VOID
NTAPI
TcpciLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO TcpciLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 0, 0},
    NULL,
    NULL,
    TcpciLibraryBindClient,
    TcpciLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject, RegistryPath, L"\\Device\\UcmTcpciCx", &TcpciLibraryInfo);
}
