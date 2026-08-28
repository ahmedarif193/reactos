/*
 * PROJECT:     ReactOS Raspberry Pi 5 VC4 user-mode support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private VC4 winsys helper library over D3DKMT.
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS

#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <d3dkmthk.h>
#include <reactos/vc4kmt.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#define VC4KMT_DMA_PACKET_MAGIC       0x52355644u
#define VC4KMT_DMA_OP_V3D_JOB         2
#define VC4KMT_DMA_OP_TFU_JOB         3
#define VC4KMT_DMA_OP_CSD_JOB         4
#define VC4KMT_RESOURCE_LIST_MAGIC_V2 0x3252474cUL
#define VC4KMT_MAX_SUBMIT_RESOURCES   4096u
#define VC4KMT_MAX_PRIMARY_PRIVATE_DATA (1024u * 1024u)

#define VC4KMT_REQUIRED_CAPS \
    (RPI5VC4_CAP_GPUVA_MAP | \
     RPI5VC4_CAP_ALLOCATION_RELOCATION | \
     RPI5VC4_CAP_MONITORED_FENCE | \
     RPI5VC4_CAP_SUBMIT_SIGNAL | \
     RPI5VC4_CAP_CPU_WAIT_SIGNAL)

typedef struct _VC4KMT_DMA_PACKET
{
    ULONG Magic;
    ULONG Op;
    ULONG Length;
    ULONG Reserved;
    union
    {
        struct
        {
            ULONG BclStart;
            ULONG BclEnd;
            ULONG RclStart;
            ULONG RclEnd;
            ULONG BclAllocIndexPlusOne;
            ULONG RclAllocIndexPlusOne;
            ULONG Qma;
            ULONG Qms;
            ULONG Qts;
        } V3dJob;
        struct
        {
            ULONG Regs[12];
        } TfuJob;
        struct
        {
            ULONG Cfg[8];
        } CsdJob;
        ULONG Pad[12];
    };
} VC4KMT_DMA_PACKET;

typedef struct _VC4KMT_ESCAPE_PACKET_HEADER
{
    USHORT PacketType;
    USHORT PayloadBytes;
} VC4KMT_ESCAPE_PACKET_HEADER;

typedef struct _VC4KMT_RESOURCE_LIST_HEADER
{
    ULONG Magic;
    UINT ResourceCount;
} VC4KMT_RESOURCE_LIST_HEADER;

typedef struct _VC4KMT_COMMAND_PACKET_HEADER
{
    UINT CommandType;
    UINT PayloadBytes;
} VC4KMT_COMMAND_PACKET_HEADER;

#include <pshpack1.h>
typedef struct _VC4KMT_SIGNAL_BLOCK
{
    D3DKMT_HANDLE hSyncObject;
    ULONG64 FenceValue;
} VC4KMT_SIGNAL_BLOCK;
#include <poppack.h>

typedef struct _VC4KMT_SUBMIT_SIGNAL_VIEW
{
    VC4KMT_ESCAPE_PACKET_HEADER *PacketHeader;
    VC4KMT_RESOURCE_LIST_HEADER *ResourceHeader;
    VC4KMT_RESOURCE *Resources;
    VC4KMT_COMMAND_PACKET_HEADER *Command;
    VC4KMT_SIGNAL_BLOCK *Signal;
    VC4KMT_DMA_PACKET *Packet;
    UINT PrivateBytes;
} VC4KMT_SUBMIT_SIGNAL_VIEW;

typedef struct _VC4KMT_BO_RECORD
{
    struct _VC4KMT_BO_RECORD *Next;
    D3DKMT_HANDLE hAllocation;
    BOOL Mapped;
} VC4KMT_BO_RECORD;

static VOID
Vc4KmtBuildSubmitSignalView(
    _Out_writes_bytes_(BufferBytes) UCHAR *Bytes,
    _In_ UINT BufferBytes,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_SUBMIT_SIGNAL_VIEW *View)
{
    UCHAR *Cursor = Bytes;

    View->PacketHeader = (VC4KMT_ESCAPE_PACKET_HEADER *)Cursor;
    Cursor += sizeof(*View->PacketHeader);

    View->ResourceHeader = (VC4KMT_RESOURCE_LIST_HEADER *)Cursor;
    Cursor += sizeof(*View->ResourceHeader);

    View->Resources = (VC4KMT_RESOURCE *)Cursor;
    Cursor += ResourceCount * sizeof(*View->Resources);

    View->Command = (VC4KMT_COMMAND_PACKET_HEADER *)Cursor;
    Cursor += sizeof(*View->Command);

    View->Signal = (VC4KMT_SIGNAL_BLOCK *)Cursor;
    Cursor += sizeof(*View->Signal);

    View->Packet = (VC4KMT_DMA_PACKET *)Cursor;
    Cursor += sizeof(*View->Packet);

    View->PrivateBytes = (UINT)(Cursor - Bytes);
    UNREFERENCED_PARAMETER(BufferBytes);
}

struct _VC4KMT_DEVICE
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hContext[RPI5VC4_GPU_NODE_COUNT];
    D3DKMT_HANDLE hPagingQueue;
    D3DKMT_HANDLE hFence[RPI5VC4_GPU_NODE_COUNT];
    volatile UINT64 *FenceCpuValue[RPI5VC4_GPU_NODE_COUNT];
    RPI5VC4_ESCAPE_INFO Info;
    UINT64 NextFenceValue[RPI5VC4_GPU_NODE_COUNT];
    D3DKMT_HANDLE hPrimaryResource;
    D3DKMT_HANDLE hPrimaryGlobalShare;
    D3DKMT_HANDLE hPrimaryAllocation;
    ULONG PrimaryGpuVa;
    UINT64 PrimaryGpuVaSize;
    UINT PrimaryWidth;
    UINT PrimaryHeight;
    UINT PrimaryPitch;
    BOOL Fake;
    ULONG FakeNextGpuVa;
    VC4KMT_BO_RECORD *BoList;
};

static NTSTATUS
Vc4KmtTrackBo(
    _Inout_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hAllocation)
{
    VC4KMT_BO_RECORD *Record;

    Record = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Record));
    if (Record == NULL)
        return STATUS_NO_MEMORY;

    Record->hAllocation = hAllocation;
    Record->Next = Device->BoList;
    Device->BoList = Record;
    return STATUS_SUCCESS;
}

static BOOL
Vc4KmtMarkBoMapped(
    _Inout_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hAllocation)
{
    VC4KMT_BO_RECORD *Record;

    for (Record = Device->BoList; Record != NULL; Record = Record->Next)
    {
        if (Record->hAllocation == hAllocation)
        {
            Record->Mapped = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}

static VOID
Vc4KmtUntrackBo(
    _Inout_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hAllocation)
{
    VC4KMT_BO_RECORD **Link = &Device->BoList;

    while (*Link != NULL)
    {
        VC4KMT_BO_RECORD *Record = *Link;

        if (Record->hAllocation == hAllocation)
        {
            *Link = Record->Next;
            HeapFree(GetProcessHeap(), 0, Record);
            return;
        }
        Link = &Record->Next;
    }
}

static VOID
Vc4KmtFreeBoRecords(
    _Inout_ VC4KMT_DEVICE *Device)
{
    while (Device->BoList != NULL)
    {
        VC4KMT_BO_RECORD *Record = Device->BoList;

        Device->BoList = Record->Next;
        HeapFree(GetProcessHeap(), 0, Record);
    }
}

static NTSTATUS
Vc4KmtCollectMappedResources(
    _In_ VC4KMT_DEVICE *Device,
    _Outptr_result_buffer_maybenull_(*ResourceCount)
        VC4KMT_RESOURCE **Resources,
    _Out_ UINT *ResourceCount)
{
    VC4KMT_BO_RECORD *Record;
    VC4KMT_RESOURCE *List;
    UINT Count = 0;
    UINT Index = 0;

    *Resources = NULL;
    *ResourceCount = 0;
    if (Device->Fake)
        return STATUS_SUCCESS;

    for (Record = Device->BoList; Record != NULL; Record = Record->Next)
    {
        if (Record->Mapped && ++Count > VC4KMT_MAX_SUBMIT_RESOURCES)
            return STATUS_INVALID_BUFFER_SIZE;
    }
    if (Count == 0)
        return STATUS_SUCCESS;

    List = HeapAlloc(GetProcessHeap(), 0, Count * sizeof(*List));
    if (List == NULL)
        return STATUS_NO_MEMORY;

    for (Record = Device->BoList; Record != NULL; Record = Record->Next)
    {
        if (!Record->Mapped)
            continue;
        List[Index].hAllocation = Record->hAllocation;
        List[Index].Flags = VC4KMT_RESOURCE_CPU_DIRTY;
        Index++;
    }

    *Resources = List;
    *ResourceCount = Count;
    return STATUS_SUCCESS;
}

/*
 * VC4KMT_FAKE=1: CPU-only stand-in device (malloc-backed BOs, no-op
 * submits).  Lets the whole UMD above the winsys — v3dv device create,
 * NIR->QPU compiles, descriptor plumbing — run on QEMU where no rpi5vc4
 * adapter exists, so pure-CPU hangs reproduce without Pi hardware.
 */
