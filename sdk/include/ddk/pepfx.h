/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Power engine plugin interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _PEPFX_H_
#define _PEPFX_H_

#ifdef __cplusplus
extern "C" {
#endif

#if ((NTDDI_VERSION >= NTDDI_WIN8))

DECLARE_HANDLE(PEPHANDLE);

typedef
VOID
POFXCALLBACKREQUESTWORKER (
    _In_ POHANDLE PluginHandle
    );

typedef POFXCALLBACKREQUESTWORKER *PPOFXCALLBACKREQUESTWORKER;

_IRQL_requires_max_(HIGH_LEVEL)
typedef
VOID
POFXCALLBACKCRITICALRESOURCE (
    _In_ POHANDLE DeviceHandle,
    _In_ ULONG Component,
    _In_ BOOLEAN Active
    );

typedef POFXCALLBACKCRITICALRESOURCE *PPOFXCALLBACKCRITICALRESOURCE;

_IRQL_requires_max_(HIGH_LEVEL)
typedef
NTSTATUS
POFXCALLBACKPROCESSORHALT (
    _In_ ULONG Flags,
    _Inout_opt_ PVOID Context,
    _In_ PPROCESSOR_HALT_ROUTINE Halt
    );

typedef POFXCALLBACKPROCESSORHALT *PPOFXCALLBACKPROCESSORHALT;

_IRQL_requires_max_(HIGH_LEVEL)
typedef
NTSTATUS
POFXCALLBACKREQUESTINTERRUPT (
    _In_ ULONG Gsiv
    );

typedef POFXCALLBACKREQUESTINTERRUPT *PPOFXCALLBACKREQUESTINTERRUPT;

typedef union _PEP_UNMASKED_INTERRUPT_FLAGS {
    struct {
        USHORT SecondaryInterrupt: 1;
        USHORT Reserved: 15;
    };
    USHORT AsUSHORT;
} PEP_UNMASKED_INTERRUPT_FLAGS, *PPEP_UNMASKED_INTERRUPT_FLAGS;

typedef struct _PEP_UNMASKED_INTERRUPT_INFORMATION {
    USHORT Version;
    USHORT Size;
    PEP_UNMASKED_INTERRUPT_FLAGS Flags;
    KINTERRUPT_MODE Mode;
    KINTERRUPT_POLARITY Polarity;
    ULONG Gsiv;
    USHORT PinNumber;
    PEPHANDLE DeviceHandle;
} PEP_UNMASKED_INTERRUPT_INFORMATION, *PPEP_UNMASKED_INTERRUPT_INFORMATION;

_IRQL_requires_max_(HIGH_LEVEL)
typedef
BOOLEAN
(*PPO_ENUMERATE_INTERRUPT_SOURCE_CALLBACK)(
    _In_ PVOID CallbackContext,
    _In_ PPEP_UNMASKED_INTERRUPT_INFORMATION InterruptInformation
    );

_IRQL_requires_max_(HIGH_LEVEL)
typedef
NTSTATUS
POFXCALLBACKENUMERATEUNMASKEDINTERRUPTS (
    _In_opt_ POHANDLE PluginHandle,
    _In_opt_ ULONG EnumerateFlags,
    _In_ PPO_ENUMERATE_INTERRUPT_SOURCE_CALLBACK Callback,
    _In_ PVOID CallbackContext,
    _Inout_ PPEP_UNMASKED_INTERRUPT_INFORMATION InterruptInformation
    );

typedef POFXCALLBACKENUMERATEUNMASKEDINTERRUPTS *PPOFXCALLBACKENUMERATEUNMASKEDINTERRUPTS;

typedef
NTSTATUS
POFXCALLBACKPROCESSORIDLEVETO (
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG ProcessorState,
    _In_ ULONG VetoReason,
    _In_ BOOLEAN Increment
    );

typedef POFXCALLBACKPROCESSORIDLEVETO *PPOFXCALLBACKPROCESSORIDLEVETO;

typedef
NTSTATUS
POFXCALLBACKPLATFORMIDLEVETO (
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG PlatformState,
    _In_ ULONG VetoReason,
    _In_ BOOLEAN Increment
    );

typedef POFXCALLBACKPLATFORMIDLEVETO *PPOFXCALLBACKPLATFORMIDLEVETO;

typedef struct _PEP_PROCESSOR_IDLE_STATE_UPDATE {
    ULONG Version;
    ULONG Latency;
    ULONG BreakEvenDuration;
} PEP_PROCESSOR_IDLE_STATE_UPDATE, *PPEP_PROCESSOR_IDLE_STATE_UPDATE;

typedef
NTSTATUS
POFXCALLBACKUPDATEPROCESSORIDLESTATE (
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG ProcessorState,
    _In_ PPEP_PROCESSOR_IDLE_STATE_UPDATE Update
    );

typedef POFXCALLBACKUPDATEPROCESSORIDLESTATE *PPOFXCALLBACKUPDATEPROCESSORIDLESTATE;

typedef struct _PEP_PLATFORM_IDLE_STATE_UPDATE {
    ULONG Version;
    ULONG Latency;
    ULONG BreakEvenDuration;
} PEP_PLATFORM_IDLE_STATE_UPDATE, *PPEP_PLATFORM_IDLE_STATE_UPDATE;

typedef
NTSTATUS
POFXCALLBACKUPDATEPLATFORMIDLESTATE (
    _In_ POHANDLE ProcessorHandle,
    _In_ ULONG PlatformState,
    _In_ PPEP_PLATFORM_IDLE_STATE_UPDATE Update
    );

typedef POFXCALLBACKUPDATEPLATFORMIDLESTATE *PPOFXCALLBACKUPDATEPLATFORMIDLESTATE;

_IRQL_requires_max_(HIGH_LEVEL)
typedef
NTSTATUS
POFXCALLBACKREQUESTCOMMON (
    _In_ ULONG RequestId,
    _Inout_opt_ PVOID Data
    );

typedef POFXCALLBACKREQUESTCOMMON *PPOFXCALLBACKREQUESTCOMMON;

#if ((NTDDI_VERSION >= NTDDI_WINTHRESHOLD))

#define PEP_KERNEL_INFORMATION_V3 0x00000003

#if (!defined PEP_KERNEL_INFORMATION_VERSION)

#define PEP_KERNEL_INFORMATION_VERSION PEP_KERNEL_INFORMATION_V3

#endif

typedef struct _PEP_KERNEL_INFORMATION_STRUCT_V3 {
    USHORT Version;
    USHORT Size;
    POHANDLE Plugin;
    PPOFXCALLBACKREQUESTWORKER RequestWorker;
    PPOFXCALLBACKENUMERATEUNMASKEDINTERRUPTS EnumerateUnmaskedInterrupts;
    PPOFXCALLBACKPROCESSORHALT ProcessorHalt;
    PPOFXCALLBACKREQUESTINTERRUPT RequestInterrupt;
    PPOFXCALLBACKCRITICALRESOURCE TransitionCriticalResource;
    PPOFXCALLBACKPROCESSORIDLEVETO ProcessorIdleVeto;
    PPOFXCALLBACKPLATFORMIDLEVETO PlatformIdleVeto;
    PPOFXCALLBACKUPDATEPROCESSORIDLESTATE UpdateProcessorIdleState;
    PPOFXCALLBACKUPDATEPLATFORMIDLESTATE UpdatePlatformIdleState;
    PPOFXCALLBACKREQUESTCOMMON RequestCommon;
} PEP_KERNEL_INFORMATION_STRUCT_V3, *PPEP_KERNEL_INFORMATION_STRUCT_V3;

#endif
#if (((NTDDI_VERSION >= NTDDI_WINTHRESHOLD) && (PEP_KERNEL_INFORMATION_VERSION == PEP_KERNEL_INFORMATION_V3)))

typedef PEP_KERNEL_INFORMATION_STRUCT_V3 PEP_KERNEL_INFORMATION, *PPEP_KERNEL_INFORMATION;

#endif

typedef enum _PEP_WORK_TYPE {
    PepWorkRequestPowerControl = 3,
    PepWorkCompleteIdleState = 5,
    PepWorkCompletePerfState,
    PepWorkAcpiNotify,
    PepWorkAcpiEvaluateControlMethodComplete,
    PepWorkMax
} PEP_WORK_TYPE, *PPEP_WORK_TYPE;

typedef struct _PEP_WORK_POWER_CONTROL {
    POHANDLE DeviceHandle;
    LPCGUID PowerControlCode;
    PVOID RequestContext;
    PVOID InBuffer;
    SIZE_T InBufferSize;
    PVOID OutBuffer;
    SIZE_T OutBufferSize;
} PEP_WORK_POWER_CONTROL, *PPEP_WORK_POWER_CONTROL;

typedef struct _PEP_WORK_COMPLETE_IDLE_STATE {
    POHANDLE DeviceHandle;
    ULONG Component;
} PEP_WORK_COMPLETE_IDLE_STATE, *PPEP_WORK_COMPLETE_IDLE_STATE;

typedef struct _PEP_WORK_COMPLETE_PERF_STATE {
    POHANDLE DeviceHandle;
    ULONG Component;
    BOOLEAN Succeeded;
} PEP_WORK_COMPLETE_PERF_STATE, *PPEP_WORK_COMPLETE_PERF_STATE;

typedef struct _PEP_WORK_ACPI_NOTIFY {
    POHANDLE DeviceHandle;
    ULONG NotifyCode;
} PEP_WORK_ACPI_NOTIFY, *PPEP_WORK_ACPI_NOTIFY;

typedef struct _PEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE {
    POHANDLE DeviceHandle;
    ULONG CompletionFlags;
    NTSTATUS MethodStatus;
    PVOID CompletionContext;
    SIZE_T OutputArgumentSize;
    PACPI_METHOD_ARGUMENT OutputArguments;
} PEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE,
    *PPEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE;

typedef struct _PEP_WORK_INFORMATION {
    PEP_WORK_TYPE WorkType;
    union {
        PEP_WORK_POWER_CONTROL PowerControl;
        PEP_WORK_COMPLETE_IDLE_STATE CompleteIdleState;
        PEP_WORK_COMPLETE_PERF_STATE CompletePerfState;
        PEP_WORK_ACPI_NOTIFY AcpiNotify;
        PEP_WORK_ACPI_EVALUATE_CONTROL_METHOD_COMPLETE ControlMethodComplete;
    };
} PEP_WORK_INFORMATION, *PPEP_WORK_INFORMATION;

typedef
_Function_class_(PEPCALLBACKNOTIFYDPM)
_IRQL_requires_max_(DISPATCH_LEVEL)
BOOLEAN
PEPCALLBACKNOTIFYDPM (
    _In_ ULONG Notification,
    _In_ PVOID Data
    );

typedef PEPCALLBACKNOTIFYDPM *PPEPCALLBACKNOTIFYDPM;

typedef
_Function_class_(PEPCALLBACKNOTIFYPPM)
_IRQL_requires_max_(HIGH_LEVEL)
BOOLEAN
PEPCALLBACKNOTIFYPPM (
    _In_ PEPHANDLE Handle,
    _In_ ULONG Notification,
    _Inout_opt_ PVOID Data
    );

typedef PEPCALLBACKNOTIFYPPM *PPEPCALLBACKNOTIFYPPM;

#if ((NTDDI_VERSION >= NTDDI_WINTHRESHOLD))

typedef
_Function_class_(PEPCALLBACKNOTIFYACPI)
_IRQL_requires_max_(HIGH_LEVEL)
BOOLEAN
PEPCALLBACKNOTIFYACPI (
    _In_ ULONG Notification,
    _Inout_opt_ PVOID Data
    );

typedef PEPCALLBACKNOTIFYACPI *PPEPCALLBACKNOTIFYACPI;

#endif

typedef struct _PEP_WORK {
    PPEP_WORK_INFORMATION WorkInformation;
    BOOLEAN NeedWork;
} PEP_WORK, *PPEP_WORK;

typedef struct _PEP_PROCESSOR_IDLE_STATE_V2 {
    union {
        ULONG Ulong;
        struct {
            ULONG Interruptible          :1;
            ULONG CacheCoherent          :1;
            ULONG ThreadContextRetained  :1;
            ULONG CStateType             :4;
            ULONG WakesSpuriously        :1;
            ULONG PlatformOnly           :1;
            ULONG Autonomous             :1;
            ULONG Reserved               :22;
        };
    };
    ULONG Latency;
    ULONG BreakEvenDuration;
} PEP_PROCESSOR_IDLE_STATE_V2, *PPEP_PROCESSOR_IDLE_STATE_V2;

#if ((NTDDI_VERSION >= NTDDI_WINTHRESHOLD))

#define PEP_INFORMATION_V3 0x00000003

#define PEP_INFORMATION_VERSION PEP_INFORMATION_V3

#endif

typedef struct _PEP_INFORMATION {
    USHORT Version;
    USHORT Size;
    PPEPCALLBACKNOTIFYDPM AcceptDeviceNotification;
    PPEPCALLBACKNOTIFYPPM AcceptProcessorNotification;
#if (NTDDI_VERSION >= NTDDI_WINTHRESHOLD)
    PPEPCALLBACKNOTIFYACPI AcceptAcpiNotification;
#endif
} PEP_INFORMATION, *PPEP_INFORMATION;

#if defined(PEP_KERNEL_INFORMATION_VERSION)

_IRQL_requires_max_(PASSIVE_LEVEL)
NTKERNELAPI
NTSTATUS
PoFxRegisterPlugin (
    _In_ PPEP_INFORMATION PepInformation,
    _Inout_ PPEP_KERNEL_INFORMATION KernelInformation
    );

#endif
#if ((NTDDI_VERSION >= NTDDI_WINTHRESHOLD))

#define PEP_NOTIFY_ACPI_PREPARE_DEVICE                              0x01

#define PEP_NOTIFY_ACPI_ABANDON_DEVICE                              0x02

#define PEP_NOTIFY_ACPI_REGISTER_DEVICE                             0x03

#define PEP_NOTIFY_ACPI_UNREGISTER_DEVICE                           0x04

#define PEP_NOTIFY_ACPI_ENUMERATE_DEVICE_NAMESPACE                  0x05

#define PEP_NOTIFY_ACPI_QUERY_OBJECT_INFORMATION                    0x06

#define PEP_NOTIFY_ACPI_EVALUATE_CONTROL_METHOD                     0x07

#define PEP_NOTIFY_ACPI_QUERY_DEVICE_CONTROL_RESOURCES              0x08

#define PEP_NOTIFY_ACPI_TRANSLATED_DEVICE_CONTROL_RESOURCES         0x09

#define PEP_NOTIFY_ACPI_WORK                                        0x0A

#define PEP_ACPI_PREPARE_DEVICE_OUTPUT_FLAG_NONE  0x0

typedef struct _PEP_ACPI_PREPARE_DEVICE {
    PCUNICODE_STRING AcpiDeviceName;
    ULONG InputFlags;
    BOOLEAN DeviceAccepted;
    ULONG OutputFlags;
} PEP_ACPI_PREPARE_DEVICE, *PPEP_ACPI_PREPARE_DEVICE;

typedef struct _PEP_ACPI_ABANDON_DEVICE {
    PCUNICODE_STRING AcpiDeviceName;
    BOOLEAN DeviceAccepted;
} PEP_ACPI_ABANDON_DEVICE, *PPEP_ACPI_ABANDON_DEVICE;

#define PEP_ACPI_REGISTER_DEVICE_OUTPUT_FLAG_NONE  0x0

typedef struct _PEP_ACPI_REGISTER_DEVICE {
    PCUNICODE_STRING AcpiDeviceName;
    ULONG InputFlags;
    POHANDLE KernelHandle;
    PEPHANDLE DeviceHandle;
    ULONG OutputFlags;
} PEP_ACPI_REGISTER_DEVICE, *PPEP_ACPI_REGISTER_DEVICE;

typedef struct _PEP_ACPI_UNREGISTER_DEVICE {
    PEPHANDLE DeviceHandle;
    ULONG InputFlags;
} PEP_ACPI_UNREGISTER_DEVICE, *PPEP_ACPI_UNREGISTER_DEVICE;

typedef union _PEP_ACPI_OBJECT_NAME {
    UCHAR Name[4];
    ULONG NameAsUlong;
} PEP_ACPI_OBJECT_NAME, *PPEP_ACPI_OBJECT_NAME;

typedef enum _PEP_ACPI_OBJECT_TYPE {
    PepAcpiObjectTypeMethod,
    PepAcpiObjectTypeDevice,
    PepAcpiObjectTypeMaximum
} PEP_ACPI_OBJECT_TYPE, *PPEP_ACPI_OBJECT_TYPE;

typedef struct _PEP_ACPI_OBJECT_NAME_WITH_TYPE {
    PEP_ACPI_OBJECT_NAME Name;
    PEP_ACPI_OBJECT_TYPE Type;
} PEP_ACPI_OBJECT_NAME_WITH_TYPE, *PPEP_ACPI_OBJECT_NAME_WITH_TYPE;

typedef struct _PEP_ACPI_ENUMERATE_DEVICE_NAMESPACE {
    PEPHANDLE DeviceHandle;
    ULONG RequestFlags;
    NTSTATUS Status;
    ULONG ObjectCount;
    SIZE_T ObjectBufferSize;
    _Field_size_bytes_full_(ObjectBufferSize)
        PEP_ACPI_OBJECT_NAME_WITH_TYPE Objects[ANYSIZE_ARRAY];
} PEP_ACPI_ENUMERATE_DEVICE_NAMESPACE, *PPEP_ACPI_ENUMERATE_DEVICE_NAMESPACE;

typedef struct _PEP_ACPI_QUERY_OBJECT_INFORMATION {
    PEPHANDLE DeviceHandle;
    PEP_ACPI_OBJECT_NAME Name;
    PEP_ACPI_OBJECT_TYPE Type;
    ULONG ObjectFlags;
    union {
        struct {
            ULONG InputArgumentCount;
            ULONG OutputArgumentCount;
        } MethodObject;
    } DUMMYUNIONNAME;
} PEP_ACPI_QUERY_OBJECT_INFORMATION, *PPEP_ACPI_QUERY_OBJECT_INFORMATION;

typedef struct _PEP_ACPI_EVALUATE_CONTROL_METHOD {
    PEPHANDLE DeviceHandle;
    ULONG RequestFlags;
    union {
        ULONG MethodName;
        ANSI_STRING MethodNameString;
    };
    NTSTATUS MethodStatus;
    PVOID CompletionContext;
    ULONG InputArgumentCount;
    SIZE_T InputArgumentSize;
    PACPI_METHOD_ARGUMENT InputArguments;
    ULONG OutputArgumentCount;
    SIZE_T OutputArgumentSize;
    PACPI_METHOD_ARGUMENT OutputArguments;
} PEP_ACPI_EVALUATE_CONTROL_METHOD, *PPEP_ACPI_EVALUATE_CONTROL_METHOD;

typedef struct _PEP_ACPI_QUERY_DEVICE_CONTROL_RESOURCES {
    PEPHANDLE DeviceHandle;
    ULONG RequestFlags;
    NTSTATUS Status;
    SIZE_T BiosResourcesSize;
    ACPI_METHOD_ARGUMENT BiosResources[ANYSIZE_ARRAY];
} PEP_ACPI_QUERY_DEVICE_CONTROL_RESOURCES,
    *PPEP_ACPI_QUERY_DEVICE_CONTROL_RESOURCES;

typedef struct _PEP_ACPI_TRANSLATED_DEVICE_CONTROL_RESOURCES {
    PEPHANDLE DeviceHandle;
    ULONG RequestFlags;
    NTSTATUS Status;
    SIZE_T TranslatedResourcesSize;
    PCM_RESOURCE_LIST TranslatedResources;
} PEP_ACPI_TRANSLATED_DEVICE_CONTROL_RESOURCES,
    *PPEP_ACPI_TRANSLATED_DEVICE_CONTROL_RESOURCES;

typedef enum _GPIO_PIN_CONFIG_TYPE {
    PullDefault,
    PullUp,
    PullDown,
    PullNone
} GPIO_PIN_CONFIG_TYPE;

typedef enum _GPIO_PIN_IORESTRICTION_TYPE {
    IoRestrictionNone,
    IoRestrictionInputOnly,
    IoRestrictionOutputOnly,
    IoRestrictionNoneAndPreserve
} GPIO_PIN_IORESTRICTION_TYPE;

typedef enum _PEP_ACPI_RESOURCE_TYPE {
    PepAcpiMemory,
    PepAcpiIoPort,
    PepAcpiInterrupt,
    PepAcpiGpioIo,
    PepAcpiGpioInt,
    PepAcpiSpbI2c,
    PepAcpiSpbSpi,
    PepAcpiSpbUart,
    PepAcpiExtendedMemory,
    PepAcpiExtendedIo
} PEP_ACPI_RESOURCE_TYPE;

typedef union _PEP_ACPI_RESOURCE_FLAGS    {
    ULONG AsULong;
    struct {
        ULONG Shared:1;
        ULONG Wake:1;
        ULONG ResourceUsage:1;
        ULONG SlaveMode:1;
        ULONG AddressingMode:1;
        ULONG SharedMode:1;
        ULONG Reserved:26;
    } DUMMYSTRUCTNAME;
} PEP_ACPI_RESOURCE_FLAGS, *PPEP_ACPI_RESOURCE_FLAGS;

typedef struct _PEP_ACPI_IO_MEMORY_RESOURCE {
    PEP_ACPI_RESOURCE_TYPE Type;
    UCHAR Information;
    PHYSICAL_ADDRESS MinimumAddress;
    PHYSICAL_ADDRESS MaximumAddress;
    ULONG Alignment;
    ULONG Length;
} PEP_ACPI_IO_MEMORY_RESOURCE, *PPEP_ACPI_IO_MEMORY_RESOURCE;

typedef struct _PEP_ACPI_INTERRUPT_RESOURCE {
    PEP_ACPI_RESOURCE_TYPE Type;
    KINTERRUPT_MODE InterruptType;
    KINTERRUPT_POLARITY InterruptPolarity;
    PEP_ACPI_RESOURCE_FLAGS Flags;
    UCHAR Count;
    PULONG Pins;
} PEP_ACPI_INTERRUPT_RESOURCE, *PPEP_ACPI_INTERRUPT_RESOURCE;

typedef struct _PEP_ACPI_GPIO_RESOURCE {
    PEP_ACPI_RESOURCE_TYPE Type;
    PEP_ACPI_RESOURCE_FLAGS Flags;
    KINTERRUPT_MODE InterruptType;
    KINTERRUPT_POLARITY InterruptPolarity;
    GPIO_PIN_CONFIG_TYPE PinConfig;
    GPIO_PIN_IORESTRICTION_TYPE IoRestrictionType;
    USHORT DriveStrength;
    USHORT DebounceTimeout;
    PUSHORT PinTable;
    USHORT PinCount;
    UCHAR ResourceSourceIndex;
    PUNICODE_STRING ResourceSourceName;
    PUCHAR VendorData;
    USHORT VendorDataLength;
} PEP_ACPI_GPIO_RESOURCE, *PPEP_ACPI_GPIO_RESOURCE;

typedef struct _PEP_ACPI_SPB_RESOURCE {
    PEP_ACPI_RESOURCE_TYPE Type;
    PEP_ACPI_RESOURCE_FLAGS Flags;
    USHORT TypeSpecificFlags;
    UCHAR ResourceSourceIndex;
    PUNICODE_STRING ResourceSourceName;
    PCHAR VendorData;
    USHORT VendorDataLength;
} PEP_ACPI_SPB_RESOURCE, *PPEP_ACPI_SPB_RESOURCE;

typedef struct _PEP_ACPI_SPB_I2C_RESOURCE {
    PEP_ACPI_SPB_RESOURCE SpbCommon;
    ULONG ConnectionSpeed;
    USHORT SlaveAddress;
} PEP_ACPI_SPB_I2C_RESOURCE, *PPEP_ACPI_SPB_I2C_RESOURCE;

typedef struct _PEP_ACPI_SPB_SPI_RESOURCE {
    PEP_ACPI_SPB_RESOURCE SpbCommon;
    ULONG ConnectionSpeed;
    UCHAR DataBitLength;
    UCHAR Phase;
    UCHAR Polarity;
    USHORT DeviceSelection;
} PEP_ACPI_SPB_SPI_RESOURCE, *PPEP_ACPI_SPB_SPI_RESOURCE;

typedef struct _PEP_ACPI_SPB_UART_RESOURCE {
    PEP_ACPI_SPB_RESOURCE SpbCommon;
    ULONG BaudRate;
    USHORT RxBufferSize;
    USHORT TxBufferSize;
    UCHAR Parity;
    UCHAR LinesInUse;
} PEP_ACPI_SPB_UART_RESOURCE, *PPEP_ACPI_SPB_UART_RESOURCE;

typedef struct _PEP_ACPI_EXTENDED_ADDRESS {
    PEP_ACPI_RESOURCE_TYPE Type;
    PEP_ACPI_RESOURCE_FLAGS Flags;
    UCHAR ResourceFlags;
    UCHAR GeneralFlags;
    UCHAR TypeSpecificFlags;
    UCHAR RevisionId;
    UCHAR Reserved;
    ULONGLONG Granularity;
    ULONGLONG MinimumAddress;
    ULONGLONG MaximumAddress;
    ULONGLONG TranslationAddress;
    ULONGLONG AddressLength;
    ULONGLONG TypeAttribute;
    PUNICODE_STRING DescriptorName;
} PEP_ACPI_EXTENDED_ADDRESS, *PPEP_ACPI_EXTENDED_ADDRESS;

typedef union _PEP_ACPI_RESOURCE {
    PEP_ACPI_RESOURCE_TYPE Type;
    PEP_ACPI_IO_MEMORY_RESOURCE IoMemory;
    PEP_ACPI_INTERRUPT_RESOURCE Interrupt;
    PEP_ACPI_GPIO_RESOURCE Gpio;
    PEP_ACPI_SPB_I2C_RESOURCE SpbI2c;
    PEP_ACPI_SPB_SPI_RESOURCE SpbSpi;
    PEP_ACPI_SPB_UART_RESOURCE SpbUart;
    PEP_ACPI_EXTENDED_ADDRESS ExtendedAddress;
} PEP_ACPI_RESOURCE, *PPEP_ACPI_RESOURCE;

typedef struct _PEP_ACPI_REQUEST_CONVERT_TO_BIOS_RESOURCES {
    NTSTATUS TranslationStatus;
    PPEP_ACPI_RESOURCE InputBuffer;
    SIZE_T InputBufferSize;
    PVOID OutputBuffer;
    SIZE_T OutputBufferSize;
    ULONG Flags;
} PEP_ACPI_REQUEST_CONVERT_TO_BIOS_RESOURCES,
    *PPEP_ACPI_REQUEST_CONVERT_TO_BIOS_RESOURCES;

#define PEP_ACPI_REQUEST_COMMON_CONVERT_TO_BIOS_RESOURCES 0x01

VOID
FORCEINLINE
PEP_ACPI_INITIALIZE_MEMORY_RESOURCE (
    _In_ UCHAR ReadWrite,
    _In_ ULONG MinimumAddress,
    _In_ ULONG MaximumAddress,
    _In_ ULONG Alignment,
    _In_ ULONG MemorySize,
    _Out_ PPEP_ACPI_RESOURCE Resource
    )
{
    PPEP_ACPI_IO_MEMORY_RESOURCE Memory32Resource;
    Memory32Resource = (PPEP_ACPI_IO_MEMORY_RESOURCE)Resource;
    Memory32Resource->Type = PepAcpiMemory;
    Memory32Resource->Information = ReadWrite;
    Memory32Resource->MinimumAddress.LowPart = MinimumAddress;
    Memory32Resource->MaximumAddress.LowPart = MaximumAddress;
    Memory32Resource->Alignment = Alignment;
    Memory32Resource->Length = MemorySize;
    return;
}

VOID
FORCEINLINE
PEP_ACPI_INITIALIZE_IOPORT_RESOURCE (
    _In_ UCHAR Decode,
    _In_ USHORT MinimumAddress,
    _In_ USHORT MaximumAddress,
    _In_ UCHAR Alignment,
    _In_ UCHAR PortLength,
    _Out_ PPEP_ACPI_RESOURCE Resource
    )
{
    PPEP_ACPI_IO_MEMORY_RESOURCE IoPortResource;
    IoPortResource = (PPEP_ACPI_IO_MEMORY_RESOURCE)Resource;
    IoPortResource->Type = PepAcpiIoPort;
    IoPortResource->Information = Decode;
    IoPortResource->MinimumAddress.LowPart = MinimumAddress;
    IoPortResource->MaximumAddress.LowPart = MaximumAddress;
    IoPortResource->Alignment = Alignment;
    IoPortResource->Length = PortLength;
    return;
}

VOID
FORCEINLINE
PEP_ACPI_INITIALIZE_INTERRUPT_RESOURCE (
    _In_ BOOLEAN ResourceUsage,
    _In_ KINTERRUPT_MODE EdgeLevel,
    _In_ KINTERRUPT_POLARITY InterruptLevel,
    _In_ BOOLEAN ShareType,
    _In_ BOOLEAN Wake,
    _In_ PULONG PinTable,
    _In_ UCHAR PinCount,
    _Out_ PPEP_ACPI_RESOURCE Resource
    )
{
    PPEP_ACPI_INTERRUPT_RESOURCE InterruptResource;
    InterruptResource = (PPEP_ACPI_INTERRUPT_RESOURCE)Resource;
    InterruptResource->Type = PepAcpiInterrupt;
    InterruptResource->Flags.ResourceUsage = ResourceUsage;
    InterruptResource->InterruptType = EdgeLevel;
    InterruptResource->InterruptPolarity = InterruptLevel;
    InterruptResource->Flags.Shared = ShareType;
    InterruptResource->Flags.Wake = Wake;
    InterruptResource->Count = PinCount;
    InterruptResource->Pins = PinTable;
    return;
}

VOID
FORCEINLINE
PEP_ACPI_INITIALIZE_GPIO_IO_RESOURCE (
    _In_ BOOLEAN Shareable,
    _In_ BOOLEAN CanWake,
    _In_ GPIO_PIN_CONFIG_TYPE PinConfig,
    _In_ USHORT DebounceTimeout,
    _In_ USHORT DriveStrength,
    _In_ GPIO_PIN_IORESTRICTION_TYPE IoRestriction,
    _In_ UCHAR ResourceSourceIndex,
    _In_ PUNICODE_STRING ResourceSourceName,
    _In_ BOOLEAN ResourceUsage,
    _In_ PUCHAR VendorData,
    _In_ USHORT VendorDataLength,
    _In_ PUSHORT PinTable,
    _In_ USHORT PinCount,
    _Out_ PPEP_ACPI_RESOURCE Resource
    )
{
    PPEP_ACPI_GPIO_RESOURCE GpioIo;
    GpioIo = (PPEP_ACPI_GPIO_RESOURCE)Resource;
    GpioIo->Type = PepAcpiGpioIo;
    GpioIo->Flags.Shared = Shareable;
    GpioIo->Flags.Wake = CanWake;
    GpioIo->PinConfig = PinConfig;
    GpioIo->DebounceTimeout = DebounceTimeout;
    GpioIo->DriveStrength = DriveStrength;
    GpioIo->IoRestrictionType = IoRestriction;
    GpioIo->ResourceSourceIndex = ResourceSourceIndex;
    GpioIo->ResourceSourceName = ResourceSourceName;
    GpioIo->Flags.ResourceUsage = ResourceUsage;
    GpioIo->VendorData = VendorData;
    GpioIo->VendorDataLength = VendorDataLength;
    GpioIo->PinTable = PinTable;
    GpioIo->PinCount = PinCount;
    return;
}

VOID
FORCEINLINE
PEP_ACPI_INITIALIZE_GPIO_INT_RESOURCE (
    _In_ KINTERRUPT_MODE InterruptType,
    _In_ KINTERRUPT_POLARITY LevelType,
    _In_ BOOLEAN Shareable,
    _In_ BOOLEAN CanWake,
    _In_ GPIO_PIN_CONFIG_TYPE PinConfig,
    _In_ USHORT DebounceTimeout,
    _In_ UCHAR ResourceSourceIndex,
    _In_ PUNICODE_STRING ResourceSourceName,
    _In_ BOOLEAN ResourceUsage,
    _In_ PUCHAR VendorData,
    _In_ USHORT VendorDataLength,
    _In_ PUSHORT PinTable,
    _In_ UCHAR PinCount,
    _Out_ PPEP_ACPI_RESOURCE Resource
    )
{
    PPEP_ACPI_GPIO_RESOURCE GpioInt;
    GpioInt = (PPEP_ACPI_GPIO_RESOURCE)Resource;
    GpioInt->Type = PepAcpiGpioInt;
    GpioInt->InterruptType = InterruptType;
    GpioInt->InterruptPolarity = LevelType;
    GpioInt->Flags.Shared = Shareable;
    GpioInt->Flags.Wake = CanWake;
    GpioInt->PinConfig = PinConfig;
    GpioInt->DebounceTimeout = DebounceTimeout;
    GpioInt->ResourceSourceIndex = ResourceSourceIndex;
    GpioInt->ResourceSourceName = ResourceSourceName;
    GpioInt->Flags.ResourceUsage = ResourceUsage;
    GpioInt->VendorData = VendorData;
    GpioInt->VendorDataLength = VendorDataLength;
    GpioInt->PinTable = PinTable;
    GpioInt->PinCount = PinCount;
    GpioInt->DriveStrength = 0;
    return;
}

VOID
FORCEINLINE
PEP_ACPI_INITIALIZE_SPB_I2C_RESOURCE (
    _In_ USHORT SlaveAddress,
    _In_ BOOLEAN DeviceInitiated,
    _In_ ULONG ConnectionSpeed,
    _In_ BOOLEAN AddressingMode,
    _In_ PUNICODE_STRING ResourceSource,
    _In_ UCHAR ResourceSourceIndex,
    _In_ BOOLEAN ResourceUsage,
    _In_ BOOLEAN SharedMode,
    _In_ PCHAR VendorData,
    _In_ USHORT VendorDataLength,
    _Out_ PPEP_ACPI_RESOURCE Resource
    )
{
    PPEP_ACPI_SPB_I2C_RESOURCE SpbI2c;
    SpbI2c = (PPEP_ACPI_SPB_I2C_RESOURCE)Resource;
    SpbI2c->SpbCommon.Type = PepAcpiSpbI2c;
    SpbI2c->SpbCommon.Flags.AddressingMode = AddressingMode;
    SpbI2c->SpbCommon.Flags.SlaveMode = DeviceInitiated;
    SpbI2c->SpbCommon.ResourceSourceIndex = ResourceSourceIndex;
    SpbI2c->SpbCommon.ResourceSourceName = ResourceSource;
    SpbI2c->SpbCommon.Flags.ResourceUsage = ResourceUsage;
    SpbI2c->SpbCommon.VendorData = VendorData;
    SpbI2c->SpbCommon.VendorDataLength = VendorDataLength;
    SpbI2c->ConnectionSpeed = ConnectionSpeed;
    SpbI2c->SlaveAddress = SlaveAddress;
    SpbI2c->SpbCommon.Flags.SharedMode = SharedMode;
    return;
}

#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
