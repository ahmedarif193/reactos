/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared definitions and device extension
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntintsafe.h>
#include <ntstrsafe.h>
#include <acpiioct.h>
#include <wdmguid.h>
#include <reactos/drivers/acpi/acpiproc_ioctl.h>
#include <reactos/hal/acpi_cstate.h>

#define ACPIPROC_TAG 'cPcA'
#define ACPIPROC_MAX_EVAL_RETRIES   4

#define ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE 0x7F

#define ACPIPROC_PDC_REVISION_ID          0x00000001
#define ACPIPROC_PDC_P_FFH                0x00000001
#define ACPIPROC_PDC_C_C1_HALT            0x00000002
#define ACPIPROC_PDC_T_FFH                0x00000004
#define ACPIPROC_PDC_SMP_C1PT             0x00000008
#define ACPIPROC_PDC_SMP_C2C3             0x00000010
#define ACPIPROC_PDC_SMP_P_SWCOORD        0x00000020
#define ACPIPROC_PDC_SMP_C_SWCOORD        0x00000040
#define ACPIPROC_PDC_SMP_T_SWCOORD        0x00000080
#define ACPIPROC_PDC_C_C1_FFH             0x00000100
#define ACPIPROC_PDC_C_C2C3_FFH           0x00000200
#define ACPIPROC_PDC_SMP_P_HWCOORD        0x00000800

#define ACPIPROC_PSD_FIELD_COUNT    5
#define ACPIPROC_CSD_FIELD_COUNT    5

typedef struct _ACPIPROC_UID {
    BOOLEAN Valid;
    BOOLEAN IsString;
    union {
        ULONG Integer;
        UNICODE_STRING String;
    } u;
} ACPIPROC_UID, *PACPIPROC_UID;

typedef struct _ACPIPROC_REGISTER_BLOCK {
    UCHAR AddressSpaceId;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR AccessSize;
    ULONGLONG Address;
} ACPIPROC_REGISTER_BLOCK, *PACPIPROC_REGISTER_BLOCK;

typedef enum _ACPIPROC_REGISTER_KIND {
    AcpiprocRegisterKindControl,
    AcpiprocRegisterKindStatus
} ACPIPROC_REGISTER_KIND;

typedef struct _ACPIPROC_PSS_ENTRY {
    ULONG Frequency;
    ULONG Power;
    ULONG TransitionLatency;
    ULONG BusMasterLatency;
    ULONG Control;
    ULONG Status;
} ACPIPROC_PSS_ENTRY, *PACPIPROC_PSS_ENTRY;

typedef struct _ACPIPROC_PSD_INFO {
    BOOLEAN Valid;
    ULONG Revision;
    ULONG NumEntries;
    ULONG Domain;
    ULONG CoordType;
    ULONG NumProcessors;
} ACPIPROC_PSD_INFO, *PACPIPROC_PSD_INFO;

typedef struct _ACPIPROC_CSTATE_ENTRY {
    ULONG Type;
    ULONG Latency;
    ULONG Power;
    ACPIPROC_REGISTER_BLOCK Register;
    BOOLEAN RequiresCacheFlush;
} ACPIPROC_CSTATE_ENTRY, *PACPIPROC_CSTATE_ENTRY;

typedef struct _ACPIPROC_CSD_INFO {
    BOOLEAN Valid;
    ULONG Revision;
    ULONG NumEntries;
    ULONG Domain;
    ULONG CoordType;
    ULONG NumProcessors;
} ACPIPROC_CSD_INFO, *PACPIPROC_CSD_INFO;

#define ACPIPROC_COORD_TYPE_SW_ANY 0
#define ACPIPROC_COORD_TYPE_SW_ALL 1
#define ACPIPROC_COORD_TYPE_HW_ALL 2

typedef struct _ACPIPROC_IDLE_HANDLER_CONTEXT {
    struct _ACPIPROC_DEVICE *DeviceExtension;
    PACPIPROC_CSTATE_ENTRY StateEntry;
} ACPIPROC_IDLE_HANDLER_CONTEXT, *PACPIPROC_IDLE_HANDLER_CONTEXT;

typedef struct _ACPIPROC_IDLE_DATA {
    PACPIPROC_CSTATE_ENTRY States;
    ULONG StateCount;
    BOOLEAN CstPresent;
    ACPIPROC_CSD_INFO Csd;
    PACPIPROC_IDLE_HANDLER_CONTEXT HandlerContexts;
    ULONG HandlerCount;
    BOOLEAN HandlersRegistered;
} ACPIPROC_IDLE_DATA, *PACPIPROC_IDLE_DATA;

typedef struct _ACPIPROC_PERF_DATA {
    ACPIPROC_REGISTER_BLOCK ControlRegister;
    ACPIPROC_REGISTER_BLOCK StatusRegister;
    BOOLEAN ControlRegisterValid;
    BOOLEAN StatusRegisterValid;
    BOOLEAN PpcValid;
    ULONG PpcLimit;
    PACPIPROC_PSS_ENTRY States;
    ULONG StateCount;
    ACPIPROC_PSD_INFO Psd;
    BOOLEAN CurrentStateValid;
    ULONG CurrentStateIndex;
    volatile LONG PpcWorkItemQueued;
} ACPIPROC_PERF_DATA, *PACPIPROC_PERF_DATA;

