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

C_ASSERT(DXGKRNL_INTERFACE_EXCHANGE_IN_LEGACY_SIZE == (2 * sizeof(ULONG)));
C_ASSERT(FIELD_OFFSET(DXGKRNL_INTERFACE_EXCHANGE_IN, ConfiguredWddmLevel) ==
         DXGKRNL_INTERFACE_EXCHANGE_IN_LEGACY_SIZE);
C_ASSERT(sizeof(DXGKRNL_INTERFACE_EXCHANGE_IN) == (3 * sizeof(ULONG)));

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
#if defined(REACTOS_GRAPHICS_DRIVER_MODEL_XPDM)
static NTSTATUS g_WddmBridgeStatus = STATUS_NOT_SUPPORTED;
#else
static NTSTATUS g_WddmBridgeStatus = STATUS_DEVICE_NOT_CONNECTED;
#endif
static REACTOS_WIN32K_DXGKRNL_INTERFACE g_DxgkrnlInterface;
static BOOLEAN g_DxgkrnlInterfaceValid = FALSE;
static ULONG g_DxgkrnlInterfaceVersion = 0;

static FAST_MUTEX g_WddmBridgeLock;
static volatile LONG g_WddmBridgeLockState = 0;
static EX_RUNDOWN_REF g_WddmBridgeRundown;
static BOOLEAN g_WddmBridgeRundownInitialized = FALSE;

typedef struct _WDDM_BRIDGE_COMPLETION_CONTEXT
{
    ULONG OutputSize;
    NTSTATUS FinalStatus;
    ULONG_PTR FinalInformation;
    volatile LONG FinalValid;
} WDDM_BRIDGE_COMPLETION_CONTEXT, *PWDDM_BRIDGE_COMPLETION_CONTEXT;

/* The mutex serializes state publication; rundown protects concurrent IOCTLs. */

static NTSTATUS
WddmBridgeSendIoctlToDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputSize,
    _Out_opt_ PULONG_PTR Information);

static NTSTATUS
NTAPI
WddmBridgeClampIoctlCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PVOID Context)
{
    PWDDM_BRIDGE_COMPLETION_CONTEXT CompletionContext = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->IoStatus.Information > CompletionContext->OutputSize)
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_INFO_LENGTH_MISMATCH;
    }

    /* IopCompleteRequest does not write the caller IOSB for an NT_ERROR
     * completion of a synchronous request, which would leave the send path
     * reading its stack preset instead of the real status.  Capture the
     * final IOSB here, before that gate. */
    CompletionContext->FinalStatus = Irp->IoStatus.Status;
    CompletionContext->FinalInformation = Irp->IoStatus.Information;
    KeMemoryBarrier();
    InterlockedExchange(&CompletionContext->FinalValid, 1);

    return STATUS_CONTINUE_COMPLETION;
}

/* ---- Helpers ------------------------------------------------------------- */

static VOID
WddmBridgeEnsureLockInitialized(VOID)
{
    LONG State = InterlockedCompareExchange(&g_WddmBridgeLockState, 1, 0);

    if (State == 0)
    {
        ExInitializeFastMutex(&g_WddmBridgeLock);
        KeMemoryBarrier();
        InterlockedExchange(&g_WddmBridgeLockState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_WddmBridgeLockState, 2, 2) != 2)
        YieldProcessor();
}

static VOID
WddmBridgeAcquireLock(VOID)
{
    WddmBridgeEnsureLockInitialized();
    KeEnterCriticalRegion();
    ExAcquireFastMutexUnsafe(&g_WddmBridgeLock);
}

static VOID
WddmBridgeReleaseLock(VOID)
{
    ExReleaseFastMutexUnsafe(&g_WddmBridgeLock);
    KeLeaveCriticalRegion();
}

