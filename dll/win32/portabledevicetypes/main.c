/*
 * Portable Device Types class factory
 *
 * Copyright 2026 Ahmed Arif
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(portabledev);

typedef HRESULT (*create_instance_fn)(REFIID iid, void **out);

struct class_factory
{
    IClassFactory IClassFactory_iface;
    create_instance_fn create_instance;
};

static inline struct class_factory *impl_from_IClassFactory(IClassFactory *iface)
{
    return CONTAINING_RECORD(iface, struct class_factory, IClassFactory_iface);
}

static HRESULT WINAPI class_factory_QueryInterface(IClassFactory *iface, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;

    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IClassFactory))
    {
        *out = iface;
        IClassFactory_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
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
    struct class_factory *factory = impl_from_IClassFactory(iface);

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

static struct class_factory values_factory = {{&class_factory_vtbl}, portable_device_values_create};
static struct class_factory key_collection_factory = {{&class_factory_vtbl}, portable_device_key_collection_create};
static struct class_factory propvariant_collection_factory = {{&class_factory_vtbl}, portable_device_propvariant_collection_create};
static struct class_factory values_collection_factory = {{&class_factory_vtbl}, portable_device_values_collection_create};

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

    if (IsEqualCLSID(clsid, &CLSID_PortableDeviceValues))
        factory = &values_factory.IClassFactory_iface;
    else if (IsEqualCLSID(clsid, &CLSID_PortableDeviceKeyCollection))
        factory = &key_collection_factory.IClassFactory_iface;
    else if (IsEqualCLSID(clsid, &CLSID_PortableDevicePropVariantCollection))
        factory = &propvariant_collection_factory.IClassFactory_iface;
    else if (IsEqualCLSID(clsid, &CLSID_PortableDeviceValuesCollection))
        factory = &values_collection_factory.IClassFactory_iface;
    else
        return CLASS_E_CLASSNOTAVAILABLE;

    return IClassFactory_QueryInterface(factory, iid, out);
}