static BOOL
Vc4KmtFakeRequested(VOID)
{
    return GetEnvironmentVariableW(L"VC4KMT_FAKE", NULL, 0) != 0;
}

static NTSTATUS
Vc4KmtOpenFake(VC4KMT_DEVICE *Device)
{
    RtlZeroMemory(&Device->Info, sizeof(Device->Info));
    Device->Info.Magic = RPI5VC4_ESCAPE_MAGIC;
    Device->Info.Op = RPI5VC4_ESCAPE_OP_QUERY_INFO;
    Device->Info.V3dReady = 1;
    Device->Info.V3dVersion = 71;
    Device->Info.V3dHubIdent[0] = 0x42554856;
    Device->Info.V3dHubIdent[1] = 0x00081117;
    Device->Info.V3dHubIdent[2] = 0x00000100;
    Device->Info.V3dHubIdent[3] = 0x00020A00;
    Device->Info.V3dCoreIdent[0] = 0x07443356;
    Device->Info.V3dCoreIdent[1] = 0x40000441;
    Device->Info.V3dCoreIdent[2] = 0;
    Device->Info.SlabGpuVa = 0x01000000;
    Device->Info.SlabPhysical = 0x40000000ULL;
    Device->Info.SlabSize = 64 * 1024 * 1024;
    Device->Info.ScreenWidth = 1280;
    Device->Info.ScreenHeight = 720;
    Device->Info.ScreenPitch = 1280 * 4;
    Device->Info.AbiVersion = RPI5VC4_ESCAPE_INFO_ABI_VERSION;
    Device->Info.Caps = RPI5VC4_CAP_GPUVA_MAP |
                        RPI5VC4_CAP_ALLOCATION_RELOCATION |
                        RPI5VC4_CAP_MONITORED_FENCE |
                        RPI5VC4_CAP_SUBMIT_SIGNAL |
                        RPI5VC4_CAP_CPU_WAIT_SIGNAL |
                        RPI5VC4_CAP_WIN32_PRESENT |
                        RPI5VC4_CAP_LINEAR_SCANOUT |
                        RPI5VC4_CAP_CL_SUBMIT |
                        RPI5VC4_CAP_TFU_SUBMIT |
                        RPI5VC4_CAP_CSD_SUBMIT |
                        RPI5VC4_CAP_CACHE_FLUSH;
    Device->Info.NodeCount = 3;
    Device->Info.MaxPendingSubmits = 64;
    Device->Info.AllocationAlignment = 4096;
    Device->Info.TfuRegisterCount = 12;
    Device->Info.CsdConfigCount = 8;
    Device->Info.LinearFormatMask = RPI5VC4_LINEAR_FORMAT_X8R8G8B8 |
                                    RPI5VC4_LINEAR_FORMAT_A8R8G8B8;
    Device->Fake = TRUE;
    Device->FakeNextGpuVa = (ULONG)RPI5VC4_DYNAMIC_GPUVA_START;
    RtlZeroMemory(Device->NextFenceValue, sizeof(Device->NextFenceValue));
    return STATUS_SUCCESS;
}

