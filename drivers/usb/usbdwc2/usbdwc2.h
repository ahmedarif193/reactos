/*
 * PROJECT:     ReactOS Synopsys DWC2 USB Miniport Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DWC2 miniport declarations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef USBDWC2_H__
#define USBDWC2_H__

#include <ntddk.h>
#include <windef.h>
#include <hubbusif.h>
#include <usbbusif.h>
#include <usbdlib.h>
#include <drivers/usbport/usbmport.h>
#include "hardware.h"

#define DWC2_MAX_CHANNELS                      16
#define DWC2_INVALID_CHANNEL                   0xFF
#define DWC2_SETUP_BUFFER_SIZE                 64
#define DWC2_MAX_TRANSFER_SIZE                 0x10000
#define DWC2_ENDPOINT_BUFFER_SIZE              (DWC2_SETUP_BUFFER_SIZE + DWC2_MAX_TRANSFER_SIZE)
#define DWC2_HANG_DIAGNOSTIC_INTERVAL          2500000ULL
#define DWC2_WEDGE_RECOVERY_INTERVAL           20000000ULL
#define DWC2_WEDGE_RECOVERY_LIMIT              3
#define DWC2_TRANSACTION_ERROR_LIMIT           3
#define DWC2_DPC_DRAIN_LIMIT                   64
#define DWC2_HOST_RX_FIFO_SIZE                 (516 + DWC2_MAX_CHANNELS)
#define DWC2_HOST_NPERIODIC_TX_FIFO_SIZE       0x100
#define DWC2_HOST_PERIODIC_TX_FIFO_SIZE         0x200
#define DWC2_MICROFRAME_100NS                   1250ULL
#define DWC2_FRAME_100NS                        10000ULL

typedef enum _DWC2_TRANSFER_STAGE
{
    Dwc2StageSetup,
    Dwc2StageData,
    Dwc2StageStatus,
    Dwc2StageComplete
} DWC2_TRANSFER_STAGE;

struct _DWC2_ENDPOINT;

typedef struct _DWC2_TRANSFER
{
    ULONG Reserved;
    PUSBPORT_TRANSFER_PARAMETERS Parameters;
    PUSBPORT_SCATTER_GATHER_LIST SgList;
    struct _DWC2_ENDPOINT *Endpoint;
    DWC2_TRANSFER_STAGE Stage;
    USBD_STATUS UsbdStatus;
    ULONG BytesTransferred;
    ULONG ProgrammedLength;
    ULONG ProgrammedDmaAddress;
    ULONG InitialPacketCount;
    ULONG DataToggle;
    ULONG NakCount;
    ULONG ChannelWaitCount;
    ULONG PendingChannelStatus;
    ULONG ErrorCount;
    ULONG RecoveryCount;
    ULONG HangDiagnosticCount;
    ULONG StageStartFrame;
    ULONGLONG SubmitTime;
    ULONGLONG StageStartTime;
    ULONGLONG CompletionTime;
    ULONGLONG RetryTime;
    UCHAR Channel;
    BOOLEAN DirectionIn;
    BOOLEAN Done;
    BOOLEAN NeedsRetry;
    BOOLEAN CompleteSplit;
    BOOLEAN QueueDiagnosticLogged;
    BOOLEAN CompletionDiagnosticLogged;
    BOOLEAN RetryDiagnosticLogged;
} DWC2_TRANSFER, *PDWC2_TRANSFER;

typedef struct _DWC2_ENDPOINT
{
    ULONG Reserved;
    USBPORT_ENDPOINT_PROPERTIES Properties;
    LIST_ENTRY Link;
    PDWC2_TRANSFER Transfer;
    PVOID BufferVA;
    ULONG BufferPA;
    ULONG BufferLength;
    ULONG State;
    ULONG Status;
    ULONG DataToggle;
    ULONG SubmitCount;
    ULONG StateChangeCount;
    ULONGLONG OpenTime;
    UCHAR Channel;
    BOOLEAN Listed;
    BOOLEAN RequiresSplit;
    BOOLEAN IdleDiagnosticLogged;
} DWC2_ENDPOINT, *PDWC2_ENDPOINT;

typedef struct _DWC2_EXTENSION
{
    ULONG Reserved;
    PUCHAR Registers;
    ULONG RegisterLength;
    ULONG NumberOfChannels;
    ULONG InterruptMask;
    KSPIN_LOCK Lock;
    volatile LONG PendingGlobalInterrupts;
    volatile LONG PendingChannelInterrupts;
    volatile LONG ImmediateInterruptCount;
    volatile LONG DmaProgressFallbackCount;
    volatile LONG GlobalEnableRaceCount;
    volatile LONG RetryTimerArmCount;
    volatile LONG RetryTimerWakeCount;
    volatile LONG RetryTimerScheduled;
    volatile LONG RetryTimerDpcActive;
    volatile LONG SofRequestCount;
    volatile LONG SofInterruptCount;
    volatile LONG CompletionCount;
    volatile LONG TransferErrorCount;
    volatile LONG DeferredHaltCount;
    volatile LONG WedgeRecoveryCount;
    volatile LONG HeartbeatCount;
    volatile LONG FifoCorruptionCount;
    volatile LONG PortStatusQueryCount;
    volatile LONG Stopping;
    KTIMER RetryTimer;
    KDPC RetryTimerDpc;
    KEVENT RetryTimerDpcEvent;
    KSPIN_LOCK RetryTimerLock;
    ULONGLONG RetryTimerDueTime;
    ULONG ResetChange;
    ULONG SuspendChange;
    ULONG LastConnectStatus;
    ULONG LastFrameNumber;
    ULONG FrameHighBits;
    ULONG LastPortStatusRaw;
    ULONG LastPortStatusPacked;
    LIST_ENTRY EndpointList;
    PDWC2_ENDPOINT Channels[DWC2_MAX_CHANNELS];
    BOOLEAN Started;
    BOOLEAN Suspended;
    BOOLEAN RootHubIrqEnabled;
    BOOLEAN SofRequested;
} DWC2_EXTENSION, *PDWC2_EXTENSION;

extern USBPORT_REGISTRATION_PACKET Dwc2RegPacket;

#endif /* USBDWC2_H__ */
