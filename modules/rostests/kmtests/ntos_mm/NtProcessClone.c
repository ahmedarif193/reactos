/*
 * PROJECT:     ReactOS kernel-mode tests
 * FILE:        modules/rostests/kmtests/ntos_mm/NtProcessClone.c
 * PURPOSE:     Process address-space cloning regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <kmt_test.h>
#include <ndk/psfuncs.h>

static VOID
CheckCloneValue(HANDLE Process, PVOID Address, ULONG Expected)
{
    ULONG Value = 0;
    SIZE_T Read = 0;
    NTSTATUS Status = NtReadVirtualMemory(Process, Address, &Value, sizeof(Value), &Read);

    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(Read, sizeof(Value));
    ok_eq_ulong(Value, Expected);
}

START_TEST(NtProcessClone)
{
    HANDLE Child = NULL, Grandchild = NULL, Section = NULL;
    PVOID Private = NULL, Shared = NULL, Omitted = NULL, Copy = NULL;
    PVOID Address;
    SIZE_T Size = 4 * PAGE_SIZE, Written, Returned;
    LARGE_INTEGER Maximum;
    OBJECT_ATTRIBUTES Attributes;
    MEMORY_BASIC_INFORMATION Memory;
    PROCESS_BASIC_INFORMATION ProcessInfo;
    BOOLEAN Inherited = FALSE;
    ULONG Old, Value;
    NTSTATUS Status;

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Private, 0, &Size,
                                      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    *(PULONG)Private = 0x11223344;
    *(PULONG)((PUCHAR)Private + PAGE_SIZE) = 0x12345678;
    Address = (PUCHAR)Private + PAGE_SIZE; Size = PAGE_SIZE;
    Status = NtProtectVirtualMemory(NtCurrentProcess(), &Address, &Size, PAGE_READONLY, &Old);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Address = (PUCHAR)Private + 2 * PAGE_SIZE;
    Status = NtFreeVirtualMemory(NtCurrentProcess(), &Address, &Size, MEM_DECOMMIT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Maximum.QuadPart = PAGE_SIZE;
    Status = NtCreateSection(&Section, SECTION_ALL_ACCESS, &Attributes, &Maximum,
                              PAGE_READWRITE, SEC_COMMIT, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    Size = PAGE_SIZE;
    Status = NtMapViewOfSection(Section, NtCurrentProcess(), &Shared, 0, 0, NULL, &Size,
                                 ViewShare, 0, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = NtMapViewOfSection(Section, NtCurrentProcess(), &Omitted, 0, 0, NULL, &Size,
                                 ViewUnmap, 0, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = NtMapViewOfSection(Section, NtCurrentProcess(), &Copy, 0, 0, NULL, &Size,
                                 ViewShare, 0, PAGE_WRITECOPY);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    *(PULONG)Shared = 10;
    *(PULONG)Copy = 20;

    Status = NtCreateProcessEx(&Child, PROCESS_ALL_ACCESS, &Attributes, NtCurrentProcess(),
                                0, NULL, NULL, NULL, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    CheckCloneValue(Child, Private, 0x11223344);
    CheckCloneValue(Child, (PUCHAR)Private + PAGE_SIZE, 0x12345678);
    CheckCloneValue(Child, (PUCHAR)Private + 3 * PAGE_SIZE, 0);
    CheckCloneValue(Child, Copy, 20);
    *(PULONG)Private = 0x55667788;
    CheckCloneValue(Child, Private, 0x11223344);
    Value = 0xAABBCCDD;
    Status = NtWriteVirtualMemory(Child, Private, &Value, sizeof(Value), &Written);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_size(Written, sizeof(Value));
    ok_eq_ulong(*(PULONG)Private, 0x55667788);
    CheckCloneValue(Child, Private, Value);
    Status = NtQueryVirtualMemory(Child, (PUCHAR)Private + PAGE_SIZE, MemoryBasicInformation,
                                   &Memory, sizeof(Memory), &Returned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Memory.Protect, PAGE_READONLY);
    ok_eq_hex(Memory.Type, MEM_PRIVATE);
    Status = NtQueryVirtualMemory(Child, (PUCHAR)Private + 2 * PAGE_SIZE, MemoryBasicInformation,
                                   &Memory, sizeof(Memory), &Returned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Memory.State, MEM_RESERVE);
    Status = NtQueryVirtualMemory(Child, Omitted, MemoryBasicInformation,
                                   &Memory, sizeof(Memory), &Returned);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Memory.State, MEM_FREE);
    Status = NtQueryInformationProcess(Child, ProcessBasicInformation, &ProcessInfo, sizeof(ProcessInfo), NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtReadVirtualMemory(Child, &ProcessInfo.PebBaseAddress->InheritedAddressSpace,
                                      &Inherited, sizeof(Inherited), &Returned);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_uint(Inherited, TRUE);
    }
    Status = NtCreateProcessEx(&Grandchild, PROCESS_ALL_ACCESS, &Attributes, Child,
                                0, NULL, NULL, NULL, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    CheckCloneValue(Grandchild, Private, Value);
    Value = 0xDEADBEEF;
    Status = NtWriteVirtualMemory(Child, Private, &Value, sizeof(Value), &Written);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckCloneValue(Grandchild, Private, 0xAABBCCDD);
    *(PULONG)Shared = 30;
    CheckCloneValue(Child, Shared, 30);
    CheckCloneValue(Grandchild, Shared, 30);
    Value = 40;
    Status = NtWriteVirtualMemory(Grandchild, Copy, &Value, sizeof(Value), &Written);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckCloneValue(Child, Copy, 20);
    ok_eq_ulong(*(PULONG)Copy, 20);
    Size = 0;
    Status = NtFreeVirtualMemory(NtCurrentProcess(), &Private, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        CheckCloneValue(Grandchild, Private, 0xAABBCCDD);
        Private = NULL;
    }
Done:
    if (Grandchild != NULL)
    {
        Status = NtTerminateProcess(Grandchild, STATUS_SUCCESS);
        ok(NT_SUCCESS(Status), "Terminate grandchild: %lx\n", Status);
        NtClose(Grandchild);
    }
    if (Child != NULL)
    {
        Status = NtTerminateProcess(Child, STATUS_SUCCESS);
        ok(NT_SUCCESS(Status), "Terminate child: %lx\n", Status);
        NtClose(Child);
    }
    if (Copy != NULL) NtUnmapViewOfSection(NtCurrentProcess(), Copy);
    if (Omitted != NULL) NtUnmapViewOfSection(NtCurrentProcess(), Omitted);
    if (Shared != NULL) NtUnmapViewOfSection(NtCurrentProcess(), Shared);
    if (Section != NULL) NtClose(Section);
    if (Private != NULL)
    {
        Size = 0;
        NtFreeVirtualMemory(NtCurrentProcess(), &Private, &Size, MEM_RELEASE);
    }
}
