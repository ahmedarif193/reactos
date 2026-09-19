/*
 * Application association registration queries
 *
 * Copyright 2002 Jon Griffiths
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */
#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "objbase.h"
#include "shlguid.h"
#include "shlwapi.h"
#include "shobjidl.h"
#include "shell32_main.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

typedef struct
{
  IApplicationAssociationRegistration IApplicationAssociationRegistration_iface;
  LONG ref;
} IApplicationAssociationRegistrationImpl;


static inline IApplicationAssociationRegistrationImpl *impl_from_IApplicationAssociationRegistration(IApplicationAssociationRegistration *iface)
{
  return CONTAINING_RECORD(iface, IApplicationAssociationRegistrationImpl, IApplicationAssociationRegistration_iface);
}

static HRESULT WINAPI ApplicationAssociationRegistration_QueryInterface(
                        IApplicationAssociationRegistration* iface, REFIID riid, LPVOID *ppv)
{
    IApplicationAssociationRegistrationImpl *This = impl_from_IApplicationAssociationRegistration(iface);

    TRACE("(%p, %s, %p)\n",This, debugstr_guid(riid), ppv);

    if (ppv == NULL)
        return E_POINTER;

    if (IsEqualGUID(&IID_IUnknown, riid) ||
        IsEqualGUID(&IID_IApplicationAssociationRegistration, riid)) {
        *ppv = &This->IApplicationAssociationRegistration_iface;
        IUnknown_AddRef((IUnknown*)*ppv);
        TRACE("returning IApplicationAssociationRegistration: %p\n", *ppv);
        return S_OK;
    }

    *ppv = NULL;
    FIXME("(%p)->(%s %p) interface not supported\n", This, debugstr_guid(riid), ppv);
    return E_NOINTERFACE;
}

static ULONG WINAPI ApplicationAssociationRegistration_AddRef(IApplicationAssociationRegistration *iface)
{
    IApplicationAssociationRegistrationImpl *This = impl_from_IApplicationAssociationRegistration(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);
    return ref;
}

