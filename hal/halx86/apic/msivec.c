/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/apic/msivec.c
 * PURPOSE:         MSI Vector Allocation and Management (amd64)
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include "apicp.h"
#include "msip.h"
#include <smp.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

typedef struct _MSI_ALLOCATION_STATE {
    BOOLEAN Allocated;                      /* TRUE if vector is allocated */
    KIRQL Irql;                             /* IRQL for this vector */
} MSI_ALLOCATION_STATE, *PMSI_ALLOCATION_STATE;

static MSI_ALLOCATION_STATE HalpMsiVectorState[MSI_VECTOR_COUNT];
static KSPIN_LOCK HalpMsiVectorLock;
extern PPROCESSOR_IDENTITY HalpProcessorIdentity;

static
ULONG
NTAPI
HalpSelectTargetProcessor(
    _In_ KAFFINITY TargetProcessors)
{
    KAFFINITY Mask;
    ULONG ProcessorNumber;

    Mask = TargetProcessors;
    if (Mask == 0)
        Mask = HalpDefaultInterruptAffinity ? HalpDefaultInterruptAffinity
                                            : KeQueryActiveProcessors();

    ProcessorNumber = 0;
    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        ProcessorNumber++;
    }

    return ProcessorNumber;
}

/* FUNCTIONS ******************************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpInitializeMsiVectors(VOID)
{
    UCHAR i;

    /* Initialize the spin lock */
    KeInitializeSpinLock(&HalpMsiVectorLock);

    /* Initialize all MSI vector states to unallocated */
    for (i = 0; i < MSI_VECTOR_COUNT; i++)
    {
        HalpMsiVectorState[i].Allocated = FALSE;
        HalpMsiVectorState[i].Irql = 0;
    }

    DPRINT("MSI Vector Allocator initialized (vectors 0x%02x-0x%02x, legacy IRQ range 0x%02x-0x%02x)\n",
           MSI_VECTOR_MIN, MSI_VECTOR_MAX,
           PRIMARY_VECTOR_BASE, PRIMARY_VECTOR_BASE + APIC_MAX_IRQ - 1);

    return TRUE;
}

KIRQL
NTAPI
HalConvertDeviceIdtToIrql(
    _In_ ULONG Vector)
{
    return HalpVectorToIrql((UCHAR)Vector);
}

