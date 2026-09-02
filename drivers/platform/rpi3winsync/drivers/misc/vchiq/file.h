//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
// Module Name:
//
//     file.h
//
// Abstract:
//
//     This file contains file handle related definitions.
//

#pragma once

EXTERN_C_START

enum {
    // Message queues
    FILE_QUEUE_CREATE_SERVICE = 0,
    FILE_QUEUE_CLOSE_SERVICE,
    FILE_QUEUE_PENDING_MSG,
    FILE_QUEUE_PENDING_VCHI_MSG,
    FILE_QUEUE_TX_DATA,
    FILE_QUEUE_RX_DATA,
    FILE_QUEUE_MAX,
};

typedef enum _MSG_BULK_TYPE {
    // Message queues
    MSG_BULK_TX = 0,
    MSG_BULK_RX,
    MSG_BULK_MAX,
}MSG_BULK_TYPE;

typedef enum _SERVICE_STATE {
    // Service State
    SERVICE_STATE_MIN = 0,
    SERVICE_STATE_OPENING,
    SERVICE_STATE_OPEN,
    SERVICE_STATE_CLOSING,
    SERVICE_STATE_CLOSE,
} SERVICE_STATE;

#define VCHIQ_PAGE_LIST_CACHE_DEPTH 8

typedef struct _VCHIQ_PAGE_LIST_CACHE_ENTRY {
    VOID* BufferPtr;
    ULONG BufferSize;
    PHYSICAL_ADDRESS PhysicalAddress;
} VCHIQ_PAGE_LIST_CACHE_ENTRY, *PVCHIQ_PAGE_LIST_CACHE_ENTRY;

typedef struct _VCHIQ_FILE_CONTEXT {
    ULONG    ArmPortNumber;
    ULONG    VCHIQPortNumber;
    
    // Lookaside memory per file handle to take advantage
    // of WDF memory cleanup by parenting to file object
    WDFLOOKASIDE PendingMsgLookAsideMemory;
    WDFLOOKASIDE PendingBulkMsgLookAsideMemory;

    LIST_ENTRY PendingDataMsgList;
    FAST_MUTEX PendingDataMsgMutex;
    
    LIST_ENTRY PendingBulkMsgList[MSG_BULK_MAX];
    FAST_MUTEX PendingBulkMsgMutex[MSG_BULK_MAX];

    WDFQUEUE FileQueue[FILE_QUEUE_MAX];

    KEVENT FileEventStop;
    KEVENT ServiceStateEvent;
    KEVENT ServiceClosedEvent;
    
    // Pointer to service data in user space. Userland expects the driver 
    // returns this back when completing a transaction
    VOID *ServiceUserData;

    ULONG IsVchi;
    LIST_ENTRY PendingVchiMsgList;
    FAST_MUTEX PendingVchiMsgMutex;

    // Minimal state management for now. Consider to expand more service
    // state tracking i current implementation is insufficient.
    volatile LONG State;
    FAST_MUTEX ServiceMutex;
    volatile LONG BulksAborted;

    // DMA
    DMA_ADAPTER* DmaAdapterPtr;
    KSPIN_LOCK PageListCacheLock;
    VCHIQ_PAGE_LIST_CACHE_ENTRY PageListCache[VCHIQ_PAGE_LIST_CACHE_DEPTH];

} VCHIQ_FILE_CONTEXT, *PVCHIQ_FILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VCHIQ_FILE_CONTEXT, VchiqGetFileContext);

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS VchiqAllocateFileObjContext (
    _In_ DEVICE_CONTEXT* DeviceContextPtr,
    _In_ WDFFILEOBJECT WdfFileObject,
    _Outptr_ VCHIQ_FILE_CONTEXT** VchiqFileContextPPtr
    );

EVT_WDF_FILE_CLOSE VchiqFileClose;

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID VchiqAbortServiceBulks (
    _In_ VCHIQ_FILE_CONTEXT* VchiqFileContextPtr
    );

EXTERN_C_END
