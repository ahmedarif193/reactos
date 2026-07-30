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
#include <pseh/pseh2.h>
#include "d3dumdrt.h"

#define D3DUMDRT_DEVICE_MAGIC 0x54524D44u   /* 'DMRT' */

typedef struct _D3DUMDRT_RESOURCE
{
    HANDLE hRuntimeResource;
    D3DKMT_HANDLE hKMResource;
    BOOL Destroying;
    struct _D3DUMDRT_RESOURCE *Next;
} D3DUMDRT_RESOURCE, *PD3DUMDRT_RESOURCE;

typedef struct _D3DUMDRT_SYNC_OBJECT
{
    D3DKMT_HANDLE hSyncObject;
    BOOL Destroying;
    struct _D3DUMDRT_SYNC_OBJECT *Next;
} D3DUMDRT_SYNC_OBJECT, *PD3DUMDRT_SYNC_OBJECT;

typedef struct _D3DUMDRT_CONTEXT
{
    D3DKMT_HANDLE hContext;
    ULONG SyncTokenReferences;
    BOOL Destroying;
    struct _D3DUMDRT_CONTEXT *Next;
} D3DUMDRT_CONTEXT, *PD3DUMDRT_CONTEXT;

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
typedef struct _D3DUMDRT_SYNC_TOKEN
{
    HANDLE hSyncToken;
    UINT ContextCount;
    struct _D3DUMDRT_SYNC_TOKEN *Next;
    D3DKMT_HANDLE Contexts[1];
} D3DUMDRT_SYNC_TOKEN, *PD3DUMDRT_SYNC_TOKEN;
#endif

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
    PD3DUMDRT_RESOURCE       Resources;
    PD3DUMDRT_SYNC_OBJECT    SyncObjects;
    PD3DUMDRT_CONTEXT        Contexts;
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
    PD3DUMDRT_SYNC_TOKEN     SyncTokens;
#endif
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
static PFND3DKMT_SETDISPLAYMODE    pfnSetDisplayMode;
static PFND3DKMT_CREATESYNCHRONIZATIONOBJECT  pfnCreateSynchronizationObject;
static PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT pfnDestroySynchronizationObject;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECT pfnWaitForSynchronizationObject;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECT  pfnSignalSynchronizationObject;
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN8)
static PFND3DKMT_OFFERALLOCATIONS pfnOfferAllocations;
static PFND3DKMT_RECLAIMALLOCATIONS pfnReclaimAllocations;
static PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2 pfnCreateSynchronizationObject2;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 pfnWaitForSynchronizationObject2;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 pfnSignalSynchronizationObject2;
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)
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
static PFND3DKMT_UPDATEGPUVIRTUALADDRESS   pfnUpdateGpuVirtualAddress;
static PFND3DKMT_GETRESOURCEPRESENTPRIVATEDRIVERDATA pfnGetResourcePresentPrivateDriverData;
static PFND3DKMT_INVALIDATECACHE          pfnInvalidateCache;
static PFND3DKMT_RECLAIMALLOCATIONS2      pfnReclaimAllocations2;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU pfnWaitForSynchronizationObjectFromCpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU pfnSignalSynchronizationObjectFromCpu;
static PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU pfnWaitForSynchronizationObjectFromGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU  pfnSignalSynchronizationObjectFromGpu;
static PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 pfnSignalSynchronizationObjectFromGpu2;
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_1)
static PFND3DKMT_UPDATEALLOCATIONPROPERTY pfnUpdateAllocationProperty;
#endif
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
    RESOLVE(SetDisplayMode, SETDISPLAYMODE);
    RESOLVE(CreateSynchronizationObject, CREATESYNCHRONIZATIONOBJECT);
    RESOLVE(DestroySynchronizationObject, DESTROYSYNCHRONIZATIONOBJECT);
    RESOLVE(WaitForSynchronizationObject, WAITFORSYNCHRONIZATIONOBJECT);
    RESOLVE(SignalSynchronizationObject, SIGNALSYNCHRONIZATIONOBJECT);
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN8)
    RESOLVE(OfferAllocations, OFFERALLOCATIONS);
    RESOLVE(ReclaimAllocations, RECLAIMALLOCATIONS);
    RESOLVE(CreateSynchronizationObject2, CREATESYNCHRONIZATIONOBJECT2);
    RESOLVE(WaitForSynchronizationObject2, WAITFORSYNCHRONIZATIONOBJECT2);
    RESOLVE(SignalSynchronizationObject2, SIGNALSYNCHRONIZATIONOBJECT2);
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)
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
    RESOLVE(UpdateGpuVirtualAddress, UPDATEGPUVIRTUALADDRESS);
    RESOLVE(GetResourcePresentPrivateDriverData, GETRESOURCEPRESENTPRIVATEDRIVERDATA);
    RESOLVE(InvalidateCache, INVALIDATECACHE);
    RESOLVE(ReclaimAllocations2, RECLAIMALLOCATIONS2);
    RESOLVE(WaitForSynchronizationObjectFromCpu, WAITFORSYNCHRONIZATIONOBJECTFROMCPU);
    RESOLVE(SignalSynchronizationObjectFromCpu, SIGNALSYNCHRONIZATIONOBJECTFROMCPU);
    RESOLVE(WaitForSynchronizationObjectFromGpu, WAITFORSYNCHRONIZATIONOBJECTFROMGPU);
    RESOLVE(SignalSynchronizationObjectFromGpu, SIGNALSYNCHRONIZATIONOBJECTFROMGPU);
    RESOLVE(SignalSynchronizationObjectFromGpu2, SIGNALSYNCHRONIZATIONOBJECTFROMGPU2);
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_1)
    RESOLVE(UpdateAllocationProperty, UPDATEALLOCATIONPROPERTY);
#endif
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

/* D3DUmdRtDeviceLock must be held. */
static PD3DUMDRT_CONTEXT
D3DUmdRtContextLocked(
    PD3DUMDRT_DEVICE Device,
    D3DKMT_HANDLE hContext)
{
    PD3DUMDRT_CONTEXT Context;

    for (Context = Device->Contexts;
         Context != NULL;
         Context = Context->Next)
    {
        if (Context->hContext == hContext)
            return Context;
    }
    return NULL;
}

static VOID
D3DUmdRtTrackContext(
    PD3DUMDRT_DEVICE Device,
    PD3DUMDRT_CONTEXT Context,
    D3DKMT_HANDLE hContext)
{
    Context->hContext = hContext;
    EnterCriticalSection(&D3DUmdRtDeviceLock);
    Context->Next = Device->Contexts;
    Device->Contexts = Context;
    InterlockedIncrement(&Device->LiveObjectCount);
    LeaveCriticalSection(&D3DUmdRtDeviceLock);
}

