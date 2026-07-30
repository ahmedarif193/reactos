/*
 * PROJECT:     ReactOS D3D user-mode driver runtime callbacks
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Building the D3DDDI_DEVICECALLBACKS a user-mode driver is given
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#pragma once

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

/*
 * Release the runtime device.  Refuses with E_FAIL while resources, contexts,
 * synchronization objects, or paging queues created through the table are
 * still open: those are parented to this device, and releasing it first would
 * leave kernel objects owned by something that no longer exists.
 */
HRESULT WINAPI
D3DUmdRtDestroyDeviceCallbacks(HANDLE hRuntimeDevice);

/* EOF */
