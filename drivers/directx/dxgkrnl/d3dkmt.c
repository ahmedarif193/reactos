/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT IOCTL dispatch and user-mode API implementation
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Handles IRP_MJ_DEVICE_CONTROL, IRP_MJ_CREATE, and IRP_MJ_CLOSE IRPs on
 * the dxgkrnl control device (\Device\DxgKrnl).
 *
 * D3DKMT entry points are dispatched via the IOCTL code embedded in the
 * IRP.  User-mode D3DKMT functions (from gdi32.dll / dxgi.dll) send
 * METHOD_BUFFERED IOCTLs to this device to perform GPU adapter discovery,
 * mode enumeration, device creation, and rendering operations.
 *
 * Implemented D3DKMT entry points:
 *   DxgkEnumAdapters          — D3DKMTEnumAdapters
 *   DxgkEnumAdapters2         — D3DKMTEnumAdapters2
 *   DxgkOpenAdapterFromLuid   — D3DKMTOpenAdapterFromLuid
 *   DxgkCloseAdapter          — D3DKMTCloseAdapter
 *   DxgkQueryAdapterInfo      — D3DKMTQueryAdapterInfo
 *   DxgkGetDisplayModeList    — D3DKMTGetDisplayModeList (delegated to vidpn.c)
 */

#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidsch.h"
#include "d3dkmt.h"
#include "vidpn.h"
#include "present.h"
#include "pnp.h"

C_ASSERT(sizeof(RXGK_CREATECONTEXTVIRTUAL_PACKET) == RXGK_CREATECONTEXTVIRTUAL_PACKET_V1_SIZE);
C_ASSERT(sizeof(RXGK_SUBMITCOMMAND_PACKET) == RXGK_SUBMITCOMMAND_PACKET_V1_SIZE);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, Commands) == 8);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, PresentHistoryToken) == 24);
C_ASSERT(DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS == D3DDDI_MAX_BROADCAST_CONTEXT + 1);
C_ASSERT(sizeof(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL) == sizeof(D3DDDI_MAPGPUVIRTUALADDRESS));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, hPagingQueue) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, hPagingQueue));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, BaseAddress) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, BaseAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, MinimumAddress) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, MinimumAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, MaximumAddress) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, MaximumAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, hAllocation) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, hAllocation));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, OffsetInPages) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, OffsetInPages));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, SizeInPages) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, SizeInPages));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, Protection) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, Protection));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, DriverProtection) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, DriverProtection));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, Reserved0) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, Reserved0));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, Reserved1) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, Reserved1));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, VirtualAddress) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, VirtualAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL, PagingFenceValue) == FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, PagingFenceValue));
C_ASSERT(sizeof(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL) == sizeof(D3DDDI_RESERVEGPUVIRTUALADDRESS));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, hPagingQueue) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, hPagingQueue));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, hAdapter) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, hAdapter));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, BaseAddress) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, BaseAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, MinimumAddress) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, MinimumAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, MaximumAddress) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, MaximumAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, Size) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, Size));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, ReservationType) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, ReservationType));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, DriverProtection) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, DriverProtection));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, VirtualAddress) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, VirtualAddress));
C_ASSERT(FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL, PagingFenceValue) == FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, PagingFenceValue));
C_ASSERT(sizeof(D3DKMT_FREEGPUVIRTUALADDRESS_LOCAL) == sizeof(D3DKMT_FREEGPUVIRTUALADDRESS));
C_ASSERT(sizeof(D3DKMT_UPDATEGPUVIRTUALADDRESS_LOCAL) == sizeof(D3DKMT_UPDATEGPUVIRTUALADDRESS));

/* Legacy IOCTL for miniport DxgkInitialize resolution */
#ifndef IOCTL_DXGKRNL_ESCAPE
#define IOCTL_DXGKRNL_ESCAPE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

/* Native Displib kernel-entry resolvers: DOD initialize, full Win8-style
 * initialize, and WDDM 2.0+ uninitialize respectively. */
#define IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY \
    CTL_CODE(0x23, 0x10, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY \
    CTL_CODE(0x23, 0x0F, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY \
    CTL_CODE(0x23, 0x11, METHOD_NEITHER, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_GET_UNINIT_ENTRY \
    CTL_CODE(0x23, 0x12, METHOD_NEITHER, FILE_ANY_ACCESS)

/* Forward declaration from adapter.c */
NTSTATUS APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);

NTSTATUS APIENTRY
DxgkUnInitialize(
    _In_ PDRIVER_OBJECT DriverObject);

/* ========================================================================
 * D3DKMT handle namespace
 *
 * Adapter, device, context, synchronization, and paging-queue handles are
 * owner-scoped typed generation ids. No public handle is reconstructed into
 * a kernel pointer, including on 64-bit builds.
 * ====================================================================== */

#define DXGKP_FILE_CONTEXT_MAGIC 0x58474446 /* "FDGX" */
#define DXGKP_GPUVA_PAGE_SIZE    4096ULL
#define DXGKP_MAX_D3DKMT_LIST_COUNT 4096U
/* Offer/Reclaim is backed by the residency state machine: an offered
 * allocation is a preferred eviction victim whose eviction marks the content
 * discarded, and reclaim reports that discard to the caller. */
#define DXGKP_OFFER_RECLAIM_END_TO_END 1
#ifndef PROCESS_QUERY_INFORMATION
#define PROCESS_QUERY_INFORMATION 0x0400
#endif
#ifndef PROCESS_SET_INFORMATION
#define PROCESS_SET_INFORMATION 0x0200
#endif

typedef struct _DXGKRNL_FILE_CONTEXT
{
    ULONG Magic;
} DXGKRNL_FILE_CONTEXT, *PDXGKRNL_FILE_CONTEXT;

/*
 * WDDM 2.x/3.x code in this tree remains experimental.  Public version
 * reporting stays at the last ABI- and behavior-complete level until the full
 * initialization-data layout, non-empty render path, scheduler, VidMm,
 * synchronization, teardown, and native comparison contracts are complete.
 */
#define DXGKP_OS_COMPLETED_WDDM_LEVEL KMT_DRIVERVERSION_WDDM_1_3

static D3DKMT_DRIVERVERSION
DxgkpMiniportDeclaredWddmLevel(
    _In_ ULONG Version)
{
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM3_2)
        return KMT_DRIVERVERSION_WDDM_3_2;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM3_1)
        return KMT_DRIVERVERSION_WDDM_3_1;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM3_0)
        return KMT_DRIVERVERSION_WDDM_3_0;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_9)
        return KMT_DRIVERVERSION_WDDM_2_9;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_8)
        return KMT_DRIVERVERSION_WDDM_2_8;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_7)
        return KMT_DRIVERVERSION_WDDM_2_7;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_6)
        return KMT_DRIVERVERSION_WDDM_2_6;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_5)
        return KMT_DRIVERVERSION_WDDM_2_5;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_4)
        return KMT_DRIVERVERSION_WDDM_2_4;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_3)
        return KMT_DRIVERVERSION_WDDM_2_3;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_2)
        return KMT_DRIVERVERSION_WDDM_2_2;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        return KMT_DRIVERVERSION_WDDM_2_1;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        return KMT_DRIVERVERSION_WDDM_2_0;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
        return KMT_DRIVERVERSION_WDDM_1_3;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WIN8)
        return KMT_DRIVERVERSION_WDDM_1_2;
    if (Version >= DXGKDDI_INTERFACE_VERSION_WIN7)
        return KMT_DRIVERVERSION_WDDM_1_1;
    return KMT_DRIVERVERSION_WDDM_1_0;
}

static D3DKMT_DRIVERVERSION
DxgkpGetReportedDriverVersion(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    D3DKMT_DRIVERVERSION Declared;

    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return KMT_DRIVERVERSION_WDDM_1_0;

    Declared = DxgkpMiniportDeclaredWddmLevel(Adapter->MiniportContext->InitData.s.Version);
    if (Declared < KMT_DRIVERVERSION_WDDM_2_0)
        return Declared;
    return min(Declared, DXGKP_OS_COMPLETED_WDDM_LEVEL);
}

static BOOLEAN
DxgkpAdapterSupportsRender(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    /* Render support means a public D3DKMTRender stream reaches the complete
     * Render/Patch/SubmitCommand path, so every callback that path uses must
     * exist on a full (non display-only) miniport. */
    if (Adapter == NULL || Adapter->MiniportContext == NULL)
        return FALSE;
    if (Adapter->MiniportContext->UseDodLayout || Adapter->MiniportContext->IsDisplayOnlyDriver)
        return FALSE;
    return DXGK_CB_FULL(Adapter, DxgkDdiRender) != NULL &&
           DXGK_CB_FULL(Adapter, DxgkDdiPatch) != NULL &&
           DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) != NULL &&
           DXGK_CB_FULL(Adapter, DxgkDdiCreateContext) != NULL;
}

static NTSTATUS
DxgkpOpenAdapterDriverKey(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PHANDLE KeyHandle)
{
    if (Adapter == NULL ||
        Adapter->PhysicalDeviceObject == NULL ||
        KeyHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *KeyHandle = NULL;
    return IoOpenDeviceRegistryKey(Adapter->PhysicalDeviceObject,
                                   PLUGPLAY_REGKEY_DRIVER,
                                   KEY_READ,
                                   KeyHandle);
}

static NTSTATUS
DxgkpOpenMiniportServiceKey(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PHANDLE KeyHandle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->RegistryPath.Buffer == NULL ||
        KeyHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *KeyHandle = NULL;

    InitializeObjectAttributes(&ObjectAttributes,
                               &Adapter->MiniportContext->RegistryPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    return ZwOpenKey(KeyHandle, KEY_READ, &ObjectAttributes);
}

static NTSTATUS
DxgkpQueryRegistryValue(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _Outptr_ PKEY_VALUE_PARTIAL_INFORMATION *ValueInformation)
{
    UNICODE_STRING ValueNameU;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo;
    ULONG ResultLength = 0;
    NTSTATUS Status;

    if (KeyHandle == NULL || ValueName == NULL || ValueInformation == NULL)
        return STATUS_INVALID_PARAMETER;

    *ValueInformation = NULL;
    RtlInitUnicodeString(&ValueNameU, ValueName);

    Status = ZwQueryValueKey(KeyHandle,
                             &ValueNameU,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &ResultLength);
    if (Status != STATUS_BUFFER_OVERFLOW &&
        Status != STATUS_BUFFER_TOO_SMALL)
    {
        return Status;
    }

    ValueInfo = ExAllocatePoolWithTag(PagedPool,
                                      ResultLength,
                                      TAG_DXGK_REGISTRY);
    if (ValueInfo == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryValueKey(KeyHandle,
                             &ValueNameU,
                             KeyValuePartialInformation,
                             ValueInfo,
                             ResultLength,
                             &ResultLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(ValueInfo, TAG_DXGK_REGISTRY);
        return Status;
    }

    *ValueInformation = ValueInfo;
    return STATUS_SUCCESS;
}

static VOID
DxgkpFreeRegistryValue(
    _In_opt_ PKEY_VALUE_PARTIAL_INFORMATION ValueInformation)
{
    if (ValueInformation != NULL)
        ExFreePoolWithTag(ValueInformation, TAG_DXGK_REGISTRY);
}

static NTSTATUS
DxgkpQueryDriverDwordValue(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Value)
{
    HANDLE KeyHandle = NULL;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = NULL;
    NTSTATUS OpenStatus;
    NTSTATUS Status;

    if (Value == NULL)
        return STATUS_INVALID_PARAMETER;

    OpenStatus = DxgkpOpenAdapterDriverKey(Adapter, &KeyHandle);
    if (NT_SUCCESS(OpenStatus))
    {
        Status = DxgkpQueryRegistryValue(KeyHandle, ValueName, &ValueInfo);
        if (NT_SUCCESS(Status))
        {
            if (ValueInfo->Type == REG_DWORD &&
                ValueInfo->DataLength >= sizeof(ULONG))
            {
                *Value = *(const ULONG *)ValueInfo->Data;
            }
            else
            {
                Status = STATUS_OBJECT_TYPE_MISMATCH;
            }
        }

        DxgkpFreeRegistryValue(ValueInfo);
        ZwClose(KeyHandle);
        if (NT_SUCCESS(Status))
            return Status;

        ValueInfo = NULL;
        KeyHandle = NULL;
    }
    else
    {
        Status = OpenStatus;
    }

    OpenStatus = DxgkpOpenMiniportServiceKey(Adapter, &KeyHandle);
    if (!NT_SUCCESS(OpenStatus))
        return Status;

    Status = DxgkpQueryRegistryValue(KeyHandle, ValueName, &ValueInfo);
    if (NT_SUCCESS(Status))
    {
        if (ValueInfo->Type == REG_DWORD &&
            ValueInfo->DataLength >= sizeof(ULONG))
        {
            *Value = *(const ULONG *)ValueInfo->Data;
        }
        else
        {
            Status = STATUS_OBJECT_TYPE_MISMATCH;
        }
    }

    DxgkpFreeRegistryValue(ValueInfo);
    ZwClose(KeyHandle);
    return Status;
}

static NTSTATUS
DxgkpQueryDriverStringValue(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PCWSTR ValueName,
    _In_ ULONG MultiSzIndex,
    _Out_writes_(BufferCount) PWSTR Buffer,
    _In_ ULONG BufferCount)
{
    HANDLE KeyHandle = NULL;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = NULL;
    ULONG ValueChars;
    const WCHAR *Selected;
    ULONG Index;
    NTSTATUS OpenStatus;
    NTSTATUS Status;

    if (Buffer == NULL || BufferCount == 0)
        return STATUS_INVALID_PARAMETER;

    Buffer[0] = UNICODE_NULL;

    OpenStatus = DxgkpOpenAdapterDriverKey(Adapter, &KeyHandle);
    if (NT_SUCCESS(OpenStatus))
    {
        Status = DxgkpQueryRegistryValue(KeyHandle, ValueName, &ValueInfo);
        if (!NT_SUCCESS(Status))
        {
            ZwClose(KeyHandle);
            KeyHandle = NULL;
        }
    }
    else
    {
        Status = OpenStatus;
    }

    if (!NT_SUCCESS(Status))
    {
        OpenStatus = DxgkpOpenMiniportServiceKey(Adapter, &KeyHandle);
        if (!NT_SUCCESS(OpenStatus))
            return Status;

        Status = DxgkpQueryRegistryValue(KeyHandle, ValueName, &ValueInfo);
        if (!NT_SUCCESS(Status))
        {
            ZwClose(KeyHandle);
            return Status;
        }
    }

    if ((ValueInfo->Type != REG_SZ &&
         ValueInfo->Type != REG_MULTI_SZ &&
         ValueInfo->Type != REG_EXPAND_SZ) ||
        ValueInfo->DataLength < sizeof(WCHAR))
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
        goto Cleanup;
    }

    ValueChars = ValueInfo->DataLength / sizeof(WCHAR);
    if (ValueChars == 0)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Cleanup;
    }

    Selected = (const WCHAR *)ValueInfo->Data;
    if (ValueInfo->Type == REG_MULTI_SZ)
    {
        const WCHAR *Walker = Selected;

        for (Index = 0; Index < MultiSzIndex && *Walker != UNICODE_NULL; ++Index)
        {
            while (*Walker != UNICODE_NULL)
                ++Walker;

            ++Walker;
        }

        if (*Walker != UNICODE_NULL)
            Selected = Walker;
    }

    if (*Selected == UNICODE_NULL)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Cleanup;
    }

    for (Index = 0; Index + 1 < BufferCount && Selected[Index] != UNICODE_NULL; ++Index)
        Buffer[Index] = Selected[Index];

    Buffer[Index] = UNICODE_NULL;
    Status = STATUS_SUCCESS;

Cleanup:
    DxgkpFreeRegistryValue(ValueInfo);
    ZwClose(KeyHandle);
    return Status;
}

static D3DKMT_HANDLE
DxgkpCreateAdapterHandle(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    D3DKMT_HANDLE Handle = 0;

    if (Adapter != NULL)
        DxgkCreateAdapterHandle(Adapter, PsGetCurrentProcess(), &Handle);
    return Handle;
}

/* Maximum adapters for on-stack snapshot arrays */
#define DXGKP_MAX_ADAPTERS  16

/*
 * DxgkpSnapshotAdapters
 *
 * Build an on-stack array of all current DXGKRNL_ADAPTER pointers while
 * holding DxgkAdapterGlobalListLock.  Returns the number of adapters found
 * (capped at DXGKP_MAX_ADAPTERS).
 */
static ULONG
DxgkpSnapshotAdapters(
    _Out_writes_(DXGKP_MAX_ADAPTERS) PDXGKRNL_ADAPTER *AdapterArray)
{
    return DxgkReferenceStartedAdapters(AdapterArray, DXGKP_MAX_ADAPTERS);
}

static VOID
DxgkpDereferenceAdapterSnapshot(
    _In_reads_(Count) PDXGKRNL_ADAPTER *Adapters,
    _In_ ULONG Count)
{
    ULONG i;

    for (i = 0; i < Count; ++i)
        DxgkDereferenceAdapter(Adapters[i]);
}

/*
 * DxgkpValidateAdapterHandle
 *
 * Resolve an owner-scoped typed adapter handle and acquire adapter rundown.
 * Returns the referenced DXGKRNL_ADAPTER pointer or NULL on failure.
 */
static PDXGKRNL_ADAPTER
DxgkpValidateAdapterHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    PDXGKRNL_ADAPTER Adapter = NULL;

    if (!NT_SUCCESS(DxgkReferenceAdapterByHandle(Handle, PsGetCurrentProcess(), &Adapter)))
        return NULL;
    return Adapter;
}

static PDXGKRNL_ADAPTER
DxgkpCloseAdapterHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    PDXGKRNL_ADAPTER Adapter;

    if (!NT_SUCCESS(DxgkCloseAdapterHandle(Handle, PsGetCurrentProcess(), &Adapter)))
        return NULL;
    return Adapter;
}

static VOID
DxgkpFreeFileContext(
    _In_ PDXGKRNL_FILE_CONTEXT Context)
{
    if (Context == NULL)
        return;

    Context->Magic = 0;
    ExFreePoolWithTag(Context, TAG_DXGK_ADAPTER);
}

/*
 * DxgkLookupAdapterByHandle
 *
 * Public wrapper around DxgkpValidateAdapterHandle for use by other
 * modules (e.g. vidpn.c DxgkGetDisplayModeList).
 */
PDXGKRNL_ADAPTER
DxgkLookupAdapterByHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    return DxgkpValidateAdapterHandle(Handle);
}

static NTSTATUS
DxgkpValidateAdapterOnlyForIoctl(
    _In_ D3DKMT_HANDLE Handle)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkpValidateAdapterHandle(Handle);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;
    DxgkDereferenceAdapter(Adapter);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpValidateGlobalShareForIoctl(
    _In_ D3DKMT_HANDLE Handle)
{
    PDXGKVMM_RESOURCE Resource;
    NTSTATUS Status;

    Status = DxgkVidMmReferenceResource(Handle, TRUE, NULL, &Resource);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_HANDLE;
    DxgkVidMmDereferenceResource(Resource);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpValidateDeviceHandleForIoctl(
    _In_ D3DKMT_HANDLE hDevice,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_opt_ PDXGKRNL_DEVICE *OutDevice)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (OutAdapter != NULL && OutDevice == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status) || Device == NULL || Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (OutAdapter != NULL)
        *OutAdapter = Adapter;
    if (OutDevice != NULL)
        *OutDevice = Device;
    else
        DxgkDereferenceDevice(Device);

    return STATUS_SUCCESS;
}

static BOOLEAN DxgkpDeviceExecutionActive(_In_opt_ PDXGKRNL_DEVICE Device)
{
    return Device != NULL && InterlockedCompareExchange(&Device->ExecutionState, 0, 0) == D3DKMT_DEVICEEXECUTION_ACTIVE;
}

static NTSTATUS
DxgkpValidateAdapterVidPnSourceForIoctl(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS LookupStatus;

    LookupStatus = DxgkReferenceAdapterByHandle(hAdapter, PsGetCurrentProcess(), &Adapter);
    if (!NT_SUCCESS(LookupStatus))
        return STATUS_INVALID_PARAMETER;

    if (VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
    {
        DxgkDereferenceAdapter(Adapter);
        return STATUS_INVALID_PARAMETER;
    }

    if (OutAdapter != NULL)
        *OutAdapter = Adapter;
    else
        DxgkDereferenceAdapter(Adapter);

    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpValidateAllocationListForIoctl(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_reads_(AllocationCount) CONST D3DKMT_HANDLE *AllocationList,
    _In_ UINT AllocationCount)
{
    UINT i;
    NTSTATUS Status = STATUS_SUCCESS;

    if (AllocationCount == 0)
        return STATUS_SUCCESS;
    if (AllocationCount > DXGKP_MAX_D3DKMT_LIST_COUNT)
        return STATUS_INVALID_PARAMETER;
    if (AllocationList == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        for (i = 0; i < AllocationCount; ++i)
        {
            PDXGKVMM_ALLOCATION Allocation;

            Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)AllocationList[i], Adapter, Device, &Allocation);
            if (!NT_SUCCESS(Status))
            {
                Status = STATUS_INVALID_HANDLE;
                break;
            }
            DxgkVidMmDereferenceAllocation(Allocation);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static NTSTATUS
DxgkpCaptureAllocationReferencesForIoctl(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_(AllocationCount) CONST D3DKMT_HANDLE *AllocationList,
    _In_ UINT AllocationCount,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_result_buffer_(AllocationCount) PDXGKVMM_ALLOCATION **OutAllocations)
{
    D3DKMT_HANDLE *Handles = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    SIZE_T HandlesSize;
    SIZE_T AllocationsSize;
    UINT Index;
    NTSTATUS Status = STATUS_SUCCESS;

    if (OutAllocations == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAllocations = NULL;
    if (Adapter == NULL || Device == NULL || AllocationList == NULL || AllocationCount == 0 || AllocationCount > DXGKP_MAX_D3DKMT_LIST_COUNT)
        return STATUS_INVALID_PARAMETER;
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Handles) || (SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Allocations))
        return STATUS_INTEGER_OVERFLOW;
    HandlesSize = (SIZE_T)AllocationCount * sizeof(*Handles);
    AllocationsSize = (SIZE_T)AllocationCount * sizeof(*Allocations);
    Allocations = ExAllocatePoolWithTag(NonPagedPool, AllocationsSize, TAG_DXGK_CAPTURE);
    if (Allocations == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(Allocations, AllocationsSize);
    Status = DxgkpCaptureUserBuffer(AllocationList, HandlesSize, AccessMode, TAG_DXGK_CAPTURE, (PVOID *)&Handles);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    for (Index = 0; Index < AllocationCount; ++Index)
    {
        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Handles[Index], Adapter, Device, &Allocations[Index]);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
    }
    *OutAllocations = Allocations;
    Allocations = NULL;

Cleanup:
    if (Allocations != NULL)
    {
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            if (Allocations[Index] != NULL)
                DxgkVidMmDereferenceAllocation(Allocations[Index]);
        }
        ExFreePoolWithTag(Allocations, TAG_DXGK_CAPTURE);
    }
    if (Handles != NULL)
        ExFreePoolWithTag(Handles, TAG_DXGK_CAPTURE);
    return Status;
}

static VOID
DxgkpReleaseAllocationReferencesForIoctl(
    _In_reads_(AllocationCount) PDXGKVMM_ALLOCATION *Allocations,
    _In_ UINT AllocationCount)
{
    UINT Index;

    if (Allocations == NULL)
        return;
    for (Index = 0; Index < AllocationCount; ++Index)
        DxgkVidMmDereferenceAllocation(Allocations[Index]);
    ExFreePoolWithTag(Allocations, TAG_DXGK_CAPTURE);
}

static NTSTATUS
DxgkpCapturePrioritiesForIoctl(
    _In_reads_(PriorityCount) CONST UINT *Priorities,
    _In_ UINT PriorityCount,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_result_buffer_(PriorityCount) UINT **OutPriorities)
{
    UINT *CapturedPriorities;
    SIZE_T PrioritiesSize;
    NTSTATUS Status;

    if (OutPriorities == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutPriorities = NULL;
    if (Priorities == NULL || PriorityCount == 0 || PriorityCount > DXGKP_MAX_D3DKMT_LIST_COUNT || (SIZE_T)PriorityCount > MAXULONG_PTR / sizeof(*CapturedPriorities))
        return STATUS_INVALID_PARAMETER;
    PrioritiesSize = (SIZE_T)PriorityCount * sizeof(*CapturedPriorities);
    Status = DxgkpCaptureUserBuffer(Priorities, PrioritiesSize, AccessMode, TAG_DXGK_CAPTURE, (PVOID *)&CapturedPriorities);
    if (!NT_SUCCESS(Status))
        return Status;
    *OutPriorities = CapturedPriorities;
    return STATUS_SUCCESS;
}

/*
 * DxgkpApplyMakeResidentPriorities
 *
 * Applies a MakeResident request's per-allocation priorities.  The whole list
 * is referenced first so an invalid handle fails before any priority changes,
 * then the request-wide setter publishes every value atomically.
 */
static NTSTATUS
DxgkpApplyMakeResidentPriorities(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_(Count) CONST D3DKMT_HANDLE *AllocationList,
    _In_reads_(Count) CONST UINT *Priorities,
    _In_ UINT Count)
{
    PDXGKVMM_ALLOCATION *Allocations;
    UINT Referenced = 0;
    UINT Index;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Count == 0)
        return STATUS_SUCCESS;
    Allocations = ExAllocatePoolWithTag(PagedPool, (SIZE_T)Count * sizeof(*Allocations), TAG_DXGK_GPUVA);
    if (Allocations == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Allocations, (SIZE_T)Count * sizeof(*Allocations));

    for (Index = 0; Index < Count; ++Index)
    {
        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)AllocationList[Index], Adapter, Device, &Allocations[Index]);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
            break;
        }
        Referenced++;
    }

    if (NT_SUCCESS(Status))
        Status = DxgkVidMmSetAllocationPriorities(Allocations, Priorities, Count);

    for (Index = 0; Index < Referenced; ++Index)
        DxgkVidMmDereferenceAllocation(Allocations[Index]);
    ExFreePoolWithTag(Allocations, TAG_DXGK_GPUVA);
    return Status;
}

/* ========================================================================
 * DxgkEnumAdapters — D3DKMTEnumAdapters
 *
 * Returns the list of started GPU adapters.  The caller passes a
 * D3DKMT_ENUMADAPTERS structure; on output NumAdapters is set and the
 * Adapters[] inline array is populated with adapter handles and LUIDs.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkEnumAdapters(
    _Inout_ D3DKMT_ENUMADAPTERS *pEnumAdapters)
{
    PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
    ULONG            Count, i, Filled;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pEnumAdapters == NULL)
        return STATUS_INVALID_PARAMETER;

    Count = DxgkpSnapshotAdapters(Snapshot);
    Filled = 0;

    for (i = 0; i < Count && Filled < MAX_ENUM_ADAPTERS; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];
        if (Adapter->State != DxgkAdapterStateStarted)
            continue;

        pEnumAdapters->Adapters[Filled].hAdapter    = DxgkpCreateAdapterHandle(Adapter);
        if (pEnumAdapters->Adapters[Filled].hAdapter == 0)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        pEnumAdapters->Adapters[Filled].AdapterLuid = Adapter->AdapterLuid;
        pEnumAdapters->Adapters[Filled].NumOfSources = Adapter->NumberOfVideoPresentSources;
        pEnumAdapters->Adapters[Filled].bPrecisePresentRegionsPreferred = FALSE;
        Filled++;
    }
    DxgkpDereferenceAdapterSnapshot(Snapshot, Count);

    if (!NT_SUCCESS(Status))
    {
        while (Filled != 0)
        {
            PDXGKRNL_ADAPTER Adapter;

            --Filled;
            if (NT_SUCCESS(DxgkCloseAdapterHandle(pEnumAdapters->Adapters[Filled].hAdapter, PsGetCurrentProcess(), &Adapter)))
                DxgkDereferenceAdapter(Adapter);
            pEnumAdapters->Adapters[Filled].hAdapter = 0;
        }
        pEnumAdapters->NumAdapters = 0;
        return Status;
    }

    pEnumAdapters->NumAdapters = Filled;

    DXGKRNL_TRACE("DxgkEnumAdapters: returning %lu adapters\n", Filled);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkEnumAdapters2 — D3DKMTEnumAdapters2
 *
 * Similar to EnumAdapters but uses a caller-provided buffer (pAdapters).
 * On input, NumAdapters is the buffer capacity; on output it is the count
 * of adapters written.  If the buffer is too small, returns the required
 * count and STATUS_BUFFER_TOO_SMALL.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */

static NTSTATUS
DxgkpEnumAdapters2(
    _Inout_ D3DKMT_ENUMADAPTERS2 *pEnumAdapters2,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
    D3DKMT_ADAPTERINFO Adapters[DXGKP_MAX_ADAPTERS];
    D3DKMT_ADAPTERINFO *UserAdapters;
    ULONG            Capacity, Count, i, Started;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pEnumAdapters2 == NULL)
        return STATUS_INVALID_PARAMETER;

    Capacity = pEnumAdapters2->NumAdapters;
    UserAdapters = pEnumAdapters2->pAdapters;
    Count = DxgkpSnapshotAdapters(Snapshot);

    /* Count started adapters */
    Started = 0;
    for (i = 0; i < Count; ++i)
    {
        if (Snapshot[i]->State == DxgkAdapterStateStarted)
            Started++;
    }

    /*
     * Count query: when the caller passes a NULL array it only wants the
     * adapter count.  Per the D3DKMT contract this SUCCEEDS with NumAdapters
     * set — it is not an error.  Returning STATUS_BUFFER_TOO_SMALL here would
     * additionally lose the count, because ReactOS skips the METHOD_BUFFERED
     * output copy-back for a non-success status, so the caller would read back
     * its unmodified input (zero).
     */
    if (UserAdapters == NULL)
    {
        pEnumAdapters2->NumAdapters = Started;
        DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
        return STATUS_SUCCESS;
    }

    /* Caller supplied an array but it is too small: report the required count. */
    if (Capacity < Started)
    {
        pEnumAdapters2->NumAdapters = Started;
        DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = DxgkpProbeOutputBuffer(UserAdapters, (SIZE_T)Started * sizeof(*Adapters), EmbeddedBufferMode);
    if (!NT_SUCCESS(Status))
    {
        DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
        return Status;
    }

    /* Build the complete result in kernel memory before publishing handles. */
    RtlZeroMemory(Adapters, sizeof(Adapters));
    Started = 0;
    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];
        if (Adapter->State != DxgkAdapterStateStarted)
            continue;

        Adapters[Started].hAdapter = DxgkpCreateAdapterHandle(Adapter);
        if (Adapters[Started].hAdapter == 0)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        Adapters[Started].AdapterLuid = Adapter->AdapterLuid;
        Adapters[Started].NumOfSources = Adapter->NumberOfVideoPresentSources;
        Adapters[Started].bPrecisePresentRegionsPreferred = FALSE;
        Started++;
    }
    DxgkpDereferenceAdapterSnapshot(Snapshot, Count);

    if (NT_SUCCESS(Status))
        Status = DxgkpCopyToUserBuffer(UserAdapters, Adapters, (SIZE_T)Started * sizeof(*Adapters), EmbeddedBufferMode);

    if (!NT_SUCCESS(Status))
    {
        while (Started != 0)
        {
            PDXGKRNL_ADAPTER Adapter;

            --Started;
            if (NT_SUCCESS(DxgkCloseAdapterHandle(Adapters[Started].hAdapter, PsGetCurrentProcess(), &Adapter)))
                DxgkDereferenceAdapter(Adapter);
            Adapters[Started].hAdapter = 0;
        }
        pEnumAdapters2->NumAdapters = 0;
        return Status;
    }

    pEnumAdapters2->NumAdapters = Started;

    DXGKRNL_TRACE("DxgkEnumAdapters2: returning %lu adapters\n", Started);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkEnumAdapters2(
    _Inout_ D3DKMT_ENUMADAPTERS2 *pEnumAdapters2)
{
    return DxgkpEnumAdapters2(pEnumAdapters2, KernelMode);
}

/* ========================================================================
 * DxgkOpenAdapterFromLuid — D3DKMTOpenAdapterFromLuid
 *
 * Opens an adapter handle given the adapter LUID assigned at StartDevice.
 * Searches the referenced started-adapter snapshot for a matching LUID and
 * returns an owner-scoped typed handle.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkOpenAdapterFromLuid(
    _Inout_ D3DKMT_OPENADAPTERFROMLUID *pOpenAdapterFromLuid)
{
    PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
    ULONG            Count, i;

    PAGED_CODE();

    if (pOpenAdapterFromLuid == NULL)
        return STATUS_INVALID_PARAMETER;

    Count = DxgkpSnapshotAdapters(Snapshot);

    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];
        if (Adapter->State != DxgkAdapterStateStarted)
            continue;

        if (Adapter->AdapterLuid.LowPart  == pOpenAdapterFromLuid->AdapterLuid.LowPart &&
            Adapter->AdapterLuid.HighPart == pOpenAdapterFromLuid->AdapterLuid.HighPart)
        {
            pOpenAdapterFromLuid->hAdapter = DxgkpCreateAdapterHandle(Adapter);
            if (pOpenAdapterFromLuid->hAdapter == 0)
            {
                DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            DXGKRNL_TRACE("DxgkOpenAdapterFromLuid: LUID={%ld,%lu} -> handle=0x%X\n",
                          Adapter->AdapterLuid.HighPart,
                          Adapter->AdapterLuid.LowPart,
                          pOpenAdapterFromLuid->hAdapter);
            DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
            return STATUS_SUCCESS;
        }
    }

    DXGKRNL_WARN("DxgkOpenAdapterFromLuid: LUID={%ld,%lu} not found\n",
                 pOpenAdapterFromLuid->AdapterLuid.HighPart,
                 pOpenAdapterFromLuid->AdapterLuid.LowPart);
    DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
    return STATUS_INVALID_PARAMETER;
}

static NTSTATUS
DxgkpOpenFirstStartedAdapter(
    _Out_ D3DKMT_HANDLE *OutHandle,
    _Out_ LUID *OutLuid)
{
    PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
    ULONG Count;

    *OutHandle = 0;
    RtlZeroMemory(OutLuid, sizeof(*OutLuid));
    Count = DxgkpSnapshotAdapters(Snapshot);
    if (Count == 0)
        return STATUS_NO_SUCH_DEVICE;
    *OutHandle = DxgkpCreateAdapterHandle(Snapshot[0]);
    *OutLuid = Snapshot[0]->AdapterLuid;
    DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
    return *OutHandle != 0 ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

/*
 * DxgkpOpenAdapterByDisplayOrdinal
 *
 * Exact GDI display selection: \\.\DISPLAYn maps to the n-th started
 * display-capable adapter (each adapter exposes one source, so display
 * ordinal == adapter ordinal among display adapters).
 */
static NTSTATUS
DxgkpOpenAdapterByDisplayOrdinal(
    _In_ ULONG DisplayOrdinal,
    _Out_ D3DKMT_HANDLE *OutHandle,
    _Out_ LUID *OutLuid)
{
    PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
    ULONG Count;
    ULONG Seen = 0;
    ULONG i;
    NTSTATUS Status = STATUS_NO_SUCH_DEVICE;

    *OutHandle = 0;
    RtlZeroMemory(OutLuid, sizeof(*OutLuid));
    if (DisplayOrdinal == 0)
        return STATUS_INVALID_PARAMETER;
    Count = DxgkpSnapshotAdapters(Snapshot);
    for (i = 0; i < Count; ++i)
    {
        if (Snapshot[i]->NumberOfVideoPresentSources == 0)
            continue;
        if (++Seen != DisplayOrdinal)
            continue;
        *OutHandle = DxgkpCreateAdapterHandle(Snapshot[i]);
        *OutLuid = Snapshot[i]->AdapterLuid;
        Status = *OutHandle != 0 ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
        break;
    }
    DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
    return Status;
}

/*
 * DxgkpParseGdiDisplayName
 * Parses "\\.\DISPLAYn" (case-insensitive prefix) from an inline WCHAR
 * buffer; returns the 1-based ordinal or 0 on mismatch.
 */
static ULONG
DxgkpParseGdiDisplayName(
    _In_reads_(NameLength) CONST WCHAR *Name,
    _In_ ULONG NameLength)
{
    static CONST WCHAR Prefix[] = L"\\\\.\\DISPLAY";
    CONST ULONG PrefixLength = (ULONG)(sizeof(Prefix) / sizeof(WCHAR)) - 1;
    ULONG Ordinal = 0;
    ULONG i;

    if (NameLength <= PrefixLength)
        return 0;
    for (i = 0; i < PrefixLength; ++i)
    {
        WCHAR C = Name[i];

        if (C >= L'a' && C <= L'z')
            C = (WCHAR)(C - L'a' + L'A');
        if (C != Prefix[i])
            return 0;
    }
    for (i = PrefixLength; i < NameLength && Name[i] != L'\0'; ++i)
    {
        if (Name[i] < L'0' || Name[i] > L'9' || Ordinal > 999)
            return 0;
        Ordinal = Ordinal * 10 + (ULONG)(Name[i] - L'0');
    }
    return Ordinal;
}

static NTSTATUS DxgkpCaptureDeviceName(_In_ PCWSTR Source, _In_ KPROCESSOR_MODE AccessMode, _Out_writes_(DestinationCount) PWCHAR Destination, _In_ ULONG DestinationCount)
{
    ULONG Index;

    if (Source == NULL || Destination == NULL || DestinationCount < 2 || (ULONG_PTR)Source > MAXULONG_PTR - (SIZE_T)DestinationCount * sizeof(WCHAR))
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < DestinationCount; ++Index)
    {
        WCHAR Character = L'\0';
        NTSTATUS Status = STATUS_SUCCESS;

        _SEH2_TRY
        {
            if (AccessMode != KernelMode)
                ProbeForRead(&Source[Index], sizeof(WCHAR), sizeof(WCHAR));
            Character = Source[Index];
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            return Status;
        Destination[Index] = Character;
        if (Character == L'\0')
            return Index == 0 ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    }
    Destination[DestinationCount - 1] = L'\0';
    return STATUS_INVALID_PARAMETER;
}

/*
 * DxgkpOpenAdapterByDeviceObjectName
 *
 * Exact selector matching against names registered by dxgkrnl.  Do not open a
 * caller-controlled object name with kernel credentials: the selector may
 * originate in user mode even though win32k transports it through an internal
 * IOCTL.
 */
static NTSTATUS
DxgkpOpenAdapterByDeviceObjectName(
    _In_ PCWSTR DeviceName,
    _Out_ D3DKMT_HANDLE *OutHandle,
    _Out_ LUID *OutLuid)
{
    PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
    UNICODE_STRING Name;
    ULONG Count;
    ULONG i;
    NTSTATUS Status = STATUS_NO_SUCH_DEVICE;

    *OutHandle = 0;
    RtlZeroMemory(OutLuid, sizeof(*OutLuid));

    RtlInitUnicodeString(&Name, DeviceName);
    if (Name.Length == 0)
        return STATUS_INVALID_PARAMETER;
    Count = DxgkpSnapshotAdapters(Snapshot);
    for (i = 0; i < Count; ++i)
    {
        UNICODE_STRING DisplayName;
        BOOLEAN Match;

        if (Snapshot[i]->State != DxgkAdapterStateStarted)
            continue;
        Match = Snapshot[i]->DeviceInterfaceName.Buffer != NULL && RtlEqualUnicodeString(&Name, &Snapshot[i]->DeviceInterfaceName, TRUE);
        if (!Match && Snapshot[i]->DisplayDeviceName[0] != L'\0')
        {
            RtlInitUnicodeString(&DisplayName, Snapshot[i]->DisplayDeviceName);
            Match = RtlEqualUnicodeString(&Name, &DisplayName, TRUE);
        }
        if (!Match)
            continue;
        *OutHandle = DxgkpCreateAdapterHandle(Snapshot[i]);
        *OutLuid = Snapshot[i]->AdapterLuid;
        Status = *OutHandle != 0 ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
        break;
    }
    DxgkpDereferenceAdapterSnapshot(Snapshot, Count);
    return Status;
}

/* ========================================================================
 * DxgkCloseAdapter — D3DKMTCloseAdapter
 *
 * Closes an adapter handle previously obtained from EnumAdapters or
 * OpenAdapterFromLuid.  Validates the handle against the global adapter
 * namespace. Closing transfers and releases the handle's adapter rundown
 * reference without changing adapter state.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */

NTSTATUS
NTAPI
DxgkCloseAdapter(
    _In_ CONST D3DKMT_CLOSEADAPTER *pCloseAdapter)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();

    if (pCloseAdapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpCloseAdapterHandle(pCloseAdapter->hAdapter);
    if (Adapter == NULL)
    {
        DXGKRNL_WARN("DxgkCloseAdapter: invalid handle 0x%X\n",
                     pCloseAdapter->hAdapter);
        return STATUS_INVALID_PARAMETER;
    }

    DXGKRNL_TRACE("DxgkCloseAdapter: handle=0x%X (Adapter %p)\n", pCloseAdapter->hAdapter, Adapter);
    DxgkDereferenceAdapter(Adapter);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkQueryAdapterInfo — D3DKMTQueryAdapterInfo
 *
 * Returns adapter capabilities and configuration information.  The Type
 * field selects the information class.  This initial implementation
 * handles the most commonly queried types required for adapter discovery:
 *
 *   KMTQAITYPE_GETSEGMENTSIZE   — video memory segment sizes
 *   KMTQAITYPE_DRIVERVERSION    — WDDM driver version
 *   KMTQAITYPE_ADAPTERTYPE      — adapter type flags (Win8+)
 *
 * Other types return STATUS_NOT_SUPPORTED so callers can fall back
 * gracefully.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */

static NTSTATUS
NTAPI
DxgkpQueryAdapterInfoCaptured(
    _Inout_ CONST D3DKMT_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS LookupStatus;

    PAGED_CODE();

    if (pQueryAdapterInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    LookupStatus = DxgkReferenceAdapterByHandle(pQueryAdapterInfo->hAdapter, PsGetCurrentProcess(), &Adapter);
    if (!NT_SUCCESS(LookupStatus))
    {
        DXGKRNL_WARN("DxgkQueryAdapterInfo: invalid handle 0x%X\n", pQueryAdapterInfo->hAdapter);
        return STATUS_INVALID_PARAMETER;
    }

#define DXGKP_QUERY_RETURN(Result) do { NTSTATUS ReturnStatus = (Result); DxgkDereferenceAdapter(Adapter); return ReturnStatus; } while (0)

    DXGKRNL_TRACE("DxgkQueryAdapterInfo: handle=0x%X Type=%d Size=%u\n",
                  pQueryAdapterInfo->hAdapter,
                  pQueryAdapterInfo->Type,
                  pQueryAdapterInfo->PrivateDriverDataSize);

    switch ((UINT)pQueryAdapterInfo->Type)
    {
        case KMTQAITYPE_GETSEGMENTSIZE:
        {
            /*
             * Return video memory segment sizes.  For a display-only driver
             * with no dedicated VRAM, report a reasonable shared system memory
             * size so applications know the adapter exists.
             */
            D3DKMT_SEGMENTSIZEINFO *pSegInfo;
            D3DKMT_SEGMENTSIZEINFO SegInfo;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_SEGMENTSIZEINFO))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }

            pSegInfo = (D3DKMT_SEGMENTSIZEINFO *)pQueryAdapterInfo->pPrivateDriverData;

            /*
             * Report the adapter's real segment topology so this query, the
             * segment-group query, and the memory budgets all agree.  An
             * adapter with no segments (display-only) truthfully reports a
             * shared-only view of zero.
             */
            {
                D3DKMT_SEGMENTGROUPSIZEINFO GroupInfo;
                NTSTATUS SegStatus;

                RtlZeroMemory(&GroupInfo, sizeof(GroupInfo));
                SegStatus = DxgkVidMmQuerySegmentSizes(Adapter, &GroupInfo);
                if (!NT_SUCCESS(SegStatus))
                    DXGKP_QUERY_RETURN(SegStatus);
                SegInfo = GroupInfo.LegacyInfo;
            }
            _SEH2_TRY
            {
                *pSegInfo = SegInfo;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: GETSEGMENTSIZE -> Shared=%llu\n", SegInfo.SharedSystemMemorySize);
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_DRIVERVERSION:
        {
            /*
             * Return the WDDM driver version that matches the dxgkrnl ABI
             * level we compiled against.
             */
            D3DKMT_DRIVERVERSION *pVersion;
            D3DKMT_DRIVERVERSION Version;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_DRIVERVERSION))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }

            pVersion = (D3DKMT_DRIVERVERSION *)pQueryAdapterInfo->pPrivateDriverData;
            Version = DxgkpGetReportedDriverVersion(Adapter);
            _SEH2_TRY
            {
                *pVersion = Version;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: DRIVERVERSION -> %d\n", Version);
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_UMDRIVERNAME:
        {
            D3DKMT_UMDFILENAMEINFO *pDriverName;
            D3DKMT_UMDFILENAMEINFO DriverName;
            NTSTATUS Status;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_UMDFILENAMEINFO))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }

            pDriverName = (D3DKMT_UMDFILENAMEINFO *)pQueryAdapterInfo->pPrivateDriverData;
            _SEH2_TRY
            {
                DriverName = *pDriverName;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;

            if ((UINT)DriverName.Version >= NUM_KMTUMDVERSIONS)
                DXGKP_QUERY_RETURN(STATUS_INVALID_PARAMETER);

            Status = DxgkpQueryDriverStringValue(Adapter,
                                                 L"UserModeDriverName",
                                                (ULONG)DriverName.Version,
                                                DriverName.UmdFileName,
                                                ARRAYSIZE(DriverName.UmdFileName));
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkQueryAdapterInfo: UMDRIVERNAME query failed 0x%08lx\n",
                             Status);
                DXGKP_QUERY_RETURN(Status);
            }

            _SEH2_TRY
            {
                *pDriverName = DriverName;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: UMDRIVERNAME[%u] -> %ws\n",
                          DriverName.Version,
                          DriverName.UmdFileName);
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_UMOPENGLINFO:
        {
            D3DKMT_OPENGLINFO *pOpenGlInfo;
            D3DKMT_OPENGLINFO OpenGlInfo;
            NTSTATUS Status;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_OPENGLINFO))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }

            pOpenGlInfo = (D3DKMT_OPENGLINFO *)pQueryAdapterInfo->pPrivateDriverData;
            RtlZeroMemory(&OpenGlInfo, sizeof(OpenGlInfo));

            Status = DxgkpQueryDriverStringValue(Adapter,
                                                 L"OpenGLDriverName",
                                                 0,
                                                OpenGlInfo.UmdOpenGlIcdFileName,
                                                ARRAYSIZE(OpenGlInfo.UmdOpenGlIcdFileName));
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkQueryAdapterInfo: UMOPENGLINFO driver query failed 0x%08lx\n",
                             Status);
                DXGKP_QUERY_RETURN(Status);
            }

            Status = DxgkpQueryDriverDwordValue(Adapter,
                                                L"OpenGLVersion",
                                               &OpenGlInfo.Version);
            if (!NT_SUCCESS(Status))
                OpenGlInfo.Version = 0;

            Status = DxgkpQueryDriverDwordValue(Adapter,
                                                L"OpenGLFlags",
                                               &OpenGlInfo.Flags);
            if (!NT_SUCCESS(Status))
                OpenGlInfo.Flags = 0;

            _SEH2_TRY
            {
                *pOpenGlInfo = OpenGlInfo;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: UMOPENGLINFO -> %ws Version=%lu Flags=0x%lx\n",
                          OpenGlInfo.UmdOpenGlIcdFileName,
                          OpenGlInfo.Version,
                          OpenGlInfo.Flags);
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case 15: /* KMTQAITYPE_ADAPTERTYPE (Win8+, not in Vista-level enum) */
        {
            /*
             * Report only paths backed by this adapter's topology and
             * mandatory render callback tuple.  The software fallback is a
             * display adapter, not a hardware renderer.
             */
            D3DKMT_ADAPTERTYPE *pAdapterType;
            D3DKMT_ADAPTERTYPE AdapterType;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_ADAPTERTYPE))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }

            pAdapterType = (D3DKMT_ADAPTERTYPE *)pQueryAdapterInfo->pPrivateDriverData;
            RtlZeroMemory(&AdapterType, sizeof(AdapterType));
            AdapterType.RenderSupported = DxgkpAdapterSupportsRender(Adapter);
            AdapterType.DisplaySupported = Adapter->NumberOfVideoPresentSources != 0;
            AdapterType.SoftwareDevice = Adapter->MiniportContext != NULL && Adapter->MiniportContext->IsBasicDisplayFallback;
            _SEH2_TRY
            {
                *pAdapterType = AdapterType;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: ADAPTERTYPE -> 0x%X\n",
                          AdapterType.Value);
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_UMDRIVERPRIVATE:
        {
            DXGKP_QUERY_RETURN(STATUS_NOT_SUPPORTED);
        }

        /*
         * Per-level capability words.  Every bit is derived from a complete,
         * reachable execution path; scaffolding stays zero.  GpuMmuSupported
         * reflects a real CPU_VIRTUAL software page-table path; no adapter
         * in this tree has an IoMmu path, hardware scheduling, hardware
         * flip queues, self-refresh memory, or cross-adapter scan-out yet.
         */
        case KMTQAITYPE_WDDM_2_0_CAPS:
        {
            D3DKMT_WDDM_2_0_CAPS Caps;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Caps))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            RtlZeroMemory(&Caps, sizeof(Caps));
            Caps.GpuMmuSupported = Adapter->GpuMmuCapsValid ? 1 : 0;
            _SEH2_TRY
            {
                *(D3DKMT_WDDM_2_0_CAPS *)pQueryAdapterInfo->pPrivateDriverData = Caps;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_QUERY_GPUMMU_CAPS:
        {
            D3DKMT_QUERY_GPUMMU_CAPS Query;

            if (DxgkpGetReportedDriverVersion(Adapter) < KMT_DRIVERVERSION_WDDM_2_0)
                DXGKP_QUERY_RETURN(STATUS_INVALID_PARAMETER);
            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Query))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            _SEH2_TRY
            {
                Query = *(D3DKMT_QUERY_GPUMMU_CAPS *)pQueryAdapterInfo->pPrivateDriverData;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            if (Query.PhysicalAdapterIndex != 0)
                DXGKP_QUERY_RETURN(STATUS_INVALID_PARAMETER);
            if (!Adapter->GpuMmuCapsValid)
                DXGKP_QUERY_RETURN(STATUS_NOT_SUPPORTED);
            RtlZeroMemory(&Query.Caps, sizeof(Query.Caps));
            Query.Caps.Flags.ReadOnlyMemorySupported = Adapter->GpuMmuCaps.ReadOnlyMemorySupported;
            Query.Caps.Flags.NoExecuteMemorySupported = Adapter->GpuMmuCaps.NoExecuteMemorySupported;
            Query.Caps.Flags.CacheCoherentMemorySupported = Adapter->GpuMmuCaps.CacheCoherentMemorySupported;
            Query.Caps.VirtualAddressBitCount = Adapter->GpuMmuCaps.VirtualAddressBitCount;
            _SEH2_TRY
            {
                *(D3DKMT_QUERY_GPUMMU_CAPS *)pQueryAdapterInfo->pPrivateDriverData = Query;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_WDDM_2_7_CAPS:
        {
            D3DKMT_WDDM_2_7_CAPS Caps;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Caps))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            RtlZeroMemory(&Caps, sizeof(Caps));
            _SEH2_TRY
            {
                *(D3DKMT_WDDM_2_7_CAPS *)pQueryAdapterInfo->pPrivateDriverData = Caps;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_WDDM_2_9_CAPS:
        {
            D3DKMT_WDDM_2_9_CAPS Caps;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Caps))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            RtlZeroMemory(&Caps, sizeof(Caps));
            Caps.HwSchSupportState = DXGK_FEATURE_SUPPORT_ALWAYS_OFF;
            _SEH2_TRY
            {
                *(D3DKMT_WDDM_2_9_CAPS *)pQueryAdapterInfo->pPrivateDriverData = Caps;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_WDDM_3_0_CAPS:
        {
            D3DKMT_WDDM_3_0_CAPS Caps;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Caps))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            RtlZeroMemory(&Caps, sizeof(Caps));
            Caps.HwFlipQueueSupportState = DXGK_FEATURE_SUPPORT_ALWAYS_OFF;
            _SEH2_TRY
            {
                *(D3DKMT_WDDM_3_0_CAPS *)pQueryAdapterInfo->pPrivateDriverData = Caps;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_WDDM_3_1_CAPS:
        {
            D3DKMT_WDDM_3_1_CAPS Caps;

            if (DxgkpGetReportedDriverVersion(Adapter) < KMT_DRIVERVERSION_WDDM_3_1)
                DXGKP_QUERY_RETURN(STATUS_INVALID_PARAMETER);
            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Caps))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            /* Native fences require hardware scheduling: truthfully absent. */
            RtlZeroMemory(&Caps, sizeof(Caps));
            _SEH2_TRY
            {
                *(D3DKMT_WDDM_3_1_CAPS *)pQueryAdapterInfo->pPrivateDriverData = Caps;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_CROSSADAPTERRESOURCE_SUPPORT:
        {
            D3DKMT_CROSSADAPTERRESOURCE_SUPPORT Support;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Support))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            RtlZeroMemory(&Support, sizeof(Support));
            Support.SupportTier = D3DKMT_CROSSADAPTERRESOURCE_SUPPORT_TIER_NONE;
            _SEH2_TRY
            {
                *(D3DKMT_CROSSADAPTERRESOURCE_SUPPORT *)pQueryAdapterInfo->pPrivateDriverData = Support;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_PHYSICALADAPTERCOUNT:
        {
            D3DKMT_PHYSICAL_ADAPTER_COUNT Count;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Count))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            RtlZeroMemory(&Count, sizeof(Count));
            Count.Count = 1;
            _SEH2_TRY
            {
                *(D3DKMT_PHYSICAL_ADAPTER_COUNT *)pQueryAdapterInfo->pPrivateDriverData = Count;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_GETSEGMENTGROUPSIZE:
        {
            D3DKMT_SEGMENTGROUPSIZEINFO GroupInfo;
            NTSTATUS Status;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(GroupInfo))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            RtlZeroMemory(&GroupInfo, sizeof(GroupInfo));
            _SEH2_TRY
            {
                GroupInfo.PhysicalAdapterIndex = ((D3DKMT_SEGMENTGROUPSIZEINFO *)pQueryAdapterInfo->pPrivateDriverData)->PhysicalAdapterIndex;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            Status = DxgkVidMmQuerySegmentSizes(Adapter, &GroupInfo);
            if (!NT_SUCCESS(Status))
                DXGKP_QUERY_RETURN(Status);
            _SEH2_TRY
            {
                *(D3DKMT_SEGMENTGROUPSIZEINFO *)pQueryAdapterInfo->pPrivateDriverData = GroupInfo;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        case KMTQAITYPE_NODEMETADATA:
        {
            D3DKMT_NODEMETADATA Metadata;
            PDXGKDDI_GET_NODE_METADATA GetNodeMetadata;
            NTSTATUS Status;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(Metadata))
            {
                DXGKP_QUERY_RETURN(STATUS_BUFFER_TOO_SMALL);
            }
            _SEH2_TRY
            {
                Metadata = *(D3DKMT_NODEMETADATA *)pQueryAdapterInfo->pPrivateDriverData;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            if ((Metadata.NodeOrdinalAndAdapterIndex & 0xFFFF) >= Adapter->NodeCount ||
                (Metadata.NodeOrdinalAndAdapterIndex >> 16) != 0)
            {
                DXGKP_QUERY_RETURN(STATUS_INVALID_PARAMETER);
            }
            GetNodeMetadata = DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata);
            if (GetNodeMetadata == NULL)
                DXGKP_QUERY_RETURN(STATUS_NOT_SUPPORTED);
            if (!DxgkAcquireKmdCall(Adapter))
                DXGKP_QUERY_RETURN(STATUS_DEVICE_NOT_READY);
            RtlZeroMemory(&Metadata.NodeData, sizeof(Metadata.NodeData));
            _SEH2_TRY
            {
                Status = GetNodeMetadata(Adapter->MiniportDeviceContext,
                                         Metadata.NodeOrdinalAndAdapterIndex & 0xFFFF,
                                         &Metadata.NodeData);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            DxgkReleaseKmdCall(Adapter);
            if (!NT_SUCCESS(Status))
                DXGKP_QUERY_RETURN(Status);
            _SEH2_TRY
            {
                *(D3DKMT_NODEMETADATA *)pQueryAdapterInfo->pPrivateDriverData = Metadata;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                DXGKP_QUERY_RETURN(_SEH2_GetExceptionCode());
            }
            _SEH2_END;
            DXGKP_QUERY_RETURN(STATUS_SUCCESS);
        }

        default:
            DXGKRNL_WARN("DxgkQueryAdapterInfo: unsupported Type=%d\n",
                         pQueryAdapterInfo->Type);
            DXGKP_QUERY_RETURN(STATUS_NOT_SUPPORTED);
    }
#undef DXGKP_QUERY_RETURN
}

static NTSTATUS
DxgkpQueryAdapterInfoWithAccessMode(
    _Inout_ CONST D3DKMT_QUERYADAPTERINFO *pQueryAdapterInfo,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    D3DKMT_QUERYADAPTERINFO CapturedQuery;
    PVOID CapturedPrivateData = NULL;
    PVOID UserPrivateData;
    NTSTATUS Status;

    PAGED_CODE();

    if (pQueryAdapterInfo == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pQueryAdapterInfo->PrivateDriverDataSize > DXGKP_MAX_USER_PRIVATE_DATA)
        return STATUS_INVALID_BUFFER_SIZE;

    CapturedQuery = *pQueryAdapterInfo;
    UserPrivateData = pQueryAdapterInfo->pPrivateDriverData;
    if (CapturedQuery.PrivateDriverDataSize != 0 && UserPrivateData != NULL)
    {
        Status = DxgkpCaptureUserBuffer(UserPrivateData, CapturedQuery.PrivateDriverDataSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, &CapturedPrivateData);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkpProbeOutputBuffer(UserPrivateData, CapturedQuery.PrivateDriverDataSize, EmbeddedBufferMode);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
            return Status;
        }
        CapturedQuery.pPrivateDriverData = CapturedPrivateData;
    }

    Status = DxgkpQueryAdapterInfoCaptured(&CapturedQuery);
    if (NT_SUCCESS(Status) && CapturedPrivateData != NULL)
        Status = DxgkpCopyToUserBuffer(UserPrivateData, CapturedPrivateData, CapturedQuery.PrivateDriverDataSize, EmbeddedBufferMode);

    if (CapturedPrivateData != NULL)
        ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
    return Status;
}

NTSTATUS
NTAPI
DxgkQueryAdapterInfo(
    _Inout_ CONST D3DKMT_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    return DxgkpQueryAdapterInfoWithAccessMode(pQueryAdapterInfo, KernelMode);
}

/* ========================================================================
 * DxgkDispatchCreate
 *
 * IRP_MJ_CREATE handler.  Accept all opens of the control device.
 * Also handles opens of the \Device\Video0 display device.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PIO_STACK_LOCATION Stack;
    PDXGKRNL_FILE_CONTEXT Context;

    /* Route \Device\Video0 opens to the display handler */
    if (DxgkDisplayDispatchCreate(DeviceObject, Irp))
        return STATUS_SUCCESS;

    DXGKRNL_TRACE("DxgkDispatchCreate\n");

    Stack = IoGetCurrentIrpStackLocation(Irp);
    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), TAG_DXGK_ADAPTER);
    if (Context == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Magic = DXGKP_FILE_CONTEXT_MAGIC;

    if (Stack != NULL && Stack->FileObject != NULL)
        Stack->FileObject->FsContext = Context;
    else
        DxgkpFreeFileContext(Context);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkDispatchClose
 *
 * IRP_MJ_CLOSE handler.
 * Also handles closes of the \Device\Video0 display device.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PIO_STACK_LOCATION Stack;
    PDXGKRNL_FILE_CONTEXT Context = NULL;

    /* Route \Device\Video0 closes to the display handler */
    if (DxgkDisplayDispatchClose(DeviceObject, Irp))
        return STATUS_SUCCESS;

    DXGKRNL_TRACE("DxgkDispatchClose\n");

    Stack = IoGetCurrentIrpStackLocation(Irp);
    if (Stack != NULL && Stack->FileObject != NULL)
    {
        Context = (PDXGKRNL_FILE_CONTEXT)Stack->FileObject->FsContext;
        Stack->FileObject->FsContext = NULL;
    }

    if (Context != NULL && Context->Magic == DXGKP_FILE_CONTEXT_MAGIC)
        DxgkpFreeFileContext(Context);

    Irp->IoStatus.Status      = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

typedef struct _DXGK_VIRTGPU_ESCAPE_PACKET_HEADER
{
    USHORT PacketType;
    USHORT PayloadBytes;
} DXGK_VIRTGPU_ESCAPE_PACKET_HEADER, *PDXGK_VIRTGPU_ESCAPE_PACKET_HEADER;

typedef struct _DXGK_VIRTGPU_COMMAND_PACKET_HEADER
{
    UINT CommandType;
    UINT PayloadBytes;
} DXGK_VIRTGPU_COMMAND_PACKET_HEADER, *PDXGK_VIRTGPU_COMMAND_PACKET_HEADER;

typedef struct _DXGK_VIRTGPU_RESOURCE_LIST_HEADER
{
    ULONG Magic;
    UINT ResourceCount;
} DXGK_VIRTGPU_RESOURCE_LIST_HEADER, *PDXGK_VIRTGPU_RESOURCE_LIST_HEADER;

#include <pshpack1.h>
typedef struct _DXGK_VIRTGPU_SIGNAL_BLOCK
{
    D3DKMT_HANDLE hSyncObject;
    ULONG64 FenceValue;
} DXGK_VIRTGPU_SIGNAL_BLOCK, *PDXGK_VIRTGPU_SIGNAL_BLOCK;
#include <poppack.h>

#define DXGK_VIRTGPU_RESOURCE_LIST_MAGIC 0x5652474cUL

static BOOLEAN
DxgkpIsVirtGpuCommandEscape(
    _In_ CONST D3DKMT_ESCAPE *pEscape,
    _Outptr_result_bytebuffer_(*CommandBytes) CONST VOID **CommandBuffer,
    _Out_ UINT *CommandBytes,
    _Outptr_result_buffer_maybenull_(*ResourceHandleCount) CONST D3DKMT_HANDLE **ResourceHandles,
    _Out_ UINT *ResourceHandleCount,
    _Out_opt_ D3DKMT_HANDLE *SignalSyncObject,
    _Out_opt_ ULONG64 *SignalFenceValue)
{
    const DXGK_VIRTGPU_ESCAPE_PACKET_HEADER *PacketHeader;
    const DXGK_VIRTGPU_RESOURCE_LIST_HEADER *ResourceHeader;
    const DXGK_VIRTGPU_COMMAND_PACKET_HEADER *CommandHeader;
    const UCHAR *Cursor;
    SIZE_T MetadataBytes;
    SIZE_T HeaderBytes;
    SIZE_T TotalBytes;

    if (CommandBuffer == NULL ||
        CommandBytes == NULL ||
        ResourceHandles == NULL ||
        ResourceHandleCount == NULL ||
        pEscape == NULL)
    {
        return FALSE;
    }

    *CommandBuffer = NULL;
    *CommandBytes = 0;
    *ResourceHandles = NULL;
    *ResourceHandleCount = 0;
    if (SignalSyncObject != NULL)
        *SignalSyncObject = 0;
    if (SignalFenceValue != NULL)
        *SignalFenceValue = 0;

    HeaderBytes = sizeof(*PacketHeader) + sizeof(*CommandHeader);
    if (pEscape->pPrivateDriverData == NULL ||
        pEscape->PrivateDriverDataSize < HeaderBytes)
    {
        return FALSE;
    }

    PacketHeader = (const DXGK_VIRTGPU_ESCAPE_PACKET_HEADER *)pEscape->pPrivateDriverData;
    if (PacketHeader->PacketType < 1 || PacketHeader->PacketType > 3)
        return FALSE;

    if ((SIZE_T)PacketHeader->PayloadBytes + sizeof(*PacketHeader) !=
        pEscape->PrivateDriverDataSize)
    {
        return FALSE;
    }

    Cursor = (const UCHAR *)pEscape->pPrivateDriverData + sizeof(*PacketHeader);
    MetadataBytes = 0;

    if (PacketHeader->PayloadBytes >= sizeof(*ResourceHeader) + sizeof(*CommandHeader))
    {
        ResourceHeader = (const DXGK_VIRTGPU_RESOURCE_LIST_HEADER *)Cursor;
        if (ResourceHeader->Magic == DXGK_VIRTGPU_RESOURCE_LIST_MAGIC)
        {
            SIZE_T HandleBytes = (SIZE_T)ResourceHeader->ResourceCount *
                                 sizeof(D3DKMT_HANDLE);

            if (PacketHeader->PayloadBytes <
                sizeof(*ResourceHeader) + HandleBytes + sizeof(*CommandHeader))
            {
                return FALSE;
            }

            *ResourceHandles = (const D3DKMT_HANDLE *)(Cursor + sizeof(*ResourceHeader));
            *ResourceHandleCount = ResourceHeader->ResourceCount;
            MetadataBytes = sizeof(*ResourceHeader) + HandleBytes;
            Cursor += MetadataBytes;
        }
    }

    CommandHeader = (const DXGK_VIRTGPU_COMMAND_PACKET_HEADER *)Cursor;
    if (CommandHeader->CommandType != PacketHeader->PacketType)
        return FALSE;

    TotalBytes = sizeof(*PacketHeader) +
                 MetadataBytes +
                 sizeof(*CommandHeader) +
                 (SIZE_T)CommandHeader->PayloadBytes;
    if (TotalBytes != pEscape->PrivateDriverDataSize)
        return FALSE;

    /*
     * Hand the miniport the command PAYLOAD only — the packet/command
     * headers are transport framing, and the miniport's Render parses
     * its own driver-private stream from offset 0.  (No consumer relied
     * on header-included framing: DOD drivers never reach this path.)
     *
     * PacketType 2 = submit-with-signal: the payload begins with a
     * DXGK_VIRTGPU_SIGNAL_BLOCK naming a sync object to signal (with the
     * given monitored-fence value) when this submission's GPU fence
     * retires.
     */
    if (CommandHeader->PayloadBytes == 0)
        return FALSE;

    if (PacketHeader->PacketType == 2)
    {
        const DXGK_VIRTGPU_SIGNAL_BLOCK *Signal;

        if (CommandHeader->PayloadBytes < sizeof(*Signal) + 1)
            return FALSE;

        Signal = (const DXGK_VIRTGPU_SIGNAL_BLOCK *)(CommandHeader + 1);
        if (SignalSyncObject != NULL)
            *SignalSyncObject = Signal->hSyncObject;
        if (SignalFenceValue != NULL)
            *SignalFenceValue = Signal->FenceValue;

        *CommandBuffer = Signal + 1;
        *CommandBytes = CommandHeader->PayloadBytes - sizeof(*Signal);
        return TRUE;
    }

    *CommandBuffer = CommandHeader + 1;
    *CommandBytes = CommandHeader->PayloadBytes;
    return TRUE;
}

static NTSTATUS
DxgkpReferenceRenderAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE AllocationHandle,
    _Out_ PDXGKVMM_ALLOCATION *AllocationReference,
    _Out_ PDXGKVMM_ALLOCATION *OpenBindingReference,
    _Out_ PDXGK_ALLOCATIONLIST AllocationListEntry)
{
    NTSTATUS Status;

    if (Adapter == NULL || Device == NULL || AllocationHandle == 0 || AllocationReference == NULL || OpenBindingReference == NULL || AllocationListEntry == NULL)
        return STATUS_INVALID_PARAMETER;
    *AllocationReference = NULL;
    *OpenBindingReference = NULL;
    RtlZeroMemory(AllocationListEntry, sizeof(*AllocationListEntry));
    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)AllocationHandle, Adapter, Device, AllocationReference);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkVidMmAcquireSubmissionResidencyPin(*AllocationReference, Adapter, AllocationListEntry);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = DxgkVidMmReferenceOpenBinding((HANDLE)(ULONG_PTR)AllocationHandle, Adapter, Device, &AllocationListEntry->hDeviceSpecificAllocation, OpenBindingReference);
    if (!NT_SUCCESS(Status))
    {
        DxgkVidMmReleaseSubmissionResidencyPin(*AllocationReference);
        goto Cleanup;
    }
    return STATUS_SUCCESS;

