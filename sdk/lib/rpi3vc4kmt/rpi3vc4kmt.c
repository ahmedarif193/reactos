/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 user-mode support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private VC4 winsys helper over D3DKMT
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS

#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <limits.h>
#include <d3dkmthk.h>
#include <reactos/rpi3vc4kmt.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define RPI3VC4KMT_RESOURCE_LIST_MAGIC_V3  0x3352474cUL
#define RPI3VC4KMT_ESCAPE_PACKET_MAGIC_V2  0x32454756UL
#define RPI3VC4KMT_RESOURCE_CPU_DIRTY      0x00000001UL
#define RPI3VC4KMT_DMA_WORKSPACE_BYTES     RPI3VC4_SUBMIT_SCRATCH_MINIMUM
#define RPI3VC4KMT_SUBMIT_BUSY             ((NTSTATUS)0x80000011L)
#define RPI3VC4KMT_BUSY_RETRY_COUNT        4000U
#define RPI3VC4KMT_MAX_SHARED_PRIVATE_DATA (1024U * 1024U)

C_ASSERT(sizeof(RPI3VC4KMT_BO) == 24);
C_ASSERT(FIELD_OFFSET(RPI3VC4KMT_BO, CpuVa) == 8);
C_ASSERT(FIELD_OFFSET(RPI3VC4KMT_BO, Flags) == 16);
C_ASSERT(sizeof(RPI3VC4KMT_FENCE) == 24);
C_ASSERT(FIELD_OFFSET(RPI3VC4KMT_FENCE, Value) == 8);
C_ASSERT(FIELD_OFFSET(RPI3VC4KMT_FENCE, CpuValue) == 16);
C_ASSERT(sizeof(RPI3VC4_SUBMIT_RCL_SURFACE) == 12);
C_ASSERT(sizeof(RPI3VC4KMT_SUBMIT_CL) == 160);
C_ASSERT(FIELD_OFFSET(RPI3VC4KMT_SUBMIT_CL, Flags) == 156);
C_ASSERT(sizeof(RPI3VC4_ESCAPE_INFO) == 96);
C_ASSERT(FIELD_OFFSET(RPI3VC4_ESCAPE_INFO, V3dPhysical) == 40);

typedef struct _RPI3VC4KMT_ESCAPE_PACKET_HEADER_V2
{
    ULONG Magic;
    UINT PacketType;
    UINT PayloadBytes;
} RPI3VC4KMT_ESCAPE_PACKET_HEADER_V2;

typedef struct _RPI3VC4KMT_RESOURCE_LIST_HEADER_V3
{
    ULONG Magic;
    UINT ResourceCount;
    UINT DmaBufferBytes;
} RPI3VC4KMT_RESOURCE_LIST_HEADER_V3;

typedef struct _RPI3VC4KMT_RESOURCE_ENTRY
{
    D3DKMT_HANDLE hAllocation;
    ULONG Flags;
} RPI3VC4KMT_RESOURCE_ENTRY;

typedef struct _RPI3VC4KMT_COMMAND_PACKET_HEADER
{
    UINT CommandType;
    UINT PayloadBytes;
} RPI3VC4KMT_COMMAND_PACKET_HEADER;

#include <pshpack1.h>
typedef struct _RPI3VC4KMT_SIGNAL_BLOCK
{
    D3DKMT_HANDLE hSyncObject;
    ULONG64 FenceValue;
} RPI3VC4KMT_SIGNAL_BLOCK;
#include <poppack.h>

struct _RPI3VC4KMT_DEVICE
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hContext;
    D3DKMT_HANDLE hFence;
    volatile UINT64 *FenceCpuValue;
    UINT64 NextFenceValue;
    RPI3VC4_ESCAPE_INFO Info;
    CRITICAL_SECTION SubmitLock;
    BOOL SubmitLockInitialized;
    UCHAR *SubmitBuffer;
    SIZE_T SubmitBufferCapacity;
};

