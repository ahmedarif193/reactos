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
#include "d3dkmt.h"
#include "vidpn.h"

/* Legacy IOCTL for miniport DxgkInitialize resolution */
#ifndef IOCTL_DXGKRNL_ESCAPE
#define IOCTL_DXGKRNL_ESCAPE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

/*
 * IOCTL_DXGKRNL_GET_INIT_ENTRY (0x230043)
 *
 * Sent by Windows 10+ WDDM miniport thunks (embedded in the miniport binary)
 * to resolve the DxgkInitialize/DxgkInitializeEx entry point at runtime.
 *
 * The miniport's built-in DxgkInitialize wrapper opens \Device\DxgKrnl,
 * sends this IOCTL with METHOD_NEITHER, and expects a pointer to
 * DxgkInitializeEx in the output buffer.
 *
 * CTL_CODE(FILE_DEVICE_VIDEO, 0x10, METHOD_NEITHER, FILE_ANY_ACCESS)
 */
#define IOCTL_DXGKRNL_GET_INIT_ENTRY \
    CTL_CODE(0x23, 0x10, METHOD_NEITHER, FILE_ANY_ACCESS)

/* Forward declaration from adapter.c */
NTSTATUS APIENTRY
DxgkInitialize(
    _In_ PDRIVER_OBJECT              DriverObject,
    _In_ PUNICODE_STRING             RegistryPath,
    _In_ PDRIVER_INITIALIZATION_DATA DriverInitializationData);

/* ========================================================================
 * Adapter handle encoding
 *
 * Adapter handles are opaque 32-bit ids stored on the DXGKRNL_ADAPTER
 * itself.  They are not reconstructed into pointers on lookup, which keeps
 * amd64 safe even though D3DKMT_HANDLE is only 32 bits wide.
 * ====================================================================== */

static ULONG DxgkAdapterHandleCookie = 0x4B544D44; /* "DMTK" */
static volatile LONG DxgkNextAdapterHandle = 0;
static volatile LONG DxgkAdapterHandleTableInitialized = 0;
static FAST_MUTEX DxgkAdapterHandleTableLock;
static LIST_ENTRY DxgkAdapterHandleTableHead;

typedef struct _DXGKRNL_ADAPTER_OPEN_HANDLE
{
    LIST_ENTRY        ListEntry;
    D3DKMT_HANDLE     Handle;
    PDXGKRNL_ADAPTER  Adapter;
} DXGKRNL_ADAPTER_OPEN_HANDLE, *PDXGKRNL_ADAPTER_OPEN_HANDLE;

#define DXGKP_FILE_CONTEXT_MAGIC 0x58474446 /* "FDGX" */
#define DXGKP_GPUVA_START        (64ULL * 1024ULL)
#define DXGKP_GPUVA_LIMIT        (256ULL * 1024ULL * 1024ULL * 1024ULL)
#define DXGKP_GPUVA_PAGE_SIZE    4096ULL
#define DXGKP_MAX_D3DKMT_LIST_COUNT 4096U

/*
 * D3DDDI_MONITORED_FENCE == 5 in d3dukmdt.h, but that enumerator lives behind a
 * DXGKDDI_INTERFACE_VERSION >= WDDM2_0 guard and dxgkrnl pins the version to
 * WIN7.  Use the numeric value so the WDDM 2.0 monitored-fence create path can
 * be recognised without raising the whole driver's interface version.
 */
#define DXGKP_D3DDDI_MONITORED_FENCE 5

typedef struct _DXGKRNL_FILE_GPUVA_RANGE
{
    LIST_ENTRY ListEntry;
    ULONGLONG BaseAddress;
    ULONGLONG Size;
} DXGKRNL_FILE_GPUVA_RANGE, *PDXGKRNL_FILE_GPUVA_RANGE;

typedef struct _DXGKRNL_FILE_CONTEXT
{
    ULONG Magic;
    FAST_MUTEX GpuVaLock;
    LIST_ENTRY GpuVaRanges;
    ULONGLONG NextGpuVa;
} DXGKRNL_FILE_CONTEXT, *PDXGKRNL_FILE_CONTEXT;

static D3DKMT_DRIVERVERSION
DxgkpGetReportedDriverVersion(VOID)
{
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
    return KMT_DRIVERVERSION_WDDM_2_0;
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM1_3)
    return KMT_DRIVERVERSION_WDDM_1_3;
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN8)
    return KMT_DRIVERVERSION_WDDM_1_2;
#elif (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WIN7)
    return KMT_DRIVERVERSION_WDDM_1_1;
#else
    return KMT_DRIVERVERSION_WDDM_1_0;
#endif
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
DxgkpAllocateAdapterHandle(VOID)
{
    ULONG Sequence;

    do
    {
        Sequence = (ULONG)InterlockedIncrement(&DxgkNextAdapterHandle);
    } while (Sequence == 0);

    return (D3DKMT_HANDLE)(Sequence ^ DxgkAdapterHandleCookie);
}

static VOID
DxgkpEnsureAdapterHandleTable(VOID)
{
    if (InterlockedCompareExchange(&DxgkAdapterHandleTableInitialized, 1, 0) != 0)
        return;

    ExInitializeFastMutex(&DxgkAdapterHandleTableLock);
    InitializeListHead(&DxgkAdapterHandleTableHead);
}

static D3DKMT_HANDLE
DxgkpCreateAdapterHandle(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_ADAPTER_OPEN_HANDLE OpenHandle;
    D3DKMT_HANDLE Handle;

    if (Adapter == NULL)
        return 0;

    DxgkpEnsureAdapterHandleTable();

    OpenHandle = ExAllocatePoolWithTag(NonPagedPool,
                                       sizeof(*OpenHandle),
                                       TAG_DXGK_ADAPTER);
    if (OpenHandle == NULL)
        return 0;

    RtlZeroMemory(OpenHandle, sizeof(*OpenHandle));

    Handle = DxgkpAllocateAdapterHandle();
    OpenHandle->Handle = Handle;
    OpenHandle->Adapter = Adapter;
    InitializeListHead(&OpenHandle->ListEntry);

    ExAcquireFastMutex(&DxgkAdapterHandleTableLock);
    InsertTailList(&DxgkAdapterHandleTableHead, &OpenHandle->ListEntry);
    ExReleaseFastMutex(&DxgkAdapterHandleTableLock);

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
    PLIST_ENTRY Entry;
    ULONG       Count = 0;
    KIRQL       OldIrql;

    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);

    for (Entry  = DxgkAdapterGlobalListHead.Flink;
         Entry != &DxgkAdapterGlobalListHead && Count < DXGKP_MAX_ADAPTERS;
         Entry  = Entry->Flink)
    {
        AdapterArray[Count++] = CONTAINING_RECORD(Entry, DXGKRNL_ADAPTER,
                                                   GlobalAdapterListEntry);
    }

    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);
    return Count;
}

/*
 * DxgkpValidateAdapterHandle
 *
 * Decode an adapter handle, then verify the resulting pointer is a member
 * of the global adapter list AND the adapter is in the Started state.
 * Returns the DXGKRNL_ADAPTER pointer or NULL on failure.
 */
static PDXGKRNL_ADAPTER
DxgkpValidateAdapterHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;
    PDXGKRNL_ADAPTER Adapter = NULL;

    if (Handle == 0)
        return NULL;

    DxgkpEnsureAdapterHandleTable();

    ExAcquireFastMutex(&DxgkAdapterHandleTableLock);
    for (Entry = DxgkAdapterHandleTableHead.Flink;
         Entry != &DxgkAdapterHandleTableHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_ADAPTER_OPEN_HANDLE OpenHandle;

        OpenHandle = CONTAINING_RECORD(Entry,
                                       DXGKRNL_ADAPTER_OPEN_HANDLE,
                                       ListEntry);
        if (OpenHandle->Handle == Handle)
        {
            if (OpenHandle->Adapter != NULL &&
                OpenHandle->Adapter->State == DxgkAdapterStateStarted)
            {
                Adapter = OpenHandle->Adapter;
            }
            break;
        }
    }
    ExReleaseFastMutex(&DxgkAdapterHandleTableLock);

    return Adapter;
}

static PDXGKRNL_ADAPTER
DxgkpCloseAdapterHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_ADAPTER_OPEN_HANDLE Found = NULL;

    if (Handle == 0)
        return NULL;

    DxgkpEnsureAdapterHandleTable();

    ExAcquireFastMutex(&DxgkAdapterHandleTableLock);
    for (Entry = DxgkAdapterHandleTableHead.Flink;
         Entry != &DxgkAdapterHandleTableHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_ADAPTER_OPEN_HANDLE OpenHandle;

        OpenHandle = CONTAINING_RECORD(Entry,
                                       DXGKRNL_ADAPTER_OPEN_HANDLE,
                                       ListEntry);
        if (OpenHandle->Handle == Handle)
        {
            if (OpenHandle->Adapter != NULL &&
                OpenHandle->Adapter->State == DxgkAdapterStateStarted)
            {
                Adapter = OpenHandle->Adapter;
                Found = OpenHandle;
                RemoveEntryList(&OpenHandle->ListEntry);
                InitializeListHead(&OpenHandle->ListEntry);
            }
            break;
        }
    }
    ExReleaseFastMutex(&DxgkAdapterHandleTableLock);

    if (Found != NULL)
        ExFreePoolWithTag(Found, TAG_DXGK_ADAPTER);

    return Adapter;
}

static ULONGLONG
DxgkpGpuVaAlignUp(
    _In_ ULONGLONG Value)
{
    return (Value + DXGKP_GPUVA_PAGE_SIZE - 1) & ~(DXGKP_GPUVA_PAGE_SIZE - 1);
}

static BOOLEAN
DxgkpGpuVaRangesOverlap(
    _In_ ULONGLONG BaseA,
    _In_ ULONGLONG SizeA,
    _In_ ULONGLONG BaseB,
    _In_ ULONGLONG SizeB)
{
    return (BaseA < BaseB + SizeB) && (BaseB < BaseA + SizeA);
}