static HRESULT
D3DUmdRtTranslateResourceHandles(
    _In_ PD3DUMDRT_DEVICE Device,
    _In_reads_(ResourceCount) CONST HANDLE *RuntimeResources,
    _In_ UINT ResourceCount,
    _Outptr_result_buffer_(ResourceCount) D3DKMT_HANDLE **OutHandles)
{
    HANDLE *CapturedResources = NULL;
    D3DKMT_HANDLE *Handles = NULL;
    SIZE_T CapturedSize;
    UINT Index;
    HRESULT Result = S_OK;

    if (OutHandles == NULL)
        return E_INVALIDARG;
    *OutHandles = NULL;
    if (Device == NULL || RuntimeResources == NULL || ResourceCount == 0 ||
        (SIZE_T)ResourceCount > ~(SIZE_T)0 / sizeof(*CapturedResources) ||
        (SIZE_T)ResourceCount > ~(SIZE_T)0 / sizeof(*Handles))
    {
        return E_INVALIDARG;
    }

    CapturedSize = (SIZE_T)ResourceCount * sizeof(*CapturedResources);
    CapturedResources = HeapAlloc(GetProcessHeap(), 0, CapturedSize);
    Handles = HeapAlloc(
                  GetProcessHeap(),
                  0,
                  (SIZE_T)ResourceCount * sizeof(*Handles));
    if (CapturedResources == NULL || Handles == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }

    _SEH2_TRY
    {
        CopyMemory(CapturedResources, RuntimeResources, CapturedSize);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = E_INVALIDARG;
    }
    _SEH2_END;
    if (FAILED(Result))
        goto Cleanup;

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    for (Index = 0; Index < ResourceCount; ++Index)
    {
        PD3DUMDRT_RESOURCE Resource;

        for (Resource = Device->Resources;
             Resource != NULL;
             Resource = Resource->Next)
        {
            if (Resource->hRuntimeResource == CapturedResources[Index])
                break;
        }
        if (Resource == NULL || Resource->Destroying ||
            Resource->hKMResource == 0)
        {
            Result = E_INVALIDARG;
            break;
        }
        Handles[Index] = Resource->hKMResource;
    }
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    if (SUCCEEDED(Result))
    {
        *OutHandles = Handles;
        Handles = NULL;
    }

Cleanup:
    if (Handles != NULL)
        HeapFree(GetProcessHeap(), 0, Handles);
    if (CapturedResources != NULL)
        HeapFree(GetProcessHeap(), 0, CapturedResources);
    return Result;
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
    PD3DUMDRT_RESOURCE Resource = NULL;
    PD3DUMDRT_RESOURCE Existing;
    D3DKMT_CREATEALLOCATION Create;
    D3DKMT_DESTROYALLOCATION Destroy;
    NTSTATUS Status;
    BOOL CreatesResource;

    if (Device == NULL || pData == NULL || pfnCreateAllocation == NULL)
        return E_INVALIDARG;
    if (pData->NumAllocations == 0 || pData->pAllocationInfo == NULL)
        return E_INVALIDARG;

    CreatesResource =
        pData->hResource != NULL && pData->hKMResource == 0;
    if (CreatesResource)
    {
        Resource = (PD3DUMDRT_RESOURCE)HeapAlloc(
                       GetProcessHeap(),
                       HEAP_ZERO_MEMORY,
                       sizeof(*Resource));
        if (Resource == NULL)
            return E_OUTOFMEMORY;

        EnterCriticalSection(&D3DUmdRtDeviceLock);
        for (Existing = Device->Resources;
             Existing != NULL;
             Existing = Existing->Next)
        {
            if (Existing->hRuntimeResource == pData->hResource)
                break;
        }
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
        if (Existing != NULL)
        {
            HeapFree(GetProcessHeap(), 0, Resource);
            return E_INVALIDARG;
        }
    }

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    /* A zero hKMResource creates a new kernel resource for the runtime's
     * opaque handle; a nonzero value appends allocations to that resource. */
    Create.hResource = pData->hKMResource;
    Create.pPrivateDriverData = (VOID *)pData->pPrivateDriverData;
    Create.PrivateDriverDataSize = pData->PrivateDriverDataSize;
    Create.NumAllocations = pData->NumAllocations;
    Create.pAllocationInfo = pData->pAllocationInfo;
    Create.Flags.CreateResource = CreatesResource ? 1 : 0;
    Create.hPrivateRuntimeResourceHandle = pData->hResource;

    Status = pfnCreateAllocation(&Create);
    if (Status < 0)
    {
        if (Resource != NULL)
            HeapFree(GetProcessHeap(), 0, Resource);
        return D3DUmdRtStatusToHresult(Status);
    }

    pData->hKMResource = Create.hResource;
    if (CreatesResource)
    {
        if (Create.hResource == 0)
        {
            UINT Index;

            for (Index = 0; Index < pData->NumAllocations; ++Index)
            {
                D3DKMT_HANDLE Allocation =
                    pData->pAllocationInfo[Index].hAllocation;

                if (Allocation == 0)
                    continue;
                ZeroMemory(&Destroy, sizeof(Destroy));
                Destroy.hDevice = Device->hDevice;
                Destroy.phAllocationList = &Allocation;
                Destroy.AllocationCount = 1;
                (VOID)pfnDestroyAllocation(&Destroy);
            }
            HeapFree(GetProcessHeap(), 0, Resource);
            return E_FAIL;
        }

        Resource->hRuntimeResource = pData->hResource;
        Resource->hKMResource = Create.hResource;
        EnterCriticalSection(&D3DUmdRtDeviceLock);
        Resource->Next = Device->Resources;
        Device->Resources = Resource;
        InterlockedIncrement(&Device->LiveObjectCount);
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
    }
    return S_OK;
}

static HRESULT APIENTRY D3DUmdRtDeallocateCb(HANDLE hDevice, CONST D3DDDICB_DEALLOCATE *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    PD3DUMDRT_RESOURCE Resource = NULL;
    PD3DUMDRT_RESOURCE *Link;
    D3DKMT_DESTROYALLOCATION Destroy;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnDestroyAllocation == NULL)
        return E_INVALIDARG;
    /* Either a resource or a handle list, never neither: with both empty there
     * is nothing named to free, and succeeding would tell the driver its memory
     * was released when it was not. */
    if (pData->hResource == NULL && (pData->NumAllocations == 0 || pData->HandleList == NULL))
        return E_INVALIDARG;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = Device->hDevice;
    if (pData->hResource != NULL)
    {
        EnterCriticalSection(&D3DUmdRtDeviceLock);
        for (Resource = Device->Resources;
             Resource != NULL;
             Resource = Resource->Next)
        {
            if (Resource->hRuntimeResource == pData->hResource)
                break;
        }
        if (Resource == NULL || Resource->Destroying)
        {
            LeaveCriticalSection(&D3DUmdRtDeviceLock);
            return E_INVALIDARG;
        }
        Resource->Destroying = TRUE;
        Destroy.hResource = Resource->hKMResource;
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
    }
    else
    {
        Destroy.phAllocationList = pData->HandleList;
        Destroy.AllocationCount = pData->NumAllocations;
    }

    Status = pfnDestroyAllocation(&Destroy);
    if (Resource != NULL)
    {
        EnterCriticalSection(&D3DUmdRtDeviceLock);
        if (Status >= 0)
        {
            for (Link = &Device->Resources;
                 *Link != NULL;
                 Link = &(*Link)->Next)
            {
                if (*Link == Resource)
                {
                    *Link = Resource->Next;
                    break;
                }
            }
            InterlockedDecrement(&Device->LiveObjectCount);
        }
        else
        {
            Resource->Destroying = FALSE;
        }
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
        if (Status >= 0)
            HeapFree(GetProcessHeap(), 0, Resource);
    }
    return D3DUmdRtStatusToHresult(Status);
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
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN7)
    pData->GpuVirtualAddress = Lock.GpuVirtualAddress;
