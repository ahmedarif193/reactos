/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Finalize the temporary RISC-V NT ELF-to-PE link backend.
 *
 * The input must be an NT-ABI ELF image converted by GenFw with the
 * address-preserving layout in sdk/cmake/riscv64-native.lds.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <typedefs.h>
#include <pecoff.h>

enum fixup_mode { MODE_KERNEL, MODE_KERNELDLL };

#define RISCV64_NT_HEADER_OFFSET 0x80
#define RISCV64_LOADER_HEADER_WINDOW 0x400

static void error(const char *format, ...)
{
    va_list args;
    fputs("riscv64-pe: ", stderr);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

static void *rva_to_ptr(unsigned char *buffer, PIMAGE_NT_HEADERS64 nt, DWORD rva)
{
    PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(nt);
    unsigned int i;
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (rva >= sections[i].VirtualAddress &&
            rva - sections[i].VirtualAddress < sections[i].SizeOfRawData)
            return buffer + sections[i].PointerToRawData + rva - sections[i].VirtualAddress;
    }
    return NULL;
}

static int normalize_nt_headers(unsigned char *buffer, size_t len,
                                PIMAGE_DOS_HEADER dos,
                                PIMAGE_NT_HEADERS64 *nt_header,
                                PIMAGE_SECTION_HEADER *section_header)
{
    PIMAGE_NT_HEADERS64 nt = *nt_header;
    size_t old_offset = (size_t)dos->e_lfanew;
    size_t header_size = sizeof(*nt) +
                         nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
    size_t new_offset = RISCV64_NT_HEADER_OFFSET;

    /* FreeLdr reads two disk sectors before it interprets the DOS header.
     * Keep the complete PE header and section table in that initial window,
     * as normal PE linkers do. GenFw places them near the end of the 4-KiB
     * header page, so move only this metadata and preserve section RVAs. */
    if (new_offset > len || header_size > len - new_offset ||
        new_offset + header_size > RISCV64_LOADER_HEADER_WINDOW ||
        new_offset + header_size > nt->OptionalHeader.SizeOfHeaders)
    {
        error("RISC-V PE headers do not fit in the image header extent\n");
        return 1;
    }

    if (old_offset != new_offset)
    {
        if (!((old_offset + header_size <= new_offset) ||
              (new_offset + header_size <= old_offset)))
        {
            error("RISC-V PE header relocation ranges overlap\n");
            return 1;
        }

        memmove(buffer + new_offset, buffer + old_offset, header_size);
        memset(buffer + old_offset, 0, header_size);
        dos->e_lfanew = (LONG)new_offset;
    }

    *nt_header = (PIMAGE_NT_HEADERS64)(buffer + new_offset);
    *section_header = IMAGE_FIRST_SECTION(*nt_header);
    return 0;
}

/* This mode finalizes NT-ABI ELF images converted by GenFw. It must not turn
 * an arbitrary firmware image into an NT module by changing its subsystem. */
