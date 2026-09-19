/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V boot-hart identification from the device tree and SBI
 */

#include <ntoskrnl.h>
#include <reactos/riscv64/fdtlib.h>
#define NDEBUG
#include <debug.h>

#define KI_SBI_EXT_BASE            0x10UL
#define KI_SBI_BASE_GET_SPEC_VERSION 0UL
#define KI_SBI_BASE_GET_IMPL_ID    1UL
#define KI_SBI_BASE_GET_IMPL_VERSION 2UL

KI_RISCV_PROCESSOR_FEATURES KiRiscvProcessorFeatures;

static const struct
{
    const CHAR *Name;
    ULONG Flag;
} KiRiscvFeatureTable[] =
{
    { "zicbom",      KI_RISCV_FEATURE_ZICBOM },
    { "zicboz",      KI_RISCV_FEATURE_ZICBOZ },
    { "zicbop",      KI_RISCV_FEATURE_ZICBOP },
    { "zihintpause", KI_RISCV_FEATURE_ZIHINTPAUSE },
    { "zicntr",      KI_RISCV_FEATURE_ZICNTR },
    { "zba",         KI_RISCV_FEATURE_ZBA },
    { "zbb",         KI_RISCV_FEATURE_ZBB },
    { "zbs",         KI_RISCV_FEATURE_ZBS },
    { "sstc",        KI_RISCV_FEATURE_SSTC },
    { "svadu",       KI_RISCV_FEATURE_SVADU },
    { "svpbmt",      KI_RISCV_FEATURE_SVPBMT },
    { "svnapot",     KI_RISCV_FEATURE_SVNAPOT },
    { "svinval",     KI_RISCV_FEATURE_SVINVAL },
    { "v",           KI_RISCV_FEATURE_V },
    { "h",           KI_RISCV_FEATURE_H },
};

static
VOID
KiRiscvCopyBoundedString(
    _Out_writes_z_(Size) PCHAR Destination,
    _In_ SIZE_T Size,
    _In_reads_(Length) const CHAR *Source,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    if (Length >= Size)
        Length = Size - 1;
    for (Index = 0; Index < Length; Index++)
    {
        if (Source[Index] == '\0')
            break;
        Destination[Index] = Source[Index];
    }
    Destination[Index] = '\0';
}

/* Record one extension name: set its policy bit and append it to the list. */
static
VOID
KiRiscvAddExtension(
    _Inout_ PKI_RISCV_PROCESSOR_FEATURES Features,
    _In_reads_(Length) const CHAR *Name,
    _In_ SIZE_T Length)
{
    SIZE_T Index, Used;

    if (Length == 0)
        return;
    for (Index = 0; Index < RTL_NUMBER_OF(KiRiscvFeatureTable); Index++)
    {
        const CHAR *Candidate = KiRiscvFeatureTable[Index].Name;
        SIZE_T Position;

        for (Position = 0; Position < Length; Position++)
        {
            if (Candidate[Position] != (CHAR)tolower((UCHAR)Name[Position]))
                break;
        }
        if ((Position == Length) && (Candidate[Length] == '\0'))
            Features->Flags |= KiRiscvFeatureTable[Index].Flag;
    }

    Used = strlen(Features->Extensions);
    if (Used + Length + 2 > sizeof(Features->Extensions))
        return;
    if (Used != 0)
        Features->Extensions[Used++] = ' ';
    for (Index = 0; Index < Length; Index++)
        Features->Extensions[Used + Index] = Name[Index];
    Features->Extensions[Used + Length] = '\0';
}

