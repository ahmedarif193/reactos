/*
 * PROJECT:     ReactOS D3D user-mode driver runtime callbacks
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The D3DDDI_DEVICECALLBACKS half of the user-mode driver contract
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * A user-mode display driver does not call D3DKMT.  It calls back through
 * D3DDDI_DEVICECALLBACKS -- pfnAllocateCb, pfnLockCb, pfnRenderCb and the rest
 * -- which it receives at pfnCreateDevice and which the *runtime* implements on
 * top of D3DKMT.  On Windows those live inside the D3D10/11/12 runtime DLLs.
 *
 * That is the direction this tree was missing.  The load path has worked since
 * the UMD landed: the runtime finds the driver, loads it, and exchanges both
 * function tables.  But a driver that has been handed a table of NULLs cannot
 * allocate a buffer, map one, or submit anything, so it can be loaded and still
 * be unable to do a single useful thing.  This file is the other half.
 *
 * Why it belongs to the OS and not to a driver: every WDDM user-mode driver
 * ever written expects these callbacks to exist with this shape.  A driver that
 * reached around them to call D3DKMT itself would work here and nowhere else,
 * which is the opposite of the point.
 *
 * Scope, stated plainly.  The entries implemented here are the ones a driver
 * needs to get from "loaded" to "submitting": allocation, lock/unlock, context
 * lifetime, submission, and escape.  The rest of the table stays NULL rather
 * than pointing at a stub that returns success without doing anything -- a
 * driver checks for NULL, and a lying entry is worse than an absent one.
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>
#include "d3dumdrt.h"

#define D3DUMDRT_DEVICE_MAGIC 0x54524D44u   /* 'DMRT' */

typedef struct _D3DUMDRT_DEVICE
{
    ULONG                    Magic;
    D3DKMT_HANDLE            hAdapter;
    D3DKMT_HANDLE            hDevice;
    /*
     * Objects this device handed the driver and has not been given back.
     * Teardown is ordered -- contexts and paging queues before the device that
     * owns them -- and a driver that skips a step must be told rather than
     * quietly leaving kernel objects parented to a device that is gone.
     */
    volatile LONG            LiveObjectCount;
    struct _D3DUMDRT_DEVICE *Next;
} D3DUMDRT_DEVICE, *PD3DUMDRT_DEVICE;

/*
 * Every handle this runtime has issued and not yet released.
 *
 * A driver hands its device handle back on every callback, and a buggy or
 * malicious one hands back something else.  Validating that by reading a magic
 * number out of the pointer does not work: reading is the very thing that
 * faults when the pointer is not ours, so the check crashes on exactly the
 * input it exists to reject.  Membership of this list is decided without
 * dereferencing the candidate at all.
 */
static PD3DUMDRT_DEVICE D3DUmdRtDeviceList;
static CRITICAL_SECTION D3DUmdRtDeviceLock;
static BOOL D3DUmdRtDeviceLockReady;

/* Resolved once; every callback goes through these. */
static PFND3DKMT_CREATEALLOCATION  pfnCreateAllocation;
static PFND3DKMT_DESTROYALLOCATION pfnDestroyAllocation;
static PFND3DKMT_LOCK              pfnLock;
static PFND3DKMT_UNLOCK            pfnUnlock;
static PFND3DKMT_RENDER            pfnRender;
static PFND3DKMT_CREATECONTEXT     pfnCreateContext;
static PFND3DKMT_DESTROYCONTEXT    pfnDestroyContext;
static PFND3DKMT_ESCAPE            pfnEscape;
/* WDDM 2.0 tier */
static PFND3DKMT_MAKERESIDENT              pfnMakeResident;
static PFND3DKMT_EVICT                     pfnEvict;
static PFND3DKMT_CREATEPAGINGQUEUE         pfnCreatePagingQueue;
static PFND3DKMT_DESTROYPAGINGQUEUE        pfnDestroyPagingQueue;
static PFND3DKMT_RESERVEGPUVIRTUALADDRESS  pfnReserveGpuVirtualAddress;
static PFND3DKMT_MAPGPUVIRTUALADDRESS      pfnMapGpuVirtualAddress;
static PFND3DKMT_FREEGPUVIRTUALADDRESS     pfnFreeGpuVirtualAddress;
static PFND3DKMT_CREATECONTEXTVIRTUAL      pfnCreateContextVirtual;
static PFND3DKMT_SUBMITCOMMAND             pfnSubmitCommand;
static PFND3DKMT_LOCK2                     pfnLock2;
static PFND3DKMT_UNLOCK2                   pfnUnlock2;
static PFND3DKMT_DESTROYALLOCATION2        pfnDestroyAllocation2;
static PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pfnCreateSynchronizationObject2;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU pfnWaitForSynchronizationObjectFromGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU  pfnSignalSynchronizationObjectFromGpu;
static PFND3DKMT_PRESENT                   pfnPresent;
static BOOL                        ProcsResolved;

