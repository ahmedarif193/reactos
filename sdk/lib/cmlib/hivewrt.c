/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Configuration Manager Library - Registry Syncing & Hive/Log/Alternate Writing
 * COPYRIGHT:   Copyright 2001 - 2005 Eric Kohl
 *              Copyright 2005 Filip Navara <navaraf@reactos.org>
 *              Copyright 2021 Max Korostil
 *              Copyright 2022 George Bișoc <george.bisoc@reactos.org>
 */

#include "cmlib.h"
#define NDEBUG
#include <debug.h>

/* DECLARATIONS *************************************************************/

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
BOOLEAN
NTAPI
IoSetThreadHardErrorMode(
    _In_ BOOLEAN HardErrorEnabled);
#endif

/* GLOBALS ******************************************************************/

/* PRIVATE FUNCTIONS ********************************************************/

/**
 * @brief
 * Validates the base block header of a primary
 * hive for consistency.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor to look
 * for the header block.
 */
static
VOID
HvpValidateBaseHeader(
    _In_ PHHIVE RegistryHive)
{
    PHBASE_BLOCK BaseBlock;

    /*
     * Cache the base block and validate it.
     * Especially...
     *
     * 1. It must must have a valid signature.
     * 2. It must have a valid format.
     * 3. It must be of an adequate major version,
     *    not anything else.
     */
    BaseBlock = RegistryHive->BaseBlock;
    ASSERT(BaseBlock->Signature == HV_HBLOCK_SIGNATURE);
    ASSERT(BaseBlock->Format == HBASE_FORMAT_MEMORY);
    ASSERT(BaseBlock->Major == HSYS_MAJOR);
}


/**
 * @brief
 * Writes data (dirty or non) to a primary hive during
 * syncing operation. Hive writing is also performed
 * during a flush occurrence on request by the system.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where the data is
 * to be written to that hive.
 *
 * @param[in] OnlyDirty
 * If set to TRUE, the function only looks for dirty
 * data to be written to the primary hive, otherwise if
 * it's set to FALSE then the function writes all the data.
 *
 * @param[in] FileType
 * The file type of a registry hive. This can be HFILE_TYPE_PRIMARY
 * or HFILE_TYPE_ALTERNATE.
 *
 * @return
 * Returns TRUE if writing to hive has succeeded,
 * FALSE otherwise.
 *
 * @remarks
 * The on-disk header metadata of a hive is already written with type
 * of HFILE_TYPE_PRIMARY, regardless of what file type the caller submits,
 * as an alternate hive is basically a mirror of the primary hive.
 */
