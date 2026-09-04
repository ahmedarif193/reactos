/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Version-2 serial KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "classlibrary.h"
#include <ntddser.h>
#include <SerCx.h>

typedef struct _SERCX2_PIO_TRANSMIT_CONTEXT
{
    WDFDEVICE Device;
    SERCX2_PIO_TRANSMIT_CONFIG Config;
} SERCX2_PIO_TRANSMIT_CONTEXT, *PSERCX2_PIO_TRANSMIT_CONTEXT;

typedef struct _SERCX2_PIO_RECEIVE_CONTEXT
{
    WDFDEVICE Device;
    SERCX2_PIO_RECEIVE_CONFIG Config;
} SERCX2_PIO_RECEIVE_CONTEXT, *PSERCX2_PIO_RECEIVE_CONTEXT;

typedef struct _SERCX2_SYSTEM_DMA_CONTEXT
{
    WDFDEVICE Device;
    BOOLEAN Receive;
    SERCX2_SYSTEM_DMA_TRANSMIT_CONFIG TxConfig;
    SERCX2_SYSTEM_DMA_RECEIVE_CONFIG RxConfig;
    WDFDMAENABLER Enabler;
    WDFDMATRANSACTION Transaction;
} SERCX2_SYSTEM_DMA_CONTEXT, *PSERCX2_SYSTEM_DMA_CONTEXT;

typedef struct _SERCX2_CUSTOM_CONTEXT
{
    WDFDEVICE Device;
    BOOLEAN Receive;
    SERCX2_CUSTOM_TRANSMIT_CONFIG TxConfig;
    SERCX2_CUSTOM_RECEIVE_CONFIG RxConfig;
    WDFOBJECT Transaction;
} SERCX2_CUSTOM_CONTEXT, *PSERCX2_CUSTOM_CONTEXT;

typedef struct _SERCX2_CUSTOM_TRANSACTION_CONTEXT
{
    WDFDEVICE Device;
    WDFOBJECT Owner;
    BOOLEAN Receive;
    SERCX2_CUSTOM_TRANSMIT_TRANSACTION_CONFIG TxConfig;
    SERCX2_CUSTOM_RECEIVE_TRANSACTION_CONFIG RxConfig;
} SERCX2_CUSTOM_TRANSACTION_CONTEXT, *PSERCX2_CUSTOM_TRANSACTION_CONTEXT;

typedef struct _SERCX2_TRANSACTION
{
    WDFREQUEST Request;
    SERCX2_TRANSACTION_TYPE Type;
    size_t Transferred;
    BOOLEAN Cleaning;
    PUCHAR Buffer;
    size_t Length;
    size_t Offset;
    BOOLEAN Running;
    BOOLEAN ReadyPending;
    BOOLEAN WaitingReady;
    BOOLEAN ReadyArrived;
    BOOLEAN Draining;
    NTSTATUS AbortStatus;
    BOOLEAN AbortPending;
} SERCX2_TRANSACTION, *PSERCX2_TRANSACTION;

typedef struct _SERCX2_DEVICE_CONTEXT
{
    PWDF_DRIVER_GLOBALS ClientGlobals;
    SERCX2_CONFIG Config;
    WDF_OBJECT_ATTRIBUTES RequestAttributes;
    BOOLEAN HasRequestAttributes;
    BOOLEAN Initialized;
    BOOLEAN Opened;
    WDFQUEUE Queue;
    WDFQUEUE WriteQueue;
    WDFQUEUE ReadQueue;
    WDFQUEUE WaitQueue;
    SERCX2PIOTRANSMIT PioTransmit;
    SERCX2PIORECEIVE PioReceive;
    SERCX2SYSTEMDMATRANSMIT SystemDmaTransmit;
    SERCX2SYSTEMDMARECEIVE SystemDmaReceive;
    SERCX2CUSTOMTRANSMIT CustomTransmit;
    SERCX2CUSTOMRECEIVE CustomReceive;
    WDFTIMER WriteTimer;
    WDFTIMER ReadTotalTimer;
    WDFTIMER ReadIntervalTimer;
    KSPIN_LOCK Lock;
    SERCX2_TRANSACTION Tx;
    SERCX2_TRANSACTION Rx;
    SERIAL_TIMEOUTS Timeouts;
    ULONG WaitMask;
    ULONG HistoryMask;
} SERCX2_DEVICE_CONTEXT, *PSERCX2_DEVICE_CONTEXT;

typedef struct _SERCX2_REQUEST_CONTEXT
{
    BOOLEAN Prepared;
    BOOLEAN CustomReceive;
    WDFDEVICE CustomDevice;
} SERCX2_REQUEST_CONTEXT, *PSERCX2_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERCX2_DEVICE_CONTEXT, SerCx2GetDeviceContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERCX2_PIO_TRANSMIT_CONTEXT, SerCx2GetPioTransmitContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERCX2_PIO_RECEIVE_CONTEXT, SerCx2GetPioReceiveContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERCX2_REQUEST_CONTEXT, SerCx2GetRequestContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERCX2_SYSTEM_DMA_CONTEXT, SerCx2GetSystemDmaContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERCX2_CUSTOM_CONTEXT, SerCx2GetCustomContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(SERCX2_CUSTOM_TRANSACTION_CONTEXT, SerCx2GetCustomTransactionContext)

static VOID SerCx2NonPioAbort(_In_ WDFDEVICE DeviceHandle, _In_ PSERCX2_TRANSACTION Transaction,
                              _In_opt_ WDFREQUEST Expected, _In_ NTSTATUS Status);
static VOID SerCx2NonPioBegin(_In_ PSERCX2_DEVICE_CONTEXT Device, _In_ PSERCX2_TRANSACTION Transaction);
static VOID NTAPI SerCx2EvtRequestCleanup(_In_ WDFOBJECT Object);
static SERCX2_TRANSACTION_TYPE SerCx2SelectType(_In_ PSERCX2_DEVICE_CONTEXT Device, _In_ WDFREQUEST Request,
                                                _In_ ULONG Length, _In_ BOOLEAN Receive);

static VOID SerCx2TxRun(_In_ WDFDEVICE DeviceHandle);
static VOID SerCx2RxRun(_In_ WDFDEVICE DeviceHandle);

static
NTSTATUS
SerCx2PrepareRequest(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ WDFREQUEST Request)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    PSERCX2_REQUEST_CONTEXT Context;
    NTSTATUS Status;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_REQUEST_CONTEXT);
    Attributes.EvtCleanupCallback = SerCx2EvtRequestCleanup;
    Status = WdfObjectAllocateContext(Request, &Attributes, (PVOID *)&Context);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
    {
        Context = SerCx2GetRequestContext(Request);
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    if (Context->Prepared)
        return STATUS_SUCCESS;
    Context->Prepared = TRUE;

    if (Device->HasRequestAttributes)
    {
        Attributes = Device->RequestAttributes;
        Attributes.ParentObject = NULL;
        Status = WdfObjectAllocateContext(Request, &Attributes, NULL);
        if (Status == STATUS_OBJECT_NAME_EXISTS)
            Status = STATUS_SUCCESS;
    }

    return Status;
}

static
VOID
SerCx2CompleteTransaction(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction,
    _In_ WDFREQUEST Request,
    _In_ NTSTATUS Status,
    _In_ size_t Information)
{
    if (Transaction == &Device->Tx)
    {
        PSERCX2_PIO_TRANSMIT_CONTEXT Pio = SerCx2GetPioTransmitContext(Device->PioTransmit);

        WdfTimerStop(Device->WriteTimer, FALSE);
        if (Pio->Config.EvtSerCx2PioTransmitCleanupTransaction != NULL)
            Pio->Config.EvtSerCx2PioTransmitCleanupTransaction(Device->PioTransmit);
    }
    else
    {
        PSERCX2_PIO_RECEIVE_CONTEXT Pio = SerCx2GetPioReceiveContext(Device->PioReceive);

        WdfTimerStop(Device->ReadTotalTimer, FALSE);
        WdfTimerStop(Device->ReadIntervalTimer, FALSE);
        if (Pio->Config.EvtSerCx2PioReceiveCleanupTransaction != NULL)
            Pio->Config.EvtSerCx2PioReceiveCleanupTransaction(Device->PioReceive);
    }

    WdfRequestCompleteWithInformation(Request, Status, Information);
}

static
BOOLEAN
SerCx2CancelReady(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction)
{
    if (Transaction == &Device->Tx)
    {
        PSERCX2_PIO_TRANSMIT_CONTEXT Pio = SerCx2GetPioTransmitContext(Device->PioTransmit);

        if (Transaction->Draining)
        {
            if (Pio->Config.EvtSerCx2PioTransmitCancelDrainFifo != NULL)
                return Pio->Config.EvtSerCx2PioTransmitCancelDrainFifo(Device->PioTransmit);
            return FALSE;
        }
        return Pio->Config.EvtSerCx2PioTransmitCancelReadyNotification(Device->PioTransmit);
    }
    else
    {
        PSERCX2_PIO_RECEIVE_CONTEXT Pio = SerCx2GetPioReceiveContext(Device->PioReceive);

        return Pio->Config.EvtSerCx2PioReceiveCancelReadyNotification(Device->PioReceive);
    }
}

static
VOID
SerCx2AbortTransaction(
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_TRANSACTION Transaction,
    _In_ WDFREQUEST Expected,
    _In_ NTSTATUS Status)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    WDFREQUEST Request;
    size_t Offset;
    BOOLEAN Cancelled = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Transaction->Request != NULL && Transaction->Type != SerCx2TransactionTypePio)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        SerCx2NonPioAbort(DeviceHandle, Transaction, Expected, Status);
        return;
    }
    if (Transaction->Request == NULL || (Expected != NULL && Transaction->Request != Expected) ||
        !Transaction->WaitingReady)
    {
        if (Transaction->Request != NULL && (Expected == NULL || Transaction->Request == Expected))
        {
            Transaction->AbortPending = TRUE;
            Transaction->AbortStatus = Status;
        }
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    Cancelled = SerCx2CancelReady(Device, Transaction);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Transaction->Request == NULL || (Expected != NULL && Transaction->Request != Expected))
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }

    if (Cancelled || Transaction->ReadyArrived)
    {
        Request = Transaction->Request;
        Offset = Transaction->Offset;
        if (Status != STATUS_CANCELLED)
        {
            if (WdfRequestUnmarkCancelable(Request) == STATUS_CANCELLED)
            {
                Transaction->AbortPending = TRUE;
                Transaction->AbortStatus = Status;
                Transaction->ReadyArrived = TRUE;
                KeReleaseSpinLock(&Device->Lock, OldIrql);
                return;
            }
        }
        Transaction->Request = NULL;
        Transaction->WaitingReady = FALSE;
        Transaction->ReadyArrived = FALSE;
        Transaction->Draining = FALSE;
        Transaction->AbortPending = FALSE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);

        SerCx2CompleteTransaction(Device, Transaction, Request, Status, Offset);
        if (Transaction == &Device->Tx)
            SerCx2TxRun(DeviceHandle);
        else
            SerCx2RxRun(DeviceHandle);
        return;
    }

    Transaction->AbortPending = TRUE;
    Transaction->AbortStatus = Status;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
}

