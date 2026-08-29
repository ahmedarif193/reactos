/*
 * PROJECT:     ReactOS Kernel-Mode Driver Framework
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GPIO KMDF class extension
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "classlibrary.h"
#include <gpio.h>
#include <gpioclx.h>
#ifdef _M_ARM64
#include <ndk/haltypes.h>
#include <reactos/drivers/reshubio.h>
#endif

#define GPIOCLX_MAX_CONNECTION_PINS 64

typedef struct _GPIOCLX_DRIVER_CONTEXT
{
    GPIO_CLIENT_REGISTRATION_PACKET Packet;
    BOOLEAN Registered;
} GPIOCLX_DRIVER_CONTEXT, *PGPIOCLX_DRIVER_CONTEXT;

struct _GPIOCLX_DEVICE_CONTEXT;

typedef struct _GPIOCLX_BANK
{
    struct _GPIOCLX_DEVICE_CONTEXT *Device;
    BANK_ID BankId;
    ULONG Gsiv;
    WDFINTERRUPT Interrupt;
    KSPIN_LOCK Lock;
    KIRQL OldIrql;
    ULONG64 EnabledMask;
    ULONG64 PendingMask;
} GPIOCLX_BANK, *PGPIOCLX_BANK;

typedef struct _GPIOCLX_INTERRUPT_CONTEXT
{
    PGPIOCLX_BANK Bank;
} GPIOCLX_INTERRUPT_CONTEXT, *PGPIOCLX_INTERRUPT_CONTEXT;

typedef struct _GPIOCLX_CONTROLLER
{
    struct _GPIOCLX_DEVICE_CONTEXT *Device;
    PVOID Reserved;
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) UCHAR Context[1];
} GPIOCLX_CONTROLLER, *PGPIOCLX_CONTROLLER;

#define GPIOCLX_MAX_LINES 512

typedef struct _GPIOCLX_DEVICE_CONTEXT
{
    WDFDRIVER Driver;
    PDEVICE_OBJECT Pdo;
    BOOLEAN HalRegistered;
    UCHAR LineMode[GPIOCLX_MAX_LINES];
    UCHAR LinePolarity[GPIOCLX_MAX_LINES];
    ULONG LineGsiv[GPIOCLX_MAX_LINES];
#ifdef _M_ARM64
    HAL_SECONDARY_INTERRUPT_INFORMATION HalInfo;
    CHAR OwnerName[64];
    USHORT OwnerNameLength;
    PIO_WORKITEM WorkItem;
    KSPIN_LOCK PendingLock;
    BOOLEAN WorkQueued;
    ULONG PendingCount;
    struct { ULONG Gsiv; KINTERRUPT_MODE Mode; KINTERRUPT_POLARITY Polarity; BOOLEAN Enable; } Pending[64];
#endif
    PGPIO_CLIENT_REGISTRATION_PACKET Packet;
    PGPIOCLX_CONTROLLER Controller;
    CLIENT_CONTROLLER_BASIC_INFORMATION Info;
    USHORT BankCount;
    PGPIOCLX_BANK Banks;
    KSPIN_LOCK FallbackLock;
    KIRQL FallbackIrql;
    BOOLEAN Initialized;
    BOOLEAN Prepared;
    BOOLEAN Started;
    BOOLEAN StartedOnce;
    WDFQUEUE Queue;
} GPIOCLX_DEVICE_CONTEXT, *PGPIOCLX_DEVICE_CONTEXT;

typedef struct _GPIOCLX_FILE_CONTEXT
{
    LARGE_INTEGER ConnectionId;
    BANK_ID BankId;
    USHORT PinCount;
    PIN_NUMBER Pins[GPIOCLX_MAX_CONNECTION_PINS];
    GPIO_CONNECT_IO_PINS_MODE Mode;
    UCHAR IoRestriction;
    UCHAR PullConfiguration;
    USHORT DebounceTimeout;
    USHORT DriveStrength;
    BOOLEAN Connected;
} GPIOCLX_FILE_CONTEXT, *PGPIOCLX_FILE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(GPIOCLX_DRIVER_CONTEXT, GpioCxGetDriverContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(GPIOCLX_DEVICE_CONTEXT, GpioCxGetDeviceContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(GPIOCLX_INTERRUPT_CONTEXT, GpioCxGetInterruptContext)
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(GPIOCLX_FILE_CONTEXT, GpioCxGetFileContext)

#include <pshpack1.h>
typedef struct _GPIOCLX_ACPI_GPIO_DESCRIPTOR
{
    UCHAR Tag;
    USHORT Length;
    UCHAR RevisionId;
    UCHAR ConnectionType;
    USHORT GeneralFlags;
    USHORT InterruptIoFlags;
    UCHAR PinConfiguration;
    USHORT DriveStrength;
    USHORT DebounceTimeout;
    USHORT PinTableOffset;
    UCHAR ResourceSourceIndex;
    USHORT ResourceSourceNameOffset;
    USHORT VendorDataOffset;
    USHORT VendorDataLength;
} GPIOCLX_ACPI_GPIO_DESCRIPTOR, *PGPIOCLX_ACPI_GPIO_DESCRIPTOR;

static NTSTATUS GpioCxParseDescriptor(_In_ PGPIOCLX_DEVICE_CONTEXT Device,
                                      _In_ PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties,
                                      _In_ UCHAR ExpectedType,
                                      _Inout_ PGPIOCLX_FILE_CONTEXT File);
#include <poppack.h>

#define GPIOCLX_ACPI_GPIO_TAG 0x8C
#define GPIOCLX_ACPI_GPIO_CONNECTION_INT 0x00
#define GPIOCLX_ACPI_GPIO_CONNECTION_IO 0x01
#define GPIOCLX_ACPI_IO_RESTRICTION_MASK 0x03
#define GPIOCLX_ACPI_IO_RESTRICTION_INPUT 0x01
#define GPIOCLX_ACPI_IO_RESTRICTION_OUTPUT 0x02

static
PGPIOCLX_DEVICE_CONTEXT
GpioCxDeviceFromControllerContext(
    _In_ PVOID Context)
{
    PGPIOCLX_CONTROLLER Controller;

    if (Context == NULL)
        return NULL;

    Controller = CONTAINING_RECORD(Context, GPIOCLX_CONTROLLER, Context);
    return Controller->Device;
}

static
VOID
GpioCxAcquireBankLock(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ BANK_ID BankId)
{
    PGPIOCLX_BANK Bank;

    if (Device->Banks == NULL || BankId >= Device->BankCount)
    {
        KeAcquireSpinLock(&Device->FallbackLock, &Device->FallbackIrql);
        return;
    }

    Bank = &Device->Banks[BankId];
    if (Bank->Interrupt != NULL)
        WdfInterruptAcquireLock(Bank->Interrupt);
    else
        KeAcquireSpinLock(&Bank->Lock, &Bank->OldIrql);
}

static
VOID
GpioCxReleaseBankLock(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ BANK_ID BankId)
{
    PGPIOCLX_BANK Bank;

    if (Device->Banks == NULL || BankId >= Device->BankCount)
    {
        KeReleaseSpinLock(&Device->FallbackLock, Device->FallbackIrql);
        return;
    }

    Bank = &Device->Banks[BankId];
    if (Bank->Interrupt != NULL)
        WdfInterruptReleaseLock(Bank->Interrupt);
    else
        KeReleaseSpinLock(&Bank->Lock, Bank->OldIrql);
}

static
BOOLEAN
NTAPI
GpioCxEvtInterruptIsr(
    _In_ WDFINTERRUPT Interrupt,
    _In_ ULONG MessageId)
{
    PGPIOCLX_BANK Bank = GpioCxGetInterruptContext(Interrupt)->Bank;
    PGPIOCLX_DEVICE_CONTEXT Device = Bank->Device;
    PGPIO_CLIENT_REGISTRATION_PACKET Packet = Device->Packet;
    PVOID Context = Device->Controller->Context;
    GPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Query;
    GPIO_MASK_INTERRUPT_PARAMETERS Mask;
    GPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Clear;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(MessageId);

    if (!Device->Started || Bank->EnabledMask == 0)
        return FALSE;

    if (Packet->CLIENT_PreProcessControllerInterrupt != NULL)
        Packet->CLIENT_PreProcessControllerInterrupt(Context, Bank->BankId, Bank->EnabledMask);

    if (Packet->CLIENT_QueryActiveInterrupts == NULL)
        return FALSE;

    RtlZeroMemory(&Query, sizeof(Query));
    Query.BankId = Bank->BankId;
    Query.EnabledMask = Bank->EnabledMask;
    Status = Packet->CLIENT_QueryActiveInterrupts(Context, &Query);
    if (!NT_SUCCESS(Status) || Query.ActiveMask == 0)
        return FALSE;

    if (Packet->CLIENT_MaskInterrupts != NULL)
    {
        RtlZeroMemory(&Mask, sizeof(Mask));
        Mask.BankId = Bank->BankId;
        Mask.PinMask = Query.ActiveMask;
        Packet->CLIENT_MaskInterrupts(Context, &Mask);
    }

    if (Packet->CLIENT_ClearActiveInterrupts != NULL)
    {
        RtlZeroMemory(&Clear, sizeof(Clear));
        Clear.BankId = Bank->BankId;
        Clear.ClearActiveMask = Query.ActiveMask;
        Packet->CLIENT_ClearActiveInterrupts(Context, &Clear);
    }

#ifdef _M_ARM64
    {
        ULONG64 Active = Query.ActiveMask;

        while (Active != 0)
        {
            ULONG Pin = (ULONG)__builtin_ctzll(Active);
            ULONG Line = (ULONG)Bank->BankId * Device->Info.NumberOfPinsPerBank + Pin;

            Active &= ~(1ULL << Pin);
            if (Line < GPIOCLX_MAX_LINES && Device->LineGsiv[Line] != 0 &&
                Device->HalInfo.DispatchInterrupt != NULL &&
                Device->HalInfo.DispatchInterrupt(Device->LineGsiv[Line]) &&
                Packet->CLIENT_UnmaskInterrupt != NULL)
            {
                GPIO_ENABLE_INTERRUPT_PARAMETERS Unmask;

                RtlZeroMemory(&Unmask, sizeof(Unmask));
                Unmask.BankId = Bank->BankId;
                Unmask.PinNumber = (PIN_NUMBER)Pin;
                Unmask.InterruptMode = (KINTERRUPT_MODE)Device->LineMode[Line];
                Unmask.Polarity = (KINTERRUPT_POLARITY)Device->LinePolarity[Line];
                Packet->CLIENT_UnmaskInterrupt(Context, &Unmask);
            }
        }
    }
#endif

    Bank->PendingMask |= Query.ActiveMask;
    WdfInterruptQueueDpcForIsr(Interrupt);
    return TRUE;
}

#ifdef _M_ARM64
static
NTSTATUS
GpioCxResolveInterruptLine(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ ULONG Gsiv,
    _Out_ PGPIOCLX_FILE_CONTEXT Line)
{
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties;
    LARGE_INTEGER ConnectionId;
    NTSTATUS Status;

    ConnectionId.QuadPart = (LONGLONG)RH_SECONDARY_INTERRUPT_CONNECTION_ID(Gsiv);
    Status = WdfCxQueryConnectionProperties(ConnectionId, &Properties);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlZeroMemory(Line, sizeof(*Line));
    Status = GpioCxParseDescriptor(Device, Properties, GPIOCLX_ACPI_GPIO_CONNECTION_INT, Line);
    ExFreePoolWithTag(Properties, WDFCX_TAG);
    if (NT_SUCCESS(Status) && Line->PinCount != 1)
        Status = STATUS_NOT_SUPPORTED;
    return Status;
}

static
VOID
GpioCxApplyInterruptLine(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ ULONG Gsiv,
    _In_ KINTERRUPT_MODE Mode,
    _In_ KINTERRUPT_POLARITY Polarity,
    _In_ BOOLEAN Enable)
{
    GPIOCLX_FILE_CONTEXT Line;
    PGPIOCLX_BANK Bank;
    ULONG PinsPerBank = Device->Info.NumberOfPinsPerBank;
    ULONG LineNumber;
    NTSTATUS Status;

    if (PinsPerBank == 0 || !Device->Started)
        return;
    if (!NT_SUCCESS(GpioCxResolveInterruptLine(Device, Gsiv, &Line)) || Line.BankId >= Device->BankCount)
        return;
    Bank = &Device->Banks[Line.BankId];
    LineNumber = (ULONG)Line.BankId * PinsPerBank + Line.Pins[0];
    if (LineNumber >= GPIOCLX_MAX_LINES)
        return;

    if (Enable)
    {
        GPIO_ENABLE_INTERRUPT_PARAMETERS Params;

        if (Device->Packet->CLIENT_EnableInterrupt == NULL)
            return;
        RtlZeroMemory(&Params, sizeof(Params));
        Params.BankId = Bank->BankId;
        Params.PinNumber = Line.Pins[0];
        Params.InterruptMode = Mode;
        Params.Polarity = Polarity;
        Params.PullConfiguration = Line.PullConfiguration;
        Params.DebounceTimeout = Line.DebounceTimeout;
        Device->LineMode[LineNumber] = (UCHAR)Mode;
        Device->LinePolarity[LineNumber] = (UCHAR)Polarity;
        Device->LineGsiv[LineNumber] = Gsiv;
        GpioCxAcquireBankLock(Device, Bank->BankId);
        Status = Device->Packet->CLIENT_EnableInterrupt(Device->Controller->Context, &Params);
        if (NT_SUCCESS(Status))
            Bank->EnabledMask |= 1ULL << Params.PinNumber;
        GpioCxReleaseBankLock(Device, Bank->BankId);
    }
    else
    {
        GPIO_DISABLE_INTERRUPT_PARAMETERS Params;

        if (Device->Packet->CLIENT_DisableInterrupt == NULL)
            return;
        RtlZeroMemory(&Params, sizeof(Params));
        Params.BankId = Bank->BankId;
        Params.PinNumber = Line.Pins[0];
        GpioCxAcquireBankLock(Device, Bank->BankId);
        Bank->EnabledMask &= ~(1ULL << Params.PinNumber);
        Device->Packet->CLIENT_DisableInterrupt(Device->Controller->Context, &Params);
        GpioCxReleaseBankLock(Device, Bank->BankId);
        Device->LineGsiv[LineNumber] = 0;
    }
}

static
VOID
NTAPI
GpioCxSecondaryWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PGPIOCLX_DEVICE_CONTEXT Device = Context;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Device == NULL)
        return;
    for (;;)
    {
        ULONG Gsiv;
        KINTERRUPT_MODE Mode;
        KINTERRUPT_POLARITY Polarity;
        BOOLEAN Enable;

        KeAcquireSpinLock(&Device->PendingLock, &OldIrql);
        if (Device->PendingCount == 0)
        {
            Device->WorkQueued = FALSE;
            KeReleaseSpinLock(&Device->PendingLock, OldIrql);
            return;
        }
        Gsiv = Device->Pending[0].Gsiv;
        Mode = Device->Pending[0].Mode;
        Polarity = Device->Pending[0].Polarity;
        Enable = Device->Pending[0].Enable;
        Device->PendingCount--;
        RtlMoveMemory(&Device->Pending[0], &Device->Pending[1], Device->PendingCount * sizeof(Device->Pending[0]));
        KeReleaseSpinLock(&Device->PendingLock, OldIrql);
        GpioCxApplyInterruptLine(Device, Gsiv, Mode, Polarity, Enable);
    }
}

static
NTSTATUS
GpioCxQueueInterruptLine(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ ULONG Gsiv,
    _In_ KINTERRUPT_MODE Mode,
    _In_ KINTERRUPT_POLARITY Polarity,
    _In_ BOOLEAN Enable)
{
    KIRQL OldIrql;
    BOOLEAN Queue = FALSE;

    if (Device->WorkItem == NULL || !Device->Started)
        return STATUS_INVALID_DEVICE_STATE;
    KeAcquireSpinLock(&Device->PendingLock, &OldIrql);
    if (Device->PendingCount >= RTL_NUMBER_OF(Device->Pending))
    {
        KeReleaseSpinLock(&Device->PendingLock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Device->Pending[Device->PendingCount].Gsiv = Gsiv;
    Device->Pending[Device->PendingCount].Mode = Mode;
    Device->Pending[Device->PendingCount].Polarity = Polarity;
    Device->Pending[Device->PendingCount].Enable = Enable;
    Device->PendingCount++;
    if (!Device->WorkQueued)
    {
        Device->WorkQueued = TRUE;
        Queue = TRUE;
    }
    KeReleaseSpinLock(&Device->PendingLock, OldIrql);
    if (Queue)
        IoQueueWorkItem(Device->WorkItem, GpioCxSecondaryWorker, DelayedWorkQueue, Device);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
GpioCxHalEnableInterrupt(
    _In_ PVOID Context,
    _In_ ULONG Gsiv,
    _In_ KINTERRUPT_MODE Mode,
    _In_ KINTERRUPT_POLARITY Polarity)
{
    return GpioCxQueueInterruptLine(Context, Gsiv, Mode, Polarity, TRUE);
}

static
VOID
NTAPI
GpioCxHalDisableInterrupt(
    _In_ PVOID Context,
    _In_ ULONG Gsiv)
{
    GpioCxQueueInterruptLine(Context, Gsiv, LevelSensitive, InterruptActiveHigh, FALSE);
}

static
VOID
GpioCxRegisterSecondaryController(
    _In_ WDFDEVICE DeviceHandle,
    _In_ PGPIOCLX_DEVICE_CONTEXT Device)
{
    HAL_SECONDARY_INTERRUPT_INTERFACE Interface;
    WDF_INTERRUPT_INFO Info;
    WCHAR NameBuffer[96];
    UNICODE_STRING Name;
    ANSI_STRING Owner;
    ULONG Length = 0;
    NTSTATUS Status;

    if (Device->HalRegistered)
        return;

    Device->Pdo = WdfDeviceWdmGetPhysicalDevice(DeviceHandle);
    if (Device->WorkItem == NULL)
    {
        KeInitializeSpinLock(&Device->PendingLock);
        Device->WorkItem = IoAllocateWorkItem(WdfDeviceWdmGetDeviceObject(DeviceHandle));
        if (Device->WorkItem == NULL)
            return;
    }

    RtlZeroMemory(&Device->HalInfo, sizeof(Device->HalInfo));
    Status = HalQuerySystemInformation(HalSecondaryInterruptInformation, sizeof(Device->HalInfo), &Device->HalInfo, &Length);
    if (!NT_SUCCESS(Status))
        return;

    Status = IoGetDeviceProperty(Device->Pdo, DevicePropertyPhysicalDeviceObjectName, sizeof(NameBuffer), NameBuffer, &Length);
    if (!NT_SUCCESS(Status))
        return;
    Name.Buffer = NameBuffer;
    Name.Length = (USHORT)(Length >= sizeof(WCHAR) ? Length - sizeof(WCHAR) : 0);
    Name.MaximumLength = sizeof(NameBuffer);
    Owner.Buffer = Device->OwnerName;
    Owner.Length = 0;
    Owner.MaximumLength = sizeof(Device->OwnerName);
    if (!NT_SUCCESS(RtlUnicodeStringToAnsiString(&Owner, &Name, FALSE)))
        return;
    Device->OwnerNameLength = Owner.Length;

    RtlZeroMemory(&Interface, sizeof(Interface));
    Interface.Size = sizeof(Interface);
    Interface.Context = Device;
    Interface.OwnerName = Device->OwnerName;
    Interface.OwnerNameLength = Device->OwnerNameLength;
    Interface.EnableInterrupt = GpioCxHalEnableInterrupt;
    Interface.DisableInterrupt = GpioCxHalDisableInterrupt;
    if (Device->BankCount != 0 && Device->Banks[0].Interrupt != NULL)
    {
        WDF_INTERRUPT_INFO_INIT(&Info);
        WdfInterruptGetInfo(Device->Banks[0].Interrupt, &Info);
        Interface.Irql = Info.Irql;
        Interface.PrimaryGsiv = Device->Banks[0].Gsiv;
    }
    if (NT_SUCCESS(HalSetSystemInformation(HalRegisterSecondaryInterruptInterface, sizeof(Interface), &Interface)))
        Device->HalRegistered = TRUE;
}

static
VOID
GpioCxUnregisterSecondaryController(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Device->PendingLock, &OldIrql);
    Device->PendingCount = 0;
    KeReleaseSpinLock(&Device->PendingLock, OldIrql);
}
#else
#define GpioCxRegisterSecondaryController(DeviceHandle, Device) ((void)0)
#define GpioCxUnregisterSecondaryController(Device) ((void)0)
#endif

static
VOID
NTAPI
GpioCxEvtInterruptDpc(
    _In_ WDFINTERRUPT Interrupt,
    _In_ WDFOBJECT AssociatedObject)
{
    PGPIOCLX_BANK Bank = GpioCxGetInterruptContext(Interrupt)->Bank;

    UNREFERENCED_PARAMETER(AssociatedObject);

    WdfInterruptAcquireLock(Interrupt);
    Bank->PendingMask = 0;
    WdfInterruptReleaseLock(Interrupt);
}

static
VOID
GpioCxFreeBanks(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device)
{
    if (Device->Banks != NULL)
    {
        ExFreePoolWithTag(Device->Banks, WDFCX_TAG);
        Device->Banks = NULL;
    }
    Device->BankCount = 0;
}

static
NTSTATUS
GpioCxCreateBanks(
    _In_ WDFDEVICE DeviceHandle,
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    ULONG Count = WdfCmResourceListGetCount(ResourcesTranslated);
    ULONG RawCount = WdfCmResourceListGetCount(ResourcesRaw);
    ULONG InterruptIndex = 0;
    USHORT PinsPerBank = Device->Info.NumberOfPinsPerBank;
    USHORT BankCount;
    ULONG Index;
    NTSTATUS Status;

    if (PinsPerBank == 0 || PinsPerBank > 64)
        return STATUS_INVALID_PARAMETER;

    BankCount = (Device->Info.TotalPins + PinsPerBank - 1) / PinsPerBank;
    if (BankCount == 0)
        BankCount = 1;

    Device->Banks = ExAllocatePoolWithTag(NonPagedPool, BankCount * sizeof(GPIOCLX_BANK), WDFCX_TAG);
    if (Device->Banks == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Device->Banks, BankCount * sizeof(GPIOCLX_BANK));
    Device->BankCount = BankCount;
    for (Index = 0; Index < BankCount; Index++)
    {
        Device->Banks[Index].Device = Device;
        Device->Banks[Index].BankId = (BANK_ID)Index;
        KeInitializeSpinLock(&Device->Banks[Index].Lock);
    }

    for (Index = 0; Index < Count && Index < RawCount; Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Translated = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Raw = WdfCmResourceListGetDescriptor(ResourcesRaw, Index);
        WDF_INTERRUPT_CONFIG InterruptConfig;
        WDF_OBJECT_ATTRIBUTES Attributes;
        PGPIOCLX_BANK Bank;
        WDFINTERRUPT Interrupt;

        if (Translated == NULL || Raw == NULL || Translated->Type != CmResourceTypeInterrupt)
            continue;

        Bank = &Device->Banks[min(InterruptIndex, (ULONG)BankCount - 1)];
        InterruptIndex++;
        if (Bank->Interrupt != NULL)
            continue;
        Bank->Gsiv = Raw->u.Interrupt.Vector;

        WDF_INTERRUPT_CONFIG_INIT(&InterruptConfig, GpioCxEvtInterruptIsr, GpioCxEvtInterruptDpc);
        InterruptConfig.InterruptRaw = Raw;
        InterruptConfig.InterruptTranslated = Translated;
        InterruptConfig.AutomaticSerialization = FALSE;

        WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, GPIOCLX_INTERRUPT_CONTEXT);
        Status = WdfInterruptCreate(DeviceHandle, &InterruptConfig, &Attributes, &Interrupt);
        if (!NT_SUCCESS(Status))
            return Status;

        GpioCxGetInterruptContext(Interrupt)->Bank = Bank;
        Bank->Interrupt = Interrupt;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
GpioCxEvtPrePrepareHardware(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(DeviceHandle);
    PGPIO_CLIENT_REGISTRATION_PACKET Packet;
    SIZE_T ControllerSize;
    NTSTATUS Status;

    if (Device == NULL || !Device->Initialized)
        return STATUS_INVALID_DEVICE_STATE;
    Packet = Device->Packet;

    if (Device->Controller == NULL)
    {
        ControllerSize = FIELD_OFFSET(GPIOCLX_CONTROLLER, Context) + max(Packet->ControllerContextSize, 1);
        Device->Controller = ExAllocatePoolWithTag(NonPagedPool, ControllerSize, WDFCX_TAG);
        if (Device->Controller == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Device->Controller, ControllerSize);
        Device->Controller->Device = Device;
    }

    Status = Packet->CLIENT_PrepareController(DeviceHandle,
                                              Device->Controller->Context,
                                              ResourcesRaw,
                                              ResourcesTranslated);
    if (!NT_SUCCESS(Status))
        return Status;
    Device->Prepared = TRUE;

    RtlZeroMemory(&Device->Info, sizeof(Device->Info));
    Device->Info.Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Device->Info.Size = sizeof(Device->Info);
    Status = Packet->CLIENT_QueryControllerBasicInformation(Device->Controller->Context, &Device->Info);
    if (!NT_SUCCESS(Status))
        return Status;

    return GpioCxCreateBanks(DeviceHandle, Device, ResourcesRaw, ResourcesTranslated);
}

static
NTSTATUS
NTAPI
GpioCxEvtPostReleaseHardware(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(DeviceHandle);
    USHORT Index;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    if (Device == NULL)
        return STATUS_SUCCESS;

    if (Device->Prepared)
    {
        Device->Prepared = FALSE;
        Device->Packet->CLIENT_ReleaseController(DeviceHandle, Device->Controller->Context);
    }

    for (Index = 0; Index < Device->BankCount; Index++)
        Device->Banks[Index].Interrupt = NULL;
    GpioCxFreeBanks(Device);
#ifdef _M_ARM64
    if (Device->WorkItem != NULL)
    {
        IoFreeWorkItem(Device->WorkItem);
        Device->WorkItem = NULL;
    }
#endif

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
GpioCxEvtPreD0Entry(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(DeviceHandle);
    NTSTATUS Status;

    if (Device == NULL || !Device->Prepared)
        return STATUS_INVALID_DEVICE_STATE;

    Status = Device->Packet->CLIENT_StartController(Device->Controller->Context,
                                                    Device->StartedOnce,
                                                    PreviousState);
    if (!NT_SUCCESS(Status))
        return Status;

    Device->Started = TRUE;
    Device->StartedOnce = TRUE;
    GpioCxRegisterSecondaryController(DeviceHandle, Device);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
GpioCxEvtPostD0Exit(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(DeviceHandle);

    if (Device == NULL || !Device->Started)
        return STATUS_SUCCESS;

    GpioCxUnregisterSecondaryController(Device);
    Device->Started = FALSE;
    return Device->Packet->CLIENT_StopController(Device->Controller->Context,
                                                 TargetState != WdfPowerDeviceD3Final,
                                                 TargetState);
}

static
VOID
NTAPI
GpioCxEvtDeviceCleanup(
    _In_ WDFOBJECT Object)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(Object);

    if (Device == NULL)
        return;

    GpioCxFreeBanks(Device);
    if (Device->Controller != NULL)
    {
        ExFreePoolWithTag(Device->Controller, WDFCX_TAG);
        Device->Controller = NULL;
    }
}

static
NTSTATUS
GpioCxParseDescriptor(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties,
    _In_ UCHAR ExpectedType,
    _Inout_ PGPIOCLX_FILE_CONTEXT File)
{
    PGPIOCLX_ACPI_GPIO_DESCRIPTOR Descriptor;
    PUCHAR Base;
    ULONG DescriptorLength;
    ULONG PinTableEnd;
    ULONG PinCount;
    ULONG Index;
    USHORT PinsPerBank = Device->Info.NumberOfPinsPerBank;
    USHORT IoRestriction;

    if (Properties->PropertiesLength < sizeof(GPIOCLX_ACPI_GPIO_DESCRIPTOR))
        return STATUS_INVALID_PARAMETER;

    Base = Properties->ConnectionProperties;
    Descriptor = (PGPIOCLX_ACPI_GPIO_DESCRIPTOR)Base;
    if (Descriptor->Tag != GPIOCLX_ACPI_GPIO_TAG)
        return STATUS_INVALID_PARAMETER;

    DescriptorLength = 3 + Descriptor->Length;
    if (DescriptorLength > Properties->PropertiesLength)
        return STATUS_INVALID_PARAMETER;

    if (Descriptor->ConnectionType != ExpectedType)
        return STATUS_NOT_SUPPORTED;

    PinTableEnd = Descriptor->ResourceSourceNameOffset;
    if (PinTableEnd == 0 || PinTableEnd > DescriptorLength)
        PinTableEnd = Descriptor->VendorDataOffset != 0 ? Descriptor->VendorDataOffset : DescriptorLength;
    if (Descriptor->PinTableOffset < sizeof(GPIOCLX_ACPI_GPIO_DESCRIPTOR) ||
        Descriptor->PinTableOffset > PinTableEnd ||
        PinTableEnd > DescriptorLength)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PinCount = (PinTableEnd - Descriptor->PinTableOffset) / sizeof(USHORT);
    if (PinCount == 0 || PinCount > GPIOCLX_MAX_CONNECTION_PINS || PinsPerBank == 0)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < PinCount; Index++)
    {
        USHORT Pin;
        BANK_ID BankId;

        RtlCopyMemory(&Pin, Base + Descriptor->PinTableOffset + Index * sizeof(USHORT), sizeof(USHORT));
        if (Pin >= Device->Info.TotalPins)
            return STATUS_INVALID_PARAMETER;

        BankId = Pin / PinsPerBank;
        if (Index == 0)
            File->BankId = BankId;
        else if (File->BankId != BankId)
            return STATUS_NOT_SUPPORTED;

        File->Pins[Index] = Pin % PinsPerBank;
    }

    File->PinCount = (USHORT)PinCount;
    IoRestriction = Descriptor->InterruptIoFlags & GPIOCLX_ACPI_IO_RESTRICTION_MASK;
    File->IoRestriction = (UCHAR)IoRestriction;
    File->Mode = IoRestriction == GPIOCLX_ACPI_IO_RESTRICTION_OUTPUT ? ConnectModeOutput : ConnectModeInput;
    File->PullConfiguration = Descriptor->PinConfiguration;
    File->DebounceTimeout = Descriptor->DebounceTimeout;
    File->DriveStrength = Descriptor->DriveStrength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
GpioCxConnectPins(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ PGPIOCLX_FILE_CONTEXT File,
    _In_ GPIO_CONNECT_IO_PINS_MODE Mode)
{
    GPIO_CONNECT_IO_PINS_PARAMETERS Parameters;
    NTSTATUS Status;

    if (Device->Packet->CLIENT_ConnectIoPins == NULL)
        return STATUS_NOT_SUPPORTED;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.BankId = File->BankId;
    Parameters.PinNumberTable = File->Pins;
    Parameters.PinCount = File->PinCount;
    Parameters.ConnectMode = Mode;
    Parameters.PullConfiguration = File->PullConfiguration;
    Parameters.DebounceTimeout = File->DebounceTimeout;
    Parameters.DriveStrength = File->DriveStrength;

    Status = Device->Packet->CLIENT_ConnectIoPins(Device->Controller->Context, &Parameters);
    if (NT_SUCCESS(Status))
    {
        File->Mode = Mode;
        File->Connected = TRUE;
    }
    return Status;
}

static
VOID
GpioCxDisconnectPins(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ PGPIOCLX_FILE_CONTEXT File,
    _In_ BOOLEAN PreserveConfiguration)
{
    GPIO_DISCONNECT_IO_PINS_PARAMETERS Parameters;

    if (!File->Connected)
        return;
    File->Connected = FALSE;

    if (Device->Packet->CLIENT_DisconnectIoPins == NULL || !Device->Prepared)
        return;

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.BankId = File->BankId;
    Parameters.PinNumberTable = File->Pins;
    Parameters.PinCount = File->PinCount;
    Parameters.DisconnectMode = File->Mode;
    Parameters.DisconnectFlags.PreserveConfiguration = PreserveConfiguration;
    Device->Packet->CLIENT_DisconnectIoPins(Device->Controller->Context, &Parameters);
}

static
BOOLEAN
NTAPI
GpioCxEvtCxDeviceFileCreate(
    _In_ WDFDEVICE DeviceHandle,
    _In_ WDFREQUEST Request,
    _In_opt_ WDFFILEOBJECT FileObject)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(DeviceHandle);
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Properties;
    PGPIOCLX_FILE_CONTEXT File;
    NTSTATUS Status;

    if (FileObject == NULL || Device == NULL || !Device->Initialized)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return TRUE;
    }

    File = GpioCxGetFileContext(FileObject);
    if (!WdfCxParseConnectionId(WdfFileObjectGetFileName(FileObject), &File->ConnectionId))
    {
        WdfRequestComplete(Request, STATUS_OBJECT_NAME_INVALID);
        return TRUE;
    }

    if (!Device->Started)
    {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_READY);
        return TRUE;
    }

    Status = WdfCxQueryConnectionProperties(File->ConnectionId, &Properties);
    if (NT_SUCCESS(Status))
    {
        Status = GpioCxParseDescriptor(Device, Properties, GPIOCLX_ACPI_GPIO_CONNECTION_IO, File);
        ExFreePoolWithTag(Properties, WDFCX_TAG);
    }

    if (NT_SUCCESS(Status))
        Status = GpioCxConnectPins(Device, File, File->Mode);

    WdfRequestComplete(Request, Status);
    return TRUE;
}

static
VOID
NTAPI
GpioCxEvtFileClose(
    _In_ WDFFILEOBJECT FileObject)
{
    WDFDEVICE DeviceHandle = WdfFileObjectGetDevice(FileObject);
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(DeviceHandle);
    PGPIOCLX_FILE_CONTEXT File = GpioCxGetFileContext(FileObject);

    if (Device == NULL || File == NULL)
        return;

    GpioCxDisconnectPins(Device, File, FALSE);
}

static
NTSTATUS
GpioCxReadPins(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ PGPIOCLX_FILE_CONTEXT File,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ size_t Length)
{
    GPIO_READ_PINS_MASK_PARAMETERS Parameters;
    ULONG64 PinValues = 0;
    ULONG Index;
    NTSTATUS Status;

    if (!Device->Info.Flags.FormatIoRequestsAsMasks ||
        Device->Packet->CLIENT_ReadGpioPinsUsingMask == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.BankId = File->BankId;
    Parameters.PinValues = &PinValues;
    Parameters.Flags.WriteConfiguredPins = File->Mode == ConnectModeOutput;

    if (!Device->Info.Flags.IndependentIoHwSupported)
        GpioCxAcquireBankLock(Device, File->BankId);
    Status = Device->Packet->CLIENT_ReadGpioPinsUsingMask(Device->Controller->Context, &Parameters);
    if (!Device->Info.Flags.IndependentIoHwSupported)
        GpioCxReleaseBankLock(Device, File->BankId);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(Buffer, Length);
    for (Index = 0; Index < File->PinCount; Index++)
    {
        if ((PinValues >> File->Pins[Index]) & 1)
            Buffer[Index / 8] |= (UCHAR)(1 << (Index % 8));
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
GpioCxWritePins(
    _In_ PGPIOCLX_DEVICE_CONTEXT Device,
    _In_ PGPIOCLX_FILE_CONTEXT File,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ size_t Length)
{
    GPIO_WRITE_PINS_MASK_PARAMETERS Parameters;
    ULONG Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Length);

    if (!Device->Info.Flags.FormatIoRequestsAsMasks ||
        Device->Packet->CLIENT_WriteGpioPinsUsingMask == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (File->Mode != ConnectModeOutput)
    {
        if (File->IoRestriction == GPIOCLX_ACPI_IO_RESTRICTION_INPUT)
            return STATUS_INVALID_DEVICE_REQUEST;

        GpioCxDisconnectPins(Device, File, TRUE);
        Status = GpioCxConnectPins(Device, File, ConnectModeOutput);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.BankId = File->BankId;
    for (Index = 0; Index < File->PinCount; Index++)
    {
        if ((Buffer[Index / 8] >> (Index % 8)) & 1)
            Parameters.SetMask |= 1ULL << File->Pins[Index];
        else
            Parameters.ClearMask |= 1ULL << File->Pins[Index];
    }

    if (!Device->Info.Flags.IndependentIoHwSupported)
        GpioCxAcquireBankLock(Device, File->BankId);
    Status = Device->Packet->CLIENT_WriteGpioPinsUsingMask(Device->Controller->Context, &Parameters);
    if (!Device->Info.Flags.IndependentIoHwSupported)
        GpioCxReleaseBankLock(Device, File->BankId);

    return Status;
}

static
VOID
NTAPI
GpioCxEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE DeviceHandle = WdfIoQueueGetDevice(Queue);
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxGetDeviceContext(DeviceHandle);
    WDFFILEOBJECT FileObject = WdfRequestGetFileObject(Request);
    PGPIOCLX_FILE_CONTEXT File;
    PVOID Buffer;
    size_t Required;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    if (FileObject == NULL || Device == NULL || !Device->Started)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    File = GpioCxGetFileContext(FileObject);
    if (!File->Connected)
    {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    Required = (File->PinCount + 7) / 8;
    switch (IoControlCode)
    {
        case IOCTL_GPIO_READ_PINS:
            Status = WdfRequestRetrieveOutputBuffer(Request, Required, &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Status = GpioCxReadPins(Device, File, Buffer, Required);
            if (NT_SUCCESS(Status))
            {
                WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, Required);
                return;
            }
            break;

        case IOCTL_GPIO_WRITE_PINS:
            Status = WdfRequestRetrieveInputBuffer(Request, Required, &Buffer, NULL);
            if (NT_SUCCESS(Status))
                Status = GpioCxWritePins(Device, File, Buffer, Required);
            break;

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    WdfRequestComplete(Request, Status);
}

static
NTSTATUS
NTAPI
GpioCxDdiRegisterClient(
    _In_ WDFDRIVER Driver,
    _In_ PGPIO_CLIENT_REGISTRATION_PACKET Packet,
    _In_ PUNICODE_STRING RegistryPath)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    PGPIOCLX_DRIVER_CONTEXT Context;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    if (Driver == NULL || Packet == NULL ||
        Packet->Version != GPIO_CLIENT_VERSION ||
        Packet->Size < sizeof(GPIO_CLIENT_REGISTRATION_PACKET) ||
        Packet->CLIENT_PrepareController == NULL ||
        Packet->CLIENT_ReleaseController == NULL ||
        Packet->CLIENT_StartController == NULL ||
        Packet->CLIENT_StopController == NULL ||
        Packet->CLIENT_QueryControllerBasicInformation == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, GPIOCLX_DRIVER_CONTEXT);
    Status = WdfObjectAllocateContext(Driver, &Attributes, (PVOID *)&Context);
    if (Status == STATUS_OBJECT_NAME_EXISTS)
    {
        Context = GpioCxGetDriverContext(Driver);
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
        return Status;

    if (Context->Registered)
        return STATUS_INVALID_DEVICE_STATE;

    RtlCopyMemory(&Context->Packet, Packet, sizeof(GPIO_CLIENT_REGISTRATION_PACKET));
    Context->Registered = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
GpioCxDdiUnregisterClient(
    _In_ WDFDRIVER Driver)
{
    PGPIOCLX_DRIVER_CONTEXT Context;

    if (Driver == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = GpioCxGetDriverContext(Driver);
    if (Context == NULL || !Context->Registered)
        return STATUS_INVALID_DEVICE_STATE;

    Context->Registered = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
GpioCxDdiProcessAddDevicePreDeviceCreate(
    _In_ WDFDRIVER Driver,
    _In_ PWDFDEVICE_INIT DeviceInit,
    _Out_ PWDF_OBJECT_ATTRIBUTES FdoAttributes)
{
    PGPIOCLX_DRIVER_CONTEXT Context;
    PWDFCXDEVICE_INIT CxInit;
    WDFCX_PNPPOWER_EVENT_CALLBACKS PnpCallbacks;
    WDFCX_FILEOBJECT_CONFIG FileConfig;
    WDF_OBJECT_ATTRIBUTES Attributes;

    if (Driver == NULL || DeviceInit == NULL || FdoAttributes == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = GpioCxGetDriverContext(Driver);
    if (Context == NULL || !Context->Registered)
        return STATUS_INVALID_DEVICE_STATE;

    CxInit = WdfCxDeviceInitAllocate(WdfDriverGlobals, DeviceInit);
    if (CxInit == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(&PnpCallbacks, sizeof(PnpCallbacks));
    PnpCallbacks.Size = sizeof(PnpCallbacks);
    PnpCallbacks.EvtCxDevicePrePrepareHardware = GpioCxEvtPrePrepareHardware;
    PnpCallbacks.EvtCxDevicePostReleaseHardware = GpioCxEvtPostReleaseHardware;
    PnpCallbacks.EvtCxDevicePreD0Entry = GpioCxEvtPreD0Entry;
    PnpCallbacks.EvtCxDevicePostD0Exit = GpioCxEvtPostD0Exit;
    WdfCxDeviceInitSetPnpPowerEventCallbacks(WdfDriverGlobals, CxInit, &PnpCallbacks);

    RtlZeroMemory(&FileConfig, sizeof(FileConfig));
    FileConfig.Size = sizeof(FileConfig);
    FileConfig.EvtCxDeviceFileCreate = GpioCxEvtCxDeviceFileCreate;
    FileConfig.EvtFileClose = GpioCxEvtFileClose;
    FileConfig.AutoForwardCleanupClose = WdfFalse;
    FileConfig.FileObjectClass = WdfFileObjectWdfCannotUseFsContexts;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&Attributes, GPIOCLX_FILE_CONTEXT);
    Attributes.ExecutionLevel = WdfExecutionLevelPassive;
    WdfCxDeviceInitSetFileObjectConfig(WdfDriverGlobals, CxInit, &FileConfig, &Attributes);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(FdoAttributes, GPIOCLX_DEVICE_CONTEXT);
    FdoAttributes->EvtCleanupCallback = GpioCxEvtDeviceCleanup;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
GpioCxDdiProcessAddDevicePostDeviceCreate(
    _In_ WDFDRIVER Driver,
    _In_ WDFDEVICE DeviceHandle)
{
    PGPIOCLX_DRIVER_CONTEXT DriverContext;
    PGPIOCLX_DEVICE_CONTEXT Device;
    WDF_IO_QUEUE_CONFIG QueueConfig;
    NTSTATUS Status;

    if (Driver == NULL || DeviceHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    DriverContext = GpioCxGetDriverContext(Driver);
    Device = GpioCxGetDeviceContext(DeviceHandle);
    if (DriverContext == NULL || !DriverContext->Registered || Device == NULL)
        return STATUS_INVALID_DEVICE_STATE;

    Device->Driver = Driver;
    Device->Packet = &DriverContext->Packet;
    KeInitializeSpinLock(&Device->FallbackLock);

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&QueueConfig, WdfIoQueueDispatchSequential);
    QueueConfig.EvtIoDeviceControl = GpioCxEvtIoDeviceControl;
    Status = WdfIoQueueCreate(DeviceHandle, &QueueConfig, WDF_NO_OBJECT_ATTRIBUTES, &Device->Queue);
    if (!NT_SUCCESS(Status))
        return Status;

    Device->Initialized = TRUE;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
GpioCxDdiAcquireInterruptLock(
    _In_ PVOID Context,
    _In_ BANK_ID BankId)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxDeviceFromControllerContext(Context);

    if (Device != NULL)
        GpioCxAcquireBankLock(Device, BankId);
}

static
VOID
NTAPI
GpioCxDdiReleaseInterruptLock(
    _In_ PVOID Context,
    _In_ BANK_ID BankId)
{
    PGPIOCLX_DEVICE_CONTEXT Device = GpioCxDeviceFromControllerContext(Context);

    if (Device != NULL)
        GpioCxReleaseBankLock(Device, BankId);
}

static PVOID GpioCxFunctions[GpioExportLastExportIndex] =
{
    GpioCxDdiRegisterClient,
    GpioCxDdiUnregisterClient,
    GpioCxDdiProcessAddDevicePreDeviceCreate,
    GpioCxDdiProcessAddDevicePostDeviceCreate,
    GpioCxDdiAcquireInterruptLock,
    GpioCxDdiReleaseInterruptLock
};

static
NTSTATUS
NTAPI
GpioCxLibraryBindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    return WdfCxBindClient(ClassBindInfo,
                           ClientGlobals,
                           GpioCxFunctions,
                           RTL_NUMBER_OF(GpioCxFunctions),
                           1);
}

static
VOID
NTAPI
GpioCxLibraryUnbindClient(
    _In_ PWDF_CLASS_BIND_INFO ClassBindInfo,
    _Inout_ PWDF_COMPONENT_GLOBALS *ClientGlobals)
{
    UNREFERENCED_PARAMETER(ClientGlobals);
    WdfCxUnbindClient(ClassBindInfo);
}

static WDF_CLASS_LIBRARY_INFO GpioCxLibraryInfo =
{
    sizeof(WDF_CLASS_LIBRARY_INFO),
    {1, 0, 0},
    NULL,
    NULL,
    GpioCxLibraryBindClient,
    GpioCxLibraryUnbindClient
};

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    return WdfCxRegisterLibrary(DriverObject,
                                RegistryPath,
                                L"\\Device\\msgpioclx",
                                &GpioCxLibraryInfo);
}