static
BOOLEAN
CMAPI
HvpWriteHive(
    _In_ PHHIVE RegistryHive,
    _In_ BOOLEAN OnlyDirty,
    _In_ ULONG FileType)
{
    BOOLEAN Success;
    ULONG FileOffset;
    ULONG BlockIndex;
    ULONG LastIndex;
    PVOID Block;

    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->BaseBlock->Length ==
           RegistryHive->Storage[Stable].Length);
    ASSERT(RegistryHive->BaseBlock->RootCell != HCELL_NIL);

    /* Validate the base header before we go further */
    HvpValidateBaseHeader(RegistryHive);

    /*
     * The sequences can diverge during a forced system shutdown
     * occurrence, such as during a power failure, a hardware
     * failure or during a system crash, and when one of the
     * sequences have been modified during writing into the log
     * or hive. In such cases the hive needs a repair.
     */
    if (RegistryHive->BaseBlock->Sequence1 !=
        RegistryHive->BaseBlock->Sequence2)
    {
        DPRINT1("The sequences DO NOT MATCH (Sequence1 == 0x%x, Sequence2 == 0x%x)\n",
                RegistryHive->BaseBlock->Sequence1, RegistryHive->BaseBlock->Sequence2);
        return FALSE;
    }

    /*
     * Update the primary sequence number and write
     * the base block to hive.
     */
    RegistryHive->BaseBlock->Type = HFILE_TYPE_PRIMARY;
    RegistryHive->BaseBlock->Sequence1++;
    RegistryHive->BaseBlock->CheckSum = HvpHiveHeaderChecksum(RegistryHive->BaseBlock);

    /* Write hive block */
    FileOffset = 0;
    Success = RegistryHive->FileWrite(RegistryHive, FileType,
                                      &FileOffset, RegistryHive->BaseBlock,
                                      sizeof(HBASE_BLOCK));
    if (!Success)
    {
        DPRINT1("Failed to write the base block header to primary hive (primary sequence)\n");
        return FALSE;
    }

    /* Write the whole primary hive, block by block */
    BlockIndex = 0;
    while (BlockIndex < RegistryHive->Storage[Stable].Length / HBLOCK_SIZE)
    {
        /*
         * If we have to synchronize the registry hive we
         * want to look for dirty blocks to reflect the new
         * updates done to the hive. Otherwise just write
         * all the blocks as if we were doing a regular
         * hive write.
         */
        if (OnlyDirty)
        {
            /* Check if the block is clean or we're past the last block */
            LastIndex = BlockIndex;
            BlockIndex = RtlFindSetBits(&RegistryHive->DirtyVector, 1, BlockIndex);
            if (BlockIndex == ~HV_CLEAN_BLOCK || BlockIndex < LastIndex)
            {
                break;
            }
        }

        /* Get the block and offset position */
        Block = HvpLookupBlock(RegistryHive, Stable, BlockIndex);
        FileOffset = (BlockIndex + 1) * HBLOCK_SIZE;

        /* Now write this block to primary hive file */
        Success = RegistryHive->FileWrite(RegistryHive, FileType,
                                          &FileOffset, Block, HBLOCK_SIZE);
        if (!Success)
        {
            DPRINT1("Failed to write hive block to primary hive file (block 0x%p, block index 0x%x)\n",
                    Block, BlockIndex);
            return FALSE;
        }

        /* Go to the next block */
        BlockIndex++;
    }

    /*
     * We wrote all the hive contents to the file, we
     * must flush the changes to disk now.
     */
    Success = CmpFileFlush(RegistryHive, FileType, NULL, 0);
    if (!Success)
    {
        DPRINT1("Failed to flush the primary hive\n");
        return FALSE;
    }

    /*
     * Increment the secondary sequence number and
     * update the checksum. A successful hive write
     * transaction is when both of sequences are the
     * same, indicating the write operation didn't
     * fail.
     */
    RegistryHive->BaseBlock->Sequence2++;
    RegistryHive->BaseBlock->CheckSum = HvpHiveHeaderChecksum(RegistryHive->BaseBlock);

    /* Write hive block */
    FileOffset = 0;
    Success = RegistryHive->FileWrite(RegistryHive, FileType,
                                      &FileOffset, RegistryHive->BaseBlock,
                                      sizeof(HBASE_BLOCK));
    if (!Success)
    {
        DPRINT1("Failed to write the base block header to primary hive (secondary sequence)\n");
        return FALSE;
    }

    /* Flush the hive immediately */
    Success = CmpFileFlush(RegistryHive, FileType, NULL, 0);
    if (!Success)
    {
        DPRINT1("Failed to flush the primary hive\n");
        return FALSE;
    }

    return TRUE;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/**
 * @brief
 * Synchronizes a registry hive with latest updates
 * from dirty data present in volatile memory, aka RAM.
 * It writes both to hive log and corresponding primary
 * hive. Syncing is done on request by the system during
 * a flush occurrence.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where syncing is
 * to be performed.
 *
 * @return
 * Returns TRUE if syncing has succeeded, FALSE otherwise.
 */
