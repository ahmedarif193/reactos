/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows-contract SD miniport engine and public SDPORT exports
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include <ddk/sdport.h>
#include "sdport_win.h"

#define SDPORT_WIN_TAG 'WdpS'

typedef enum _SDPORT_WIN_PHASE
{
    SdPortWinPhaseIdle,
    SdPortWinPhaseCommand,
    SdPortWinPhaseTransfer
} SDPORT_WIN_PHASE;

typedef struct _SDPORT_WIN_BLOCK
{
    SD_MINIPORT Miniport;
    PDEVICE_OBJECT Fdo;
    SDPORT_CAPABILITIES Capabilities;
    BOOLEAN CapabilitiesValid;
    SDPORT_REQUEST Request;
    SDPORT_WIN_REQUEST Descriptor;
    PSCATTER_GATHER_LIST SgList;
    SDPORT_WIN_PHASE Phase;
    BOOLEAN Issuing;
    BOOLEAN NestedCompleted;
    NTSTATUS NestedStatus;
    ULONG Errors;
    ULONG Length;
    SDPORT_SLOT_EXTENSION Slot;
} SDPORT_WIN_BLOCK, *PSDPORT_WIN_BLOCK;

static SDPORT_INITIALIZATION_DATA SdPortWinInitData;
static BOOLEAN SdPortWinRegistered;

NTSTATUS
NTAPI
SdPortInitializeLegacy(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ PVOID InitializationData);

static
PSDPORT_WIN_BLOCK
SdPortWinBlockFromExtension(
    _In_ PVOID Extension)
{
    return (PSDPORT_WIN_BLOCK)Extension;
}

static
PVOID
SdPortWinPrivate(
    _In_ PSDPORT_WIN_BLOCK Block)
{
    return Block->Slot.PrivateExtension;
}

static
NTSTATUS
SdPortWinThunkGetSlotCount(
    _In_ PVOID Extension,
    _Out_ PUCHAR SlotCount)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);
    UCHAR Count = 1;
    NTSTATUS Status = STATUS_SUCCESS;

    if (SdPortWinInitData.GetSlotCount != NULL)
        Status = SdPortWinInitData.GetSlotCount(&Block->Miniport, &Count);
    if (!NT_SUCCESS(Status))
        return Status;

    Block->Miniport.SlotCount = 1;
    Block->Miniport.SlotExtensionList[0] = &Block->Slot;
    *SlotCount = 1;
    return STATUS_SUCCESS;
}

static
NTSTATUS
SdPortWinThunkInitialize(
    _In_ PVOID Extension,
    _In_ PHYSICAL_ADDRESS PhysicalBase,
    _In_ PVOID VirtualBase,
    _In_ ULONG Length,
    _In_ BOOLEAN CrashdumpMode)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    return SdPortWinInitData.Initialize(SdPortWinPrivate(Block),
                                        PhysicalBase,
                                        VirtualBase,
                                        Length,
                                        CrashdumpMode);
}

static
BOOLEAN
SdPortWinThunkGetCardDetectState(
    _In_ PVOID Extension)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (SdPortWinInitData.GetCardDetectState == NULL)
        return TRUE;
    return SdPortWinInitData.GetCardDetectState(SdPortWinPrivate(Block));
}

static
BOOLEAN
SdPortWinThunkInterrupt(
    _In_ PVOID Extension,
    _Out_ PULONG Events,
    _Out_ PULONG Errors,
    _Out_ PBOOLEAN NotifyCardChange,
    _Out_ PBOOLEAN NotifySdioInterrupt,
    _Out_ PBOOLEAN NotifyTuning)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    return SdPortWinInitData.Interrupt(SdPortWinPrivate(Block),
                                       Events,
                                       Errors,
                                       NotifyCardChange,
                                       NotifySdioInterrupt,
                                       NotifyTuning);
}

static
NTSTATUS
SdPortWinThunkSaveContext(
    _In_ PVOID Extension)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (SdPortWinInitData.SaveContext != NULL)
        SdPortWinInitData.SaveContext(SdPortWinPrivate(Block));
    return STATUS_SUCCESS;
}

static
NTSTATUS
SdPortWinThunkRestoreContext(
    _In_ PVOID Extension)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (SdPortWinInitData.RestoreContext != NULL)
        SdPortWinInitData.RestoreContext(SdPortWinPrivate(Block));
    return STATUS_SUCCESS;
}