Cleanup:
    DxgkVidMmDereferenceAllocation(*AllocationReference);
    *AllocationReference = NULL;
    RtlZeroMemory(AllocationListEntry, sizeof(*AllocationListEntry));
    return Status;
}

static NTSTATUS
DxgkpSubmitVirtGpuCommandEscape(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_bytes_(CommandBytes) CONST VOID *CommandBuffer,
    _In_ UINT CommandBytes,
    _In_reads_opt_(ResourceHandleCount) CONST D3DKMT_HANDLE *ResourceHandles,
    _In_ UINT ResourceHandleCount,
    _In_ D3DKMT_HANDLE SignalSyncObject,
    _In_ ULONG64 SignalFenceValue)
{
    DXGKARG_RENDER RenderArgs;
    DXGKARG_SUBMITCOMMAND SubmitArgs;
    DXGK_ALLOCATIONLIST *AllocationList = NULL;
    PDXGKVMM_ALLOCATION *OpenBindingReferenceList = NULL;
    PDXGKVMM_ALLOCATION *AllocationReferenceList = NULL;
    PDXGKRNL_DMA_BUFFER DmaBuffer = NULL;
    PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = NULL;
    DXGKRNL_TRACK_DMA_ARGS TrackArgs;
    PVOID DmaBufferPrivateData = NULL;
    ULONG SubmissionFenceId;
    ULONG VidSchFence = 0;
    UINT DmaBytesUsed = 0;
    D3DDDI_PATCHLOCATIONLIST EscapePatchList[VIDSCH_INLINE_PATCHES];
    UINT EscapePatchCount = 0;
    UINT OpenBindingReferenceCount = 0;
    UINT AllocationReferenceCount = 0;
    UINT i;
    NTSTATUS Status;
    BOOLEAN KmdTransaction = FALSE;

    if (Adapter == NULL || Device == NULL || CommandBuffer == NULL || CommandBytes == 0)
        return STATUS_INVALID_PARAMETER;
    if ((SIZE_T)ResourceHandleCount > MAXULONG_PTR / sizeof(*AllocationList) || (SIZE_T)ResourceHandleCount > MAXULONG_PTR / sizeof(*OpenBindingReferenceList) || (SIZE_T)ResourceHandleCount > MAXULONG_PTR / sizeof(*AllocationReferenceList))
        return STATUS_INVALID_PARAMETER;

    if (DXGK_CB_FULL(Adapter, DxgkDdiRender) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Adapter->SchedulingCaps.MultiEngineAware)
        return STATUS_NOT_SUPPORTED;

    Status = DxgkAllocateDmaBuffer(Adapter, CommandBytes, &DmaBuffer);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    KmdTransaction = TRUE;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    if (ResourceHandleCount != 0)
    {
        AllocationList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)ResourceHandleCount * sizeof(*AllocationList), TAG_DXGK_SUBMITDMA);
        OpenBindingReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)ResourceHandleCount * sizeof(*OpenBindingReferenceList), TAG_DXGK_SUBMITDMA);
        AllocationReferenceList = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)ResourceHandleCount * sizeof(*AllocationReferenceList), TAG_DXGK_SUBMITDMA);
        if (AllocationList == NULL || OpenBindingReferenceList == NULL || AllocationReferenceList == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlZeroMemory(AllocationList, (SIZE_T)ResourceHandleCount * sizeof(*AllocationList));
        RtlZeroMemory(OpenBindingReferenceList, (SIZE_T)ResourceHandleCount * sizeof(*OpenBindingReferenceList));
        RtlZeroMemory(AllocationReferenceList, (SIZE_T)ResourceHandleCount * sizeof(*AllocationReferenceList));

        for (i = 0; i < ResourceHandleCount; ++i)
        {
            Status = DxgkpReferenceRenderAllocation(Adapter, Device, ResourceHandles[i], &AllocationReferenceList[i], &OpenBindingReferenceList[i], &AllocationList[i]);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
            AllocationReferenceCount++;
            OpenBindingReferenceCount++;
        }
    }

    RtlZeroMemory(&RenderArgs, sizeof(RenderArgs));
    RenderArgs.pCommand = CommandBuffer;
    RenderArgs.CommandLength = CommandBytes;
    RenderArgs.pDmaBuffer = DmaBuffer->VirtualAddress;
    RenderArgs.DmaSize = CommandBytes;
    RenderArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
    RenderArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
    RenderArgs.pAllocationList = AllocationList;
    RenderArgs.AllocationListSize = ResourceHandleCount;
    RenderArgs.pPatchLocationListOut = EscapePatchList;
    RenderArgs.PatchLocationListOutSize = RTL_NUMBER_OF(EscapePatchList);
    RenderArgs.DmaBufferSegmentId = DmaBuffer->SegmentId;
    RenderArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    Status = DXGK_CB_FULL(Adapter, DxgkDdiRender)(Device->hMiniportDevice, &RenderArgs);
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (RenderArgs.pPatchLocationListOut != NULL &&
        RenderArgs.pPatchLocationListOut >= EscapePatchList &&
        (SIZE_T)(RenderArgs.pPatchLocationListOut - EscapePatchList) <=
            RTL_NUMBER_OF(EscapePatchList))
    {
        EscapePatchCount = (UINT)(RenderArgs.pPatchLocationListOut -
                                  EscapePatchList);
    }

    if (RenderArgs.pDmaBuffer != NULL && (PUCHAR)RenderArgs.pDmaBuffer >= (PUCHAR)DmaBuffer->VirtualAddress && (PUCHAR)RenderArgs.pDmaBuffer <= (PUCHAR)DmaBuffer->VirtualAddress + DmaBuffer->Capacity)
    {
        DmaBytesUsed = (UINT)((PUCHAR)RenderArgs.pDmaBuffer - (PUCHAR)DmaBuffer->VirtualAddress);
    }
    if (DmaBytesUsed == 0)
        DmaBytesUsed = CommandBytes;
    if (DmaBytesUsed > DmaBuffer->Capacity)
    {
        Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        goto Cleanup;
    }
    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = DmaBytesUsed;

    RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
    TrackArgs.hSignalSyncObject = SignalSyncObject;
    TrackArgs.SignalFenceValue = SignalFenceValue;
    TrackArgs.Device = Device;
    TrackArgs.EnforceSubmissionQuota = TRUE;
    TrackArgs.OpenBindingReferences = OpenBindingReferenceList;
    TrackArgs.OpenBindingReferenceCount = OpenBindingReferenceCount;
    TrackArgs.AllocationReferences = AllocationReferenceList;
    TrackArgs.AllocationReferenceCount = AllocationReferenceCount;

    Status = VidSchSubmitCommandTracked(Adapter, 0, 0, DmaBuffer, &DmaBufferPrivateData, sizeof(DmaBufferPrivateData), AllocationList, ResourceHandleCount, EscapePatchList, EscapePatchCount, Device->hMiniportDevice, NULL, 0, &TrackArgs, 0, 0, &VidSchFence);
    if (NT_SUCCESS(Status))
    {
        DmaBuffer = NULL;
        goto Cleanup;
    }
    if (!(Status == STATUS_DEVICE_NOT_READY && Adapter->VidSchContext == NULL) && !(Status == STATUS_NOT_SUPPORTED && (ResourceHandleCount > VIDSCH_INLINE_ALLOCATIONS || EscapePatchCount > VIDSCH_INLINE_PATCHES)))
        goto Cleanup;

    SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    if (SubmissionFenceId == 0)
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Cleanup;
    }

    TrackArgs.SubmissionFenceId = SubmissionFenceId;
    TrackArgs.NodeOrdinal = 0;
    TrackArgs.DmaBuffer = DmaBuffer;
    Status = DxgkPrepareTrackedDmaBuffer(Adapter, &TrackArgs, &Reservation);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Documented WDDM order: Render -> Patch -> SubmitCommand. */
    if (DXGK_CB_FULL(Adapter, DxgkDdiPatch) != NULL)
    {
        DXGKARG_PATCH PatchArgs;
        NTSTATUS PatchStatus;

        RtlZeroMemory(&PatchArgs, sizeof(PatchArgs));
        PatchArgs.hDevice = Device->hMiniportDevice;
        PatchArgs.DmaBufferSegmentId = DmaBuffer->SegmentId;
        PatchArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;
        PatchArgs.pDmaBuffer = DmaBuffer->VirtualAddress;
        PatchArgs.DmaBufferSize = DmaBuffer->Capacity;
        PatchArgs.DmaBufferSubmissionStartOffset = DmaBuffer->SubmissionStartOffset;
        PatchArgs.DmaBufferSubmissionEndOffset = DmaBuffer->SubmissionEndOffset;
        PatchArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
        PatchArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
        PatchArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
        PatchArgs.DmaBufferPrivateDataSubmissionEndOffset = sizeof(DmaBufferPrivateData);
        PatchArgs.pAllocationList = AllocationList;
        PatchArgs.AllocationListSize = ResourceHandleCount;
        PatchArgs.pPatchLocationList = EscapePatchList;
        PatchArgs.PatchLocationListSize = EscapePatchCount;
        PatchArgs.PatchLocationListSubmissionStart = 0;
        PatchArgs.PatchLocationListSubmissionLength = EscapePatchCount;
        PatchArgs.SubmissionFenceId = SubmissionFenceId;
        PatchArgs.Flags.Value = 0;
        PatchArgs.EngineOrdinal = 0;

        if (!DxgkAcquireKmdCall(Adapter))
        {
            Status = STATUS_DELETE_PENDING;
            goto Cleanup;
        }
        _SEH2_TRY
        {
            PatchStatus = DXGK_CB_FULL(Adapter, DxgkDdiPatch)(Adapter->MiniportDeviceContext, &PatchArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            PatchStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);

        if (!NT_SUCCESS(PatchStatus))
        {
            Status = PatchStatus;
            goto Cleanup;
        }
    }

    RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
    SubmitArgs.hDevice = Device->hMiniportDevice;
    SubmitArgs.DmaBufferSegmentId = DmaBuffer->SegmentId;
    SubmitArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;
    SubmitArgs.DmaBufferSize = DmaBuffer->Capacity;
    SubmitArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
    SubmitArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
    SubmitArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
    SubmitArgs.DmaBufferPrivateDataSubmissionEndOffset = sizeof(DmaBufferPrivateData);
    SubmitArgs.DmaBufferSubmissionStartOffset = DmaBuffer->SubmissionStartOffset;
    SubmitArgs.DmaBufferSubmissionEndOffset = DmaBuffer->SubmissionEndOffset;
    SubmitArgs.SubmissionFenceId = SubmissionFenceId;
    SubmitArgs.VidPnSourceId = 0;
    SubmitArgs.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    SubmitArgs.NodeOrdinal = 0;
    SubmitArgs.EngineOrdinal = 0;
    SubmitArgs.Flags.Value = 0;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    if (!DxgkReserveSubmissionFenceIdentity(Adapter, 0, SubmissionFenceId))
    {
        DxgkReleaseKmdCall(Adapter);
        Status = STATUS_DEVICE_BUSY;
        goto Cleanup;
    }
    Reservation->FenceIdentityOwned = TRUE;
    Status = DxgkActivateTrackedDmaBuffer(Reservation);
    if (!NT_SUCCESS(Status))
    {
        DxgkReleaseKmdCall(Adapter);
        goto Cleanup;
    }
    DxgkPublishSubmittedFence(Adapter, 0, SubmissionFenceId);
    Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(Adapter->MiniportDeviceContext, &SubmitArgs);
    DxgkReleaseKmdCall(Adapter);

    if (!NT_SUCCESS(Status))
        KeBugCheckEx(0x119, 0x2, (ULONG_PTR)Status, (ULONG_PTR)&SubmitArgs, (ULONG_PTR)Adapter);

    {
        PDXGKRNL_SUBMIT_DMA_BUFFER CommittedReservation = Reservation;

        Reservation = NULL;
        DmaBuffer = NULL;
        DxgkCommitTrackedDmaBuffer(Adapter, CommittedReservation);
    }
    Status = STATUS_SUCCESS;

Cleanup:
    if (Reservation != NULL)
        DxgkCancelTrackedDmaBuffer(Reservation);
    if (KmdTransaction)
        DxgkEndKmdTransaction(Adapter);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_DXGK_SUBMITDMA);
    if (OpenBindingReferenceList != NULL)
    {
        for (i = 0; i < OpenBindingReferenceCount; ++i)
            DxgkVidMmDereferenceLogicalAllocation(OpenBindingReferenceList[i]);
        ExFreePoolWithTag(OpenBindingReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (AllocationReferenceList != NULL)
    {
        for (i = 0; i < AllocationReferenceCount; ++i)
        {
            DxgkVidMmReleaseSubmissionResidencyPin(AllocationReferenceList[i]);
            DxgkVidMmDereferenceAllocation(AllocationReferenceList[i]);
        }
        ExFreePoolWithTag(AllocationReferenceList, TAG_DXGK_SUBMITDMA);
    }
    if (DmaBuffer != NULL)
        DxgkFreeDmaBuffer(DmaBuffer);

    return Status;
}

/* ========================================================================
 * Standalone D3DKMT handler wrappers
 *
 * These functions wrap the Lock/Unlock/Escape logic so they can be called
 * both from the IOCTL dispatch (with IRP context) and from the
 * REACTOS_WIN32K_DXGKRNL_INTERFACE exchange (without IRP context).
 * ====================================================================== */

/*
 * DxgkLock -- D3DKMTLock handler.
 *
 * When called through the interface (no IRP), we assume kernel caller
 * and always use the kernel (system VA) mapping path.
 */
NTSTATUS
NTAPI
DxgkLock(
    _Inout_ D3DKMT_LOCK *pLock)
{
    PDXGKRNL_ADAPTER LockAdapter;
    PDXGKRNL_DEVICE  LockDevice;
    PDXGKVMM_ALLOCATION LockAlloc;
    PVOID LockVa = NULL;
    NTSTATUS Status;

    if (pLock == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(pLock->hDevice, PsGetCurrentProcess(), &LockAdapter, &LockDevice);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkLock: invalid device 0x%X\n", pLock->hDevice);
        return STATUS_INVALID_HANDLE;
    }
    if (!DxgkpDeviceExecutionActive(LockDevice))
    {
        DxgkDereferenceDevice(LockDevice);
        return STATUS_DEVICE_REMOVED;
    }

    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)pLock->hAllocation, LockAdapter, LockDevice, &LockAlloc);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkLock: invalid alloc 0x%X\n", pLock->hAllocation);
        DxgkDereferenceDevice(LockDevice);
        return STATUS_INVALID_HANDLE;
    }

    if (!DxgkBeginKmdTransaction(LockAdapter))
    {
        DxgkVidMmDereferenceAllocation(LockAlloc);
        DxgkDereferenceDevice(LockDevice);
        return STATUS_DELETE_PENDING;
    }
    /* Interface callers are always kernel -- use system VA mapping.  Keep
     * reset recovery outside the active-check-to-map interval. */
    if (!DxgkpDeviceExecutionActive(LockDevice))
        Status = STATUS_DEVICE_REMOVED;
    else
        Status = DxgkVidMmMapAllocationCpu(LockAlloc, &LockVa);
    DxgkEndKmdTransaction(LockAdapter);
    DxgkVidMmDereferenceAllocation(LockAlloc);
    DxgkDereferenceDevice(LockDevice);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkLock: MapCpu failed 0x%08lX\n", Status);
        return Status;
    }

    pLock->pData = LockVa;
    DXGKRNL_TRACE("DxgkLock: alloc=0x%X -> VA=%p\n",
                  pLock->hAllocation, LockVa);
    return STATUS_SUCCESS;
}