static NTSTATUS
Rpi3Vc4KmtCreateContext(
    _In_ D3DKMT_HANDLE hDevice,
    _Out_ D3DKMT_HANDLE *hContextOut)
{
    D3DKMT_CREATECONTEXT CreateContext;
    NTSTATUS Status;

    RtlZeroMemory(&CreateContext, sizeof(CreateContext));
    CreateContext.hDevice = hDevice;
    CreateContext.NodeOrdinal = 0;
    CreateContext.EngineAffinity = 1;
    CreateContext.ClientHint = D3DKMT_CLIENTHINT_OPENGL;
    Status = D3DKMTCreateContext(&CreateContext);
    if (!NT_SUCCESS(Status))
        return Status;
    if (CreateContext.hContext == 0)
        return STATUS_INVALID_DEVICE_STATE;

    *hContextOut = CreateContext.hContext;
    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi3Vc4KmtValidateInfo(
    _In_ const RPI3VC4_ESCAPE_INFO *Info)
{
    const ULONG RequiredCaps = RPI3VC4_CAP_POWER_CONTROL |
                               RPI3VC4_CAP_IDENT_VALID |
                               RPI3VC4_CAP_VALIDATED_CL_SUBMIT |
                               RPI3VC4_CAP_MONITORED_FENCE |
                               RPI3VC4_CAP_OPENGL_20;

    if (Info->Magic != RPI3VC4_ESCAPE_MAGIC ||
        Info->AbiVersion < RPI3VC4_ESCAPE_INFO_ABI_VERSION ||
        !Info->V3dReady || !NT_SUCCESS(Info->InitializationStatus) ||
        (Info->Caps & RequiredCaps) != RequiredCaps ||
        Info->V3dIdent0 != 0x02443356UL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
rpi3vc4kmt_open(
    _Outptr_ RPI3VC4KMT_DEVICE **DeviceOut)
{
    RPI3VC4KMT_DEVICE *Device;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;

    if (DeviceOut == NULL)
        return STATUS_INVALID_PARAMETER;
    *DeviceOut = NULL;

    Device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Device));
    if (Device == NULL)
        return STATUS_NO_MEMORY;

    InitializeCriticalSection(&Device->SubmitLock);
    Device->SubmitLockInitialized = TRUE;

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    wcscpy(OpenAdapter.DeviceName, L"\\\\.\\DISPLAY1");
    Status = D3DKMTOpenAdapterFromGdiDisplayName(&OpenAdapter);
    if (!NT_SUCCESS(Status))
        goto Failure;
    Device->hAdapter = OpenAdapter.hAdapter;

    RtlZeroMemory(&CreateDevice, sizeof(CreateDevice));
    CreateDevice.hAdapter = Device->hAdapter;
    Status = D3DKMTCreateDevice(&CreateDevice);
    if (!NT_SUCCESS(Status))
        goto Failure;
    Device->hDevice = CreateDevice.hDevice;

    RtlZeroMemory(&Device->Info, sizeof(Device->Info));
    Device->Info.Magic = RPI3VC4_ESCAPE_MAGIC;
    Device->Info.Op = RPI3VC4_ESCAPE_OP_QUERY_INFO;
    RtlZeroMemory(&Escape, sizeof(Escape));
    Escape.hAdapter = Device->hAdapter;
    Escape.hDevice = Device->hDevice;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = &Device->Info;
    Escape.PrivateDriverDataSize = sizeof(Device->Info);
    Status = D3DKMTEscape(&Escape);
    if (!NT_SUCCESS(Status))
        goto Failure;
    Status = Rpi3Vc4KmtValidateInfo(&Device->Info);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = Rpi3Vc4KmtCreateContext(Device->hDevice, &Device->hContext);
    if (!NT_SUCCESS(Status))
        goto Failure;

    RtlZeroMemory(&CreateSync, sizeof(CreateSync));
    CreateSync.hDevice = Device->hDevice;
    CreateSync.Info.Type = D3DDDI_MONITORED_FENCE;
    CreateSync.Info.Flags.NoGPUAccess = 1;
    CreateSync.Info.MonitoredFence.InitialFenceValue = 0;
    Status = D3DKMTCreateSynchronizationObject2(&CreateSync);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (CreateSync.hSyncObject == 0 ||
        CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Failure;
    }
    Device->hFence = CreateSync.hSyncObject;
    Device->FenceCpuValue = (volatile UINT64 *)
        CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress;

    *DeviceOut = Device;
    return STATUS_SUCCESS;

Failure:
    rpi3vc4kmt_close(Device);
    return Status;
}

VOID
rpi3vc4kmt_close(
    _In_opt_ RPI3VC4KMT_DEVICE *Device)
{
    if (Device == NULL)
        return;

    if (Device->hContext != 0)
    {
        D3DKMT_DESTROYCONTEXT DestroyContext;

        RtlZeroMemory(&DestroyContext, sizeof(DestroyContext));
        DestroyContext.hContext = Device->hContext;
        (void)D3DKMTDestroyContext(&DestroyContext);
    }
    if (Device->hFence != 0)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;

        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = Device->hFence;
        (void)D3DKMTDestroySynchronizationObject(&DestroySync);
    }
    if (Device->hDevice != 0)
    {
        D3DKMT_DESTROYDEVICE DestroyDevice;

        RtlZeroMemory(&DestroyDevice, sizeof(DestroyDevice));
        DestroyDevice.hDevice = Device->hDevice;
        (void)D3DKMTDestroyDevice(&DestroyDevice);
    }
    if (Device->hAdapter != 0)
    {
        D3DKMT_CLOSEADAPTER CloseAdapter;

        RtlZeroMemory(&CloseAdapter, sizeof(CloseAdapter));
        CloseAdapter.hAdapter = Device->hAdapter;
        (void)D3DKMTCloseAdapter(&CloseAdapter);
    }
    if (Device->SubmitLockInitialized)
        DeleteCriticalSection(&Device->SubmitLock);
    if (Device->SubmitBuffer != NULL)
        HeapFree(GetProcessHeap(), 0, Device->SubmitBuffer);
    HeapFree(GetProcessHeap(), 0, Device);
}

