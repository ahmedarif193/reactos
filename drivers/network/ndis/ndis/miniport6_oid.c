/*
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/miniport6_oid.c
 * PURPOSE:     NDIS 6.x OID Request Handling
 * PROGRAMMERS: ReactOS Development Team
 * NOTES:       This file implements the NDIS 6.x OID request handling
 *              which replaces the legacy MiniportQueryInformation/
 *              MiniportSetInformation handlers.
 */

#include "ndissys.h"

#if NDIS_SUPPORT_NDIS6

/*
 * Internal structure to track NDIS 6.x OID requests
 */
typedef struct _NDIS6_OID_REQUEST_CONTEXT {
    LIST_ENTRY ListEntry;
    NDIS_HANDLE MiniportHandle;
    PNDIS_OID_REQUEST Request;
    PVOID OriginalRequestId;
    BOOLEAN IsDirect;
    BOOLEAN IsPending;
    BOOLEAN IsCancelled;
    NDIS_STATUS CompletionStatus;
    KEVENT CompletionEvent;
    /* Upper layer completion callback context */
    PVOID CompletionContext;
    /* Timestamp for timeout handling */
    LARGE_INTEGER SubmitTime;
    /* Reference count to protect synchronous waiters */
    LONG RefCount;
} NDIS6_OID_REQUEST_CONTEXT, *PNDIS6_OID_REQUEST_CONTEXT;

typedef struct _NDIS6_MINIPORT_DRIVER_BLOCK NDIS6_MINIPORT_DRIVER_BLOCK, *PNDIS6_MINIPORT_DRIVER_BLOCK;

/* Global list of pending OID requests */
static LIST_ENTRY Ndis6OidRequestList;
static KSPIN_LOCK Ndis6OidRequestListLock;
static BOOLEAN Ndis6OidInitialized = FALSE;

/*
 * InitializeNdis6OidSupport
 * Internal function to initialize NDIS 6.x OID support structures
 */
static VOID
InitializeNdis6OidSupport(VOID)
{
    if (!Ndis6OidInitialized)
    {
        InitializeListHead(&Ndis6OidRequestList);
        KeInitializeSpinLock(&Ndis6OidRequestListLock);
        Ndis6OidInitialized = TRUE;
    }
}

/*
 * Ndis6iDereferenceOidRequestContext
 * Decrements the reference count and frees the context when it reaches zero.
 */
static VOID
Ndis6iDereferenceOidRequestContext(
    _In_ PNDIS6_OID_REQUEST_CONTEXT Context)
{
    KIRQL OldIrql;

    if (Context == NULL)
    {
        return;
    }

    if (InterlockedDecrement(&Context->RefCount) != 0)
    {
        return;
    }

    /* Remove from global list */
    KeAcquireSpinLock(&Ndis6OidRequestListLock, &OldIrql);
    RemoveEntryList(&Context->ListEntry);
    KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);

    DPRINT1("Freed OID context %p\n", Context);

    ExFreePoolWithTag(Context, NDIS_TAG);
}

/*
 * Ndis6iAllocateOidRequestContext
 * Allocates a tracking context for an OID request
 *
 * Parameters:
 *   MiniportHandle - Handle to the miniport adapter
 *   Request - Pointer to the OID request
 *   IsDirect - TRUE if this is a direct OID request
 *
 * Returns:
 *   Pointer to the allocated context, or NULL on failure
 */
static PNDIS6_OID_REQUEST_CONTEXT
Ndis6iAllocateOidRequestContext(
    _In_ NDIS_HANDLE MiniportHandle,
    _In_ PNDIS_OID_REQUEST Request,
    _In_ BOOLEAN IsDirect)
{
    PNDIS6_OID_REQUEST_CONTEXT Context;
    KIRQL OldIrql;

    InitializeNdis6OidSupport();

    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(NDIS6_OID_REQUEST_CONTEXT),
                                    NDIS_TAG);
    if (Context == NULL)
    {
        DPRINT1("Failed to allocate OID request context\n");
        return NULL;
    }

    RtlZeroMemory(Context, sizeof(NDIS6_OID_REQUEST_CONTEXT));

    Context->MiniportHandle = MiniportHandle;
    Context->Request = Request;
    Context->OriginalRequestId = Request->RequestId;
    Context->IsDirect = IsDirect;
    Context->IsPending = TRUE;
    Context->IsCancelled = FALSE;
    Context->CompletionStatus = NDIS_STATUS_PENDING;
    Context->RefCount = 1;

    KeInitializeEvent(&Context->CompletionEvent, NotificationEvent, FALSE);
    KeQuerySystemTime(&Context->SubmitTime);

    /* Add to global list */
    KeAcquireSpinLock(&Ndis6OidRequestListLock, &OldIrql);
    InsertTailList(&Ndis6OidRequestList, &Context->ListEntry);
    KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);

    DPRINT1("Allocated OID context %p for request %p (Direct=%d)\n",
        Context, Request, IsDirect);

    return Context;
}

