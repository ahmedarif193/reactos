/*
 * PROJECT:     ReactOS WLAN Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal state model for the WLAN AutoConfig service
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WLANSVC_INTERNAL_H_
#define _WLANSVC_INTERNAL_H_

/*
 * The dot11/WLAN types and enums come from the MIDL-generated wlansvc_s.h
 * (included by precomp.h before this header).  MIDL does not emit wlanapi.h's
 * preprocessor #defines, so the few needed here are restated (guarded).
 */

#ifndef DOT11_SSID_MAX_LENGTH
#define DOT11_SSID_MAX_LENGTH 32
#endif
#ifndef DOT11_RATE_SET_MAX_LENGTH
#define DOT11_RATE_SET_MAX_LENGTH 126
#endif
#ifndef WLAN_MAX_NAME_LENGTH
#define WLAN_MAX_NAME_LENGTH 256
#endif
#ifndef WLAN_MAX_PHY_INDEX
#define WLAN_MAX_PHY_INDEX 64
#endif

#ifndef WLAN_AVAILABLE_NETWORK_CONNECTED
#define WLAN_AVAILABLE_NETWORK_CONNECTED   0x00000001
#define WLAN_AVAILABLE_NETWORK_HAS_PROFILE 0x00000002
#endif

#ifndef WLAN_READ_ACCESS
#define WLAN_READ_ACCESS    (STANDARD_RIGHTS_READ | FILE_READ_DATA)
#define WLAN_EXECUTE_ACCESS (STANDARD_RIGHTS_EXECUTE | FILE_EXECUTE | WLAN_READ_ACCESS)
#define WLAN_WRITE_ACCESS   (STANDARD_RIGHTS_WRITE | FILE_WRITE_DATA | DELETE | WRITE_DAC | WLAN_READ_ACCESS | WLAN_EXECUTE_ACCESS)
#endif

#ifndef WLAN_REASON_CODE_SUCCESS
#define WLAN_REASON_CODE_SUCCESS    0   /* == L2_REASON_CODE_SUCCESS */
#endif

/*
 * <reactos/nwifi_ioctl.h> pulls in <windot11.h>, whose dot11 types the MIDL
 * stub has already defined; predefining its include guard suppresses the
 * duplicate body.  The dot11 macros lost this way are restated above.
 */
#ifndef __WINDOT11_H__
#define __WINDOT11_H__
#endif
#include <reactos/nwifi_ioctl.h>

/*
 * Service-local working limits.  The wire structs are variable-length (IEs
 * appended after each BSS entry), so these only bound the service's own
 * cached copies, not the IOCTL buffers.
 */
#ifndef NWIFI_MAX_IE_SIZE
#define NWIFI_MAX_IE_SIZE       512
#endif
#ifndef NWIFI_MAX_PSK_BYTES
#define NWIFI_MAX_PSK_BYTES     64      /* 8..63 ASCII chars, or 64 hex */
#endif
#ifndef NWIFI_MAX_BSS_ENTRIES
#define NWIFI_MAX_BSS_ENTRIES   64
#endif

/*
 * WIDL emits only the wlanapi.h types reachable from the RPC interface into the
 * generated stub header, and drops every preprocessor #define.  The missing
 * #defines and types are restated here, identical to sdk/include/psdk/wlanapi.h.
 */

#ifndef WLAN_NOTIFICATION_SOURCE_NONE
#define WLAN_NOTIFICATION_SOURCE_NONE         0x00000000
#define WLAN_NOTIFICATION_SOURCE_ALL          0x0000FFFF
#define WLAN_NOTIFICATION_SOURCE_ACM          0x00000008
#define WLAN_NOTIFICATION_SOURCE_MSM          0x00000010
#define WLAN_NOTIFICATION_SOURCE_SECURITY     0x00000020
#define WLAN_NOTIFICATION_SOURCE_IHV          0x00000040
#endif