static
VOID
NTAPI
SerCx2EvtWriteCancel(
    _In_ WDFREQUEST Request)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);

    SerCx2AbortTransaction(DeviceHandle, &Device->Tx, Request, STATUS_CANCELLED);
}

static
VOID
NTAPI
SerCx2EvtReadCancel(
    _In_ WDFREQUEST Request)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);

    SerCx2AbortTransaction(DeviceHandle, &Device->Rx, Request, STATUS_CANCELLED);
}

static
VOID
NTAPI
SerCx2EvtWriteTimer(
    _In_ WDFTIMER Timer)
{
    WDFDEVICE DeviceHandle = WdfTimerGetParentObject(Timer);
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);

    SerCx2AbortTransaction(DeviceHandle, &Device->Tx, NULL, STATUS_TIMEOUT);
}

static
VOID
NTAPI
SerCx2EvtReadTimer(
    _In_ WDFTIMER Timer)
{
    WDFDEVICE DeviceHandle = WdfTimerGetParentObject(Timer);
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);

    SerCx2AbortTransaction(DeviceHandle, &Device->Rx, NULL, STATUS_TIMEOUT);
}

static
BOOLEAN
SerCx2ResumeAfterReady(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction,
    _Out_ WDFREQUEST *AbortRequest,
    _Out_ PNTSTATUS AbortStatus,
    _Out_ size_t *AbortOffset)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    *AbortRequest = NULL;
    *AbortStatus = STATUS_SUCCESS;
    *AbortOffset = 0;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Transaction->Request == NULL)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return FALSE;
    }

    Status = WdfRequestUnmarkCancelable(Transaction->Request);
    if (Status == STATUS_CANCELLED && !Transaction->AbortPending)
    {
        Transaction->ReadyArrived = TRUE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return FALSE;
    }

    Transaction->WaitingReady = FALSE;
    Transaction->ReadyArrived = FALSE;
    if (Transaction->AbortPending || Status == STATUS_CANCELLED)
    {
        *AbortRequest = Transaction->Request;
        *AbortStatus = Status == STATUS_CANCELLED ? STATUS_CANCELLED : Transaction->AbortStatus;
        *AbortOffset = Transaction->Offset;
        Transaction->Request = NULL;
        Transaction->AbortPending = FALSE;
        Transaction->Draining = FALSE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return FALSE;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    return TRUE;
}

static
BOOLEAN
SerCx2ArmWait(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction,
    _In_ PFN_WDF_REQUEST_CANCEL Cancel,
    _Out_ WDFREQUEST *AbortRequest,
    _Out_ size_t *AbortOffset)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    *AbortRequest = NULL;
    *AbortOffset = 0;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Transaction->WaitingReady = TRUE;
    Transaction->ReadyArrived = FALSE;
    Status = WdfRequestMarkCancelableEx(Transaction->Request, Cancel);
    if (!NT_SUCCESS(Status))
    {
        *AbortRequest = Transaction->Request;
        *AbortOffset = Transaction->Offset;
        Transaction->Request = NULL;
        Transaction->WaitingReady = FALSE;
        Transaction->Draining = FALSE;
        Transaction->AbortPending = FALSE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return FALSE;
    }
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    return TRUE;
}


static
WDFDEVICE
SerCx2DeviceHandle(
    _In_ PSERCX2_DEVICE_CONTEXT Device)
{
    return SerCx2GetPioTransmitContext(Device->PioTransmit)->Device;
}

static
BOOLEAN
SerCx2LengthFits(
    _In_ ULONG Length,
    _In_ ULONG Minimum,
    _In_ ULONG Maximum)
{
    return Length >= Minimum && (Maximum == 0 || Length <= Maximum);
}

static
PSERCX2_SYSTEM_DMA_CONTEXT
SerCx2DmaFor(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ BOOLEAN Receive)
{
    PVOID Object = Receive ? (PVOID)Device->SystemDmaReceive : (PVOID)Device->SystemDmaTransmit;

    return Object != NULL ? SerCx2GetSystemDmaContext((WDFOBJECT)Object) : NULL;
}

static
PSERCX2_CUSTOM_TRANSACTION_CONTEXT
SerCx2CustomFor(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ BOOLEAN Receive,
    _Out_opt_ WDFOBJECT *Handle)
{
    PVOID Object = Receive ? (PVOID)Device->CustomReceive : (PVOID)Device->CustomTransmit;
    PSERCX2_CUSTOM_CONTEXT Custom;

    if (Handle != NULL)
        *Handle = NULL;
    if (Object == NULL)
        return NULL;
    Custom = SerCx2GetCustomContext((WDFOBJECT)Object);
    if (Custom->Transaction == NULL)
        return NULL;
    if (Handle != NULL)
        *Handle = Custom->Transaction;
    return SerCx2GetCustomTransactionContext(Custom->Transaction);
}

static
BOOLEAN
SerCx2DmaFits(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ BOOLEAN Receive,
    _In_ ULONG Length)
{
    PSERCX2_SYSTEM_DMA_CONTEXT Dma = SerCx2DmaFor(Device, Receive);

    if (Dma == NULL || Dma->Transaction == NULL)
        return FALSE;
    return Receive ?
        SerCx2LengthFits(Length, Dma->RxConfig.MinimumTransactionLength, Dma->RxConfig.MaximumTransactionLength) :
        SerCx2LengthFits(Length, Dma->TxConfig.MinimumTransactionLength, Dma->TxConfig.MaximumTransactionLength);
}

static
BOOLEAN
SerCx2CustomFits(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ BOOLEAN Receive,
    _In_ ULONG Length)
{
    PVOID Object = Receive ? (PVOID)Device->CustomReceive : (PVOID)Device->CustomTransmit;
    PSERCX2_CUSTOM_CONTEXT Custom;

    if (Object == NULL)
        return FALSE;
    Custom = SerCx2GetCustomContext((WDFOBJECT)Object);
    if (Custom->Transaction == NULL)
        return FALSE;
    return Receive ?
        SerCx2LengthFits(Length, Custom->RxConfig.MinimumTransactionLength, Custom->RxConfig.MaximumTransactionLength) :
        SerCx2LengthFits(Length, Custom->TxConfig.MinimumTransactionLength, Custom->TxConfig.MaximumTransactionLength);
}

static
SERCX2_TRANSACTION_TYPE
SerCx2SelectType(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ WDFREQUEST Request,
    _In_ ULONG Length,
    _In_ BOOLEAN Receive)
{
    SERCX2_TRANSACTION_TYPE Type = SerCx2TransactionTypeInvalid;

    if (Receive && Device->Config.EvtSerCx2SelectNextReceiveTransactionType != NULL)
        Device->Config.EvtSerCx2SelectNextReceiveTransactionType(SerCx2DeviceHandle(Device), Request, Length, &Type);
    else if (!Receive && Device->Config.EvtSerCx2SelectNextTransmitTransactionType != NULL)
        Device->Config.EvtSerCx2SelectNextTransmitTransactionType(SerCx2DeviceHandle(Device), Request, Length, &Type);

    if (Type == SerCx2TransactionTypePio)
        return Type;
    if (Type == SerCx2TransactionTypeSystemDma && SerCx2DmaFits(Device, Receive, Length))
        return Type;
    if (Type == SerCx2TransactionTypeCustom && SerCx2CustomFits(Device, Receive, Length))
        return Type;
    if (Type == SerCx2TransactionTypeInvalid)
    {
        if (SerCx2DmaFits(Device, Receive, Length))
            return SerCx2TransactionTypeSystemDma;
        if (SerCx2CustomFits(Device, Receive, Length))
            return SerCx2TransactionTypeCustom;
    }
    return SerCx2TransactionTypePio;
}

static
VOID
SerCx2NonPioStopTimers(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction)
{
    if (Transaction == &Device->Tx)
    {
        WdfTimerStop(Device->WriteTimer, FALSE);
    }
    else
    {
        WdfTimerStop(Device->ReadTotalTimer, FALSE);
        WdfTimerStop(Device->ReadIntervalTimer, FALSE);
    }
}

static
VOID
SerCx2NonPioRestartInterval(
    _In_ PSERCX2_DEVICE_CONTEXT Device)
{
    ULONG Interval = Device->Timeouts.ReadIntervalTimeout;
    KIRQL OldIrql;
    BOOLEAN Active;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Active = Device->Rx.Request != NULL && Device->Rx.Type != SerCx2TransactionTypePio && !Device->Rx.Cleaning;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    if (Active && Interval != 0 && Interval != MAXULONG)
        WdfTimerStart(Device->ReadIntervalTimer, WDF_REL_TIMEOUT_IN_MS(Interval));
}

static
VOID
SerCx2NonPioCleanupComplete(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Transaction->Request = NULL;
    Transaction->Type = SerCx2TransactionTypePio;
    Transaction->Running = FALSE;
    Transaction->Cleaning = FALSE;
    Transaction->Draining = FALSE;
    Transaction->AbortPending = FALSE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (Transaction == &Device->Tx)
        SerCx2TxRun(SerCx2DeviceHandle(Device));
    else
        SerCx2RxRun(SerCx2DeviceHandle(Device));
}