const RPI3VC4_ESCAPE_INFO *
rpi3vc4kmt_info(
    _In_ const RPI3VC4KMT_DEVICE *Device)
{
    return Device != NULL ? &Device->Info : NULL;
}

NTSTATUS
rpi3vc4kmt_bo_create(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ UINT Size,
    _Out_ RPI3VC4KMT_BO *Bo)
{
    D3DKMT_CREATEALLOCATION CreateAllocation;
    D3DDDI_ALLOCATIONINFO AllocationInfo;
    NTSTATUS Status;

    if (Device == NULL || Bo == NULL || Size == 0)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Bo, sizeof(*Bo));
    RtlZeroMemory(&CreateAllocation, sizeof(CreateAllocation));
    RtlZeroMemory(&AllocationInfo, sizeof(AllocationInfo));
    AllocationInfo.PrivateDriverDataSize = sizeof(Size);
    AllocationInfo.pPrivateDriverData = &Size;
    CreateAllocation.hDevice = Device->hDevice;
    CreateAllocation.NumAllocations = 1;
    CreateAllocation.pAllocationInfo = &AllocationInfo;
    Status = D3DKMTCreateAllocation(&CreateAllocation);
    if (!NT_SUCCESS(Status))
        return Status;
    if (AllocationInfo.hAllocation == 0)
        return STATUS_INVALID_DEVICE_STATE;

    Bo->hAllocation = AllocationInfo.hAllocation;
    Bo->Size = Size;
    return STATUS_SUCCESS;
}

