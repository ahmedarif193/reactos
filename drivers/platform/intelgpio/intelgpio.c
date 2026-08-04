/*
 * PROJECT:     ReactOS Intel GPIO Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Alder Lake-N GPIO and pin configuration controller
 */

#include <ntddk.h>
#include <initguid.h>
#include <reactos/drivers/intelgpio.h>

#define NDEBUG
#include <debug.h>

#define INTELGPIO_TAG 'oGpI'
#define INTELGPIO_COMMUNITY_COUNT 4
#define INTELGPIO_GROUP_COUNT 13
#define INTELGPIO_GPIO_COUNT 360
#define INTELGPIO_PENDING_WORD_COUNT ((INTELGPIO_GPIO_COUNT + 31) / 32)
#define INTELGPIO_MAXIMUM_WAIT_MS 180000

#define INTELGPIO_REVID 0x000
#define INTELGPIO_PADBAR 0x00c
#define INTELGPIO_PAD_OWN 0x020
#define INTELGPIO_PADCFGLOCK 0x080
#define INTELGPIO_HOSTSW_OWN 0x0b0
#define INTELGPIO_GPI_IS 0x100
#define INTELGPIO_GPI_IE 0x120

#define INTELGPIO_PADCFG0_RXEVCFG_MASK 0x06000000
#define INTELGPIO_PADCFG0_RXEVCFG_LEVEL 0x00000000
#define INTELGPIO_PADCFG0_RXEVCFG_EDGE 0x02000000
#define INTELGPIO_PADCFG0_RXEVCFG_EDGE_BOTH 0x06000000
#define INTELGPIO_PADCFG0_PREGFRXSEL 0x01000000
#define INTELGPIO_PADCFG0_RXINV 0x00800000
#define INTELGPIO_PADCFG0_ROUTE_IOAPIC 0x00100000
#define INTELGPIO_PADCFG0_ROUTE_MASK 0x001e0000
#define INTELGPIO_PADCFG0_PMODE_MASK 0x00003c00
#define INTELGPIO_PADCFG0_RXDIS 0x00000200
#define INTELGPIO_PADCFG0_TXDIS 0x00000100
#define INTELGPIO_PADCFG0_RXSTATE 0x00000002
#define INTELGPIO_PADCFG0_TXSTATE 0x00000001

#define INTELGPIO_PADCFG1_TERM_UP 0x00002000
#define INTELGPIO_PADCFG1_TERM_MASK 0x00001c00
#define INTELGPIO_PADCFG1_TERM_20K 0x00001000

#define INTELGPIO_PADCFG2_DEBOUNCE_MASK 0x0000001e
#define INTELGPIO_PADCFG2_DEBOUNCE_ENABLE 0x00000001

typedef struct _INTELGPIO_GROUP
{
    UCHAR Community;
    UCHAR RegisterNumber;
    UCHAR PadOwnNumber;
    UCHAR Reserved;
    USHORT FirstPin;
    USHORT PinCount;
    LONG GpioBase;
} INTELGPIO_GROUP;

typedef struct _INTELGPIO_COMMUNITY
{
    PVOID RegisterBase;
    PVOID PadBase;
    ULONG RegisterLength;
    ULONG Revision;
    ULONG PinBase;
    ULONG PinCount;
    ULONG PadStride;
} INTELGPIO_COMMUNITY, *PINTELGPIO_COMMUNITY;

typedef struct _INTELGPIO_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    KSPIN_LOCK RegisterLock;
    UNICODE_STRING InterfaceName;
    INTELGPIO_COMMUNITY Communities[INTELGPIO_COMMUNITY_COUNT];
    PKINTERRUPT InterruptObject;
    KEVENT InterruptEvent;
    KDPC InterruptDpc;
    volatile LONG PendingInterrupts[INTELGPIO_PENDING_WORD_COUNT];
    ULONG EnabledInterrupts[INTELGPIO_GROUP_COUNT];
    BOOLEAN InterruptConnected;
    BOOLEAN Started;
} INTELGPIO_DEVICE_EXTENSION, *PINTELGPIO_DEVICE_EXTENSION;

static const INTELGPIO_GROUP IntelGpioGroups[] =
{
    {0, 0, 0, 0,   0, 26,   0},
    {0, 1, 4, 0,  26, 16,  32},
    {0, 2, 6, 0,  42, 25,  64},
    {1, 0, 0, 0,  67,  8,  96},
    {1, 1, 1, 0,  75, 20, 128},
    {1, 2, 4, 0,  95, 24, 160},
    {1, 3, 7, 0, 119, 21, 192},
    {1, 4, 10, 0, 140, 29, 224},
    {2, 0, 0, 0, 169, 24, 256},
    {2, 1, 3, 0, 193, 25, 288},
    {2, 2, 7, 0, 218,  6,  -1},
    {2, 3, 8, 0, 224, 25, 320},
    {3, 0, 0, 0, 249,  8, 352}
};

