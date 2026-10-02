/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform IPsec types
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __ipsectypes_h__
#define __ipsectypes_h__

#include <fwptypes.h>
#include <iketypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FWPM_FILTER0_ FWPM_FILTER0;

typedef struct IPSEC_SA_LIFETIME0_
    {
    UINT32 lifetimeSeconds;
    UINT32 lifetimeKilobytes;
    UINT32 lifetimePackets;
    } 	IPSEC_SA_LIFETIME0;

typedef
enum IPSEC_TRANSFORM_TYPE_
    {
        IPSEC_TRANSFORM_AH	= 1,
        IPSEC_TRANSFORM_ESP_AUTH	= ( IPSEC_TRANSFORM_AH + 1 ) ,
        IPSEC_TRANSFORM_ESP_CIPHER	= ( IPSEC_TRANSFORM_ESP_AUTH + 1 ) ,
        IPSEC_TRANSFORM_ESP_AUTH_AND_CIPHER	= ( IPSEC_TRANSFORM_ESP_CIPHER + 1 ) ,
        IPSEC_TRANSFORM_ESP_AUTH_FW	= ( IPSEC_TRANSFORM_ESP_AUTH_AND_CIPHER + 1 ) ,
        IPSEC_TRANSFORM_TYPE_MAX	= ( IPSEC_TRANSFORM_ESP_AUTH_FW + 1 )
    } 	IPSEC_TRANSFORM_TYPE;

typedef
enum IPSEC_AUTH_TYPE_
    {
        IPSEC_AUTH_MD5	= 0,
        IPSEC_AUTH_SHA_1	= ( IPSEC_AUTH_MD5 + 1 ) ,
        IPSEC_AUTH_SHA_256	= ( IPSEC_AUTH_SHA_1 + 1 ) ,
        IPSEC_AUTH_AES_128	= ( IPSEC_AUTH_SHA_256 + 1 ) ,
        IPSEC_AUTH_AES_192	= ( IPSEC_AUTH_AES_128 + 1 ) ,
        IPSEC_AUTH_AES_256	= ( IPSEC_AUTH_AES_192 + 1 ) ,
        IPSEC_AUTH_MAX	= ( IPSEC_AUTH_AES_256 + 1 )
    } 	IPSEC_AUTH_TYPE;

typedef UINT8 IPSEC_AUTH_CONFIG;

typedef struct IPSEC_AUTH_TRANSFORM_ID0_
    {
    IPSEC_AUTH_TYPE authType;
    IPSEC_AUTH_CONFIG authConfig;
    } 	IPSEC_AUTH_TRANSFORM_ID0;

typedef GUID IPSEC_CRYPTO_MODULE_ID;

typedef struct IPSEC_AUTH_TRANSFORM0_
    {
    IPSEC_AUTH_TRANSFORM_ID0 authTransformId;
     IPSEC_CRYPTO_MODULE_ID *cryptoModuleId;
    } 	IPSEC_AUTH_TRANSFORM0;

typedef
enum IPSEC_CIPHER_TYPE_
    {
        IPSEC_CIPHER_TYPE_DES	= 1,
        IPSEC_CIPHER_TYPE_3DES	= ( IPSEC_CIPHER_TYPE_DES + 1 ) ,
        IPSEC_CIPHER_TYPE_AES_128	= ( IPSEC_CIPHER_TYPE_3DES + 1 ) ,
        IPSEC_CIPHER_TYPE_AES_192	= ( IPSEC_CIPHER_TYPE_AES_128 + 1 ) ,
        IPSEC_CIPHER_TYPE_AES_256	= ( IPSEC_CIPHER_TYPE_AES_192 + 1 ) ,
        IPSEC_CIPHER_TYPE_MAX	= ( IPSEC_CIPHER_TYPE_AES_256 + 1 )
    } 	IPSEC_CIPHER_TYPE;

typedef UINT8 IPSEC_CIPHER_CONFIG;

