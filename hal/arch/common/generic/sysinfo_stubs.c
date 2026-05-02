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

/* NOTE: HalGetScatterGatherList, HalPutDmaAdapter, HalPutScatterGatherList
 * are already implemented in hal/arch/common/generic/dma.c */

/*
 * Windows 7+ WHEA (Windows Hardware Error Architecture) APIs (NT 6.1+)
 */
VOID
NTAPI
HalBugCheckSystem(
    _In_ PWHEA_ERROR_RECORD ErrorRecord)
{
    DPRINT1("HalBugCheckSystem: STUB (Win7+ WHEA API)\n");
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
    _In_ PVOID GroupAffinity,
    _In_ ULONG GroupCount,
    _In_ PVOID ResourceList,
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

/*
 * Windows 8+ Environment Variable Ex APIs (NT 6.2+)
 * Extended UEFI variable support with additional information classes.
 */
NTSTATUS
NTAPI
HalEnumerateEnvironmentVariablesEx(
    _In_ ULONG InformationClass,
    _Out_ PVOID Buffer,
    _Inout_ PULONG BufferLength)
{
    DPRINT1("HalEnumerateEnvironmentVariablesEx: STUB (Win8+ API)\n");
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(Buffer);
    if (BufferLength)
        *BufferLength = 0;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalSetEnvironmentVariableEx(
    _In_ ULONG InformationClass,
    _In_ PVOID Buffer,
    _In_ PVOID Name,
    _In_ PVOID Value,
    _In_ ULONG ValueLength)
{
    DPRINT1("HalSetEnvironmentVariableEx: STUB (Win8+ API)\n");
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Value);
    UNREFERENCED_PARAMETER(ValueLength);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
HalQueryEnvironmentVariableInfoEx(
    _In_ ULONG InformationClass,
    _Out_ PVOID MaximumVariableStorageSize,
    _Out_ PVOID RemainingVariableStorageSize,
    _Out_ PVOID MaximumVariableSize,
    _Out_ PVOID Attributes)
{
    DPRINT1("HalQueryEnvironmentVariableInfoEx: STUB (Win8+ API)\n");
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(MaximumVariableStorageSize);
    UNREFERENCED_PARAMETER(RemainingVariableStorageSize);
    UNREFERENCED_PARAMETER(MaximumVariableSize);
    UNREFERENCED_PARAMETER(Attributes);
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