/* Deprecated "riscv,isa" form: rv64<single letters>[_<multi-letter>]... */
static
VOID
KiRiscvParseIsaString(
    _Inout_ PKI_RISCV_PROCESSOR_FEATURES Features,
    _In_reads_(Length) const CHAR *Isa,
    _In_ SIZE_T Length)
{
    SIZE_T Position = 0, Start;

    if ((Length >= 4) && (tolower((UCHAR)Isa[0]) == 'r') && (tolower((UCHAR)Isa[1]) == 'v'))
    {
        Position = 2;
        while ((Position < Length) && (Isa[Position] >= '0') && (Isa[Position] <= '9'))
            Position++;
        if (Features->IsaBase[0] == '\0')
        {
            KiRiscvCopyBoundedString(Features->IsaBase, sizeof(Features->IsaBase), Isa, Position);
            if ((Position < Length) && (Isa[Position] != '_'))
            {
                SIZE_T BaseLength = strlen(Features->IsaBase);

                if (BaseLength + 1 < sizeof(Features->IsaBase))
                {
                    Features->IsaBase[BaseLength] = (CHAR)tolower((UCHAR)Isa[Position]);
                    Features->IsaBase[BaseLength + 1] = '\0';
                }
            }
        }
    }

    /* Single-letter extensions until the first underscore. */
    while ((Position < Length) && (Isa[Position] != '\0') && (Isa[Position] != '_'))
    {
        KiRiscvAddExtension(Features, Isa + Position, 1);
        Position++;
    }

    /* Underscore-separated multi-letter extensions. */
    while ((Position < Length) && (Isa[Position] != '\0'))
    {
        if (Isa[Position] == '_')
        {
            Position++;
            continue;
        }
        Start = Position;
        while ((Position < Length) && (Isa[Position] != '\0') && (Isa[Position] != '_'))
            Position++;
        KiRiscvAddExtension(Features, Isa + Start, Position - Start);
    }
}

static
BOOLEAN
KiRiscvReadCellValue(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Node,
    _In_z_ const CHAR *Name,
    _Out_ PULONG64 Value)
{
    ULONG Length;
    const VOID *Property = RiscvFdtGetProperty(Fdt, Node, Name, &Length);

    *Value = 0;
    if ((Property == NULL) || ((Length != 4) && (Length != 8)))
        return FALSE;
    return RiscvFdtReadCells(Property, Length, 0, Length / 4, Value);
}

static
ULONG
KiRiscvFindBootHartNode(
    _In_ const RISCV_FDT *Fdt,
    _In_ ULONG Cpus,
    _In_ ULONG64 HartId)
{
    ULONG Child;

    for (Child = RiscvFdtFirstChild(Fdt, Cpus);
         Child != RISCV_FDT_NO_NODE;
         Child = RiscvFdtNextSibling(Fdt, Child))
    {
        ULONG NameLength;
        const CHAR *Name = RiscvFdtNodeName(Fdt, Child, &NameLength);
        ULONGLONG Address, Size;

        if ((Name == NULL) || !RiscvFdtNameMatches(Name, NameLength, "cpu", 3))
            continue;
        if (!RiscvFdtReadReg(Fdt, Child, Cpus, 0, &Address, &Size))
            continue;
        if (Address == HartId)
            return Child;
    }
    return RISCV_FDT_NO_NODE;
}

