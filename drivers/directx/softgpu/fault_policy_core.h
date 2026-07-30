/*
 * PROJECT:     ReactOS WDDM Software GPU Miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Version-gated terminal DMA fault retirement policy
 */

#ifndef _SOFTGPU_FAULT_POLICY_CORE_H_
#define _SOFTGPU_FAULT_POLICY_CORE_H_

#include <ntddk.h>

typedef struct _SOFTGPU_FAULT_RETIREMENT
{
    ULONG NextHead;
    ULONG CompletedFence;
    BOOLEAN NotifyPageFault;
} SOFTGPU_FAULT_RETIREMENT, *PSOFTGPU_FAULT_RETIREMENT;

typedef enum _SOFTGPU_DMA_ADDRESS_CLASS
{
    SoftGpuDmaAddressGpuVaChecked = 0,
    SoftGpuDmaAddressPhysicalSurface,
    SoftGpuDmaAddressKernelPaging
} SOFTGPU_DMA_ADDRESS_CLASS;

/*
 * A virtual submission is supplied directly from a process GPUVA and bypasses
 * DxgkDdiRender. Until the 2D executor walks GPU page tables for surface
 * commands, only records whose memory access is explicitly GPUVA-checked are
 * legal there. Paging records embed a kernel VA and are always KMD-generated.
 */
FORCEINLINE
BOOLEAN
SoftGpuDmaCommandAddressClassAllowed(
    _In_ BOOLEAN VirtualAddressing,
    _In_ SOFTGPU_DMA_ADDRESS_CLASS AddressClass)
{
    if ((ULONG)AddressClass >
            (ULONG)SoftGpuDmaAddressKernelPaging)
    {
        return FALSE;
    }
    return !VirtualAddressing ||
           AddressClass == SoftGpuDmaAddressGpuVaChecked;
}

/*
 * WDDM 2.0 introduced DMA_PAGE_FAULTED, where the bad packet is consumed
 * without advancing the successful fence watermark.  Older targets cannot
 * report that interrupt and preserve their historical forward-progress
 * behavior by consuming the packet as a no-op completion.
 */
FORCEINLINE
VOID
SoftGpuFaultPolicyRetire(
    _In_ ULONG TargetLevel,
    _In_ ULONG HeadIndex,
    _In_ ULONG SubmissionFence,
    _In_ ULONG PreviousCompletedFence,
    _Out_ PSOFTGPU_FAULT_RETIREMENT Retirement)
{
    Retirement->NextHead = HeadIndex + 1;
    Retirement->CompletedFence = PreviousCompletedFence;
    Retirement->NotifyPageFault = TargetLevel >= 2000;

    if (!Retirement->NotifyPageFault &&
        (LONG)(SubmissionFence - PreviousCompletedFence) > 0)
    {
        Retirement->CompletedFence = SubmissionFence;
    }
}

#endif /* _SOFTGPU_FAULT_POLICY_CORE_H_ */