static NTSTATUS
Vc4KmtValidateInfo(
    _In_ const RPI5VC4_ESCAPE_INFO *Info)
{
    if (Info->Magic != RPI5VC4_ESCAPE_MAGIC ||
        Info->AbiVersion < RPI5VC4_ESCAPE_INFO_ABI_VERSION)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if ((Info->Caps & VC4KMT_REQUIRED_CAPS) != VC4KMT_REQUIRED_CAPS)
        return STATUS_NOT_SUPPORTED;

    if (Info->V3dReady && !(Info->Caps & RPI5VC4_CAP_CL_SUBMIT))
        return STATUS_NOT_SUPPORTED;

    if (Info->SlabSize == 0 || Info->AllocationAlignment == 0)
        return STATUS_INVALID_DEVICE_REQUEST;

    if (Info->NodeCount <= RPI5VC4_NODE_3D ||
        ((Info->Caps & RPI5VC4_CAP_TFU_SUBMIT) != 0 &&
         Info->NodeCount <= RPI5VC4_NODE_TFU) ||
        ((Info->Caps & RPI5VC4_CAP_CSD_SUBMIT) != 0 &&
         Info->NodeCount <= RPI5VC4_NODE_CSD))
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
Vc4KmtQueryInfo(
    _Inout_ VC4KMT_DEVICE *Device)
{
    RPI5VC4_ESCAPE_INFO Info;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Magic = RPI5VC4_ESCAPE_MAGIC;
    Info.Op = RPI5VC4_ESCAPE_OP_QUERY_INFO;

    RtlZeroMemory(&Escape, sizeof(Escape));
    Escape.hAdapter = Device->hAdapter;
    Escape.hDevice = Device->hDevice;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = &Info;
    Escape.PrivateDriverDataSize = sizeof(Info);
    Status = D3DKMTEscape(&Escape);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Vc4KmtValidateInfo(&Info);
    if (NT_SUCCESS(Status))
        Device->Info = Info;
    return Status;
}

static NTSTATUS
Vc4KmtFreeGpuVaRange(
    _In_ VC4KMT_DEVICE *Device,
    _In_ D3DGPU_VIRTUAL_ADDRESS BaseAddress,
    _In_ D3DGPU_SIZE_T Size)
{
    D3DKMT_FREEGPUVIRTUALADDRESS FreeGpuVa;

    if (BaseAddress == 0 || Size == 0 || Device->Fake)
        return STATUS_SUCCESS;

    RtlZeroMemory(&FreeGpuVa, sizeof(FreeGpuVa));
    FreeGpuVa.hAdapter = Device->hAdapter;
    FreeGpuVa.BaseAddress = BaseAddress;
    FreeGpuVa.Size = Size;
    return D3DKMTFreeGpuVirtualAddress(&FreeGpuVa);
}

static VOID
Vc4KmtClosePrimary(
    _Inout_ VC4KMT_DEVICE *Device)
{
    (void)Vc4KmtFreeGpuVaRange(Device,
                               Device->PrimaryGpuVa,
                               Device->PrimaryGpuVaSize);

    if (Device->hPrimaryResource != 0 && !Device->Fake)
    {
        D3DKMT_DESTROYALLOCATION DestroyData;

        RtlZeroMemory(&DestroyData, sizeof(DestroyData));
        DestroyData.hDevice = Device->hDevice;
        DestroyData.hResource = Device->hPrimaryResource;
        (void)D3DKMTDestroyAllocation(&DestroyData);
    }

    Device->hPrimaryResource = 0;
    Device->hPrimaryGlobalShare = 0;
    Device->hPrimaryAllocation = 0;
    Device->PrimaryGpuVa = 0;
    Device->PrimaryGpuVaSize = 0;
    Device->PrimaryWidth = 0;
    Device->PrimaryHeight = 0;
    Device->PrimaryPitch = 0;
}

static NTSTATUS
Vc4KmtCreateDevice(
    _In_ D3DKMT_HANDLE hAdapter,
    _Out_ D3DKMT_HANDLE *hDeviceOut)
{
    D3DKMT_CREATEDEVICE CreateDevice;
    NTSTATUS Status;

    RtlZeroMemory(&CreateDevice, sizeof(CreateDevice));
    CreateDevice.hAdapter = hAdapter;
    Status = D3DKMTCreateDevice(&CreateDevice);
    if (!NT_SUCCESS(Status))
        return Status;

    *hDeviceOut = CreateDevice.hDevice;
    return STATUS_SUCCESS;
}

static NTSTATUS
Vc4KmtCreateContext(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ UINT NodeOrdinal,
    _Out_ D3DKMT_HANDLE *hContextOut)
{
    D3DKMT_CREATECONTEXT CreateContext;
    NTSTATUS Status;

    RtlZeroMemory(&CreateContext, sizeof(CreateContext));
    CreateContext.hDevice = hDevice;
    CreateContext.NodeOrdinal = NodeOrdinal;
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

NTSTATUS
vc4kmt_open(
    _Outptr_ VC4KMT_DEVICE **DeviceOut)
{
    VC4KMT_DEVICE *Device;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;
    UINT NodeOrdinal;

    if (DeviceOut == NULL)
        return STATUS_INVALID_PARAMETER;

    *DeviceOut = NULL;

    Device = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Device));
    if (Device == NULL)
        return STATUS_NO_MEMORY;

    if (Vc4KmtFakeRequested())
    {
        Status = Vc4KmtOpenFake(Device);
        *DeviceOut = Device;
        return Status;
    }

    RtlZeroMemory(&OpenAdapter, sizeof(OpenAdapter));
    wcscpy(OpenAdapter.DeviceName, L"\\\\.\\DISPLAY1");
    Status = D3DKMTOpenAdapterFromGdiDisplayName(&OpenAdapter);
    if (!NT_SUCCESS(Status))
        goto fail;

    Device->hAdapter = OpenAdapter.hAdapter;

    Status = Vc4KmtCreateDevice(Device->hAdapter, &Device->hDevice);
    if (!NT_SUCCESS(Status))
        goto fail;

    /*
     * WDDM2 GPU virtual addressing binds each allocation through a paging
     * queue. D3DKMTMapGpuVirtualAddress validates hPagingQueue up front, so
     * the winsys owns the queue used to publish the per-process V3D PTEs.
     */
    {
        D3DKMT_CREATEPAGINGQUEUE CreatePagingQueue;

        RtlZeroMemory(&CreatePagingQueue, sizeof(CreatePagingQueue));
        CreatePagingQueue.hDevice = Device->hDevice;
        Status = D3DKMTCreatePagingQueue(&CreatePagingQueue);
        if (!NT_SUCCESS(Status))
            goto fail;

        Device->hPagingQueue = CreatePagingQueue.hPagingQueue;
    }

    RtlZeroMemory(&Device->Info, sizeof(Device->Info));
    Device->Info.Magic = RPI5VC4_ESCAPE_MAGIC;
    Device->Info.Op = RPI5VC4_ESCAPE_OP_QUERY_INFO;

    RtlZeroMemory(&Escape, sizeof(Escape));
    Escape.hAdapter = Device->hAdapter;
    Escape.hDevice = Device->hDevice;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = &Device->Info;
    Escape.PrivateDriverDataSize = sizeof(Device->Info);
    Status = D3DKMTEscape(&Escape);
    if (!NT_SUCCESS(Status))
        goto fail;

    Status = Vc4KmtValidateInfo(&Device->Info);
    if (!NT_SUCCESS(Status))
        goto fail;

    Status = Vc4KmtCreateContext(Device->hDevice, RPI5VC4_NODE_3D,
                                 &Device->hContext[RPI5VC4_NODE_3D]);
    if (!NT_SUCCESS(Status))
        goto fail;
    if ((Device->Info.Caps & RPI5VC4_CAP_TFU_SUBMIT) != 0)
    {
        Status = Vc4KmtCreateContext(Device->hDevice, RPI5VC4_NODE_TFU,
                                     &Device->hContext[RPI5VC4_NODE_TFU]);
        if (!NT_SUCCESS(Status))
            goto fail;
    }
    if ((Device->Info.Caps & RPI5VC4_CAP_CSD_SUBMIT) != 0)
    {
        Status = Vc4KmtCreateContext(Device->hDevice, RPI5VC4_NODE_CSD,
                                     &Device->hContext[RPI5VC4_NODE_CSD]);
        if (!NT_SUCCESS(Status))
            goto fail;
    }

    /*
     * Hardware nodes retire independently.  A shared monitored timeline
     * would let a later TFU/CSD signal satisfy an older 3D fence and expose
     * the latter's BOs for reuse while its control list is still running.
     * Give each node its own monotonically increasing timeline instead.
     */
    for (NodeOrdinal = 0;
         NodeOrdinal < RPI5VC4_GPU_NODE_COUNT;
         ++NodeOrdinal)
    {
        D3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
        NTSTATUS SyncStatus;

        if (Device->hContext[NodeOrdinal] == 0)
            continue;

        RtlZeroMemory(&CreateSync, sizeof(CreateSync));
        CreateSync.hDevice = Device->hDevice;
        CreateSync.Info.Type = D3DDDI_MONITORED_FENCE;
        CreateSync.Info.Flags.NoGPUAccess = 1;
        CreateSync.Info.MonitoredFence.InitialFenceValue = 0;
        SyncStatus = D3DKMTCreateSynchronizationObject2(&CreateSync);
        if (!NT_SUCCESS(SyncStatus))
        {
            Status = SyncStatus;
            goto fail;
        }
        if (CreateSync.hSyncObject == 0 || CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress == NULL)
        {
            if (CreateSync.hSyncObject != 0)
            {
                D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;

                RtlZeroMemory(&DestroySync, sizeof(DestroySync));
                DestroySync.hSyncObject = CreateSync.hSyncObject;
                (void)D3DKMTDestroySynchronizationObject(&DestroySync);
            }
            Status = STATUS_INVALID_DEVICE_STATE;
            goto fail;
        }
        Device->hFence[NodeOrdinal] = CreateSync.hSyncObject;
        Device->FenceCpuValue[NodeOrdinal] =
            (volatile UINT64 *)
                CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    }

    RtlZeroMemory(Device->NextFenceValue, sizeof(Device->NextFenceValue));
    *DeviceOut = Device;
    return STATUS_SUCCESS;

fail:
    vc4kmt_close(Device);
    return Status;
}

