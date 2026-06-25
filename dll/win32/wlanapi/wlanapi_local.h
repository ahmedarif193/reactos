/*
 * Wireless LAN API (wlanapi.dll) -- internal definitions
 *
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The MIDL-generated wlansvc_c.h inlines its own copy of the <wlanapi.h>
 * types but drops the preprocessor #defines; those are restated here,
 * identical to psdk/wlanapi.h.
 */

#ifndef _WLANAPI_LOCAL_H_
#define _WLANAPI_LOCAL_H_

#ifndef DOT11_RATE_SET_MAX_LENGTH
#define DOT11_RATE_SET_MAX_LENGTH 126
#endif

#ifndef WLAN_NOTIFICATION_SOURCE_NONE
#define WLAN_NOTIFICATION_SOURCE_NONE         0x00000000
#define WLAN_NOTIFICATION_SOURCE_ALL          0x0000FFFF
#define WLAN_NOTIFICATION_SOURCE_ACM          0x00000008
#define WLAN_NOTIFICATION_SOURCE_MSM          0x00000010
#define WLAN_NOTIFICATION_SOURCE_SECURITY     0x00000020
#define WLAN_NOTIFICATION_SOURCE_IHV          0x00000040
#endif

/*
 * WIDL only inlines the <wlanapi.h> types that the RPC interface references.
 * _RpcGetNetworkBssList transfers the BSS list as a raw byte buffer, so
 * WLAN_BSS_LIST is never inlined into wlansvc_c.h.  wlanapi.dll only handles
 * it as an opaque pointer (the buffer is passed straight through to the
 * caller), so an incomplete type is enough here -- and it avoids clashing
 * with the structs wlansvc_c.h does inline.
 */
typedef struct _WLAN_BSS_LIST WLAN_BSS_LIST, *PWLAN_BSS_LIST;

DWORD WINAPI
WlanGetNetworkBssList(IN HANDLE hClientHandle,
                      IN const GUID *pInterfaceGuid,
                      IN const PDOT11_SSID pDot11Ssid,
                      IN DOT11_BSS_TYPE dot11BssType,
                      IN BOOL bSecurityEnabled,
                      PVOID pReserved,
                      OUT PWLAN_BSS_LIST *ppWlanBssList);

/* main.c */
PVOID WINAPI WlanAllocateMemory(DWORD dwSize);
VOID  WINAPI WlanFreeMemory(PVOID pMemory);

/* notification.c */
DWORD WlanStartNotificationThread(HANDLE hClientHandle);
VOID  WlanStopNotificationThread(HANDLE hClientHandle);
DWORD WlanRegisterNotificationImpl(HANDLE hClientHandle,
                                   DWORD dwNotifSource,
                                   BOOL bIgnoreDuplicate,
                                   WLAN_NOTIFICATION_CALLBACK funcCallback,
                                   PVOID pCallbackContext,
                                   PDWORD pdwPrevNotifSource);

#endif /* _WLANAPI_LOCAL_H_ */