NTSTATUS
rpi3vc4kmt_bo_map(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Inout_ RPI3VC4KMT_BO *Bo,
    _Outptr_ PVOID *CpuVaOut)
{
    D3DKMT_LOCK Lock;
    NTSTATUS Status;

    if (Device == NULL || Bo == NULL || CpuVaOut == NULL ||
        Bo->hAllocation == 0 || (Bo->Flags & RPI3VC4KMT_BO_SHADER) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Bo->CpuVa != NULL)
    {
        *CpuVaOut = Bo->CpuVa;
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice = Device->hDevice;
    Lock.hAllocation = Bo->hAllocation;
    Status = D3DKMTLock(&Lock);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Lock.pData == NULL)
    {
        D3DKMT_UNLOCK Unlock;
        D3DKMT_HANDLE Allocation = Bo->hAllocation;

        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = Device->hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Allocation;
        (void)D3DKMTUnlock(&Unlock);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Bo->CpuVa = Lock.pData;
    *CpuVaOut = Lock.pData;
    return STATUS_SUCCESS;
}

NTSTATUS
rpi3vc4kmt_bo_create_shader(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ UINT Size,
    _Out_ RPI3VC4KMT_BO *Bo)
{
    D3DKMT_LOCK Lock;
    D3DKMT_UNLOCK Unlock;
    D3DKMT_HANDLE Allocation;
    BOOLEAN Locked = FALSE;
    NTSTATUS Status;

    if (Device == NULL || Data == NULL || Size == 0 || Bo == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = rpi3vc4kmt_bo_create(Device, Size, Bo);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice = Device->hDevice;
    Lock.hAllocation = Bo->hAllocation;
    Status = D3DKMTLock(&Lock);
    if (!NT_SUCCESS(Status) || Lock.pData == NULL)
        goto Failure;
    Locked = TRUE;
    RtlCopyMemory(Lock.pData, Data, Size);
    MemoryBarrier();

    Allocation = Bo->hAllocation;
    RtlZeroMemory(&Unlock, sizeof(Unlock));
    Unlock.hDevice = Device->hDevice;
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &Allocation;
    Status = D3DKMTUnlock(&Unlock);
    if (!NT_SUCCESS(Status))
        goto Failure;
    Locked = FALSE;
    Bo->Flags = RPI3VC4KMT_BO_SHADER | RPI3VC4KMT_BO_CPU_DIRTY;
    return STATUS_SUCCESS;

Failure:
    if (Locked)
    {
        Allocation = Bo->hAllocation;
        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = Device->hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Allocation;
        (void)D3DKMTUnlock(&Unlock);
    }
    (void)rpi3vc4kmt_bo_destroy(Device, Bo);
    return NT_SUCCESS(Status) ? STATUS_INVALID_DEVICE_STATE : Status;
}

NTSTATUS
rpi3vc4kmt_bo_invalidate(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_BO *Bo,
    _In_ UINT Offset,
    _In_ UINT Length)
{
    D3DKMT_INVALIDATECACHE Invalidate;

    if (Device == NULL || Bo == NULL || Bo->hAllocation == 0 ||
        Length == 0 || Offset > Bo->Size || Length > Bo->Size - Offset)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Invalidate, sizeof(Invalidate));
    Invalidate.hDevice = Device->hDevice;
    Invalidate.hAllocation = Bo->hAllocation;
    Invalidate.Offset = Offset;
    Invalidate.Length = Length;
    return D3DKMTInvalidateCache(&Invalidate);
}

NTSTATUS
rpi3vc4kmt_shared_resource_info(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _Out_writes_bytes_to_opt_(RuntimeDataCapacity, *RuntimeDataSize)
        PVOID RuntimeData,
    _In_ UINT RuntimeDataCapacity,
    _Out_ UINT *RuntimeDataSize)
{
    D3DKMT_QUERYRESOURCEINFO Query;
    NTSTATUS Status;

    if (Device == NULL || hGlobalShare == 0 || RuntimeDataSize == NULL ||
        (RuntimeDataCapacity != 0 && RuntimeData == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Query, sizeof(Query));
    Query.hDevice = Device->hDevice;
    Query.hGlobalShare = hGlobalShare;
    Query.pPrivateRuntimeData = RuntimeData;
    Query.PrivateRuntimeDataSize = RuntimeDataCapacity;
    Status = D3DKMTQueryResourceInfo(&Query);
    *RuntimeDataSize = Query.PrivateRuntimeDataSize;
    return Status;
}

NTSTATUS
rpi3vc4kmt_bo_open_shared(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_ UINT Size,
    _Out_ RPI3VC4KMT_BO *Bo,
    _Out_ D3DKMT_HANDLE *hResource)
{
    D3DKMT_QUERYRESOURCEINFO Query;
    D3DKMT_OPENRESOURCE OpenResource;
    D3DDDI_OPENALLOCATIONINFO *OpenAllocations = NULL;
    PVOID PrivateRuntimeData = NULL;
    PVOID ResourcePrivateData = NULL;
    PVOID TotalPrivateData = NULL;
    NTSTATUS Status;

    if (Device == NULL || hGlobalShare == 0 || Size == 0 ||
        Bo == NULL || hResource == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Bo, sizeof(*Bo));
    *hResource = 0;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.hDevice = Device->hDevice;
    Query.hGlobalShare = hGlobalShare;
    Status = D3DKMTQueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Query.NumAllocations != 1 ||
        Query.PrivateRuntimeDataSize > RPI3VC4KMT_MAX_SHARED_PRIVATE_DATA ||
        Query.ResourcePrivateDriverDataSize >
            RPI3VC4KMT_MAX_SHARED_PRIVATE_DATA ||
        Query.TotalPrivateDriverDataSize >
            RPI3VC4KMT_MAX_SHARED_PRIVATE_DATA)
    {
        return STATUS_NOT_SUPPORTED;
    }

    OpenAllocations = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                sizeof(*OpenAllocations));
    if (OpenAllocations == NULL)
        return STATUS_NO_MEMORY;
    if (Query.PrivateRuntimeDataSize != 0)
        PrivateRuntimeData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                       Query.PrivateRuntimeDataSize);
    if (Query.ResourcePrivateDriverDataSize != 0)
        ResourcePrivateData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                        Query.ResourcePrivateDriverDataSize);
    if (Query.TotalPrivateDriverDataSize != 0)
        TotalPrivateData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                     Query.TotalPrivateDriverDataSize);
    if ((Query.PrivateRuntimeDataSize != 0 && PrivateRuntimeData == NULL) ||
        (Query.ResourcePrivateDriverDataSize != 0 &&
         ResourcePrivateData == NULL) ||
        (Query.TotalPrivateDriverDataSize != 0 && TotalPrivateData == NULL))
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }

    RtlZeroMemory(&OpenResource, sizeof(OpenResource));
    OpenResource.hDevice = Device->hDevice;
    OpenResource.hGlobalShare = hGlobalShare;
    OpenResource.NumAllocations = 1;
    OpenResource.pOpenAllocationInfo = OpenAllocations;
    OpenResource.pPrivateRuntimeData = PrivateRuntimeData;
    OpenResource.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    OpenResource.pResourcePrivateDriverData = ResourcePrivateData;
    OpenResource.ResourcePrivateDriverDataSize =
        Query.ResourcePrivateDriverDataSize;
    OpenResource.pTotalPrivateDriverDataBuffer = TotalPrivateData;
    OpenResource.TotalPrivateDriverDataBufferSize =
        Query.TotalPrivateDriverDataSize;
    Status = D3DKMTOpenResource(&OpenResource);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    *hResource = OpenResource.hResource;
    Bo->hAllocation = OpenAllocations[0].hAllocation;
    if (*hResource == 0 || Bo->hAllocation == 0)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Bo->Size = Size;
    Status = STATUS_SUCCESS;

