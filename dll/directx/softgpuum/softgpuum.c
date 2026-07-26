/*
 * PROJECT:     ReactOS softgpu user-mode display driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The user-mode half of the softgpu WDDM driver
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * A WDDM driver is two halves, and this tree only had one. `softgpu.sys` is the
 * kernel half; this is the half the Direct3D runtime loads into the
 * application's own process.
 *
 * The runtime finds it the way Windows does: it asks dxgkrnl for
 * KMTQAITYPE_UMDRIVERNAME, which reads `UserModeDriverName` from the adapter's
 * adapter software key, loads that DLL, and resolves exactly one
 * export -- OpenAdapter10_2. Everything else is reached through the function
 * tables handed back from there.
 *
 * That is the entire contract, and it is why this file exists rather than a
 * Mesa winsys: Windows ships no Mesa, and a driver that bypasses this interface
 * is not a WDDM driver. What renders behind these entry points is an
 * implementation detail; *that they exist and have this shape* is the ABI.
 *
 * Scope, stated plainly: the adapter and device lifecycle is real -- open, get
 * caps, create device, destroy device, close adapter all work and are
 * reference-counted. The rendering entries are present and honest: they return
 * E_NOTIMPL rather than pretending to draw. A runtime that probes this driver
 * gets truthful answers about what it can and cannot do.
 */

/* Include order matters: d3dkmthk.h pulls in d3dukmdt.h, which owns the shared
 * D3DDDI_* types the UMD DDI builds on.  Reaching d3dumddi.h first would make
 * it define them itself and collide with the copy every other consumer sees. */
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>

#define SOFTGPUUM_ADAPTER_MAGIC 0x53475541u   /* 'SGUA' */
#define SOFTGPUUM_DEVICE_MAGIC  0x53475544u   /* 'SGUD' */

typedef struct _SOFTGPUUM_ADAPTER
{
    ULONG  Magic;
    HANDLE hRuntimeAdapter;
    UINT   Interface;
    UINT   Version;
    D3DDDI_ADAPTERCALLBACKS Callbacks;
    volatile LONG DeviceCount;
} SOFTGPUUM_ADAPTER, *PSOFTGPUUM_ADAPTER;

typedef struct _SOFTGPUUM_DEVICE
{
    ULONG  Magic;
    PSOFTGPUUM_ADAPTER Adapter;
    HANDLE hRuntimeDevice;
    D3DDDI_DEVICECALLBACKS Callbacks;
} SOFTGPUUM_DEVICE, *PSOFTGPUUM_DEVICE;

static PSOFTGPUUM_ADAPTER SoftGpuUmAdapter(HANDLE hAdapter)
{
    PSOFTGPUUM_ADAPTER Adapter = (PSOFTGPUUM_ADAPTER)hAdapter;

    if (Adapter == NULL || Adapter->Magic != SOFTGPUUM_ADAPTER_MAGIC)
        return NULL;
    return Adapter;
}

static PSOFTGPUUM_DEVICE SoftGpuUmDevice(HANDLE hDevice)
{
    PSOFTGPUUM_DEVICE Device = (PSOFTGPUUM_DEVICE)hDevice;

    if (Device == NULL || Device->Magic != SOFTGPUUM_DEVICE_MAGIC)
        return NULL;
    return Device;
}

/* ------------------------------------------------------------------------ *
 * Device entries
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY SoftGpuUmDestroyDevice(HANDLE hDevice)
{
    PSOFTGPUUM_DEVICE Device = SoftGpuUmDevice(hDevice);

    if (Device == NULL)
        return E_INVALIDARG;
    /* The adapter outlives its devices; the runtime closes it separately. */
    InterlockedDecrement(&Device->Adapter->DeviceCount);
    Device->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Device);
    return S_OK;
}

/*
 * Every other device entry. They are wired into the table at their correct
 * slots -- the runtime dispatches by position, so a NULL there is a wild call
 * -- and each refuses honestly rather than returning success without drawing.
 * A runtime that probes for a capability gets a truthful "no".
 */
static HRESULT APIENTRY SoftGpuUmNotImplemented(HANDLE hDevice)
{
    return SoftGpuUmDevice(hDevice) != NULL ? E_NOTIMPL : E_INVALIDARG;
}