static
VOID
SdPortWinThunkToggleEvents(
    _In_ PVOID Extension,
    _In_ ULONG EventMask,
    _In_ BOOLEAN Enable)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (SdPortWinInitData.ToggleEvents != NULL)
        SdPortWinInitData.ToggleEvents(SdPortWinPrivate(Block), EventMask, Enable);
}

static
VOID
SdPortWinThunkClearEvents(
    _In_ PVOID Extension,
    _In_ ULONG EventMask)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (SdPortWinInitData.ClearEvents != NULL)
        SdPortWinInitData.ClearEvents(SdPortWinPrivate(Block), EventMask);
}

static
VOID
SdPortWinThunkCleanup(
    _In_ PVOID Extension)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (SdPortWinInitData.Cleanup != NULL)
        SdPortWinInitData.Cleanup(&Block->Miniport);
}

NTSTATUS
NTAPI
SdPortInitialize(
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ PSDPORT_INITIALIZATION_DATA InitializationData)
{
    SDPORT_WIN_LEGACY_TABLE Table;

    if (InitializationData == NULL ||
        InitializationData->StructureSize < FIELD_OFFSET(SDPORT_INITIALIZATION_DATA, PrivateExtensionSize) ||
        InitializationData->Initialize == NULL ||
        InitializationData->IssueRequest == NULL ||
        InitializationData->Interrupt == NULL ||
        InitializationData->RequestDpc == NULL ||
        InitializationData->GetResponse == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&SdPortWinInitData, sizeof(SdPortWinInitData));
    RtlCopyMemory(&SdPortWinInitData,
                  InitializationData,
                  min(InitializationData->StructureSize, sizeof(SDPORT_INITIALIZATION_DATA)));
    SdPortWinRegistered = TRUE;

    RtlZeroMemory(&Table, sizeof(Table));
    Table.GetSlotCount = SdPortWinThunkGetSlotCount;
    Table.Initialize = SdPortWinThunkInitialize;
    Table.GetCardDetectState = SdPortWinThunkGetCardDetectState;
    Table.Interrupt = SdPortWinThunkInterrupt;
    Table.SaveContext = SdPortWinThunkSaveContext;
    Table.RestoreContext = SdPortWinThunkRestoreContext;
    Table.ToggleEvents = SdPortWinThunkToggleEvents;
    Table.ClearEvents = SdPortWinThunkClearEvents;
    Table.Cleanup = SdPortWinThunkCleanup;

    return SdPortInitializeWindowsMiniport((PDRIVER_OBJECT)Argument1,
                                           (PUNICODE_STRING)Argument2,
                                           &Table);
}

BOOLEAN
SdPortWinIsRegistered(VOID)
{
    return SdPortWinRegistered;
}

NTSTATUS
SdPortWinAllocateExtension(
    _In_ PDEVICE_OBJECT Fdo,
    _In_ PDEVICE_OBJECT Pdo,
    _Outptr_ PVOID *Extension)
{
    PSDPORT_WIN_BLOCK Block;
    SIZE_T Size;
    WCHAR Enumerator[16];
    ULONG Length = 0;

    *Extension = NULL;
    Size = FIELD_OFFSET(SDPORT_WIN_BLOCK, Slot) +
           FIELD_OFFSET(SDPORT_SLOT_EXTENSION, PrivateExtension) +
           SdPortWinInitData.PrivateExtensionSize;

    Block = ExAllocatePoolWithTag(NonPagedPool, Size, SDPORT_WIN_TAG);
    if (Block == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Block, Size);
    Block->Fdo = Fdo;
    Block->Miniport.InitializationData = &SdPortWinInitData;
    Block->Miniport.ConfigurationInfo.DeviceObject = Fdo;
    Block->Miniport.ConfigurationInfo.BusType = SdBusTypeAcpi;
    Block->Miniport.SlotCount = 1;
    Block->Miniport.SlotExtensionList[0] = &Block->Slot;
    Block->Slot.Miniport = &Block->Miniport;
    Block->Phase = SdPortWinPhaseIdle;

    if (Pdo != NULL &&
        NT_SUCCESS(IoGetDeviceProperty(Pdo,
                                       DevicePropertyEnumeratorName,
                                       sizeof(Enumerator),
                                       Enumerator,
                                       &Length)) &&
        _wcsicmp(Enumerator, L"PCI") == 0)
    {
        Block->Miniport.ConfigurationInfo.BusType = SdBusTypePci;
    }

    *Extension = Block;
    return STATUS_SUCCESS;
}