static VOID
WddmBridgeDropReference(VOID)
{
    PFILE_OBJECT FileObject = g_DxgkrnlFileObject;

    g_DxgkrnlDeviceObject = NULL;
    g_DxgkrnlFileObject = NULL;
    g_DxgkrnlInterfaceValid = FALSE;
    g_DxgkrnlInterfaceVersion = 0;
    RtlZeroMemory(&g_DxgkrnlInterface, sizeof(g_DxgkrnlInterface));

    if (FileObject != NULL)
    {
        DPRINT("WddmBridgeDropReference: releasing \\Device\\DxgKrnl file object %p\n",
               FileObject);
        ObDereferenceObject(FileObject);
    }
}

NTSTATUS
WddmBridgeGetStatus(VOID)
{
    NTSTATUS Status;

    WddmBridgeAcquireLock();
    Status = g_WddmBridgeStatus;
    WddmBridgeReleaseLock();
    return Status;
}

NTSTATUS
WddmBridgeRequireReady(VOID)
{
    NTSTATUS Status;

    WddmBridgeAcquireLock();
    Status = g_WddmBridgeStatus;

    if (NT_SUCCESS(Status) && (g_DxgkrnlFileObject == NULL || g_DxgkrnlDeviceObject == NULL))
        Status = STATUS_DEVICE_NOT_CONNECTED;

    WddmBridgeReleaseLock();
    return Status;
}

BOOLEAN
WddmBridgeIsReady(VOID)
{
    return NT_SUCCESS(WddmBridgeRequireReady());
}

ULONG
WddmBridgeGetInterfaceVersion(VOID)
{
    ULONG Version;

    WddmBridgeAcquireLock();
    Version = g_DxgkrnlInterfaceValid ? g_DxgkrnlInterfaceVersion : 0;
    WddmBridgeReleaseLock();
    return Version;
}