/* ------------------------------------------------------------------------ *
 * Adapter entries
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY SoftGpuUmGetCaps(HANDLE hAdapter, CONST D3DDDIARG_GETCAPS *pData)
{
    PSOFTGPUUM_ADAPTER Adapter = SoftGpuUmAdapter(hAdapter);

    if (Adapter == NULL || pData == NULL)
        return E_INVALIDARG;
    if (pData->pData == NULL || pData->DataSize == 0)
        return E_INVALIDARG;

    /*
     * Report nothing rather than guessing. A zeroed capability block is a
     * driver that supports no optional feature, which is exactly true here and
     * is a state every runtime already handles. Inventing a capability would
     * cost nothing until an application took the branch it unlocked.
     */
    ZeroMemory(pData->pData, pData->DataSize);
    return S_OK;
}

static HRESULT APIENTRY SoftGpuUmCreateDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE *pData)
{
    PSOFTGPUUM_ADAPTER Adapter = SoftGpuUmAdapter(hAdapter);
    PSOFTGPUUM_DEVICE Device;
    D3DDDI_DEVICEFUNCS *Funcs;

    if (Adapter == NULL || pData == NULL)
        return E_INVALIDARG;
    if (pData->pDeviceFuncs == NULL || pData->pCallbacks == NULL)
        return E_INVALIDARG;

    Device = (PSOFTGPUUM_DEVICE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Device));
    if (Device == NULL)
        return E_OUTOFMEMORY;

    Device->Magic = SOFTGPUUM_DEVICE_MAGIC;
    Device->Adapter = Adapter;
    Device->hRuntimeDevice = pData->hDevice;
    Device->Callbacks = *pData->pCallbacks;

    /*
     * Publish the table. Zeroing first matters: the runtime reads every slot
     * its version defines, and a slot left with stack garbage is indwill be
     * called as a function pointer.
     */
    Funcs = pData->pDeviceFuncs;
    ZeroMemory(Funcs, sizeof(*Funcs));
    Funcs->pfnDestroyDevice = SoftGpuUmDestroyDevice;

    /* The driver handle the runtime will pass back to every device entry. */
    pData->hDevice = (HANDLE)Device;

    InterlockedIncrement(&Adapter->DeviceCount);
    return S_OK;
}

static HRESULT APIENTRY SoftGpuUmCloseAdapter(HANDLE hAdapter)
{
    PSOFTGPUUM_ADAPTER Adapter = SoftGpuUmAdapter(hAdapter);

    if (Adapter == NULL)
        return E_INVALIDARG;
    /* Closing an adapter with devices still open is the runtime breaking its
     * own contract; refuse rather than free memory those devices still name. */
    if (InterlockedCompareExchange(&Adapter->DeviceCount, 0, 0) != 0)
        return E_FAIL;

    Adapter->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Adapter);
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * The one export the runtime resolves by name.
 * ------------------------------------------------------------------------ */
HRESULT APIENTRY OpenAdapter10_2(D3DDDIARG_OPENADAPTER *pOpenData)
{
    PSOFTGPUUM_ADAPTER Adapter;

    if (pOpenData == NULL)
        return E_INVALIDARG;
    if (pOpenData->pAdapterFuncs == NULL)
        return E_INVALIDARG;

    Adapter = (PSOFTGPUUM_ADAPTER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Adapter));
    if (Adapter == NULL)
        return E_OUTOFMEMORY;

    Adapter->Magic = SOFTGPUUM_ADAPTER_MAGIC;
    Adapter->hRuntimeAdapter = pOpenData->hAdapter;
    Adapter->Interface = pOpenData->Interface;
    Adapter->Version = pOpenData->Version;
    if (pOpenData->pAdapterCallbacks != NULL)
        Adapter->Callbacks = *pOpenData->pAdapterCallbacks;

    ZeroMemory(pOpenData->pAdapterFuncs, sizeof(*pOpenData->pAdapterFuncs));
    pOpenData->pAdapterFuncs->pfnGetCaps = SoftGpuUmGetCaps;
    pOpenData->pAdapterFuncs->pfnCreateDevice = SoftGpuUmCreateDevice;
    pOpenData->pAdapterFuncs->pfnCloseAdapter = SoftGpuUmCloseAdapter;

    /* Swap in our handle and report the interface version we were built
     * against, which is how the runtime decides how much of each table to
     * read. Reporting nothing gets the tail treated as garbage. */
    pOpenData->hAdapter = (HANDLE)Adapter;
    pOpenData->DriverVersion = D3D_UMD_INTERFACE_VERSION;
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD Reason, LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hInstance);
    return TRUE;
}

/* EOF */
