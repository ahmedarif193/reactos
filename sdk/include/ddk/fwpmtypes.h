/*
 * fwpmtypes.h
 *
 * Windows Filtering Platform — management types shared by the kernel-side
 * management API (fwpmk.h) and, eventually, the user-mode fwpuclnt.h.
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Created for the dev-nt6-1 branch as part of the NT 5.2 -> NT 6.1 upgrade.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#pragma once
#define _FWPMTYPES_

#include <fwptypes.h>
#include <iketypes.h>
#include <ipsectypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef
enum FWPM_SERVICE_STATE_
    {
        FWPM_SERVICE_STOPPED	= 0,
        FWPM_SERVICE_START_PENDING	= ( FWPM_SERVICE_STOPPED + 1 ) ,
        FWPM_SERVICE_STOP_PENDING	= ( FWPM_SERVICE_START_PENDING + 1 ) ,
        FWPM_SERVICE_RUNNING	= ( FWPM_SERVICE_STOP_PENDING + 1 ) ,
        FWPM_SERVICE_STATE_MAX	= ( FWPM_SERVICE_RUNNING + 1 )
    } 	FWPM_SERVICE_STATE;

#define FWPM_SESSION_FLAG_DYNAMIC (0x00000001)

#if ((NTDDI_VERSION >= NTDDI_WIN7))

#define FWPM_SESSION_FLAG_RESERVED (0x10000000)

#endif

typedef struct FWPM_SESSION0_
    {
    GUID sessionKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32 flags;
    UINT32 txnWaitTimeoutInMSec;
    DWORD processId;
     SID *sid;
     wchar_t *username;
    BOOL kernelMode;
    } 	FWPM_SESSION0;

#define FWPM_PROVIDER_FLAG_PERSISTENT (0x00000001)
#define FWPM_PROVIDER_FLAG_DISABLED (0x00000010)

typedef struct FWPM_PROVIDER0_
    {
    GUID providerKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32 flags;
    FWP_BYTE_BLOB providerData;
     wchar_t *serviceName;
    } 	FWPM_PROVIDER0;

typedef struct FWPM_CLASSIFY_OPTION0_
    {
    FWP_CLASSIFY_OPTION_TYPE type;
    FWP_VALUE0 value;
    } 	FWPM_CLASSIFY_OPTION0;

typedef struct FWPM_CLASSIFY_OPTIONS0_
    {
    UINT32 numOptions;
     FWPM_CLASSIFY_OPTION0 *options;
    } 	FWPM_CLASSIFY_OPTIONS0;

typedef struct FWPM_NETWORK_CONNECTION_POLICY_SETTING0_
    {
    FWP_NETWORK_CONNECTION_POLICY_SETTING_TYPE type;
    FWP_VALUE0 value;
    } 	FWPM_NETWORK_CONNECTION_POLICY_SETTING0;

typedef struct FWPM_NETWORK_CONNECTION_POLICY_SETTINGS0_
    {
    UINT32 numSettings;
     FWPM_NETWORK_CONNECTION_POLICY_SETTING0 *settings;
    } 	FWPM_NETWORK_CONNECTION_POLICY_SETTINGS0;

typedef
enum FWPM_PROVIDER_CONTEXT_TYPE_
    {
        FWPM_IPSEC_KEYING_CONTEXT	= 0,
        FWPM_IPSEC_IKE_QM_TRANSPORT_CONTEXT	= ( FWPM_IPSEC_KEYING_CONTEXT + 1 ) ,
        FWPM_IPSEC_IKE_QM_TUNNEL_CONTEXT	= ( FWPM_IPSEC_IKE_QM_TRANSPORT_CONTEXT + 1 ) ,
        FWPM_IPSEC_AUTHIP_QM_TRANSPORT_CONTEXT	= ( FWPM_IPSEC_IKE_QM_TUNNEL_CONTEXT + 1 ) ,
        FWPM_IPSEC_AUTHIP_QM_TUNNEL_CONTEXT	= ( FWPM_IPSEC_AUTHIP_QM_TRANSPORT_CONTEXT + 1 ) ,
        FWPM_IPSEC_IKE_MM_CONTEXT	= ( FWPM_IPSEC_AUTHIP_QM_TUNNEL_CONTEXT + 1 ) ,
        FWPM_IPSEC_AUTHIP_MM_CONTEXT	= ( FWPM_IPSEC_IKE_MM_CONTEXT + 1 ) ,
        FWPM_CLASSIFY_OPTIONS_CONTEXT	= ( FWPM_IPSEC_AUTHIP_MM_CONTEXT + 1 ) ,
        FWPM_GENERAL_CONTEXT	= ( FWPM_CLASSIFY_OPTIONS_CONTEXT + 1 ) ,
        FWPM_IPSEC_IKEV2_QM_TUNNEL_CONTEXT	= ( FWPM_GENERAL_CONTEXT + 1 ) ,
        FWPM_IPSEC_IKEV2_MM_CONTEXT	= ( FWPM_IPSEC_IKEV2_QM_TUNNEL_CONTEXT + 1 ) ,
        FWPM_IPSEC_DOSP_CONTEXT	= ( FWPM_IPSEC_IKEV2_MM_CONTEXT + 1 ) ,
        FWPM_IPSEC_IKEV2_QM_TRANSPORT_CONTEXT	= ( FWPM_IPSEC_DOSP_CONTEXT + 1 ) ,
        FWPM_NETWORK_CONNECTION_POLICY_CONTEXT	= ( FWPM_IPSEC_IKEV2_QM_TRANSPORT_CONTEXT + 1 ) ,
        FWPM_PROVIDER_CONTEXT_TYPE_MAX	= ( FWPM_NETWORK_CONNECTION_POLICY_CONTEXT + 1 )
    } 	FWPM_PROVIDER_CONTEXT_TYPE;

#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

typedef struct FWPM_PROVIDER_CONTEXT3_
    {
    GUID providerContextKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32 flags;
     GUID *providerKey;
    FWP_BYTE_BLOB providerData;
    FWPM_PROVIDER_CONTEXT_TYPE type;
     union
        {
         IPSEC_KEYING_POLICY1 *keyingPolicy;
         IPSEC_TRANSPORT_POLICY2 *ikeQmTransportPolicy;
         IPSEC_TUNNEL_POLICY3 *ikeQmTunnelPolicy;
         IPSEC_TRANSPORT_POLICY2 *authipQmTransportPolicy;
         IPSEC_TUNNEL_POLICY3 *authipQmTunnelPolicy;
         IKEEXT_POLICY2 *ikeMmPolicy;
         IKEEXT_POLICY2 *authIpMmPolicy;
         FWP_BYTE_BLOB *dataBuffer;
         FWPM_CLASSIFY_OPTIONS0 *classifyOptions;
         IPSEC_TUNNEL_POLICY3 *ikeV2QmTunnelPolicy;
         IPSEC_TRANSPORT_POLICY2 *ikeV2QmTransportPolicy;
         IKEEXT_POLICY2 *ikeV2MmPolicy;
         IPSEC_DOSP_OPTIONS0 *idpOptions;
         FWPM_NETWORK_CONNECTION_POLICY_SETTINGS0 *networkConnectionPolicy;
        } 	;
    UINT64 providerContextId;
    } 	FWPM_PROVIDER_CONTEXT3;

#endif

#define FWPM_SUBLAYER_FLAG_PERSISTENT (0x00000001)

typedef struct FWPM_SUBLAYER0_
    {
    GUID subLayerKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32 flags;
     GUID *providerKey;
    FWP_BYTE_BLOB providerData;
    UINT16 weight;
    } 	FWPM_SUBLAYER0;

#define FWPM_CALLOUT_FLAG_PERSISTENT (0x00010000)
#define FWPM_CALLOUT_FLAG_USES_PROVIDER_CONTEXT (0x00020000)
#define FWPM_CALLOUT_FLAG_REGISTERED (0x00040000)

typedef struct FWPM_CALLOUT0_
    {
    GUID calloutKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32 flags;
     GUID *providerKey;
    FWP_BYTE_BLOB providerData;
    GUID applicableLayer;
    UINT32 calloutId;
    } 	FWPM_CALLOUT0;

typedef struct FWPM_ACTION0_
    {
    FWP_ACTION_TYPE type;
      union
        {
         GUID filterType;
         GUID calloutKey;
        } 	;
    } 	FWPM_ACTION0;

typedef struct FWPM_FILTER_CONDITION0_
    {
    GUID fieldKey;
    FWP_MATCH_TYPE matchType;
    FWP_CONDITION_VALUE0 conditionValue;
    } 	FWPM_FILTER_CONDITION0;

#define FWPM_FILTER_FLAG_NONE (0x00000000)
#define FWPM_FILTER_FLAG_PERSISTENT (0x00000001)
#define FWPM_FILTER_FLAG_BOOTTIME (0x00000002)
#define FWPM_FILTER_FLAG_HAS_PROVIDER_CONTEXT (0x00000004)
#define FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT (0x00000008)
#define FWPM_FILTER_FLAG_PERMIT_IF_CALLOUT_UNREGISTERED (0x00000010)
#define FWPM_FILTER_FLAG_DISABLED (0x00000020)
#define FWPM_FILTER_FLAG_INDEXED (0x00000040)
#define FWPM_FILTER_FLAG_HAS_SECURITY_REALM_PROVIDER_CONTEXT (0x00000080)
#define FWPM_FILTER_FLAG_SYSTEMOS_ONLY (0x00000100)
#define FWPM_FILTER_FLAG_GAMEOS_ONLY (0x00000200)
#define FWPM_FILTER_FLAG_SILENT_MODE (0x00000400)
#define FWPM_FILTER_FLAG_IPSEC_NO_ACQUIRE_INITIATE (0x00000800)

typedef struct FWPM_FILTER0_
    {
    GUID filterKey;
    FWPM_DISPLAY_DATA0 displayData;
    UINT32 flags;
     GUID *providerKey;
    FWP_BYTE_BLOB providerData;
    GUID layerKey;
    GUID subLayerKey;
    FWP_VALUE0 weight;
    UINT32 numFilterConditions;
     FWPM_FILTER_CONDITION0 *filterCondition;
    FWPM_ACTION0 action;
      union
        {
         UINT64 rawContext;
         GUID providerContextKey;
        } 	;
     GUID *reserved;
    UINT64 filterId;
    FWP_VALUE0 effectiveWeight;
    } 	FWPM_FILTER0;

#ifdef __cplusplus
}
#endif