typedef struct _ACPIPROC_TSS_ENTRY {
    ULONG Power;
    ULONG Performance;
    ULONG TransitionLatency;
    ULONG Control;
    ULONG Status;
} ACPIPROC_TSS_ENTRY, *PACPIPROC_TSS_ENTRY;

typedef enum _ACPIPROC_THERMAL_EVENT {
    AcpiprocThermalEventHot,
    AcpiprocThermalEventCritical
} ACPIPROC_THERMAL_EVENT, *PACPIPROC_THERMAL_EVENT;

typedef struct _ACPIPROC_THERMAL_DATA {
    BOOLEAN HotTripValid;
    BOOLEAN CriticalTripValid;
    ULONG HotTripPoint;
    ULONG CriticalTripPoint;
    BOOLEAN HotEventPending;
    BOOLEAN CriticalEventPending;
    volatile LONG WorkItemQueued;
} ACPIPROC_THERMAL_DATA, *PACPIPROC_THERMAL_DATA;

typedef struct _ACPIPROC_THROTTLE_DATA {
    ACPIPROC_REGISTER_BLOCK ControlRegister;
    ACPIPROC_REGISTER_BLOCK StatusRegister;
    BOOLEAN ControlRegisterValid;
    BOOLEAN StatusRegisterValid;
    PACPIPROC_TSS_ENTRY States;
    ULONG StateCount;
    BOOLEAN CurrentStateValid;
    ULONG CurrentStateIndex;
    BOOLEAN TpcValid;
    ULONG TpcLimit;
    volatile LONG TpcWorkItemQueued;
} ACPIPROC_THROTTLE_DATA, *PACPIPROC_THROTTLE_DATA;

typedef struct _ACPIPROC_DEVICE {
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    IO_REMOVE_LOCK RemoveLock;
    LIST_ENTRY ListEntry;
    BOOLEAN InGlobalList;
    BOOLEAN Started;
    ULONG ProcessorIndex;
    ULONG DeviceStatus; /* _STA */
    ACPIPROC_UID Uid;
    ACPIPROC_PERF_DATA Perf;
    ACPIPROC_IDLE_DATA Idle;
    ACPIPROC_THROTTLE_DATA Throttle;
    ACPIPROC_THERMAL_DATA Thermal;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    BOOLEAN InterfaceAcquired;
    BOOLEAN NotificationsRegistered;
} ACPIPROC_DEVICE, *PACPIPROC_DEVICE;

extern LONG AcpiprocNextProcessorIndex;
extern LIST_ENTRY AcpiprocDeviceList;
extern FAST_MUTEX AcpiprocDeviceListLock;

DRIVER_ADD_DEVICE AcpiprocAddDevice;
DRIVER_DISPATCH AcpiprocDispatchDefault;
DRIVER_DISPATCH AcpiprocDispatchPnP;
DRIVER_DISPATCH AcpiprocDispatchPower;
DRIVER_DISPATCH AcpiprocDispatchDeviceControl;

NTSTATUS
AcpiprocStartDevice(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocStopDevice(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

NTSTATUS
AcpiprocForwardIrpAndWait(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _Inout_ PIRP Irp);

NTSTATUS
AcpiprocSendAcpiIrp(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength);

NTSTATUS
AcpiprocEvaluateIntegerMethod(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_reads_(4) PCSTR MethodName,
    _Out_ PULONG Value);

NTSTATUS
AcpiprocQueryUid(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocCleanupUid(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

NTSTATUS
AcpiprocInitializePerfStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocCleanupPerfStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

NTSTATUS
AcpiprocInitializeIdleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocCleanupIdleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

NTSTATUS
AcpiprocInitializeThrottleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocCleanupThrottleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

NTSTATUS
AcpiprocRefreshTpcLimit(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _Out_opt_ PBOOLEAN LimitChanged);

NTSTATUS
AcpiprocSetThrottleIndex(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ULONG StateIndex);

NTSTATUS
AcpiprocSetPerfStateIndex(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ULONG StateIndex);

VOID
AcpiprocHandlePpcNotification(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocHandleTpcNotification(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

NTSTATUS
AcpiprocInitializeThermalInfo(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocCleanupThermalInfo(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

VOID
AcpiprocHandleThermalNotification(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ACPIPROC_THERMAL_EVENT EventType);

NTSTATUS
AcpiprocExecuteMethod(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_reads_(4) PCSTR MethodName,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength);

NTSTATUS
AcpiprocCopyPackageIntegers(
    _In_ PACPI_METHOD_ARGUMENT PackageArgument,
    _Out_writes_(Count) PULONG Values,
    _In_ ULONG Count);

NTSTATUS
AcpiprocParseGenericRegisterDescriptor(
    _In_reads_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PACPIPROC_REGISTER_BLOCK RegisterBlock);

NTSTATUS
AcpiprocReadRegister(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _Out_ PULONGLONG Value);

NTSTATUS
AcpiprocWriteRegister(
    _In_ PACPIPROC_REGISTER_BLOCK RegisterBlock,
    _In_ ACPIPROC_REGISTER_KIND Kind,
    _In_ ULONGLONG Value);
