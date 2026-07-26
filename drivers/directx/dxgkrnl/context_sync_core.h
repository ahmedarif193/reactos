/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Scheduler-ordered synchronization object state core
 */

#pragma once

#include <ntifs.h>
#include <windef.h>
#include <d3dkmthk.h>

#define DXGK_CONTEXT_SYNC_SIGNAL_AT_SUBMISSION 0x00000001UL
#define DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT 0x00000002UL
#define DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND 0x00000004UL
#define DXGK_CONTEXT_SYNC_SIGNAL2_FLAGS_MASK (DXGK_CONTEXT_SYNC_SIGNAL_AT_SUBMISSION | DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT | DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND)
#define DXGK_CONTEXT_SYNC_OBJECT_NO_SIGNAL 0x00000010UL
#define DXGK_CONTEXT_SYNC_OBJECT_NO_WAIT 0x00000020UL
#define DXGK_CONTEXT_SYNC_OBJECT_SIGNAL_BY_KMD 0x00000100UL
/* The kmtest target intentionally compiles against its older default DDI
 * level, where the public enum name is hidden although its WDDM 2.0 value is
 * ABI-stable.  The production wrapper asserts the numeric contract. */
#define DXGK_CONTEXT_SYNC_TYPE_MONITORED_FENCE ((D3DDDI_SYNCHRONIZATIONOBJECT_TYPE)5)
#define DXGK_CONTEXT_SYNC_MAX_REFERENCES (D3DDDI_MAX_OBJECT_SIGNALED + 3)

typedef enum _DXGK_CONTEXT_SYNC_OPERATION
{
    DxgkContextSyncOperationInvalid = 0,
    DxgkContextSyncOperationLegacyWait,
    DxgkContextSyncOperationWait2,
    DxgkContextSyncOperationLegacySignal,
    DxgkContextSyncOperationSignal2
} DXGK_CONTEXT_SYNC_OPERATION;

typedef enum _DXGK_CONTEXT_SYNC_REFERENCE_KIND
{
    DxgkContextSyncReferenceInvalid = 0,
    DxgkContextSyncReferenceDevice,
    DxgkContextSyncReferenceOwner,
    DxgkContextSyncReferenceObject,
    DxgkContextSyncReferenceEvent
} DXGK_CONTEXT_SYNC_REFERENCE_KIND;

typedef struct _DXGK_CONTEXT_SYNC_CORE_OBJECT
{
    PVOID Identity;
    D3DDDI_SYNCHRONIZATIONOBJECT_TYPE Type;
    ULONG ObjectFlags;
    volatile LONG *Destroying;
    volatile LONG64 *FenceValue;
    volatile LONG *MutexOwned;
    volatile LONG64 *SemaphoreCount;
    ULONG SemaphoreLimit;
    PKEVENT StateEvent;
    PKEVENT NotificationEvent;
} DXGK_CONTEXT_SYNC_CORE_OBJECT, *PDXGK_CONTEXT_SYNC_CORE_OBJECT;

typedef struct _DXGK_CONTEXT_SYNC_CORE_REFERENCE
{
    PVOID Object;
    DXGK_CONTEXT_SYNC_REFERENCE_KIND Kind;
} DXGK_CONTEXT_SYNC_CORE_REFERENCE, *PDXGK_CONTEXT_SYNC_CORE_REFERENCE;

typedef VOID
(NTAPI *PDXGK_CONTEXT_SYNC_CORE_RELEASE_ROUTINE)(
    _In_ PVOID Object,
    _In_ DXGK_CONTEXT_SYNC_REFERENCE_KIND Kind,
    _In_opt_ PVOID Context);

typedef struct _DXGK_CONTEXT_SYNC_CORE_RETENTION
{
    volatile LONG ReleaseClaimed;
    volatile LONG ReferenceCount;
    PDXGK_CONTEXT_SYNC_CORE_RELEASE_ROUTINE ReleaseRoutine;
    PVOID ReleaseContext;
    DXGK_CONTEXT_SYNC_CORE_REFERENCE References[DXGK_CONTEXT_SYNC_MAX_REFERENCES];
} DXGK_CONTEXT_SYNC_CORE_RETENTION, *PDXGK_CONTEXT_SYNC_CORE_RETENTION;

VOID
DxgkContextSyncCoreInitializeRetention(
    _Out_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention,
    _In_ PDXGK_CONTEXT_SYNC_CORE_RELEASE_ROUTINE ReleaseRoutine,
    _In_opt_ PVOID ReleaseContext);

NTSTATUS
DxgkContextSyncCoreAddReference(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention,
    _In_ PVOID Object,
    _In_ DXGK_CONTEXT_SYNC_REFERENCE_KIND Kind);

BOOLEAN
DxgkContextSyncCoreClaimRelease(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention);

VOID
DxgkContextSyncCoreReleaseClaimed(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention);

VOID
DxgkContextSyncCoreRelease(
    _Inout_ PDXGK_CONTEXT_SYNC_CORE_RETENTION Retention);

NTSTATUS
DxgkContextSyncCoreValidate(
    _In_ DXGK_CONTEXT_SYNC_OPERATION Operation,
    _In_reads_opt_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG ObjectCount,
    _In_ ULONG SignalFlags,
    _In_ UINT64 PayloadValue,
    _In_reads_opt_(ObjectCount) CONST UINT64 *PayloadValueArray,
    _In_ BOOLEAN EventReferenced);

NTSTATUS
DxgkContextSyncCoreExecuteWait(
    _In_reads_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG ObjectCount,
    _In_ UINT64 FenceValue,
    _In_reads_opt_(ObjectCount) CONST UINT64 *FenceValueArray);

NTSTATUS
DxgkContextSyncCoreExecuteSignal(
    _In_reads_opt_(ObjectCount) PDXGK_CONTEXT_SYNC_CORE_OBJECT Objects,
    _In_ ULONG ObjectCount,
    _In_ ULONG SignalFlags,
    _In_ UINT64 FenceValue,
    _In_reads_opt_(ObjectCount) CONST UINT64 *FenceValueArray,
    _In_opt_ PKEVENT EnqueueEvent);