static
VOID
SerCx2NonPioRequestDone(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction)
{
    BOOLEAN Receive = Transaction == &Device->Rx;
    SERCX2_TRANSACTION_TYPE Type = Transaction->Type;

    SerCx2NonPioStopTimers(Device, Transaction);
    if (Type == SerCx2TransactionTypeSystemDma)
    {
        PSERCX2_SYSTEM_DMA_CONTEXT Dma = SerCx2DmaFor(Device, Receive);

        if (Dma != NULL)
        {
            if (Receive && Dma->RxConfig.EvtSerCx2SystemDmaReceiveCleanupTransaction != NULL)
            {
                Dma->RxConfig.EvtSerCx2SystemDmaReceiveCleanupTransaction(Device->SystemDmaReceive);
                return;
            }
            if (!Receive && Dma->TxConfig.EvtSerCx2SystemDmaTransmitCleanupTransaction != NULL)
            {
                Dma->TxConfig.EvtSerCx2SystemDmaTransmitCleanupTransaction(Device->SystemDmaTransmit);
                return;
            }
        }
    }
    else if (Type == SerCx2TransactionTypeCustom)
    {
        WDFOBJECT Handle;
        PSERCX2_CUSTOM_TRANSACTION_CONTEXT Custom = SerCx2CustomFor(Device, Receive, &Handle);

        if (Custom != NULL)
        {
            if (Receive && Custom->RxConfig.EvtSerCx2CustomReceiveTransactionCleanup != NULL)
            {
                Custom->RxConfig.EvtSerCx2CustomReceiveTransactionCleanup((SERCX2CUSTOMRECEIVETRANSACTION)Handle);
                return;
            }
            if (!Receive && Custom->TxConfig.EvtSerCx2CustomTransmitTransactionCleanup != NULL)
            {
                Custom->TxConfig.EvtSerCx2CustomTransmitTransactionCleanup((SERCX2CUSTOMTRANSMITTRANSACTION)Handle);
                return;
            }
        }
    }
    SerCx2NonPioCleanupComplete(Device, Transaction);
}

static
VOID
SerCx2NonPioFinish(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction,
    _In_ NTSTATUS Status,
    _In_ size_t Information)
{
    WDFREQUEST Request;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Request = Transaction->Request;
    if (Request == NULL || Transaction->Cleaning)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }
    Transaction->Cleaning = TRUE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (Transaction->Type == SerCx2TransactionTypeSystemDma)
        WdfRequestUnmarkCancelable(Request);
    WdfRequestCompleteWithInformation(Request, Status, Information);
    SerCx2NonPioRequestDone(Device, Transaction);
}

static
VOID
NTAPI
SerCx2EvtRequestCleanup(
    _In_ WDFOBJECT Object)
{
    PSERCX2_REQUEST_CONTEXT Context = SerCx2GetRequestContext(Object);
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction;
    KIRQL OldIrql;

    if (Context == NULL || Context->CustomDevice == NULL)
        return;
    Device = SerCx2GetDeviceContext(Context->CustomDevice);
    Transaction = Context->CustomReceive ? &Device->Rx : &Device->Tx;
    Context->CustomDevice = NULL;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Transaction->Request != (WDFREQUEST)Object || Transaction->Type != SerCx2TransactionTypeCustom ||
        Transaction->Cleaning)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }
    Transaction->Cleaning = TRUE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    SerCx2NonPioRequestDone(Device, Transaction);
}

static
BOOLEAN
NTAPI
SerCx2EvtProgramDma(
    _In_ WDFDMATRANSACTION Transaction,
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFCONTEXT Context,
    _In_ WDF_DMA_DIRECTION Direction,
    _In_ PSCATTER_GATHER_LIST SgList)
{
    UNREFERENCED_PARAMETER(Transaction);
    UNREFERENCED_PARAMETER(DeviceHandle);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Direction);
    UNREFERENCED_PARAMETER(SgList);
    return TRUE;
}

static
VOID
NTAPI
SerCx2EvtDmaTransferComplete(
    _In_ WDFDMATRANSACTION DmaTransaction,
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFCONTEXT Context,
    _In_ WDF_DMA_DIRECTION Direction,
    _In_ DMA_COMPLETION_STATUS DmaStatus)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PSERCX2_TRANSACTION Transaction = Context;
    BOOLEAN Receive = Transaction == &Device->Rx;
    PSERCX2_SYSTEM_DMA_CONTEXT Dma = SerCx2DmaFor(Device, Receive);
    size_t Bytes = WdfDmaTransactionGetBytesTransferred(DmaTransaction);
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS FinalStatus;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Direction);

    WdfDmaTransactionDmaCompletedFinal(DmaTransaction, Bytes, &FinalStatus);
    WdfDmaTransactionRelease(DmaTransaction);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Transaction->Transferred = Bytes;
    if (Transaction->AbortPending)
        Status = Transaction->AbortStatus;
    else if (DmaStatus == DmaCancelled)
        Status = STATUS_CANCELLED;
    else if (DmaStatus != DmaComplete)
        Status = STATUS_IO_DEVICE_ERROR;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (!Receive && NT_SUCCESS(Status) && Dma != NULL &&
        Dma->TxConfig.EvtSerCx2SystemDmaTransmitDrainFifo != NULL)
    {
        Transaction->Draining = TRUE;
        Dma->TxConfig.EvtSerCx2SystemDmaTransmitDrainFifo(Device->SystemDmaTransmit);
        return;
    }
    SerCx2NonPioFinish(Device, Transaction, Status, Bytes);
}

static
VOID
SerCx2DmaStart(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction)
{
    BOOLEAN Receive = Transaction == &Device->Rx;
    PSERCX2_SYSTEM_DMA_CONTEXT Dma = SerCx2DmaFor(Device, Receive);
    WDFREQUEST Request = Transaction->Request;
    ULONG Length = (ULONG)min(Transaction->Length, MAXULONG);
    NTSTATUS Status;

    if (Dma == NULL || Dma->Transaction == NULL)
    {
        SerCx2NonPioFinish(Device, Transaction, STATUS_INVALID_DEVICE_STATE, 0);
        return;
    }

    Status = WdfDmaTransactionInitializeUsingRequest(Dma->Transaction, Request, SerCx2EvtProgramDma,
                                                     Receive ? WdfDmaDirectionReadFromDevice :
                                                               WdfDmaDirectionWriteToDevice);
    if (!NT_SUCCESS(Status))
    {
        SerCx2NonPioFinish(Device, Transaction, Status, 0);
        return;
    }
    WdfDmaTransactionSetTransferCompleteCallback(Dma->Transaction, SerCx2EvtDmaTransferComplete, Transaction);

    if (Receive && Dma->RxConfig.EvtSerCx2SystemDmaReceiveConfigureDma != NULL)
        Dma->RxConfig.EvtSerCx2SystemDmaReceiveConfigureDma(Device->SystemDmaReceive, Request, Length, Dma->Transaction);
    else if (!Receive && Dma->TxConfig.EvtSerCx2SystemDmaTransmitConfigureDma != NULL)
        Dma->TxConfig.EvtSerCx2SystemDmaTransmitConfigureDma(Device->SystemDmaTransmit, Request, Length, Dma->Transaction);

    Status = WdfRequestMarkCancelableEx(Request, Receive ? SerCx2EvtReadCancel : SerCx2EvtWriteCancel);
    if (!NT_SUCCESS(Status))
    {
        WdfDmaTransactionRelease(Dma->Transaction);
        SerCx2NonPioFinish(Device, Transaction, Status, 0);
        return;
    }

    Status = WdfDmaTransactionExecute(Dma->Transaction, Transaction);
    if (!NT_SUCCESS(Status))
    {
        WdfDmaTransactionRelease(Dma->Transaction);
        SerCx2NonPioFinish(Device, Transaction, Status, 0);
        return;
    }

    if (Receive && Dma->RxConfig.EvtSerCx2SystemDmaReceiveEnableNewDataNotification != NULL)
    {
        SerCx2NonPioRestartInterval(Device);
        Dma->RxConfig.EvtSerCx2SystemDmaReceiveEnableNewDataNotification(Device->SystemDmaReceive);
    }
}

static
VOID
SerCx2CustomStart(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction)
{
    BOOLEAN Receive = Transaction == &Device->Rx;
    WDFOBJECT Handle;
    PSERCX2_CUSTOM_TRANSACTION_CONTEXT Custom = SerCx2CustomFor(Device, Receive, &Handle);
    WDFREQUEST Request = Transaction->Request;
    PSERCX2_REQUEST_CONTEXT RequestContext = SerCx2GetRequestContext(Request);
    ULONG Length = (ULONG)min(Transaction->Length, MAXULONG);

    if (Custom == NULL || RequestContext == NULL ||
        (Receive ? Custom->RxConfig.EvtSerCx2CustomReceiveTransactionStart == NULL :
                   Custom->TxConfig.EvtSerCx2CustomTransmitTransactionStart == NULL))
    {
        SerCx2NonPioFinish(Device, Transaction, STATUS_INVALID_DEVICE_STATE, 0);
        return;
    }

    RequestContext->CustomDevice = SerCx2DeviceHandle(Device);
    RequestContext->CustomReceive = Receive;
    if (Receive)
    {
        Custom->RxConfig.EvtSerCx2CustomReceiveTransactionStart((SERCX2CUSTOMRECEIVETRANSACTION)Handle, Request, Length);
        if (Custom->RxConfig.EvtSerCx2CustomReceiveTransactionEnableNewDataNotification != NULL)
        {
            SerCx2NonPioRestartInterval(Device);
            Custom->RxConfig.EvtSerCx2CustomReceiveTransactionEnableNewDataNotification((SERCX2CUSTOMRECEIVETRANSACTION)Handle);
        }
    }
    else
    {
        Custom->TxConfig.EvtSerCx2CustomTransmitTransactionStart((SERCX2CUSTOMTRANSMITTRANSACTION)Handle, Request, Length);
    }
}

static
VOID
SerCx2NonPioInitialized(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction,
    _In_ NTSTATUS Status)
{
    KIRQL OldIrql;
    BOOLEAN Active;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Active = Transaction->Request != NULL && Transaction->Type != SerCx2TransactionTypePio && !Transaction->Cleaning;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    if (!Active)
        return;

    if (!NT_SUCCESS(Status))
    {
        SerCx2NonPioFinish(Device, Transaction, Status, 0);
        return;
    }
    if (Transaction->Type == SerCx2TransactionTypeSystemDma)
        SerCx2DmaStart(Device, Transaction);
    else
        SerCx2CustomStart(Device, Transaction);
}