static BOOL D3DUmdRtResolveProcs(VOID)
{
    HMODULE Gdi32;

    if (ProcsResolved)
        return TRUE;

    Gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (Gdi32 == NULL)
        return FALSE;

#define RESOLVE(Lower, Upper) \
    pfn##Lower = (PFND3DKMT_##Upper)(PVOID)GetProcAddress(Gdi32, "D3DKMT" #Lower)

    RESOLVE(CreateAllocation, CREATEALLOCATION);
    RESOLVE(DestroyAllocation, DESTROYALLOCATION);
    RESOLVE(Lock, LOCK);
    RESOLVE(Unlock, UNLOCK);
    RESOLVE(Render, RENDER);
    RESOLVE(CreateContext, CREATECONTEXT);
    RESOLVE(DestroyContext, DESTROYCONTEXT);
    RESOLVE(Escape, ESCAPE);
    RESOLVE(MakeResident, MAKERESIDENT);
    RESOLVE(Evict, EVICT);
    RESOLVE(CreatePagingQueue, CREATEPAGINGQUEUE);
    RESOLVE(DestroyPagingQueue, DESTROYPAGINGQUEUE);
    RESOLVE(ReserveGpuVirtualAddress, RESERVEGPUVIRTUALADDRESS);
    RESOLVE(MapGpuVirtualAddress, MAPGPUVIRTUALADDRESS);
    RESOLVE(FreeGpuVirtualAddress, FREEGPUVIRTUALADDRESS);
    RESOLVE(CreateContextVirtual, CREATECONTEXTVIRTUAL);
    RESOLVE(SubmitCommand, SUBMITCOMMAND);
    RESOLVE(Lock2, LOCK2);
    RESOLVE(Unlock2, UNLOCK2);
    RESOLVE(DestroyAllocation2, DESTROYALLOCATION2);
    RESOLVE(CreateSynchronizationObject2, CREATESYNCHRONIZATIONOBJECT2);
    RESOLVE(WaitForSynchronizationObjectFromGpu, WAITFORSYNCHRONIZATIONOBJECTFROMGPU);
    RESOLVE(SignalSynchronizationObjectFromGpu, SIGNALSYNCHRONIZATIONOBJECTFROMGPU);
    RESOLVE(Present, PRESENT);
#undef RESOLVE

    ProcsResolved = TRUE;
    return TRUE;
}

static PD3DUMDRT_DEVICE D3DUmdRtDevice(HANDLE hDevice)
{
    PD3DUMDRT_DEVICE Device;

    if (hDevice == NULL || !D3DUmdRtDeviceLockReady)
        return NULL;

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    for (Device = D3DUmdRtDeviceList; Device != NULL; Device = Device->Next)
    {
        if ((HANDLE)Device == hDevice)
            break;
    }
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    /* The magic is a corruption check on a pointer already known to be ours,
     * not a validity check on an arbitrary one. */
    if (Device != NULL && Device->Magic != D3DUMDRT_DEVICE_MAGIC)
        return NULL;
    return Device;
}

/*
 * A driver reads HRESULTs, the kernel returns NTSTATUS.  Mapping the two
 * failures a driver actually branches on -- out of memory and a bad argument --
 * matters more than round-tripping the exact code: a driver that sees
 * E_OUTOFMEMORY frees something and retries, and one that sees E_INVALIDARG
 * does not.
 */
static HRESULT D3DUmdRtStatusToHresult(NTSTATUS Status)
{
    if (Status >= 0)
        return S_OK;
    switch (Status)
    {
        case STATUS_NO_MEMORY:
        case STATUS_INSUFFICIENT_RESOURCES:
            return E_OUTOFMEMORY;
        case STATUS_INVALID_PARAMETER:
        case STATUS_INVALID_HANDLE:
            return E_INVALIDARG;
        case STATUS_NOT_IMPLEMENTED:
        case STATUS_NOT_SUPPORTED:
            return E_NOTIMPL;
        case STATUS_DEVICE_REMOVED:
            /* The DDI's own device-removed code.  A driver treats this as
             * terminal for the device and recreates rather than retrying, so it
             * must not be flattened into E_FAIL. */
            return (HRESULT)0x88760870L;
        default:
            return E_FAIL;
    }
}