NTSTATUS
NTAPI
HalGetInterruptTargetInformation(
    _Inout_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation)
{
    ULONG ProcessorNumber;
    KAFFINITY Mask;

    if (TargetInformation == NULL ||
        TargetInformation->Version != HAL_INTERRUPT_TARGET_INFORMATION_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mask = TargetInformation->TargetProcessors;
    if (Mask == 0)
        Mask = HalpDefaultInterruptAffinity ? HalpDefaultInterruptAffinity
                                            : KeQueryActiveProcessors();

    ProcessorNumber = HalpSelectTargetProcessor(Mask);
    TargetInformation->ProcessorNumber = ProcessorNumber;
    TargetInformation->TargetProcessors = ((KAFFINITY)1 << ProcessorNumber);
    TargetInformation->DestinationId = HalpProcessorIdentity[ProcessorNumber].LapicId;
    if (TargetInformation->DestinationId == 0)
        TargetInformation->DestinationId = ProcessorNumber;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalGetMessageRoutingInfo(
    _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo)
{
    HAL_INTERRUPT_TARGET_INFORMATION TargetInfo;
    KIRQL DesiredIrql;
    UCHAR AllocatedVector;
    KIRQL AllocatedIrql;
    KAFFINITY AllocatedAffinity;
    BOOLEAN Allocated;
    NTSTATUS Status;

    if (RoutingInfo == NULL ||
        RoutingInfo->Version != HAL_MESSAGE_ROUTING_INFO_VERSION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (RoutingInfo->Flags & HAL_MSI_ROUTING_ALLOCATE_VECTOR)
    {
        DesiredIrql = RoutingInfo->DesiredIrql ? RoutingInfo->DesiredIrql
                                               : (CLOCK_LEVEL - 1);
        Allocated = HalpAllocateMsiVector(DesiredIrql,
                                          &AllocatedVector,
                                          &AllocatedIrql,
                                          &AllocatedAffinity);
        if (!Allocated)
            return STATUS_INSUFFICIENT_RESOURCES;

        RoutingInfo->Vector = AllocatedVector;
        RoutingInfo->Irql = AllocatedIrql;
        RoutingInfo->TargetProcessors = AllocatedAffinity;
    }
    else
    {
        if (RoutingInfo->Vector == 0)
            return STATUS_INVALID_PARAMETER;

        if (RoutingInfo->TargetProcessors == 0)
            RoutingInfo->TargetProcessors = HalpDefaultInterruptAffinity ?
                                            HalpDefaultInterruptAffinity :
                                            KeQueryActiveProcessors();
        if (RoutingInfo->Irql == 0)
            RoutingInfo->Irql = HalpVectorToIrql((UCHAR)RoutingInfo->Vector);
    }

    TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
    TargetInfo.TargetProcessors = RoutingInfo->TargetProcessors;
    TargetInfo.ProcessorNumber = 0;
    TargetInfo.DestinationId = 0;
    Status = HalGetInterruptTargetInformation(&TargetInfo);
    if (!NT_SUCCESS(Status))
        return Status;

    RoutingInfo->TargetProcessors = TargetInfo.TargetProcessors;
    RoutingInfo->DestinationId = TargetInfo.DestinationId;
    RoutingInfo->MessageAddress.QuadPart = 0xFEE00000ULL |
                                           ((ULONGLONG)TargetInfo.DestinationId << 12);
    RoutingInfo->MessageData = (USHORT)(RoutingInfo->Vector & 0xFF);
    return STATUS_SUCCESS;
}

KIRQL
FASTCALL
HalpMsiVectorToIrql(
    _In_ UCHAR Vector)
{
    /* Validate vector is in MSI range */
    if (Vector < MSI_VECTOR_MIN || Vector > MSI_VECTOR_MAX)
    {
        return 0;
    }

    /* Use the same conversion as IRQ vectors */
    return TprToIrql(Vector);
}

BOOLEAN
NTAPI
HalpAllocateMsiVector(
    _In_ KIRQL DesiredIrql,
    _Out_ PUCHAR OutVector,
    _Out_ PKIRQL OutIrql,
    _Out_ PKAFFINITY OutAffinity)
{
    KIRQL OldIrql;
    UCHAR Vector;
    KIRQL AllocatedIrql;
    ULONG Offset;
    UCHAR Index;

    /* Validate parameters */
    if (!OutVector || !OutIrql || !OutAffinity)
    {
        return FALSE;
    }

    /* Validate desired IRQL is in valid range for MSI (DEVICE_LEVEL range) */
    if (DesiredIrql < CMCI_LEVEL || DesiredIrql >= HIGH_LEVEL)
    {
        DPRINT1("HalpAllocateMsiVector: Invalid IRQL %d\n", DesiredIrql);
        return FALSE;
    }

    /* Acquire the spin lock */
    OldIrql = KeAcquireSpinLockRaiseToDpc(&HalpMsiVectorLock);

    /* Search for a free vector, using the same algorithm as HalpGetRootInterruptVector */
    for (Offset = 0; Offset < 15; Offset++)
    {
        /* Loop through IRQL range, starting from desired IRQL */
        for (AllocatedIrql = DesiredIrql;
             AllocatedIrql >= CMCI_LEVEL && AllocatedIrql < CLOCK_LEVEL;
             AllocatedIrql--)
        {
            /* Calculate the vector candidate */
            Vector = IrqlToTpr(AllocatedIrql) + (UCHAR)Offset;

            /* Check if vector is in MSI range */
            if (Vector < MSI_VECTOR_MIN || Vector > MSI_VECTOR_MAX)
            {
                continue;
            }

            /* Calculate index into our allocation state array */
            Index = Vector - MSI_VECTOR_MIN;

            /* Check if this vector is already allocated (in general HAL sense) */
            if (HalpVectorToIndex[Vector] != APIC_FREE_VECTOR)
            {
                continue;
            }

            /* Check if this vector is already allocated for MSI */
            if (HalpMsiVectorState[Index].Allocated)
            {
                continue;
            }

            /* Found a free vector! Allocate it */
            HalpMsiVectorState[Index].Allocated = TRUE;
            HalpMsiVectorState[Index].Irql = AllocatedIrql;

            /* Mark in the general HAL vector table as reserved */
            HalpVectorToIndex[Vector] = APIC_RESERVED_VECTOR;

            /* Return results */
            *OutVector = Vector;
            *OutIrql = AllocatedIrql;
            *OutAffinity = HalpDefaultInterruptAffinity;

            DPRINT("HalpAllocateMsiVector: allocated vector 0x%02x at IRQL %d\n",
                   Vector, AllocatedIrql);

            /* Release the spin lock and return success */
            KeReleaseSpinLock(&HalpMsiVectorLock, OldIrql);
            return TRUE;
        }
    }

    DPRINT1("HalpAllocateMsiVector: No free vectors available\n");

    /* Release the spin lock */
    KeReleaseSpinLock(&HalpMsiVectorLock, OldIrql);

    return FALSE;
}

BOOLEAN
NTAPI
HalpFreeMsiVector(
    _In_ UCHAR Vector)
{
    KIRQL OldIrql;
    UCHAR Index;

    /* Validate vector is in MSI range */
    if (Vector < MSI_VECTOR_MIN || Vector > MSI_VECTOR_MAX)
    {
        DPRINT1("HalpFreeMsiVector: Invalid vector 0x%02x\n", Vector);
        return FALSE;
    }

    /* Calculate index into allocation state array */
    Index = Vector - MSI_VECTOR_MIN;

    /* Acquire the spin lock */
    OldIrql = KeAcquireSpinLockRaiseToDpc(&HalpMsiVectorLock);

    /* Check if vector is allocated */
    if (!HalpMsiVectorState[Index].Allocated)
    {
        DPRINT1("HalpFreeMsiVector: Vector 0x%02x not allocated\n", Vector);
        KeReleaseSpinLock(&HalpMsiVectorLock, OldIrql);
        return FALSE;
    }

    /* Mark as unallocated */
    HalpMsiVectorState[Index].Allocated = FALSE;
    HalpMsiVectorState[Index].Irql = 0;

    /* Clear from general HAL vector table */
    HalpVectorToIndex[Vector] = APIC_FREE_VECTOR;

    DPRINT("HalpFreeMsiVector: Freed vector 0x%02x\n", Vector);

    /* Release the spin lock */
    KeReleaseSpinLock(&HalpMsiVectorLock, OldIrql);

    return TRUE;
}