static
VOID
SerCx2NonPioBegin(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ PSERCX2_TRANSACTION Transaction)
{
    BOOLEAN Receive = Transaction == &Device->Rx;
    WDFREQUEST Request = Transaction->Request;
    ULONG Length = (ULONG)min(Transaction->Length, MAXULONG);

    if (Transaction->Type == SerCx2TransactionTypeSystemDma)
    {
        PSERCX2_SYSTEM_DMA_CONTEXT Dma = SerCx2DmaFor(Device, Receive);

        if (Dma != NULL && Receive && Dma->RxConfig.EvtSerCx2SystemDmaReceiveInitializeTransaction != NULL)
        {
            Dma->RxConfig.EvtSerCx2SystemDmaReceiveInitializeTransaction(Device->SystemDmaReceive, Request, Length);
            return;
        }
        if (Dma != NULL && !Receive && Dma->TxConfig.EvtSerCx2SystemDmaTransmitInitializeTransaction != NULL)
        {
            Dma->TxConfig.EvtSerCx2SystemDmaTransmitInitializeTransaction(Device->SystemDmaTransmit, Request, Length);
            return;
        }
    }
    else
    {
        WDFOBJECT Handle;
        PSERCX2_CUSTOM_TRANSACTION_CONTEXT Custom = SerCx2CustomFor(Device, Receive, &Handle);

        if (Custom != NULL && Receive && Custom->RxConfig.EvtSerCx2CustomReceiveTransactionInitialize != NULL)
        {
            Custom->RxConfig.EvtSerCx2CustomReceiveTransactionInitialize((SERCX2CUSTOMRECEIVETRANSACTION)Handle, Request, Length);
            return;
        }
        if (Custom != NULL && !Receive && Custom->TxConfig.EvtSerCx2CustomTransmitTransactionInitialize != NULL)
        {
            Custom->TxConfig.EvtSerCx2CustomTransmitTransactionInitialize((SERCX2CUSTOMTRANSMITTRANSACTION)Handle, Request, Length);
            return;
        }
    }
    SerCx2NonPioInitialized(Device, Transaction, STATUS_SUCCESS);
}

static
VOID
SerCx2NonPioAbort(
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_TRANSACTION Transaction,
    _In_opt_ WDFREQUEST Expected,
    _In_ NTSTATUS Status)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    BOOLEAN Receive = Transaction == &Device->Rx;
    SERCX2_TRANSACTION_TYPE Type;
    BOOLEAN Draining;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Transaction->Request == NULL || (Expected != NULL && Transaction->Request != Expected) ||
        Transaction->Cleaning || Transaction->AbortPending)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }
    Transaction->AbortPending = TRUE;
    Transaction->AbortStatus = Status;
    Type = Transaction->Type;
    Draining = Transaction->Draining;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (Type == SerCx2TransactionTypeSystemDma)
    {
        PSERCX2_SYSTEM_DMA_CONTEXT Dma = SerCx2DmaFor(Device, Receive);

        if (Dma == NULL)
            return;
        if (Draining)
        {
            if (Dma->TxConfig.EvtSerCx2SystemDmaTransmitCancelDrainFifo != NULL &&
                Dma->TxConfig.EvtSerCx2SystemDmaTransmitCancelDrainFifo(Device->SystemDmaTransmit))
            {
                Transaction->Draining = FALSE;
                SerCx2NonPioFinish(Device, Transaction, Status, Transaction->Transferred);
            }
            return;
        }
        WdfDmaTransactionStopSystemTransfer(Dma->Transaction);
    }
    else if (Type == SerCx2TransactionTypeCustom)
    {
        WDFOBJECT Handle;
        PSERCX2_CUSTOM_TRANSACTION_CONTEXT Custom = SerCx2CustomFor(Device, Receive, &Handle);

        if (Custom == NULL)
            return;
        if (Receive && Custom->RxConfig.EvtSerCx2CustomReceiveTransactionCancel != NULL)
            Custom->RxConfig.EvtSerCx2CustomReceiveTransactionCancel((SERCX2CUSTOMRECEIVETRANSACTION)Handle);
        else if (!Receive && Custom->TxConfig.EvtSerCx2CustomTransmitTransactionCancel != NULL)
            Custom->TxConfig.EvtSerCx2CustomTransmitTransactionCancel((SERCX2CUSTOMTRANSMITTRANSACTION)Handle);
    }
}

static
BOOLEAN
SerCx2TxStartNext(
    _In_ PSERCX2_DEVICE_CONTEXT Device)
{
    PSERCX2_PIO_TRANSMIT_CONTEXT Pio = SerCx2GetPioTransmitContext(Device->PioTransmit);
    SERCX2_TRANSACTION_TYPE Type;
    WDF_REQUEST_PARAMETERS Parameters;
    WDFREQUEST Request;
    PVOID Buffer;
    size_t Length;
    ULONGLONG Timeout;
    KIRQL OldIrql;
    NTSTATUS Status;

    for (;;)
    {
        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Device->WriteQueue, &Request)))
            return FALSE;

        WDF_REQUEST_PARAMETERS_INIT(&Parameters);
        WdfRequestGetParameters(Request, &Parameters);
        Length = Parameters.Parameters.Write.Length;
        if (Length == 0)
        {
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
            continue;
        }

        Status = WdfRequestRetrieveInputBuffer(Request, Length, &Buffer, &Length);
        if (!NT_SUCCESS(Status))
        {
            WdfRequestComplete(Request, Status);
            continue;
        }

        Type = SerCx2SelectType(Device, Request, (ULONG)min(Length, MAXULONG), FALSE);
        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        RtlZeroMemory(&Device->Tx, sizeof(Device->Tx));
        Device->Tx.Request = Request;
        Device->Tx.Buffer = Buffer;
        Device->Tx.Length = Length;
        Device->Tx.Running = TRUE;
        Device->Tx.Type = Type;
        Timeout = (ULONGLONG)Device->Timeouts.WriteTotalTimeoutConstant +
                  (ULONGLONG)Device->Timeouts.WriteTotalTimeoutMultiplier * Length;
        KeReleaseSpinLock(&Device->Lock, OldIrql);

        if (Timeout != 0)
            WdfTimerStart(Device->WriteTimer, WDF_REL_TIMEOUT_IN_MS(Timeout));

        if (Type != SerCx2TransactionTypePio)
        {
            SerCx2NonPioBegin(Device, &Device->Tx);
            return TRUE;
        }
        if (Pio->Config.EvtSerCx2PioTransmitInitializeTransaction != NULL)
            Pio->Config.EvtSerCx2PioTransmitInitializeTransaction(Device->PioTransmit, (ULONG)Length);
        return TRUE;
    }
}

static
VOID
SerCx2TxRun(
    _In_ WDFDEVICE DeviceHandle)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PSERCX2_PIO_TRANSMIT_CONTEXT Pio;
    WDFREQUEST AbortRequest;
    NTSTATUS AbortStatus;
    size_t AbortOffset;
    KIRQL OldIrql;

    if (Device->PioTransmit == NULL)
        return;
    Pio = SerCx2GetPioTransmitContext(Device->PioTransmit);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Device->Tx.Running)
    {
        Device->Tx.ReadyPending = TRUE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }
    Device->Tx.Running = TRUE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    for (;;)
    {
        WDFREQUEST Request;
        size_t Remaining;
        ULONG Written;

        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        Request = Device->Tx.Request;
        if (Request != NULL && Device->Tx.Type != SerCx2TransactionTypePio)
        {
            Device->Tx.Running = FALSE;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            return;
        }
        if (Request != NULL && Device->Tx.WaitingReady)
        {
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            if (!SerCx2ResumeAfterReady(Device, &Device->Tx, &AbortRequest, &AbortStatus, &AbortOffset))
            {
                if (AbortRequest != NULL)
                {
                    SerCx2CompleteTransaction(Device, &Device->Tx, AbortRequest, AbortStatus, AbortOffset);
                    continue;
                }
                break;
            }
            if (Device->Tx.Draining)
            {
                Request = Device->Tx.Request;
                KeAcquireSpinLock(&Device->Lock, &OldIrql);
                Device->Tx.Request = NULL;
                Device->Tx.Draining = FALSE;
                KeReleaseSpinLock(&Device->Lock, OldIrql);
                SerCx2CompleteTransaction(Device, &Device->Tx, Request, STATUS_SUCCESS, Device->Tx.Length);
                continue;
            }
        }
        else
        {
            KeReleaseSpinLock(&Device->Lock, OldIrql);
        }

        if (Request == NULL)
        {
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            Device->Tx.Running = FALSE;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            if (!SerCx2TxStartNext(Device))
                return;
            continue;
        }

        Remaining = Device->Tx.Length - Device->Tx.Offset;
        Written = Pio->Config.EvtSerCx2PioTransmitWriteBuffer(Device->PioTransmit,
                                                              Device->Tx.Buffer + Device->Tx.Offset,
                                                              (ULONG)min(Remaining, MAXULONG));
        Device->Tx.Offset += min(Written, Remaining);

        if (Device->Tx.Offset >= Device->Tx.Length)
        {
            if (Pio->Config.EvtSerCx2PioTransmitDrainFifo != NULL)
            {
                Device->Tx.Draining = TRUE;
                if (!SerCx2ArmWait(Device, &Device->Tx, SerCx2EvtWriteCancel, &AbortRequest, &AbortOffset))
                {
                    SerCx2CompleteTransaction(Device, &Device->Tx, AbortRequest, STATUS_CANCELLED, AbortOffset);
                    continue;
                }
                Pio->Config.EvtSerCx2PioTransmitDrainFifo(Device->PioTransmit);
            }
            else
            {
                KeAcquireSpinLock(&Device->Lock, &OldIrql);
                Device->Tx.Request = NULL;
                KeReleaseSpinLock(&Device->Lock, OldIrql);
                SerCx2CompleteTransaction(Device, &Device->Tx, Request, STATUS_SUCCESS, Device->Tx.Length);
                continue;
            }
        }
        else
        {
            if (!SerCx2ArmWait(Device, &Device->Tx, SerCx2EvtWriteCancel, &AbortRequest, &AbortOffset))
            {
                SerCx2CompleteTransaction(Device, &Device->Tx, AbortRequest, STATUS_CANCELLED, AbortOffset);
                continue;
            }
            Pio->Config.EvtSerCx2PioTransmitEnableReadyNotification(Device->PioTransmit);
        }

        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        if (Device->Tx.ReadyPending)
        {
            Device->Tx.ReadyPending = FALSE;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            continue;
        }
        Device->Tx.Running = FALSE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Device->Tx.Running = FALSE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
}

