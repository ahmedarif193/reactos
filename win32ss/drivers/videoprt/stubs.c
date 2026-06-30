/*
 * VideoPort driver
 *
 * Copyright (C) 2002-2004, 2007 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "videoprt.h"

#include <stdarg.h>

#define NDEBUG
#include <debug.h>

/*
 * Video port notification types used by VideoPortNotification.
 * These match the SCSI_NOTIFICATION_TYPE enum from srb.h which
 * videoprt historically shares with ScsiPortNotification.
 */
typedef enum _VIDEO_NOTIFICATION_TYPE {
    RequestComplete,
    NextRequest,
    NextLuRequest,
    ResetDetected,
    /* Video-specific types follow the SCSI base types */
    _VideoNotifyCallDisableInterrupts = 0x10,
    _VideoNotifyCallEnableInterrupts,
    _VideoNotifyBusChangeDetected,
    _VideoNotifyWakeupRequest,
    _VideoNotifyStartResetPresentation,
    _VideoNotifyCompletedResetPresentation,
} VIDEO_NOTIFICATION_TYPE;

VP_STATUS
NTAPI
VideoPortFlushRegistry(
    PVOID HwDeviceExtension)
{
    UNIMPLEMENTED;
    return 0;
}

ULONG
NTAPI
VideoPortGetAssociatedDeviceID(
    IN PVOID DeviceObject)
{
    UNIMPLEMENTED;
    return 0;
}


ULONG
NTAPI
VideoPortGetBytesUsed(
    IN PVOID HwDeviceExtension,
    IN PDMA pDma)
{
    UNIMPLEMENTED;
    return 0;
}

PVOID
NTAPI
VideoPortGetMdl(
    IN PVOID HwDeviceExtension,
    IN PDMA pDma)
{
    UNIMPLEMENTED;
    return 0;
}

VOID
NTAPI
VideoPortSetBytesUsed(
    IN PVOID HwDeviceExtension,
    IN OUT PDMA pDma,
    IN ULONG BytesUsed)
{
    UNIMPLEMENTED;
}

BOOLEAN
NTAPI
VideoPortUnlockPages(
    IN PVOID hwDeviceExtension,
    IN PDMA pDma)
{
    UNIMPLEMENTED;
    return 0;
}