static const ULONG IntelGpioCommunityPinBase[INTELGPIO_COMMUNITY_COUNT] = {0, 67, 169, 249};
static const ULONG IntelGpioCommunityPinCount[INTELGPIO_COMMUNITY_COUNT] = {67, 102, 80, 8};

static
ULONG
IntelGpioRead32(
    _In_ PINTELGPIO_COMMUNITY Community,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Community->RegisterBase + Offset));
}

static
BOOLEAN
IntelGpioResolvePin(
    _In_ ULONG GpioNumber,
    _Out_ const INTELGPIO_GROUP **Group,
    _Out_ PULONG PinNumber)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(IntelGpioGroups); Index++)
    {
        if (IntelGpioGroups[Index].GpioBase >= 0 && GpioNumber >= (ULONG)IntelGpioGroups[Index].GpioBase && GpioNumber < (ULONG)IntelGpioGroups[Index].GpioBase + IntelGpioGroups[Index].PinCount)
        {
            *Group = &IntelGpioGroups[Index];
            *PinNumber = IntelGpioGroups[Index].FirstPin + GpioNumber - IntelGpioGroups[Index].GpioBase;
            return TRUE;
        }
    }
    return FALSE;
}

static
PULONG
IntelGpioGetPadRegister(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG PinNumber,
    _In_ ULONG RegisterOffset)
{
    const INTELGPIO_GROUP *Group;
    PINTELGPIO_COMMUNITY Community;
    ULONG PadNumber;
    ULONG Offset;
    ULONG HardwarePin;

    if (!IntelGpioResolvePin(PinNumber, &Group, &HardwarePin))
        return NULL;
    Community = &DeviceExtension->Communities[Group->Community];
    if (!Community->PadBase || RegisterOffset >= Community->PadStride)
        return NULL;
    PadNumber = HardwarePin - Community->PinBase;
    Offset = (ULONG)((PUCHAR)Community->PadBase - (PUCHAR)Community->RegisterBase) + PadNumber * Community->PadStride + RegisterOffset;
    if (Offset > Community->RegisterLength || sizeof(ULONG) > Community->RegisterLength - Offset)
        return NULL;
    return (PULONG)((PUCHAR)Community->RegisterBase + Offset);
}

static
BOOLEAN
IntelGpioIsHostOwned(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG PinNumber)
{
    const INTELGPIO_GROUP *Group;
    PINTELGPIO_COMMUNITY Community;
    ULONG GroupOffset;
    ULONG Value;
    ULONG Shift;
    ULONG HardwarePin;

    if (!IntelGpioResolvePin(PinNumber, &Group, &HardwarePin))
        return FALSE;
    Community = &DeviceExtension->Communities[Group->Community];
    if (Community->Revision >= 0x110)
    {
        Value = IntelGpioRead32(Community, INTELGPIO_PAD_OWN + (HardwarePin - Community->PinBase) * sizeof(ULONG));
        return (Value & 7) == 0;
    }
    GroupOffset = HardwarePin - Group->FirstPin;
    Value = IntelGpioRead32(Community, INTELGPIO_PAD_OWN + Group->PadOwnNumber * sizeof(ULONG) + (GroupOffset / 8) * sizeof(ULONG));
    Shift = (GroupOffset % 8) * 4;
    return (Value & (0xfUL << Shift)) == 0;
}

static
BOOLEAN
IntelGpioIsAcpiMode(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG PinNumber)
{
    const INTELGPIO_GROUP *Group;
    PINTELGPIO_COMMUNITY Community;
    ULONG Value;
    ULONG GroupOffset;
    ULONG HardwarePin;

    if (!IntelGpioResolvePin(PinNumber, &Group, &HardwarePin))
        return TRUE;
    Community = &DeviceExtension->Communities[Group->Community];
    GroupOffset = HardwarePin - Group->FirstPin;
    Value = IntelGpioRead32(Community, INTELGPIO_HOSTSW_OWN + Group->RegisterNumber * sizeof(ULONG));
    return (Value & (1UL << GroupOffset)) == 0;
}