/*
 * WIDL emits only the wlanapi.h types reachable from the RPC interface; the
 * ones below are used service-locally (built into opaque LPBYTE blobs by the
 * BSS-list, connection-query and notification paths) and never named in
 * wlansvc.idl, so they are absent from the generated stub.  Restate them here,
 * identical to sdk/include/psdk/wlanapi.h and guarded by that header's own
 * include sentinel so a real <wlanapi.h>, if ever pulled in, supersedes these
 * rather than colliding.  Their member types (DOT11_*, WLAN_INTERFACE_STATE,
 * WLAN_SIGNAL_QUALITY, WLAN_REASON_CODE, ...) do come from the stub and are in
 * scope here.
 */
#ifndef _WLANAPI_H
#define _WLANAPI_H
#endif

#ifndef _WLANAPI_H

typedef enum _WLAN_NOTIFICATION_ACM {
    wlan_notification_acm_start = 0,
    wlan_notification_acm_autoconf_enabled,
    wlan_notification_acm_autoconf_disabled,
    wlan_notification_acm_background_scan_enabled,
    wlan_notification_acm_background_scan_disabled,
    wlan_notification_acm_bss_type_change,
    wlan_notification_acm_power_setting_change,
    wlan_notification_acm_scan_complete,
    wlan_notification_acm_scan_fail,
    wlan_notification_acm_connection_start,
    wlan_notification_acm_connection_complete,
    wlan_notification_acm_connection_attempt_fail,
    wlan_notification_acm_filter_list_change,
    wlan_notification_acm_interface_arrival,
    wlan_notification_acm_interface_removal,
    wlan_notification_acm_profile_change,
    wlan_notification_acm_profile_name_change,
    wlan_notification_acm_profiles_exhausted,
    wlan_notification_acm_network_not_available,
    wlan_notification_acm_network_available,
    wlan_notification_acm_disconnecting,
    wlan_notification_acm_disconnected,
    wlan_notification_acm_adhoc_network_state_change,
    wlan_notification_acm_profile_unblocked,
    wlan_notification_acm_screen_power_change,
    wlan_notification_acm_profile_blocked,
    wlan_notification_acm_scan_list_refresh,
    wlan_notification_acm_operational_state_change,
    wlan_notification_acm_end
} WLAN_NOTIFICATION_ACM, *PWLAN_NOTIFICATION_ACM;

typedef struct _WLAN_RATE_SET {
    ULONG uRateSetLength;
    USHORT usRateSet[DOT11_RATE_SET_MAX_LENGTH];
} WLAN_RATE_SET, *PWLAN_RATE_SET;

typedef struct _WLAN_BSS_ENTRY {
    DOT11_SSID dot11Ssid;
    ULONG uPhyId;
    DOT11_MAC_ADDRESS dot11Bssid;
    DOT11_BSS_TYPE dot11BssType;
    DOT11_PHY_TYPE dot11BssPhyType;
    LONG lRssi;
    ULONG uLinkQuality;
    BOOLEAN bInRegDomain;
    USHORT usBeaconPeriod;
    ULONGLONG ullTimestamp;
    ULONGLONG ullHostTimestamp;
    USHORT usCapabilityInformation;
    ULONG  ulChCenterFrequency;
    WLAN_RATE_SET wlanRateSet;
    ULONG ulIeOffset;
    ULONG ulIeSize;
} WLAN_BSS_ENTRY, *PWLAN_BSS_ENTRY;

typedef struct _WLAN_BSS_LIST {
    DWORD dwTotalSize;
    DWORD dwNumberOfItems;
    WLAN_BSS_ENTRY wlanBssEntries[1];
} WLAN_BSS_LIST, *PWLAN_BSS_LIST;

typedef struct _WLAN_ASSOCIATION_ATTRIBUTES {
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
    DOT11_MAC_ADDRESS dot11Bssid;
    DOT11_PHY_TYPE dot11PhyType;
    ULONG uDot11PhyIndex;
    WLAN_SIGNAL_QUALITY wlanSignalQuality;
    ULONG ulRxRate;
    ULONG ulTxRate;
} WLAN_ASSOCIATION_ATTRIBUTES, *PWLAN_ASSOCIATION_ATTRIBUTES;

