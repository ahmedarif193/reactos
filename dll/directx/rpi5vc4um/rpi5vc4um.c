/*
 * PROJECT:     ReactOS Raspberry Pi 5 VideoCore VII user-mode display driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     The user-mode half of the rpi5vc4 WDDM driver
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * A WDDM driver is two halves.  `rpi5vc4.sys` is the kernel half and until now
 * there was no other one, which means the D3D runtime could find the adapter,
 * load nothing, and render nothing -- the kernel half can be entirely correct
 * and the stack still does not draw.
 *
 * The runtime finds this DLL the way Windows does: it asks dxgkrnl for
 * KMTQAITYPE_UMDRIVERNAME, which reads `UserModeDriverName` from the adapter's
 * software key (rpi5vc4_reg.inf sets it), loads that DLL, and resolves exactly
 * one export -- OpenAdapter10_2.  Everything else is reached through the
 * function tables handed back from there.
 *
 * Scope, stated plainly.  The adapter and device lifecycle is real: open, get
 * caps, create device, destroy device and close adapter all work and are
 * reference-counted, so a runtime can bring this driver up and tear it down
 * correctly.  Rendering entries return E_NOTIMPL rather than pretending to
 * draw.  That is deliberate: the VideoCore VII command paths live in the kernel
 * half (rpi5vc4_v3d.c) and are not yet reachable from here, and a driver that
 * claimed to rasterise while doing nothing would fail somewhere far away from
 * the cause.  A runtime probing this driver gets truthful answers.
 */

/* Include order matters: d3dkmthk.h pulls in d3dukmdt.h, which owns the shared
 * D3DDDI_* types the UMD DDI builds on.  Reaching d3dumddi.h first would make
 * it define them itself and collide with the copy every other consumer sees. */
#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windows.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>

#define RPI5VC4UM_ADAPTER_MAGIC 0x56435541u   /* 'VCUA' */
#define RPI5VC4UM_DEVICE_MAGIC  0x56435544u   /* 'VCUD' */

typedef struct _RPI5VC4UM_ADAPTER
{
    ULONG  Magic;
    HANDLE hRuntimeAdapter;
    UINT   Interface;
    UINT   Version;
    D3DDDI_ADAPTERCALLBACKS Callbacks;
    volatile LONG DeviceCount;
} RPI5VC4UM_ADAPTER, *PRPI5VC4UM_ADAPTER;

typedef struct _RPI5VC4UM_DEVICE
{
    ULONG  Magic;
    PRPI5VC4UM_ADAPTER Adapter;
    HANDLE hRuntimeDevice;
    D3DDDI_DEVICECALLBACKS Callbacks;
} RPI5VC4UM_DEVICE, *PRPI5VC4UM_DEVICE;

static PRPI5VC4UM_ADAPTER Rpi5Vc4UmAdapter(HANDLE hAdapter)
{
    PRPI5VC4UM_ADAPTER Adapter = (PRPI5VC4UM_ADAPTER)hAdapter;

    if (Adapter == NULL || Adapter->Magic != RPI5VC4UM_ADAPTER_MAGIC)
        return NULL;
    return Adapter;
}

static PRPI5VC4UM_DEVICE Rpi5Vc4UmDevice(HANDLE hDevice)
{
    PRPI5VC4UM_DEVICE Device = (PRPI5VC4UM_DEVICE)hDevice;

    if (Device == NULL || Device->Magic != RPI5VC4UM_DEVICE_MAGIC)
        return NULL;
    return Device;
}

/* ------------------------------------------------------------------------ *
 * Device entries
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY Rpi5Vc4UmDestroyDevice(HANDLE hDevice)
{
    PRPI5VC4UM_DEVICE Device = Rpi5Vc4UmDevice(hDevice);

    if (Device == NULL)
        return E_INVALIDARG;
    /* The adapter outlives its devices; the runtime closes it separately. */
    InterlockedDecrement(&Device->Adapter->DeviceCount);
    Device->Magic = 0;
    HeapFree(GetProcessHeap(), 0, Device);
    return S_OK;
}