static int riscv64_native_fixup(enum fixup_mode mode, unsigned char *buffer, size_t len,
                               PIMAGE_NT_HEADERS64 nt_header, const char *layout)
{
    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)nt_header;
    PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(nt);
    static const unsigned int directories[] = {IMAGE_DIRECTORY_ENTRY_EXPORT, IMAGE_DIRECTORY_ENTRY_IMPORT, IMAGE_DIRECTORY_ENTRY_IAT};
    unsigned int rva[5], size[5], i, j;
    int consumed = 0;

    if (sizeof(nt_header->OptionalHeader) != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        (mode != MODE_KERNEL && mode != MODE_KERNELDLL) ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_RISCV64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.Subsystem != IMAGE_SUBSYSTEM_EFI_APPLICATION || nt->OptionalHeader.ImageBase != 0 ||
        nt->OptionalHeader.SectionAlignment != 0x1000 ||
        nt->OptionalHeader.FileAlignment != 0x1000 ||
        nt->OptionalHeader.NumberOfRvaAndSizes != IMAGE_NUMBEROF_DIRECTORY_ENTRIES)
    {
        error("Expected a zero-based GenFw RISC-V PE32+ image with 4-KiB alignment\n");
        return 1;
    }

    if (sscanf(layout, "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x%n",
               &rva[0], &size[0], &rva[1], &size[1], &rva[2], &size[2],
               &rva[3], &size[3], &rva[4], &size[4], &consumed) != 10 || layout[consumed])
    {
        error("Invalid RISC-V directory and section layout\n");
        return 1;
    }

    for (i = 0; i < 5; ++i)
    {
        for (j = 0; j < nt->FileHeader.NumberOfSections; ++j)
        {
            PIMAGE_SECTION_HEADER section = &sections[j];
            unsigned int offset;

            if (rva[i] < section->VirtualAddress)
                continue;
            offset = rva[i] - section->VirtualAddress;
            if (offset > section->SizeOfRawData || size[i] > section->SizeOfRawData - offset ||
                offset > section->Misc.VirtualSize || size[i] > section->Misc.VirtualSize - offset)
                continue;
            if (section->PointerToRawData > len || section->SizeOfRawData > len - section->PointerToRawData)
                continue;
            if ((i == 3 && (offset || strncmp((char *)section->Name, ".text", 8))) ||
                (i == 4 && (offset || strncmp((char *)section->Name, ".data", 8))))
                continue;
            break;
        }
        if (!size[i] || j == nt->FileHeader.NumberOfSections)
        {
            error("Native directory/section %u does not match the linked ELF layout\n", i);
            return 1;
        }
    }
    if (rva[3] != 0x1000 || rva[4] != ((rva[3] + (uint64_t)size[3] + 0xfff) & ~(uint64_t)0xfff) ||
        (mode == MODE_KERNELDLL && nt->OptionalHeader.AddressOfEntryPoint != 0) ||
        (mode == MODE_KERNEL && (nt->OptionalHeader.AddressOfEntryPoint < rva[3] ||
                                nt->OptionalHeader.AddressOfEntryPoint - rva[3] >= size[3])))
    {
        error("RISC-V image section or entry-point contract was not preserved\n");
        return 1;
    }

    /* The bootstrap loader needs only DIR64 rebasing. Do not silently accept
     * truncated pointer fixups or introduce instruction-relocation policy. */
    {
        IMAGE_DATA_DIRECTORY reloc = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        unsigned char *start = rva_to_ptr(buffer, nt_header, reloc.VirtualAddress);
        size_t offset = 0;
        if (!start || start < buffer || (size_t)(start - buffer) > len ||
            reloc.Size > len - (size_t)(start - buffer) || !reloc.Size)
        {
            error("Missing or invalid RISC-V base relocation directory\n");
            return 1;
        }
        while (offset < reloc.Size)
        {
            PIMAGE_BASE_RELOCATION block = (PIMAGE_BASE_RELOCATION)(start + offset);
            if (reloc.Size - offset < sizeof(*block) || block->SizeOfBlock < sizeof(*block) ||
                block->SizeOfBlock > reloc.Size - offset || (block->SizeOfBlock & 3))
            {
                error("Invalid RISC-V base relocation block\n");
                return 1;
            }
            for (i = sizeof(*block); i < block->SizeOfBlock; i += sizeof(WORD))
            {
                WORD fixup = *(WORD *)(start + offset + i);
                if ((fixup >> 12) != IMAGE_REL_BASED_ABSOLUTE && (fixup >> 12) != IMAGE_REL_BASED_DIR64)
                {
                    error("Unsupported native RISC-V base relocation type %u\n", fixup >> 12);
                    return 1;
                }
            }
            offset += block->SizeOfBlock;
        }
    }

    for (i = 0; i < 3; ++i)
    {
        nt->OptionalHeader.DataDirectory[directories[i]].VirtualAddress = rva[i];
        nt->OptionalHeader.DataDirectory[directories[i]].Size = size[i];
    }
    nt->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_NATIVE;
    nt->OptionalHeader.MajorOperatingSystemVersion = 10;
    nt->OptionalHeader.MajorSubsystemVersion = 10;
    if (mode == MODE_KERNELDLL)
        nt->FileHeader.Characteristics |= IMAGE_FILE_DLL;
    else
        nt->FileHeader.Characteristics &= ~IMAGE_FILE_DLL;
    return 0;
}