VOID
vc4kmt_close(
    _In_opt_ VC4KMT_DEVICE *Device)
{
    UINT NodeOrdinal;

    if (Device == NULL)
        return;

    Vc4KmtClosePrimary(Device);

    for (NodeOrdinal = RPI5VC4_GPU_NODE_COUNT; NodeOrdinal != 0; )
    {
        D3DKMT_DESTROYCONTEXT DestroyContext;

        NodeOrdinal--;
        if (Device->hContext[NodeOrdinal] == 0)
            continue;
        RtlZeroMemory(&DestroyContext, sizeof(DestroyContext));
        DestroyContext.hContext = Device->hContext[NodeOrdinal];
        (void)D3DKMTDestroyContext(&DestroyContext);
        Device->hContext[NodeOrdinal] = 0;
    }

    for (NodeOrdinal = RPI5VC4_GPU_NODE_COUNT; NodeOrdinal != 0; )
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;

        NodeOrdinal--;
        if (Device->hFence[NodeOrdinal] == 0)
            continue;
        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = Device->hFence[NodeOrdinal];
        (void)D3DKMTDestroySynchronizationObject(&DestroySync);
        Device->hFence[NodeOrdinal] = 0;
        Device->FenceCpuValue[NodeOrdinal] = NULL;
    }

    if (Device->hPagingQueue != 0)
    {
        D3DDDI_DESTROYPAGINGQUEUE DestroyPagingQueue;

        RtlZeroMemory(&DestroyPagingQueue, sizeof(DestroyPagingQueue));
        DestroyPagingQueue.hPagingQueue = Device->hPagingQueue;
        (void)D3DKMTDestroyPagingQueue(&DestroyPagingQueue);
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

    Vc4KmtFreeBoRecords(Device);
    HeapFree(GetProcessHeap(), 0, Device);
}

const RPI5VC4_ESCAPE_INFO *
vc4kmt_info(
    _In_ const VC4KMT_DEVICE *Device)
{
    return Device ? &Device->Info : NULL;
}

NTSTATUS
vc4kmt_bo_create(
    _In_ VC4KMT_DEVICE *Device,
    _In_ UINT Size,
    _Out_ VC4KMT_BO *Bo)
{
    D3DKMT_CREATEALLOCATION CreateData;
    D3DDDI_ALLOCATIONINFO AllocationInfo;
    D3DDDI_MAPGPUVIRTUALADDRESS MapGpuVa;
    NTSTATUS Status;
    UINT SizeInPages;
    UINT64 MappedSize;

    if (Device == NULL || Bo == NULL || Size == 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Bo, sizeof(*Bo));

    if (Device->Fake)
    {
        UINT Aligned = (Size + 4095u) & ~4095u;

        Bo->CpuVa = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Aligned);
        if (Bo->CpuVa == NULL)
            return STATUS_NO_MEMORY;
        Bo->hAllocation = 0x46414B45;
        Bo->Size = Size;
        Bo->GpuVa = Device->FakeNextGpuVa;
        Device->FakeNextGpuVa += Aligned;
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&CreateData, sizeof(CreateData));
    RtlZeroMemory(&AllocationInfo, sizeof(AllocationInfo));
    AllocationInfo.PrivateDriverDataSize = sizeof(Size);
    AllocationInfo.pPrivateDriverData = &Size;
    CreateData.hDevice = Device->hDevice;
    CreateData.NumAllocations = 1;
    CreateData.pAllocationInfo = &AllocationInfo;

    Status = D3DKMTCreateAllocation(&CreateData);
    if (!NT_SUCCESS(Status))
        return Status;

    Bo->hAllocation = AllocationInfo.hAllocation;
    Bo->Size = Size;

    MappedSize = ((UINT64)Size + 4095u) & ~4095ULL;
    SizeInPages = (UINT)(MappedSize / 4096u);
    RtlZeroMemory(&MapGpuVa, sizeof(MapGpuVa));
    MapGpuVa.hAllocation = Bo->hAllocation;
    MapGpuVa.MinimumAddress = RPI5VC4_DYNAMIC_GPUVA_START;
    MapGpuVa.MaximumAddress = 1ULL << 32;
    MapGpuVa.SizeInPages = SizeInPages;
    MapGpuVa.hPagingQueue = Device->hPagingQueue;
    MapGpuVa.Protection.Write = 1;
    MapGpuVa.Protection.Execute = 1;
    Status = D3DKMTMapGpuVirtualAddress(&MapGpuVa);
    if (!NT_SUCCESS(Status))
        goto fail;

    if (MapGpuVa.VirtualAddress == 0 ||
        MapGpuVa.VirtualAddress > 0xffffffffULL ||
        MappedSize > (1ULL << 32) - MapGpuVa.VirtualAddress)
    {
        (void)Vc4KmtFreeGpuVaRange(Device,
                                   MapGpuVa.VirtualAddress,
                                   MappedSize);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto fail;
    }

    Bo->GpuVa = (ULONG)MapGpuVa.VirtualAddress;
    Status = Vc4KmtTrackBo(Device, Bo->hAllocation);
    if (!NT_SUCCESS(Status))
        goto fail;
    return STATUS_SUCCESS;

fail:
    (void)vc4kmt_bo_destroy(Device, Bo);
    return Status;
}

