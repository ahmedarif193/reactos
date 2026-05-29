/*
 * PROJECT:     ReactOS ntfslx driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Synchronous block-device helpers
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

/*
 * Per-second IRP-rate / latency telemetry. Off in shipping builds.
 * Captures: count of NtfslxReadDisk calls, total wallclock spent in
 * KeWaitForSingleObject for the IRP, total bytes requested, and the
 * worst-case single-IRP latency observed in the past second.
 */
#define NTFSLX_BLOCKDEV_DIAG 0
#if NTFSLX_BLOCKDEV_DIAG
static volatile LONG g_BdDiagCalls;
static volatile LONGLONG g_BdDiagWaitUsTotal;
static volatile LONGLONG g_BdDiagBytesTotal;
static volatile LONGLONG g_BdDiagMaxWaitUs;
volatile LONG g_BdDiagWriteCalls;     /* not static — diskwrite.c bumps it */
static volatile LONG g_BdDiagLastReportTick;

static
VOID
NtfslxBlockdevDiagMaybeReport(VOID)
{
    LONG Tick = (LONG)(KeQueryInterruptTime() / 10000000);
    LONG Last = g_BdDiagLastReportTick;
    if (Tick == Last)
        return;
    if (InterlockedCompareExchange(&g_BdDiagLastReportTick, Tick, Last) != Last)
        return;
    DbgPrint("ntfslx-bd: read calls=%ld waitUsTotal=%I64d bytes=%I64d maxUs=%I64d "
             "writeCalls=%ld\n",
             g_BdDiagCalls, g_BdDiagWaitUsTotal, g_BdDiagBytesTotal,
             g_BdDiagMaxWaitUs, g_BdDiagWriteCalls);
}
#endif


/*
 * Signal our private event when the downstream disk IRP completes. Registered
 * as the IRP completion routine for NtfslxReadDisk so that synchronous paging
 * I/O (which runs at APC_LEVEL with kernel APCs disabled) can still wake up:
 * IoBuildSynchronousFsdRequest's native completion path uses a kernel APC,
 * which never delivers at APC_LEVEL and leads to an indefinite wait.
 */