static
BOOLEAN
SerCx2RxStartNext(
    _In_ PSERCX2_DEVICE_CONTEXT Device)
{
    PSERCX2_PIO_RECEIVE_CONTEXT Pio = SerCx2GetPioReceiveContext(Device->PioReceive);
    SERCX2_TRANSACTION_TYPE Type;
    WDF_REQUEST_PARAMETERS Parameters;
    WDFREQUEST Request;
    PVOID Buffer;
    size_t Length;
    ULONGLONG Timeout;
    KIRQL OldIrql;
    NTSTATUS Status;

    for (;;)
    {
        if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Device->ReadQueue, &Request)))
            return FALSE;

        WDF_REQUEST_PARAMETERS_INIT(&Parameters);
        WdfRequestGetParameters(Request, &Parameters);
        Length = Parameters.Parameters.Read.Length;
        if (Length == 0)
        {
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
            continue;
        }

        Status = WdfRequestRetrieveOutputBuffer(Request, Length, &Buffer, &Length);
        if (!NT_SUCCESS(Status))
        {
            WdfRequestComplete(Request, Status);
            continue;
        }

        Type = SerCx2SelectType(Device, Request, (ULONG)min(Length, MAXULONG), TRUE);
        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        RtlZeroMemory(&Device->Rx, sizeof(Device->Rx));
        Device->Rx.Request = Request;
        Device->Rx.Buffer = Buffer;
        Device->Rx.Length = Length;
        Device->Rx.Running = TRUE;
        Device->Rx.Type = Type;
        Timeout = 0;
        if (Device->Timeouts.ReadTotalTimeoutConstant != 0 || Device->Timeouts.ReadTotalTimeoutMultiplier != 0)
        {
            if (Device->Timeouts.ReadTotalTimeoutMultiplier == MAXULONG)
                Timeout = Device->Timeouts.ReadTotalTimeoutConstant;
            else
                Timeout = (ULONGLONG)Device->Timeouts.ReadTotalTimeoutConstant +
                          (ULONGLONG)Device->Timeouts.ReadTotalTimeoutMultiplier * Length;
        }
        KeReleaseSpinLock(&Device->Lock, OldIrql);

        if (Timeout != 0)
            WdfTimerStart(Device->ReadTotalTimer, WDF_REL_TIMEOUT_IN_MS(Timeout));

        if (Type != SerCx2TransactionTypePio)
        {
            SerCx2NonPioBegin(Device, &Device->Rx);
            return TRUE;
        }
        if (Pio->Config.EvtSerCx2PioReceiveInitializeTransaction != NULL)
            Pio->Config.EvtSerCx2PioReceiveInitializeTransaction(Device->PioReceive, (ULONG)Length);
        return TRUE;
    }
}

static
VOID
SerCx2RxRun(
    _In_ WDFDEVICE DeviceHandle)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PSERCX2_PIO_RECEIVE_CONTEXT Pio;
    WDFREQUEST AbortRequest;
    NTSTATUS AbortStatus;
    size_t AbortOffset;
    KIRQL OldIrql;

    if (Device->PioReceive == NULL)
        return;
    Pio = SerCx2GetPioReceiveContext(Device->PioReceive);

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (Device->Rx.Running)
    {
        Device->Rx.ReadyPending = TRUE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }
    Device->Rx.Running = TRUE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    for (;;)
    {
        WDFREQUEST Request;
        size_t Remaining;
        ULONG Read;
        ULONG Interval;
        BOOLEAN ReturnImmediately;
        BOOLEAN ReturnIfData;

        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        Request = Device->Rx.Request;
        if (Request != NULL && Device->Rx.Type != SerCx2TransactionTypePio)
        {
            Device->Rx.Running = FALSE;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            return;
        }
        Interval = Device->Timeouts.ReadIntervalTimeout;
        ReturnImmediately = Interval == MAXULONG &&
                            Device->Timeouts.ReadTotalTimeoutConstant == 0 &&
                            Device->Timeouts.ReadTotalTimeoutMultiplier == 0;
        ReturnIfData = Interval == MAXULONG && Device->Timeouts.ReadTotalTimeoutMultiplier == MAXULONG;
        if (Request != NULL && Device->Rx.WaitingReady)
        {
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            if (!SerCx2ResumeAfterReady(Device, &Device->Rx, &AbortRequest, &AbortStatus, &AbortOffset))
            {
                if (AbortRequest != NULL)
                {
                    SerCx2CompleteTransaction(Device, &Device->Rx, AbortRequest, AbortStatus, AbortOffset);
                    continue;
                }
                break;
            }
        }
        else
        {
            KeReleaseSpinLock(&Device->Lock, OldIrql);
        }

        if (Request == NULL)
        {
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            Device->Rx.Running = FALSE;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            if (!SerCx2RxStartNext(Device))
                return;
            continue;
        }

        Remaining = Device->Rx.Length - Device->Rx.Offset;
        Read = Pio->Config.EvtSerCx2PioReceiveReadBuffer(Device->PioReceive,
                                                         Device->Rx.Buffer + Device->Rx.Offset,
                                                         (ULONG)min(Remaining, MAXULONG));
        Read = (ULONG)min(Read, Remaining);
        Device->Rx.Offset += Read;

        if (Device->Rx.Offset >= Device->Rx.Length ||
            (Read == 0 && ReturnImmediately) ||
            (Device->Rx.Offset != 0 && (ReturnIfData || (Read == 0 && Interval == MAXULONG))))
        {
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            Device->Rx.Request = NULL;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            SerCx2CompleteTransaction(Device, &Device->Rx, Request, STATUS_SUCCESS, Device->Rx.Offset);
            continue;
        }

        if (Read != 0)
        {
            if (Interval != 0 && Interval != MAXULONG)
                WdfTimerStart(Device->ReadIntervalTimer, WDF_REL_TIMEOUT_IN_MS(Interval));
            continue;
        }

        if (!SerCx2ArmWait(Device, &Device->Rx, SerCx2EvtReadCancel, &AbortRequest, &AbortOffset))
        {
            SerCx2CompleteTransaction(Device, &Device->Rx, AbortRequest, STATUS_CANCELLED, AbortOffset);
            continue;
        }
        Pio->Config.EvtSerCx2PioReceiveEnableReadyNotification(Device->PioReceive);

        KeAcquireSpinLock(&Device->Lock, &OldIrql);
        if (Device->Rx.ReadyPending)
        {
            Device->Rx.ReadyPending = FALSE;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            continue;
        }
        Device->Rx.Running = FALSE;
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Device->Rx.Running = FALSE;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
}

static
VOID
NTAPI
SerCx2EvtIoWrite(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(Queue);
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Length);

    if (Device->PioTransmit == NULL)
    {
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
        return;
    }

    Status = SerCx2PrepareRequest(Device, Request);
    if (NT_SUCCESS(Status))
        Status = WdfRequestForwardToIoQueue(Request, Device->WriteQueue);
    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return;
    }

    SerCx2TxRun(DeviceHandle);
}

static
VOID
NTAPI
SerCx2EvtIoRead(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t Length)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(Queue);
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Length);

    if (Device->PioReceive == NULL)
    {
        WdfRequestComplete(Request, STATUS_NOT_SUPPORTED);
        return;
    }

    Status = SerCx2PrepareRequest(Device, Request);
    if (NT_SUCCESS(Status))
        Status = WdfRequestForwardToIoQueue(Request, Device->ReadQueue);
    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return;
    }

    SerCx2RxRun(DeviceHandle);
}

static
VOID
SerCx2CompletePendingWait(
    _In_ PSERCX2_DEVICE_CONTEXT Device,
    _In_ ULONG Events)
{
    WDFREQUEST Request;
    PULONG Output;

    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Device->WaitQueue, &Request)))
    {
        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID *)&Output, NULL)))
        {
            *Output = Events;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
        }
        else
        {
            WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        }
    }
}

static
VOID
SerCx2Purge(
    _In_ WDFDEVICE DeviceHandle,
    _In_ ULONG Mask)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);

    if (Mask & SERIAL_PURGE_TXABORT)
    {
        WdfIoQueuePurge(Device->WriteQueue, NULL, NULL);
        SerCx2AbortTransaction(DeviceHandle, &Device->Tx, NULL, STATUS_CANCELLED);
        WdfIoQueueStart(Device->WriteQueue);
    }

    if (Mask & SERIAL_PURGE_RXABORT)
    {
        WdfIoQueuePurge(Device->ReadQueue, NULL, NULL);
        SerCx2AbortTransaction(DeviceHandle, &Device->Rx, NULL, STATUS_CANCELLED);
        WdfIoQueueStart(Device->ReadQueue);
    }

    if ((Mask & (SERIAL_PURGE_TXCLEAR | SERIAL_PURGE_RXCLEAR)) &&
        Device->Config.EvtSerCx2PurgeFifos != NULL)
    {
        Device->Config.EvtSerCx2PurgeFifos(DeviceHandle,
                                           (Mask & SERIAL_PURGE_RXCLEAR) != 0,
                                           (Mask & SERIAL_PURGE_TXCLEAR) != 0);
    }
}

