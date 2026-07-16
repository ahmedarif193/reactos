/*
 * Usage: raddr2line input-file address/offset
 *
 * This is a tool and is compiled using the host compiler,
 * i.e. on Linux gcc and not mingw-gcc (cross-compiler).
 * Therefore we can't include SDK headers and we have to
 * duplicate some definitions here.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsym.h"

static int
range_is_valid(size_t FileSize, size_t Offset, size_t Length)
{
    return Offset <= FileSize && Length <= FileSize - Offset;
}

static TARGET_ULONG_PTR
fixup_offset(TARGET_ULONG_PTR ImageBase, TARGET_ULONG_PTR Offset)
{
    if (ImageBase != 0 && Offset >= ImageBase)
        Offset -= ImageBase;

    return Offset;
}

static int
parse_offset(const char *Text, TARGET_ULONG_PTR *Offset)
{
    unsigned long long Value;
    char *End;
    int Base = 0;

    if (Text[0] == '0' && (Text[1] == 'd' || Text[1] == 'D'))
    {
        Text += 2;
        Base = 10;
    }
    if (*Text == '\0' || *Text == '-')
        return 0;

    errno = 0;
    Value = strtoull(Text, &End, Base);
    if (errno != 0 || *End != '\0' || Value > (TARGET_ULONG_PTR)-1)
        return 0;

    *Offset = (TARGET_ULONG_PTR)Value;
    return 1;
}

static PIMAGE_SECTION_HEADER
find_rossym_section(PIMAGE_FILE_HEADER FileHeader,
                    PIMAGE_SECTION_HEADER SectionHeaders)
{
    unsigned Index;

    for (Index = 0; Index < FileHeader->NumberOfSections; Index++)
    {
        if (memcmp(SectionHeaders[Index].Name, ".rossym", 7) == 0 &&
            SectionHeaders[Index].Name[7] == '\0')
        {
            return &SectionHeaders[Index];
        }
    }
    return NULL;
}

static void
read_entry(const unsigned char *Entries, size_t Index, ROSSYM_ENTRY *Entry)
{
    memcpy(Entry, Entries + Index * sizeof(*Entry), sizeof(*Entry));
}

static const char *
get_string(const char *Strings, ULONG StringsLength, ULONG Offset)
{
    if (Offset >= StringsLength ||
        memchr(Strings + Offset, '\0', StringsLength - Offset) == NULL)
    {
        return NULL;
    }
    return Strings + Offset;
}

static int
find_and_print_offset(const void *Data, size_t DataSize,
                      TARGET_ULONG_PTR Offset)
{
    SYMBOLFILE_HEADER Header;
    const unsigned char *Entries;
    const char *Strings;
    size_t SymbolsEnd, SymbolCount, Index, Low, High;
    TARGET_ULONG_PTR PreviousAddress = 0;
    ROSSYM_ENTRY Entry;

    if (DataSize < sizeof(Header))
        return 1;

    memcpy(&Header, Data, sizeof(Header));
    SymbolsEnd = (size_t)Header.SymbolsOffset + Header.SymbolsLength;
    if (Header.SymbolsOffset < sizeof(Header) ||
        !range_is_valid(DataSize, Header.SymbolsOffset, Header.SymbolsLength) ||
        Header.StringsOffset < SymbolsEnd ||
        !range_is_valid(DataSize, Header.StringsOffset, Header.StringsLength) ||
        Header.SymbolsLength % sizeof(ROSSYM_ENTRY) != 0)
    {
        fprintf(stderr, "Invalid .rossym header\n");
        return 1;
    }

    Entries = (const unsigned char *)Data + Header.SymbolsOffset;
    Strings = (const char *)Data + Header.StringsOffset;
    SymbolCount = Header.SymbolsLength / sizeof(ROSSYM_ENTRY);

    for (Index = 0; Index < SymbolCount; Index++)
    {
        read_entry(Entries, Index, &Entry);
        if ((Index != 0 && Entry.Address < PreviousAddress) ||
            get_string(Strings, Header.StringsLength, Entry.FunctionOffset) == NULL ||
            get_string(Strings, Header.StringsLength, Entry.FileOffset) == NULL)
        {
            fprintf(stderr, "Invalid .rossym entry\n");
            return 1;
        }
        PreviousAddress = Entry.Address;
    }

    Low = 0;
    High = SymbolCount;
    while (Low < High)
    {
        size_t Middle = Low + (High - Low) / 2;

        read_entry(Entries, Middle, &Entry);
        if (Entry.Address <= Offset)
            Low = Middle + 1;
        else
            High = Middle;
    }

    if (Low == 0)
        return 1;

    read_entry(Entries, Low - 1, &Entry);
    printf("%s:%u (%s)\n",
           get_string(Strings, Header.StringsLength, Entry.FileOffset),
           (unsigned)Entry.SourceLine,
           get_string(Strings, Header.StringsLength, Entry.FunctionOffset));
    return 0;
}

static int
process_data(const void *FileData, size_t FileSize, TARGET_ULONG_PTR Offset)
{
    const unsigned char *Bytes = FileData;
    PIMAGE_DOS_HEADER DosHeader;
    PIMAGE_FILE_HEADER FileHeader;
    PIMAGE_OPTIONAL_HEADER OptionalHeader;
    PIMAGE_SECTION_HEADER SectionHeaders, RosSymSection;
    size_t NtOffset, OptionalOffset, SectionsOffset;
    ULONG Signature;

    if (FileSize < sizeof(IMAGE_DOS_HEADER))
        goto InvalidPe;

    DosHeader = (PIMAGE_DOS_HEADER)Bytes;
    if (DosHeader->e_magic != IMAGE_DOS_MAGIC || DosHeader->e_lfanew <= 0)
        goto InvalidPe;

    NtOffset = (size_t)DosHeader->e_lfanew;
    if (!range_is_valid(FileSize, NtOffset,
                        sizeof(Signature) + sizeof(IMAGE_FILE_HEADER)))
    {
        goto InvalidPe;
    }
    memcpy(&Signature, Bytes + NtOffset, sizeof(Signature));
    if (Signature != IMAGE_NT_SIGNATURE)
        goto InvalidPe;

    FileHeader = (PIMAGE_FILE_HEADER)(Bytes + NtOffset + sizeof(Signature));
    OptionalOffset = NtOffset + sizeof(Signature) + sizeof(IMAGE_FILE_HEADER);
    if (FileHeader->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER) ||
        !range_is_valid(FileSize, OptionalOffset, FileHeader->SizeOfOptionalHeader))
    {
        goto InvalidPe;
    }

    OptionalHeader = (PIMAGE_OPTIONAL_HEADER)(Bytes + OptionalOffset);
#ifdef _TARGET_PE64
    if (OptionalHeader->Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
#else
    if (OptionalHeader->Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
#endif
        goto InvalidPe;

    SectionsOffset = OptionalOffset + FileHeader->SizeOfOptionalHeader;
    if (SectionsOffset > FileSize ||
        FileHeader->NumberOfSections >
            (FileSize - SectionsOffset) / sizeof(IMAGE_SECTION_HEADER))
    {
        goto InvalidPe;
    }

    SectionHeaders = (PIMAGE_SECTION_HEADER)(Bytes + SectionsOffset);
    RosSymSection = find_rossym_section(FileHeader, SectionHeaders);
    if (RosSymSection == NULL)
    {
        fprintf(stderr, "Couldn't find .rossym section in executable\n");
        return 1;
    }
    if (!range_is_valid(FileSize, RosSymSection->PointerToRawData,
                        RosSymSection->SizeOfRawData))
    {
        fprintf(stderr, "Invalid .rossym section range\n");
        return 1;
    }

    Offset = fixup_offset(OptionalHeader->ImageBase, Offset);
    return find_and_print_offset(Bytes + RosSymSection->PointerToRawData,
                                 RosSymSection->SizeOfRawData, Offset);

InvalidPe:
    fprintf(stderr, "Input file is not a valid target PE image\n");
    return 1;
}

static int
process_file(const char *FileName, TARGET_ULONG_PTR Offset)
{
    void *FileData;
    size_t FileSize;
    int Result;

    FileData = load_file(FileName, &FileSize);
    if (FileData == NULL)
    {
        fprintf(stderr, "An error occurred loading '%s'\n", FileName);
        return 1;
    }

    Result = process_data(FileData, FileSize, Offset);
    free(FileData);
    return Result;
}

int
main(int argc, const char **argv)
{
    TARGET_ULONG_PTR Offset;
    char *Path;
    int Result;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: raddr2line <exefile> <address-or-offset>\n");
        return 1;
    }
    if (!parse_offset(argv[2], &Offset))
    {
        fprintf(stderr, "Invalid address or offset '%s'\n", argv[2]);
        return 1;
    }

    Path = convert_path(argv[1]);
    if (Path == NULL)
    {
        fprintf(stderr, "Unable to convert input path\n");
        return 1;
    }
    Result = process_file(Path, Offset);
    free(Path);

    if (Result != 0)
        printf("??:0\n");
    return Result;
}
