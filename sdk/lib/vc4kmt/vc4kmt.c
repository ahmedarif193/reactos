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
#define VC4KMT_RESOURCE_LIST_MAGIC    0x5652474cUL
#define VC4KMT_MAX_SUBMIT_RESOURCES   2

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

typedef struct _VC4KMT_SUBMIT_SIGNAL_ESCAPE_MAX
{
    VC4KMT_ESCAPE_PACKET_HEADER PacketHeader;
    VC4KMT_RESOURCE_LIST_HEADER Resources;
    D3DKMT_HANDLE hResource[VC4KMT_MAX_SUBMIT_RESOURCES];
    VC4KMT_COMMAND_PACKET_HEADER Command;
    VC4KMT_SIGNAL_BLOCK Signal;
    VC4KMT_DMA_PACKET Packet;
} VC4KMT_SUBMIT_SIGNAL_ESCAPE_MAX;

typedef struct _VC4KMT_SUBMIT_SIGNAL_VIEW
{
    VC4KMT_ESCAPE_PACKET_HEADER *PacketHeader;
    VC4KMT_RESOURCE_LIST_HEADER *Resources;
    D3DKMT_HANDLE *hResource;
    VC4KMT_COMMAND_PACKET_HEADER *Command;
    VC4KMT_SIGNAL_BLOCK *Signal;
    VC4KMT_DMA_PACKET *Packet;
    UINT PrivateBytes;
} VC4KMT_SUBMIT_SIGNAL_VIEW;

static VOID
Vc4KmtBuildSubmitSignalView(
    _Out_writes_bytes_(sizeof(VC4KMT_SUBMIT_SIGNAL_ESCAPE_MAX)) UCHAR *Bytes,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_SUBMIT_SIGNAL_VIEW *View)
{
    UCHAR *Cursor = Bytes;

    View->PacketHeader = (VC4KMT_ESCAPE_PACKET_HEADER *)Cursor;
    Cursor += sizeof(*View->PacketHeader);

    View->Resources = (VC4KMT_RESOURCE_LIST_HEADER *)Cursor;
    Cursor += sizeof(*View->Resources);

    View->hResource = (D3DKMT_HANDLE *)Cursor;
    Cursor += ResourceCount * sizeof(D3DKMT_HANDLE);

    View->Command = (VC4KMT_COMMAND_PACKET_HEADER *)Cursor;
    Cursor += sizeof(*View->Command);

    View->Signal = (VC4KMT_SIGNAL_BLOCK *)Cursor;
    Cursor += sizeof(*View->Signal);

    View->Packet = (VC4KMT_DMA_PACKET *)Cursor;
    Cursor += sizeof(*View->Packet);

    View->PrivateBytes = (UINT)(Cursor - Bytes);
}

struct _VC4KMT_DEVICE
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hPagingQueue;
    D3DKMT_HANDLE hFence;
    volatile UINT64 *FenceCpuValue;
    RPI5VC4_ESCAPE_INFO Info;
    UINT64 NextFenceValue;
    BOOL Fake;
    ULONG FakeNextGpuVa;
};

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
    Device->FakeNextGpuVa = Device->Info.SlabGpuVa;
    Device->NextFenceValue = 0;
    return STATUS_SUCCESS;
}

static ULONG
Vc4KmtGpuVaFromSegmentLogical(
    _In_ const RPI5VC4_ESCAPE_INFO *Info,
    _In_ ULONGLONG SegmentLogical)
{
    return (ULONG)(Info->SlabGpuVa + (SegmentLogical - Info->SlabPhysical));
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

    return STATUS_SUCCESS;
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

NTSTATUS
vc4kmt_open(
    _Outptr_ VC4KMT_DEVICE **DeviceOut)
{
    VC4KMT_DEVICE *Device;
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME OpenAdapter;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;

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
     * queue.  D3DKMTMapGpuVirtualAddress validates hPagingQueue up front, so
     * the winsys must own one even though rpi5vc4's fixed-slab placement is
     * synchronous — the returned paging fence is already complete, so no
     * residency wait is needed.
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

    /*
     * Per-device monitored fence: submits are executed asynchronously by
     * the kernel fence pipeline; each submit signals this fence with its
     * own value on GPU retire, and vc4kmt_wait blocks on the CPU-visible
     * value page.  This is part of the required ABI, so adapter open must
     * fail rather than turn every later fence into an already-complete lie.
     */
    {
        D3DKMT_CREATESYNCHRONIZATIONOBJECT2 CreateSync;
        NTSTATUS SyncStatus;

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
        Device->hFence = CreateSync.hSyncObject;
        Device->FenceCpuValue = (volatile UINT64 *)CreateSync.Info.MonitoredFence.FenceValueCPUVirtualAddress;
    }

    Device->NextFenceValue = 0;
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
    if (Device == NULL)
        return;

    if (Device->hFence != 0)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;

        RtlZeroMemory(&DestroySync, sizeof(DestroySync));
        DestroySync.hSyncObject = Device->hFence;
        (void)D3DKMTDestroySynchronizationObject(&DestroySync);
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

    SizeInPages = (Size + 4095u) / 4096u;
    RtlZeroMemory(&MapGpuVa, sizeof(MapGpuVa));
    MapGpuVa.hAllocation = Bo->hAllocation;
    MapGpuVa.SizeInPages = SizeInPages;
    MapGpuVa.hPagingQueue = Device->hPagingQueue;
    Status = D3DKMTMapGpuVirtualAddress(&MapGpuVa);
    if (!NT_SUCCESS(Status))
        goto fail;

    if ((ULONGLONG)MapGpuVa.VirtualAddress < Device->Info.SlabPhysical ||
        (ULONGLONG)MapGpuVa.VirtualAddress >=
            Device->Info.SlabPhysical + Device->Info.SlabSize)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto fail;
    }

    Bo->GpuVa = Vc4KmtGpuVaFromSegmentLogical(&Device->Info,
                                              (ULONGLONG)MapGpuVa.VirtualAddress);
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
    *CpuVaOut = Bo->CpuVa;
    return STATUS_SUCCESS;
}