static
VOID
NTAPI
SerCx2EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(Queue);
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    KIRQL OldIrql;
    PVOID Buffer;
    ULONG Value;
    NTSTATUS Status;

    Status = SerCx2PrepareRequest(Device, Request);
    if (!NT_SUCCESS(Status))
    {
        WdfRequestComplete(Request, Status);
        return;
    }

    switch (IoControlCode)
    {
        case IOCTL_SERIAL_SET_WAIT_MASK:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            Value = *(PULONG)Buffer;
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            Device->WaitMask = Value;
            Device->HistoryMask = 0;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            SerCx2CompletePendingWait(Device, 0);
            if (Device->Config.EvtSerCx2SetWaitMask != NULL)
            {
                Device->Config.EvtSerCx2SetWaitMask(DeviceHandle, Request, Value);
                return;
            }
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_SERIAL_GET_WAIT_MASK:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            *(PULONG)Buffer = Device->WaitMask;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
            return;

        case IOCTL_SERIAL_WAIT_ON_MASK:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            if (Device->WaitMask == 0)
            {
                KeReleaseSpinLock(&Device->Lock, OldIrql);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Value = Device->HistoryMask & Device->WaitMask;
            Device->HistoryMask &= ~Value;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            if (Value != 0)
            {
                *(PULONG)Buffer = Value;
                WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
                return;
            }
            Status = WdfRequestForwardToIoQueue(Request, Device->WaitQueue);
            if (NT_SUCCESS(Status))
                return;
            break;

        case IOCTL_SERIAL_SET_TIMEOUTS:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(SERIAL_TIMEOUTS), &Buffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            Device->Timeouts = *(PSERIAL_TIMEOUTS)Buffer;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_SERIAL_GET_TIMEOUTS:
            Status = WdfRequestRetrieveOutputBuffer(Request, sizeof(SERIAL_TIMEOUTS), &Buffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            KeAcquireSpinLock(&Device->Lock, &OldIrql);
            *(PSERIAL_TIMEOUTS)Buffer = Device->Timeouts;
            KeReleaseSpinLock(&Device->Lock, OldIrql);
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(SERIAL_TIMEOUTS));
            return;

        case IOCTL_SERIAL_PURGE:
            Status = WdfRequestRetrieveInputBuffer(Request, sizeof(ULONG), &Buffer, NULL);
            if (!NT_SUCCESS(Status))
                break;
            Value = *(PULONG)Buffer;
            if (Value == 0 || (Value & ~(SERIAL_PURGE_TXABORT | SERIAL_PURGE_RXABORT |
                                        SERIAL_PURGE_TXCLEAR | SERIAL_PURGE_RXCLEAR)) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            SerCx2Purge(DeviceHandle, Value);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_SERIAL_SET_QUEUE_SIZE:
            Status = STATUS_SUCCESS;
            break;

        default:
            if (Device->Config.EvtSerCx2Control != NULL)
            {
                Device->Config.EvtSerCx2Control(DeviceHandle, Request, OutputBufferLength, InputBufferLength, IoControlCode);
                return;
            }
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    WdfRequestComplete(Request, Status);
}

static
VOID
NTAPI
SerCx2EvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags)
{
    UNREFERENCED_PARAMETER(Queue);

    if (ActionFlags & WdfRequestStopRequestCancelable)
        return;

    WdfRequestStopAcknowledge(Request, FALSE);
}

static
BOOLEAN
NTAPI
SerCx2EvtCxDeviceFileCreate(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFREQUEST Request,
    _In_opt_ WDFFILEOBJECT FileObject)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties = NULL;
    LARGE_INTEGER ConnectionId;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    if (FileObject == NULL || !Device->Initialized)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return TRUE;
    }

    if (Device->Opened)
    {
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return TRUE;
    }

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Device->WaitMask = 0;
    Device->HistoryMask = 0;
    RtlZeroMemory(&Device->Timeouts, sizeof(Device->Timeouts));
    KeReleaseSpinLock(&Device->Lock, OldIrql);

    if (WdfCxParseConnectionId(WdfFileObjectGetFileName(FileObject), &ConnectionId))
    {
        Status = WdfCxQueryConnectionProperties(ConnectionId, &Properties);
        if (NT_SUCCESS(Status) && Device->Config.EvtSerCx2ApplyConfig != NULL)
            Status = Device->Config.EvtSerCx2ApplyConfig(DeviceHandle, Properties);
        if (Properties != NULL)
            ExFreePoolWithTag(Properties, WDFCX_TAG);
    }

    if (NT_SUCCESS(Status) && Device->Config.EvtSerCx2FileOpen != NULL)
        Status = Device->Config.EvtSerCx2FileOpen(DeviceHandle);

    if (NT_SUCCESS(Status))
        Device->Opened = TRUE;

    WdfRequestComplete(Request, Status);
    return TRUE;
}

static
VOID
NTAPI
SerCx2EvtFileClose(
    _In_ WDFFILEOBJECT FileObject)
{
    WDFDEVICE DeviceHandle = WdfFileObjectGetDevice(FileObject);
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);

    if (!Device->Opened)
        return;

    SerCx2Purge(DeviceHandle, SERIAL_PURGE_TXABORT | SERIAL_PURGE_RXABORT);
    SerCx2CompletePendingWait(Device, 0);

    Device->Opened = FALSE;
    if (Device->Config.EvtSerCx2FileClose != NULL)
        Device->Config.EvtSerCx2FileClose(DeviceHandle);
}

static
NTSTATUS
NTAPI
SerCx2DdiInitializeDeviceInit(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    PWDFCXDEVICE_INIT CxInit;
    WDFCX_FILEOBJECT_CONFIG FileConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;

    if (ClientGlobals == NULL || DeviceInit == NULL)
        return STATUS_INVALID_PARAMETER;

    CxInit = WdfCxDeviceInitAllocate(WdfDriverGlobals, DeviceInit);
    if (CxInit == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    WdfCxClientDeviceInitSetIoType(ClientGlobals, DeviceInit, WdfDeviceIoBuffered);
    WdfCxClientDeviceInitSetDeviceType(ClientGlobals, DeviceInit, FILE_DEVICE_SERIAL_PORT);
    WdfCxClientDeviceInitSetExclusive(ClientGlobals, DeviceInit, TRUE);
    WdfCxClientDeviceInitSetCharacteristics(ClientGlobals, DeviceInit, FILE_AUTOGENERATED_DEVICE_NAME, TRUE);

    RtlZeroMemory(&FileConfig, sizeof(FileConfig));
    FileConfig.Size = sizeof(FileConfig);
    FileConfig.EvtCxDeviceFileCreate = SerCx2EvtCxDeviceFileCreate;
    FileConfig.EvtFileClose = SerCx2EvtFileClose;
    FileConfig.AutoForwardCleanupClose = WdfFalse;
    FileConfig.FileObjectClass = WdfFileObjectWdfCannotUseFsContexts;

    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    WdfCxDeviceInitSetFileObjectConfig(WdfDriverGlobals, CxInit, &FileConfig, &Attributes);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_REQUEST_CONTEXT);
    Attributes.EvtCleanupCallback = SerCx2EvtRequestCleanup;
    WdfCxDeviceInitSetRequestAttributes(WdfDriverGlobals, CxInit, &Attributes);

    return STATUS_SUCCESS;
}

static
NTSTATUS
SerCx2CreateTimer(
    _In_ WDFDEVICE DeviceHandle,
    _In_ PFN_WDF_TIMER Callback,
    _Out_ WDFTIMER *Timer)
{
    WDF_TIMER_CONFIG Config;
    WDF_OBJECT_ATTRIBUTES Attributes;

    WDF_TIMER_CONFIG_INIT(&Config, Callback);
    Config.AutomaticSerialization = FALSE;
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = DeviceHandle;
    return WdfTimerCreate(&Config, &Attributes, Timer);
}

static
NTSTATUS
NTAPI
SerCx2DdiInitializeDevice(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_CONFIG Config)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    PSERCX2_DEVICE_CONTEXT Device;
    NTSTATUS Status;

    if (ClientGlobals == NULL || DeviceHandle == NULL || Config == NULL ||
        Config->Size != sizeof(SERCX2_CONFIG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_DEVICE_CONTEXT);
    Status = WdfObjectAllocateContext(DeviceHandle, &Attributes, (PVOID *)&Device);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
        return STATUS_INVALID_DEVICE_STATE;
    if (!NT_SUCCESS(Status))
        return Status;

    Device->ClientGlobals = ClientGlobals;
    Device->Config = *Config;
    KeInitializeSpinLock(&Device->Lock);

    if (Config->RequestAttributes != NULL &&
        Config->RequestAttributes->Size == sizeof(WDF_OBJECT_ATTRIBUTES) &&
        Config->RequestAttributes->ContextTypeInfo != NULL)
    {
        Device->RequestAttributes = *Config->RequestAttributes;
        Device->HasRequestAttributes = TRUE;
    }

    Status = SerCx2CreateTimer(DeviceHandle, SerCx2EvtWriteTimer, &Device->WriteTimer);
    if (NT_SUCCESS(Status))
        Status = SerCx2CreateTimer(DeviceHandle, SerCx2EvtReadTimer, &Device->ReadTotalTimer);
    if (NT_SUCCESS(Status))
        Status = SerCx2CreateTimer(DeviceHandle, SerCx2EvtReadTimer, &Device->ReadIntervalTimer);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_IO_QUEUE_CONFIG_INIT(&QueueConfig, WdfIoQueueDispatchManual);
    QueueConfig.PowerManaged = WdfFalse;
    Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->WriteQueue);
    if (NT_SUCCESS(Status))
        Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->ReadQueue);
    if (NT_SUCCESS(Status))
        Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->WaitQueue);
    if (!NT_SUCCESS(Status))
        return Status;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchParallel);
    QueueConfig.PowerManaged = WdfTrue;
    QueueConfig.EvtIoRead = SerCx2EvtIoRead;
    QueueConfig.EvtIoWrite = SerCx2EvtIoWrite;
    QueueConfig.EvtIoDeviceControl = SerCx2EvtIoDeviceControl;
    QueueConfig.EvtIoStop = SerCx2EvtIoStop;
    Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->Queue);
    if (!NT_SUCCESS(Status))
        return Status;

    Device->Initialized = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SerCx2DdiCompleteWait(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ ULONG Events)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    WDFREQUEST Request;
    PULONG Output;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Device == NULL)
        return;

    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    Events &= Device->WaitMask;
    if (Events == 0)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }

    if (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Device->WaitQueue, &Request)))
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        if (NT_SUCCESS(WdfRequestRetrieveOutputBuffer(Request, sizeof(ULONG), (PVOID *)&Output, NULL)))
        {
            *Output = Events;
            WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, sizeof(ULONG));
        }
        else
        {
            WdfRequestComplete(Request, STATUS_BUFFER_TOO_SMALL);
        }
        return;
    }

    Device->HistoryMask |= Events;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
}

