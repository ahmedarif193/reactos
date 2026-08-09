/*
 * Windows Sensor API class factory
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(sensorsapi);

typedef HRESULT (*create_instance_fn)(REFIID iid, void **out);

struct class_factory
{
    IClassFactory IClassFactory_iface;
    create_instance_fn create_instance;
};

static inline struct class_factory *factory_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct class_factory, IClassFactory_iface);
}

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!IsEqualIID(iid, &IID_IUnknown) && !IsEqualIID(iid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *out = iface;
    IClassFactory_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI class_factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct class_factory *factory = factory_from_IClassFactory(iface);

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    return factory->create_instance(iid, out);
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer,
};

static struct class_factory manager_factory = {{&class_factory_vtbl}, sensor_manager_create};
static struct class_factory collection_factory = {{&class_factory_vtbl}, sensor_collection_create};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    IClassFactory *factory;

    if (!out)
        return E_POINTER;
    *out = NULL;
    if (IsEqualCLSID(clsid, &CLSID_SensorManager))
        factory = &manager_factory.IClassFactory_iface;
    else if (IsEqualCLSID(clsid, &CLSID_SensorCollection))
        factory = &collection_factory.IClassFactory_iface;
    else
        return CLASS_E_CLASSNOTAVAILABLE;
    return IClassFactory_QueryInterface(factory, iid, out);
}
