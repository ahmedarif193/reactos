/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI C-state parsing helpers
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "intelppm.h"

#define NDEBUG
#include <debug.h>

#ifndef ACPI_ADR_SPACE_SYSTEM_MEMORY
#define ACPI_ADR_SPACE_SYSTEM_MEMORY    0x00
#define ACPI_ADR_SPACE_SYSTEM_IO        0x01
#endif

VOID
NTAPI
HalProcessorIdle(VOID);
BOOLEAN
NTAPI
HalIsAcpiBusMasterActive(VOID);

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

#define ACPIPROC_CST_FIELDS              4

static
VOID
AcpiprocCopyRegisterFromHal(
    _Out_ PACPIPROC_REGISTER_BLOCK Destination,
    _In_ const HAL_ACPI_C_STATE_REGISTER *Source);

static
NTSTATUS
AcpiprocCaptureLegacyCstates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

static
VOID
AcpiprocSortCstateEntries(
    _Inout_updates_(Count) PACPIPROC_CSTATE_ENTRY States,
    _In_ ULONG Count)
{
    ACPIPROC_CSTATE_ENTRY TempEntry;

    if (!States || Count < 2)
        return;

    for (ULONG Index = 0; Index < Count - 1; ++Index)
    {
        for (ULONG Next = Index + 1; Next < Count; ++Next)
        {
            if (States[Next].Latency < States[Index].Latency)
            {
                TempEntry = States[Index];
                States[Index] = States[Next];
                States[Next] = TempEntry;
            }
        }
    }
}

static
VOID
AcpiprocCopyRegisterFromHal(
    _Out_ PACPIPROC_REGISTER_BLOCK Destination,
    _In_ const HAL_ACPI_C_STATE_REGISTER *Source)
{
    if (!Destination || !Source)
        return;

    Destination->AddressSpaceId = Source->AddressSpaceId;
    Destination->BitWidth = Source->BitWidth;
    Destination->BitOffset = Source->BitOffset;
    Destination->AccessSize = Source->AccessSize;
    Destination->Address = Source->Address;
}

