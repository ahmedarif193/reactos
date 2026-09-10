/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Encode and decode assigned memory and I/O resources
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <ntoskrnl.h>

ULONGLONG
NTAPI
RtlCmDecodeMemIoResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _Out_opt_ PULONGLONG Start)
{
    ULONG Shift = 0;

    switch (Descriptor->Type)
    {
        case CmResourceTypePort:
        case CmResourceTypeMemory:
            break;

        case CmResourceTypeMemoryLarge:
            switch (Descriptor->Flags & CM_RESOURCE_MEMORY_LARGE)
            {
                case CM_RESOURCE_MEMORY_LARGE_40: Shift = 8; break;
                case CM_RESOURCE_MEMORY_LARGE_48: Shift = 16; break;
                case CM_RESOURCE_MEMORY_LARGE_64: Shift = 32; break;
                default: return 0;
            }
            break;

        default:
            return 0;
    }

    if (Start != NULL)
        *Start = Descriptor->u.Memory.Start.QuadPart;
    return (ULONGLONG)Descriptor->u.Memory.Length << Shift;
}

NTSTATUS
NTAPI
RtlCmEncodeMemIoResource(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor,
    _In_ UCHAR Type,
    _In_ ULONGLONG Length,
    _In_ ULONGLONG Start)
{
    USHORT Flags = 0;
    ULONG Shift = 0;

    if (Type != CmResourceTypePort &&
        Type != CmResourceTypeMemory &&
        Type != CmResourceTypeMemoryLarge)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length > MAXULONG)
    {
        if (Type == CmResourceTypePort)
            return STATUS_UNSUCCESSFUL;

        if (Length <= CM_RESOURCE_MEMORY_LARGE_40_MAXLEN && !(Length & 0xff))
        {
            Shift = 8;
            Flags = CM_RESOURCE_MEMORY_LARGE_40;
        }
        else if (Length <= CM_RESOURCE_MEMORY_LARGE_48_MAXLEN && !(Length & 0xffff))
        {
            Shift = 16;
            Flags = CM_RESOURCE_MEMORY_LARGE_48;
        }
        else if (!(Length & MAXULONG))
        {
            Shift = 32;
            Flags = CM_RESOURCE_MEMORY_LARGE_64;
        }
        else
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    Descriptor->Type = Type == CmResourceTypePort ? Type :
                       (Shift ? CmResourceTypeMemoryLarge : CmResourceTypeMemory);
    Descriptor->Flags = (Descriptor->Flags & ~CM_RESOURCE_MEMORY_LARGE) | Flags;
    Descriptor->u.Memory.Start.QuadPart = Start;
    Descriptor->u.Memory.Length = (ULONG)(Length >> Shift);
    return STATUS_SUCCESS;
}
