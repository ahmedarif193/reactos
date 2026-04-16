/*
 * NTFS multi-sector transfer (MST) fixup for the ntfslx ReactOS driver.
 *
 * Pre-write fixup applies the update sequence array protection.
 * Post-write fixup restores the original data after write.
 *
 * The post-read fixup (NtfslxPostReadMstFixup) already exists in core.c.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NTFS_BLOCK_SIZE      512
#define NTFS_BLOCK_SIZE_BITS 9

/*
 * NtfslxPreWriteMstFixup - apply multi-sector transfer protection before write
 *
 * Saves the last USHORT of each 512-byte sector into the update sequence
 * array and replaces them with the update sequence number (USN).
 *
 * The USN is cyclically incremented (skipping 0 and 0xFFFF).
 *
 * Returns STATUS_SUCCESS if fixup was applied.
 * Returns STATUS_INVALID_PARAMETER if the record should not be fixed up.
 */
NTSTATUS
NtfslxPreWriteMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record,
    _In_ ULONG Size)
{
    PUSHORT UsaPos;
    PUSHORT DataPos;
    USHORT UsaOffset;
    USHORT UsaCount;
    USHORT Usn;
    USHORT LeUsn;

    if (Record == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Do not fixup BAAD or HOLE records */
    if (Record->Magic == NTFSLX_RECORD_MAGIC_BAAD ||
        Record->Magic == NTFSLX_RECORD_MAGIC_HOLE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    UsaOffset = Record->UsaOffset;
    /* UsaCount includes the USN itself; subtract 1 for fixup count */
    UsaCount = Record->UsaCount - 1;

    /* Validate size and alignment */
    if ((Size & (NTFS_BLOCK_SIZE - 1)) != 0 ||
        (UsaOffset & 1) != 0 ||
        (ULONG)UsaOffset + (ULONG)UsaCount * 2 > Size ||
        (Size >> NTFS_BLOCK_SIZE_BITS) != UsaCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Position of USN in the update sequence array */
    UsaPos = (PUSHORT)((PUCHAR)Record + UsaOffset);

    /*
     * Cyclically increment USN, skipping 0 and 0xFFFF.
     */
    Usn = *UsaPos + 1;
    if (Usn == 0xFFFF || Usn == 0)
        Usn = 1;
    LeUsn = Usn;
    *UsaPos = LeUsn;

    /* First USHORT that needs fixup: last two bytes of first sector */
    DataPos = (PUSHORT)Record + NTFS_BLOCK_SIZE / sizeof(USHORT) - 1;

    while (UsaCount--)
    {
        /* Save original data into USA entry */
        *(++UsaPos) = *DataPos;
        /* Replace with USN */
        *DataPos = LeUsn;
        /* Advance to next sector */
        DataPos += NTFS_BLOCK_SIZE / sizeof(USHORT);
    }

    return STATUS_SUCCESS;
}

/*
 * NtfslxPostWriteMstFixup - restore original data after write
 *
 * Reverses NtfslxPreWriteMstFixup by copying the saved values from the
 * update sequence array back into the last USHORT of each sector.
 *
 * No error checking - caller must have successfully applied pre-write fixup.
 */
VOID
NtfslxPostWriteMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record)
{
    PUSHORT UsaPos;
    PUSHORT DataPos;
    USHORT UsaOffset;
    USHORT UsaCount;

    UsaOffset = Record->UsaOffset;
    UsaCount = Record->UsaCount - 1;

    /* Position of USN in the update sequence array */
    UsaPos = (PUSHORT)((PUCHAR)Record + UsaOffset);

    /* First USHORT that needs restoring */
    DataPos = (PUSHORT)Record + NTFS_BLOCK_SIZE / sizeof(USHORT) - 1;

    while (UsaCount--)
    {
        /* Restore saved data from USA */
        *DataPos = *(++UsaPos);
        /* Advance to next sector */
        DataPos += NTFS_BLOCK_SIZE / sizeof(USHORT);
    }
}