static
NTSTATUS
NTAPI
SerCx2DdiPioTransmitCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_PIO_TRANSMIT_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2PIOTRANSMIT *Object)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PSERCX2_PIO_TRANSMIT_CONTEXT Context;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFOBJECT Handle;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ClientGlobals);
    *Object = NULL;

    if (Device == NULL || Config == NULL || Config->Size != sizeof(SERCX2_PIO_TRANSMIT_CONFIG) ||
        Config->EvtSerCx2PioTransmitWriteBuffer == NULL ||
        Config->EvtSerCx2PioTransmitEnableReadyNotification == NULL ||
        Config->EvtSerCx2PioTransmitCancelReadyNotification == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Device->PioTransmit != NULL)
        return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_PIO_TRANSMIT_CONTEXT);
    Attributes.ParentObject = DeviceHandle;
    Status = WdfObjectCreate(&Attributes, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    Context = SerCx2GetPioTransmitContext(Handle);
    Context->Device = DeviceHandle;
    Context->Config = *Config;

    if (ClientAttributes != NULL && ClientAttributes->ContextTypeInfo != NULL)
    {
        Attributes = *ClientAttributes;
        Attributes.ParentObject = NULL;
        Status = WdfObjectAllocateContext(Handle, &Attributes, NULL);
        if (!NT_SUCCESS(Status))
        {
            WdfObjectDelete(Handle);
            return Status;
        }
    }

    Device->PioTransmit = (SERCX2PIOTRANSMIT)Handle;
    *Object = Device->PioTransmit;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SerCx2DdiPioReceiveCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_PIO_RECEIVE_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2PIORECEIVE *Object)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PSERCX2_PIO_RECEIVE_CONTEXT Context;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFOBJECT Handle;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ClientGlobals);
    *Object = NULL;

    if (Device == NULL || Config == NULL || Config->Size != sizeof(SERCX2_PIO_RECEIVE_CONFIG) ||
        Config->EvtSerCx2PioReceiveReadBuffer == NULL ||
        Config->EvtSerCx2PioReceiveEnableReadyNotification == NULL ||
        Config->EvtSerCx2PioReceiveCancelReadyNotification == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Device->PioReceive != NULL)
        return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_PIO_RECEIVE_CONTEXT);
    Attributes.ParentObject = DeviceHandle;
    Status = WdfObjectCreate(&Attributes, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;

    Context = SerCx2GetPioReceiveContext(Handle);
    Context->Device = DeviceHandle;
    Context->Config = *Config;

    if (ClientAttributes != NULL && ClientAttributes->ContextTypeInfo != NULL)
    {
        Attributes = *ClientAttributes;
        Attributes.ParentObject = NULL;
        Status = WdfObjectAllocateContext(Handle, &Attributes, NULL);
        if (!NT_SUCCESS(Status))
        {
            WdfObjectDelete(Handle);
            return Status;
        }
    }

    Device->PioReceive = (SERCX2PIORECEIVE)Handle;
    *Object = Device->PioReceive;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
SerCx2DdiPioTransmitReady(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2PIOTRANSMIT Object)
{
    PSERCX2_PIO_TRANSMIT_CONTEXT Context = SerCx2GetPioTransmitContext(Object);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Context != NULL)
        SerCx2TxRun(Context->Device);
}

static
VOID
NTAPI
SerCx2DdiPioReceiveReady(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2PIORECEIVE Object)
{
    PSERCX2_PIO_RECEIVE_CONTEXT Context = SerCx2GetPioReceiveContext(Object);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Context != NULL)
        SerCx2RxRun(Context->Device);
}

static
VOID
NTAPI
SerCx2DdiPioTransmitDrainFifoComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2PIOTRANSMIT Object)
{
    PSERCX2_PIO_TRANSMIT_CONTEXT Context = SerCx2GetPioTransmitContext(Object);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Context != NULL)
        SerCx2TxRun(Context->Device);
}

static
VOID
NTAPI
SerCx2DdiPioTransmitPurgeFifoComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2PIOTRANSMIT Object,
    _In_ ULONG BytesPurged)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(BytesPurged);
}

static
VOID
NTAPI
SerCx2DdiNoOperation(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    ...)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
}


static
NTSTATUS
SerCx2CreateDmaObject(
    _In_ WDFDEVICE DeviceHandle,
    _In_ BOOLEAN Receive,
    _In_ PVOID Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ WDFOBJECT *Object)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PSERCX2_SYSTEM_DMA_CONTEXT Context;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDF_DMA_ENABLER_CONFIG EnablerConfig;
    WDFOBJECT Handle;
    ULONG MaximumLength;
    NTSTATUS Status;

    *Object = NULL;
    if (Device == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Receive ? Device->SystemDmaReceive != NULL : Device->SystemDmaTransmit != NULL)
        return STATUS_INVALID_DEVICE_STATE;
    if (Receive ? ((PSERCX2_SYSTEM_DMA_RECEIVE_CONFIG)Config)->Size != sizeof(SERCX2_SYSTEM_DMA_RECEIVE_CONFIG) :
                  ((PSERCX2_SYSTEM_DMA_TRANSMIT_CONFIG)Config)->Size != sizeof(SERCX2_SYSTEM_DMA_TRANSMIT_CONFIG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_SYSTEM_DMA_CONTEXT);
    Attributes.ParentObject = DeviceHandle;
    Status = WdfObjectCreate(&Attributes, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Context = SerCx2GetSystemDmaContext(Handle);
    Context->Device = DeviceHandle;
    Context->Receive = Receive;
    if (Receive)
        Context->RxConfig = *(PSERCX2_SYSTEM_DMA_RECEIVE_CONFIG)Config;
    else
        Context->TxConfig = *(PSERCX2_SYSTEM_DMA_TRANSMIT_CONFIG)Config;

    MaximumLength = Receive ? Context->RxConfig.MaximumTransactionLength : Context->TxConfig.MaximumTransactionLength;
    if (MaximumLength == 0)
        MaximumLength = 64 * 1024;
    WDF_DMA_ENABLER_CONFIG_INIT(&EnablerConfig, WdfDmaProfileSystem, MaximumLength);
    WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
    Attributes.ParentObject = Handle;
    Status = WdfDmaEnablerCreate(DeviceHandle, &EnablerConfig, &Attributes, &Context->Enabler);
    if (NT_SUCCESS(Status))
    {
        WDF_OBJECT_ATTRIBUTES_INIT(&Attributes);
        Attributes.ParentObject = Handle;
        Status = WdfDmaTransactionCreate(Context->Enabler, &Attributes, &Context->Transaction);
    }
    if (!NT_SUCCESS(Status))
    {
        WdfObjectDelete(Handle);
        return Status;
    }

    if (ClientAttributes != NULL && ClientAttributes->ContextTypeInfo != NULL)
    {
        Attributes = *ClientAttributes;
        Attributes.ParentObject = NULL;
        Status = WdfObjectAllocateContext(Handle, &Attributes, NULL);
        if (!NT_SUCCESS(Status))
        {
            WdfObjectDelete(Handle);
            return Status;
        }
    }

    if (Receive)
        Device->SystemDmaReceive = (SERCX2SYSTEMDMARECEIVE)Handle;
    else
        Device->SystemDmaTransmit = (SERCX2SYSTEMDMATRANSMIT)Handle;
    *Object = Handle;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SerCx2DdiSystemDmaTransmitCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_SYSTEM_DMA_TRANSMIT_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2SYSTEMDMATRANSMIT *Object)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return SerCx2CreateDmaObject(DeviceHandle, FALSE, Config, ClientAttributes, (WDFOBJECT *)Object);
}

static
NTSTATUS
NTAPI
SerCx2DdiSystemDmaReceiveCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_SYSTEM_DMA_RECEIVE_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2SYSTEMDMARECEIVE *Object)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return SerCx2CreateDmaObject(DeviceHandle, TRUE, Config, ClientAttributes, (WDFOBJECT *)Object);
}

static
WDFDMAENABLER
NTAPI
SerCx2DdiSystemDmaGetDmaEnabler(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFOBJECT Object)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Object == NULL)
        return NULL;
    return SerCx2GetSystemDmaContext(Object)->Enabler;
}

static
PSERCX2_TRANSACTION
SerCx2DmaTransactionFor(
    _In_ WDFOBJECT Object,
    _Out_ PSERCX2_DEVICE_CONTEXT *Device)
{
    PSERCX2_SYSTEM_DMA_CONTEXT Context;

    *Device = NULL;
    if (Object == NULL)
        return NULL;
    Context = SerCx2GetSystemDmaContext(Object);
    *Device = SerCx2GetDeviceContext(Context->Device);
    return Context->Receive ? &(*Device)->Rx : &(*Device)->Tx;
}

static
VOID
NTAPI
SerCx2DdiSystemDmaInitializeTransactionComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFOBJECT Object,
    _In_ NTSTATUS Status)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2DmaTransactionFor(Object, &Device);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction != NULL)
        SerCx2NonPioInitialized(Device, Transaction, Status);
}

static
VOID
NTAPI
SerCx2DdiSystemDmaCleanupTransactionComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFOBJECT Object)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2DmaTransactionFor(Object, &Device);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction != NULL && Transaction->Cleaning)
        SerCx2NonPioCleanupComplete(Device, Transaction);
}

static
VOID
NTAPI
SerCx2DdiSystemDmaReceiveNewDataNotification(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2SYSTEMDMARECEIVE Object)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2DmaTransactionFor((WDFOBJECT)Object, &Device);
    PSERCX2_SYSTEM_DMA_CONTEXT Context;

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction == NULL)
        return;
    SerCx2NonPioRestartInterval(Device);
    Context = SerCx2GetSystemDmaContext((WDFOBJECT)Object);
    if (Transaction->Request != NULL && !Transaction->Cleaning &&
        Context->RxConfig.EvtSerCx2SystemDmaReceiveEnableNewDataNotification != NULL)
    {
        Context->RxConfig.EvtSerCx2SystemDmaReceiveEnableNewDataNotification(Object);
    }
}

static
VOID
NTAPI
SerCx2DdiSystemDmaTransmitDrainFifoComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2SYSTEMDMATRANSMIT Object)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2DmaTransactionFor((WDFOBJECT)Object, &Device);
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction == NULL)
        return;
    KeAcquireSpinLock(&Device->Lock, &OldIrql);
    if (!Transaction->Draining)
    {
        KeReleaseSpinLock(&Device->Lock, OldIrql);
        return;
    }
    Transaction->Draining = FALSE;
    Status = Transaction->AbortPending ? Transaction->AbortStatus : STATUS_SUCCESS;
    KeReleaseSpinLock(&Device->Lock, OldIrql);
    SerCx2NonPioFinish(Device, Transaction, Status, Transaction->Transferred);
}

static
VOID
NTAPI
SerCx2DdiSystemDmaTransmitPurgeFifoComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2SYSTEMDMATRANSMIT Object,
    _In_ ULONG BytesPurged)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(BytesPurged);
}

