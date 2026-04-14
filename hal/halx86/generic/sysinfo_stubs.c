/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/generic/sysinfo_stubs.c
 * PURPOSE:         Minimal Win7 HAL compatibility stubs
 */

#include <hal.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
#if (NTDDI_VERSION >= NTDDI_WIN7)
HalBugCheckSystem(
    _In_ PWHEA_ERROR_SOURCE_DESCRIPTOR ErrorSource,
    _In_ PWHEA_ERROR_RECORD ErrorRecord)
{
    UNREFERENCED_PARAMETER(ErrorSource);
    UNREFERENCED_PARAMETER(ErrorRecord);
#else
HalBugCheckSystem(
    _In_ PWHEA_ERROR_RECORD ErrorRecord)
{
    UNREFERENCED_PARAMETER(ErrorRecord);
#endif
    DPRINT1("HalBugCheckSystem: STUB\n");
}

NTSTATUS
NTAPI
HalAllocateHardwareCounters(
    _In_reads_(GroupCount) PGROUP_AFFINITY GroupAffinity,
    _In_ ULONG GroupCount,
    _In_ PPHYSICAL_COUNTER_RESOURCE_LIST ResourceList,
    _Out_opt_ PHANDLE CounterSetHandle)
{
    UNREFERENCED_PARAMETER(GroupAffinity);
    UNREFERENCED_PARAMETER(GroupCount);
    UNREFERENCED_PARAMETER(ResourceList);

    if (CounterSetHandle)
        *CounterSetHandle = NULL;

    DPRINT1("HalAllocateHardwareCounters: STUB\n");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalFreeHardwareCounters(
    _In_opt_ HANDLE CounterSetHandle)
{
    UNREFERENCED_PARAMETER(CounterSetHandle);
    DPRINT1("HalFreeHardwareCounters: STUB\n");
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalEnumerateEnvironmentVariablesEx(
    _In_ ULONG InformationClass,
    _Out_writes_bytes_opt_(*BufferLength) PVOID Buffer,
    _Inout_opt_ PULONG BufferLength)
{
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(Buffer);

    if (BufferLength)
        *BufferLength = 0;

    DPRINT1("HalEnumerateEnvironmentVariablesEx: STUB\n");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalGetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _Out_writes_bytes_opt_(*ValueLength) PVOID Value,
    _Inout_ PULONG ValueLength,
    _Out_opt_ PULONG Attributes)
{
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);

    if (ValueLength)
        *ValueLength = 0;
    if (Attributes)
        *Attributes = 0;

    DPRINT1("HalGetEnvironmentVariableEx: STUB\n");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalSetEnvironmentVariableEx(
    _In_ PWSTR VariableName,
    _In_ LPCGUID VendorGuid,
    _In_reads_bytes_(ValueLength) PVOID Value,
    _In_ ULONG ValueLength,
    _In_ ULONG Attributes)
{
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Attributes);

    DPRINT1("HalSetEnvironmentVariableEx: STUB\n");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalEnableInterrupt(
    _In_ PINTERRUPT_CONNECTION_DATA ConnectionData)
{
    UNREFERENCED_PARAMETER(ConnectionData);
    DPRINT1("HalEnableInterrupt: STUB\n");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalDisableInterrupt(
    _In_ PINTERRUPT_CONNECTION_DATA ConnectionData)
{
    UNREFERENCED_PARAMETER(ConnectionData);
    DPRINT1("HalDisableInterrupt: STUB\n");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalGetVectorInput(
    _In_ ULONG Vector,
    _In_ PGROUP_AFFINITY Affinity,
    _Out_ PULONG Input,
    _Out_ PKINTERRUPT_POLARITY Polarity,
    _Out_ PINTERRUPT_REMAPPING_INFO IntRemapInfo)
{
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(Affinity);

    if (Input)
        *Input = 0;
    if (Polarity)
        *Polarity = InterruptPolarityUnknown;
    if (IntRemapInfo)
        RtlZeroMemory(IntRemapInfo, sizeof(*IntRemapInfo));

    DPRINT1("HalGetVectorInput: STUB\n");
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalQueryEnvironmentVariableInfoEx(
    _In_ ULONG Attributes,
    _Out_opt_ PULONGLONG MaximumVariableStorageSize,
    _Out_opt_ PULONGLONG RemainingVariableStorageSize,
    _Out_opt_ PULONGLONG MaximumVariableSize)
{
    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(MaximumVariableStorageSize);
    UNREFERENCED_PARAMETER(RemainingVariableStorageSize);
    UNREFERENCED_PARAMETER(MaximumVariableSize);

    DPRINT1("HalQueryEnvironmentVariableInfoEx: STUB\n");
    return STATUS_NOT_SUPPORTED;
}
