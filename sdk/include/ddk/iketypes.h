/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform IKE types
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __iketypes_h__
#define __iketypes_h__

#include <fwptypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef
enum IKEEXT_AUTHENTICATION_METHOD_TYPE_
    {
        IKEEXT_PRESHARED_KEY	= 0,
        IKEEXT_CERTIFICATE	= ( IKEEXT_PRESHARED_KEY + 1 ) ,
        IKEEXT_KERBEROS	= ( IKEEXT_CERTIFICATE + 1 ) ,
        IKEEXT_ANONYMOUS	= ( IKEEXT_KERBEROS + 1 ) ,
        IKEEXT_SSL	= ( IKEEXT_ANONYMOUS + 1 ) ,
        IKEEXT_NTLM_V2	= ( IKEEXT_SSL + 1 ) ,
        IKEEXT_IPV6_CGA	= ( IKEEXT_NTLM_V2 + 1 ) ,
        IKEEXT_CERTIFICATE_ECDSA_P256	= ( IKEEXT_IPV6_CGA + 1 ) ,
        IKEEXT_CERTIFICATE_ECDSA_P384	= ( IKEEXT_CERTIFICATE_ECDSA_P256 + 1 ) ,
        IKEEXT_SSL_ECDSA_P256	= ( IKEEXT_CERTIFICATE_ECDSA_P384 + 1 ) ,
        IKEEXT_SSL_ECDSA_P384	= ( IKEEXT_SSL_ECDSA_P256 + 1 ) ,
        IKEEXT_EAP	= ( IKEEXT_SSL_ECDSA_P384 + 1 ) ,
        IKEEXT_RESERVED	= ( IKEEXT_EAP + 1 ) ,
        IKEEXT_AUTHENTICATION_METHOD_TYPE_MAX	= ( IKEEXT_RESERVED + 1 )
    } 	IKEEXT_AUTHENTICATION_METHOD_TYPE;

typedef
enum IKEEXT_AUTHENTICATION_IMPERSONATION_TYPE_
    {
        IKEEXT_IMPERSONATION_NONE	= 0,
        IKEEXT_IMPERSONATION_SOCKET_PRINCIPAL	= ( IKEEXT_IMPERSONATION_NONE + 1 ) ,
        IKEEXT_IMPERSONATION_MAX	= ( IKEEXT_IMPERSONATION_SOCKET_PRINCIPAL + 1 )
    } 	IKEEXT_AUTHENTICATION_IMPERSONATION_TYPE;

#if ((NTDDI_VERSION >= NTDDI_WIN7))

typedef struct IKEEXT_PRESHARED_KEY_AUTHENTICATION1__
    {
    FWP_BYTE_BLOB presharedKey;
    UINT32 flags;
    } 	IKEEXT_PRESHARED_KEY_AUTHENTICATION1;

#endif

typedef
enum IKEEXT_CERT_CONFIG_TYPE_
    {
        IKEEXT_CERT_CONFIG_EXPLICIT_TRUST_LIST	= 0,
        IKEEXT_CERT_CONFIG_ENTERPRISE_STORE	= ( IKEEXT_CERT_CONFIG_EXPLICIT_TRUST_LIST + 1 ) ,
        IKEEXT_CERT_CONFIG_TRUSTED_ROOT_STORE	= ( IKEEXT_CERT_CONFIG_ENTERPRISE_STORE + 1 ) ,
        IKEEXT_CERT_CONFIG_UNSPECIFIED	= ( IKEEXT_CERT_CONFIG_TRUSTED_ROOT_STORE + 1 ) ,
        IKEEXT_CERT_CONFIG_TYPE_MAX	= ( IKEEXT_CERT_CONFIG_UNSPECIFIED + 1 )
    } 	IKEEXT_CERT_CONFIG_TYPE;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef
enum IKEEXT_CERT_CRITERIA_NAME_TYPE_
    {
        IKEEXT_CERT_CRITERIA_DNS	= 0,
        IKEEXT_CERT_CRITERIA_UPN	= ( IKEEXT_CERT_CRITERIA_DNS + 1 ) ,
        IKEEXT_CERT_CRITERIA_RFC822	= ( IKEEXT_CERT_CRITERIA_UPN + 1 ) ,
        IKEEXT_CERT_CRITERIA_CN	= ( IKEEXT_CERT_CRITERIA_RFC822 + 1 ) ,
        IKEEXT_CERT_CRITERIA_OU	= ( IKEEXT_CERT_CRITERIA_CN + 1 ) ,
        IKEEXT_CERT_CRITERIA_O	= ( IKEEXT_CERT_CRITERIA_OU + 1 ) ,
        IKEEXT_CERT_CRITERIA_DC	= ( IKEEXT_CERT_CRITERIA_O + 1 ) ,
        IKEEXT_CERT_CRITERIA_NAME_TYPE_MAX	= ( IKEEXT_CERT_CRITERIA_DC + 1 )
    } 	IKEEXT_CERT_CRITERIA_NAME_TYPE;