BOOLEAN
CMAPI
HvSyncHive(
    _In_ PHHIVE RegistryHive)
{
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    BOOLEAN HardErrors;
#endif

    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->Signature == HV_HHIVE_SIGNATURE);

    /* Avoid any write operations on volatile hives */
    if (RegistryHive->HiveFlags & HIVE_VOLATILE)
    {
        DPRINT("Hive 0x%p is volatile\n", RegistryHive);
        return TRUE;
    }

    /*
     * Check if there's any dirty data in the vector.
     * A space with clean blocks would be pointless for
     * a log because we want to write dirty data in and
     * sync up, not clean data. So just consider our
     * job as done as there's literally nothing to do.
     */
    if (RtlFindSetBits(&RegistryHive->DirtyVector, 1, 0) == ~HV_CLEAN_BLOCK)
    {
        DPRINT("The dirty vector has clean data, nothing to do\n");
        return TRUE;
    }

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    /* Disable hard errors before syncing the hive */
    HardErrors = IoSetThreadHardErrorMode(FALSE);
#endif

#if !defined(_BLDR_)
    /* Update hive header modification time */
    KeQuerySystemTime(&RegistryHive->BaseBlock->TimeStamp);
#endif


    /* Update the primary hive file */
    if (!HvpWriteHive(RegistryHive, TRUE, HFILE_TYPE_PRIMARY))
    {
        DPRINT1("Failed to write the primary hive\n");
#if !defined(CMLIB_HOST) && !defined(_BLDR_)
        IoSetThreadHardErrorMode(HardErrors);
#endif
        return FALSE;
    }


    /* Clear dirty bitmap. */
    RtlClearAllBits(&RegistryHive->DirtyVector);
    RegistryHive->DirtyCount = 0;

#if !defined(CMLIB_HOST) && !defined(_BLDR_)
    IoSetThreadHardErrorMode(HardErrors);
#endif
    return TRUE;
}

/**
 * @unimplemented
 * @brief
 * Determines whether a registry hive needs
 * to be shrinked or not based on its overall
 * size of the hive space to avoid unnecessary
 * bloat.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where hive
 * shrinking is to be determined.
 *
 * @return
 * Returns TRUE if hive shrinking needs to be
 * done, FALSE otherwise.
 */
BOOLEAN
CMAPI
HvHiveWillShrink(
    _In_ PHHIVE RegistryHive)
{
    /* No shrinking yet */
    UNIMPLEMENTED_ONCE;
    return FALSE;
}

/**
 * @brief
 * Writes data to a registry hive. Unlike
 * HvSyncHive, this function just writes
 * the wholy registry data to a primary hive,
 * ignoring if a certain data block is dirty
 * or not.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where data
 * is be written into.
 *
 * @return
 * Returns TRUE if hive writing has succeeded,
 * FALSE otherwise.
 */
BOOLEAN
CMAPI
HvWriteHive(
    _In_ PHHIVE RegistryHive)
{
    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->Signature == HV_HHIVE_SIGNATURE);

#if !defined(_BLDR_)
    /* Update hive header modification time */
    KeQuerySystemTime(&RegistryHive->BaseBlock->TimeStamp);
#endif

    /* Update hive file */
    if (!HvpWriteHive(RegistryHive, FALSE, HFILE_TYPE_PRIMARY))
    {
        DPRINT1("Failed to write the hive\n");
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief
 * Synchronizes a hive with recovered
 * data during a healing/resuscitation
 * operation of the registry.
 *
 * @param[in] RegistryHive
 * A pointer to a hive descriptor where data
 * syncing is to be done.
 *
 * @return
 * Returns TRUE if hive syncing during recovery
 * succeeded, FALSE otherwise.
 */
BOOLEAN
CMAPI
HvSyncHiveFromRecover(
    _In_ PHHIVE RegistryHive)
{
    ASSERT(!RegistryHive->ReadOnly);
    ASSERT(RegistryHive->Signature == HV_HHIVE_SIGNATURE);

    /* Call the private API call to do the deed for us */
    return HvpWriteHive(RegistryHive, TRUE, HFILE_TYPE_PRIMARY);
}

/* EOF */
