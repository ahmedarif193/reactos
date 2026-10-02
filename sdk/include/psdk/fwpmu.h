/*
 * PROJECT:     LiberNT PSDK headers
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform user-mode management API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once
#define _FWPMU_

#ifdef _KRPCENV_
#define FWPMU_KRPCENV_DEFINED
#endif

#ifdef GUID_DEFS_ONLY
#include <fwpmk.h>
#else
#define GUID_DEFS_ONLY
#include <fwpmk.h>
#undef GUID_DEFS_ONLY
#endif

#ifndef FWPMU_KRPCENV_DEFINED
#undef _KRPCENV_
#endif
#undef FWPMU_KRPCENV_DEFINED

#ifndef GUID_DEFS_ONLY

#ifdef __cplusplus
extern "C" {
#endif

#define FWPM_TXN_READ_ONLY (0x00000001)

void
WINAPI
FwpmFreeMemory0(
    _Inout_ void **p);

DWORD
WINAPI
FwpmEngineOpen0(
    _In_opt_ const wchar_t *serverName,
    _In_ UINT32 authnService,
    _In_opt_ SEC_WINNT_AUTH_IDENTITY_W *authIdentity,
    _In_opt_ const FWPM_SESSION0 *session,
    _Out_ HANDLE *engineHandle);

DWORD
WINAPI
FwpmEngineClose0(
    _Inout_ HANDLE engineHandle);

DWORD
WINAPI
FwpmTransactionBegin0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 flags);

DWORD
WINAPI
FwpmTransactionCommit0(
    _In_ HANDLE engineHandle);

DWORD
WINAPI
FwpmTransactionAbort0(
    _In_ HANDLE engineHandle);

DWORD
WINAPI
FwpmProviderAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_PROVIDER0 *provider,
    _In_opt_ PSECURITY_DESCRIPTOR sd);

DWORD
WINAPI
FwpmProviderDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmSubLayerAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_SUBLAYER0 *subLayer,
    _In_opt_ PSECURITY_DESCRIPTOR sd);

DWORD
WINAPI
FwpmSubLayerDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmSubLayerGetByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key,
    _Outptr_ FWPM_SUBLAYER0 **subLayer);

DWORD
WINAPI
FwpmCalloutAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_CALLOUT0 *callout,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT32 *id);

DWORD
WINAPI
FwpmCalloutDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 id);

DWORD
WINAPI
FwpmCalloutDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmFilterAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_FILTER0 *filter,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT64 *id);

DWORD
WINAPI
FwpmFilterDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT64 id);

DWORD
WINAPI
FwpmFilterDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key);

DWORD
WINAPI
FwpmGetAppIdFromFileName0(
    _In_ PCWSTR fileName,
    _Outptr_ FWP_BYTE_BLOB **appId);

#ifdef __cplusplus
}
#endif

#endif