NTSTATUS
WddmBridgeGetInterface(
    _Out_ PREACTOS_WIN32K_DXGKRNL_INTERFACE Interface)
{
    NTSTATUS Status;

    if (Interface == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Interface, sizeof(*Interface));
    WddmBridgeAcquireLock();
    Status = g_WddmBridgeStatus;
    if (NT_SUCCESS(Status) && (g_DxgkrnlFileObject == NULL || g_DxgkrnlDeviceObject == NULL))
        Status = STATUS_DEVICE_NOT_CONNECTED;
    if (NT_SUCCESS(Status) && !g_DxgkrnlInterfaceValid)
        Status = STATUS_DEVICE_NOT_READY;
    if (NT_SUCCESS(Status))
        RtlCopyMemory(Interface, &g_DxgkrnlInterface, sizeof(*Interface));
    WddmBridgeReleaseLock();
    return Status;
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
 * IoGetDeviceObjectPointer acquires a reference on the file object and
 * requires PASSIVE_LEVEL on every architecture.
 */
NTSTATUS
WddmBridgeInit(VOID)
{
    NTSTATUS Status;
    UNICODE_STRING DxgKrnlName;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    DXGKRNL_INTERFACE_EXCHANGE_IN ExchangeIn;
    REACTOS_WIN32K_DXGKRNL_INTERFACE ExchangeOut;
    ULONG_PTR Information = 0;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        DPRINT1("WddmBridgeInit: called at IRQL %lu\n", KeGetCurrentIrql());
        return STATUS_INVALID_DEVICE_STATE;
    }

    WddmBridgeAcquireLock();

    if (g_DxgkrnlFileObject != NULL)
    {
        DPRINT("WddmBridgeInit: bridge already open, skipping\n");
        Status = g_WddmBridgeStatus;
        if (NT_SUCCESS(Status) && g_DxgkrnlDeviceObject == NULL)
            Status = STATUS_DEVICE_NOT_CONNECTED;
        WddmBridgeReleaseLock();
        return Status;
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
    Status = IoGetDeviceObjectPointer(&DxgKrnlName, FILE_ALL_ACCESS, &FileObject, &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        g_WddmBridgeStatus = Status;
        DPRINT1("WddmBridgeInit: IoGetDeviceObjectPointer(\\Device\\DxgKrnl) "
                "failed with 0x%08lX\n", Status);
        WddmBridgeReleaseLock();
        return Status;
    }

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
    ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_CURRENT;
    ExchangeIn.Size = DXGKRNL_INTERFACE_VERSION_CURRENT_SIZE;
    ExchangeIn.ConfiguredWddmLevel = REACTOS_WDDM_TARGET_LEVEL;
    RtlZeroMemory(&ExchangeOut, sizeof(ExchangeOut));
    Status = WddmBridgeSendIoctlToDevice(DeviceObject, IOCTL_DXGKRNL_EXCHANGE_INTERFACE, &ExchangeIn, sizeof(ExchangeIn), &ExchangeOut, sizeof(ExchangeOut), &Information);

    /* An older dxgkrnl rejects version 6.  Retry the append-only v5 prefix. */
    if (Status == STATUS_NOT_SUPPORTED)
    {
        ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_5;
        ExchangeIn.Size = DXGKRNL_INTERFACE_VERSION_5_SIZE;
        RtlZeroMemory(&ExchangeOut, sizeof(ExchangeOut));
        Information = 0;
        Status = WddmBridgeSendIoctlToDevice(DeviceObject, IOCTL_DXGKRNL_EXCHANGE_INTERFACE, &ExchangeIn, sizeof(ExchangeIn), &ExchangeOut, sizeof(ExchangeOut), &Information);
    }

    /* A version-4 dxgkrnl rejects version 5.  Retry its exact prefix. */
    if (Status == STATUS_NOT_SUPPORTED)
    {
        ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_4;
        ExchangeIn.Size = DXGKRNL_INTERFACE_VERSION_4_SIZE;
        RtlZeroMemory(&ExchangeOut, sizeof(ExchangeOut));
        Information = 0;
        Status = WddmBridgeSendIoctlToDevice(DeviceObject, IOCTL_DXGKRNL_EXCHANGE_INTERFACE, &ExchangeIn, sizeof(ExchangeIn), &ExchangeOut, sizeof(ExchangeOut), &Information);
    }

    /* A version-3 dxgkrnl rejects version 4.  Retry its exact prefix. */
    if (Status == STATUS_NOT_SUPPORTED)
    {
        ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_3;
        ExchangeIn.Size = DXGKRNL_INTERFACE_VERSION_3_SIZE;
        RtlZeroMemory(&ExchangeOut, sizeof(ExchangeOut));
        Information = 0;
        Status = WddmBridgeSendIoctlToDevice(DeviceObject, IOCTL_DXGKRNL_EXCHANGE_INTERFACE, &ExchangeIn, sizeof(ExchangeIn), &ExchangeOut, sizeof(ExchangeOut), &Information);
    }

    /* A version-2 dxgkrnl rejects version 3.  Retry the append-only v2 prefix. */
    if (Status == STATUS_NOT_SUPPORTED)
    {
        ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_2;
        ExchangeIn.Size = DXGKRNL_INTERFACE_VERSION_2_SIZE;
        RtlZeroMemory(&ExchangeOut, sizeof(ExchangeOut));
        Information = 0;
        Status = WddmBridgeSendIoctlToDevice(DeviceObject, IOCTL_DXGKRNL_EXCHANGE_INTERFACE, &ExchangeIn, sizeof(ExchangeIn), &ExchangeOut, sizeof(ExchangeOut), &Information);
    }

    /* A version-1 dxgkrnl rejects version 2.  Retry with its prefix size. */
    if (Status == STATUS_NOT_SUPPORTED)
    {
        ExchangeIn.Version = DXGKRNL_INTERFACE_VERSION_1;
        ExchangeIn.Size = DXGKRNL_INTERFACE_VERSION_1_SIZE;
        RtlZeroMemory(&ExchangeOut, sizeof(ExchangeOut));
        Information = 0;
        Status = WddmBridgeSendIoctlToDevice(DeviceObject, IOCTL_DXGKRNL_EXCHANGE_INTERFACE, &ExchangeIn, sizeof(ExchangeIn), &ExchangeOut, sizeof(ExchangeOut), &Information);
    }

    if (NT_SUCCESS(Status) && Information != ExchangeIn.Size)
        Status = STATUS_INFO_LENGTH_MISMATCH;

    /* Private protocol revisions are append-only, not WDDM levels. */
    if (NT_SUCCESS(Status) &&
        (ExchangeOut.RxgkIntPfnCreateDevice == NULL ||
         ExchangeOut.RxgkIntPfnPresent == NULL ||
         ExchangeOut.RxgkIntPfnQueryAdapterInfo == NULL))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (NT_SUCCESS(Status) &&
        ExchangeIn.Version >= DXGKRNL_INTERFACE_VERSION_2 &&
        (ExchangeOut.RxgkIntPfnCreateContextVirtual == NULL ||
         ExchangeOut.RxgkIntPfnSubmitCommand == NULL))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }

    if (NT_SUCCESS(Status) &&
        ExchangeIn.Version >= DXGKRNL_INTERFACE_VERSION_6 &&
        (ExchangeOut.RxgkIntPfnWaitForSynchronizationObjectFromGpu == NULL ||
         ExchangeOut.RxgkIntPfnSignalSynchronizationObjectFromGpu == NULL ||
         ExchangeOut.RxgkIntPfnSignalSynchronizationObjectFromGpu2 == NULL))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 1105)
    if (NT_SUCCESS(Status) &&
        ExchangeIn.Version >= DXGKRNL_INTERFACE_VERSION_3 &&
        ExchangeOut.RxgkIntPfnCreateAllocation2 == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }

    if (NT_SUCCESS(Status) &&
        ExchangeIn.Version >= DXGKRNL_INTERFACE_VERSION_5 &&
        ExchangeOut.RxgkIntPfnOpenSynchronizationObject == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    if (NT_SUCCESS(Status) &&
        ExchangeIn.Version >= DXGKRNL_INTERFACE_VERSION_4 &&
        ExchangeOut.RxgkIntPfnGetAllocationPriority == NULL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
#endif

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WddmBridgeInit: interface exchange failed with 0x%08lX\n", Status);
        ObDereferenceObject(FileObject);
        g_WddmBridgeStatus = Status;
        WddmBridgeReleaseLock();
        return Status;
    }

    if (g_WddmBridgeRundownInitialized)
        ExReInitializeRundownProtection(&g_WddmBridgeRundown);
    else
    {
        ExInitializeRundownProtection(&g_WddmBridgeRundown);
        g_WddmBridgeRundownInitialized = TRUE;
    }

    g_DxgkrnlFileObject = FileObject;
    g_DxgkrnlDeviceObject = DeviceObject;
    RtlCopyMemory(&g_DxgkrnlInterface, &ExchangeOut, sizeof(g_DxgkrnlInterface));
    g_DxgkrnlInterfaceValid = TRUE;
    g_DxgkrnlInterfaceVersion = ExchangeIn.Version;
    g_WddmBridgeStatus = STATUS_SUCCESS;
    DPRINT("WddmBridgeInit: interface exchange v%lu succeeded, direct dxgkrnl callbacks cached\n", g_DxgkrnlInterfaceVersion);
    WddmBridgeReleaseLock();
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
    return WddmBridgeSendIoctlWithInformation(IoControlCode, InputBuffer, InputSize, OutputBuffer, OutputSize, NULL);
}

