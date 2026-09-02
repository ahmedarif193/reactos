//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//
// Module Name:
//
//      transfer.c
//
// Abstract:
//
//        VCHIQ bulk transfer related implementation
//

#include "precomp.h"

#include "trace.h"
#include "transfer.tmh"

#include "slotscommon.h"
#include "device.h"
#include "file.h"
#include "slots.h"
#include "memory.h"
#include "transfer.h"

VCHIQ_PAGED_SEGMENT_BEGIN

/*++

Routine Description:

    VchiqAllocateTransferRequestObjContext would allocated
        a context for the current TX request.

Arguments:

    DeviceContextPtr - Device context pointer

    VchiqFileContextPtr - File context pointer returned to caller

    WdfRequest - A handle to a framework request object for vchiq transfer

    BufferMdlPtr - Pointer to the mdl buffer allocated to perform the transfer

    PageListPtr - Page list pointer to the buffer transfer

    PageListSize - Page list size
    
    PageListPhyAddr - Page list physical address

    ScatterGatherListPtr - Scatter gather list allocated for this transfer

    VchiqTxRequestContextPPtr - TX request context allocatec by the function
          and returned to caller

Return Value:

    NTSTATUS

--*/
_Use_decl_annotations_
NTSTATUS VchiqAllocateTransferRequestObjContext (
    DEVICE_CONTEXT* DeviceContextPtr,
    VCHIQ_FILE_CONTEXT* VchiqFileContextPtr,
    WDFREQUEST WdfRequest,
    MDL* BufferMdlPtr,
    VOID* PageListPtr,
    ULONG PageListSize,
    PHYSICAL_ADDRESS PageListPhyAddr,
    SCATTER_GATHER_LIST* ScatterGatherListPtr,
    BOOLEAN WriteToDevice,
    VCHIQ_TX_REQUEST_CONTEXT** VchiqTxRequestContextPPtr
    )
{
    WDF_OBJECT_ATTRIBUTES wdfObjectAttributes;
    PVOID context;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &wdfObjectAttributes,
        VCHIQ_TX_REQUEST_CONTEXT);
    wdfObjectAttributes.EvtCleanupCallback =
        VchiqTransferRequestContextCleanup;

    PAGED_CODE();

    NTSTATUS status = WdfObjectAllocateContext(
        WdfRequest,
        &wdfObjectAttributes,
        &context);
    if (!NT_SUCCESS(status)) {
        VCHIQ_LOG_WARNING(
            "WdfObjectAllocateContext() failed %!STATUS!)",
            status);
        goto End;
    }

    *VchiqTxRequestContextPPtr = context;

    (*VchiqTxRequestContextPPtr)->BufferMdlPtr = BufferMdlPtr;
    (*VchiqTxRequestContextPPtr)->PageListPtr = PageListPtr;
    (*VchiqTxRequestContextPPtr)->PageListSize = PageListSize;
    (*VchiqTxRequestContextPPtr)->PageListPhyAddr = PageListPhyAddr;
    (*VchiqTxRequestContextPPtr)->ScatterGatherListPtr = ScatterGatherListPtr;
    (*VchiqTxRequestContextPPtr)->ScatterGatherBufferPtr = NULL;
    (*VchiqTxRequestContextPPtr)->DeviceContextPtr = DeviceContextPtr;
    (*VchiqTxRequestContextPPtr)->VchiqFileContextPtr = VchiqFileContextPtr;
    (*VchiqTxRequestContextPPtr)->FragmentPtr = NULL;
    (*VchiqTxRequestContextPPtr)->WriteToDevice = WriteToDevice;
    (*VchiqTxRequestContextPPtr)->FirmwareStatus = STATUS_PENDING;
    (*VchiqTxRequestContextPPtr)->ActualLength = 0;
    (*VchiqTxRequestContextPPtr)->CompletionState = 0;

End:
    return status;
}

VCHIQ_PAGED_SEGMENT_END

VCHIQ_NONPAGED_SEGMENT_BEGIN

