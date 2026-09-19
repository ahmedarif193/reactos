/*
 * PROJECT:     ReactOS RISC-V Platform Support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bounded flattened-device-tree header validation
 */

#pragma once

#define RISCV_FDT_MAGIC                   0xD00DFEEDUL
#define RISCV_FDT_HEADER_SIZE             40UL
#define RISCV_FDT_SUPPORTED_VERSION       17UL
#define RISCV_FDT_MAXIMUM_SIZE            (16UL * 1024UL * 1024UL)

#define RISCV_FDT_BEGIN_NODE              1UL
#define RISCV_FDT_END_NODE                2UL
#define RISCV_FDT_PROPERTY                3UL
#define RISCV_FDT_NOP                     4UL
#define RISCV_FDT_END                     9UL

#define RISCV_FDT_OFFSET_MAGIC            0UL
#define RISCV_FDT_OFFSET_TOTAL_SIZE       4UL
#define RISCV_FDT_OFFSET_STRUCTURE        8UL
#define RISCV_FDT_OFFSET_STRINGS          12UL
#define RISCV_FDT_OFFSET_RESERVATIONS     16UL
#define RISCV_FDT_OFFSET_VERSION          20UL
#define RISCV_FDT_OFFSET_LAST_VERSION     24UL
#define RISCV_FDT_OFFSET_STRINGS_SIZE     32UL
#define RISCV_FDT_OFFSET_STRUCTURE_SIZE   36UL

static __inline ULONG
RiscvFdtReadBigEndian32(
    _In_reads_bytes_(sizeof(ULONG)) const VOID *Address)
{
    const UCHAR *Bytes = Address;

    return ((ULONG)Bytes[0] << 24) |
           ((ULONG)Bytes[1] << 16) |
           ((ULONG)Bytes[2] << 8) |
           Bytes[3];
}

static __inline BOOLEAN
RiscvFdtRangeIsValid(
    _In_ ULONG TotalSize,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    return (Offset <= TotalSize) && (Size <= TotalSize - Offset);
}

static __inline BOOLEAN
RiscvFdtValidateHeader(
    _In_reads_bytes_(AvailableSize) const VOID *Blob,
    _In_ SIZE_T AvailableSize,
    _Out_opt_ PULONG TotalSize)
{
    const UCHAR *Bytes = Blob;
    ULONG Size, StructureOffset, StringsOffset, ReservationsOffset;
    ULONG StructureSize, StringsSize, Version, LastCompatibleVersion;

    if ((Blob == NULL) || (AvailableSize < RISCV_FDT_HEADER_SIZE))
        return FALSE;
    if (RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_MAGIC) !=
        RISCV_FDT_MAGIC)
    {
        return FALSE;
    }

    Size = RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_TOTAL_SIZE);
    if ((Size < RISCV_FDT_HEADER_SIZE) ||
        (Size > RISCV_FDT_MAXIMUM_SIZE) ||
        (Size > AvailableSize))
    {
        return FALSE;
    }

    Version = RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_VERSION);
    LastCompatibleVersion =
        RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_LAST_VERSION);
    if ((Version < RISCV_FDT_SUPPORTED_VERSION) ||
        (LastCompatibleVersion > RISCV_FDT_SUPPORTED_VERSION) ||
        (LastCompatibleVersion > Version))
    {
        return FALSE;
    }

    StructureOffset =
        RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRUCTURE);
    StructureSize =
        RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRUCTURE_SIZE);
    StringsOffset =
        RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRINGS);
    StringsSize =
        RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRINGS_SIZE);
    ReservationsOffset =
        RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_RESERVATIONS);
    if ((StructureOffset & 3) || (ReservationsOffset & 7) ||
        (StructureOffset < RISCV_FDT_HEADER_SIZE) ||
        (StringsOffset < RISCV_FDT_HEADER_SIZE) ||
        (ReservationsOffset < RISCV_FDT_HEADER_SIZE) ||
        !RiscvFdtRangeIsValid(Size, StructureOffset, StructureSize) ||
        !RiscvFdtRangeIsValid(Size, StringsOffset, StringsSize) ||
        (ReservationsOffset > Size - 16))
    {
        return FALSE;
    }

    if (TotalSize != NULL)
        *TotalSize = Size;
    return TRUE;
}