static ULONG WINAPI ApplicationAssociationRegistration_Release(IApplicationAssociationRegistration *iface)
{
    IApplicationAssociationRegistrationImpl *This = impl_from_IApplicationAssociationRegistration(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if (!ref) {
        SHFree(This);
    }
    return ref;
}

static HRESULT read_association(HKEY root, const WCHAR *key, const WCHAR *name, WCHAR **value)
{
    DWORD size = 0;
    LONG ret;
    *value = NULL;
    ret = RegGetValueW(root, key, name, RRF_RT_REG_SZ, NULL, NULL, &size);
    if (ret || size < sizeof(WCHAR)) return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    if (!(*value = CoTaskMemAlloc(size))) return E_OUTOFMEMORY;
    ret = RegGetValueW(root, key, name, RRF_RT_REG_SZ, NULL, *value, &size);
    if (ret || !**value)
    {
        CoTaskMemFree(*value);
        *value = NULL;
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    }
    return S_OK;
}

static WCHAR *association_key(const WCHAR *prefix, const WCHAR *query, const WCHAR *suffix)
{
    SIZE_T length = wcslen(prefix) + wcslen(query) + wcslen(suffix) + 1;
    WCHAR *key = CoTaskMemAlloc(length * sizeof(WCHAR));
    if (key)
    {
        wcscpy(key, prefix);
        wcscat(key, query);
        wcscat(key, suffix);
    }
    return key;
}

static HRESULT WINAPI ApplicationAssociationRegistration_QueryCurrentDefault(IApplicationAssociationRegistration *iface,
        LPCWSTR query, ASSOCIATIONTYPE type, ASSOCIATIONLEVEL level, LPWSTR *association)
{
    const WCHAR *prefix;
    WCHAR *key, *extension;
    HKEY root, classes;
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    LONG ret;
    DWORD size;

    if (!association) return E_INVALIDARG;
    *association = NULL;
    if (!query || level < AL_MACHINE || level > AL_EFFECTIVE || type < AT_FILEEXTENSION || type > AT_MIMETYPE)
        return E_INVALIDARG;
    if ((type == AT_URLPROTOCOL || type == AT_FILEEXTENSION) && !*query) return E_INVALIDARG;
    if (type == AT_FILEEXTENSION && *query != '.') return E_INVALIDARG;

    if (level != AL_MACHINE && type != AT_STARTMENUCLIENT)
    {
        if (type == AT_FILEEXTENSION)
            prefix = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\";
        else if (type == AT_URLPROTOCOL)
            prefix = L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\";
        else
            prefix = L"Software\\Microsoft\\Windows\\Shell\\Associations\\MIMEAssociations\\";
        if (!(key = association_key(prefix, query, L"\\UserChoice"))) return E_OUTOFMEMORY;
        hr = read_association(HKEY_CURRENT_USER, key, L"ProgId", association);
        CoTaskMemFree(key);
        if (SUCCEEDED(hr) || hr == E_OUTOFMEMORY) return hr;
    }

    if (type == AT_STARTMENUCLIENT)
    {
        if (!(key = association_key(L"Software\\Clients\\", query, L""))) return E_OUTOFMEMORY;
        if (level != AL_MACHINE) hr = read_association(HKEY_CURRENT_USER, key, NULL, association);
        if (FAILED(hr) && hr != E_OUTOFMEMORY && level != AL_USER)
            hr = read_association(HKEY_LOCAL_MACHINE, key, NULL, association);
        CoTaskMemFree(key);
        return hr;
    }

    root = level == AL_MACHINE ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    if (level == AL_EFFECTIVE) classes = HKEY_CLASSES_ROOT;
    else if (RegOpenKeyExW(root, L"Software\\Classes", 0, KEY_READ, &classes)) return hr;

    if (type == AT_FILEEXTENSION)
        hr = read_association(classes, query, NULL, association);
    else if (type == AT_URLPROTOCOL)
    {
        /* Legacy protocol handlers use the protocol name itself as their ProgID. */
        size = 0;
        ret = RegGetValueW(classes, query, L"URL Protocol", RRF_RT_REG_SZ, NULL, NULL, &size);
        if (!ret)
        {
            size = (wcslen(query) + 1) * sizeof(WCHAR);
            if ((*association = CoTaskMemAlloc(size))) { memcpy(*association, query, size); hr = S_OK; }
            else hr = E_OUTOFMEMORY;
        }
    }
    else if (type == AT_MIMETYPE)
    {
        if (!(key = association_key(L"MIME\\Database\\Content Type\\", query, L""))) hr = E_OUTOFMEMORY;
        else
        {
            hr = read_association(classes, key, L"Extension", &extension);
            CoTaskMemFree(key);
            if (SUCCEEDED(hr))
            {
                hr = ApplicationAssociationRegistration_QueryCurrentDefault(iface, extension, AT_FILEEXTENSION, level, association);
                CoTaskMemFree(extension);
            }
        }
    }
    if (classes != HKEY_CLASSES_ROOT) RegCloseKey(classes);
    return hr;
}

static HRESULT open_app_capabilities(LPCWSTR appname, HKEY *capabilities)
{
    const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    WCHAR *path;
    HRESULT hr;
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(roots); ++i)
    {
        hr = read_association(roots[i], L"Software\\RegisteredApplications", appname, &path);
        if (hr == E_OUTOFMEMORY) return hr;
        if (FAILED(hr)) continue;
        hr = HRESULT_FROM_WIN32(RegOpenKeyExW(roots[i], path, 0, KEY_READ, capabilities));
        CoTaskMemFree(path);
        return hr;
    }
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}

static HRESULT WINAPI ApplicationAssociationRegistration_QueryAppIsDefault(IApplicationAssociationRegistration *iface,
        LPCWSTR query, ASSOCIATIONTYPE type, ASSOCIATIONLEVEL level, LPCWSTR appname, BOOL *is_default)
{
    static const WCHAR *subkeys[] = {L"FileAssociations", L"UrlAssociations", L"Startmenu", L"MimeAssociations"};
    WCHAR *current, *registered;
    HKEY capabilities;
    HRESULT hr;
    if (!is_default) return E_INVALIDARG;
    *is_default = FALSE;
    if (!query || !appname || !*appname || type < AT_FILEEXTENSION || type > AT_MIMETYPE ||
        level < AL_MACHINE || level > AL_EFFECTIVE) return E_INVALIDARG;
    hr = open_app_capabilities(appname, &capabilities);
    if (FAILED(hr)) return hr;
    hr = read_association(capabilities, subkeys[type], query, &registered);
    RegCloseKey(capabilities);
    if (FAILED(hr)) return hr;
    hr = ApplicationAssociationRegistration_QueryCurrentDefault(iface, query, type, level, &current);
    if (SUCCEEDED(hr))
    {
        *is_default = !wcsicmp(current, registered);
        CoTaskMemFree(current);
    }
    else if (hr == HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION)) hr = S_OK;
    CoTaskMemFree(registered);
    return hr;
}