#endif
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
    PD3DUMDRT_CONTEXT Context;
    D3DKMT_CREATECONTEXT Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnCreateContext == NULL)
        return E_INVALIDARG;

    Context = HeapAlloc(
                  GetProcessHeap(),
                  HEAP_ZERO_MEMORY,
                  sizeof(*Context));
    if (Context == NULL)
        return E_OUTOFMEMORY;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.NodeOrdinal = pData->NodeOrdinal;
    Create.EngineAffinity = pData->EngineAffinity;
    Create.Flags = pData->Flags;
    Create.pPrivateDriverData = pData->pPrivateDriverData;
    Create.PrivateDriverDataSize = pData->PrivateDriverDataSize;

    Status = pfnCreateContext(&Create);
    if (Status < 0)
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return D3DUmdRtStatusToHresult(Status);
    }
    if (Create.hContext == 0)
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return E_FAIL;
    }

    /*
     * The driver receives the context's first command buffer and both lists
     * here.  All three come back from the kernel together and are useless
     * apart -- a buffer with no allocation list cannot name what it touches.
     */
    D3DUmdRtTrackContext(
        Device,
        Context,
        Create.hContext);
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
    PD3DUMDRT_CONTEXT Context;
    PD3DUMDRT_CONTEXT *Link;
    D3DKMT_DESTROYCONTEXT Destroy;
    D3DKMT_HANDLE hContext;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL ||
        pfnDestroyContext == NULL ||
        pData->hContext == NULL ||
        (ULONG_PTR)pData->hContext > MAXDWORD)
    {
        return E_INVALIDARG;
    }

    hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    EnterCriticalSection(&D3DUmdRtDeviceLock);
    Context = D3DUmdRtContextLocked(Device, hContext);
    if (Context == NULL || Context->Destroying ||
        Context->SyncTokenReferences != 0)
    {
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
        return E_INVALIDARG;
    }
    Context->Destroying = TRUE;
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hContext = hContext;
    Status = pfnDestroyContext(&Destroy);

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    if (Status >= 0)
    {
        for (Link = &Device->Contexts;
             *Link != NULL;
             Link = &(*Link)->Next)
        {
            if (*Link == Context)
            {
                *Link = Context->Next;
                break;
            }
        }
        InterlockedDecrement(&Device->LiveObjectCount);
    }
    else
    {
        Context->Destroying = FALSE;
    }
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    if (Status >= 0)
        HeapFree(GetProcessHeap(), 0, Context);
    return D3DUmdRtStatusToHresult(Status);
}

static HRESULT APIENTRY D3DUmdRtRenderCb(HANDLE hDevice, D3DDDICB_RENDER *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_RENDER Render;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnRender == NULL)
        return E_INVALIDARG;
    if (pData->BroadcastContextCount >
        D3DDDI_MAX_BROADCAST_CONTEXT)
    {
        return E_INVALIDARG;
    }

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
    if (pData->BroadcastContextCount != 0)
    {
        UINT Index;

        for (Index = 0;
             Index < pData->BroadcastContextCount;
             ++Index)
        {
            Render.BroadcastContext[Index] =
                (D3DKMT_HANDLE)(ULONG_PTR)
                    pData->BroadcastContext[Index];
        }
    }
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN7)
    Render.pPrivateDriverData = pData->pPrivateDriverData;
    Render.PrivateDriverDataSize =
        pData->PrivateDriverDataSize;
#endif

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
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN7)
    pData->NewCommandBuffer = Render.NewCommandBuffer;
#endif
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * Display mode
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY
D3DUmdRtSetDisplayModeCb(
    HANDLE hDevice,
    D3DDDICB_SETDISPLAYMODE *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SETDISPLAYMODE SetMode;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL ||
        pData->hPrimaryAllocation == 0)
    {
        return E_INVALIDARG;
    }
    if (pfnSetDisplayMode == NULL)
        return E_NOTIMPL;

    ZeroMemory(&SetMode, sizeof(SetMode));
    SetMode.hDevice = Device->hDevice;
    SetMode.hPrimaryAllocation = pData->hPrimaryAllocation;
    SetMode.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    SetMode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
    Status = pfnSetDisplayMode(&SetMode);
    pData->PrivateDriverFormatAttribute =
        SetMode.PrivateDriverFormatAttribute;
    return D3DUmdRtStatusToHresult(Status);
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
    if (pData->BroadcastContextCount >
        D3DDDI_MAX_BROADCAST_CONTEXT)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Present, sizeof(Present));
    Present.hDevice = Device->hDevice;
    if (pData->hContext != NULL)
    {
        Present.hContext =
            (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    }
    Present.hSource = pData->hSrcAllocation;
    Present.hDestination = pData->hDstAllocation;
    if (pData->hDstAllocation != 0)
        Present.Flags.Blt = 1;
    else
        Present.Flags.Flip = 1;
    Present.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    Present.BroadcastContextCount = pData->BroadcastContextCount;
    for (Index = 0;
         Index < pData->BroadcastContextCount;
         ++Index)
    {
        Present.BroadcastContext[Index] = (D3DKMT_HANDLE)(ULONG_PTR)pData->BroadcastContext[Index];
    }
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)
    Present.BroadcastSrcAllocation = pData->BroadcastSrcAllocation;
    Present.BroadcastDstAllocation = pData->BroadcastDstAllocation;
    Present.pPrivateDriverData = pData->pPrivateDriverData;
    Present.PrivateDriverDataSize = pData->PrivateDriverDataSize;
#endif

    Status = pfnPresent(&Present);
    if (Status < 0)
        return D3DUmdRtStatusToHresult(Status);

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)
    /*
     * Whether a compositor owns the screen is decided below this callback.
     * Return the KMT result verbatim; inferring it from BLT versus FLIP would
     * report composition even when no compositor owns the source.
     */
    pData->bOptimizeForComposition =
        Present.bOptimizeForComposition;
#endif
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

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN8)

/* ------------------------------------------------------------------------ *
 * Win8: allocation offer and reclaim
 * ------------------------------------------------------------------------ */

static BOOL
D3DUmdRtOfferPriority(
    D3DDDI_OFFER_PRIORITY Priority,
    D3DKMT_OFFER_PRIORITY *pKmtPriority)
{
    switch (Priority)
    {
        case D3DDDI_OFFER_PRIORITY_LOW:
            *pKmtPriority = D3DKMT_OFFER_PRIORITY_LOW;
            return TRUE;
        case D3DDDI_OFFER_PRIORITY_NORMAL:
            *pKmtPriority = D3DKMT_OFFER_PRIORITY_NORMAL;
            return TRUE;
        case D3DDDI_OFFER_PRIORITY_HIGH:
            *pKmtPriority = D3DKMT_OFFER_PRIORITY_HIGH;
            return TRUE;
        case D3DDDI_OFFER_PRIORITY_AUTO:
            *pKmtPriority = D3DKMT_OFFER_PRIORITY_AUTO;
            return TRUE;
        default:
            return FALSE;
    }
}

static HRESULT APIENTRY
D3DUmdRtOfferAllocationsCb(
    HANDLE hDevice,
    CONST D3DDDICB_OFFERALLOCATIONS *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_OFFERALLOCATIONS Offer;
    D3DKMT_HANDLE *ResourceHandles = NULL;
    HRESULT Result;

    if (Device == NULL || pData == NULL || pfnOfferAllocations == NULL)
        return E_INVALIDARG;
    if (pData->NumAllocations == 0 ||
        (pData->pResources == NULL) == (pData->HandleList == NULL))
    {
        return E_INVALIDARG;
    }

    if (pData->pResources != NULL)
    {
        Result = D3DUmdRtTranslateResourceHandles(
                     Device,
                     pData->pResources,
                     pData->NumAllocations,
                     &ResourceHandles);
        if (FAILED(Result))
            return Result;
    }

    ZeroMemory(&Offer, sizeof(Offer));
    Offer.hDevice = Device->hDevice;
    if (ResourceHandles != NULL)
        Offer.pResources = ResourceHandles;
    else
        Offer.HandleList = pData->HandleList;
    Offer.NumAllocations = pData->NumAllocations;
    if (!D3DUmdRtOfferPriority(pData->Priority, &Offer.Priority))
    {
        Result = E_INVALIDARG;
        goto Cleanup;
    }

    Result = D3DUmdRtStatusToHresult(pfnOfferAllocations(&Offer));

Cleanup:
    if (ResourceHandles != NULL)
        HeapFree(GetProcessHeap(), 0, ResourceHandles);
    return Result;
}