typedef struct IKEEXT_CERT_EKUS0_
    {
    ULONG numEku;
     LPSTR *eku;
    } 	IKEEXT_CERT_EKUS0;

typedef struct IKEEXT_CERT_NAME0_
    {
    IKEEXT_CERT_CRITERIA_NAME_TYPE nameType;
     LPWSTR certName;
    } 	IKEEXT_CERT_NAME0;

typedef struct IKEEXT_CERTIFICATE_CRITERIA0_
    {
    FWP_BYTE_BLOB certData;
    FWP_BYTE_BLOB certHash;
     IKEEXT_CERT_EKUS0 *eku;
     IKEEXT_CERT_NAME0 *name;
    UINT32 flags;
    } 	IKEEXT_CERTIFICATE_CRITERIA0;

typedef struct IKEEXT_CERTIFICATE_AUTHENTICATION2_
    {
    IKEEXT_CERT_CONFIG_TYPE inboundConfigType;
     union
        {
         struct
            {
            UINT32 inboundRootArraySize;
             IKEEXT_CERTIFICATE_CRITERIA0 *inboundRootCriteria;
            } 	;
         struct
            {
            UINT32 inboundEnterpriseStoreArraySize;
             IKEEXT_CERTIFICATE_CRITERIA0 *inboundEnterpriseStoreCriteria;
            } 	;
         struct
            {
            UINT32 inboundRootStoreArraySize;
             IKEEXT_CERTIFICATE_CRITERIA0 *inboundTrustedRootStoreCriteria;
            } 	;
        } 	;
    IKEEXT_CERT_CONFIG_TYPE outboundConfigType;
     union
        {
         struct
            {
            UINT32 outboundRootArraySize;
             IKEEXT_CERTIFICATE_CRITERIA0 *outboundRootCriteria;
            } 	;
         struct
            {
            UINT32 outboundEnterpriseStoreArraySize;
             IKEEXT_CERTIFICATE_CRITERIA0 *outboundEnterpriseStoreCriteria;
            } 	;
         struct
            {
            UINT32 outboundRootStoreArraySize;
             IKEEXT_CERTIFICATE_CRITERIA0 *outboundTrustedRootStoreCriteria;
            } 	;
        } 	;
    UINT32 flags;
    FWP_BYTE_BLOB localCertLocationUrl;
    } 	IKEEXT_CERTIFICATE_AUTHENTICATION2;

#endif

typedef struct IKEEXT_IPV6_CGA_AUTHENTICATION0_
    {
     wchar_t *keyContainerName;
     wchar_t *cspName;
    UINT32 cspType;
    FWP_BYTE_ARRAY16 cgaModifier;
    BYTE cgaCollisionCount;
    } 	IKEEXT_IPV6_CGA_AUTHENTICATION0;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct IKEEXT_KERBEROS_AUTHENTICATION1__
    {
    UINT32 flags;
     wchar_t *proxyServer;
    } 	IKEEXT_KERBEROS_AUTHENTICATION1;

#endif

typedef struct IKEEXT_RESERVED_AUTHENTICATION0__
    {
    UINT32 flags;
    } 	IKEEXT_RESERVED_AUTHENTICATION0;

typedef struct IKEEXT_NTLM_V2_AUTHENTICATION0__
    {
    UINT32 flags;
    } 	IKEEXT_NTLM_V2_AUTHENTICATION0;

#if ((NTDDI_VERSION >= NTDDI_WIN7))

typedef struct IKEEXT_EAP_AUTHENTICATION0__
    {
    UINT32 flags;
    } 	IKEEXT_EAP_AUTHENTICATION0;

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct IKEEXT_AUTHENTICATION_METHOD2_
    {
    IKEEXT_AUTHENTICATION_METHOD_TYPE authenticationMethodType;
     union
        {
         IKEEXT_PRESHARED_KEY_AUTHENTICATION1 presharedKeyAuthentication;
         IKEEXT_CERTIFICATE_AUTHENTICATION2 certificateAuthentication;
         IKEEXT_KERBEROS_AUTHENTICATION1 kerberosAuthentication;
         IKEEXT_RESERVED_AUTHENTICATION0 reservedAuthentication;
         IKEEXT_NTLM_V2_AUTHENTICATION0 ntlmV2Authentication;
         IKEEXT_CERTIFICATE_AUTHENTICATION2 sslAuthentication;
         IKEEXT_IPV6_CGA_AUTHENTICATION0 cgaAuthentication;
         IKEEXT_EAP_AUTHENTICATION0 eapAuthentication;
        } 	;
    } 	IKEEXT_AUTHENTICATION_METHOD2;

#endif