VOID
SdPortWinFreeExtension(
    _In_ PVOID Extension)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (Block->SgList != NULL)
        ExFreePoolWithTag(Block->SgList, SDPORT_WIN_TAG);
    ExFreePoolWithTag(Block, SDPORT_WIN_TAG);
}

VOID
SdPortWinQueryCapabilities(
    _In_ PVOID Extension,
    _Out_ PSDPORT_WIN_CAPABILITIES Capabilities)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);
    PSDPORT_CAPABILITIES Caps = &Block->Capabilities;

    RtlZeroMemory(Caps, sizeof(*Caps));
    if (SdPortWinInitData.GetSlotCapabilities != NULL)
        SdPortWinInitData.GetSlotCapabilities(SdPortWinPrivate(Block), Caps);
    Block->CapabilitiesValid = TRUE;

    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->MaximumBlockSize = Caps->MaximumBlockSize;
    Capabilities->MaximumBlockCount = Caps->MaximumBlockCount;
    Capabilities->BaseClockFrequencyKhz = Caps->BaseClockFrequencyKhz;
    Capabilities->HighSpeed = Caps->Supported.HighSpeed;
    Capabilities->Sdr50 = Caps->Supported.SDR50;
    Capabilities->Sdr104 = Caps->Supported.SDR104;
    Capabilities->Ddr50 = Caps->Supported.DDR50;
    Capabilities->Hs200 = Caps->Supported.HS200;
    Capabilities->Hs400 = Caps->Supported.HS400;
    Capabilities->ScatterGatherDma = Caps->Supported.ScatterGatherDma;
    Capabilities->Voltage18 = Caps->Supported.Voltage18V;
    Capabilities->Voltage30 = Caps->Supported.Voltage30V;
    Capabilities->Voltage33 = Caps->Supported.Voltage33V;
    Capabilities->BusWidth8Bit = Caps->Supported.BusWidth8Bit;
}

NTSTATUS
SdPortWinIssueBusOperation(
    _In_ PVOID Extension,
    _In_ SDPORT_WIN_BUS_OPERATION Operation,
    _In_ ULONG Parameter)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);
    SDPORT_BUS_OPERATION BusOperation;

    if (SdPortWinInitData.IssueBusOperation == NULL)
        return STATUS_SUCCESS;

    RtlZeroMemory(&BusOperation, sizeof(BusOperation));
    switch (Operation)
    {
        case SdPortWinResetHost:
            BusOperation.Type = SdResetHost;
            BusOperation.Parameters.ResetType = SdResetTypeAll;
            break;

        case SdPortWinSetClock:
            BusOperation.Type = SdSetClock;
            BusOperation.Parameters.FrequencyKhz = Parameter;
            break;

        case SdPortWinSetVoltage:
            BusOperation.Type = SdSetVoltage;
            switch (Parameter & 0x0E)
            {
                case 0x0E:
                    BusOperation.Parameters.Voltage = SdBusVoltage33;
                    break;
                case 0x0C:
                    BusOperation.Parameters.Voltage = SdBusVoltage30;
                    break;
                case 0x0A:
                    BusOperation.Parameters.Voltage = SdBusVoltage18;
                    break;
                default:
                    BusOperation.Parameters.Voltage = SdBusVoltageOff;
                    break;
            }
            if (Parameter == 0)
                BusOperation.Parameters.Voltage = SdBusVoltageOff;
            break;

        case SdPortWinSetBusWidth:
            BusOperation.Type = SdSetBusWidth;
            BusOperation.Parameters.BusWidth =
                Parameter == 8 ? SdBusWidth8Bit :
                Parameter == 4 ? SdBusWidth4Bit : SdBusWidth1Bit;
            break;

        case SdPortWinSetBusSpeed:
            BusOperation.Type = SdSetBusSpeed;
            BusOperation.Parameters.BusSpeed = Parameter != 0 ? SdBusSpeedHigh : SdBusSpeedNormal;
            break;

        case SdPortWinSetSignalingVoltage:
            BusOperation.Type = SdSetSignalingVoltage;
            BusOperation.Parameters.SignalingVoltage =
                Parameter != 0 ? SdSignalingVoltage18 : SdSignalingVoltage33;
            break;

        case SdPortWinExecuteTuning:
            BusOperation.Type = SdExecuteTuning;
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }

    return SdPortWinInitData.IssueBusOperation(SdPortWinPrivate(Block), &BusOperation);
}

