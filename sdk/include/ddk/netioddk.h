/*
 * netioddk.h
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Amine Khaldi (amine.khaldi@reactos.org)
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

#ifndef _NETIODDK_
#define _NETIODDK_

#pragma once

#include <netiodef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (NTDDI_VERSION >= NTDDI_VISTA)

typedef CONST struct _NPI_CLIENT_CHARACTERISTICS * PNPI_CLIENT_CHARACTERISTICS;
typedef CONST struct _NPI_PROVIDER_CHARACTERISTICS * PNPI_PROVIDER_CHARACTERISTICS;
typedef CONST struct _NPI_REGISTRATION_INSTANCE * PNPI_REGISTRATION_INSTANCE;

typedef struct _NPI_REGISTRATION_INSTANCE {
  USHORT Version;
  USHORT Size;
  PNPIID NpiId;
  PNPI_MODULEID ModuleId;
  ULONG Number;
  CONST VOID *NpiSpecificCharacteristics OPTIONAL;
} NPI_REGISTRATION_INSTANCE;

typedef struct _NPI {
  HANDLE Handle;
  CONST VOID* Dispatch;
} NPI;

typedef NTSTATUS
(NTAPI NPI_CLIENT_ATTACH_PROVIDER_FN)(
  _In_ HANDLE NmrBindingHandle,
  _In_ PVOID ClientContext,
  _In_ PNPI_REGISTRATION_INSTANCE ProviderRegistrationInstance);
typedef NPI_CLIENT_ATTACH_PROVIDER_FN *PNPI_CLIENT_ATTACH_PROVIDER_FN;

typedef NTSTATUS
(NTAPI NPI_CLIENT_DETACH_PROVIDER_FN  )(
  _In_ PVOID ClientBindingContext);
typedef NPI_CLIENT_DETACH_PROVIDER_FN *PNPI_CLIENT_DETACH_PROVIDER_FN;

typedef VOID
(NTAPI NPI_CLIENT_CLEANUP_BINDING_CONTEXT_FN)(
  _In_ PVOID ClientBindingContext);
typedef NPI_CLIENT_CLEANUP_BINDING_CONTEXT_FN *PNPI_CLIENT_CLEANUP_BINDING_CONTEXT_FN;

typedef struct _NPI_CLIENT_CHARACTERISTICS {
  USHORT Version;
  USHORT Length;
  PNPI_CLIENT_ATTACH_PROVIDER_FN ClientAttachProvider;
  PNPI_CLIENT_DETACH_PROVIDER_FN ClientDetachProvider;
  PNPI_CLIENT_CLEANUP_BINDING_CONTEXT_FN ClientCleanupBindingContext;
  NPI_REGISTRATION_INSTANCE ClientRegistrationInstance;
} NPI_CLIENT_CHARACTERISTICS;

typedef NTSTATUS
(NTAPI NPI_PROVIDER_ATTACH_CLIENT_FN)(
  _In_ HANDLE NmrBindingHandle,
  _In_ PVOID ProviderContext,
  _In_ PNPI_REGISTRATION_INSTANCE ClientRegistrationInstance,
  _In_ PVOID ClientBindingContext,
  _In_ CONST VOID *ClientDispatch,
  _Out_ PVOID *ProviderBindingContext,
  _Out_ CONST VOID* *ProviderDispatch);
typedef NPI_PROVIDER_ATTACH_CLIENT_FN *PNPI_PROVIDER_ATTACH_CLIENT_FN;

typedef NTSTATUS
(NTAPI NPI_PROVIDER_DETACH_CLIENT_FN)(
  _In_ PVOID ProviderBindingContext);
typedef NPI_PROVIDER_DETACH_CLIENT_FN *PNPI_PROVIDER_DETACH_CLIENT_FN;

typedef VOID
(NTAPI NPI_PROVIDER_CLEANUP_BINDING_CONTEXT_FN)(
  _In_ PVOID ProviderBindingContext);
typedef NPI_PROVIDER_CLEANUP_BINDING_CONTEXT_FN *PNPI_PROVIDER_CLEANUP_BINDING_CONTEXT_FN;

typedef struct _NPI_PROVIDER_CHARACTERISTICS {
  USHORT Version;
  USHORT Length;
  PNPI_PROVIDER_ATTACH_CLIENT_FN ProviderAttachClient;
  PNPI_PROVIDER_DETACH_CLIENT_FN ProviderDetachClient;
  PNPI_PROVIDER_CLEANUP_BINDING_CONTEXT_FN ProviderCleanupBindingContext;
  NPI_REGISTRATION_INSTANCE ProviderRegistrationInstance;
} NPI_PROVIDER_CHARACTERISTICS;

NTSTATUS
NTAPI
NmrRegisterClient(
  _In_ PNPI_CLIENT_CHARACTERISTICS ClientCharacteristics,
  _In_opt_ __drv_aliasesMem PVOID ClientContext,
  _Out_ PHANDLE NmrClientHandle);

NTSTATUS
NTAPI
NmrDeregisterClient(
  _In_ HANDLE NmrClientHandle);

NTSTATUS
NTAPI
NmrWaitForClientDeregisterComplete(
  _In_ HANDLE NmrClientHandle);

NTSTATUS
NTAPI
NmrClientAttachProvider(
  _In_ HANDLE NmrBindingHandle,
  _In_ __drv_aliasesMem PVOID ClientBindingContext,
  _In_ CONST VOID *ClientDispatch,
  _Out_ PVOID *ProviderBindingContext,
  _Out_ CONST VOID* *ProviderDispatch);

VOID
NTAPI
NmrClientDetachProviderComplete(
  _In_ HANDLE NmrBindingHandle);

NTSTATUS
NTAPI
NmrRegisterProvider(
  _In_ PNPI_PROVIDER_CHARACTERISTICS ProviderCharacteristics,
  _In_opt_ __drv_aliasesMem PVOID ProviderContext,
  _Out_ PHANDLE NmrProviderHandle);

NTSTATUS
NTAPI
NmrDeregisterProvider(
  _In_ HANDLE NmrProviderHandle);

NTSTATUS
NTAPI
NmrWaitForProviderDeregisterComplete(
  _In_ HANDLE NmrProviderHandle);

VOID
NTAPI
NmrProviderDetachClientComplete(
  _In_ HANDLE NmrBindingHandle);

#endif /* (NTDDI_VERSION >= NTDDI_VISTA) */


