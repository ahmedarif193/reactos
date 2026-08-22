/*
 * PROJECT:     ReactOS Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Modern shell compatibility entry points
 * COPYRIGHT:   Adapted from corresponding Wine shell32 and shcore implementations
 */

#include <stdarg.h>

#define COBJMACROS
#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H

#include <windef.h>
#include <winbase.h>
#include <winreg.h>
#include <winuser.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(shell);

HRESULT WINAPI SHGetStockIconInfo(SHSTOCKICONID siid, UINT uFlags, SHSTOCKICONINFO *psii)
{
    FIXME("(%d, 0x%x, %p): stub\n", siid, uFlags, psii);

    if (psii == NULL || psii->cbSize != sizeof(*psii))
        return E_INVALIDARG;

    return E_NOTIMPL;
}

HRESULT WINAPI SHCreateAssociationRegistration(REFIID riid, void **ppv)
{
    FIXME("(%s, %p): stub\n", debugstr_guid(riid), ppv);

    if (ppv == NULL)
        return E_INVALIDARG;

    *ppv = NULL;
    return E_NOTIMPL;
}

struct protocol_assoc_handler
{
    IAssocHandler IAssocHandler_iface;
    LONG ref;
    WCHAR *executable;
    WCHAR *ui_name;
};

struct protocol_assoc_enumerator
{
    IEnumAssocHandlers IEnumAssocHandlers_iface;
    LONG ref;
    IAssocHandler *handler;
    BOOL consumed;
};

struct protocol_assoc_invoker
{
    IAssocHandlerInvoker IAssocHandlerInvoker_iface;
    LONG ref;
    IAssocHandler *handler;
    IDataObject *data_object;
};

static inline struct protocol_assoc_handler *impl_from_protocol_assoc_handler(IAssocHandler *iface)
{
    return CONTAINING_RECORD(iface, struct protocol_assoc_handler, IAssocHandler_iface);
}

static inline struct protocol_assoc_enumerator *impl_from_protocol_assoc_enumerator(IEnumAssocHandlers *iface)
{
    return CONTAINING_RECORD(iface, struct protocol_assoc_enumerator, IEnumAssocHandlers_iface);
}

static inline struct protocol_assoc_invoker *impl_from_protocol_assoc_invoker(IAssocHandlerInvoker *iface)
{
    return CONTAINING_RECORD(iface, struct protocol_assoc_invoker, IAssocHandlerInvoker_iface);
}

static WCHAR *protocol_assoc_heap_strdup(const WCHAR *source)
{
    SIZE_T size = (lstrlenW(source) + 1) * sizeof(*source);
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, size);

    if (copy)
        memcpy(copy, source, size);
    return copy;
}

static HRESULT protocol_assoc_task_strdup(const WCHAR *source, WCHAR **destination)
{
    SIZE_T size;

    if (!destination)
        return E_POINTER;

    *destination = NULL;
    size = (lstrlenW(source) + 1) * sizeof(*source);
    if (!(*destination = CoTaskMemAlloc(size)))
        return E_OUTOFMEMORY;

    memcpy(*destination, source, size);
    return S_OK;
}

static HRESULT protocol_assoc_query_string(ASSOCSTR string_type, const WCHAR *protocol, WCHAR **value)
{
    DWORD length = 0;
    HRESULT hr;

    *value = NULL;
    hr = AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, string_type, protocol, L"open", NULL, &length);
    if (hr != S_FALSE && FAILED(hr))
        return hr;
    if (!length)
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);

    if (!(*value = HeapAlloc(GetProcessHeap(), 0, length * sizeof(**value))))
        return E_OUTOFMEMORY;

    hr = AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, string_type, protocol, L"open", *value, &length);
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, *value);
        *value = NULL;
    }
    return hr;
}