/* ------------------------------------------------------------------------ *
 * Allocation
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtAllocateCb(HANDLE hDevice, D3DDDICB_ALLOCATE *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_CREATEALLOCATION Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnCreateAllocation == NULL)
        return E_INVALIDARG;
    if (pData->NumAllocations == 0 || pData->pAllocationInfo == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    /* hResource travels in as the runtime's own handle and out as the kernel's;
     * the driver keeps the kernel one, so it must be written back below. */
    Create.hResource = pData->hKMResource;
    Create.pPrivateDriverData = (VOID *)pData->pPrivateDriverData;
    Create.PrivateDriverDataSize = pData->PrivateDriverDataSize;
    Create.NumAllocations = pData->NumAllocations;
    Create.pAllocationInfo = pData->pAllocationInfo;

    Status = pfnCreateAllocation(&Create);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

    pData->hKMResource = Create.hResource;
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtDeallocateCb(HANDLE hDevice, CONST D3DDDICB_DEALLOCATE *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_DESTROYALLOCATION Destroy;

    if (Device == NULL || pData == NULL || pfnDestroyAllocation == NULL)
        return E_INVALIDARG;
    /* Either a resource or a handle list, never neither: with both empty there
     * is nothing named to free, and succeeding would tell the driver its memory
     * was released when it was not. */
    if (pData->hResource == NULL && (pData->NumAllocations == 0 || pData->HandleList == NULL))
        return E_INVALIDARG;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = Device->hDevice;
    Destroy.hResource = (D3DKMT_HANDLE)(ULONG_PTR)pData->hResource;
    if (pData->hResource == NULL)
    {
        Destroy.phAllocationList = pData->HandleList;
        Destroy.AllocationCount = pData->NumAllocations;
    }
    return D3DUmdRtStatusToHresult(pfnDestroyAllocation(&Destroy));
}

/* ------------------------------------------------------------------------ *
 * CPU access
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtLockCb(HANDLE hDevice, D3DDDICB_LOCK *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_LOCK Lock;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnLock == NULL)
        return E_INVALIDARG;
    if (pData->hAllocation == 0)
        return E_INVALIDARG;

    ZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice = Device->hDevice;
    Lock.hAllocation = pData->hAllocation;
    Lock.NumPages = pData->NumPages;
    Lock.pPages = (UINT *)pData->pPages;
    Lock.Flags = *(D3DDDICB_LOCKFLAGS *)&pData->Flags;

    Status = pfnLock(&Lock);
    if (Status < 0)
    {
        /* Leave pData->pData alone on failure.  A driver that saw a stale
         * pointer here would write through it believing the lock succeeded. */
        return D3DUmdRtStatusToHresult(Status);
    }

    pData->pData = Lock.pData;
    pData->GpuVirtualAddress = Lock.GpuVirtualAddress;
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtUnlockCb(HANDLE hDevice, CONST D3DDDICB_UNLOCK *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_UNLOCK Unlock;

    if (Device == NULL || pData == NULL || pfnUnlock == NULL)
        return E_INVALIDARG;
    if (pData->NumAllocations == 0 || pData->phAllocations == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Unlock, sizeof(Unlock));
    Unlock.hDevice = Device->hDevice;
    Unlock.NumAllocations = pData->NumAllocations;
    Unlock.phAllocations = pData->phAllocations;
    return D3DUmdRtStatusToHresult(pfnUnlock(&Unlock));
}

/* ------------------------------------------------------------------------ *
 * Context lifetime and submission
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtCreateContextCb(HANDLE hDevice, D3DDDICB_CREATECONTEXT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_CREATECONTEXT Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnCreateContext == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.NodeOrdinal = pData->NodeOrdinal;
    Create.EngineAffinity = pData->EngineAffinity;
    Create.Flags = pData->Flags;
    Create.pPrivateDriverData = pData->pPrivateDriverData;
    Create.PrivateDriverDataSize = pData->PrivateDriverDataSize;

    Status = pfnCreateContext(&Create);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

    /*
     * The driver receives the context's first command buffer and both lists
     * here.  All three come back from the kernel together and are useless
     * apart -- a buffer with no allocation list cannot name what it touches.
     */
    InterlockedIncrement(&Device->LiveObjectCount);
    pData->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    pData->pCommandBuffer = Create.pCommandBuffer;
    pData->CommandBufferSize = Create.CommandBufferSize;
    pData->pAllocationList = Create.pAllocationList;
    pData->AllocationListSize = Create.AllocationListSize;
    pData->pPatchLocationList = Create.pPatchLocationList;
    pData->PatchLocationListSize = Create.PatchLocationListSize;
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtDestroyContextCb(HANDLE hDevice, CONST D3DDDICB_DESTROYCONTEXT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_DESTROYCONTEXT Destroy;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnDestroyContext == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Status = pfnDestroyContext(&Destroy);
    if (Status >= 0)
        InterlockedDecrement(&Device->LiveObjectCount);
    return D3DUmdRtStatusToHresult(Status);
}

