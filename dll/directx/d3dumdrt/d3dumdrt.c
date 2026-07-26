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

    if (Device == NULL || pData == NULL || pfnDestroyContext == NULL)
        return E_INVALIDARG;

    ZeroMemory(&Destroy, sizeof(Destroy));
    Destroy.hContext = (D3DKMT_HANDLE)(ULONG_PTR)pData->hContext;
    return D3DUmdRtStatusToHresult(pfnDestroyContext(&Destroy));
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

    *phRuntimeDevice = (HANDLE)Device;
    return S_OK;
}

VOID WINAPI
D3DUmdRtDestroyDeviceCallbacks(HANDLE hRuntimeDevice)
{
    PD3DUMDRT_DEVICE Device;
    PD3DUMDRT_DEVICE *Link;

    if (hRuntimeDevice == NULL || !D3DUmdRtDeviceLockReady)
        return;

    /* Unlink and free under one lock: a handle released twice must find
     * nothing to unlink the second time rather than free the memory again. */
    EnterCriticalSection(&D3DUmdRtDeviceLock);
    for (Link = &D3DUmdRtDeviceList; *Link != NULL; Link = &(*Link)->Next)
    {
        if ((HANDLE)*Link == hRuntimeDevice)
            break;
    }
    Device = *Link;
    if (Device != NULL)
        *Link = Device->Next;
    LeaveCriticalSection(&D3DUmdRtDeviceLock);

    if (Device == NULL)
        return;
    Device->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Device);
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
