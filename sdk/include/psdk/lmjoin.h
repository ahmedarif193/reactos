/*
 * Copyright 2005 Ulrich Czekalla (For CodeWeavers)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_LMJOIN_H
#define __WINE_LMJOIN_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __WINCRYPT_H__
typedef const struct _CERT_CONTEXT *PCCERT_CONTEXT;
#endif

typedef enum tagNETSETUP_JOIN_STATUS
{
    NetSetupUnknownStatus = 0,
    NetSetupUnjoined,
    NetSetupWorkgroupName,
    NetSetupDomainName
} NETSETUP_JOIN_STATUS, *PNETSETUP_JOIN_STATUS;

#ifdef __REACTOS__
#define NETSETUP_JOIN_DOMAIN              0x00000001
#define NETSETUP_ACCT_CREATE              0x00000002
#define NETSETUP_ACCT_DELETE              0x00000004
#define NETSETUP_WIN9X_UPGRADE            0x00000010
#define NETSETUP_DOMAIN_JOIN_IF_JOINED    0x00000020
#define NETSETUP_JOIN_UNSECURE            0x00000040
#define NETSETUP_MACHINE_PWD_PASSED       0x00000080
#define NETSETUP_DEFER_SPN_SET            0x00000100
#define NETSETUP_JOIN_DC_ACCOUNT          0x00000200
#define NETSETUP_JOIN_WITH_NEW_NAME       0x00000400
#define NETSETUP_INSTALL_INVOCATION       0x00040000
#define NETSETUP_IGNORE_UNSUPPORTED_FLAGS 0x10000000

#define NETSETUP_VALID_UNJOIN_FLAGS      (NETSETUP_ACCT_DELETE | \
                                          NETSETUP_JOIN_DC_ACCOUNT | \
                                          NETSETUP_IGNORE_UNSUPPORTED_FLAGS)

NET_API_STATUS NET_API_FUNCTION NetJoinDomain(
    _In_opt_ LPCWSTR lpServer,
    _In_ LPCWSTR lpDomain,
    _In_opt_ LPCWSTR lpAccountOU,
    _In_opt_ LPCWSTR lpAccount,
    _In_opt_ LPCWSTR lpPassword,
    _In_ DWORD fJoinOptions);

NET_API_STATUS NET_API_FUNCTION NetUnjoinDomain(
    _In_opt_ LPCWSTR lpServer,
    _In_opt_ LPCWSTR lpAccount,
    _In_opt_ LPCWSTR lpPassword,
    _In_ DWORD fUnjoinOptions);
#endif

typedef enum _DSREG_JOIN_TYPE
{
    DSREG_UNKNOWN_JOIN = 0,
    DSREG_DEVICE_JOIN = 1,
    DSREG_WORKPLACE_JOIN = 2
} DSREG_JOIN_TYPE, *PDSREG_JOIN_TYPE;

typedef struct _DSREG_USER_INFO
{
    LPWSTR pszUserEmail;
    LPWSTR pszUserKeyId;
    LPWSTR pszUserKeyName;
} DSREG_USER_INFO, *PDSREG_USER_INFO;

typedef struct _DSREG_JOIN_INFO
{
    DSREG_JOIN_TYPE joinType;
    PCCERT_CONTEXT pJoinCertificate;
    LPWSTR pszDeviceId;
    LPWSTR pszIdpDomain;
    LPWSTR pszTenantId;
    LPWSTR pszJoinUserEmail;
    LPWSTR pszTenantDisplayName;
    LPWSTR pszMdmEnrollmentUrl;
    LPWSTR pszMdmTermsOfUseUrl;
    LPWSTR pszMdmComplianceUrl;
    LPWSTR pszUserSettingSyncUrl;
    DSREG_USER_INFO *pUserInfo;
} DSREG_JOIN_INFO, *PDSREG_JOIN_INFO;

NET_API_STATUS NET_API_FUNCTION NetGetJoinInformation(
    LPCWSTR Server,
    LPWSTR *Name,
    PNETSETUP_JOIN_STATUS type);

HRESULT NET_API_FUNCTION NetGetAadJoinInformation(
    LPCWSTR pcszTenantId,
    PDSREG_JOIN_INFO *ppJoinInfo);

void NET_API_FUNCTION NetFreeAadJoinInformation(
    DSREG_JOIN_INFO *join_info);

#ifdef __cplusplus
}
#endif

#endif