static HRESULT APIENTRY D3DUmdRtRenderCb(HANDLE hDevice, D3DDDICB_RENDER *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_RENDER Render;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnRender == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Render, sizeof(Render));
    Render.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Render.CommandOffset = pData->CommandOffset;
    Render.CommandLength = pData->CommandLength;
    Render.AllocationCount = pData->NumAllocations;
    Render.PatchLocationCount = pData->NumPatchLocations;
    Render.NewCommandBufferSize = pData->NewCommandBufferSize;
    Render.NewAllocationListSize = pData->NewAllocationListSize;
    Render.NewPatchLocationListSize = pData->NewPatchLocationListSize;
    Render.Flags = *(D3DKMT_RENDERFLAGS *)&pData->Flags;
    Render.BroadcastContextCount = pData->BroadcastContextCount;

    Status = pfnRender(&Render);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

    /*
     * Submission hands the driver its *next* buffer, because the one it just
     * submitted now belongs to the GPU.  A driver that kept writing into the
     * old buffer would be editing work already in flight, so these outputs are
     * not optional bookkeeping -- they are the whole reason render returns.
     */
    pData->pNewCommandBuffer = Render.pNewCommandBuffer;
    pData->NewCommandBufferSize = Render.NewCommandBufferSize;
    pData->pNewAllocationList = Render.pNewAllocationList;
    pData->NewAllocationListSize = Render.NewAllocationListSize;
    pData->pNewPatchLocationList = Render.pNewPatchLocationList;
    pData->NewPatchLocationListSize = Render.NewPatchLocationListSize;
    pData->QueuedBufferCount = Render.QueuedBufferCount;
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * Present
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtPresentCb(HANDLE hDevice, D3DDDICB_PRESENT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_PRESENT Present;
    NTSTATUS Status;
    UINT Index;

    if (Device == NULL || pData == NULL || pfnPresent == NULL)
        return E_INVALIDARG;
    /* There is nothing to present without a source. */
    if (pData->hSrcAllocation == 0)
        return E_INVALIDARG;

    ZeroMemory(&Present, sizeof(Present));
    Present.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Present.hSource = pData->hSrcAllocation;
    Present.hDestination = pData->hDstAllocation;
    Present.BroadcastContextCount = pData->BroadcastContextCount;
    for (Index = 0; Index < pData->BroadcastContextCount &&
                    Index < D3DDDI_MAX_BROADCAST_CONTEXT; ++Index)
    {
        Present.BroadcastContext[Index] = (D3DKMT_HANDLE)(ULONG_PTR)pData->BroadcastContext[Index];
    }
    Present.BroadcastSrcAllocation = pData->BroadcastSrcAllocation;
    Present.BroadcastDstAllocation = pData->BroadcastDstAllocation;
    Present.pPrivateDriverData = pData->pPrivateDriverData;
    Present.PrivateDriverDataSize = pData->PrivateDriverDataSize;

    Status = pfnPresent(&Present);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

    /*
     * Whether a compositor owns the screen changes what the driver should do
     * next -- it can skip work that will never be seen -- so this is reported
     * back rather than dropped.
     */
    pData->bOptimizeForComposition = Present.Flags.Flip ? FALSE : TRUE;
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * Escape -- the driver's private channel to its own kernel half
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtEscapeCb(HANDLE hAdapter, CONST D3DDDICB_ESCAPE *pData)
{
    D3DKMT_ESCAPE Escape;

    if (pData == NULL || pfnEscape == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Escape, sizeof(Escape));
    /* Escape is the one callback keyed on the adapter rather than the device,
     * because a driver may need it before any device exists. */
    Escape.hAdapter = (D3DKMT_HANDLE)(ULONG_PTR)hAdapter;
    Escape.hDevice = (D3DKMT_HANDLE)(ULONG_PTR)pData->hDevice;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.Flags = *(D3DDDI_ESCAPEFLAGS *)&pData->Flags;
    Escape.pPrivateDriverData = pData->pPrivateDriverData;
    Escape.PrivateDriverDataSize = pData->PrivateDriverDataSize;
    Escape.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    return D3DUmdRtStatusToHresult(pfnEscape(&Escape));
}

/* ------------------------------------------------------------------------ *
 * WDDM 2.0: residency, paging queues, GPU virtual addressing
 *
 * These are what a WDDM 2.0 driver uses instead of the WDDM 1.x model where the
 * kernel decided residency at submission time.  The driver now owns its working
 * set: it makes allocations resident, is told when it must give memory back, and
 * places allocations in its own GPU address space itself.
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtMakeResidentCb(HANDLE hDevice, D3DDDI_MAKERESIDENT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);

    if (Device == NULL || pData == NULL || pfnMakeResident == NULL)
        return E_INVALIDARG;
    if (pData->NumAllocations == 0 || pData->AllocationList == NULL)
        return E_INVALIDARG;

    /*
     * The DDI and the D3DKMT entry take the same structure -- it is one of the
     * shared d3dukmdt.h types -- so this forwards rather than translating. The
     * paging fence and NumBytesToTrim it writes back travel out untouched: a
     * driver that ignored NumBytesToTrim on failure would retry forever without
     * freeing the amount it was just told to free.
     */
    pData->hPagingQueue = pData->hPagingQueue;
    return D3DUmdRtStatusToHresult(pfnMakeResident(pData));
}