static
SDPORT_RESPONSE_TYPE
SdPortWinMapResponseType(
    _In_ UCHAR LegacyType)
{
    switch (LegacyType)
    {
        case 1: return SdResponseTypeNone;
        case 2: return SdResponseTypeR1;
        case 3: return SdResponseTypeR1B;
        case 4: return SdResponseTypeR2;
        case 5: return SdResponseTypeR3;
        case 6: return SdResponseTypeR4;
        case 7: return SdResponseTypeR5;
        case 8: return SdResponseTypeR5B;
        case 9: return SdResponseTypeR6;
        default: return SdResponseTypeUndefined;
    }
}

static
SDPORT_TRANSFER_TYPE
SdPortWinMapTransferType(
    _In_ UCHAR LegacyType)
{
    switch (LegacyType)
    {
        case 1: return SdTransferTypeNone;
        case 2: return SdTransferTypeSingleBlock;
        case 3: return SdTransferTypeMultiBlock;
        case 4: return SdTransferTypeMultiBlockNoStop;
        default: return SdTransferTypeNone;
    }
}

static
NTSTATUS
SdPortWinBuildScatterGatherList(
    _In_ PSDPORT_WIN_BLOCK Block,
    _In_ PMDL Mdl,
    _In_ ULONG Length)
{
    PPFN_NUMBER PfnArray = MmGetMdlPfnArray(Mdl);
    ULONG ByteOffset = MmGetMdlByteOffset(Mdl);
    ULONG PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), Length);
    PSCATTER_GATHER_LIST List;
    ULONG Remaining = Length;
    ULONG PageIndex = 0;
    ULONG Elements = 0;

    if (Block->SgList != NULL)
    {
        ExFreePoolWithTag(Block->SgList, SDPORT_WIN_TAG);
        Block->SgList = NULL;
    }

    List = ExAllocatePoolWithTag(NonPagedPool,
                                 FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                                     PageCount * sizeof(SCATTER_GATHER_ELEMENT),
                                 SDPORT_WIN_TAG);
    if (List == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (Remaining != 0 && PageIndex < PageCount)
    {
        ULONG Offset = PageIndex == 0 ? ByteOffset : 0;
        ULONGLONG Address = ((ULONGLONG)PfnArray[PageIndex] << PAGE_SHIFT) + Offset;
        ULONG SegmentLength = PAGE_SIZE - Offset;

        PageIndex++;
        while (PageIndex < PageCount &&
               SegmentLength < Remaining &&
               PfnArray[PageIndex] == PfnArray[PageIndex - 1] + 1)
        {
            SegmentLength += PAGE_SIZE;
            PageIndex++;
        }
        if (SegmentLength > Remaining)
            SegmentLength = Remaining;

        List->Elements[Elements].Address.QuadPart = (LONGLONG)Address;
        List->Elements[Elements].Length = SegmentLength;
        List->Elements[Elements].Reserved = 0;
        Elements++;
        Remaining -= SegmentLength;
    }

    List->NumberOfElements = Elements;
    List->Reserved = 0;
    Block->SgList = List;
    return STATUS_SUCCESS;
}

static
VOID
SdPortWinFinish(
    _In_ PSDPORT_WIN_BLOCK Block,
    _In_ NTSTATUS Status)
{
    ULONG Errors = Block->Errors;

    if (Block->Descriptor.BytesTransferred != NULL)
    {
        *Block->Descriptor.BytesTransferred =
            NT_SUCCESS(Status) ? Block->Length :
            Block->Length - Block->Request.Command.BlockCount * Block->Request.Command.BlockSize;
    }

    if (Block->SgList != NULL)
    {
        ExFreePoolWithTag(Block->SgList, SDPORT_WIN_TAG);
        Block->SgList = NULL;
    }

    Block->Phase = SdPortWinPhaseIdle;
    Block->Errors = 0;
    SdPortWinRequestCompleted(Block->Fdo, Status, Errors);
}

static
NTSTATUS
SdPortWinIssue(
    _In_ PSDPORT_WIN_BLOCK Block,
    _Out_ PBOOLEAN Completed,
    _Out_ PNTSTATUS CompletionStatus)
{
    NTSTATUS Status;

    Block->Issuing = TRUE;
    Block->NestedCompleted = FALSE;
    Block->NestedStatus = STATUS_SUCCESS;
    Status = SdPortWinInitData.IssueRequest(SdPortWinPrivate(Block), &Block->Request);
    Block->Issuing = FALSE;

    *Completed = Block->NestedCompleted;
    *CompletionStatus = Block->NestedStatus;
    if (Status == STATUS_PENDING)
        Status = STATUS_SUCCESS;
    return Status;
}

