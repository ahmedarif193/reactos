/*
 * PROJECT:     ReactOS ACPI Processor Module Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ACPI0007 topology and processor power capability discovery
 */

#pragma once

#include <ntddk.h>
#include <ntpoapi.h>
#include <poclass.h>
#include <wdmguid.h>
#include <acpiioct.h>

NTHALAPI VOID NTAPI HalProcessorIdle(VOID);

#define PNPCPU_TAG 'uCpP'
#define PNPCPU_MAX_ACPI_OUTPUT (64 * 1024)
#define PNPCPU_MAX_IDLE_STATES MAX_IDLE_HANDLERS
#define PNPCPU_MAX_PERF_STATES 16

#define PNPCPU_SPACE_SYSTEM_MEMORY 0
#define PNPCPU_SPACE_SYSTEM_IO 1
#define PNPCPU_SPACE_FIXED_HARDWARE 0x7F

#define PNPCPU_PERF_NONE 0
#define PNPCPU_PERF_PSS 1
#define PNPCPU_PERF_CPPC 2
#define PNPCPU_PERF_HWP 3

#define PNPCPU_CPPC_RESTORE_DESIRED 0x00000001
#define PNPCPU_CPPC_RESTORE_MINIMUM 0x00000002
#define PNPCPU_CPPC_RESTORE_MAXIMUM 0x00000004
#define PNPCPU_CPPC_RESTORE_ENABLE 0x00000008
#define PNPCPU_CPPC_RESTORE_AUTONOMOUS 0x00000010

#define PNPCPU_CAP_CPC 0x00000001
#define PNPCPU_CAP_CST 0x00000002
#define PNPCPU_CAP_PSS 0x00000004
#define PNPCPU_CAP_PCT 0x00000008
#define PNPCPU_CAP_PSD 0x00000010
#define PNPCPU_CAP_PPC 0x00000020
#define PNPCPU_CAP_TSS 0x00000040
#define PNPCPU_CAP_TSD 0x00000080

#define PNPCPU_METHOD(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

typedef struct _PNPCPU_REGISTER
{
    BOOLEAN Valid;
    UCHAR SpaceId;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR AccessSize;
    ULONGLONG Address;
    PVOID MappingBase;
    PVOID MappedAddress;
    SIZE_T MappingLength;
} PNPCPU_REGISTER, *PPNPCPU_REGISTER;

typedef struct _PNPCPU_IDLE_STATE
{
    PNPCPU_REGISTER Register;
    ULONG Type;
    ULONG Latency;
    ULONG Power;
} PNPCPU_IDLE_STATE, *PPNPCPU_IDLE_STATE;

typedef struct _PNPCPU_PERF_STATE
{
    ULONG Frequency;
    ULONG Power;
    ULONG TransitionLatency;
    ULONG BusMasterLatency;
    ULONG Control;
    ULONG Status;
    UCHAR Percentage;
} PNPCPU_PERF_STATE, *PPNPCPU_PERF_STATE;

typedef struct _PNPCPU_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    KMUTEX ConfigurationLock;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    BOOLEAN InterfaceAcquired;
    BOOLEAN NotificationsRegistered;
    BOOLEAN Started;
    BOOLEAN Removing;
    volatile LONG WorkCount;
    KEVENT WorkIdleEvent;
    BOOLEAN UidValid;
    ULONG Uid;
    BOOLEAN ApicIdValid;
    ULONG ApicId;
    BOOLEAN ProximityValid;
    ULONG ProximityDomain;
    BOOLEAN ProcessorNumberValid;
    ULONG ProcessorNumber;
    ULONG CapabilityMask;
    ULONG CapabilityCounts[8];
    BOOLEAN PowerRegistered;
    BOOLEAN MonitorMwaitSupported;
    BOOLEAN IntelEstSupported;
    BOOLEAN IntelHwpSupported;
    BOOLEAN IntelHwpEppSupported;
    BOOLEAN HwpRequestCaptured;
    PVOID EnergyPreferenceHandle;
    ULONG MwaitSubstates;
    UCHAR HwpHighest;
    UCHAR HwpLowest;
    ULONGLONG HwpOriginalRequest;
    volatile LONG IdleMonitor;
    ULONG IdleStateCount;
    PNPCPU_IDLE_STATE IdleStates[PNPCPU_MAX_IDLE_STATES];
    ULONG PerfMode;
    ULONG PerfStateCount;
    volatile LONG ThermalLimit;
    PNPCPU_PERF_STATE PerfStates[PNPCPU_MAX_PERF_STATES];
    PNPCPU_REGISTER PerfControl;
    PNPCPU_REGISTER PerfStatus;
    PNPCPU_REGISTER CppcDesired;
    PNPCPU_REGISTER CppcMinimum;
    PNPCPU_REGISTER CppcMaximum;
    PNPCPU_REGISTER CppcEnable;
    PNPCPU_REGISTER CppcAutonomous;
    ULONG CppcHighest;
    ULONG CppcNominal;
    ULONG CppcLowestNonlinear;
    ULONG CppcLowest;
    ULONG CppcRestoreMask;
    ULONGLONG CppcOriginalDesired;
    ULONGLONG CppcOriginalMinimum;
    ULONGLONG CppcOriginalMaximum;
    ULONGLONG CppcOriginalEnable;
    ULONGLONG CppcOriginalAutonomous;
} PNPCPU_DEVICE_EXTENSION, *PPNPCPU_DEVICE_EXTENSION;