/*
 * DxgkUnlock -- D3DKMTUnlock handler.
 */
NTSTATUS
NTAPI
DxgkUnlock(
    _In_ CONST D3DKMT_UNLOCK *pUnlock)
{
    PDXGKRNL_ADAPTER UnlockAdapter;
    PDXGKRNL_DEVICE  UnlockDevice;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT ui;

    if (pUnlock == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pUnlock->NumAllocations == 0 ||
        pUnlock->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT ||
        pUnlock->phAllocations == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkReferenceOwnedDeviceByHandle(pUnlock->hDevice, PsGetCurrentProcess(), &UnlockAdapter, &UnlockDevice);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_HANDLE;

    _SEH2_TRY
    {
        for (ui = 0; ui < pUnlock->NumAllocations; ui++)
        {
            PDXGKVMM_ALLOCATION UnlockAlloc;
            D3DKMT_HANDLE UnlockHandle;

            UnlockHandle = pUnlock->phAllocations[ui];
            Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)UnlockHandle, UnlockAdapter, UnlockDevice, &UnlockAlloc);
            if (!NT_SUCCESS(Status))
            {
                Status = STATUS_INVALID_HANDLE;
                break;
            }

            DxgkVidMmUnmapAllocationCpu(UnlockAlloc);
            DxgkVidMmDereferenceAllocation(UnlockAlloc);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    DxgkDereferenceDevice(UnlockDevice);
    return Status;
}

/*
 * DxgkEscape -- D3DKMTEscape handler.
 */
static NTSTATUS
DxgkpEscapeCaptured(
    _In_ CONST D3DKMT_ESCAPE *pEscape)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_ADAPTER EscDeviceAdapter = NULL;
    PDXGKRNL_ADAPTER EscContextAdapter = NULL;
    PDXGKRNL_DEVICE  EscDevice = NULL;
    PDXGKRNL_DEVICE  EscContextDevice = NULL;
    PDXGKRNL_CONTEXT EscContext = NULL;
    DXGKARG_ESCAPE   EscapeArgs;
    CONST VOID       *CommandBuffer;
    UINT             CommandBytes;
    CONST D3DKMT_HANDLE *ResourceHandles;
    UINT             ResourceHandleCount;
    NTSTATUS         Status;

    if (pEscape == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpValidateAdapterHandle(pEscape->hAdapter);
    if (Adapter == NULL)
    {
        DXGKRNL_WARN("DxgkEscape: invalid adapter handle 0x%X\n",
                     pEscape->hAdapter);
        return STATUS_INVALID_HANDLE;
    }

    if (pEscape->pPrivateDriverData == NULL ||
        pEscape->PrivateDriverDataSize == 0)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    if (pEscape->hDevice != 0)
    {
        EscDevice = DxgkLookupDeviceByHandle(pEscape->hDevice, &EscDeviceAdapter);
        if (EscDevice == NULL || EscDeviceAdapter != Adapter)
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        if (!DxgkpDeviceExecutionActive(EscDevice))
        {
            Status = STATUS_DEVICE_REMOVED;
            goto Cleanup;
        }
    }

    if (pEscape->hContext != 0)
    {
        EscContext = DxgkLookupContextByHandle(pEscape->hContext, &EscContextAdapter, &EscContextDevice);
        if (EscContext == NULL || EscContextAdapter != Adapter || EscContextDevice != EscDevice)
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
    }

    {
        D3DKMT_HANDLE SignalSyncObject = 0;
        ULONG64 SignalFenceValue = 0;

        if (DxgkpIsVirtGpuCommandEscape(pEscape, &CommandBuffer, &CommandBytes, &ResourceHandles, &ResourceHandleCount, &SignalSyncObject, &SignalFenceValue))
        {
            if (EscDevice == NULL)
            {
                DXGKRNL_WARN("DxgkEscape: command packet without valid device 0x%X\n",
                             pEscape->hDevice);
                Status = STATUS_INVALID_HANDLE;
                goto Cleanup;
            }

            Status = DxgkpSubmitVirtGpuCommandEscape(Adapter, EscDevice, CommandBuffer, CommandBytes, ResourceHandles, ResourceHandleCount, SignalSyncObject, SignalFenceValue);
            goto Cleanup;
        }
    }

    if (pEscape->hDevice == 0)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    if (DXGK_CB_FULL(Adapter, DxgkDdiEscape) == NULL)
    {
        DXGKRNL_WARN("DxgkEscape: miniport has no DxgkDdiEscape\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    RtlZeroMemory(&EscapeArgs, sizeof(EscapeArgs));
    EscapeArgs.hDevice = EscDevice ? EscDevice->hMiniportDevice : NULL;
    EscapeArgs.pPrivateDriverData  = pEscape->pPrivateDriverData;
    EscapeArgs.PrivateDriverDataSize = pEscape->PrivateDriverDataSize;
    EscapeArgs.Flags.Value         = pEscape->Flags.Value;
    EscapeArgs.hContext            = EscContext ? EscContext->hMiniportContext : NULL;

    DXGKRNL_VERBOSE("DxgkEscape: adapter=0x%X size=%u flags=0x%X\n",
                    pEscape->hAdapter,
                    pEscape->PrivateDriverDataSize,
                    pEscape->Flags.Value);

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    if (!DxgkpDeviceExecutionActive(EscDevice))
    {
        DxgkReleaseKmdCall(Adapter);
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiEscape)(Adapter->MiniportDeviceContext, &EscapeArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DXGKRNL_ERR("DxgkEscape: miniport faulted 0x%08lX\n", Status);
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

Cleanup:
    if (EscContext != NULL)
        DxgkDereferenceContext(EscContext);
    if (EscDevice != NULL)
        DxgkDereferenceDevice(EscDevice);
    if (Adapter != NULL)
        DxgkDereferenceAdapter(Adapter);
    return Status;
}

static NTSTATUS
DxgkpEscapeWithAccessMode(
    _In_ CONST D3DKMT_ESCAPE *pEscape,
    _In_ KPROCESSOR_MODE EmbeddedBufferMode)
{
    D3DKMT_ESCAPE CapturedEscape;
    PVOID CapturedPrivateData = NULL;
    PVOID UserPrivateData;
    NTSTATUS CopyStatus;
    NTSTATUS Status;

    PAGED_CODE();

    if (pEscape == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pEscape->PrivateDriverDataSize > RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA)
        return STATUS_INVALID_BUFFER_SIZE;

    CapturedEscape = *pEscape;
    UserPrivateData = pEscape->pPrivateDriverData;
    if (CapturedEscape.PrivateDriverDataSize != 0 && UserPrivateData != NULL)
    {
        Status = DxgkpCaptureUserBuffer(UserPrivateData, CapturedEscape.PrivateDriverDataSize, EmbeddedBufferMode, TAG_DXGK_CAPTURE, &CapturedPrivateData);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkpProbeOutputBuffer(UserPrivateData, CapturedEscape.PrivateDriverDataSize, EmbeddedBufferMode);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
            return Status;
        }
        CapturedEscape.pPrivateDriverData = CapturedPrivateData;
    }

    Status = DxgkpEscapeCaptured(&CapturedEscape);
    if (NT_SUCCESS(Status) && CapturedPrivateData != NULL)
    {
        CopyStatus = DxgkpCopyToUserBuffer(UserPrivateData, CapturedPrivateData, CapturedEscape.PrivateDriverDataSize, EmbeddedBufferMode);
        if (!NT_SUCCESS(CopyStatus))
            Status = CopyStatus;
    }

    if (CapturedPrivateData != NULL)
        ExFreePoolWithTag(CapturedPrivateData, TAG_DXGK_CAPTURE);
    return Status;
}

NTSTATUS
NTAPI
DxgkEscape(
    _In_ CONST D3DKMT_ESCAPE *pEscape)
{
    return DxgkpEscapeWithAccessMode(pEscape, KernelMode);
}

static NTSTATUS
NTAPI
DxgkCheckMonitorPowerState(
    _In_ CONST D3DKMT_CHECKMONITORPOWERSTATE *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    return DxgkpValidateAdapterVidPnSourceForIoctl(pData->hAdapter,
                                                   pData->VidPnSourceId,
                                                   NULL);
}

static NTSTATUS
DxgkpQueryScanLine(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ D3DKMT_GETSCANLINE *pData)
{
    PDXGKDDI_GET_SCAN_LINE GetScanLine;
    DXGKARG_GETSCANLINE GetScanLineArgs;
    NTSTATUS Status;

    PAGED_CODE();
    GetScanLine = DXGK_CB_FULL(Adapter, DxgkDdiGetScanLine);
    if (GetScanLine == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DEVICE_REMOVED;
    RtlZeroMemory(&GetScanLineArgs, sizeof(GetScanLineArgs));
    GetScanLineArgs.VidPnSourceId = pData->VidPnSourceId;
    _SEH2_TRY
    {
        Status = GetScanLine(Adapter->MiniportDeviceContext, &GetScanLineArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    pData->ScanLine = GetScanLineArgs.ScanLine;
    pData->InVerticalBlank = GetScanLineArgs.InVerticalBlank;
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkCheckOcclusion(
    _In_ CONST D3DKMT_CHECKOCCLUSION *pData)
{
    if (pData == NULL || pData->hWindow == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * D3DKMTCheckOcclusion reports whether the target window's client area is
     * occluded (covered/minimized) so a presenter can throttle. Windows returns
     * STATUS_SUCCESS when visible and STATUS_GRAPHICS_PRESENT_OCCLUDED when not.
     * The single-headed software adapter has no real overlay/compositor occlusion
     * model, so a valid visible window is reported as not occluded (SUCCESS),
     * matching Win11 for a shown top-level window. (Was STATUS_NOT_SUPPORTED,
     * which failed the displayext occlusion parity test.)
     */
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkUnsupportedDeviceCall(
    _In_ D3DKMT_HANDLE hDevice)
{
    NTSTATUS Status;

    Status = DxgkpValidateDeviceHandleForIoctl(hDevice, NULL, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkCreateOverlay(
    _Inout_ D3DKMT_CREATEOVERLAY *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    pData->hOverlay = 0;
    return DxgkUnsupportedDeviceCall(pData->hDevice);
}

static NTSTATUS
NTAPI
DxgkDestroyOverlay(
    _In_ CONST D3DKMT_DESTROYOVERLAY *pData)
{
    if (pData == NULL || pData->hOverlay == 0)
        return STATUS_INVALID_PARAMETER;

    return DxgkUnsupportedDeviceCall(pData->hDevice);
}

static NTSTATUS
NTAPI
DxgkFlipOverlay(
    _In_ CONST D3DKMT_FLIPOVERLAY *pData)
{
    if (pData == NULL || pData->hOverlay == 0 || pData->hSource == 0)
        return STATUS_INVALID_PARAMETER;
    if (pData->PrivateDriverDataSize != 0 && pData->pPrivateDriverData == NULL)
        return STATUS_INVALID_PARAMETER;

    return DxgkUnsupportedDeviceCall(pData->hDevice);
}

static NTSTATUS
NTAPI
DxgkUpdateOverlay(
    _In_ CONST D3DKMT_UPDATEOVERLAY *pData)
{
    if (pData == NULL || pData->hOverlay == 0)
        return STATUS_INVALID_PARAMETER;

    return DxgkUnsupportedDeviceCall(pData->hDevice);
}

static NTSTATUS
NTAPI
DxgkGetContextSchedulingPriority(
    _Inout_ D3DKMT_GETCONTEXTSCHEDULINGPRIORITY *pData)
{
    PDXGKRNL_CONTEXT Context;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = DxgkLookupContextByHandle(pData->hContext, NULL, NULL);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;

    pData->Priority = Context->SchedulingPriority;
    DxgkDereferenceContext(Context);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkSetContextSchedulingPriority(
    _In_ CONST D3DKMT_SETCONTEXTSCHEDULINGPRIORITY *pData)
{
    PDXGKRNL_CONTEXT Context;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Documented D3DKMT range: -7 (idle) .. 7 (realtime). */
    if (pData->Priority < -7 || pData->Priority > 7)
        return STATUS_INVALID_PARAMETER;

    Context = DxgkLookupContextByHandle(pData->hContext, NULL, NULL);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;

    Context->SchedulingPriority = pData->Priority;
    DxgkDereferenceContext(Context);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkGetDeviceState(
    _Inout_ D3DKMT_GETDEVICESTATE *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    UINT i;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    switch (pData->StateType)
    {
        case D3DKMT_DEVICESTATE_EXECUTION:
            if (!DxgkpAdapterSupportsRender(Adapter))
                Status = STATUS_NOT_SUPPORTED;
            else
            {
                pData->ExecutionState = InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE ? (D3DKMT_DEVICEEXECUTION_STATE)InterlockedCompareExchange(&Device->ExecutionState, 0, 0) : (Adapter->State == DxgkAdapterStateStarted && InterlockedCompareExchange(&Adapter->MiniportCallbacksValid, 0, 0) != 0 && InterlockedCompareExchange(&Device->Destroying, 0, 0) == 0 ? D3DKMT_DEVICEEXECUTION_ACTIVE : D3DKMT_DEVICEEXECUTION_STOPPED);
                Status = STATUS_SUCCESS;
            }
            break;

        case D3DKMT_DEVICESTATE_RESET:
            pData->ResetState.Value = 0;
            Status = STATUS_SUCCESS;
            break;

        case D3DKMT_DEVICESTATE_PRESENT:
        case D3DKMT_DEVICESTATE_PRESENT_DWM:
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        case D3DKMT_DEVICESTATE_PAGE_FAULT:
#endif
            Status = STATUS_NOT_SUPPORTED;
            break;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_1)
        case D3DKMT_DEVICESTATE_PRESENT_QUEUE:
            Status = DxgkPresentGetQueueLimitState(Device, pData->PresentQueueState.VidPnSourceId, &pData->PresentQueueState.bQueuedPresentLimitReached);
            break;
#endif

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
NTAPI
DxgkGetMultisampleMethodList(
    _Inout_ D3DKMT_GETMULTISAMPLEMETHODLIST *pData)
{
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateAdapterVidPnSourceForIoctl(pData->hAdapter,
                                                     pData->VidPnSourceId,
                                                     NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    pData->MethodCount = 0;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkGetPresentHistory(
    _Inout_ D3DKMT_GETPRESENTHISTORY *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(DxgkpValidateAdapterOnlyForIoctl(pData->hAdapter)))
        return STATUS_INVALID_HANDLE;
    if (pData->ProvidedSize != 0 && pData->pTokens == NULL)
        return STATUS_INVALID_PARAMETER;

    pData->WrittenSize = 0;
    pData->NumTokens = 0;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkGetRuntimeData(
    _Inout_ CONST D3DKMT_GETRUNTIMEDATA *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(DxgkpValidateAdapterOnlyForIoctl(pData->hAdapter)))
        return STATUS_INVALID_HANDLE;
    if (pData->hGlobalShare == 0 || !NT_SUCCESS(DxgkpValidateGlobalShareForIoctl(pData->hGlobalShare)))
    {
        return STATUS_INVALID_HANDLE;
    }
    if (pData->RuntimeDataSize != 0 && pData->pRuntimeData == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkGetScanLine(
    _Inout_ D3DKMT_GETSCANLINE *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateAdapterVidPnSourceForIoctl(pData->hAdapter,
                                                     pData->VidPnSourceId,
                                                     &Adapter);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DxgkpQueryScanLine(Adapter, pData);
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

static NTSTATUS
NTAPI
DxgkInvalidateActiveVidPn(
    _In_ CONST D3DKMT_INVALIDATEACTIVEVIDPN *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!NT_SUCCESS(DxgkpValidateAdapterOnlyForIoctl(pData->hAdapter)))
        return STATUS_INVALID_HANDLE;
    if (pData->PrivateDriverDataSize != 0 && pData->pPrivateDriverData == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkPollDisplayChildren(
    _In_ CONST D3DKMT_POLLDISPLAYCHILDREN *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->Reserved != 0)
        return STATUS_INVALID_PARAMETER;
    if (pData->DisableModeReset && !pData->SynchronousPolling)
        return STATUS_INVALID_PARAMETER;
    return DxgkpPollDisplayChildrenRequest(pData);
}

static NTSTATUS
DxgkQueryAllocationResidencyWithAccessMode(
    _In_ CONST D3DKMT_QUERYALLOCATIONRESIDENCY *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS *ResidencyStates = NULL;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS ResourceState;
    UINT AllocationCount;
    UINT TotalPrivateDriverDataSize;
    UINT Index;
    BOOLEAN ResourceSnapshot = FALSE;
    SIZE_T ResidencyStatesSize;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    if (pData->pResidencyStatus == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (pData->hResource != 0)
    {
        if (pData->AllocationCount != 0 || pData->phAllocationList != NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkVidMmReferenceResource(pData->hResource, FALSE, Device, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations, &AllocationCount, &TotalPrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ResourceSnapshot = TRUE;
    }
    else
    {
        AllocationCount = pData->AllocationCount;
        if (AllocationCount == 0 || AllocationCount > DXGKP_MAX_D3DKMT_LIST_COUNT || pData->phAllocationList == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCaptureAllocationReferencesForIoctl(Adapter, Device, pData->phAllocationList, AllocationCount, AccessMode, &Allocations);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*ResidencyStates))
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Cleanup;
    }
    ResidencyStatesSize = (SIZE_T)AllocationCount * sizeof(*ResidencyStates);
    ResidencyStates = ExAllocatePoolWithTag(NonPagedPool, ResidencyStatesSize, TAG_DXGK_CAPTURE);
    if (ResidencyStates == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Status = DxgkVidMmQueryAllocationResidencyStates(Allocations, AllocationCount, ResidencyStates);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (pData->hResource != 0)
    {
        ResourceState = ResidencyStates[0];
        for (Index = 1; Index < AllocationCount; ++Index)
        {
            if (ResidencyStates[Index] > ResourceState)
                ResourceState = ResidencyStates[Index];
        }
        Status = DxgkpCopyToUserBuffer(pData->pResidencyStatus, &ResourceState, sizeof(ResourceState), AccessMode);
    }
    else
        Status = DxgkpCopyToUserBuffer(pData->pResidencyStatus, ResidencyStates, ResidencyStatesSize, AccessMode);

Cleanup:
    if (ResidencyStates != NULL)
        ExFreePoolWithTag(ResidencyStates, TAG_DXGK_CAPTURE);
    if (Allocations != NULL)
    {
        if (ResourceSnapshot)
            DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
        else
            DxgkpReleaseAllocationReferencesForIoctl(Allocations, AllocationCount);
    }
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
NTAPI
DxgkQueryAllocationResidency(
    _In_ CONST D3DKMT_QUERYALLOCATIONRESIDENCY *pData)
{
    return DxgkQueryAllocationResidencyWithAccessMode(pData, KernelMode);
}

static NTSTATUS
NTAPI
DxgkQueryStatistics(
    _Inout_ CONST D3DKMT_QUERYSTATISTICS *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkReleaseProcessVidPnSourceOwners(
    _In_ HANDLE hProcess)
{
    PEPROCESS Process;
    NTSTATUS Status;

    Status = ObReferenceObjectByHandle(hProcess, PROCESS_SET_INFORMATION, *PsProcessType, UserMode, (PVOID *)&Process, NULL);
    if (!NT_SUCCESS(Status))
        return STATUS_INVALID_PARAMETER;
    Status = DxgkVidPnReleaseProcessOwners(Process);
    ObDereferenceObject(Process);
    return Status;
}

static NTSTATUS
DxgkSetAllocationPriorityWithAccessMode(
    _In_ CONST D3DKMT_SETALLOCATIONPRIORITY *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    UINT *Priorities = NULL;
    UINT AllocationCount;
    UINT TotalPrivateDriverDataSize;
    BOOLEAN ResourceSnapshot = FALSE;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        DxgkDereferenceDevice(Device);
        return STATUS_DEVICE_REMOVED;
    }

    if (pData->hResource != 0)
    {
        if (pData->AllocationCount != 0 || pData->phAllocationList != NULL || pData->pPriorities == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCapturePrioritiesForIoctl(pData->pPriorities, 1, AccessMode, &Priorities);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Status = DxgkVidMmReferenceResource(pData->hResource, FALSE, Device, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations, &AllocationCount, &TotalPrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ResourceSnapshot = TRUE;
        if (AllocationCount > 1)
        {
            UINT *ResourcePriorities;
            SIZE_T ResourcePrioritiesSize;
            UINT Index;

            if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*ResourcePriorities))
            {
                Status = STATUS_INTEGER_OVERFLOW;
                goto Cleanup;
            }
            ResourcePrioritiesSize = (SIZE_T)AllocationCount * sizeof(*ResourcePriorities);
            ResourcePriorities = ExAllocatePoolWithTag(NonPagedPool, ResourcePrioritiesSize, TAG_DXGK_CAPTURE);
            if (ResourcePriorities == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }
            for (Index = 0; Index < AllocationCount; ++Index)
                ResourcePriorities[Index] = Priorities[0];
            ExFreePoolWithTag(Priorities, TAG_DXGK_CAPTURE);
            Priorities = ResourcePriorities;
        }
    }
    else
    {
        AllocationCount = pData->AllocationCount;
        if (AllocationCount == 0 || AllocationCount > DXGKP_MAX_D3DKMT_LIST_COUNT || pData->phAllocationList == NULL || pData->pPriorities == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCapturePrioritiesForIoctl(pData->pPriorities, AllocationCount, AccessMode, &Priorities);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Status = DxgkpCaptureAllocationReferencesForIoctl(Adapter, Device, pData->phAllocationList, AllocationCount, AccessMode, &Allocations);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    Status = DxgkVidMmSetAllocationPriorities(Allocations, Priorities, AllocationCount);

Cleanup:
    if (Priorities != NULL)
        ExFreePoolWithTag(Priorities, TAG_DXGK_CAPTURE);
    if (Allocations != NULL)
    {
        if (ResourceSnapshot)
            DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
        else
            DxgkpReleaseAllocationReferencesForIoctl(Allocations, AllocationCount);
    }
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
NTAPI
DxgkSetAllocationPriority(
    _In_ CONST D3DKMT_SETALLOCATIONPRIORITY *pData)
{
    return DxgkSetAllocationPriorityWithAccessMode(pData, KernelMode);
}

static NTSTATUS
DxgkGetAllocationPriorityWithAccessMode(
    _In_ CONST D3DKMT_GETALLOCATIONPRIORITY *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKVMM_RESOURCE Resource = NULL;
    PDXGKVMM_ALLOCATION *Allocations = NULL;
    UINT *Priorities = NULL;
    UINT AllocationCount;
    UINT TotalPrivateDriverDataSize;
    UINT ResourcePriority = 0;
    UINT Index;
    BOOLEAN ResourceSnapshot = FALSE;
    SIZE_T PrioritiesSize;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    if (pData->hResource != 0)
    {
        if (pData->AllocationCount != 0 || pData->phAllocationList != NULL || pData->pPriorities == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkVidMmReferenceResource(pData->hResource, FALSE, Device, &Resource);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        Status = DxgkVidMmSnapshotResourceAllocations(Resource, Adapter, &Allocations, &AllocationCount, &TotalPrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ResourceSnapshot = TRUE;
    }
    else
    {
        AllocationCount = pData->AllocationCount;
        if (AllocationCount == 0 || AllocationCount > DXGKP_MAX_D3DKMT_LIST_COUNT || pData->phAllocationList == NULL || pData->pPriorities == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = DxgkpCaptureAllocationReferencesForIoctl(Adapter, Device, pData->phAllocationList, AllocationCount, AccessMode, &Allocations);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }
    if ((SIZE_T)AllocationCount > MAXULONG_PTR / sizeof(*Priorities))
    {
        Status = STATUS_INTEGER_OVERFLOW;
        goto Cleanup;
    }
    PrioritiesSize = (SIZE_T)AllocationCount * sizeof(*Priorities);
    Priorities = ExAllocatePoolWithTag(NonPagedPool, PrioritiesSize, TAG_DXGK_CAPTURE);
    if (Priorities == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    Status = DxgkVidMmQueryAllocationPriorities(Allocations, AllocationCount, Priorities);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (ResourceSnapshot)
    {
        for (Index = 0; Index < AllocationCount; ++Index)
        {
            if (Priorities[Index] > ResourcePriority)
                ResourcePriority = Priorities[Index];
        }
        Status = DxgkpCopyToUserBuffer(pData->pPriorities, &ResourcePriority, sizeof(ResourcePriority), AccessMode);
    }
    else
        Status = DxgkpCopyToUserBuffer(pData->pPriorities, Priorities, PrioritiesSize, AccessMode);

Cleanup:
    if (Priorities != NULL)
        ExFreePoolWithTag(Priorities, TAG_DXGK_CAPTURE);
    if (Allocations != NULL)
    {
        if (ResourceSnapshot)
            DxgkVidMmReleaseAllocationSnapshot(Allocations, AllocationCount);
        else
            DxgkpReleaseAllocationReferencesForIoctl(Allocations, AllocationCount);
    }
    if (Resource != NULL)
        DxgkVidMmDereferenceResource(Resource);
    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
NTAPI
DxgkGetAllocationPriority(
    _In_ CONST D3DKMT_GETALLOCATIONPRIORITY *pData)
{
    return DxgkGetAllocationPriorityWithAccessMode(pData, KernelMode);
}

static NTSTATUS
NTAPI
DxgkSetDisplayPrivateDriverFormat(
    _In_ CONST D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    return DxgkUnsupportedDeviceCall(pData->hDevice);
}

static NTSTATUS
NTAPI
DxgkSetGammaRamp(
    _In_ CONST D3DKMT_SETGAMMARAMP *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    return DxgkUnsupportedDeviceCall(pData->hDevice);
}

static NTSTATUS
NTAPI
DxgkSetQueuedLimit(
    _In_ CONST D3DKMT_SETQUEUEDLIMIT *pData)
{
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, NULL, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    switch (pData->Type)
    {
        case D3DKMT_SET_QUEUEDLIMIT_PRESENT:
            Status = DxgkPresentSetQueuedLimit(Device, pData->QueuedPresentLimit);
            break;
        case D3DKMT_GET_QUEUEDLIMIT_PRESENT:
            Status = DxgkPresentGetQueuedLimit(Device, &((D3DKMT_SETQUEUEDLIMIT *)pData)->QueuedPresentLimit);
            break;
        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }
    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
NTAPI
DxgkWaitForIdle(
    _In_ CONST D3DKMT_WAITFORIDLE *pData)
{
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, NULL, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkDeviceWaitForIdle(Device);
    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
DxgkpReferenceVerticalBlankTarget(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Outptr_result_maybenull_ PDXGKRNL_DEVICE *OutDevice)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_ADAPTER DeviceAdapter;
    PDXGKRNL_DEVICE Device = NULL;
    NTSTATUS Status;

    if (OutAdapter == NULL || OutDevice == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAdapter = NULL;
    *OutDevice = NULL;
    Status = DxgkpValidateAdapterVidPnSourceForIoctl(hAdapter, VidPnSourceId, &Adapter);
    if (!NT_SUCCESS(Status))
        return Status;
    if (hDevice != 0)
    {
        Status = DxgkpValidateDeviceHandleForIoctl(hDevice, &DeviceAdapter, &Device);
        if (!NT_SUCCESS(Status))
        {
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }
        if (DeviceAdapter != Adapter)
            Status = STATUS_INVALID_PARAMETER;
        else if (!DxgkpDeviceExecutionActive(Device))
            Status = STATUS_DEVICE_REMOVED;
        if (!NT_SUCCESS(Status))
        {
            DxgkDereferenceDevice(Device);
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }
    }
    *OutAdapter = Adapter;
    *OutDevice = Device;
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkWaitForVerticalBlankEvent(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpReferenceVerticalBlankTarget(pData->hAdapter, pData->hDevice, pData->VidPnSourceId, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkpWaitForVerticalBlank(Adapter, Device, pData->VidPnSourceId, 0, NULL);
    if (Device != NULL)
        DxgkDereferenceDevice(Device);
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

static NTSTATUS
NTAPI
DxgkOfferAllocations(
    _In_ CONST D3DKMT_OFFERALLOCATIONS *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        DxgkDereferenceDevice(Device);
        return STATUS_DEVICE_REMOVED;
    }

    if (pData->NumAllocations == 0 ||
        pData->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT ||
        pData->Priority < D3DKMT_OFFER_PRIORITY_LOW ||
        pData->Priority > D3DKMT_OFFER_PRIORITY_AUTO ||
        pData->Flags.Reserved != 0 ||
        ((pData->pResources == NULL) == (pData->HandleList == NULL)))
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INVALID_PARAMETER;
    }
    if (!DXGKP_OFFER_RECLAIM_END_TO_END)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }

    if (pData->HandleList != NULL)
    {
        UINT i;

        Status = DxgkpValidateAllocationListForIoctl(Adapter, Device, pData->HandleList, pData->NumAllocations);
        if (NT_SUCCESS(Status))
        {
            _SEH2_TRY
            {
                for (i = 0; i < pData->NumAllocations && NT_SUCCESS(Status); ++i)
                    Status = DxgkVidMmOfferAllocation(Adapter, Device, pData->HandleList[i], (ULONG)pData->Priority);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        DxgkDereferenceDevice(Device);
        return Status;
    }

    DxgkDereferenceDevice(Device);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkReclaimAllocations(
    _Inout_ D3DKMT_RECLAIMALLOCATIONS *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        DxgkDereferenceDevice(Device);
        return STATUS_DEVICE_REMOVED;
    }

    if (pData->NumAllocations == 0 ||
        pData->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT ||
        ((pData->pResources == NULL) == (pData->HandleList == NULL)))
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INVALID_PARAMETER;
    }
    if (!DXGKP_OFFER_RECLAIM_END_TO_END)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }

    if (pData->HandleList != NULL)
    {
        UINT i;

        Status = DxgkpValidateAllocationListForIoctl(Adapter, Device, pData->HandleList, pData->NumAllocations);
        if (NT_SUCCESS(Status))
        {
            _SEH2_TRY
            {
                for (i = 0; i < pData->NumAllocations && NT_SUCCESS(Status); ++i)
                {
                    BOOLEAN Discarded = FALSE;

                    Status = DxgkVidMmReclaimAllocation(Adapter, Device, pData->HandleList[i], &Discarded);
                    if (NT_SUCCESS(Status) && pData->pDiscarded != NULL)
                        pData->pDiscarded[i] = Discarded ? TRUE : FALSE;
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        DxgkDereferenceDevice(Device);
        return Status;
    }

    DxgkDereferenceDevice(Device);
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkSetVidPnSourceOwner1(
    _In_ CONST D3DKMT_SETVIDPNSOURCEOWNER1 *pData)
{
    D3DKMT_SETVIDPNSOURCEOWNER Owner;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Owner = pData->Version0;
    return DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(&Owner, pData->Flags.Value, KernelMode);
}

static NTSTATUS
NTAPI
DxgkWaitForVerticalBlankEvent2(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT2 *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->NumObjects > D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS)
        return STATUS_INVALID_PARAMETER;
    if (pData->NumObjects != 0)
        return STATUS_ACCESS_DENIED;
    Status = DxgkpReferenceVerticalBlankTarget(pData->hAdapter, pData->hDevice, pData->VidPnSourceId, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkpWaitForVerticalBlank(Adapter, Device, pData->VidPnSourceId, pData->NumObjects, pData->ObjectHandleArray);
    if (Device != NULL)
        DxgkDereferenceDevice(Device);
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

static NTSTATUS
NTAPI
DxgkCreateSynchronizationObject2(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *pData)
{
    D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS SupportedFlags;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->Info.Type == D3DDDI_MONITORED_FENCE && pData->Info.Flags.NoSignal && pData->Info.Flags.NoWait)
        return STATUS_INVALID_PARAMETER;
    if (pData->Info.Type != D3DDDI_MONITORED_FENCE && pData->Info.Flags.Value != 0)
        return STATUS_NOT_SUPPORTED;
    if (pData->Info.Type == D3DDDI_MONITORED_FENCE)
    {
        /* This stack currently exposes one physical adapter per logical
         * adapter. Zero means all physical adapters; bit zero selects the
         * sole physical adapter explicitly. */
        if (pData->Info.MonitoredFence.Padding != 0 || (pData->Info.MonitoredFence.EngineAffinity & ~1u) != 0)
            return STATUS_INVALID_PARAMETER;
        RtlZeroMemory(&SupportedFlags, sizeof(SupportedFlags));
        SupportedFlags.NoSignal = 1;
        SupportedFlags.NoWait = 1;
        SupportedFlags.NoSignalMaxValueOnTdr = 1;
        SupportedFlags.NoGPUAccess = 1;
        if ((pData->Info.Flags.Value & ~SupportedFlags.Value) != 0)
            return STATUS_NOT_SUPPORTED;
    }

    switch (pData->Info.Type)
    {
        case D3DDDI_SYNCHRONIZATION_MUTEX:
        case D3DDDI_SEMAPHORE:
        case D3DDDI_FENCE:
        case D3DDDI_CPU_NOTIFICATION:
        case D3DDDI_MONITORED_FENCE:
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkCreateSynchronizationObject2Core(pData->hDevice, &pData->Info, &pData->hSyncObject);
    if (NT_SUCCESS(Status))
    {
        pData->Info.SharedHandle = 0;

        if (pData->Info.Type == D3DDDI_MONITORED_FENCE)
        {
            PVOID UserVa = NULL;
            D3DGPU_VIRTUAL_ADDRESS FenceGpuVa = 0;
            NTSTATUS PageStatus;

            PageStatus = DxgkSyncObjectAttachMonitoredPage(pData->hSyncObject, pData->Info.MonitoredFence.InitialFenceValue, pData->Info.Flags, &UserVa, &FenceGpuVa);
            if (!NT_SUCCESS(PageStatus))
            {
                D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;

                RtlZeroMemory(&DestroySync, sizeof(DestroySync));
                DestroySync.hSyncObject = pData->hSyncObject;
                DxgkDestroySynchronizationObject(&DestroySync);
                pData->hSyncObject = 0;
                return PageStatus;
            }

            pData->Info.MonitoredFence.FenceValueCPUVirtualAddress = UserVa;
            pData->Info.MonitoredFence.FenceValueGPUVirtualAddress = FenceGpuVa;
        }
    }

    return Status;
}

static NTSTATUS
NTAPI
DxgkWaitForSynchronizationObject2(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->ObjectCount == 0 || pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceContextByHandle(pData->hContext, PsGetCurrentProcess(), &Adapter, &Device, &Context);
    if (!NT_SUCCESS(Status))
        return Status;
    ASSERT(Context->Device == Device && Device->Adapter == Adapter);
    Status = DxgkContextOrderAdmitWait(Context, DxgkContextSyncOperationWait2, pData->ObjectHandleArray, pData->ObjectCount, pData->Fence.FenceValue, UserMode);
    DxgkDereferenceContext(Context);
    return Status;
}

static NTSTATUS
NTAPI
DxgkSignalSynchronizationObject2(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *pData)
{
    PDXGKRNL_CONTEXT Contexts[D3DDDI_MAX_BROADCAST_CONTEXT + 1];
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_ADAPTER BroadcastAdapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_DEVICE BroadcastDevice;
    PEPROCESS OwnerProcess;
    UINT64 PayloadValue;
    ULONG SignalFlags;
    ULONG ContextCount = 0;
    ULONG BroadcastIndex;
    ULONG CompareIndex;
    NTSTATUS Status;

    PAGED_CODE();
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    SignalFlags = pData->Flags.Value;
    if (pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED || pData->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT || (SignalFlags & ~DXGK_CONTEXT_SYNC_SIGNAL2_FLAGS_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if (((SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0 && pData->ObjectCount != 0) || ((SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) == 0 && pData->ObjectCount == 0))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Contexts, sizeof(Contexts));
    OwnerProcess = PsGetCurrentProcess();
    Status = DxgkReferenceContextByHandle(pData->hContext, OwnerProcess, &Adapter, &Device, &Contexts[0]);
    if (!NT_SUCCESS(Status))
        return Status;
    ContextCount = 1;
    if (Device->OwnerProcess != OwnerProcess)
    {
        Status = STATUS_INVALID_HANDLE;
        goto Cleanup;
    }
    for (BroadcastIndex = 0; BroadcastIndex < pData->BroadcastContextCount; ++BroadcastIndex)
    {
        Status = DxgkReferenceContextByHandle(pData->BroadcastContext[BroadcastIndex], OwnerProcess, &BroadcastAdapter, &BroadcastDevice, &Contexts[ContextCount]);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        ++ContextCount;
        if (BroadcastDevice != Device || BroadcastAdapter != Adapter || BroadcastDevice->OwnerProcess != OwnerProcess)
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        for (CompareIndex = 0; CompareIndex + 1 < ContextCount; ++CompareIndex)
        {
            if (Contexts[CompareIndex] == Contexts[ContextCount - 1])
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Cleanup;
            }
        }
    }
    PayloadValue = (SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0 ? (UINT64)(ULONG_PTR)pData->CpuEventHandle : pData->Fence.FenceValue;
    Status = DxgkContextOrderAdmitSignal(Contexts, ContextCount, DxgkContextSyncOperationSignal2, pData->ObjectHandleArray, pData->ObjectCount, SignalFlags, PayloadValue, UserMode);

Cleanup:
    while (ContextCount != 0)
        DxgkDereferenceContext(Contexts[--ContextCount]);
    return Status;
}

/* ========================================================================
 * WDDM 2.0 paging queues
 *
 * A paging queue serialises residency operations for a device and is backed
 * by a monitored-fence sync object.  For the software/display-only path we
 * track a lightweight handle plus its backing sync object.  Nonempty residency
 * requests remain gated until paging work, completion, budgets, and teardown
 * operate end to end.
 * ====================================================================== */

typedef struct _DXGKRNL_PAGING_QUEUE
{
    LIST_ENTRY      ListEntry;
    D3DKMT_HANDLE   Handle;
    D3DKMT_HANDLE   hDevice;
    D3DKMT_HANDLE   hSyncObject;
    PDXGKRNL_DEVICE Device;
    PEPROCESS       OwnerProcess;
    volatile LONG   ReferenceCount;
    volatile LONG   Destroying;
    volatile LONG   TeardownClaimed;

    /* Monotonic paging fence: one value per queued paging operation; the
     * queue's monitored fence is signaled to it at packet retirement. */
    volatile LONG64 LastQueuedFence;
} DXGKRNL_PAGING_QUEUE, *PDXGKRNL_PAGING_QUEUE;

static LONG DxgkPagingQueueInitialized = 0;
static FAST_MUTEX DxgkPagingQueueListLock;
static LIST_ENTRY DxgkPagingQueueListHead;

static VOID
DxgkpPagingQueueInit(VOID)
{
    LONG State;

    State = InterlockedCompareExchange(&DxgkPagingQueueInitialized, 1, 0);
    if (State == 0)
    {
        ExInitializeFastMutex(&DxgkPagingQueueListLock);
        InitializeListHead(&DxgkPagingQueueListHead);
        InterlockedExchange(&DxgkPagingQueueInitialized, 2);
        return;
    }

    while (InterlockedCompareExchange(&DxgkPagingQueueInitialized, 2, 2) != 2)
        YieldProcessor();
}

static BOOLEAN
DxgkpReferencePagingQueue(
    _In_ PVOID Object)
{
    PDXGKRNL_PAGING_QUEUE Queue = Object;

    if (Queue == NULL || InterlockedCompareExchange(&Queue->Destroying, 0, 0) != 0)
        return FALSE;
    InterlockedIncrement(&Queue->ReferenceCount);
    if (InterlockedCompareExchange(&Queue->Destroying, 0, 0) != 0)
    {
        InterlockedDecrement(&Queue->ReferenceCount);
        return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpDereferencePagingQueue(
    _In_ PDXGKRNL_PAGING_QUEUE Queue)
{
    if (InterlockedDecrement(&Queue->ReferenceCount) == 0)
    {
        PDXGKRNL_DEVICE Device = Queue->Device;

        ExFreePoolWithTag(Queue, TAG_DXGK_SYNC);
        DxgkDereferenceDevice(Device);
    }
}

static NTSTATUS
DxgkpReferencePagingQueueDevice(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice)
{
    PDXGKRNL_PAGING_QUEUE Queue;
    PVOID Object;
    NTSTATUS Status;

    if (Handle == 0 || OwnerProcess == NULL || OutAdapter == NULL || OutDevice == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutAdapter = NULL;
    *OutDevice = NULL;
    Status = DxgkReferenceOwnedHandle(Handle, DxgkHandleTypePagingQueue, OwnerProcess, DxgkpReferencePagingQueue, &Object);
    if (!NT_SUCCESS(Status))
        return Status;
    Queue = Object;
    if (!DxgkReferenceDevice(Queue->Device))
    {
        DxgkpDereferencePagingQueue(Queue);
        return STATUS_DELETE_PENDING;
    }
    if (!DxgkpDeviceExecutionActive(Queue->Device))
    {
        DxgkDereferenceDevice(Queue->Device);
        DxgkpDereferencePagingQueue(Queue);
        return STATUS_DEVICE_REMOVED;
    }
    *OutAdapter = Queue->Device->Adapter;
    *OutDevice = Queue->Device;
    DxgkpDereferencePagingQueue(Queue);
    return STATUS_SUCCESS;
}

/* Like DxgkpReferencePagingQueueDevice, but keeps the queue referenced so
 * the caller can use its monitored fence and fence counter; release both
 * the device and the queue when done. */
static NTSTATUS
DxgkpReferencePagingQueueForPaging(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice,
    _Out_ PDXGKRNL_PAGING_QUEUE *OutQueue)
{
    PDXGKRNL_PAGING_QUEUE Queue;
    PVOID Object;
    NTSTATUS Status;

    if (Handle == 0 || OwnerProcess == NULL || OutAdapter == NULL || OutDevice == NULL || OutQueue == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutAdapter = NULL;
    *OutDevice = NULL;
    *OutQueue = NULL;
    Status = DxgkReferenceOwnedHandle(Handle, DxgkHandleTypePagingQueue, OwnerProcess, DxgkpReferencePagingQueue, &Object);
    if (!NT_SUCCESS(Status))
        return Status;
    Queue = Object;
    if (!DxgkReferenceDevice(Queue->Device))
    {
        DxgkpDereferencePagingQueue(Queue);
        return STATUS_DELETE_PENDING;
    }
    if (!DxgkpDeviceExecutionActive(Queue->Device))
    {
        DxgkDereferenceDevice(Queue->Device);
        DxgkpDereferencePagingQueue(Queue);
        return STATUS_DEVICE_REMOVED;
    }
    *OutAdapter = Queue->Device->Adapter;
    *OutDevice = Queue->Device;
    *OutQueue = Queue;
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkCreatePagingQueue(
    _Inout_ D3DKMT_CREATEPAGINGQUEUE *pData)
{
    PDXGKRNL_ADAPTER      Adapter;
    PDXGKRNL_DEVICE       Device;
    PDXGKRNL_PAGING_QUEUE Queue;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
    NTSTATUS              Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(pData->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        DxgkDereferenceDevice(Device);
        return STATUS_DEVICE_REMOVED;
    }
    if (pData->PhysicalAdapterIndex != 0 || pData->Priority < D3DDDI_PAGINGQUEUE_PRIORITY_BELOW_NORMAL || pData->Priority > D3DDDI_PAGINGQUEUE_PRIORITY_ABOVE_NORMAL)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INVALID_PARAMETER;
    }

    DxgkpPagingQueueInit();

    RtlZeroMemory(&CreateSync, sizeof(CreateSync));
    CreateSync.hDevice = pData->hDevice;
    CreateSync.Info.Type = D3DDDI_MONITORED_FENCE;
    CreateSync.Info.Flags.NoGPUAccess = 1;
    CreateSync.Info.MonitoredFence.InitialFenceValue = 0;
    Status = DxgkCreateSynchronizationObject2(&CreateSync);
    if (!NT_SUCCESS(Status))
    {
        DxgkDereferenceDevice(Device);
        return Status;
    }

    Queue = (PDXGKRNL_PAGING_QUEUE)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Queue), TAG_DXGK_SYNC);
    if (Queue == NULL)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = CreateSync.hSyncObject;
        DxgkDestroySynchronizationObject(&DestroySync);
        DxgkDereferenceDevice(Device);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Queue, sizeof(*Queue));
    Queue->hDevice = pData->hDevice;
    Queue->hSyncObject = CreateSync.hSyncObject;
    Queue->Device = Device;
    Queue->OwnerProcess = PsGetCurrentProcess();
    Queue->ReferenceCount = 1;
    InitializeListHead(&Queue->ListEntry);

    Status = DxgkCreateOwnedHandle(DxgkHandleTypePagingQueue, Queue, Adapter, Queue->OwnerProcess, &Queue->Destroying, &Queue->TeardownClaimed, &Queue->Handle);
    if (!NT_SUCCESS(Status))
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;

        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = Queue->hSyncObject;
        DxgkDestroySynchronizationObject(&DestroySync);
        DxgkpDereferencePagingQueue(Queue);
        return Status;
    }

    ExAcquireFastMutex(&DxgkPagingQueueListLock);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;

        ExReleaseFastMutex(&DxgkPagingQueueListLock);
        DxgkRemoveOwnedHandleObject(DxgkHandleTypePagingQueue, Queue);
        InterlockedExchange(&Queue->Destroying, 1);
        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = Queue->hSyncObject;
        DxgkDestroySynchronizationObject(&DestroySync);
        DxgkpDereferencePagingQueue(Queue);
        return STATUS_DELETE_PENDING;
    }
    InsertTailList(&DxgkPagingQueueListHead, &Queue->ListEntry);
    ExReleaseFastMutex(&DxgkPagingQueueListLock);

    pData->hPagingQueue = Queue->Handle;
    pData->hSyncObject = Queue->hSyncObject;
    pData->FenceValueCPUVirtualAddress = CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress;

    DXGKRNL_TRACE("DxgkCreatePagingQueue: queue=0x%X sync=0x%X\n", Queue->Handle, Queue->hSyncObject);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkDestroyPagingQueue(
    _Inout_ D3DDDI_DESTROYPAGINGQUEUE *pData)
{
    PDXGKRNL_PAGING_QUEUE Found;
    PVOID Object;
    D3DKMT_HANDLE         hSyncObject = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->hPagingQueue == 0)
        return STATUS_INVALID_HANDLE;

    Status = DxgkDetachOwnedHandle(pData->hPagingQueue, DxgkHandleTypePagingQueue, PsGetCurrentProcess(), &Object);
    if (!NT_SUCCESS(Status))
        return Status;
    Found = Object;
    ASSERT(InterlockedCompareExchange(&Found->TeardownClaimed, 1, 1) == 1);
    ExAcquireFastMutex(&DxgkPagingQueueListLock);
    if (!IsListEmpty(&Found->ListEntry))
    {
        RemoveEntryList(&Found->ListEntry);
        InitializeListHead(&Found->ListEntry);
    }
    ExReleaseFastMutex(&DxgkPagingQueueListLock);

    hSyncObject = Found->hSyncObject;

    if (hSyncObject != 0)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = hSyncObject;
        DxgkDestroySynchronizationObject(&DestroySync);
    }

    DXGKRNL_TRACE("DxgkDestroyPagingQueue: queue=0x%X\n", pData->hPagingQueue);
    DxgkpDereferencePagingQueue(Found);
    return STATUS_SUCCESS;
}

static VOID
DxgkpCleanupPagingQueues(
    _In_opt_ PEPROCESS Process,
    _In_opt_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device)
{
    LIST_ENTRY Retired;
    PLIST_ENTRY Entry;

    DxgkpPagingQueueInit();
    InitializeListHead(&Retired);
    ExAcquireFastMutex(&DxgkPagingQueueListLock);
    Entry = DxgkPagingQueueListHead.Flink;
    while (Entry != &DxgkPagingQueueListHead)
    {
        PDXGKRNL_PAGING_QUEUE Queue = CONTAINING_RECORD(Entry, DXGKRNL_PAGING_QUEUE, ListEntry);
        PLIST_ENTRY Next = Entry->Flink;

        if ((Process == NULL || Queue->OwnerProcess == Process) && (Adapter == NULL || Queue->Device->Adapter == Adapter) && (Device == NULL || Queue->Device == Device))
        {
            BOOLEAN OwnsTeardown = DxgkTryClaimTeardown(&Queue->TeardownClaimed);

            InterlockedExchange(&Queue->Destroying, 1);
            RemoveEntryList(&Queue->ListEntry);
            if (OwnsTeardown)
                InsertTailList(&Retired, &Queue->ListEntry);
            else
            {
                /* The direct owner retains this Device until its final release. */
                InitializeListHead(&Queue->ListEntry);
            }
        }
        Entry = Next;
    }
    ExReleaseFastMutex(&DxgkPagingQueueListLock);

    while (!IsListEmpty(&Retired))
    {
        PDXGKRNL_PAGING_QUEUE Queue = CONTAINING_RECORD(RemoveHeadList(&Retired), DXGKRNL_PAGING_QUEUE, ListEntry);

        ASSERT(InterlockedCompareExchange(&Queue->TeardownClaimed, 1, 1) == 1);
        DxgkRemoveOwnedHandleObject(DxgkHandleTypePagingQueue, Queue);
        DxgkpDereferencePagingQueue(Queue);
    }
}

VOID
DxgkD3dkmtProcessCleanup(
    _In_ PEPROCESS Process)
{
    DxgkpCleanupPagingQueues(Process, NULL, NULL);
}

VOID
DxgkD3dkmtAdapterCleanup(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    DxgkpCleanupPagingQueues(NULL, Adapter, NULL);
}

VOID
DxgkD3dkmtDeviceCleanup(
    _In_ PDXGKRNL_DEVICE Device)
{
    DxgkpCleanupPagingQueues(NULL, NULL, Device);
}

static NTSTATUS
NTAPI
DxgkQueryVideoMemoryInfo(
    _Inout_ D3DKMT_QUERYVIDEOMEMORYINFO *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PEPROCESS Process;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pData->PhysicalAdapterIndex != 0 || (pData->MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL && pData->MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL))
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pData->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (pData->hProcess != NULL)
    {
        Status = ObReferenceObjectByHandle(pData->hProcess, PROCESS_QUERY_INFORMATION, *PsProcessType, UserMode, (PVOID *)&Process, NULL);
        if (!NT_SUCCESS(Status))
        {
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }
    }
    else
    {
        Process = PsGetCurrentProcess();
        ObReferenceObject(Process);
    }

    Status = DxgkVidMmQueryProcessBudget(Adapter, Process, pData->MemorySegmentGroup, &pData->Budget, &pData->CurrentUsage, &pData->CurrentReservation, &pData->AvailableForReservation);
    ObDereferenceObject(Process);
    DxgkDereferenceAdapter(Adapter);
    return Status;
}

static NTSTATUS
NTAPI
DxgkSignalSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    D3DKMT_HANDLE    Handles[D3DDDI_MAX_OBJECT_SIGNALED];
    UINT64           Fences[D3DDDI_MAX_OBJECT_SIGNALED];
    NTSTATUS         Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED ||
        pData->ObjectHandleArray == NULL ||
        pData->FenceValueArray == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DxgkpCopyFromUserBuffer(Handles, pData->ObjectHandleArray, sizeof(D3DKMT_HANDLE) * pData->ObjectCount, AccessMode);
    if (NT_SUCCESS(Status))
        Status = DxgkpCopyFromUserBuffer(Fences, pData->FenceValueArray, sizeof(UINT64) * pData->ObjectCount, AccessMode);
    if (!NT_SUCCESS(Status))
    {
        DxgkDereferenceDevice(Device);
        return Status;
    }

    Status = DxgkSyncObjectCpuSignalBatch(Device, Handles, Fences, pData->ObjectCount, pData->Flags);
    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
NTAPI
DxgkWaitForSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    D3DKMT_HANDLE    Handles[D3DDDI_MAX_OBJECT_WAITED_ON];
    UINT64           Fences[D3DDDI_MAX_OBJECT_WAITED_ON];
    NTSTATUS         Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON ||
        pData->ObjectHandleArray == NULL ||
        pData->FenceValueArray == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if ((pData->Flags.Value & ~1UL) != 0)
        return STATUS_NOT_SUPPORTED;
    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DxgkpCopyFromUserBuffer(Handles, pData->ObjectHandleArray, sizeof(D3DKMT_HANDLE) * pData->ObjectCount, AccessMode);
    if (NT_SUCCESS(Status))
        Status = DxgkpCopyFromUserBuffer(Fences, pData->FenceValueArray, sizeof(UINT64) * pData->ObjectCount, AccessMode);
    if (!NT_SUCCESS(Status))
    {
        DxgkDereferenceDevice(Device);
        return Status;
    }

    Status = DxgkSyncObjectCpuWaitBatch(Device, Handles, Fences, pData->ObjectCount, pData->hAsyncEvent, pData->Flags.WaitAny != 0);
    DxgkDereferenceDevice(Device);
    return Status;
}

static NTSTATUS
NTAPI
DxgkSubmitCommand(
    _In_ CONST D3DKMT_SUBMITCOMMAND *SubmitCommand)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;
    ULONG FlagsValue;
    ULONG FenceId;
    NTSTATUS Status;

    if (SubmitCommand == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(&FlagsValue, &SubmitCommand->Flags, sizeof(FlagsValue));
    if (SubmitCommand->BroadcastContextCount != 1 || SubmitCommand->BroadcastContext[0] == 0 || SubmitCommand->Commands == 0 || SubmitCommand->CommandLength == 0 || SubmitCommand->PrivateDriverDataSize > RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA || (SubmitCommand->PrivateDriverDataSize != 0 && SubmitCommand->pPrivateDriverData == NULL))
        return STATUS_INVALID_PARAMETER;
    if (SubmitCommand->NumPrimaries != 0 || SubmitCommand->NumHistoryBuffers != 0 || SubmitCommand->PresentHistoryToken != 0 || (FlagsValue & ~RXGK_SUBMITCOMMAND_SUPPORTED_FLAGS) != 0)
        return STATUS_NOT_SUPPORTED;

    Status = DxgkReferenceVirtualContextByHandle(SubmitCommand->BroadcastContext[0], PsGetCurrentProcess(), &Adapter, &Device, &Context);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DxgkpDeviceExecutionActive(Device))
    {
        DxgkDereferenceContext(Context);
        return STATUS_DEVICE_REMOVED;
    }

    if (Device->ProcessRecord == NULL || Adapter->MiniportContext->InitData.s.Version < DXGKDDI_INTERFACE_VERSION_WDDM2_0 || DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) == NULL)
    {
        DxgkDereferenceContext(Context);
        return STATUS_NOT_SUPPORTED;
    }
    if (Context->UserModeCreateFlags.NullRendering && !SubmitCommand->Flags.NullRendering)
    {
        DxgkDereferenceContext(Context);
        return STATUS_NOT_SUPPORTED;
    }
    if (!DxgkGpuVaPageTableReady(Adapter, Device->ProcessRecord))
    {
        if (!SubmitCommand->Flags.NullRendering || !Context->UserModeCreateFlags.NullRendering)
        {
            DxgkDereferenceContext(Context);
            return STATUS_NOT_SUPPORTED;
        }
    }
    else if (!SubmitCommand->Flags.NullRendering && !DxgkGpuVaValidateRange(Adapter, Device->ProcessRecord, SubmitCommand->Commands, SubmitCommand->CommandLength))
    {
        DxgkDereferenceContext(Context);
        return STATUS_INVALID_PARAMETER;
    }

    if (!DxgkBeginKmdTransaction(Adapter))
    {
        DxgkDereferenceContext(Context);
        return STATUS_DELETE_PENDING;
    }
    if (!DxgkpDeviceExecutionActive(Device))
    {
        DxgkEndKmdTransaction(Adapter);
        DxgkDereferenceContext(Context);
        return STATUS_DEVICE_REMOVED;
    }
    Status = VidSchSubmitCommandVirtual(Adapter, Context, SubmitCommand->Commands, SubmitCommand->CommandLength, SubmitCommand->pPrivateDriverData, SubmitCommand->PrivateDriverDataSize, SubmitCommand->Flags.NullRendering != 0, &FenceId);
    DxgkEndKmdTransaction(Adapter);
    if (!NT_SUCCESS(Status))
        DxgkDereferenceContext(Context);
    return Status;
}

static NTSTATUS
DxgkpValidateWddmPrivatePacket(
    _In_ ULONG InputLength,
    _In_ ULONG PacketSize,
    _In_ ULONG PacketVersion,
    _In_ ULONG HeaderSize,
    _In_ ULONG PrivateDataSize,
    _In_ ULONG PrivateDataOffset)
{
    if (PacketVersion != RXGK_WDDM_PACKET_VERSION_1)
        return STATUS_NOT_SUPPORTED;

    if (PacketSize < HeaderSize || PacketSize != InputLength ||
        PrivateDataSize > RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PrivateDataSize == 0)
    {
        if (PrivateDataOffset != 0 || PacketSize != HeaderSize)
            return STATUS_INVALID_PARAMETER;
    }
    else
    {
        if (PrivateDataOffset != HeaderSize ||
            PrivateDataSize > PacketSize - HeaderSize ||
            PacketSize != HeaderSize + PrivateDataSize)
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpDispatchBufferedIoctl
 *
 * Helper to handle a METHOD_BUFFERED D3DKMT IOCTL.  The I/O manager has
 * already copied the user buffer into Irp->AssociatedIrp.SystemBuffer.
 * We validate sizes, call the handler, and set Information to the output
 * size on success.
 *
 * Returns the NTSTATUS that should be placed in IoStatus.
 * ====================================================================== */
static NTSTATUS
DxgkpDispatchBufferedIoctl(
    _In_ PIRP              Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PVOID   SystemBuffer  = Irp->AssociatedIrp.SystemBuffer;
    ULONG   InputLength   = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG   OutputLength  = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG   IoControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;
    KPROCESSOR_MODE EmbeddedBufferMode = Stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL ? KernelMode : Irp->RequestorMode;
    NTSTATUS Status;

    switch (IoControlCode)
    {
        case IOCTL_D3DKMT_ENUMADAPTERS:
        {
            if (InputLength < sizeof(D3DKMT_ENUMADAPTERS) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkEnumAdapters((D3DKMT_ENUMADAPTERS *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_ENUMADAPTERS);
            return Status;
        }

        case IOCTL_D3DKMT_ENUMADAPTERS2:
        {
            if (InputLength < sizeof(D3DKMT_ENUMADAPTERS2) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkpEnumAdapters2((D3DKMT_ENUMADAPTERS2 *)SystemBuffer, EmbeddedBufferMode);
            /*
             * The count query (pAdapters == NULL with started adapters present)
             * returns STATUS_BUFFER_TOO_SMALL with NumAdapters set to the count.
             * That output must still be copied back to the caller, so set
             * Information on the BUFFER_TOO_SMALL path too — otherwise the
             * METHOD_BUFFERED copy-back transfers zero bytes and the caller
             * never learns the adapter count.
             */
            if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
                Irp->IoStatus.Information = sizeof(D3DKMT_ENUMADAPTERS2);
            return Status == STATUS_BUFFER_TOO_SMALL ? STATUS_BUFFER_OVERFLOW : Status;
        }

        case IOCTL_D3DKMT_OPENADAPTERFROMLUID:
        {
            if (InputLength < sizeof(D3DKMT_OPENADAPTERFROMLUID) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkOpenAdapterFromLuid((D3DKMT_OPENADAPTERFROMLUID *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_OPENADAPTERFROMLUID);
            return Status;
        }

        case IOCTL_D3DKMT_CLOSEADAPTER:
        {
            if (InputLength < sizeof(D3DKMT_CLOSEADAPTER) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkCloseAdapter((CONST D3DKMT_CLOSEADAPTER *)SystemBuffer);
            return Status;
        }

        case IOCTL_D3DKMT_QUERYADAPTERINFO:
        {
            if (InputLength < sizeof(D3DKMT_QUERYADAPTERINFO) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkpQueryAdapterInfoWithAccessMode((CONST D3DKMT_QUERYADAPTERINFO *)SystemBuffer, EmbeddedBufferMode);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_QUERYADAPTERINFO);
            return Status;
        }

        case IOCTL_D3DKMT_GETDISPLAYMODELIST:
        {
            if (InputLength < sizeof(D3DKMT_GETDISPLAYMODELIST) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkGetDisplayModeList((D3DKMT_GETDISPLAYMODELIST *)SystemBuffer);
            if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
                Irp->IoStatus.Information = sizeof(D3DKMT_GETDISPLAYMODELIST);
            return Status == STATUS_BUFFER_TOO_SMALL ? STATUS_BUFFER_OVERFLOW : Status;
        }

        /*
         * Priority 1: Bridge IOCTLs from win32k D3DKMT stubs.
         *
         * Exact adapter selection: FromGdiDisplayName maps \\.\DISPLAYn to
         * the n-th display-capable adapter; FromDeviceName resolves the
         * device path to a PDO and matches it; FromHdc opens the primary
         * display adapter (HDC-to-display translation belongs to win32k,
         * which owns HDCs — the bridge transports the primary's surface).
         */
        case IOCTL_D3DKMT_OPENADAPTERFROMHDC:
        {
            if (InputLength < sizeof(D3DKMT_OPENADAPTERFROMHDC) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;
            return Stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL ? STATUS_NOT_SUPPORTED : STATUS_ACCESS_DENIED;
        }

        case IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME:
        {
            if (InputLength < sizeof(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;
            return Stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL ? STATUS_NOT_SUPPORTED : STATUS_ACCESS_DENIED;
        }

        case IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME:
        {
            D3DKMT_OPENADAPTERFROMDEVICENAME *pData;
            WCHAR NameBuffer[260];

            if (InputLength < sizeof(D3DKMT_OPENADAPTERFROMDEVICENAME) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;
            if (Stack->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL)
                return STATUS_ACCESS_DENIED;

            pData = (D3DKMT_OPENADAPTERFROMDEVICENAME *)SystemBuffer;
            Status = DxgkpCaptureDeviceName(pData->pDeviceName, EmbeddedBufferMode, NameBuffer, RTL_NUMBER_OF(NameBuffer));
            if (!NT_SUCCESS(Status))
                return Status;

            Status = DxgkpOpenAdapterByDeviceObjectName(NameBuffer, &pData->hAdapter, &pData->AdapterLuid);
            if (!NT_SUCCESS(Status))
                return Status;
            Irp->IoStatus.Information = sizeof(D3DKMT_OPENADAPTERFROMDEVICENAME);
            return STATUS_SUCCESS;
        }

        case IOCTL_D3DKMT_CREATEDEVICE:
        {
            D3DKMT_CREATEDEVICE *pData;
            D3DKMT_CREATEDEVICE CreateDeviceCopy;
            PDXGKRNL_ADAPTER Adapter;

            if (InputLength < sizeof(D3DKMT_CREATEDEVICE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pData = (D3DKMT_CREATEDEVICE *)SystemBuffer;
            Adapter = DxgkpValidateAdapterHandle(pData->hAdapter);
            if (Adapter == NULL)
                return STATUS_INVALID_PARAMETER;

            /*
             * METHOD_BUFFERED returns the whole CREATEDEVICE structure to the
             * caller.  Marshal through a local copy so the kernel-private
             * pAdapter pointer never overwrites the input hAdapter field in
             * the caller's buffer.
             */
            CreateDeviceCopy = *pData;
            CreateDeviceCopy.pAdapter = Adapter;

            Status = DxgkCreateDevice(&CreateDeviceCopy);
            DxgkDereferenceAdapter(Adapter);
            if (NT_SUCCESS(Status))
            {
                pData->hDevice = CreateDeviceCopy.hDevice;
                pData->pCommandBuffer = CreateDeviceCopy.pCommandBuffer;
                pData->CommandBufferSize = CreateDeviceCopy.CommandBufferSize;
                pData->pAllocationList = CreateDeviceCopy.pAllocationList;
                pData->AllocationListSize = CreateDeviceCopy.AllocationListSize;
                pData->pPatchLocationList = CreateDeviceCopy.pPatchLocationList;
                pData->PatchLocationListSize = CreateDeviceCopy.PatchLocationListSize;
                Irp->IoStatus.Information = sizeof(D3DKMT_CREATEDEVICE);
            }
            return Status;
        }

        case IOCTL_D3DKMT_DESTROYDEVICE:
        {
            D3DKMT_DESTROYDEVICE *pData;

            if (InputLength < sizeof(D3DKMT_DESTROYDEVICE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pData = (D3DKMT_DESTROYDEVICE *)SystemBuffer;
            Status = DxgkDestroyDevice(pData);
            return Status;
        }

        case IOCTL_D3DKMT_CREATEALLOCATION:
        {
            if (InputLength < sizeof(D3DKMT_CREATEALLOCATION) || OutputLength < sizeof(D3DKMT_CREATEALLOCATION) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkpCreateAllocationWithAccessMode((D3DKMT_CREATEALLOCATION *)SystemBuffer, EmbeddedBufferMode);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_CREATEALLOCATION);
            return Status;
        }

        case IOCTL_D3DKMT_CREATEALLOCATION2:
        {
            if (InputLength < sizeof(D3DKMT_CREATEALLOCATION) || OutputLength < sizeof(D3DKMT_CREATEALLOCATION) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkpCreateAllocation2WithAccessMode((D3DKMT_CREATEALLOCATION *)SystemBuffer, EmbeddedBufferMode);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_CREATEALLOCATION);
            return Status;
        }

        case IOCTL_D3DKMT_DESTROYALLOCATION:
        {
            if (InputLength < sizeof(D3DKMT_DESTROYALLOCATION) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkDestroyAllocation((CONST D3DKMT_DESTROYALLOCATION *)SystemBuffer);
            return Status;
        }

        case IOCTL_D3DKMT_LOCK:
        {
            D3DKMT_LOCK *pLock;
            PDXGKRNL_ADAPTER LockAdapter;
            PDXGKRNL_DEVICE  LockDevice;
            PDXGKVMM_ALLOCATION LockAlloc;
            PVOID LockVa = NULL;
            BOOLEAN UserMappingCaller;

            if (InputLength < sizeof(D3DKMT_LOCK) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pLock = (D3DKMT_LOCK *)SystemBuffer;

            Status = DxgkReferenceOwnedDeviceByHandle(pLock->hDevice, PsGetCurrentProcess(), &LockAdapter, &LockDevice);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("D3DKMTLock: invalid device 0x%X\n", pLock->hDevice);
                return STATUS_INVALID_HANDLE;
            }
            if (!DxgkpDeviceExecutionActive(LockDevice))
            {
                DxgkDereferenceDevice(LockDevice);
                return STATUS_DEVICE_REMOVED;
            }

            Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)pLock->hAllocation, LockAdapter, LockDevice, &LockAlloc);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("D3DKMTLock: invalid alloc 0x%X\n", pLock->hAllocation);
                DxgkDereferenceDevice(LockDevice);
                return STATUS_INVALID_HANDLE;
            }

            /*
             * ReactOS currently has two D3DKMT transport paths:
             *   - win32k WDDM bridge: IRP_MJ_INTERNAL_DEVICE_CONTROL
             *   - direct user runtimes/tests: IRP_MJ_DEVICE_CONTROL from user mode
             *   - CDD/EngDeviceIoControl: IRP_MJ_DEVICE_CONTROL from kernel mode
             *
             * The bridge and direct user callers need a user VA. CDD runs in
             * kernel and needs a system VA for the shadow surface.
             */
            UserMappingCaller = (Stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) || (Irp->RequestorMode == UserMode);
            if (!DxgkBeginKmdTransaction(LockAdapter))
            {
                DxgkVidMmDereferenceAllocation(LockAlloc);
                DxgkDereferenceDevice(LockDevice);
                return STATUS_DELETE_PENDING;
            }
            if (!DxgkpDeviceExecutionActive(LockDevice))
                Status = STATUS_DEVICE_REMOVED;
            else if (UserMappingCaller)
                Status = DxgkVidMmMapAllocationUser(LockAlloc, &LockVa);
            else
                Status = DxgkVidMmMapAllocationCpu(LockAlloc, &LockVa);
            DxgkEndKmdTransaction(LockAdapter);
            DxgkVidMmDereferenceAllocation(LockAlloc);
            DxgkDereferenceDevice(LockDevice);

            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("D3DKMTLock: Map%s failed 0x%08lX\n",
                             UserMappingCaller ? "User" : "Cpu",
                             Status);
                return Status;
            }

            pLock->pData = LockVa;
            if (UserMappingCaller)
                ASSERT((ULONG_PTR)LockVa < (ULONG_PTR)MmSystemRangeStart);
            DXGKRNL_VERBOSE("D3DKMTLock: alloc=0x%X -> VA=%p\n",
                            pLock->hAllocation, LockVa);
            Irp->IoStatus.Information = sizeof(D3DKMT_LOCK);
            return STATUS_SUCCESS;
        }

        case IOCTL_D3DKMT_UNLOCK:
        {
            D3DKMT_UNLOCK *pUnlock;
            PDXGKRNL_ADAPTER UnlockAdapter;
            PDXGKRNL_DEVICE  UnlockDevice;
            BOOLEAN UserMappingCaller;
            UINT ui;

            if (InputLength < sizeof(D3DKMT_UNLOCK) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pUnlock = (D3DKMT_UNLOCK *)SystemBuffer;
            if (pUnlock->NumAllocations == 0 ||
                pUnlock->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT ||
                pUnlock->phAllocations == NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            UserMappingCaller = (Stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) || (Irp->RequestorMode == UserMode);
            Status = DxgkReferenceOwnedDeviceByHandle(pUnlock->hDevice, PsGetCurrentProcess(), &UnlockAdapter, &UnlockDevice);
            if (!NT_SUCCESS(Status))
                return STATUS_INVALID_HANDLE;

            Status = STATUS_SUCCESS;
            _SEH2_TRY
            {
                for (ui = 0; ui < pUnlock->NumAllocations; ui++)
                {
                    PDXGKVMM_ALLOCATION UnlockAlloc;
                    D3DKMT_HANDLE UnlockHandle;
                    BOOLEAN Locked;

                    UnlockHandle = pUnlock->phAllocations[ui];
                    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)UnlockHandle, UnlockAdapter, UnlockDevice, &UnlockAlloc);
                    if (!NT_SUCCESS(Status))
                    {
                        Status = STATUS_INVALID_HANDLE;
                        break;
                    }
                    DXGKRNL_VERBOSE("D3DKMTUnlock:[%u/%u] alloc=0x%X obj=%p bridge=%u\n", ui + 1, pUnlock->NumAllocations, UnlockHandle, UnlockAlloc, UserMappingCaller);

                    if (UserMappingCaller)
                    {
                        (VOID)KeWaitForSingleObject(&UnlockAlloc->UserModeLock, Executive, KernelMode, FALSE, NULL);
                        Locked = UnlockAlloc->UserModeAddress != NULL && UnlockAlloc->UserModeMdl != NULL && UnlockAlloc->UserModeProcess == PsGetCurrentProcess() && UnlockAlloc->UserModeLockCount != 0;
                        KeReleaseMutex(&UnlockAlloc->UserModeLock, FALSE);
                        if (!Locked)
                            Status = STATUS_INVALID_PARAMETER;
                        else
                            DxgkVidMmUnmapAllocationUser(UnlockAlloc);
                    }
                    else if (UnlockAlloc->CpuAddress == NULL)
                        Status = STATUS_INVALID_PARAMETER;
                    else
                        DxgkVidMmUnmapAllocationCpu(UnlockAlloc);

                    DxgkVidMmDereferenceAllocation(UnlockAlloc);
                    if (!NT_SUCCESS(Status))
                        break;
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            DxgkDereferenceDevice(UnlockDevice);
            DXGKRNL_VERBOSE("D3DKMTUnlock: device=0x%X count=%u\n", pUnlock->hDevice, pUnlock->NumAllocations);
            return Status;
        }

        case IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE:
        {
            D3DKMT_GETSHAREDPRIMARYHANDLE *pGSPH;
            if (InputLength < sizeof(D3DKMT_GETSHAREDPRIMARYHANDLE) || SystemBuffer == NULL)
            {
                DXGKRNL_WARN("GETSHAREDPRIMARYHANDLE: buffer too small (%u < %Iu)\n",
                             InputLength, sizeof(D3DKMT_GETSHAREDPRIMARYHANDLE));
                return STATUS_BUFFER_TOO_SMALL;
            }

            pGSPH = (D3DKMT_GETSHAREDPRIMARYHANDLE *)SystemBuffer;
            DXGKRNL_TRACE("GETSHAREDPRIMARYHANDLE dispatch: hAdapter=0x%X VidPn=%u\n",
                          pGSPH->hAdapter, pGSPH->VidPnSourceId);

            Status = DxgkGetSharedPrimaryHandle(pGSPH);

            DXGKRNL_TRACE("GETSHAREDPRIMARYHANDLE dispatch: status=0x%08lX h=0x%X\n",
                          Status, pGSPH->hSharedPrimary);

            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_GETSHAREDPRIMARYHANDLE);
            return Status;
        }

        case IOCTL_D3DKMT_GETSHADOWSURFACE:
        {
            DXGKMT_GETSHADOWSURFACE *pData;

            if (InputLength < sizeof(DXGKMT_GETSHADOWSURFACE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pData = (DXGKMT_GETSHADOWSURFACE *)SystemBuffer;
            Status = DxgkGetShadowSurface(pData);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(DXGKMT_GETSHADOWSURFACE);
            return Status;
        }

        case IOCTL_D3DKMT_QUERYRESOURCEINFO:
        {
            if (InputLength < sizeof(D3DKMT_QUERYRESOURCEINFO) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkQueryResourceInfo(
                         (D3DKMT_QUERYRESOURCEINFO *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_QUERYRESOURCEINFO);
            return Status;
        }

        case IOCTL_D3DKMT_OPENRESOURCE:
        {
            if (InputLength < sizeof(D3DKMT_OPENRESOURCE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkOpenResource((D3DKMT_OPENRESOURCE *)SystemBuffer);
            if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
                Irp->IoStatus.Information = sizeof(D3DKMT_OPENRESOURCE);
            return Status == STATUS_BUFFER_TOO_SMALL ? STATUS_BUFFER_OVERFLOW : Status;
        }

        case IOCTL_D3DKMT_CREATECONTEXTVIRTUAL:
        {
            PRXGK_CREATECONTEXTVIRTUAL_PACKET Packet;
            D3DKMT_CREATECONTEXTVIRTUAL Request;

            if (SystemBuffer == NULL || InputLength < sizeof(RXGK_CREATECONTEXTVIRTUAL_PACKET) || OutputLength < sizeof(RXGK_CREATECONTEXTVIRTUAL_PACKET))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            Packet = (PRXGK_CREATECONTEXTVIRTUAL_PACKET)SystemBuffer;
            Status = DxgkpValidateWddmPrivatePacket(InputLength, Packet->Size, Packet->Version, sizeof(*Packet), Packet->PrivateDriverDataSize, Packet->PrivateDriverDataOffset);
            if (!NT_SUCCESS(Status))
                return Status;

            if (Packet->Reserved != 0 || Packet->DeviceHandle == 0)
            {
                return STATUS_INVALID_PARAMETER;
            }

            if ((Packet->Flags & ~RXGK_CREATECONTEXTVIRTUAL_SUPPORTED_FLAGS) != 0)
            {
                return STATUS_NOT_SUPPORTED;
            }

            RtlZeroMemory(&Request, sizeof(Request));
            Request.hDevice = Packet->DeviceHandle;
            Request.NodeOrdinal = Packet->NodeOrdinal;
            Request.EngineAffinity = Packet->EngineAffinity;
            Request.Flags.Value = Packet->Flags;
            Request.ClientHint = (D3DKMT_CLIENTHINT)Packet->ClientHint;
            Request.PrivateDriverDataSize = Packet->PrivateDriverDataSize;
            if (Packet->PrivateDriverDataSize != 0)
            {
                Request.pPrivateDriverData = (PUCHAR)Packet + Packet->PrivateDriverDataOffset;
            }

            Status = DxgkCreateContextVirtual(&Request);
            if (NT_SUCCESS(Status))
            {
                Packet->ContextHandle = Request.hContext;
                Irp->IoStatus.Information = sizeof(*Packet);
            }
            return Status;
        }

        case IOCTL_D3DKMT_SUBMITCOMMAND:
        {
            PRXGK_SUBMITCOMMAND_PACKET Packet;
            D3DKMT_SUBMITCOMMAND Request;

            if (SystemBuffer == NULL || InputLength < sizeof(RXGK_SUBMITCOMMAND_PACKET))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            Packet = (PRXGK_SUBMITCOMMAND_PACKET)SystemBuffer;
            Status = DxgkpValidateWddmPrivatePacket(InputLength, Packet->Size, Packet->Version, sizeof(*Packet), Packet->PrivateDriverDataSize, Packet->PrivateDriverDataOffset);
            if (!NT_SUCCESS(Status))
                return Status;

            if (Packet->Reserved != 0 || Packet->ContextHandle == 0 || Packet->Commands == 0 || Packet->CommandLength == 0)
            {
                return STATUS_INVALID_PARAMETER;
            }

            if (Packet->PresentHistoryToken != 0 || (Packet->Flags & ~RXGK_SUBMITCOMMAND_SUPPORTED_FLAGS) != 0)
            {
                return STATUS_NOT_SUPPORTED;
            }

            RtlZeroMemory(&Request, sizeof(Request));
            Request.Commands = Packet->Commands;
            Request.CommandLength = Packet->CommandLength;
            Request.Flags.NullRendering = ((Packet->Flags & RXGK_SUBMITCOMMAND_FLAG_NULL_RENDERING) != 0);
            Request.BroadcastContextCount = 1;
            Request.BroadcastContext[0] = Packet->ContextHandle;
            Request.PrivateDriverDataSize = Packet->PrivateDriverDataSize;
            if (Packet->PrivateDriverDataSize != 0)
            {
                Request.pPrivateDriverData = (PUCHAR)Packet + Packet->PrivateDriverDataOffset;
            }

            return DxgkSubmitCommand(&Request);
        }

        case IOCTL_D3DKMT_RENDER:
        {
            if (InputLength < sizeof(D3DKMT_RENDER) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkRender((D3DKMT_RENDER *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_RENDER);
            return Status;
        }

        case IOCTL_D3DKMT_PRESENT:
        {
            /*
             * D3DKMT_PRESENT grows a version-gated tail (WIN8 / WDDM1_3 /
             * WDDM2_0 fields) but dxgkrnl only consumes the base fields
             * (handles, rects, flags, flip interval) that exist at every
             * version.  The user-facing callers (gdi32/win32k/bridge) are
             * pinned to the smaller VISTA ABI, so requiring the full WDDM2_0
             * sizeof here would reject every present with BUFFER_TOO_SMALL.
             * Validate only through the last base field instead.
             */
            /*
             * Validate only through the last field DxgkPresent actually reads
             * (Flags).  Everything DxgkPresent uses — the handles, rects, Color,
             * VidPnSourceId, FlipInterval and Flags — lives before the large
             * BroadcastContext[] array, at offsets that are identical at every
             * DXGKDDI version.  The user-facing callers send a much smaller
             * struct than the kernel's full WDDM2_0 sizeof (e.g. 680 vs 1496),
             * so validating through PresentHistoryToken/sizeof would reject them.
             */
            if (InputLength < RTL_SIZEOF_THROUGH_FIELD(D3DKMT_PRESENT, Flags) ||
                SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkPresent((D3DKMT_PRESENT *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = min(InputLength, OutputLength);
            return Status;
        }

        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT:
        {
            if (InputLength < sizeof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkWaitForSynchronizationObject(
                         (D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *)SystemBuffer);
            return Status;
        }

        case IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT:
        {
            if (InputLength < sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkSignalSynchronizationObject(
                         (D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *)SystemBuffer);
            return Status;
        }

        case IOCTL_D3DKMT_SETDISPLAYMODE:
        {
            if (InputLength < sizeof(D3DKMT_SETDISPLAYMODE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkSetDisplayMode((D3DKMT_SETDISPLAYMODE *)SystemBuffer);
            return Status;
        }

        case IOCTL_D3DKMT_CREATECONTEXT:
        {
            if (InputLength < sizeof(D3DKMT_CREATECONTEXT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkpCreateContextWithAccessMode((D3DKMT_CREATECONTEXT *)SystemBuffer, EmbeddedBufferMode);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_CREATECONTEXT);
            return Status;
        }

        case IOCTL_D3DKMT_DESTROYCONTEXT:
        {
            if (InputLength < sizeof(D3DKMT_DESTROYCONTEXT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkDestroyContext((D3DKMT_DESTROYCONTEXT *)SystemBuffer);
            return Status;
        }

        case IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT:
        {
            if (InputLength < sizeof(D3DKMT_CREATESYNCHRONIZATIONOBJECT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkCreateSynchronizationObject(
                         (D3DKMT_CREATESYNCHRONIZATIONOBJECT *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_CREATESYNCHRONIZATIONOBJECT);
            return Status;
        }

        case IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT:
        {
            if (InputLength < sizeof(D3DKMT_DESTROYSYNCHRONIZATIONOBJECT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkDestroySynchronizationObject(
                         (D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *)SystemBuffer);
            return Status;
        }

        case IOCTL_D3DKMT_ESCAPE:
        {
            if (InputLength < sizeof(D3DKMT_ESCAPE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return DxgkpEscapeWithAccessMode((CONST D3DKMT_ESCAPE *)SystemBuffer, EmbeddedBufferMode);
        }

        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER:
        {
            if (InputLength < sizeof(D3DKMT_SETVIDPNSOURCEOWNER) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkpSetVidPnSourceOwnerWithAccessMode((D3DKMT_SETVIDPNSOURCEOWNER *)SystemBuffer, EmbeddedBufferMode);
            return Status;
        }

        case IOCTL_D3DKMT_GETDEVICESTATE:
        {
            D3DKMT_GETDEVICESTATE *pDevState;

            if (InputLength < sizeof(D3DKMT_GETDEVICESTATE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pDevState = (D3DKMT_GETDEVICESTATE *)SystemBuffer;
            Status = DxgkGetDeviceState(pDevState);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_GETDEVICESTATE);
            return Status;
        }

        case IOCTL_DXGKRNL_PREPAREMAPGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS:
        {
            D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL *pMap;
            PDXGKRNL_ADAPTER Adapter;
            PDXGKRNL_DEVICE Device;
            PDXGKVMM_ALLOCATION Allocation = NULL;
            ULONGLONG MapOffset;
            ULONGLONG MapSize;
            BOOLEAN StateProtection;
            BOOLEAN PrepareOnly = IoControlCode == IOCTL_DXGKRNL_PREPAREMAPGPUVIRTUALADDRESS;

            if (InputLength < sizeof(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL) || OutputLength < sizeof(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;
            if (Stack->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL)
                return STATUS_ACCESS_DENIED;

            pMap = (D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL *)SystemBuffer;
            StateProtection = pMap->Protection.Zero || pMap->Protection.NoAccess;
            if (pMap->hPagingQueue == 0 || pMap->SizeInPages == 0 || pMap->SizeInPages > MAXULONGLONG / DXGKP_GPUVA_PAGE_SIZE || pMap->OffsetInPages > MAXULONGLONG / DXGKP_GPUVA_PAGE_SIZE || pMap->Reserved0 != 0 || pMap->Reserved1 != 0 || pMap->Protection.SystemUseOnly || pMap->Protection.Reserved != 0 || (pMap->Protection.Zero && pMap->Protection.NoAccess) || (StateProtection && (pMap->Protection.Write || pMap->Protection.Execute || pMap->hAllocation != 0)) || (!StateProtection && pMap->hAllocation == 0) || (pMap->BaseAddress & (DXGKP_GPUVA_PAGE_SIZE - 1)) != 0 || (pMap->BaseAddress == 0 && ((pMap->MinimumAddress & (DXGKP_GPUVA_PAGE_SIZE - 1)) != 0 || (pMap->MaximumAddress & (DXGKP_GPUVA_PAGE_SIZE - 1)) != 0 || (pMap->MaximumAddress != 0 && pMap->MinimumAddress >= pMap->MaximumAddress))))
                return STATUS_INVALID_PARAMETER;

            Status = DxgkpReferencePagingQueueDevice(pMap->hPagingQueue, PsGetCurrentProcess(), &Adapter, &Device);
            if (!NT_SUCCESS(Status))
                return Status;
            if (Device->ProcessRecord == NULL)
            {
                DxgkDereferenceDevice(Device);
                return STATUS_NOT_SUPPORTED;
            }

            MapOffset = pMap->OffsetInPages * DXGKP_GPUVA_PAGE_SIZE;
            MapSize = pMap->SizeInPages * DXGKP_GPUVA_PAGE_SIZE;
            if ((pMap->BaseAddress != 0 && pMap->BaseAddress > MAXULONGLONG - MapSize) || (pMap->BaseAddress == 0 && pMap->MaximumAddress != 0 && MapSize > pMap->MaximumAddress - pMap->MinimumAddress))
            {
                DxgkDereferenceDevice(Device);
                return STATUS_INVALID_PARAMETER;
            }
            if (!StateProtection)
            {
                Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)pMap->hAllocation, Adapter, Device, &Allocation);
                if (!NT_SUCCESS(Status))
                {
                    DxgkDereferenceDevice(Device);
                    return STATUS_INVALID_HANDLE;
                }
                if (MapOffset > Allocation->Size || MapSize > Allocation->Size - MapOffset)
                {
                    DxgkVidMmDereferenceAllocation(Allocation);
                    DxgkDereferenceDevice(Device);
                    return STATUS_INVALID_PARAMETER;
                }
            }

            {
                D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection;

                Protection.Value = pMap->Protection.Value;
                Status = PrepareOnly ? DxgkGpuVaPlanMap(Adapter, Device->ProcessRecord, Allocation, MapOffset, pMap->BaseAddress, pMap->MinimumAddress, pMap->MaximumAddress, MapSize, Protection, &pMap->VirtualAddress) : DxgkGpuVaMap(Adapter, Device->ProcessRecord, Allocation, pMap->hAllocation, MapOffset, pMap->BaseAddress, pMap->MinimumAddress, pMap->MaximumAddress, MapSize, Protection, pMap->DriverProtection, &pMap->VirtualAddress);
            }
            if (Allocation != NULL)
                DxgkVidMmDereferenceAllocation(Allocation);
            DxgkDereferenceDevice(Device);
            if (NT_SUCCESS(Status))
            {
                pMap->PagingFenceValue = 0;
                Irp->IoStatus.Information = sizeof(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL);
            }
            return Status;
        }

        case IOCTL_DXGKRNL_PREPARERESERVEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS:
        {
            D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL *pReserve;
            PDXGKRNL_ADAPTER Adapter = NULL;
            PDXGKRNL_DEVICE Device = NULL;
            PDXGKRNL_PROCESS ProcessRecord = NULL;
            BOOLEAN ProcessReferenced = FALSE;
            BOOLEAN AdapterReferenced = FALSE;
            BOOLEAN PrepareOnly = IoControlCode == IOCTL_DXGKRNL_PREPARERESERVEGPUVIRTUALADDRESS;

            if (InputLength < sizeof(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL) || OutputLength < sizeof(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;
            if (Stack->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL)
                return STATUS_ACCESS_DENIED;

            pReserve = (D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL *)SystemBuffer;
            if (pReserve->hPagingQueue == 0 || pReserve->Size == 0 || pReserve->ReservationType > D3DDDIGPUVIRTUALADDRESS_RESERVE_NO_COMMIT)
                return STATUS_INVALID_PARAMETER;

            Status = DxgkpReferencePagingQueueDevice(pReserve->hPagingQueue, PsGetCurrentProcess(), &Adapter, &Device);
            if (NT_SUCCESS(Status))
                ProcessRecord = Device->ProcessRecord;
            else if (Status == STATUS_OBJECT_TYPE_MISMATCH)
            {
                Status = DxgkReferenceAdapterByHandle(pReserve->hAdapter, PsGetCurrentProcess(), &Adapter);
                if (!NT_SUCCESS(Status))
                    return Status;
                AdapterReferenced = TRUE;
                Status = DxgkReferenceProcessRecordByAdapter(Adapter, PsGetCurrentProcess(), &ProcessRecord);
                if (!NT_SUCCESS(Status))
                {
                    DxgkDereferenceAdapter(Adapter);
                    return Status;
                }
                ProcessReferenced = TRUE;
            }
            else
                return Status;

            if (!Adapter->GpuMmuCapsValid || Adapter->GpuMmuCaps.PageTableUpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
                Status = STATUS_NOT_SUPPORTED;
            else if (ProcessRecord == NULL)
                Status = STATUS_NOT_SUPPORTED;
            else if ((Device != NULL && pReserve->ReservationType > D3DDDIGPUVIRTUALADDRESS_RESERVE_ZERO) || (Device == NULL && (pReserve->ReservationType != 0 || pReserve->DriverProtection != 0 || pReserve->PagingFenceValue != 0)))
                Status = STATUS_INVALID_PARAMETER;
            else
                Status = PrepareOnly ? DxgkGpuVaPlanReserve(ProcessRecord, pReserve->BaseAddress, pReserve->MinimumAddress, pReserve->MaximumAddress, pReserve->Size, (D3DDDIGPUVIRTUALADDRESS_RESERVATION_TYPE)pReserve->ReservationType, &pReserve->VirtualAddress) : DxgkGpuVaReserve(ProcessRecord, pReserve->BaseAddress, pReserve->MinimumAddress, pReserve->MaximumAddress, pReserve->Size, (D3DDDIGPUVIRTUALADDRESS_RESERVATION_TYPE)pReserve->ReservationType, pReserve->DriverProtection, &pReserve->VirtualAddress);

            if (Device != NULL)
                DxgkDereferenceDevice(Device);
            if (ProcessReferenced)
                DxgkDereferenceProcessRecord(ProcessRecord);
            if (AdapterReferenced)
                DxgkDereferenceAdapter(Adapter);
            if (NT_SUCCESS(Status))
            {
                pReserve->PagingFenceValue = 0;
                Irp->IoStatus.Information = sizeof(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL);
            }
            return Status;
        }

        case IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS:
        {
            D3DKMT_FREEGPUVIRTUALADDRESS_LOCAL *pFree;
            PDXGKRNL_ADAPTER Adapter;
            PDXGKRNL_PROCESS ProcessRecord;

            if (InputLength < sizeof(D3DKMT_FREEGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pFree = (D3DKMT_FREEGPUVIRTUALADDRESS_LOCAL *)SystemBuffer;
            Adapter = DxgkLookupAdapterByHandle(pFree->hAdapter);
            if (Adapter == NULL)
                return STATUS_INVALID_HANDLE;
            if (!Adapter->GpuMmuCapsValid || Adapter->GpuMmuCaps.PageTableUpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL)
            {
                DxgkDereferenceAdapter(Adapter);
                return STATUS_NOT_SUPPORTED;
            }

            Status = DxgkReferenceProcessRecordByAdapter(Adapter, PsGetCurrentProcess(), &ProcessRecord);
            if (!NT_SUCCESS(Status))
            {
                DxgkDereferenceAdapter(Adapter);
                return Status;
            }
            Status = DxgkGpuVaFree(ProcessRecord, pFree->BaseAddress, pFree->Size);
            DxgkDereferenceProcessRecord(ProcessRecord);
            DxgkDereferenceAdapter(Adapter);
            return Status;
        }

        case IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS:
        {
            D3DKMT_UPDATEGPUVIRTUALADDRESS_LOCAL *pUpdate;
            D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operations;
            PDXGKRNL_ADAPTER Adapter;
            PDXGKRNL_DEVICE Device;
            PDXGKRNL_CONTEXT VirtualContext;
            SIZE_T OperationsSize;

            if (InputLength < sizeof(D3DKMT_UPDATEGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pUpdate = (D3DKMT_UPDATEGPUVIRTUALADDRESS_LOCAL *)SystemBuffer;
            if (pUpdate->hDevice == 0 || pUpdate->hContext == 0 || pUpdate->NumOperations == 0 || pUpdate->NumOperations > DXGKP_MAX_D3DKMT_LIST_COUNT || pUpdate->Operations == NULL || pUpdate->Reserved0 != 0 || pUpdate->Reserved1 != 0 || pUpdate->Flags.Reserved != 0)
                return STATUS_INVALID_PARAMETER;

            Status = DxgkReferenceVirtualContextByHandle(pUpdate->hContext, PsGetCurrentProcess(), &Adapter, &Device, &VirtualContext);
            if (!NT_SUCCESS(Status))
                return Status;
            if (!DxgkpDeviceExecutionActive(Device))
            {
                DxgkDereferenceContext(VirtualContext);
                return STATUS_DEVICE_REMOVED;
            }
            if (Device->Handle != pUpdate->hDevice || Device->ProcessRecord == NULL)
            {
                DxgkDereferenceContext(VirtualContext);
                return STATUS_INVALID_HANDLE;
            }
            if (pUpdate->hFenceObject != 0)
            {
                DxgkDereferenceContext(VirtualContext);
                return STATUS_NOT_SUPPORTED;
            }
            if (pUpdate->FenceValue != 0)
            {
                DxgkDereferenceContext(VirtualContext);
                return STATUS_INVALID_PARAMETER;
            }

            OperationsSize = sizeof(*Operations) * (SIZE_T)pUpdate->NumOperations;
            Operations = ExAllocatePoolWithTag(PagedPool, OperationsSize, TAG_DXGK_GPUVA);
            if (Operations == NULL)
            {
                DxgkDereferenceContext(VirtualContext);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Status = DxgkpCopyFromUserBuffer(Operations, pUpdate->Operations, OperationsSize, EmbeddedBufferMode);

            if (NT_SUCCESS(Status))
                Status = DxgkGpuVaApplyUpdate(Adapter, Device->ProcessRecord, Operations, pUpdate->NumOperations);
            ExFreePoolWithTag(Operations, TAG_DXGK_GPUVA);
            DxgkDereferenceContext(VirtualContext);
            return Status;
        }

        case IOCTL_D3DKMT_MAKERESIDENT:
        {   /* Priorities are applied request-wide before any residency
             * transition, so a rejected priority cannot leave a partially
             * repriorized list behind. */
            D3DDDI_MAKERESIDENT_LOCAL *pMakeResident;
            PDXGKRNL_ADAPTER Adapter;
            PDXGKRNL_DEVICE Device;
            PDXGKRNL_PAGING_QUEUE PagingQueue = NULL;
            D3DKMT_HANDLE *AllocationList = NULL;
            UINT *PriorityList = NULL;
            SIZE_T AllocationListSize;
            ULONGLONG PagingFenceValue = 0;
            ULONG Completed = 0;
            UINT i;

            if (InputLength < sizeof(D3DDDI_MAKERESIDENT_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pMakeResident = (D3DDDI_MAKERESIDENT_LOCAL *)SystemBuffer;
            if (pMakeResident->hPagingQueue == 0 || pMakeResident->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT || (pMakeResident->NumAllocations != 0 && pMakeResident->AllocationList == NULL) || pMakeResident->Flags.Reserved != 0 || (pMakeResident->Flags.MustSucceed && !pMakeResident->Flags.CantTrimFurther))
                return STATUS_INVALID_PARAMETER;
            Status = DxgkpReferencePagingQueueForPaging(pMakeResident->hPagingQueue, PsGetCurrentProcess(), &Adapter, &Device, &PagingQueue);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pMakeResident->NumAllocations != 0)
            {
                AllocationListSize = sizeof(*AllocationList) * (SIZE_T)pMakeResident->NumAllocations;
                AllocationList = ExAllocatePoolWithTag(PagedPool, AllocationListSize, TAG_DXGK_GPUVA);
                if (AllocationList == NULL)
                {
                    DxgkpDereferencePagingQueue(PagingQueue);
                    DxgkDereferenceDevice(Device);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                if (pMakeResident->PriorityList != NULL)
                {
                    PriorityList = ExAllocatePoolWithTag(PagedPool, sizeof(*PriorityList) * (SIZE_T)pMakeResident->NumAllocations, TAG_DXGK_GPUVA);
                    if (PriorityList == NULL)
                    {
                        ExFreePoolWithTag(AllocationList, TAG_DXGK_GPUVA);
                        DxgkpDereferencePagingQueue(PagingQueue);
                        DxgkDereferenceDevice(Device);
                        return STATUS_INSUFFICIENT_RESOURCES;
                    }
                }

                Status = DxgkpCopyFromUserBuffer(AllocationList, pMakeResident->AllocationList, AllocationListSize, EmbeddedBufferMode);
                if (NT_SUCCESS(Status) && PriorityList != NULL)
                    Status = DxgkpCopyFromUserBuffer(PriorityList, pMakeResident->PriorityList, sizeof(*PriorityList) * (SIZE_T)pMakeResident->NumAllocations, EmbeddedBufferMode);

                if (NT_SUCCESS(Status) && PriorityList != NULL)
                {
                    for (i = 0; i < pMakeResident->NumAllocations; ++i)
                    {
                        PDXGKVMM_ALLOCATION Allocation;

                        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)AllocationList[i], Adapter, Device, &Allocation);
                        if (!NT_SUCCESS(Status))
                        {
                            Status = STATUS_INVALID_HANDLE;
                            break;
                        }
                        if (PriorityList[i] < D3DDDI_ALLOCATIONPRIORITY_MINIMUM || PriorityList[i] > D3DDDI_ALLOCATIONPRIORITY_MAXIMUM)
                            Status = STATUS_INVALID_PARAMETER;
                        DxgkVidMmDereferenceAllocation(Allocation);
                        if (!NT_SUCCESS(Status))
                            break;
                    }
                }

                if (NT_SUCCESS(Status) && PriorityList != NULL)
                    Status = DxgkpApplyMakeResidentPriorities(Adapter, Device, AllocationList, PriorityList, pMakeResident->NumAllocations);
                if (NT_SUCCESS(Status))
                    Status = DxgkGpuVaMakeResident(Adapter, Device->ProcessRecord, Device, PagingQueue->hSyncObject, &PagingQueue->LastQueuedFence, AllocationList, pMakeResident->NumAllocations, &Completed, &pMakeResident->NumBytesToTrim, &PagingFenceValue);
            }
            else
            {
                Status = STATUS_SUCCESS;
                pMakeResident->NumBytesToTrim = 0;
            }

            /* STATUS_PENDING is an API status, not an IRP completion status:
             * the IRP completes successfully and the nonzero paging fence in
             * the output tells the bridge to surface PENDING to the caller. */
            if (Status == STATUS_PENDING)
                Status = STATUS_SUCCESS;
            pMakeResident->PagingFenceValue = PagingFenceValue;
            pMakeResident->NumAllocations = Completed;
            if (PriorityList != NULL)
                ExFreePoolWithTag(PriorityList, TAG_DXGK_GPUVA);
            if (AllocationList != NULL)
                ExFreePoolWithTag(AllocationList, TAG_DXGK_GPUVA);
            DxgkpDereferencePagingQueue(PagingQueue);
            DxgkDereferenceDevice(Device);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DDDI_MAKERESIDENT_LOCAL);
            return Status;
        }

        case IOCTL_D3DKMT_EVICT:
        {
            D3DKMT_EVICT_LOCAL *pEvict;
            PDXGKRNL_ADAPTER Adapter;
            PDXGKRNL_DEVICE Device;
            D3DKMT_HANDLE *AllocationList = NULL;
            SIZE_T AllocationListSize;

            if (InputLength < sizeof(D3DKMT_EVICT_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pEvict = (D3DKMT_EVICT_LOCAL *)SystemBuffer;
            if (pEvict->hDevice == 0 || pEvict->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT || (pEvict->NumAllocations != 0 && pEvict->AllocationList == NULL) || pEvict->Flags.Reserved != 0)
                return STATUS_INVALID_PARAMETER;

            Status = DxgkReferenceOwnedDeviceByHandle(pEvict->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
            if (!NT_SUCCESS(Status))
                return Status;
            if (!DxgkpDeviceExecutionActive(Device))
            {
                DxgkDereferenceDevice(Device);
                return STATUS_DEVICE_REMOVED;
            }

            pEvict->NumBytesToTrim = 0;
            if (pEvict->NumAllocations == 0)
            {
                DxgkDereferenceDevice(Device);
                Irp->IoStatus.Information = sizeof(D3DKMT_EVICT_LOCAL);
                return STATUS_SUCCESS;
            }

            AllocationListSize = sizeof(*AllocationList) * (SIZE_T)pEvict->NumAllocations;
            AllocationList = ExAllocatePoolWithTag(PagedPool, AllocationListSize, TAG_DXGK_GPUVA);
            if (AllocationList == NULL)
            {
                DxgkDereferenceDevice(Device);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            Status = DxgkpCopyFromUserBuffer(AllocationList, pEvict->AllocationList, AllocationListSize, EmbeddedBufferMode);

            if (NT_SUCCESS(Status))
            {
                UINT i;

                for (i = 0; i < pEvict->NumAllocations; ++i)
                {
                    PDXGKVMM_ALLOCATION Allocation;

                    Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)AllocationList[i], Adapter, Device, &Allocation);
                    if (!NT_SUCCESS(Status))
                    {
                        Status = STATUS_INVALID_HANDLE;
                        break;
                    }
                    DxgkVidMmDereferenceAllocation(Allocation);
                }
            }

            if (NT_SUCCESS(Status))
                Status = DxgkGpuVaEvict(Adapter, Device->ProcessRecord, Device, AllocationList, pEvict->NumAllocations, pEvict->Flags.EvictOnlyIfNecessary != 0, &pEvict->NumBytesToTrim);

            ExFreePoolWithTag(AllocationList, TAG_DXGK_GPUVA);
            DxgkDereferenceDevice(Device);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_EVICT_LOCAL);
            return Status;
        }

        /* ---- Stub handlers for remaining D3DKMT entry points --------------- */

        case IOCTL_D3DKMT_SETALLOCATIONPRIORITY:
        case IOCTL_D3DKMT_GETALLOCATIONPRIORITY:
        case IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY:
        {
            D3DKMT_QUERYALLOCATIONRESIDENCY *pResidency;

            if (IoControlCode == IOCTL_D3DKMT_SETALLOCATIONPRIORITY)
            {
                D3DKMT_SETALLOCATIONPRIORITY *pPriority;

                if (InputLength < sizeof(D3DKMT_SETALLOCATIONPRIORITY) || SystemBuffer == NULL)
                    return STATUS_BUFFER_TOO_SMALL;

                pPriority = (D3DKMT_SETALLOCATIONPRIORITY *)SystemBuffer;
                if (pPriority->AllocationCount != 0 &&
                    (pPriority->phAllocationList == NULL ||
                     pPriority->pPriorities == NULL))
                {
                    return STATUS_INVALID_PARAMETER;
                }

                return DxgkSetAllocationPriorityWithAccessMode(pPriority, EmbeddedBufferMode);
            }

            if (IoControlCode == IOCTL_D3DKMT_GETALLOCATIONPRIORITY)
            {
                if (InputLength < sizeof(D3DKMT_GETALLOCATIONPRIORITY) || SystemBuffer == NULL)
                    return STATUS_BUFFER_TOO_SMALL;

                return DxgkGetAllocationPriorityWithAccessMode((CONST D3DKMT_GETALLOCATIONPRIORITY *)SystemBuffer, EmbeddedBufferMode);
            }

            if (InputLength < sizeof(D3DKMT_QUERYALLOCATIONRESIDENCY) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pResidency = (D3DKMT_QUERYALLOCATIONRESIDENCY *)SystemBuffer;
            return DxgkQueryAllocationResidencyWithAccessMode(pResidency, EmbeddedBufferMode);
        }

        case IOCTL_D3DKMT_CHECKSHAREDRESOURCEACCESS:
        {
            if (InputLength < sizeof(D3DKMT_CHECKSHAREDRESOURCEACCESS) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_GETPRESENTHISTORY:
        {
            D3DKMT_GETPRESENTHISTORY *pHistory;

            if (InputLength < sizeof(D3DKMT_GETPRESENTHISTORY) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pHistory = (D3DKMT_GETPRESENTHISTORY *)SystemBuffer;
            if (!NT_SUCCESS(DxgkpValidateAdapterOnlyForIoctl(pHistory->hAdapter)))
                return STATUS_INVALID_HANDLE;

            pHistory->WrittenSize = 0;
            pHistory->NumTokens = 0;
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_GETPRESENTQUEUEEVENT:
        {
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP:
        {
            D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *pCheck;

            if (InputLength < sizeof(D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pCheck = (D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *)SystemBuffer;
            return DxgkCheckVidPnExclusiveOwnership(pCheck);
        }

        case IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT:
        {
            D3DKMT_WAITFORVERTICALBLANKEVENT *pVBlank;

            if (InputLength < sizeof(D3DKMT_WAITFORVERTICALBLANKEVENT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pVBlank = (D3DKMT_WAITFORVERTICALBLANKEVENT *)SystemBuffer;
            return DxgkWaitForVerticalBlankEvent(pVBlank);
        }

        case IOCTL_D3DKMT_GETSCANLINE:
        {
            D3DKMT_GETSCANLINE *pScanLine;

            if (InputLength < sizeof(D3DKMT_GETSCANLINE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pScanLine = (D3DKMT_GETSCANLINE *)SystemBuffer;
            Status = DxgkGetScanLine(pScanLine);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_GETSCANLINE);
            return Status;
        }

        case IOCTL_D3DKMT_CHECKMONITORPOWERSTATE:
        {
            D3DKMT_CHECKMONITORPOWERSTATE *pPowerState;

            if (InputLength < sizeof(D3DKMT_CHECKMONITORPOWERSTATE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pPowerState = (D3DKMT_CHECKMONITORPOWERSTATE *)SystemBuffer;
            return DxgkCheckMonitorPowerState(pPowerState);
        }

        case IOCTL_D3DKMT_CHECKOCCLUSION:
        {
            if (InputLength < sizeof(D3DKMT_CHECKOCCLUSION) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            /* Route to the handler (this IS the path gdi32->win32k->bridge IOCTL
             * takes; it previously hardcoded NOT_SUPPORTED, bypassing the
             * handler and failing the displayext occlusion parity test). */
            return DxgkCheckOcclusion((CONST D3DKMT_CHECKOCCLUSION *)SystemBuffer);
        }

        case IOCTL_D3DKMT_CHECKEXCLUSIVEOWNERSHIP:
        {
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_WAITFORIDLE:
        {
            D3DKMT_WAITFORIDLE *pWait;

            if (InputLength < sizeof(D3DKMT_WAITFORIDLE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pWait = (D3DKMT_WAITFORIDLE *)SystemBuffer;
            return DxgkWaitForIdle(pWait);
        }

        case IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY:
        case IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY:
        {
            if (IoControlCode == IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY)
            {
                D3DKMT_SETCONTEXTSCHEDULINGPRIORITY *pPriority;

                if (InputLength < sizeof(D3DKMT_SETCONTEXTSCHEDULINGPRIORITY) || SystemBuffer == NULL)
                    return STATUS_BUFFER_TOO_SMALL;

                pPriority = (D3DKMT_SETCONTEXTSCHEDULINGPRIORITY *)SystemBuffer;
                return DxgkSetContextSchedulingPriority(pPriority);
            }
            else
            {
                D3DKMT_GETCONTEXTSCHEDULINGPRIORITY *pPriority;

                if (InputLength < sizeof(D3DKMT_GETCONTEXTSCHEDULINGPRIORITY) || SystemBuffer == NULL)
                    return STATUS_BUFFER_TOO_SMALL;

                pPriority = (D3DKMT_GETCONTEXTSCHEDULINGPRIORITY *)SystemBuffer;
                Status = DxgkGetContextSchedulingPriority(pPriority);
                if (NT_SUCCESS(Status))
                    Irp->IoStatus.Information = sizeof(D3DKMT_GETCONTEXTSCHEDULINGPRIORITY);
                return Status;
            }
        }

        case IOCTL_D3DKMT_SETPROCESSSCHEDULINGPRIORITYCLASS:
        case IOCTL_D3DKMT_GETPROCESSSCHEDULINGPRIORITYCLASS:
        {
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_RELEASEPROCESSVIDPNSOURCEOWNERS:
        {
            if (InputLength < sizeof(HANDLE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;
            return DxgkReleaseProcessVidPnSourceOwners(*(HANDLE *)SystemBuffer);
        }

        case IOCTL_D3DKMT_SETQUEUEDLIMIT:
        {
            D3DKMT_SETQUEUEDLIMIT *pQueuedLimit;

            if (InputLength < sizeof(D3DKMT_SETQUEUEDLIMIT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pQueuedLimit = (D3DKMT_SETQUEUEDLIMIT *)SystemBuffer;
            if (!NT_SUCCESS(DxgkpValidateDeviceHandleForIoctl(pQueuedLimit->hDevice, NULL, NULL)))
                return STATUS_INVALID_HANDLE;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_SETGAMMARAMP:
        {
            D3DKMT_SETGAMMARAMP *pGammaRamp;

            if (InputLength < sizeof(D3DKMT_SETGAMMARAMP) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pGammaRamp = (D3DKMT_SETGAMMARAMP *)SystemBuffer;
            Status = DxgkpValidateDeviceHandleForIoctl(pGammaRamp->hDevice,
                                                       NULL,
                                                       NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_GETMULTISAMPLEMETHODLIST:
        {
            D3DKMT_GETMULTISAMPLEMETHODLIST *pMethods;

            if (InputLength < sizeof(D3DKMT_GETMULTISAMPLEMETHODLIST) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pMethods = (D3DKMT_GETMULTISAMPLEMETHODLIST *)SystemBuffer;
            Status = DxgkpValidateAdapterVidPnSourceForIoctl(pMethods->hAdapter,
                                                             pMethods->VidPnSourceId,
                                                             NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            pMethods->MethodCount = 0;
            Irp->IoStatus.Information = sizeof(D3DKMT_GETMULTISAMPLEMETHODLIST);
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_GETRUNTIMEDATA:
        {
            D3DKMT_GETRUNTIMEDATA *pRuntimeData;

            if (InputLength < sizeof(D3DKMT_GETRUNTIMEDATA) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pRuntimeData = (D3DKMT_GETRUNTIMEDATA *)SystemBuffer;
            if (!NT_SUCCESS(DxgkpValidateAdapterOnlyForIoctl(pRuntimeData->hAdapter)))
                return STATUS_INVALID_HANDLE;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_QUERYSTATISTICS:
        {
            if (InputLength < sizeof(D3DKMT_QUERYSTATISTICS) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_CREATEDCFROMMEMORY:
        case IOCTL_D3DKMT_DESTROYDCFROMMEMORY:
        {
            /* GDI DC-from-memory interop: not yet implemented */
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION:
        case IOCTL_D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION:
        {
            if (IoControlCode == IOCTL_D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION)
            {
                if (InputLength < sizeof(D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION) || SystemBuffer == NULL)
                    return STATUS_BUFFER_TOO_SMALL;
            }
            else
            {
                if (InputLength < sizeof(D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION) || SystemBuffer == NULL)
                    return STATUS_BUFFER_TOO_SMALL;
            }

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT:
        {
            D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT *pFormat;

            if (InputLength < sizeof(D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pFormat = (D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT *)SystemBuffer;
            Status = DxgkpValidateDeviceHandleForIoctl(pFormat->hDevice,
                                                       NULL,
                                                       NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_INVALIDATEACTIVEVIDPN:
        {
            D3DKMT_INVALIDATEACTIVEVIDPN *pInvalidate;

            if (InputLength < sizeof(D3DKMT_INVALIDATEACTIVEVIDPN) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pInvalidate = (D3DKMT_INVALIDATEACTIVEVIDPN *)SystemBuffer;
            if (!NT_SUCCESS(DxgkpValidateAdapterOnlyForIoctl(pInvalidate->hAdapter)))
                return STATUS_INVALID_HANDLE;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_POLLDISPLAYCHILDREN:
        {
            D3DKMT_POLLDISPLAYCHILDREN *pPoll;

            if (InputLength < sizeof(D3DKMT_POLLDISPLAYCHILDREN) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pPoll = (D3DKMT_POLLDISPLAYCHILDREN *)SystemBuffer;
            return DxgkPollDisplayChildren(pPoll);
        }

        case IOCTL_D3DKMT_CREATEKEYEDMUTEX:
        case IOCTL_D3DKMT_DESTROYKEYEDMUTEX:
        case IOCTL_D3DKMT_OPENKEYEDMUTEX:
        case IOCTL_D3DKMT_ACQUIREKEYEDMUTEX:
        case IOCTL_D3DKMT_RELEASEKEYEDMUTEX:
        {
            /* Keyed mutex: not yet implemented */
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_CREATEOVERLAY:
        case IOCTL_D3DKMT_DESTROYOVERLAY:
        case IOCTL_D3DKMT_FLIPOVERLAY:
        case IOCTL_D3DKMT_UPDATEOVERLAY:
        case IOCTL_D3DKMT_GETOVERLAYSTATE:
        {
            /* Overlay: not yet implemented */
            return STATUS_NOT_SUPPORTED;
        }

        /* ---- WDDM 1.2 stubs ------------------------------------------------ */

        case IOCTL_D3DKMT_OFFERALLOCATIONS:
        {
            D3DKMT_OFFERALLOCATIONS *pOffer;

            if (InputLength < sizeof(D3DKMT_OFFERALLOCATIONS) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pOffer = (D3DKMT_OFFERALLOCATIONS *)SystemBuffer;
            return DxgkOfferAllocations(pOffer);
        }

        case IOCTL_D3DKMT_RECLAIMALLOCATIONS:
        {
            D3DKMT_RECLAIMALLOCATIONS *pReclaim;

            if (InputLength < sizeof(D3DKMT_RECLAIMALLOCATIONS) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pReclaim = (D3DKMT_RECLAIMALLOCATIONS *)SystemBuffer;
            return DxgkReclaimAllocations(pReclaim);
        }

        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER1:
        {
            D3DKMT_SETVIDPNSOURCEOWNER1 *pOwner1;

            if (InputLength < sizeof(D3DKMT_SETVIDPNSOURCEOWNER1) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pOwner1 = (D3DKMT_SETVIDPNSOURCEOWNER1 *)SystemBuffer;
            Status = DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(&pOwner1->Version0, pOwner1->Flags.Value, EmbeddedBufferMode);
            return Status;
        }

        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER2:
        {
            D3DKMT_SETVIDPNSOURCEOWNER2 *pOwner2;

            if (InputLength < sizeof(D3DKMT_SETVIDPNSOURCEOWNER2) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pOwner2 = (D3DKMT_SETVIDPNSOURCEOWNER2 *)SystemBuffer;
            /*
             * The NT-handle form needs a real DispMgr authorization object
             * type to validate each entry against; until that type exists
             * the path stays gated rather than accepting unverified proofs.
             */
            if (pOwner2->pVidPnSourceNtHandles != NULL || pOwner2->Version1.Flags.UseNtHandles)
                return STATUS_NOT_SUPPORTED;
            Status = DxgkpSetVidPnSourceOwnerWithFlagsAndAccessMode(&pOwner2->Version1.Version0, pOwner2->Version1.Flags.Value, EmbeddedBufferMode);
            return Status;
        }

        case IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT2:
        {
            D3DKMT_WAITFORVERTICALBLANKEVENT2 *pVBlank2;

            if (InputLength < sizeof(D3DKMT_WAITFORVERTICALBLANKEVENT2) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pVBlank2 = (D3DKMT_WAITFORVERTICALBLANKEVENT2 *)SystemBuffer;
            Status = DxgkpValidateAdapterVidPnSourceForIoctl(pVBlank2->hAdapter,
                                                             pVBlank2->VidPnSourceId,
                                                             NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pVBlank2->hDevice != 0)
            {
                Status = DxgkpValidateDeviceHandleForIoctl(pVBlank2->hDevice, NULL, NULL);
                if (!NT_SUCCESS(Status))
                    return Status;
            }

            if (pVBlank2->NumObjects > D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS)
                return STATUS_INVALID_PARAMETER;

            return DxgkWaitForVerticalBlankEvent2(pVBlank2);
        }

        case IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT2:
        {
            if (InputLength < sizeof(D3DKMT_CREATESYNCHRONIZATIONOBJECT2) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkCreateSynchronizationObject2(
                         (D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_CREATESYNCHRONIZATIONOBJECT2);
            return Status;
        }

        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2:
        {
            if (InputLength < sizeof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return DxgkWaitForSynchronizationObject2(
                       (CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 *)SystemBuffer);
        }

        case IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2:
        {
            if (InputLength < sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return DxgkSignalSynchronizationObject2(
                       (CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *)SystemBuffer);
        }

        case IOCTL_D3DKMT_CREATEPAGINGQUEUE:
        {
            if (InputLength < sizeof(D3DKMT_CREATEPAGINGQUEUE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkCreatePagingQueue((D3DKMT_CREATEPAGINGQUEUE *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_CREATEPAGINGQUEUE);
            return Status;
        }

        case IOCTL_D3DKMT_DESTROYPAGINGQUEUE:
        {
            if (InputLength < sizeof(D3DDDI_DESTROYPAGINGQUEUE) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return DxgkDestroyPagingQueue((D3DDDI_DESTROYPAGINGQUEUE *)SystemBuffer);
        }

        case IOCTL_D3DKMT_QUERYVIDEOMEMORYINFO:
        {
            if (InputLength < sizeof(D3DKMT_QUERYVIDEOMEMORYINFO) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkQueryVideoMemoryInfo((D3DKMT_QUERYVIDEOMEMORYINFO *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_QUERYVIDEOMEMORYINFO);
            return Status;
        }

        case IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU:
        {
            if (InputLength < sizeof(D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return DxgkSignalSynchronizationObjectFromCpu((CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *)SystemBuffer, EmbeddedBufferMode);
        }

        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU:
        {
            if (InputLength < sizeof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return DxgkWaitForSynchronizationObjectFromCpu((CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *)SystemBuffer, EmbeddedBufferMode);
        }

        case IOCTL_DXGKRNL_EXCHANGE_INTERFACE:
        {
            /*
             * Private kernel-to-kernel IOCTL for the win32k <-> dxgkrnl
             * callback table exchange protocol.
             *
             * win32k sends a DXGKRNL_INTERFACE_EXCHANGE_IN describing its
             * expected version and buffer size.  dxgkrnl validates and fills
             * the output buffer with a REACTOS_WIN32K_DXGKRNL_INTERFACE
             * populated with function pointers to the IOCTL-forwarding stubs.
             *
             * Since the actual D3DKMT handlers live inside dxgkrnl's IOCTL
             * dispatch, the callbacks returned here are thin shims that call
             * WddmBridgeSendIoctl (win32k side).  However, dxgkrnl returns
             * a "magic" filled structure so win32k knows the exchange succeeded
             * and that all callbacks can be populated.
            */
            PDXGKRNL_INTERFACE_EXCHANGE_IN pExchangeIn;
            PREACTOS_WIN32K_DXGKRNL_INTERFACE pInterface;
            ULONG Version;
            ULONG InterfaceSize;

            if (Stack->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL || Irp->RequestorMode != KernelMode)
                return STATUS_ACCESS_DENIED;
            if (InputLength < sizeof(DXGKRNL_INTERFACE_EXCHANGE_IN) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pExchangeIn = (PDXGKRNL_INTERFACE_EXCHANGE_IN)SystemBuffer;
            Version = pExchangeIn->Version;

            if (Version == DXGKRNL_INTERFACE_VERSION_1)
                InterfaceSize = DXGKRNL_INTERFACE_VERSION_1_SIZE;
            else if (Version == DXGKRNL_INTERFACE_VERSION_2)
                InterfaceSize = DXGKRNL_INTERFACE_VERSION_2_SIZE;
            else if (Version == DXGKRNL_INTERFACE_VERSION_3)
                InterfaceSize = DXGKRNL_INTERFACE_VERSION_3_SIZE;
            else if (Version == DXGKRNL_INTERFACE_VERSION_4)
                InterfaceSize = DXGKRNL_INTERFACE_VERSION_4_SIZE;
            else
            {
                DXGKRNL_WARN("IOCTL_DXGKRNL_EXCHANGE_INTERFACE: "
                             "unsupported version %lu\n", Version);
                return STATUS_NOT_SUPPORTED;
            }

            if (pExchangeIn->Size < InterfaceSize || OutputLength < InterfaceSize)
            {
                DXGKRNL_WARN("IOCTL_DXGKRNL_EXCHANGE_INTERFACE: interface buffers %lu/%lu too small (need %lu)\n", pExchangeIn->Size, OutputLength, InterfaceSize);
                return STATUS_BUFFER_TOO_SMALL;
            }

            /*
             * The output buffer overlaps the input buffer for METHOD_BUFFERED.
             * Fill the interface structure at the SystemBuffer location with
             * dxgkrnl's D3DKMT handler function pointers.  win32k uses these
             * to call directly into dxgkrnl without going through IOCTLs.
             */
            pInterface = (PREACTOS_WIN32K_DXGKRNL_INTERFACE)SystemBuffer;
            RtlZeroMemory(pInterface, InterfaceSize);

            /* Populate the interface with dxgkrnl's D3DKMT handlers */
            pInterface->RxgkIntPfnPresent            = (PDXGADAPTER_PRESENT)DxgkPresent;
            pInterface->RxgkIntPfnQueryAdapterInfo    = (PDXGADAPTER_QUERYADAPTERINFO)DxgkQueryAdapterInfo;
            pInterface->RxgkIntPfnRender              = (PDXGADAPTER_RENDER)DxgkRender;
            pInterface->RxgkIntPfnCreateAllocation    = (PDXGADAPTER_CREATEALLOCATION)DxgkCreateAllocation;
            pInterface->RxgkIntPfnCloseAdapter        = (PDXGADAPTER_CLOSEADAPTER)DxgkCloseAdapter;
            pInterface->RxgkIntPfnCreateContext        = (PDXGADAPTER_CREATECONTEXT)DxgkCreateContext;
            pInterface->RxgkIntPfnCreateDevice         = (PDXGADAPTER_CREATEDEVICE)DxgkCreateDevice;
            pInterface->RxgkIntPfnDestroyContext        = (PDXGADAPTER_DESTROYCONTEXT)DxgkDestroyContext;
            pInterface->RxgkIntPfnDestroyDevice        = (PDXGADAPTER_DESTROYDEVICE)DxgkDestroyDevice;
            pInterface->RxgkIntPfnDestroyAllocation    = (PDXGADAPTER_DESTROYALLOCATION)DxgkDestroyAllocation;
            pInterface->RxgkIntPfnGetDisplayModeList   = (PDXGADAPTER_GETDISPLAYMODELIST)DxgkGetDisplayModeList;
            pInterface->RxgkIntPfnSetDisplayMode       = (PDXGADAPTER_SETDISPLAYMODE)DxgkSetDisplayMode;
            pInterface->RxgkIntPfnSetVidPnSourceOwner  = (PDXGADAPTER_SETVIDPNSOURCEOWNER)DxgkSetVidPnSourceOwner;
            pInterface->RxgkIntPfnGetSharedPrimaryHandle = (PDXGADAPTER_GETSHAREDPRIMARYHANDLE)DxgkGetSharedPrimaryHandle;
            pInterface->RxgkIntPfnQueryResourceInfo    = (PDXGADAPTER_QUERYRESOURCEINFO)DxgkQueryResourceInfo;
            pInterface->RxgkIntPfnOpenResource         = (PDXGADAPTER_OPENRESOURCE)DxgkOpenResource;
            pInterface->RxgkIntPfnEscape               = (PDXGADAPTER_ESCAPE)DxgkEscape;
            pInterface->RxgkIntPfnCreateSynchronizationObject = (PDXGADAPTER_CREATESYNCHRONIZATIONOBJECT)DxgkCreateSynchronizationObject;
            pInterface->RxgkIntPfnDestroySynchronizationObject = (PDXGADAPTER_DESTROYSYNCHRONIZATIONOBJECT)DxgkDestroySynchronizationObject;
            pInterface->RxgkIntPfnSignalSynchronizationObject = (PDXGADAPTER_SIGNALSYNCHRONIZATIONOBJECT)DxgkSignalSynchronizationObject;
            pInterface->RxgkIntPfnWaitForSynchronizationObject = (PDXGADAPTER_WAITFORSYNCHRONIZATIONOBJECT)DxgkWaitForSynchronizationObject;
            pInterface->RxgkIntPfnLock                 = (PDXGADAPTER_LOCK)DxgkLock;
            pInterface->RxgkIntPfnUnlock               = (PDXGADAPTER_UNLOCK)DxgkUnlock;
            pInterface->RxgkIntPfnCheckMonitorPowerState = (PDXGADAPTER_CHECKMONITORPOWERSTATE)DxgkCheckMonitorPowerState;
            pInterface->RxgkIntPfnCheckOcclusion       = (PDXGADAPTER_CHECKOCCLUSION)DxgkCheckOcclusion;
            pInterface->RxgkIntPfnCreateOverlay        = (PDXGADAPTER_CREATEOVERLAY)DxgkCreateOverlay;
            pInterface->RxgkIntPfnDestroyOverlay       = (PDXGADAPTER_DESTROYOVERLAY)DxgkDestroyOverlay;
            pInterface->RxgkIntPfnFlipOverlay          = (PDXGADAPTER_FLIPOVERLAY)DxgkFlipOverlay;
            pInterface->RxgkIntPfnUpdateOverlay        = (PDXGADAPTER_UPDATEOVERLAY)DxgkUpdateOverlay;
            pInterface->RxgkIntPfnGetContextSchedulingPriority = (PDXGADAPTER_GETCONTEXTSCHEDULINGPRIORITY)DxgkGetContextSchedulingPriority;
            pInterface->RxgkIntPfnGetDeviceState       = (PDXGADAPTER_GETDEVICESTATE)DxgkGetDeviceState;
            pInterface->RxgkIntPfnSetContextSchedulingPriority = (PDXGADAPTER_SETCONTEXTSCHEDULINGPRIORITY)DxgkSetContextSchedulingPriority;
            pInterface->RxgkIntPfnGetMultisampleMethodList = (PDXGADAPTER_GETMULTISAMPLEMETHODLIST)DxgkGetMultisampleMethodList;
            pInterface->RxgkIntPfnGetPresentHistory    = (PDXGADAPTER_GETPRESENTHISTORY)DxgkGetPresentHistory;
            pInterface->RxgkIntPfnGetRuntimeData       = (PDXGADAPTER_GETRUNTIMEDATA)DxgkGetRuntimeData;
            pInterface->RxgkIntPfnGetScanLine          = (PDXGADAPTER_GETSCANLINE)DxgkGetScanLine;
            pInterface->RxgkIntPfnInvalidateActiveVidPn = (PDXGADAPTER_INVALIDATEACTIVEVIDPN)DxgkInvalidateActiveVidPn;
            pInterface->RxgkIntPfnPollDisplayChildren  = (PDXGADAPTER_POLLDISPLAYCHILDREN)DxgkPollDisplayChildren;
            pInterface->RxgkIntPfnQueryAllocationResidency = (PDXGADAPTER_QUERYALLOCATIONRESIDENCY)DxgkQueryAllocationResidency;
            pInterface->RxgkIntPfnQueryStatistics      = (PDXGADAPTER_QUERYSTATISTICS)DxgkQueryStatistics;
            pInterface->RxgkIntPfnReleaseProcessVidPnSourceOwners = (PDXGADAPTER_RELEASEPROCESSVIDPNSOURCEOWNERS)DxgkReleaseProcessVidPnSourceOwners;
            pInterface->RxgkIntPfnSetAllocationPriority = (PDXGADAPTER_SETALLOCATIONPRIORITY)DxgkSetAllocationPriority;
            pInterface->RxgkIntPfnSetDisplayPrivateDriverFormat = (PDXGADAPTER_SETDISPLAYPRIVATEDRIVERFORMAT)DxgkSetDisplayPrivateDriverFormat;
            pInterface->RxgkIntPfnSetGammaRamp         = (PDXGADAPTER_SETGAMMARAMP)DxgkSetGammaRamp;
            pInterface->RxgkIntPfnSetQueuedLimit       = (PDXGADAPTER_SETQUEUEDLIMIT)DxgkSetQueuedLimit;
            pInterface->RxgkIntPfnWaitForIdle          = (PDXGADAPTER_WAITFORIDLE)DxgkWaitForIdle;
            pInterface->RxgkIntPfnWaitForVerticalBlankEvent = (PDXGADAPTER_WAITFORVERTICALBLANKEVENT)DxgkWaitForVerticalBlankEvent;
            pInterface->RxgkIntPfnEnumAdapters         = (PDXGADAPTER_ENUMADAPTERS)DxgkEnumAdapters;
            pInterface->RxgkIntPfnOpenAdapterFromLuid  = (PDXGADAPTER_OPENADAPTERFROMLUID)DxgkOpenAdapterFromLuid;
            pInterface->RxgkIntPfnOfferAllocations     = (PDXGADAPTER_OFFERALLOCATIONS)DxgkOfferAllocations;
            pInterface->RxgkIntPfnReclaimAllocations   = (PDXGADAPTER_RECLAIMALLOCATIONS)DxgkReclaimAllocations;
            pInterface->RxgkIntPfnSetVidPnSourceOwner1 = (PDXGADAPTER_SETVIDPNSOURCEOWNER1)DxgkSetVidPnSourceOwner1;
            pInterface->RxgkIntPfnWaitForVerticalBlankEvent2 = (PDXGADAPTER_WAITFORVERTICALBLANKEVENT2)DxgkWaitForVerticalBlankEvent2;
            pInterface->RxgkIntPfnCreateSynchronizationObject2 = (PDXGADAPTER_CREATESYNCHRONIZATIONOBJECT2)DxgkCreateSynchronizationObject2;
            pInterface->RxgkIntPfnWaitForSynchronizationObject2 = (PDXGADAPTER_WAITFORSYNCHRONIZATIONOBJECT2)DxgkWaitForSynchronizationObject2;
            pInterface->RxgkIntPfnSignalSynchronizationObject2 = (PDXGADAPTER_SIGNALSYNCHRONIZATIONOBJECT2)DxgkSignalSynchronizationObject2;
            pInterface->RxgkIntPfnQueryVideoMemoryInfo = (PDXGADAPTER_QUERYVIDEOMEMORYINFO)DxgkQueryVideoMemoryInfo;
            pInterface->RxgkIntPfnCreatePagingQueue    = (PDXGADAPTER_CREATEPAGINGQUEUE)DxgkCreatePagingQueue;
            pInterface->RxgkIntPfnDestroyPagingQueue   = (PDXGADAPTER_DESTROYPAGINGQUEUE)DxgkDestroyPagingQueue;

            if (Version >= DXGKRNL_INTERFACE_VERSION_2)
            {
                pInterface->RxgkIntPfnCreateContextVirtual = (PDXGADAPTER_CREATECONTEXTVIRTUAL)DxgkCreateContextVirtual;
                pInterface->RxgkIntPfnSubmitCommand = (PDXGADAPTER_SUBMITCOMMAND)DxgkSubmitCommand;
            }
            if (Version >= DXGKRNL_INTERFACE_VERSION_3)
                pInterface->RxgkIntPfnCreateAllocation2 = (PDXGADAPTER_CREATEALLOCATION2)DxgkCreateAllocation2;
            if (Version >= DXGKRNL_INTERFACE_VERSION_4)
                pInterface->RxgkIntPfnGetAllocationPriority = (PDXGADAPTER_GETALLOCATIONPRIORITY)DxgkGetAllocationPriority;

            Irp->IoStatus.Information = InterfaceSize;

            DXGKRNL_TRACE("IOCTL_DXGKRNL_EXCHANGE_INTERFACE: exchange successful (v%lu), %lu bytes populated\n", Version, InterfaceSize);
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/* ========================================================================
 * DxgkDispatchDeviceControl
 *
 * IRP_MJ_DEVICE_CONTROL handler.  Dispatches D3DKMT IOCTLs to the
 * appropriate handler function.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS           Status;

    /*
     * Route \Device\Video0 IOCTLs (IOCTL_VIDEO_*) to the display handler.
     * The display handler completes the IRP and returns TRUE.
     */
    if (DxgkDisplayDispatchIoctl(DeviceObject, Irp))
        return STATUS_SUCCESS;

    DXGKRNL_VERBOSE("DxgkDispatchDeviceControl: IoControlCode=0x%lX\n",
                    Stack->Parameters.DeviceIoControl.IoControlCode);

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY:
        case IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY:
        case IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY:
        case IOCTL_DXGKRNL_GET_UNINIT_ENTRY:
        {
            PVOID *OutputPtr = (PVOID *)Irp->UserBuffer;
            PVOID EntryPoint;

            if (Stack->MajorFunction != IRP_MJ_INTERNAL_DEVICE_CONTROL || Irp->RequestorMode != KernelMode)
            {
                Status = STATUS_ACCESS_DENIED;
                Irp->IoStatus.Information = 0;
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_DXGKRNL_GET_DOD_INIT_ENTRY)
                EntryPoint = (PVOID)DxgkInitializeDisplayOnlyDriver;
            else if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_DXGKRNL_GET_LEGACY_FULL_INIT_ENTRY || Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_DXGKRNL_GET_FULL_INIT_ENTRY)
                EntryPoint = (PVOID)DxgkInitialize;
            else
                EntryPoint = (PVOID)DxgkUnInitialize;
            if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PVOID) || OutputPtr == NULL)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Irp->IoStatus.Information = 0;
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            _SEH2_TRY
            {
                *OutputPtr = EntryPoint;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
                Irp->IoStatus.Information = 0;
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            _SEH2_END;
            DXGKRNL_TRACE("DxgkDispatchDeviceControl: resolved private entry 0x%lX to %p\n", Stack->Parameters.DeviceIoControl.IoControlCode, EntryPoint);
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = sizeof(PVOID);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        /* D3DKMT buffered IOCTLs */
        case IOCTL_D3DKMT_ENUMADAPTERS:
        case IOCTL_D3DKMT_ENUMADAPTERS2:
        case IOCTL_D3DKMT_OPENADAPTERFROMLUID:
        case IOCTL_D3DKMT_CLOSEADAPTER:
        case IOCTL_D3DKMT_QUERYADAPTERINFO:
        case IOCTL_D3DKMT_GETDISPLAYMODELIST:
        case IOCTL_D3DKMT_OPENADAPTERFROMHDC:
        case IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME:
        case IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME:
        case IOCTL_D3DKMT_CREATEDEVICE:
        case IOCTL_D3DKMT_DESTROYDEVICE:
        case IOCTL_D3DKMT_CREATEALLOCATION:
        case IOCTL_D3DKMT_CREATEALLOCATION2:
        case IOCTL_D3DKMT_DESTROYALLOCATION:
        case IOCTL_D3DKMT_LOCK:
        case IOCTL_D3DKMT_UNLOCK:
        case IOCTL_D3DKMT_RENDER:
        case IOCTL_D3DKMT_PRESENT:
        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_SETDISPLAYMODE:
        case IOCTL_D3DKMT_CREATECONTEXT:
        case IOCTL_D3DKMT_CREATECONTEXTVIRTUAL:
        case IOCTL_D3DKMT_DESTROYCONTEXT:
        case IOCTL_D3DKMT_SUBMITCOMMAND:
        case IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_ESCAPE:
        case IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE:
        case IOCTL_D3DKMT_GETSHADOWSURFACE:
        case IOCTL_D3DKMT_QUERYRESOURCEINFO:
        case IOCTL_D3DKMT_OPENRESOURCE:
        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER:
        case IOCTL_D3DKMT_GETDEVICESTATE:
        case IOCTL_DXGKRNL_PREPAREMAPGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS:
        case IOCTL_DXGKRNL_PREPARERESERVEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_MAKERESIDENT:
        case IOCTL_D3DKMT_EVICT:
        /* Remaining D3DKMT stubs (full Win7 coverage) */
        case IOCTL_D3DKMT_SETALLOCATIONPRIORITY:
        case IOCTL_D3DKMT_GETALLOCATIONPRIORITY:
        case IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY:
        case IOCTL_D3DKMT_CHECKSHAREDRESOURCEACCESS:
        case IOCTL_D3DKMT_GETPRESENTHISTORY:
        case IOCTL_D3DKMT_GETPRESENTQUEUEEVENT:
        case IOCTL_D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP:
        case IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT:
        case IOCTL_D3DKMT_GETSCANLINE:
        case IOCTL_D3DKMT_CHECKMONITORPOWERSTATE:
        case IOCTL_D3DKMT_CHECKOCCLUSION:
        case IOCTL_D3DKMT_CHECKEXCLUSIVEOWNERSHIP:
        case IOCTL_D3DKMT_WAITFORIDLE:
        case IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY:
        case IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY:
        case IOCTL_D3DKMT_SETPROCESSSCHEDULINGPRIORITYCLASS:
        case IOCTL_D3DKMT_GETPROCESSSCHEDULINGPRIORITYCLASS:
        case IOCTL_D3DKMT_RELEASEPROCESSVIDPNSOURCEOWNERS:
        case IOCTL_D3DKMT_SETQUEUEDLIMIT:
        case IOCTL_D3DKMT_SETGAMMARAMP:
        case IOCTL_D3DKMT_GETMULTISAMPLEMETHODLIST:
        case IOCTL_D3DKMT_GETRUNTIMEDATA:
        case IOCTL_D3DKMT_QUERYSTATISTICS:
        case IOCTL_D3DKMT_CREATEDCFROMMEMORY:
        case IOCTL_D3DKMT_DESTROYDCFROMMEMORY:
        case IOCTL_D3DKMT_SHAREDPRIMARYLOCKNOTIFICATION:
        case IOCTL_D3DKMT_SHAREDPRIMARYUNLOCKNOTIFICATION:
        case IOCTL_D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT:
        case IOCTL_D3DKMT_INVALIDATEACTIVEVIDPN:
        case IOCTL_D3DKMT_POLLDISPLAYCHILDREN:
        case IOCTL_D3DKMT_CREATEKEYEDMUTEX:
        case IOCTL_D3DKMT_DESTROYKEYEDMUTEX:
        case IOCTL_D3DKMT_OPENKEYEDMUTEX:
        case IOCTL_D3DKMT_ACQUIREKEYEDMUTEX:
        case IOCTL_D3DKMT_RELEASEKEYEDMUTEX:
        case IOCTL_D3DKMT_CREATEOVERLAY:
        case IOCTL_D3DKMT_DESTROYOVERLAY:
        case IOCTL_D3DKMT_FLIPOVERLAY:
        case IOCTL_D3DKMT_UPDATEOVERLAY:
        case IOCTL_D3DKMT_GETOVERLAYSTATE:
        /* WDDM 1.2 additions */
        case IOCTL_D3DKMT_OFFERALLOCATIONS:
        case IOCTL_D3DKMT_RECLAIMALLOCATIONS:
        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER1:
        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER2:
        case IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT2:
        case IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT2:
        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2:
        case IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2:
        /* WDDM 2.0 paging queue / video memory */
        case IOCTL_D3DKMT_CREATEPAGINGQUEUE:
        case IOCTL_D3DKMT_DESTROYPAGINGQUEUE:
        case IOCTL_D3DKMT_QUERYVIDEOMEMORYINFO:
        case IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU:
        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU:
        case IOCTL_DXGKRNL_EXCHANGE_INTERFACE:
        {
            if (Stack->MajorFunction == IRP_MJ_DEVICE_CONTROL && Irp->RequestorMode == UserMode)
            {
                Irp->IoStatus.Status = STATUS_ACCESS_DENIED;
                Irp->IoStatus.Information = 0;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_ACCESS_DENIED;
            }
            Status = DxgkpDispatchBufferedIoctl(Irp, Stack);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;
        }

        default:
            DXGKRNL_WARN("DxgkDispatchDeviceControl: unknown IOCTL 0x%lX\n",
                         Stack->Parameters.DeviceIoControl.IoControlCode);
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