Cleanup:
    if (!NT_SUCCESS(Status) && *hResource != 0)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = Device->hDevice;
        Destroy.hResource = *hResource;
        (void)D3DKMTDestroyAllocation(&Destroy);
        RtlZeroMemory(Bo, sizeof(*Bo));
        *hResource = 0;
    }
    if (TotalPrivateData != NULL)
        HeapFree(GetProcessHeap(), 0, TotalPrivateData);
    if (ResourcePrivateData != NULL)
        HeapFree(GetProcessHeap(), 0, ResourcePrivateData);
    if (PrivateRuntimeData != NULL)
        HeapFree(GetProcessHeap(), 0, PrivateRuntimeData);
    if (OpenAllocations != NULL)
        HeapFree(GetProcessHeap(), 0, OpenAllocations);
    return Status;
}

NTSTATUS
rpi3vc4kmt_bo_close_shared(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Inout_ RPI3VC4KMT_BO *Bo,
    _In_ D3DKMT_HANDLE hResource)
{
    D3DKMT_DESTROYALLOCATION Destroy;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Device == NULL || Bo == NULL || hResource == 0)
        return STATUS_INVALID_PARAMETER;

    if (Bo->CpuVa != NULL && Bo->hAllocation != 0)
    {
        D3DKMT_UNLOCK Unlock;
        D3DKMT_HANDLE Allocation = Bo->hAllocation;

        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = Device->hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Allocation;
        Status = D3DKMTUnlock(&Unlock);
    }

    RtlZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = Device->hDevice;
    Destroy.hResource = hResource;
    if (NT_SUCCESS(Status))
        Status = D3DKMTDestroyAllocation(&Destroy);
    else
        (void)D3DKMTDestroyAllocation(&Destroy);

    RtlZeroMemory(Bo, sizeof(*Bo));
    return Status;
}

NTSTATUS
rpi3vc4kmt_primary_info(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Out_ D3DKMT_HANDLE *hGlobalShare,
    _Out_ UINT *Width,
    _Out_ UINT *Height,
    _Out_ UINT *Pitch)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE GetPrimary;
    NTSTATUS Status;

    if (Device == NULL || hGlobalShare == NULL || Width == NULL ||
        Height == NULL || Pitch == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *hGlobalShare = 0;
    *Width = 0;
    *Height = 0;
    *Pitch = 0;

    if ((Device->Info.Caps & RPI3VC4_CAP_LINEAR_SCANOUT) == 0 ||
        Device->Info.ScreenWidth == 0 || Device->Info.ScreenHeight == 0 ||
        Device->Info.ScreenWidth > MAXUINT / sizeof(ULONG))
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&GetPrimary, sizeof(GetPrimary));
    GetPrimary.hAdapter = Device->hAdapter;
    GetPrimary.VidPnSourceId = 0;
    Status = D3DKMTGetSharedPrimaryHandle(&GetPrimary);
    if (!NT_SUCCESS(Status))
        return Status;
    if (GetPrimary.hSharedPrimary == 0)
        return STATUS_NOT_FOUND;

    *hGlobalShare = GetPrimary.hSharedPrimary;
    *Width = Device->Info.ScreenWidth;
    *Height = Device->Info.ScreenHeight;
    *Pitch = Device->Info.ScreenWidth * sizeof(ULONG);
    return STATUS_SUCCESS;
}

NTSTATUS
rpi3vc4kmt_present_primary(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_BO *Primary,
    _In_opt_ HWND Window,
    _In_ const RECT *DirtyRect)
{
    D3DKMT_PRESENT Present;

    if (Device == NULL || Primary == NULL || Primary->hAllocation == 0 ||
        DirtyRect == NULL || DirtyRect->left < 0 || DirtyRect->top < 0 ||
        DirtyRect->right <= DirtyRect->left ||
        DirtyRect->bottom <= DirtyRect->top ||
        (UINT)DirtyRect->right > Device->Info.ScreenWidth ||
        (UINT)DirtyRect->bottom > Device->Info.ScreenHeight)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Present, sizeof(Present));
    Present.hContext = Device->hContext;
    Present.hWindow = Window;
    Present.VidPnSourceId = 0;
    Present.hSource = Primary->hAllocation;
    Present.DstRect = *DirtyRect;
    Present.SrcRect = *DirtyRect;
    Present.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    Present.Flags.Blt = 1;
    Present.Flags.DstRectValid = 1;
    Present.Flags.SrcRectValid = 1;
    Present.Flags.RestrictVidPnSource = 1;
    return D3DKMTPresent(&Present);
}