static HRESULT APIENTRY
D3DUmdRtReclaimAllocationsCb(
    HANDLE hDevice,
    CONST D3DDDICB_RECLAIMALLOCATIONS *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_RECLAIMALLOCATIONS Reclaim;
    D3DKMT_HANDLE *ResourceHandles = NULL;
    HRESULT Result;

    if (Device == NULL || pData == NULL || pfnReclaimAllocations == NULL)
        return E_INVALIDARG;
    if (pData->NumAllocations == 0 ||
        (pData->pResources == NULL) == (pData->HandleList == NULL))
    {
        return E_INVALIDARG;
    }
    if (pData->pResources != NULL)
    {
        Result = D3DUmdRtTranslateResourceHandles(
                     Device,
                     pData->pResources,
                     pData->NumAllocations,
                     &ResourceHandles);
        if (FAILED(Result))
            return Result;
    }

    ZeroMemory(&Reclaim, sizeof(Reclaim));
    Reclaim.hDevice = Device->hDevice;
    if (ResourceHandles != NULL)
        Reclaim.pResources = ResourceHandles;
    else
        Reclaim.HandleList = pData->HandleList;
    Reclaim.pDiscarded = pData->pDiscarded;
    Reclaim.NumAllocations = pData->NumAllocations;
    Result = D3DUmdRtStatusToHresult(pfnReclaimAllocations(&Reclaim));
    if (ResourceHandles != NULL)
        HeapFree(GetProcessHeap(), 0, ResourceHandles);
    return Result;
}

#endif /* D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN8 */

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)

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

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)
static HRESULT APIENTRY
D3DUmdRtReclaimAllocations2Cb(
    HANDLE hDevice,
    D3DDDICB_RECLAIMALLOCATIONS2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_RECLAIMALLOCATIONS2 Reclaim;
    D3DKMT_HANDLE *ResourceHandles = NULL;
    NTSTATUS Status;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        pfnReclaimAllocations2 == NULL)
    {
        return E_INVALIDARG;
    }
    if (pData->PagingQueue == 0 ||
        pData->NumAllocations == 0 ||
        (pData->pResources == NULL) == (pData->HandleList == NULL))
    {
        return E_INVALIDARG;
    }

    if (pData->pResources != NULL)
    {
        Result = D3DUmdRtTranslateResourceHandles(
                     Device,
                     pData->pResources,
                     pData->NumAllocations,
                     &ResourceHandles);
        if (FAILED(Result))
            return Result;
    }

    ZeroMemory(&Reclaim, sizeof(Reclaim));
    Reclaim.hPagingQueue = pData->PagingQueue;
    Reclaim.NumAllocations = pData->NumAllocations;
    if (ResourceHandles != NULL)
        Reclaim.pResources = ResourceHandles;
    else
        Reclaim.HandleList = pData->HandleList;
    Reclaim.pDiscarded = pData->pDiscarded;

    Status = pfnReclaimAllocations2(&Reclaim);
    if (Status >= 0)
        pData->PagingFenceValue = Reclaim.PagingFenceValue;
    Result = D3DUmdRtStatusToHresult(Status);

    if (ResourceHandles != NULL)
        HeapFree(GetProcessHeap(), 0, ResourceHandles);
    return Result;
}
#endif

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_1)
static HRESULT APIENTRY
D3DUmdRtUpdateAllocationPropertyCb(
    HANDLE hDevice,
    D3DDDI_UPDATEALLOCPROPERTY *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);

    if (Device == NULL || pData == NULL ||
        pfnUpdateAllocationProperty == NULL ||
        pData->hPagingQueue == 0 ||
        pData->hAllocation == 0)
    {
        return E_INVALIDARG;
    }

    return D3DUmdRtStatusToHresult(
               pfnUpdateAllocationProperty(pData));
}

static HRESULT APIENTRY
D3DUmdRtOfferAllocations2Cb(
    HANDLE hDevice,
    CONST D3DDDICB_OFFERALLOCATIONS2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_OFFERALLOCATIONS Offer;
    D3DKMT_HANDLE *ResourceHandles = NULL;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        pfnOfferAllocations == NULL ||
        pData->NumAllocations == 0 ||
        (pData->pResources == NULL) ==
            (pData->HandleList == NULL) ||
        pData->Flags.Reserved != 0)
    {
        return E_INVALIDARG;
    }

    if (pData->pResources != NULL)
    {
        Result = D3DUmdRtTranslateResourceHandles(
                     Device,
                     pData->pResources,
                     pData->NumAllocations,
                     &ResourceHandles);
        if (FAILED(Result))
            return Result;
    }

    ZeroMemory(&Offer, sizeof(Offer));
    Offer.hDevice = Device->hDevice;
    if (ResourceHandles != NULL)
        Offer.pResources = ResourceHandles;
    else
        Offer.HandleList = pData->HandleList;
    Offer.NumAllocations = pData->NumAllocations;
    Offer.Flags.AllowDecommit = pData->Flags.AllowDecommit;
    if (!D3DUmdRtOfferPriority(
             pData->Priority,
             &Offer.Priority))
    {
        Result = E_INVALIDARG;
        goto CleanupOffer2;
    }

    Result = D3DUmdRtStatusToHresult(
                 pfnOfferAllocations(&Offer));

CleanupOffer2:
    if (ResourceHandles != NULL)
        HeapFree(GetProcessHeap(), 0, ResourceHandles);
    return Result;
}
#endif

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
static HRESULT APIENTRY
D3DUmdRtReclaimAllocations3Cb(
    HANDLE hDevice,
    D3DDDICB_RECLAIMALLOCATIONS3 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_RECLAIMALLOCATIONS2 Reclaim;
    D3DKMT_HANDLE *ResourceHandles = NULL;
    NTSTATUS Status;
    HRESULT Result;

    if (Device == NULL || pData == NULL ||
        pfnReclaimAllocations2 == NULL ||
        pData->PagingQueue == 0 ||
        pData->NumAllocations == 0 ||
        pData->pResults == NULL ||
        (pData->pResources == NULL) ==
            (pData->HandleList == NULL))
    {
        return E_INVALIDARG;
    }

    if (pData->pResources != NULL)
    {
        Result = D3DUmdRtTranslateResourceHandles(
                     Device,
                     pData->pResources,
                     pData->NumAllocations,
                     &ResourceHandles);
        if (FAILED(Result))
            return Result;
    }

    ZeroMemory(&Reclaim, sizeof(Reclaim));
    Reclaim.hPagingQueue = pData->PagingQueue;
    Reclaim.NumAllocations = pData->NumAllocations;
    if (ResourceHandles != NULL)
        Reclaim.pResources = ResourceHandles;
    else
        Reclaim.HandleList = pData->HandleList;
    Reclaim.pResults = pData->pResults;

    Status = pfnReclaimAllocations2(&Reclaim);
    if (Status >= 0)
        pData->PagingFenceValue = Reclaim.PagingFenceValue;
    Result = D3DUmdRtStatusToHresult(Status);

    if (ResourceHandles != NULL)
        HeapFree(GetProcessHeap(), 0, ResourceHandles);
    return Result;
}

static HRESULT
D3DUmdRtCaptureSyncContexts(
    CONST D3DDDICB_SYNCTOKEN *pData,
    D3DKMT_HANDLE *Contexts)
{
    HANDLE Captured[D3DDDI_MAX_BROADCAST_CONTEXT];
    UINT Index;
    UINT Other;
    HRESULT Result = S_OK;

    if (pData == NULL || Contexts == NULL ||
        pData->hSyncToken == NULL ||
        pData->BroadcastContextCount == 0 ||
        pData->BroadcastContextCount >
            D3DDDI_MAX_BROADCAST_CONTEXT ||
        pData->BroadcastContextArray == NULL)
    {
        return E_INVALIDARG;
    }

    _SEH2_TRY
    {
        CopyMemory(
            Captured,
            pData->BroadcastContextArray,
            (SIZE_T)pData->BroadcastContextCount *
                sizeof(*Captured));
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Result = E_INVALIDARG;
    }
    _SEH2_END;
    if (FAILED(Result))
        return Result;

    for (Index = 0;
         Index < pData->BroadcastContextCount;
         ++Index)
    {
        if (Captured[Index] == NULL ||
            (ULONG_PTR)Captured[Index] > MAXDWORD)
        {
            return E_INVALIDARG;
        }
        for (Other = 0; Other < Index; ++Other)
        {
            if (Captured[Other] == Captured[Index])
                return E_INVALIDARG;
        }
        Contexts[Index] =
            (D3DKMT_HANDLE)(ULONG_PTR)Captured[Index];
    }
    return S_OK;
}

