/*
 * OLE32 Windows 10 API Stubs
 * Copyright 2024 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <wine/config.h>

#include <stdarg.h>

#include <windef.h>
#include <winbase.h>
#include <objbase.h>
#include <wine/debug.h>

/* HSTRING is a handle type for Windows Runtime strings */
typedef struct HSTRING__ *HSTRING;

WINE_DEFAULT_DEBUG_CHANNEL(ole);

/* Note: CoGetApartmentType is already implemented in compobj.c */

/*
 * @unimplemented
 */
HRESULT WINAPI
CoGetApplicationSingletonKey(
    _In_ GUID guidApplication,
    _Out_ HANDLE *pHandle)
{
    FIXME("(%s, %p) stub\n", wine_dbgstr_guid(&guidApplication), pHandle);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
CoGetApplicationToken(
    _Out_ HANDLE *pHandle)
{
    FIXME("(%p) stub\n", pHandle);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
CoSetApplicationSingletonKey(
    _In_ GUID guidApplication,
    _In_ HANDLE hKey)
{
    FIXME("(%s, %p) stub\n", wine_dbgstr_guid(&guidApplication), hKey);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
CoDecodeProxy(
    _In_ DWORD dwClientPid,
    _In_ UINT64 ui64ProxyAddress,
    _Out_ void* pServerInformation)  /* PServerInformation not defined in ReactOS headers */
{
    FIXME("(%lu, %I64u, %p) stub\n", dwClientPid, ui64ProxyAddress, pServerInformation);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
CreateStringStream(
    _In_ PCWSTR pszString,
    _Out_ IStream **ppStream)
{
    FIXME("(%s, %p) stub\n", wine_dbgstr_w(pszString), ppStream);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
ReadStringStream(
    _In_ IStream *pStream,
    _Out_ LPWSTR *ppszString)
{
    FIXME("(%p, %p) stub\n", pStream, ppszString);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
WriteStringStream(
    _In_ IStream *pStream,
    _In_ LPCWSTR pszString)
{
    FIXME("(%p, %s) stub\n", pStream, wine_dbgstr_w(pszString));
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
RoRevokeActivationFactories(
    _In_ DWORD cookie)
{
    FIXME("(%lu) stub\n", cookie);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
RoSetErrorReportingFlags(
    _In_ UINT32 flags)
{
    FIXME("(%u) stub\n", flags);
    return S_OK;
}

/*
 * @unimplemented
 */
VOID WINAPI
RoUninitialize(VOID)
{
    FIXME("() stub\n");
}

/*
 * @unimplemented
 */
HRESULT WINAPI
SetDocumentBitStg(
    _In_ IStorage *pStg,
    _In_ BOOL fSet)
{
    FIXME("(%p, %d) stub\n", pStg, fSet);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
StgGetIFillLockBytesOnFile(
    _In_ OLECHAR const *pwcsName,
    _Out_ IFillLockBytes **ppflb)
{
    FIXME("(%s, %p) stub\n", wine_dbgstr_w(pwcsName), ppflb);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
StgGetIFillLockBytesOnILockBytes(
    _In_ ILockBytes *pilb,
    _Out_ IFillLockBytes **ppflb)
{
    FIXME("(%p, %p) stub\n", pilb, ppflb);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
StgOpenAsyncDocfileOnIFillLockBytes(
    _In_ IFillLockBytes *pflb,
    _In_ DWORD grfMode,
    _In_ DWORD asyncFlags,
    _Out_ IStorage **ppstgOpen)
{
    FIXME("(%p, %lu, %lu, %p) stub\n", pflb, grfMode, asyncFlags, ppstgOpen);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
UpdateDCOMSettings(VOID)
{
    FIXME("() stub\n");
    return S_OK;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
UtConvertDvtd16toDvtd32(
    _In_ LPVOID pIn,
    _Out_ LPVOID pOut)
{
    FIXME("(%p, %p) stub\n", pIn, pOut);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
UtConvertDvtd32toDvtd16(
    _In_ LPVOID pIn,
    _Out_ LPVOID pOut)
{
    FIXME("(%p, %p) stub\n", pIn, pOut);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
UtGetDvtd16Info(
    _In_ LPVOID pIn,
    _Out_ LPDWORD pdwSize)
{
    FIXME("(%p, %p) stub\n", pIn, pdwSize);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
UtGetDvtd32Info(
    _In_ LPVOID pIn,
    _Out_ LPDWORD pdwSize)
{
    FIXME("(%p, %p) stub\n", pIn, pdwSize);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
WindowsCreateBuffer(
    _In_ UINT32 length,
    _Out_ HANDLE *phBuffer)
{
    FIXME("(%u, %p) stub\n", length, phBuffer);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
WindowsCreateBufferFromData(
    _In_reads_(length) const BYTE *data,
    _In_ UINT32 length,
    _Out_ HANDLE *phBuffer)
{
    FIXME("(%p, %u, %p) stub\n", data, length, phBuffer);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
WindowsDeleteBuffer(
    _In_ HANDLE hBuffer)
{
    FIXME("(%p) stub\n", hBuffer);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
WindowsGetBufferFromString(
    _In_ HSTRING string,
    _Out_ HANDLE *phBuffer)
{
    FIXME("(%p, %p) stub\n", string, phBuffer);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
WindowsPromoteStringBuffer(
    _In_ HANDLE hBuffer,
    _Out_ HSTRING *pString)
{
    FIXME("(%p, %p) stub\n", hBuffer, pString);
    return E_NOTIMPL;
}

/*
 * @unimplemented
 */
HRESULT WINAPI
WriteOleStg(
    _In_ LPSTORAGE pstg,
    _In_ IOleObject *pOleObj,
    _In_ DWORD dwReserved,
    _In_ LPSTREAM pstm)
{
    FIXME("(%p, %p, %lu, %p) stub\n", pstg, pOleObj, dwReserved, pstm);
    return E_NOTIMPL;
}

/* EOF */