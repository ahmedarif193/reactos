/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Extended atom-table native-parity tests
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

NTSTATUS
NTAPI
RtlCreateAtomTableEx(
    _In_ ULONG TableSize,
    _In_ ULONG Flags,
    _Inout_ PRTL_ATOM_TABLE *AtomTable);

NTSTATUS
NTAPI
RtlAddAtomToAtomTableEx(
    _In_ PRTL_ATOM_TABLE AtomTable,
    _In_ PWSTR AtomName,
    _Out_opt_ PRTL_ATOM Atom,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
RtlDeleteAtomFromAtomTable(
    _In_ PRTL_ATOM_TABLE AtomTable,
    _In_ RTL_ATOM Atom);

NTSTATUS
NTAPI
RtlDestroyAtomTable(
    _In_ PRTL_ATOM_TABLE AtomTable);

NTSTATUS
NTAPI
RtlLookupAtomInAtomTable(
    _In_ PRTL_ATOM_TABLE AtomTable,
    _In_ PWSTR AtomName,
    _Out_ PRTL_ATOM Atom);

NTSTATUS
NTAPI
RtlQueryAtomInAtomTable(
    _In_ PRTL_ATOM_TABLE AtomTable,
    _In_ RTL_ATOM Atom,
    _Out_opt_ PULONG RefCount,
    _Out_opt_ PULONG PinCount,
    _Out_opt_ PWSTR AtomName,
    _Inout_opt_ PULONG NameLength);

START_TEST(RtlAtomEx)
{
    static WCHAR AtomName[] = L"ReactOS-RtlAtomEx";
    static const ULONG Flags[] = {0, 1, 2, MAXULONG};
    PRTL_ATOM_TABLE Table;
    RTL_ATOM Atom;
    RTL_ATOM LookupAtom;
    NTSTATUS Status;
    NTSTATUS QueryStatus;
    ULONG PinCount;
    ULONG RefCount;
    ULONG Index;

    Table = (PRTL_ATOM_TABLE)(ULONG_PTR)0x12345678;
    Status = RtlCreateAtomTableEx(37, MAXULONG, &Table);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Table, (PVOID)(ULONG_PTR)0x12345678);

    for (Index = 0; Index < RTL_NUMBER_OF(Flags); Index++)
    {
        Table = NULL;
        Status = RtlCreateAtomTableEx(37, Flags[Index], &Table);
        trace("RtlCreateAtomTableEx(flags 0x%lx) returned 0x%08lx, table %p\n", Flags[Index], Status, Table);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Table != NULL, "atom table is NULL for flags 0x%lx\n", Flags[Index]);
        if (NT_SUCCESS(Status))
        {
            Atom = 0;
            Status = RtlAddAtomToAtomTableEx(Table, AtomName, &Atom, Flags[Index]);
            trace("RtlAddAtomToAtomTableEx(flags 0x%lx) returned 0x%08lx, atom 0x%x\n", Flags[Index], Status, Atom);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok(Atom >= 0xc000, "invalid string atom 0x%x\n", Atom);
            if (NT_SUCCESS(Status))
            {
                PinCount = MAXULONG;
                RefCount = MAXULONG;
                QueryStatus = RtlQueryAtomInAtomTable(Table, Atom, &RefCount, &PinCount, NULL, NULL);
                trace("RtlQueryAtomInAtomTable returned 0x%08lx, refs %lu, pins %lu\n", QueryStatus, RefCount, PinCount);
                ok_eq_hex(QueryStatus, STATUS_SUCCESS);
                ok_eq_ulong(RefCount, 1);
                ok_eq_ulong(PinCount, Flags[Index] & 2);
                QueryStatus = RtlDeleteAtomFromAtomTable(Table, Atom);
                LookupAtom = 0;
                ok_eq_hex(QueryStatus, STATUS_SUCCESS);
                QueryStatus = RtlLookupAtomInAtomTable(Table, AtomName, &LookupAtom);
                trace("RtlDeleteAtomFromAtomTable lookup returned 0x%08lx\n", QueryStatus);
                ok_eq_hex(QueryStatus, STATUS_OBJECT_NAME_NOT_FOUND);
            }
            RtlDestroyAtomTable(Table);
        }
    }
}