NTSTATUS
WddmBridgeSendIoctlWithInformation(
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputSize,
    _Out_opt_ PULONG_PTR Information)
{
    PDEVICE_OBJECT DeviceObject;
    BOOLEAN RundownAcquired = FALSE;
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    if (Information != NULL)
        *Information = 0;

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

    WddmBridgeAcquireLock();
    DeviceObject = g_DxgkrnlDeviceObject;
    Status = g_WddmBridgeStatus;
    if (NT_SUCCESS(Status) && DeviceObject == NULL)
        Status = STATUS_DEVICE_NOT_CONNECTED;
    if (NT_SUCCESS(Status) && (!g_WddmBridgeRundownInitialized || !ExAcquireRundownProtection(&g_WddmBridgeRundown)))
        Status = STATUS_DEVICE_NOT_CONNECTED;
    else if (NT_SUCCESS(Status))
        RundownAcquired = TRUE;

    WddmBridgeReleaseLock();

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WddmBridgeSendIoctl: bridge not initialised "
                "(IoControlCode=0x%08lX, status=0x%08lX)\n",
                IoControlCode,
                Status);
        return Status;
    }

    Status = WddmBridgeSendIoctlToDevice(DeviceObject, IoControlCode, InputBuffer, InputSize, OutputBuffer, OutputSize, Information);
    if (RundownAcquired)
        ExReleaseRundownProtection(&g_WddmBridgeRundown);
    return Status;
}

