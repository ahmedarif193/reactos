#include "mkntfsimg.h"

#define NTFS_BLOCK_SIZE      512
#define NTFS_BLOCK_SIZE_BITS 9

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

    if (Record->Magic == NTFSLX_RECORD_MAGIC_BAAD ||
        Record->Magic == NTFSLX_RECORD_MAGIC_HOLE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    UsaOffset = Record->UsaOffset;
    UsaCount = Record->UsaCount - 1;

    if ((Size & (NTFS_BLOCK_SIZE - 1)) != 0 ||
        (UsaOffset & 1) != 0 ||
        (ULONG)UsaOffset + (ULONG)UsaCount * 2 > Size ||
        (Size >> NTFS_BLOCK_SIZE_BITS) != UsaCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    UsaPos = (PUSHORT)((PUCHAR)Record + UsaOffset);

    Usn = *UsaPos + 1;
    if (Usn == 0xFFFF || Usn == 0)
        Usn = 1;
    LeUsn = Usn;
    *UsaPos = LeUsn;

    DataPos = (PUSHORT)Record + NTFS_BLOCK_SIZE / sizeof(USHORT) - 1;

    while (UsaCount--)
    {
        *(++UsaPos) = *DataPos;
        *DataPos = LeUsn;
        DataPos += NTFS_BLOCK_SIZE / sizeof(USHORT);
    }

    return STATUS_SUCCESS;
}

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

    UsaPos = (PUSHORT)((PUCHAR)Record + UsaOffset);

    DataPos = (PUSHORT)Record + NTFS_BLOCK_SIZE / sizeof(USHORT) - 1;

    while (UsaCount--)
    {
        *DataPos = *(++UsaPos);
        DataPos += NTFS_BLOCK_SIZE / sizeof(USHORT);
    }
}
