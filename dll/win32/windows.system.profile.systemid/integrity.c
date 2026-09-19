/*
 * PROJECT:     ReactOS Windows Runtime
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     WindowsIntegrityPolicy activation and S-mode state
 */

#include "private.h"

struct integrity_policy
{
    IActivationFactory IActivationFactory_iface;
    IWindowsIntegrityPolicyStatics IWindowsIntegrityPolicyStatics_iface;
    LONG ref;
};

struct policy_handler
{
    struct policy_handler *next;
    __FIEventHandler_1_IInspectable *handler;
    EventRegistrationToken token;
};

static SRWLOCK handler_lock = SRWLOCK_INIT;
static struct policy_handler *handlers;
static LONGLONG next_token;

static struct integrity_policy *impl_from_factory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct integrity_policy, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct integrity_policy *impl = impl_from_factory(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IActivationFactory) || IsEqualGUID(iid, &IID_IAgileObject))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IWindowsIntegrityPolicyStatics))
        *out = &impl->IWindowsIntegrityPolicyStatics_iface;
    else
        return E_NOINTERFACE;
    IUnknown_AddRef((IUnknown *)*out);
    return S_OK;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&impl_from_factory(iface)->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return InterlockedDecrement(&impl_from_factory(iface)->ref);
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = CoTaskMemAlloc(sizeof(**iids));
    if (!*iids) return E_OUTOFMEMORY;
    **iids = IID_IWindowsIntegrityPolicyStatics;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    const WCHAR *class_name = RuntimeClass_Windows_System_Profile_WindowsIntegrityPolicy;
    if (!name) return E_POINTER;
    return WindowsCreateString(class_name, wcslen(class_name), name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return E_NOTIMPL; /* Static runtime class. */
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel,
    factory_ActivateInstance
};

DEFINE_IINSPECTABLE_(policy, IWindowsIntegrityPolicyStatics, struct integrity_policy,
                     impl_from_policy, IWindowsIntegrityPolicyStatics_iface,
                     &impl->IActivationFactory_iface)

static HRESULT WINAPI policy_disabled(IWindowsIntegrityPolicyStatics *iface, boolean *value)
{
    if (!value) return E_POINTER;
    /* ReactOS does not enforce Windows S mode or provide a mode transition. */
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI policy_add_PolicyChanged(IWindowsIntegrityPolicyStatics *iface,
                                             __FIEventHandler_1_IInspectable *handler,
                                             EventRegistrationToken *token)
{
    struct policy_handler *entry;
    if (!handler || !token) return E_INVALIDARG;
    token->value = 0;
    entry = HeapAlloc(GetProcessHeap(), 0, sizeof(*entry));
    if (!entry) return E_OUTOFMEMORY;
    entry->handler = handler;
    IUnknown_AddRef((IUnknown *)handler);
    AcquireSRWLockExclusive(&handler_lock);
    entry->token.value = ++next_token;
    entry->next = handlers;
    handlers = entry;
    *token = entry->token;
    ReleaseSRWLockExclusive(&handler_lock);
    return S_OK;
}

static HRESULT WINAPI policy_remove_PolicyChanged(IWindowsIntegrityPolicyStatics *iface,
                                                EventRegistrationToken token)
{
    struct policy_handler **cursor, *entry = NULL;
    AcquireSRWLockExclusive(&handler_lock);
    for (cursor = &handlers; *cursor; cursor = &(*cursor)->next)
    {
        if ((*cursor)->token.value != token.value) continue;
        entry = *cursor;
        *cursor = entry->next;
        break;
    }
    ReleaseSRWLockExclusive(&handler_lock);
    if (entry)
    {
        IUnknown_Release((IUnknown *)entry->handler);
        HeapFree(GetProcessHeap(), 0, entry);
    }
    return S_OK;
}

static const IWindowsIntegrityPolicyStaticsVtbl policy_vtbl =
{
    policy_QueryInterface, policy_AddRef, policy_Release,
    policy_GetIids, policy_GetRuntimeClassName, policy_GetTrustLevel,
    policy_disabled, policy_disabled, policy_disabled, policy_disabled,
    policy_add_PolicyChanged, policy_remove_PolicyChanged
};

static struct integrity_policy policy = {{&factory_vtbl}, {&policy_vtbl}, 1};
IActivationFactory *integrity_policy_factory = &policy.IActivationFactory_iface;
