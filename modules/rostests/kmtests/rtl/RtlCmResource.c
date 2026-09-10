/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Assigned resource length encodings used by modern PCI drivers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <kmt_test.h>

START_TEST(RtlCmResource)
{
    static const struct
    {
        ULONGLONG Length;
        UCHAR Type;
        USHORT Flags;
        ULONG EncodedLength;
    } Cases[] =
    {
        {0, CmResourceTypeMemory, 0, 0},
        {0xffffffffULL, CmResourceTypeMemory, 0, 0xffffffff},
        {0x100000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_40, 0x1000000},
        {0xffffffff00ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_40, 0xffffffff},
        {0x10000000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_48, 0x1000000},
        {0xffffffff0000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_48, 0xffffffff},
        {0x1000000000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_64, 0x10000},
        {0xffffffff00000000ULL, CmResourceTypeMemoryLarge, CM_RESOURCE_MEMORY_LARGE_64, 0xffffffff},
    };
    CM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor, Before;
    ULONGLONG Start, Length;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); ++Index)
    {
        RtlZeroMemory(&Descriptor, sizeof(Descriptor));
        Descriptor.ShareDisposition = CmResourceShareShared;
        Descriptor.Flags = CM_RESOURCE_MEMORY_READ_ONLY | CM_RESOURCE_MEMORY_PREFETCHABLE;
        Status = RtlCmEncodeMemIoResource(&Descriptor, CmResourceTypeMemory, Cases[Index].Length, 0x123456789ULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_uint(Descriptor.Type, Cases[Index].Type);
        ok_eq_uint(Descriptor.ShareDisposition, CmResourceShareShared);
        ok_eq_hex(Descriptor.Flags, Cases[Index].Flags | CM_RESOURCE_MEMORY_READ_ONLY | CM_RESOURCE_MEMORY_PREFETCHABLE);
        ok_eq_hex(Descriptor.u.Memory.Length, Cases[Index].EncodedLength);
        ok_eq_ulonglong(Descriptor.u.Memory.Start.QuadPart, 0x123456789ULL);
    }

    /* Decode a descriptor independently of the encoder. */
    RtlZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.Type = CmResourceTypeMemoryLarge;
    Descriptor.Flags = CM_RESOURCE_MEMORY_LARGE_48;
    Descriptor.u.Memory48.Start.QuadPart = 0x100000000ULL;
    Descriptor.u.Memory48.Length48 = 0x12345678;
    Length = RtlCmDecodeMemIoResource(&Descriptor, &Start);
    ok_eq_ulonglong(Length, 0x123456780000ULL);
    ok_eq_ulonglong(Start, 0x100000000ULL);
    ok_eq_ulonglong(RtlCmDecodeMemIoResource(&Descriptor, NULL), 0x123456780000ULL);

    Status = RtlCmEncodeMemIoResource(&Descriptor, CmResourceTypePort, 0x100, 0x3f8);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_uint(Descriptor.Type, CmResourceTypePort);
    ok_eq_ulonglong(RtlCmDecodeMemIoResource(&Descriptor, &Start), 0x100);
    ok_eq_ulonglong(Start, 0x3f8);

    Before = Descriptor;
    Status = RtlCmEncodeMemIoResource(&Descriptor, CmResourceTypeMemory, 0x100000001ULL, 0);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    ok_eq_size(RtlCompareMemory(&Descriptor, &Before, sizeof(Before)), sizeof(Before));
    Status = RtlCmEncodeMemIoResource(&Descriptor, CmResourceTypePort, 0x100000000ULL, 0);
    ok_eq_hex(Status, STATUS_UNSUCCESSFUL);
    Status = RtlCmEncodeMemIoResource(&Descriptor, CmResourceTypeInterrupt, 1, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}
