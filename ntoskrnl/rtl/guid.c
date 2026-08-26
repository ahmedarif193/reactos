/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RTL name-based GUID generation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct
{
    UCHAR Buffer[64];
    ULONG State[5];
    ULONG Count[2];
} SHA_CTX, *PSHA_CTX;

VOID NTAPI A_SHAInit(PSHA_CTX Context);
VOID NTAPI A_SHAUpdate(PSHA_CTX Context, const unsigned char *Buffer, ULONG BufferSize);
VOID NTAPI A_SHAFinal(PSHA_CTX Context, PULONG Result);

NTSTATUS
NTAPI
RtlGenerateClass5Guid(
    _In_ REFGUID NamespaceGuid,
    _In_reads_bytes_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ GUID *Guid)
{
    SHA_CTX Context;
    UCHAR NamespaceBytes[sizeof(GUID)];
    UCHAR Digest[20];

    if ((NamespaceGuid == NULL) || (Guid == NULL) || ((BufferSize != 0) && (Buffer == NULL)))
        return STATUS_INVALID_PARAMETER;

    NamespaceBytes[0] = (UCHAR)(NamespaceGuid->Data1 >> 24);
    NamespaceBytes[1] = (UCHAR)(NamespaceGuid->Data1 >> 16);
    NamespaceBytes[2] = (UCHAR)(NamespaceGuid->Data1 >> 8);
    NamespaceBytes[3] = (UCHAR)NamespaceGuid->Data1;
    NamespaceBytes[4] = (UCHAR)(NamespaceGuid->Data2 >> 8);
    NamespaceBytes[5] = (UCHAR)NamespaceGuid->Data2;
    NamespaceBytes[6] = (UCHAR)(NamespaceGuid->Data3 >> 8);
    NamespaceBytes[7] = (UCHAR)NamespaceGuid->Data3;
    RtlCopyMemory(&NamespaceBytes[8], NamespaceGuid->Data4, sizeof(NamespaceGuid->Data4));

    A_SHAInit(&Context);
    A_SHAUpdate(&Context, NamespaceBytes, sizeof(NamespaceBytes));
    if (BufferSize != 0)
        A_SHAUpdate(&Context, Buffer, BufferSize);
    A_SHAFinal(&Context, (PULONG)Digest);

    Digest[6] = (Digest[6] & 0x0F) | 0x50;
    Digest[8] = (Digest[8] & 0x3F) | 0x80;
    Guid->Data1 = ((ULONG)Digest[0] << 24) | ((ULONG)Digest[1] << 16) | ((ULONG)Digest[2] << 8) | Digest[3];
    Guid->Data2 = (USHORT)(((USHORT)Digest[4] << 8) | Digest[5]);
    Guid->Data3 = (USHORT)(((USHORT)Digest[6] << 8) | Digest[7]);
    RtlCopyMemory(Guid->Data4, &Digest[8], sizeof(Guid->Data4));
    return STATUS_SUCCESS;
}
