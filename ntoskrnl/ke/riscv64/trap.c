/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V first-entry trap diagnostics
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#ifdef KDBG
#include <kdbg/kdb.h>
#endif

static const CHAR *const KiRiscvRegisterNames[32] =
{
    "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
    "s0", "s1", "a0", "a1", "a2", "a3", "a4", "a5",
    "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7",
    "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

static const CHAR *const KiRiscvExceptionNames[16] =
{
    "instruction address misaligned", "instruction access fault",
    "illegal instruction", "breakpoint",
    "load address misaligned", "load access fault",
    "store/AMO address misaligned", "store/AMO access fault",
    "environment call from U-mode", "environment call from S-mode",
    "reserved (10)", "reserved (11)",
    "instruction page fault", "load page fault",
    "reserved (14)", "store/AMO page fault"
};

static
const CHAR *
KiRiscvDecodeScause(
    _In_ ULONG64 Scause)
{
    ULONG64 Code = Scause & ~(1ULL << 63);

    if (Scause & (1ULL << 63))
    {
        switch (Code)
        {
            case 1: return "supervisor software interrupt";
            case 5: return "supervisor timer interrupt";
            case 9: return "supervisor external interrupt";
            default: return "interrupt";
        }
    }
    if (Code < RTL_NUMBER_OF(KiRiscvExceptionNames))
        return KiRiscvExceptionNames[Code];
    return "unknown exception";
}

/* Name the loader memory descriptor and module that own an address, so a
 * failed kernel fault can be attributed without an attached debugger. */
static
VOID
KiRiscvDescribeAddress(
    _In_ PVOID Address)
{
    PLIST_ENTRY Entry;
    ULONG_PTR Va = (ULONG_PTR)Address;
    const CHAR *Window;
    PFN_NUMBER Page;

    if (KeLoaderBlock == NULL)
        return;
    if ((Va >= RISCV64_LOADER_KSEG0_BASE) && (Va - RISCV64_LOADER_KSEG0_BASE < RISCV64_LOADER_PHYSICAL_LIMIT))
    {
        Window = "KSEG0";
        Page = (Va - RISCV64_LOADER_KSEG0_BASE) >> PAGE_SHIFT;
    }
    else if ((Va >= RISCV64_LOADER_DIRECT_MAP_BASE) &&
             (Va - RISCV64_LOADER_DIRECT_MAP_BASE < RISCV64_LOADER_PHYSICAL_LIMIT))
    {
        Window = "direct-map";
        Page = (Va - RISCV64_LOADER_DIRECT_MAP_BASE) >> PAGE_SHIFT;
    }
    else
    {
        return;
    }

    for (Entry = KeLoaderBlock->MemoryDescriptorListHead.Flink;
         Entry != &KeLoaderBlock->MemoryDescriptorListHead;
         Entry = Entry->Flink)
    {
        PMEMORY_ALLOCATION_DESCRIPTOR Md = CONTAINING_RECORD(Entry, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

        if ((Page >= Md->BasePage) && (Page < Md->BasePage + Md->PageCount))
        {
            DbgPrint("address %p is %s page %lx: loader type %lu (base %lx, %lx pages)\n",
                     Address, Window, Page, Md->MemoryType, Md->BasePage, Md->PageCount);
            return;
        }
    }
    DbgPrint("address %p is %s page %lx: no loader descriptor\n", Address, Window, Page);
}

/* Poor man's backtrace: stack words that fall inside a loaded image. */
static
VOID
KiRiscvScanStackForCode(
    _In_ PULONG_PTR Stack)
{
    ULONG Index, Printed = 0;
    MI_RISCV_PAGE_WALK Walk;

    if ((KeLoaderBlock == NULL) || ((ULONG_PTR)Stack & 7))
        return;
    for (Index = 0; (Index < 512) && (Printed < 24); ++Index)
    {
        ULONG_PTR Value;
        PLIST_ENTRY Entry;

        if (!NT_SUCCESS(MiRiscvWalkCurrentPageTables(&Stack[Index], &Walk)))
            break;
        Value = Stack[Index];
        for (Entry = KeLoaderBlock->LoadOrderListHead.Flink;
             Entry != &KeLoaderBlock->LoadOrderListHead;
             Entry = Entry->Flink)
        {
            PLDR_DATA_TABLE_ENTRY Ldr = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            ULONG_PTR Base = (ULONG_PTR)Ldr->DllBase;

            if ((Value > Base) && (Value < Base + Ldr->SizeOfImage))
            {
                DbgPrint("  [sp+%03lx] %p  %wZ+%lx\n", Index * sizeof(ULONG_PTR), (PVOID)Value, &Ldr->BaseDllName, (ULONG)(Value - Base));
                ++Printed;
                break;
            }
        }
    }
}

static
VOID
KiRiscvDumpTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG Index;

    DbgPrint("\n*** Unexpected supervisor trap: %s\n",
             KiRiscvDecodeScause(TrapFrame->Scause));
    DbgPrint("scause  %p  sepc    %p\nstval   %p  sstatus %p\n",
             (PVOID)(ULONG_PTR)TrapFrame->Scause,
             (PVOID)(ULONG_PTR)TrapFrame->Context.Pc,
             (PVOID)(ULONG_PTR)TrapFrame->Stval,
             (PVOID)(ULONG_PTR)TrapFrame->Sstatus);
    for (Index = 1; Index < 32; Index++)
    {
        DbgPrint("%-4s %p%s",
                 KiRiscvRegisterNames[Index],
                 (PVOID)(ULONG_PTR)TrapFrame->Context.X[Index],
                 ((Index & 3) == 0) || (Index == 31) ? "\n" : "  ");
    }
    DbgPrint("previous irql %u, trap frame %p\n",
             TrapFrame->PreviousIrql,
             TrapFrame);
    KiRiscvDescribeAddress((PVOID)(ULONG_PTR)TrapFrame->Stval);
    KiRiscvScanStackForCode((PULONG_PTR)(ULONG_PTR)TrapFrame->Context.Sp);
}

BOOLEAN
NTAPI
KiRiscvInitializeTrapVector(VOID)
{
    ULONG_PTR Status, Vector;

    __asm__ __volatile__("csrr %0, sstatus" : "=r"(Status) :: "memory");
    if ((Status & RISCV_SSTATUS_SIE) || (KeNumberProcessors != 1) ||
        (KeGetCurrentPrcb() != KiProcessorBlock[0]))
    {
        return FALSE;
    }

    /* Direct mode: every delegated supervisor trap enters the same prologue.
     * Entry owns valid executable mappings for this code and its PCR. */
    Vector = (ULONG_PTR)KiRiscvTrapEntry;
    ASSERT((Vector & 3) == 0);
    __asm__ __volatile__("csrw stvec, %0\n\tcsrr %0, stvec" : "+r"(Vector) :: "memory");
    return Vector == (ULONG_PTR)KiRiscvTrapEntry;
}

DECLSPEC_NORETURN
VOID
NTAPI
KiRiscvTrapStop(_In_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();

    TrapFrame->PreviousTrapFrame = Thread->TrapFrame;
    Thread->TrapFrame = TrapFrame;

    /* Non-returning design: describe the trap on the console before the
     * bugcheck path, which may not be able to print register detail. */
    KiRiscvDumpTrapFrame(TrapFrame);
    KeBugCheckEx(UNEXPECTED_KERNEL_MODE_TRAP,
                 TrapFrame->Scause,
                 TrapFrame->Context.Pc,
                 TrapFrame->Stval,
                 (ULONG_PTR)TrapFrame);
}

/* Single-hart loop detector for faults on leaves that already permit access. */
static ULONG64 KiRiscvLastSpuriousPc;
static ULONG_PTR KiRiscvLastSpuriousAddress;
static ULONG KiRiscvSpuriousCount;

/* Does a valid leaf grant this access to the trapping mode? Mirrors the
 * privileged-spec permission check (U/SUM/MXR, R/W/X) without A/D. */
static
BOOLEAN
KiRiscvLeafPermitsAccess(
    _In_ ULONG64 Leaf,
    _In_ ULONG64 Code,
    _In_ KPROCESSOR_MODE Mode,
    _In_ ULONG64 Sstatus)
{
    BOOLEAN UserPage = (Leaf & MI_RISCV_PTE_OWNER) != 0;

    if (Mode == UserMode)
    {
        if (!UserPage)
            return FALSE;
    }
    else if (UserPage)
    {
        if ((Code == 12) || !(Sstatus & RISCV_SSTATUS_SUM))
            return FALSE;
    }

    switch (Code)
    {
        case 12:
            return (Leaf & MI_RISCV_PTE_EXECUTE) != 0;
        case 13:
            return ((Leaf & MI_RISCV_PTE_READ) != 0) ||
                   (((Sstatus & RISCV_SSTATUS_MXR) != 0) && ((Leaf & MI_RISCV_PTE_EXECUTE) != 0));
        case 15:
            return (Leaf & MI_RISCV_PTE_WRITE) != 0;
        default:
            return FALSE;
    }
}

/* Resumable supervisor-trap dispatch. Interrupts (scause bit 63) go to
 * KiRiscvInterruptDispatch at their own IRQL; page faults go to MM; other
 * exceptions are dispatched as NT exceptions. A handled trap returns to the
 * entry code, which restores the frame and executes sret. */
VOID
NTAPI
KiRiscvTrapHandler(_Inout_ PKTRAP_FRAME TrapFrame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG64 Scause = TrapFrame->Scause;
    ULONG64 Code = Scause & ~(1ULL << 63);
    PVOID Address = (PVOID)(ULONG_PTR)TrapFrame->Stval;
    KPROCESSOR_MODE Mode = KiUserTrap(TrapFrame) ? UserMode : KernelMode;
    MI_RISCV_PAGE_WALK Walk;
    EXCEPTION_RECORD Record;
    ULONG FaultCode;
    NTSTATUS Status;

    TrapFrame->PreviousTrapFrame = Thread->TrapFrame;
    Thread->TrapFrame = TrapFrame;

    if ((Code == 8) && (Mode == UserMode) && !(Scause & (1ULL << 63)))
        KiRiscvSystemService(TrapFrame);

    if (Scause & (1ULL << 63))
    {
        /* A DPC-level switch may run other threads meanwhile; this thread
         * resumes here on its own stack with the same frame. */
        KiRiscvInterruptDispatch(TrapFrame);
        Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
        return;
    }

    if (!(Scause & (1ULL << 63)) && ((Code == 12) || (Code == 13) || (Code == 15)))
    {
        KIRQL Irql = KeGetCurrentIrql();

        /* Present means a valid leaf exists: a protection fault, not demand. */
        FaultCode = (Code == 15) ? MI_RISCV_FAULT_WRITE : (Code == 12) ? MI_RISCV_FAULT_EXECUTE : 0;
        if (NT_SUCCESS(MiRiscvWalkCurrentPageTables(Address, &Walk)))
        {
            FaultCode |= MI_RISCV_FAULT_PRESENT;

            if (KiRiscvLeafPermitsAccess(Walk.Value.u.Long, Code, Mode, TrapFrame->Sstatus))
            {
                ULONG64 Needed = MI_RISCV_PTE_ACCESSED | ((Code == 15) ? MI_RISCV_PTE_DIRTY : 0);
                ULONG64 Expected = Walk.Value.u.Long;

                if ((Expected & Needed) != Needed)
                {
                    /* Without Svadu the leaf only lacks A (or D for a store).
                     * MM records that itself, which it cannot do above
                     * APC_LEVEL; a page touched there is nonpageable. */
                    if (Irql > APC_LEVEL)
                    {
                        __atomic_compare_exchange_n(&Walk.Entry->u.Long, &Expected, Expected | Needed, FALSE,
                                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
                        KeInvalidateTlbEntry(Address);
                        Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
                        return;
                    }
                }
                else
                {
                    /* The leaf permits the access and A/D are set: the
                     * translation was stale. Retry, but never loop on a fault
                     * this model cannot explain. */
                    if ((KiRiscvLastSpuriousPc != TrapFrame->Context.Pc) ||
                        (KiRiscvLastSpuriousAddress != (ULONG_PTR)Address))
                    {
                        KiRiscvLastSpuriousPc = TrapFrame->Context.Pc;
                        KiRiscvLastSpuriousAddress = (ULONG_PTR)Address;
                        KiRiscvSpuriousCount = 0;
                    }
                    if (++KiRiscvSpuriousCount < 8)
                    {
                        /* A stale table entry is only dropped by a full fence. */
                        KeFlushCurrentTb();
                        Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
                        return;
                    }
                    DbgPrint("\n*** Repeated fault on permitted leaf %llx at %p (pc %p)\n",
                             Walk.Value.u.Long, Address, (PVOID)(ULONG_PTR)TrapFrame->Context.Pc);
                    KiRiscvTrapStop(TrapFrame);
                }
            }
        }

        /* A missing loader-window page is a loader or adoption bug, not demand
         * paging: attribute it before MM raises PAGE_FAULT_IN_NONPAGED_AREA. */
        if (((ULONG_PTR)Address >= RISCV64_LOADER_KSEG0_BASE) &&
            ((ULONG_PTR)Address - RISCV64_LOADER_KSEG0_BASE < 2 * RISCV64_LOADER_PHYSICAL_LIMIT))
        {
            DbgPrint("\n*** Loader-window fault at %p from %p: cause %lu, code %lx, leaf %llx\n", Address,
                     (PVOID)(ULONG_PTR)TrapFrame->Context.Pc, (ULONG)Code, FaultCode,
                     (FaultCode & MI_RISCV_FAULT_PRESENT) ? Walk.Value.u.Long : 0ULL);
            KiRiscvDescribeAddress(Address);
            KiRiscvScanStackForCode((PULONG_PTR)(ULONG_PTR)TrapFrame->Context.Sp);
        }

        /* The resident kernel metadata reader never resolves paging faults.
         * Recover before MM's fatal kernel-address path can recurse into SEH. */
        if (Mode == KernelMode && (ULONG_PTR)Address >= (ULONG_PTR)MmSystemRangeStart &&
            KiRiscvFixupUserCopy(TrapFrame, STATUS_ACCESS_VIOLATION))
        {
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            return;
        }
        Status = MmAccessFault(FaultCode, Address, Mode, TrapFrame);
        if (NT_SUCCESS(Status))
        {
            KeInvalidateTlbEntry(Address);
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            return;
        }
        if (Irql > APC_LEVEL)
        {
            KeBugCheckEx(IRQL_NOT_LESS_OR_EQUAL,
                         (ULONG_PTR)Address,
                         Irql,
                         (Code == 15) ? 1 : 0,
                         TrapFrame->Context.Pc);
        }
        if (KiRiscvFixupUserCopy(TrapFrame, Status))
        {
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            return;
        }
        DbgPrint("\n*** MmAccessFault(%lx, %p) failed: 0x%08lx at %p\n", FaultCode, Address, Status, (PVOID)(ULONG_PTR)TrapFrame->Context.Pc);
        KiRiscvDescribeAddress(Address);

        RtlZeroMemory(&Record, sizeof(Record));
        Record.ExceptionAddress = (PVOID)(ULONG_PTR)TrapFrame->Context.Pc;
        if ((Status == STATUS_ACCESS_VIOLATION) || (Status == STATUS_GUARD_PAGE_VIOLATION) || (Status == STATUS_STACK_OVERFLOW))
        {
            Record.ExceptionCode = Status;
            Record.NumberParameters = 2;
            Record.ExceptionInformation[0] = (Code == 15) ? 1 : (Code == 12) ? 8 : 0;
            Record.ExceptionInformation[1] = (ULONG_PTR)Address;
        }
        else
        {
            Record.ExceptionCode = STATUS_IN_PAGE_ERROR;
            Record.NumberParameters = 3;
            Record.ExceptionInformation[0] = (Code == 15) ? 1 : 0;
            Record.ExceptionInformation[1] = (ULONG_PTR)Address;
            Record.ExceptionInformation[2] = Status;
        }
        KiDispatchException(&Record, NULL, TrapFrame, Mode, TRUE);
        Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
        return;
    }

    if (!(Scause & (1ULL << 63)))
    {
        if (KiRiscvFixupUserCopy(TrapFrame, STATUS_ACCESS_VIOLATION))
        {
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            return;
        }
        RtlZeroMemory(&Record, sizeof(Record));
        Record.ExceptionAddress = (PVOID)(ULONG_PTR)TrapFrame->Context.Pc;
        switch (Code)
        {
            case 3:
                if (TrapFrame->Context.T0 == RISCV_FAST_FAIL_TRAP)
                {
                    Record.ExceptionCode = STATUS_STACK_BUFFER_OVERRUN;
                    Record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
                    Record.NumberParameters = 1;
                    Record.ExceptionInformation[0] = TrapFrame->Context.A0;
                    if (Mode == KernelMode)
                        KeBugCheckEx(KERNEL_SECURITY_CHECK_FAILURE,
                                     TrapFrame->Context.A0, (ULONG_PTR)TrapFrame,
                                     (ULONG_PTR)&Record, 0);
                    /* Fast fail bypasses application SEH and VEH. Only the
                     * second-chance debugger may intervene before exit. */
                    KiDispatchException(&Record, NULL, TrapFrame, UserMode, FALSE);
                    Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
                    return;
                }
                Record.ExceptionCode = STATUS_BREAKPOINT;
                Record.NumberParameters = 1;
                Record.ExceptionInformation[0] = 0;
                break;
            case 2:
                Record.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
                break;
            case 0:
            case 4:
            case 6:
                Record.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
                break;
            case 1:
            case 5:
            case 7:
                Record.ExceptionCode = STATUS_ACCESS_VIOLATION;
                Record.NumberParameters = 2;
                Record.ExceptionInformation[0] = (Code == 7) ? 1 : (Code == 1) ? 8 : 0;
                Record.ExceptionInformation[1] = (ULONG_PTR)Address;
                break;
            default:
                Record.ExceptionCode = 0;
                break;
        }
        if (Record.ExceptionCode != 0)
        {
            KiDispatchException(&Record, NULL, TrapFrame, Mode, TRUE);
            Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
            return;
        }
    }

    Thread->TrapFrame = TrapFrame->PreviousTrapFrame;
    KiRiscvTrapStop(TrapFrame);
}

ULONG_PTR
NTAPI
KeGetTrapFramePc(_In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Pc;
}

BOOLEAN
NTAPI
KeGetTrapFrameInterruptState(_In_ PKTRAP_FRAME TrapFrame)
{
    /* Hardware saved the previous SIE into SPIE on trap entry. */
    return (TrapFrame->Sstatus & RISCV_SSTATUS_SPIE) != 0;
}

BOOLEAN
NTAPI
KiUserTrap(_In_ PKTRAP_FRAME TrapFrame)
{
    return (TrapFrame->Sstatus & RISCV_SSTATUS_SPP) == 0;
}

PKTRAP_FRAME
NTAPI
KeGetTrapFrame(_In_ PKTHREAD Thread)
{
    return Thread->TrapFrame;
}

PKEXCEPTION_FRAME
NTAPI
KeGetExceptionFrame(_In_ PKTHREAD Thread)
{
    UNREFERENCED_PARAMETER(Thread);
    /* All base registers belong to KTRAP_FRAME.Context. There is no second
     * nonvolatile bank to locate by subtracting a foreign frame length. */
    return NULL;
}

static BOOLEAN
KiRiscvDeliverUserException(PKTRAP_FRAME TrapFrame, PCONTEXT Context,
                            PEXCEPTION_RECORD Record)
{
    KUSER_EXCEPTION_STACK UserFrame;
    ULONG_PTR Stack, Entry = (ULONG_PTR)KeUserExceptionDispatcher;
    NTSTATUS Status;
    if (!Entry || (Entry & 1) || Entry >= MmUserProbeAddress ||
        Context->Sp < MM_ALLOCATION_GRANULARITY + sizeof(UserFrame) ||
        Context->Sp >= MmUserProbeAddress || (Context->Sp & 15) ||
        Record->NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS)
        return FALSE;
    Stack = Context->Sp - sizeof(UserFrame);
    RtlZeroMemory(&UserFrame, sizeof(UserFrame));
    UserFrame.Context = *Context;
    UserFrame.ExceptionRecord = *Record;
    Status = KiRiscvCopyToUser((PVOID)Stack, &UserFrame, sizeof(UserFrame));
    if (!NT_SUCCESS(Status)) return FALSE;
    TrapFrame->Context.Pc = Entry;
    TrapFrame->Context.Sp = Stack;
    TrapFrame->Context.A0 = Stack + FIELD_OFFSET(KUSER_EXCEPTION_STACK, ExceptionRecord);
    TrapFrame->Context.A1 = Stack;
    TrapFrame->Context.Ra = 0;
    return TRUE;
}

VOID
KiDispatchException(
    _In_ PEXCEPTION_RECORD ExceptionRecord,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ BOOLEAN FirstChance)
{
    CONTEXT Context;

    KeGetCurrentPrcb()->KeExceptionDispatchCount++;
    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_FULL |
        (TrapFrame->Context.ContextFlags & CONTEXT_UNWOUND_TO_CALL);
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);

    if (PreviousMode != KernelMode)
    {
        if (FirstChance)
        {
            if ((!PsGetCurrentProcess()->DebugPort && !KdIgnoreUmExceptions) ||
                KdIsThisAKdTrap(ExceptionRecord, &Context, PreviousMode))
            {
                if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord,
                                   &Context, PreviousMode, FALSE))
                    goto Handled;
            }
            if (DbgkForwardException(ExceptionRecord, TRUE, FALSE)) return;
            if (KiRiscvDeliverUserException(TrapFrame, &Context, ExceptionRecord)) return;
        }
        if (DbgkForwardException(ExceptionRecord, TRUE, TRUE) ||
            DbgkForwardException(ExceptionRecord, FALSE, TRUE)) return;
        DPRINT1("RISC-V64 terminating %.16s: exception %lx at %p\n",
                PsGetCurrentProcess()->ImageFileName, ExceptionRecord->ExceptionCode,
                ExceptionRecord->ExceptionAddress);
        ZwTerminateProcess(NtCurrentProcess(), ExceptionRecord->ExceptionCode);
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                     ExceptionRecord->ExceptionCode,
                     (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                     (ULONG_PTR)TrapFrame,
                     PreviousMode);
    }

    if (FirstChance)
    {
        if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, &Context, PreviousMode, FALSE))
            goto Handled;
        if (RtlDispatchException(ExceptionRecord, &Context)) goto Handled;
    }
    if (KiDebugRoutine(TrapFrame, ExceptionFrame, ExceptionRecord, &Context, PreviousMode, TRUE))
        goto Handled;

#ifdef KDBG
    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        EXCEPTION_RECORD64 Record64;
        ULONG Index;

        RtlZeroMemory(&Record64, sizeof(Record64));
        Record64.ExceptionCode = ExceptionRecord->ExceptionCode;
        Record64.ExceptionFlags = ExceptionRecord->ExceptionFlags;
        Record64.ExceptionAddress = (ULONG64)(ULONG_PTR)ExceptionRecord->ExceptionAddress;
        Record64.NumberParameters = min(ExceptionRecord->NumberParameters, EXCEPTION_MAXIMUM_PARAMETERS);
        for (Index = 0; Index < Record64.NumberParameters; Index++)
            Record64.ExceptionInformation[Index] = ExceptionRecord->ExceptionInformation[Index];
        if (KdbEnterDebuggerException(&Record64, PreviousMode, &Context, FALSE) == kdContinue)
            goto Handled;
    }
#endif

    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                 ExceptionRecord->ExceptionCode,
                 (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                 (ULONG_PTR)TrapFrame,
                 0);

Handled:
    KeContextToTrapFrame(&Context, ExceptionFrame, TrapFrame, Context.ContextFlags, PreviousMode);
}
