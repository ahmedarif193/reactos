/*
 * PROJECT:     ReactOS Windows Runtime
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     TaskbarManager capability discovery for unpackaged applications
 */
#include "private.h"

struct taskbar_factory
{
    IActivationFactory IActivationFactory_iface;
    ITaskbarManagerStatics ITaskbarManagerStatics_iface;
    LONG ref;
};
struct taskbar_manager
{
    ITaskbarManager ITaskbarManager_iface;
    LONG ref;
};

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct taskbar_factory *impl = CONTAINING_RECORD(iface, struct taskbar_factory, IActivationFactory_iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IActivationFactory) || IsEqualGUID(iid, &IID_IAgileObject))
        *out = iface;
    else if (IsEqualGUID(iid, &IID_ITaskbarManagerStatics))
        *out = &impl->ITaskbarManagerStatics_iface;
    else return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}
static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    struct taskbar_factory *impl = CONTAINING_RECORD(iface, struct taskbar_factory, IActivationFactory_iface);
    return InterlockedIncrement(&impl->ref);
}
static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    struct taskbar_factory *impl = CONTAINING_RECORD(iface, struct taskbar_factory, IActivationFactory_iface);
    return InterlockedDecrement(&impl->ref);
}
static HRESULT get_iids(REFIID iid, ULONG *count, IID **ids)
{
    if (!count || !ids) return E_POINTER;
    *count = 0;
    *ids = CoTaskMemAlloc(sizeof(**ids));
    if (!*ids) return E_OUTOFMEMORY;
    **ids = *iid;
    *count = 1;
    return S_OK;
}
static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **ids)
{ return get_iids(&IID_ITaskbarManagerStatics, count, ids); }
static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    const WCHAR *value = RuntimeClass_Windows_UI_Shell_TaskbarManager;
    if (!name) return E_POINTER;
    return WindowsCreateString(value, wcslen(value), name);
}
static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **out)
{ if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, factory_GetIids,
    factory_GetRuntimeClassName, factory_GetTrustLevel, factory_ActivateInstance
};

static HRESULT WINAPI manager_QueryInterface(ITaskbarManager *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_IAgileObject) && !IsEqualGUID(iid, &IID_ITaskbarManager)) return E_NOINTERFACE;
    *out = iface;
    ITaskbarManager_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI manager_AddRef(ITaskbarManager *iface)
{
    struct taskbar_manager *impl = CONTAINING_RECORD(iface, struct taskbar_manager, ITaskbarManager_iface);
    return InterlockedIncrement(&impl->ref);
}
static ULONG WINAPI manager_Release(ITaskbarManager *iface)
{
    struct taskbar_manager *impl = CONTAINING_RECORD(iface, struct taskbar_manager, ITaskbarManager_iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref) HeapFree(GetProcessHeap(), 0, impl);
    return ref;
}
static HRESULT WINAPI manager_GetIids(ITaskbarManager *iface, ULONG *count, IID **ids)
{ return get_iids(&IID_ITaskbarManager, count, ids); }
static HRESULT WINAPI manager_GetRuntimeClassName(ITaskbarManager *iface, HSTRING *name)
{ return factory_GetRuntimeClassName(NULL, name); }
static HRESULT WINAPI manager_GetTrustLevel(ITaskbarManager *iface, TrustLevel *level)
{ return factory_GetTrustLevel(NULL, level); }
static HRESULT WINAPI manager_disabled(ITaskbarManager *iface, boolean *value)
{
    if (!value) return E_POINTER;
    /* No packaged-app pinning service is available in the ReactOS shell. */
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI manager_async(ITaskbarManager *iface, __FIAsyncOperation_1_boolean **operation)
{
    if (!operation) return E_POINTER;
    *operation = NULL;
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}
static HRESULT WINAPI manager_entry_async(ITaskbarManager *iface,
        __x_ABI_CWindows_CApplicationModel_CCore_CIAppListEntry *entry, __FIAsyncOperation_1_boolean **operation)
{
    if (!operation) return E_POINTER;
    *operation = NULL;
    if (!entry) return E_INVALIDARG;
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}
static const ITaskbarManagerVtbl manager_vtbl =
{
    manager_QueryInterface, manager_AddRef, manager_Release, manager_GetIids,
    manager_GetRuntimeClassName, manager_GetTrustLevel,
    manager_disabled, manager_disabled, manager_async, manager_entry_async,
    manager_async, manager_entry_async
};

DEFINE_IINSPECTABLE(statics, ITaskbarManagerStatics, struct taskbar_factory, IActivationFactory_iface)
static HRESULT WINAPI statics_GetDefault(ITaskbarManagerStatics *iface, ITaskbarManager **out)
{
    struct taskbar_manager *manager;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(manager = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*manager)))) return E_OUTOFMEMORY;
    manager->ITaskbarManager_iface.lpVtbl = &manager_vtbl;
    manager->ref = 1;
    *out = &manager->ITaskbarManager_iface;
    return S_OK;
}
static const ITaskbarManagerStaticsVtbl statics_vtbl =
{
    statics_QueryInterface, statics_AddRef, statics_Release, statics_GetIids,
    statics_GetRuntimeClassName, statics_GetTrustLevel, statics_GetDefault
};
static struct taskbar_factory factory = {{&factory_vtbl}, {&statics_vtbl}, 1};
IActivationFactory *taskbarmanager_factory = &factory.IActivationFactory_iface;
