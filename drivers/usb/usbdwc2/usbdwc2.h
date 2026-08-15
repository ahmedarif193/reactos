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
    ULONG InitialPacketCount;
    ULONG DataToggle;
    ULONG NakCount;
    ULONG RetrySof;
    ULONGLONG StageStartTime;
    UCHAR Channel;
    BOOLEAN DirectionIn;
    BOOLEAN Done;
    BOOLEAN NeedsSof;
    BOOLEAN CompleteSplit;
    BOOLEAN HangDiagnosticLogged;
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
    UCHAR Channel;
    BOOLEAN Listed;
    BOOLEAN RequiresSplit;
} DWC2_ENDPOINT, *PDWC2_ENDPOINT;

typedef struct _DWC2_EXTENSION
{
    ULONG Reserved;
    PUCHAR Registers;
    ULONG RegisterLength;
    ULONG NumberOfChannels;
    ULONG InterruptMask;
    volatile LONG PendingGlobalInterrupts;
    volatile LONG PendingChannelInterrupts;
    ULONG SofCount;
    ULONG ResetChange;
    ULONG SuspendChange;
    ULONG LastConnectStatus;
    LIST_ENTRY EndpointList;
    PDWC2_ENDPOINT Channels[DWC2_MAX_CHANNELS];
    BOOLEAN Started;
    BOOLEAN Suspended;
    BOOLEAN RootHubIrqEnabled;
} DWC2_EXTENSION, *PDWC2_EXTENSION;

extern USBPORT_REGISTRATION_PACKET Dwc2RegPacket;

#endif /* USBDWC2_H__ */