static HRESULT APIENTRY
D3DUmdRtAcquireResourceCb(
    HANDLE hDevice,
    CONST D3DDDICB_SYNCTOKEN *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    PD3DUMDRT_SYNC_TOKEN Token;
    PD3DUMDRT_SYNC_TOKEN Existing;
    PD3DUMDRT_CONTEXT Context;
    D3DKMT_HANDLE Contexts[D3DDDI_MAX_BROADCAST_CONTEXT];
    SIZE_T TokenSize;
    UINT Index;
    HRESULT Result;

    if (Device == NULL)
        return E_INVALIDARG;

    Result = D3DUmdRtCaptureSyncContexts(
                 pData,
                 Contexts);
    if (FAILED(Result))
        return Result;

    if ((SIZE_T)pData->BroadcastContextCount >
        ((SIZE_T)-1 -
         FIELD_OFFSET(D3DUMDRT_SYNC_TOKEN, Contexts)) /
            sizeof(Token->Contexts[0]))
    {
        return E_INVALIDARG;
    }
    TokenSize =
        FIELD_OFFSET(D3DUMDRT_SYNC_TOKEN, Contexts) +
        (SIZE_T)pData->BroadcastContextCount *
            sizeof(Token->Contexts[0]);
    Token = HeapAlloc(
                GetProcessHeap(),
                HEAP_ZERO_MEMORY,
                TokenSize);
    if (Token == NULL)
        return E_OUTOFMEMORY;

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    for (Existing = Device->SyncTokens;
         Existing != NULL;
         Existing = Existing->Next)
    {
        if (Existing->hSyncToken == pData->hSyncToken)
            break;
    }
    if (Existing != NULL)
    {
        Result = E_INVALIDARG;
        goto UnlockAcquire;
    }

    for (Index = 0;
         Index < pData->BroadcastContextCount;
         ++Index)
    {
        Context = D3DUmdRtContextLocked(
                      Device,
                      Contexts[Index]);
        if (Context == NULL || Context->Destroying)
        {
            Result = E_INVALIDARG;
            goto UnlockAcquire;
        }
    }

    Token->hSyncToken = pData->hSyncToken;
    Token->ContextCount = pData->BroadcastContextCount;
    CopyMemory(
        Token->Contexts,
        Contexts,
        (SIZE_T)Token->ContextCount *
            sizeof(Token->Contexts[0]));
    for (Index = 0; Index < Token->ContextCount; ++Index)
    {
        Context = D3DUmdRtContextLocked(
                      Device,
                      Token->Contexts[Index]);
        ++Context->SyncTokenReferences;
    }
    Token->Next = Device->SyncTokens;
    Device->SyncTokens = Token;
    InterlockedIncrement(&Device->LiveObjectCount);
    Token = NULL;
    Result = S_OK;

UnlockAcquire:
    LeaveCriticalSection(&D3DUmdRtDeviceLock);
    if (Token != NULL)
        HeapFree(GetProcessHeap(), 0, Token);
    return Result;
}

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_3)
static HRESULT APIENTRY
D3DUmdRtReleaseResourceCb(
    HANDLE hDevice,
    CONST D3DDDICB_SYNCTOKEN *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    PD3DUMDRT_SYNC_TOKEN Token;
    PD3DUMDRT_SYNC_TOKEN *Link;
    PD3DUMDRT_CONTEXT Context;
    D3DKMT_HANDLE Contexts[D3DDDI_MAX_BROADCAST_CONTEXT];
    UINT Index;
    UINT Other;
    HRESULT Result;

    if (Device == NULL)
        return E_INVALIDARG;
    Result = D3DUmdRtCaptureSyncContexts(
                 pData,
                 Contexts);
    if (FAILED(Result))
        return Result;

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    for (Link = &Device->SyncTokens;
         *Link != NULL;
         Link = &(*Link)->Next)
    {
        if ((*Link)->hSyncToken == pData->hSyncToken)
            break;
    }
    Token = *Link;
    if (Token == NULL ||
        Token->ContextCount != pData->BroadcastContextCount)
    {
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
        return E_INVALIDARG;
    }

    for (Index = 0; Index < Token->ContextCount; ++Index)
    {
        for (Other = 0; Other < Token->ContextCount; ++Other)
        {
            if (Token->Contexts[Index] == Contexts[Other])
                break;
        }
        if (Other == Token->ContextCount)
        {
            LeaveCriticalSection(&D3DUmdRtDeviceLock);
            return E_INVALIDARG;
        }
    }

    *Link = Token->Next;
    for (Index = 0; Index < Token->ContextCount; ++Index)
    {
        Context = D3DUmdRtContextLocked(
                      Device,
                      Token->Contexts[Index]);
        if (Context != NULL &&
            Context->SyncTokenReferences != 0)
        {
            --Context->SyncTokenReferences;
        }
    }
    InterlockedDecrement(&Device->LiveObjectCount);
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    HeapFree(GetProcessHeap(), 0, Token);
    return S_OK;
}
#endif
#endif

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

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)

static HRESULT APIENTRY
D3DUmdRtUpdateGpuVirtualAddressCb(
    HANDLE hDevice,
    CONST D3DDDICB_UPDATEGPUVIRTUALADDRESS *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_UPDATEGPUVIRTUALADDRESS Update;

    if (Device == NULL || pData == NULL ||
        pfnUpdateGpuVirtualAddress == NULL ||
        pData->hContext == NULL ||
        (ULONG_PTR)pData->hContext > (ULONG_PTR)MAXDWORD ||
        pData->NumOperations == 0 ||
        pData->Operations == NULL ||
        pData->Reserved0 != 0 ||
        pData->Reserved1 != 0 ||
        pData->Flags.Reserved != 0)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Update, sizeof(Update));
    Update.hDevice = Device->hDevice;
    Update.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Update.hFenceObject = pData->hFenceObject;
    Update.NumOperations = pData->NumOperations;
    Update.Operations = pData->Operations;
    Update.Reserved0 = pData->Reserved0;
    Update.Reserved1 = pData->Reserved1;
    Update.FenceValue = pData->FenceValue;
    Update.Flags.Value = pData->Flags.Value;
    return D3DUmdRtStatusToHresult(
               pfnUpdateGpuVirtualAddress(&Update));
}

static HRESULT APIENTRY
D3DUmdRtGetResourcePresentPrivateDriverDataCb(
    HANDLE hDevice,
    D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    NTSTATUS Status;

    if (Device == NULL ||
        pData == NULL ||
        pfnGetResourcePresentPrivateDriverData == NULL ||
        pData->hResource == 0 ||
        (pData->PrivateDriverDataSize != 0 &&
         pData->pPrivateDriverData == NULL))
    {
        return E_INVALIDARG;
    }

    Status = pfnGetResourcePresentPrivateDriverData(pData);
    if (Status == STATUS_INVALID_BUFFER_SIZE)
    {
        /*
         * This callback's contract names the NT status itself as the HRESULT
         * used for a size query.  Do not flatten it to E_FAIL: the driver must
         * distinguish "retry with this size" from a real query failure.
         */
        return (HRESULT)STATUS_INVALID_BUFFER_SIZE;
    }
    return D3DUmdRtStatusToHresult(Status);
}