/*
 * Ndis6iFreeOidRequestContext
 * Frees a tracking context for an OID request
 *
 * Parameters:
 *   Context - Pointer to the context to free
 */
static VOID
Ndis6iFreeOidRequestContext(
    _In_ PNDIS6_OID_REQUEST_CONTEXT Context)
{
    Ndis6iDereferenceOidRequestContext(Context);
}

/*
 * Ndis6iFindOidRequestContext
 * Finds a tracking context for an OID request
 *
 * Parameters:
 *   MiniportHandle - Handle to the miniport adapter
 *   Request - Pointer to the OID request
 *
 * Returns:
 *   Pointer to the context, or NULL if not found
 */
static PNDIS6_OID_REQUEST_CONTEXT
Ndis6iFindOidRequestContext(
    _In_ NDIS_HANDLE MiniportHandle,
    _In_ PNDIS_OID_REQUEST Request)
{
    PLIST_ENTRY Entry;
    PNDIS6_OID_REQUEST_CONTEXT Context;
    KIRQL OldIrql;

    InitializeNdis6OidSupport();

    KeAcquireSpinLock(&Ndis6OidRequestListLock, &OldIrql);

    for (Entry = Ndis6OidRequestList.Flink;
         Entry != &Ndis6OidRequestList;
         Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry, NDIS6_OID_REQUEST_CONTEXT, ListEntry);
        if (Context->MiniportHandle == MiniportHandle &&
            Context->Request == Request)
        {
            KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);
            return Context;
        }
    }

    KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);
    return NULL;
}

/*
 * Ndis6iFindOidRequestContextByRequestId
 * Finds a tracking context for an OID request by request ID
 *
 * Parameters:
 *   MiniportHandle - Handle to the miniport adapter
 *   RequestId - ID of the request to find
 *
 * Returns:
 *   Pointer to the context, or NULL if not found
 */
static PNDIS6_OID_REQUEST_CONTEXT
Ndis6iFindOidRequestContextByRequestId(
    _In_ NDIS_HANDLE MiniportHandle,
    _In_ PVOID RequestId)
{
    PLIST_ENTRY Entry;
    PNDIS6_OID_REQUEST_CONTEXT Context;
    KIRQL OldIrql;

    InitializeNdis6OidSupport();

    KeAcquireSpinLock(&Ndis6OidRequestListLock, &OldIrql);

    for (Entry = Ndis6OidRequestList.Flink;
         Entry != &Ndis6OidRequestList;
         Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry, NDIS6_OID_REQUEST_CONTEXT, ListEntry);
        if (Context->MiniportHandle == MiniportHandle &&
            Context->OriginalRequestId == RequestId &&
            Context->IsPending)
        {
            KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);
            return Context;
        }
    }

    KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);
    return NULL;
}

/*
 * Ndis6iResolveMiniportContext
 * Resolves a miniport handle to the driver block and adapter context.
 *
 * The handle can be either a DEVICE_OBJECT (NDIS 6.x path) or a LOGICAL_ADAPTER.
 */
static BOOLEAN
Ndis6iResolveMiniportContext(
    _In_ NDIS_HANDLE MiniportHandle,
    _Out_ PNDIS6_MINIPORT_DRIVER_BLOCK *DriverBlock,
    _Out_ PVOID *MiniportAdapterContext)
{
    PDEVICE_OBJECT DeviceObject;
    PLOGICAL_ADAPTER Adapter;

    if (MiniportHandle == NULL || DriverBlock == NULL || MiniportAdapterContext == NULL)
    {
        return FALSE;
    }

    /* First, try to treat handle as a DEVICE_OBJECT */
    DeviceObject = (PDEVICE_OBJECT)MiniportHandle;
    if (DeviceObject != NULL &&
        DeviceObject->Type == IO_TYPE_DEVICE &&
        DeviceObject->Size >= sizeof(DEVICE_OBJECT) &&
        DeviceObject->DeviceExtension != NULL)
    {
        *DriverBlock = (PNDIS6_MINIPORT_DRIVER_BLOCK)((PVOID*)DeviceObject->DeviceExtension)[0];
        *MiniportAdapterContext = ((PVOID*)DeviceObject->DeviceExtension)[2];
        return TRUE;
    }

    /* Otherwise treat it as a LOGICAL_ADAPTER */
    Adapter = (PLOGICAL_ADAPTER)MiniportHandle;
    if (Adapter->NdisMiniportBlock.DeviceObject != NULL &&
        Adapter->NdisMiniportBlock.DeviceObject->DeviceExtension != NULL)
    {
        DeviceObject = Adapter->NdisMiniportBlock.DeviceObject;
        *DriverBlock = (PNDIS6_MINIPORT_DRIVER_BLOCK)((PVOID*)DeviceObject->DeviceExtension)[0];
        *MiniportAdapterContext = ((PVOID*)DeviceObject->DeviceExtension)[2];
        return TRUE;
    }

    return FALSE;
}