NTSTATUS
vc4kmt_bo_map(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo,
    _Outptr_ PVOID *CpuVaOut)
{
    D3DKMT_LOCK LockData;
    NTSTATUS Status;

    if (Device == NULL || Bo == NULL || CpuVaOut == NULL ||
        Bo->hAllocation == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Bo->CpuVa != NULL)
    {
        if (!Device->Fake &&
            !Vc4KmtMarkBoMapped(Device, Bo->hAllocation))
        {
            return STATUS_INVALID_DEVICE_STATE;
        }
        *CpuVaOut = Bo->CpuVa;
        return STATUS_SUCCESS;
    }

    if (Device->Fake)
        return STATUS_INVALID_DEVICE_REQUEST;

    RtlZeroMemory(&LockData, sizeof(LockData));
    LockData.hDevice = Device->hDevice;
    LockData.hAllocation = Bo->hAllocation;
    Status = D3DKMTLock(&LockData);
    if (!NT_SUCCESS(Status))
        return Status;

    if (LockData.pData == NULL)
    {
        D3DKMT_UNLOCK UnlockData;
        D3DKMT_HANDLE hAllocation = Bo->hAllocation;

        RtlZeroMemory(&UnlockData, sizeof(UnlockData));
        UnlockData.hDevice = Device->hDevice;
        UnlockData.NumAllocations = 1;
        UnlockData.phAllocations = &hAllocation;
        (void)D3DKMTUnlock(&UnlockData);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Bo->CpuVa = LockData.pData;
    if (!Vc4KmtMarkBoMapped(Device, Bo->hAllocation))
    {
        D3DKMT_UNLOCK UnlockData;
        D3DKMT_HANDLE hAllocation = Bo->hAllocation;

        RtlZeroMemory(&UnlockData, sizeof(UnlockData));
        UnlockData.hDevice = Device->hDevice;
        UnlockData.NumAllocations = 1;
        UnlockData.phAllocations = &hAllocation;
        (void)D3DKMTUnlock(&UnlockData);
        Bo->CpuVa = NULL;
        return STATUS_INVALID_DEVICE_STATE;
    }
    *CpuVaOut = Bo->CpuVa;
    return STATUS_SUCCESS;
}

NTSTATUS
vc4kmt_bo_invalidate(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_BO *Bo,
    _In_ UINT Offset,
    _In_ UINT Length)
{
    D3DKMT_INVALIDATECACHE InvalidateData;

    if (Device == NULL || Bo == NULL || Bo->hAllocation == 0 ||
        Length == 0 || Offset > Bo->Size || Length > Bo->Size - Offset)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Device->Fake)
    {
        MemoryBarrier();
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&InvalidateData, sizeof(InvalidateData));
    InvalidateData.hDevice = Device->hDevice;
    InvalidateData.hAllocation = Bo->hAllocation;
    InvalidateData.Offset = Offset;
    InvalidateData.Length = Length;
    return D3DKMTInvalidateCache(&InvalidateData);
}

ULONG
vc4kmt_bo_gpuva(
    _In_ const VC4KMT_BO *Bo)
{
    return Bo ? Bo->GpuVa : 0;
}

