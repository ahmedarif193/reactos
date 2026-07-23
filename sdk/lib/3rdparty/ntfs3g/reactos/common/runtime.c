/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Freestanding C runtime support shared by all hosts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host.h"

typedef union _NTFS3G_ALLOCATION_HEADER
{
    size_t Size;
    uint64_t IntegerAlignment;
    void *PointerAlignment;
} NTFS3G_ALLOCATION_HEADER;

static int Ntfs3gErrno;

int *
ntfs3g_errno_location(void)
{
    return &Ntfs3gErrno;
}

void *
ntfs3g_malloc(size_t Size)
{
    NTFS3G_ALLOCATION_HEADER *Header;

    if (Size > SIZE_MAX - sizeof(*Header)) {
        errno = ENOMEM;
        return NULL;
    }

    Header = Ntfs3gRosHostAllocate(sizeof(*Header) + Size);
    if (!Header) {
        errno = ENOMEM;
        return NULL;
    }
    Header->Size = Size;
    return Header + 1;
}

void *
ntfs3g_calloc(size_t Count,
              size_t Size)
{
    size_t Total;
    void *Buffer;

    if (Size && Count > SIZE_MAX / Size) {
        errno = ENOMEM;
        return NULL;
    }
    Total = Count * Size;
    Buffer = ntfs3g_malloc(Total);
    if (Buffer)
        memset(Buffer, 0, Total);
    return Buffer;
}

void
ntfs3g_free(void *Buffer)
{
    if (Buffer)
        Ntfs3gRosHostFree((NTFS3G_ALLOCATION_HEADER *)Buffer - 1);
}

void *
ntfs3g_realloc(void *Buffer,
               size_t Size)
{
    NTFS3G_ALLOCATION_HEADER *OldHeader;
    void *NewBuffer;
    size_t CopySize;

    if (!Buffer)
        return ntfs3g_malloc(Size);
    if (!Size) {
        ntfs3g_free(Buffer);
        return NULL;
    }

    OldHeader = (NTFS3G_ALLOCATION_HEADER *)Buffer - 1;
    NewBuffer = ntfs3g_malloc(Size);
    if (!NewBuffer)
        return NULL;
    CopySize = OldHeader->Size < Size ? OldHeader->Size : Size;
    memcpy(NewBuffer, Buffer, CopySize);
    ntfs3g_free(Buffer);
    return NewBuffer;
}

char *
ntfs3g_strdup(const char *String)
{
    size_t Length;
    char *Copy;

    if (!String) {
        errno = EINVAL;
        return NULL;
    }
    Length = strlen(String) + 1;
    Copy = ntfs3g_malloc(Length);
    if (Copy)
        memcpy(Copy, String, Length);
    return Copy;
}

size_t
ntfs3g_strnlen(const char *String,
               size_t MaximumLength)
{
    size_t Length;

    for (Length = 0; Length < MaximumLength && String[Length]; ++Length)
        ;
    return Length;
}

char *
ntfs3g_strerror(int Error)
{
    switch (Error) {
        case 0: return "success";
        case EACCES: return "access denied";
        case EBUSY: return "device busy";
        case EINVAL: return "invalid argument";
        case EIO: return "I/O error";
        case ENOENT: return "file not found";
        case ENOMEM: return "out of memory";
        case EROFS: return "read-only filesystem";
        default: return "NTFS-3G host error";
    }
}

time_t
ntfs3g_time(time_t *Result)
{
    time_t Value = (time_t)Ntfs3gRosHostGetTime();

    if (Result)
        *Result = Value;
    return Value;
}

char *
ntfs3g_setlocale(int Category,
                 const char *Locale)
{
    static char CLocale[] = "C";

    (void)Category;
    (void)Locale;
    return CLocale;
}

int
___mb_cur_max_func(void)
{
    return 4;
}

int
ntfs3g_ffs(int Value)
{
    unsigned int Bits = (unsigned int)Value;
    int Position = 1;

    if (!Bits)
        return 0;
    while (!(Bits & 1)) {
        Bits >>= 1;
        ++Position;
    }
    return Position;
}

int
ntfs3g_mbtowc(wchar_t *WideCharacter,
              const char *String,
              size_t Length)
{
    unsigned char Character;

    if (!String)
        return 0;
    if (!Length) {
        errno = EILSEQ;
        return -1;
    }
    Character = *(const unsigned char *)String;
    if (Character > 0x7f) {
        errno = EILSEQ;
        return -1;
    }
    if (WideCharacter)
        *WideCharacter = Character;
    return Character ? 1 : 0;
}

size_t
ntfs3g_mbstowcs(wchar_t *Destination,
                const char *Source,
                size_t Length)
{
    size_t Count = 0;

    while (Source[Count]) {
        if ((unsigned char)Source[Count] > 0x7f) {
            errno = EILSEQ;
            return (size_t)-1;
        }
        if (Destination && Count < Length)
            Destination[Count] = (unsigned char)Source[Count];
        ++Count;
    }
    if (Destination && Count < Length)
        Destination[Count] = L'\0';
    return Count;
}

int
ntfs3g_wctomb(char *Destination,
              wchar_t WideCharacter)
{
    if (!Destination)
        return 0;
    if ((uint32_t)WideCharacter > 0x7f) {
        errno = EILSEQ;
        return -1;
    }
    *Destination = (char)WideCharacter;
    return 1;
}