int main(int argc, char **argv)
{
    FILE *file;
    unsigned char *buffer;
    PIMAGE_DOS_HEADER dos;
    PIMAGE_NT_HEADERS64 nt;
    PIMAGE_SECTION_HEADER section;
    long file_size;
    size_t len, i;
    unsigned int checksum = 0;
    enum fixup_mode mode;
    int result = 1;

    if (argc != 4 || (strcmp(argv[1], "kernel") && strcmp(argv[1], "kerneldll")))
    {
        error("usage: riscv64-pe kernel|kerneldll exportRva,size,importRva,size,iatRva,size,textRva,size,dataRva,size image\n");
        return 1;
    }
    mode = !strcmp(argv[1], "kernel") ? MODE_KERNEL : MODE_KERNELDLL;
    file = fopen(argv[3], "rb+");
    if (!file)
    {
        perror(argv[3]);
        return 1;
    }
    if (fseek(file, 0, SEEK_END) || (file_size = ftell(file)) < (long)sizeof(*dos) ||
        fseek(file, 0, SEEK_SET))
    {
        error("cannot size image\n");
        fclose(file);
        return 1;
    }
    len = (size_t)file_size;
    buffer = malloc(len);
    if (!buffer || fread(buffer, 1, len, file) != len)
    {
        error("cannot read image\n");
        goto done;
    }
    dos = (PIMAGE_DOS_HEADER)buffer;
    if (dos->e_magic != IMAGE_DOS_MAGIC || dos->e_lfanew < 0 ||
        (size_t)dos->e_lfanew > len || sizeof(*nt) > len - (size_t)dos->e_lfanew)
    {
        error("invalid DOS or PE header extent\n");
        goto done;
    }
    nt = (PIMAGE_NT_HEADERS64)(buffer + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.SizeOfOptionalHeader != sizeof(nt->OptionalHeader) ||
        nt->FileHeader.NumberOfSections > (len - (size_t)dos->e_lfanew - sizeof(*nt)) / sizeof(*section))
    {
        error("invalid PE header or section table\n");
        goto done;
    }
    if (normalize_nt_headers(buffer, len, dos, &nt, &section))
        goto done;
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (section[i].PointerToRawData > len || section[i].SizeOfRawData > len - section[i].PointerToRawData)
        {
            error("section outside input image\n");
            goto done;
        }
    }
    if (riscv64_native_fixup(mode, buffer, len, nt, argv[2]))
        goto done;

    /* The temporary link layout has only resident text/data and relocations.
     * Do not claim support for pageable, INIT, TLS or unwind sections. */
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        if (!strncmp((char *)section[i].Name, ".text", 8) ||
            !strncmp((char *)section[i].Name, ".data", 8))
            section[i].Characteristics |= IMAGE_SCN_MEM_NOT_PAGED;
    }
    nt->OptionalHeader.CheckSum = 0;
    for (i = 0; i < len; i += 2)
    {
        checksum += buffer[i];
        if (i + 1 < len)
            checksum += (unsigned int)buffer[i + 1] << 8;
        checksum = (checksum + (checksum >> 16)) & 0xffff;
    }
    nt->OptionalHeader.CheckSum = checksum + (unsigned int)len;
    if (fseek(file, 0, SEEK_SET) || fwrite(buffer, 1, len, file) != len || fflush(file))
    {
        error("cannot write image\n");
        goto done;
    }
    result = 0;
done:
    free(buffer);
    if (fclose(file))
        result = 1;
    return result;
}