_Function_class_(KDEFERRED_ROUTINE)
VOID
NTAPI
WdDdiWatchdogDpcCallback(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 *
 * VideoPortNotification is the primary callback mechanism from the miniport
 * driver back to the video port driver. It is a variadic function called
 * from HwStartIO and interrupt/DPC contexts to signal events.
 *
 * This function resides in .text (non-paged) because it may be called at
 * DISPATCH_LEVEL from DPC or interrupt synchronization callbacks.
 */
VOID
NTAPI
VideoPortNotification(
    _In_ VIDEO_NOTIFICATION_TYPE NotificationType,
    _In_ PVOID HwDeviceExtension,
    ...)
{
    switch (NotificationType)
    {
        case RequestComplete:
            /*
             * The miniport has completed processing a VRP (video request
             * packet). In the synchronous ReactOS model, HwStartIO returns
             * after filling in the status block, so this is a no-op.
             * On Windows this signals the port driver to complete the
             * associated IRP.
             */
            break;

        case NextRequest:
            /*
             * The miniport is ready to process the next IOCTL/VRP.
             * ReactOS dispatches IOCTLs synchronously so this is a no-op.
             */
            break;

        case ResetDetected:
            DPRINT1("VideoPortNotification: ResetDetected\n");
            break;

        default:
            DPRINT1("VideoPortNotification: Unhandled type %d\n",
                    NotificationType);
            break;
    }
}

/*
 * @implemented
 *
 * VideoPortDbgReportCreate creates a debug report object for miniport
 * Watson/WER integration. Stub returns NULL (no report).
 */
PVIDEO_DEBUG_REPORT
NTAPI
VideoPortDbgReportCreate(
    _In_ PVOID HwDeviceExtension,
    _In_ ULONG ulCode,
    _In_ ULONG_PTR ulpArg1,
    _In_ ULONG_PTR ulpArg2,
    _In_ ULONG_PTR ulpArg3,
    _In_ ULONG_PTR ulpArg4)
{
    DPRINT1("VideoPortDbgReportCreate: code 0x%lx (stub)\n", ulCode);
    return NULL;
}

/*
 * @implemented
 *
 * VideoPortDbgReportSecondaryData adds data to a debug report.
 * Since DbgReportCreate returns NULL, this is a safe no-op.
 */
BOOLEAN
NTAPI
VideoPortDbgReportSecondaryData(
    _Inout_ PVIDEO_DEBUG_REPORT pReport,
    _In_ PVOID pvData,
    _In_ ULONG ulDataSize)
{
    DPRINT1("VideoPortDbgReportSecondaryData (stub)\n");
    return FALSE;
}

/*
 * @implemented
 *
 * VideoPortDbgReportComplete submits and frees a debug report.
 * No-op since we never create real reports.
 */
VOID
NTAPI
VideoPortDbgReportComplete(
    _Inout_ PVIDEO_DEBUG_REPORT pReport)
{
    DPRINT1("VideoPortDbgReportComplete (stub)\n");
}

LONG
FASTCALL
VideoPortInterlockedDecrement(
    IN PLONG Addend)
{
    return _InterlockedDecrement(Addend);
}

LONG
FASTCALL
VideoPortInterlockedIncrement(
    IN PLONG Addend)
{
    return _InterlockedIncrement(Addend);
}

LONG
FASTCALL
VideoPortInterlockedExchange(
    IN OUT PLONG Target,
    IN LONG Value)
{
    return InterlockedExchange(Target, Value);
}

#if defined(_M_AMD64) || defined(_M_ARM64)
UCHAR
NTAPI
VideoPortReadPortUchar(
    PUCHAR Port)
{
    return READ_PORT_UCHAR(Port);
}

USHORT
NTAPI
VideoPortReadPortUshort(
    PUSHORT Port)
{
    return READ_PORT_USHORT(Port);
}

ULONG
NTAPI
VideoPortReadPortUlong(
    PULONG Port)
{
    return READ_PORT_ULONG(Port);
}

VOID
NTAPI
VideoPortReadPortBufferUchar(
    PUCHAR Port,
    PUCHAR Buffer,
    ULONG Count)
{
    READ_PORT_BUFFER_UCHAR(Port, Buffer, Count);
}

VOID
NTAPI
VideoPortReadPortBufferUshort(
    PUSHORT Port,
    PUSHORT Buffer,
    ULONG Count)
{
    READ_PORT_BUFFER_USHORT(Port, Buffer, Count);
}

VOID
NTAPI
VideoPortReadPortBufferUlong(
    PULONG Port,
    PULONG Buffer,
    ULONG Count)
{
    READ_PORT_BUFFER_ULONG(Port, Buffer, Count);
}

UCHAR
NTAPI
VideoPortReadRegisterUchar(
    PUCHAR Register)
{
    return READ_REGISTER_UCHAR(Register);
}

USHORT
NTAPI
VideoPortReadRegisterUshort(
    PUSHORT Register)
{
    return READ_REGISTER_USHORT(Register);
}

ULONG
NTAPI
VideoPortReadRegisterUlong(
    PULONG Register)
{
    return READ_REGISTER_ULONG(Register);
}

VOID
NTAPI
VideoPortReadRegisterBufferUchar(
    PUCHAR Register,
    PUCHAR Buffer,
    ULONG Count)
{
    READ_REGISTER_BUFFER_UCHAR(Register, Buffer, Count);
}

VOID
NTAPI
VideoPortReadRegisterBufferUshort(
    PUSHORT Register,
    PUSHORT Buffer,
    ULONG Count)
{
    READ_REGISTER_BUFFER_USHORT(Register, Buffer, Count);
}

VOID
NTAPI
VideoPortReadRegisterBufferUlong(
    PULONG Register,
    PULONG Buffer,
    ULONG Count)
{
    READ_REGISTER_BUFFER_ULONG(Register, Buffer, Count);
}

VOID
NTAPI
VideoPortWritePortUchar(
    PUCHAR Port,
    UCHAR Value)
{
    WRITE_PORT_UCHAR(Port, Value);
}

VOID
NTAPI
VideoPortWritePortUshort(
    PUSHORT Port,
    USHORT Value)
{
    WRITE_PORT_USHORT(Port, Value);
}

VOID
NTAPI
VideoPortWritePortUlong(
    PULONG Port,
    ULONG Value)
{
    WRITE_PORT_ULONG(Port, Value);
}

VOID
NTAPI
VideoPortWritePortBufferUchar(
    PUCHAR Port,
    PUCHAR Buffer,
    ULONG Count)
{
    WRITE_PORT_BUFFER_UCHAR(Port, Buffer, Count);
}

VOID
NTAPI
VideoPortWritePortBufferUshort(
    PUSHORT Port,
    PUSHORT Buffer,
    ULONG Count)
{
    WRITE_PORT_BUFFER_USHORT(Port, Buffer, Count);
}

VOID
NTAPI
VideoPortWritePortBufferUlong(
    PULONG Port,
    PULONG Buffer,
    ULONG Count)
{
    WRITE_PORT_BUFFER_ULONG(Port, Buffer, Count);
}

VOID
NTAPI
VideoPortWriteRegisterUchar(
    PUCHAR Register,
    UCHAR Value)
{
    WRITE_REGISTER_UCHAR(Register, Value);
}

VOID
NTAPI
VideoPortWriteRegisterUshort(
    PUSHORT Register,
    USHORT Value)
{
    WRITE_REGISTER_USHORT(Register, Value);
}

VOID
NTAPI
VideoPortWriteRegisterUlong(
    PULONG Register,
    ULONG Value)
{
    WRITE_REGISTER_ULONG(Register, Value);
}

VOID
NTAPI
VideoPortWriteRegisterBufferUchar(
    PUCHAR Register,
    PUCHAR Buffer,
    ULONG Count)
{
    WRITE_REGISTER_BUFFER_UCHAR(Register, Buffer, Count);
}

VOID
NTAPI
VideoPortWriteRegisterBufferUshort(
    PUSHORT Register,
    PUSHORT Buffer,
    ULONG Count)
{
    WRITE_REGISTER_BUFFER_USHORT(Register, Buffer, Count);
}

VOID
NTAPI
VideoPortWriteRegisterBufferUlong(
    PULONG Register,
    PULONG Buffer,
    ULONG Count)
{
    WRITE_REGISTER_BUFFER_ULONG(Register, Buffer, Count);
}

VOID
NTAPI
VideoPortQuerySystemTime(
    OUT PLARGE_INTEGER CurrentTime)
{
    KeQuerySystemTime(CurrentTime);
}

#endif /* _M_AMD64 || _M_ARM64 */