static HRESULT APIENTRY D3DUmdRtEvictCb(HANDLE hDevice, D3DDDICB_EVICT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_EVICT Evict;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnEvict == NULL)
        return E_INVALIDARG;
    if (pData->NumAllocations == 0 || pData->AllocationList == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Evict, sizeof(Evict));
    Evict.hDevice = Device->hDevice;
    Evict.NumAllocations = pData->NumAllocations;
    Evict.AllocationList = pData->AllocationList;
    Evict.Flags = pData->Flags;

    Status = pfnEvict(&Evict);
    /* NumBytesToTrim is meaningful on both outcomes: it is how much the caller
     * is over budget, not how much this call freed. */
    pData->NumBytesToTrim = Evict.NumBytesToTrim;
    return D3DUmdRtStatusToHresult(Status);
}

static HRESULT APIENTRY D3DUmdRtCreatePagingQueueCb(HANDLE hDevice, D3DDDICB_CREATEPAGINGQUEUE *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_CREATEPAGINGQUEUE Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnCreatePagingQueue == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.Priority = pData->Priority;
    Create.PhysicalAdapterIndex = pData->PhysicalAdapterIndex;

    Status = pfnCreatePagingQueue(&Create);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

    /*
     * The queue arrives with its own monitored fence.  That fence is how the
     * driver learns a paging operation finished, so handing back the queue
     * without it would leave the driver unable to tell when anything it
     * requested had actually happened.
     */
    InterlockedIncrement(&Device->LiveObjectCount);
    pData->hPagingQueue = Create.hPagingQueue;
    pData->hSyncObject = Create.hSyncObject;
    pData->FenceValueCPUVirtualAddress = Create.FenceValueCPUVirtualAddress;
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtDestroyPagingQueueCb(HANDLE hDevice, CONST D3DDDI_DESTROYPAGINGQUEUE *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DDDI_DESTROYPAGINGQUEUE Destroy;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnDestroyPagingQueue == NULL)
        return E_INVALIDARG;

    Destroy = *pData;
    Status = pfnDestroyPagingQueue(&Destroy);
    if (Status >= 0)
        InterlockedDecrement(&Device->LiveObjectCount);
    return D3DUmdRtStatusToHresult(Status);
}

static HRESULT APIENTRY D3DUmdRtReserveGpuVirtualAddressCb(HANDLE hDevice,
                                                           D3DDDI_RESERVEGPUVIRTUALADDRESS *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);

    if (Device == NULL || pData == NULL || pfnReserveGpuVirtualAddress == NULL)
        return E_INVALIDARG;
    /* Shared structure again: reserve, map, and free all take exactly what
     * D3DKMT takes, so nothing is translated and nothing can be lost. */
    return D3DUmdRtStatusToHresult(pfnReserveGpuVirtualAddress(pData));
}