typedef
enum IKEEXT_CIPHER_TYPE_
    {
        IKEEXT_CIPHER_DES	= 0,
        IKEEXT_CIPHER_3DES	= ( IKEEXT_CIPHER_DES + 1 ) ,
        IKEEXT_CIPHER_AES_128	= ( IKEEXT_CIPHER_3DES + 1 ) ,
        IKEEXT_CIPHER_AES_192	= ( IKEEXT_CIPHER_AES_128 + 1 ) ,
        IKEEXT_CIPHER_AES_256	= ( IKEEXT_CIPHER_AES_192 + 1 ) ,
        IKEEXT_CIPHER_AES_GCM_128_16ICV	= ( IKEEXT_CIPHER_AES_256 + 1 ) ,
        IKEEXT_CIPHER_AES_GCM_256_16ICV	= ( IKEEXT_CIPHER_AES_GCM_128_16ICV + 1 ) ,
        IKEEXT_CIPHER_TYPE_MAX	= ( IKEEXT_CIPHER_AES_GCM_256_16ICV + 1 )
    } 	IKEEXT_CIPHER_TYPE;

typedef struct IKEEXT_CIPHER_ALGORITHM0_
    {
    IKEEXT_CIPHER_TYPE algoIdentifier;
    UINT32 keyLen;
    UINT32 rounds;
    } 	IKEEXT_CIPHER_ALGORITHM0;

typedef
enum IKEEXT_INTEGRITY_TYPE_
    {
        IKEEXT_INTEGRITY_MD5	= 0,
        IKEEXT_INTEGRITY_SHA1	= ( IKEEXT_INTEGRITY_MD5 + 1 ) ,
        IKEEXT_INTEGRITY_SHA_256	= ( IKEEXT_INTEGRITY_SHA1 + 1 ) ,
        IKEEXT_INTEGRITY_SHA_384	= ( IKEEXT_INTEGRITY_SHA_256 + 1 ) ,
        IKEEXT_INTEGRITY_TYPE_MAX	= ( IKEEXT_INTEGRITY_SHA_384 + 1 )
    } 	IKEEXT_INTEGRITY_TYPE;

typedef struct IKEEXT_INTEGRITY_ALGORITHM0_
    {
    IKEEXT_INTEGRITY_TYPE algoIdentifier;
    } 	IKEEXT_INTEGRITY_ALGORITHM0;

typedef
enum IKEEXT_DH_GROUP_
    {
        IKEEXT_DH_GROUP_NONE	= 0,
        IKEEXT_DH_GROUP_1	= ( IKEEXT_DH_GROUP_NONE + 1 ) ,
        IKEEXT_DH_GROUP_2	= ( IKEEXT_DH_GROUP_1 + 1 ) ,
        IKEEXT_DH_GROUP_14	= ( IKEEXT_DH_GROUP_2 + 1 ) ,
        IKEEXT_DH_GROUP_2048	= IKEEXT_DH_GROUP_14,
        IKEEXT_DH_ECP_256	= ( IKEEXT_DH_GROUP_2048 + 1 ) ,
        IKEEXT_DH_ECP_384	= ( IKEEXT_DH_ECP_256 + 1 ) ,
        IKEEXT_DH_GROUP_24	= ( IKEEXT_DH_ECP_384 + 1 ) ,
        IKEEXT_DH_GROUP_MAX	= ( IKEEXT_DH_GROUP_24 + 1 )
    } 	IKEEXT_DH_GROUP;

typedef struct IKEEXT_PROPOSAL0_
    {
    IKEEXT_CIPHER_ALGORITHM0 cipherAlgorithm;
    IKEEXT_INTEGRITY_ALGORITHM0 integrityAlgorithm;
    UINT32 maxLifetimeSeconds;
    IKEEXT_DH_GROUP dhGroup;
    UINT32 quickModeLimit;
    } 	IKEEXT_PROPOSAL0;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct IKEEXT_POLICY2_
    {
    UINT32 softExpirationTime;
    UINT32 numAuthenticationMethods;
     IKEEXT_AUTHENTICATION_METHOD2 *authenticationMethods;
    IKEEXT_AUTHENTICATION_IMPERSONATION_TYPE initiatorImpersonationType;
    UINT32 numIkeProposals;
     IKEEXT_PROPOSAL0 *ikeProposals;
    UINT32 flags;
    UINT32 maxDynamicFilters;
    UINT32 retransmitDurationSecs;
    } 	IKEEXT_POLICY2;

typedef struct IKEEXT_EM_POLICY2_
    {
    UINT32 numAuthenticationMethods;
     IKEEXT_AUTHENTICATION_METHOD2 *authenticationMethods;
    IKEEXT_AUTHENTICATION_IMPERSONATION_TYPE initiatorImpersonationType;
    } 	IKEEXT_EM_POLICY2;

#endif

#ifdef __cplusplus
}
#endif

#endif