/* ============================================================================
 *  Kernel-mode counterparts of the user-mode netioapi.h v2 API family.
 *  Added on the dev-nt6-1 branch so kernel callers (WFP callouts, NDIS 6
 *  protocol drivers) can use the same interface identifier helpers without
 *  pulling in the user-mode psdk netioapi.h.
 *
 *  These forward to the same implementation that backs user-mode iphlpapi.
 *  At this stage the calls are stubs; wire them up properly in Phase F/G.
 * ============================================================================ */
#if (NTDDI_VERSION >= NTDDI_VISTA)

/* MIB_IF_ROW2 and friends already live in the psdk netioapi.h, which is
 * designed to be includable from kernel code too (it only needs ifdef.h
 * and nldef.h). Kernel consumers should include <netioapi.h> directly
 * via the psdk search path after including <ws2ipdef.h>. The declarations
 * below cover just the helpers that the kernel-mode netio.sys exports
 * for its own clients. */

/* ConvertInterface* helpers — these exist in user-mode iphlpapi and in
 * kernel-mode netio.sys with identical signatures. Declare them here so
 * kernel callers don't have to include the psdk header. */
NTSTATUS
NTAPI
ConvertInterfaceLuidToIndexKernel(
  _In_  CONST ULONG64 *InterfaceLuid,
  _Out_ PULONG         InterfaceIndex);

NTSTATUS
NTAPI
ConvertInterfaceIndexToLuidKernel(
  _In_  ULONG    InterfaceIndex,
  _Out_ ULONG64* InterfaceLuid);

/* FreeMibTable — symmetric counterpart to user-mode FreeMibTable */
VOID
NTAPI
FreeMibTableKernel(
  _Inout_ PVOID Table);

#endif /* (NTDDI_VERSION >= NTDDI_VISTA) */

#ifdef __cplusplus
}
#endif

#endif /* _NETIODDK_ */