static
NTSTATUS
AcpiprocCaptureLegacyCstates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    HAL_ACPI_C_STATE_INFO HalInfo;
    PACPIPROC_CSTATE_ENTRY States = NULL;
    size_t AllocationSize;
    NTSTATUS Status;

    Status = HalGetAcpiCStateInformation(&HalInfo);
    if (!NT_SUCCESS(Status))
        return Status;

    if (HalInfo.Count == 0)
        return STATUS_NOT_SUPPORTED;

    Status = RtlSizeTMult(HalInfo.Count,
                          sizeof(ACPIPROC_CSTATE_ENTRY),
                          &AllocationSize);
    if (!NT_SUCCESS(Status))
        return Status;

    States = ExAllocatePoolWithTag(NonPagedPoolNx, AllocationSize, ACPIPROC_TAG);
    if (!States)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(States, AllocationSize);

    for (ULONG Index = 0; Index < HalInfo.Count; ++Index)
    {
        States[Index].Type = HalInfo.States[Index].Type;
        States[Index].Latency = HalInfo.States[Index].Latency;
        States[Index].Power = 0;
        States[Index].RequiresCacheFlush = HalInfo.States[Index].RequiresCacheFlush;
        AcpiprocCopyRegisterFromHal(&States[Index].Register,
                                    &HalInfo.States[Index].Register);
    }

    DeviceExtension->Idle.States = States;
    DeviceExtension->Idle.StateCount = HalInfo.Count;
    DeviceExtension->Idle.CstPresent = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocParseCstatePackage(
    _In_ PACPI_METHOD_ARGUMENT PackageArgument,
    _Out_ PACPIPROC_CSTATE_ENTRY Entry)
{
    PACPI_METHOD_ARGUMENT Field;
    ULONG Remaining;
    SIZE_T FieldSize;
    NTSTATUS Status;

    if (!PackageArgument || !Entry)
        return STATUS_INVALID_PARAMETER;

    if (PackageArgument->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        return STATUS_ACPI_INVALID_DATA;

    Field = (PACPI_METHOD_ARGUMENT)PackageArgument->Data;
    Remaining = PackageArgument->DataLength;

    if (Remaining < ACPI_METHOD_ARGUMENT_LENGTH(0))
        return STATUS_ACPI_INVALID_DATA;

    if (Field->Type != ACPI_METHOD_ARGUMENT_BUFFER)
        return STATUS_ACPI_INVALID_DATA;

    Status = AcpiprocParseGenericRegisterDescriptor(Field->Data,
                                                    Field->DataLength,
                                                    &Entry->Register);
    if (!NT_SUCCESS(Status))
        return Status;

    FieldSize = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Field);
    if (FieldSize > Remaining)
        return STATUS_ACPI_INVALID_DATA;

    Remaining -= FieldSize;
    Field = (PACPI_METHOD_ARGUMENT)((PUCHAR)Field + FieldSize);

    if (Remaining == 0 || Field->Type != ACPI_METHOD_ARGUMENT_INTEGER)
        return STATUS_ACPI_INVALID_DATA;

    Entry->Type = Field->Argument;

    FieldSize = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Field);
    if (FieldSize > Remaining)
        return STATUS_ACPI_INVALID_DATA;

    Remaining -= FieldSize;
    Field = (PACPI_METHOD_ARGUMENT)((PUCHAR)Field + FieldSize);

    if (Remaining == 0 || Field->Type != ACPI_METHOD_ARGUMENT_INTEGER)
        return STATUS_ACPI_INVALID_DATA;

    Entry->Latency = Field->Argument;

    FieldSize = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Field);
    if (FieldSize > Remaining)
        return STATUS_ACPI_INVALID_DATA;

    Remaining -= FieldSize;
    Field = (PACPI_METHOD_ARGUMENT)((PUCHAR)Field + FieldSize);

    if (Remaining == 0 || Field->Type != ACPI_METHOD_ARGUMENT_INTEGER)
        return STATUS_ACPI_INVALID_DATA;

    Entry->Power = Field->Argument;
    Entry->RequiresCacheFlush = FALSE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocCaptureCst(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer = NULL;
    ULONG OutputLength = 0;
    PACPI_METHOD_ARGUMENT Argument;
    PACPI_METHOD_ARGUMENT EntryArgument;
    ULONG DeclaredCount;
    ULONG PackageCount;
    ULONG ActualCount = 0;
    PACPIPROC_CSTATE_ENTRY States = NULL;
    size_t AllocationSize;
    PUCHAR BufferEnd;
    NTSTATUS Status;

    Status = AcpiprocExecuteMethod(DeviceExtension, "_CST", &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!OutputBuffer || OutputBuffer->Count < 1)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    BufferEnd = (PUCHAR)OutputBuffer + OutputLength;
    Argument = OutputBuffer->Argument;
    if (((PUCHAR)Argument + sizeof(*Argument)) > BufferEnd)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    if (Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    DeclaredCount = Argument->Argument;
    if (DeclaredCount == 0)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    if (OutputBuffer->Count <= 1)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    PackageCount = OutputBuffer->Count - 1;
    if (DeclaredCount < PackageCount)
        PackageCount = DeclaredCount;

    Status = RtlSizeTMult(PackageCount, sizeof(ACPIPROC_CSTATE_ENTRY), &AllocationSize);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    States = ExAllocatePoolWithTag(NonPagedPoolNx, AllocationSize, ACPIPROC_TAG);
    if (!States)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(States, AllocationSize);

    EntryArgument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    for (ULONG Index = 0; Index < PackageCount && EntryArgument; ++Index)
    {
        SIZE_T EntrySize = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(EntryArgument);

        if (((PUCHAR)EntryArgument + EntrySize) > BufferEnd)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            break;
        }

        Status = AcpiprocParseCstatePackage(EntryArgument, &States[ActualCount]);
        if (!NT_SUCCESS(Status))
            break;

        ++ActualCount;

        if (ActualCount == PackageCount)
            break;

        EntryArgument = ACPI_METHOD_NEXT_ARGUMENT(EntryArgument);
        if (((PUCHAR)EntryArgument) >= BufferEnd)
            EntryArgument = NULL;
    }

    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (ActualCount == 0)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    if (ActualCount > 1)
    {
        AcpiprocSortCstateEntries(States, ActualCount);
    }

    DeviceExtension->Idle.States = States;
    DeviceExtension->Idle.StateCount = ActualCount;
    DeviceExtension->Idle.CstPresent = TRUE;
    States = NULL;

    DPRINT("acpiproc: %lu C-states available via _CST\n", ActualCount);
    Status = STATUS_SUCCESS;

Cleanup:
    if (States)
        ExFreePoolWithTag(States, ACPIPROC_TAG);

    if (OutputBuffer)
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);

    return Status;
}

