/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bridge between the SD port engine and Windows-contract miniports
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#pragma once

#include <ntddk.h>

typedef struct _SDPORT_WIN_LEGACY_TABLE
{
    NTSTATUS (*GetSlotCount)(PVOID, PUCHAR);
    NTSTATUS (*Initialize)(PVOID, PHYSICAL_ADDRESS, PVOID, ULONG, BOOLEAN);
    BOOLEAN (*GetCardDetectState)(PVOID);
    BOOLEAN (*Interrupt)(PVOID, PULONG, PULONG, PBOOLEAN, PBOOLEAN, PBOOLEAN);
    NTSTATUS (*SaveContext)(PVOID);
    NTSTATUS (*RestoreContext)(PVOID);
    VOID (*ToggleEvents)(PVOID, ULONG, BOOLEAN);
    VOID (*ClearEvents)(PVOID, ULONG);
    VOID (*Cleanup)(PVOID);
} SDPORT_WIN_LEGACY_TABLE, *PSDPORT_WIN_LEGACY_TABLE;

typedef struct _SDPORT_WIN_CAPABILITIES
{
    ULONG MaximumBlockSize;
    ULONG MaximumBlockCount;
    ULONG BaseClockFrequencyKhz;
    BOOLEAN HighSpeed;
    BOOLEAN Sdr50;
    BOOLEAN Sdr104;
    BOOLEAN Ddr50;
    BOOLEAN Hs200;
    BOOLEAN Hs400;
    BOOLEAN ScatterGatherDma;
    BOOLEAN Voltage18;
    BOOLEAN Voltage30;
    BOOLEAN Voltage33;
    BOOLEAN BusWidth8Bit;
} SDPORT_WIN_CAPABILITIES, *PSDPORT_WIN_CAPABILITIES;

typedef enum _SDPORT_WIN_BUS_OPERATION
{
    SdPortWinResetHost,
    SdPortWinSetClock,
    SdPortWinSetVoltage,
    SdPortWinSetBusWidth,
    SdPortWinSetBusSpeed,
    SdPortWinSetSignalingVoltage,
    SdPortWinExecuteTuning
} SDPORT_WIN_BUS_OPERATION;

typedef struct _SDPORT_WIN_REQUEST
{
    UCHAR Cmd;
    BOOLEAN AppCmd;
    UCHAR ResponseType;
    UCHAR TransferType;
    UCHAR TransferDirection;
    ULONG Argument;
    ULONG BlockSize;
    ULONG BlockCount;
    PVOID DataBuffer;
    PMDL DataMdl;
    PULONG Response;
    PULONG BytesTransferred;
} SDPORT_WIN_REQUEST, *PSDPORT_WIN_REQUEST;

NTSTATUS
SdPortInitializeWindowsMiniport(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_ const SDPORT_WIN_LEGACY_TABLE *Table);

BOOLEAN
SdPortWinIsRegistered(VOID);

NTSTATUS
SdPortWinAllocateExtension(
    _In_ PDEVICE_OBJECT Fdo,
    _In_ PDEVICE_OBJECT Pdo,
    _Outptr_ PVOID *Extension);

VOID
SdPortWinFreeExtension(
    _In_ PVOID Extension);

VOID
SdPortWinQueryCapabilities(
    _In_ PVOID Extension,
    _Out_ PSDPORT_WIN_CAPABILITIES Capabilities);

NTSTATUS
SdPortWinIssueBusOperation(
    _In_ PVOID Extension,
    _In_ SDPORT_WIN_BUS_OPERATION Operation,
    _In_ ULONG Parameter);

NTSTATUS
SdPortWinIssueRequest(
    _In_ PVOID Extension,
    _In_ PSDPORT_WIN_REQUEST Request);

VOID
SdPortWinRequestDpc(
    _In_ PVOID Extension,
    _In_ ULONG Events,
    _In_ ULONG Errors);

VOID
SdPortWinRequestCompleted(
    _In_ PDEVICE_OBJECT Fdo,
    _In_ NTSTATUS Status,
    _In_ ULONG Errors);
