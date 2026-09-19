/*
 * PROJECT:     ReactOS RISC-V Platform Support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bounded read-only flattened-device-tree navigation
 *
 * Every function validates offsets against the structure and strings blocks
 * before dereferencing. Node handles are byte offsets of FDT_BEGIN_NODE
 * tokens inside the structure block; RISCV_FDT_NO_NODE means "none".
 * This header is shared by FreeLdr, the kernel and the HAL, so it depends
 * only on the base NT scalar types and <reactos/riscv64/fdt.h>.
 */

#pragma once

#include <reactos/riscv64/fdt.h>

#define RISCV_FDT_NO_NODE 0xFFFFFFFFUL

typedef struct _RISCV_FDT
{
    const UCHAR *Blob;
    ULONG TotalSize;
    ULONG StructureOffset;
    ULONG StructureSize;
    ULONG StringsOffset;
    ULONG StringsSize;
} RISCV_FDT, *PRISCV_FDT;

static __inline BOOLEAN
RiscvFdtOpen(
    _In_reads_bytes_(AvailableSize) const VOID *Blob,
    _In_ SIZE_T AvailableSize,
    _Out_ PRISCV_FDT Fdt)
{
    const UCHAR *Bytes = (const UCHAR *)Blob;
    ULONG TotalSize;

    if ((Fdt == NULL) || !RiscvFdtValidateHeader(Blob, AvailableSize, &TotalSize))
        return FALSE;

    Fdt->Blob = Bytes;
    Fdt->TotalSize = TotalSize;
    Fdt->StructureOffset = RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRUCTURE);
    Fdt->StructureSize = RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRUCTURE_SIZE);
    Fdt->StringsOffset = RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRINGS);
    Fdt->StringsSize = RiscvFdtReadBigEndian32(Bytes + RISCV_FDT_OFFSET_STRINGS_SIZE);
    return (Fdt->StructureSize & 3) == 0;
}

/* Read the token at a structure-block offset. Returns 0 (no token) when
 * the offset is not inside the structure block. */
static __inline ULONG
RiscvFdtToken(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Offset)
{
    if ((Offset & 3) || (Offset > Fdt->StructureSize - sizeof(ULONG)) ||
        (Fdt->StructureSize < sizeof(ULONG)))
    {
        return 0;
    }
    return RiscvFdtReadBigEndian32(Fdt->Blob + Fdt->StructureOffset + Offset);
}

/* Length of the NUL-terminated string at a structure-block offset,
 * including the terminator, or 0 if it is not terminated within bounds. */
static __inline ULONG
RiscvFdtStructureStringLength(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Offset)
{
    const CHAR *String = (const CHAR *)(Fdt->Blob + Fdt->StructureOffset + Offset);
    ULONG Index;

    if (Offset >= Fdt->StructureSize)
        return 0;
    for (Index = 0; Index < Fdt->StructureSize - Offset; ++Index)
    {
        if (String[Index] == '\0')
            return Index + 1;
    }
    return 0;
}

/* Returns the structure offset following the token that starts at Offset,
 * or RISCV_FDT_NO_NODE for a malformed or terminal token. */
static __inline ULONG
RiscvFdtSkipToken(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Offset)
{
    ULONG Token = RiscvFdtToken(Fdt, Offset);
    ULONG Next = Offset + sizeof(ULONG);
    ULONG Length;

    switch (Token)
    {
        case RISCV_FDT_BEGIN_NODE:
            Length = RiscvFdtStructureStringLength(Fdt, Next);
            if (Length == 0)
                return RISCV_FDT_NO_NODE;
            Next += Length;
            return (Next + 3) & ~3UL;

        case RISCV_FDT_PROPERTY:
            if (Next > Fdt->StructureSize - 2 * sizeof(ULONG))
                return RISCV_FDT_NO_NODE;
            Length = RiscvFdtReadBigEndian32(Fdt->Blob + Fdt->StructureOffset + Next);
            Next += 2 * sizeof(ULONG);
            if (Length > Fdt->StructureSize - Next)
                return RISCV_FDT_NO_NODE;
            Next += Length;
            return (Next + 3) & ~3UL;

        case RISCV_FDT_END_NODE:
        case RISCV_FDT_NOP:
            return Next;

        default:
            return RISCV_FDT_NO_NODE;
    }
}