VOID
NTAPI
KiRiscvIdentifyProcessor(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRISCV64_LOADER_BLOCK RiscvBlock = &LoaderBlock->u.Riscv64;
    PKI_RISCV_PROCESSOR_FEATURES Features = &KiRiscvProcessorFeatures;
    RISCV_FDT Fdt;
    ULONG Cpus, Cpu, Length, Cursor, EntryLength;
    const VOID *Property;
    const CHAR *Entry;
    ULONG64 Value;
    KI_RISCV_SBI_RETURN Result;

    RtlZeroMemory(Features, sizeof(*Features));
    Features->HartId = RiscvBlock->BootHartId;

    Result = KiRiscvSbiCall(KI_SBI_EXT_BASE, KI_SBI_BASE_GET_SPEC_VERSION, 0, 0, 0);
    if (Result.Error == 0)
        Features->SbiSpecVersion = (ULONG)Result.Value;
    Result = KiRiscvSbiCall(KI_SBI_EXT_BASE, KI_SBI_BASE_GET_IMPL_ID, 0, 0, 0);
    if (Result.Error == 0)
        Features->SbiImplId = Result.Value;
    Result = KiRiscvSbiCall(KI_SBI_EXT_BASE, KI_SBI_BASE_GET_IMPL_VERSION, 0, 0, 0);
    if (Result.Error == 0)
        Features->SbiImplVersion = Result.Value;

    if (!(RiscvBlock->Flags & RISCV64_LOADER_FLAG_DEVICE_TREE_VALID) ||
        !RiscvFdtOpen((const VOID *)(ULONG_PTR)RiscvBlock->DeviceTree,
                      (SIZE_T)RiscvBlock->DeviceTreeSize,
                      &Fdt) ||
        !RiscvFdtFindNode(&Fdt, "/cpus", &Cpus, NULL))
    {
        return;
    }

    if (KiRiscvReadCellValue(&Fdt, Cpus, "timebase-frequency", &Value))
        Features->TimebaseFrequency = Value;

    Cpu = KiRiscvFindBootHartNode(&Fdt, Cpus, RiscvBlock->BootHartId);
    if (Cpu == RISCV_FDT_NO_NODE)
        return;

    Property = RiscvFdtGetProperty(&Fdt, Cpu, "riscv,isa-base", &Length);
    if (Property != NULL)
        KiRiscvCopyBoundedString(Features->IsaBase, sizeof(Features->IsaBase), Property, Length);
    Property = RiscvFdtGetProperty(&Fdt, Cpu, "mmu-type", &Length);
    if (Property != NULL)
        KiRiscvCopyBoundedString(Features->MmuType, sizeof(Features->MmuType), Property, Length);

    Property = RiscvFdtGetProperty(&Fdt, Cpu, "riscv,isa-extensions", &Length);
    if (Property != NULL)
    {
        Cursor = 0;
        while ((Entry = RiscvFdtNextString(Property, Length, &Cursor, &EntryLength)) != NULL)
            KiRiscvAddExtension(Features, Entry, EntryLength);
    }
    else
    {
        Property = RiscvFdtGetProperty(&Fdt, Cpu, "riscv,isa", &Length);
        if (Property != NULL)
            KiRiscvParseIsaString(Features, Property, Length);
    }

    if (KiRiscvReadCellValue(&Fdt, Cpu, "riscv,cbom-block-size", &Value))
        Features->CbomBlockSize = (ULONG)Value;
    if (KiRiscvReadCellValue(&Fdt, Cpu, "riscv,cboz-block-size", &Value))
        Features->CbozBlockSize = (ULONG)Value;
    if (KiRiscvReadCellValue(&Fdt, Cpu, "riscv,cbop-block-size", &Value))
        Features->CbopBlockSize = (ULONG)Value;
    Features->Valid = TRUE;
}

/* HAL bridge: the native image path imports functions only, not data. */
ULONG
NTAPI
KiRiscvQueryFeatureFlags(VOID)
{
    return KiRiscvProcessorFeatures.Flags;
}

VOID
NTAPI
KiRiscvReportProcessorFeatures(VOID)
{
    PKI_RISCV_PROCESSOR_FEATURES Features = &KiRiscvProcessorFeatures;

    DPRINT1("RISC-V boot hart %I64u: isa-base %s, mmu-type %s%s\n",
            Features->HartId,
            Features->IsaBase[0] ? Features->IsaBase : "<unknown>",
            Features->MmuType[0] ? Features->MmuType : "<unknown>",
            Features->Valid ? "" : " (cpu node not found)");
    DPRINT1("RISC-V extensions: %s\n",
            Features->Extensions[0] ? Features->Extensions : "<none advertised>");
    DPRINT1("RISC-V feature bits 0x%08lx, cbom %lu cboz %lu cbop %lu bytes\n",
            Features->Flags,
            Features->CbomBlockSize,
            Features->CbozBlockSize,
            Features->CbopBlockSize);
    DPRINT1("SBI spec %lu.%lu, impl id %Iu version 0x%Ix, timebase %I64u Hz\n",
            (Features->SbiSpecVersion >> 24) & 0x7F,
            Features->SbiSpecVersion & 0x00FFFFFF,
            Features->SbiImplId,
            Features->SbiImplVersion,
            Features->TimebaseFrequency);
}