static HRESULT WINAPI protocol_assoc_handler_QueryInterface(IAssocHandler *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;

    *object = NULL;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IAssocHandler))
        return E_NOINTERFACE;

    *object = iface;
    IAssocHandler_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI protocol_assoc_handler_AddRef(IAssocHandler *iface)
{
    struct protocol_assoc_handler *handler = impl_from_protocol_assoc_handler(iface);
    return InterlockedIncrement(&handler->ref);
}

static ULONG WINAPI protocol_assoc_handler_Release(IAssocHandler *iface)
{
    struct protocol_assoc_handler *handler = impl_from_protocol_assoc_handler(iface);
    ULONG ref = InterlockedDecrement(&handler->ref);

    if (!ref)
    {
        HeapFree(GetProcessHeap(), 0, handler->executable);
        HeapFree(GetProcessHeap(), 0, handler->ui_name);
        HeapFree(GetProcessHeap(), 0, handler);
    }
    return ref;
}

static HRESULT WINAPI protocol_assoc_handler_GetName(IAssocHandler *iface, WCHAR **name)
{
    struct protocol_assoc_handler *handler = impl_from_protocol_assoc_handler(iface);
    return protocol_assoc_task_strdup(handler->executable, name);
}

static HRESULT WINAPI protocol_assoc_handler_GetUIName(IAssocHandler *iface, WCHAR **ui_name)
{
    struct protocol_assoc_handler *handler = impl_from_protocol_assoc_handler(iface);
    return protocol_assoc_task_strdup(handler->ui_name, ui_name);
}

static HRESULT WINAPI protocol_assoc_handler_GetIconLocation(IAssocHandler *iface, WCHAR **path, int *index)
{
    struct protocol_assoc_handler *handler = impl_from_protocol_assoc_handler(iface);
    HRESULT hr;

    if (!index)
        return E_POINTER;

    hr = protocol_assoc_task_strdup(handler->executable, path);
    if (SUCCEEDED(hr))
        *index = 0;
    return hr;
}

static HRESULT WINAPI protocol_assoc_handler_IsRecommended(IAssocHandler *iface)
{
    return S_OK;
}

static HRESULT WINAPI protocol_assoc_handler_MakeDefault(IAssocHandler *iface, const WCHAR *description)
{
    return E_ACCESSDENIED;
}

static HRESULT WINAPI protocol_assoc_handler_Invoke(IAssocHandler *iface, IDataObject *data_object)
{
    struct protocol_assoc_handler *handler = impl_from_protocol_assoc_handler(iface);
    FORMATETC format = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    SHELLEXECUTEINFOW execute_info = {sizeof(execute_info)};
    STGMEDIUM medium;
    WCHAR item[MAX_PATH];
    HDROP drop;
    HRESULT hr;

    if (!data_object)
        return E_INVALIDARG;

    hr = IDataObject_GetData(data_object, &format, &medium);
    if (FAILED(hr))
        return hr;

    drop = GlobalLock(medium.hGlobal);
    if (!drop || !DragQueryFileW(drop, 0, item, sizeof(item) / sizeof(item[0])))
        hr = HRESULT_FROM_WIN32(GetLastError());
    else
    {
        execute_info.fMask = SEE_MASK_FLAG_NO_UI;
        execute_info.lpFile = handler->executable;
        execute_info.lpParameters = item;
        execute_info.nShow = SW_SHOWNORMAL;
        hr = ShellExecuteExW(&execute_info) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
    if (drop)
        GlobalUnlock(medium.hGlobal);
    ReleaseStgMedium(&medium);
    return hr;
}

static HRESULT WINAPI protocol_assoc_invoker_QueryInterface(IAssocHandlerInvoker *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;

    *object = NULL;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IAssocHandlerInvoker))
        return E_NOINTERFACE;

    *object = iface;
    IAssocHandlerInvoker_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI protocol_assoc_invoker_AddRef(IAssocHandlerInvoker *iface)
{
    struct protocol_assoc_invoker *invoker = impl_from_protocol_assoc_invoker(iface);
    return InterlockedIncrement(&invoker->ref);
}