static
VOID
SdPortWinAdvance(
    _In_ PSDPORT_WIN_BLOCK Block,
    _In_ NTSTATUS Status)
{
    BOOLEAN Completed;
    NTSTATUS IssueStatus;
    NTSTATUS CompletionStatus;

    for (;;)
    {
        if (Block->Phase == SdPortWinPhaseCommand)
        {
            if (!NT_SUCCESS(Status))
            {
                SdPortWinFinish(Block, Status);
                return;
            }

            if (Block->Descriptor.Response != NULL)
            {
                ULONG ResponseBuffer[4] = {0, 0, 0, 0};

                SdPortWinInitData.GetResponse(SdPortWinPrivate(Block),
                                              &Block->Request.Command,
                                              ResponseBuffer);
                RtlCopyMemory(Block->Descriptor.Response, ResponseBuffer, sizeof(ResponseBuffer));
            }

            if (Block->Request.Command.TransferType == SdTransferTypeNone)
            {
                SdPortWinFinish(Block, STATUS_SUCCESS);
                return;
            }

            Block->Phase = SdPortWinPhaseTransfer;
            Block->Request.Type = SdRequestTypeStartTransfer;
            Block->Request.Status = STATUS_PENDING;
            IssueStatus = SdPortWinIssue(Block, &Completed, &CompletionStatus);
            if (!NT_SUCCESS(IssueStatus))
            {
                SdPortWinFinish(Block, IssueStatus);
                return;
            }
            if (!Completed)
                return;
            Status = CompletionStatus;
            continue;
        }

        if (Block->Phase == SdPortWinPhaseTransfer)
        {
            if (Status == STATUS_MORE_PROCESSING_REQUIRED)
            {
                Block->Request.Type = SdRequestTypeStartTransfer;
                Block->Request.Status = STATUS_PENDING;
                IssueStatus = SdPortWinIssue(Block, &Completed, &CompletionStatus);
                if (!NT_SUCCESS(IssueStatus))
                {
                    SdPortWinFinish(Block, IssueStatus);
                    return;
                }
                if (!Completed)
                    return;
                Status = CompletionStatus;
                continue;
            }

            SdPortWinFinish(Block, Status);
            return;
        }

        return;
    }
}