static
VOID
VchiqReleaseTransferFragment(
    _In_ VCHIQ_TX_REQUEST_CONTEXT* RequestContext)
{
    DEVICE_CONTEXT* deviceContext;
    KIRQL oldIrql;

    if (RequestContext->FragmentPtr == NULL)
        return;

    deviceContext = RequestContext->DeviceContextPtr;
    KeAcquireSpinLock(&deviceContext->FragmentLock, &oldIrql);
    *(UCHAR**)RequestContext->FragmentPtr = deviceContext->FreeFragmentPtr;
    deviceContext->FreeFragmentPtr = RequestContext->FragmentPtr;
    RequestContext->FragmentPtr = NULL;
    KeReleaseSpinLock(&deviceContext->FragmentLock, oldIrql);
    KeReleaseSemaphore(
        &deviceContext->AvailableFragments,
        IO_NO_INCREMENT,
        1,
        FALSE);
}

static
VOID
VchiqReleaseScatterGather(
    _Inout_ VCHIQ_TX_REQUEST_CONTEXT* RequestContext)
{
    if (RequestContext->ScatterGatherListPtr != NULL)
    {
        RequestContext->VchiqFileContextPtr->DmaAdapterPtr->DmaOperations->
            FreeAdapterObject(
                RequestContext->VchiqFileContextPtr->DmaAdapterPtr,
                DeallocateObjectKeepRegisters);
        RequestContext->VchiqFileContextPtr->DmaAdapterPtr->DmaOperations->
            PutScatterGatherList(
                RequestContext->VchiqFileContextPtr->DmaAdapterPtr,
                RequestContext->ScatterGatherListPtr,
                RequestContext->WriteToDevice);
        RequestContext->ScatterGatherListPtr = NULL;
    }

    if (RequestContext->ScatterGatherBufferPtr != NULL)
    {
        ExFreePoolWithTag(
            RequestContext->ScatterGatherBufferPtr,
            VCHIQ_ALLOC_TAG_SGL);
        RequestContext->ScatterGatherBufferPtr = NULL;
    }
}

static
VOID
VchiqFinalizeTransferRequest(
    _In_ WDFREQUEST Request,
    _In_ BOOLEAN RequestOwned)
{
    VCHIQ_TX_REQUEST_CONTEXT* requestContext =
        VchiqGetTxRequestContext(Request);
    LONG state;
    NTSTATUS status;
    ULONG_PTR information;

    if (requestContext == NULL)
        return;

    for (;;)
    {
        state = InterlockedCompareExchange(
            &requestContext->CompletionState,
            0,
            0);
        if (!(state & VCHIQ_TRANSFER_STATE_FIRMWARE_DONE) ||
            (!RequestOwned && !(state & VCHIQ_TRANSFER_STATE_CANCELED)))
        {
            return;
        }

        if (state & VCHIQ_TRANSFER_STATE_FINALIZED)
            return;

        if (InterlockedCompareExchange(
                &requestContext->CompletionState,
                state | VCHIQ_TRANSFER_STATE_FINALIZED,
                state) == state)
        {
            break;
        }
    }

    status = (state & VCHIQ_TRANSFER_STATE_CANCELED) ?
        STATUS_CANCELLED : requestContext->FirmwareStatus;
    information = NT_SUCCESS(status) ? requestContext->ActualLength : 0;

    if (requestContext->ScatterGatherListPtr)
    {
        VchiqReleaseScatterGather(requestContext);
    }

    if (requestContext->FragmentPtr != NULL)
    {
        if (NT_SUCCESS(status) && !requestContext->WriteToDevice)
        {
            VCHIQ_PAGELIST* pageList = requestContext->PageListPtr;
            UCHAR* buffer = MmGetSystemAddressForMdlSafe(
                requestContext->BufferMdlPtr,
                NormalPagePriority);
            ULONG actual = requestContext->ActualLength;
            ULONG headBytes;
            ULONG tailBytes;

            if (buffer == NULL || pageList == NULL)
            {
                status = STATUS_INSUFFICIENT_RESOURCES;
                information = 0;
            }
            else
            {
                headBytes = (CACHE_LINE_SIZE - pageList->Offset) &
                    (CACHE_LINE_SIZE - 1);
                tailBytes = (pageList->Offset + actual) &
                    (CACHE_LINE_SIZE - 1);
                if (headBytes > actual)
                    headBytes = actual;

                if (headBytes != 0)
                {
                    RtlCopyMemory(
                        buffer,
                        requestContext->FragmentPtr,
                        headBytes);
                }
                if (headBytes < actual && tailBytes != 0)
                {
                    RtlCopyMemory(
                        buffer + actual - tailBytes,
                        requestContext->FragmentPtr + CACHE_LINE_SIZE,
                        tailBytes);
                }
            }
        }

        VchiqReleaseTransferFragment(requestContext);
    }

    WdfRequestCompleteWithInformation(Request, status, information);
}