static HRESULT APIENTRY
D3DUmdRtInvalidateCacheCb(
    HANDLE hDevice,
    CONST D3DDDICB_INVALIDATECACHE *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_INVALIDATECACHE Invalidate;

    if (Device == NULL ||
        pData == NULL ||
        pfnInvalidateCache == NULL ||
        pData->hAllocation == 0 ||
        pData->Length == 0 ||
        pData->Offset > (SIZE_T)-1 - pData->Length)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Invalidate, sizeof(Invalidate));
    Invalidate.hDevice = Device->hDevice;
    Invalidate.hAllocation = pData->hAllocation;
    Invalidate.Offset = pData->Offset;
    Invalidate.Length = pData->Length;
    return D3DUmdRtStatusToHresult(pfnInvalidateCache(&Invalidate));
}

#endif /* D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0 */

/* ------------------------------------------------------------------------ *
 * WDDM 2.0: virtual-addressing contexts and submission
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY D3DUmdRtCreateContextVirtualCb(HANDLE hDevice,
                                                       D3DDDICB_CREATECONTEXTVIRTUAL *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    PD3DUMDRT_CONTEXT Context;
    D3DKMT_CREATECONTEXTVIRTUAL Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnCreateContextVirtual == NULL)
        return E_INVALIDARG;

    Context = HeapAlloc(
                  GetProcessHeap(),
                  HEAP_ZERO_MEMORY,
                  sizeof(*Context));
    if (Context == NULL)
        return E_OUTOFMEMORY;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.NodeOrdinal = pData->NodeOrdinal;
    Create.EngineAffinity = pData->EngineAffinity;
    Create.Flags = pData->Flags;
    Create.pPrivateDriverData = pData->pPrivateDriverData;
    Create.PrivateDriverDataSize = pData->PrivateDriverDataSize;

    Status = pfnCreateContextVirtual(&Create);
    if (Status < 0)
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return D3DUmdRtStatusToHresult(Status);
    }
    if (Create.hContext == 0)
    {
        HeapFree(GetProcessHeap(), 0, Context);
        return E_FAIL;
    }

    /*
     * Unlike CreateContext there is no command buffer here, and that is the
     * point: a virtual-addressing context submits from buffers the driver
     * allocated and placed in its own GPU address space, so the kernel has none
     * to hand out.
     */
    D3DUmdRtTrackContext(
        Device,
        Context,
        Create.hContext);
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
    PD3DUMDRT_RESOURCE Resource = NULL;
    PD3DUMDRT_RESOURCE *Link;
    D3DKMT_DESTROYALLOCATION2 Destroy;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL || pfnDestroyAllocation2 == NULL)
        return E_INVALIDARG;
    if (pData->hResource == NULL && (pData->NumAllocations == 0 || pData->HandleList == NULL))
        return E_INVALIDARG;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hDevice = Device->hDevice;
    if (pData->hResource != NULL)
    {
        /*
         * hResource is the runtime's opaque cookie, just as it is for the
         * WDDM 1.x callback. Resolve it to the KMT resource created by
         * AllocateCb instead of passing a user pointer to the kernel as a
         * D3DKMT handle.
         */
        EnterCriticalSection(&D3DUmdRtDeviceLock);
        for (Resource = Device->Resources;
             Resource != NULL;
             Resource = Resource->Next)
        {
            if (Resource->hRuntimeResource == pData->hResource)
                break;
        }
        if (Resource == NULL || Resource->Destroying)
        {
            LeaveCriticalSection(&D3DUmdRtDeviceLock);
            return E_INVALIDARG;
        }
        Resource->Destroying = TRUE;
        Destroy.hResource = Resource->hKMResource;
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
    }
    else
    {
        Destroy.phAllocationList = pData->HandleList;
        Destroy.AllocationCount = pData->NumAllocations;
    }
    Destroy.Flags = *(D3DDDICB_DESTROYALLOCATION2FLAGS *)&pData->Flags;

    Status = pfnDestroyAllocation2(&Destroy);
    if (Resource != NULL)
    {
        EnterCriticalSection(&D3DUmdRtDeviceLock);
        if (Status >= 0)
        {
            for (Link = &Device->Resources;
                 *Link != NULL;
                 Link = &(*Link)->Next)
            {
                if (*Link == Resource)
                {
                    *Link = Resource->Next;
                    break;
                }
            }
            InterlockedDecrement(&Device->LiveObjectCount);
        }
        else
        {
            Resource->Destroying = FALSE;
        }
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
        if (Status >= 0)
            HeapFree(GetProcessHeap(), 0, Resource);
    }
    return D3DUmdRtStatusToHresult(Status);
}

#endif /* D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0 */

/* ------------------------------------------------------------------------ *
 * Synchronization-object lifetime and ordered context operations
 * ------------------------------------------------------------------------ */

static VOID
D3DUmdRtTrackSynchronizationObject(
    PD3DUMDRT_DEVICE Device,
    PD3DUMDRT_SYNC_OBJECT SyncObject,
    D3DKMT_HANDLE hSyncObject)
{
    SyncObject->hSyncObject = hSyncObject;
    EnterCriticalSection(&D3DUmdRtDeviceLock);
    SyncObject->Next = Device->SyncObjects;
    Device->SyncObjects = SyncObject;
    InterlockedIncrement(&Device->LiveObjectCount);
    LeaveCriticalSection(&D3DUmdRtDeviceLock);
}

static HRESULT APIENTRY
D3DUmdRtCreateSynchronizationObjectCb(
    HANDLE hDevice,
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    PD3DUMDRT_SYNC_OBJECT SyncObject;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL ||
        pfnCreateSynchronizationObject == NULL)
    {
        return E_INVALIDARG;
    }

    SyncObject = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*SyncObject));
    if (SyncObject == NULL)
        return E_OUTOFMEMORY;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.Info = pData->Info;
    Status = pfnCreateSynchronizationObject(&Create);
    if (Status < 0)
    {
        HeapFree(GetProcessHeap(), 0, SyncObject);
        return D3DUmdRtStatusToHresult(Status);
    }
    if (Create.hSyncObject == 0)
    {
        HeapFree(GetProcessHeap(), 0, SyncObject);
        return E_FAIL;
    }

    D3DUmdRtTrackSynchronizationObject(Device, SyncObject,
                                       Create.hSyncObject);
    pData->hSyncObject = Create.hSyncObject;
    return S_OK;
}

static HRESULT APIENTRY
D3DUmdRtDestroySynchronizationObjectCb(
    HANDLE hDevice,
    CONST D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    PD3DUMDRT_SYNC_OBJECT SyncObject;
    PD3DUMDRT_SYNC_OBJECT *Link;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL ||
        pfnDestroySynchronizationObject == NULL ||
        pData->hSyncObject == 0)
    {
        return E_INVALIDARG;
    }

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    for (SyncObject = Device->SyncObjects;
         SyncObject != NULL;
         SyncObject = SyncObject->Next)
    {
        if (SyncObject->hSyncObject == pData->hSyncObject)
            break;
    }
    if (SyncObject == NULL || SyncObject->Destroying)
    {
        LeaveCriticalSection(&D3DUmdRtDeviceLock);
        return E_INVALIDARG;
    }
    SyncObject->Destroying = TRUE;
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hSyncObject = pData->hSyncObject;
    Status = pfnDestroySynchronizationObject(&Destroy);

    EnterCriticalSection(&D3DUmdRtDeviceLock);
    if (Status >= 0)
    {
        for (Link = &Device->SyncObjects;
             *Link != NULL;
             Link = &(*Link)->Next)
        {
            if (*Link == SyncObject)
            {
                *Link = SyncObject->Next;
                break;
            }
        }
        InterlockedDecrement(&Device->LiveObjectCount);
    }
    else
    {
        SyncObject->Destroying = FALSE;
    }
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    if (Status >= 0)
        HeapFree(GetProcessHeap(), 0, SyncObject);
    return D3DUmdRtStatusToHresult(Status);
}