NTSTATUS
rpi3vc4kmt_bo_destroy(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Inout_ RPI3VC4KMT_BO *Bo)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Device == NULL || Bo == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Bo->CpuVa != NULL && Bo->hAllocation != 0)
    {
        D3DKMT_UNLOCK Unlock;
        D3DKMT_HANDLE Allocation = Bo->hAllocation;

        RtlZeroMemory(&Unlock, sizeof(Unlock));
        Unlock.hDevice = Device->hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Allocation;
        Status = D3DKMTUnlock(&Unlock);
    }
    if (Bo->hAllocation != 0)
    {
        D3DKMT_DESTROYALLOCATION Destroy;
        D3DKMT_HANDLE Allocation = Bo->hAllocation;
        NTSTATUS DestroyStatus;

        RtlZeroMemory(&Destroy, sizeof(Destroy));
        Destroy.hDevice = Device->hDevice;
        Destroy.phAllocationList = &Allocation;
        Destroy.AllocationCount = 1;
        DestroyStatus = D3DKMTDestroyAllocation(&Destroy);
        if (NT_SUCCESS(Status))
            Status = DestroyStatus;
    }
    RtlZeroMemory(Bo, sizeof(*Bo));
    return Status;
}

static BOOLEAN
Rpi3Vc4KmtAddBytes(
    _In_ SIZE_T Current,
    _In_ SIZE_T Add,
    _Out_ SIZE_T *Result)
{
    if (Add > MAXULONG_PTR - Current)
        return FALSE;
    *Result = Current + Add;
    return TRUE;
}

static SIZE_T
Rpi3Vc4KmtAlign4(
    _In_ SIZE_T Value)
{
    return (Value + 3u) & ~(SIZE_T)3u;
}

static NTSTATUS
Rpi3Vc4KmtEnsureSubmitBuffer(
    _Inout_ RPI3VC4KMT_DEVICE *Device,
    _In_ SIZE_T RequiredBytes)
{
    UCHAR *Buffer;
    SIZE_T Capacity;

    if (Device->SubmitBufferCapacity >= RequiredBytes)
        return STATUS_SUCCESS;

    Capacity = max(Device->SubmitBufferCapacity, (SIZE_T)4096);
    while (Capacity < RequiredBytes)
    {
        if (Capacity > MAXULONG_PTR / 2)
            return STATUS_INTEGER_OVERFLOW;
        Capacity *= 2;
    }

    Buffer = Device->SubmitBuffer != NULL
        ? HeapReAlloc(GetProcessHeap(), 0, Device->SubmitBuffer, Capacity)
        : HeapAlloc(GetProcessHeap(), 0, Capacity);
    if (Buffer == NULL)
        return STATUS_NO_MEMORY;

    Device->SubmitBuffer = Buffer;
    Device->SubmitBufferCapacity = Capacity;
    return STATUS_SUCCESS;
}