NTSTATUS
vc4kmt_shared_resource_info(
    _In_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _Out_writes_bytes_to_opt_(RuntimeDataCapacity, *RuntimeDataSize)
        PVOID RuntimeData,
    _In_ UINT RuntimeDataCapacity,
    _Out_ UINT *RuntimeDataSize)
{
    D3DKMT_QUERYRESOURCEINFO Query;
    NTSTATUS Status;

    if (Device == NULL || hGlobalShare == 0 || RuntimeDataSize == NULL ||
        (RuntimeDataCapacity != 0 && RuntimeData == NULL) || Device->Fake)
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
vc4kmt_bo_open_shared(
    _In_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_ UINT Size,
    _Out_ VC4KMT_BO *Bo,
    _Out_ D3DKMT_HANDLE *hResource)
{
    D3DKMT_QUERYRESOURCEINFO Query;
    D3DKMT_OPENRESOURCE OpenResource;
    D3DDDI_OPENALLOCATIONINFO *OpenAllocations = NULL;
    D3DDDI_MAPGPUVIRTUALADDRESS MapGpuVa;
    PVOID PrivateRuntimeData = NULL;
    PVOID ResourcePrivateData = NULL;
    PVOID TotalPrivateData = NULL;
    UINT64 MappedSize;
    NTSTATUS Status;

    if (Device == NULL || hGlobalShare == 0 || Size == 0 ||
        Bo == NULL || hResource == NULL || Device->Fake)
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
        Query.PrivateRuntimeDataSize > VC4KMT_MAX_PRIMARY_PRIVATE_DATA ||
        Query.ResourcePrivateDriverDataSize > VC4KMT_MAX_PRIMARY_PRIVATE_DATA ||
        Query.TotalPrivateDriverDataSize > VC4KMT_MAX_PRIMARY_PRIVATE_DATA)
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
        (Query.ResourcePrivateDriverDataSize != 0 && ResourcePrivateData == NULL) ||
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
    if (OpenResource.hResource == 0 ||
        OpenAllocations[0].hAllocation == 0)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Bo->Size = Size;

    MappedSize = ((UINT64)Size + 4095u) & ~4095ULL;
    RtlZeroMemory(&MapGpuVa, sizeof(MapGpuVa));
    MapGpuVa.hAllocation = Bo->hAllocation;
    MapGpuVa.MinimumAddress = RPI5VC4_DYNAMIC_GPUVA_START;
    MapGpuVa.MaximumAddress = 1ULL << 32;
    MapGpuVa.SizeInPages = (UINT)(MappedSize / 4096u);
    MapGpuVa.hPagingQueue = Device->hPagingQueue;
    MapGpuVa.Protection.Write = 1;
    Status = D3DKMTMapGpuVirtualAddress(&MapGpuVa);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (MapGpuVa.VirtualAddress == 0 ||
        MapGpuVa.VirtualAddress > 0xffffffffULL ||
        MappedSize > (1ULL << 32) - MapGpuVa.VirtualAddress)
    {
        (void)Vc4KmtFreeGpuVaRange(Device, MapGpuVa.VirtualAddress,
                                   MappedSize);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Cleanup;
    }

    Bo->GpuVa = (ULONG)MapGpuVa.VirtualAddress;
    Status = Vc4KmtTrackBo(Device, Bo->hAllocation);

Cleanup:
    if (!NT_SUCCESS(Status) && *hResource != 0)
    {
        D3DKMT_DESTROYALLOCATION Destroy;

        if (Bo->GpuVa != 0)
            (void)Vc4KmtFreeGpuVaRange(Device, Bo->GpuVa,
                                       ((UINT64)Bo->Size + 4095u) & ~4095ULL);
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
vc4kmt_bo_close_shared(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo,
    _In_ D3DKMT_HANDLE hResource)
{
    D3DKMT_DESTROYALLOCATION Destroy;
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS CleanupStatus;

    if (Device == NULL || Bo == NULL || hResource == 0 || Device->Fake)
        return STATUS_INVALID_PARAMETER;

    Vc4KmtUntrackBo(Device, Bo->hAllocation);
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

    CleanupStatus = Vc4KmtFreeGpuVaRange(
        Device, Bo->GpuVa, ((UINT64)Bo->Size + 4095u) & ~4095ULL);
    if (NT_SUCCESS(Status))
        Status = CleanupStatus;

    RtlZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = Device->hDevice;
    Destroy.hResource = hResource;
    CleanupStatus = D3DKMTDestroyAllocation(&Destroy);
    if (NT_SUCCESS(Status))
        Status = CleanupStatus;

    RtlZeroMemory(Bo, sizeof(*Bo));
    return Status;
}

NTSTATUS
vc4kmt_primary_gpuva(
    _In_ VC4KMT_DEVICE *Device,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Pitch,
    _Out_ ULONG *GpuVaOut)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE GetPrimary;
    D3DKMT_QUERYRESOURCEINFO Query;
    D3DKMT_OPENRESOURCE OpenResource;
    D3DDDI_OPENALLOCATIONINFO *OpenAllocations = NULL;
    D3DDDI_MAPGPUVIRTUALADDRESS MapGpuVa;
    PVOID PrivateRuntimeData = NULL;
    PVOID ResourcePrivateData = NULL;
    PVOID TotalPrivateData = NULL;
    ULONGLONG SurfaceBytes;
    ULONGLONG SizeInPages;
    NTSTATUS Status;

    if (Device == NULL || GpuVaOut == NULL || Width == 0 || Height == 0 ||
        Width > MAXUINT / sizeof(ULONG) ||
        Pitch != Width * sizeof(ULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *GpuVaOut = 0;

    if (Device->Fake ||
        (Device->Info.Caps & (RPI5VC4_CAP_TFU_SUBMIT |
                              RPI5VC4_CAP_WIN32_PRESENT |
                              RPI5VC4_CAP_LINEAR_SCANOUT)) !=
            (RPI5VC4_CAP_TFU_SUBMIT |
             RPI5VC4_CAP_WIN32_PRESENT |
             RPI5VC4_CAP_LINEAR_SCANOUT))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Device->PrimaryGpuVa != 0 &&
        Device->PrimaryWidth == Width &&
        Device->PrimaryHeight == Height &&
        Device->PrimaryPitch == Pitch)
    {
        *GpuVaOut = Device->PrimaryGpuVa;
        return STATUS_SUCCESS;
    }

    Vc4KmtClosePrimary(Device);
    Status = Vc4KmtQueryInfo(Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Device->Info.ScreenWidth != Width ||
        Device->Info.ScreenHeight != Height)
    {
        return STATUS_NOT_SUPPORTED;
    }

    SurfaceBytes = (ULONGLONG)Pitch * Height;
    if (SurfaceBytes == 0 || SurfaceBytes > MAXUINT)
        return STATUS_INTEGER_OVERFLOW;
    if (SurfaceBytes > Device->Info.SlabSize)
        return STATUS_INVALID_DEVICE_REQUEST;
    SizeInPages = (SurfaceBytes + 4095) / 4096;

    RtlZeroMemory(&GetPrimary, sizeof(GetPrimary));
    GetPrimary.hAdapter = Device->hAdapter;
    GetPrimary.VidPnSourceId = 0;
    Status = D3DKMTGetSharedPrimaryHandle(&GetPrimary);
    if (!NT_SUCCESS(Status))
        return Status;
    if (GetPrimary.hSharedPrimary == 0)
        return STATUS_NOT_FOUND;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.hDevice = Device->hDevice;
    Query.hGlobalShare = GetPrimary.hSharedPrimary;
    Status = D3DKMTQueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Query.NumAllocations != 1 ||
        Query.PrivateRuntimeDataSize > VC4KMT_MAX_PRIMARY_PRIVATE_DATA ||
        Query.ResourcePrivateDriverDataSize >
            VC4KMT_MAX_PRIMARY_PRIVATE_DATA ||
        Query.TotalPrivateDriverDataSize > VC4KMT_MAX_PRIMARY_PRIVATE_DATA)
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
    OpenResource.hGlobalShare = GetPrimary.hSharedPrimary;
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
    if (OpenResource.hResource == 0 ||
        OpenAllocations[0].hAllocation == 0)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Device->hPrimaryResource = OpenResource.hResource;
    Device->hPrimaryGlobalShare = GetPrimary.hSharedPrimary;
    Device->hPrimaryAllocation = OpenAllocations[0].hAllocation;

    RtlZeroMemory(&MapGpuVa, sizeof(MapGpuVa));
    MapGpuVa.hAllocation = Device->hPrimaryAllocation;
    MapGpuVa.MinimumAddress = RPI5VC4_DYNAMIC_GPUVA_START;
    MapGpuVa.MaximumAddress = 1ULL << 32;
    MapGpuVa.SizeInPages = SizeInPages;
    MapGpuVa.hPagingQueue = Device->hPagingQueue;
    MapGpuVa.Protection.Write = 1;
    Status = D3DKMTMapGpuVirtualAddress(&MapGpuVa);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (MapGpuVa.VirtualAddress == 0 ||
        MapGpuVa.VirtualAddress > 0xffffffffULL ||
        SizeInPages * 4096ULL >
            (1ULL << 32) - MapGpuVa.VirtualAddress)
    {
        (void)Vc4KmtFreeGpuVaRange(Device,
                                   MapGpuVa.VirtualAddress,
                                   SizeInPages * 4096ULL);
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Cleanup;
    }

    Device->PrimaryGpuVa = (ULONG)MapGpuVa.VirtualAddress;
    Device->PrimaryGpuVaSize = SizeInPages * 4096ULL;
    Device->PrimaryWidth = Width;
    Device->PrimaryHeight = Height;
    Device->PrimaryPitch = Pitch;
    *GpuVaOut = Device->PrimaryGpuVa;
    Status = STATUS_SUCCESS;

Cleanup:
    if (!NT_SUCCESS(Status))
        Vc4KmtClosePrimary(Device);
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

D3DKMT_HANDLE
vc4kmt_primary_allocation(
    _In_ const VC4KMT_DEVICE *Device)
{
    return Device != NULL ? Device->hPrimaryAllocation : 0;
}

VOID
vc4kmt_primary_invalidate(
    _In_opt_ VC4KMT_DEVICE *Device)
{
    if (Device != NULL)
        Vc4KmtClosePrimary(Device);
}

NTSTATUS
vc4kmt_bo_destroy(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo)
{
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS FreeStatus;

    if (Device == NULL || Bo == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Device->Fake)
    {
        if (Bo->CpuVa != NULL)
            HeapFree(GetProcessHeap(), 0, Bo->CpuVa);
        RtlZeroMemory(Bo, sizeof(*Bo));
        return STATUS_SUCCESS;
    }

    Vc4KmtUntrackBo(Device, Bo->hAllocation);

    if (Bo->CpuVa != NULL && Bo->hAllocation != 0)
    {
        D3DKMT_UNLOCK UnlockData;
        D3DKMT_HANDLE hAllocation = Bo->hAllocation;

        RtlZeroMemory(&UnlockData, sizeof(UnlockData));
        UnlockData.hDevice = Device->hDevice;
        UnlockData.NumAllocations = 1;
        UnlockData.phAllocations = &hAllocation;
        (void)D3DKMTUnlock(&UnlockData);
        Bo->CpuVa = NULL;
    }

    FreeStatus = Vc4KmtFreeGpuVaRange(
        Device,
        Bo->GpuVa,
        ((UINT64)Bo->Size + 4095u) & ~4095ULL);
    if (!NT_SUCCESS(FreeStatus))
        Status = FreeStatus;

    if (Bo->hAllocation != 0)
    {
        D3DKMT_DESTROYALLOCATION DestroyData;
        D3DKMT_HANDLE hAllocation = Bo->hAllocation;

        RtlZeroMemory(&DestroyData, sizeof(DestroyData));
        DestroyData.hDevice = Device->hDevice;
        DestroyData.phAllocationList = &hAllocation;
        DestroyData.AllocationCount = 1;
        FreeStatus = D3DKMTDestroyAllocation(&DestroyData);
        if (NT_SUCCESS(Status))
            Status = FreeStatus;
    }

    RtlZeroMemory(Bo, sizeof(*Bo));
    return Status;
}

static NTSTATUS
Vc4KmtSubmitPacket(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_DMA_PACKET *Packet,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    UCHAR *SubmitBytes;
    VC4KMT_SUBMIT_SIGNAL_VIEW SubmitView;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;
    UINT FixedBytes;
    UINT SubmitBytesSize;
    UINT NodeOrdinal;
    UINT i;

    FixedBytes = sizeof(VC4KMT_ESCAPE_PACKET_HEADER) +
                 sizeof(VC4KMT_RESOURCE_LIST_HEADER) +
                 sizeof(VC4KMT_COMMAND_PACKET_HEADER) +
                 sizeof(VC4KMT_SIGNAL_BLOCK) +
                 sizeof(VC4KMT_DMA_PACKET);
    if (Device == NULL || Packet == NULL || FenceOut == NULL ||
        ResourceCount > VC4KMT_MAX_SUBMIT_RESOURCES ||
        (ResourceCount != 0 && Resources == NULL) ||
        ResourceCount > (0xffffu - FixedBytes) / sizeof(*Resources))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));

    if (Packet->Op == VC4KMT_DMA_OP_TFU_JOB)
        NodeOrdinal = RPI5VC4_NODE_TFU;
    else if (Packet->Op == VC4KMT_DMA_OP_CSD_JOB)
        NodeOrdinal = RPI5VC4_NODE_CSD;
    else if (Packet->Op == VC4KMT_DMA_OP_V3D_JOB)
        NodeOrdinal = RPI5VC4_NODE_3D;
    else
        return STATUS_INVALID_PARAMETER;
    if (NodeOrdinal >= RPI5VC4_GPU_NODE_COUNT)
        return STATUS_INVALID_DEVICE_STATE;

    if (Device->Fake)
    {
        FenceOut->hSyncObject = 0;
        FenceOut->Value = ++Device->NextFenceValue[NodeOrdinal];
        FenceOut->CpuValue = NULL;
        return STATUS_SUCCESS;
    }

    if (Device->hContext[NodeOrdinal] == 0)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    SubmitBytesSize = FixedBytes +
                      ResourceCount * sizeof(*Resources);
    SubmitBytes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                            SubmitBytesSize);
    if (SubmitBytes == NULL)
        return STATUS_NO_MEMORY;

    /*
     * The kernel queues the submit to its async fence pipeline and signals
     * the device's monitored fence with this submit's value when the V3D
     * retires the job.  The returned VC4KMT_FENCE therefore completes
     * asynchronously; vc4kmt_wait blocks on the CPU-visible value page.
     */
    Vc4KmtBuildSubmitSignalView(SubmitBytes, SubmitBytesSize,
                                ResourceCount, &SubmitView);

    SubmitView.PacketHeader->PacketType = 2;
    SubmitView.PacketHeader->PayloadBytes =
        (USHORT)(SubmitView.PrivateBytes - sizeof(*SubmitView.PacketHeader));
    SubmitView.ResourceHeader->Magic = VC4KMT_RESOURCE_LIST_MAGIC_V2;
    SubmitView.ResourceHeader->ResourceCount = ResourceCount;
    for (i = 0; i < ResourceCount; i++)
        SubmitView.Resources[i] = Resources[i];
    SubmitView.Command->CommandType = 2;
    SubmitView.Command->PayloadBytes = sizeof(*SubmitView.Signal) +
                                       sizeof(*SubmitView.Packet);
    SubmitView.Signal->hSyncObject = Device->hFence[NodeOrdinal];
    SubmitView.Signal->FenceValue = Device->NextFenceValue[NodeOrdinal] + 1;
    *SubmitView.Packet = *Packet;

    /* Publish every WC store (the CL/BO bytes this process wrote through
     * write-combined mappings) to DRAM before ownership passes to the
     * kernel: the V3D is a non-coherent DMA reader, and a DSB drains only
     * the issuing PE's write buffer — this is the writer PE. */
