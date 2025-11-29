/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Helpers for inspecting the FreeLdr PE image layout
 */

#include <freeldr.h>
#include <debug.h>

static
PIMAGE_NT_HEADERS
FrldrGetNtHeaders(VOID)
{
    return RtlImageNtHeader(&__ImageBase);
}

BOOLEAN
FrldrValidateBss(VOID)
{
    return TRUE;
}

ULONG
FrldrGetImageSize(VOID)
{
    PIMAGE_NT_HEADERS NtHeaders = FrldrGetNtHeaders();
    if (!NtHeaders)
    {
        return 0;
    }

    return NtHeaders->OptionalHeader.SizeOfImage;
}

VOID
FrldrZeroBss(VOID)
{
    PIMAGE_NT_HEADERS NtHeaders;
    PIMAGE_SECTION_HEADER Section;
    PVOID ImageBase;
    USHORT SectionCount;

    NtHeaders = FrldrGetNtHeaders();
    if (!NtHeaders)
    {
        return;
    }

    ImageBase = &__ImageBase;
    Section = IMAGE_FIRST_SECTION(NtHeaders);
    SectionCount = NtHeaders->FileHeader.NumberOfSections;

    while (SectionCount--)
    {
        ULONG Characteristics = Section->Characteristics;
        SIZE_T VirtualSize = Section->Misc.VirtualSize;
        SIZE_T RawSize = Section->SizeOfRawData;
        ULONG_PTR SectionBase;
        SIZE_T InitSize, ZeroSize;

        if (VirtualSize == 0)
        {
            Section++;
            continue;
        }

        SectionBase = (ULONG_PTR)ImageBase + Section->VirtualAddress;

        /*
         * Windows zeroes both explicit BSS sections and the tail of any
         * section where VirtualSize exceeds SizeOfRawData. Clang/LLD can
         * fold .bss into .data without setting IMAGE_SCN_CNT_UNINITIALIZED_DATA,
         * so we handle both cases explicitly.
         */
        if (Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA)
        {
            ZeroSize = VirtualSize;
            InitSize = 0;
        }
        else
        {
            InitSize = min(VirtualSize, RawSize);
            ZeroSize = (VirtualSize > RawSize) ? (VirtualSize - InitSize) : 0;
        }

        if (ZeroSize != 0)
        {
            ULONG_PTR ZeroBase = SectionBase + InitSize;
            RtlZeroMemory((PVOID)ZeroBase, ZeroSize);
        }

        Section++;
    }
}