static
NTSTATUS
NTAPI
NtfslxReadDiskCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NtfslxReadDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ LONGLONG StartingOffset,
    _In_ ULONG Length,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ BOOLEAN Override)
{
    PIO_STACK_LOCATION Stack;
    LARGE_INTEGER Offset;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;
    ULONGLONG RealReadOffset;
    ULONG RealLength;
    PUCHAR ReadBuffer;
    BOOLEAN AllocatedBuffer;

    ReadBuffer = Buffer;
    AllocatedBuffer = FALSE;
    RealReadOffset = (ULONGLONG)StartingOffset;
    RealLength = Length;

    if ((RealReadOffset % SectorSize) != 0 || (RealLength % SectorSize) != 0)
    {
        RealReadOffset = ROUND_DOWN(RealReadOffset, SectorSize);
        RealLength = ROUND_UP(Length + (ULONG)(StartingOffset - RealReadOffset), SectorSize);

        ReadBuffer = ExAllocatePoolWithTag(NonPagedPool, RealLength, NTFSLX_TAG);
        if (ReadBuffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        AllocatedBuffer = TRUE;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Offset.QuadPart = RealReadOffset;
    Irp = IoBuildAsynchronousFsdRequest(IRP_MJ_READ,
                                        DeviceObject,
                                        ReadBuffer,
                                        RealLength,
                                        &Offset,
                                        NULL);
    if (Irp == NULL)
    {
        if (AllocatedBuffer)
        {
            ExFreePoolWithTag(ReadBuffer, NTFSLX_TAG);
        }

        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->UserIosb = &Irp->IoStatus;
    Irp->Flags |= IRP_SYNCHRONOUS_PAGING_IO;

    IoSetCompletionRoutine(Irp,
                           NtfslxReadDiskCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    if (Override)
    {
        Stack = IoGetNextIrpStackLocation(Irp);
        Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    }

#if NTFSLX_BLOCKDEV_DIAG
    {
        LARGE_INTEGER Freq, T0, T1;
        LONGLONG ElapsedUs;
        LONGLONG Prev;
        T0 = KeQueryPerformanceCounter(&Freq);
        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        T1 = KeQueryPerformanceCounter(NULL);
        ElapsedUs = ((T1.QuadPart - T0.QuadPart) * 1000000) / Freq.QuadPart;
        InterlockedIncrement(&g_BdDiagCalls);
        InterlockedExchangeAdd64(&g_BdDiagWaitUsTotal, ElapsedUs);
        InterlockedExchangeAdd64(&g_BdDiagBytesTotal, RealLength);
        for (;;)
        {
            Prev = g_BdDiagMaxWaitUs;
            if (ElapsedUs <= Prev) break;
            if (InterlockedCompareExchange64(&g_BdDiagMaxWaitUs, ElapsedUs, Prev) == Prev)
                break;
        }
        NtfslxBlockdevDiagMaybeReport();
    }
#else
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
#endif
    Status = Irp->IoStatus.Status;

    /*
     * Our completion routine returned STATUS_MORE_PROCESSING_REQUIRED,
     * short-circuiting the I/O manager's normal teardown. We are
     * responsible for unlocking and freeing the MDL we built, and the
     * IRP itself.
     */
    if (Irp->MdlAddress != NULL)
    {
        MmUnlockPages(Irp->MdlAddress);
        IoFreeMdl(Irp->MdlAddress);
        Irp->MdlAddress = NULL;
    }
    IoFreeIrp(Irp);

    if (AllocatedBuffer)
    {
        if (NT_SUCCESS(Status))
        {
            RtlCopyMemory(Buffer,
                          ReadBuffer + (ULONG)(StartingOffset - RealReadOffset),
                          Length);
        }

        ExFreePoolWithTag(ReadBuffer, NTFSLX_TAG);
    }

    return Status;
}

/*
 * Completion routine that signals our private event and short-circuits the
 * I/O manager's APC-based teardown. Returning STATUS_MORE_PROCESSING_REQUIRED
 * keeps the IRP alive past the natural completion point so the caller can
 * sample Irp->IoStatus and free the IRP itself - the IO manager's standard
 * APC-driven completion path is what would deadlock under any FAST_MUTEX or
 * KGUARDED_MUTEX held by the caller (both call KeEnterCriticalRegion which
 * disables special kernel APCs, blocking sync IRP completion indefinitely).
 */
static
NTSTATUS
NTAPI
NtfslxDeviceIoControlCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * NtfslxDeviceIoControl - synchronous IOCTL wrapper that's safe to call from
 * a thread holding a FAST_MUTEX or KGUARDED_MUTEX.
 *
 * Uses IoAllocateIrp + manual IRP construction instead of
 * IoBuildDeviceIoControlRequest because the latter's queue-on-thread-IRP-list
 * bookkeeping is normally torn down by IopCompleteRequest's special-kernel-APC,
 * and that APC is blocked indefinitely while either FAST_MUTEX or
 * KGUARDED_MUTEX is held. By assembling the IRP ourselves we own the entire
 * lifecycle: SystemBuffer alloc, IO stack fill, MDL/buffer copy-out, IRP free.
 *
 * Limitation: METHOD_BUFFERED only. METHOD_DIRECT and METHOD_NEITHER are not
 * needed by any current caller (all the disk/volume IOCTLs we issue at mount
 * and FSCTL forward time are METHOD_BUFFERED). A METHOD check rejects others
 * with STATUS_NOT_IMPLEMENTED so a future caller doesn't silently corrupt.
 */
NTSTATUS
NtfslxDeviceIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(*OutputBufferSize, *OutputBufferSize) PVOID OutputBuffer,
    _Inout_opt_ PULONG OutputBufferSize,
    _In_ BOOLEAN Override)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp = NULL;
    PVOID SystemBuffer = NULL;
    ULONG SystemBufferSize;
    ULONG OutLen;
    NTSTATUS Status;

    OutLen = OutputBufferSize ? *OutputBufferSize : 0;

    if ((ControlCode & 3) != METHOD_BUFFERED)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /*
     * METHOD_BUFFERED uses one shared SystemBuffer for input and output: the
     * IO manager normally allocates max(InputBufferSize, OutputBufferSize)
     * from NonPagedPool, copies input in, and after completion copies the
     * lower bytes out to OutputBuffer. We replicate that here, sized to fit
     * either direction so the lower-level driver can write into SystemBuffer
     * up to OutLen bytes.
     */
    SystemBufferSize = (InputBufferSize > OutLen) ? InputBufferSize : OutLen;
    if (SystemBufferSize > 0)
    {
        SystemBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                              SystemBufferSize,
                                              NTFSLX_TAG);
        if (SystemBuffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        if (InputBufferSize > 0 && InputBuffer != NULL)
        {
            RtlCopyMemory(SystemBuffer, InputBuffer, InputBufferSize);
        }
    }

    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (Irp == NULL)
    {
        if (SystemBuffer != NULL)
        {
            ExFreePoolWithTag(SystemBuffer, NTFSLX_TAG);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->AssociatedIrp.SystemBuffer = SystemBuffer;
    Irp->UserBuffer = OutputBuffer;
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    Irp->Flags = IRP_BUFFERED_IO;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    Stack->MinorFunction = 0;
    Stack->Parameters.DeviceIoControl.IoControlCode = ControlCode;
    Stack->Parameters.DeviceIoControl.InputBufferLength = InputBufferSize;
    Stack->Parameters.DeviceIoControl.OutputBufferLength = OutLen;
    Stack->Parameters.DeviceIoControl.Type3InputBuffer = NULL;

    if (Override)
    {
        Stack->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    }

    IoSetCompletionRoutine(Irp,
                           NtfslxDeviceIoControlCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
    Status = Irp->IoStatus.Status;

    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_OVERFLOW)
    {
        ULONG Information = (ULONG)Irp->IoStatus.Information;
        if (Information > OutLen)
        {
            Information = OutLen;
        }
        if (Information > 0 && SystemBuffer != NULL && OutputBuffer != NULL)
        {
            RtlCopyMemory(OutputBuffer, SystemBuffer, Information);
        }
        if (OutputBufferSize != NULL)
        {
            *OutputBufferSize = (ULONG)Irp->IoStatus.Information;
        }
    }
    else if (OutputBufferSize != NULL)
    {
        *OutputBufferSize = 0;
    }

    if (SystemBuffer != NULL)
    {
        ExFreePoolWithTag(SystemBuffer, NTFSLX_TAG);
    }
    IoFreeIrp(Irp);

    return Status;
}