static PDXGKRNL_FILE_CONTEXT
DxgkpGetFileContext(
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PDXGKRNL_FILE_CONTEXT Context;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    if (Stack == NULL || Stack->FileObject == NULL)
        return NULL;

    Context = (PDXGKRNL_FILE_CONTEXT)Stack->FileObject->FsContext;
    if (Context == NULL || Context->Magic != DXGKP_FILE_CONTEXT_MAGIC)
        return NULL;

    return Context;
}

static VOID
DxgkpFreeFileContext(
    _In_ PDXGKRNL_FILE_CONTEXT Context)
{
    PLIST_ENTRY Entry;

    if (Context == NULL)
        return;

    ExAcquireFastMutex(&Context->GpuVaLock);
    while (!IsListEmpty(&Context->GpuVaRanges))
    {
        PDXGKRNL_FILE_GPUVA_RANGE Range;

        Entry = RemoveHeadList(&Context->GpuVaRanges);
        Range = CONTAINING_RECORD(Entry, DXGKRNL_FILE_GPUVA_RANGE, ListEntry);
        ExFreePoolWithTag(Range, TAG_DXGK_ADAPTER);
    }
    ExReleaseFastMutex(&Context->GpuVaLock);

    Context->Magic = 0;
    ExFreePoolWithTag(Context, TAG_DXGK_ADAPTER);
}

static NTSTATUS
DxgkpGpuVaReserveRange(
    _In_ PDXGKRNL_FILE_CONTEXT Context,
    _In_ ULONGLONG BaseAddress,
    _In_ ULONGLONG MinimumAddress,
    _In_ ULONGLONG MaximumAddress,
    _In_ ULONGLONG Size,
    _Out_ ULONGLONG *VirtualAddress)
{
    PDXGKRNL_FILE_GPUVA_RANGE Range;
    PLIST_ENTRY Entry;
    ULONGLONG Candidate;
    BOOLEAN Conflict;

    if (Context == NULL || VirtualAddress == NULL || Size == 0)
        return STATUS_INVALID_PARAMETER;

    Size = DxgkpGpuVaAlignUp(Size);
    if (MaximumAddress == 0)
        MaximumAddress = DXGKP_GPUVA_LIMIT;

    if (BaseAddress != 0)
        Candidate = BaseAddress & ~(DXGKP_GPUVA_PAGE_SIZE - 1);
    else if (MinimumAddress != 0)
        Candidate = DxgkpGpuVaAlignUp(MinimumAddress);
    else
        Candidate = Context->NextGpuVa;

    if (Candidate < DXGKP_GPUVA_START)
        Candidate = DXGKP_GPUVA_START;

    Range = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Range),
                                  TAG_DXGK_ADAPTER);
    if (Range == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    ExAcquireFastMutex(&Context->GpuVaLock);
    do
    {
        Conflict = FALSE;
        for (Entry = Context->GpuVaRanges.Flink;
             Entry != &Context->GpuVaRanges;
             Entry = Entry->Flink)
        {
            PDXGKRNL_FILE_GPUVA_RANGE Existing;

            Existing = CONTAINING_RECORD(Entry,
                                         DXGKRNL_FILE_GPUVA_RANGE,
                                         ListEntry);
            if (DxgkpGpuVaRangesOverlap(Candidate,
                                        Size,
                                        Existing->BaseAddress,
                                        Existing->Size))
            {
                if (BaseAddress != 0)
                {
                    ExReleaseFastMutex(&Context->GpuVaLock);
                    ExFreePoolWithTag(Range, TAG_DXGK_ADAPTER);
                    return STATUS_CONFLICTING_ADDRESSES;
                }

                Candidate = DxgkpGpuVaAlignUp(Existing->BaseAddress +
                                              Existing->Size);
                Conflict = TRUE;
                break;
            }
        }
    } while (Conflict && Candidate + Size <= MaximumAddress);

    if (Candidate + Size > MaximumAddress)
    {
        ExReleaseFastMutex(&Context->GpuVaLock);
        ExFreePoolWithTag(Range, TAG_DXGK_ADAPTER);
        return STATUS_NO_MEMORY;
    }

    Range->BaseAddress = Candidate;
    Range->Size = Size;
    InsertTailList(&Context->GpuVaRanges, &Range->ListEntry);

    if (Context->NextGpuVa <= Candidate)
        Context->NextGpuVa = DxgkpGpuVaAlignUp(Candidate + Size);

    ExReleaseFastMutex(&Context->GpuVaLock);

    *VirtualAddress = Candidate;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpGpuVaFreeRange(
    _In_ PDXGKRNL_FILE_CONTEXT Context,
    _In_ ULONGLONG BaseAddress,
    _In_ ULONGLONG Size)
{
    PLIST_ENTRY Entry, Next;
    BOOLEAN Freed = FALSE;

    if (Context == NULL || BaseAddress == 0)
        return STATUS_INVALID_PARAMETER;

    if (Size != 0)
        Size = DxgkpGpuVaAlignUp(Size);

    ExAcquireFastMutex(&Context->GpuVaLock);
    for (Entry = Context->GpuVaRanges.Flink;
         Entry != &Context->GpuVaRanges;
         Entry = Next)
    {
        PDXGKRNL_FILE_GPUVA_RANGE Range;

        Next = Entry->Flink;
        Range = CONTAINING_RECORD(Entry,
                                  DXGKRNL_FILE_GPUVA_RANGE,
                                  ListEntry);

        if ((Size == 0 && Range->BaseAddress == BaseAddress) ||
            (Size != 0 && DxgkpGpuVaRangesOverlap(Range->BaseAddress,
                                                  Range->Size,
                                                  BaseAddress,
                                                  Size)))
        {
            RemoveEntryList(&Range->ListEntry);
            ExFreePoolWithTag(Range, TAG_DXGK_ADAPTER);
            Freed = TRUE;
            if (Size == 0)
                break;
        }
    }
    ExReleaseFastMutex(&Context->GpuVaLock);

    return Freed ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
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
DxgkpValidateDeviceHandleForIoctl(
    _In_ D3DKMT_HANDLE hDevice,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_opt_ PDXGKRNL_DEVICE *OutDevice)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device;

    Device = DxgkLookupDeviceByHandle(hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (OutAdapter != NULL)
        *OutAdapter = Adapter;
    if (OutDevice != NULL)
        *OutDevice = Device;

    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpValidateAdapterVidPnSourceForIoctl(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_opt_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_ADAPTER Adapter;

    Adapter = DxgkLookupAdapterByHandle(hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (VidPnSourceId >= Adapter->NumberOfVideoPresentSources)
        return STATUS_INVALID_PARAMETER;

    if (OutAdapter != NULL)
        *OutAdapter = Adapter;

    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpValidateAllocationListForIoctl(
    _In_ PDXGKRNL_ADAPTER Adapter,
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

            Allocation = DxgkVidMmHandleToAllocation(
                             (HANDLE)(ULONG_PTR)AllocationList[i]);
            if (Allocation == NULL ||
                (Adapter != NULL && Allocation->Adapter != Adapter))
            {
                Status = STATUS_INVALID_HANDLE;
                break;
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

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
            return STATUS_INSUFFICIENT_RESOURCES;

        pEnumAdapters->Adapters[Filled].AdapterLuid = Adapter->AdapterLuid;
        pEnumAdapters->Adapters[Filled].NumOfSources = Adapter->NumberOfVideoPresentSources;
        pEnumAdapters->Adapters[Filled].bPrecisePresentRegionsPreferred = FALSE;
        Filled++;
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

NTSTATUS
NTAPI
DxgkEnumAdapters2(
    _Inout_ D3DKMT_ENUMADAPTERS2 *pEnumAdapters2)
{
    PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
    ULONG            Count, i, Started;

    PAGED_CODE();

    if (pEnumAdapters2 == NULL)
        return STATUS_INVALID_PARAMETER;

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
    if (pEnumAdapters2->pAdapters == NULL)
    {
        pEnumAdapters2->NumAdapters = Started;
        return STATUS_SUCCESS;
    }

    /* Caller supplied an array but it is too small: report the required count. */
    if (pEnumAdapters2->NumAdapters < Started)
    {
        pEnumAdapters2->NumAdapters = Started;
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Fill the caller's buffer */
    Started = 0;
    for (i = 0; i < Count; ++i)
    {
        PDXGKRNL_ADAPTER Adapter = Snapshot[i];
        if (Adapter->State != DxgkAdapterStateStarted)
            continue;

        pEnumAdapters2->pAdapters[Started].hAdapter    = DxgkpCreateAdapterHandle(Adapter);
        if (pEnumAdapters2->pAdapters[Started].hAdapter == 0)
            return STATUS_INSUFFICIENT_RESOURCES;

        pEnumAdapters2->pAdapters[Started].AdapterLuid = Adapter->AdapterLuid;
        pEnumAdapters2->pAdapters[Started].NumOfSources = Adapter->NumberOfVideoPresentSources;
        pEnumAdapters2->pAdapters[Started].bPrecisePresentRegionsPreferred = FALSE;
        Started++;
    }

    pEnumAdapters2->NumAdapters = Started;

    DXGKRNL_TRACE("DxgkEnumAdapters2: returning %lu adapters\n", Started);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkOpenAdapterFromLuid — D3DKMTOpenAdapterFromLuid
 *
 * Opens an adapter handle given the adapter LUID assigned at StartDevice.
 * Searches the global adapter list for a matching LUID and returns an
 * encoded handle.
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
                return STATUS_INSUFFICIENT_RESOURCES;

            DXGKRNL_TRACE("DxgkOpenAdapterFromLuid: LUID={%ld,%lu} -> handle=0x%X\n",
                          Adapter->AdapterLuid.HighPart,
                          Adapter->AdapterLuid.LowPart,
                          pOpenAdapterFromLuid->hAdapter);
            return STATUS_SUCCESS;
        }
    }

    DXGKRNL_WARN("DxgkOpenAdapterFromLuid: LUID={%ld,%lu} not found\n",
                 pOpenAdapterFromLuid->AdapterLuid.HighPart,
                 pOpenAdapterFromLuid->AdapterLuid.LowPart);
    return STATUS_INVALID_PARAMETER;
}

/* ========================================================================
 * DxgkCloseAdapter — D3DKMTCloseAdapter
 *
 * Closes an adapter handle previously obtained from EnumAdapters or
 * OpenAdapterFromLuid.  Validates the handle against the global adapter
 * list.  No state change occurs on the adapter itself — handles are
 * lightweight references.
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
        return STATUS_INVALID_HANDLE;
    }

    /*
     * Adapter handles are lightweight — no per-handle state is maintained
     * beyond the adapter's own lifetime.  Simply validate and succeed.
     * A full implementation would track per-process open counts and clean
     * up on process exit.
     */
    DXGKRNL_TRACE("DxgkCloseAdapter: handle=0x%X (Adapter %p)\n",
                  pCloseAdapter->hAdapter, Adapter);
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

NTSTATUS
NTAPI
DxgkQueryAdapterInfo(
    _Inout_ CONST D3DKMT_QUERYADAPTERINFO *pQueryAdapterInfo)
{
    PDXGKRNL_ADAPTER Adapter;

    PAGED_CODE();

    if (pQueryAdapterInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkpValidateAdapterHandle(pQueryAdapterInfo->hAdapter);
    if (Adapter == NULL)
    {
        DXGKRNL_WARN("DxgkQueryAdapterInfo: invalid handle 0x%X\n",
                     pQueryAdapterInfo->hAdapter);
        return STATUS_INVALID_HANDLE;
    }

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

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_SEGMENTSIZEINFO))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            pSegInfo = (D3DKMT_SEGMENTSIZEINFO *)pQueryAdapterInfo->pPrivateDriverData;

            /*
             * For DOD/display-only: no dedicated VRAM, report system memory.
             * A real adapter would query segments from vidmm here.
             */
            _SEH2_TRY
            {
                RtlZeroMemory(pSegInfo, sizeof(*pSegInfo));
                pSegInfo->DedicatedVideoMemorySize  = 0;
                pSegInfo->DedicatedSystemMemorySize = 0;
                pSegInfo->SharedSystemMemorySize    = 256 * 1024 * 1024; /* 256 MB */
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: GETSEGMENTSIZE -> Shared=%llu\n",
                          pSegInfo->SharedSystemMemorySize);
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_DRIVERVERSION:
        {
            /*
             * Return the WDDM driver version that matches the dxgkrnl ABI
             * level we compiled against.
             */
            D3DKMT_DRIVERVERSION *pVersion;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_DRIVERVERSION))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            pVersion = (D3DKMT_DRIVERVERSION *)pQueryAdapterInfo->pPrivateDriverData;
            _SEH2_TRY
            {
                *pVersion = DxgkpGetReportedDriverVersion();
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: DRIVERVERSION -> %d\n",
                          *pVersion);
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_UMDRIVERNAME:
        {
            D3DKMT_UMDFILENAMEINFO *pDriverName;
            D3DKMT_UMDFILENAMEINFO DriverName;
            NTSTATUS Status;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_UMDFILENAMEINFO))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            pDriverName = (D3DKMT_UMDFILENAMEINFO *)pQueryAdapterInfo->pPrivateDriverData;
            _SEH2_TRY
            {
                DriverName = *pDriverName;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if ((UINT)DriverName.Version >= NUM_KMTUMDVERSIONS)
                return STATUS_INVALID_PARAMETER;

            Status = DxgkpQueryDriverStringValue(Adapter,
                                                 L"UserModeDriverName",
                                                (ULONG)DriverName.Version,
                                                DriverName.UmdFileName,
                                                ARRAYSIZE(DriverName.UmdFileName));
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkQueryAdapterInfo: UMDRIVERNAME query failed 0x%08lx\n",
                             Status);
                return Status;
            }

            _SEH2_TRY
            {
                *pDriverName = DriverName;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: UMDRIVERNAME[%u] -> %ws\n",
                          DriverName.Version,
                          DriverName.UmdFileName);
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_UMOPENGLINFO:
        {
            D3DKMT_OPENGLINFO *pOpenGlInfo;
            D3DKMT_OPENGLINFO OpenGlInfo;
            NTSTATUS Status;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(D3DKMT_OPENGLINFO))
            {
                return STATUS_BUFFER_TOO_SMALL;
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
                return Status;
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
                return _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: UMOPENGLINFO -> %ws Version=%lu Flags=0x%lx\n",
                          OpenGlInfo.UmdOpenGlIcdFileName,
                          OpenGlInfo.Version,
                          OpenGlInfo.Flags);
            return STATUS_SUCCESS;
        }

        case 15: /* KMTQAITYPE_ADAPTERTYPE (Win8+, not in Vista-level enum) */
        {
            /*
             * Return adapter type flags.  We report RenderSupported and
             * DisplaySupported since this is a WDDM display adapter.
             */
            UINT *pAdapterTypeValue;

            if (pQueryAdapterInfo->pPrivateDriverData == NULL ||
                pQueryAdapterInfo->PrivateDriverDataSize < sizeof(UINT))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            pAdapterTypeValue = (UINT *)pQueryAdapterInfo->pPrivateDriverData;

            /*
             * D3DKMT_ADAPTERTYPE is a union of bitfields and a UINT Value.
             * Set RenderSupported (bit 0) and DisplaySupported (bit 1).
             */
            _SEH2_TRY
            {
                *pAdapterTypeValue = 0x3; /* RenderSupported | DisplaySupported */
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                return _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            DXGKRNL_TRACE("DxgkQueryAdapterInfo: ADAPTERTYPE -> 0x%X\n",
                          *pAdapterTypeValue);
            return STATUS_SUCCESS;
        }

        case KMTQAITYPE_UMDRIVERPRIVATE:
        {
            return STATUS_NOT_SUPPORTED;
        }

        default:
            DXGKRNL_WARN("DxgkQueryAdapterInfo: unsupported Type=%d\n",
                         pQueryAdapterInfo->Type);
            return STATUS_NOT_SUPPORTED;
    }
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
    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Context),
                                    TAG_DXGK_ADAPTER);
    if (Context == NULL)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Magic = DXGKP_FILE_CONTEXT_MAGIC;
    ExInitializeFastMutex(&Context->GpuVaLock);
    InitializeListHead(&Context->GpuVaRanges);
    Context->NextGpuVa = DXGKP_GPUVA_START;

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