typedef struct _WLAN_SECURITY_ATTRIBUTES {
    BOOL bSecurityEnabled;
    BOOL bOneXEnabled;
    DOT11_AUTH_ALGORITHM dot11AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM dot11CipherAlgorithm;
} WLAN_SECURITY_ATTRIBUTES, *PWLAN_SECURITY_ATTRIBUTES;

typedef struct _WLAN_CONNECTION_ATTRIBUTES {
    WLAN_INTERFACE_STATE isState;
    WLAN_CONNECTION_MODE wlanConnectionMode;
    WCHAR strProfileName[WLAN_MAX_NAME_LENGTH];
    WLAN_ASSOCIATION_ATTRIBUTES wlanAssociationAttributes;
    WLAN_SECURITY_ATTRIBUTES wlanSecurityAttributes;
} WLAN_CONNECTION_ATTRIBUTES, *PWLAN_CONNECTION_ATTRIBUTES;

typedef struct _WLAN_CONNECTION_NOTIFICATION_DATA {
    WLAN_CONNECTION_MODE wlanConnectionMode;
    WCHAR strProfileName[WLAN_MAX_NAME_LENGTH];
    DOT11_SSID dot11Ssid;
    DOT11_BSS_TYPE dot11BssType;
    BOOL bSecurityEnabled;
    WLAN_REASON_CODE wlanReasonCode;
    DWORD dwFlags;
    WCHAR strProfileXml[1];
} WLAN_CONNECTION_NOTIFICATION_DATA, *PWLAN_CONNECTION_NOTIFICATION_DATA;

#endif /* _WLANAPI_H */

/* Service-side state objects. */

/* A cached scan result for one BSS, owned by an interface. */
typedef struct _WLANSVC_BSS_ENTRY
{
    LIST_ENTRY         ListEntry;
    DOT11_SSID         Ssid;
    DOT11_MAC_ADDRESS  Bssid;
    DOT11_BSS_TYPE     BssType;
    DOT11_PHY_TYPE     PhyType;
    LONG               Rssi;
    ULONG              LinkQuality;
    USHORT             BeaconPeriod;
    USHORT             CapabilityInformation;
    ULONG              ChCenterFrequency;
    ULONGLONG          HostTimestamp;
    ULONGLONG          BeaconTimestamp;
    BOOL               SecurityEnabled;
    DOT11_AUTH_ALGORITHM   DefaultAuth;
    DOT11_CIPHER_ALGORITHM DefaultCipher;
    DOT11_CIPHER_ALGORITHM DefaultMulticastCipher;
    ULONG              RateCount;
    USHORT             Rates[DOT11_RATE_SET_MAX_LENGTH];
    ULONG              IeLength;
    UCHAR              IeData[NWIFI_MAX_IE_SIZE];
} WLANSVC_BSS_ENTRY, *PWLANSVC_BSS_ENTRY;

/* A WPA2-PSK / open network profile parsed from profile XML. */
typedef struct _WLANSVC_PROFILE
{
    LIST_ENTRY              ListEntry;
    WCHAR                   Name[WLAN_MAX_NAME_LENGTH];
    DOT11_SSID              Ssid;
    DOT11_BSS_TYPE          BssType;
    BOOL                    AutoConnect;
    DOT11_AUTH_ALGORITHM    Auth;
    DOT11_CIPHER_ALGORITHM  Cipher;
    BOOL                    SecurityEnabled;
    /* Passphrase carried opaquely for the supplicant; not consumed here. */
    ULONG                   KeyLength;          /* bytes, 0 for open */
    UCHAR                   Key[NWIFI_MAX_PSK_BYTES];
    DWORD                   Flags;              /* WLAN_PROFILE_* */
    /* The original XML, returned verbatim by WlanGetProfile. */
    LPWSTR                  Xml;
} WLANSVC_PROFILE, *PWLANSVC_PROFILE;

