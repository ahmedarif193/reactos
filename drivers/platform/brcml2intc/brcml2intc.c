/*
 * PROJECT:     ReactOS Broadcom L2 Interrupt Controller Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     GpioClx client for Broadcom edge-latched L2 controllers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntddk.h>
#include <wdf.h>
#include <gpioclx.h>

#define BRCM_L2_TAG                 'I2LB'
#define BRCM_L2_LINE_COUNT          32
#define BRCM_L2_REGISTER_LENGTH     0x18
#define BRCM_L2_ALL_LINES           0xffffffffUL

#define BRCM_L2_CPU_STATUS          0x00
#define BRCM_L2_CPU_CLEAR           0x08
#define BRCM_L2_CPU_MASK_STATUS     0x0c
#define BRCM_L2_CPU_MASK_SET        0x10
#define BRCM_L2_CPU_MASK_CLEAR      0x14

typedef struct _BRCM_L2_CONTEXT
{
    PUCHAR Registers;
    ULONG RegisterLength;
    ULONG EnabledMask;
} BRCM_L2_CONTEXT, *PBRCM_L2_CONTEXT;

DRIVER_INITIALIZE DriverEntry;

static
ULONG
BrcmL2ReadRegister(
    _In_ PBRCM_L2_CONTEXT Context,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Context->Registers + Offset));
}

static
VOID
BrcmL2WriteRegister(
    _In_ PBRCM_L2_CONTEXT Context,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Context->Registers + Offset), Value);
}

static
NTSTATUS
BrcmL2ValidateLine(
    _In_ PBRCM_L2_CONTEXT Context,
    _In_ BANK_ID BankId,
    _In_ PIN_NUMBER PinNumber)
{
    if (Context->Registers == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (BankId != 0 || PinNumber >= BRCM_L2_LINE_COUNT)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2PrepareController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Memory = NULL;
    ULONG InterruptCount = 0;
    ULONG Index;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ResourcesRaw);

    for (Index = 0; Index < WdfCmResourceListGetCount(ResourcesTranslated); Index++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;

        Resource = WdfCmResourceListGetDescriptor(ResourcesTranslated, Index);
        if (Resource == NULL)
            continue;

        if (Resource->Type == CmResourceTypeMemory && Memory == NULL)
            Memory = Resource;
        else if (Resource->Type == CmResourceTypeInterrupt)
            InterruptCount++;
    }

    if (Memory == NULL || Memory->u.Memory.Length < BRCM_L2_REGISTER_LENGTH ||
        InterruptCount != 1)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Context->Registers = MmMapIoSpaceEx(Memory->u.Memory.Start,
                                        Memory->u.Memory.Length,
                                        PAGE_READWRITE | PAGE_NOCACHE);
    if (Context->Registers == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Context->RegisterLength = Memory->u.Memory.Length;
    Context->EnabledMask = 0;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2ReleaseController(
    _In_ WDFDEVICE Device,
    _In_ PVOID ContextPointer)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(Device);

    if (Context->Registers != NULL)
    {
        BrcmL2WriteRegister(Context, BRCM_L2_CPU_MASK_SET, BRCM_L2_ALL_LINES);
        MmUnmapIoSpace(Context->Registers, Context->RegisterLength);
        Context->Registers = NULL;
        Context->RegisterLength = 0;
        Context->EnabledMask = 0;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2StartController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN RestoreContext,
    _In_ WDF_POWER_DEVICE_STATE PreviousPowerState)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(PreviousPowerState);

    if (Context->Registers == NULL)
        return STATUS_DEVICE_NOT_READY;

    BrcmL2WriteRegister(Context, BRCM_L2_CPU_MASK_SET, BRCM_L2_ALL_LINES);
    if (!RestoreContext)
    {
        Context->EnabledMask = 0;
        BrcmL2WriteRegister(Context, BRCM_L2_CPU_CLEAR, BRCM_L2_ALL_LINES);
    }
    else if (Context->EnabledMask != 0)
    {
        BrcmL2WriteRegister(Context,
                            BRCM_L2_CPU_MASK_CLEAR,
                            Context->EnabledMask);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2StopController(
    _In_ PVOID ContextPointer,
    _In_ BOOLEAN SaveContext,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;

    UNREFERENCED_PARAMETER(TargetState);

    if (Context->Registers == NULL)
        return STATUS_DEVICE_NOT_READY;

    BrcmL2WriteRegister(Context, BRCM_L2_CPU_MASK_SET, BRCM_L2_ALL_LINES);
    if (!SaveContext)
        Context->EnabledMask = 0;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2QueryControllerBasicInformation(
    _In_ PVOID ContextPointer,
    _Out_ PCLIENT_CONTROLLER_BASIC_INFORMATION Information)
{
    UNREFERENCED_PARAMETER(ContextPointer);

    RtlZeroMemory(Information, sizeof(*Information));
    Information->Version = GPIO_CONTROLLER_BASIC_INFORMATION_VERSION;
    Information->Size = sizeof(*Information);
    Information->TotalPins = BRCM_L2_LINE_COUNT;
    Information->NumberOfPinsPerBank = BRCM_L2_LINE_COUNT;
    Information->Flags.MemoryMappedController = TRUE;
    Information->Flags.ActiveInterruptsAutoClearOnRead = FALSE;
    Information->Flags.FormatIoRequestsAsMasks = FALSE;
    Information->Flags.DeviceIdlePowerMgmtSupported = FALSE;
    Information->Flags.BankIdlePowerMgmtSupported = FALSE;
    Information->Flags.EmulateDebouncing = FALSE;
    Information->Flags.EmulateActiveBoth = FALSE;
    Information->Flags.IndependentIoHwSupported = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
BrcmL2ValidateInterruptConfiguration(
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    if (Parameters->InterruptMode != Latched ||
        Parameters->Polarity != InterruptRisingEdge)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if ((Parameters->PullConfiguration != GPIO_PIN_PULL_CONFIGURATION_DEFAULT &&
         Parameters->PullConfiguration != GPIO_PIN_PULL_CONFIGURATION_NONE) ||
        Parameters->DebounceTimeout != 0 ||
        Parameters->VendorDataLength != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2EnableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    ULONG Mask;
    NTSTATUS Status;

    Status = BrcmL2ValidateLine(Context, Parameters->BankId, Parameters->PinNumber);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = BrcmL2ValidateInterruptConfiguration(Parameters);
    if (!NT_SUCCESS(Status))
        return Status;

    Mask = 1UL << Parameters->PinNumber;
    GPIO_CLX_AcquireInterruptLock(ContextPointer, Parameters->BankId);
    Context->EnabledMask |= Mask;
    BrcmL2WriteRegister(Context, BRCM_L2_CPU_CLEAR, Mask);
    BrcmL2WriteRegister(Context, BRCM_L2_CPU_MASK_CLEAR, Mask);
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Parameters->BankId);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2DisableInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_DISABLE_INTERRUPT_PARAMETERS Parameters)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    ULONG Mask;
    NTSTATUS Status;

    Status = BrcmL2ValidateLine(Context, Parameters->BankId, Parameters->PinNumber);
    if (!NT_SUCCESS(Status))
        return Status;

    Mask = 1UL << Parameters->PinNumber;
    GPIO_CLX_AcquireInterruptLock(ContextPointer, Parameters->BankId);
    BrcmL2WriteRegister(Context, BRCM_L2_CPU_MASK_SET, Mask);
    BrcmL2WriteRegister(Context, BRCM_L2_CPU_CLEAR, Mask);
    Context->EnabledMask &= ~Mask;
    GPIO_CLX_ReleaseInterruptLock(ContextPointer, Parameters->BankId);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2UnmaskInterrupt(
    _In_ PVOID ContextPointer,
    _In_ PGPIO_ENABLE_INTERRUPT_PARAMETERS Parameters)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    ULONG Mask;
    NTSTATUS Status;

    Status = BrcmL2ValidateLine(Context, Parameters->BankId, Parameters->PinNumber);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = BrcmL2ValidateInterruptConfiguration(Parameters);
    if (!NT_SUCCESS(Status))
        return Status;

    Mask = 1UL << Parameters->PinNumber;
    if ((Context->EnabledMask & Mask) == 0)
        return STATUS_INVALID_DEVICE_STATE;

    BrcmL2WriteRegister(Context, BRCM_L2_CPU_MASK_CLEAR, Mask);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2MaskInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_MASK_INTERRUPT_PARAMETERS Parameters)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    ULONG Mask;

    if (Context->Registers == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (Parameters->BankId != 0)
    {
        Parameters->FailedMask = Parameters->PinMask;
        return STATUS_INVALID_PARAMETER;
    }

    Mask = (ULONG)Parameters->PinMask;
    Parameters->FailedMask = Parameters->PinMask & ~((ULONG64)BRCM_L2_ALL_LINES);
    if (Mask != 0)
        BrcmL2WriteRegister(Context, BRCM_L2_CPU_MASK_SET, Mask);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2QueryActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    ULONG Status;
    ULONG Masked;

    if (Context->Registers == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (Parameters->BankId != 0)
        return STATUS_INVALID_PARAMETER;

    Status = BrcmL2ReadRegister(Context, BRCM_L2_CPU_STATUS);
    Masked = BrcmL2ReadRegister(Context, BRCM_L2_CPU_MASK_STATUS);
    Parameters->ActiveMask = Status & ~Masked & Context->EnabledMask &
                             (ULONG)Parameters->EnabledMask;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2ClearActiveInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_CLEAR_ACTIVE_INTERRUPTS_PARAMETERS Parameters)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    ULONG Mask;

    if (Context->Registers == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (Parameters->BankId != 0)
    {
        Parameters->FailedClearMask = Parameters->ClearActiveMask;
        return STATUS_INVALID_PARAMETER;
    }

    Mask = (ULONG)Parameters->ClearActiveMask;
    Parameters->FailedClearMask =
        Parameters->ClearActiveMask & ~((ULONG64)BRCM_L2_ALL_LINES);
    if (Mask != 0)
        BrcmL2WriteRegister(Context, BRCM_L2_CPU_CLEAR, Mask);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2QueryEnabledInterrupts(
    _In_ PVOID ContextPointer,
    _Inout_ PGPIO_QUERY_ENABLED_INTERRUPTS_PARAMETERS Parameters)
{
    PBRCM_L2_CONTEXT Context = ContextPointer;
    ULONG Masked;

    if (Context->Registers == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (Parameters->BankId != 0)
        return STATUS_INVALID_PARAMETER;

    Masked = BrcmL2ReadRegister(Context, BRCM_L2_CPU_MASK_STATUS);
    Parameters->EnabledMask = Context->EnabledMask & ~Masked;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
BrcmL2EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES Attributes;
    WDFDEVICE Device;
    NTSTATUS Status;

    Status = GPIO_CLX_ProcessAddDevicePreDeviceCreate(Driver,
                                                       DeviceInit,
                                                       &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WdfDeviceCreate(&DeviceInit, &Attributes, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    return GPIO_CLX_ProcessAddDevicePostDeviceCreate(Driver, Device);
}

static
VOID
NTAPI
BrcmL2EvtDriverUnload(
    _In_ WDFDRIVER Driver)
{
    GPIO_CLX_UnregisterClient(Driver);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    GPIO_CLIENT_REGISTRATION_PACKET Packet;
    WDF_DRIVER_CONFIG Config;
    WDFDRIVER Driver;
    NTSTATUS Status;

    WDF_DRIVER_CONFIG_INIT(&Config, BrcmL2EvtDeviceAdd);
    Config.DriverPoolTag = BRCM_L2_TAG;
    Config.EvtDriverUnload = BrcmL2EvtDriverUnload;

    Status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             WDF_NO_OBJECT_ATTRIBUTES,
                             &Config,
                             &Driver);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Packet, sizeof(Packet));
    Packet.Version = GPIO_CLIENT_VERSION;
    Packet.Size = sizeof(Packet);
    Packet.Flags = GPIO_CLIENT_REGISTRATION_FLAGS_NONE;
    Packet.ControllerContextSize = sizeof(BRCM_L2_CONTEXT);
    Packet.CLIENT_PrepareController = BrcmL2PrepareController;
    Packet.CLIENT_ReleaseController = BrcmL2ReleaseController;
    Packet.CLIENT_StartController = BrcmL2StartController;
    Packet.CLIENT_StopController = BrcmL2StopController;
    Packet.CLIENT_QueryControllerBasicInformation =
        BrcmL2QueryControllerBasicInformation;
    Packet.CLIENT_EnableInterrupt = BrcmL2EnableInterrupt;
    Packet.CLIENT_DisableInterrupt = BrcmL2DisableInterrupt;
    Packet.CLIENT_UnmaskInterrupt = BrcmL2UnmaskInterrupt;
    Packet.CLIENT_MaskInterrupts = BrcmL2MaskInterrupts;
    Packet.CLIENT_QueryActiveInterrupts = BrcmL2QueryActiveInterrupts;
    Packet.CLIENT_ClearActiveInterrupts = BrcmL2ClearActiveInterrupts;
    Packet.CLIENT_QueryEnabledInterrupts = BrcmL2QueryEnabledInterrupts;

    return GPIO_CLX_RegisterClient(Driver, &Packet, RegistryPath);
}