static
NTSTATUS
AcpiprocCaptureCsd(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer = NULL;
    ULONG OutputLength = 0;
    PACPI_METHOD_ARGUMENT DomainPackage;
    ULONG Values[ACPIPROC_CSD_FIELD_COUNT];
    NTSTATUS Status;

    DeviceExtension->Idle.Csd.Valid = FALSE;
    RtlZeroMemory(&DeviceExtension->Idle.Csd, sizeof(DeviceExtension->Idle.Csd));

    Status = AcpiprocExecuteMethod(DeviceExtension, "_CSD", &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!OutputBuffer || OutputBuffer->Count == 0)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    DomainPackage = OutputBuffer->Argument;
    if (DomainPackage->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }

    Status = AcpiprocCopyPackageIntegers(DomainPackage,
                                         Values,
                                         ACPIPROC_CSD_FIELD_COUNT);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->Idle.Csd.Valid = TRUE;
        DeviceExtension->Idle.Csd.NumEntries = Values[0];
        DeviceExtension->Idle.Csd.Revision = Values[1];
        DeviceExtension->Idle.Csd.Domain = Values[2];
        DeviceExtension->Idle.Csd.CoordType = Values[3];
        DeviceExtension->Idle.Csd.NumProcessors = Values[4];
    }

Cleanup:
    if (OutputBuffer)
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);

    return Status;
}

static
BOOLEAN
AcpiprocIsCstateSupported(
    _In_ PACPIPROC_CSTATE_ENTRY Entry)
{
    switch (Entry->Register.AddressSpaceId)
    {
        case ACPI_ADR_SPACE_SYSTEM_IO:
            return TRUE;

        case ACPI_ADR_SPACE_SYSTEM_MEMORY:
            return TRUE;

        case ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE:
            return (Entry->Type == 1);

        default:
            return FALSE;
    }
}

static
BOOLEAN
AcpiprocCstateCoordinationAllowed(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_ PACPIPROC_CSTATE_ENTRY Entry)
{
    ULONG ProcessorCount;

    if (!DeviceExtension || !Entry)
        return FALSE;

    ProcessorCount = (ULONG)(UCHAR)KeNumberProcessors;
    if (ProcessorCount <= 1)
        return TRUE;

    if (DeviceExtension->Idle.Csd.Valid)
    {
        if (DeviceExtension->Idle.Csd.NumProcessors <= 1)
            return TRUE;

        switch (DeviceExtension->Idle.Csd.CoordType)
        {
            case ACPIPROC_COORD_TYPE_SW_ANY:
                return (Entry->Type < 3);

            case ACPIPROC_COORD_TYPE_SW_ALL:
                return (Entry->Type <= 1);

            case ACPIPROC_COORD_TYPE_HW_ALL:
                return TRUE;

            default:
                return (Entry->Type <= 1);
        }
    }

    /* No _CSD data. Only allow C3+ if this is a uniprocessor */
    return (Entry->Type < 3);
}

