/* ReactOS-specific DXGI integration. */

#include "dxgi_private.h"

#include <d3dkmthk.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

/* Keep D3DKMT as the authoritative adapter order while returning complete Wine DXGI adapter objects. */
HRESULT dxgi_get_wddm_adapter_index(struct wined3d *wined3d, UINT wddm_adapter_idx, UINT *wined3d_adapter_idx)
{
    struct wined3d_adapter_identifier identifier = {0};
    D3DKMT_ENUMADAPTERS2 enum_adapters = {0};
    D3DKMT_ADAPTERINFO *adapters;
    unsigned int adapter_count;
    unsigned int capacity;
    unsigned int i;
    NTSTATUS status;
    HRESULT hr;

    status = D3DKMTEnumAdapters2(&enum_adapters);
    if (!NT_SUCCESS(status))
    {
        WARN("D3DKMTEnumAdapters2 count query failed, status %#lx.\n", status);
        return DXGI_ERROR_NOT_FOUND;
    }

    if (wddm_adapter_idx >= enum_adapters.NumAdapters)
        return DXGI_ERROR_NOT_FOUND;

    capacity = enum_adapters.NumAdapters;
    if (!(adapters = calloc(capacity, sizeof(*adapters))))
        return E_OUTOFMEMORY;

    enum_adapters.pAdapters = adapters;
    status = D3DKMTEnumAdapters2(&enum_adapters);
    if (!NT_SUCCESS(status))
    {
        WARN("D3DKMTEnumAdapters2 enumeration failed, status %#lx.\n", status);
        free(adapters);
        return DXGI_ERROR_NOT_FOUND;
    }

    if (enum_adapters.NumAdapters > capacity || wddm_adapter_idx >= enum_adapters.NumAdapters)
    {
        free(adapters);
        return DXGI_ERROR_NOT_FOUND;
    }

    wined3d_mutex_lock();
    adapter_count = wined3d_get_adapter_count(wined3d);
    wined3d_mutex_unlock();

    hr = DXGI_ERROR_NOT_FOUND;
    for (i = 0; i < adapter_count; ++i)
    {
        if (FAILED(wined3d_adapter_get_identifier(wined3d_get_adapter(wined3d, i), 0, &identifier)))
            continue;

        if (identifier.adapter_luid.LowPart == adapters[wddm_adapter_idx].AdapterLuid.LowPart && identifier.adapter_luid.HighPart == adapters[wddm_adapter_idx].AdapterLuid.HighPart)
        {
            *wined3d_adapter_idx = i;
            hr = S_OK;
            break;
        }
    }

    free(adapters);
    return hr;
}