static
ULONG
IntelGpioGetLockState(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG PinNumber)
{
    const INTELGPIO_GROUP *Group;
    PINTELGPIO_COMMUNITY Community;
    ULONG GroupOffset;
    ULONG State = 0;
    ULONG HardwarePin;

    if (!IntelGpioResolvePin(PinNumber, &Group, &HardwarePin))
        return INTELGPIO_PIN_STATE_CONFIG_LOCKED | INTELGPIO_PIN_STATE_TX_LOCKED;
    Community = &DeviceExtension->Communities[Group->Community];
    GroupOffset = HardwarePin - Group->FirstPin;
    if (IntelGpioRead32(Community, INTELGPIO_PADCFGLOCK + Group->RegisterNumber * 8) & (1UL << GroupOffset))
        State |= INTELGPIO_PIN_STATE_CONFIG_LOCKED;
    if (IntelGpioRead32(Community, INTELGPIO_PADCFGLOCK + Group->RegisterNumber * 8 + sizeof(ULONG)) & (1UL << GroupOffset))
        State |= INTELGPIO_PIN_STATE_TX_LOCKED;
    return State;
}

static
BOOLEAN
IntelGpioClaimPendingInterrupt(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG RequestedPin,
    _Out_ PULONG TriggeredPin)
{
    ULONG FirstWord = 0;
    ULONG LastWord = INTELGPIO_PENDING_WORD_COUNT;
    ULONG WordIndex;

    if (RequestedPin != INTELGPIO_ANY_PIN)
    {
        FirstWord = RequestedPin / 32;
        LastWord = FirstWord + 1;
    }
    for (WordIndex = FirstWord; WordIndex < LastWord; WordIndex++)
    {
        ULONG BitStart = RequestedPin == INTELGPIO_ANY_PIN ? 0 : RequestedPin % 32;
        ULONG BitEnd = RequestedPin == INTELGPIO_ANY_PIN ? 32 : BitStart + 1;
        ULONG Bit;

        for (Bit = BitStart; Bit < BitEnd; Bit++)
        {
            LONG Mask = (LONG)(1UL << Bit);
            LONG OldValue;
            LONG NewValue;

            do
            {
                OldValue = DeviceExtension->PendingInterrupts[WordIndex];
                if (!(OldValue & Mask))
                    break;
                NewValue = OldValue & ~Mask;
            } while (InterlockedCompareExchange(&DeviceExtension->PendingInterrupts[WordIndex], NewValue, OldValue) != OldValue);
            if (OldValue & Mask)
            {
                *TriggeredPin = WordIndex * 32 + Bit;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static
VOID
NTAPI
IntelGpioInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PINTELGPIO_DEVICE_EXTENSION DeviceExtension = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    KeSetEvent(&DeviceExtension->InterruptEvent, IO_NO_INCREMENT, FALSE);
}

static
BOOLEAN
NTAPI
IntelGpioInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PINTELGPIO_DEVICE_EXTENSION DeviceExtension = ServiceContext;
    BOOLEAN Handled = FALSE;
    ULONG GroupIndex;

    UNREFERENCED_PARAMETER(Interrupt);
    for (GroupIndex = 0; GroupIndex < INTELGPIO_GROUP_COUNT; GroupIndex++)
    {
        const INTELGPIO_GROUP *Group = &IntelGpioGroups[GroupIndex];
        PINTELGPIO_COMMUNITY Community = &DeviceExtension->Communities[Group->Community];
        ULONG Pending;
        ULONG Bit;

        if (!Community->RegisterBase || !DeviceExtension->EnabledInterrupts[GroupIndex])
            continue;
        Pending = IntelGpioRead32(Community, INTELGPIO_GPI_IS + Group->RegisterNumber * sizeof(ULONG));
        Pending &= IntelGpioRead32(Community, INTELGPIO_GPI_IE + Group->RegisterNumber * sizeof(ULONG));
        Pending &= DeviceExtension->EnabledInterrupts[GroupIndex];
        if (!Pending)
            continue;
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Community->RegisterBase + INTELGPIO_GPI_IS + Group->RegisterNumber * sizeof(ULONG)), Pending);
        for (Bit = 0; Bit < Group->PinCount; Bit++)
        {
            ULONG GpioNumber;

            if (!(Pending & (1UL << Bit)))
                continue;
            GpioNumber = Group->GpioBase + Bit;
            InterlockedOr(&DeviceExtension->PendingInterrupts[GpioNumber / 32], (LONG)(1UL << (GpioNumber % 32)));
        }
        Handled = TRUE;
    }
    if (Handled)
        KeInsertQueueDpc(&DeviceExtension->InterruptDpc, NULL, NULL);
    return Handled;
}

