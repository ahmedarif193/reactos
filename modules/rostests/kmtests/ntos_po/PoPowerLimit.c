/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite power limit requests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <initguid.h>
#include <wdmguid.h>
#include <poclass.h>

#define NDEBUG
#include <debug.h>

NTKERNELAPI
NTSTATUS
NTAPI
IoCreateDriver(
    _In_opt_ PUNICODE_STRING DriverName,
    _In_ PDRIVER_INITIALIZE InitializationFunction);

NTKERNELAPI
VOID
NTAPI
IoDeleteDriver(
    _In_ PDRIVER_OBJECT DriverObject);

static LONG InterfaceReferences;
static BOOLEAN ProvideInterface;
static POWER_LIMIT_VALUE CurrentValues[2];
static PVOID InterfaceContext = &CurrentValues;

static
VOID
NTAPI
InterfaceReference(
    _In_ PVOID Context)
{
    ok_eq_pointer(Context, InterfaceContext);
    InterlockedIncrement(&InterfaceReferences);
}

static
VOID
NTAPI
InterfaceDereference(
    _In_ PVOID Context)
{
    ok_eq_pointer(Context, InterfaceContext);
    InterlockedDecrement(&InterfaceReferences);
}

static
NTSTATUS
NTAPI
QueryAttributes(
    _Inout_opt_ PVOID Context,
    _In_ ULONG BufferCount,
    _Inout_opt_ PVOID Buffer,
    _Out_ PULONG AttributeCount)
{
    PPOWER_LIMIT_ATTRIBUTES Attributes = Buffer;
    ULONG Index;

    ok_eq_pointer(Context, InterfaceContext);
    *AttributeCount = RTL_NUMBER_OF(CurrentValues);
    if (BufferCount == 0)
        return STATUS_SUCCESS;
    if (BufferCount < RTL_NUMBER_OF(CurrentValues) || Buffer == NULL)
        return STATUS_BUFFER_TOO_SMALL;

    for (Index = 0; Index < RTL_NUMBER_OF(CurrentValues); Index++)
    {
        RtlZeroMemory(&Attributes[Index], sizeof(Attributes[Index]));
        Attributes[Index].Type = (POWER_LIMIT_TYPES)Index;
        Attributes[Index].DomainId = 7;
        Attributes[Index].MaxValue = 1000 * (Index + 1);
        Attributes[Index].MinValue = 10;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
SetPowerLimit(
    _Inout_opt_ PVOID Context,
    _In_ ULONG ValueCount,
    _In_ PVOID Values)
{
    PPOWER_LIMIT_VALUE NewValues = Values;
    ULONG Index;

    ok_eq_pointer(Context, InterfaceContext);
    for (Index = 0; Index < ValueCount; Index++)
    {
        if ((ULONG)NewValues[Index].Type >= RTL_NUMBER_OF(CurrentValues))
            return STATUS_INVALID_PARAMETER;
        CurrentValues[NewValues[Index].Type] = NewValues[Index];
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
QueryPowerLimit(
    _Inout_opt_ PVOID Context,
    _In_ ULONG ValueCount,
    _Inout_ PVOID Values)
{
    PPOWER_LIMIT_VALUE Result = Values;
    ULONG Index;

    ok_eq_pointer(Context, InterfaceContext);
    for (Index = 0; Index < ValueCount; Index++)
    {
        if ((ULONG)Result[Index].Type >= RTL_NUMBER_OF(CurrentValues))
            return STATUS_INVALID_PARAMETER;
        Result[Index] = CurrentValues[Result[Index].Type];
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PnpDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PPOWER_LIMIT_INTERFACE Interface;
    NTSTATUS Status = Irp->IoStatus.Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Stack->MinorFunction == IRP_MN_QUERY_INTERFACE &&
        ProvideInterface &&
        IsEqualGUID(Stack->Parameters.QueryInterface.InterfaceType, &GUID_POWER_LIMIT_INTERFACE))
    {
        ok_eq_uint(Stack->Parameters.QueryInterface.Size, sizeof(POWER_LIMIT_INTERFACE));
        ok_eq_uint(Stack->Parameters.QueryInterface.Version, 1);
        Interface = (PPOWER_LIMIT_INTERFACE)Stack->Parameters.QueryInterface.Interface;
        Interface->Size = sizeof(*Interface);
        Interface->Version = 1;
        Interface->Context = InterfaceContext;
        Interface->InterfaceReference = InterfaceReference;
        Interface->InterfaceDereference = InterfaceDereference;
        Interface->DomainCount = 1;
        Interface->QueryAttributes = QueryAttributes;
        Interface->SetPowerLimit = SetPowerLimit;
        Interface->QueryPowerLimit = QueryPowerLimit;
        InterfaceReference(InterfaceContext);
        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
TargetDriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_PNP] = PnpDispatch;
    return STATUS_SUCCESS;
}

START_TEST(PoPowerLimit)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\KmtPowerLimit");
    COUNTED_REASON_CONTEXT Reason;
    POWER_LIMIT_ATTRIBUTES Attributes[2];
    POWER_LIMIT_VALUE Values[2];
    PDRIVER_OBJECT DriverObject = NULL;
    PDEVICE_OBJECT Target = NULL, Policy = NULL;
    PVOID Request;
    ULONG Count;
    NTSTATUS Status;

    Status = IoCreateDriver(&DriverName, TargetDriverEntry);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No driver object\n"))
        return;

    Status = ObReferenceObjectByName(&DriverName, OBJ_KERNEL_HANDLE, NULL, 0, IoDriverObjectType, KernelMode, NULL, (PVOID *)&DriverObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No driver object pointer\n"))
        return;

    Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Target);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = IoCreateDevice(DriverObject, 0, NULL, FILE_DEVICE_UNKNOWN, 0, FALSE, &Policy);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    if (!skip(NT_SUCCESS(Status), "No device object\n"))
    {
        Target->Flags &= ~DO_DEVICE_INITIALIZING;
        Policy->Flags &= ~DO_DEVICE_INITIALIZING;

        RtlZeroMemory(&Reason, sizeof(Reason));
        Reason.Version = DIAGNOSTIC_REASON_VERSION;
        Reason.Flags = DIAGNOSTIC_REASON_SIMPLE_STRING;
        RtlInitUnicodeString(&Reason.SimpleString, L"KmtPowerLimit");

        Request = (PVOID)1;
        Status = PoCreatePowerLimitRequest(&Request, NULL, Policy, &Reason);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

        ProvideInterface = FALSE;
        Request = NULL;
        Status = PoCreatePowerLimitRequest(&Request, Target, Policy, &Reason);
        ok(!NT_SUCCESS(Status), "Status = 0x%08lx\n", Status);
        ok_eq_pointer(Request, NULL);
        ok_eq_long(InterfaceReferences, 0L);

        ProvideInterface = TRUE;
        Request = NULL;
        Status = PoCreatePowerLimitRequest(&Request, Target, Policy, &Reason);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Request != NULL, "Request is NULL\n");
        ok_eq_long(InterfaceReferences, 1L);
        if (NT_SUCCESS(Status))
        {
            Count = 0;
            Status = PoQueryPowerLimitAttributes(Request, 0, NULL, &Count);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Count, 2UL);

            Count = 0;
            RtlZeroMemory(Attributes, sizeof(Attributes));
            Status = PoQueryPowerLimitAttributes(Request, RTL_NUMBER_OF(Attributes), Attributes, &Count);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Count, 2UL);
            ok_eq_int(Attributes[1].Type, PowerLimitBurst);
            ok_eq_ulong(Attributes[1].DomainId, 7UL);
            ok_eq_ulong(Attributes[1].MaxValue, 2000UL);

            Values[0].Type = PowerLimitContinuous;
            Values[0].DomainId = 7;
            Values[0].TargetValue = 150;
            Values[0].TimeParameter = 0;
            Values[1].Type = PowerLimitBurst;
            Values[1].DomainId = 7;
            Values[1].TargetValue = 900;
            Values[1].TimeParameter = 28;
            Status = PoSetPowerLimitValue(Request, &Reason, RTL_NUMBER_OF(Values), Values);
            ok_eq_hex(Status, STATUS_SUCCESS);

            RtlZeroMemory(Values, sizeof(Values));
            Values[0].Type = PowerLimitBurst;
            Values[1].Type = PowerLimitContinuous;
            Status = PoQueryPowerLimitValue(Request, RTL_NUMBER_OF(Values), Values);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Values[0].TargetValue, 900UL);
            ok_eq_ulong(Values[0].TimeParameter, 28UL);
            ok_eq_ulong(Values[1].TargetValue, 150UL);

            Status = PoDeletePowerLimitRequest(Request);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_long(InterfaceReferences, 0L);
        }

        IoDeleteDevice(Policy);
    }

    if (Target != NULL)
        IoDeleteDevice(Target);

    ObDereferenceObject(DriverObject);
    IoDeleteDriver(DriverObject);
}