/* Node name (without the leading path) and its length excluding the NUL. */
static __inline const CHAR *
RiscvFdtNodeName(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG NodeOffset,
    _Out_ PULONG NameLength)
{
    ULONG Length;

    *NameLength = 0;
    if (RiscvFdtToken(Fdt, NodeOffset) != RISCV_FDT_BEGIN_NODE)
        return NULL;
    Length = RiscvFdtStructureStringLength(Fdt, NodeOffset + sizeof(ULONG));
    if (Length == 0)
        return NULL;
    *NameLength = Length - 1;
    return (const CHAR *)(Fdt->Blob + Fdt->StructureOffset + NodeOffset + sizeof(ULONG));
}

/* The root node offset, or RISCV_FDT_NO_NODE. */
static __inline ULONG
RiscvFdtRootNode(
    _In_ const RISCV_FDT *Fdt)
{
    ULONG Offset = 0;

    while (RiscvFdtToken(Fdt, Offset) == RISCV_FDT_NOP)
        Offset += sizeof(ULONG);
    return (RiscvFdtToken(Fdt, Offset) == RISCV_FDT_BEGIN_NODE) ? Offset : RISCV_FDT_NO_NODE;
}

/* First child node of Parent, or RISCV_FDT_NO_NODE. */
static __inline ULONG
RiscvFdtFirstChild(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Parent)
{
    ULONG Offset;

    if (RiscvFdtToken(Fdt, Parent) != RISCV_FDT_BEGIN_NODE)
        return RISCV_FDT_NO_NODE;
    Offset = RiscvFdtSkipToken(Fdt, Parent);
    while (Offset != RISCV_FDT_NO_NODE)
    {
        switch (RiscvFdtToken(Fdt, Offset))
        {
            case RISCV_FDT_BEGIN_NODE:
                return Offset;
            case RISCV_FDT_PROPERTY:
            case RISCV_FDT_NOP:
                Offset = RiscvFdtSkipToken(Fdt, Offset);
                break;
            default:
                return RISCV_FDT_NO_NODE;
        }
    }
    return RISCV_FDT_NO_NODE;
}

/* Next sibling of Node, or RISCV_FDT_NO_NODE. */
static __inline ULONG
RiscvFdtNextSibling(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node)
{
    ULONG Offset;
    LONG Depth = 0;

    if (RiscvFdtToken(Fdt, Node) != RISCV_FDT_BEGIN_NODE)
        return RISCV_FDT_NO_NODE;
    Offset = Node;
    while (Offset != RISCV_FDT_NO_NODE)
    {
        ULONG Token = RiscvFdtToken(Fdt, Offset);

        if (Token == RISCV_FDT_BEGIN_NODE)
        {
            if ((Depth == 0) && (Offset != Node))
                return Offset;
            ++Depth;
        }
        else if (Token == RISCV_FDT_END_NODE)
        {
            --Depth;
            if (Depth < 0)
                return RISCV_FDT_NO_NODE;
        }
        else if ((Token != RISCV_FDT_PROPERTY) && (Token != RISCV_FDT_NOP))
        {
            return RISCV_FDT_NO_NODE;
        }
        Offset = RiscvFdtSkipToken(Fdt, Offset);
    }
    return RISCV_FDT_NO_NODE;
}

/* Property value and length, or NULL when absent or malformed. */
static __inline const VOID *
RiscvFdtGetProperty(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name,
    _Out_ PULONG Length)
{
    ULONG Offset;

    *Length = 0;
    if (RiscvFdtToken(Fdt, Node) != RISCV_FDT_BEGIN_NODE)
        return NULL;
    Offset = RiscvFdtSkipToken(Fdt, Node);
    while (Offset != RISCV_FDT_NO_NODE)
    {
        ULONG Token = RiscvFdtToken(Fdt, Offset);

        if (Token == RISCV_FDT_PROPERTY)
        {
            const UCHAR *Header = Fdt->Blob + Fdt->StructureOffset + Offset + sizeof(ULONG);
            ULONG ValueLength, NameOffset, Index;
            const CHAR *Candidate;

            if (Offset > Fdt->StructureSize - 3 * sizeof(ULONG))
                return NULL;
            ValueLength = RiscvFdtReadBigEndian32(Header);
            NameOffset = RiscvFdtReadBigEndian32(Header + sizeof(ULONG));
            if (NameOffset >= Fdt->StringsSize)
                return NULL;
            Candidate = (const CHAR *)(Fdt->Blob + Fdt->StringsOffset + NameOffset);
            for (Index = 0; Index < Fdt->StringsSize - NameOffset; ++Index)
            {
                if (Candidate[Index] != Name[Index])
                    break;
                if (Name[Index] == '\0')
                {
                    if (ValueLength > Fdt->StructureSize - (Offset + 3 * sizeof(ULONG)))
                        return NULL;
                    *Length = ValueLength;
                    return Header + 2 * sizeof(ULONG);
                }
            }
        }
        else if (Token != RISCV_FDT_NOP)
        {
            /* Properties precede child nodes; a BEGIN_NODE or END_NODE ends them. */
            return NULL;
        }
        Offset = RiscvFdtSkipToken(Fdt, Offset);
    }
    return NULL;
}

