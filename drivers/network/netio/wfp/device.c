/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform management device for user-mode clients
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wfp.h"

#include <wdmsec.h>
#include <drivers/wfp/wfpioctl.h>

typedef struct _WFP_FILE
{
    FAST_MUTEX Lock;
    HANDLE Session;
} WFP_FILE, *PWFP_FILE;

typedef struct _WFP_SCRATCH
{
    UINT64 Number[2];
    FWP_BYTE_BLOB Blob[2];
    FWP_RANGE0 Range;
} WFP_SCRATCH, *PWFP_SCRATCH;

static
NTSTATUS
WfpDecodeValue(
    _In_ PUCHAR Base,
    _In_ ULONG Length,
    _In_ const WFP_IOCTL_VALUE *Encoded,
    _Out_ FWP_CONDITION_VALUE0 *Value,
    _Out_ PUINT64 Number,
    _Out_ FWP_BYTE_BLOB *Blob)
{
    PUCHAR Data = NULL;
    PCWSTR Text;

    RtlZeroMemory(Value, sizeof(*Value));
    Value->type = Encoded->Type;
    switch (Encoded->Type)
    {
        case FWP_EMPTY:
            return STATUS_SUCCESS;

        case FWP_UINT8:
            Value->uint8 = (UINT8)Encoded->Number;
            return STATUS_SUCCESS;

        case FWP_UINT16:
            Value->uint16 = (UINT16)Encoded->Number;
            return STATUS_SUCCESS;

        case FWP_UINT32:
            Value->uint32 = (UINT32)Encoded->Number;
            return STATUS_SUCCESS;

        case FWP_UINT64:
            *Number = Encoded->Number;
            Value->uint64 = Number;
            return STATUS_SUCCESS;

        default:
            break;
    }

    if (Encoded->Offset > Length || Encoded->Size > Length - Encoded->Offset ||
        (Encoded->Offset & (sizeof(UINT64) - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Data = Base + Encoded->Offset;

    switch (Encoded->Type)
    {
        case FWP_BYTE_ARRAY16_TYPE:
            if (Encoded->Size != sizeof(FWP_BYTE_ARRAY16))
            {
                return STATUS_INVALID_PARAMETER;
            }
            Value->byteArray16 = (FWP_BYTE_ARRAY16 *)Data;
            return STATUS_SUCCESS;

        case FWP_BYTE_ARRAY6_TYPE:
            if (Encoded->Size != sizeof(FWP_BYTE_ARRAY6))
            {
                return STATUS_INVALID_PARAMETER;
            }
            Value->byteArray6 = (FWP_BYTE_ARRAY6 *)Data;
            return STATUS_SUCCESS;

        case FWP_BYTE_BLOB_TYPE:
        case FWP_SECURITY_DESCRIPTOR_TYPE:
        case FWP_TOKEN_ACCESS_INFORMATION_TYPE:
            Blob->size = Encoded->Size;
            Blob->data = Data;
            Value->byteBlob = Blob;
            return STATUS_SUCCESS;

        case FWP_SID:
            if (Encoded->Size < RtlLengthRequiredSid(0) ||
                Encoded->Size < RtlLengthRequiredSid(((PISID)Data)->SubAuthorityCount) ||
                !RtlValidSid((PSID)Data))
            {
                return STATUS_INVALID_PARAMETER;
            }
            Value->sid = (SID *)Data;
            return STATUS_SUCCESS;

        case FWP_UNICODE_STRING_TYPE:
            Text = (PCWSTR)Data;
            if (Encoded->Size < sizeof(WCHAR) || (Encoded->Size % sizeof(WCHAR)) != 0 ||
                Text[Encoded->Size / sizeof(WCHAR) - 1] != UNICODE_NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }
            Value->unicodeString = (LPWSTR)Data;
            return STATUS_SUCCESS;

        case FWP_V4_ADDR_MASK:
            if (Encoded->Size != sizeof(FWP_V4_ADDR_AND_MASK))
            {
                return STATUS_INVALID_PARAMETER;
            }
            Value->v4AddrMask = (FWP_V4_ADDR_AND_MASK *)Data;
            return STATUS_SUCCESS;

        case FWP_V6_ADDR_MASK:
            if (Encoded->Size != sizeof(FWP_V6_ADDR_AND_MASK))
            {
                return STATUS_INVALID_PARAMETER;
            }
            Value->v6AddrMask = (FWP_V6_ADDR_AND_MASK *)Data;
            return STATUS_SUCCESS;

        default:
            return STATUS_FWP_TYPE_MISMATCH;
    }
}

static
NTSTATUS
WfpAddFilter(
    _In_ HANDLE Session,
    _In_ PWFP_IOCTL_FILTER Encoded,
    _In_ ULONG Length,
    _Out_ PUINT64 Id)
{
    FWPM_FILTER_CONDITION0 *Conditions = NULL;
    PWFP_SCRATCH Scratch = NULL;
    FWPM_FILTER0 Filter;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    if (Length < FIELD_OFFSET(WFP_IOCTL_FILTER, Conditions) ||
        Encoded->ConditionCount > (Length - FIELD_OFFSET(WFP_IOCTL_FILTER, Conditions)) / sizeof(WFP_IOCTL_CONDITION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Encoded->ConditionCount != 0)
    {
        Conditions = ExAllocatePoolWithTag(PagedPool,
                                           Encoded->ConditionCount * (sizeof(*Conditions) + sizeof(*Scratch)),
                                           WFP_TAG);
        if (Conditions == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Scratch = (PWFP_SCRATCH)(Conditions + Encoded->ConditionCount);
    }

    for (Index = 0; NT_SUCCESS(Status) && Index < Encoded->ConditionCount; Index++)
    {
        Conditions[Index].fieldKey = Encoded->Conditions[Index].FieldKey;
        Conditions[Index].matchType = Encoded->Conditions[Index].MatchType;
        if (Encoded->Conditions[Index].High.Type == FWP_EMPTY)
        {
            Status = WfpDecodeValue((PUCHAR)Encoded,
                                    Length,
                                    &Encoded->Conditions[Index].Value,
                                    &Conditions[Index].conditionValue,
                                    &Scratch[Index].Number[0],
                                    &Scratch[Index].Blob[0]);
            continue;
        }

        Status = WfpDecodeValue((PUCHAR)Encoded,
                                Length,
                                &Encoded->Conditions[Index].Value,
                                (FWP_CONDITION_VALUE0 *)&Scratch[Index].Range.valueLow,
                                &Scratch[Index].Number[0],
                                &Scratch[Index].Blob[0]);
        if (NT_SUCCESS(Status))
        {
            Status = WfpDecodeValue((PUCHAR)Encoded,
                                    Length,
                                    &Encoded->Conditions[Index].High,
                                    (FWP_CONDITION_VALUE0 *)&Scratch[Index].Range.valueHigh,
                                    &Scratch[Index].Number[1],
                                    &Scratch[Index].Blob[1]);
        }
        Conditions[Index].conditionValue.type = FWP_RANGE_TYPE;
        Conditions[Index].conditionValue.rangeValue = &Scratch[Index].Range;
    }

    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Filter, sizeof(Filter));
        Filter.filterKey = Encoded->FilterKey;
        Filter.flags = Encoded->Flags;
        Filter.providerKey = Encoded->HasProvider ? &Encoded->ProviderKey : NULL;
        Filter.layerKey = Encoded->LayerKey;
        Filter.subLayerKey = Encoded->SubLayerKey;
        Filter.weight.type = Encoded->WeightType;
        if (Encoded->WeightType == FWP_UINT8)
        {
            Filter.weight.uint8 = (UINT8)Encoded->Weight;
        }
        else if (Encoded->WeightType == FWP_UINT64)
        {
            Filter.weight.uint64 = &Encoded->Weight;
        }
        Filter.numFilterConditions = Encoded->ConditionCount;
        Filter.filterCondition = Conditions;
        Filter.action.type = Encoded->ActionType;
        Filter.action.calloutKey = Encoded->ActionKey;
        Filter.rawContext = Encoded->RawContext;
        Status = FwpmFilterAdd0(Session, &Filter, NULL, Id);
    }

    if (Conditions != NULL)
    {
        ExFreePoolWithTag(Conditions, WFP_TAG);
    }
    return Status;
}

static
NTSTATUS
WfpDeviceControl(
    _In_ PWFP_FILE File,
    _In_ ULONG ControlCode,
    _Inout_ PVOID Buffer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    PWFP_IOCTL_OBJECT Object = Buffer;
    FWPM_PROVIDER0 Provider;
    FWPM_SUBLAYER0 SubLayer;
    FWPM_CALLOUT0 Callout;
    FWPM_SESSION0 Session;
    NTSTATUS Status;
    UINT64 FilterId;
    UINT32 CalloutId;

    *Information = 0;

    if (ControlCode == IOCTL_WFP_OPEN)
    {
        if (InputLength < sizeof(WFP_IOCTL_OPEN) || File->Session != NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }

        RtlZeroMemory(&Session, sizeof(Session));
        Session.flags = ((PWFP_IOCTL_OPEN)Buffer)->Flags;
        Session.txnWaitTimeoutInMSec = ((PWFP_IOCTL_OPEN)Buffer)->TransactionTimeout;
        return FwpmEngineOpen0(NULL, 0, NULL, &Session, &File->Session);
    }

    if (File->Session == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    switch (ControlCode)
    {
        case IOCTL_WFP_TRANSACTION_BEGIN:
            return FwpmTransactionBegin0(File->Session, InputLength >= sizeof(ULONG) ? *(PULONG)Buffer : 0);

        case IOCTL_WFP_TRANSACTION_COMMIT:
            return FwpmTransactionCommit0(File->Session);

        case IOCTL_WFP_TRANSACTION_ABORT:
            return FwpmTransactionAbort0(File->Session);

        case IOCTL_WFP_FILTER_ADD:
            if (OutputLength < sizeof(UINT64))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            Status = WfpAddFilter(File->Session, Buffer, InputLength, &FilterId);
            if (NT_SUCCESS(Status))
            {
                *(PUINT64)Buffer = FilterId;
                *Information = sizeof(UINT64);
            }
            return Status;

        case IOCTL_WFP_FILTER_DELETE_ID:
            if (InputLength < sizeof(UINT64))
            {
                return STATUS_INVALID_PARAMETER;
            }
            return FwpmFilterDeleteById0(File->Session, *(PUINT64)Buffer);

        case IOCTL_WFP_CALLOUT_DELETE_ID:
            if (InputLength < sizeof(UINT32))
            {
                return STATUS_INVALID_PARAMETER;
            }
            return FwpmCalloutDeleteById0(File->Session, *(PUINT32)Buffer);

        default:
            break;
    }

    if (InputLength < sizeof(WFP_IOCTL_OBJECT))
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (ControlCode)
    {
        case IOCTL_WFP_PROVIDER_ADD:
            RtlZeroMemory(&Provider, sizeof(Provider));
            Provider.providerKey = Object->Key;
            Provider.flags = Object->Flags;
            return FwpmProviderAdd0(File->Session, &Provider, NULL);

        case IOCTL_WFP_PROVIDER_DELETE:
            return FwpmProviderDeleteByKey0(File->Session, &Object->Key);

        case IOCTL_WFP_SUBLAYER_ADD:
            RtlZeroMemory(&SubLayer, sizeof(SubLayer));
            SubLayer.subLayerKey = Object->Key;
            SubLayer.flags = Object->Flags;
            SubLayer.weight = Object->Weight;
            SubLayer.providerKey = Object->HasProvider ? &Object->ProviderKey : NULL;
            return FwpmSubLayerAdd0(File->Session, &SubLayer, NULL);

        case IOCTL_WFP_SUBLAYER_DELETE:
            return FwpmSubLayerDeleteByKey0(File->Session, &Object->Key);

        case IOCTL_WFP_CALLOUT_ADD:
            if (OutputLength < sizeof(UINT32))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }
            RtlZeroMemory(&Callout, sizeof(Callout));
            Callout.calloutKey = Object->Key;
            Callout.flags = Object->Flags;
            Callout.applicableLayer = Object->LayerKey;
            Callout.providerKey = Object->HasProvider ? &Object->ProviderKey : NULL;
            Status = FwpmCalloutAdd0(File->Session, &Callout, NULL, &CalloutId);
            if (NT_SUCCESS(Status))
            {
                *(PUINT32)Buffer = CalloutId;
                *Information = sizeof(UINT32);
            }
            return Status;

        case IOCTL_WFP_CALLOUT_DELETE_KEY:
            return FwpmCalloutDeleteByKey0(File->Session, &Object->Key);

        case IOCTL_WFP_FILTER_DELETE_KEY:
            return FwpmFilterDeleteByKey0(File->Session, &Object->Key);

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

static
NTSTATUS
NTAPI
WfpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PWFP_FILE File = Stack->FileObject->FsContext;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG_PTR Information = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            File = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*File), WFP_TAG);
            if (File == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }
            ExInitializeFastMutex(&File->Lock);
            File->Session = NULL;
            Stack->FileObject->FsContext = File;
            break;

        case IRP_MJ_CLEANUP:
            KeEnterCriticalRegion();
            ExAcquireFastMutexUnsafe(&File->Lock);
            if (File->Session != NULL)
            {
                FwpmEngineClose0(File->Session);
                File->Session = NULL;
            }
            ExReleaseFastMutexUnsafe(&File->Lock);
            KeLeaveCriticalRegion();
            break;

        case IRP_MJ_CLOSE:
            ExFreePoolWithTag(File, WFP_TAG);
            Stack->FileObject->FsContext = NULL;
            break;

        case IRP_MJ_DEVICE_CONTROL:
            KeEnterCriticalRegion();
            ExAcquireFastMutexUnsafe(&File->Lock);
            Status = WfpDeviceControl(File,
                                      Stack->Parameters.DeviceIoControl.IoControlCode,
                                      Irp->AssociatedIrp.SystemBuffer,
                                      Stack->Parameters.DeviceIoControl.InputBufferLength,
                                      Stack->Parameters.DeviceIoControl.OutputBufferLength,
                                      &Information);
            ExReleaseFastMutexUnsafe(&File->Lock);
            KeLeaveCriticalRegion();
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
WfpCreateDevice(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(WFP_ENGINE_DEVICE_NAME);
    UNICODE_STRING LinkName = RTL_CONSTANT_STRING(WFP_ENGINE_SYMLINK_NAME);
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDeviceSecure(DriverObject,
                                  0,
                                  &DeviceName,
                                  FILE_DEVICE_NETWORK,
                                  FILE_DEVICE_SECURE_OPEN,
                                  FALSE,
                                  &SDDL_DEVOBJ_SYS_ALL_ADM_ALL,
                                  NULL,
                                  &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = IoCreateSymbolicLink(&LinkName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = WfpDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = WfpDispatch;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = WfpDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = WfpDispatch;
    DeviceObject->Flags |= DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}