NTSTATUS
rpi3vc4kmt_submit_cl(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_SUBMIT_CL *Submit,
    _Out_ RPI3VC4KMT_FENCE *FenceOut)
{
    RPI3VC4KMT_ESCAPE_PACKET_HEADER_V2 *PacketHeader;
    RPI3VC4KMT_RESOURCE_LIST_HEADER_V3 *ResourceHeader;
    RPI3VC4KMT_RESOURCE_ENTRY *Resources;
    RPI3VC4KMT_COMMAND_PACKET_HEADER *CommandHeader;
    RPI3VC4KMT_SIGNAL_BLOCK *Signal;
    RPI3VC4_SUBMIT_CL *Command;
    ULONG *ResourceFlags;
    D3DKMT_ESCAPE Escape;
    UCHAR *Bytes;
    UCHAR *Cursor;
    SIZE_T CommandBytes;
    SIZE_T TotalBytes;
    SIZE_T Offset;
    SIZE_T FixedBytes;
    UINT Index;
    ULONG Tries;
    NTSTATUS Status;

    if (Device == NULL || Submit == NULL || FenceOut == NULL ||
        Submit->BoCount == 0 || Submit->BoCount > RPI3VC4_MAX_SUBMIT_RESOURCES ||
        Submit->Bos == NULL || Submit->Width == 0 || Submit->Height == 0 ||
        (Submit->BinClSize != 0 && Submit->BinCl == NULL) ||
        (Submit->ShaderRecSize != 0 && Submit->ShaderRec == NULL) ||
        (Submit->UniformsSize != 0 && Submit->Uniforms == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(FenceOut, sizeof(*FenceOut));

    CommandBytes = sizeof(*Command);
    Offset = Rpi3Vc4KmtAlign4(CommandBytes);
    if (!Rpi3Vc4KmtAddBytes(Offset, Submit->BinClSize, &CommandBytes))
        return STATUS_INTEGER_OVERFLOW;
    Offset = Rpi3Vc4KmtAlign4(CommandBytes);
    if (!Rpi3Vc4KmtAddBytes(Offset, Submit->ShaderRecSize, &CommandBytes))
        return STATUS_INTEGER_OVERFLOW;
    Offset = Rpi3Vc4KmtAlign4(CommandBytes);
    if (!Rpi3Vc4KmtAddBytes(Offset, Submit->UniformsSize, &CommandBytes))
        return STATUS_INTEGER_OVERFLOW;
    Offset = Rpi3Vc4KmtAlign4(CommandBytes);
    if (Submit->BoCount > (MAXULONG_PTR - Offset) / sizeof(ULONG))
        return STATUS_INTEGER_OVERFLOW;
    CommandBytes = Offset + (SIZE_T)Submit->BoCount * sizeof(ULONG);

    FixedBytes = sizeof(*PacketHeader) + sizeof(*ResourceHeader) +
                 (SIZE_T)Submit->BoCount * sizeof(*Resources) +
                 sizeof(*CommandHeader) + sizeof(*Signal);
    if (!Rpi3Vc4KmtAddBytes(FixedBytes, CommandBytes, &TotalBytes) ||
        TotalBytes > MAXUINT ||
        CommandBytes > MAXUINT - sizeof(*Signal))
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    EnterCriticalSection(&Device->SubmitLock);
    Status = Rpi3Vc4KmtEnsureSubmitBuffer(Device, TotalBytes);
    if (!NT_SUCCESS(Status))
        goto Done;
    Bytes = Device->SubmitBuffer;
    RtlZeroMemory(Bytes, TotalBytes);
    Cursor = Bytes;
    PacketHeader = (RPI3VC4KMT_ESCAPE_PACKET_HEADER_V2 *)Cursor;
    Cursor += sizeof(*PacketHeader);
    ResourceHeader = (RPI3VC4KMT_RESOURCE_LIST_HEADER_V3 *)Cursor;
    Cursor += sizeof(*ResourceHeader);
    Resources = (RPI3VC4KMT_RESOURCE_ENTRY *)Cursor;
    Cursor += (SIZE_T)Submit->BoCount * sizeof(*Resources);
    CommandHeader = (RPI3VC4KMT_COMMAND_PACKET_HEADER *)Cursor;
    Cursor += sizeof(*CommandHeader);
    Signal = (RPI3VC4KMT_SIGNAL_BLOCK *)Cursor;
    Cursor += sizeof(*Signal);
    Command = (RPI3VC4_SUBMIT_CL *)Cursor;

    PacketHeader->Magic = RPI3VC4KMT_ESCAPE_PACKET_MAGIC_V2;
    PacketHeader->PacketType = 2;
    PacketHeader->PayloadBytes = (UINT)(TotalBytes - sizeof(*PacketHeader));
    ResourceHeader->Magic = RPI3VC4KMT_RESOURCE_LIST_MAGIC_V3;
    ResourceHeader->ResourceCount = Submit->BoCount;
    ResourceHeader->DmaBufferBytes = RPI3VC4KMT_DMA_WORKSPACE_BYTES +
                                     ((sizeof(RPI3VC4_DMA_PACKET) + 15u) & ~15u);
    CommandHeader->CommandType = 2;
    CommandHeader->PayloadBytes = (UINT)(sizeof(*Signal) + CommandBytes);

    Signal->hSyncObject = Device->hFence;
    Signal->FenceValue = Device->NextFenceValue + 1;

    Command->Version = RPI3VC4_SUBMIT_CL_VERSION;
    Command->Size = sizeof(*Command);
    Command->BoHandleCount = Submit->BoCount;
    Command->Width = Submit->Width;
    Command->Height = Submit->Height;
    Command->MinXTile = Submit->MinXTile;
    Command->MinYTile = Submit->MinYTile;
    Command->MaxXTile = Submit->MaxXTile;
    Command->MaxYTile = Submit->MaxYTile;
    Command->ColorRead = Submit->ColorRead;
    Command->ColorWrite = Submit->ColorWrite;
    Command->ZsRead = Submit->ZsRead;
    Command->ZsWrite = Submit->ZsWrite;
    Command->MsaaColorWrite = Submit->MsaaColorWrite;
    Command->MsaaZsWrite = Submit->MsaaZsWrite;
    Command->ClearColor[0] = Submit->ClearColor[0];
    Command->ClearColor[1] = Submit->ClearColor[1];
    Command->ClearZ = Submit->ClearZ;
    Command->ClearS = Submit->ClearS;
    Command->Flags = Submit->Flags;
    Command->ScratchBytes = RPI3VC4KMT_DMA_WORKSPACE_BYTES;

    Offset = Rpi3Vc4KmtAlign4(sizeof(*Command));
    Command->BinClOffset = (ULONG)Offset;
    Command->BinClSize = Submit->BinClSize;
    if (Submit->BinClSize != 0)
        RtlCopyMemory((UCHAR *)Command + Offset, Submit->BinCl,
                      Submit->BinClSize);
    Offset = Rpi3Vc4KmtAlign4(Offset + Submit->BinClSize);
    Command->ShaderRecOffset = (ULONG)Offset;
    Command->ShaderRecSize = Submit->ShaderRecSize;
    Command->ShaderRecCount = Submit->ShaderRecCount;
    if (Submit->ShaderRecSize != 0)
        RtlCopyMemory((UCHAR *)Command + Offset, Submit->ShaderRec,
                      Submit->ShaderRecSize);
    Offset = Rpi3Vc4KmtAlign4(Offset + Submit->ShaderRecSize);
    Command->UniformsOffset = (ULONG)Offset;
    Command->UniformsSize = Submit->UniformsSize;
    if (Submit->UniformsSize != 0)
        RtlCopyMemory((UCHAR *)Command + Offset, Submit->Uniforms,
                      Submit->UniformsSize);
    Offset = Rpi3Vc4KmtAlign4(Offset + Submit->UniformsSize);
    Command->ResourceFlagsOffset = (ULONG)Offset;
    ResourceFlags = (ULONG *)((UCHAR *)Command + Offset);

    for (Index = 0; Index < Submit->BoCount; Index++)
    {
        const RPI3VC4KMT_BO *Bo = Submit->Bos[Index];

        if (Bo == NULL || Bo->hAllocation == 0 || Bo->Size == 0 ||
            (Bo->Flags & ~(RPI3VC4KMT_BO_SHADER |
                           RPI3VC4KMT_BO_CPU_DIRTY)) != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Done;
        }
        Resources[Index].hAllocation = Bo->hAllocation;
        Resources[Index].Flags =
            (Bo->Flags & RPI3VC4KMT_BO_CPU_DIRTY) != 0
                ? RPI3VC4KMT_RESOURCE_CPU_DIRTY
                : 0;
        ResourceFlags[Index] =
            (Bo->Flags & RPI3VC4KMT_BO_SHADER) != 0
                ? RPI3VC4_RESOURCE_SHADER
                : 0;
    }

    MemoryBarrier();
    RtlZeroMemory(&Escape, sizeof(Escape));
    Escape.hAdapter = Device->hAdapter;
    Escape.hDevice = Device->hDevice;
    Escape.hContext = Device->hContext;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = Bytes;
    Escape.PrivateDriverDataSize = (UINT)TotalBytes;
    for (Tries = 0; Tries < RPI3VC4KMT_BUSY_RETRY_COUNT; Tries++)
    {
        Status = D3DKMTEscape(&Escape);
        if (Status != RPI3VC4KMT_SUBMIT_BUSY)
            break;
        Sleep(1);
    }
    if (NT_SUCCESS(Status))
    {
        Device->NextFenceValue++;
        FenceOut->hSyncObject = Device->hFence;
        FenceOut->Value = Device->NextFenceValue;
        FenceOut->CpuValue = Device->FenceCpuValue;
    }

Done:
    LeaveCriticalSection(&Device->SubmitLock);
    return Status;
}

NTSTATUS
rpi3vc4kmt_wait(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_FENCE *Fence,
    _In_ DWORD TimeoutMs)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;
    D3DKMT_HANDLE Handle;
    UINT64 Value;
    DWORD Start;

    if (Device == NULL || Fence == NULL || Fence->CpuValue == NULL ||
        Fence->hSyncObject == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (*Fence->CpuValue >= Fence->Value)
        return STATUS_SUCCESS;
    if (TimeoutMs == 0)
        return STATUS_IO_TIMEOUT;

    if (TimeoutMs == INFINITE)
    {
        Handle = Fence->hSyncObject;
        Value = Fence->Value;
        RtlZeroMemory(&Wait, sizeof(Wait));
        Wait.hDevice = Device->hDevice;
        Wait.ObjectCount = 1;
        Wait.ObjectHandleArray = &Handle;
        Wait.FenceValueArray = &Value;
        return D3DKMTWaitForSynchronizationObjectFromCpu(&Wait);
    }

    Start = GetTickCount();
    for (;;)
    {
        if (*Fence->CpuValue >= Fence->Value)
            return STATUS_SUCCESS;
        if (GetTickCount() - Start >= TimeoutMs)
            return STATUS_IO_TIMEOUT;
        Sleep(1);
    }
}
