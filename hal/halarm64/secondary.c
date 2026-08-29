/*
 * PROJECT:     ReactOS Hardware Abstraction Layer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Secondary (GPIO) interrupt controller services
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <hal.h>
#include "halext.h"

#define NDEBUG
#include <debug.h>

#undef HalAllocateGsivForSecondaryInterrupt
#undef HalSecondaryInterruptQueryPrimaryInformation
#undef HalIsInterruptTypeSecondary

#define HALP_SECONDARY_GSIV_COUNT   512
#define HALP_SECONDARY_GSIV_MIN     0x400
#define HALP_SECONDARY_VECTOR_BASE  0x1000
#define HALP_SECONDARY_OWNER_MAX    64
#define HALP_SECONDARY_CONTROLLERS  8
#define HALP_SECONDARY_DEFAULT_IRQL 12
#define HALP_ARM64_LPI_BASE         8192

NTSTATUS NTAPI KeInitializeSecondaryInterruptServices(VOID);
BOOLEAN NTAPI KeDispatchSecondaryInterrupt(_In_ ULONG Vector, _In_ ULONG_PTR Flags, _In_opt_ PVOID Reserved);

extern BOOLEAN HalpGicItsEnabled;
extern ULONG HalpGicLpiCount;

typedef struct _HALP_SECONDARY_LINE
{
    CHAR OwnerName[HALP_SECONDARY_OWNER_MAX];
    USHORT OwnerNameLength;
    KINTERRUPT_MODE Mode;
    KINTERRUPT_POLARITY Polarity;
    BOOLEAN Allocated;
    BOOLEAN Enabled;
} HALP_SECONDARY_LINE, *PHALP_SECONDARY_LINE;

typedef struct _HALP_SECONDARY_CONTROLLER
{
    HAL_SECONDARY_INTERRUPT_INTERFACE Interface;
    CHAR OwnerName[HALP_SECONDARY_OWNER_MAX];
    USHORT OwnerNameLength;
    BOOLEAN Registered;
} HALP_SECONDARY_CONTROLLER, *PHALP_SECONDARY_CONTROLLER;

static HALP_SECONDARY_LINE HalpSecondaryLines[HALP_SECONDARY_GSIV_COUNT];
static HALP_SECONDARY_CONTROLLER HalpSecondaryControllers[HALP_SECONDARY_CONTROLLERS];
static KSPIN_LOCK HalpSecondaryLock;
static ULONG HalpSecondaryLineCount;
static ULONG HalpSecondaryGsivBase;
static BOOLEAN HalpSecondaryDispatchReady;

static
ULONG
HalpSecondaryGetGsivBase(VOID)
{
    if (HalpSecondaryGsivBase == 0)
    {
        ULONG Base = HALP_SECONDARY_GSIV_MIN;

        if (HalpGicItsEnabled && HalpGicLpiCount != 0 && Base < HALP_ARM64_LPI_BASE + HalpGicLpiCount)
            Base = HALP_ARM64_LPI_BASE + HalpGicLpiCount;
        HalpSecondaryGsivBase = Base;
    }
    return HalpSecondaryGsivBase;
}

static
USHORT
HalpSecondaryCopyOwner(
    _Out_writes_(HALP_SECONDARY_OWNER_MAX) PCHAR Target,
    _In_reads_bytes_opt_(Length) const CHAR *Source,
    _In_ USHORT Length)
{
    if (Source == NULL)
        Length = 0;
    if (Length > HALP_SECONDARY_OWNER_MAX - 1)
        Length = HALP_SECONDARY_OWNER_MAX - 1;
    RtlZeroMemory(Target, HALP_SECONDARY_OWNER_MAX);
    if (Length != 0)
        RtlCopyMemory(Target, Source, Length);
    return Length;
}

static
BOOLEAN
HalpSecondaryOwnerMatches(
    _In_ PCCHAR First,
    _In_ USHORT FirstLength,
    _In_ PCCHAR Second,
    _In_ USHORT SecondLength)
{
    return FirstLength == SecondLength &&
           (FirstLength == 0 || RtlCompareMemory(First, Second, FirstLength) == FirstLength);
}

static
PHALP_SECONDARY_CONTROLLER
HalpSecondaryFindController(
    _In_ PHALP_SECONDARY_LINE Line)
{
    ULONG Index;

    for (Index = 0; Index < HALP_SECONDARY_CONTROLLERS; Index++)
    {
        PHALP_SECONDARY_CONTROLLER Controller = &HalpSecondaryControllers[Index];

        if (Controller->Registered &&
            HalpSecondaryOwnerMatches(Controller->OwnerName, Controller->OwnerNameLength,
                                      Line->OwnerName, Line->OwnerNameLength))
        {
            return Controller;
        }
    }
    return NULL;
}

BOOLEAN
HalpSecondaryIsIntId(
    _In_ ULONG IntId)
{
    return IntId >= HALP_SECONDARY_VECTOR_BASE &&
           IntId < HALP_SECONDARY_VECTOR_BASE + HALP_SECONDARY_GSIV_COUNT;
}

ULONG
HalpSecondaryTranslateGsi(
    _In_ ULONG Gsi)
{
    ULONG Base = HalpSecondaryGetGsivBase();

    if (Gsi >= Base && Gsi < Base + HALP_SECONDARY_GSIV_COUNT)
        return HALP_SECONDARY_VECTOR_BASE + (Gsi - Base);
    return (ULONG)-1;
}

KIRQL
HalpSecondaryIrql(
    _In_ ULONG IntId)
{
    PHALP_SECONDARY_LINE Line = &HalpSecondaryLines[IntId - HALP_SECONDARY_VECTOR_BASE];
    PHALP_SECONDARY_CONTROLLER Controller = HalpSecondaryFindController(Line);

    if (Controller != NULL && Controller->Interface.Irql != PASSIVE_LEVEL)
        return Controller->Interface.Irql;
    return HALP_SECONDARY_DEFAULT_IRQL;
}

static
NTSTATUS
HalpSecondaryApplyLine(
    _In_ ULONG Index,
    _In_ BOOLEAN Enable)
{
    PHALP_SECONDARY_LINE Line = &HalpSecondaryLines[Index];
    PHALP_SECONDARY_CONTROLLER Controller = HalpSecondaryFindController(Line);
    ULONG Gsiv = HalpSecondaryGetGsivBase() + Index;

    if (Controller == NULL)
        return STATUS_DEVICE_NOT_READY;
    if (Enable)
        return Controller->Interface.EnableInterrupt(Controller->Interface.Context, Gsiv, Line->Mode, Line->Polarity);
    Controller->Interface.DisableInterrupt(Controller->Interface.Context, Gsiv);
    return STATUS_SUCCESS;
}

BOOLEAN
HalpSecondaryEnable(
    _In_ ULONG IntId,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    ULONG Index = IntId - HALP_SECONDARY_VECTOR_BASE;
    KIRQL OldIrql;

    if (!HalpSecondaryIsIntId(IntId))
        return FALSE;
    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    if (!HalpSecondaryLines[Index].Allocated)
    {
        KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
        return FALSE;
    }
    HalpSecondaryLines[Index].Mode = InterruptMode;
    HalpSecondaryLines[Index].Enabled = TRUE;
    HalpSecondaryApplyLine(Index, TRUE);
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
    return TRUE;
}

VOID
HalpSecondaryDisable(
    _In_ ULONG IntId)
{
    ULONG Index = IntId - HALP_SECONDARY_VECTOR_BASE;
    KIRQL OldIrql;

    if (!HalpSecondaryIsIntId(IntId))
        return;
    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    if (HalpSecondaryLines[Index].Allocated && HalpSecondaryLines[Index].Enabled)
    {
        HalpSecondaryLines[Index].Enabled = FALSE;
        HalpSecondaryApplyLine(Index, FALSE);
    }
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
}

static
NTSTATUS
NTAPI
HalpAllocateGsivForSecondaryInterrupt(
    _In_reads_bytes_(OwnerNameLength) PCCHAR OwnerName,
    _In_ USHORT OwnerNameLength,
    _Out_ PULONG Gsiv)
{
    PHALP_SECONDARY_LINE Line;
    ULONG Index;
    KIRQL OldIrql;
    NTSTATUS Status;

    *Gsiv = 0;
    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    if (HalpSecondaryLineCount >= HALP_SECONDARY_GSIV_COUNT)
    {
        KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (HalpSecondaryLineCount == 0)
    {
        Status = KeInitializeSecondaryInterruptServices();
        if (!NT_SUCCESS(Status))
        {
            KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
            return Status;
        }
    }
    Index = HalpSecondaryLineCount++;
    Line = &HalpSecondaryLines[Index];
    Line->OwnerNameLength = HalpSecondaryCopyOwner(Line->OwnerName, OwnerName, OwnerNameLength);
    Line->Mode = LevelSensitive;
    Line->Polarity = InterruptActiveHigh;
    Line->Allocated = TRUE;
    Line->Enabled = FALSE;
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);

    *Gsiv = HalpSecondaryGetGsivBase() + Index;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
HalpSecondaryInterruptQueryPrimaryInformation(
    _In_ PINTERRUPT_VECTOR_DATA VectorData,
    _Out_ PULONG PrimaryGsiv)
{
    ULONG IntId;
    PHALP_SECONDARY_CONTROLLER Controller;

    *PrimaryGsiv = 0;
    if (VectorData == NULL)
        return STATUS_INVALID_PARAMETER;
    IntId = HalpSecondaryTranslateGsi(VectorData->ControllerInput.Gsiv);
    if (IntId == (ULONG)-1 || !HalpSecondaryLines[IntId - HALP_SECONDARY_VECTOR_BASE].Allocated)
        return STATUS_NOT_FOUND;
    Controller = HalpSecondaryFindController(&HalpSecondaryLines[IntId - HALP_SECONDARY_VECTOR_BASE]);
    if (Controller == NULL)
        return STATUS_DEVICE_NOT_READY;
    *PrimaryGsiv = Controller->Interface.PrimaryGsiv;
    return STATUS_SUCCESS;
}

static
BOOLEAN
NTAPI
HalpIsInterruptTypeSecondary(
    _In_ ULONG Type,
    _In_ ULONG InputGsiv)
{
    UNREFERENCED_PARAMETER(Type);
    return HalpSecondaryTranslateGsi(InputGsiv) != (ULONG)-1;
}

static
BOOLEAN
NTAPI
HalpSecondaryDispatchInterrupt(
    _In_ ULONG Gsiv)
{
    ULONG IntId = HalpSecondaryTranslateGsi(Gsiv);

    if (IntId == (ULONG)-1)
        return FALSE;
    return KeDispatchSecondaryInterrupt(IntId, 0, NULL);
}

VOID
HalpSecondaryInitializeDispatch(VOID)
{
    if (HalpSecondaryDispatchReady)
        return;
    KeInitializeSpinLock(&HalpSecondaryLock);
    HALPRIVATEDISPATCH->HalAllocateGsivForSecondaryInterrupt = HalpAllocateGsivForSecondaryInterrupt;
    HALPRIVATEDISPATCH->HalSecondaryInterruptQueryPrimaryInformation = HalpSecondaryInterruptQueryPrimaryInformation;
    HALPRIVATEDISPATCH->HalIsInterruptTypeSecondary = HalpIsInterruptTypeSecondary;
    HalpSecondaryDispatchReady = TRUE;
}

NTSTATUS
HalpSecondaryRegisterInterface(
    _In_ ULONG BufferSize,
    _In_ PVOID Buffer)
{
    PHAL_SECONDARY_INTERRUPT_INTERFACE Interface = Buffer;
    PHALP_SECONDARY_CONTROLLER Controller = NULL;
    ULONG Index;
    KIRQL OldIrql;

    if (Interface == NULL || BufferSize < sizeof(HAL_SECONDARY_INTERRUPT_INTERFACE) ||
        Interface->Size < sizeof(HAL_SECONDARY_INTERRUPT_INTERFACE) ||
        Interface->EnableInterrupt == NULL || Interface->DisableInterrupt == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    HalpSecondaryInitializeDispatch();
    KeAcquireSpinLock(&HalpSecondaryLock, &OldIrql);
    for (Index = 0; Index < HALP_SECONDARY_CONTROLLERS; Index++)
    {
        if (HalpSecondaryControllers[Index].Registered &&
            HalpSecondaryControllers[Index].Interface.Context == Interface->Context)
        {
            Controller = &HalpSecondaryControllers[Index];
            break;
        }
    }
    if (Controller == NULL)
    {
        for (Index = 0; Index < HALP_SECONDARY_CONTROLLERS; Index++)
        {
            if (!HalpSecondaryControllers[Index].Registered)
            {
                Controller = &HalpSecondaryControllers[Index];
                break;
            }
        }
    }
    if (Controller == NULL)
    {
        KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Controller->Interface = *Interface;
    Controller->OwnerNameLength = HalpSecondaryCopyOwner(Controller->OwnerName, Interface->OwnerName, Interface->OwnerNameLength);
    Controller->Interface.OwnerName = Controller->OwnerName;
    Controller->Interface.OwnerNameLength = Controller->OwnerNameLength;
    Controller->Registered = TRUE;

    for (Index = 0; Index < HalpSecondaryLineCount; Index++)
    {
        if (HalpSecondaryLines[Index].Allocated && HalpSecondaryLines[Index].Enabled &&
            HalpSecondaryFindController(&HalpSecondaryLines[Index]) == Controller)
        {
            HalpSecondaryApplyLine(Index, TRUE);
        }
    }
    KeReleaseSpinLock(&HalpSecondaryLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
HalpSecondaryQueryInformation(
    _In_ ULONG BufferSize,
    _Out_ PVOID Buffer,
    _Out_opt_ PULONG ReturnedLength)
{
    PHAL_SECONDARY_INTERRUPT_INFORMATION Information = Buffer;

    if (ReturnedLength != NULL)
        *ReturnedLength = sizeof(HAL_SECONDARY_INTERRUPT_INFORMATION);
    if (Information == NULL || BufferSize < sizeof(HAL_SECONDARY_INTERRUPT_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;

    HalpSecondaryInitializeDispatch();
    Information->Size = sizeof(HAL_SECONDARY_INTERRUPT_INFORMATION);
    Information->GsivBase = HalpSecondaryGetGsivBase();
    Information->GsivCount = HALP_SECONDARY_GSIV_COUNT;
    Information->DispatchInterrupt = HalpSecondaryDispatchInterrupt;
    return STATUS_SUCCESS;
}
