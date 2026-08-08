/*
 * ReactOS native adapter for Wine GStreamer parser classes
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * COPYRIGHT:   Adapted from Wine dlls/winegstreamer/main.c
 *
 * This keeps Wine's private winegstreamer COM boundary while using the
 * native ReactOS DirectShow parsers on systems without a Unix GStreamer
 * backend.
 */

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "rpcproxy.h"
#include "wine/debug.h"

#include "legacy/quartz_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

const char *qzdebugstr_guid(const GUID *id)
{
    return debugstr_guid(id);
}

typedef HRESULT (*parser_create_func)(IUnknown *outer, void **out);

struct class_factory
{
    IClassFactory IClassFactory_iface;
    parser_create_func create;
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
    UNREFERENCED_PARAMETER(iface);
    return 2;
}

static ULONG WINAPI class_factory_Release(IClassFactory *iface)
{
    UNREFERENCED_PARAMETER(iface);
    return 1;
}

static HRESULT WINAPI class_factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct class_factory *factory = impl_from_IClassFactory(iface);
    IUnknown *object;
    HRESULT hr;

    if (!out)
        return E_POINTER;

    *out = NULL;
    if (FAILED(hr = factory->create(outer, (void **)&object)))
        return hr;

    hr = IUnknown_QueryInterface(object, iid, out);
    IUnknown_Release(object);
    return hr;
}

static HRESULT WINAPI class_factory_LockServer(IClassFactory *iface, BOOL lock)
{
    UNREFERENCED_PARAMETER(iface);
    UNREFERENCED_PARAMETER(lock);
    return S_OK;
}

static IClassFactoryVtbl class_factory_vtbl =
{
    class_factory_QueryInterface,
    class_factory_AddRef,
    class_factory_Release,
    class_factory_CreateInstance,
    class_factory_LockServer,
};

static struct class_factory avi_splitter_factory = {{&class_factory_vtbl}, AVISplitter_create};
static struct class_factory mpeg_splitter_factory = {{&class_factory_vtbl}, MPEGSplitter_create};
static struct class_factory wave_parser_factory = {{&class_factory_vtbl}, WAVEParser_create};

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    UNREFERENCED_PARAMETER(reserved);
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    static const GUID CLSID_wg_avi_splitter = {0x272bfbfb,0x50d0,0x4078,{0xb6,0x00,0x1e,0x95,0x9c,0x30,0x13,0x37}};
    static const GUID CLSID_wg_mpeg1_splitter = {0xa8edbf98,0x2442,0x42c5,{0x85,0xa1,0xab,0x05,0xa5,0x80,0xdf,0x53}};
    static const GUID CLSID_wg_wave_parser = {0x3f839ec7,0x5ea6,0x49e1,{0x80,0xc2,0x1e,0xa3,0x00,0xf8,0xb0,0xe0}};
    IClassFactory *factory;

    if (IsEqualGUID(clsid, &CLSID_wg_avi_splitter))
        factory = &avi_splitter_factory.IClassFactory_iface;
    else if (IsEqualGUID(clsid, &CLSID_wg_mpeg1_splitter))
        factory = &mpeg_splitter_factory.IClassFactory_iface;
    else if (IsEqualGUID(clsid, &CLSID_wg_wave_parser))
        factory = &wave_parser_factory.IClassFactory_iface;
    else
        return CLASS_E_CLASSNOTAVAILABLE;

    return IClassFactory_QueryInterface(factory, iid, out);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    /* Legacy parser objects do not expose a module object counter. */
    return S_FALSE;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources();
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources();
}

HRESULT WINAPI winegstreamer_create_wm_sync_reader(IUnknown *outer, void **out)
{
    UNREFERENCED_PARAMETER(outer);
    if (out)
        *out = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI winegstreamer_create_video_decoder(void **out)
{
    if (out)
        *out = NULL;
    return E_NOTIMPL;
}
