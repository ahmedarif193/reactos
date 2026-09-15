/*
 * ReactOS WoW64 D3DKMT functions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"

/*
 * gdi.c uses Wine's D3DKMT declarations.  Keep ReactOS additions in a
 * separate translation unit so that the current native WDDM structures can
 * be used without changing the Wine-synced source.
 */
#undef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0x11007
#include "../../../sdk/include/ddk/d3dkmthk.h"

#include "wow64win_private.h"

W32KAPI NTSTATUS WINAPI NtGdiDdDDICreateContext(D3DKMT_CREATECONTEXT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyContext(const D3DKMT_DESTROYCONTEXT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIGetSharedPrimaryHandle(D3DKMT_GETSHAREDPRIMARYHANDLE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDILock(D3DKMT_LOCK *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIPresent(D3DKMT_PRESENT *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIUnlock(const D3DKMT_UNLOCK *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDICreatePagingQueue(D3DKMT_CREATEPAGINGQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIDestroyPagingQueue(D3DDDI_DESTROYPAGINGQUEUE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIMapGpuVirtualAddress(D3DDDI_MAPGPUVIRTUALADDRESS *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIFreeGpuVirtualAddress(const D3DKMT_FREEGPUVIRTUALADDRESS *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIInvalidateCache(const D3DKMT_INVALIDATECACHE *desc);
W32KAPI NTSTATUS WINAPI NtGdiDdDDIWaitForSynchronizationObjectFromGpu(const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *desc);

typedef struct
{
    D3DKMT_HANDLE hDevice;
    UINT NodeOrdinal;
    UINT EngineAffinity;
    D3DDDI_CREATECONTEXTFLAGS Flags;
    ULONG pPrivateDriverData;
    UINT PrivateDriverDataSize;
    D3DKMT_CLIENTHINT ClientHint;
    D3DKMT_HANDLE hContext;
    ULONG pCommandBuffer;
    UINT CommandBufferSize;
    ULONG pAllocationList;
    UINT AllocationListSize;
    ULONG pPatchLocationList;
    UINT PatchLocationListSize;
    UINT64 CommandBuffer;
} D3DKMT_CREATECONTEXT32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DDDI_PAGINGQUEUE_PRIORITY Priority;
    D3DKMT_HANDLE hPagingQueue;
    D3DKMT_HANDLE hSyncObject;
    ULONG FenceValueCPUVirtualAddress;
    UINT PhysicalAdapterIndex;
} D3DKMT_CREATEPAGINGQUEUE32;

typedef struct
{
    D3DKMT_HANDLE hPagingQueue;
    UINT64 BaseAddress;
    UINT64 MinimumAddress;
    UINT64 MaximumAddress;
    D3DKMT_HANDLE hAllocation;
    UINT64 OffsetInPages;
    UINT64 SizeInPages;
    D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE Protection;
    UINT64 DriverProtection;
    UINT Reserved0;
    UINT64 Reserved1;
    UINT64 VirtualAddress;
    UINT64 PagingFenceValue;
} D3DDDI_MAPGPUVIRTUALADDRESS32;

typedef struct
{
    D3DKMT_HANDLE hAdapter;
    UINT64 BaseAddress;
    UINT64 Size;
} D3DKMT_FREEGPUVIRTUALADDRESS32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    UINT PrivateDriverData;
    UINT NumPages;
    ULONG pPages;
    ULONG pData;
    D3DDDICB_LOCKFLAGS Flags;
    UINT64 GpuVirtualAddress;
} D3DKMT_LOCK32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    UINT NumAllocations;
    ULONG phAllocations;
} D3DKMT_UNLOCK32;

typedef struct
{
    D3DKMT_HANDLE hDevice;
    D3DKMT_HANDLE hAllocation;
    ULONG Offset;
    ULONG Length;
} D3DKMT_INVALIDATECACHE32;

typedef struct
{
    D3DKMT_HANDLE hContext;
    UINT ObjectCount;
    ULONG ObjectHandleArray;
    union
    {
        ULONG MonitoredFenceValueArray;
        UINT64 FenceValue;
        UINT64 Reserved[8];
    };
} D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32;

typedef union
{
    struct
    {
        D3DKMT_PRESENT_MODEL Model;
        UINT TokenSize;
        BYTE Payload[1072];
    };
    UINT64 Alignment;
    BYTE Bytes[1080];
} D3DKMT_PRESENTHISTORYTOKEN32;

typedef struct
{
    UINT DirtyRectCount;
    ULONG pDirtyRects;
    UINT MoveRectCount;
    ULONG pMoveRects;
} D3DKMT_PRESENT_RGNS32;

typedef struct
{
    D3DKMT_HANDLE hContext;
    ULONG hWindow;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    D3DKMT_HANDLE hSource;
    D3DKMT_HANDLE hDestination;
    UINT Color;
    RECT DstRect;
    RECT SrcRect;
    UINT SubRectCnt;
    ULONG pSrcSubRects;
    UINT PresentCount;
    D3DDDI_FLIPINTERVAL_TYPE FlipInterval;
    D3DKMT_PRESENTFLAGS Flags;
    ULONG BroadcastContextCount;
    D3DKMT_HANDLE BroadcastContext[D3DDDI_MAX_BROADCAST_CONTEXT];
    ULONG PresentLimitSemaphore;
    D3DKMT_PRESENTHISTORYTOKEN32 PresentHistoryToken;
    ULONG pPresentRegions;
    D3DKMT_HANDLE hAdapter;
    UINT Duration;
    ULONG BroadcastSrcAllocation;
    ULONG BroadcastDstAllocation;
    UINT PrivateDriverDataSize;
    ULONG pPrivateDriverData;
    BOOLEAN bOptimizeForComposition;
} D3DKMT_PRESENT32;

C_ASSERT(sizeof(D3DKMT_CREATECONTEXT32) == 64);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATECONTEXT32, CommandBuffer) == 56);
C_ASSERT(sizeof(D3DKMT_CREATEPAGINGQUEUE32) == 24);
C_ASSERT(FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE32, PhysicalAdapterIndex) == 20);
C_ASSERT(sizeof(D3DDDI_MAPGPUVIRTUALADDRESS32) == 104);
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS32, hAllocation) == 32);
C_ASSERT(FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS32, VirtualAddress) == 88);
C_ASSERT(sizeof(D3DKMT_FREEGPUVIRTUALADDRESS32) == 24);
C_ASSERT(sizeof(D3DKMT_LOCK32) == 40);
C_ASSERT(FIELD_OFFSET(D3DKMT_LOCK32, GpuVirtualAddress) == 32);
C_ASSERT(sizeof(D3DKMT_UNLOCK32) == 12);
C_ASSERT(sizeof(D3DKMT_INVALIDATECACHE32) == 16);
C_ASSERT(sizeof(D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32) == 80);
C_ASSERT(FIELD_OFFSET(D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32, Reserved) == 16);
C_ASSERT(sizeof(D3DKMT_PRESENTHISTORYTOKEN32) == 1080);
C_ASSERT(sizeof(D3DKMT_PRESENT_RGNS32) == 16);
C_ASSERT(sizeof(D3DKMT_PRESENT32) == 1456);
C_ASSERT(FIELD_OFFSET(D3DKMT_PRESENT32, PresentHistoryToken) == 344);
C_ASSERT(FIELD_OFFSET(D3DKMT_PRESENT32, pPresentRegions) == 1424);
C_ASSERT(FIELD_OFFSET(D3DKMT_PRESENT32, bOptimizeForComposition) == 1452);

NTSTATUS WINAPI wow64_NtGdiDdDDICreateContext(UINT *args)
{
    D3DKMT_CREATECONTEXT32 *desc32 = get_ptr(&args);
    D3DKMT_CREATECONTEXT desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.NodeOrdinal = desc32->NodeOrdinal;
    desc.EngineAffinity = desc32->EngineAffinity;
    desc.Flags = desc32->Flags;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.ClientHint = desc32->ClientHint;

    status = NtGdiDdDDICreateContext(&desc);
    desc32->hContext = desc.hContext;
    desc32->pCommandBuffer = PtrToUlong(desc.pCommandBuffer);
    desc32->CommandBufferSize = desc.CommandBufferSize;
    desc32->pAllocationList = PtrToUlong(desc.pAllocationList);
    desc32->AllocationListSize = desc.AllocationListSize;
    desc32->pPatchLocationList = PtrToUlong(desc.pPatchLocationList);
    desc32->PatchLocationListSize = desc.PatchLocationListSize;
    desc32->CommandBuffer = desc.CommandBuffer;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyContext(UINT *args)
{
    const D3DKMT_DESTROYCONTEXT *desc32 = get_ptr(&args);
    D3DKMT_DESTROYCONTEXT desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hContext = desc32->hContext;
    return NtGdiDdDDIDestroyContext(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIGetSharedPrimaryHandle(UINT *args)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE *desc32 = get_ptr(&args);
    D3DKMT_GETSHAREDPRIMARYHANDLE desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hAdapter = desc32->hAdapter;
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.hSharedPrimary = desc32->hSharedPrimary;
    status = NtGdiDdDDIGetSharedPrimaryHandle(&desc);
    desc32->hSharedPrimary = desc.hSharedPrimary;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDILock(UINT *args)
{
    D3DKMT_LOCK32 *desc32 = get_ptr(&args);
    D3DKMT_LOCK desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.hAllocation = desc32->hAllocation;
    desc.PrivateDriverData = desc32->PrivateDriverData;
    desc.NumPages = desc32->NumPages;
    desc.pPages = ULongToPtr(desc32->pPages);
    desc.Flags = desc32->Flags;

    status = NtGdiDdDDILock(&desc);
    desc32->hAllocation = desc.hAllocation;
    desc32->pData = PtrToUlong(desc.pData);
    desc32->GpuVirtualAddress = desc.GpuVirtualAddress;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIPresent(UINT *args)
{
    D3DKMT_PRESENT32 *desc32 = get_ptr(&args);
    D3DKMT_PRESENT_RGNS32 *regions32;
    D3DKMT_PRESENT_RGNS regions;
    D3DKMT_PRESENT desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    if (desc32->PresentHistoryToken.Model != D3DKMT_PM_UNINITIALIZED ||
        desc32->PresentHistoryToken.TokenSize != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    memset(&desc, 0, sizeof(desc));
    desc.hContext = desc32->hContext;
    desc.hWindow = UlongToHandle(desc32->hWindow);
    desc.VidPnSourceId = desc32->VidPnSourceId;
    desc.hSource = desc32->hSource;
    desc.hDestination = desc32->hDestination;
    desc.Color = desc32->Color;
    desc.DstRect = desc32->DstRect;
    desc.SrcRect = desc32->SrcRect;
    desc.SubRectCnt = desc32->SubRectCnt;
    desc.pSrcSubRects = ULongToPtr(desc32->pSrcSubRects);
    desc.PresentCount = desc32->PresentCount;
    desc.FlipInterval = desc32->FlipInterval;
    desc.Flags = desc32->Flags;
    desc.BroadcastContextCount = desc32->BroadcastContextCount;
    memcpy(desc.BroadcastContext, desc32->BroadcastContext, sizeof(desc.BroadcastContext));
    desc.PresentLimitSemaphore = UlongToHandle(desc32->PresentLimitSemaphore);

    regions32 = ULongToPtr(desc32->pPresentRegions);
    if (regions32)
    {
        regions.DirtyRectCount = regions32->DirtyRectCount;
        regions.pDirtyRects = ULongToPtr(regions32->pDirtyRects);
        regions.MoveRectCount = regions32->MoveRectCount;
        regions.pMoveRects = ULongToPtr(regions32->pMoveRects);
        desc.pPresentRegions = &regions;
    }

    desc.hAdapter = desc32->hAdapter;
    desc.Duration = desc32->Duration;
    desc.BroadcastSrcAllocation = ULongToPtr(desc32->BroadcastSrcAllocation);
    desc.BroadcastDstAllocation = ULongToPtr(desc32->BroadcastDstAllocation);
    desc.PrivateDriverDataSize = desc32->PrivateDriverDataSize;
    desc.pPrivateDriverData = ULongToPtr(desc32->pPrivateDriverData);

    status = NtGdiDdDDIPresent(&desc);
    desc32->bOptimizeForComposition = desc.bOptimizeForComposition;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIUnlock(UINT *args)
{
    const D3DKMT_UNLOCK32 *desc32 = get_ptr(&args);
    D3DKMT_UNLOCK desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hDevice = desc32->hDevice;
    desc.NumAllocations = desc32->NumAllocations;
    desc.phAllocations = ULongToPtr(desc32->phAllocations);
    return NtGdiDdDDIUnlock(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDICreatePagingQueue(UINT *args)
{
    D3DKMT_CREATEPAGINGQUEUE32 *desc32 = get_ptr(&args);
    D3DKMT_CREATEPAGINGQUEUE desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hDevice = desc32->hDevice;
    desc.Priority = desc32->Priority;
    desc.PhysicalAdapterIndex = desc32->PhysicalAdapterIndex;

    status = NtGdiDdDDICreatePagingQueue(&desc);
    desc32->hPagingQueue = desc.hPagingQueue;
    desc32->hSyncObject = desc.hSyncObject;
    desc32->FenceValueCPUVirtualAddress = PtrToUlong(desc.FenceValueCPUVirtualAddress);
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIDestroyPagingQueue(UINT *args)
{
    const D3DDDI_DESTROYPAGINGQUEUE *desc32 = get_ptr(&args);
    D3DDDI_DESTROYPAGINGQUEUE desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hPagingQueue = desc32->hPagingQueue;
    return NtGdiDdDDIDestroyPagingQueue(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIMapGpuVirtualAddress(UINT *args)
{
    D3DDDI_MAPGPUVIRTUALADDRESS32 *desc32 = get_ptr(&args);
    D3DDDI_MAPGPUVIRTUALADDRESS desc;
    NTSTATUS status;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hPagingQueue = desc32->hPagingQueue;
    desc.BaseAddress = desc32->BaseAddress;
    desc.MinimumAddress = desc32->MinimumAddress;
    desc.MaximumAddress = desc32->MaximumAddress;
    desc.hAllocation = desc32->hAllocation;
    desc.OffsetInPages = desc32->OffsetInPages;
    desc.SizeInPages = desc32->SizeInPages;
    desc.Protection = desc32->Protection;
    desc.DriverProtection = desc32->DriverProtection;
    desc.Reserved0 = desc32->Reserved0;
    desc.Reserved1 = desc32->Reserved1;
    desc.VirtualAddress = desc32->VirtualAddress;
    desc.PagingFenceValue = desc32->PagingFenceValue;

    status = NtGdiDdDDIMapGpuVirtualAddress(&desc);
    desc32->VirtualAddress = desc.VirtualAddress;
    desc32->PagingFenceValue = desc.PagingFenceValue;
    return status;
}

NTSTATUS WINAPI wow64_NtGdiDdDDIFreeGpuVirtualAddress(UINT *args)
{
    const D3DKMT_FREEGPUVIRTUALADDRESS32 *desc32 = get_ptr(&args);
    D3DKMT_FREEGPUVIRTUALADDRESS desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hAdapter = desc32->hAdapter;
    desc.BaseAddress = desc32->BaseAddress;
    desc.Size = desc32->Size;
    return NtGdiDdDDIFreeGpuVirtualAddress(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIInvalidateCache(UINT *args)
{
    const D3DKMT_INVALIDATECACHE32 *desc32 = get_ptr(&args);
    D3DKMT_INVALIDATECACHE desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    desc.hDevice = desc32->hDevice;
    desc.hAllocation = desc32->hAllocation;
    desc.Offset = desc32->Offset;
    desc.Length = desc32->Length;
    return NtGdiDdDDIInvalidateCache(&desc);
}

NTSTATUS WINAPI wow64_NtGdiDdDDIWaitForSynchronizationObjectFromGpu(UINT *args)
{
    const D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU32 *desc32 = get_ptr(&args);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU desc;

    if (!desc32) return STATUS_INVALID_PARAMETER;
    memset(&desc, 0, sizeof(desc));
    desc.hContext = desc32->hContext;
    desc.ObjectCount = desc32->ObjectCount;
    desc.ObjectHandleArray = ULongToPtr(desc32->ObjectHandleArray);
    desc.MonitoredFenceValueArray = ULongToPtr(desc32->MonitoredFenceValueArray);
    return NtGdiDdDDIWaitForSynchronizationObjectFromGpu(&desc);
}
