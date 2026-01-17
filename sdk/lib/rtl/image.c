/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/rtl/image.c
 * PURPOSE:         Image handling functions
 *                  Relocate functions were previously located in
 *                  ntoskrnl/ldr/loader.c and
 *                  dll/ntdll/ldr/utils.c files
 * PROGRAMMER:      Eric Kohl + original authors from loader.c and utils.c file
 *                  Aleksey Bragin
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#define RVA(m, b) ((PVOID)((ULONG_PTR)(b) + (ULONG_PTR)(m)))

/* ARM64 PE relocation types (from Microsoft PE/COFF specification) */
#ifndef IMAGE_REL_BASED_ARM64_PAGEBASE_REL21
#define IMAGE_REL_BASED_ARM64_PAGEBASE_REL21 11
#endif
#ifndef IMAGE_REL_BASED_ARM64_PAGEOFFSET_12A
#define IMAGE_REL_BASED_ARM64_PAGEOFFSET_12A 12
#endif

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
LdrVerifyMappedImageMatchesChecksum(
    IN PVOID BaseAddress,
    IN SIZE_T ImageSize,
    IN ULONG FileLength)
{
#if 0
    PIMAGE_NT_HEADERS Header;
    PUSHORT Ptr;
    ULONG Sum;
    ULONG CalcSum;
    ULONG HeaderSum;
    ULONG i;

    // HACK: Ignore calls with ImageSize=0. Should be fixed by new MM.
    if (ImageSize == 0) return TRUE;

    /* Get NT header to check if it's an image at all */
    Header = RtlImageNtHeader(BaseAddress);
    if (!Header) return FALSE;

    /* Get checksum to match */
    HeaderSum = Header->OptionalHeader.CheckSum;

    /* Zero checksum seems to be accepted */
    if (HeaderSum == 0) return TRUE;

    /* Calculate the checksum */
    Sum = 0;
    Ptr = (PUSHORT) BaseAddress;
    for (i = 0; i < ImageSize / sizeof (USHORT); i++)
    {
        Sum += (ULONG)*Ptr;
        if (HIWORD(Sum) != 0)
        {
            Sum = LOWORD(Sum) + HIWORD(Sum);
        }
        Ptr++;
    }

    if (ImageSize & 1)
    {
        Sum += (ULONG)*((PUCHAR)Ptr);
        if (HIWORD(Sum) != 0)
        {
            Sum = LOWORD(Sum) + HIWORD(Sum);
        }
    }

    CalcSum = (USHORT)(LOWORD(Sum) + HIWORD(Sum));

    /* Subtract image checksum from calculated checksum. */
    /* fix low word of checksum */
    if (LOWORD(CalcSum) >= LOWORD(HeaderSum))
    {
        CalcSum -= LOWORD(HeaderSum);
    }
    else
    {
        CalcSum = ((LOWORD(CalcSum) - LOWORD(HeaderSum)) & 0xFFFF) - 1;
    }

    /* Fix high word of checksum */
    if (LOWORD(CalcSum) >= HIWORD(HeaderSum))
    {
        CalcSum -= HIWORD(HeaderSum);
    }
    else
    {
        CalcSum = ((LOWORD(CalcSum) - HIWORD(HeaderSum)) & 0xFFFF) - 1;
    }

    /* Add file length */
    CalcSum += ImageSize;

    if (CalcSum != HeaderSum)
        DPRINT1("Image %p checksum mismatches! 0x%x != 0x%x, ImageSize %x, FileLen %x\n", BaseAddress, CalcSum, HeaderSum, ImageSize, FileLength);

    return (BOOLEAN)(CalcSum == HeaderSum);
#else
    /*
     * FIXME: Warning, this violates the PE standard and makes ReactOS drivers
     * and other system code when normally on Windows they would not, since
     * we do not write the checksum in them.
     * Our compilers should be made to write out the checksum and this function
     * should be enabled as to reject badly checksummed code.
     */
    return TRUE;
#endif
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
RtlpImageNtHeaderEx(
    _In_ ULONG Flags,
    _In_ PVOID Base,
    _In_ ULONG64 Size,
    _Out_ PIMAGE_NT_HEADERS *OutHeaders)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_DOS_HEADER DosHeader;
    BOOLEAN WantsRangeCheck;
    ULONG NtHeaderOffset;

    /* You must want NT Headers, no? */
    if (OutHeaders == NULL)
    {
        DPRINT1("OutHeaders is NULL\n");
        return STATUS_INVALID_PARAMETER;
    }

    /* Assume failure */
    *OutHeaders = NULL;

    /* Validate Flags */
    if (Flags & ~RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK)
    {
        DPRINT1("Invalid flags: 0x%lx\n", Flags);
        return STATUS_INVALID_PARAMETER;
    }

    /* Validate base */
    if ((Base == NULL) || (Base == (PVOID)-1))
    {
        DPRINT1("Invalid base address: %p\n", Base);
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if the caller wants range checks */
    WantsRangeCheck = !(Flags & RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK);
    if (WantsRangeCheck)
    {
        /* Make sure the image size is at least big enough for the DOS header */
        if (Size < sizeof(IMAGE_DOS_HEADER))
        {
            DPRINT1("Size too small\n");
            return STATUS_INVALID_IMAGE_FORMAT;
        }
    }

    /* Check if the DOS Signature matches */
    DosHeader = Base;
    if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    {
        /* Not a valid COFF */
        DPRINT1("Invalid image DOS signature!\n");
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Get the offset to the NT headers (and copy from LONG to ULONG) */
    NtHeaderOffset = DosHeader->e_lfanew;

    /* The offset must not be larger than 256MB, as a hard-coded check.
       In Windows this check is only done in user mode, not in kernel mode,
       but it shouldn't harm to have it anyway. Note that without this check,
       other overflow checks would become necessary! */
    if (NtHeaderOffset >= (256 * 1024 * 1024))
    {
        /* Fail */
        DPRINT1("NT headers offset is larger than 256MB!\n");
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Check if the caller wants validation */
    if (WantsRangeCheck)
    {
        /* Make sure the file header fits into the size */
        if ((NtHeaderOffset +
             RTL_SIZEOF_THROUGH_FIELD(IMAGE_NT_HEADERS, FileHeader)) >= Size)
        {
            /* Fail */
            DPRINT1("NT headers beyond image size!\n");
            return STATUS_INVALID_IMAGE_FORMAT;
        }
    }

    /* Now get a pointer to the NT Headers */
    NtHeaders = (PIMAGE_NT_HEADERS)((ULONG_PTR)Base + NtHeaderOffset);

    /* Check if the mapping is in user space */
    if (Base <= MmHighestUserAddress)
    {
        /* Make sure we don't overflow into kernel space */
        if ((PVOID)(NtHeaders + 1) > MmHighestUserAddress)
        {
            DPRINT1("Image overflows from user space into kernel space!\n");
            return STATUS_INVALID_IMAGE_FORMAT;
        }
    }

    /* Verify the PE Signature */
    if (NtHeaders->Signature != IMAGE_NT_SIGNATURE)
    {
        /* Fail */
        DPRINT1("Invalid image NT signature!\n");
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Now return success and the NT header */
    *OutHeaders = NtHeaders;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
PIMAGE_NT_HEADERS
NTAPI
RtlImageNtHeader(IN PVOID Base)
{
    PIMAGE_NT_HEADERS NtHeader;

    /* Call the new API */
    RtlImageNtHeaderEx(RTL_IMAGE_NT_HEADER_EX_FLAG_NO_RANGE_CHECK,
                       Base,
                       0,
                       &NtHeader);
    return NtHeader;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlImageDirectoryEntryToData(
    PVOID BaseAddress,
    BOOLEAN MappedAsImage,
    USHORT Directory,
    PULONG Size)
{
    PIMAGE_NT_HEADERS NtHeader;
    ULONG Va;

    /* Magic flag for non-mapped images. */
    if ((ULONG_PTR)BaseAddress & 1)
    {
        BaseAddress = (PVOID)((ULONG_PTR)BaseAddress & ~1);
        MappedAsImage = FALSE;
    }

    NtHeader = RtlImageNtHeader(BaseAddress);
    if (NtHeader == NULL)
        return NULL;

    if (NtHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        PIMAGE_OPTIONAL_HEADER64 OptionalHeader = (PIMAGE_OPTIONAL_HEADER64)&NtHeader->OptionalHeader;

        if (Directory >= SWAPD(OptionalHeader->NumberOfRvaAndSizes))
            return NULL;

        Va = SWAPD(OptionalHeader->DataDirectory[Directory].VirtualAddress);
        if (Va == 0)
            return NULL;

        *Size = SWAPD(OptionalHeader->DataDirectory[Directory].Size);

        if (MappedAsImage || Va < SWAPD(OptionalHeader->SizeOfHeaders))
            return (PVOID)((ULONG_PTR)BaseAddress + Va);
    }
    else
    {
        PIMAGE_OPTIONAL_HEADER32 OptionalHeader = (PIMAGE_OPTIONAL_HEADER32)&NtHeader->OptionalHeader;

        if (Directory >= SWAPD(OptionalHeader->NumberOfRvaAndSizes))
            return NULL;

        Va = SWAPD(OptionalHeader->DataDirectory[Directory].VirtualAddress);
        if (Va == 0)
            return NULL;

        *Size = SWAPD(OptionalHeader->DataDirectory[Directory].Size);

        if (MappedAsImage || Va < SWAPD(OptionalHeader->SizeOfHeaders))
            return (PVOID)((ULONG_PTR)BaseAddress + Va);
    }

    /* Image mapped as ordinary file, we must find raw pointer */
    return RtlImageRvaToVa(NtHeader, BaseAddress, Va, NULL);
}

/*
 * @implemented
 */
PIMAGE_SECTION_HEADER
NTAPI
RtlImageRvaToSection(
    PIMAGE_NT_HEADERS NtHeader,
    PVOID BaseAddress,
    ULONG Rva)
{
    PIMAGE_SECTION_HEADER Section;
    ULONG Va;
    ULONG Count;

    Count = SWAPW(NtHeader->FileHeader.NumberOfSections);
    Section = IMAGE_FIRST_SECTION(NtHeader);

    while (Count--)
    {
        Va = SWAPD(Section->VirtualAddress);
        if ((Va <= Rva) && (Rva < Va + SWAPD(Section->SizeOfRawData)))
            return Section;
        Section++;
    }

    return NULL;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlImageRvaToVa(
    PIMAGE_NT_HEADERS NtHeader,
    PVOID BaseAddress,
    ULONG Rva,
    PIMAGE_SECTION_HEADER *SectionHeader)
{
    PIMAGE_SECTION_HEADER Section = NULL;

    if (SectionHeader)
        Section = *SectionHeader;

    if ((Section == NULL) ||
        (Rva < SWAPD(Section->VirtualAddress)) ||
        (Rva >= SWAPD(Section->VirtualAddress) + SWAPD(Section->SizeOfRawData)))
    {
        Section = RtlImageRvaToSection(NtHeader, BaseAddress, Rva);
        if (Section == NULL)
            return NULL;

        if (SectionHeader)
            *SectionHeader = Section;
    }

    return (PVOID)((ULONG_PTR)BaseAddress + Rva +
                   (ULONG_PTR)SWAPD(Section->PointerToRawData) -
                   (ULONG_PTR)SWAPD(Section->VirtualAddress));
}

PIMAGE_BASE_RELOCATION
NTAPI
LdrProcessRelocationBlockLongLong(
    IN ULONG_PTR Address,
    IN ULONG Count,
    IN PUSHORT TypeOffset,
    IN LONGLONG Delta)
{
    SHORT Offset;
    USHORT Type;
    ULONG i;
    PUSHORT ShortPtr;
    PULONG LongPtr;
    PULONGLONG LongLongPtr;

    for (i = 0; i < Count; i++)
    {
        Offset = SWAPW(*TypeOffset) & 0xFFF;
        Type = SWAPW(*TypeOffset) >> 12;
        ShortPtr = (PUSHORT)(RVA(Address, Offset));
        /*
        * Don't relocate within the relocation section itself.
        * GCC/LD generates sometimes relocation records for the relocation section.
        * This is a bug in GCC/LD.
        * Fix for it disabled, since it was only in ntoskrnl and not in ntdll
        */
        /*
        if ((ULONG_PTR)ShortPtr < (ULONG_PTR)RelocationDir ||
        (ULONG_PTR)ShortPtr >= (ULONG_PTR)RelocationEnd)
        {*/
        switch (Type)
        {
            /* case IMAGE_REL_BASED_SECTION : */
            /* case IMAGE_REL_BASED_REL32 : */
        case IMAGE_REL_BASED_ABSOLUTE:
            break;

        case IMAGE_REL_BASED_HIGH:
            *ShortPtr = HIWORD(MAKELONG(0, *ShortPtr) + (Delta & 0xFFFFFFFF));
            break;

        case IMAGE_REL_BASED_LOW:
            *ShortPtr = SWAPW(*ShortPtr) + LOWORD(Delta & 0xFFFF);
            break;

        case IMAGE_REL_BASED_HIGHLOW:
            LongPtr = (PULONG)RVA(Address, Offset);
            *LongPtr = SWAPD(*LongPtr) + (Delta & 0xFFFFFFFF);
            break;

        case IMAGE_REL_BASED_DIR64:
            LongLongPtr = (PUINT64)RVA(Address, Offset);
            *LongLongPtr = SWAPQ(*LongLongPtr) + Delta;
            break;

#if defined(_M_ARM64) || defined(__aarch64__)
        case IMAGE_REL_BASED_ARM64_PAGEBASE_REL21:
            {
                /*
                 * ARM64 ADRP instruction relocation.
                 * ADRP Xd, label: Adds a 4KB page-aligned offset to PC and writes result to Xd.
                 * Instruction encoding: [31][30:29][28:24][23:5 immhi][4:0 Rd]
                 * Bit 31: op (1 for ADRP)
                 * Bits 30:29: immlo (low 2 bits of offset)
                 * Bits 28:24: opcode (10000 for ADRP)
                 * Bits 23:5: immhi (high 19 bits of offset)
                 * Bits 4:0: Rd (destination register)
                 */
                PULONG InstrPtr = (PULONG)RVA(Address, Offset);
                ULONG Instr = *InstrPtr;

                /* Extract the current 21-bit page offset from the instruction */
                ULONG ImmLo = (Instr >> 29) & 0x3;           /* bits [30:29] */
                ULONG ImmHi = (Instr >> 5) & 0x7FFFF;        /* bits [23:5] */
                LONGLONG CurrentPageOffset = ((LONGLONG)((ImmHi << 2) | ImmLo)) << 12;

                /* Sign-extend from 33 bits to 64 bits */
                if (CurrentPageOffset & (1LL << 32))
                    CurrentPageOffset |= 0xFFFFFFFE00000000LL;

                /* Calculate new page offset by adding the relocation delta */
                LONGLONG NewPageOffset = CurrentPageOffset + Delta;

                /* Extract the 21-bit offset (page-aligned, so divide by 4KB) */
                LONGLONG PageDelta = (NewPageOffset >> 12) & 0x1FFFFF;

                /* Reconstruct immlo and immhi */
                ULONG NewImmLo = (ULONG)(PageDelta & 0x3);
                ULONG NewImmHi = (ULONG)((PageDelta >> 2) & 0x7FFFF);

                /* Update the instruction while preserving other fields */
                Instr = (Instr & 0x9F00001F) |           /* Keep op, opcode, and Rd */
                        (NewImmLo << 29) |                /* Update immlo [30:29] */
                        (NewImmHi << 5);                  /* Update immhi [23:5] */

                *InstrPtr = Instr;
            }
            break;

        case IMAGE_REL_BASED_ARM64_PAGEOFFSET_12A:
            {
                /*
                 * ARM64 ADD/LDR/STR immediate offset relocation.
                 * ADD Xd, Xn, #imm12 or LDR Xt, [Xn, #imm12] or STR Xt, [Xn, #imm12]
                 * This fixup adjusts the 12-bit immediate field to match the lower 12 bits
                 * of the relocated address.
                 *
                 * ADD encoding: [31:24][23:22][21:10 imm12][9:5 Rn][4:0 Rd]
                 * LDR/STR encoding: [31:30 size][29:27][26][25:24][23:22][21:10 imm12][9:5 Rn][4:0 Rt]
                 */
                PULONG InstrPtr = (PULONG)RVA(Address, Offset);
                ULONG Instr = *InstrPtr;

                /* Extract the current 12-bit immediate from instruction [21:10] */
                ULONG CurrentImm = (Instr >> 10) & 0xFFF;

                /* Calculate byte offset from page base */
                ULONG ByteOffset;
                ULONG Size = (Instr >> 30) & 0x3;

                /* For LDR/STR, the immediate is scaled by access size, so unscale it first */
                if ((Instr & 0x3B000000) == 0x39000000)  /* LDR/STR with immediate offset */
                {
                    /* Unscale: Size 0 = byte (*1), Size 1 = halfword (*2),
                     * Size 2 = word (*4), Size 3 = doubleword (*8) */
                    ByteOffset = CurrentImm << Size;
                }
                else  /* ADD or other instructions */
                {
                    /* ADD uses byte offset directly */
                    ByteOffset = CurrentImm;
                }

                /* Apply relocation delta to the byte offset
                 * NewTargetAddr = OldTargetAddr + Delta
                 * NewPageOffset = (NewTargetAddr & 0xFFF) = ((OldTargetAddr & 0xFFF) + (Delta & 0xFFF)) & 0xFFF */
                ByteOffset = (ByteOffset + (ULONG)(Delta & 0xFFF)) & 0xFFF;

                /* Scale back for LDR/STR instructions */
                ULONG NewImm;
                if ((Instr & 0x3B000000) == 0x39000000)  /* LDR/STR with immediate offset */
                {
                    NewImm = ByteOffset >> Size;
                }
                else  /* ADD or other instructions */
                {
                    NewImm = ByteOffset;
                }

                /* Update the 12-bit immediate field [21:10] */
                Instr = (Instr & 0xFFC003FF) | ((NewImm & 0xFFF) << 10);

                *InstrPtr = Instr;
            }
            break;
#endif /* ARM64 */

        case IMAGE_REL_BASED_HIGHADJ:
        case IMAGE_REL_BASED_MIPS_JMPADDR:
        default:
            DPRINT1("Unknown/unsupported fixup type %hu.\n", Type);
            DPRINT1("Address %p, Current %u, Count %u, *TypeOffset %x\n",
                    (PVOID)Address, i, Count, SWAPW(*TypeOffset));
            return (PIMAGE_BASE_RELOCATION)NULL;
        }

        TypeOffset++;
    }

    return (PIMAGE_BASE_RELOCATION)TypeOffset;
}

ULONG
NTAPI
LdrRelocateImage(
    _In_ PVOID BaseAddress,
    _In_opt_ PCSTR LoaderName,
    _In_ ULONG Success,
    _In_ ULONG Conflict,
    _In_ ULONG Invalid)
{
    return LdrRelocateImageWithBias(BaseAddress, 0, LoaderName, Success, Conflict, Invalid);
}

ULONG
NTAPI
LdrRelocateImageWithBias(
    _In_ PVOID BaseAddress,
    _In_ LONGLONG AdditionalBias,
    _In_opt_ PCSTR LoaderName,
    _In_ ULONG Success,
    _In_ ULONG Conflict,
    _In_ ULONG Invalid)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_DATA_DIRECTORY RelocationDDir;
    PIMAGE_BASE_RELOCATION RelocationDir, RelocationEnd;
    ULONG Count;
    ULONG_PTR Address;
    PUSHORT TypeOffset;
    LONGLONG Delta;

    UNREFERENCED_PARAMETER(LoaderName);

    NtHeaders = RtlImageNtHeader(BaseAddress);

    if (NtHeaders == NULL)
        return Invalid;

    if (SWAPW(NtHeaders->FileHeader.Characteristics) & IMAGE_FILE_RELOCS_STRIPPED)
    {
        return Conflict;
    }

    RelocationDDir = &NtHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];

    if (SWAPD(RelocationDDir->VirtualAddress) == 0 || SWAPD(RelocationDDir->Size) == 0)
    {
        return Success;
    }

    Delta = (ULONG_PTR)BaseAddress - SWAPD(NtHeaders->OptionalHeader.ImageBase) + AdditionalBias;
    RelocationDir = (PIMAGE_BASE_RELOCATION)((ULONG_PTR)BaseAddress + SWAPD(RelocationDDir->VirtualAddress));
    RelocationEnd = (PIMAGE_BASE_RELOCATION)((ULONG_PTR)RelocationDir + SWAPD(RelocationDDir->Size));

    while (RelocationDir < RelocationEnd &&
            SWAPW(RelocationDir->SizeOfBlock) > 0)
    {
        Count = (SWAPW(RelocationDir->SizeOfBlock) - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(USHORT);
        Address = (ULONG_PTR)RVA(BaseAddress, SWAPD(RelocationDir->VirtualAddress));
        TypeOffset = (PUSHORT)(RelocationDir + 1);

        RelocationDir = LdrProcessRelocationBlockLongLong(Address,
                        Count,
                        TypeOffset,
                        Delta);

        if (RelocationDir == NULL)
        {
            DPRINT1("Error during call to LdrProcessRelocationBlockLongLong()!\n");
            return Invalid;
        }
    }

    return Success;
}

/* EOF */