/* ------------------------------------------------------------------------ *
 * Adapter entries
 * ------------------------------------------------------------------------ */

static HRESULT APIENTRY Rpi5Vc4UmGetCaps(HANDLE hAdapter, CONST D3DDDIARG_GETCAPS *pData)
{
    PRPI5VC4UM_ADAPTER Adapter = Rpi5Vc4UmAdapter(hAdapter);

    if (Adapter == NULL || pData == NULL)
        return E_INVALIDARG;
    if (pData->pData == NULL || pData->DataSize == 0)
        return E_INVALIDARG;

    /*
     * Report nothing rather than guessing.  A zeroed capability block is a
     * driver that supports no optional feature, which is exactly true while
     * rendering is unimplemented and is a state every runtime already handles.
     * Inventing a capability costs nothing until an application takes the
     * branch it unlocks, and then it fails far from here.
     */
    ZeroMemory(pData->pData, pData->DataSize);
    return S_OK;
}

static HRESULT APIENTRY Rpi5Vc4UmCreateDevice(HANDLE hAdapter, D3DDDIARG_CREATEDEVICE *pData)
{
    PRPI5VC4UM_ADAPTER Adapter = Rpi5Vc4UmAdapter(hAdapter);
    PRPI5VC4UM_DEVICE Device;
    D3DDDI_DEVICEFUNCS *Funcs;

    if (Adapter == NULL || pData == NULL)
        return E_INVALIDARG;
    if (pData->pDeviceFuncs == NULL || pData->pCallbacks == NULL)
        return E_INVALIDARG;

    Device = (PRPI5VC4UM_DEVICE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Device));
    if (Device == NULL)
        return E_OUTOFMEMORY;

    Device->Magic = RPI5VC4UM_DEVICE_MAGIC;
    Device->Adapter = Adapter;
    Device->hRuntimeDevice = pData->hDevice;
    Device->Callbacks = *pData->pCallbacks;

    /*
     * Publish the table.  Zeroing first matters: the runtime reads every slot
     * its version defines, and a slot left holding stack garbage is called as
     * a function pointer.
     */
    Funcs = pData->pDeviceFuncs;
    ZeroMemory(Funcs, sizeof(*Funcs));
    Funcs->pfnDestroyDevice = Rpi5Vc4UmDestroyDevice;

    /* The driver handle the runtime passes back to every device entry. */
    pData->hDevice = (HANDLE)Device;

    InterlockedIncrement(&Adapter->DeviceCount);
    return S_OK;
}

static HRESULT APIENTRY Rpi5Vc4UmCloseAdapter(HANDLE hAdapter)
{
    PRPI5VC4UM_ADAPTER Adapter = Rpi5Vc4UmAdapter(hAdapter);

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
    PRPI5VC4UM_ADAPTER Adapter;

    if (pOpenData == NULL)
        return E_INVALIDARG;
    if (pOpenData->pAdapterFuncs == NULL)
        return E_INVALIDARG;

    Adapter = (PRPI5VC4UM_ADAPTER)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Adapter));
    if (Adapter == NULL)
        return E_OUTOFMEMORY;

    Adapter->Magic = RPI5VC4UM_ADAPTER_MAGIC;
    Adapter->hRuntimeAdapter = pOpenData->hAdapter;
    Adapter->Interface = pOpenData->Interface;
    Adapter->Version = pOpenData->Version;
    if (pOpenData->pAdapterCallbacks != NULL)
        Adapter->Callbacks = *pOpenData->pAdapterCallbacks;

    ZeroMemory(pOpenData->pAdapterFuncs, sizeof(*pOpenData->pAdapterFuncs));
    pOpenData->pAdapterFuncs->pfnGetCaps = Rpi5Vc4UmGetCaps;
    pOpenData->pAdapterFuncs->pfnCreateDevice = Rpi5Vc4UmCreateDevice;
    pOpenData->pAdapterFuncs->pfnCloseAdapter = Rpi5Vc4UmCloseAdapter;

    /* Swap in our handle and report the interface version this driver was
     * built against, which is how the runtime decides how much of each table
     * to read.  Reporting nothing gets the tail treated as garbage. */
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