typedef struct IPSEC_CIPHER_TRANSFORM_ID0_
    {
    IPSEC_CIPHER_TYPE cipherType;
    IPSEC_CIPHER_CONFIG cipherConfig;
    } 	IPSEC_CIPHER_TRANSFORM_ID0;

typedef struct IPSEC_CIPHER_TRANSFORM0_
    {
    IPSEC_CIPHER_TRANSFORM_ID0 cipherTransformId;
     IPSEC_CRYPTO_MODULE_ID *cryptoModuleId;
    } 	IPSEC_CIPHER_TRANSFORM0;

typedef struct IPSEC_AUTH_AND_CIPHER_TRANSFORM0_
    {
    IPSEC_AUTH_TRANSFORM0 authTransform;
    IPSEC_CIPHER_TRANSFORM0 cipherTransform;
    } 	IPSEC_AUTH_AND_CIPHER_TRANSFORM0;

typedef struct IPSEC_SA_TRANSFORM0_
    {
    IPSEC_TRANSFORM_TYPE ipsecTransformType;
     union
        {
         IPSEC_AUTH_TRANSFORM0 *ahTransform;
         IPSEC_AUTH_TRANSFORM0 *espAuthTransform;
         IPSEC_CIPHER_TRANSFORM0 *espCipherTransform;
         IPSEC_AUTH_AND_CIPHER_TRANSFORM0 *espAuthAndCipherTransform;
         IPSEC_AUTH_TRANSFORM0 *espAuthFwTransform;
        } 	;
    } 	IPSEC_SA_TRANSFORM0;

typedef
enum IPSEC_PFS_GROUP_
    {
        IPSEC_PFS_NONE	= 0,
        IPSEC_PFS_1	= ( IPSEC_PFS_NONE + 1 ) ,
        IPSEC_PFS_2	= ( IPSEC_PFS_1 + 1 ) ,
        IPSEC_PFS_2048	= ( IPSEC_PFS_2 + 1 ) ,
        IPSEC_PFS_14	= IPSEC_PFS_2048,
        IPSEC_PFS_ECP_256	= ( IPSEC_PFS_14 + 1 ) ,
        IPSEC_PFS_ECP_384	= ( IPSEC_PFS_ECP_256 + 1 ) ,
        IPSEC_PFS_MM	= ( IPSEC_PFS_ECP_384 + 1 ) ,
        IPSEC_PFS_24	= ( IPSEC_PFS_MM + 1 ) ,
        IPSEC_PFS_MAX	= ( IPSEC_PFS_24 + 1 )
    } 	IPSEC_PFS_GROUP;

typedef struct IPSEC_PROPOSAL0_
    {
    IPSEC_SA_LIFETIME0 lifetime;
    UINT32 numSaTransforms;
     IPSEC_SA_TRANSFORM0 *saTransforms;
    IPSEC_PFS_GROUP pfsGroup;
    } 	IPSEC_PROPOSAL0;

typedef struct IPSEC_SA_IDLE_TIMEOUT0_
    {
    UINT32 idleTimeoutSeconds;
    UINT32 idleTimeoutSecondsFailOver;
    } 	IPSEC_SA_IDLE_TIMEOUT0;

typedef struct IPSEC_TRAFFIC_SELECTOR0_
    {
    UINT8 protocolId;
    UINT16 portStart;
    UINT16 portEnd;
    FWP_IP_VERSION ipVersion;
     union
        {
         UINT32 startV4Address;
         UINT8 startV6Address[ 16 ];
        } 	;
     union
        {
         UINT32 endV4Address;
         UINT8 endV6Address[ 16 ];
        } 	;
    } 	IPSEC_TRAFFIC_SELECTOR0;