static HRESULT APIENTRY
D3DUmdRtWaitForSynchronizationObjectCb(
    HANDLE hDevice,
    CONST D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT Wait;

    if (Device == NULL || pData == NULL ||
        pfnWaitForSynchronizationObject == NULL ||
        pData->hContext == NULL ||
        pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Wait, sizeof(Wait));
    Wait.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Wait.ObjectCount = pData->ObjectCount;
    CopyMemory(Wait.ObjectHandleArray, pData->ObjectHandleArray,
               pData->ObjectCount * sizeof(Wait.ObjectHandleArray[0]));
    return D3DUmdRtStatusToHresult(
               pfnWaitForSynchronizationObject(&Wait));
}

static HRESULT APIENTRY
D3DUmdRtSignalSynchronizationObjectCb(
    HANDLE hDevice,
    CONST D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT Signal;

    if (Device == NULL || pData == NULL ||
        pfnSignalSynchronizationObject == NULL ||
        pData->hContext == NULL ||
        pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Signal, sizeof(Signal));
    Signal.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Signal.ObjectCount = pData->ObjectCount;
    CopyMemory(Signal.ObjectHandleArray, pData->ObjectHandleArray,
               pData->ObjectCount * sizeof(Signal.ObjectHandleArray[0]));
    Signal.Flags = pData->Flags;
    return D3DUmdRtStatusToHresult(
               pfnSignalSynchronizationObject(&Signal));
}

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN8)

static HRESULT APIENTRY
D3DUmdRtCreateSynchronizationObject2Cb(
    HANDLE hDevice,
    D3DDDICB_CREATESYNCHRONIZATIONOBJECT2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    PD3DUMDRT_SYNC_OBJECT SyncObject;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create;
    NTSTATUS Status;

    if (Device == NULL || pData == NULL ||
        pfnCreateSynchronizationObject2 == NULL)
    {
        return E_INVALIDARG;
    }

    SyncObject = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           sizeof(*SyncObject));
    if (SyncObject == NULL)
        return E_OUTOFMEMORY;

    ZeroMemory(&Create, sizeof(Create));
    Create.hDevice = Device->hDevice;
    Create.Info = pData->Info;
    Status = pfnCreateSynchronizationObject2(&Create);
    if (Status < 0)
    {
        HeapFree(GetProcessHeap(), 0, SyncObject);
        return D3DUmdRtStatusToHresult(Status);
    }
    if (Create.hSyncObject == 0)
    {
        HeapFree(GetProcessHeap(), 0, SyncObject);
        return E_FAIL;
    }

    D3DUmdRtTrackSynchronizationObject(Device, SyncObject,
                                       Create.hSyncObject);
    pData->Info = Create.Info;
    pData->hSyncObject = Create.hSyncObject;
    return S_OK;
}

static HRESULT APIENTRY
D3DUmdRtWaitForSynchronizationObject2Cb(
    HANDLE hDevice,
    CONST D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 Wait;

    if (Device == NULL || pData == NULL ||
        pfnWaitForSynchronizationObject2 == NULL ||
        pData->hContext == NULL ||
        pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Wait, sizeof(Wait));
    Wait.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Wait.ObjectCount = pData->ObjectCount;
    CopyMemory(Wait.ObjectHandleArray, pData->ObjectHandleArray,
               pData->ObjectCount * sizeof(Wait.ObjectHandleArray[0]));
    Wait.Fence.FenceValue = pData->FenceValue;
    return D3DUmdRtStatusToHresult(
               pfnWaitForSynchronizationObject2(&Wait));
}

static HRESULT APIENTRY
D3DUmdRtSignalSynchronizationObject2Cb(
    HANDLE hDevice,
    CONST D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 Signal;
    UINT Index;

    if (Device == NULL || pData == NULL ||
        pfnSignalSynchronizationObject2 == NULL ||
        pData->hContext == NULL ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED ||
        pData->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
    {
        return E_INVALIDARG;
    }
    if ((pData->Flags.EnqueueCpuEvent && pData->ObjectCount != 0) ||
        (!pData->Flags.EnqueueCpuEvent && pData->ObjectCount == 0))
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Signal, sizeof(Signal));
    Signal.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Signal.ObjectCount = pData->ObjectCount;
    CopyMemory(Signal.ObjectHandleArray, pData->ObjectHandleArray,
               pData->ObjectCount * sizeof(Signal.ObjectHandleArray[0]));
    Signal.Flags = pData->Flags;
    Signal.BroadcastContextCount = pData->BroadcastContextCount;
    for (Index = 0; Index < pData->BroadcastContextCount; ++Index)
    {
        Signal.BroadcastContext[Index] =
            (D3DKMT_HANDLE)(ULONG_PTR)pData->BroadcastContext[Index];
    }
    if (pData->Flags.EnqueueCpuEvent)
        Signal.CpuEventHandle = pData->CpuEventHandle;
    else
        Signal.Fence.FenceValue = pData->FenceValue;
    return D3DUmdRtStatusToHresult(
               pfnSignalSynchronizationObject2(&Signal));
}

#endif /* D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN8 */

#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)

static HRESULT APIENTRY
D3DUmdRtWaitForSynchronizationObjectFromCpuCb(
    HANDLE hDevice,
    CONST D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait;

    if (Device == NULL || pData == NULL ||
        pfnWaitForSynchronizationObjectFromCpu == NULL ||
        pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON ||
        pData->ObjectHandleArray == NULL ||
        pData->FenceValueArray == NULL)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Wait, sizeof(Wait));
    Wait.hDevice = Device->hDevice;
    Wait.ObjectCount = pData->ObjectCount;
    Wait.ObjectHandleArray = pData->ObjectHandleArray;
    Wait.FenceValueArray = pData->FenceValueArray;
    Wait.hAsyncEvent = pData->hAsyncEvent;
    Wait.Flags = pData->Flags;
    return D3DUmdRtStatusToHresult(
               pfnWaitForSynchronizationObjectFromCpu(&Wait));
}

static HRESULT APIENTRY
D3DUmdRtSignalSynchronizationObjectFromCpuCb(
    HANDLE hDevice,
    CONST D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal;

    if (Device == NULL || pData == NULL ||
        pfnSignalSynchronizationObjectFromCpu == NULL ||
        pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED ||
        pData->ObjectHandleArray == NULL ||
        pData->FenceValueArray == NULL)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Signal, sizeof(Signal));
    Signal.hDevice = Device->hDevice;
    Signal.ObjectCount = pData->ObjectCount;
    Signal.ObjectHandleArray = pData->ObjectHandleArray;
    Signal.FenceValueArray = pData->FenceValueArray;
    return D3DUmdRtStatusToHresult(
               pfnSignalSynchronizationObjectFromCpu(&Signal));
}

static HRESULT APIENTRY
D3DUmdRtWaitForSynchronizationObjectFromGpuCb(
    HANDLE hDevice,
    CONST D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait;

    if (Device == NULL || pData == NULL ||
        pfnWaitForSynchronizationObjectFromGpu == NULL ||
        pData->hContext == NULL ||
        pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON ||
        pData->ObjectHandleArray == NULL ||
        pData->MonitoredFenceValueArray == NULL)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Wait, sizeof(Wait));
    Wait.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Wait.ObjectCount = pData->ObjectCount;
    Wait.ObjectHandleArray = pData->ObjectHandleArray;
    Wait.MonitoredFenceValueArray = pData->MonitoredFenceValueArray;
    return D3DUmdRtStatusToHresult(
               pfnWaitForSynchronizationObjectFromGpu(&Wait));
}

