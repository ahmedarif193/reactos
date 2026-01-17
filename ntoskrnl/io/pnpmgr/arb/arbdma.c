/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     PnP manager Root DMA Arbiter
 * COPYRIGHT:   Copyright 2025 Justin Miller <justin.miller@reactos.org>
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern ARBITER_INSTANCE IopRootDmaArbiter;

/* FUNCTIONS *****************************************************************/

/*
 * DMA arbiter callback functions
 *
 * NOTE: These functions may be called during arbiter initialization which can occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64 during early boot. They are also called
 * during runtime resource arbitration. Therefore:
 * - No PAGED_CODE assertion (would fail at DISPATCH_LEVEL during init)
 *
 * These callbacks are stored as function pointers in the ARBITER_INSTANCE structure
 * and invoked by the arbiter library during resource allocation.
 */

NTSTATUS
NTAPI
IopArbDmaUnpackRequirements(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _Out_ PUINT64 OutMinimumAddress,
    _Out_ PUINT64 OutMaximumAddress,
    _Out_ PULONG OutLength,
    _Out_ PULONG OutAlignment)
{
    /* No PAGED_CODE() - may be called during boot at elevated IRQL on ARM64 */
    DPRINT("IopArbDmaUnpackRequirements: IoDescriptor: %p, OutMinimumAddress: %p, OutMaximumAddress: %p, OutLength: %p, OutAlignment: %p\n",
           IoDescriptor,
           OutMinimumAddress,
           OutMaximumAddress,
           OutLength,
           OutAlignment);

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
IopArbDmaPackResource(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor,
    _In_ UINT64 Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor)
{
    /* No PAGED_CODE() - may be called during boot at elevated IRQL on ARM64 */
    DPRINT("IopArbDmaPackResource: IoDescriptor: %p, Start: %p, CmDescriptor: %p\n",
           IoDescriptor,
           Start,
           CmDescriptor);

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
IopArbDmaUnpackResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDescriptor,
    _Out_ PUINT64 Start,
    _Out_ PULONG OutLength)
{
    /* No PAGED_CODE() - may be called during boot at elevated IRQL on ARM64 */
    DPRINT("IopArbDmaUnpackResource: CmDescriptor: %p, Start: %p, OutLength: %p\n",
           CmDescriptor,
           Start,
           OutLength);

    UNIMPLEMENTED;
    return STATUS_NOT_IMPLEMENTED;
}

INT32
NTAPI
IopArbDmaScoreRequirement(
    _In_ PIO_RESOURCE_DESCRIPTOR IoDescriptor)
{
    /* No PAGED_CODE() - may be called during boot at elevated IRQL on ARM64 */
    DPRINT("IopArbDmaScoreRequirement: IoDescriptor: %p\n",
           IoDescriptor);

    UNIMPLEMENTED;
    return 0;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopArbDmaInitialize(VOID)
{
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    /* Note: No PAGED_CODE() here - this is called during boot initialization
     * when IRQL may be elevated on ARM64. The function is only called once
     * during system startup from IopInitializePlugPlayServices(). */
    IopRootDmaArbiter.Name = L"RootDma";
    IopRootDmaArbiter.UnpackRequirement = IopArbDmaUnpackRequirements;
    IopRootDmaArbiter.PackResource = IopArbDmaPackResource;
    IopRootDmaArbiter.UnpackResource = IopArbDmaUnpackResource;
    IopRootDmaArbiter.ScoreRequirement = IopArbDmaScoreRequirement;

    Status = ArbInitializeArbiterInstance(&IopRootDmaArbiter,
                                          NULL,
                                          CmResourceTypeBusNumber,
                                          IopRootDmaArbiter.Name,
                                          L"Root",
                                          NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopArbDmaInitialize: Failed with %X", Status);
    }

    return Status;
}
