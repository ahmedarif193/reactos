/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Retained device-tree platform discovery
 */

#include <ntifs.h>
#include <reactos/riscv64/fdt.h>
#include "halp.h"

static
BOOLEAN
HalpRiscvBoundedStringEquals(
    _In_reads_bytes_(Available) const CHAR *String,
    _In_ SIZE_T Available,
    _In_z_ const CHAR *Expected,
    _Out_opt_ PSIZE_T EncodedLength)
{
    SIZE_T Index;

    for (Index = 0; Index < Available; ++Index)
    {
        if (String[Index] != Expected[Index])
            return FALSE;
        if (String[Index] == ANSI_NULL)
        {
            if (EncodedLength != NULL)
                *EncodedLength = Index + 1;
            return TRUE;
        }
    }
    return FALSE;
}

static
BOOLEAN
HalpRiscvFindStringEnd(
    _In_reads_bytes_(Available) const CHAR *String,
    _In_ SIZE_T Available,
    _Out_ PSIZE_T EncodedLength)
{
    SIZE_T Index;

    for (Index = 0; Index < Available; ++Index)
    {
        if (String[Index] == ANSI_NULL)
        {
            *EncodedLength = Index + 1;
            return TRUE;
        }
    }
    return FALSE;
}

BOOLEAN
HalpRiscvReadTimebaseFrequency(
    _In_reads_bytes_(DeviceTreeSize) const VOID *DeviceTree,
    _In_ SIZE_T DeviceTreeSize,
    _Out_ PULONG64 Frequency)
{
    const UCHAR *Blob = DeviceTree;
    const UCHAR *Structure, *Strings;
    ULONG TotalSize, StructureOffset, StructureSize;
    ULONG StringsOffset, StringsSize, Offset = 0;
    LONG Depth = -1;
    LONG CpusDepth = -1;
    BOOLEAN FoundCpus = FALSE, FoundFrequency = FALSE, FoundEnd = FALSE;
    ULONG64 Value = 0;

    if ((Frequency == NULL) ||
        !RiscvFdtValidateHeader(DeviceTree,
                                DeviceTreeSize,
                                &TotalSize) ||
        (TotalSize != DeviceTreeSize))
    {
        return FALSE;
    }

    StructureOffset =
        RiscvFdtReadBigEndian32(Blob + RISCV_FDT_OFFSET_STRUCTURE);
    StructureSize =
        RiscvFdtReadBigEndian32(Blob + RISCV_FDT_OFFSET_STRUCTURE_SIZE);
    StringsOffset =
        RiscvFdtReadBigEndian32(Blob + RISCV_FDT_OFFSET_STRINGS);
    StringsSize =
        RiscvFdtReadBigEndian32(Blob + RISCV_FDT_OFFSET_STRINGS_SIZE);
    Structure = Blob + StructureOffset;
    Strings = Blob + StringsOffset;

    if (StructureSize < sizeof(ULONG))
        return FALSE;

    while (Offset <= StructureSize - sizeof(ULONG))
    {
        ULONG Token = RiscvFdtReadBigEndian32(Structure + Offset);
        SIZE_T EncodedLength;

        Offset += sizeof(ULONG);
        switch (Token)
        {
            case RISCV_FDT_BEGIN_NODE:
                if (!HalpRiscvFindStringEnd(
                        (const CHAR *)Structure + Offset,
                        StructureSize - Offset,
                        &EncodedLength))
                {
                    return FALSE;
                }
                ++Depth;
                if ((Depth == 1) &&
                    HalpRiscvBoundedStringEquals(
                        (const CHAR *)Structure + Offset,
                        EncodedLength,
                        "cpus",
                        NULL))
                {
                    if (FoundCpus)
                        return FALSE;
                    FoundCpus = TRUE;
                    CpusDepth = Depth;
                }
                if (EncodedLength > StructureSize - Offset)
                    return FALSE;
                Offset += (ULONG)EncodedLength;
                Offset = (Offset + 3) & ~3UL;
                if (Offset > StructureSize)
                    return FALSE;
                break;

            case RISCV_FDT_END_NODE:
                if (Depth < 0)
                    return FALSE;
                if (Depth == CpusDepth)
                    CpusDepth = -1;
                --Depth;
                break;

            case RISCV_FDT_PROPERTY:
            {
                ULONG Length, NameOffset;
                const CHAR *Name;

                if (Offset > StructureSize - 2 * sizeof(ULONG))
                    return FALSE;
                Length = RiscvFdtReadBigEndian32(Structure + Offset);
                NameOffset = RiscvFdtReadBigEndian32(
                    Structure + Offset + sizeof(ULONG));
                Offset += 2 * sizeof(ULONG);
                if ((Length > StructureSize - Offset) ||
                    (NameOffset >= StringsSize) ||
                    !HalpRiscvFindStringEnd(
                        (const CHAR *)Strings + NameOffset,
                        StringsSize - NameOffset,
                        &EncodedLength))
                {
                    return FALSE;
                }
                Name = (const CHAR *)Strings + NameOffset;
                if ((Depth == CpusDepth) &&
                    HalpRiscvBoundedStringEquals(Name,
                                                 EncodedLength,
                                                 "timebase-frequency",
                                                 NULL))
                {
                    if (FoundFrequency || (Length != sizeof(ULONG)))
                        return FALSE;
                    Value = RiscvFdtReadBigEndian32(Structure + Offset);
                    if (Value == 0)
                        return FALSE;
                    FoundFrequency = TRUE;
                }
                Offset += Length;
                Offset = (Offset + 3) & ~3UL;
                if (Offset > StructureSize)
                    return FALSE;
                break;
            }

            case RISCV_FDT_NOP:
                break;

            case RISCV_FDT_END:
                if (Depth != -1)
                    return FALSE;
                FoundEnd = TRUE;
                Offset = StructureSize;
                break;

            default:
                return FALSE;
        }
    }

    if (!FoundEnd || !FoundCpus || !FoundFrequency)
        return FALSE;
    *Frequency = Value;
    return TRUE;
}
