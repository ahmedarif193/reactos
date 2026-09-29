/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            lib/cmlib/cmse.c
 * PURPOSE:         Configuration Manager Library - Security Subsystem Interface
 * PROGRAMMERS:     Hermes Belusca-Maito (hermes.belusca@sfr.fr)
 */

/* INCLUDES ******************************************************************/

#include "cmlib.h"
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
CmpRemoveSecurityCellList(IN PHHIVE Hive,
                          IN HCELL_INDEX SecurityCell)
{
    PCM_KEY_SECURITY SecurityData, FlinkCell, BlinkCell;

    PAGED_CODE();


    SecurityData = (PCM_KEY_SECURITY)HvGetCell(Hive, SecurityCell);
    if (!SecurityData) return STATUS_INSUFFICIENT_RESOURCES;

    FlinkCell = (PCM_KEY_SECURITY)HvGetCell(Hive, SecurityData->Flink);
    if (!FlinkCell)
    {
        HvReleaseCell(Hive, SecurityCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    BlinkCell = (PCM_KEY_SECURITY)HvGetCell(Hive, SecurityData->Blink);
    if (!BlinkCell)
    {
        HvReleaseCell(Hive, SecurityData->Flink);
        HvReleaseCell(Hive, SecurityCell);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (FlinkCell->Blink != SecurityCell || BlinkCell->Flink != SecurityCell)
    {
        HvReleaseCell(Hive, SecurityData->Blink);
        HvReleaseCell(Hive, SecurityData->Flink);
        HvReleaseCell(Hive, SecurityCell);
        return STATUS_REGISTRY_CORRUPT;
    }

    /* Unlink the security block and free it */
    FlinkCell->Blink = SecurityData->Blink;
    BlinkCell->Flink = SecurityData->Flink;
#ifdef USE_CM_CACHE
    CmpRemoveFromSecurityCache(Hive, SecurityCell);
#endif

    /* Release the cells */
    HvReleaseCell(Hive, SecurityData->Blink);
    HvReleaseCell(Hive, SecurityData->Flink);
    HvReleaseCell(Hive, SecurityCell);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
CmpDereferenceSecurityCell(IN PHHIVE Hive,
                           IN HCELL_INDEX SecurityCell)
{
    PCM_KEY_SECURITY SecurityData;
    HCELL_INDEX Flink, Blink;
    BOOLEAN FreeCell = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    SecurityData = (PCM_KEY_SECURITY)HvGetCell(Hive, SecurityCell);
    if (!SecurityData)
        return STATUS_INSUFFICIENT_RESOURCES;
    ASSERT(SecurityData->Signature == CM_KEY_SECURITY_SIGNATURE);
    ASSERT(SecurityData->ReferenceCount != 0);
    Flink = SecurityData->Flink;
    Blink = SecurityData->Blink;
    if (!HvMarkCellDirty(Hive, SecurityCell, FALSE))
    {
        Status = STATUS_REGISTRY_IO_FAILED;
        goto Done;
    }
    if (SecurityData->ReferenceCount > 1)
    {
        SecurityData->ReferenceCount--;
    }
    else if (HvMarkCellDirty(Hive, Flink, FALSE) &&
             HvMarkCellDirty(Hive, Blink, FALSE))
    {
        Status = CmpRemoveSecurityCellList(Hive, SecurityCell);
        FreeCell = NT_SUCCESS(Status);
    }
    else
    {
        Status = STATUS_REGISTRY_IO_FAILED;
    }
Done:
    HvReleaseCell(Hive, SecurityCell);
    if (FreeCell)
        HvFreeCell(Hive, SecurityCell);
    return Status;
}

NTSTATUS
NTAPI
CmpFreeSecurityDescriptor(IN PHHIVE Hive,
                          IN HCELL_INDEX Cell)
{
    PCM_KEY_NODE CellData;
    HCELL_INDEX SecurityCell;
    NTSTATUS Status = STATUS_INSUFFICIENT_RESOURCES;

    PAGED_CODE();

    CmpLockHiveSecurity(Hive);
    CellData = (PCM_KEY_NODE)HvGetCell(Hive, Cell);
    if (CellData)
    {
        ASSERT(CellData->Signature == CM_KEY_NODE_SIGNATURE);
        SecurityCell = CellData->Security;
        Status = STATUS_SUCCESS;
        if (SecurityCell != HCELL_NIL)
        {
            Status = CmpDereferenceSecurityCell(Hive, SecurityCell);
            if (NT_SUCCESS(Status))
                CellData->Security = HCELL_NIL;
        }
        HvReleaseCell(Hive, Cell);
    }
    CmpUnlockHiveSecurity(Hive);
    return Status;
}