static __inline BOOLEAN
RiscvFdtReadU32(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name,
    _Out_ PULONG Value)
{
    ULONG Length;
    const VOID *Property = RiscvFdtGetProperty(Fdt, Node, Name, &Length);

    if ((Property == NULL) || (Length != sizeof(ULONG)))
        return FALSE;
    *Value = RiscvFdtReadBigEndian32(Property);
    return TRUE;
}

/* Read Cells (1 or 2) big-endian 32-bit cells at cell index Index. */
static __inline BOOLEAN
RiscvFdtReadCells(
    _In_reads_bytes_(Length) const VOID *Property,
    _In_ ULONG Length,
    _In_ ULONG Index,
    _In_ ULONG Cells,
    _Out_ PULONGLONG Value)
{
    const UCHAR *Bytes = (const UCHAR *)Property;
    ULONGLONG Result = 0;
    ULONG Cell;

    if ((Cells == 0) || (Cells > 2) || (Index > MAXULONG / sizeof(ULONG)) ||
        ((Index + Cells) * sizeof(ULONG) > Length))
    {
        return FALSE;
    }
    for (Cell = 0; Cell < Cells; ++Cell)
        Result = (Result << 32) | RiscvFdtReadBigEndian32(Bytes + (Index + Cell) * sizeof(ULONG));
    *Value = Result;
    return TRUE;
}

/* #address-cells/#size-cells of a node, with the specification defaults. */
static __inline VOID
RiscvFdtGetCellCounts(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _Out_ PULONG AddressCells,
    _Out_ PULONG SizeCells)
{
    if (!RiscvFdtReadU32(Fdt, Node, "#address-cells", AddressCells))
        *AddressCells = 2;
    if (!RiscvFdtReadU32(Fdt, Node, "#size-cells", SizeCells))
        *SizeCells = 1;
}

/* Entry Index of a node's "reg" property, decoded with the parent's cells. */
static __inline BOOLEAN
RiscvFdtReadReg(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_ ULONG Parent,
    _In_ ULONG Index,
    _Out_ PULONGLONG Address,
    _Out_ PULONGLONG Size)
{
    ULONG AddressCells, SizeCells, Length, Stride;
    const VOID *Property;

    RiscvFdtGetCellCounts(Fdt, Parent, &AddressCells, &SizeCells);
    if ((AddressCells == 0) || (AddressCells > 2) || (SizeCells > 2))
        return FALSE;
    Property = RiscvFdtGetProperty(Fdt, Node, "reg", &Length);
    if (Property == NULL)
        return FALSE;
    Stride = AddressCells + SizeCells;
    if (!RiscvFdtReadCells(Property, Length, Index * Stride, AddressCells, Address))
        return FALSE;
    if (SizeCells == 0)
    {
        *Size = 0;
        return TRUE;
    }
    return RiscvFdtReadCells(Property, Length, Index * Stride + AddressCells, SizeCells, Size);
}

/* TRUE when a string-list property contains Wanted as one whole entry. */
static __inline BOOLEAN
RiscvFdtStringListContains(
    _In_reads_bytes_(Length) const VOID *Property,
    _In_ ULONG Length,
    _In_z_ const CHAR *Wanted)
{
    const CHAR *List = (const CHAR *)Property;
    ULONG Start = 0, Index;

    if (Property == NULL)
        return FALSE;
    for (Index = 0; Index < Length; ++Index)
    {
        if (List[Index] == '\0')
        {
            ULONG Position;

            for (Position = 0; Start + Position <= Index; ++Position)
            {
                if (List[Start + Position] != Wanted[Position])
                    break;
                if (Wanted[Position] == '\0')
                    return TRUE;
            }
            Start = Index + 1;
        }
    }
    return FALSE;
}

/* Iterate the entries of a string-list property. *Cursor starts at 0. */
static __inline const CHAR *
RiscvFdtNextString(
    _In_reads_bytes_(Length) const VOID *Property,
    _In_ ULONG Length,
    _Inout_ PULONG Cursor,
    _Out_ PULONG EntryLength)
{
    const CHAR *List = (const CHAR *)Property;
    ULONG Start = *Cursor, Index;

    *EntryLength = 0;
    if ((Property == NULL) || (Start >= Length))
        return NULL;
    for (Index = Start; Index < Length; ++Index)
    {
        if (List[Index] == '\0')
        {
            *EntryLength = Index - Start;
            *Cursor = Index + 1;
            return List + Start;
        }
    }
    return NULL;
}