/*
 * Ndis6iGetOidFromRequest
 * Helper function to get the OID from an OID request
 *
 * Parameters:
 *   Request - Pointer to the OID request
 *
 * Returns:
 *   The OID value
 */
static NDIS_OID
Ndis6iGetOidFromRequest(
    _In_ PNDIS_OID_REQUEST Request)
{
    switch (Request->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return Request->DATA.QUERY_INFORMATION.Oid;

        case NdisRequestSetInformation:
            return Request->DATA.SET_INFORMATION.Oid;

        case NdisRequestMethod:
            return Request->DATA.METHOD_INFORMATION.Oid;

        default:
            return 0;
    }
}

/*
 * Ndis6iCompleteOidRequest
 * Internal function to complete an OID request
 *
 * Parameters:
 *   Context - Pointer to the OID request context
 *   Status - Completion status
 *   IsDirect - TRUE if this is a direct OID request
 */
static VOID
Ndis6iCompleteOidRequest(
    _In_ PNDIS6_OID_REQUEST_CONTEXT Context,
    _In_ NDIS_STATUS Status,
    _In_ BOOLEAN IsDirect)
{
    NDIS_OID Oid;

    if (Context == NULL)
    {
        DPRINT1("NULL context in completion\n");
        return;
    }

    /* Update context */
    Context->CompletionStatus = Status;
    Context->IsPending = FALSE;

    Oid = Ndis6iGetOidFromRequest(Context->Request);

    DPRINT1("Completing OID 0x%08x with status 0x%x (Direct=%d)\n",
        Oid, Status, IsDirect);

    /* Signal completion event for synchronous waiters */
    KeSetEvent(&Context->CompletionEvent, IO_NO_INCREMENT, FALSE);

    /*
     * TODO: Call upper layer completion callback if async
     * In a full implementation, we would:
     * 1. Look up the upper layer that originated this request
     * 2. Call its OID request completion handler
     * 3. This allows protocols to chain OID requests
     */
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMOidRequestComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_OID_REQUEST_CONTEXT Context;

    DPRINT("NdisMOidRequestComplete called: Handle=%p, Request=%p, Status=0x%x\n",
        MiniportAdapterHandle, OidRequest, Status);

    /* Validate parameters */
    if (MiniportAdapterHandle == NULL || OidRequest == NULL)
    {
        DPRINT1("Invalid parameter\n");
        return;
    }

    /* Find the request context */
    Context = Ndis6iFindOidRequestContext(MiniportAdapterHandle, OidRequest);
    if (Context == NULL)
    {
        DPRINT1("OID request context not found for request %p\n",
            OidRequest);
        /*
         * Even if we don't find the context, this is still a valid call.
         * The miniport may be completing a request that was submitted
         * through an older code path or that we didn't track.
         */
        return;
    }

    /* Verify this is not a direct OID request */
    if (Context->IsDirect)
    {
        DPRINT1("Direct OID request completed via NdisMOidRequestComplete\n");
        /* Allow it but log the inconsistency */
    }

    /* Complete the request */
    Ndis6iCompleteOidRequest(Context, Status, FALSE);

    /* Free the context */
    Ndis6iFreeOidRequestContext(Context);

    DPRINT1("NdisMOidRequestComplete completed\n");
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ PVOID RequestId)
{
    PNDIS6_OID_REQUEST_CONTEXT Context;

    DPRINT("NdisMCancelOidRequest called: Handle=%p, RequestId=%p\n",
        MiniportAdapterHandle, RequestId);

    /* Validate parameters */
    if (MiniportAdapterHandle == NULL)
    {
        DPRINT1("Invalid adapter handle\n");
        return;
    }

    /* Find the request context by request ID */
    Context = Ndis6iFindOidRequestContextByRequestId(MiniportAdapterHandle, RequestId);
    if (Context == NULL)
    {
        DPRINT1("OID request with ID %p not found or already completed\n",
            RequestId);
        return;
    }

    /* Mark as cancelled */
    Context->IsCancelled = TRUE;

    DPRINT1("Marked OID request %p as cancelled\n", Context->Request);

    /*
     * TODO: In a full implementation, we would:
     * 1. Call the miniport's CancelOidRequestHandler to notify it
     * 2. The miniport should then complete the request with NDIS_STATUS_REQUEST_ABORTED
     */
}

#if NDIS_SUPPORT_NDIS61
/*
 * @implemented
 */
