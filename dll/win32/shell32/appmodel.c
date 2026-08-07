/*
 * PROJECT:     ReactOS Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Modern shell compatibility entry points
 * COPYRIGHT:   Adapted from corresponding Wine shell32 and shcore implementations
 */

#include <stdarg.h>

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