typedef struct IPSEC_TRAFFIC_SELECTOR_POLICY0_
    {
    UINT32 flags;
    UINT32 numLocalTrafficSelectors;
     IPSEC_TRAFFIC_SELECTOR0 *localTrafficSelectors;
    UINT32 numRemoteTrafficSelectors;
     IPSEC_TRAFFIC_SELECTOR0 *remoteTrafficSelectors;
    } 	IPSEC_TRAFFIC_SELECTOR_POLICY0;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct IPSEC_TRANSPORT_POLICY2_
    {
    UINT32 numIpsecProposals;
     IPSEC_PROPOSAL0 *ipsecProposals;
    UINT32 flags;
    UINT32 ndAllowClearTimeoutSeconds;
    IPSEC_SA_IDLE_TIMEOUT0 saIdleTimeout;
     IKEEXT_EM_POLICY2 *emPolicy;
    } 	IPSEC_TRANSPORT_POLICY2;

typedef struct IPSEC_TUNNEL_ENDPOINT0_
    {
    FWP_IP_VERSION ipVersion;
     union
        {
         UINT32 v4Address;
         UINT8 v6Address[ 16 ];
        } 	;
    } 	IPSEC_TUNNEL_ENDPOINT0;

typedef struct IPSEC_TUNNEL_ENDPOINTS2_
    {
    FWP_IP_VERSION ipVersion;
     union
        {
         UINT32 localV4Address;
         UINT8 localV6Address[ 16 ];
        } 	;
     union
        {
         UINT32 remoteV4Address;
         UINT8 remoteV6Address[ 16 ];
        } 	;
    UINT64 localIfLuid;
     wchar_t *remoteFqdn;
    UINT32 numAddresses;
     IPSEC_TUNNEL_ENDPOINT0 *remoteAddresses;
    } 	IPSEC_TUNNEL_ENDPOINTS2;

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3))

typedef struct IPSEC_TUNNEL_POLICY3_
    {
    UINT32 flags;
    UINT32 numIpsecProposals;
     IPSEC_PROPOSAL0 *ipsecProposals;
    IPSEC_TUNNEL_ENDPOINTS2 tunnelEndpoints;
    IPSEC_SA_IDLE_TIMEOUT0 saIdleTimeout;
     IKEEXT_EM_POLICY2 *emPolicy;
    UINT32 fwdPathSaLifetime;
    UINT32 compartmentId;
    UINT32 numTrafficSelectorPolicy;
     IPSEC_TRAFFIC_SELECTOR_POLICY0 *trafficSelectorPolicies;
    } 	IPSEC_TUNNEL_POLICY3;

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct IPSEC_KEYING_POLICY1_
    {
    UINT32 numKeyMods;
     GUID *keyModKeys;
    UINT32 flags;
    } 	IPSEC_KEYING_POLICY1;

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN7))

typedef struct IPSEC_DOSP_OPTIONS0_
    {
    UINT32 stateIdleTimeoutSeconds;
    UINT32 perIPRateLimitQueueIdleTimeoutSeconds;
    UINT8 ipV6IPsecUnauthDscp;
    UINT32 ipV6IPsecUnauthRateLimitBytesPerSec;
    UINT32 ipV6IPsecUnauthPerIPRateLimitBytesPerSec;
    UINT8 ipV6IPsecAuthDscp;
    UINT32 ipV6IPsecAuthRateLimitBytesPerSec;
    UINT8 icmpV6Dscp;
    UINT32 icmpV6RateLimitBytesPerSec;
    UINT8 ipV6FilterExemptDscp;
    UINT32 ipV6FilterExemptRateLimitBytesPerSec;
    UINT8 defBlockExemptDscp;
    UINT32 defBlockExemptRateLimitBytesPerSec;
    UINT32 maxStateEntries;
    UINT32 maxPerIPRateLimitQueues;
    UINT32 flags;
    UINT32 numPublicIFLuids;
     UINT64 *publicIFLuids;
    UINT32 numInternalIFLuids;
     UINT64 *internalIFLuids;
    FWP_V6_ADDR_AND_MASK publicV6AddrMask;
    FWP_V6_ADDR_AND_MASK internalV6AddrMask;
    } 	IPSEC_DOSP_OPTIONS0;

#endif

#ifdef __cplusplus
}
#endif

#endif