VOID
EXPORT
NdisMDirectOidRequestComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_OID_REQUEST_CONTEXT Context;

    DPRINT("NdisMDirectOidRequestComplete called: Handle=%p, Request=%p, Status=0x%x\n",
        MiniportAdapterHandle, OidRequest, Status);

    /* Validate parameters */
    if (MiniportAdapterHandle == NULL || OidRequest == NULL)
    {
        DPRINT1("Invalid parameter\n");
        return;
    }

    /* Find the request context */
    Context = Ndis6iFindOidRequestContext(MiniportAdapterHandle, OidRequest);
    if (Context == NULL)
    {
        DPRINT1("Direct OID request context not found for request %p\n",
            OidRequest);
        return;
    }

    /* Verify this is a direct OID request */
    if (!Context->IsDirect)
    {
        DPRINT1("Non-direct OID request completed via NdisMDirectOidRequestComplete\n");
        /* Allow it but log the inconsistency */
    }

    /* Complete the request */
    Ndis6iCompleteOidRequest(Context, Status, TRUE);

    /* Free the context */
    Ndis6iFreeOidRequestContext(Context);

    DPRINT1("NdisMDirectOidRequestComplete completed\n");
}
#endif /* NDIS_SUPPORT_NDIS61 */

/*
 * Forward declaration of NDIS6_MINIPORT_DRIVER_BLOCK
 * This structure is defined in miniport6.c
 */
typedef struct _NDIS6_MINIPORT_DRIVER_BLOCK {
    LIST_ENTRY ListEntry;
    PDRIVER_OBJECT DriverObject;
    UNICODE_STRING RegistryPath;
    NDIS_HANDLE MiniportDriverContext;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;
    ULONG Flags;
    LONG RefCount;
} NDIS6_MINIPORT_DRIVER_BLOCK, *PNDIS6_MINIPORT_DRIVER_BLOCK;

/*
 * Cancel all pending OID requests for a miniport.
 */
VOID
Ndis6iCancelOidRequestsForMiniport(
    _In_ NDIS_HANDLE MiniportHandle)
{
    PLIST_ENTRY Entry;
    PNDIS6_OID_REQUEST_CONTEXT Context;
    PNDIS6_OID_REQUEST_CONTEXT *Contexts = NULL;
    ULONG Count = 0;
    ULONG Index = 0;
    KIRQL OldIrql;
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PVOID MiniportAdapterContext;
    MINIPORT_CANCEL_OID_REQUEST *CancelHandler = NULL;
#if NDIS_SUPPORT_NDIS61
    PVOID CancelDirectHandler = NULL;
#endif

    if (!Ndis6iResolveMiniportContext(MiniportHandle, &DriverBlock, &MiniportAdapterContext))
    {
        return;
    }

    if (DriverBlock != NULL)
    {
        CancelHandler = DriverBlock->Characteristics.CancelOidRequestHandler;
#if NDIS_SUPPORT_NDIS61
        CancelDirectHandler = DriverBlock->Characteristics.CancelDirectOidRequestHandler;
#endif
    }

    KeAcquireSpinLock(&Ndis6OidRequestListLock, &OldIrql);
    for (Entry = Ndis6OidRequestList.Flink;
         Entry != &Ndis6OidRequestList;
         Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry, NDIS6_OID_REQUEST_CONTEXT, ListEntry);
        if (Context->MiniportHandle == MiniportHandle && Context->IsPending)
        {
            Count++;
        }
    }
    KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);

    if (Count == 0)
    {
        return;
    }

    Contexts = ExAllocatePoolWithTag(NonPagedPool, sizeof(PNDIS6_OID_REQUEST_CONTEXT) * Count, NDIS_TAG);
    if (Contexts == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&Ndis6OidRequestListLock, &OldIrql);
    for (Entry = Ndis6OidRequestList.Flink;
         Entry != &Ndis6OidRequestList && Index < Count;
         Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry, NDIS6_OID_REQUEST_CONTEXT, ListEntry);
        if (Context->MiniportHandle == MiniportHandle && Context->IsPending)
        {
            Context->IsCancelled = TRUE;
            InterlockedIncrement(&Context->RefCount);
            Contexts[Index++] = Context;
        }
    }
    KeReleaseSpinLock(&Ndis6OidRequestListLock, OldIrql);

    for (Index = 0; Index < Count; Index++)
    {
        Context = Contexts[Index];
        if (Context == NULL)
        {
            continue;
        }

#if NDIS_SUPPORT_NDIS61
        if (Context->IsDirect && CancelDirectHandler != NULL)
        {
            ((MINIPORT_CANCEL_OID_REQUEST*)CancelDirectHandler)(
                MiniportAdapterContext,
                Context->OriginalRequestId);
            Ndis6iDereferenceOidRequestContext(Context);
            continue;
        }
#endif

        if (CancelHandler != NULL)
        {
            CancelHandler(MiniportAdapterContext, Context->OriginalRequestId);
        }
        else
        {
            Ndis6iCompleteOidRequest(Context, NDIS_STATUS_REQUEST_ABORTED, Context->IsDirect);
        }

        Ndis6iDereferenceOidRequestContext(Context);
    }

    ExFreePoolWithTag(Contexts, NDIS_TAG);
}

