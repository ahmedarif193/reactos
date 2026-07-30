/*
 * PROJECT:     ReactOS Boot Video Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-mode FreeType runtime glue
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <stdio.h>
#include <stdarg.h>

#define TAG_BOOTVID_FREETYPE 'ftVB'
#define BOOTVID_FT_ALIGNMENT 16

typedef struct _BOOTVID_FT_ALLOCATION
{
    PVOID RawAllocation;
    SIZE_T Size;
} BOOTVID_FT_ALLOCATION, *PBOOTVID_FT_ALLOCATION;

void*
malloc(
    size_t Size)
{
    PUCHAR RawAllocation;
    PUCHAR AlignedObject;
    PBOOTVID_FT_ALLOCATION Header;
    SIZE_T TotalSize;

    if ((Size == 0) ||
        (Size > MAXULONG_PTR - sizeof(*Header) - BOOTVID_FT_ALIGNMENT + 1))
    {
        return NULL;
    }

    TotalSize = Size + sizeof(*Header) + BOOTVID_FT_ALIGNMENT - 1;
    RawAllocation = ExAllocatePoolWithTag(NonPagedPool,
                                          TotalSize,
                                          TAG_BOOTVID_FREETYPE);
    if (!RawAllocation)
        return NULL;

    AlignedObject = (PUCHAR)(((ULONG_PTR)(RawAllocation + sizeof(*Header) +
                                          BOOTVID_FT_ALIGNMENT - 1)) &
                              ~((ULONG_PTR)BOOTVID_FT_ALIGNMENT - 1));
    Header = (PBOOTVID_FT_ALLOCATION)(AlignedObject - sizeof(*Header));
    Header->RawAllocation = RawAllocation;
    Header->Size = Size;
    return AlignedObject;
}

void
free(
    void* Object)
{
    PBOOTVID_FT_ALLOCATION Header;

    if (!Object)
        return;

    Header = (PBOOTVID_FT_ALLOCATION)((PUCHAR)Object - sizeof(*Header));
    ExFreePoolWithTag(Header->RawAllocation, TAG_BOOTVID_FREETYPE);
}

void*
realloc(
    void* Object,
    size_t Size)
{
    PBOOTVID_FT_ALLOCATION Header;
    PVOID NewObject;

    if (!Object)
        return malloc(Size);

    if (Size == 0)
    {
        free(Object);
        return NULL;
    }

    NewObject = malloc(Size);
    if (!NewObject)
        return NULL;

    Header = (PBOOTVID_FT_ALLOCATION)((PUCHAR)Object - sizeof(*Header));
    RtlCopyMemory(NewObject, Object, min(Header->Size, Size));
    free(Object);
    return NewObject;
}

void*
calloc(
    size_t Count,
    size_t Size)
{
    SIZE_T TotalSize;
    PVOID Object;

    if ((Count == 0) || (Size == 0) || (Count > MAXULONG_PTR / Size))
        return NULL;

    TotalSize = Count * Size;
    Object = malloc(TotalSize);
    if (Object)
        RtlZeroMemory(Object, TotalSize);

    return Object;
}

void
FT_Message(
    const char* Format,
    ...)
{
    CHAR Buffer[256];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer), Format, Arguments);
    va_end(Arguments);

    Buffer[sizeof(Buffer) - 1] = ANSI_NULL;
    DbgPrint("FreeType: %s", Buffer);
}

void
FT_Panic(
    const char* Format,
    ...)
{
    UNREFERENCED_PARAMETER(Format);
    KeBugCheckEx(MANUALLY_INITIATED_CRASH, 0, 0, 0, 0);
}

FILE*
fopen(
    const char* FileName,
    const char* Mode)
{
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(Mode);
    return NULL;
}

int
fseek(
    FILE* Stream,
    long Offset,
    int Origin)
{
    UNREFERENCED_PARAMETER(Stream);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Origin);
    return -1;
}

long
ftell(
    FILE* Stream)
{
    UNREFERENCED_PARAMETER(Stream);
    return -1;
}

size_t
fread(
    void* Buffer,
    size_t Size,
    size_t Count,
    FILE* Stream)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Stream);
    return 0;
}

int
fclose(
    FILE* Stream)
{
    UNREFERENCED_PARAMETER(Stream);
    return EOF;
}