/* Compare a node name against a path component. A component without a
 * unit address ("serial") matches "serial@10000000"; one with a unit
 * address must match exactly. */
static __inline BOOLEAN
RiscvFdtNameMatches(
    _In_reads_(NameLength) const CHAR *Name,
    _In_ ULONG NameLength,
    _In_reads_(ComponentLength) const CHAR *Component,
    _In_ ULONG ComponentLength)
{
    ULONG Index;
    BOOLEAN ComponentHasUnit = FALSE;

    for (Index = 0; Index < ComponentLength; ++Index)
    {
        if (Component[Index] == '@')
            ComponentHasUnit = TRUE;
    }
    if (ComponentLength > NameLength)
        return FALSE;
    for (Index = 0; Index < ComponentLength; ++Index)
    {
        if (Name[Index] != Component[Index])
            return FALSE;
    }
    if (ComponentLength == NameLength)
        return TRUE;
    return !ComponentHasUnit && (Name[ComponentLength] == '@');
}

/* Resolve an absolute path ("/soc/serial@10000000"), stopping at an
 * optional ':' suffix. Returns the node and, optionally, its parent. */
static __inline BOOLEAN
RiscvFdtFindNode(
    _In_ const RISCV_FDT *Fdt,
    _In_z_ const CHAR *Path,
    _Out_ PULONG Node,
    _Out_opt_ PULONG Parent)
{
    ULONG Current = RiscvFdtRootNode(Fdt);
    ULONG ParentNode = RISCV_FDT_NO_NODE;
    ULONG Position = 0;

    *Node = RISCV_FDT_NO_NODE;
    if (Parent)
        *Parent = RISCV_FDT_NO_NODE;
    if ((Current == RISCV_FDT_NO_NODE) || (Path[0] != '/'))
        return FALSE;
    Position = 1;
    for (;;)
    {
        ULONG ComponentLength = 0, Child;
        const CHAR *Component = Path + Position;

        while ((Component[ComponentLength] != '\0') &&
               (Component[ComponentLength] != '/') &&
               (Component[ComponentLength] != ':'))
        {
            ++ComponentLength;
        }
        if (ComponentLength == 0)
            break;

        for (Child = RiscvFdtFirstChild(Fdt, Current);
             Child != RISCV_FDT_NO_NODE;
             Child = RiscvFdtNextSibling(Fdt, Child))
        {
            ULONG NameLength;
            const CHAR *Name = RiscvFdtNodeName(Fdt, Child, &NameLength);

            if (Name && RiscvFdtNameMatches(Name, NameLength, Component, ComponentLength))
                break;
        }
        if (Child == RISCV_FDT_NO_NODE)
            return FALSE;
        ParentNode = Current;
        Current = Child;
        Position += ComponentLength;
        if (Path[Position] == '/')
            ++Position;
        else
            break;
    }

    *Node = Current;
    if (Parent)
        *Parent = ParentNode;
    return TRUE;
}

/* Resolve a path that may be an alias name ("serial0") or an absolute path,
 * with an optional ":options" suffix that is returned separately. */
static __inline BOOLEAN
RiscvFdtResolvePath(
    _In_ const RISCV_FDT *Fdt,
    _In_z_ const CHAR *PathOrAlias,
    _Out_ PULONG Node,
    _Out_opt_ PULONG Parent,
    _Out_opt_ const CHAR **Options)
{
    ULONG Index = 0;

    if (Options)
        *Options = NULL;
    while ((PathOrAlias[Index] != '\0') && (PathOrAlias[Index] != ':'))
        ++Index;
    if (Options && (PathOrAlias[Index] == ':'))
        *Options = PathOrAlias + Index + 1;

    if (PathOrAlias[0] == '/')
        return RiscvFdtFindNode(Fdt, PathOrAlias, Node, Parent);

    {
        ULONG Aliases, Length;
        const CHAR *Target;
        CHAR AliasName[64];

        if (Index >= sizeof(AliasName))
            return FALSE;
        for (Length = 0; Length < Index; ++Length)
            AliasName[Length] = PathOrAlias[Length];
        AliasName[Index] = '\0';
        if (!RiscvFdtFindNode(Fdt, "/aliases", &Aliases, NULL))
            return FALSE;
        Target = (const CHAR *)RiscvFdtGetProperty(Fdt, Aliases, AliasName, &Length);
        if ((Target == NULL) || (Length == 0) || (Target[Length - 1] != '\0'))
            return FALSE;
        return RiscvFdtFindNode(Fdt, Target, Node, Parent);
    }
}
