/*
 * Windows Sensor API private declarations
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdarg.h>
#include <string.h>

#define COBJMACROS

#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <setupapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <portabledevicetypes.h>
#include <sensorsapi.h>
#include <sensors.h>
#include <reactos/drivers/sensorprovider.h>

#include <wine/debug.h>

HRESULT sensor_manager_create(REFIID iid, void **out);
HRESULT sensor_collection_create(REFIID iid, void **out);
HRESULT sensor_create(const WCHAR *device_path, const REACTOS_SENSOR_PROVIDER_INFORMATION *provider,
        ULONG channel_index, REFIID iid, void **out);
HRESULT sensor_data_report_create(const PROPERTYKEY *key, double scale,
        const REACTOS_SENSOR_READING *reading, REFIID iid, void **out);

static inline WCHAR *sensor_strdupW(const WCHAR *source)
{
    SIZE_T size;
    WCHAR *destination;

    if (!source)
        return NULL;
    size = (lstrlenW(source) + 1) * sizeof(*source);
    destination = HeapAlloc(GetProcessHeap(), 0, size);
    if (destination)
        memcpy(destination, source, size);
    return destination;
}

static inline void *sensor_heap_realloc(void *memory, SIZE_T size)
{
    if (memory)
        return HeapReAlloc(GetProcessHeap(), 0, memory, size);
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static inline HRESULT sensor_hresult_from_last_error(void)
{
    DWORD error = GetLastError();

    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}