/*
 * Ndis6iSubmitOidRequest
 * Internal function to submit an OID request to a miniport
 *
 * Parameters:
 *   MiniportHandle - Handle to the miniport adapter
 *   Request - Pointer to the OID request
 *   IsDirect - TRUE if this is a direct OID request
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS if the request completed synchronously
 *   NDIS_STATUS_PENDING if the request will complete asynchronously
 *   Error status on failure
 */
NDIS_STATUS
Ndis6iSubmitOidRequest(
    _In_ NDIS_HANDLE MiniportHandle,
    _In_ PNDIS_OID_REQUEST Request,
    _In_ BOOLEAN IsDirect)
{
    PNDIS6_OID_REQUEST_CONTEXT Context;
    NDIS_STATUS Status;
    NDIS_OID Oid;
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PVOID MiniportAdapterContext;

    DPRINT1("NDIS6: Ndis6iSubmitOidRequest: Handle=%p, Request=%p, IsDirect=%d\n",
        MiniportHandle, Request, IsDirect);

    /* Validate parameters */
    if (MiniportHandle == NULL || Request == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Validate request header */
    if (Request->Header.Type != NDIS_OBJECT_TYPE_OID_REQUEST ||
        Request->Header.Revision < NDIS_OID_REQUEST_REVISION_1)
    {
        DPRINT1("NDIS6: Invalid OID request header\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Oid = Ndis6iGetOidFromRequest(Request);

    DPRINT1("NDIS6: Submitting OID 0x%08x, Type=%d\n",
        Oid, Request->RequestType);

    if (!Ndis6iResolveMiniportContext(MiniportHandle, &DriverBlock, &MiniportAdapterContext))
    {
        DPRINT1("NDIS6: Cannot resolve miniport context for handle %p\n", MiniportHandle);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Verify we have the driver block and handler */
    if (DriverBlock == NULL)
    {
        DPRINT1("NDIS6: No driver block available\n");
        return NDIS_STATUS_FAILURE;
    }

    /* Check for appropriate handler based on request type */
    if (IsDirect)
    {
#if NDIS_SUPPORT_NDIS61
        if (DriverBlock->Characteristics.DirectOidRequestHandler == NULL)
        {
            DPRINT1("NDIS6: DirectOidRequestHandler is NULL\n");
            return NDIS_STATUS_NOT_SUPPORTED;
        }
#else
        DPRINT1("NDIS6: Direct OID requests not supported in NDIS 6.0\n");
        return NDIS_STATUS_NOT_SUPPORTED;
#endif
    }
    else
    {
        if (DriverBlock->Characteristics.OidRequestHandler == NULL)
        {
            DPRINT1("NDIS6: OidRequestHandler is NULL\n");
            return NDIS_STATUS_NOT_SUPPORTED;
        }
    }

    /* Allocate tracking context */
    Context = Ndis6iAllocateOidRequestContext(MiniportHandle, Request, IsDirect);
    if (Context == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    /*
     * Call the miniport's OID request handler.
     *
     * The handler signature is:
     *   NDIS_STATUS OidRequestHandler(
     *       NDIS_HANDLE MiniportAdapterContext,
     *       PNDIS_OID_REQUEST OidRequest);
     *
     * The handler can return:
     *   NDIS_STATUS_SUCCESS - Request completed synchronously
     *   NDIS_STATUS_PENDING - Request will complete asynchronously via NdisMOidRequestComplete
     *   Other error status - Request failed
     */
    if (IsDirect)
    {
#if NDIS_SUPPORT_NDIS61
        MINIPORT_OID_REQUEST *DirectHandler =
            (MINIPORT_OID_REQUEST*)DriverBlock->Characteristics.DirectOidRequestHandler;
        DPRINT1("NDIS6: Calling DirectOidRequestHandler for OID 0x%08x\n", Oid);
        Status = DirectHandler(MiniportAdapterContext, Request);
#else
        Status = NDIS_STATUS_NOT_SUPPORTED;
#endif
    }
    else
    {
        DPRINT1("NDIS6: Calling OidRequestHandler for OID 0x%08x\n", Oid);
        Status = DriverBlock->Characteristics.OidRequestHandler(MiniportAdapterContext, Request);
    }

    DPRINT1("NDIS6: OidRequestHandler returned 0x%x\n", Status);

    /*
     * If the request completed synchronously (not PENDING),
     * we need to complete our tracking context now.
     */
    if (Status != NDIS_STATUS_PENDING)
    {
        /* Mark as completed */
        Context->CompletionStatus = Status;
        Context->IsPending = FALSE;
        KeSetEvent(&Context->CompletionEvent, IO_NO_INCREMENT, FALSE);

        /* Free the context since we're not waiting for async completion */
        Ndis6iFreeOidRequestContext(Context);
    }

    DPRINT("NDIS6: Ndis6iSubmitOidRequest returning 0x%x\n", Status);

    return Status;
}

/*
 * Ndis6iSynchronousOidRequest
 * Internal function to submit a synchronous OID request
 *
 * This function submits an OID request and waits for completion.
 * Used internally by NDIS for OID requests that must complete before
 * the function returns.
 *
 * Parameters:
 *   MiniportHandle - Handle to the miniport adapter
 *   Request - Pointer to the OID request
 *
 * Returns:
 *   The completion status of the OID request
 */
NDIS_STATUS
Ndis6iSynchronousOidRequest(
    _In_ NDIS_HANDLE MiniportHandle,
    _In_ PNDIS_OID_REQUEST Request)
{
    PNDIS6_OID_REQUEST_CONTEXT Context;
    NDIS_STATUS Status;
    NTSTATUS WaitStatus;
    LARGE_INTEGER Timeout;
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PVOID MiniportAdapterContext;

    DPRINT1("Ndis6iSynchronousOidRequest: Handle=%p, Request=%p\n",
        MiniportHandle, Request);

    /* Validate parameters */
    if (MiniportHandle == NULL || Request == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Validate request header */
    if (Request->Header.Type != NDIS_OBJECT_TYPE_OID_REQUEST ||
        Request->Header.Revision < NDIS_OID_REQUEST_REVISION_1)
    {
        DPRINT1("NDIS6: Invalid OID request header (sync)\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (!Ndis6iResolveMiniportContext(MiniportHandle, &DriverBlock, &MiniportAdapterContext))
    {
        DPRINT1("NDIS6: Cannot resolve miniport context (sync)\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (DriverBlock == NULL || DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        DPRINT1("NDIS6: No OidRequestHandler (sync)\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Allocate tracking context and hold an extra reference for the waiter */
    Context = Ndis6iAllocateOidRequestContext(MiniportHandle, Request, FALSE);
    if (Context == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }
    InterlockedIncrement(&Context->RefCount);

    /* Call the miniport's OID request handler */
    Status = DriverBlock->Characteristics.OidRequestHandler(MiniportAdapterContext, Request);

    if (Status != NDIS_STATUS_PENDING)
    {
        /* Synchronous completion */
        Ndis6iCompleteOidRequest(Context, Status, FALSE);
        Ndis6iDereferenceOidRequestContext(Context); /* completion */
        Ndis6iDereferenceOidRequestContext(Context); /* waiter */
        return Status;
    }

    /* Wait for completion with timeout */
    if (Request->Timeout != 0)
    {
        /* Use specified timeout (in seconds, convert to 100ns units) */
        Timeout.QuadPart = -(LONGLONG)Request->Timeout * 10000000LL;
    }
    else
    {
        /* Default 30 second timeout */
        Timeout.QuadPart = -300000000LL; /* 30 seconds in 100ns units */
    }

    WaitStatus = KeWaitForSingleObject(&Context->CompletionEvent,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       &Timeout);

    if (WaitStatus == STATUS_TIMEOUT)
    {
        DPRINT1("Synchronous OID request timed out\n");
        Context->IsCancelled = TRUE;
        Status = NDIS_STATUS_REQUEST_ABORTED;
    }
    else
    {
        Status = Context->CompletionStatus;
    }

    /* Release waiter's reference; completion path will release its own */
    Ndis6iDereferenceOidRequestContext(Context);

    DPRINT("Ndis6iSynchronousOidRequest returning 0x%x\n", Status);

    return Status;
}

#if NDIS_LEGACY_DRIVER
/*
 * Ndis6iMapLegacyOidToNdis6
 * Maps a legacy NDIS_REQUEST to an NDIS_OID_REQUEST
 *
 * This function is used for backward compatibility to support protocols
 * or code that still uses the legacy NDIS_REQUEST structure.
 *
 * Parameters:
 *   LegacyRequest - Pointer to the legacy NDIS_REQUEST
 *   Ndis6Request - Pointer to receive the NDIS_OID_REQUEST
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS on success
 *   Error status on failure
 *
 * Note: The caller is responsible for freeing the Ndis6Request when done.
 */
NDIS_STATUS
Ndis6iMapLegacyOidToNdis6(
    _In_ PNDIS_REQUEST LegacyRequest,
    _Out_ PNDIS_OID_REQUEST *Ndis6Request)
{
    PNDIS_OID_REQUEST Request;

    DPRINT1("Ndis6iMapLegacyOidToNdis6: LegacyRequest=%p\n",
        LegacyRequest);

    if (LegacyRequest == NULL || Ndis6Request == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *Ndis6Request = NULL;

    /* Allocate NDIS 6.x request */
    Request = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(NDIS_OID_REQUEST),
                                    NDIS_TAG);
    if (Request == NULL)
    {
        DPRINT1("Failed to allocate NDIS_OID_REQUEST\n");
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Request, sizeof(NDIS_OID_REQUEST));

    /* Initialize header */
    Request->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Request->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Request->Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;

    /* Map request type */
    Request->RequestType = LegacyRequest->RequestType;
    Request->PortNumber = NDIS_DEFAULT_PORT_NUMBER;
    Request->Timeout = 0;
    Request->RequestId = NULL;
    Request->RequestHandle = NULL;

    /* Map request data based on type */
    switch (LegacyRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            Request->DATA.QUERY_INFORMATION.Oid =
                LegacyRequest->DATA.QUERY_INFORMATION.Oid;
            Request->DATA.QUERY_INFORMATION.InformationBuffer =
                LegacyRequest->DATA.QUERY_INFORMATION.InformationBuffer;
            Request->DATA.QUERY_INFORMATION.InformationBufferLength =
                LegacyRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
            Request->DATA.QUERY_INFORMATION.BytesWritten = 0;
            Request->DATA.QUERY_INFORMATION.BytesNeeded = 0;
            break;

        case NdisRequestSetInformation:
            Request->DATA.SET_INFORMATION.Oid =
                LegacyRequest->DATA.SET_INFORMATION.Oid;
            Request->DATA.SET_INFORMATION.InformationBuffer =
                LegacyRequest->DATA.SET_INFORMATION.InformationBuffer;
            Request->DATA.SET_INFORMATION.InformationBufferLength =
                LegacyRequest->DATA.SET_INFORMATION.InformationBufferLength;
            Request->DATA.SET_INFORMATION.BytesRead = 0;
            Request->DATA.SET_INFORMATION.BytesNeeded = 0;
            break;

        default:
            DPRINT1("Unsupported legacy request type: %d\n",
                LegacyRequest->RequestType);
            ExFreePoolWithTag(Request, NDIS_TAG);
            return NDIS_STATUS_NOT_SUPPORTED;
    }

    *Ndis6Request = Request;

    DPRINT1("Mapped legacy request to NDIS 6.x request %p\n", Request);

    return NDIS_STATUS_SUCCESS;
}

/*
 * Ndis6iFreeMappedOidRequest
 * Frees an NDIS_OID_REQUEST created by Ndis6iMapLegacyOidToNdis6
 *
 * Parameters:
 *   Request - Pointer to the NDIS_OID_REQUEST to free
 */
VOID
Ndis6iFreeMappedOidRequest(
    _In_ PNDIS_OID_REQUEST Request)
{
    if (Request != NULL)
    {
        ExFreePoolWithTag(Request, NDIS_TAG);
    }
}

/*
 * Ndis6iCopyOidResultsToLegacy
 * Copies results from an NDIS_OID_REQUEST back to a legacy NDIS_REQUEST
 *
 * Parameters:
 *   Ndis6Request - Pointer to the completed NDIS_OID_REQUEST
 *   LegacyRequest - Pointer to the legacy NDIS_REQUEST to update
 */
VOID
Ndis6iCopyOidResultsToLegacy(
    _In_ PNDIS_OID_REQUEST Ndis6Request,
    _Inout_ PNDIS_REQUEST LegacyRequest)
{
    if (Ndis6Request == NULL || LegacyRequest == NULL)
    {
        return;
    }

    switch (LegacyRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            LegacyRequest->DATA.QUERY_INFORMATION.BytesWritten =
                Ndis6Request->DATA.QUERY_INFORMATION.BytesWritten;
            LegacyRequest->DATA.QUERY_INFORMATION.BytesNeeded =
                Ndis6Request->DATA.QUERY_INFORMATION.BytesNeeded;
            break;

        case NdisRequestSetInformation:
            LegacyRequest->DATA.SET_INFORMATION.BytesRead =
                Ndis6Request->DATA.SET_INFORMATION.BytesRead;
            LegacyRequest->DATA.SET_INFORMATION.BytesNeeded =
                Ndis6Request->DATA.SET_INFORMATION.BytesNeeded;
            break;

        default:
            break;
    }
}
#endif /* NDIS_LEGACY_DRIVER */

/*
 * Ndis6iHandleOidRequest
 * Handles legacy NDIS_REQUEST for NDIS 6.x adapters
 *
 * This function is called by MiniDoRequest when it detects that an
 * adapter is an NDIS 6.x adapter. It converts the legacy NDIS_REQUEST
 * to an NDIS_OID_REQUEST and submits it to the miniport's OidRequestHandler.
 *
 * Parameters:
 *   Adapter - Pointer to the LOGICAL_ADAPTER
 *   NdisRequest - Pointer to the legacy NDIS_REQUEST
 *
 * Returns:
 *   The completion status of the OID request
 */
NDIS_STATUS
Ndis6iHandleOidRequest(
    PLOGICAL_ADAPTER Adapter,
    PNDIS_REQUEST NdisRequest)
{
    NDIS_OID_REQUEST OidRequest;
    NDIS_STATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;

    DPRINT("Ndis6iHandleOidRequest - Adapter=%p, Request=%p, Type=%d\n",
           Adapter, NdisRequest, NdisRequest->RequestType);

    /* Get the device object from the miniport block */
    DeviceObject = Adapter->NdisMiniportBlock.DeviceObject;
    if (DeviceObject == NULL || DeviceObject->DeviceExtension == NULL)
    {
        DPRINT1("No device object\n");
        return NDIS_STATUS_FAILURE;
    }

    /* Get the driver block and adapter context from device extension */
    DriverBlock = (PNDIS6_MINIPORT_DRIVER_BLOCK)((PVOID*)DeviceObject->DeviceExtension)[0];

    if (DriverBlock == NULL)
    {
        DPRINT1("No driver block\n");
        return NDIS_STATUS_FAILURE;
    }

    if (DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        DPRINT1("No OidRequestHandler\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Build an NDIS_OID_REQUEST from the legacy NDIS_REQUEST */
    RtlZeroMemory(&OidRequest, sizeof(OidRequest));

    OidRequest.Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    OidRequest.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    OidRequest.Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    OidRequest.RequestType = NdisRequest->RequestType;
    OidRequest.PortNumber = NDIS_DEFAULT_PORT_NUMBER;
    OidRequest.Timeout = 0;
    OidRequest.RequestId = NULL;
    OidRequest.RequestHandle = NULL;

    switch (NdisRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            OidRequest.DATA.QUERY_INFORMATION.Oid =
                NdisRequest->DATA.QUERY_INFORMATION.Oid;
            OidRequest.DATA.QUERY_INFORMATION.InformationBuffer =
                NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer;
            OidRequest.DATA.QUERY_INFORMATION.InformationBufferLength =
                NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
            OidRequest.DATA.QUERY_INFORMATION.BytesWritten = 0;
            OidRequest.DATA.QUERY_INFORMATION.BytesNeeded = 0;

            DPRINT("Query OID 0x%08x\n", OidRequest.DATA.QUERY_INFORMATION.Oid);
            break;

        case NdisRequestSetInformation:
            OidRequest.DATA.SET_INFORMATION.Oid =
                NdisRequest->DATA.SET_INFORMATION.Oid;
            OidRequest.DATA.SET_INFORMATION.InformationBuffer =
                NdisRequest->DATA.SET_INFORMATION.InformationBuffer;
            OidRequest.DATA.SET_INFORMATION.InformationBufferLength =
                NdisRequest->DATA.SET_INFORMATION.InformationBufferLength;
            OidRequest.DATA.SET_INFORMATION.BytesRead = 0;
            OidRequest.DATA.SET_INFORMATION.BytesNeeded = 0;

            DPRINT("Set OID 0x%08x\n", OidRequest.DATA.SET_INFORMATION.Oid);
            break;

        default:
            DPRINT1("Unsupported request type %d\n", NdisRequest->RequestType);
            return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Submit synchronously to handle NDIS_STATUS_PENDING correctly */
    Status = Ndis6iSynchronousOidRequest((NDIS_HANDLE)DeviceObject, &OidRequest);

    DPRINT("OidRequestHandler (sync) returned 0x%x\n", Status);

    /* Copy results back to legacy request */
    switch (NdisRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            NdisRequest->DATA.QUERY_INFORMATION.BytesWritten =
                OidRequest.DATA.QUERY_INFORMATION.BytesWritten;
            NdisRequest->DATA.QUERY_INFORMATION.BytesNeeded =
                OidRequest.DATA.QUERY_INFORMATION.BytesNeeded;
            break;

        case NdisRequestSetInformation:
            NdisRequest->DATA.SET_INFORMATION.BytesRead =
                OidRequest.DATA.SET_INFORMATION.BytesRead;
            NdisRequest->DATA.SET_INFORMATION.BytesNeeded =
                OidRequest.DATA.SET_INFORMATION.BytesNeeded;
            break;

        default:
            break;
    }

    return Status;
}

#endif /* NDIS_SUPPORT_NDIS6 */

/* EOF */