ULONG
vc4kmt_bo_gpuva(
    _In_ const VC4KMT_BO *Bo)
{
    return Bo ? Bo->GpuVa : 0;
}

NTSTATUS
vc4kmt_bo_destroy(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Device == NULL || Bo == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Device->Fake)
    {
        if (Bo->CpuVa != NULL)
            HeapFree(GetProcessHeap(), 0, Bo->CpuVa);
        RtlZeroMemory(Bo, sizeof(*Bo));
        return STATUS_SUCCESS;
    }

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

    if (Bo->hAllocation != 0)
    {
        D3DKMT_DESTROYALLOCATION DestroyData;
        D3DKMT_HANDLE hAllocation = Bo->hAllocation;

        RtlZeroMemory(&DestroyData, sizeof(DestroyData));
        DestroyData.hDevice = Device->hDevice;
        DestroyData.phAllocationList = &hAllocation;
        DestroyData.AllocationCount = 1;
        Status = D3DKMTDestroyAllocation(&DestroyData);
    }

    RtlZeroMemory(Bo, sizeof(*Bo));
    return Status;
}

static NTSTATUS
Vc4KmtSubmitPacket(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_DMA_PACKET *Packet,
    _In_reads_opt_(ResourceCount) const D3DKMT_HANDLE *ResourceHandles,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut)
{
    UCHAR SubmitBytes[sizeof(VC4KMT_SUBMIT_SIGNAL_ESCAPE_MAX)];
    VC4KMT_SUBMIT_SIGNAL_VIEW SubmitView;
    D3DKMT_ESCAPE Escape;
    NTSTATUS Status;
    UINT i;

    if (Device == NULL || Packet == NULL || FenceOut == NULL ||
        ResourceCount > VC4KMT_MAX_SUBMIT_RESOURCES ||
        (ResourceCount != 0 && ResourceHandles == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(FenceOut, sizeof(*FenceOut));

    if (Device->Fake)
    {
        FenceOut->hSyncObject = 0;
        FenceOut->Value = ++Device->NextFenceValue;
        FenceOut->CpuValue = NULL;
        return STATUS_SUCCESS;
    }

    /*
     * The kernel queues the submit to its async fence pipeline and signals
     * the device's monitored fence with this submit's value when the V3D
     * retires the job.  The returned VC4KMT_FENCE therefore completes
     * asynchronously; vc4kmt_wait blocks on the CPU-visible value page.
     */
    RtlZeroMemory(SubmitBytes, sizeof(SubmitBytes));
    Vc4KmtBuildSubmitSignalView(SubmitBytes, ResourceCount, &SubmitView);

    SubmitView.PacketHeader->PacketType = 2;
    SubmitView.PacketHeader->PayloadBytes =
        (USHORT)(SubmitView.PrivateBytes - sizeof(*SubmitView.PacketHeader));
    SubmitView.Resources->Magic = VC4KMT_RESOURCE_LIST_MAGIC;
    SubmitView.Resources->ResourceCount = ResourceCount;
    for (i = 0; i < ResourceCount; i++)
        SubmitView.hResource[i] = ResourceHandles[i];
    SubmitView.Command->CommandType = 2;
    SubmitView.Command->PayloadBytes = sizeof(*SubmitView.Signal) +
                                       sizeof(*SubmitView.Packet);
    SubmitView.Signal->hSyncObject = Device->hFence;
    SubmitView.Signal->FenceValue = Device->NextFenceValue + 1;
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
    if (!NT_SUCCESS(Status))
        return Status;

    FenceOut->hSyncObject = Device->hFence;
    FenceOut->Value = ++Device->NextFenceValue;
    FenceOut->CpuValue = Device->FenceCpuValue;
    return STATUS_SUCCESS;
}

NTSTATUS
vc4kmt_submit_cl(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CL_SUBMIT *Submit,
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

    return Vc4KmtSubmitPacket(Device, &Packet, NULL, 0, FenceOut);
}

NTSTATUS
vc4kmt_submit_tfu(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_TFU_SUBMIT *Submit,
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

    return Vc4KmtSubmitPacket(Device, &Packet, NULL, 0, FenceOut);
}

NTSTATUS
vc4kmt_submit_csd(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CSD_SUBMIT *Submit,
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

    return Vc4KmtSubmitPacket(Device, &Packet, NULL, 0, FenceOut);
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
