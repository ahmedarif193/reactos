/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RISC-V: explicit fault recovery for architecture-owned user copies. */
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern UCHAR KiRiscvCopyFromUserFault[], KiRiscvCopyToUserFault[], KiRiscvCopyUserFailure[];
extern UCHAR KiRiscvReadKernelFault[];
NTSTATUS NTAPI KiRiscvReadKernelRaw(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiRiscvCopyFromUserRaw(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiRiscvCopyToUserRaw(PVOID Destination, const VOID *Source, SIZE_T Length);

static
BOOLEAN
KiRiscvUserRange(_In_ const VOID *Address, _In_ SIZE_T Length)
{
    ULONG_PTR Start = (ULONG_PTR)Address;
    ULONG_PTR Limit = (ULONG_PTR)MmUserProbeAddress;

    return (Start >= MM_ALLOCATION_GRANULARITY) && (Start < Limit) && (Length <= Limit - Start);
}

/* SUM permits supervisor access to U pages; it does not reject S-only pages
 * in the low half. Require actual user permissions, not just a low address.
 * The assembly fixup is still required if a mapping changes after probing. */
static
NTSTATUS
KiRiscvProbeUserPages(_In_ const VOID *Address, _In_ SIZE_T Length, _In_ BOOLEAN Write)
{
    ULONG_PTR Page = ALIGN_DOWN_BY((ULONG_PTR)Address, PAGE_SIZE);
    ULONG_PTR Last = ALIGN_DOWN_BY((ULONG_PTR)Address + Length - 1, PAGE_SIZE);
    ULONG64 Access = MI_RISCV_PTE_OWNER | (Write ? MI_RISCV_PTE_WRITE : MI_RISCV_PTE_READ);
    MI_RISCV_PAGE_WALK Walk;
    NTSTATUS Status;
    ULONG Fault;

    for (;;)
    {
        Status = MiRiscvWalkCurrentPageTables((PVOID)Page, &Walk);
        if (!NT_SUCCESS(Status) || ((Walk.Value.u.Long & Access) != Access))
        {
            Fault = Write ? MI_RISCV_FAULT_WRITE : 0;
            if (NT_SUCCESS(Status)) Fault |= MI_RISCV_FAULT_PRESENT;
            Status = MmAccessFault(Fault, (PVOID)Page, UserMode, KeGetCurrentThread()->TrapFrame);
            if (!NT_SUCCESS(Status)) return Status;
            Status = MiRiscvWalkCurrentPageTables((PVOID)Page, &Walk);
            if (!NT_SUCCESS(Status) || ((Walk.Value.u.Long & Access) != Access))
                return STATUS_ACCESS_VIOLATION;
        }
        if (Page == Last) return STATUS_SUCCESS;
        Page += PAGE_SIZE;
    }
}

NTSTATUS
NTAPI
KiRiscvCopyFromUser(_Out_writes_bytes_(Length) PVOID Destination, _In_ const VOID *Source, _In_ SIZE_T Length)
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    if (!Length) return STATUS_SUCCESS;
    if (!KiRiscvUserRange(Source, Length)) return STATUS_ACCESS_VIOLATION;
    Status = KiRiscvProbeUserPages(Source, Length, FALSE);
    if (!NT_SUCCESS(Status)) return Status;
    return KiRiscvCopyFromUserRaw(Destination, Source, Length);
}

NTSTATUS
NTAPI
KiRiscvCopyToUser(_Out_ PVOID Destination, _In_reads_bytes_(Length) const VOID *Source, _In_ SIZE_T Length)
{
    NTSTATUS Status;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    if (!Length) return STATUS_SUCCESS;
    if (!KiRiscvUserRange(Destination, Length)) return STATUS_ACCESS_VIOLATION;
    Status = KiRiscvProbeUserPages(Destination, Length, TRUE);
    if (!NT_SUCCESS(Status)) return Status;
    return KiRiscvCopyToUserRaw(Destination, Source, Length);
}

BOOLEAN
NTAPI
KiRiscvFixupUserCopy(_Inout_ PKTRAP_FRAME Frame, _In_ NTSTATUS Status)
{
    ULONG_PTR Pc = Frame->Context.Pc;
    ULONG_PTR UserAddress;

    if (KiUserTrap(Frame)) return FALSE;
    if (Pc == (ULONG_PTR)KiRiscvReadKernelFault &&
        (Frame->Scause == 13 || Frame->Scause == 5) &&
        Frame->Stval == Frame->Context.A1) {
        Frame->Context.A0 = (LONG_PTR)Status;
        Frame->Context.Pc = (ULONG_PTR)KiRiscvCopyUserFailure;
        return TRUE;
    }
    if ((Pc == (ULONG_PTR)KiRiscvCopyFromUserFault) && ((Frame->Scause == 13) || (Frame->Scause == 5)))
        UserAddress = Frame->Context.A1;
    else if ((Pc == (ULONG_PTR)KiRiscvCopyToUserFault) && ((Frame->Scause == 15) || (Frame->Scause == 7)))
        UserAddress = Frame->Context.A0;
    else
        return FALSE;
    if ((Frame->Stval != UserAddress) || !KiRiscvUserRange((PVOID)UserAddress, 1))
        return FALSE;

    Frame->Context.A0 = (LONG_PTR)Status;
    Frame->Context.Pc = (ULONG_PTR)KiRiscvCopyUserFailure;
    return TRUE;
}

NTSTATUS NTAPI
KiRiscvReadMemory(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    ULONG_PTR Address = (ULONG_PTR)Source, Last, Page;
    MI_RISCV_PAGE_WALK Walk;
    if (!Length) return STATUS_SUCCESS;
    if (Address < (ULONG_PTR)MmSystemRangeStart)
        return KiRiscvCopyFromUser(Destination, Source, Length);
    if (Length - 1 > MAXULONG_PTR - Address) return STATUS_ACCESS_VIOLATION;
    Last = ALIGN_DOWN_BY(Address + Length - 1, PAGE_SIZE);
    for (Page = ALIGN_DOWN_BY(Address, PAGE_SIZE); ; Page += PAGE_SIZE) {
        if (!NT_SUCCESS(MiRiscvWalkCurrentPageTables((PVOID)Page, &Walk)) ||
            !(Walk.Value.u.Long & MI_RISCV_PTE_READ)) return STATUS_ACCESS_VIOLATION;
        if (Page == Last) break;
    }
    return KiRiscvReadKernelRaw(Destination, Source, Length);
}

/* NtReadVirtualMemory's current-process user path must be fault-safe before
 * the user SEH dispatcher can safely consume metadata. Both sides are U
 * pages, copied through a bounded kernel buffer using the existing fixups. */
NTSTATUS NTAPI
KiRiscvReadCurrentProcess(PVOID Source, PVOID Destination, SIZE_T Length,
                         PSIZE_T Returned)
{
    UCHAR Buffer[256];
    SIZE_T Done = 0, Chunk;
    NTSTATUS Status = STATUS_SUCCESS, CopyStatus;
    if (Length && (!KiRiscvUserRange(Source, Length) ||
                   !KiRiscvUserRange(Destination, Length)))
        return STATUS_ACCESS_VIOLATION;
    if (Returned) {
        Status = KiRiscvCopyToUser(Returned, &Done, sizeof(Done));
        if (!NT_SUCCESS(Status)) return Status;
    }
    while (Done < Length) {
        Chunk = min(Length - Done, sizeof(Buffer));
        Status = KiRiscvCopyFromUser(Buffer, (PUCHAR)Source + Done, Chunk);
        if (!NT_SUCCESS(Status)) break;
        Status = KiRiscvCopyToUser((PUCHAR)Destination + Done, Buffer, Chunk);
        if (!NT_SUCCESS(Status)) break;
        Done += Chunk;
    }
    if (!NT_SUCCESS(Status) && Done) Status = STATUS_PARTIAL_COPY;
    if (Returned) {
        CopyStatus = KiRiscvCopyToUser(Returned, &Done, sizeof(Done));
        if (!NT_SUCCESS(CopyStatus)) return CopyStatus;
    }
    return Status;
}
