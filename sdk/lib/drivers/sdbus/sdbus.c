/*
 * PROJECT:     ReactOS SD Bus Client Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SdBusOpenInterface, SdBusSubmitRequest, SdBusSubmitRequestAsync
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * DESCRIPTION:
 *     This static library implements the client-side SD bus helper functions
 *     that SD function drivers (sffdisk.sys, sffp_sd.sys) link against.
 *     It corresponds to the sdbus.lib shipped with the Windows WDK.
 *
 *     - SdBusOpenInterface queries the bus interface from the PDO via
 *       IRP_MN_QUERY_INTERFACE.
 *     - SdBusSubmitRequest sends a synchronous request to the bus driver
 *       via IRP_MJ_INTERNAL_DEVICE_CONTROL.
 *     - SdBusSubmitRequestAsync sends an asynchronous request using a
 *       caller-provided IRP.
 */

/* INCLUDES *******************************************************************/

#include <ntddk.h>
#include <ntddsd.h>

#define NDEBUG
#include <debug.h>

/* PRIVATE DEFINITIONS ********************************************************/

/**
 * @brief Retrieve the PDO device object from the SD bus interface context.
 *
 * The SD bus driver's interface context (SDBUS_INTERFACE_CONTEXT) has the
 * following layout:
 *
 *   offset 0: PPDO_EXTENSION  PdoExtension
 *
 * And PDO_EXTENSION embeds SDBUS_COMMON_EXTENSION as its first member:
 *
 *   offset 0: PDEVICE_OBJECT  Self
 *
 * Therefore, the PDO device object can be retrieved by double-dereferencing
 * the interface context pointer. This layout knowledge is equivalent to what
 * Microsoft's sdbus.lib has about the Windows SD bus driver internals.
 *
 * @param InterfaceContext  The Context field from SDBUS_INTERFACE_STANDARD.
 */
#define SDBUS_GET_PDO_FROM_CONTEXT(InterfaceContext) \
    (*(PDEVICE_OBJECT *)(*(PVOID *)(InterfaceContext)))

typedef struct _SDBUS_SYNC_REQUEST_CONTEXT
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
} SDBUS_SYNC_REQUEST_CONTEXT, *PSDBUS_SYNC_REQUEST_CONTEXT;

/*
 * GUID for the SD bus interface. Defined locally to avoid dependency
 * on initguid.h in the caller.
 */
static const GUID GUID_SDBUS_INTERFACE_STANDARD_LOCAL =
    { 0x6BB24D81, 0xE924, 0x4825, { 0xAF, 0x49, 0x3A, 0xCD, 0x33, 0xC1, 0xD8, 0x20 } };