static HRESULT WINAPI ApplicationAssociationRegistration_QueryAppIsDefaultAll(IApplicationAssociationRegistration *iface,
        ASSOCIATIONLEVEL level, LPCWSTR appname, BOOL *is_default)
{
    static const WCHAR *subkeys[] = {L"FileAssociations", L"UrlAssociations", L"Startmenu", L"MimeAssociations"};
    HKEY capabilities, key;
    WCHAR *query;
    DWORD type, index, length, max_length, count = 0;
    BOOL match;
    LONG ret;
    HRESULT hr;
    if (!is_default) return E_INVALIDARG;
    *is_default = FALSE;
    if (!appname || !*appname || level < AL_MACHINE || level > AL_EFFECTIVE) return E_INVALIDARG;
    hr = open_app_capabilities(appname, &capabilities);
    if (FAILED(hr)) return hr;
    for (type = 0; type < ARRAY_SIZE(subkeys); ++type)
    {
        ret = RegOpenKeyExW(capabilities, subkeys[type], 0, KEY_READ, &key);
        if (ret == ERROR_FILE_NOT_FOUND) continue;
        if (ret) { hr = HRESULT_FROM_WIN32(ret); break; }
        ret = RegQueryInfoKeyW(key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_length, NULL, NULL, NULL);
        if (ret) { RegCloseKey(key); hr = HRESULT_FROM_WIN32(ret); break; }
        query = CoTaskMemAlloc((max_length + 1) * sizeof(WCHAR));
        if (!query) { RegCloseKey(key); hr = E_OUTOFMEMORY; break; }
        for (index = 0;; ++index)
        {
            length = max_length + 1;
            ret = RegEnumValueW(key, index, query, &length, NULL, NULL, NULL, NULL);
            if (ret == ERROR_NO_MORE_ITEMS) break;
            if (ret) { hr = HRESULT_FROM_WIN32(ret); break; }
            hr = ApplicationAssociationRegistration_QueryAppIsDefault(iface, query, type, level, appname, &match);
            if (FAILED(hr) || !match) break;
            ++count;
        }
        CoTaskMemFree(query);
        RegCloseKey(key);
        if (ret != ERROR_NO_MORE_ITEMS) goto done;
    }
    if (SUCCEEDED(hr)) *is_default = count != 0;
done:
    RegCloseKey(capabilities);
    return hr;
}

static HRESULT WINAPI ApplicationAssociationRegistration_SetAppAsDefault(IApplicationAssociationRegistration* This, LPCWSTR appname,
                                                                         LPCWSTR set, ASSOCIATIONTYPE set_type)
{
    FIXME("(%p)->(%s, %s, %d)\n", This, debugstr_w(appname), debugstr_w(set), set_type);
    return E_NOTIMPL;
}

static HRESULT WINAPI ApplicationAssociationRegistration_SetAppAsDefaultAll(IApplicationAssociationRegistration* This, LPCWSTR appname)
{
    FIXME("(%p)->(%s)\n", This, debugstr_w(appname));
    return E_NOTIMPL;
}


static HRESULT WINAPI ApplicationAssociationRegistration_ClearUserAssociations(IApplicationAssociationRegistration* This)
{
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}


static const IApplicationAssociationRegistrationVtbl IApplicationAssociationRegistration_vtbl =
{
    ApplicationAssociationRegistration_QueryInterface,
    ApplicationAssociationRegistration_AddRef,
    ApplicationAssociationRegistration_Release,
    ApplicationAssociationRegistration_QueryCurrentDefault,
    ApplicationAssociationRegistration_QueryAppIsDefault,
    ApplicationAssociationRegistration_QueryAppIsDefaultAll,
    ApplicationAssociationRegistration_SetAppAsDefault,
    ApplicationAssociationRegistration_SetAppAsDefaultAll,
    ApplicationAssociationRegistration_ClearUserAssociations
};

HRESULT WINAPI ApplicationAssociationRegistration_Constructor(IUnknown *outer, REFIID riid, LPVOID *ppv)
{
    IApplicationAssociationRegistrationImpl *This;
    HRESULT hr;

    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (outer)
        return CLASS_E_NOAGGREGATION;

    if (!(This = SHAlloc(sizeof(*This))))
        return E_OUTOFMEMORY;

    This->IApplicationAssociationRegistration_iface.lpVtbl = &IApplicationAssociationRegistration_vtbl;
    This->ref = 0;

    hr = IApplicationAssociationRegistration_QueryInterface(&This->IApplicationAssociationRegistration_iface, riid, ppv);
    if (FAILED(hr))
        SHFree(This);

    TRACE("returning 0x%lx with %p\n", hr, *ppv);
    return hr;
}