static ULONG WINAPI protocol_assoc_invoker_Release(IAssocHandlerInvoker *iface)
{
    struct protocol_assoc_invoker *invoker = impl_from_protocol_assoc_invoker(iface);
    ULONG ref = InterlockedDecrement(&invoker->ref);

    if (!ref)
    {
        IAssocHandler_Release(invoker->handler);
        IDataObject_Release(invoker->data_object);
        HeapFree(GetProcessHeap(), 0, invoker);
    }
    return ref;
}

static HRESULT WINAPI protocol_assoc_invoker_SupportsSelection(IAssocHandlerInvoker *iface)
{
    struct protocol_assoc_invoker *invoker = impl_from_protocol_assoc_invoker(iface);
    FORMATETC format = {CF_HDROP, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return IDataObject_QueryGetData(invoker->data_object, &format);
}

static HRESULT WINAPI protocol_assoc_invoker_Invoke(IAssocHandlerInvoker *iface)
{
    struct protocol_assoc_invoker *invoker = impl_from_protocol_assoc_invoker(iface);
    return IAssocHandler_Invoke(invoker->handler, invoker->data_object);
}

static const IAssocHandlerInvokerVtbl protocol_assoc_invoker_vtbl =
{
    protocol_assoc_invoker_QueryInterface,
    protocol_assoc_invoker_AddRef,
    protocol_assoc_invoker_Release,
    protocol_assoc_invoker_SupportsSelection,
    protocol_assoc_invoker_Invoke
};

static HRESULT WINAPI protocol_assoc_handler_CreateInvoker(IAssocHandler *iface, IDataObject *data_object, IAssocHandlerInvoker **result)
{
    struct protocol_assoc_invoker *invoker;

    if (!result)
        return E_POINTER;
    *result = NULL;
    if (!data_object)
        return E_INVALIDARG;

    if (!(invoker = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*invoker))))
        return E_OUTOFMEMORY;

    invoker->IAssocHandlerInvoker_iface.lpVtbl = &protocol_assoc_invoker_vtbl;
    invoker->ref = 1;
    invoker->handler = iface;
    invoker->data_object = data_object;
    IAssocHandler_AddRef(iface);
    IDataObject_AddRef(data_object);
    *result = &invoker->IAssocHandlerInvoker_iface;
    return S_OK;
}

static const IAssocHandlerVtbl protocol_assoc_handler_vtbl =
{
    protocol_assoc_handler_QueryInterface,
    protocol_assoc_handler_AddRef,
    protocol_assoc_handler_Release,
    protocol_assoc_handler_GetName,
    protocol_assoc_handler_GetUIName,
    protocol_assoc_handler_GetIconLocation,
    protocol_assoc_handler_IsRecommended,
    protocol_assoc_handler_MakeDefault,
    protocol_assoc_handler_Invoke,
    protocol_assoc_handler_CreateInvoker
};

static HRESULT protocol_assoc_handler_create(const WCHAR *protocol, IAssocHandler **result)
{
    struct protocol_assoc_handler *handler;
    WCHAR *friendly_name = NULL;
    WCHAR *executable = NULL;
    HRESULT hr;

    *result = NULL;
    hr = protocol_assoc_query_string(ASSOCSTR_EXECUTABLE, protocol, &executable);
    if (FAILED(hr))
        return hr;

    hr = protocol_assoc_query_string(ASSOCSTR_FRIENDLYAPPNAME, protocol, &friendly_name);
    if (FAILED(hr) && !(friendly_name = protocol_assoc_heap_strdup(PathFindFileNameW(executable))))
        hr = E_OUTOFMEMORY;
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, executable);
        return hr;
    }

    if (!(handler = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*handler))))
    {
        HeapFree(GetProcessHeap(), 0, executable);
        HeapFree(GetProcessHeap(), 0, friendly_name);
        return E_OUTOFMEMORY;
    }

    handler->IAssocHandler_iface.lpVtbl = &protocol_assoc_handler_vtbl;
    handler->ref = 1;
    handler->executable = executable;
    handler->ui_name = friendly_name;
    *result = &handler->IAssocHandler_iface;
    return S_OK;
}

