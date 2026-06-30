/*
 * PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel-to-kernel communication channel to \Device\DxgKrnl
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 *
 * BACKGROUND
 * ----------
 * On Windows Vista+, win32k communicates with dxgkrnl through a private
 * interface that is negotiated at startup.  The exact mechanism differs
 * across Windows versions; the broad shape is:
 *
 *   1. win32k opens \Device\DxgKrnl (IoGetDeviceObjectPointer).
 *   2. win32k sends an IOCTL asking dxgkrnl to fill in the
 *      WIN32K_DXGKRNL_INTERFACE callback table.
 *   3. win32k stores the table and dispatches each NtGdiDdDDI* syscall
 *      directly to the appropriate callback — no further IOCTL overhead.
 *
 * In the current ReactOS path, win32k must complete the private interface
 * exchange before D3DKMT thunks are exposed.  A missing or incompatible
 * dxgkrnl device is reported as a real failure rather than a partial WDDM
 * startup.
 *
 * This file provides:
 *   WddmBridgeInit()        - open the dxgkrnl device object
 *   WddmBridgeSendIoctl()   - synchronous kernel-to-kernel IOCTL helper
 *   WddmBridgeCleanup()     - release the file object reference
 */

#include <ntifs.h>
#include <windef.h>
#include <d3dkmthk.h>
#include <reactos/rddm/rxgkinterface.h>
#include "wddm_bridge.h"
#define NDEBUG
#include <debug.h>

/* Import IOCTL and handshake definitions from dxgkrnl */
#define DXGKRNL_DEVICE_TYPE     0x23

#define IOCTL_DXGKRNL_EXCHANGE_INTERFACE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x200, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define DXGKRNL_INTERFACE_VERSION_1  1

typedef struct _DXGKRNL_INTERFACE_EXCHANGE_IN
{
    ULONG Version;
    ULONG Size;
} DXGKRNL_INTERFACE_EXCHANGE_IN, *PDXGKRNL_INTERFACE_EXCHANGE_IN;

/* ---- Global state -------------------------------------------------------- */

/*
 * Reference to \Device\DxgKrnl.
 * g_DxgkrnlFileObject is the authoritative lifetime handle; the device object
 * pointer is cached separately to avoid a repeated dereference on every IOCTL.
 *
 * Both are NULL until WddmBridgeInit() has been called successfully.
 */
PFILE_OBJECT   g_DxgkrnlFileObject   = NULL;
PDEVICE_OBJECT g_DxgkrnlDeviceObject = NULL;
static NTSTATUS g_WddmBridgeStatus = STATUS_DEVICE_NOT_CONNECTED;
static REACTOS_WIN32K_DXGKRNL_INTERFACE g_DxgkrnlInterface;
static BOOLEAN g_DxgkrnlInterfaceValid = FALSE;

/* ---- Helpers ------------------------------------------------------------- */

static VOID
WddmBridgeDropReference(VOID)
{
    PFILE_OBJECT FileObject = g_DxgkrnlFileObject;

    if (FileObject != NULL)
    {
        DPRINT("WddmBridgeDropReference: releasing \\Device\\DxgKrnl file object %p\n",
               FileObject);

        g_DxgkrnlDeviceObject = NULL;
        g_DxgkrnlFileObject = NULL;
        g_DxgkrnlInterfaceValid = FALSE;
        RtlZeroMemory(&g_DxgkrnlInterface, sizeof(g_DxgkrnlInterface));

        ObDereferenceObject(FileObject);
    }
}

NTSTATUS
WddmBridgeGetStatus(VOID)
{
    return g_WddmBridgeStatus;
}

NTSTATUS
WddmBridgeRequireReady(VOID)
{
    NTSTATUS Status = WddmBridgeGetStatus();

    if (!NT_SUCCESS(Status))
        return Status;

    if (g_DxgkrnlFileObject == NULL || g_DxgkrnlDeviceObject == NULL)
        return STATUS_DEVICE_NOT_CONNECTED;

    return STATUS_SUCCESS;
}