static
NTSTATUS
IntelGpioConfigureInterrupt(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ PINTELGPIO_INTERRUPT_CONFIGURATION Configuration)
{
    const INTELGPIO_GROUP *Group;
    PINTELGPIO_COMMUNITY Community;
    PULONG PadConfig0;
    ULONG HardwarePin;
    ULONG GroupIndex;
    ULONG GroupOffset;
    ULONG Mask;
    ULONG InterruptEnable;
    ULONG Value;
    KIRQL OldIrql;

    if (Configuration->Mode > IntelGpioInterruptEdgeBoth || !IntelGpioResolvePin(Configuration->PinNumber, &Group, &HardwarePin))
        return STATUS_INVALID_PARAMETER;
    if (!DeviceExtension->InterruptConnected && Configuration->Mode != IntelGpioInterruptDisabled)
        return STATUS_NOT_SUPPORTED;
    if (!IntelGpioIsHostOwned(DeviceExtension, Configuration->PinNumber))
        return STATUS_ACCESS_DENIED;
    GroupIndex = (ULONG)(Group - IntelGpioGroups);
    GroupOffset = HardwarePin - Group->FirstPin;
    Mask = 1UL << GroupOffset;
    Community = &DeviceExtension->Communities[Group->Community];
    PadConfig0 = IntelGpioGetPadRegister(DeviceExtension, Configuration->PinNumber, 0);
    if (!PadConfig0)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&DeviceExtension->RegisterLock, &OldIrql);
    InterruptEnable = IntelGpioRead32(Community, INTELGPIO_GPI_IE + Group->RegisterNumber * sizeof(ULONG));
    InterruptEnable &= ~Mask;
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Community->RegisterBase + INTELGPIO_GPI_IE + Group->RegisterNumber * sizeof(ULONG)), InterruptEnable);
    DeviceExtension->EnabledInterrupts[GroupIndex] &= ~Mask;
    if (Configuration->Mode == IntelGpioInterruptDisabled)
    {
        if (!(IntelGpioGetLockState(DeviceExtension, Configuration->PinNumber) & INTELGPIO_PIN_STATE_CONFIG_LOCKED))
        {
            Value = READ_REGISTER_ULONG(PadConfig0);
            Value &= ~INTELGPIO_PADCFG0_ROUTE_IOAPIC;
            WRITE_REGISTER_ULONG(PadConfig0, Value);
        }
        KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
        return STATUS_SUCCESS;
    }
    if (IntelGpioIsAcpiMode(DeviceExtension, Configuration->PinNumber) || (IntelGpioGetLockState(DeviceExtension, Configuration->PinNumber) & INTELGPIO_PIN_STATE_CONFIG_LOCKED))
    {
        KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
        return STATUS_ACCESS_DENIED;
    }

    Value = READ_REGISTER_ULONG(PadConfig0);
    Value &= ~(INTELGPIO_PADCFG0_PMODE_MASK | INTELGPIO_PADCFG0_RXDIS | INTELGPIO_PADCFG0_ROUTE_MASK | INTELGPIO_PADCFG0_RXEVCFG_MASK | INTELGPIO_PADCFG0_RXINV);
    Value |= INTELGPIO_PADCFG0_ROUTE_IOAPIC;
    if (Configuration->Mode == IntelGpioInterruptLevelLow || Configuration->Mode == IntelGpioInterruptEdgeFalling)
        Value |= INTELGPIO_PADCFG0_RXINV;
    if (Configuration->Mode == IntelGpioInterruptEdgeRising || Configuration->Mode == IntelGpioInterruptEdgeFalling)
        Value |= INTELGPIO_PADCFG0_RXEVCFG_EDGE;
    else if (Configuration->Mode == IntelGpioInterruptEdgeBoth)
        Value |= INTELGPIO_PADCFG0_RXEVCFG_EDGE_BOTH;
    else
        Value |= INTELGPIO_PADCFG0_RXEVCFG_LEVEL;
    WRITE_REGISTER_ULONG(PadConfig0, Value);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Community->RegisterBase + INTELGPIO_GPI_IS + Group->RegisterNumber * sizeof(ULONG)), Mask);
    DeviceExtension->EnabledInterrupts[GroupIndex] |= Mask;
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Community->RegisterBase + INTELGPIO_GPI_IE + Group->RegisterNumber * sizeof(ULONG)), InterruptEnable | Mask);
    KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntelGpioWaitInterrupt(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PINTELGPIO_INTERRUPT_WAIT Wait)
{
    const INTELGPIO_GROUP *Group;
    LARGE_INTEGER Timeout;
    PLARGE_INTEGER TimeoutPointer;
    ULONG HardwarePin;
    NTSTATUS Status;

    if (Wait->PinNumber != INTELGPIO_ANY_PIN && !IntelGpioResolvePin(Wait->PinNumber, &Group, &HardwarePin))
        return STATUS_INVALID_PARAMETER;
    if (!DeviceExtension->InterruptConnected)
        return STATUS_NOT_SUPPORTED;
    if (Wait->TimeoutMilliseconds > INTELGPIO_MAXIMUM_WAIT_MS)
        return STATUS_INVALID_PARAMETER;
    if (IntelGpioClaimPendingInterrupt(DeviceExtension, Wait->PinNumber, &Wait->TriggeredPin))
        return STATUS_SUCCESS;
    if (!Wait->TimeoutMilliseconds)
        return STATUS_TIMEOUT;
    if (KeGetCurrentIrql() > APC_LEVEL)
        return STATUS_INVALID_DEVICE_STATE;
    Timeout.QuadPart = -((LONGLONG)Wait->TimeoutMilliseconds * 10000);
    TimeoutPointer = &Timeout;
    Status = KeWaitForSingleObject(&DeviceExtension->InterruptEvent, Executive, KernelMode, FALSE, TimeoutPointer);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!DeviceExtension->Started)
        return STATUS_DEVICE_NOT_READY;
    return IntelGpioClaimPendingInterrupt(DeviceExtension, Wait->PinNumber, &Wait->TriggeredPin) ? STATUS_SUCCESS : STATUS_RETRY;
}