_Use_decl_annotations_
VOID
VchiqTransferFirmwareComplete(
    WDFREQUEST Request,
    NTSTATUS Status,
    ULONG ActualLength,
    BOOLEAN RequestOwned)
{
    VCHIQ_TX_REQUEST_CONTEXT* requestContext =
        VchiqGetTxRequestContext(Request);

    if (requestContext == NULL)
    {
        if (RequestOwned)
            WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    if (RequestOwned && WdfRequestIsCanceled(Request))
    {
        InterlockedOr(
            &requestContext->CompletionState,
            VCHIQ_TRANSFER_STATE_CANCELED);
    }

    requestContext->FirmwareStatus = Status;
    requestContext->ActualLength = ActualLength;
    MemoryBarrier();
    InterlockedOr(
        &requestContext->CompletionState,
        VCHIQ_TRANSFER_STATE_FIRMWARE_DONE);
    VchiqFinalizeTransferRequest(Request, RequestOwned);
}

_Use_decl_annotations_
VOID
VchiqBulkRequestCanceled(
    WDFQUEUE Queue,
    WDFREQUEST Request)
{
    VCHIQ_TX_REQUEST_CONTEXT* requestContext =
        VchiqGetTxRequestContext(Request);

    UNREFERENCED_PARAMETER(Queue);

    if (requestContext == NULL)
    {
        WdfRequestComplete(Request, STATUS_CANCELLED);
        return;
    }

    InterlockedOr(
        &requestContext->CompletionState,
        VCHIQ_TRANSFER_STATE_CANCELED);
    VchiqFinalizeTransferRequest(Request, FALSE);
}

/*++

Routine Description:

    VchiqTransferRequestContextCleanup would perform cleanup
        when request object is delete.

Arguments:

    WdfObject - A handle to a framework object in this case
         a WDFRequest

Return Value:

    VOID

--*/
_Use_decl_annotations_
VOID VchiqTransferRequestContextCleanup (
    WDFOBJECT WdfObject
    )
{
    VCHIQ_TX_REQUEST_CONTEXT* vchiqTxRequestContextPtr =
        VchiqGetTxRequestContext(WdfObject);
    VCHIQ_FILE_CONTEXT* vchiqFileContextPtr =
        vchiqTxRequestContextPtr->VchiqFileContextPtr;
    
    if (vchiqTxRequestContextPtr->BufferMdlPtr) {
        vchiqTxRequestContextPtr->BufferMdlPtr = NULL;
    }

    if (vchiqTxRequestContextPtr->PageListPtr) {
        VchiqReleasePageListBuffer(
            vchiqFileContextPtr,
            vchiqTxRequestContextPtr->PageListSize,
            vchiqTxRequestContextPtr->PageListPhyAddr,
            vchiqTxRequestContextPtr->PageListPtr);
        vchiqTxRequestContextPtr->PageListPtr = NULL;
    }

    VchiqReleaseTransferFragment(vchiqTxRequestContextPtr);

    VchiqReleaseScatterGather(vchiqTxRequestContextPtr);

    return;
}

VCHIQ_NONPAGED_SEGMENT_END