/* One WLAN interface (one nwifi-presented dot11 adapter). */
typedef struct _WLANSVC_INTERFACE
{
    LIST_ENTRY            ListEntry;
    GUID                  InterfaceGuid;
    /* nwifi-local identity (nwifi keys interfaces by index + LUID). */
    ULONG                 NwifiIndex;
    ULONG64               UpperLuid;
    WCHAR                 Description[WLAN_MAX_NAME_LENGTH];
    WLAN_INTERFACE_STATE  State;
    DOT11_MAC_ADDRESS     MacAddress;
    DOT11_PHY_TYPE        PhyType;
    BOOL                  RadioOn;
    BOOL                  AutoConfigEnabled;

    /* Scan/BSS cache. */
    LIST_ENTRY            BssListHead;
    ULONG                 BssCount;

    /* Profile store. */
    LIST_ENTRY            ProfileListHead;
    ULONG                 ProfileCount;

    /* Current connection (valid when State == connected/associating). */
    BOOL                  Connected;
    DOT11_SSID            ConnectedSsid;
    DOT11_MAC_ADDRESS     ConnectedBssid;
    DOT11_BSS_TYPE        ConnectedBssType;
    DOT11_AUTH_ALGORITHM  ConnectedAuth;
    DOT11_CIPHER_ALGORITHM ConnectedCipher;
    WCHAR                 ConnectedProfile[WLAN_MAX_NAME_LENGTH];
    LONG                  Rssi;
    ULONG                 LinkQuality;
    ULONG                 LinkSpeedKbps;
} WLANSVC_INTERFACE, *PWLANSVC_INTERFACE;

/* A queued notification waiting to be drained by _RpcAsyncGetNotification. */
typedef struct _WLANSVC_NOTIFICATION
{
    LIST_ENTRY ListEntry;
    DWORD      NotificationSource;
    DWORD      NotificationCode;
    GUID       InterfaceGuid;
    DWORD      dwDataSize;
    /* Inline payload (e.g. WLAN_CONNECTION_NOTIFICATION_DATA); may be 0. */
    BYTE       Data[1];
} WLANSVC_NOTIFICATION, *PWLANSVC_NOTIFICATION;

/* Per-open-handle client context. */
typedef struct _WLANSVCHANDLE
{
    LIST_ENTRY WlanSvcHandleListEntry;
    DWORD      dwClientVersion;
    /* Notification subscription (WlanRegisterNotification). */
    DWORD      dwNotifSource;
    /* Pending notifications + a signal the async getter parks on. */
    LIST_ENTRY NotificationQueue;
    HANDLE     hNotifyEvent;
} WLANSVCHANDLE, *PWLANSVCHANDLE;

/* Globals; WlanSvcLock guards all of the above. */
extern LIST_ENTRY            WlanSvcInterfaceListHead;
extern CRITICAL_SECTION      WlanSvcLock;
extern LIST_ENTRY            WlanSvcHandleListHead;

/* core.c */
VOID WlanSvcInitialize(VOID);
VOID WlanSvcCleanup(VOID);
PWLANSVC_INTERFACE WlanSvcFindInterface(const GUID *pInterfaceGuid);
PWLANSVC_INTERFACE WlanSvcFindInterfaceByIndex(ULONG NwifiIndex);
VOID WlanSvcRefreshInterfaces(VOID);
PWLANSVCHANDLE WlanSvcGetHandleEntry(WLANSVC_RPC_HANDLE ClientHandle);

/* scan.c */
DWORD WlanSvcDoScan(PWLANSVC_INTERFACE Iface, PDOT11_SSID pSsid);
DWORD WlanSvcRefreshBssCache(PWLANSVC_INTERFACE Iface);
DWORD WlanSvcApplyBssCache(PWLANSVC_INTERFACE Iface,
                           PNWIFI_BSS_LIST Results);