static HRESULT APIENTRY D3DUmdRtMapGpuVirtualAddressCb(HANDLE hDevice,
                                                       D3DDDI_MAPGPUVIRTUALADDRESS *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);

    if (Device == NULL || pData == NULL || pfnMapGpuVirtualAddress == NULL)
        return E_INVALIDARG;
    /*
     * Mapping is asynchronous: it returns a paging fence value the driver must
     * wait for before the GPU may touch the range. The fence travels back in
     * pData, so a driver that used the address immediately would be reading a
     * page table entry that has not been written yet.
     */
    return D3DUmdRtStatusToHresult(pfnMapGpuVirtualAddress(pData));
}

static HRESULT APIENTRY D3DUmdRtFreeGpuVirtualAddressCb(HANDLE hDevice,
                                                        CONST D3DDDICB_FREEGPUVIRTUALADDRESS *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_FREEGPUVIRTUALADDRESS Free;

    if (Device == NULL || pData == NULL || pfnFreeGpuVirtualAddress == NULL)
        return E_INVALIDARG;
    if (pData->Size == 0)
        return E_INVALIDARG;

    ZeroMemory(&Free, sizeof(Free));
    Free.hAdapter = Device->hAdapter;
    Free.BaseAddress = pData->BaseAddress;
    Free.Size = pData->Size;
    return D3DUmdRtStatusToHresult(pfnFreeGpuVirtualAddress(&Free));
}

/* ------------------------------------------------------------------------ *
 * WDDM 2.0: virtual-addressing contexts and submission
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtCreateContextVirtualCb(HANDLE hDevice,
                                                       D3DDDICB_CREATECONTEXTVIRTUAL *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_CREATECONTEXTVIRTUAL Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnCreateContextVirtual == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.NodeOrdinal = pData->NodeOrdinal;
    Create.EngineAffinity = pData->EngineAffinity;
    Create.Flags = pData->Flags;
    Create.pPrivateDriverData = pData->pPrivateDriverData;
    Create.PrivateDriverDataSize = pData->PrivateDriverDataSize;

    Status = pfnCreateContextVirtual(&Create);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

    /*
     * Unlike CreateContext there is no command buffer here, and that is the
     * point: a virtual-addressing context submits from buffers the driver
     * allocated and placed in its own GPU address space, so the kernel has none
     * to hand out.
     */
    InterlockedIncrement(&Device->LiveObjectCount);
    pData->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtSubmitCommandCb(HANDLE hDevice, CONST D3DDDICB_SUBMITCOMMAND *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SUBMITCOMMAND Submit;
    UINT Index;

    if (Device == NULL || pData == NULL || pfnSubmitCommand == NULL)
        return E_INVALIDARG;
    if (pData->BroadcastContextCount == 0 ||
        pData->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Submit, sizeof(Submit));
    /* The commands are named by GPU address, not by a kernel buffer handle --
     * that is what makes this the virtual-addressing submission path. */
    Submit.Commands = pData->Commands;
    Submit.CommandLength = pData->CommandLength;
    Submit.Flags = *(D3DKMT_SUBMITCOMMANDFLAGS *)&pData->Flags;
    Submit.BroadcastContextCount = pData->BroadcastContextCount;
    for (Index = 0; Index < pData->BroadcastContextCount; ++Index)
        Submit.BroadcastContext[Index] = (D3DKMT_HANDLE)(ULONG_PTR)pData->BroadcastContext[Index];
    Submit.pPrivateDriverData = pData->pPrivateDriverData;
    Submit.PrivateDriverDataSize = pData->PrivateDriverDataSize;
    Submit.NumPrimaries = pData->NumPrimaries;
    for (Index = 0; Index < pData->NumPrimaries && Index < D3DDDI_MAX_WRITTEN_PRIMARIES; ++Index)
        Submit.WrittenPrimaries[Index] = pData->WrittenPrimaries[Index];
    return D3DUmdRtStatusToHresult(pfnSubmitCommand(&Submit));
}

static HRESULT APIENTRY D3DUmdRtLock2Cb(HANDLE hDevice, D3DDDICB_LOCK2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_LOCK2 Lock;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnLock2 == NULL)
        return E_INVALIDARG;
    if (pData->hAllocation == 0)
        return E_INVALIDARG;

    ZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice = Device->hDevice;
    Lock.hAllocation = pData->hAllocation;
    Lock.Flags = *(D3DDDICB_LOCK2FLAGS *)&pData->Flags;

    Status = pfnLock2(&Lock);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);
    pData->pData = Lock.pData;
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtUnlock2Cb(HANDLE hDevice, CONST D3DDDICB_UNLOCK2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_UNLOCK2 Unlock;

    if (Device == NULL || pData == NULL || pfnUnlock2 == NULL)
        return E_INVALIDARG;
    if (pData->hAllocation == 0)
        return E_INVALIDARG;

    ZeroMemory(&Unlock, sizeof(Unlock));
    Unlock.hDevice = Device->hDevice;
    Unlock.hAllocation = pData->hAllocation;
    return D3DUmdRtStatusToHresult(pfnUnlock2(&Unlock));
}