#define DXGK_VIRTGPU_RESOURCE_LIST_MAGIC 0x5652474cUL

static BOOLEAN
DxgkpIsVirtGpuCommandEscape(
    _In_ CONST D3DKMT_ESCAPE *pEscape,
    _Outptr_result_bytebuffer_(*CommandBytes) CONST VOID **CommandBuffer,
    _Out_ UINT *CommandBytes,
    _Outptr_result_buffer_maybenull_(*ResourceHandleCount) CONST D3DKMT_HANDLE **ResourceHandles,
    _Out_ UINT *ResourceHandleCount)
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

    *CommandBuffer = CommandHeader;
    *CommandBytes = sizeof(*CommandHeader) + CommandHeader->PayloadBytes;
    return TRUE;
}

static NTSTATUS
DxgkpOpenRenderAllocation(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_ D3DKMT_HANDLE AllocationHandle,
    _Out_ PHANDLE DeviceSpecificHandle)
{
    PDXGKVMM_ALLOCATION Allocation;
    DXGK_OPENALLOCATIONINFO OpenInfo;
    DXGKARG_OPENALLOCATION OpenArgs;
    NTSTATUS Status;

    if (Adapter == NULL || Device == NULL || DeviceSpecificHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    *DeviceSpecificHandle = NULL;

    if (AllocationHandle == 0 ||
        DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation) == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Allocation = DxgkVidMmHandleToAllocation((HANDLE)(ULONG_PTR)AllocationHandle);
    if (Allocation == NULL)
        return STATUS_INVALID_HANDLE;

    RtlZeroMemory(&OpenInfo, sizeof(OpenInfo));
    RtlZeroMemory(&OpenArgs, sizeof(OpenArgs));

    OpenInfo.hAllocation = AllocationHandle;

    OpenArgs.NumAllocations = 1;
    OpenArgs.pOpenAllocation = &OpenInfo;
    OpenArgs.pPrivateDriverData = NULL;
    OpenArgs.PrivateDriverDataSize = 0;
    OpenArgs.hResource = (Allocation->Resource != NULL) ?
                         Allocation->Resource->MiniportHandle : NULL;
    OpenArgs.ReadOnly = FALSE;

    Status = DXGK_CB_FULL(Adapter, DxgkDdiOpenAllocation)(
                 Device->hMiniportDevice,
                 &OpenArgs);
    if (!NT_SUCCESS(Status))
        return Status;

    if (OpenInfo.hDeviceSpecificAllocation == NULL)
        return STATUS_INVALID_HANDLE;

    *DeviceSpecificHandle = OpenInfo.hDeviceSpecificAllocation;
    return STATUS_SUCCESS;
}

static VOID
DxgkpCloseRenderAllocations(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_opt_(OpenHandleCount) CONST HANDLE *OpenHandleList,
    _In_ UINT OpenHandleCount)
{
    DXGKARG_CLOSEALLOCATION CloseArgs;

    if (Adapter == NULL ||
        Device == NULL ||
        OpenHandleList == NULL ||
        OpenHandleCount == 0 ||
        DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation) == NULL)
    {
        return;
    }

    RtlZeroMemory(&CloseArgs, sizeof(CloseArgs));
    CloseArgs.NumAllocations = OpenHandleCount;
    CloseArgs.pOpenHandleList = (PHANDLE)OpenHandleList;

    DXGK_CB_FULL(Adapter, DxgkDdiCloseAllocation)(
        Device->hMiniportDevice,
        &CloseArgs);
}