static
NTSTATUS
NTAPI
SdBusSubmitRequestCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PSDBUS_SYNC_REQUEST_CONTEXT RequestContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    RequestContext = (PSDBUS_SYNC_REQUEST_CONTEXT)Context;
    RequestContext->IoStatus = Irp->IoStatus;
    KeSetEvent(&RequestContext->Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/**
 * @brief Open the SDBUS_INTERFACE_STANDARD on the given PDO.
 *
 * Sends an IRP_MN_QUERY_INTERFACE PnP request down to the bus driver
 * to obtain the SD bus interface method pointers and context.
 *
 * @param[in]  Pdo                The physical device object representing the SD function.
 * @param[out] InterfaceStandard  Receives the bus interface method pointers and context.
 * @param[in]  Size               Size of the interface structure in bytes.
 * @param[in]  Version            Requested interface version (SDBUS_INTERFACE_VERSION).
 *
 * @return STATUS_SUCCESS, STATUS_BUFFER_TOO_SMALL, STATUS_INSUFFICIENT_RESOURCES,
 *         or an error from the bus driver.
 */
NTSTATUS
NTAPI
SdBusOpenInterface(
    _In_ PDEVICE_OBJECT Pdo,
    _Out_ PSDBUS_INTERFACE_STANDARD InterfaceStandard,
    _In_ USHORT Size,
    _In_ USHORT Version)
{
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    if (Size < sizeof(SDBUS_INTERFACE_STANDARD))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(InterfaceStandard, Size);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       Pdo,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* PnP IRPs must start with STATUS_NOT_SUPPORTED */
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_PNP;
    IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IoStack->Parameters.QueryInterface.InterfaceType =
        &GUID_SDBUS_INTERFACE_STANDARD_LOCAL;
    IoStack->Parameters.QueryInterface.Size = Size;
    IoStack->Parameters.QueryInterface.Version = Version;
    IoStack->Parameters.QueryInterface.Interface =
        (PINTERFACE)InterfaceStandard;
    IoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(Pdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

/**
 * @brief Submit a synchronous SD bus request to the bus driver.
 *
 * Builds an IRP_MJ_INTERNAL_DEVICE_CONTROL IRP, places the request packet
 * pointer in Parameters.Others.Argument1 where the bus driver expects it,
 * and waits for completion.
 *
 * @param[in] InterfaceContext  The Context field from SDBUS_INTERFACE_STANDARD.
 * @param[in] Packet            Pointer to the SD bus request packet describing the operation.
 *
 * @return NTSTATUS from the bus driver.
 */
NTSTATUS
NTAPI
SdBusSubmitRequest(
    _In_ PVOID InterfaceContext,
    _In_ PSDBUS_REQUEST_PACKET Packet)
{
    PDEVICE_OBJECT Pdo;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    SDBUS_SYNC_REQUEST_CONTEXT RequestContext;
    NTSTATUS Status;

    Pdo = SDBUS_GET_PDO_FROM_CONTEXT(InterfaceContext);

    KeInitializeEvent(&RequestContext.Event, NotificationEvent, FALSE);
    RequestContext.IoStatus.Status = STATUS_PENDING;
    RequestContext.IoStatus.Information = 0;

    Irp = IoAllocateIrp(Pdo->StackSize, FALSE);
    if (Irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.Others.Argument1 = (PVOID)Packet;

    IoSetCompletionRoutine(Irp,
                           SdBusSubmitRequestCompletion,
                           &RequestContext,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(Pdo, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&RequestContext.Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }
    else if (RequestContext.IoStatus.Status == STATUS_PENDING)
    {
        RequestContext.IoStatus.Status = Status;
    }

    Status = RequestContext.IoStatus.Status;
    IoFreeIrp(Irp);

    return Status;
}

/**
 * @brief Submit an asynchronous SD bus request to the bus driver.
 *
 * The caller provides a pre-allocated IRP. The completion routine will be
 * called when the bus driver completes the request.
 *
 * @param[in]     InterfaceContext   The Context field from SDBUS_INTERFACE_STANDARD.
 * @param[in]     Packet             Pointer to the SD bus request packet.
 * @param[in,out] Irp                A caller-allocated IRP to use for the request.
 * @param[in]     CompletionRoutine  Called when the bus driver completes the IRP.
 * @param[in]     UserContext        Context passed to the completion routine.
 *
 * @return NTSTATUS from IoCallDriver (typically STATUS_PENDING).
 */
NTSTATUS
NTAPI
SdBusSubmitRequestAsync(
    _In_ PVOID InterfaceContext,
    _In_ PSDBUS_REQUEST_PACKET Packet,
    _In_ PIRP Irp,
    _In_ PIO_COMPLETION_ROUTINE CompletionRoutine,
    _In_ PVOID UserContext)
{
    PDEVICE_OBJECT Pdo;
    PIO_STACK_LOCATION IoStack;

    Pdo = SDBUS_GET_PDO_FROM_CONTEXT(InterfaceContext);

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.Others.Argument1 = (PVOID)Packet;

    IoSetCompletionRoutine(Irp,
                           CompletionRoutine,
                           UserContext,
                           TRUE,
                           TRUE,
                           TRUE);

    return IoCallDriver(Pdo, Irp);
}
