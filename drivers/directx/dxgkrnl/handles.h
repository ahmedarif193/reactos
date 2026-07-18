/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Owner-scoped typed D3DKMT handle namespace
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#pragma once

typedef enum _DXGKRNL_HANDLE_TYPE
{
    DxgkHandleTypeAdapter = 1,
    DxgkHandleTypeDevice = 2,
    DxgkHandleTypeContext = 3,
    DxgkHandleTypeSynchronizationObject = 4,
    DxgkHandleTypePagingQueue = 5,
    DxgkHandleTypeAllocation = 6
} DXGKRNL_HANDLE_TYPE;

typedef BOOLEAN (*PDXGKRNL_HANDLE_REFERENCE_ROUTINE)(_In_ PVOID Object);

BOOLEAN
DxgkTryClaimTeardown(
    _Inout_ volatile LONG *TeardownClaimed);

NTSTATUS
DxgkHandleManagerInitialize(VOID);

VOID
DxgkHandleManagerUninitialize(VOID);

NTSTATUS
DxgkCreateAdapterHandle(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS OwnerProcess,
    _Out_ D3DKMT_HANDLE *OutHandle);

NTSTATUS
DxgkReferenceAdapterByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter);

NTSTATUS
DxgkCloseAdapterHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter);

NTSTATUS
DxgkCreateDeviceHandle(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ PEPROCESS OwnerProcess,
    _Out_ D3DKMT_HANDLE *OutHandle);

NTSTATUS
DxgkReferenceDeviceByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice);

NTSTATUS
DxgkDetachDeviceHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_DEVICE *OutDevice);

NTSTATUS
DxgkCreateContextHandle(
    _In_ PDXGKRNL_CONTEXT Context,
    _In_ PEPROCESS OwnerProcess,
    _Out_ D3DKMT_HANDLE *OutHandle);

NTSTATUS
DxgkReferenceContextByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice,
    _Out_ PDXGKRNL_CONTEXT *OutContext);

NTSTATUS
DxgkDetachContextHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_CONTEXT *OutContext);

VOID
DxgkRemoveDeviceHandleObject(
    _In_ PDXGKRNL_DEVICE Device);

VOID
DxgkRemoveContextHandleObject(
    _In_ PDXGKRNL_CONTEXT Context);

NTSTATUS
DxgkCreateOwnedHandle(
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PVOID Object,
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS OwnerProcess,
    _In_opt_ volatile LONG *Destroying,
    _Inout_ volatile LONG *TeardownClaimed,
    _Out_ D3DKMT_HANDLE *OutHandle);

NTSTATUS
DxgkReferenceOwnedHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_opt_ PEPROCESS OwnerProcess,
    _In_ PDXGKRNL_HANDLE_REFERENCE_ROUTINE ReferenceRoutine,
    _Out_ PVOID *OutObject);

NTSTATUS
DxgkDetachOwnedHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PVOID *OutObject);

VOID
DxgkRemoveOwnedHandleObject(
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PVOID Object);

VOID
DxgkPurgeProcessHandles(
    _In_ PEPROCESS Process);

VOID
DxgkPurgeAdapterHandles(
    _In_ PDXGKRNL_ADAPTER Adapter);

BOOLEAN
DxgkReferenceAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

BOOLEAN
DxgkReferenceAdapterObject(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkDereferenceAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter);

ULONG
DxgkReferenceStartedAdapters(
    _Out_writes_(Capacity) PDXGKRNL_ADAPTER *Adapters,
    _In_ ULONG Capacity);

VOID
DxgkBeginAdapterRundown(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkWaitForAdapterRundown(
    _In_ PDXGKRNL_ADAPTER Adapter);

VOID
DxgkReinitializeAdapterRundown(
    _In_ PDXGKRNL_ADAPTER Adapter);