static HRESULT APIENTRY D3DUmdRtDeallocate2Cb(HANDLE hDevice, CONST D3DDDICB_DEALLOCATE2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_DESTROYALLOCATION2 Destroy;

    if (Device == NULL || pData == NULL || pfnDestroyAllocation2 == NULL)
        return E_INVALIDARG;
    if (pData->hResource == NULL && (pData->NumAllocations == 0 || pData->HandleList == NULL))
        return E_INVALIDARG;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = Device->hDevice;
    Destroy.hResource = (D3DKMT_HANDLE)(ULONG_PTR)pData->hResource;
    if (pData->hResource == NULL)
    {
        Destroy.phAllocationList = pData->HandleList;
        Destroy.AllocationCount = pData->NumAllocations;
    }
    Destroy.Flags = *(D3DDDICB_DESTROYALLOCATION2FLAGS *)&pData->Flags;
    return D3DUmdRtStatusToHresult(pfnDestroyAllocation2(&Destroy));
}

/* ------------------------------------------------------------------------ *
 * WDDM 2.0: monitored fences, from both sides
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtCreateSynchronizationObject2Cb(HANDLE hDevice,
                                                               D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnCreateSynchronizationObject2 == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.Info = pData->Info;

    Status = pfnCreateSynchronizationObject2(&Create);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

    /* Info travels back as well as in: a monitored fence reports the CPU and
     * GPU addresses of its value there, and without them the driver has a fence
     * it cannot read. */
    pData->Info = Create.Info;
    pData->hSyncObject = Create.hSyncObject;
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtWaitForSynchronizationObjectFromGpuCb(
    HANDLE hDevice, CONST D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait;

    if (Device == NULL || pData == NULL || pfnWaitForSynchronizationObjectFromGpu == NULL)
        return E_INVALIDARG;
    if (pData->ObjectCount == 0 || pData->ObjectHandleArray == NULL ||
        pData->MonitoredFenceValueArray == NULL)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Wait, sizeof(Wait));
    Wait.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Wait.ObjectCount = pData->ObjectCount;
    Wait.ObjectHandleArray = pData->ObjectHandleArray;
    Wait.MonitoredFenceValueArray = pData->MonitoredFenceValueArray;
    return D3DUmdRtStatusToHresult(pfnWaitForSynchronizationObjectFromGpu(&Wait));
}

static HRESULT APIENTRY D3DUmdRtSignalSynchronizationObjectFromGpuCb(
    HANDLE hDevice, CONST D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU Signal;

    if (Device == NULL || pData == NULL || pfnSignalSynchronizationObjectFromGpu == NULL)
        return E_INVALIDARG;
    if (pData->ObjectCount == 0 || pData->ObjectHandleArray == NULL ||
        pData->MonitoredFenceValueArray == NULL)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Signal, sizeof(Signal));
    Signal.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Signal.ObjectCount = pData->ObjectCount;
    Signal.ObjectHandleArray = pData->ObjectHandleArray;
    Signal.MonitoredFenceValueArray = pData->MonitoredFenceValueArray;
    return D3DUmdRtStatusToHresult(pfnSignalSynchronizationObjectFromGpu(&Signal));
}

/* ------------------------------------------------------------------------ *
 * Building the table
 * ------------------------------------------------------------------------ */