static
NTSTATUS
IntelGpioQueryPin(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PINTELGPIO_PIN_INFORMATION Information)
{
    PULONG PadConfig0 = IntelGpioGetPadRegister(DeviceExtension, Information->PinNumber, 0);
    PULONG PadConfig1 = IntelGpioGetPadRegister(DeviceExtension, Information->PinNumber, 4);
    PULONG PadConfig2 = IntelGpioGetPadRegister(DeviceExtension, Information->PinNumber, 8);
    KIRQL OldIrql;
    ULONG State = 0;

    if (!PadConfig0 || !PadConfig1)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&DeviceExtension->RegisterLock, &OldIrql);
    Information->PadConfiguration0 = READ_REGISTER_ULONG(PadConfig0);
    Information->PadConfiguration1 = READ_REGISTER_ULONG(PadConfig1);
    Information->PadConfiguration2 = PadConfig2 ? READ_REGISTER_ULONG(PadConfig2) : 0;
    if (IntelGpioIsHostOwned(DeviceExtension, Information->PinNumber))
        State |= INTELGPIO_PIN_STATE_HOST_OWNED;
    if (IntelGpioIsAcpiMode(DeviceExtension, Information->PinNumber))
        State |= INTELGPIO_PIN_STATE_ACPI_MODE;
    State |= IntelGpioGetLockState(DeviceExtension, Information->PinNumber);
    if (!(Information->PadConfiguration0 & INTELGPIO_PADCFG0_PMODE_MASK))
        State |= INTELGPIO_PIN_STATE_GPIO_MODE;
    if (!(Information->PadConfiguration0 & INTELGPIO_PADCFG0_RXDIS))
        State |= INTELGPIO_PIN_STATE_INPUT_ENABLED;
    if (!(Information->PadConfiguration0 & INTELGPIO_PADCFG0_TXDIS))
        State |= INTELGPIO_PIN_STATE_OUTPUT_ENABLED;
    Information->State = State;
    Information->Value = (State & INTELGPIO_PIN_STATE_OUTPUT_ENABLED) ? !!(Information->PadConfiguration0 & INTELGPIO_PADCFG0_TXSTATE) : !!(Information->PadConfiguration0 & INTELGPIO_PADCFG0_RXSTATE);
    KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntelGpioConfigurePin(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ PINTELGPIO_PIN_CONFIGURATION Configuration)
{
    PULONG PadConfig0 = IntelGpioGetPadRegister(DeviceExtension, Configuration->PinNumber, 0);
    PULONG PadConfig1 = IntelGpioGetPadRegister(DeviceExtension, Configuration->PinNumber, 4);
    PULONG PadConfig2 = IntelGpioGetPadRegister(DeviceExtension, Configuration->PinNumber, 8);
    KIRQL OldIrql;
    ULONG Value0;
    ULONG Value1;
    ULONG Value2;
    ULONG LockState;

    if (!PadConfig0 || !PadConfig1 || Configuration->Direction < IntelGpioDirectionInput || Configuration->Direction > IntelGpioDirectionInputOutput)
        return STATUS_INVALID_PARAMETER;
    if (Configuration->PullConfiguration > IntelGpioPullDown20K)
        return STATUS_INVALID_PARAMETER;
    if (Configuration->DebounceExponent != INTELGPIO_DEBOUNCE_PRESERVE && Configuration->DebounceExponent > 15)
        return STATUS_INVALID_PARAMETER;
    if (Configuration->DebounceExponent != INTELGPIO_DEBOUNCE_PRESERVE && !PadConfig2)
        return STATUS_INVALID_PARAMETER;
    if (!IntelGpioIsHostOwned(DeviceExtension, Configuration->PinNumber))
        return STATUS_ACCESS_DENIED;
    LockState = IntelGpioGetLockState(DeviceExtension, Configuration->PinNumber);
    if (LockState & INTELGPIO_PIN_STATE_CONFIG_LOCKED)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if ((Configuration->Direction & IntelGpioDirectionOutput) && (LockState & INTELGPIO_PIN_STATE_TX_LOCKED))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    KeAcquireSpinLock(&DeviceExtension->RegisterLock, &OldIrql);
    Value0 = READ_REGISTER_ULONG(PadConfig0);
    if (Configuration->InitialValue)
        Value0 |= INTELGPIO_PADCFG0_TXSTATE;
    else
        Value0 &= ~INTELGPIO_PADCFG0_TXSTATE;
    Value0 &= ~(INTELGPIO_PADCFG0_PMODE_MASK | INTELGPIO_PADCFG0_ROUTE_MASK | INTELGPIO_PADCFG0_RXEVCFG_MASK);
    if (Configuration->Direction & IntelGpioDirectionInput)
        Value0 &= ~INTELGPIO_PADCFG0_RXDIS;
    else
        Value0 |= INTELGPIO_PADCFG0_RXDIS;
    if (Configuration->Direction & IntelGpioDirectionOutput)
        Value0 &= ~INTELGPIO_PADCFG0_TXDIS;
    else
        Value0 |= INTELGPIO_PADCFG0_TXDIS;
    if (Configuration->DebounceExponent != INTELGPIO_DEBOUNCE_PRESERVE)
    {
        if (Configuration->DebounceExponent)
            Value0 |= INTELGPIO_PADCFG0_PREGFRXSEL;
        else
            Value0 &= ~INTELGPIO_PADCFG0_PREGFRXSEL;
    }
    WRITE_REGISTER_ULONG(PadConfig0, Value0);

    if (Configuration->PullConfiguration != IntelGpioPullPreserve)
    {
        Value1 = READ_REGISTER_ULONG(PadConfig1);
        Value1 &= ~(INTELGPIO_PADCFG1_TERM_UP | INTELGPIO_PADCFG1_TERM_MASK);
        if (Configuration->PullConfiguration == IntelGpioPullUp20K)
            Value1 |= INTELGPIO_PADCFG1_TERM_UP | INTELGPIO_PADCFG1_TERM_20K;
        else if (Configuration->PullConfiguration == IntelGpioPullDown20K)
            Value1 |= INTELGPIO_PADCFG1_TERM_20K;
        WRITE_REGISTER_ULONG(PadConfig1, Value1);
    }
    if (Configuration->DebounceExponent != INTELGPIO_DEBOUNCE_PRESERVE)
    {
        Value2 = READ_REGISTER_ULONG(PadConfig2);
        Value2 &= ~(INTELGPIO_PADCFG2_DEBOUNCE_MASK | INTELGPIO_PADCFG2_DEBOUNCE_ENABLE);
        if (Configuration->DebounceExponent)
            Value2 |= (Configuration->DebounceExponent << 1) | INTELGPIO_PADCFG2_DEBOUNCE_ENABLE;
        WRITE_REGISTER_ULONG(PadConfig2, Value2);
    }
    KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
IntelGpioWritePin(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ PINTELGPIO_PIN_WRITE Write)
{
    PULONG PadConfig0 = IntelGpioGetPadRegister(DeviceExtension, Write->PinNumber, 0);
    KIRQL OldIrql;
    ULONG Value;

    if (!PadConfig0)
        return STATUS_INVALID_PARAMETER;
    if (!IntelGpioIsHostOwned(DeviceExtension, Write->PinNumber) || (IntelGpioGetLockState(DeviceExtension, Write->PinNumber) & INTELGPIO_PIN_STATE_TX_LOCKED))
        return STATUS_ACCESS_DENIED;
    KeAcquireSpinLock(&DeviceExtension->RegisterLock, &OldIrql);
    Value = READ_REGISTER_ULONG(PadConfig0);
    if ((Value & INTELGPIO_PADCFG0_PMODE_MASK) || (Value & INTELGPIO_PADCFG0_TXDIS))
    {
        KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (Write->Value)
        Value |= INTELGPIO_PADCFG0_TXSTATE;
    else
        Value &= ~INTELGPIO_PADCFG0_TXSTATE;
    WRITE_REGISTER_ULONG(PadConfig0, Value);
    KeReleaseSpinLock(&DeviceExtension->RegisterLock, OldIrql);
    return STATUS_SUCCESS;
}

static
VOID
IntelGpioUnmapCommunities(
    _Inout_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    DeviceExtension->Started = FALSE;
    KeSetEvent(&DeviceExtension->InterruptEvent, IO_NO_INCREMENT, FALSE);
    for (Index = 0; Index < INTELGPIO_GROUP_COUNT; Index++)
    {
        const INTELGPIO_GROUP *Group = &IntelGpioGroups[Index];
        PINTELGPIO_COMMUNITY Community = &DeviceExtension->Communities[Group->Community];
        ULONG InterruptEnable;

        if (!Community->RegisterBase || !DeviceExtension->EnabledInterrupts[Index])
            continue;
        InterruptEnable = IntelGpioRead32(Community, INTELGPIO_GPI_IE + Group->RegisterNumber * sizeof(ULONG));
        InterruptEnable &= ~DeviceExtension->EnabledInterrupts[Index];
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Community->RegisterBase + INTELGPIO_GPI_IE + Group->RegisterNumber * sizeof(ULONG)), InterruptEnable);
        DeviceExtension->EnabledInterrupts[Index] = 0;
    }
    if (DeviceExtension->InterruptConnected)
    {
        IoDisconnectInterrupt(DeviceExtension->InterruptObject);
        DeviceExtension->InterruptObject = NULL;
        DeviceExtension->InterruptConnected = FALSE;
    }
    for (Index = 0; Index < INTELGPIO_COMMUNITY_COUNT; Index++)
    {
        if (DeviceExtension->Communities[Index].RegisterBase)
            MmUnmapIoSpace(DeviceExtension->Communities[Index].RegisterBase, DeviceExtension->Communities[Index].RegisterLength);
        RtlZeroMemory(&DeviceExtension->Communities[Index], sizeof(DeviceExtension->Communities[Index]));
    }
}