#if defined(_M_ARM64)
    __dsb(_ARM64_BARRIER_SY);
#else
    MemoryBarrier();
#endif

    RtlZeroMemory(&Escape, sizeof(Escape));
    Escape.hAdapter = Device->hAdapter;
    Escape.hDevice = Device->hDevice;
    Escape.hContext = Device->hContext[NodeOrdinal];
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.pPrivateDriverData = SubmitBytes;
    Escape.PrivateDriverDataSize = SubmitView.PrivateBytes;

    {
        ULONG Tries;

        for (Tries = 0; Tries < 4000; Tries++)
        {
            Status = D3DKMTEscape(&Escape);
            if (Status != (NTSTATUS)0x80000011L)
                break;
            Sleep(1);
        }
    }
    if (NT_SUCCESS(Status))
    {
        FenceOut->hSyncObject = Device->hFence[NodeOrdinal];
        FenceOut->Value = ++Device->NextFenceValue[NodeOrdinal];
        FenceOut->CpuValue = Device->FenceCpuValue[NodeOrdinal];
    }
    HeapFree(GetProcessHeap(), 0, SubmitBytes);
    return Status;
}

NTSTATUS
vc4kmt_submit_cl_resources(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CL_SUBMIT *Submit,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    VC4KMT_DMA_PACKET Packet;

    if (Device == NULL || Submit == NULL || FenceOut == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Magic = VC4KMT_DMA_PACKET_MAGIC;
    Packet.Op = VC4KMT_DMA_OP_V3D_JOB;
    Packet.Length = sizeof(Packet);
    /* Absolute GPU VAs — the kernel executes without relocation. */
    Packet.V3dJob.BclStart = Submit->BclStart;
    Packet.V3dJob.BclEnd = Submit->BclEnd;
    Packet.V3dJob.RclStart = Submit->RclStart;
    Packet.V3dJob.RclEnd = Submit->RclEnd;
    Packet.V3dJob.BclAllocIndexPlusOne = 0;
    Packet.V3dJob.RclAllocIndexPlusOne = 0;
    Packet.V3dJob.Qma = Submit->Qma;
    Packet.V3dJob.Qms = Submit->Qms;
    Packet.V3dJob.Qts = Submit->Qts;

    return Vc4KmtSubmitPacket(Device, &Packet, Resources,
                              ResourceCount, FenceOut);
}

NTSTATUS
vc4kmt_submit_cl(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CL_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    VC4KMT_RESOURCE *Resources;
    UINT ResourceCount;
    NTSTATUS Status;

    if (Device == NULL || Submit == NULL || FenceOut == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));
    Status = Vc4KmtCollectMappedResources(Device, &Resources,
                                           &ResourceCount);
    if (NT_SUCCESS(Status))
    {
        Status = vc4kmt_submit_cl_resources(Device, Submit, Resources,
                                             ResourceCount, FenceOut);
    }
    if (Resources != NULL)
        HeapFree(GetProcessHeap(), 0, Resources);
    return Status;
}

