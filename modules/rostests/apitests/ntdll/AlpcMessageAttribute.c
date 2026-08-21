/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Tests for the user-mode ALPC message attribute helpers
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

static const ULONG AttributeFlags[] =
{
    ALPC_MESSAGE_SECURITY_ATTRIBUTE,
    ALPC_MESSAGE_VIEW_ATTRIBUTE,
    ALPC_MESSAGE_CONTEXT_ATTRIBUTE,
    ALPC_MESSAGE_HANDLE_ATTRIBUTE,
    ALPC_MESSAGE_TOKEN_ATTRIBUTE,
    ALPC_MESSAGE_DIRECT_ATTRIBUTE,
    ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE,
};

static const SIZE_T AttributeSizes[] =
{
    sizeof(ALPC_SECURITY_ATTR),
    sizeof(ALPC_VIEW_ATTR),
    sizeof(ALPC_CONTEXT_ATTR),
    sizeof(ALPC_HANDLE_ATTR),
    sizeof(ALPC_TOKEN_ATTR),
    sizeof(ALPC_DIRECT_ATTR),
    sizeof(ALPC_WORK_ON_BEHALF_ATTR),
};

static
SIZE_T
AlpcTestExpectedAttributeSize(
    _In_ ULONG Flags)
{
    SIZE_T Size = sizeof(ALPC_MESSAGE_ATTRIBUTES);
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(AttributeFlags); ++Index)
    {
        if (Flags & AttributeFlags[Index])
            Size += AttributeSizes[Index];
    }
    return Size;
}

static
VOID
AlpcTestAttributeCombinationMatrix(VOID)
{
    UCHAR Buffer[512];
    UCHAR Before[512];
    PALPC_MESSAGE_ATTRIBUTES Attributes;
    SIZE_T RequiredSize;
    SIZE_T ExpectedSize;
    NTSTATUS Status;
    ULONG Combination;
    ULONG Flags;
    ULONG Index;

    for (Combination = 0; Combination < (1u << RTL_NUMBER_OF(AttributeFlags)); ++Combination)
    {
        Flags = 0;
        for (Index = 0; Index < RTL_NUMBER_OF(AttributeFlags); ++Index)
        {
            if (Combination & (1u << Index))
                Flags |= AttributeFlags[Index];
        }

        ExpectedSize = AlpcTestExpectedAttributeSize(Flags);
        ok_eq_size(AlpcGetHeaderSize(Flags), ExpectedSize);

        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        RtlCopyMemory(Before, Buffer, sizeof(Buffer));
        RequiredSize = (SIZE_T)0x5555555555555555ULL;
        Status = AlpcInitializeMessageAttribute(Flags, (PALPC_MESSAGE_ATTRIBUTES)Buffer, ExpectedSize - 1, &RequiredSize);
        ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
        ok_eq_size(RequiredSize, ExpectedSize);
        ok(!memcmp(Buffer, Before, sizeof(Buffer)), "short attribute buffer changed for flags %08lx\n", Flags);

        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        RtlCopyMemory(Before, Buffer, sizeof(Buffer));
        RequiredSize = (SIZE_T)0x5555555555555555ULL;
        Status = AlpcInitializeMessageAttribute(Flags, (PALPC_MESSAGE_ATTRIBUTES)Buffer, ExpectedSize, &RequiredSize);
        ok_hex(Status, STATUS_SUCCESS);
        ok_eq_size(RequiredSize, ExpectedSize);
        Attributes = (PALPC_MESSAGE_ATTRIBUTES)Buffer;
        ok_eq_ulong(Attributes->AllocatedAttributes, Flags);
        ok_eq_ulong(Attributes->ValidAttributes, 0);
        AlpcTestTraceBufferMutation("MessageAttributes.combination_exact", Before, Buffer, sizeof(Buffer));

        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        RequiredSize = (SIZE_T)0x5555555555555555ULL;
        Status = AlpcInitializeMessageAttribute(Flags, (PALPC_MESSAGE_ATTRIBUTES)Buffer, sizeof(Buffer), &RequiredSize);
        ok_hex(Status, STATUS_SUCCESS);
        ok_eq_size(RequiredSize, ExpectedSize);
    }
}