NTSTATUS
SdPortWinIssueRequest(
    _In_ PVOID Extension,
    _In_ PSDPORT_WIN_REQUEST Descriptor)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);
    PSDPORT_COMMAND Command = &Block->Request.Command;
    BOOLEAN HasData;
    BOOLEAN Completed;
    NTSTATUS CompletionStatus;
    NTSTATUS Status;

    if (Block->Phase != SdPortWinPhaseIdle)
        return STATUS_DEVICE_BUSY;

    Block->Descriptor = *Descriptor;
    Block->Errors = 0;
    RtlZeroMemory(&Block->Request, sizeof(Block->Request));

    Command->Index = Descriptor->Cmd;
    Command->Type = SdCommandTypeUndefined;
    Command->Class = Descriptor->AppCmd ? SdCommandClassApp : SdCommandClassStandard;
    Command->ResponseType = SdPortWinMapResponseType(Descriptor->ResponseType);
    Command->TransferType = SdPortWinMapTransferType(Descriptor->TransferType);
    Command->TransferDirection =
        Descriptor->TransferDirection == 2 ? SdTransferDirectionWrite :
        Descriptor->TransferDirection == 1 ? SdTransferDirectionRead : SdTransferDirectionUndefined;
    Command->Argument = Descriptor->Argument;
    HasData = Command->TransferType != SdTransferTypeNone;

    if (HasData)
    {
        Command->BlockSize = (USHORT)Descriptor->BlockSize;
        Command->BlockCount = (USHORT)Descriptor->BlockCount;
        Command->Length = Descriptor->BlockSize * Descriptor->BlockCount;
        Command->TransferMethod = SdTransferMethodPio;
        Command->UseAutoCmd12 = Command->TransferType == SdTransferTypeMultiBlock &&
                                Block->Capabilities.Supported.AutoCmd12;

        if (Descriptor->DataMdl != NULL)
        {
            Command->DataBuffer = MmGetSystemAddressForMdlSafe(Descriptor->DataMdl, NormalPagePriority);
            if (Command->DataBuffer == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            if (Block->Capabilities.Supported.ScatterGatherDma &&
                !(Command->TransferDirection == SdTransferDirectionRead ?
                  Block->Capabilities.Flags.UsePioForRead :
                  Block->Capabilities.Flags.UsePioForWrite))
            {
                Status = SdPortWinBuildScatterGatherList(Block, Descriptor->DataMdl, Command->Length);
                if (NT_SUCCESS(Status))
                {
                    Command->TransferMethod = SdTransferMethodSgDma;
                    Command->ScatterGatherList = Block->SgList;
                    Command->ScatterGatherListSize =
                        FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) +
                        Block->SgList->NumberOfElements * sizeof(SCATTER_GATHER_ELEMENT);
                    Command->DmaVirtualAddress = Command->DataBuffer;
                    Command->DmaPhysicalAddress = Block->SgList->Elements[0].Address;
                }
            }
        }
        else
        {
            Command->DataBuffer = Descriptor->DataBuffer;
        }
    }
    else
    {
        Command->TransferMethod = SdTransferMethodUndefined;
    }

    Block->Length = Command->Length;
    Block->Request.Type = HasData ? SdRequestTypeCommandWithTransfer : SdRequestTypeCommandNoTransfer;
    Block->Request.Status = STATUS_PENDING;
    Block->Phase = SdPortWinPhaseCommand;

    Status = SdPortWinIssue(Block, &Completed, &CompletionStatus);
    if (!NT_SUCCESS(Status))
    {
        Block->Phase = SdPortWinPhaseIdle;
        if (Block->SgList != NULL)
        {
            ExFreePoolWithTag(Block->SgList, SDPORT_WIN_TAG);
            Block->SgList = NULL;
        }
        return Status;
    }

    if (Completed)
        SdPortWinAdvance(Block, CompletionStatus);

    return STATUS_SUCCESS;
}

VOID
SdPortWinRequestDpc(
    _In_ PVOID Extension,
    _In_ ULONG Events,
    _In_ ULONG Errors)
{
    PSDPORT_WIN_BLOCK Block = SdPortWinBlockFromExtension(Extension);

    if (Block->Phase == SdPortWinPhaseIdle)
        return;

    Block->Errors |= Errors;
    SdPortWinInitData.RequestDpc(SdPortWinPrivate(Block), &Block->Request, Events, Errors);
}

VOID
NTAPI
SdPortCompleteRequest(
    _Inout_ PSDPORT_REQUEST Request,
    _In_ NTSTATUS Status)
{
    PSDPORT_WIN_BLOCK Block = CONTAINING_RECORD(Request, SDPORT_WIN_BLOCK, Request);

    if (Block->Phase == SdPortWinPhaseIdle)
        return;

    Request->Status = Status;
    if (Block->Issuing)
    {
        Block->NestedCompleted = TRUE;
        Block->NestedStatus = Status;
        return;
    }

    SdPortWinAdvance(Block, Status);
}

NTSTATUS
NTAPI
SdPortPoFxPowerControl(
    _In_ PVOID PrivateExtension,
    _In_ LPCGUID PowerControlCode,
    _In_reads_bytes_opt_(InBufferSize) PVOID InBuffer,
    _In_ SIZE_T InBufferSize,
    _Out_writes_bytes_opt_(OutBufferSize) PVOID OutBuffer,
    _In_ SIZE_T OutBufferSize,
    _Out_opt_ PSIZE_T BytesReturned)
{
    UNREFERENCED_PARAMETER(PrivateExtension);
    UNREFERENCED_PARAMETER(PowerControlCode);
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);
    UNREFERENCED_PARAMETER(OutBuffer);
    UNREFERENCED_PARAMETER(OutBufferSize);

    if (BytesReturned != NULL)
        *BytesReturned = 0;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
