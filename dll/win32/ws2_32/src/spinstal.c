/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS WinSock 2 API
 * FILE:        dll/win32/ws2_32/src/spinstal.c
 * PURPOSE:     Transport Service Provider Installation
 * PROGRAMMER:  Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES ******************************************************************/

#include <ws2_32.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * @unimplemented
 */
INT
WSPAPI
WSCInstallProvider(IN LPGUID lpProviderId,
                   IN CONST WCHAR * lpszProviderDllPath,
                   IN CONST LPWSAPROTOCOL_INFOW lpProtocolInfoList,
                   IN DWORD dwNumberOfEntries,
                   OUT LPINT lpErrno)
{
    UNIMPLEMENTED;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCDeinstallProvider(IN LPGUID lpProviderId,
                     OUT LPINT lpErrno)
{
    UNIMPLEMENTED;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCWriteProviderOrder(IN LPDWORD lpwdCatalogEntryId,
                      IN DWORD dwNumberOfEntries)
{
    UNIMPLEMENTED;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCGetProviderInfo(IN LPGUID lpProviderId,
                   IN WSC_PROVIDER_INFO_TYPE InfoType,
                   OUT PBYTE Info,
                   IN OUT size_t *InfoSize,
                   IN DWORD Flags,
                   OUT LPINT lpErrno)
{
    UNIMPLEMENTED;
    if (lpErrno) *lpErrno = WSAEINVAL;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCGetProviderInfo32(IN LPGUID lpProviderId,
                     IN WSC_PROVIDER_INFO_TYPE InfoType,
                     OUT PBYTE Info,
                     IN OUT size_t *InfoSize,
                     IN DWORD Flags,
                     OUT LPINT lpErrno)
{
    UNIMPLEMENTED;
    if (lpErrno) *lpErrno = WSAEINVAL;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}

/*
 * @unimplemented
 */
INT
WSPAPI
WSCSetApplicationCategory(IN LPCWSTR Path,
                          IN DWORD PathLength,
                          IN LPCWSTR Extra,
                          IN DWORD ExtraLength,
                          IN DWORD PermittedLspCategories,
                          OUT DWORD *pPrevPermLspCat,
                          OUT LPINT lpErrno)
{
    UNIMPLEMENTED;
    if (lpErrno) *lpErrno = WSAEINVAL;
    SetLastError(WSAEINVAL);
    return SOCKET_ERROR;
}