static
VOID
AlpcTestAttributeAlignmentAndFaults(VOID)
{
    UCHAR Buffer[512];
    PALPC_MESSAGE_ATTRIBUTES Attributes;
    SIZE_T RequiredSize;
    PVOID Attribute = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    Attributes = (PALPC_MESSAGE_ATTRIBUTES)(Buffer + 1);
    RequiredSize = 0;
    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_ATTRIBUTE_ALL, Attributes, sizeof(Buffer) - 1, &RequiredSize);
    trace("ALPC_OBSERVE status MessageAttributes.misaligned_buffer=%08lx required=%Iu allocated=%08lx valid=%08lx\n", Status, RequiredSize, Attributes->AllocatedAttributes, Attributes->ValidAttributes);
    ok(Status != STATUS_NOT_IMPLEMENTED, "misaligned attribute initialization reached a stub\n");

    StartSeh()
        Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_CONTEXT_ATTRIBUTE, (PALPC_MESSAGE_ATTRIBUTES)Buffer, sizeof(Buffer), NULL);
    EndSeh(STATUS_ACCESS_VIOLATION);
    UNREFERENCED_PARAMETER(Status);

    StartSeh()
        Attribute = AlpcGetMessageAttribute(NULL, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    EndSeh(STATUS_ACCESS_VIOLATION);
    UNREFERENCED_PARAMETER(Attribute);
}

static
VOID
AlpcTestUnknownAttributeFlags(VOID)
{
    static const ULONG UnknownFlags[] = {1, 0x01000000, 0x01000001, 0xffffffff};
    UCHAR Buffer[512];
    PALPC_MESSAGE_ATTRIBUTES Attributes = (PALPC_MESSAGE_ATTRIBUTES)Buffer;
    SIZE_T ExpectedSize;
    SIZE_T RequiredSize;
    PVOID Attribute;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(UnknownFlags); ++Index)
    {
        ExpectedSize = AlpcTestExpectedAttributeSize(UnknownFlags[Index]);
        ok_eq_size(AlpcGetHeaderSize(UnknownFlags[Index]), ExpectedSize);
        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        RequiredSize = 0;
        Status = AlpcInitializeMessageAttribute(UnknownFlags[Index], Attributes, sizeof(Buffer), &RequiredSize);
        ok_hex(Status, STATUS_SUCCESS);
        ok_eq_size(RequiredSize, ExpectedSize);
        ok_eq_ulong(Attributes->AllocatedAttributes, UnknownFlags[Index]);
        ok_eq_ulong(Attributes->ValidAttributes, 0);
        Attribute = AlpcGetMessageAttribute(Attributes, UnknownFlags[Index] & (0 - UnknownFlags[Index]));
        trace("ALPC_OBSERVE value MessageAttributes.unknown flags=%08lx pointer=%p offset=%Id\n", UnknownFlags[Index], Attribute, Attribute ? (SSIZE_T)((UCHAR *)Attribute - Buffer) : -1);
    }
}

START_TEST(AlpcMessageAttribute)
{
    UCHAR Buffer[256];
    PALPC_MESSAGE_ATTRIBUTES Attributes = (PALPC_MESSAGE_ATTRIBUTES)Buffer;
    SIZE_T ExpectedSize, RequiredSize;
    PVOID Attribute;
    NTSTATUS Status;
    ULONG Flags;
    ULONG i;

    if (AlpcTestIsChildMode("attribute-faults"))
    {
        AlpcTestAttributeAlignmentAndFaults();
        return;
    }

    ok_eq_ulong(AlpcMaxAllowedMessageLength(), ALPC_MAX_ALLOWED_MESSAGE_LENGTH);
    ok_eq_size(AlpcGetHeaderSize(0), sizeof(ALPC_MESSAGE_ATTRIBUTES));
    ok_eq_size(AlpcGetHeaderSize(1), sizeof(ALPC_MESSAGE_ATTRIBUTES));
    ok_eq_size(sizeof(ALPC_HANDLE_ATTR32), 0x10);
    ok_eq_size(sizeof(ALPC_MESSAGE_HANDLE_INFORMATION), 0x14);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, Flags), 0);
#ifdef _WIN64
    ok_eq_size(sizeof(ALPC_HANDLE_ATTR), 0x18);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, Handle), 8);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, HandleAttrArray), 8);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, ObjectType), 0x10);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, HandleCount), 0x10);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, DesiredAccess), 0x14);
    ok_eq_size(AlpcGetHeaderSize(ALPC_MESSAGE_HANDLE_ATTRIBUTE), sizeof(ALPC_MESSAGE_ATTRIBUTES) + 0x18);