SdPortWinAccessPciConfig(
    _In_ PSD_MINIPORT Miniport,
    _In_ BOOLEAN Write,
    _In_ UCHAR Offset,
    _Inout_ PUCHAR Buffer,
    _In_ ULONG Length)
{
    PSDPORT_WIN_BLOCK Block;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT Target;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    if (Miniport == NULL || Miniport->ConfigurationInfo.BusType != SdBusTypePci)
        return STATUS_NOT_SUPPORTED;

    Block = CONTAINING_RECORD(Miniport, SDPORT_WIN_BLOCK, Miniport);
    Pdo = Block->Fdo;
    Target = IoGetAttachedDeviceReference(Pdo);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, Target, NULL, 0, NULL, &Event, &IoStatus);
    if (Irp == NULL)
    {
        ObDereferenceObject(Target);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = Write ? IRP_MN_WRITE_CONFIG : IRP_MN_READ_CONFIG;
    Stack->Parameters.ReadWriteConfig.WhichSpace = PCI_WHICHSPACE_CONFIG;
    Stack->Parameters.ReadWriteConfig.Buffer = Buffer;
    Stack->Parameters.ReadWriteConfig.Offset = Offset;
    Stack->Parameters.ReadWriteConfig.Length = Length;

    Status = IoCallDriver(Target, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    ObDereferenceObject(Target);
    return Status;
}

NTSTATUS
NTAPI
SdPortGetPciConfigSpace(
    _In_ PSD_MINIPORT Miniport,
    _In_ UCHAR Offset,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    return SdPortWinAccessPciConfig(Miniport, FALSE, Offset, Buffer, Length);
}

NTSTATUS
NTAPI
SdPortSetPciConfigSpace(
    _In_ PSD_MINIPORT Miniport,
    _In_ UCHAR Offset,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    return SdPortWinAccessPciConfig(Miniport, TRUE, Offset, Buffer, Length);
}

VOID
NTAPI
SdPortWait(
    _In_ ULONG Microseconds)
{
    KeStallExecutionProcessor(Microseconds);
}

VOID
NTAPI
SdPortWriteRegisterUlong(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _In_ ULONG Data)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)BaseAddress + Register), Data);
}

VOID
NTAPI
SdPortWriteRegisterUshort(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _In_ USHORT Data)
{
    WRITE_REGISTER_USHORT((PUSHORT)((PUCHAR)BaseAddress + Register), Data);
}

VOID
NTAPI
SdPortWriteRegisterUchar(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _In_ UCHAR Data)
{
    WRITE_REGISTER_UCHAR((PUCHAR)BaseAddress + Register, Data);
}

ULONG
NTAPI
SdPortReadRegisterUlong(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)BaseAddress + Register));
}

USHORT
NTAPI
SdPortReadRegisterUshort(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register)
{
    return READ_REGISTER_USHORT((PUSHORT)((PUCHAR)BaseAddress + Register));
}

UCHAR
NTAPI
SdPortReadRegisterUchar(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register)
{
    return READ_REGISTER_UCHAR((PUCHAR)BaseAddress + Register);
}

VOID
NTAPI
SdPortReadRegisterBufferUlong(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _Out_writes_(Length) PULONG Buffer,
    _In_ ULONG Length)
{
    READ_REGISTER_BUFFER_ULONG((PULONG)((PUCHAR)BaseAddress + Register), Buffer, Length);
}

VOID
NTAPI
SdPortReadRegisterBufferUshort(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _Out_writes_(Length) PUSHORT Buffer,
    _In_ ULONG Length)
{
    READ_REGISTER_BUFFER_USHORT((PUSHORT)((PUCHAR)BaseAddress + Register), Buffer, Length);
}

VOID
NTAPI
SdPortReadRegisterBufferUchar(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _Out_writes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    READ_REGISTER_BUFFER_UCHAR((PUCHAR)BaseAddress + Register, Buffer, Length);
}

VOID
NTAPI
SdPortWriteRegisterBufferUlong(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _In_reads_(Length) PULONG Buffer,
    _In_ ULONG Length)
{
    WRITE_REGISTER_BUFFER_ULONG((PULONG)((PUCHAR)BaseAddress + Register), Buffer, Length);
}

VOID
NTAPI
SdPortWriteRegisterBufferUshort(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _In_reads_(Length) PUSHORT Buffer,
    _In_ ULONG Length)
{
    WRITE_REGISTER_BUFFER_USHORT((PUSHORT)((PUCHAR)BaseAddress + Register), Buffer, Length);
}

VOID
NTAPI
SdPortWriteRegisterBufferUchar(
    _In_ PVOID BaseAddress,
    _In_ ULONG Register,
    _In_reads_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    WRITE_REGISTER_BUFFER_UCHAR((PUCHAR)BaseAddress + Register, Buffer, Length);
}
