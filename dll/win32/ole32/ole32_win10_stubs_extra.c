/*
 * OLE32 Windows 10 API Stubs - Extra Functions
 * Copyright 2024 ReactOS Team
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

/* Co* Functions */
HRESULT WINAPI CoCancelCall(_In_ DWORD dwThreadId, _In_ ULONG ulTimeout)
{
    FIXME("(%lu, %lu) stub\n", dwThreadId, ulTimeout);
    return E_NOTIMPL;
}

HRESULT WINAPI CoCreateInstanceFromApp(REFCLSID rclsid, IUnknown *punkOuter, DWORD dwClsCtx, 
    PVOID reserved, DWORD dwCount, void *pResults)
{
    FIXME("(%s, %p, %lx, %p, %lu, %p) stub\n", wine_dbgstr_guid(rclsid), punkOuter, 
           dwClsCtx, reserved, dwCount, pResults);
    return E_NOTIMPL;
}

HRESULT WINAPI CoCreateInstanceInAppContainer(IUnknown *punkOuter, DWORD dwClsCtx, PVOID reserved,
    DWORD dwCount, void *pResults)
{
    FIXME("(%p, %lx, %p, %lu, %p) stub\n", punkOuter, dwClsCtx, reserved, dwCount, pResults);
    return E_NOTIMPL;
}

