/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Bounds-checked PE identity reader
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_pe.h"

#include <reactos/rperf.h>
#include <stddef.h>

#define RPERF_PE_MAX_OPTIONAL_HEADER 4096U
#define RPERF_PE_MAX_SECTIONS 96U
#define RPERF_PE_MAX_DEBUG_DIRECTORIES 4096U

typedef struct _RPERF_PE_FILE
{
    HANDLE Handle;
    ULONGLONG Size;
} RPERF_PE_FILE;

static BOOL
RperfPeRangeValid(const RPERF_PE_FILE *File,
                  ULONGLONG Offset,
                  ULONGLONG Bytes)
{
    return Offset <= File->Size && Bytes <= File->Size - Offset;
}

static BOOL
RperfPeRead(const RPERF_PE_FILE *File,
            ULONGLONG Offset,
            PVOID Buffer,
            ULONG Bytes)
{
    LARGE_INTEGER Position;
    DWORD Read;

    if (!RperfPeRangeValid(File, Offset, Bytes) ||
        Offset > 0x7fffffffffffffffULL)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Position.QuadPart = Offset;
    if (!SetFilePointerEx(File->Handle, Position, NULL, FILE_BEGIN) ||
        !ReadFile(File->Handle, Buffer, Bytes, &Read, NULL) || Read != Bytes)
    {
        if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_HANDLE_EOF);
        return FALSE;
    }
    return TRUE;
}

static ULONG
RperfPeArchitecture(USHORT Machine)
{
    switch (Machine)
    {
        case IMAGE_FILE_MACHINE_I386:
            return RPERF_ARCH_X86;
        case IMAGE_FILE_MACHINE_AMD64:
            return RPERF_ARCH_AMD64;
#ifdef IMAGE_FILE_MACHINE_ARMNT
        case IMAGE_FILE_MACHINE_ARMNT:
            return RPERF_ARCH_ARM;
#endif
#ifdef IMAGE_FILE_MACHINE_ARM64
        case IMAGE_FILE_MACHINE_ARM64:
            return RPERF_ARCH_ARM64;
#endif
        default:
            return RPERF_ARCH_UNKNOWN;
    }
}

static BOOL
RperfPeRvaToOffset(const IMAGE_SECTION_HEADER *Sections,
                   USHORT SectionCount,
                   ULONG Rva,
                   ULONG Bytes,
                   ULONGLONG *Offset)
{
    USHORT Index;

    for (Index = 0; Index < SectionCount; ++Index)
    {
        const IMAGE_SECTION_HEADER *Section = &Sections[Index];
        ULONGLONG Start = Section->VirtualAddress;
        ULONGLONG Span = max(Section->Misc.VirtualSize,
                             Section->SizeOfRawData);
        ULONGLONG Relative;

        if ((ULONGLONG)Rva < Start ||
            (ULONGLONG)Rva >= Start + Span)
            continue;
        Relative = (ULONGLONG)Rva - Start;
        if (Relative > Section->SizeOfRawData ||
            Bytes > Section->SizeOfRawData - Relative)
            return FALSE;
        *Offset = (ULONGLONG)Section->PointerToRawData + Relative;
        return TRUE;
    }
    return FALSE;
}

static VOID
RperfPeCopyPdbPath(const UCHAR *Bytes,
                   ULONG ByteCount,
                   PWSTR Path,
                   SIZE_T PathCount)
{
    ULONG Length = 0;
    INT Result;

    if (PathCount == 0)
        return;
    Path[0] = UNICODE_NULL;
    while (Length < ByteCount && Bytes[Length] != ANSI_NULL)
        Length++;
    if (Length == 0)
        return;
    Result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 (PCSTR)Bytes, Length,
                                 Path, (INT)PathCount - 1);
    if (Result == 0)
    {
        Result = MultiByteToWideChar(CP_ACP, 0,
                                     (PCSTR)Bytes, Length,
                                     Path, (INT)PathCount - 1);
    }
    if (Result > 0)
        Path[Result] = UNICODE_NULL;
}