static
NTSTATUS
SerCx2CreateCustomObject(
    _In_ WDFDEVICE DeviceHandle,
    _In_ BOOLEAN Receive,
    _In_ PVOID Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ WDFOBJECT *Object)
{
    PSERCX2_DEVICE_CONTEXT Device = SerCx2GetDeviceContext(DeviceHandle);
    PSERCX2_CUSTOM_CONTEXT Context;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFOBJECT Handle;
    NTSTATUS Status;

    *Object = NULL;
    if (Device == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Receive ? Device->CustomReceive != NULL : Device->CustomTransmit != NULL)
        return STATUS_INVALID_DEVICE_STATE;
    if (Receive ? ((PSERCX2_CUSTOM_RECEIVE_CONFIG)Config)->Size != sizeof(SERCX2_CUSTOM_RECEIVE_CONFIG) :
                  ((PSERCX2_CUSTOM_TRANSMIT_CONFIG)Config)->Size != sizeof(SERCX2_CUSTOM_TRANSMIT_CONFIG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_CUSTOM_CONTEXT);
    Attributes.ParentObject = DeviceHandle;
    Status = WdfObjectCreate(&Attributes, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Context = SerCx2GetCustomContext(Handle);
    Context->Device = DeviceHandle;
    Context->Receive = Receive;
    if (Receive)
        Context->RxConfig = *(PSERCX2_CUSTOM_RECEIVE_CONFIG)Config;
    else
        Context->TxConfig = *(PSERCX2_CUSTOM_TRANSMIT_CONFIG)Config;

    if (ClientAttributes != NULL && ClientAttributes->ContextTypeInfo != NULL)
    {
        Attributes = *ClientAttributes;
        Attributes.ParentObject = NULL;
        Status = WdfObjectAllocateContext(Handle, &Attributes, NULL);
        if (!NT_SUCCESS(Status))
        {
            WdfObjectDelete(Handle);
            return Status;
        }
    }

    if (Receive)
        Device->CustomReceive = (SERCX2CUSTOMRECEIVE)Handle;
    else
        Device->CustomTransmit = (SERCX2CUSTOMTRANSMIT)Handle;
    *Object = Handle;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SerCx2DdiCustomTransmitCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_CUSTOM_TRANSMIT_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2CUSTOMTRANSMIT *Object)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return SerCx2CreateCustomObject(DeviceHandle, FALSE, Config, ClientAttributes, (WDFOBJECT *)Object);
}

static
NTSTATUS
NTAPI
SerCx2DdiCustomReceiveCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFDEVICE DeviceHandle,
    _In_ PSERCX2_CUSTOM_RECEIVE_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2CUSTOMRECEIVE *Object)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return SerCx2CreateCustomObject(DeviceHandle, TRUE, Config, ClientAttributes, (WDFOBJECT *)Object);
}

static
NTSTATUS
SerCx2CreateCustomTransaction(
    _In_ WDFOBJECT Owner,
    _In_ BOOLEAN Receive,
    _In_ PVOID Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ WDFOBJECT *Object)
{
    PSERCX2_CUSTOM_CONTEXT Custom;
    PSERCX2_CUSTOM_TRANSACTION_CONTEXT Context;
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFOBJECT Handle;
    NTSTATUS Status;

    *Object = NULL;
    if (Owner == NULL || Config == NULL)
        return STATUS_INVALID_PARAMETER;
    Custom = SerCx2GetCustomContext(Owner);
    if (Custom == NULL || Custom->Receive != Receive)
        return STATUS_INVALID_PARAMETER;
    if (Receive ? ((PSERCX2_CUSTOM_RECEIVE_TRANSACTION_CONFIG)Config)->Size != sizeof(SERCX2_CUSTOM_RECEIVE_TRANSACTION_CONFIG) :
                  ((PSERCX2_CUSTOM_TRANSMIT_TRANSACTION_CONFIG)Config)->Size != sizeof(SERCX2_CUSTOM_TRANSMIT_TRANSACTION_CONFIG))
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, SERCX2_CUSTOM_TRANSACTION_CONTEXT);
    Attributes.ParentObject = Owner;
    Status = WdfObjectCreate(&Attributes, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    Context = SerCx2GetCustomTransactionContext(Handle);
    Context->Device = Custom->Device;
    Context->Owner = Owner;
    Context->Receive = Receive;
    if (Receive)
        Context->RxConfig = *(PSERCX2_CUSTOM_RECEIVE_TRANSACTION_CONFIG)Config;
    else
        Context->TxConfig = *(PSERCX2_CUSTOM_TRANSMIT_TRANSACTION_CONFIG)Config;

    if (ClientAttributes != NULL && ClientAttributes->ContextTypeInfo != NULL)
    {
        Attributes = *ClientAttributes;
        Attributes.ParentObject = NULL;
        Status = WdfObjectAllocateContext(Handle, &Attributes, NULL);
        if (!NT_SUCCESS(Status))
        {
            WdfObjectDelete(Handle);
            return Status;
        }
    }

    if (Custom->Transaction == NULL)
        Custom->Transaction = Handle;
    *Object = Handle;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SerCx2DdiCustomTransmitTransactionCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2CUSTOMTRANSMIT Owner,
    _In_ PSERCX2_CUSTOM_TRANSMIT_TRANSACTION_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2CUSTOMTRANSMITTRANSACTION *Object)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return SerCx2CreateCustomTransaction((WDFOBJECT)Owner, FALSE, Config, ClientAttributes, (WDFOBJECT *)Object);
}

static
NTSTATUS
NTAPI
SerCx2DdiCustomReceiveTransactionCreate(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2CUSTOMRECEIVE Owner,
    _In_ PSERCX2_CUSTOM_RECEIVE_TRANSACTION_CONFIG Config,
    _In_opt_ PWDF_OBJECT_ATTRIBUTES ClientAttributes,
    _Out_ SERCX2CUSTOMRECEIVETRANSACTION *Object)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    return SerCx2CreateCustomTransaction((WDFOBJECT)Owner, TRUE, Config, ClientAttributes, (WDFOBJECT *)Object);
}

static
PSERCX2_TRANSACTION
SerCx2CustomTransactionFor(
    _In_ WDFOBJECT Object,
    _Out_ PSERCX2_DEVICE_CONTEXT *Device)
{
    PSERCX2_CUSTOM_TRANSACTION_CONTEXT Context;

    *Device = NULL;
    if (Object == NULL)
        return NULL;
    Context = SerCx2GetCustomTransactionContext(Object);
    *Device = SerCx2GetDeviceContext(Context->Device);
    return Context->Receive ? &(*Device)->Rx : &(*Device)->Tx;
}

static
VOID
NTAPI
SerCx2DdiCustomTransactionInitializeComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFOBJECT Object,
    _In_ NTSTATUS Status)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2CustomTransactionFor(Object, &Device);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction != NULL)
        SerCx2NonPioInitialized(Device, Transaction, Status);
}

static
VOID
NTAPI
SerCx2DdiCustomTransactionCleanupComplete(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ WDFOBJECT Object)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2CustomTransactionFor(Object, &Device);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction != NULL && Transaction->Cleaning)
        SerCx2NonPioCleanupComplete(Device, Transaction);
}

static
VOID
NTAPI
SerCx2DdiCustomReceiveTransactionNewDataNotification(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2CUSTOMRECEIVETRANSACTION Object)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2CustomTransactionFor((WDFOBJECT)Object, &Device);
    PSERCX2_CUSTOM_TRANSACTION_CONTEXT Context;

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction == NULL)
        return;
    SerCx2NonPioRestartInterval(Device);
    Context = SerCx2GetCustomTransactionContext((WDFOBJECT)Object);
    if (Transaction->Request != NULL && !Transaction->Cleaning &&
        Context->RxConfig.EvtSerCx2CustomReceiveTransactionEnableNewDataNotification != NULL)
    {
        Context->RxConfig.EvtSerCx2CustomReceiveTransactionEnableNewDataNotification(Object);
    }
}

static
VOID
NTAPI
SerCx2DdiCustomReceiveTransactionReportProgress(
    _In_ PSERCX_DRIVER_GLOBALS ClientGlobals,
    _In_ SERCX2CUSTOMRECEIVETRANSACTION Object,
    _In_ SERCX2_CUSTOM_RECEIVE_TRANSACTION_PROGRESS Progress)
{
    PSERCX2_DEVICE_CONTEXT Device;
    PSERCX2_TRANSACTION Transaction = SerCx2CustomTransactionFor((WDFOBJECT)Object, &Device);

    UNREFERENCED_PARAMETER(ClientGlobals);
    if (Transaction != NULL && Progress == SerCx2CustomReceiveTransactionBytesTransferred)
        SerCx2NonPioRestartInterval(Device);
}

static PVOID SerCx2Functions[SercxFunctionTableNumEntries] =
{
    SerCx2DdiInitializeDeviceInit,
    SerCx2DdiInitializeDevice,
    SerCx2DdiCompleteWait,
    SerCx2DdiPioTransmitCreate,
    SerCx2DdiPioReceiveCreate,
    SerCx2DdiSystemDmaTransmitCreate,
    SerCx2DdiSystemDmaReceiveCreate,
    SerCx2DdiSystemDmaGetDmaEnabler,
    SerCx2DdiSystemDmaGetDmaEnabler,
    SerCx2DdiSystemDmaInitializeTransactionComplete,
    SerCx2DdiSystemDmaInitializeTransactionComplete,
    SerCx2DdiSystemDmaCleanupTransactionComplete,
    SerCx2DdiSystemDmaCleanupTransactionComplete,
    SerCx2DdiSystemDmaReceiveNewDataNotification,
    SerCx2DdiNoOperation,
    SerCx2DdiNoOperation,
    SerCx2DdiNoOperation,
    SerCx2DdiNoOperation,
    SerCx2DdiPioTransmitReady,
    SerCx2DdiPioReceiveReady,
    SerCx2DdiNoOperation,
    SerCx2DdiPioTransmitDrainFifoComplete,
    SerCx2DdiPioTransmitPurgeFifoComplete,
    SerCx2DdiSystemDmaTransmitDrainFifoComplete,
    SerCx2DdiSystemDmaTransmitPurgeFifoComplete,
    SerCx2DdiCustomTransmitCreate,
    SerCx2DdiCustomReceiveCreate,
    SerCx2DdiCustomTransmitTransactionCreate,
    SerCx2DdiCustomReceiveTransactionCreate,
    SerCx2DdiCustomTransactionInitializeComplete,
    SerCx2DdiCustomTransactionInitializeComplete,
    SerCx2DdiCustomTransactionCleanupComplete,
    SerCx2DdiCustomTransactionCleanupComplete,
    SerCx2DdiCustomReceiveTransactionNewDataNotification,
    SerCx2DdiCustomReceiveTransactionReportProgress
};

static
NTSTATUS
NTAPI
SerCx2LibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo,
                           ClientGlobals,
                           SerCx2Functions,
                           RTL_NUMBER_OF(SerCx2Functions),
                           2);
}

static
VOID
NTAPI
SerCx2LibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO SerCx2LibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {2, 0, 0},
    NULL,
    NULL,
    SerCx2LibraryBindClient,
    SerCx2LibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject,
                                RegistryPath,
                                L"\\Device\\SerCx2",
                                &SerCx2LibraryInfo);
}