#else
    ok_eq_size(sizeof(ALPC_HANDLE_ATTR), 0x10);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, Handle), 4);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, HandleAttrArray), 4);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, ObjectType), 8);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, HandleCount), 8);
    ok_eq_size(FIELD_OFFSET(ALPC_HANDLE_ATTR, DesiredAccess), 0xc);
    ok_eq_size(AlpcGetHeaderSize(ALPC_MESSAGE_HANDLE_ATTRIBUTE), sizeof(ALPC_MESSAGE_ATTRIBUTES) + 0x10);
#endif
    trace("ALPC_ASSERT_ABI handle_attr=%Iu handle_attr32=%Iu handle_information=%Iu\n", sizeof(ALPC_HANDLE_ATTR), sizeof(ALPC_HANDLE_ATTR32), sizeof(ALPC_MESSAGE_HANDLE_INFORMATION));

    ExpectedSize = sizeof(ALPC_MESSAGE_ATTRIBUTES);
    Flags = 0;
    for (i = 0; i < RTL_NUMBER_OF(AttributeFlags); ++i)
    {
        Flags |= AttributeFlags[i];
        ExpectedSize += AttributeSizes[i];
        ok_eq_size(AlpcGetHeaderSize(Flags), ExpectedSize);
    }
    ok_eq_size(AlpcGetHeaderSize(ALPC_MESSAGE_ATTRIBUTE_ALL), ExpectedSize);

    RequiredSize = 0x55555555;
    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_ATTRIBUTE_ALL, NULL, 0, &RequiredSize);
    ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_size(RequiredSize, ExpectedSize);

    RequiredSize = 0x55555555;
    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_ATTRIBUTE_ALL, NULL, ExpectedSize, &RequiredSize);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_size(RequiredSize, ExpectedSize);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    RequiredSize = 0x55555555;
    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_ATTRIBUTE_ALL, Attributes, ExpectedSize - 1, &RequiredSize);
    ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_size(RequiredSize, ExpectedSize);
    ok_eq_ulong(Attributes->AllocatedAttributes, 0x55555555);
    ok_eq_ulong(Attributes->ValidAttributes, 0x55555555);

    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_ATTRIBUTE_ALL, Attributes, sizeof(Buffer), &RequiredSize);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_size(RequiredSize, ExpectedSize);
    ok_eq_ulong(Attributes->AllocatedAttributes, ALPC_MESSAGE_ATTRIBUTE_ALL);
    ok_eq_ulong(Attributes->ValidAttributes, 0);

    ExpectedSize = sizeof(ALPC_MESSAGE_ATTRIBUTES);
    for (i = 0; i < RTL_NUMBER_OF(AttributeFlags); ++i)
    {
        Attribute = AlpcGetMessageAttribute(Attributes, AttributeFlags[i]);
        ok(Attribute == Buffer + ExpectedSize, "attribute %#lx is at %p, expected %p\n", AttributeFlags[i], Attribute, Buffer + ExpectedSize);
        ExpectedSize += AttributeSizes[i];
    }

    ok(AlpcGetMessageAttribute(Attributes, 0) == NULL, "zero attribute unexpectedly returned a pointer\n");
    ok(AlpcGetMessageAttribute(Attributes, ALPC_MESSAGE_SECURITY_ATTRIBUTE | ALPC_MESSAGE_VIEW_ATTRIBUTE) == NULL, "multiple attributes unexpectedly returned a pointer\n");
    ok(AlpcGetMessageAttribute(Attributes, 1) == NULL, "unknown attribute unexpectedly returned a pointer\n");

    Attributes->AllocatedAttributes &= ~ALPC_MESSAGE_HANDLE_ATTRIBUTE;
    ok(AlpcGetMessageAttribute(Attributes, ALPC_MESSAGE_HANDLE_ATTRIBUTE) == NULL, "unallocated attribute unexpectedly returned a pointer\n");

    Attributes->AllocatedAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    Attributes->ValidAttributes = 0;
    ok(AlpcGetMessageAttribute(Attributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE) != NULL, "ValidAttributes unexpectedly controlled lookup\n");
    Attributes->ValidAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    ok(AlpcGetMessageAttribute(Attributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE) != NULL, "valid allocated attribute was not found\n");

    AlpcTestAttributeCombinationMatrix();
    AlpcTestUnknownAttributeFlags();
    AlpcTestRunIsolatedCase(L"AlpcMessageAttribute", L"attribute-faults", ALPC_TEST_CHILD_TIMEOUT_MS);
}
