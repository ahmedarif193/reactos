/*
 * PROJECT:     LiberNT WFP user-mode client
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform management API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winioctl.h>
#include <rpc.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#define NTOS_MODE_USER
#include <ndk/iofuncs.h>
#include <ndk/obfuncs.h>
#include <ndk/rtlfuncs.h>
#include <fwpmu.h>
#include <drivers/wfp/wfpioctl.h>

#define FWP_FACILITY_STATUS 0xC0220000
#define FWP_FACILITY_ERROR  0x80320000

static
DWORD
WfpControl(
    _In_ HANDLE EngineHandle,
    _In_ ULONG ControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID Input,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID Output,
    _In_ ULONG OutputLength)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Status = NtDeviceIoControlFile(EngineHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   ControlCode,
                                   Input,
                                   InputLength,
                                   Output,
                                   OutputLength);
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(EngineHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
        {
            Status = IoStatus.Status;
        }
    }

    if (NT_SUCCESS(Status))
    {
        return ERROR_SUCCESS;
    }
    if (((ULONG)Status & 0xFFFF0000) == FWP_FACILITY_STATUS)
    {
        return FWP_FACILITY_ERROR | ((ULONG)Status & 0xFFFF);
    }
    return RtlNtStatusToDosError(Status);
}

static
VOID
WfpEncodeObject(
    _Out_ PWFP_IOCTL_OBJECT Object,
    _In_ const GUID *Key,
    _In_opt_ const GUID *ProviderKey,
    _In_opt_ const GUID *LayerKey,
    _In_ ULONG Flags,
    _In_ USHORT Weight)
{
    ZeroMemory(Object, sizeof(*Object));
    Object->Key = *Key;
    Object->Flags = Flags;
    Object->Weight = Weight;
    if (ProviderKey != NULL)
    {
        Object->HasProvider = TRUE;
        Object->ProviderKey = *ProviderKey;
    }
    if (LayerKey != NULL)
    {
        Object->LayerKey = *LayerKey;
    }
}

void
WINAPI
FwpmFreeMemory0(
    _Inout_ void **p)
{
    if (p != NULL && *p != NULL)
    {
        HeapFree(GetProcessHeap(), 0, *p);
        *p = NULL;
    }
}

DWORD
WINAPI
FwpmEngineOpen0(
    _In_opt_ const wchar_t *serverName,
    _In_ UINT32 authnService,
    _In_opt_ SEC_WINNT_AUTH_IDENTITY_W *authIdentity,
    _In_opt_ const FWPM_SESSION0 *session,
    _Out_ HANDLE *engineHandle)
{
    WFP_IOCTL_OPEN Open;
    HANDLE Handle;
    DWORD Error;

    UNREFERENCED_PARAMETER(authnService);
    UNREFERENCED_PARAMETER(authIdentity);

    if (engineHandle == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    *engineHandle = NULL;
    if (serverName != NULL)
    {
        return RPC_S_CANNOT_SUPPORT;
    }

    Handle = CreateFileW(WFP_ENGINE_WIN32_NAME,
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    Open.Flags = session != NULL ? session->flags : 0;
    Open.TransactionTimeout = session != NULL ? session->txnWaitTimeoutInMSec : 0;
    Error = WfpControl(Handle, IOCTL_WFP_OPEN, &Open, sizeof(Open), NULL, 0);
    if (Error != ERROR_SUCCESS)
    {
        CloseHandle(Handle);
        return Error;
    }

    *engineHandle = Handle;
    return ERROR_SUCCESS;
}

DWORD
WINAPI
FwpmEngineClose0(
    _Inout_ HANDLE engineHandle)
{
    return CloseHandle(engineHandle) ? ERROR_SUCCESS : GetLastError();
}

DWORD
WINAPI
FwpmTransactionBegin0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 flags)
{
    ULONG Flags = flags;

    return WfpControl(engineHandle, IOCTL_WFP_TRANSACTION_BEGIN, &Flags, sizeof(Flags), NULL, 0);
}

DWORD
WINAPI
FwpmTransactionCommit0(
    _In_ HANDLE engineHandle)
{
    return WfpControl(engineHandle, IOCTL_WFP_TRANSACTION_COMMIT, NULL, 0, NULL, 0);
}

DWORD
WINAPI
FwpmTransactionAbort0(
    _In_ HANDLE engineHandle)
{
    return WfpControl(engineHandle, IOCTL_WFP_TRANSACTION_ABORT, NULL, 0, NULL, 0);
}

DWORD
WINAPI
FwpmProviderAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_PROVIDER0 *provider,
    _In_opt_ PSECURITY_DESCRIPTOR sd)
{
    WFP_IOCTL_OBJECT Object;

    UNREFERENCED_PARAMETER(sd);

    if (provider == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    WfpEncodeObject(&Object, &provider->providerKey, NULL, NULL, provider->flags, 0);
    return WfpControl(engineHandle, IOCTL_WFP_PROVIDER_ADD, &Object, sizeof(Object), NULL, 0);
}

DWORD
WINAPI
FwpmProviderDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    WFP_IOCTL_OBJECT Object;

    if (key == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    WfpEncodeObject(&Object, key, NULL, NULL, 0, 0);
    return WfpControl(engineHandle, IOCTL_WFP_PROVIDER_DELETE, &Object, sizeof(Object), NULL, 0);
}

DWORD
WINAPI
FwpmSubLayerAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_SUBLAYER0 *subLayer,
    _In_opt_ PSECURITY_DESCRIPTOR sd)
{
    WFP_IOCTL_OBJECT Object;

    UNREFERENCED_PARAMETER(sd);

    if (subLayer == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    WfpEncodeObject(&Object, &subLayer->subLayerKey, subLayer->providerKey, NULL, subLayer->flags, subLayer->weight);
    return WfpControl(engineHandle, IOCTL_WFP_SUBLAYER_ADD, &Object, sizeof(Object), NULL, 0);
}

DWORD
WINAPI
FwpmSubLayerDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    WFP_IOCTL_OBJECT Object;

    if (key == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    WfpEncodeObject(&Object, key, NULL, NULL, 0, 0);
    return WfpControl(engineHandle, IOCTL_WFP_SUBLAYER_DELETE, &Object, sizeof(Object), NULL, 0);
}

DWORD
WINAPI
FwpmSubLayerGetByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key,
    _Outptr_ FWPM_SUBLAYER0 **subLayer)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(key);

    *subLayer = NULL;
    return RPC_S_CANNOT_SUPPORT;
}

DWORD
WINAPI
FwpmCalloutAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_CALLOUT0 *callout,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT32 *id)
{
    union
    {
        WFP_IOCTL_OBJECT Object;
        UINT32 Id;
    } Buffer;
    DWORD Error;

    UNREFERENCED_PARAMETER(sd);

    if (callout == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    WfpEncodeObject(&Buffer.Object, &callout->calloutKey, callout->providerKey, &callout->applicableLayer, callout->flags, 0);
    Error = WfpControl(engineHandle, IOCTL_WFP_CALLOUT_ADD, &Buffer, sizeof(Buffer), &Buffer, sizeof(Buffer));
    if (Error == ERROR_SUCCESS && id != NULL)
    {
        *id = Buffer.Id;
    }
    return Error;
}

DWORD
WINAPI
FwpmCalloutDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 id)
{
    return WfpControl(engineHandle, IOCTL_WFP_CALLOUT_DELETE_ID, &id, sizeof(id), NULL, 0);
}

DWORD
WINAPI
FwpmCalloutDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    WFP_IOCTL_OBJECT Object;

    if (key == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    WfpEncodeObject(&Object, key, NULL, NULL, 0, 0);
    return WfpControl(engineHandle, IOCTL_WFP_CALLOUT_DELETE_KEY, &Object, sizeof(Object), NULL, 0);
}

static
BOOL
WfpEncodeValue(
    _In_ const FWP_CONDITION_VALUE0 *Value,
    _Out_opt_ PWFP_IOCTL_VALUE Encoded,
    _In_opt_ PUCHAR Base,
    _Inout_ PULONG Offset)
{
    const VOID *Data = NULL;
    ULONG Size = 0;
    ULONG64 Number = 0;

    switch (Value->type)
    {
        case FWP_EMPTY:
            break;

        case FWP_UINT8:
            Number = Value->uint8;
            break;

        case FWP_UINT16:
            Number = Value->uint16;
            break;

        case FWP_UINT32:
            Number = Value->uint32;
            break;

        case FWP_UINT64:
            if (Value->uint64 == NULL)
            {
                return FALSE;
            }
            Number = *Value->uint64;
            break;

        case FWP_BYTE_ARRAY16_TYPE:
            Data = Value->byteArray16;
            Size = sizeof(FWP_BYTE_ARRAY16);
            break;

        case FWP_BYTE_ARRAY6_TYPE:
            Data = Value->byteArray6;
            Size = sizeof(FWP_BYTE_ARRAY6);
            break;

        case FWP_BYTE_BLOB_TYPE:
        case FWP_SECURITY_DESCRIPTOR_TYPE:
        case FWP_TOKEN_ACCESS_INFORMATION_TYPE:
            if (Value->byteBlob == NULL)
            {
                return FALSE;
            }
            Data = Value->byteBlob->data;
            Size = Value->byteBlob->size;
            break;

        case FWP_SID:
            if (Value->sid == NULL || !IsValidSid(Value->sid))
            {
                return FALSE;
            }
            Data = Value->sid;
            Size = GetLengthSid(Value->sid);
            break;

        case FWP_UNICODE_STRING_TYPE:
            if (Value->unicodeString == NULL)
            {
                return FALSE;
            }
            Data = Value->unicodeString;
            Size = (lstrlenW(Value->unicodeString) + 1) * sizeof(WCHAR);
            break;

        case FWP_V4_ADDR_MASK:
            Data = Value->v4AddrMask;
            Size = sizeof(FWP_V4_ADDR_AND_MASK);
            break;

        case FWP_V6_ADDR_MASK:
            Data = Value->v6AddrMask;
            Size = sizeof(FWP_V6_ADDR_AND_MASK);
            break;

        default:
            return FALSE;
    }

    if (Size != 0 && Data == NULL)
    {
        return FALSE;
    }

    if (Encoded != NULL)
    {
        Encoded->Type = Value->type;
        Encoded->Size = Size;
        Encoded->Offset = Size != 0 ? *Offset : 0;
        Encoded->Number = Number;
        if (Size != 0)
        {
            CopyMemory(Base + *Offset, Data, Size);
        }
    }
    *Offset += (Size + sizeof(UINT64) - 1) & ~(sizeof(UINT64) - 1);
    return TRUE;
}

static
BOOL
WfpEncodeCondition(
    _In_ const FWPM_FILTER_CONDITION0 *Condition,
    _Out_opt_ PWFP_IOCTL_CONDITION Encoded,
    _In_opt_ PUCHAR Base,
    _Inout_ PULONG Offset)
{
    const FWP_RANGE0 *Range;

    if (Encoded != NULL)
    {
        ZeroMemory(Encoded, sizeof(*Encoded));
        Encoded->FieldKey = Condition->fieldKey;
        Encoded->MatchType = Condition->matchType;
    }

    if (Condition->conditionValue.type != FWP_RANGE_TYPE)
    {
        return WfpEncodeValue(&Condition->conditionValue, Encoded != NULL ? &Encoded->Value : NULL, Base, Offset);
    }

    Range = Condition->conditionValue.rangeValue;
    if (Range == NULL || Range->valueHigh.type == FWP_EMPTY)
    {
        return FALSE;
    }
    return WfpEncodeValue((const FWP_CONDITION_VALUE0 *)&Range->valueLow,
                          Encoded != NULL ? &Encoded->Value : NULL,
                          Base,
                          Offset) &&
           WfpEncodeValue((const FWP_CONDITION_VALUE0 *)&Range->valueHigh,
                          Encoded != NULL ? &Encoded->High : NULL,
                          Base,
                          Offset);
}

DWORD
WINAPI
FwpmFilterAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_FILTER0 *filter,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT64 *id)
{
    PWFP_IOCTL_FILTER Encoded;
    ULONG Fixed, Offset, Total, Index;
    DWORD Error;

    UNREFERENCED_PARAMETER(sd);

    if (filter == NULL || (filter->numFilterConditions != 0 && filter->filterCondition == NULL))
    {
        return ERROR_INVALID_PARAMETER;
    }

    Fixed = FIELD_OFFSET(WFP_IOCTL_FILTER, Conditions) + filter->numFilterConditions * sizeof(WFP_IOCTL_CONDITION);
    Fixed = (Fixed + sizeof(UINT64) - 1) & ~(sizeof(UINT64) - 1);
    Offset = Fixed;
    for (Index = 0; Index < filter->numFilterConditions; Index++)
    {
        if (!WfpEncodeCondition(&filter->filterCondition[Index], NULL, NULL, &Offset))
        {
            return FWP_E_TYPE_MISMATCH;
        }
    }

    Total = Offset;

    Encoded = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Total);
    if (Encoded == NULL)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    Encoded->FilterKey = filter->filterKey;
    Encoded->Flags = filter->flags;
    if (filter->providerKey != NULL)
    {
        Encoded->HasProvider = TRUE;
        Encoded->ProviderKey = *filter->providerKey;
    }
    Encoded->LayerKey = filter->layerKey;
    Encoded->SubLayerKey = filter->subLayerKey;
    Encoded->ActionKey = filter->action.calloutKey;
    Encoded->ActionType = filter->action.type;
    Encoded->WeightType = filter->weight.type;
    if (filter->weight.type == FWP_UINT8)
    {
        Encoded->Weight = filter->weight.uint8;
    }
    else if (filter->weight.type == FWP_UINT64 && filter->weight.uint64 != NULL)
    {
        Encoded->Weight = *filter->weight.uint64;
    }
    Encoded->RawContext = filter->rawContext;
    Encoded->ConditionCount = filter->numFilterConditions;

    Offset = Fixed;
    for (Index = 0; Index < filter->numFilterConditions; Index++)
    {
        WfpEncodeCondition(&filter->filterCondition[Index], &Encoded->Conditions[Index], (PUCHAR)Encoded, &Offset);
    }

    Error = WfpControl(engineHandle, IOCTL_WFP_FILTER_ADD, Encoded, Total, Encoded, sizeof(UINT64));
    if (Error == ERROR_SUCCESS && id != NULL)
    {
        *id = *(UINT64 *)Encoded;
    }
    HeapFree(GetProcessHeap(), 0, Encoded);
    return Error;
}

DWORD
WINAPI
FwpmFilterDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT64 id)
{
    return WfpControl(engineHandle, IOCTL_WFP_FILTER_DELETE_ID, &id, sizeof(id), NULL, 0);
}

DWORD
WINAPI
FwpmFilterDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    WFP_IOCTL_OBJECT Object;

    if (key == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    WfpEncodeObject(&Object, key, NULL, NULL, 0, 0);
    return WfpControl(engineHandle, IOCTL_WFP_FILTER_DELETE_KEY, &Object, sizeof(Object), NULL, 0);
}

DWORD
WINAPI
FwpmFilterCreateEnumHandle0(
    _In_ HANDLE engineHandle,
    _In_opt_ const void *enumTemplate,
    _Out_ HANDLE *enumHandle)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(enumTemplate);

    *enumHandle = NULL;
    return RPC_S_CANNOT_SUPPORT;
}

DWORD
WINAPI
FwpmFilterDestroyEnumHandle0(
    _In_ HANDLE engineHandle,
    _Inout_ HANDLE enumHandle)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(enumHandle);

    return RPC_S_CANNOT_SUPPORT;
}

DWORD
WINAPI
FwpmFilterEnum0(
    _In_ HANDLE engineHandle,
    _In_ HANDLE enumHandle,
    _In_ UINT32 numEntriesRequested,
    _Outptr_ FWPM_FILTER0 ***entries,
    _Out_ UINT32 *numEntriesReturned)
{
    UNREFERENCED_PARAMETER(engineHandle);
    UNREFERENCED_PARAMETER(enumHandle);
    UNREFERENCED_PARAMETER(numEntriesRequested);

    *entries = NULL;
    *numEntriesReturned = 0;
    return RPC_S_CANNOT_SUPPORT;
}

DWORD
WINAPI
FwpmGetAppIdFromFileName0(
    _In_ PCWSTR fileName,
    _Outptr_ FWP_BYTE_BLOB **appId)
{
    union
    {
        OBJECT_NAME_INFORMATION Information;
        WCHAR Storage[1024];
    } Name;
    FWP_BYTE_BLOB *Blob;
    HANDLE File;
    NTSTATUS Status;
    PWCHAR Text;
    ULONG Length, Index;

    if (fileName == NULL || appId == NULL)
    {
        return ERROR_INVALID_PARAMETER;
    }
    *appId = NULL;

    File = CreateFileW(fileName,
                       0,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        return GetLastError();
    }

    Status = NtQueryObject(File, ObjectNameInformation, &Name, sizeof(Name), &Length);
    CloseHandle(File);
    if (!NT_SUCCESS(Status))
    {
        return RtlNtStatusToDosError(Status);
    }

    Length = Name.Information.Name.Length;
    Blob = HeapAlloc(GetProcessHeap(), 0, sizeof(*Blob) + Length + sizeof(WCHAR));
    if (Blob == NULL)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    Text = (PWCHAR)(Blob + 1);
    for (Index = 0; Index < Length / sizeof(WCHAR); Index++)
    {
        Text[Index] = RtlDowncaseUnicodeChar(Name.Information.Name.Buffer[Index]);
    }
    Text[Index] = UNICODE_NULL;
    Blob->size = Length + sizeof(WCHAR);
    Blob->data = (UINT8 *)Text;

    *appId = Blob;
    return ERROR_SUCCESS;
}