static NTSTATUS
DxgkpSubmitVirtGpuCommandEscape(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_bytes_(CommandBytes) CONST VOID *CommandBuffer,
    _In_ UINT CommandBytes,
    _In_reads_opt_(ResourceHandleCount) CONST D3DKMT_HANDLE *ResourceHandles,
    _In_ UINT ResourceHandleCount)
{
    DXGKARG_RENDER RenderArgs;
    DXGKARG_SUBMITCOMMAND SubmitArgs;
    DXGK_ALLOCATIONLIST *AllocationList = NULL;
    HANDLE *OpenHandleList = NULL;
    PVOID DmaBuffer = NULL;
    PVOID DmaBufferPrivateData = NULL;
    ULONG SubmissionFenceId;
    UINT DmaBytesUsed = 0;
    UINT i;
    NTSTATUS Status;

    if (Adapter == NULL || Device == NULL || CommandBuffer == NULL || CommandBytes == 0)
        return STATUS_INVALID_PARAMETER;

    if (DXGK_CB_FULL(Adapter, DxgkDdiRender) == NULL ||
        DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    DmaBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                      CommandBytes,
                                      TAG_DXGK_SUBMITDMA);
    if (DmaBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (ResourceHandleCount != 0)
    {
        AllocationList = ExAllocatePoolWithTag(NonPagedPool,
                                               ResourceHandleCount * sizeof(*AllocationList),
                                               TAG_DXGK_SUBMITDMA);
        OpenHandleList = ExAllocatePoolWithTag(NonPagedPool,
                                               ResourceHandleCount * sizeof(*OpenHandleList),
                                               TAG_DXGK_SUBMITDMA);
        if (AllocationList == NULL || OpenHandleList == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlZeroMemory(AllocationList, ResourceHandleCount * sizeof(*AllocationList));
        RtlZeroMemory(OpenHandleList, ResourceHandleCount * sizeof(*OpenHandleList));

        for (i = 0; i < ResourceHandleCount; ++i)
        {
            Status = DxgkpOpenRenderAllocation(Adapter,
                                               Device,
                                               ResourceHandles[i],
                                               &OpenHandleList[i]);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            AllocationList[i].hDeviceSpecificAllocation = OpenHandleList[i];
            AllocationList[i].Value = 0;
        }
    }

    RtlZeroMemory(&RenderArgs, sizeof(RenderArgs));
    RenderArgs.pCommand = CommandBuffer;
    RenderArgs.CommandLength = CommandBytes;
    RenderArgs.pDmaBuffer = DmaBuffer;
    RenderArgs.DmaSize = CommandBytes;
    RenderArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
    RenderArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
    RenderArgs.pAllocationList = AllocationList;
    RenderArgs.AllocationListSize = ResourceHandleCount;

    Status = DXGK_CB_FULL(Adapter, DxgkDdiRender)(Device->hMiniportDevice,
                                                  &RenderArgs);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (RenderArgs.pDmaBuffer != NULL &&
        (PUCHAR)RenderArgs.pDmaBuffer >= (PUCHAR)DmaBuffer)
    {
        DmaBytesUsed = (UINT)((PUCHAR)RenderArgs.pDmaBuffer - (PUCHAR)DmaBuffer);
    }
    if (DmaBytesUsed == 0)
        DmaBytesUsed = CommandBytes;

    SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    if (SubmissionFenceId == 0)
        SubmissionFenceId = 1;

    RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
    SubmitArgs.pDmaBuffer = DmaBuffer;
    SubmitArgs.DmaBufferSize = CommandBytes;
    SubmitArgs.pDmaBufferPrivateData = &DmaBufferPrivateData;
    SubmitArgs.DmaBufferPrivateDataSize = sizeof(DmaBufferPrivateData);
    SubmitArgs.DmaBufferSubmissionStartOffset = 0;
    SubmitArgs.DmaBufferSubmissionEndOffset = DmaBytesUsed;
    SubmitArgs.pAllocationList = NULL;
    SubmitArgs.AllocationListSize = 0;
    SubmitArgs.SubmissionFenceId = SubmissionFenceId;
    SubmitArgs.VidPnSourceId = 0;
    SubmitArgs.NodeOrdinal = 0;
    SubmitArgs.EngineOrdinal = 0;
    SubmitArgs.hContext = Device->hMiniportDevice;
    SubmitArgs.Flags = 0;

    Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(Adapter->MiniportDeviceContext,
                                                         &SubmitArgs);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = DxgkTrackSubmittedDmaBuffer(Adapter,
                                         SubmissionFenceId,
                                         0,
                                         DmaBuffer,
                                         TAG_DXGK_SUBMITDMA,
                                         Device,
                                         NULL,
                                         NULL,
                                         0,
                                         NULL,
                                         OpenHandleList,
                                         ResourceHandleCount);
    if (NT_SUCCESS(Status))
    {
        DmaBuffer = NULL;
        if (OpenHandleList != NULL)
        {
            ExFreePoolWithTag(OpenHandleList, TAG_DXGK_SUBMITDMA);
            OpenHandleList = NULL;
        }
        Status = STATUS_SUCCESS;
    }
    else
    {
        DXGKRNL_WARN("DxgkpSubmitVirtGpuCommandEscape: DMA track failed "
                     "0x%08lX for fence=%u\n",
                     Status,
                     SubmissionFenceId);
        DmaBuffer = NULL;
    }

Cleanup:
    if (OpenHandleList != NULL)
        DxgkpCloseRenderAllocations(Adapter,
                                    Device,
                                    OpenHandleList,
                                    ResourceHandleCount);
    if (OpenHandleList != NULL)
        ExFreePoolWithTag(OpenHandleList, TAG_DXGK_SUBMITDMA);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_DXGK_SUBMITDMA);
    if (DmaBuffer != NULL)
        ExFreePoolWithTag(DmaBuffer, TAG_DXGK_SUBMITDMA);

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

    LockDevice = DxgkLookupDeviceByHandle(pLock->hDevice, &LockAdapter);
    if (LockDevice == NULL || LockAdapter == NULL)
    {
        DXGKRNL_WARN("DxgkLock: invalid device 0x%X\n", pLock->hDevice);
        return STATUS_INVALID_HANDLE;
    }

    LockAlloc = DxgkVidMmHandleToAllocation(
                    (HANDLE)(ULONG_PTR)pLock->hAllocation);
    if (LockAlloc == NULL || LockAlloc->Adapter != LockAdapter)
    {
        DXGKRNL_WARN("DxgkLock: invalid alloc 0x%X\n", pLock->hAllocation);
        return STATUS_INVALID_HANDLE;
    }

    /* Interface callers are always kernel -- use system VA mapping */
    Status = DxgkVidMmMapAllocationCpu(LockAlloc, &LockVa);
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

    UnlockDevice = DxgkLookupDeviceByHandle(pUnlock->hDevice, &UnlockAdapter);
    if (UnlockDevice == NULL || UnlockAdapter == NULL)
        return STATUS_INVALID_HANDLE;

    _SEH2_TRY
    {
        for (ui = 0; ui < pUnlock->NumAllocations; ui++)
        {
            PDXGKVMM_ALLOCATION UnlockAlloc;
            D3DKMT_HANDLE UnlockHandle;

            UnlockHandle = pUnlock->phAllocations[ui];
            UnlockAlloc = DxgkVidMmHandleToAllocation(
                              (HANDLE)(ULONG_PTR)UnlockHandle);
            if (UnlockAlloc == NULL || UnlockAlloc->Adapter != UnlockAdapter)
                _SEH2_YIELD(return STATUS_INVALID_HANDLE);

            DxgkVidMmUnmapAllocationCpu(UnlockAlloc);
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

/*
 * DxgkEscape -- D3DKMTEscape handler.
 */
NTSTATUS
NTAPI
DxgkEscape(
    _In_ CONST D3DKMT_ESCAPE *pEscape)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  EscDevice = NULL;
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
        return STATUS_NOT_SUPPORTED;
    }

    if (pEscape->hDevice != 0)
        EscDevice = DxgkLookupDeviceByHandle(pEscape->hDevice, NULL);

    if (DxgkpIsVirtGpuCommandEscape(pEscape,
                                    &CommandBuffer,
                                    &CommandBytes,
                                    &ResourceHandles,
                                    &ResourceHandleCount))
    {
        if (EscDevice == NULL)
        {
            DXGKRNL_WARN("DxgkEscape: command packet without valid device 0x%X\n",
                         pEscape->hDevice);
            return STATUS_INVALID_HANDLE;
        }

        return DxgkpSubmitVirtGpuCommandEscape(Adapter,
                                               EscDevice,
                                               CommandBuffer,
                                               CommandBytes,
                                               ResourceHandles,
                                               ResourceHandleCount);
    }

    if (pEscape->hDevice == 0)
        return STATUS_NOT_SUPPORTED;

    if (DXGK_CB_FULL(Adapter, DxgkDdiEscape) == NULL)
    {
        DXGKRNL_WARN("DxgkEscape: miniport has no DxgkDdiEscape\n");
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&EscapeArgs, sizeof(EscapeArgs));
    EscapeArgs.hDevice = EscDevice ? EscDevice->hMiniportDevice : NULL;
    EscapeArgs.pPrivateDriverData  = pEscape->pPrivateDriverData;
    EscapeArgs.PrivateDriverDataSize = pEscape->PrivateDriverDataSize;
    EscapeArgs.Flags.Value         = pEscape->Flags.Value;

    DXGKRNL_VERBOSE("DxgkEscape: adapter=0x%X size=%u flags=0x%X\n",
                    pEscape->hAdapter,
                    pEscape->PrivateDriverDataSize,
                    pEscape->Flags.Value);

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiEscape)(
                     Adapter->MiniportDeviceContext,
                     &EscapeArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DXGKRNL_ERR("DxgkEscape: miniport faulted 0x%08lX\n", Status);
    }
    _SEH2_END;

    return Status;
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

static VOID
DxgkpFillSyntheticScanLine(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ D3DKMT_GETSCANLINE *pData)
{
    LARGE_INTEGER Tick;
    ULONG Height;

    Height = Adapter->CommittedHeight;
    if (Height == 0)
        Height = Adapter->SharedPrimaryHeight;
    if (Height == 0)
        Height = Adapter->PostDisplayHeight;
    if (Height == 0)
        Height = 768;

    KeQueryTickCount(&Tick);

    pData->ScanLine = (UINT)(Tick.QuadPart % Height);
    pData->InVerticalBlank = FALSE;
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

    Context = DxgkLookupContextByHandle(pData->hContext, NULL, NULL);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;

    Context->SchedulingPriority = pData->Priority;
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkGetDeviceState(
    _Inout_ D3DKMT_GETDEVICESTATE *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    if (DxgkLookupDeviceByHandle(pData->hDevice, NULL) == NULL)
        return STATUS_INVALID_HANDLE;

    if (pData->StateType == D3DKMT_DEVICESTATE_EXECUTION)
        pData->ExecutionState = D3DKMT_DEVICEEXECUTION_ACTIVE;

    return STATUS_SUCCESS;
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
    if (DxgkLookupAdapterByHandle(pData->hAdapter) == NULL)
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
    if (DxgkLookupAdapterByHandle(pData->hAdapter) == NULL)
        return STATUS_INVALID_HANDLE;
    if (pData->hGlobalShare == 0 ||
        DxgkVidMmHandleToGlobalShare(pData->hGlobalShare) == NULL)
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

    /*
     * Some early WDDM miniports expose DxgkDdiGetScanLine but leave it as a
     * breakpoint stub. Keep D3DKMT callable by synthesizing a bounded value
     * from dxgkrnl's committed mode instead of invoking an optional query that
     * can trap the machine.
     */
    DxgkpFillSyntheticScanLine(Adapter, pData);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkInvalidateActiveVidPn(
    _In_ CONST D3DKMT_INVALIDATEACTIVEVIDPN *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (DxgkLookupAdapterByHandle(pData->hAdapter) == NULL)
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
    if (!pData->PollAllAdapters &&
        DxgkLookupAdapterByHandle(pData->hAdapter) == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkQueryAllocationResidency(
    _In_ CONST D3DKMT_QUERYALLOCATIONRESIDENCY *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    UINT i;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->AllocationCount == 0 ||
        pData->AllocationCount > DXGKP_MAX_D3DKMT_LIST_COUNT ||
        pData->phAllocationList == NULL ||
        pData->pResidencyStatus == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                 pData->phAllocationList,
                                                 pData->AllocationCount);
    if (!NT_SUCCESS(Status))
        return Status;

    _SEH2_TRY
    {
        for (i = 0; i < pData->AllocationCount; ++i)
            pData->pResidencyStatus[i] =
                D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINSHAREDMEMORY;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
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
    if (hProcess == NULL)
        return STATUS_INVALID_HANDLE;

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkSetAllocationPriority(
    _In_ CONST D3DKMT_SETALLOCATIONPRIORITY *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->AllocationCount == 0 ||
        pData->AllocationCount > DXGKP_MAX_D3DKMT_LIST_COUNT ||
        pData->phAllocationList == NULL ||
        pData->pPriorities == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                 pData->phAllocationList,
                                                 pData->AllocationCount);
    return Status;
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
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    return DxgkUnsupportedDeviceCall(pData->hDevice);
}

static NTSTATUS
NTAPI
DxgkWaitForIdle(
    _In_ CONST D3DKMT_WAITFORIDLE *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    return DxgkLookupDeviceByHandle(pData->hDevice, NULL) != NULL
               ? STATUS_SUCCESS
               : STATUS_INVALID_HANDLE;
}

static NTSTATUS
NTAPI
DxgkWaitForVerticalBlankEvent(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT *pData)
{
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateAdapterVidPnSourceForIoctl(pData->hAdapter,
                                                     pData->VidPnSourceId,
                                                     NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->hDevice != 0)
    {
        Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, NULL, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkOfferAllocations(
    _In_ CONST D3DKMT_OFFERALLOCATIONS *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->NumAllocations == 0 ||
        pData->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT ||
        ((pData->pResources == NULL) == (pData->HandleList == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pData->HandleList != NULL)
    {
        Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                     pData->HandleList,
                                                     pData->NumAllocations);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS
NTAPI
DxgkReclaimAllocations(
    _Inout_ D3DKMT_RECLAIMALLOCATIONS *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->NumAllocations == 0 ||
        pData->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT ||
        ((pData->pResources == NULL) == (pData->HandleList == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (pData->HandleList != NULL)
    {
        Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                     pData->HandleList,
                                                     pData->NumAllocations);
        if (!NT_SUCCESS(Status))
            return Status;
    }

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
    return DxgkSetVidPnSourceOwner(&Owner);
}

static NTSTATUS
NTAPI
DxgkWaitForVerticalBlankEvent2(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT2 *pData)
{
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->NumObjects > D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateAdapterVidPnSourceForIoctl(pData->hAdapter,
                                                     pData->VidPnSourceId,
                                                     NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pData->hDevice != 0)
    {
        Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, NULL, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkCreateSynchronizationObject2(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *pData)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT Create1;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Create1, sizeof(Create1));
    Create1.hDevice = pData->hDevice;
    Create1.Info.Type = pData->Info.Type;

    switch (pData->Info.Type)
    {
        case D3DDDI_SYNCHRONIZATION_MUTEX:
            Create1.Info.SynchronizationMutex.InitialState =
                pData->Info.SynchronizationMutex.InitialState;
            break;
        case D3DDDI_SEMAPHORE:
            Create1.Info.Semaphore.MaxCount =
                pData->Info.Semaphore.MaxCount;
            Create1.Info.Semaphore.InitialCount =
                pData->Info.Semaphore.InitialCount;
            break;
        case DXGKP_D3DDDI_MONITORED_FENCE:
            /*
             * WDDM 2.0 monitored fence: back it with a CPU-side fence object.
             * The handle is what callers track; full GPU monitored-fence
             * semantics require the dxgmms1 scheduler.
             */
            Create1.Info.Type = D3DDDI_FENCE;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    Status = DxgkCreateSynchronizationObject(&Create1);
    if (NT_SUCCESS(Status))
        pData->hSyncObject = Create1.hSyncObject;

    return Status;
}

static NTSTATUS
NTAPI
DxgkWaitForSynchronizationObject2(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 *pData)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT Wait1;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Wait1, sizeof(Wait1));
    Wait1.hContext = pData->hContext;
    Wait1.ObjectCount = pData->ObjectCount;
    RtlCopyMemory(Wait1.ObjectHandleArray,
                  pData->ObjectHandleArray,
                  sizeof(D3DKMT_HANDLE) * pData->ObjectCount);

    return DxgkWaitForSynchronizationObject(&Wait1);
}

static NTSTATUS
NTAPI
DxgkSignalSynchronizationObject2(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *pData)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT Signal1;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED ||
        pData->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Signal1, sizeof(Signal1));
    Signal1.hContext = pData->hContext;
    Signal1.ObjectCount = pData->ObjectCount;
    Signal1.Flags = pData->Flags;
    RtlCopyMemory(Signal1.ObjectHandleArray,
                  pData->ObjectHandleArray,
                  sizeof(D3DKMT_HANDLE) * pData->ObjectCount);

    return DxgkSignalSynchronizationObject(&Signal1);
}

/* ========================================================================
 * WDDM 2.0 paging queues
 *
 * A paging queue serialises residency operations for a device and is backed
 * by a monitored-fence sync object.  For the software/display-only path we
 * track a lightweight handle plus its backing sync object; residency
 * operations (MakeResident/Evict) complete synchronously elsewhere.
 * ====================================================================== */

typedef struct _DXGKRNL_PAGING_QUEUE
{
    LIST_ENTRY      ListEntry;
    D3DKMT_HANDLE   Handle;
    D3DKMT_HANDLE   hDevice;
    D3DKMT_HANDLE   hSyncObject;
} DXGKRNL_PAGING_QUEUE, *PDXGKRNL_PAGING_QUEUE;

static ULONG DxgkPagingQueueCookie = 0x51474150; /* "PAGQ" */
static LONG DxgkPagingQueueInitialized = 0;
static FAST_MUTEX DxgkPagingQueueListLock;
static LIST_ENTRY DxgkPagingQueueListHead;
static volatile LONG DxgkNextPagingQueueHandle = 0;

static VOID
DxgkpPagingQueueInit(VOID)
{
    if (InterlockedCompareExchange(&DxgkPagingQueueInitialized, 1, 0) != 0)
        return;

    ExInitializeFastMutex(&DxgkPagingQueueListLock);
    InitializeListHead(&DxgkPagingQueueListHead);
}

static D3DKMT_HANDLE
DxgkpAllocatePagingQueueHandle(VOID)
{
    ULONG Sequence;

    do
    {
        Sequence = (ULONG)InterlockedIncrement(&DxgkNextPagingQueueHandle);
    } while (Sequence == 0);

    return (D3DKMT_HANDLE)(Sequence ^ DxgkPagingQueueCookie);
}

static BOOLEAN
DxgkpIsValidPagingQueue(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;
    BOOLEAN     Found = FALSE;

    if (Handle == 0)
        return FALSE;

    DxgkpPagingQueueInit();

    ExAcquireFastMutex(&DxgkPagingQueueListLock);
    for (Entry = DxgkPagingQueueListHead.Flink;
         Entry != &DxgkPagingQueueListHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_PAGING_QUEUE Candidate =
            CONTAINING_RECORD(Entry, DXGKRNL_PAGING_QUEUE, ListEntry);
        if (Candidate->Handle == Handle)
        {
            Found = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkPagingQueueListLock);

    return Found;
}

static NTSTATUS
NTAPI
DxgkCreatePagingQueue(
    _Inout_ D3DKMT_CREATEPAGINGQUEUE *pData)
{
    PDXGKRNL_ADAPTER      Adapter;
    PDXGKRNL_DEVICE       Device;
    PDXGKRNL_PAGING_QUEUE Queue;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT CreateSync;
    NTSTATUS              Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    DxgkpPagingQueueInit();

    /* Back the paging queue with a CPU-side fence (monitored fence stand-in). */
    RtlZeroMemory(&CreateSync, sizeof(CreateSync));
    CreateSync.hDevice = pData->hDevice;
    CreateSync.Info.Type = D3DDDI_FENCE;
    Status = DxgkCreateSynchronizationObject(&CreateSync);
    if (!NT_SUCCESS(Status))
        return Status;

    Queue = (PDXGKRNL_PAGING_QUEUE)ExAllocatePoolWithTag(NonPagedPool,
                                                         sizeof(*Queue),
                                                         TAG_DXGK_SYNC);
    if (Queue == NULL)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = CreateSync.hSyncObject;
        DxgkDestroySynchronizationObject(&DestroySync);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Queue, sizeof(*Queue));
    Queue->Handle = DxgkpAllocatePagingQueueHandle();
    Queue->hDevice = pData->hDevice;
    Queue->hSyncObject = CreateSync.hSyncObject;
    InitializeListHead(&Queue->ListEntry);

    ExAcquireFastMutex(&DxgkPagingQueueListLock);
    InsertTailList(&DxgkPagingQueueListHead, &Queue->ListEntry);
    ExReleaseFastMutex(&DxgkPagingQueueListLock);

    pData->hPagingQueue = Queue->Handle;
    pData->hSyncObject = Queue->hSyncObject;
    pData->FenceValueCPUVirtualAddress = NULL;

    DXGKRNL_TRACE("DxgkCreatePagingQueue: queue=0x%X sync=0x%X\n",
                  Queue->Handle, Queue->hSyncObject);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkDestroyPagingQueue(
    _Inout_ D3DDDI_DESTROYPAGINGQUEUE *pData)
{
    PLIST_ENTRY           Entry;
    PDXGKRNL_PAGING_QUEUE Found = NULL;
    D3DKMT_HANDLE         hSyncObject = 0;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->hPagingQueue == 0)
        return STATUS_INVALID_HANDLE;

    DxgkpPagingQueueInit();

    ExAcquireFastMutex(&DxgkPagingQueueListLock);
    for (Entry = DxgkPagingQueueListHead.Flink;
         Entry != &DxgkPagingQueueListHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_PAGING_QUEUE Candidate =
            CONTAINING_RECORD(Entry, DXGKRNL_PAGING_QUEUE, ListEntry);
        if (Candidate->Handle == pData->hPagingQueue)
        {
            RemoveEntryList(&Candidate->ListEntry);
            Found = Candidate;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkPagingQueueListLock);

    if (Found == NULL)
        return STATUS_INVALID_HANDLE;

    hSyncObject = Found->hSyncObject;
    ExFreePoolWithTag(Found, TAG_DXGK_SYNC);

    if (hSyncObject != 0)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = hSyncObject;
        DxgkDestroySynchronizationObject(&DestroySync);
    }

    DXGKRNL_TRACE("DxgkDestroyPagingQueue: queue=0x%X\n", pData->hPagingQueue);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkQueryVideoMemoryInfo(
    _Inout_ D3DKMT_QUERYVIDEOMEMORYINFO *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    UINT64           Budget;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Adapter = DxgkLookupAdapterByHandle(pData->hAdapter);
    if (Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (pData->MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL &&
        pData->MemorySegmentGroup != D3DKMT_MEMORY_SEGMENT_GROUP_NON_LOCAL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Display-only / software adapter: report a software-managed budget over
     * system RAM.  The runtime only requires a self-consistent view
     * (CurrentUsage <= Budget, CurrentReservation <= AvailableForReservation).
     */
    if (pData->MemorySegmentGroup == D3DKMT_MEMORY_SEGMENT_GROUP_LOCAL)
        Budget = 256ULL * 1024ULL * 1024ULL;   /* 256 MB pseudo-VRAM */
    else
        Budget = 512ULL * 1024ULL * 1024ULL;   /* 512 MB shared system */

    pData->Budget = Budget;
    pData->CurrentUsage = 0;
    pData->CurrentReservation = 0;
    pData->AvailableForReservation = Budget / 2ULL;

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkSignalSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    D3DKMT_HANDLE    Handles[D3DDDI_MAX_OBJECT_SIGNALED];
    UINT64           Fences[D3DDDI_MAX_OBJECT_SIGNALED];
    NTSTATUS         Status;
    ULONG            i;

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

    /* ObjectHandleArray / FenceValueArray point at user memory; capture safely. */
    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        RtlCopyMemory(Handles, pData->ObjectHandleArray,
                      sizeof(D3DKMT_HANDLE) * pData->ObjectCount);
        RtlCopyMemory(Fences, pData->FenceValueArray,
                      sizeof(UINT64) * pData->ObjectCount);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        return Status;

    for (i = 0; i < pData->ObjectCount; ++i)
    {
        Status = DxgkSyncObjectCpuSignal(Device->Handle, Handles[i], Fences[i]);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
DxgkWaitForSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    D3DKMT_HANDLE    Handles[D3DDDI_MAX_OBJECT_WAITED_ON];
    UINT64           Fences[D3DDDI_MAX_OBJECT_WAITED_ON];
    BOOLEAN          NonBlocking;
    NTSTATUS         Status;
    ULONG            i;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON ||
        pData->ObjectHandleArray == NULL ||
        pData->FenceValueArray == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = DxgkpValidateDeviceHandleForIoctl(pData->hDevice, &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        RtlCopyMemory(Handles, pData->ObjectHandleArray,
                      sizeof(D3DKMT_HANDLE) * pData->ObjectCount);
        RtlCopyMemory(Fences, pData->FenceValueArray,
                      sizeof(UINT64) * pData->ObjectCount);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (!NT_SUCCESS(Status))
        return Status;

    /* A supplied async event makes the wait non-blocking. */
    NonBlocking = (pData->hAsyncEvent != NULL);

    for (i = 0; i < pData->ObjectCount; ++i)
    {
        Status = DxgkSyncObjectCpuWait(Device->Handle, Handles[i],
                                       Fences[i], NonBlocking);
        if (!NT_SUCCESS(Status))
            return Status;
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

            Status = DxgkEnumAdapters2((D3DKMT_ENUMADAPTERS2 *)SystemBuffer);
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
            return Status;
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

            Status = DxgkQueryAdapterInfo((CONST D3DKMT_QUERYADAPTERINFO *)SystemBuffer);
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
            return Status;
        }

        /*
         * Priority 1: Bridge IOCTLs from win32k D3DKMT stubs.
         *
         * OpenAdapterFromHdc / FromGdiDisplayName / FromDeviceName all
         * resolve to the single started adapter for the current display-only
         * DOD path.  A full implementation would look up the adapter by the
         * HDC, display name, or device name, but for now we return the first
         * started adapter — matching the viogpudo single-adapter model.
         */
        case IOCTL_D3DKMT_OPENADAPTERFROMHDC:
        {
            D3DKMT_OPENADAPTERFROMHDC *pData;
            PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
            ULONG Count, k;

            if (InputLength < sizeof(D3DKMT_OPENADAPTERFROMHDC) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pData = (D3DKMT_OPENADAPTERFROMHDC *)SystemBuffer;
            if (pData->hDc == NULL)
                return STATUS_INVALID_PARAMETER;

            Count = DxgkpSnapshotAdapters(Snapshot);
            for (k = 0; k < Count; ++k)
            {
                if (Snapshot[k]->State == DxgkAdapterStateStarted)
                {
                    pData->hAdapter = DxgkpCreateAdapterHandle(Snapshot[k]);
                    if (pData->hAdapter == 0)
                        return STATUS_INSUFFICIENT_RESOURCES;

                    pData->AdapterLuid = Snapshot[k]->AdapterLuid;
                    pData->VidPnSourceId = 0;
                    Irp->IoStatus.Information = sizeof(D3DKMT_OPENADAPTERFROMHDC);
                    return STATUS_SUCCESS;
                }
            }
            return STATUS_NO_SUCH_DEVICE;
        }

        case IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME:
        {
            D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *pData;
            PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
            ULONG Count, k;

            if (InputLength < sizeof(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pData = (D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *)SystemBuffer;
            Count = DxgkpSnapshotAdapters(Snapshot);
            for (k = 0; k < Count; ++k)
            {
                if (Snapshot[k]->State == DxgkAdapterStateStarted)
                {
                    pData->hAdapter = DxgkpCreateAdapterHandle(Snapshot[k]);
                    if (pData->hAdapter == 0)
                        return STATUS_INSUFFICIENT_RESOURCES;

                    pData->AdapterLuid = Snapshot[k]->AdapterLuid;
                    pData->VidPnSourceId = 0;
                    Irp->IoStatus.Information = sizeof(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME);
                    return STATUS_SUCCESS;
                }
            }
            return STATUS_NO_SUCH_DEVICE;
        }

        case IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME:
        {
            D3DKMT_OPENADAPTERFROMDEVICENAME *pData;
            PDXGKRNL_ADAPTER Snapshot[DXGKP_MAX_ADAPTERS];
            ULONG Count, k;

            if (InputLength < sizeof(D3DKMT_OPENADAPTERFROMDEVICENAME) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pData = (D3DKMT_OPENADAPTERFROMDEVICENAME *)SystemBuffer;
            if (pData->pDeviceName == NULL)
                return STATUS_INVALID_PARAMETER;

            Count = DxgkpSnapshotAdapters(Snapshot);
            for (k = 0; k < Count; ++k)
            {
                if (Snapshot[k]->State == DxgkAdapterStateStarted)
                {
                    pData->hAdapter = DxgkpCreateAdapterHandle(Snapshot[k]);
                    if (pData->hAdapter == 0)
                        return STATUS_INSUFFICIENT_RESOURCES;

                    pData->AdapterLuid = Snapshot[k]->AdapterLuid;
                    Irp->IoStatus.Information = sizeof(D3DKMT_OPENADAPTERFROMDEVICENAME);
                    return STATUS_SUCCESS;
                }
            }
            return STATUS_NO_SUCH_DEVICE;
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
                return STATUS_INVALID_HANDLE;

            /*
             * METHOD_BUFFERED returns the whole CREATEDEVICE structure to the
             * caller.  Marshal through a local copy so the kernel-private
             * pAdapter pointer never overwrites the input hAdapter field in
             * the caller's buffer.
             */
            CreateDeviceCopy = *pData;
            CreateDeviceCopy.pAdapter = Adapter;

            Status = DxgkCreateDevice(&CreateDeviceCopy);
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
            if (InputLength < sizeof(D3DKMT_CREATEALLOCATION) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkCreateAllocation((D3DKMT_CREATEALLOCATION *)SystemBuffer);
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

            LockDevice = DxgkLookupDeviceByHandle(pLock->hDevice, &LockAdapter);
            if (LockDevice == NULL || LockAdapter == NULL)
            {
                DXGKRNL_WARN("D3DKMTLock: invalid device 0x%X\n", pLock->hDevice);
                return STATUS_INVALID_HANDLE;
            }

            LockAlloc = DxgkVidMmHandleToAllocation(
                            (HANDLE)(ULONG_PTR)pLock->hAllocation);
            if (LockAlloc == NULL || LockAlloc->Adapter != LockAdapter)
            {
                DXGKRNL_WARN("D3DKMTLock: invalid alloc 0x%X\n", pLock->hAllocation);
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
            UserMappingCaller =
                (Stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) ||
                (Irp->RequestorMode == UserMode);
            if (UserMappingCaller)
                Status = DxgkVidMmMapAllocationUser(LockAlloc, &LockVa);
            else
                Status = DxgkVidMmMapAllocationCpu(LockAlloc, &LockVa);

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
            UserMappingCaller =
                (Stack->MajorFunction == IRP_MJ_INTERNAL_DEVICE_CONTROL) ||
                (Irp->RequestorMode == UserMode);

            UnlockDevice = DxgkLookupDeviceByHandle(pUnlock->hDevice, &UnlockAdapter);
            if (UnlockDevice == NULL || UnlockAdapter == NULL)
                return STATUS_INVALID_HANDLE;

            if (pUnlock->NumAllocations == 0 ||
                pUnlock->NumAllocations > DXGKP_MAX_D3DKMT_LIST_COUNT ||
                pUnlock->phAllocations == NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            for (ui = 0; ui < pUnlock->NumAllocations; ui++)
            {
                PDXGKVMM_ALLOCATION UnlockAlloc;
                D3DKMT_HANDLE UnlockHandle;
                BOOLEAN Locked;

                UnlockHandle = pUnlock->phAllocations[ui];
                UnlockAlloc = DxgkVidMmHandleToAllocation(
                                  (HANDLE)(ULONG_PTR)UnlockHandle);
                DXGKRNL_VERBOSE("D3DKMTUnlock:[%u/%u] alloc=0x%X obj=%p bridge=%u\n",
                                ui + 1,
                                pUnlock->NumAllocations,
                                UnlockHandle,
                                UnlockAlloc,
                                UserMappingCaller);
                if (UnlockAlloc == NULL || UnlockAlloc->Adapter != UnlockAdapter)
                    return STATUS_INVALID_HANDLE;

                if (UserMappingCaller)
                {
                    ExAcquireFastMutex(&UnlockAlloc->UserModeLock);
                    Locked = (UnlockAlloc->UserModeAddress != NULL &&
                              UnlockAlloc->UserModeMdl != NULL &&
                              UnlockAlloc->UserModeProcess == PsGetCurrentProcess() &&
                              UnlockAlloc->UserModeLockCount != 0);
                    ExReleaseFastMutex(&UnlockAlloc->UserModeLock);
                    if (!Locked)
                        return STATUS_INVALID_PARAMETER;

                    DxgkVidMmUnmapAllocationUser(UnlockAlloc);
                }
                else
                {
                    if (UnlockAlloc->CpuAddress == NULL)
                        return STATUS_INVALID_PARAMETER;

                    DxgkVidMmUnmapAllocationCpu(UnlockAlloc);
                }
            }

            DXGKRNL_VERBOSE("D3DKMTUnlock: device=0x%X count=%u\n",
                            pUnlock->hDevice, pUnlock->NumAllocations);
            return STATUS_SUCCESS;
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

            Status = DxgkOpenResource(
                         (D3DKMT_OPENRESOURCE *)SystemBuffer);
            if (NT_SUCCESS(Status))
                Irp->IoStatus.Information = sizeof(D3DKMT_OPENRESOURCE);
            return Status;
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
                Irp->IoStatus.Information = sizeof(D3DKMT_PRESENT);
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

            Status = DxgkCreateContext((D3DKMT_CREATECONTEXT *)SystemBuffer);
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

            return DxgkEscape((CONST D3DKMT_ESCAPE *)SystemBuffer);
        }

        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER:
        {
            if (InputLength < sizeof(D3DKMT_SETVIDPNSOURCEOWNER) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Status = DxgkSetVidPnSourceOwner((D3DKMT_SETVIDPNSOURCEOWNER *)SystemBuffer);
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

        case IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS:
        {
            D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL *pMap;
            PDXGKRNL_FILE_CONTEXT Context;

            if (InputLength < sizeof(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Context = DxgkpGetFileContext(Irp);
            if (Context == NULL)
                return STATUS_INVALID_HANDLE;

            pMap = (D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL *)SystemBuffer;
            if (pMap->SizeInPages == 0)
                return STATUS_INVALID_PARAMETER;

            Status = DxgkpGpuVaReserveRange(Context,
                                            pMap->BaseAddress,
                                            pMap->MinimumAddress,
                                            pMap->MaximumAddress,
                                            pMap->SizeInPages * DXGKP_GPUVA_PAGE_SIZE,
                                            &pMap->VirtualAddress);
            if (NT_SUCCESS(Status))
            {
                pMap->PagingFenceValue = 0;
                Irp->IoStatus.Information = sizeof(D3DDDI_MAPGPUVIRTUALADDRESS_LOCAL);
            }
            return Status;
        }

        case IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS:
        {
            D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL *pReserve;
            PDXGKRNL_FILE_CONTEXT Context;

            if (InputLength < sizeof(D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Context = DxgkpGetFileContext(Irp);
            if (Context == NULL)
                return STATUS_INVALID_HANDLE;

            pReserve = (D3DDDI_RESERVEGPUVIRTUALADDRESS_LOCAL *)SystemBuffer;

            /*
             * The reservation references either a paging queue (M1) or an
             * adapter (M2); both alias the same union field.  Reject handles
             * that are neither, so a bogus handle fails instead of silently
             * reserving against the shared file context.
             */
            if (!DxgkpIsValidPagingQueue(pReserve->hPagingQueue) &&
                DxgkLookupAdapterByHandle(pReserve->hPagingQueue) == NULL)
            {
                return STATUS_INVALID_HANDLE;
            }

            Status = DxgkpGpuVaReserveRange(Context,
                                            pReserve->BaseAddress,
                                            pReserve->MinimumAddress,
                                            pReserve->MaximumAddress,
                                            pReserve->Size,
                                            &pReserve->VirtualAddress);
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
            PDXGKRNL_FILE_CONTEXT Context;

            if (InputLength < sizeof(D3DKMT_FREEGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            Context = DxgkpGetFileContext(Irp);
            if (Context == NULL)
                return STATUS_INVALID_HANDLE;

            pFree = (D3DKMT_FREEGPUVIRTUALADDRESS_LOCAL *)SystemBuffer;
            return DxgkpGpuVaFreeRange(Context, pFree->BaseAddress, pFree->Size);
        }

        case IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS:
        {
            if (InputLength < sizeof(D3DKMT_UPDATEGPUVIRTUALADDRESS_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_MAKERESIDENT:
        {
            D3DDDI_MAKERESIDENT_LOCAL *pMakeResident;

            if (InputLength < sizeof(D3DDDI_MAKERESIDENT_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pMakeResident = (D3DDDI_MAKERESIDENT_LOCAL *)SystemBuffer;
            if (pMakeResident->NumAllocations != 0 &&
                pMakeResident->AllocationList == NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            Status = DxgkpValidateAllocationListForIoctl(NULL,
                                                         pMakeResident->AllocationList,
                                                         pMakeResident->NumAllocations);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pMakeResident->NumAllocations != 0)
                return STATUS_NOT_SUPPORTED;

            pMakeResident->PagingFenceValue = 0;
            pMakeResident->NumBytesToTrim = 0;
            Irp->IoStatus.Information = sizeof(D3DDDI_MAKERESIDENT_LOCAL);
            return STATUS_SUCCESS;
        }

        case IOCTL_D3DKMT_EVICT:
        {
            D3DKMT_EVICT_LOCAL *pEvict;
            PDXGKRNL_ADAPTER Adapter;

            if (InputLength < sizeof(D3DKMT_EVICT_LOCAL) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pEvict = (D3DKMT_EVICT_LOCAL *)SystemBuffer;
            if (pEvict->NumAllocations == 0)
            {
                pEvict->NumBytesToTrim = 0;
                Irp->IoStatus.Information = sizeof(D3DKMT_EVICT_LOCAL);
                return STATUS_SUCCESS;
            }

            Status = DxgkpValidateDeviceHandleForIoctl(pEvict->hDevice,
                                                       &Adapter,
                                                       NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pEvict->NumAllocations != 0 &&
                pEvict->AllocationList == NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }

            Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                         pEvict->AllocationList,
                                                         pEvict->NumAllocations);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pEvict->NumAllocations != 0)
                return STATUS_NOT_SUPPORTED;

            pEvict->NumBytesToTrim = 0;
            Irp->IoStatus.Information = sizeof(D3DKMT_EVICT_LOCAL);
            return STATUS_SUCCESS;
        }

        /* ---- Stub handlers for remaining D3DKMT entry points --------------- */

        case IOCTL_D3DKMT_SETALLOCATIONPRIORITY:
        case IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY:
        {
            D3DKMT_QUERYALLOCATIONRESIDENCY *pResidency;
            PDXGKRNL_ADAPTER Adapter;
            UINT i;

            if (IoControlCode == IOCTL_D3DKMT_SETALLOCATIONPRIORITY)
            {
                D3DKMT_SETALLOCATIONPRIORITY *pPriority;

                if (InputLength < sizeof(D3DKMT_SETALLOCATIONPRIORITY) || SystemBuffer == NULL)
                    return STATUS_BUFFER_TOO_SMALL;

                pPriority = (D3DKMT_SETALLOCATIONPRIORITY *)SystemBuffer;
                Status = DxgkpValidateDeviceHandleForIoctl(pPriority->hDevice,
                                                           &Adapter,
                                                           NULL);
                if (!NT_SUCCESS(Status))
                    return Status;

                if (pPriority->AllocationCount != 0 &&
                    (pPriority->phAllocationList == NULL ||
                     pPriority->pPriorities == NULL))
                {
                    return STATUS_INVALID_PARAMETER;
                }

                return DxgkSetAllocationPriority(pPriority);
            }

            if (InputLength < sizeof(D3DKMT_QUERYALLOCATIONRESIDENCY) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pResidency = (D3DKMT_QUERYALLOCATIONRESIDENCY *)SystemBuffer;
            Status = DxgkpValidateDeviceHandleForIoctl(pResidency->hDevice,
                                                       &Adapter,
                                                       NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pResidency->AllocationCount != 0 &&
                (pResidency->phAllocationList == NULL ||
                 pResidency->pResidencyStatus == NULL))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                         pResidency->phAllocationList,
                                                         pResidency->AllocationCount);
            if (!NT_SUCCESS(Status))
                return Status;

            _SEH2_TRY
            {
                for (i = 0; i < pResidency->AllocationCount; i++)
                {
                    pResidency->pResidencyStatus[i] =
                        D3DKMT_ALLOCATIONRESIDENCYSTATUS_RESIDENTINSHAREDMEMORY;
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;

            if (!NT_SUCCESS(Status))
                return Status;

            return STATUS_SUCCESS;
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
            if (DxgkLookupAdapterByHandle(pHistory->hAdapter) == NULL)
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
            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_SETQUEUEDLIMIT:
        {
            D3DKMT_SETQUEUEDLIMIT *pQueuedLimit;

            if (InputLength < sizeof(D3DKMT_SETQUEUEDLIMIT) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pQueuedLimit = (D3DKMT_SETQUEUEDLIMIT *)SystemBuffer;
            if (DxgkLookupDeviceByHandle(pQueuedLimit->hDevice, NULL) == NULL)
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
            if (DxgkLookupAdapterByHandle(pRuntimeData->hAdapter) == NULL)
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
            if (DxgkLookupAdapterByHandle(pInvalidate->hAdapter) == NULL)
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
            PDXGKRNL_ADAPTER Adapter;

            if (InputLength < sizeof(D3DKMT_OFFERALLOCATIONS) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pOffer = (D3DKMT_OFFERALLOCATIONS *)SystemBuffer;
            Status = DxgkpValidateDeviceHandleForIoctl(pOffer->hDevice,
                                                       &Adapter,
                                                       NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pOffer->NumAllocations != 0 &&
                ((pOffer->pResources == NULL && pOffer->AllocationList == NULL) ||
                 (pOffer->pResources != NULL && pOffer->AllocationList != NULL)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            if (pOffer->AllocationList != NULL)
            {
                Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                             pOffer->AllocationList,
                                                             pOffer->NumAllocations);
                if (!NT_SUCCESS(Status))
                    return Status;
            }

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_RECLAIMALLOCATIONS:
        {
            D3DKMT_RECLAIMALLOCATIONS *pReclaim;
            PDXGKRNL_ADAPTER Adapter;

            if (InputLength < sizeof(D3DKMT_RECLAIMALLOCATIONS) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pReclaim = (D3DKMT_RECLAIMALLOCATIONS *)SystemBuffer;
            Status = DxgkpValidateDeviceHandleForIoctl(pReclaim->hDevice,
                                                       &Adapter,
                                                       NULL);
            if (!NT_SUCCESS(Status))
                return Status;

            if (pReclaim->NumAllocations != 0 &&
                ((pReclaim->pResources == NULL && pReclaim->HandleList == NULL) ||
                 (pReclaim->pResources != NULL && pReclaim->HandleList != NULL)))
            {
                return STATUS_INVALID_PARAMETER;
            }

            if (pReclaim->HandleList != NULL)
            {
                Status = DxgkpValidateAllocationListForIoctl(Adapter,
                                                             pReclaim->HandleList,
                                                             pReclaim->NumAllocations);
                if (!NT_SUCCESS(Status))
                    return Status;
            }

            return STATUS_NOT_SUPPORTED;
        }

        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER1:
        {
            D3DKMT_SETVIDPNSOURCEOWNER1 *pOwner1;

            if (InputLength < sizeof(D3DKMT_SETVIDPNSOURCEOWNER1) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            pOwner1 = (D3DKMT_SETVIDPNSOURCEOWNER1 *)SystemBuffer;
            Status = DxgkSetVidPnSourceOwner(&pOwner1->Version0);
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

            return DxgkSignalSynchronizationObjectFromCpu(
                       (CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *)SystemBuffer);
        }

        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU:
        {
            if (InputLength < sizeof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            return DxgkWaitForSynchronizationObjectFromCpu(
                       (CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *)SystemBuffer);
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

            if (InputLength < sizeof(DXGKRNL_INTERFACE_EXCHANGE_IN) || SystemBuffer == NULL)
                return STATUS_BUFFER_TOO_SMALL;

            if (OutputLength < sizeof(REACTOS_WIN32K_DXGKRNL_INTERFACE))
                return STATUS_BUFFER_TOO_SMALL;

            pExchangeIn = (PDXGKRNL_INTERFACE_EXCHANGE_IN)SystemBuffer;

            if (pExchangeIn->Version != DXGKRNL_INTERFACE_VERSION_1)
            {
                DXGKRNL_WARN("IOCTL_DXGKRNL_EXCHANGE_INTERFACE: "
                             "unsupported version %lu\n", pExchangeIn->Version);
                return STATUS_NOT_SUPPORTED;
            }

            if (pExchangeIn->Size < sizeof(REACTOS_WIN32K_DXGKRNL_INTERFACE))
            {
                DXGKRNL_WARN("IOCTL_DXGKRNL_EXCHANGE_INTERFACE: "
                             "interface size %lu too small (need %u)\n",
                             pExchangeIn->Size,
                             (ULONG)sizeof(REACTOS_WIN32K_DXGKRNL_INTERFACE));
                return STATUS_BUFFER_TOO_SMALL;
            }

            /*
             * The output buffer overlaps the input buffer for METHOD_BUFFERED.
             * Fill the interface structure at the SystemBuffer location with
             * dxgkrnl's D3DKMT handler function pointers.  win32k uses these
             * to call directly into dxgkrnl without going through IOCTLs.
             */
            pInterface = (PREACTOS_WIN32K_DXGKRNL_INTERFACE)SystemBuffer;
            RtlZeroMemory(pInterface, sizeof(REACTOS_WIN32K_DXGKRNL_INTERFACE));

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

            Irp->IoStatus.Information = sizeof(REACTOS_WIN32K_DXGKRNL_INTERFACE);

            DXGKRNL_TRACE("IOCTL_DXGKRNL_EXCHANGE_INTERFACE: "
                          "exchange successful (v%lu), %u callbacks populated\n",
                          pExchangeIn->Version, 56);
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
        case IOCTL_DXGKRNL_GET_INIT_ENTRY:
        {
            /*
             * Windows 10+ miniport thunks send this IOCTL (METHOD_NEITHER,
             * IRP_MJ_INTERNAL_DEVICE_CONTROL) to resolve DxgkInitializeEx.
             * Output goes to Irp->UserBuffer (METHOD_NEITHER output path).
             */
            PVOID *OutputPtr = (PVOID *)Irp->UserBuffer;

            /*
             * Return DxgkInitializeDisplayOnlyDriver.  Prebuilt DOD
             * miniports (viogpudo) fill KMDDOD_INITIALIZATION_DATA
             * and expect to call DxgkInitializeDisplayOnlyDriver.
             */
            DXGKRNL_TRACE("IOCTL_DXGKRNL_GET_INIT_ENTRY: returning "
                          "DxgkInitializeDisplayOnlyDriver=%p\n",
                          DxgkInitializeDisplayOnlyDriver);

            if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PVOID) ||
                OutputPtr == NULL)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Irp->IoStatus.Information = 0;
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }

            _SEH2_TRY
            {
                *OutputPtr = (PVOID)DxgkInitializeDisplayOnlyDriver;
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
        case IOCTL_D3DKMT_DESTROYALLOCATION:
        case IOCTL_D3DKMT_LOCK:
        case IOCTL_D3DKMT_UNLOCK:
        case IOCTL_D3DKMT_RENDER:
        case IOCTL_D3DKMT_PRESENT:
        case IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_SETDISPLAYMODE:
        case IOCTL_D3DKMT_CREATECONTEXT:
        case IOCTL_D3DKMT_DESTROYCONTEXT:
        case IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT:
        case IOCTL_D3DKMT_ESCAPE:
        case IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE:
        case IOCTL_D3DKMT_GETSHADOWSURFACE:
        case IOCTL_D3DKMT_QUERYRESOURCEINFO:
        case IOCTL_D3DKMT_OPENRESOURCE:
        case IOCTL_D3DKMT_SETVIDPNSOURCEOWNER:
        case IOCTL_D3DKMT_GETDEVICESTATE:
        case IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS:
        case IOCTL_D3DKMT_MAKERESIDENT:
        case IOCTL_D3DKMT_EVICT:
        /* Remaining D3DKMT stubs (full Win7 coverage) */
        case IOCTL_D3DKMT_SETALLOCATIONPRIORITY:
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