BOOLEAN
WddmBridgeIsReady(VOID)
{
    return NT_SUCCESS(WddmBridgeRequireReady());
}

NTSTATUS
WddmBridgeGetInterface(
    _Out_ PREACTOS_WIN32K_DXGKRNL_INTERFACE Interface)
{
    NTSTATUS Status;

    if (Interface == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Interface, sizeof(*Interface));

    Status = WddmBridgeRequireReady();
    if (!NT_SUCCESS(Status))
        return Status;

    if (!g_DxgkrnlInterfaceValid)
        return STATUS_DEVICE_NOT_READY;

    RtlCopyMemory(Interface, &g_DxgkrnlInterface, sizeof(*Interface));
    return STATUS_SUCCESS;
}

/*
 * WddmBridgeInit
 *
 * Opens \Device\DxgKrnl and stores the resulting file and device object
 * pointers so that later callers can use WddmBridgeSendIoctl.
 *
 * Called from DxStartupDxgkInt() which is invoked during win32k
 * initialisation (InitializeGreCSRSS in win32ss/gdi/ntgdi/init.c).
 *
 * It is safe to call this function more than once: subsequent calls are
 * no-ops if the bridge is already open.
 *
 * x86/amd64 note: IoGetDeviceObjectPointer acquires a reference on the file
 * object.  On x86-64 the IRQL is PASSIVE_LEVEL during driver/module init,
 * which is required by this API — do not call from a raised-IRQL context.
 */