static HRESULT APIENTRY
D3DUmdRtSignalSynchronizationObjectFromGpuCb(
    HANDLE hDevice,
    CONST D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU Signal;

    if (Device == NULL || pData == NULL ||
        pfnSignalSynchronizationObjectFromGpu == NULL ||
        pData->hContext == NULL ||
        pData->ObjectCount == 0 ||
        pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED ||
        pData->ObjectHandleArray == NULL ||
        pData->MonitoredFenceValueArray == NULL)
    {
        return E_INVALIDARG;
    }

    ZeroMemory(&Signal, sizeof(Signal));
    Signal.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    Signal.ObjectCount = pData->ObjectCount;
    Signal.ObjectHandleArray = pData->ObjectHandleArray;
    Signal.MonitoredFenceValueArray = pData->MonitoredFenceValueArray;
    return D3DUmdRtStatusToHresult(
               pfnSignalSynchronizationObjectFromGpu(&Signal));
}

static HRESULT APIENTRY
D3DUmdRtSignalSynchronizationObjectFromGpu2Cb(
    HANDLE hDevice,
    CONST D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 *pData)
{
    PD3DUMDRT_DEVICE Device = D3DUmdRtDevice(hDevice);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 Signal;
    D3DKMT_HANDLE BroadcastContexts[D3DDDI_MAX_BROADCAST_CONTEXT];
    UINT Index;

    if (Device == NULL || pData == NULL ||
        pfnSignalSynchronizationObjectFromGpu2 == NULL ||
        pData->BroadcastContextCount == 0 ||
        pData->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT ||
        pData->BroadcastContextArray == NULL)
    {
        return E_INVALIDARG;
    }
    if ((pData->Flags.EnqueueCpuEvent && pData->ObjectCount != 0) ||
        (!pData->Flags.EnqueueCpuEvent &&
         (pData->ObjectCount == 0 ||
          pData->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED ||
          pData->ObjectHandleArray == NULL ||
          pData->MonitoredFenceValueArray == NULL)))
    {
        return E_INVALIDARG;
    }

    for (Index = 0; Index < pData->BroadcastContextCount; ++Index)
    {
        BroadcastContexts[Index] =
            (D3DKMT_HANDLE)(ULONG_PTR)pData->BroadcastContextArray[Index];
    }

    ZeroMemory(&Signal, sizeof(Signal));
    Signal.ObjectCount = pData->ObjectCount;
    Signal.ObjectHandleArray = pData->ObjectHandleArray;
    Signal.Flags = pData->Flags;
    Signal.BroadcastContextCount = pData->BroadcastContextCount;
    Signal.BroadcastContextArray = BroadcastContexts;
    if (pData->Flags.EnqueueCpuEvent)
        Signal.CpuEventHandle = pData->CpuEventHandle;
    else
        Signal.MonitoredFenceValueArray =
            pData->MonitoredFenceValueArray;
    return D3DUmdRtStatusToHresult(
               pfnSignalSynchronizationObjectFromGpu2(&Signal));
}

#endif /* D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0 */

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
    pCallbacks->pfnSetDisplayModeCb = D3DUmdRtSetDisplayModeCb;
    pCallbacks->pfnLockCb = D3DUmdRtLockCb;
    pCallbacks->pfnUnlockCb = D3DUmdRtUnlockCb;
    pCallbacks->pfnRenderCb = D3DUmdRtRenderCb;
    pCallbacks->pfnCreateContextCb = D3DUmdRtCreateContextCb;
    pCallbacks->pfnDestroyContextCb = D3DUmdRtDestroyContextCb;
    pCallbacks->pfnCreateSynchronizationObjectCb =
        D3DUmdRtCreateSynchronizationObjectCb;
    pCallbacks->pfnDestroySynchronizationObjectCb =
        D3DUmdRtDestroySynchronizationObjectCb;
    pCallbacks->pfnWaitForSynchronizationObjectCb =
        D3DUmdRtWaitForSynchronizationObjectCb;
    pCallbacks->pfnSignalSynchronizationObjectCb =
        D3DUmdRtSignalSynchronizationObjectCb;
    pCallbacks->pfnEscapeCb = D3DUmdRtEscapeCb;
    pCallbacks->pfnPresentCb = D3DUmdRtPresentCb;
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WIN8)
    pCallbacks->pfnOfferAllocationsCb = D3DUmdRtOfferAllocationsCb;
    pCallbacks->pfnReclaimAllocationsCb = D3DUmdRtReclaimAllocationsCb;
    pCallbacks->pfnCreateSynchronizationObject2Cb =
        D3DUmdRtCreateSynchronizationObject2Cb;
    pCallbacks->pfnWaitForSynchronizationObject2Cb =
        D3DUmdRtWaitForSynchronizationObject2Cb;
    pCallbacks->pfnSignalSynchronizationObject2Cb =
        D3DUmdRtSignalSynchronizationObject2Cb;
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_0)
    pCallbacks->pfnMakeResidentCb = D3DUmdRtMakeResidentCb;
    pCallbacks->pfnEvictCb = D3DUmdRtEvictCb;
    pCallbacks->pfnCreatePagingQueueCb = D3DUmdRtCreatePagingQueueCb;
    pCallbacks->pfnDestroyPagingQueueCb = D3DUmdRtDestroyPagingQueueCb;
    pCallbacks->pfnReserveGpuVirtualAddressCb = D3DUmdRtReserveGpuVirtualAddressCb;
    pCallbacks->pfnMapGpuVirtualAddressCb = D3DUmdRtMapGpuVirtualAddressCb;
    pCallbacks->pfnFreeGpuVirtualAddressCb = D3DUmdRtFreeGpuVirtualAddressCb;
    pCallbacks->pfnUpdateGpuVirtualAddressCb =
        D3DUmdRtUpdateGpuVirtualAddressCb;
    pCallbacks->pfnInvalidateCacheCb = D3DUmdRtInvalidateCacheCb;
    pCallbacks->pfnGetResourcePresentPrivateDriverDataCb =
        D3DUmdRtGetResourcePresentPrivateDriverDataCb;
    pCallbacks->pfnCreateContextVirtualCb = D3DUmdRtCreateContextVirtualCb;
    pCallbacks->pfnSubmitCommandCb = D3DUmdRtSubmitCommandCb;
    pCallbacks->pfnLock2Cb = D3DUmdRtLock2Cb;
    pCallbacks->pfnUnlock2Cb = D3DUmdRtUnlock2Cb;
    pCallbacks->pfnDeallocate2Cb = D3DUmdRtDeallocate2Cb;
    pCallbacks->pfnWaitForSynchronizationObjectFromCpuCb =
        D3DUmdRtWaitForSynchronizationObjectFromCpuCb;
    pCallbacks->pfnSignalSynchronizationObjectFromCpuCb =
        D3DUmdRtSignalSynchronizationObjectFromCpuCb;
    pCallbacks->pfnWaitForSynchronizationObjectFromGpuCb =
        D3DUmdRtWaitForSynchronizationObjectFromGpuCb;
    pCallbacks->pfnSignalSynchronizationObjectFromGpuCb =
        D3DUmdRtSignalSynchronizationObjectFromGpuCb;
    pCallbacks->pfnSignalSynchronizationObjectFromGpu2Cb =
        D3DUmdRtSignalSynchronizationObjectFromGpu2Cb;
    pCallbacks->pfnReclaimAllocations2Cb =
        D3DUmdRtReclaimAllocations2Cb;
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_1)
    pCallbacks->pfnUpdateAllocationPropertyCb =
        D3DUmdRtUpdateAllocationPropertyCb;
    pCallbacks->pfnOfferAllocations2Cb =
        D3DUmdRtOfferAllocations2Cb;
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_2)
    pCallbacks->pfnReclaimAllocations3Cb =
        D3DUmdRtReclaimAllocations3Cb;
    pCallbacks->pfnAcquireResourceCb =
        D3DUmdRtAcquireResourceCb;
#endif
#if (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_1_3)
    pCallbacks->pfnReleaseResourceCb =
        D3DUmdRtReleaseResourceCb;
#endif

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
