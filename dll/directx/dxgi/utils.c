/*
 * PROJECT:     ReactOS DXGI Runtime
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shared utility functions for DXGI objects
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#include "dxgi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

/*
 * DxgiObject_Init
 *
 * Initialise the base DxgiObject.
 */
void DxgiObject_Init(DxgiObject *obj)
{
    obj->PrivateDataHead = NULL;
}

/*
 * DxgiObject_Destroy
 *
 * Free all private data attached to a DxgiObject.
 */
void DxgiObject_Destroy(DxgiObject *obj)
{
    DXGI_PRIVATE_DATA_ENTRY *entry = obj->PrivateDataHead;
    while (entry)
    {
        DXGI_PRIVATE_DATA_ENTRY *next = entry->pNext;
        if (entry->pInterface)
            IUnknown_Release(entry->pInterface);
        if (entry->pData)
            HeapFree(GetProcessHeap(), 0, entry->pData);
        HeapFree(GetProcessHeap(), 0, entry);
        entry = next;
    }
    obj->PrivateDataHead = NULL;
}

static DXGI_PRIVATE_DATA_ENTRY *FindEntry(DxgiObject *obj, REFGUID Name,
                                           DXGI_PRIVATE_DATA_ENTRY **previous)
{
    DXGI_PRIVATE_DATA_ENTRY *entry, *prev = NULL;

    for (entry = obj->PrivateDataHead; entry; entry = entry->pNext)
    {
        if (IsEqualGUID(&entry->Guid, Name))
        {
            if (previous)
                *previous = prev;
            return entry;
        }

        prev = entry;
    }

    if (previous)
        *previous = NULL;

    return NULL;
}

static DXGI_PRIVATE_DATA_ENTRY *CreateEntry(DxgiObject *obj, REFGUID Name)
{
    DXGI_PRIVATE_DATA_ENTRY *entry;

    entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry));
    if (!entry)
        return NULL;

    entry->Guid = *Name;
    entry->pNext = obj->PrivateDataHead;
    obj->PrivateDataHead = entry;
    return entry;
}

/*
 * Free the contents of a private data entry (but not the entry itself).
 */
static void ClearEntry(DXGI_PRIVATE_DATA_ENTRY *entry)
{
    if (entry->pInterface)
    {
        IUnknown_Release(entry->pInterface);
        entry->pInterface = NULL;
    }
    if (entry->pData)
    {
        HeapFree(GetProcessHeap(), 0, entry->pData);
        entry->pData = NULL;
    }
    entry->DataSize = 0;
}

static void RemoveEntry(DxgiObject *obj, DXGI_PRIVATE_DATA_ENTRY *entry,
                         DXGI_PRIVATE_DATA_ENTRY *previous)
{
    if (previous)
        previous->pNext = entry->pNext;
    else
        obj->PrivateDataHead = entry->pNext;

    ClearEntry(entry);
    HeapFree(GetProcessHeap(), 0, entry);
}

HRESULT DxgiObject_SetPrivateData(DxgiObject *obj, REFGUID Name,
                                   UINT DataSize, const void *pData)
{
    DXGI_PRIVATE_DATA_ENTRY *entry, *previous;
    void *data;

    if (!obj || !Name)
        return DXGI_ERROR_INVALID_CALL;

    if (DataSize && !pData)
        return DXGI_ERROR_INVALID_CALL;

    entry = FindEntry(obj, Name, &previous);

    if (DataSize == 0)
    {
        if (entry)
            RemoveEntry(obj, entry, previous);
        return S_OK;
    }

    data = HeapAlloc(GetProcessHeap(), 0, DataSize);
    if (!data)
        return E_OUTOFMEMORY;

    memcpy(data, pData, DataSize);

    if (!entry)
    {
        entry = CreateEntry(obj, Name);
        if (!entry)
        {
            HeapFree(GetProcessHeap(), 0, data);
            return E_OUTOFMEMORY;
        }
    }

    ClearEntry(entry);
    entry->pData = data;
    entry->DataSize = DataSize;
    return S_OK;
}

HRESULT DxgiObject_SetPrivateDataInterface(DxgiObject *obj, REFGUID Name,
                                            const IUnknown *pUnknown)
{
    DXGI_PRIVATE_DATA_ENTRY *entry, *previous;
    IUnknown *unknown;

    if (!obj || !Name)
        return DXGI_ERROR_INVALID_CALL;

    entry = FindEntry(obj, Name, &previous);

    if (!pUnknown)
    {
        if (entry)
            RemoveEntry(obj, entry, previous);
        return S_OK;
    }

    unknown = (IUnknown *)pUnknown;
    IUnknown_AddRef(unknown);

    if (!entry)
    {
        entry = CreateEntry(obj, Name);
        if (!entry)
        {
            IUnknown_Release(unknown);
            return E_OUTOFMEMORY;
        }
    }

    ClearEntry(entry);
    entry->pInterface = unknown;
    entry->DataSize = sizeof(IUnknown *);
    return S_OK;
}

HRESULT DxgiObject_GetPrivateData(DxgiObject *obj, REFGUID Name,
                                   UINT *pDataSize, void *pData)
{
    DXGI_PRIVATE_DATA_ENTRY *entry;
    UINT required;

    if (!obj || !Name || !pDataSize)
        return DXGI_ERROR_INVALID_CALL;

    entry = FindEntry(obj, Name, NULL);
    if (!entry)
    {
        *pDataSize = 0;
        return DXGI_ERROR_NOT_FOUND;
    }

    if (entry->pInterface)
        required = sizeof(IUnknown *);
    else
        required = entry->DataSize;

    if (!pData)
    {
        *pDataSize = required;
        return S_OK;
    }

    if (*pDataSize < required)
    {
        *pDataSize = required;
        return DXGI_ERROR_MORE_DATA;
    }

    *pDataSize = required;

    if (entry->pInterface)
    {
        IUnknown_AddRef(entry->pInterface);
        *(IUnknown **)pData = entry->pInterface;
    }
    else
    {
        memcpy(pData, entry->pData, required);
    }

    return S_OK;
}