NTSTATUS
WddmBridgeInit(VOID)
{
    NTSTATUS         Status;
    UNICODE_STRING   DxgKrnlName;
    PFILE_OBJECT     FileObject;
    PDEVICE_OBJECT   DeviceObject;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        DPRINT1("WddmBridgeInit: called at IRQL %lu\n", KeGetCurrentIrql());
        return STATUS_INVALID_DEVICE_STATE;
    }

    /*
     * Idempotency guard: if we already have a valid file object the bridge
     * has been successfully opened.  Return early to avoid a redundant open
     * (and a leaked reference).
     */
    if (g_DxgkrnlFileObject != NULL)
    {
        DPRINT("WddmBridgeInit: bridge already open, skipping\n");
        return WddmBridgeRequireReady();
    }

    RtlInitUnicodeString(&DxgKrnlName, L"\\Device\\DxgKrnl");

    /*
     * Acquire a reference to the dxgkrnl device object.
     * FILE_ALL_ACCESS is used for now; it may be narrowed once the private
     * IOCTL interface is fully documented.
     *
     * If dxgkrnl has not yet created its device object (e.g. it is loaded
     * after win32k), this call returns STATUS_OBJECT_NAME_NOT_FOUND.
     * The caller (DxStartupDxgkInt) is expected to handle this gracefully
     * and retry later, or fall back to the legacy XDDM path.
     */
    Status = IoGetDeviceObjectPointer(&DxgKrnlName,
                                      FILE_ALL_ACCESS,
                                      &FileObject,
                                      &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        g_WddmBridgeStatus = Status;
        DPRINT1("WddmBridgeInit: IoGetDeviceObjectPointer(\\Device\\DxgKrnl) "
                "failed with 0x%08lX\n", Status);
        return Status;
    }

    /*
     * Commit the pointers.  We store both so that:
     *   - g_DxgkrnlFileObject  tracks the lifetime reference.
     *   - g_DxgkrnlDeviceObject is passed directly to IoCallDriver, avoiding
     *     a pointer dereference through the file object on every IOCTL.
     *
     * On SMP systems these stores are visible to all processors before any
     * subsequent use because win32k's initialisation path runs while the
     * system is still single-threaded with respect to these globals.
     * If that assumption ever changes a cmpxchg/InterlockedCompareExchangePointer
     * should be used here.
     */
    g_DxgkrnlFileObject   = FileObject;
    g_DxgkrnlDeviceObject = DeviceObject;
    g_WddmBridgeStatus = STATUS_DEVICE_NOT_READY;

    DPRINT("WddmBridgeInit: \\Device\\DxgKrnl opened successfully "
           "(FileObject=%p, DeviceObject=%p)\n",
           g_DxgkrnlFileObject, g_DxgkrnlDeviceObject);

    /*
     * CORE-20027: Send the interface exchange IOCTL to dxgkrnl.
     *
     * This is a version handshake: win32k sends its expected interface
     * version and buffer size; dxgkrnl validates and acknowledges.
     * On success, win32k fills the callback table locally using
     * WddmBridgeInitCallbacks() (the function pointers are win32k-side
     * shims that forward via WddmBridgeSendIoctl).
     *
     * The exchange IOCTL uses METHOD_BUFFERED with
     * IRP_MJ_INTERNAL_DEVICE_CONTROL for kernel-to-kernel communication.
     */
    {
        DXGKRNL_INTERFACE_EXCHANGE_IN ExchangeIn;
        REACTOS_WIN32K_DXGKRNL_INTERFACE ExchangeOut;

        ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_1;
        ExchangeIn.Size = sizeof(REACTOS_WIN32K_DXGKRNL_INTERFACE);
        RtlZeroMemory(&ExchangeOut, sizeof(ExchangeOut));

        Status = WddmBridgeSendIoctl(
            IOCTL_DXGKRNL_EXCHANGE_INTERFACE,
            &ExchangeIn, sizeof(ExchangeIn),
            &ExchangeOut, sizeof(ExchangeOut));

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("WddmBridgeInit: IOCTL_DXGKRNL_EXCHANGE_INTERFACE "
                    "failed with 0x%08lX\n", Status);
            WddmBridgeDropReference();
            g_WddmBridgeStatus = Status;
            return Status;
        }
        else
        {
            if (ExchangeOut.RxgkIntPfnCreateDevice == NULL ||
                ExchangeOut.RxgkIntPfnPresent == NULL ||
                ExchangeOut.RxgkIntPfnQueryAdapterInfo == NULL)
            {
                DPRINT1("WddmBridgeInit: interface exchange returned an "
                        "incomplete direct callback table\n");
                WddmBridgeDropReference();
                g_WddmBridgeStatus = STATUS_INVALID_DEVICE_STATE;
                return STATUS_INVALID_DEVICE_STATE;
            }

            RtlCopyMemory(&g_DxgkrnlInterface,
                          &ExchangeOut,
                          sizeof(g_DxgkrnlInterface));
            g_DxgkrnlInterfaceValid = TRUE;

            DPRINT("WddmBridgeInit: interface exchange succeeded, direct "
                   "dxgkrnl callbacks cached\n");
        }
    }

    g_WddmBridgeStatus = STATUS_SUCCESS;
    return STATUS_SUCCESS;
}

/*
 * WddmBridgeSendIoctl
 *
 * Issues an IRP_MJ_INTERNAL_DEVICE_CONTROL request to \Device\DxgKrnl and
 * waits synchronously for completion.
 *
 * Using IRP_MJ_INTERNAL_DEVICE_CONTROL (rather than IRP_MJ_DEVICE_CONTROL)
 * keeps the call fully in kernel mode: the I/O manager does not validate
 * user-mode buffer accessibility and there is no user↔kernel copy overhead.
 * This is standard practice for kernel-to-kernel driver communication on
 * Windows (e.g. storport ↔ miniport, ndis ↔ miniport).
 *
 * The private D3DKMT IOCTLs currently use METHOD_BUFFERED.  The input/output
 * buffer pairs passed here are kernel addresses because this is a
 * kernel-to-kernel call, but dxgkrnl still validates the IOCTL buffer sizes.
 *
 * Caller must be at IRQL <= APC_LEVEL.
 */
