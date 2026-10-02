/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Platform runtime mechanism interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _PRMINTERFACE_H_
#define _PRMINTERFACE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef
_Function_class_(PRM_HANDLER_ROUTINE)
NTSTATUS
PRM_HANDLER_ROUTINE (
    _In_ PVOID ParameterBuffer,
    _In_ PVOID ContextBuffer
    );

typedef
_Function_class_(PRM_INVOKE_HANDLER)
_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
PRM_INVOKE_HANDLER (
    _In_ LPGUID HandlerGuid,
    _In_ PVOID ParameterBuffer,
    _In_ ULONG Reserved,
    _Out_ PULONG64 EfiStatus
    );

typedef PRM_INVOKE_HANDLER *PPRM_INVOKE_HANDLER;

typedef
_Function_class_(PRM_QUERY_HANDLER)
_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
PRM_QUERY_HANDLER (
    _In_ LPGUID HandlerGuid,
    _Out_ PBOOLEAN Found
    );

typedef PRM_QUERY_HANDLER *PPRM_QUERY_HANDLER;

typedef
_Function_class_(PRM_LOCK_MODULE)
_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
PRM_LOCK_MODULE (
    _In_ LPGUID HandlerGuid
    );

typedef PRM_LOCK_MODULE *PPRM_LOCK_MODULE;

typedef
_Function_class_(PRM_UNLOCK_MODULE)
_IRQL_requires_max_(HIGH_LEVEL)
NTSTATUS
PRM_UNLOCK_MODULE (
    _In_ LPGUID HandlerGuid
    );

typedef PRM_UNLOCK_MODULE *PPRM_UNLOCK_MODULE;

typedef struct _PRM_INTERFACE {
    ULONG Version;
    PPRM_UNLOCK_MODULE UnlockModule;
    PPRM_LOCK_MODULE LockModule;
    PPRM_INVOKE_HANDLER InvokeHandler;
    PPRM_QUERY_HANDLER  QueryHandler;
} PRM_INTERFACE, *PPRM_INTERFACE;

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
_IRQL_requires_same_
NTKERNELAPI
NTSTATUS
ExGetPrmInterface (
    _In_ ULONG Version,
    _Out_ PPRM_INTERFACE InterfaceOut
    );

#ifdef __cplusplus
}
#endif

#endif
