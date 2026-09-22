/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests rich edit context menus supplied by an OLE callback
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define COBJMACROS
#define INITGUID
#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <ole2.h>
#include <richedit.h>
#include <richole.h>

typedef struct
{
    IRichEditOleCallback IRichEditOleCallback_iface;
    LONG MenuCalls;
} TEST_CALLBACK;

static TEST_CALLBACK *
impl_from_IRichEditOleCallback(IRichEditOleCallback *iface)
{
    return CONTAINING_RECORD(iface, TEST_CALLBACK, IRichEditOleCallback_iface);
}

static HRESULT STDMETHODCALLTYPE
Callback_QueryInterface(IRichEditOleCallback *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IRichEditOleCallback))
    {
        *ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE
Callback_AddRef(IRichEditOleCallback *iface)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE
Callback_Release(IRichEditOleCallback *iface)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE
Callback_GetNewStorage(IRichEditOleCallback *iface, LPSTORAGE *lplpstg)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_GetInPlaceContext(IRichEditOleCallback *iface, LPOLEINPLACEFRAME *lplpFrame,
                           LPOLEINPLACEUIWINDOW *lplpDoc, LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_ShowContainerUI(IRichEditOleCallback *iface, BOOL fShow)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_QueryInsertObject(IRichEditOleCallback *iface, LPCLSID lpclsid, LPSTORAGE lpstg, LONG cp)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_DeleteObject(IRichEditOleCallback *iface, LPOLEOBJECT lpoleobj)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_QueryAcceptData(IRichEditOleCallback *iface, LPDATAOBJECT lpdataobj, CLIPFORMAT *lpcfFormat,
                         DWORD reco, BOOL fReally, HGLOBAL hMetaPict)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_ContextSensitiveHelp(IRichEditOleCallback *iface, BOOL fEnterMode)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_GetClipboardData(IRichEditOleCallback *iface, CHARRANGE *lpchrg, DWORD reco, LPDATAOBJECT *lplpdataobj)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_GetDragDropEffect(IRichEditOleCallback *iface, BOOL fDrag, DWORD grfKeyState, LPDWORD pdwEffect)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE
Callback_GetContextMenu(IRichEditOleCallback *iface, WORD seltype, LPOLEOBJECT lpoleobj,
                        CHARRANGE *lpchrg, HMENU *lphmenu)
{
    TEST_CALLBACK *This = impl_from_IRichEditOleCallback(iface);

    This->MenuCalls++;
    *lphmenu = NULL;
    return S_OK;
}

static IRichEditOleCallbackVtbl CallbackVtbl =
{
    Callback_QueryInterface,
    Callback_AddRef,
    Callback_Release,
    Callback_GetNewStorage,
    Callback_GetInPlaceContext,
    Callback_ShowContainerUI,
    Callback_QueryInsertObject,
    Callback_DeleteObject,
    Callback_QueryAcceptData,
    Callback_ContextSensitiveHelp,
    Callback_GetClipboardData,
    Callback_GetDragDropEffect,
    Callback_GetContextMenu
};

START_TEST(ContextMenu)
{
    TEST_CALLBACK Callback = { { &CallbackVtbl }, 0 };
    HMODULE Module;
    HWND hWnd;

    Module = LoadLibraryW(L"riched20.dll");
    ok(Module != NULL, "LoadLibraryW failed: %lu\n", GetLastError());
    if (!Module)
        return;

    hWnd = CreateWindowExW(0, RICHEDIT_CLASS20W, NULL, WS_POPUP | ES_MULTILINE,
                           0, 0, 200, 100, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (hWnd)
    {
        ok(SendMessageW(hWnd, EM_SETOLECALLBACK, 0, (LPARAM)&Callback.IRichEditOleCallback_iface),
           "EM_SETOLECALLBACK failed\n");

        SetLastError(0xdeadbeef);
        SendMessageW(hWnd, WM_CONTEXTMENU, (WPARAM)hWnd, MAKELPARAM(10, 10));
        ok(Callback.MenuCalls == 1, "GetContextMenu was called %ld times\n", Callback.MenuCalls);
        ok(GetLastError() != ERROR_INVALID_MENU_HANDLE, "A NULL context menu was tracked\n");

        SendMessageW(hWnd, EM_SETOLECALLBACK, 0, 0);
        DestroyWindow(hWnd);
    }

    FreeLibrary(Module);
}
