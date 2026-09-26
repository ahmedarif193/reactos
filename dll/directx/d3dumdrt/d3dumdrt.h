/*
 * PROJECT:     ReactOS D3D user-mode driver runtime callbacks
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Building the D3DDDI_DEVICECALLBACKS a user-mode driver is given
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build the callback table a user-mode display driver receives at
 * pfnCreateDevice.  hAdapter and hDevice are the caller's own D3DKMT handles;
 * every callback is serviced against them, so the returned runtime handle must
 * not outlive either.
 *
 * The runtime handle is what the driver passes back as hDevice on every
 * callback -- it is deliberately not the D3DKMT device handle, so a driver
 * cannot use it to call D3DKMT directly and bypass the interface it is
 * supposed to be written against.
 */
HRESULT WINAPI
D3DUmdRtCreateDeviceCallbacks(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice,
    D3DDDI_DEVICECALLBACKS *pCallbacks,
    HANDLE *phRuntimeDevice);

/* The front end selects the allocation-info layout negotiated with its UMD.
 * The original entry point retains version 1 for legacy callers. */
#define D3DUMDRT_ALLOCATION_INFO_VERSION_1 1u
#define D3DUMDRT_ALLOCATION_INFO_VERSION_2 2u

HRESULT WINAPI
D3DUmdRtCreateDeviceCallbacksEx(
    D3DKMT_HANDLE hAdapter,
    D3DKMT_HANDLE hDevice,
    UINT AllocationInfoVersion,
    D3DDDI_DEVICECALLBACKS *pCallbacks,
    HANDLE *phRuntimeDevice);

/*
 * Release the runtime device.  Refuses with E_FAIL while resources, contexts,
 * synchronization objects, or paging queues created through the table are
 * still open: those are parented to this device, and releasing it first would
 * leave kernel objects owned by something that no longer exists.
 */
HRESULT WINAPI
D3DUmdRtDestroyDeviceCallbacks(HANDLE hRuntimeDevice);

/* Register runtime-owned sharing policy and a copied descriptor before the
 * UMD creates its resource. Driver-private data remains owned by the UMD. */
HRESULT WINAPI
D3DUmdRtRegisterResource(HANDLE hRuntimeDevice, HANDLE hRuntimeResource,
                        D3DKMT_CREATEALLOCATIONFLAGS Flags,
                        CONST VOID *RuntimeData, UINT RuntimeDataSize);

/* Either output may be NULL. An unshared resource has a zero global handle. */
HRESULT WINAPI
D3DUmdRtGetResourceHandles(HANDLE hRuntimeDevice, HANDLE hRuntimeResource,
                          D3DKMT_HANDLE *KernelResource, D3DKMT_HANDLE *GlobalShare);

/* Returns the sole allocation from a successful one-allocation CreateResource.
 * A live resource with imported, extended, or invalidated membership returns
 * E_NOTIMPL; an absent or changing resource returns E_INVALIDARG. The caller
 * must serialize resource lifetime and identity rotation with this query. */
HRESULT WINAPI
D3DUmdRtGetSingleResourceAllocation(HANDLE hRuntimeDevice, HANDLE hRuntimeResource,
                                   D3DKMT_HANDLE *Allocation);

/* Transfers an opened kernel resource to this runtime only on success. The
 * runtime handle must not already be registered. SingleAllocation is the
 * resource's only allocation, or 0 if it has several. */
HRESULT WINAPI
D3DUmdRtAdoptResource(HANDLE hRuntimeDevice, HANDLE hRuntimeResource,
                     D3DKMT_HANDLE KernelResource, D3DKMT_HANDLE GlobalShare,
                     D3DKMT_HANDLE SingleAllocation);

/* Call after UMD destruction or failed creation. This closes any remaining
 * kernel resource and releases registration, including copied metadata. */
HRESULT WINAPI
D3DUmdRtReleaseResource(HANDLE hRuntimeDevice, HANDLE hRuntimeResource);

/* Rotate 2..16 registered resource identities after the same successful UMD
 * rotation. The caller serializes resource destruction and both rotations.
 * Object keys stay fixed; slot zero receives slot one's backing. */
HRESULT WINAPI
D3DUmdRtRotateResourceIdentities(HANDLE hRuntimeDevice, CONST HANDLE *RuntimeResources,
                                UINT Count);

#ifdef __cplusplus
}
#endif

/* EOF */