NTSTATUS
WddmBridgeSendIoctl(
    _In_      ULONG  IoControlCode,
    _In_opt_  PVOID  InputBuffer,
    _In_      ULONG  InputSize,
    _Out_opt_ PVOID  OutputBuffer,
    _In_      ULONG  OutputSize)
{
    KEVENT           Event;
    IO_STATUS_BLOCK  IoStatus;
    PIRP             Irp;
    NTSTATUS         Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    if (KeGetCurrentIrql() > APC_LEVEL)
    {
        DPRINT1("WddmBridgeSendIoctl: called at IRQL %lu for IOCTL 0x%08lX\n",
                KeGetCurrentIrql(), IoControlCode);
        return STATUS_INVALID_DEVICE_STATE;
    }

    if ((InputBuffer == NULL && InputSize != 0) ||
        (InputBuffer != NULL && InputSize == 0) ||
        (OutputBuffer == NULL && OutputSize != 0) ||
        (OutputBuffer != NULL && OutputSize == 0))
    {
        DPRINT1("WddmBridgeSendIoctl: invalid buffer/size pair for IOCTL 0x%08lX "
                "(in=%p/%lu out=%p/%lu)\n",
                IoControlCode,
                InputBuffer,
                InputSize,
                OutputBuffer,
                OutputSize);
        return STATUS_INVALID_PARAMETER;
    }

    /* Bridge must be open before sending any IOCTL. */
    if (g_DxgkrnlDeviceObject == NULL)
    {
        Status = WddmBridgeGetStatus();
        if (NT_SUCCESS(Status))
            Status = STATUS_DEVICE_NOT_CONNECTED;

        DPRINT1("WddmBridgeSendIoctl: bridge not initialised "
                "(IoControlCode=0x%08lX, status=0x%08lX)\n",
                IoControlCode,
                Status);
        return Status;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /*
     * IoBuildDeviceIoControlRequest builds an IRP suitable for a
     * IRP_MJ_DEVICE_CONTROL request.  For an internal (kernel-mode-only)
     * IOCTL we patch the MajorFunction field after the fact.
     *
     * Note: IoBuildDeviceIoControlRequest allocates the IRP from non-paged
     * pool.  The event and IO_STATUS_BLOCK must remain valid until the IRP
     * completes — both are stack-allocated here so that is guaranteed by the
     * synchronous wait below.
     */
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        g_DxgkrnlDeviceObject,
                                        InputBuffer,
                                        InputSize,
                                        OutputBuffer,
                                        OutputSize,
                                        TRUE,        /* InternalDeviceIoControl */
                                        &Event,
                                        &IoStatus);
    if (Irp == NULL)
    {
        DPRINT1("WddmBridgeSendIoctl: IoBuildDeviceIoControlRequest failed "
                "(IoControlCode=0x%08lX, out of memory)\n", IoControlCode);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Patch to IRP_MJ_INTERNAL_DEVICE_CONTROL.
     * IoBuildDeviceIoControlRequest always sets MajorFunction to
     * IRP_MJ_DEVICE_CONTROL regardless of the InternalDeviceIoControl flag
     * in some ReactOS versions.  Explicitly setting the field here is
     * unconditionally correct.
     */
    IoGetNextIrpStackLocation(Irp)->MajorFunction =
        IRP_MJ_INTERNAL_DEVICE_CONTROL;

    Status = IoCallDriver(g_DxgkrnlDeviceObject, Irp);

    /* Wait for asynchronous completion. */
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WddmBridgeSendIoctl: IOCTL 0x%08lX failed with 0x%08lX\n",
                IoControlCode, Status);
    }

    return Status;
}

/*
 * WddmBridgeCleanup
 *
 * Releases the file object reference acquired during WddmBridgeInit.
 * After this call g_DxgkrnlFileObject and g_DxgkrnlDeviceObject are NULL.
 *
 * Safe to call if WddmBridgeInit has never been called (or failed).
 *
 * Must be called from win32k's unload path before the driver is unloaded so
 * that dxgkrnl's device object reference count is balanced.
 */
VOID
WddmBridgeCleanup(VOID)
{
    WddmBridgeDropReference();
    g_WddmBridgeStatus = STATUS_DEVICE_NOT_CONNECTED;
}