static
NTSTATUS
IntelGpioStartHardware(
    _Inout_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR InterruptDescriptor = NULL;
    ULONG ResourceIndex;
    ULONG CommunityIndex = 0;
    NTSTATUS Status;

    if (!Resources || !Resources->Count)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (DeviceExtension->Started)
        IntelGpioUnmapCommunities(DeviceExtension);
    KeResetEvent(&DeviceExtension->InterruptEvent);
    RtlZeroMemory((PVOID)DeviceExtension->PendingInterrupts, sizeof(DeviceExtension->PendingInterrupts));
    PartialList = &Resources->List[0].PartialResourceList;
    for (ResourceIndex = 0; ResourceIndex < PartialList->Count; ResourceIndex++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = &PartialList->PartialDescriptors[ResourceIndex];
        PINTELGPIO_COMMUNITY Community;
        ULONG RevisionValue;
        ULONG PadBar;

        if (Descriptor->Type == CmResourceTypeInterrupt && !InterruptDescriptor)
        {
            InterruptDescriptor = Descriptor;
            continue;
        }
        if (Descriptor->Type != CmResourceTypeMemory || CommunityIndex >= INTELGPIO_COMMUNITY_COUNT)
            continue;
        Community = &DeviceExtension->Communities[CommunityIndex];
        Community->RegisterLength = Descriptor->u.Memory.Length;
        if (Community->RegisterLength < INTELGPIO_GPI_IE + 5 * sizeof(ULONG))
            break;
        Community->RegisterBase = MmMapIoSpace(Descriptor->u.Memory.Start, Descriptor->u.Memory.Length, MmNonCached);
        if (!Community->RegisterBase)
            break;
        RevisionValue = IntelGpioRead32(Community, INTELGPIO_REVID);
        if (RevisionValue == MAXULONG)
            break;
        Community->Revision = RevisionValue >> 16;
        Community->PinBase = IntelGpioCommunityPinBase[CommunityIndex];
        Community->PinCount = IntelGpioCommunityPinCount[CommunityIndex];
        Community->PadStride = Community->Revision >= 0x92 ? 16 : 8;
        PadBar = IntelGpioRead32(Community, INTELGPIO_PADBAR);
        if (PadBar >= Community->RegisterLength || Community->PinCount * Community->PadStride > Community->RegisterLength - PadBar)
            break;
        Community->PadBase = (PUCHAR)Community->RegisterBase + PadBar;
        DPRINT1("INTELGPIO: community %lu base=%p revision=0x%lx padbar=0x%lx\n", CommunityIndex, Community->RegisterBase, Community->Revision, PadBar);
        CommunityIndex++;
    }
    if (CommunityIndex != INTELGPIO_COMMUNITY_COUNT)
    {
        IntelGpioUnmapCommunities(DeviceExtension);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    DeviceExtension->Started = TRUE;
    if (InterruptDescriptor)
    {
        Status = IoConnectInterrupt(&DeviceExtension->InterruptObject, IntelGpioInterruptService, DeviceExtension, NULL, InterruptDescriptor->u.Interrupt.Vector, (KIRQL)InterruptDescriptor->u.Interrupt.Level, (KIRQL)InterruptDescriptor->u.Interrupt.Level, (InterruptDescriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive, InterruptDescriptor->ShareDisposition == CmResourceShareShared, InterruptDescriptor->u.Interrupt.Affinity, FALSE);
        if (!NT_SUCCESS(Status))
        {
            IntelGpioUnmapCommunities(DeviceExtension);
            return Status;
        }
        DeviceExtension->InterruptConnected = TRUE;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
IntelGpioForwardSynchronously(
    _In_ PINTELGPIO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, IntelGpioCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
NTAPI
IntelGpioCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelGpioDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELGPIO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;
    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        goto Complete;
    if (!DeviceExtension->Started)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Release;
    }
    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTELGPIO_QUERY_PIN:
            if (!Buffer || InputLength < sizeof(INTELGPIO_PIN_INFORMATION) || OutputLength < sizeof(INTELGPIO_PIN_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (((PINTELGPIO_PIN_INFORMATION)Buffer)->Version != INTELGPIO_INTERFACE_VERSION)
                Status = STATUS_REVISION_MISMATCH;
            else
            {
                Status = IntelGpioQueryPin(DeviceExtension, Buffer);
                if (NT_SUCCESS(Status))
                    Irp->IoStatus.Information = sizeof(INTELGPIO_PIN_INFORMATION);
            }
            break;

        case IOCTL_INTELGPIO_CONFIGURE_PIN:
            if (!Buffer || InputLength < sizeof(INTELGPIO_PIN_CONFIGURATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (((PINTELGPIO_PIN_CONFIGURATION)Buffer)->Version != INTELGPIO_INTERFACE_VERSION)
                Status = STATUS_REVISION_MISMATCH;
            else
                Status = IntelGpioConfigurePin(DeviceExtension, Buffer);
            break;

        case IOCTL_INTELGPIO_WRITE_PIN:
            if (!Buffer || InputLength < sizeof(INTELGPIO_PIN_WRITE))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (((PINTELGPIO_PIN_WRITE)Buffer)->Version != INTELGPIO_INTERFACE_VERSION)
                Status = STATUS_REVISION_MISMATCH;
            else
                Status = IntelGpioWritePin(DeviceExtension, Buffer);
            break;

        case IOCTL_INTELGPIO_CONFIGURE_INTERRUPT:
            if (!Buffer || InputLength < sizeof(INTELGPIO_INTERRUPT_CONFIGURATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (((PINTELGPIO_INTERRUPT_CONFIGURATION)Buffer)->Version != INTELGPIO_INTERFACE_VERSION)
                Status = STATUS_REVISION_MISMATCH;
            else
                Status = IntelGpioConfigureInterrupt(DeviceExtension, Buffer);
            break;

        case IOCTL_INTELGPIO_WAIT_INTERRUPT:
            if (!Buffer || InputLength < sizeof(INTELGPIO_INTERRUPT_WAIT) || OutputLength < sizeof(INTELGPIO_INTERRUPT_WAIT))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (((PINTELGPIO_INTERRUPT_WAIT)Buffer)->Version != INTELGPIO_INTERFACE_VERSION)
                Status = STATUS_REVISION_MISMATCH;
            else
            {
                Status = IntelGpioWaitInterrupt(DeviceExtension, Buffer);
                if (NT_SUCCESS(Status))
                    Irp->IoStatus.Information = sizeof(INTELGPIO_INTERRUPT_WAIT);
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

Release:
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
IntelGpioPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELGPIO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IntelGpioForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = IntelGpioStartHardware(DeviceExtension, IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated);
            if (NT_SUCCESS(Status))
                Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
            if (!NT_SUCCESS(Status))
                IntelGpioUnmapCommunities(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
            IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
            IntelGpioUnmapCommunities(DeviceExtension);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
            IntelGpioUnmapCommunities(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
            DeviceExtension->Started = FALSE;
            KeSetEvent(&DeviceExtension->InterruptEvent, IO_NO_INCREMENT, FALSE);
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (NT_SUCCESS(Status))
                IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            else
                DPRINT1("INTELGPIO: remove lock acquisition failed, status 0x%lx\n", Status);
            IntelGpioUnmapCommunities(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelGpioPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELGPIO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelGpioAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PINTELGPIO_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(INTELGPIO_DEVICE_EXTENSION), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!DeviceExtension->LowerDevice)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, INTELGPIO_TAG, 0, 0);
    KeInitializeSpinLock(&DeviceExtension->RegisterLock);
    KeInitializeEvent(&DeviceExtension->InterruptEvent, SynchronizationEvent, FALSE);
    KeInitializeDpc(&DeviceExtension->InterruptDpc, IntelGpioInterruptDpc, DeviceExtension);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_INTEL_GPIO, NULL, &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(DeviceExtension->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverExtension->AddDevice = IntelGpioAddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = IntelGpioCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = IntelGpioCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IntelGpioDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = IntelGpioPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = IntelGpioPower;
    return STATUS_SUCCESS;
}