static
BOOLEAN
AcpiprocShouldExposeCstate(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _In_ PACPIPROC_CSTATE_ENTRY Entry)
{
    if (!AcpiprocIsCstateSupported(Entry))
        return FALSE;

    if (!AcpiprocCstateCoordinationAllowed(DeviceExtension, Entry))
    {
        if (DeviceExtension->Idle.Csd.Valid)
        {
            DPRINT1("acpiproc: CPU%lu skipping C%lu (CoordType %lu, domain %lu)\n",
                    DeviceExtension->ProcessorIndex,
                    Entry->Type,
                    DeviceExtension->Idle.Csd.CoordType,
                    DeviceExtension->Idle.Csd.Domain);
        }
        else
        {
            DPRINT1("acpiproc: CPU%lu skipping C%lu (no coordination data on SMP)\n",
                    DeviceExtension->ProcessorIndex,
                    Entry->Type);
        }
        return FALSE;
    }

    return TRUE;
}

static
NTSTATUS
FASTCALL
AcpiprocIdleHandler(
    _In_ ULONG_PTR Context,
    _Inout_ PPROCESSOR_IDLE_TIMES IdleTimes)
{
    PACPIPROC_IDLE_HANDLER_CONTEXT HandlerContext;
    PACPIPROC_CSTATE_ENTRY Entry;
    ULONGLONG Value;

    UNREFERENCED_PARAMETER(IdleTimes);

    HandlerContext = (PACPIPROC_IDLE_HANDLER_CONTEXT)Context;
    if (!HandlerContext)
        return STATUS_INVALID_PARAMETER;

    Entry = HandlerContext->StateEntry;
    if (!Entry)
        return STATUS_INVALID_PARAMETER;

    switch (Entry->Register.AddressSpaceId)
    {
        case ACPI_ADR_SPACE_SYSTEM_IO:
        case ACPI_ADR_SPACE_SYSTEM_MEMORY:
            if (Entry->RequiresCacheFlush && Entry->Type >= 3)
            {
                KeInvalidateAllCaches();
            }

            if (Entry->Type >= 3 && HalIsAcpiBusMasterActive())
            {
                return STATUS_DEVICE_BUSY;
            }

            return AcpiprocReadRegister(&Entry->Register,
                                        AcpiprocRegisterKindControl,
                                        &Value);

        case ACPIPROC_ADDRESS_SPACE_FIXED_HARDWARE:
            HalProcessorIdle();
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
AcpiprocRegisterIdleHandlers(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    ULONG SupportedStates = 0;
    ULONG ProcessorCount;
    PPO_PROCESSOR_IDLE_HANDLER IdleHandlers = NULL;
    PACPIPROC_IDLE_HANDLER_CONTEXT Contexts = NULL;
    NTSTATUS Status;
    ULONG TargetIndex = 0;

    ProcessorCount = (ULONG)(UCHAR)KeNumberProcessors;
    if (DeviceExtension->ProcessorIndex >= ProcessorCount)
        return STATUS_INVALID_DEVICE_STATE;

    for (ULONG Index = 0; Index < DeviceExtension->Idle.StateCount; ++Index)
    {
        if (AcpiprocShouldExposeCstate(DeviceExtension,
                                       &DeviceExtension->Idle.States[Index]))
            ++SupportedStates;
    }

    if (SupportedStates == 0)
    {
        DPRINT1("acpiproc: CPU%lu has no usable C-states after filtering\n",
                DeviceExtension->ProcessorIndex);
        return STATUS_NOT_SUPPORTED;
    }

    Contexts = ExAllocatePoolWithTag(NonPagedPoolNx,
                                     SupportedStates * sizeof(ACPIPROC_IDLE_HANDLER_CONTEXT),
                                     ACPIPROC_TAG);
    if (!Contexts)
        return STATUS_INSUFFICIENT_RESOURCES;

    IdleHandlers = ExAllocatePoolWithTag(PagedPool,
                                         SupportedStates * sizeof(PO_PROCESSOR_IDLE_HANDLER),
                                         ACPIPROC_TAG);
    if (!IdleHandlers)
    {
        ExFreePoolWithTag(Contexts, ACPIPROC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Contexts,
                  SupportedStates * sizeof(ACPIPROC_IDLE_HANDLER_CONTEXT));
    RtlZeroMemory(IdleHandlers,
                  SupportedStates * sizeof(PO_PROCESSOR_IDLE_HANDLER));

    for (ULONG Index = 0; Index < DeviceExtension->Idle.StateCount; ++Index)
    {
        PACPIPROC_CSTATE_ENTRY Entry = &DeviceExtension->Idle.States[Index];

        if (!AcpiprocShouldExposeCstate(DeviceExtension, Entry))
            continue;

        Contexts[TargetIndex].DeviceExtension = DeviceExtension;
        Contexts[TargetIndex].StateEntry = Entry;

        IdleHandlers[TargetIndex].Info.HardwareLatency = Entry->Latency;
        IdleHandlers[TargetIndex].Info.Handler = AcpiprocIdleHandler;
        IdleHandlers[TargetIndex].Context = (ULONG_PTR)&Contexts[TargetIndex];

        ++TargetIndex;
    }

    Status = PoRegisterProcessorIdleHandler(DeviceExtension->ProcessorIndex,
                                            IdleHandlers,
                                            SupportedStates);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->Idle.HandlerContexts = Contexts;
        DeviceExtension->Idle.HandlerCount = SupportedStates;
        DeviceExtension->Idle.HandlersRegistered = TRUE;
    }
    else
    {
        ExFreePoolWithTag(Contexts, ACPIPROC_TAG);
    }

    ExFreePoolWithTag(IdleHandlers, ACPIPROC_TAG);
    return Status;
}

static
VOID
AcpiprocUnregisterIdleHandlers(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    if (DeviceExtension->Idle.HandlersRegistered)
    {
        PoUnregisterProcessorIdleHandler(DeviceExtension->ProcessorIndex);
        DeviceExtension->Idle.HandlersRegistered = FALSE;
    }

    if (DeviceExtension->Idle.HandlerContexts)
    {
        ExFreePoolWithTag(DeviceExtension->Idle.HandlerContexts, ACPIPROC_TAG);
        DeviceExtension->Idle.HandlerContexts = NULL;
    }

    DeviceExtension->Idle.HandlerCount = 0;
}

NTSTATUS
AcpiprocInitializeIdleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS Status;
    BOOLEAN HaveStates = FALSE;

    AcpiprocCleanupIdleStates(DeviceExtension);

    Status = AcpiprocCaptureCst(DeviceExtension);
    if (NT_SUCCESS(Status) && DeviceExtension->Idle.StateCount != 0)
    {
        HaveStates = TRUE;
    }
    else if (!NT_SUCCESS(Status) &&
             Status != STATUS_OBJECT_NAME_NOT_FOUND &&
             Status != STATUS_ACPI_INVALID_DATA &&
             Status != STATUS_NOT_SUPPORTED)
    {
        return Status;
    }

    if (!HaveStates)
    {
        Status = AcpiprocCaptureLegacyCstates(DeviceExtension);
        if (NT_SUCCESS(Status) && DeviceExtension->Idle.StateCount != 0)
        {
            HaveStates = TRUE;
        }
        else if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
        {
            return Status;
        }
    }

    Status = AcpiprocCaptureCsd(DeviceExtension);
    if (!NT_SUCCESS(Status) &&
        Status != STATUS_OBJECT_NAME_NOT_FOUND &&
        Status != STATUS_ACPI_INVALID_DATA)
    {
        DPRINT1("acpiproc: _CSD unavailable (status 0x%lx)\n", Status);
    }

    if (HaveStates && DeviceExtension->Idle.StateCount != 0)
    {
        Status = AcpiprocRegisterIdleHandlers(DeviceExtension);
        if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
        {
            DPRINT1("acpiproc: Failed to register idle handlers (status 0x%lx)\n",
                    Status);
        }
    }

    return STATUS_SUCCESS;
}

VOID
AcpiprocCleanupIdleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    AcpiprocUnregisterIdleHandlers(DeviceExtension);

    if (DeviceExtension->Idle.States)
    {
        ExFreePoolWithTag(DeviceExtension->Idle.States, ACPIPROC_TAG);
        DeviceExtension->Idle.States = NULL;
    }

    DeviceExtension->Idle.StateCount = 0;
    DeviceExtension->Idle.CstPresent = FALSE;
    RtlZeroMemory(&DeviceExtension->Idle.Csd, sizeof(DeviceExtension->Idle.Csd));
}