NTSTATUS
vc4kmt_submit_tfu_resources(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_TFU_SUBMIT *Submit,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    VC4KMT_DMA_PACKET Packet;

    if (Device == NULL || Submit == NULL || FenceOut == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));

    if ((Device->Info.Caps & RPI5VC4_CAP_TFU_SUBMIT) == 0)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Magic = VC4KMT_DMA_PACKET_MAGIC;
    Packet.Op = VC4KMT_DMA_OP_TFU_JOB;
    Packet.Length = sizeof(Packet);
    RtlCopyMemory(Packet.TfuJob.Regs, Submit->Regs, sizeof(Packet.TfuJob.Regs));

    return Vc4KmtSubmitPacket(Device, &Packet, Resources,
                              ResourceCount, FenceOut);
}

NTSTATUS
vc4kmt_submit_tfu(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_TFU_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    VC4KMT_RESOURCE *Resources;
    UINT ResourceCount;
    NTSTATUS Status;

    if (Device == NULL || Submit == NULL || FenceOut == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));
    Status = Vc4KmtCollectMappedResources(Device, &Resources,
                                           &ResourceCount);
    if (NT_SUCCESS(Status))
    {
        Status = vc4kmt_submit_tfu_resources(Device, Submit, Resources,
                                              ResourceCount, FenceOut);
    }
    if (Resources != NULL)
        HeapFree(GetProcessHeap(), 0, Resources);
    return Status;
}

NTSTATUS
vc4kmt_submit_csd_resources(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CSD_SUBMIT *Submit,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    VC4KMT_DMA_PACKET Packet;

    if (Device == NULL || Submit == NULL || FenceOut == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));

    if ((Device->Info.Caps & RPI5VC4_CAP_CSD_SUBMIT) == 0)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Magic = VC4KMT_DMA_PACKET_MAGIC;
    Packet.Op = VC4KMT_DMA_OP_CSD_JOB;
    Packet.Length = sizeof(Packet);
    RtlCopyMemory(Packet.CsdJob.Cfg, Submit->Cfg, sizeof(Packet.CsdJob.Cfg));

    return Vc4KmtSubmitPacket(Device, &Packet, Resources,
                              ResourceCount, FenceOut);
}

NTSTATUS
vc4kmt_submit_csd(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CSD_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    VC4KMT_RESOURCE *Resources;
    UINT ResourceCount;
    NTSTATUS Status;

    if (Device == NULL || Submit == NULL || FenceOut == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));
    Status = Vc4KmtCollectMappedResources(Device, &Resources,
                                           &ResourceCount);
    if (NT_SUCCESS(Status))
    {
        Status = vc4kmt_submit_csd_resources(Device, Submit, Resources,
                                              ResourceCount, FenceOut);
    }
    if (Resources != NULL)
        HeapFree(GetProcessHeap(), 0, Resources);
    return Status;
}

NTSTATUS
vc4kmt_wait(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_FENCE *Fence,
    _In_ DWORD TimeoutMs)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;
    D3DKMT_HANDLE Handles[1];
    UINT64 Values[1];
    DWORD Start;

    if (Device == NULL || Fence == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Degenerate fence (no sync object, no value page): already complete. */
    if (Fence->hSyncObject == 0 && Fence->CpuValue == NULL)
        return STATUS_SUCCESS;

    if (Fence->CpuValue != NULL && *Fence->CpuValue >= Fence->Value)
        return STATUS_SUCCESS;

    /* A zero-time monitored-fence wait is a poll.  The shared fence page was
     * already sampled above, so avoid GetTickCount and the timed wait loop.
     * A concurrent signal after that sample belongs to the next poll, which
     * is the same boundary exposed by a zero-duration kernel wait. */
    if (Fence->CpuValue != NULL && TimeoutMs == 0)
        return STATUS_IO_TIMEOUT;

    if (Fence->CpuValue != NULL && TimeoutMs != INFINITE)
    {
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

    if (Fence->hSyncObject != 0)
    {
        Handles[0] = Fence->hSyncObject;
        Values[0] = Fence->Value;
        RtlZeroMemory(&Wait, sizeof(Wait));
        Wait.hDevice = Device->hDevice;
        Wait.ObjectCount = 1;
        Wait.ObjectHandleArray = Handles;
        Wait.FenceValueArray = Values;
        if (TimeoutMs == INFINITE)
            return D3DKMTWaitForSynchronizationObjectFromCpu(&Wait);
        return STATUS_NOT_SUPPORTED;
    }

    Start = GetTickCount();

    for (;;)
    {
        if (*Fence->CpuValue >= Fence->Value)
            return STATUS_SUCCESS;

        if (TimeoutMs != INFINITE && GetTickCount() - Start >= TimeoutMs)
            return STATUS_IO_TIMEOUT;

        Sleep(1);
    }
}

NTSTATUS
vc4kmt_wait_gpu(
    _In_ VC4KMT_DEVICE *Device,
    _In_ VC4KMT_ENGINE Engine,
    _In_ const VC4KMT_FENCE *Fence)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait;
    D3DKMT_HANDLE Handle;
    UINT64 Value;
    UINT NodeOrdinal = (UINT)Engine;

    if (Device == NULL || Fence == NULL ||
        NodeOrdinal >= RPI5VC4_GPU_NODE_COUNT)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Fence->CpuValue != NULL && *Fence->CpuValue >= Fence->Value) ||
        (Fence->hSyncObject == 0 && Fence->CpuValue == NULL))
    {
        return STATUS_SUCCESS;
    }

    if (Device->Fake)
        return STATUS_SUCCESS;
    if (Device->hContext[NodeOrdinal] == 0 || Fence->hSyncObject == 0)
        return STATUS_INVALID_DEVICE_STATE;

    Handle = Fence->hSyncObject;
    Value = Fence->Value;
    RtlZeroMemory(&Wait, sizeof(Wait));
    Wait.hContext = Device->hContext[NodeOrdinal];
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &Handle;
    Wait.MonitoredFenceValueArray = &Value;
    return D3DKMTWaitForSynchronizationObjectFromGpu(&Wait);
}

VOID
vc4kmt_fence_destroy(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_FENCE *Fence)
{
    /* Fences share the device's monitored fence object; nothing to free. */
    if (Device == NULL || Fence == NULL)
        return;

    RtlZeroMemory(Fence, sizeof(*Fence));
}