static NTSTATUS
WddmBridgeSendIoctlToDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputSize,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputSize,
    _Out_opt_ PULONG_PTR Information)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    WDDM_BRIDGE_COMPLETION_CONTEXT CompletionContext;
    PIRP Irp;
    NTSTATUS Status;

    if (Information != NULL)
        *Information = 0;

    if (DeviceObject == NULL)
        return STATUS_DEVICE_NOT_CONNECTED;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoStatus.Information = 0;
    CompletionContext.OutputSize = OutputSize;
    CompletionContext.FinalStatus = STATUS_NOT_SUPPORTED;
    CompletionContext.FinalInformation = 0;
    CompletionContext.FinalValid = 0;

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
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, InputBuffer, InputSize, OutputBuffer, OutputSize, TRUE, &Event, &IoStatus);
    if (Irp == NULL)
    {
        DPRINT1("WddmBridgeSendIoctl: IoBuildDeviceIoControlRequest failed "
                "(IoControlCode=0x%08lX, out of memory)\n", IoControlCode);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoSetCompletionRoutine(Irp, WddmBridgeClampIoctlCompletion, &CompletionContext, TRUE, TRUE, TRUE);

    /*
     * Patch to IRP_MJ_INTERNAL_DEVICE_CONTROL.
     * IoBuildDeviceIoControlRequest always sets MajorFunction to
     * IRP_MJ_DEVICE_CONTROL regardless of the InternalDeviceIoControl flag
     * in some ReactOS versions.  Explicitly setting the field here is
     * unconditionally correct.
     */
    IoGetNextIrpStackLocation(Irp)->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;

    Status = IoCallDriver(DeviceObject, Irp);

    /* Wait for asynchronous completion. */
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&CompletionContext.FinalValid, 0, 0) != 0)
    {
        KeMemoryBarrier();
        IoStatus.Status = CompletionContext.FinalStatus;
        IoStatus.Information = CompletionContext.FinalInformation;
    }
    Status = IoStatus.Status;

    if (Information != NULL)
        *Information = IoStatus.Information;

    /* STATUS_DEVICE_BUSY is the escape-busy/retry backpressure the UMD
     * deliberately spins on when the GPU queue is full — expected flow
     * control, not an error.  Printing it floods serial at 100Hz during a
     * busy/TDR window and buries the real faults; stay silent for it,
     * report everything else. */
    if (!NT_SUCCESS(Status) && Status != STATUS_DEVICE_BUSY)
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
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    if (KeGetCurrentIrql() > APC_LEVEL)
        return;

    WddmBridgeAcquireLock();
    g_WddmBridgeStatus = STATUS_DEVICE_NOT_CONNECTED;
    g_DxgkrnlDeviceObject = NULL;
    if (g_WddmBridgeRundownInitialized)
        ExWaitForRundownProtectionRelease(&g_WddmBridgeRundown);
    WddmBridgeDropReference();
    WddmBridgeReleaseLock();
}
