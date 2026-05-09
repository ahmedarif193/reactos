/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/arch/common/generic/sysinfo_stubs.c
 * PURPOSE:         HAL System Information Stubs for Windows 8/8.1/10+ APIs
 * PROGRAMMERS:     ReactOS Portable Systems Group
 *
 * NOTES:
 *   This file contains architecture-independent stub implementations for
 *   HAL exports introduced in Windows 8, 8.1, and Windows 10.
 *   These stubs allow ReactOS to report as Windows 10-compatible while
 *   providing proper STATUS_NOT_SUPPORTED responses for unimplemented features.
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ******************************************************************/

/*
 * Base HAL Functions (Windows 2000+)
 * These are deprecated legacy functions - drivers should use DMA_OPERATIONS instead.
 */
VOID
FASTCALL
HalExamineMBR(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG SectorSize,
    _In_ ULONG MBRTypeIdentifier,
    _Out_ PVOID *Buffer)
{
    DPRINT1("HalExamineMBR: STUB (deprecated API - use disk class driver)\n");
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(SectorSize);
    UNREFERENCED_PARAMETER(MBRTypeIdentifier);
    if (Buffer)
        *Buffer = NULL;
}

ULONG
NTAPI
HalGetDmaAlignment(
    _In_ PVOID DmaAdapter)
{
    DPRINT1("HalGetDmaAlignment: STUB (deprecated API - use DMA_OPERATIONS)\n");
    UNREFERENCED_PARAMETER(DmaAdapter);
    /* Return default alignment (no alignment restriction) */
    return 0;
}

