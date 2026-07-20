/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Nonpaged monitored-fence CPU wait registry core
 */

#pragma once

#include <ntifs.h>

typedef struct _DXGK_SYNC_WAIT_CORE_REQUEST DXGK_SYNC_WAIT_CORE_REQUEST, *PDXGK_SYNC_WAIT_CORE_REQUEST;

typedef struct _DXGK_SYNC_WAIT_CORE_TARGET
{
    PVOID Object;
    volatile LONG64 *FenceValue;
    UINT64 TargetValue;
} DXGK_SYNC_WAIT_CORE_TARGET, *PDXGK_SYNC_WAIT_CORE_TARGET;

typedef struct _DXGK_SYNC_WAIT_CORE_UPDATE
{
    PVOID Object;
    volatile LONG64 *FenceValue;
    volatile UINT64 *PublishedValue;
    UINT64 NewValue;
} DXGK_SYNC_WAIT_CORE_UPDATE, *PDXGK_SYNC_WAIT_CORE_UPDATE;

typedef VOID
(NTAPI *PDXGK_SYNC_WAIT_CORE_COMPLETION)(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REQUEST Request,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context);

typedef NTSTATUS
(NTAPI *PDXGK_SYNC_WAIT_CORE_ADMISSION)(
    _In_ PDXGK_SYNC_WAIT_CORE_REQUEST Request,
    _In_opt_ PVOID Context);

typedef NTSTATUS
(NTAPI *PDXGK_SYNC_WAIT_CORE_PUBLISH_ADMISSION)(
    _In_opt_ PVOID Context);

struct _DXGK_SYNC_WAIT_CORE_REQUEST
{
    LIST_ENTRY RegistryEntry;
    volatile LONG State;
    NTSTATUS CompletionStatus;
    BOOLEAN WaitAny;
    ULONG TargetCount;
    PDXGK_SYNC_WAIT_CORE_TARGET Targets;
    PDXGK_SYNC_WAIT_CORE_ADMISSION AdmissionRoutine;
    PDXGK_SYNC_WAIT_CORE_COMPLETION CompletionRoutine;
    PVOID CompletionContext;
};

typedef struct _DXGK_SYNC_WAIT_CORE_REGISTRY
{
    KSPIN_LOCK Lock;
    LIST_ENTRY RequestList;
    BOOLEAN ShuttingDown;
    NTSTATUS ShutdownStatus;
} DXGK_SYNC_WAIT_CORE_REGISTRY, *PDXGK_SYNC_WAIT_CORE_REGISTRY;

VOID
DxgkSyncWaitCoreInitializeRegistry(
    _Out_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry);

VOID
DxgkSyncWaitCoreInitializeRequest(
    _Out_ PDXGK_SYNC_WAIT_CORE_REQUEST Request,
    _In_reads_(TargetCount) PDXGK_SYNC_WAIT_CORE_TARGET Targets,
    _In_ ULONG TargetCount,
    _In_ BOOLEAN WaitAny,
    _In_opt_ PDXGK_SYNC_WAIT_CORE_ADMISSION AdmissionRoutine,
    _In_ PDXGK_SYNC_WAIT_CORE_COMPLETION CompletionRoutine,
    _In_opt_ PVOID CompletionContext);

NTSTATUS
DxgkSyncWaitCoreRegister(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _Inout_ PDXGK_SYNC_WAIT_CORE_REQUEST Request);

NTSTATUS
DxgkSyncWaitCorePublishBatch(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _In_reads_(UpdateCount) PDXGK_SYNC_WAIT_CORE_UPDATE Updates,
    _In_ ULONG UpdateCount,
    _In_ BOOLEAN AllowFenceRewind,
    _In_opt_ PDXGK_SYNC_WAIT_CORE_PUBLISH_ADMISSION AdmissionRoutine,
    _In_opt_ PVOID AdmissionContext);

VOID
DxgkSyncWaitCoreCancelObject(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _In_ PVOID Object,
    _In_ NTSTATUS Status);

VOID
DxgkSyncWaitCoreCancelAll(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry,
    _In_ NTSTATUS Status,
    _In_ BOOLEAN ShutDown);

BOOLEAN
DxgkSyncWaitCoreIsEmpty(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REGISTRY Registry);
