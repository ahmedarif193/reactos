/*
 * fwpmk.h
 *
 * Windows Filtering Platform — kernel-mode management API for callout
 * drivers that need to add filters or sublayers at runtime. User-mode
 * callers use fwpuclnt.h instead; this file is the kernel counterpart.
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
#define _FWPMK_

#include <fwpmtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FWPMAPI NTAPI

/* ------------------------------------------------------------------ */
/* Engine open / close                                                 */
/* ------------------------------------------------------------------ */

NTSTATUS FWPMAPI
FwpmEngineOpen0(
  _In_opt_ const wchar_t* serverName,
  _In_     UINT32         authnService,
  _In_opt_ PVOID          authIdentity,
  _In_opt_ PVOID          session,
  _Out_    HANDLE*        engineHandle);

NTSTATUS FWPMAPI
FwpmEngineClose0(
  _In_ HANDLE engineHandle);

/* ------------------------------------------------------------------ */
/* Transactions                                                        */
/* ------------------------------------------------------------------ */

NTSTATUS FWPMAPI
FwpmTransactionBegin0(
  _In_ HANDLE engineHandle,
  _In_ UINT32 flags);

NTSTATUS FWPMAPI
FwpmTransactionCommit0(
  _In_ HANDLE engineHandle);

NTSTATUS FWPMAPI
FwpmTransactionAbort0(
  _In_ HANDLE engineHandle);

/* ------------------------------------------------------------------ */
/* Filter add / delete / get                                           */
/* ------------------------------------------------------------------ */

NTSTATUS FWPMAPI
FwpmFilterAdd0(
  _In_     HANDLE              engineHandle,
  _In_     const FWPM_FILTER0* filter,
  _In_opt_ PVOID               sd,
  _Out_opt_ UINT64*            id);

NTSTATUS FWPMAPI
FwpmFilterDeleteByKey0(
  _In_ HANDLE      engineHandle,
  _In_ const GUID* key);

NTSTATUS FWPMAPI
FwpmFilterDeleteById0(
  _In_ HANDLE engineHandle,
  _In_ UINT64 id);

NTSTATUS FWPMAPI
FwpmFilterGetByKey0(
  _In_  HANDLE        engineHandle,
  _In_  const GUID*   key,
  _Out_ FWPM_FILTER0** filter);

NTSTATUS FWPMAPI
FwpmFilterGetById0(
  _In_  HANDLE        engineHandle,
  _In_  UINT64        id,
  _Out_ FWPM_FILTER0** filter);

/* ------------------------------------------------------------------ */
/* Callout add / delete                                                */
/* ------------------------------------------------------------------ */

NTSTATUS FWPMAPI
FwpmCalloutAdd0(
  _In_     HANDLE               engineHandle,
  _In_     const FWPM_CALLOUT0* callout,
  _In_opt_ PVOID                sd,
  _Out_opt_ UINT32*             id);

NTSTATUS FWPMAPI
FwpmCalloutDeleteByKey0(
  _In_ HANDLE      engineHandle,
  _In_ const GUID* key);

NTSTATUS FWPMAPI
FwpmCalloutDeleteById0(
  _In_ HANDLE engineHandle,
  _In_ UINT32 id);

/* ------------------------------------------------------------------ */
/* Provider add / delete                                               */
/* ------------------------------------------------------------------ */

NTSTATUS FWPMAPI
FwpmProviderAdd0(
  _In_ HANDLE                engineHandle,
  _In_ const FWPM_PROVIDER0* provider,
  _In_opt_ PVOID              sd);

NTSTATUS FWPMAPI
FwpmProviderDeleteByKey0(
  _In_ HANDLE      engineHandle,
  _In_ const GUID* key);

/* ------------------------------------------------------------------ */
/* Memory management                                                   */
/* ------------------------------------------------------------------ */

VOID FWPMAPI
FwpmFreeMemory0(
  _Inout_ PVOID* p);

#ifdef __cplusplus
}
#endif

/* EOF */