static BOOL
RperfPeReadCodeView(const RPERF_PE_FILE *File,
                    ULONGLONG Offset,
                    ULONG Bytes,
                    RPERF_PE_IDENTITY *Identity)
{
    UCHAR Header[24];
    ULONG PathBytes;
    UCHAR *Path = NULL;

    if (Bytes < sizeof(Header) ||
        !RperfPeRead(File, Offset, Header, sizeof(Header)))
        return FALSE;
    if (memcmp(Header, "RSDS", 4) != 0)
        return TRUE;
    CopyMemory(Identity->DebugId, Header + 4,
               sizeof(Identity->DebugId));
    CopyMemory(&Identity->DebugAge, Header + 20,
               sizeof(Identity->DebugAge));
    PathBytes = Bytes - sizeof(Header);
    if (PathBytes != 0)
    {
        Path = HeapAlloc(GetProcessHeap(), 0, PathBytes);
        if (Path == NULL)
            return FALSE;
        if (!RperfPeRead(File, Offset + sizeof(Header), Path, PathBytes))
        {
            HeapFree(GetProcessHeap(), 0, Path);
            return FALSE;
        }
        RperfPeCopyPdbPath(Path, PathBytes,
                           Identity->PdbPath,
                           ARRAYSIZE(Identity->PdbPath));
        HeapFree(GetProcessHeap(), 0, Path);
    }
    return TRUE;
}

