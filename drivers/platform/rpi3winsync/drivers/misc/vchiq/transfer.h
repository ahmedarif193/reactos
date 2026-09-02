//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
// Module Name:
//
//     transfer.h
//
// Abstract:
//
//     This file contains vchiq bulk transfer related definitions.
//

#pragma once

EXTERN_C_START

typedef struct _VCHIQ_TX_REQUEST_CONTEXT {
     MDL* BufferMdlPtr;
     VOID* PageListPtr;
     ULONG PageListSize;
     PHYSICAL_ADDRESS PageListPhyAddr;
     SCATTER_GATHER_LIST* ScatterGatherListPtr;
     VOID* ScatterGatherBufferPtr;
     DEVICE_CONTEXT* DeviceContextPtr;
     VCHIQ_FILE_CONTEXT* VchiqFileContextPtr;
     UCHAR* FragmentPtr;
     BOOLEAN WriteToDevice;
     volatile LONG CompletionState;
     NTSTATUS FirmwareStatus;
     ULONG ActualLength;
     DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT)
     UCHAR DmaTransferContext[DMA_TRANSFER_CONTEXT_SIZE_V1];
} VCHIQ_TX_REQUEST_CONTEXT, *PVCHIQ_TX_REQUEST_CONTEXT;

#define VCHIQ_TRANSFER_STATE_CANCELED      0x00000001L
#define VCHIQ_TRANSFER_STATE_FIRMWARE_DONE 0x00000002L
#define VCHIQ_TRANSFER_STATE_FINALIZED     0x00000004L

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    VCHIQ_TX_REQUEST_CONTEXT, 
    VchiqGetTxRequestContext);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS VchiqAllocateTransferRequestObjContext (
    _In_ DEVICE_CONTEXT* DeviceContextPtr,
    _In_ VCHIQ_FILE_CONTEXT* VchiqFileContextPtr,
    _In_ WDFREQUEST Request,
    _In_ MDL* BufferMdlPtr,
    _In_ VOID* PageListPtr,
    _In_ ULONG PageListSize,
    _In_ PHYSICAL_ADDRESS PageListPhyAddr,
    _In_ SCATTER_GATHER_LIST* ScatterGatherListPtr,
    _In_ BOOLEAN WriteToDevice,
    _Outptr_ VCHIQ_TX_REQUEST_CONTEXT** VchiqTxRequestContextPPtr
    );

EVT_WDF_OBJECT_CONTEXT_CLEANUP VchiqTransferRequestContextCleanup;
EVT_WDF_IO_QUEUE_IO_CANCELED_ON_QUEUE VchiqBulkRequestCanceled;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID VchiqTransferFirmwareComplete (
    _In_ WDFREQUEST Request,
    _In_ NTSTATUS Status,
    _In_ ULONG ActualLength,
    _In_ BOOLEAN RequestOwned
    );

EXTERN_C_END
