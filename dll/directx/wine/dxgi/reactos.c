/* ReactOS-specific DXGI integration. */

#ifdef __REACTOS__

#include "dxgi_private.h"

#include <d3dkmthk.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

HRESULT dxgi_get_wddm_adapter_desc(LUID luid, DXGI_ADAPTER_DESC3 *desc)
{
    D3DKMT_OPENADAPTERFROMLUID open_adapter = {0};
    D3DKMT_CLOSEADAPTER close_adapter;
    D3DKMT_QUERYADAPTERINFO query = {0};
    D3DKMT_QUERY_DEVICE_IDS ids = {0};
    D3DKMT_ADAPTERREGISTRYINFO registry = {0};
    D3DKMT_SEGMENTSIZEINFO segments = {0};
    D3DKMT_ADAPTERTYPE type = {0};
    HRESULT hr = E_FAIL;

    open_adapter.AdapterLuid = luid;
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromLuid(&open_adapter)))
        return E_FAIL;
    query.hAdapter = open_adapter.hAdapter;
    query.Type = KMTQAITYPE_ADAPTERTYPE;
    query.pPrivateDriverData = &type;
    query.PrivateDriverDataSize = sizeof(type);
    if (!NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
        goto done;
    query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
    query.pPrivateDriverData = &ids;
    query.PrivateDriverDataSize = sizeof(ids);
    if (!NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
        goto done;

    desc->VendorId = ids.DeviceIds.VendorID;
    desc->DeviceId = ids.DeviceIds.DeviceID;
    desc->SubSysId = ids.DeviceIds.SubVendorID | (ids.DeviceIds.SubSystemID << 16);
    desc->Revision = ids.DeviceIds.RevisionID;
    desc->Flags = type.SoftwareDevice ? DXGI_ADAPTER_FLAG3_SOFTWARE : 0;
    query.Type = KMTQAITYPE_ADAPTERREGISTRYINFO;
    query.pPrivateDriverData = &registry;
    query.PrivateDriverDataSize = sizeof(registry);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)) && registry.AdapterString[0])
        lstrcpynW(desc->Description, registry.AdapterString, ARRAY_SIZE(desc->Description));
    query.Type = KMTQAITYPE_GETSEGMENTSIZE;
    query.pPrivateDriverData = &segments;
    query.PrivateDriverDataSize = sizeof(segments);
    if (NT_SUCCESS(D3DKMTQueryAdapterInfo(&query)))
    {
        desc->DedicatedVideoMemory = segments.DedicatedVideoMemorySize;
        desc->DedicatedSystemMemory = segments.DedicatedSystemMemorySize;
        desc->SharedSystemMemory = segments.SharedSystemMemorySize;
    }
    hr = S_OK;
done:
    close_adapter.hAdapter = open_adapter.hAdapter;
    D3DKMTCloseAdapter(&close_adapter);
    return hr;
}

static unsigned int dxgi_get_wddm_adapter_priority(const D3DKMT_ADAPTERINFO *adapter)
{
    D3DKMT_QUERYADAPTERINFO query_info = {0};
    D3DKMT_ADAPTERTYPE adapter_type = {0};

    query_info.hAdapter = adapter->hAdapter;
    query_info.Type = KMTQAITYPE_ADAPTERTYPE;
    query_info.pPrivateDriverData = &adapter_type;
    query_info.PrivateDriverDataSize = sizeof(adapter_type);
    if (!NT_SUCCESS(D3DKMTQueryAdapterInfo(&query_info)))
        return 1;

    /* A software display fallback must not hide a separately started hardware
     * render adapter from clients which select the first DXGI adapter. */
    if (adapter_type.SoftwareDevice)
        return 2;
    if (adapter_type.RenderSupported)
        return 0;
    return 1;
}

/* Keep D3DKMT as the authoritative adapter set while returning complete Wine
 * DXGI adapter objects.  Preserve order within capability classes, with
 * hardware render adapters before display-only and software fallbacks. */
HRESULT dxgi_get_wddm_adapter_index(struct wined3d *wined3d, UINT wddm_adapter_idx, UINT *wined3d_adapter_idx)
{
    struct wined3d_adapter_identifier identifier = {0};
    D3DKMT_ENUMADAPTERS2 enum_adapters = {0};
    D3DKMT_ADAPTERINFO *adapters;
    unsigned int adapter_count;
    unsigned int capacity;
    unsigned int requested_idx;
    unsigned int selected_idx = ~0u;
    unsigned int priority;
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
        hr = DXGI_ERROR_NOT_FOUND;
        goto done;
    }

    if (enum_adapters.NumAdapters > capacity || wddm_adapter_idx >= enum_adapters.NumAdapters)
    {
        hr = DXGI_ERROR_NOT_FOUND;
        goto done;
    }

    requested_idx = wddm_adapter_idx;
    for (priority = 0; priority != 3; ++priority)
    {
        for (i = 0; i < enum_adapters.NumAdapters; ++i)
        {
            if (dxgi_get_wddm_adapter_priority(&adapters[i]) != priority)
                continue;
            if (requested_idx-- == 0)
            {
                selected_idx = i;
                break;
            }
        }
        if (selected_idx != ~0u)
            break;
    }

    if (selected_idx == ~0u)
    {
        hr = DXGI_ERROR_NOT_FOUND;
        goto done;
    }

    wined3d_mutex_lock();
    adapter_count = wined3d_get_adapter_count(wined3d);
    wined3d_mutex_unlock();

    hr = DXGI_ERROR_NOT_FOUND;
    for (i = 0; i < adapter_count; ++i)
    {
        if (FAILED(wined3d_adapter_get_identifier(wined3d_get_adapter(wined3d, i), 0, &identifier)))
            continue;

        if (identifier.adapter_luid.LowPart == adapters[selected_idx].AdapterLuid.LowPart && identifier.adapter_luid.HighPart == adapters[selected_idx].AdapterLuid.HighPart)
        {
            *wined3d_adapter_idx = i;
            hr = S_OK;
            break;
        }
    }

done:
    for (i = 0; i < enum_adapters.NumAdapters && i < capacity; ++i)
    {
        D3DKMT_CLOSEADAPTER close_adapter;

        if (!adapters[i].hAdapter)
            continue;
        close_adapter.hAdapter = adapters[i].hAdapter;
        D3DKMTCloseAdapter(&close_adapter);
    }
    free(adapters);
    return hr;
}

#endif /* __REACTOS__ */