BOOL
RperfReadPeIdentity(PCWSTR Path,
                    RPERF_PE_IDENTITY *Identity)
{
    RPERF_PE_FILE File;
    LARGE_INTEGER FileSize;
    IMAGE_DOS_HEADER Dos;
    DWORD Signature;
    IMAGE_FILE_HEADER Coff;
    UCHAR *Optional = NULL;
    IMAGE_SECTION_HEADER *Sections = NULL;
    ULONGLONG NtOffset, SectionOffset, DebugOffset;
    IMAGE_DATA_DIRECTORY DebugDirectory;
    ULONG Index, DebugCount;
    BOOL Result = FALSE;

    if (Path == NULL || *Path == UNICODE_NULL || Identity == NULL)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    ZeroMemory(Identity, sizeof(*Identity));
    File.Handle = CreateFileW(Path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                              FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (File.Handle == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!GetFileSizeEx(File.Handle, &FileSize) || FileSize.QuadPart < 0)
        goto Cleanup;
    File.Size = (ULONGLONG)FileSize.QuadPart;
    if (!RperfPeRead(&File, 0, &Dos, sizeof(Dos)) ||
        Dos.e_magic != IMAGE_DOS_SIGNATURE || Dos.e_lfanew < 0)
    {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        goto Cleanup;
    }
    NtOffset = (ULONG)Dos.e_lfanew;
    if (!RperfPeRead(&File, NtOffset, &Signature, sizeof(Signature)) ||
        Signature != IMAGE_NT_SIGNATURE ||
        !RperfPeRead(&File, NtOffset + sizeof(Signature),
                     &Coff, sizeof(Coff)) ||
        Coff.NumberOfSections == 0 ||
        Coff.NumberOfSections > RPERF_PE_MAX_SECTIONS ||
        Coff.SizeOfOptionalHeader < sizeof(USHORT) ||
        Coff.SizeOfOptionalHeader > RPERF_PE_MAX_OPTIONAL_HEADER)
    {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        goto Cleanup;
    }
    Optional = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                         Coff.SizeOfOptionalHeader);
    Sections = HeapAlloc(GetProcessHeap(), 0,
                         Coff.NumberOfSections * sizeof(*Sections));
    if (Optional == NULL || Sections == NULL)
        goto Cleanup;
    if (!RperfPeRead(&File,
                     NtOffset + sizeof(Signature) + sizeof(Coff),
                     Optional, Coff.SizeOfOptionalHeader))
        goto Cleanup;
    ZeroMemory(&DebugDirectory, sizeof(DebugDirectory));
    if (*(UNALIGNED USHORT *)Optional == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    {
        IMAGE_OPTIONAL_HEADER32 Header;
        if (Coff.SizeOfOptionalHeader <
            offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory))
        {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            goto Cleanup;
        }
        ZeroMemory(&Header, sizeof(Header));
        CopyMemory(&Header, Optional,
                   min(sizeof(Header), Coff.SizeOfOptionalHeader));
        Identity->Checksum = Header.CheckSum;
        Identity->ImageSize = Header.SizeOfImage;
        if (Header.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DEBUG &&
            Coff.SizeOfOptionalHeader >=
                offsetof(IMAGE_OPTIONAL_HEADER32, DataDirectory) +
                (IMAGE_DIRECTORY_ENTRY_DEBUG + 1) *
                sizeof(IMAGE_DATA_DIRECTORY))
        {
            DebugDirectory =
                Header.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        }
    }
    else if (*(UNALIGNED USHORT *)Optional ==
             IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    {
        IMAGE_OPTIONAL_HEADER64 Header;
        if (Coff.SizeOfOptionalHeader <
            offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory))
        {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            goto Cleanup;
        }
        ZeroMemory(&Header, sizeof(Header));
        CopyMemory(&Header, Optional,
                   min(sizeof(Header), Coff.SizeOfOptionalHeader));
        Identity->Checksum = Header.CheckSum;
        Identity->ImageSize = Header.SizeOfImage;
        if (Header.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_DEBUG &&
            Coff.SizeOfOptionalHeader >=
                offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) +
                (IMAGE_DIRECTORY_ENTRY_DEBUG + 1) *
                sizeof(IMAGE_DATA_DIRECTORY))
        {
            DebugDirectory =
                Header.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        }
    }
    else
    {
        SetLastError(ERROR_BAD_EXE_FORMAT);
        goto Cleanup;
    }
    Identity->Architecture = RperfPeArchitecture(Coff.Machine);
    Identity->TimeDateStamp = Coff.TimeDateStamp;
    SectionOffset = NtOffset + sizeof(Signature) + sizeof(Coff) +
                    Coff.SizeOfOptionalHeader;
    if (!RperfPeRead(&File, SectionOffset, Sections,
                     Coff.NumberOfSections * sizeof(*Sections)))
        goto Cleanup;
    for (Index = 0; Index < Coff.NumberOfSections; ++Index)
    {
        if (memcmp(Sections[Index].Name, ".rossym", 7) == 0 &&
            Sections[Index].Name[7] == ANSI_NULL)
        {
            Identity->HasRosSym = TRUE;
            break;
        }
    }
    if (DebugDirectory.VirtualAddress != 0 && DebugDirectory.Size != 0)
    {
        if (DebugDirectory.Size % sizeof(IMAGE_DEBUG_DIRECTORY) != 0 ||
            !RperfPeRvaToOffset(Sections, Coff.NumberOfSections,
                                DebugDirectory.VirtualAddress,
                                DebugDirectory.Size, &DebugOffset))
        {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            goto Cleanup;
        }
        DebugCount = DebugDirectory.Size / sizeof(IMAGE_DEBUG_DIRECTORY);
        if (DebugCount > RPERF_PE_MAX_DEBUG_DIRECTORIES)
        {
            SetLastError(ERROR_BAD_EXE_FORMAT);
            goto Cleanup;
        }
        for (Index = 0; Index < DebugCount; ++Index)
        {
            IMAGE_DEBUG_DIRECTORY Entry;
            ULONGLONG DataOffset;

            if (!RperfPeRead(&File,
                             DebugOffset + Index * sizeof(Entry),
                             &Entry, sizeof(Entry)))
                goto Cleanup;
            if (Entry.Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
                Entry.SizeOfData < 24)
                continue;
            if (Entry.PointerToRawData != 0)
                DataOffset = Entry.PointerToRawData;
            else if (!RperfPeRvaToOffset(Sections, Coff.NumberOfSections,
                                         Entry.AddressOfRawData,
                                         Entry.SizeOfData, &DataOffset))
                continue;
            if (!RperfPeRangeValid(&File, DataOffset, Entry.SizeOfData) ||
                !RperfPeReadCodeView(&File, DataOffset,
                                     Entry.SizeOfData, Identity))
                goto Cleanup;
            break;
        }
    }
    Result = TRUE;

Cleanup:
    if (Optional != NULL)
        HeapFree(GetProcessHeap(), 0, Optional);
    if (Sections != NULL)
        HeapFree(GetProcessHeap(), 0, Sections);
    CloseHandle(File.Handle);
    return Result;
}

BOOL
RperfEnrichModuleFromImage(RPERF_MODULE *Module,
                           PCWSTR Path)
{
    RPERF_PE_IDENTITY Identity;

    if (Module == NULL || !RperfReadPeIdentity(Path, &Identity))
        return FALSE;
    Module->Architecture = Identity.Architecture;
    Module->TimeDateStamp = Identity.TimeDateStamp;
    Module->Checksum = Identity.Checksum;
    if (Identity.ImageSize != 0)
        Module->Size = Identity.ImageSize;
    CopyMemory(Module->DebugId, Identity.DebugId,
               sizeof(Module->DebugId));
    Module->DebugAge = Identity.DebugAge;
    if (Identity.HasRosSym)
        Module->Flags |= RPERF_MODULE_FLAG_EMBEDDED_ROSSYM;
    return TRUE;
}
