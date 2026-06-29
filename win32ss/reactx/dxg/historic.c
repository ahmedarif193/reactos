
/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Native driver for dxg implementation
 * FILE:             win32ss/reactx/dxg/historic.c
 * PROGRAMER:        Magnus olsen (magnus@greatlord.com)
 * REVISION HISTORY:
 *       15/10-2007   Magnus Olsen
 */

#include <dxg_int.h>

/*
 * Cached file object and device object for \Device\DxgKrnl.
 * Opened on first DxDdIoctl call, kept alive for the session lifetime.
 * Cleanup happens in DxDdCleanupDxGraphics (main.c).
 */
static PFILE_OBJECT   gpDxgKrnlFileObject   = NULL;
static PDEVICE_OBJECT gpDxgKrnlDeviceObject = NULL;

/*++
* @name DxDxgGenericThunk
* @implemented
*
* The function DxDxgGenericThunk redirects DirectX calls to other functions.
*
* @param ULONG_PTR ulIndex
* The functions we want to redirect
*
* @param ULONG_PTR ulHandle
* Unknown
*
* @param SIZE_T *pdwSizeOfPtr1
* Unknown
*
* @param PVOID pvPtr1
* Unknown
*
* @param SIZE_T *pdwSizeOfPtr2
* Unknown
*
* @param PVOID pvPtr2
* Unknown
*
* @return
* Always returns DDHAL_DRIVER_NOTHANDLED
*
* @remarks.
* This function is no longer used in Windows NT 2000/XP/2003
*
*--*/
DWORD
NTAPI
DxDxgGenericThunk(ULONG_PTR ulIndex,
                  ULONG_PTR ulHandle,
                  SIZE_T *pdwSizeOfPtr1,
                  PVOID pvPtr1,
                  SIZE_T *pdwSizeOfPtr2,
                  PVOID pvPtr2)
{
    return DDHAL_DRIVER_NOTHANDLED;
}

/*++
* @name DxDdpOpenDxgKrnl
* @implemented
*
* Internal helper: obtains the device and file objects for \Device\DxgKrnl.
* On the first call it opens the device by name and caches the result.
* Subsequent calls return the cached objects without re-opening.
*
* @return
* STATUS_SUCCESS if the device objects are available, or an error NTSTATUS.
*--*/
static NTSTATUS
DxDdpOpenDxgKrnl(VOID)
{
    UNICODE_STRING DeviceName;
    NTSTATUS Status;

    /* Fast path: already resolved */
    if (gpDxgKrnlDeviceObject != NULL)
        return STATUS_SUCCESS;

    RtlInitUnicodeString(&DeviceName, L"\\Device\\DxgKrnl");

    Status = IoGetDeviceObjectPointer(&DeviceName,
                                      FILE_ALL_ACCESS,
                                      &gpDxgKrnlFileObject,
                                      &gpDxgKrnlDeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("DXG: DxDdpOpenDxgKrnl: IoGetDeviceObjectPointer failed 0x%08lX\n",
                 Status);
        gpDxgKrnlFileObject   = NULL;
        gpDxgKrnlDeviceObject = NULL;
    }

    return Status;
}

/*++
* @name DxDdpCloseDxgKrnl
* @implemented
*
* Releases the cached \Device\DxgKrnl references.
* Called from DxDdCleanupDxGraphics during session teardown.
*--*/
VOID
DxDdpCloseDxgKrnl(VOID)
{
    if (gpDxgKrnlFileObject != NULL)
    {
        ObDereferenceObject(gpDxgKrnlFileObject);
        gpDxgKrnlFileObject   = NULL;
        gpDxgKrnlDeviceObject = NULL;
    }
}

/*++
* @name DxDdIoctl
* @implemented
*
* The D3DKMT IOCTL gateway.  This is dispatch table entry 0x5B and is the
* single most critical function for WDDM support.
*
* win32k.sys calls EngDxIoctl() which routes here.  The function builds a
* synchronous IRP_MJ_DEVICE_CONTROL to \Device\DxgKrnl carrying the caller's
* IOCTL code and buffer, waits for completion, and returns the NTSTATUS.
*
* @param ULONG ulIoctl
* The D3DKMT IOCTL control code (IOCTL_D3DKMT_*).
*
* @param PVOID pBuffer
* Pointer to the caller's in/out data buffer.
*
* @param ULONG ulBufferSize
* Size of pBuffer in bytes.
*
* @return
* NTSTATUS from the downstream dxgkrnl handler.  STATUS_NOT_SUPPORTED if
* the dxgkrnl device is not available.
*--*/
DWORD
NTAPI
DxDdIoctl(ULONG ulIoctl,
          PVOID pBuffer,
          ULONG ulBufferSize)
{
    NTSTATUS       Status;
    KEVENT         Event;
    PIRP           Irp;
    IO_STATUS_BLOCK IoStatusBlock;

    /* Ensure we have a connection to dxgkrnl */
    Status = DxDdpOpenDxgKrnl();
    if (!NT_SUCCESS(Status))
        return (DWORD)Status;

    /* Initialize a completion event for the synchronous IRP */
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /*
     * Build a synchronous IRP_MJ_DEVICE_CONTROL.
     *
     * IoBuildDeviceIoControlRequest creates a METHOD_BUFFERED IRP.
     * We pass the same buffer as both input and output -- the downstream
     * handler (DxgkpDispatchBufferedIoctl) reads from SystemBuffer and
     * writes results back into it, which the I/O manager copies out.
     */
    Irp = IoBuildDeviceIoControlRequest(ulIoctl,
                                        gpDxgKrnlDeviceObject,
                                        pBuffer,
                                        ulBufferSize,
                                        pBuffer,
                                        ulBufferSize,
                                        FALSE,    /* Not internal IOCTL */
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
    {
        DbgPrint("DXG: DxDdIoctl: IoBuildDeviceIoControlRequest failed\n");
        return (DWORD)STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Send the IRP down to dxgkrnl */
    Status = IoCallDriver(gpDxgKrnlDeviceObject, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatusBlock.Status;
    }

    return (DWORD)Status;
}