static HRESULT WINAPI protocol_assoc_enumerator_QueryInterface(IEnumAssocHandlers *iface, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;

    *object = NULL;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumAssocHandlers))
        return E_NOINTERFACE;

    *object = iface;
    IEnumAssocHandlers_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI protocol_assoc_enumerator_AddRef(IEnumAssocHandlers *iface)
{
    struct protocol_assoc_enumerator *enumerator = impl_from_protocol_assoc_enumerator(iface);
    return InterlockedIncrement(&enumerator->ref);
}

static ULONG WINAPI protocol_assoc_enumerator_Release(IEnumAssocHandlers *iface)
{
    struct protocol_assoc_enumerator *enumerator = impl_from_protocol_assoc_enumerator(iface);
    ULONG ref = InterlockedDecrement(&enumerator->ref);

    if (!ref)
    {
        IAssocHandler_Release(enumerator->handler);
        HeapFree(GetProcessHeap(), 0, enumerator);
    }
    return ref;
}

static HRESULT WINAPI protocol_assoc_enumerator_Next(IEnumAssocHandlers *iface, ULONG count, IAssocHandler **handlers, ULONG *fetched)
{
    struct protocol_assoc_enumerator *enumerator = impl_from_protocol_assoc_enumerator(iface);

    if (!handlers || (!fetched && count != 1))
        return E_POINTER;
    if (fetched)
        *fetched = 0;
    if (!count || enumerator->consumed)
        return S_FALSE;

    handlers[0] = enumerator->handler;
    IAssocHandler_AddRef(handlers[0]);
    enumerator->consumed = TRUE;
    if (fetched)
        *fetched = 1;
    return count == 1 ? S_OK : S_FALSE;
}

static const IEnumAssocHandlersVtbl protocol_assoc_enumerator_vtbl =
{
    protocol_assoc_enumerator_QueryInterface,
    protocol_assoc_enumerator_AddRef,
    protocol_assoc_enumerator_Release,
    protocol_assoc_enumerator_Next
};

HRESULT WINAPI SHAssocEnumHandlersForProtocolByApplication(const WCHAR *protocol, REFIID riid, void **handlers)
{
    struct protocol_assoc_enumerator *enumerator;
    IAssocHandler *handler;
    HRESULT hr;

    TRACE("(%s, %s, %p)\n", debugstr_w(protocol), debugstr_guid(riid), handlers);

    if (!handlers)
        return E_POINTER;
    *handlers = NULL;
    if (!protocol || !riid)
        return E_INVALIDARG;

    hr = protocol_assoc_handler_create(protocol, &handler);
    if (FAILED(hr))
        return hr;

    if (!(enumerator = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*enumerator))))
    {
        IAssocHandler_Release(handler);
        return E_OUTOFMEMORY;
    }

    enumerator->IEnumAssocHandlers_iface.lpVtbl = &protocol_assoc_enumerator_vtbl;
    enumerator->ref = 1;
    enumerator->handler = handler;
    hr = IEnumAssocHandlers_QueryInterface(&enumerator->IEnumAssocHandlers_iface, riid, handlers);
    IEnumAssocHandlers_Release(&enumerator->IEnumAssocHandlers_iface);
    return hr;
}

HRESULT WINAPI SHGetPropertyStoreForWindow(HWND hwnd, REFIID riid, void **ppv)
{
    FIXME("(%p, %s, %p): stub\n", hwnd, debugstr_guid(riid), ppv);

    if (ppv == NULL)
        return E_INVALIDARG;

    *ppv = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI SHQueryUserNotificationState(QUERY_USER_NOTIFICATION_STATE *pquns)
{
    TRACE("(%p)\n", pquns);

    if (pquns == NULL)
        return E_INVALIDARG;

    *pquns = QUNS_ACCEPTS_NOTIFICATIONS;
    return S_OK;
}