NTSTATUS
NTAPI
HalEnableInterrupt(
    _In_ PINTERRUPT_CONNECTION_DATA ConnectionData)
{
    DPRINT1("HalEnableInterrupt: STUB (Win7+ API)\n");
    UNREFERENCED_PARAMETER(ConnectionData);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalDisableInterrupt(
    _In_ PINTERRUPT_CONNECTION_DATA ConnectionData)
{
    DPRINT1("HalDisableInterrupt: STUB (Win7+ API)\n");
    UNREFERENCED_PARAMETER(ConnectionData);
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
    DPRINT1("HalGetVectorInput: STUB (Win7+ API)\n");
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(Affinity);

    if (Input)
        *Input = 0;
    if (Polarity)
        *Polarity = InterruptPolarityUnknown;
    if (IntRemapInfo)
        RtlZeroMemory(IntRemapInfo, sizeof(*IntRemapInfo));

    return STATUS_NOT_SUPPORTED;
}

KIRQL
NTAPI
HalConvertDeviceIdtToIrql(
    _In_ ULONG Vector)
{
    UNREFERENCED_PARAMETER(Vector);
    return 0;
}

NTSTATUS
NTAPI
HalGetInterruptTargetInformation(
    _Inout_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    KAFFINITY Mask;
    ULONG ProcessorNumber;

    if (TargetInformation == NULL ||
        TargetInformation->Version != HAL_INTERRUPT_TARGET_INFORMATION_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mask = TargetInformation->TargetProcessors;
    if (Mask == 0)
        Mask = KeQueryActiveProcessors();

    ProcessorNumber = 0;
    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        ProcessorNumber++;
    }

    TargetInformation->ProcessorNumber = ProcessorNumber;
    TargetInformation->TargetProcessors = ((KAFFINITY)1 << ProcessorNumber);
    TargetInformation->DestinationId = ProcessorNumber;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalGetMessageRoutingInfo(
    _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    HAL_INTERRUPT_TARGET_INFORMATION TargetInfo;
    NTSTATUS Status;

    if (RoutingInfo == NULL ||
        RoutingInfo->Version != HAL_MESSAGE_ROUTING_INFO_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (RoutingInfo->Flags & HAL_MSI_ROUTING_ALLOCATE_VECTOR)
        return STATUS_NOT_SUPPORTED;

    if (RoutingInfo->Vector == 0)
        return STATUS_INVALID_PARAMETER;

    TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
    TargetInfo.TargetProcessors = RoutingInfo->TargetProcessors;
    TargetInfo.ProcessorNumber = 0;
    TargetInfo.DestinationId = 0;
    Status = HalGetInterruptTargetInformation(&TargetInfo);
    if (!NT_SUCCESS(Status))
        return Status;

    RoutingInfo->TargetProcessors = TargetInfo.TargetProcessors;
    RoutingInfo->DestinationId = TargetInfo.DestinationId;
    if (RoutingInfo->Irql == 0)
        RoutingInfo->Irql = HalConvertDeviceIdtToIrql(RoutingInfo->Vector);
    RoutingInfo->MessageAddress.QuadPart = 0xFEE00000ULL |
                                           ((ULONGLONG)TargetInfo.DestinationId << 12);
    RoutingInfo->MessageData = (USHORT)(RoutingInfo->Vector & 0xFF);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalGetMemoryCachingRequirements(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ SIZE_T Length,
    _Out_ MEMORY_CACHING_TYPE *CacheType)
{
    UNREFERENCED_PARAMETER(BaseAddress);
    UNREFERENCED_PARAMETER(Length);

    if (CacheType == NULL)
        return STATUS_INVALID_PARAMETER;

    *CacheType = MmNonCached;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalGetProcessorIdByNtNumber(
    _In_ ULONG ProcessorNumber,
    _Out_ PULONG ProcessorId)
{
    if (ProcessorId == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ProcessorNumber >= MAXIMUM_PROCESSORS)
        return STATUS_INVALID_PARAMETER;

    *ProcessorId = ProcessorNumber;
    return STATUS_SUCCESS;
}

/* NOTE: HalGetScatterGatherList, HalPutDmaAdapter, HalPutScatterGatherList
 * are already implemented in hal/arch/common/generic/dma.c */

/*
 * Windows 7+ WHEA (Windows Hardware Error Architecture) APIs (NT 6.1+)
 */
VOID
NTAPI
HalBugCheckSystem(
#if (NTDDI_VERSION >= NTDDI_WIN7)
    _In_ PWHEA_ERROR_SOURCE_DESCRIPTOR ErrorSource,
    _In_ PWHEA_ERROR_RECORD ErrorRecord)
#else
    _In_ PWHEA_ERROR_RECORD ErrorRecord)
#endif
{
    DPRINT1("HalBugCheckSystem: STUB (Win7+ WHEA API)\n");
#if (NTDDI_VERSION >= NTDDI_WIN7)
    UNREFERENCED_PARAMETER(ErrorSource);
#endif
    UNREFERENCED_PARAMETER(ErrorRecord);
    /* This function is called during bugcheck - just trace and return */
}

/*
 * Windows 8+ Hardware Counter APIs (NT 6.2+)
 * These are stubs for compatibility - hardware performance counters
 * require CPU-specific PMU (Performance Monitoring Unit) support.
 */
NTSTATUS
NTAPI
HalAllocateHardwareCounters(
    _In_reads_(GroupCount) PGROUP_AFFINITY GroupAffinity,
    _In_ ULONG GroupCount,
    _In_ PPHYSICAL_COUNTER_RESOURCE_LIST ResourceList,
    _Out_ PHANDLE CounterSetHandle)
{
    DPRINT1("HalAllocateHardwareCounters: STUB (Win8+ API)\n");
    UNREFERENCED_PARAMETER(GroupAffinity);
    UNREFERENCED_PARAMETER(GroupCount);
    UNREFERENCED_PARAMETER(ResourceList);
    if (CounterSetHandle)
        *CounterSetHandle = NULL;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalFreeHardwareCounters(
    _In_ HANDLE CounterSetHandle)
{
    DPRINT1("HalFreeHardwareCounters: STUB (Win8+ API)\n");
    UNREFERENCED_PARAMETER(CounterSetHandle);
    return STATUS_SUCCESS;
}

/*
 * Windows 8+ DMA Crash Dump Register APIs (NT 6.2+)
 * Extended crash dump register allocation with support for multiple register sets.
 */
NTSTATUS
NTAPI
HalDmaAllocateCrashDumpRegistersEx(
    _In_ PVOID Adapter,
    _In_ ULONG NumberOfMapRegisters,
    _In_ ULONG Type,
    _Out_ PVOID *MapRegisterBase,
    _Out_ PULONG MapRegistersAvailable)
{
    DPRINT1("HalDmaAllocateCrashDumpRegistersEx: STUB (Win8+ API)\n");
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
    UNREFERENCED_PARAMETER(Type);
    if (MapRegisterBase)
        *MapRegisterBase = NULL;
    if (MapRegistersAvailable)
        *MapRegistersAvailable = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalDmaFreeCrashDumpRegistersEx(
    _In_ PVOID Adapter,
    _In_ ULONG Type)
{
    DPRINT1("HalDmaFreeCrashDumpRegistersEx: STUB (Win8+ API)\n");
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Type);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalEnumerateEnvironmentVariablesEx(
    _In_ ULONG InformationClass,
    _Out_writes_bytes_opt_(*BufferLength) PVOID Buffer,
    _Inout_opt_ PULONG BufferLength)
{
    DPRINT1("HalEnumerateEnvironmentVariablesEx: STUB (Win7+ API)\n");
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(Buffer);

    if (BufferLength)
        *BufferLength = 0;
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
    DPRINT1("HalGetEnvironmentVariableEx: STUB (Win7+ API)\n");
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);

    if (ValueLength)
        *ValueLength = 0;
    if (Attributes)
        *Attributes = 0;

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
    DPRINT1("HalSetEnvironmentVariableEx: STUB (Win7+ API)\n");
    UNREFERENCED_PARAMETER(VariableName);
    UNREFERENCED_PARAMETER(VendorGuid);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);
    UNREFERENCED_PARAMETER(Attributes);
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
    DPRINT1("HalQueryEnvironmentVariableInfoEx: STUB (Win7+ API)\n");
    UNREFERENCED_PARAMETER(Attributes);
    UNREFERENCED_PARAMETER(MaximumVariableStorageSize);
    UNREFERENCED_PARAMETER(RemainingVariableStorageSize);
    UNREFERENCED_PARAMETER(MaximumVariableSize);
    return STATUS_NOT_SUPPORTED;
}

/*
 * Windows 8.1+ APIs (NT 6.3+)
 * Interrupt controller and IRQ management extensions.
 */
NTSTATUS
NTAPI
HalConvertDeviceIdtVectorToIrql(
    _In_ ULONG Vector,
    _In_ ULONG Reserved,
    _Out_ PULONG Irql)
{
    DPRINT1("HalConvertDeviceIdtVectorToIrql: STUB (Win8.1+ API)\n");
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(Reserved);
    if (Irql)
        *Irql = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalAllocateGsivForSecondaryIc(
    _In_ PVOID ParentHandle,
    _In_ ULONG GsivCount)
{
    DPRINT1("HalAllocateGsivForSecondaryIc: STUB (Win8.1+ API)\n");
    UNREFERENCED_PARAMETER(ParentHandle);
    UNREFERENCED_PARAMETER(GsivCount);
    return STATUS_NOT_SUPPORTED;
}

/*
 * Windows 10+ APIs (NT 10.0+)
 * Modern interrupt handling and IOMMU policy management.
 */
NTSTATUS
NTAPI
HalSetIommuPolicy(
    _In_ PVOID Policy)
{
    DPRINT1("HalSetIommuPolicy: STUB (Win10+ API)\n");
    UNREFERENCED_PARAMETER(Policy);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalRequestInterrupt(
    _In_ ULONG Irql,
    _In_ PVOID InterruptObject,
    _In_ ULONG Vector,
    _In_ ULONG MessageNumber,
    _In_ ULONG ProcessorNumber)
{
    DPRINT1("HalRequestInterrupt: STUB (Win10+ API)\n");
    UNREFERENCED_PARAMETER(Irql);
    UNREFERENCED_PARAMETER(InterruptObject);
    UNREFERENCED_PARAMETER(Vector);
    UNREFERENCED_PARAMETER(MessageNumber);
    UNREFERENCED_PARAMETER(ProcessorNumber);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalEnumerateUnmaskedInterrupts(
    _Out_ PVOID InterruptInformation,
    _Inout_ PULONG InterruptInformationLength)
{
    DPRINT1("HalEnumerateUnmaskedInterrupts: STUB (Win10+ API)\n");
    UNREFERENCED_PARAMETER(InterruptInformation);
    if (InterruptInformationLength)
        *InterruptInformationLength = 0;
    return STATUS_NOT_SUPPORTED;
}

/* EOF */