HRESULT WINAPI CoCreateInstanceInPartition(REFGUID guidPartition, REFCLSID rclsid, 
    IUnknown *punkOuter, DWORD dwClsCtx, REFIID riid, void **ppv)
{
    FIXME("(%s, %s, %p, %lx, %s, %p) stub\n", wine_dbgstr_guid(guidPartition), 
           wine_dbgstr_guid(rclsid), punkOuter, dwClsCtx, wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI CoCreateObjectInContext(IUnknown *pContext, REFCLSID rclsid, REFIID riid, void **ppv)
{
    FIXME("(%p, %s, %s, %p) stub\n", pContext, wine_dbgstr_guid(rclsid), 
           wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI CoDisconnectContext(DWORD dwTimeout)
{
    FIXME("(%lu) stub\n", dwTimeout);
    return E_NOTIMPL;
}

HRESULT WINAPI CoEnterApplicationThreadContext(IUnknown *pUnk, void **ppOutContext)
{
    FIXME("(%p, %p) stub\n", pUnk, ppOutContext);
    return E_NOTIMPL;
}

HRESULT WINAPI CoExitApplicationThreadContext(void *pContext)
{
    FIXME("(%p) stub\n", pContext);
    return E_NOTIMPL;
}

HRESULT WINAPI CoGetApplicationThreadingModel(DWORD *pThreadingModel)
{
    FIXME("(%p) stub\n", pThreadingModel);
    return E_NOTIMPL;
}

HRESULT WINAPI CoGetCancelObject(DWORD dwThreadId, REFIID riid, void **ppv)
{
    FIXME("(%lu, %s, %p) stub\n", dwThreadId, wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI CoGetInstanceFromApp(REFCLSID rclsid, IUnknown *punkOuter, DWORD dwClsCtx,
    PVOID reserved, DWORD dwCount, void *pResults)
{
    FIXME("(%s, %p, %lx, %p, %lu, %p) stub\n", wine_dbgstr_guid(rclsid), punkOuter,
           dwClsCtx, reserved, dwCount, pResults);
    return E_NOTIMPL;
}

HRESULT WINAPI CoGetInterceptor(REFIID riidIntercepted, IUnknown *punkOuter, 
    REFIID riid, void **ppv)
{
    FIXME("(%s, %p, %s, %p) stub\n", wine_dbgstr_guid(riidIntercepted), 
           punkOuter, wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI CoGetInterceptorFromTypeInfo(REFIID riidIntercepted, IUnknown *punkOuter,
    ITypeInfo *pTypeInfo, REFIID riid, void **ppv)
{
    FIXME("(%s, %p, %p, %s, %p) stub\n", wine_dbgstr_guid(riidIntercepted), 
           punkOuter, pTypeInfo, wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI CoGetStdMarshalEx(IUnknown *pUnk, DWORD dwDestContext, IUnknown **ppUnk)
{
    FIXME("(%p, %lu, %p) stub\n", pUnk, dwDestContext, ppUnk);
    return E_NOTIMPL;
}

HRESULT WINAPI CoInitializeWinRT(DWORD dwCoInit)
{
    FIXME("(%lu) stub\n", dwCoInit);
    return S_OK;
}

HRESULT WINAPI CoQueryAuthenticationServices(DWORD *pcAuthSvc, SOLE_AUTHENTICATION_SERVICE **asAuthSvc)
{
    FIXME("(%p, %p) stub\n", pcAuthSvc, asAuthSvc);
    return E_NOTIMPL;
}

HRESULT WINAPI CoQueryProxyBlanketEx(IUnknown *pProxy, DWORD *pAuthnSvc, DWORD *pAuthzSvc,
    OLECHAR **pServerPrincName, DWORD *pAuthnLevel, DWORD *pImpLevel, void *pAuthInfo,
    DWORD *pCapabilities, DWORD *pFlags)
{
    FIXME("(%p, %p, %p, %p, %p, %p, %p, %p, %p) stub\n", pProxy, pAuthnSvc, pAuthzSvc,
           pServerPrincName, pAuthnLevel, pImpLevel, pAuthInfo, pCapabilities, pFlags);
    return E_NOTIMPL;
}

HRESULT WINAPI CoQueryReleaseObject(IUnknown *pUnk)
{
    FIXME("(%p) stub\n", pUnk);
    return E_NOTIMPL;
}

HRESULT WINAPI CoRegisterAppContainerPSClsid(REFCLSID rclsid, const WCHAR *pszAppContainerSid)
{
    FIXME("(%s, %s) stub\n", wine_dbgstr_guid(rclsid), wine_dbgstr_w(pszAppContainerSid));
    return E_NOTIMPL;
}

HRESULT WINAPI CoRegisterApplicationRestart(PCWSTR pwzCommandLine, DWORD dwFlags)
{
    FIXME("(%s, %lu) stub\n", wine_dbgstr_w(pwzCommandLine), dwFlags);
    return E_NOTIMPL;
}

HRESULT WINAPI CoRevokeAppContainerPSClsid(REFCLSID rclsid)
{
    FIXME("(%s) stub\n", wine_dbgstr_guid(rclsid));
    return E_NOTIMPL;
}

HRESULT WINAPI CoSetProxyBlanketEx(IUnknown *pProxy, DWORD dwAuthnSvc, DWORD dwAuthzSvc,
    OLECHAR *pServerPrincName, DWORD dwAuthnLevel, DWORD dwImpLevel, void *pAuthInfo,
    DWORD dwCapabilities, DWORD dwFlags)
{
    FIXME("(%p, %lu, %lu, %s, %lu, %lu, %p, %lu, %lu) stub\n", pProxy, dwAuthnSvc, dwAuthzSvc,
           wine_dbgstr_w(pServerPrincName), dwAuthnLevel, dwImpLevel, pAuthInfo, 
           dwCapabilities, dwFlags);
    return E_NOTIMPL;
}

VOID WINAPI CoUninitializeWinRT(VOID)
{
    FIXME("() stub\n");
}

VOID WINAPI CoUnloadingWOW(BOOL fUnloading)
{
    FIXME("(%d) stub\n", fUnloading);
}

HRESULT WINAPI CoUnregisterApplicationRestart(VOID)
{
    FIXME("() stub\n");
    return S_OK;
}

/* Other Functions */
HRESULT WINAPI CreateObjrefMoniker(IUnknown *pUnk, IMoniker **ppMoniker)
{
    FIXME("(%p, %p) stub\n", pUnk, ppMoniker);
    return E_NOTIMPL;
}

HRESULT WINAPI DllGetClassObjectWOW(REFCLSID rclsid, REFIID riid, void **ppv)
{
    FIXME("(%s, %s, %p) stub\n", wine_dbgstr_guid(rclsid), wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI EnableHookObject(BOOL fEnable)
{
    FIXME("(%d) stub\n", fEnable);
    return E_NOTIMPL;
}

HRESULT WINAPI GetDocumentBitStg(IStorage *pStg, DWORD *pdwMask)
{
    FIXME("(%p, %p) stub\n", pStg, pdwMask);
    return E_NOTIMPL;
}

HRESULT WINAPI GetHookInterface(void **ppv)
{
    FIXME("(%p) stub\n", ppv);
    return E_NOTIMPL;
}

BOOL WINAPI IsValidIid(REFIID riid)
{
    FIXME("(%s) stub\n", wine_dbgstr_guid(riid));
    return TRUE;
}

BOOL WINAPI IsValidPtrIn(const void *pv, UINT cb)
{
    FIXME("(%p, %u) stub\n", pv, cb);
    return TRUE;
}

BOOL WINAPI IsValidPtrOut(void *pv, UINT cb)
{
    FIXME("(%p, %u) stub\n", pv, cb);
    return TRUE;
}

HRESULT WINAPI MonikerRelativePathTo(IMoniker *pmkSrc, IMoniker *pmkDest, 
    IMoniker **ppmkRelPath, BOOL dwReserved)
{
    FIXME("(%p, %p, %p, %d) stub\n", pmkSrc, pmkDest, ppmkRelPath, dwReserved);
    return E_NOTIMPL;
}

HRESULT WINAPI OleConvertIStorageToOLESTREAMEx(IStorage *pStg, CLIPFORMAT cfFormat,
    LONG lWidth, LONG lHeight, DWORD dwSize, STGMEDIUM *pmedium, LPOLESTREAM lpolestm)
{
    FIXME("(%p, %u, %ld, %ld, %lu, %p, %p) stub\n", pStg, cfFormat, lWidth, lHeight,
           dwSize, pmedium, lpolestm);
    return E_NOTIMPL;
}

HRESULT WINAPI OleConvertOLESTREAMToIStorageEx(LPOLESTREAM lpolestm, IStorage *pStg,
    CLIPFORMAT *pcfFormat, LONG *plwWidth, LONG *plHeight, DWORD *pdwSize, STGMEDIUM *pmedium)
{
    FIXME("(%p, %p, %p, %p, %p, %p, %p) stub\n", lpolestm, pStg, pcfFormat, plwWidth,
           plHeight, pdwSize, pmedium);
    return E_NOTIMPL;
}

HRESULT WINAPI OleCreateEx(REFCLSID rclsid, REFIID riid, DWORD dwFlags, DWORD renderopt,
    ULONG cFormats, DWORD *rgAdvf, LPFORMATETC rgFormatEtc, IAdviseSink *lpAdviseSink,
    DWORD *rgdwConnection, IOleClientSite *pClientSite, IStorage *pStg, void **ppvObj)
{
    FIXME("(%s, %s, %lu, %lu, %lu, %p, %p, %p, %p, %p, %p, %p) stub\n",
           wine_dbgstr_guid(rclsid), wine_dbgstr_guid(riid), dwFlags, renderopt,
           cFormats, rgAdvf, rgFormatEtc, lpAdviseSink, rgdwConnection, pClientSite,
           pStg, ppvObj);
    return E_NOTIMPL;
}

HRESULT WINAPI OleCreateLinkEx(LPMONIKER pmkLinkSrc, REFIID riid, DWORD dwFlags,
    DWORD renderopt, ULONG cFormats, DWORD *rgAdvf, LPFORMATETC rgFormatEtc,
    IAdviseSink *lpAdviseSink, DWORD *rgdwConnection, IOleClientSite *pClientSite,
    IStorage *pStg, void **ppvObj)
{
    FIXME("(%p, %s, %lu, %lu, %lu, %p, %p, %p, %p, %p, %p, %p) stub\n",
           pmkLinkSrc, wine_dbgstr_guid(riid), dwFlags, renderopt, cFormats,
           rgAdvf, rgFormatEtc, lpAdviseSink, rgdwConnection, pClientSite, pStg, ppvObj);
    return E_NOTIMPL;
}

HRESULT WINAPI OleCreateLinkFromDataEx(IDataObject *pSrcDataObj, REFIID riid, DWORD dwFlags,
    DWORD renderopt, ULONG cFormats, DWORD *rgAdvf, LPFORMATETC rgFormatEtc,
    IAdviseSink *lpAdviseSink, DWORD *rgdwConnection, IOleClientSite *pClientSite,
    IStorage *pStg, void **ppvObj)
{
    FIXME("(%p, %s, %lu, %lu, %lu, %p, %p, %p, %p, %p, %p, %p) stub\n",
           pSrcDataObj, wine_dbgstr_guid(riid), dwFlags, renderopt, cFormats,
           rgAdvf, rgFormatEtc, lpAdviseSink, rgdwConnection, pClientSite, pStg, ppvObj);
    return E_NOTIMPL;
}

HRESULT WINAPI OleCreateLinkToFileEx(LPCOLESTR lpszFileName, REFIID riid, DWORD dwFlags,
    DWORD renderopt, ULONG cFormats, DWORD *rgAdvf, LPFORMATETC rgFormatEtc,
    IAdviseSink *lpAdviseSink, DWORD *rgdwConnection, IOleClientSite *pClientSite,
    IStorage *pStg, void **ppvObj)
{
    FIXME("(%s, %s, %lu, %lu, %lu, %p, %p, %p, %p, %p, %p, %p) stub\n",
           wine_dbgstr_w(lpszFileName), wine_dbgstr_guid(riid), dwFlags, renderopt,
           cFormats, rgAdvf, rgFormatEtc, lpAdviseSink, rgdwConnection, pClientSite,
           pStg, ppvObj);
    return E_NOTIMPL;
}

HRESULT WINAPI OpenOrCreateStream(IStorage *pStg, LPCWSTR pwcsName, IStream **ppstm)
{
    FIXME("(%p, %s, %p) stub\n", pStg, wine_dbgstr_w(pwcsName), ppstm);
    return E_NOTIMPL;
}

HRESULT WINAPI ReadOleStg(IStorage *pStg, DWORD *pdwFlags, DWORD *pdwOptUpdate,
    DWORD *pdwReserved, LPMONIKER *ppmk, LPSTREAM *ppstmOut)
{
    FIXME("(%p, %p, %p, %p, %p, %p) stub\n", pStg, pdwFlags, pdwOptUpdate,
           pdwReserved, ppmk, ppstmOut);
    return E_NOTIMPL;
}

/* Windows Runtime Functions */
HRESULT WINAPI RestrictedErrorInfoCreateReference(DWORD dwContext, HRESULT hr,
    LPCWSTR pszDescription, void **ppReference)
{
    FIXME("(%lu, %lx, %s, %p) stub\n", dwContext, hr, wine_dbgstr_w(pszDescription), ppReference);
    return E_NOTIMPL;
}

HRESULT WINAPI RestrictedErrorInfoGetDetails(void *pReference, BSTR *pbstrDescription,
    HRESULT *phr, BSTR *pbstrRestrictedDescription, BSTR *pbstrCapabilitySid)
{
    FIXME("(%p, %p, %p, %p, %p) stub\n", pReference, pbstrDescription, phr,
           pbstrRestrictedDescription, pbstrCapabilitySid);
    return E_NOTIMPL;
}

HRESULT WINAPI RestrictedErrorInfoGetReference(void **ppReference)
{
    FIXME("(%p) stub\n", ppReference);
    return E_NOTIMPL;
}

HRESULT WINAPI RoActivateInstance(HSTRING classId, IUnknown **ppv)
{
    FIXME("(%p, %p) stub\n", classId, ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI RoCaptureErrorContext(HRESULT hr)
{
    FIXME("(%lx) stub\n", hr);
    return S_OK;
}

VOID WINAPI RoFailFastWithErrorContext(HRESULT hr)
{
    FIXME("(%lx) stub\n", hr);
}

HRESULT WINAPI RoGetActivationFactory(HSTRING classId, REFIID riid, void **ppv)
{
    FIXME("(%p, %s, %p) stub\n", classId, wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI RoGetActivationFactoryProxy(HSTRING classId, REFIID riid, void **ppv)
{
    FIXME("(%p, %s, %p) stub\n", classId, wine_dbgstr_guid(riid), ppv);
    return E_NOTIMPL;
}

HRESULT WINAPI RoGetAgileReference(DWORD options, REFIID riid, IUnknown *pUnk, void **ppReference)
{
    FIXME("(%lu, %s, %p, %p) stub\n", options, wine_dbgstr_guid(riid), pUnk, ppReference);
    return E_NOTIMPL;
}

HRESULT WINAPI RoGetErrorReportingFlags(UINT32 *pflags)
{
    FIXME("(%p) stub\n", pflags);
    if (pflags)
        *pflags = 0;
    return S_OK;
}

/* Note: IMetaDataDispenser and IMetaDataImport2 are not defined in ReactOS headers */
typedef void IMetaDataDispenser;
typedef void IMetaDataImport2;

HRESULT WINAPI RoGetMetaDataFile(const HSTRING name, IMetaDataDispenser *pDispenser,
    HSTRING *phString, IMetaDataImport2 **ppImport, void **ppTypeNameFactory)
{
    FIXME("(%p, %p, %p, %p, %p) stub\n", name, pDispenser, phString, ppImport, ppTypeNameFactory);
    return E_NOTIMPL;
}

HRESULT WINAPI RoInitialize(DWORD dwThreadingModel)
{
    FIXME("(%lu) stub\n", dwThreadingModel);
    return S_OK;
}

BOOL WINAPI RoIsApiContractMajorVersionPresent(PCWSTR contractName, UINT16 majorVersion)
{
    FIXME("(%s, %u) stub\n", wine_dbgstr_w(contractName), majorVersion);
    return FALSE;
}

BOOL WINAPI RoIsApiContractPresent(PCWSTR contractName)
{
    FIXME("(%s) stub\n", wine_dbgstr_w(contractName));
    return FALSE;
}

HRESULT WINAPI RoOriginateError(HRESULT hr, HSTRING message)
{
    FIXME("(%lx, %p) stub\n", hr, message);
    return S_OK;
}

HRESULT WINAPI RoOriginateErrorW(HRESULT hr, UINT cchMax, PCWSTR message)
{
    FIXME("(%lx, %u, %s) stub\n", hr, cchMax, wine_dbgstr_w(message));
    return S_OK;
}

HRESULT WINAPI RoOriginateLanguageException(HRESULT hr, HSTRING message, IUnknown *pLanguageException)
{
    FIXME("(%lx, %p, %p) stub\n", hr, message, pLanguageException);
    return S_OK;
}

HRESULT WINAPI RoParseTypeName(HSTRING typeName, DWORD *pPartsCount, HSTRING **pParts)
{
    FIXME("(%p, %p, %p) stub\n", typeName, pPartsCount, pParts);
    return E_NOTIMPL;
}

HRESULT WINAPI RoRegisterActivationFactories(HSTRING *pClassIds, void **pFactories, UINT32 count,
    void **ppCookie)
{
    FIXME("(%p, %p, %u, %p) stub\n", pClassIds, pFactories, count, ppCookie);
    return E_NOTIMPL;
}

HRESULT WINAPI RoResolveRestrictedErrorInfoReference(PCWSTR pReference, void **ppInfo)
{
    FIXME("(%s, %p) stub\n", wine_dbgstr_w(pReference), ppInfo);
    return E_NOTIMPL;
}

/* EOF */