VOID  WlanSvcFlushBssList(PWLANSVC_INTERFACE Iface);
DWORD WlanSvcBuildAvailableNetworkList(PWLANSVC_INTERFACE Iface,
                                       DWORD dwFlags,
                                       PWLAN_AVAILABLE_NETWORK_LIST *ppList);
DWORD WlanSvcBuildBssList(PWLANSVC_INTERFACE Iface,
                          PDOT11_SSID pSsid,
                          DOT11_BSS_TYPE BssType,
                          BOOL bSecurityEnabled,
                          PWLAN_BSS_LIST *ppList,
                          PDWORD pdwSize);

/* connect.c */
DWORD WlanSvcConnect(PWLANSVC_INTERFACE Iface,
                     const PWLAN_CONNECTION_PARAMETERS pParams);
DWORD WlanSvcDisconnect(PWLANSVC_INTERFACE Iface);
VOID  WlanSvcUpdateLinkState(PWLANSVC_INTERFACE Iface);
DWORD WlanSvcQueryInterface(PWLANSVC_INTERFACE Iface,
                            WLAN_INTF_OPCODE OpCode,
                            PDWORD pdwDataSize,
                            LPBYTE *ppData,
                            PDWORD pValueType);

/* profile.c */
DWORD WlanSvcParseProfileXml(LPCWSTR Xml, PWLANSVC_PROFILE *ppProfile);
PWLANSVC_PROFILE WlanSvcFindProfile(PWLANSVC_INTERFACE Iface, LPCWSTR Name);
DWORD WlanSvcSetProfile(PWLANSVC_INTERFACE Iface, DWORD dwFlags, LPCWSTR Xml,
                        BOOL bOverwrite, PDWORD pdwReasonCode);
DWORD WlanSvcGetProfile(PWLANSVC_INTERFACE Iface, LPCWSTR Name,
                        BOOL bPlaintextKey, LPWSTR *ppXml, PDWORD pdwFlags);
DWORD WlanSvcDeleteProfile(PWLANSVC_INTERFACE Iface, LPCWSTR Name);
VOID  WlanSvcFreeProfile(PWLANSVC_PROFILE Profile);

/* notify.c */
VOID WlanSvcIndicateAcm(PWLANSVC_INTERFACE Iface, DWORD NotificationCode);
VOID WlanSvcIndicateConnection(PWLANSVC_INTERFACE Iface, DWORD NotificationCode,
                               WLAN_CONNECTION_MODE Mode, LPCWSTR ProfileName,
                               WLAN_REASON_CODE Reason);
DWORD WlanSvcDequeueNotification(PWLANSVCHANDLE Handle,
                                 PWLAN_NOTIFICATION_DATA *ppData);
VOID WlanSvcDrainHandleQueue(PWLANSVCHANDLE Handle);

/* nwifi_ioctl.c */
DWORD NwifiEnumInterfaces(PNWIFI_INTERFACE_LIST *ppList);
DWORD NwifiScan(ULONG InterfaceIndex, PDOT11_SSID pSsid, DOT11_BSS_TYPE BssType);
DWORD NwifiGetBssList(ULONG InterfaceIndex, ULONG64 UpperLuid,
                      const DOT11_MAC_ADDRESS *pMac, PNWIFI_BSS_LIST *ppList);
DWORD NwifiConnect(PNWIFI_CONNECT_REQUEST pReq);
DWORD NwifiSetKey(PNWIFI_SET_KEY pKey);
DWORD NwifiDisconnect(ULONG InterfaceIndex, ULONG64 UpperLuid,
                      const DOT11_MAC_ADDRESS *pMac);
DWORD NwifiQueryState(ULONG InterfaceIndex, ULONG64 UpperLuid,
                      const DOT11_MAC_ADDRESS *pMac, PNWIFI_LINK_STATE pState);
VOID  NwifiStartNotifyWorker(VOID);
VOID  NwifiStopNotifyWorker(VOID);
VOID  NwifiCloseControl(VOID);

#endif /* _WLANSVC_INTERNAL_H_ */