HRESULT WINAPI
D3DUmdRtCreateDeviceCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice,
    D3DDDI_DEVICECALLBACKS *pCallbacks,
    HANDLE *phRuntimeDevice)
{
    PD3DUMDRT_DEVICE Device;

    if (pCallbacks == NULL || phRuntimeDevice == NULL)
        return E_INVALIDARG;
    if (hAdapter == 0 || hDevice == 0)
        return E_INVALIDARG;
    if (!D3DUmdRtResolveProcs())
        return E_FAIL;

    Device = (PD3DUMDRT_DEVICE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Device));
    if (Device == NULL)
        return E_OUTOFMEMORY;
    Device->Magic = D3DUMDRT_DEVICE_MAGIC;
    Device->hAdapter = hAdapter;
    Device->hDevice = hDevice;

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    Device->Next = D3DUmdRtDeviceList;
    D3DUmdRtDeviceList = Device;
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    /*
     * Zero first, then fill only what is implemented.  A driver tests these for
     * NULL before using them, so an unimplemented entry left NULL is a truthful
     * "not available"; one pointed at a stub that returns S_OK is a lie the
     * driver cannot detect until its results are wrong.
     */
    ZeroMemory(pCallbacks, sizeof(*pCallbacks));
    pCallbacks->pfnAllocateCb = D3DUmdRtAllocateCb;
    pCallbacks->pfnDeallocateCb = D3DUmdRtDeallocateCb;
    pCallbacks->pfnLockCb = D3DUmdRtLockCb;
    pCallbacks->pfnUnlockCb = D3DUmdRtUnlockCb;
    pCallbacks->pfnRenderCb = D3DUmdRtRenderCb;
    pCallbacks->pfnCreateContextCb = D3DUmdRtCreateContextCb;
    pCallbacks->pfnDestroyContextCb = D3DUmdRtDestroyContextCb;
    pCallbacks->pfnEscapeCb = D3DUmdRtEscapeCb;
    pCallbacks->pfnPresentCb = D3DUmdRtPresentCb;
    pCallbacks->pfnMakeResidentCb = D3DUmdRtMakeResidentCb;
    pCallbacks->pfnEvictCb = D3DUmdRtEvictCb;
    pCallbacks->pfnCreatePagingQueueCb = D3DUmdRtCreatePagingQueueCb;
    pCallbacks->pfnDestroyPagingQueueCb = D3DUmdRtDestroyPagingQueueCb;
    pCallbacks->pfnReserveGpuVirtualAddressCb = D3DUmdRtReserveGpuVirtualAddressCb;
    pCallbacks->pfnMapGpuVirtualAddressCb = D3DUmdRtMapGpuVirtualAddressCb;
    pCallbacks->pfnFreeGpuVirtualAddressCb = D3DUmdRtFreeGpuVirtualAddressCb;
    pCallbacks->pfnCreateContextVirtualCb = D3DUmdRtCreateContextVirtualCb;
    pCallbacks->pfnSubmitCommandCb = D3DUmdRtSubmitCommandCb;
    pCallbacks->pfnLock2Cb = D3DUmdRtLock2Cb;
    pCallbacks->pfnUnlock2Cb = D3DUmdRtUnlock2Cb;
    pCallbacks->pfnDeallocate2Cb = D3DUmdRtDeallocate2Cb;
    pCallbacks->pfnCreateSynchronizationObject2Cb = D3DUmdRtCreateSynchronizationObject2Cb;
    pCallbacks->pfnWaitForSynchronizationObjectFromGpuCb = D3DUmdRtWaitForSynchronizationObjectFromGpuCb;
    pCallbacks->pfnSignalSynchronizationObjectFromGpuCb = D3DUmdRtSignalSynchronizationObjectFromGpuCb;

    *phRuntimeDevice = (HANDLE)Device;
    return S_OK;
}

HRESULT WINAPI
D3DUmdRtDestroyDeviceCallbacks(HANDLE hRuntimeDevice)
{
    PD3DUMDRT_DEVICE Device;
    PD3DUMDRT_DEVICE *Link;

    if (hRuntimeDevice == NULL || !D3DUmdRtDeviceLockReady)
        return E_INVALIDARG;

    /* Unlink and free under one lock: a handle released twice must find
     * nothing to unlink the second time rather than free the memory again. */
    EnterCriticalSection(&D3DUmdRtDeviceLock);
    for (Link = &D3DUmdRtDeviceList; *Link != NULL; Link = &(*Link)->Next)
    {
        if ((HANDLE)*Link == hRuntimeDevice)
            break;
    }
    Device = *Link;
    /*
     * Teardown is ordered: contexts and paging queues are parented to this
     * device, so releasing it first would leave kernel objects owned by
     * something that no longer exists.  Refuse and say so, rather than freeing
     * the bookkeeping and letting the driver find out later.
     */
    if (Device != NULL && InterlockedCompareExchange(&Device->LiveObjectCount, 0, 0) != 0)
    {
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
        return E_FAIL;
    }
    if (Device != NULL)
        *Link = Device->Next;
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    if (Device == NULL)
        return E_INVALIDARG;
    Device->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Device);
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hInstance);
        InitializeCriticalSection(&D3DUmdRtDeviceLock);
        D3DUmdRtDeviceLockReady = TRUE;
    }
    return TRUE;
}

/* EOF */
