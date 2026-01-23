/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/trapc.c
 * PURPOSE:         Trap handling stubs for ARM64
 */

#include <ntoskrnl.h>
#include <arm64trap.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>
#ifdef KDBG
#include <kdbg/kdb.h>
#endif

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;
extern BOOLEAN KdPitchDebugger;

static
BOOLEAN
KiArm64FixupUserAccessFlagFault(
    _In_ PVOID FaultAddress,
    _In_ ULONG FaultStatus)
{
    ULONG64 Ttbr0, RootPa;
    volatile ULONG64 *L0Table, *L1Table, *L2Table, *L3Table;
    ULONG L0Idx, L1Idx, L2Idx, L3Idx;
    ULONG64 L0Entry, L1Entry, L2Entry, L3Entry;

    /* Access Flag fault codes: 0x9/0xA/0xB for levels 1/2/3 */
    if ((FaultStatus != 0x9) && (FaultStatus != 0xA) && (FaultStatus != 0xB))
        return FALSE;

    if ((ULONG_PTR)FaultAddress >= (ULONG_PTR)MmSystemRangeStart)
        return FALSE;

    __asm__ __volatile__("mrs %0, ttbr0_el1" : "=r"(Ttbr0));
    RootPa = Ttbr0 & 0x0000FFFFFFFFF000ULL;
    if (RootPa == 0)
        return FALSE;

    L0Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 39) & 0x1FF;
    L1Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 30) & 0x1FF;
    L2Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 21) & 0x1FF;
    L3Idx = ((ULONG64)(ULONG_PTR)FaultAddress >> 12) & 0x1FF;

    L0Table = (volatile ULONG64 *)(KSEG0_BASE | RootPa);
    L0Entry = L0Table[L0Idx];
    if ((L0Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    L1Table = (volatile ULONG64 *)(KSEG0_BASE | (L0Entry & 0x0000FFFFFFFFF000ULL));
    L1Entry = L1Table[L1Idx];
    if ((L1Entry & 0x1ULL) == 0)
        return FALSE;
    if ((FaultStatus == 0x9) && ((L1Entry & 0x3ULL) == 0x1ULL))
    {
        if ((L1Entry & (1ULL << 10)) == 0)
            L1Table[L1Idx] = (L1Entry | (1ULL << 10));
        goto TlbFlush;
    }
    if ((L1Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    L2Table = (volatile ULONG64 *)(KSEG0_BASE | (L1Entry & 0x0000FFFFFFFFF000ULL));
    L2Entry = L2Table[L2Idx];
    if ((L2Entry & 0x1ULL) == 0)
        return FALSE;
    if ((FaultStatus == 0xA) && ((L2Entry & 0x3ULL) == 0x1ULL))
    {
        if ((L2Entry & (1ULL << 10)) == 0)
            L2Table[L2Idx] = (L2Entry | (1ULL << 10));
        goto TlbFlush;
    }
    if ((L2Entry & 0x3ULL) != 0x3ULL)
        return FALSE;

    L3Table = (volatile ULONG64 *)(KSEG0_BASE | (L2Entry & 0x0000FFFFFFFFF000ULL));
    L3Entry = L3Table[L3Idx];
    if ((FaultStatus == 0xB) && ((L3Entry & 0x3ULL) == 0x3ULL))
    {
        if ((L3Entry & (1ULL << 10)) == 0)
            L3Table[L3Idx] = (L3Entry | (1ULL << 10));
        goto TlbFlush;
    }

    return FALSE;

TlbFlush:
    __asm__ __volatile__("dsb ishst" ::: "memory");
    __asm__ __volatile__("tlbi vaae1is, %0" :: "r"((ULONG_PTR)FaultAddress >> PAGE_SHIFT) : "memory");
    __asm__ __volatile__("dsb ish" ::: "memory");
    __asm__ __volatile__("isb" ::: "memory");
    return TRUE;
}

static __inline VOID
KiArm64StageLogf(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[192];
    va_list Args;

    va_start(Args, Format);
    if (NT_SUCCESS(RtlStringCbVPrintfA(Buffer, sizeof(Buffer), Format, Args)))
    {
        DPRINT1("%s\n", Buffer);
    }
    va_end(Args);
}

#ifndef KI_ARM64_STAGE_LOGF
#define KI_ARM64_STAGE_LOGF(...) KiArm64StageLogf(__VA_ARGS__)
#endif

NTSTATUS
NTAPI
MmArmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation);

/*
 * MmAccessFault: The dispatch function that routes page faults to the
 * appropriate handler (MmArmAccessFault for ARM3 allocations, or
 * MmNotPresentFaultSectionView for ROS section views like VACB buffers).
 *
 * ARM64 MUST call this instead of MmArmAccessFault to properly handle
 * kernel section views created by the ReactOS memory manager.
 */
NTSTATUS
NTAPI
MmAccessFault(
    _In_ ULONG FaultCode,
    _In_ PVOID Address,
    _In_ KPROCESSOR_MODE Mode,
    _In_ PVOID TrapInformation);

VOID
KiSystemService(
    _Inout_ PKTHREAD Thread,
    _Inout_ PKTRAP_FRAME TrapFrame,
    _In_ ULONG Instruction);

/*
 * MiArm64UpdateTtbr0AliasAndHyperspace - Update TTBR0 alias and hyperspace in TTBR1
 *
 * !!! WARNING: !SMP_SAFE !!!
 *
 * This function is ARCHITECTURALLY UNSAFE for SMP systems because:
 *
 * 1. TTBR1 (Kernel Page Table Root) is SHARED across all processors.
 * 2. L0[494] is a single entry in TTBR1's hierarchy - updating it affects ALL CPUs.
 * 3. If CPU 0 switches to Process A and updates L0[494] to point to A's TTBR0,
 *    CPU 1 (which may be running Process B) now sees Process A's user page tables
 *    through its self-map, NOT Process B's tables.
 *
 * For Phase 1 (Single Core bring-up), this is acceptable.
 * For SMP support, each CPU would need its own TTBR0 alias index, or the
 * alias mechanism must be redesigned (e.g., per-CPU page table overlays).
 *
 * ARM64 Hyperspace Architecture:
 *
 * HYPER_SPACE (0xFFFFF60000000000) is at L0[492] in TTBR1. The self-map structure
 * requires that the L0 entries used for the self-map hierarchy remain consistent.
 * Modifying L0[492] breaks MiAddressToPte() for hyperspace addresses because the
 * self-map recursion (L0[493] -> L0[492]) expects L0[492] to contain the original
 * kernel hyperspace mapping with proper page table level chaining.
 *
 * Solution for per-process hyperspace on ARM64:
 * Instead of modifying TTBR1's L0[492] (which breaks the self-map), we update
 * ONLY the leaf-level entries (L3 PTEs) within the existing hyperspace structure.
 * This approach:
 * 1. Keeps L0[492] pointing to the kernel's hyperspace L1 (preserves self-map)
 * 2. Updates the working set list PTE in the hyperspace L3 table to point to
 *    the current process's working set page
 *
 * For now, we only update the TTBR0 alias (L0[494]) here. The hyperspace
 * per-process handling is done in MmInitializeProcessAddressSpace.
 *
 * Parameters:
 *   Ttbr0Pa   - Physical address of the new TTBR0 L0 page table
 *   HyperPa   - Physical address of the new hyperspace PDPT (unused for now)
 */
static __inline VOID
MiArm64UpdateTtbr0AliasAndHyperspace(ULONG64 Ttbr0Pa, ULONG64 HyperPa)
{
    UINT64 Ttbr1;
    UINT64 Ttbr1Pa;
    volatile UINT64 *L0Table;
    BOOLEAN NeedFlush = FALSE;

    (void)HyperPa;  /* Reserved for future use */

    /* ARM64 PTE constants */
    #define ARM64_TTBR0_ALIAS_INDEX 494
    #define ARM64_PTE_TABLE_VALID   0x3ULL
    #define ARM64_PTE_ADDR_MASK_    0x0000FFFFFFFFF000ULL

    /* Read TTBR1 to get the kernel page table root */
    __asm__ __volatile__("mrs %0, ttbr1_el1" : "=r"(Ttbr1));
    Ttbr1Pa = Ttbr1 & ARM64_PTE_ADDR_MASK_;

    /* Access L0 table via KSEG0 direct mapping (0xFFFF800000000000 + PA) */
    L0Table = (volatile UINT64 *)(0xFFFF800000000000ULL | Ttbr1Pa);

    /*
     * Update L0[494] to point to TTBR0's L0 page (TTBR0 alias for user PTEs).
     */
    {
        UINT64 OldEntry = L0Table[ARM64_TTBR0_ALIAS_INDEX];
        UINT64 NewEntry = (Ttbr0Pa & ARM64_PTE_ADDR_MASK_) | ARM64_PTE_TABLE_VALID;

        if (OldEntry != NewEntry)
        {
            L0Table[ARM64_TTBR0_ALIAS_INDEX] = NewEntry;
            NeedFlush = TRUE;
        }
    }

    /*
     * ARM64 Hyperspace Architecture:
     *
     * HYPER_SPACE (0xFFFFF60000000000) at L0[492] in TTBR1 is SHARED across
     * all processes. DO NOT modify TTBR1's L0[492] per context switch!
     *
     * Modifying TTBR1's L0 entries per context switch is architecturally unsafe:
     * 1. TTBR1 is the shared kernel translation regime
     * 2. Interrupts/deferred work running concurrently see a moving kernel VA->PA view
     * 3. Self-map recursion assumptions become unstable
     * 4. On SMP this becomes a correctness disaster
     *
     * For per-process hyperspace behavior, use the "leaf-only" approach:
     * - Keep TTBR1's L0[492] pointing to the kernel's hyperspace L1 (stable)
     * - Update only leaf-level PTEs (L3) within the hyperspace mapping to point
     *   to per-process pages (working set list, etc.)
     *
     * This is handled in MmInitializeProcessAddressSpace, not during context switch.
     */

    if (NeedFlush)
    {
        __asm__ __volatile__(
            "dsb ishst\n\t"
            "tlbi vmalle1is\n\t"
            "dsb ish\n\t"
            "isb"
            ::: "memory"
        );
    }

    #undef ARM64_TTBR0_ALIAS_INDEX
    #undef ARM64_PTE_TABLE_VALID
    #undef ARM64_PTE_ADDR_MASK_
}

VOID
NTAPI
KiSwapProcess(_Inout_ PKPROCESS NewProcess,
              _Inout_ PKPROCESS OldProcess)
{
    ASSERT(NewProcess != NULL);

    /* Use UART output to avoid DPRINT1 hang during TTBR0-switched state */
#ifdef CONFIG_SMP
    {
        PKIPCR Pcr = KeGetPcr();
        if (Pcr != NULL)
        {
            KAFFINITY Member = Pcr->Prcb.SetMember;

            NewProcess->ActiveProcessors ^= Member;
            if (OldProcess != NULL)
            {
                OldProcess->ActiveProcessors ^= Member;
            }
        }
    }
#endif

    if (OldProcess == NewProcess)
    {
        /* Same process, nothing to do */
        return;
    }

    if ((OldProcess != NULL) &&
        (NewProcess->DirectoryTableBase[0] == OldProcess->DirectoryTableBase[0]))
    {
        /* Same DTB, nothing to do */
        return;
    }

    ASSERT(NewProcess->DirectoryTableBase[0] != 0);

    /*
     * ARM64 TTBR0 Alias Update
     *
     * Before switching TTBR0, update L0[494] (TTBR0 alias) so the kernel's
     * self-map can access the new process's user page tables.
     *
     * Note: L0[492] (hyperspace) is NOT modified here. TTBR1's hyperspace
     * region is shared and stable. Per-process hyperspace data (working set
     * list, etc.) is handled via leaf-level PTE updates, not L0 entry changes.
     */
    MiArm64UpdateTtbr0AliasAndHyperspace(NewProcess->DirectoryTableBase[0],
                                          NewProcess->DirectoryTableBase[1]);

    /*
     * ARM64 TTBR0 Switch (Bring-up mode)
     *
     * ASID Note: We mask to page alignment, setting ASID=0 intentionally.
     * This is acceptable for single-core bring-up because we flush the entire
     * TLB (vmalle1is) on every context switch. For SMP/performance, must:
     * - Preserve ASID bits from DirectoryTableBase
     * - Use targeted TLBI (aside1is) instead of full flush
     * - Implement proper ASID allocation
     */
    {
        ULONGLONG MaskedBase = NewProcess->DirectoryTableBase[0] & ~((ULONGLONG)PAGE_SIZE - 1ULL);

        __asm__ __volatile__("dsb ishst" ::: "memory");
        __asm__ __volatile__("msr ttbr0_el1, %0" :: "r"(MaskedBase) : "memory");
        __asm__ __volatile__("isb" ::: "memory");
        __asm__ __volatile__("tlbi vmalle1is" ::: "memory");
        __asm__ __volatile__("dsb ish" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");
    }
}

typedef struct _ARM64_EARLY_SYNC_CONTEXT
{
    ARM64_EARLY_TRAP_STATE State;
    PKTRAP_FRAME TrapFramePointer;
    PKEXCEPTION_FRAME ExceptionFramePointer;
    KTRAP_FRAME TrapFrame;
    KEXCEPTION_FRAME ExceptionFrame;
} ARM64_EARLY_SYNC_CONTEXT, *PARM64_EARLY_SYNC_CONTEXT;

C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.VectorId) == 0x0);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.ExceptionSyndrome) == 0x8);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.FaultAddress) == 0x10);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Elr) == 0x18);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Spsr) == 0x20);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.X[0]) == 0x28);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Sp) == 0x120);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Pc) == 0x128);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, State.Registers.Pstate) == 0x130);
C_ASSERT(sizeof(ARM64_EARLY_TRAP_STATE) == 0x138);  /* State should end at 0x138 */
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, TrapFramePointer) == 0x138);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, ExceptionFramePointer) == 0x140);
C_ASSERT(FIELD_OFFSET(ARM64_EARLY_SYNC_CONTEXT, TrapFrame) == 0x148);
#define ARM64_EARLY_SYNC_CONTEXT_ALLOC_SIZE 0x380
C_ASSERT(sizeof(ARM64_EARLY_SYNC_CONTEXT) <= ARM64_EARLY_SYNC_CONTEXT_ALLOC_SIZE);

/*
 * KiArm64PreviousModeFromContext - Determine the true previous mode
 *
 * On ARM64, when kernel code dereferences a NULL pointer (e.g., accessing
 * offset 4 from NULL), the FAR contains a low user address (0x4), but the
 * ELR contains the kernel address where the fault occurred.
 *
 * SPSR.M[3:0] should indicate EL1 for kernel faults, but in some scenarios
 * (possibly related to exception nesting or SPSR caching), it may be 0 (EL0).
 *
 * To correctly identify kernel faults, we check BOTH:
 * 1. SPSR.M[3:0] - the processor mode bits
 * 2. ELR - the exception link register (faulting instruction address)
 *
 * If ELR is in kernel space (>= MmSystemRangeStart), the fault came from
 * kernel code, regardless of what SPSR says. This ensures NULL pointer
 * dereferences in kernel code are properly treated as kernel faults.
 */
static
KPROCESSOR_MODE
KiArm64PreviousModeFromContext(
    _In_ ULONG64 SpsrValue,
    _In_ ULONG64 ElrValue)
{
    ULONG Mode = (ULONG)(SpsrValue & 0xFULL);

    /*
     * If SPSR.M indicates EL1 (non-zero), it's definitely a kernel fault.
     */
    if (Mode != 0)
    {
        return KernelMode;
    }

    /*
     * SPSR.M is 0 (EL0), but check ELR to catch kernel NULL pointer dereferences.
     * If ELR is in kernel space, the faulting instruction was in the kernel,
     * so this is a kernel fault even though FAR may be a user address.
     *
     * Use hardcoded kernel base (0xFFFF800000000000) instead of MmSystemRangeStart
     * because MmSystemRangeStart may not be initialized during early boot.
     */
#define ARM64_KERNEL_BASE 0xFFFF800000000000ULL
    if (ElrValue >= ARM64_KERNEL_BASE)
    {
        return KernelMode;
    }
#undef ARM64_KERNEL_BASE

    /*
     * Both SPSR.M is 0 (EL0) and ELR is in user space - this is a true user fault.
     */
    return UserMode;
}

static
VOID
KiArm64InitializeTrapFrame(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context,
    _Out_ PKTRAP_FRAME TrapFrame)
{
    ULONG64 Fpcr = 0;
    ULONG64 Fpsr = 0;
    PKEXCEPTION_FRAME ExceptionFrame = &Context->ExceptionFrame;
    KIRQL CurrentIrql;

    RtlZeroMemory(TrapFrame, sizeof(*TrapFrame));

    /*
     * Capture IRQL BEFORE any potential IRQL changes during exception handling.
     * For synchronous exceptions (data/instruction abort), we're at the IRQL
     * that was active when the fault occurred. This is critical for Windows
     * ARM64 compliance - the trap frame must preserve the interrupted IRQL.
     */
    CurrentIrql = KeGetCurrentIrql();

    TrapFrame->PreviousMode = (CHAR)KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);
    TrapFrame->PreviousIrql = (UCHAR)CurrentIrql;
    TrapFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    TrapFrame->FaultAddress = Context->State.FaultAddress;
    TrapFrame->Spsr = (ULONG)Context->State.Spsr;
    TrapFrame->Esr = (ULONG)Context->State.ExceptionSyndrome;
    TrapFrame->Sp = Context->State.Registers.Sp;
    TrapFrame->Pc = Context->State.Elr;
    TrapFrame->Lr = Context->State.Registers.X[30];
    TrapFrame->Fp = Context->State.Registers.X[29];

    RtlCopyMemory(TrapFrame->X,
                  Context->State.Registers.X,
                  sizeof(TrapFrame->X));

    RtlZeroMemory(ExceptionFrame, sizeof(*ExceptionFrame));
    ExceptionFrame->TrapFrame = (ULONG64)(ULONG_PTR)TrapFrame;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(Fpcr));
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(Fpsr));
    ExceptionFrame->Fpcr = Fpcr;
    ExceptionFrame->Fpsr = Fpsr;
    ExceptionFrame->X19 = Context->State.Registers.X[19];
    ExceptionFrame->X20 = Context->State.Registers.X[20];
    ExceptionFrame->X21 = Context->State.Registers.X[21];
    ExceptionFrame->X22 = Context->State.Registers.X[22];
    ExceptionFrame->X23 = Context->State.Registers.X[23];
    ExceptionFrame->X24 = Context->State.Registers.X[24];
    ExceptionFrame->X25 = Context->State.Registers.X[25];
    ExceptionFrame->X26 = Context->State.Registers.X[26];
    ExceptionFrame->X27 = Context->State.Registers.X[27];
    ExceptionFrame->X28 = Context->State.Registers.X[28];
    ExceptionFrame->Fp = Context->State.Registers.X[29];
    ExceptionFrame->Lr = Context->State.Registers.X[30];
    Context->ExceptionFramePointer = ExceptionFrame;
}

static LONG KiArm64SyncExceptionLogBudget = 128;
static volatile LONG KiArm64DataAbortOwner[MAXIMUM_PROCESSORS];
/*
 * One-shot trap guard per CPU to prevent recursive exception storms before
 * the debugger is fully operational. Set on first entry; any re-entry while
 * set triggers an immediate bugcheck with the captured context.
 */
static volatile LONG KiArm64TrapActive[MAXIMUM_PROCESSORS] = {0};

static
VOID
KiArm64ReleaseWorkingSetsForBugCheck(VOID)
{
    PETHREAD Thread = PsGetCurrentThread();
    PEPROCESS Process = PsGetCurrentProcess();
    BOOLEAN Raised = FALSE;
    KIRQL PreviousIrql = KeGetCurrentIrql();

    if (PreviousIrql < APC_LEVEL)
    {
        KeRaiseIrql(APC_LEVEL, &PreviousIrql);
        Raised = TRUE;
    }

    if (Thread->OwnsSystemWorkingSetExclusive || Thread->OwnsSystemWorkingSetShared)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "[arm64] KiArm64ReleaseWorkingSets: releasing system WS=%p thread=%p mutex=%p count=0x%llx\n",
                   &MmSystemCacheWs,
                   Thread,
                   &MmSystemCacheWs.WorkingSetMutex,
                   (unsigned long long)MmSystemCacheWs.WorkingSetMutex.Value);
        MiUnlockWorkingSet(Thread, &MmSystemCacheWs);
    }

    if ((Thread->OwnsSessionWorkingSetExclusive || Thread->OwnsSessionWorkingSetShared) &&
        (MmSessionSpace != NULL))
    {
        MiUnlockWorkingSet(Thread, &MmSessionSpace->GlobalVirtualAddress->Vm);
    }

    if (Thread->OwnsProcessWorkingSetExclusive || Thread->OwnsProcessWorkingSetShared)
    {
        if (Process != NULL)
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_TRACE_LEVEL,
                       "[arm64] KiArm64ReleaseWorkingSets: releasing process WS thread=%p process=%p vm=%p mutex=%p count=0x%llx\n",
                       Thread,
                       Process,
                       &Process->Vm,
                       &Process->Vm.WorkingSetMutex,
                       (unsigned long long)Process->Vm.WorkingSetMutex.Value);
            MiUnlockProcessWorkingSetUnsafe(Process, Thread);
        }
    }

    if (Raised)
    {
        KeLowerIrql(PreviousIrql);
    }
}

static
VOID
KiArm64ResetDataAbortGuard(VOID)
{
    ULONG ProcessorIndex = KeGetCurrentProcessorNumber();

    if (ProcessorIndex < MAXIMUM_PROCESSORS)
    {
        InterlockedExchange(&KiArm64DataAbortOwner[ProcessorIndex], 0);
    }
}

static __inline VOID
KiArm64ClearTrapActive(VOID)
{
    ULONG ProcessorIndex = KeGetCurrentProcessorNumber();

    if (ProcessorIndex < MAXIMUM_PROCESSORS)
    {
        InterlockedExchange(&KiArm64TrapActive[ProcessorIndex], 0);
    }
}

#define KI_ARM64_ACCESS_READ    0
#define KI_ARM64_ACCESS_WRITE   1
#define KI_ARM64_ACCESS_EXECUTE 8

static __inline ULONG_PTR
KiArm64AccessTypeToExceptionInfo(
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN InstructionFetch)
{
    if (InstructionFetch)
    {
        return KI_ARM64_ACCESS_EXECUTE;
    }

    return WriteAccess ? KI_ARM64_ACCESS_WRITE : KI_ARM64_ACCESS_READ;
}

static
ULONG
KiArm64BuildFaultCode(
    _In_ ULONG FaultStatus,
    _In_ BOOLEAN WriteAccess,
    _In_ BOOLEAN InstructionFetch,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    ULONG Code = 0;

    /* Prevent unused warnings for bring-up-only debug guards on GCC/MinGW. */
    if (0)
    {
        (void)KiArm64SyncExceptionLogBudget;
        KiArm64ReleaseWorkingSetsForBugCheck();
    }

    switch (FaultStatus & 0x3FULL)
    {
        case 0x00: /* Address size fault level 0 */
        case 0x01: /* Address size fault level 1 */
        case 0x02: /* Address size fault level 2 */
        case 0x03: /* Address size fault level 3 */
        case 0x04: /* Translation fault level 0 */
        case 0x05: /* Translation fault level 1 */
        case 0x06: /* Translation fault level 2 */
        case 0x07: /* Translation fault level 3 */
            /* Treat as not-present fault (bit 0 cleared) */
            break;

        default:
            Code |= 0x1; /* Present */
            break;
    }

    if (WriteAccess)
    {
        Code |= 0x2;
    }

    if (InstructionFetch)
    {
        Code |= 0x20;
    }

    if (PreviousMode == UserMode)
    {
        Code |= 0x4;
    }

    return Code;
}

static
VOID
KiArm64ReportUnhandledSyncException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context,
    _In_ ULONG Esr)
{
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    ULONG Iss = Esr & 0x01FFFFFFUL;

    /*
     * Log unhandled exception details for debugging.
     * ESR=0 typically means this is not a real synchronous exception or
     * the CPU/emulator doesn't properly set ESR for this exception type.
     *
     * Common ESR classes:
     *   0x00: Unknown/uncategorized
     *   0x07: SVE/SIMD/FP trap (CPACR)
     *   0x0E: Illegal execution state
     *   0x18: MSR/MRS trap
     *   0x20-21: Instruction abort
     *   0x24-25: Data abort
     *   0x2F: SError
     *   0x3C: BRK
     */
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[arm64] UNHANDLED SYNC EXCEPTION: ESR=0x%08lx (Class=0x%02lx ISS=0x%06lx)\n",
               Esr, EsrClass, Iss);
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
               "[arm64]   ELR=%p FAR=%p VectorID=%lu\n",
               (PVOID)(ULONG_PTR)Context->State.Elr,
               (PVOID)(ULONG_PTR)Context->State.FaultAddress,
               (ULONG)Context->State.VectorId);
}

static volatile LONG KiArm64FirstCrashPrinted;

DECLSPEC_NORETURN
VOID
KiArm64BugCheckSynchronousException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context)
{
    /*
     * Even if ELR is in low VA (e.g. NULL), proceed to a controlled
     * bugcheck with a reconstructed trap frame. Spinning here hides the
     * original fault and trips the watchdog.
     */
    KTRAP_FRAME TrapFrame;
    ULONG Esr = (ULONG)(Context->State.ExceptionSyndrome & 0xFFFFFFFFULL);
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    BOOLEAN WriteAccess = (Esr & (1u << 6)) != 0;

    /* Reconstruct a trap frame so the bugcheck dump has architectural state. */
    KiArm64InitializeTrapFrame(Context, &TrapFrame);

    /*
     * Enable KD so "*** Fatal System Error" prints via DbgPrint in KeBugCheckWithTf.
     * Set KdPitchDebugger to prevent KD re-initialization attempts that could
     * cause faults during the bugcheck path. This ensures we get crash output
     * without risking infinite fault loops from KdEnableDebuggerWithLock.
     */
    KdDebuggerEnabled = TRUE;
    KdDebuggerNotPresent = TRUE;  /* No interactive debugger attached */
    KdPitchDebugger = TRUE;       /* Prevent KD re-init which can fault */

    /* Avoid touching working-set structures or pool during a hard stop. */
    KiArm64ResetDataAbortGuard();

    /* Print first crash info */
    if (InterlockedCompareExchange(&KiArm64FirstCrashPrinted, 1, 0) == 0)
    {
        KI_ARM64_STAGE_LOGF("[arm64] FirstCrash: vec=%lu esr=0x%lx elr=%p far=%p",
                           (ULONG)Context->State.VectorId,
                           (ULONG)Context->State.ExceptionSyndrome,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress);
    }

#ifdef KDBG
    /*
     * Call KDBG to display crash diagnostics before bugcheck.
     * This is the last safe point to show crash info.
     */
    {
        EXCEPTION_RECORD64 ExceptionRecord64;
        CONTEXT KdbContext;

        RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
        /* Determine exception code based on ESR class */
        if (EsrClass == 0x24 || EsrClass == 0x25)
        {
            ExceptionRecord64.ExceptionCode = STATUS_ACCESS_VIOLATION;
            ExceptionRecord64.NumberParameters = 2;
            ExceptionRecord64.ExceptionInformation[0] = WriteAccess ? 1 : 0;
            ExceptionRecord64.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;
        }
        else
        {
            ExceptionRecord64.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            ExceptionRecord64.NumberParameters = 0;
        }
        ExceptionRecord64.ExceptionFlags = 0;
        ExceptionRecord64.ExceptionAddress = Context->State.Elr;

        RtlZeroMemory(&KdbContext, sizeof(KdbContext));
        KdbContext.ContextFlags = CONTEXT_FULL | CONTEXT_ARM64;
        KeTrapFrameToContext(&TrapFrame, NULL, &KdbContext);

        KdbEnterDebuggerException(&ExceptionRecord64, KernelMode, &KdbContext, FALSE);
    }
#endif /* KDBG */

    KeBugCheckWithTf(TRAP_CAUSE_UNKNOWN,
                     (ULONG_PTR)Context->State.VectorId,
                     (ULONG_PTR)Context->State.ExceptionSyndrome,
                     (ULONG_PTR)Context->State.FaultAddress,
                     (ULONG_PTR)Context->State.Elr,
                     &TrapFrame);

    /* ARM64: __builtin_unreachable() generates trap instruction, avoid it */
    while (1) { }
}


BOOLEAN
KiArm64HandleSynchronousException(
    _Inout_ PARM64_EARLY_SYNC_CONTEXT Context)
{
    ULONG Esr = (ULONG)(Context->State.ExceptionSyndrome & 0xFFFFFFFFULL);
    ULONG EsrClass = (Esr >> 26) & 0x3FULL;
    ULONG Iss = Esr & 0x01FFFFFFUL;
    ULONG FaultStatus = Iss & 0x3FULL;
    PKTRAP_FRAME TrapFrame;
    KPROCESSOR_MODE PreviousMode;
    NTSTATUS Status;
    BOOLEAN WriteAccess;

    /*
     * Avoid DbgPrintEx at the very start of data abort handling.
     *
     * Data aborts can occur while switching address spaces / faulting in
     * critical mappings. Early DbgPrintEx can recurse into paging or grab locks
     * and wedge the fault path (observed during user stack probing).
     *
     * Keep entry logging for non-pagefault classes only.
     */
    if (EsrClass != 0x15 && EsrClass != 0x11 &&
        EsrClass != 0x20 && EsrClass != 0x21 &&
        EsrClass != 0x24 && EsrClass != 0x25) /* Skip SVC + instruction/data abort */
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                   "[arm64] SyncException: Class=0x%02lx ESR=0x%08lx ELR=%p FAR=%p\n",
                   EsrClass, Esr,
                   (PVOID)(ULONG_PTR)Context->State.Elr,
                   (PVOID)(ULONG_PTR)Context->State.FaultAddress);
    }

    /*
     * ARM64 memory model: Ensure exception state from assembly is visible.
     * The DSB/ISB in assembly ensures system register reads are complete,
     * but we need a compiler barrier to prevent C code reordering.
     */
    __asm__ __volatile__("" ::: "memory");

    Context->TrapFramePointer = NULL;
    Context->ExceptionFramePointer = NULL;

    /* One-shot guard to avoid recursive exception storms while KD/logging
     * is not fully reliable during bring-up. Do this before any further logging. */
    {
        ULONG CpuIndex = KeGetCurrentProcessorNumber();
        if (CpuIndex < MAXIMUM_PROCESSORS)
        {
            if (InterlockedCompareExchange(&KiArm64TrapActive[CpuIndex], 1, 0) != 0)
            {
                KiArm64BugCheckSynchronousException(Context);
                return TRUE; /* not reached */
            }
        }
    }

    switch (EsrClass)
    {
	        case 0x11: /* SVC from lower EL */
	        case 0x15: /* SVC from same EL */
	        {
	            ULONG ServiceNumber;
	            ULONG TableIndexShifted;
	            ULONG Instruction;
	            PKTHREAD Thread;

	            TrapFrame = &Context->TrapFrame;
	            KiArm64InitializeTrapFrame(Context, TrapFrame);

            /*
             * For ARM64 system calls, X8 carries the service number while the
             * SVC immediate selects the service table.  The current stubs emit
	             * SVC #0 (NT table), but keep the bits so KiSystemService can grow
             * into additional tables later on.
             */
            ServiceNumber = (ULONG)(TrapFrame->X[8] & SERVICE_NUMBER_MASK);
            TableIndexShifted = (((ULONG)(Iss & 0xFFFF)) << SERVICE_TABLE_SHIFT) & SERVICE_TABLE_MASK;
            Instruction = TableIndexShifted | ServiceNumber;

            /*
             * ARM64 FIX: Clear trap-active BEFORE calling KiSystemService.
             *
             * System calls can legitimately call nested system calls (e.g.,
             * NtCreateSymbolicLinkObject calls ObInsertObject which calls
             * other Nt* functions via Zw* wrappers). Each Zw* wrapper uses
             * SVC #0 to enter the kernel, even when already in kernel mode.
             *
             * If we don't clear the trap flag before KiSystemService, the
             * nested SVC will see KiArm64TrapActive as set and incorrectly
             * treat it as a recursive exception, causing a spurious crash.
             *
             * This is safe because:
             * 1. The trap frame is already initialized
             * 2. KiSystemService handles its own exception safety
             * 3. Any real fault during the syscall will set its own flag
             */
            KiArm64ClearTrapActive();

            Thread = KeGetCurrentThread();
            KiSystemService(Thread, TrapFrame, Instruction);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            return TRUE;
        }

	        case 0x20: /* Instruction abort, lower EL */
	        case 0x21: /* Instruction abort, same EL  */
	        {
	            EXCEPTION_RECORD ExceptionRecord;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

	            PreviousMode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);
	            WriteAccess = FALSE;

            /*
             * Use MmAccessFault (not MmArmAccessFault) to properly dispatch
             * the fault to the correct handler. MmAccessFault routes faults
             * to MmNotPresentFaultSectionView for ROS section views (like
             * VACB buffers) or MmArmAccessFault for ARM3 allocations.
             */
            Status = MmAccessFault(KiArm64BuildFaultCode(FaultStatus,
                                                         WriteAccess,
                                                         TRUE,
                                                         PreviousMode),
                                   (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                                   PreviousMode,
                                   TrapFrame);

            if (NT_SUCCESS(Status))
            {
                /*
                 * ARM64 CRITICAL: Cache Invalidation After Instruction Fetch Fault.
                 *
                 * For instruction fetch faults, we must invalidate BOTH I-cache
                 * and D-cache before returning to retry the instruction fetch.
                 *
                 * Why both caches?
                 * - I-cache: Contains instruction data for virtual addresses
                 * - D-cache: Modern ARM64 uses Harvard cache architecture where
                 *   instruction fetches can be satisfied from either cache
                 *
                 * The page fault handler just mapped fresh code from the ramdisk,
                 * but the caches may contain stale data for this virtual address.
                 * We must ensure the CPU fetches the FRESH instructions from the
                 * newly mapped physical page.
                 *
                 * Cache invalidation strategy for INCOMING code:
                 * - DC IVAC (Invalidate only) for D-cache - discard stale data
                 *   DO NOT use DC CIVAC - it would write back garbage to RAM!
                 * - IC IVAU for I-cache - standard instruction cache invalidation
                 *
                 * Order: DC IVAC -> DSB ISH -> IC IVAU -> DSB ISH -> ISB
                 */
                {
                    ULONG64 Ctr;
                    ULONG DcacheLineSize, IcacheLineSize;
                    ULONG_PTR Va;

                    /* Read CTR_EL0 to get cache line sizes */
                    __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
                    DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);
                    IcacheLineSize = 4u << (Ctr & 0xF);

                    /* Align fault address to page boundary */
                    Va = (ULONG_PTR)Context->State.FaultAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);

                    /*
                     * D-cache: Use DC IVAC (Invalidate only) for incoming code.
                     * This discards stale cache lines without writing back garbage.
                     */
                    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
                    {
                        __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
                    }

                    /* Ensure D-cache invalidation completes before I-cache ops */
                    __asm__ __volatile__("dsb ish" ::: "memory");

                    /* Invalidate I-cache for the entire page */
                    for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += IcacheLineSize)
                    {
                        __asm__ __volatile__("ic ivau, %0" :: "r"(Va + offset) : "memory");
                    }

                    /* Ensure all cache operations complete */
                    __asm__ __volatile__("dsb ish" ::: "memory");
                    __asm__ __volatile__("isb" ::: "memory");
                }

                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            if ((PreviousMode == KernelMode) && (!KdDebuggerEnabled || KdDebuggerNotPresent))
            {
                KiArm64BugCheckSynchronousException(Context);
                /* not reached */
            }

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 2;
            ExceptionRecord.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, TRUE);
            ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                PreviousMode,
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x24: /* Data abort, lower EL */
        case 0x25: /* Data abort, same EL  */
        {
            PETHREAD CurrentThread;
            ULONG64 CurrentSp;
            PVOID StackLimit;

            /*
             * ARM64: Handle alignment faults (DFSC = 0x21) as exceptions.
             *
             * DFSC values 0x21, 0x22, 0x23 are alignment faults, not page faults.
             * These cannot be resolved by the page fault handler and should be
             * dispatched as STATUS_DATATYPE_MISALIGNMENT exceptions.
             *
             * This is critical for Clang ARM64 which generates 128-bit SIMD stores
             * (str q0) that require 16-byte alignment. When the target address is
             * not 16-byte aligned, the CPU generates an alignment fault.
             *
             * Example: RtlZeroMemory on TEB offset 0x7D8 (8-byte aligned, not 16)
             * causes str q0 instruction to fault with DFSC=0x21.
             */
            if ((FaultStatus & 0x3C) == 0x20)  /* DFSC 0x20-0x23 are alignment related */
            {
                EXCEPTION_RECORD ExceptionRecord;
                KPROCESSOR_MODE Mode;
                BOOLEAN IsWrite = (Iss & (1u << 6)) != 0;

                TrapFrame = &Context->TrapFrame;
                KiArm64InitializeTrapFrame(Context, TrapFrame);

                Mode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);

                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                           "[arm64] DA ALIGNMENT FAULT: DFSC=0x%lx far=%p elr=%p mode=%d write=%d\n",
                           FaultStatus,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           Mode,
                           IsWrite);

                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 2;
                ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)IsWrite;
                ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                    KiArm64ReportUnhandledSyncException(Context, Esr);

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    Mode,
                                    TRUE);

                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

#if DBG
            /* Debug: Detect repeated faults on the same address */
            static volatile PVOID LastFaultAddress = NULL;
            static volatile LONG LastFaultCount = 0;
            PVOID CurrentFaultAddr = (PVOID)(ULONG_PTR)Context->State.FaultAddress;

            if (CurrentFaultAddr == LastFaultAddress)
            {
                LONG Count = InterlockedIncrement(&LastFaultCount);
                if (Count >= 2 && Count <= 5)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                               "[arm64] DA REPEATED FAULT #%ld: addr=%p elr=%p\n",
                               Count, CurrentFaultAddr, (PVOID)(ULONG_PTR)Context->State.Elr);
                }
                else if (Count > 100000)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                               "[arm64] DA FAULT LOOP DETECTED: addr=%p count=%ld\n",
                               CurrentFaultAddr, Count);
                }
            }
            else
            {
                LastFaultAddress = CurrentFaultAddr;
                InterlockedExchange(&LastFaultCount, 1);
            }
#endif

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            /* Check for stack exhaustion early */
            __asm__ __volatile__("mov %0, sp" : "=r"(CurrentSp));
            CurrentThread = PsGetCurrentThread();
            StackLimit = (PVOID)CurrentThread->Tcb.StackLimit;

            if (CurrentSp < (ULONG64)StackLimit + 0x800)
            {
                /* Stack exhausted - bugcheck before we overflow */
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                           "[arm64] DataAbort: STACK EXHAUSTED! SP=%p StackLimit=%p FAR=%p\n",
                           (PVOID)CurrentSp, StackLimit, (PVOID)(ULONG_PTR)Context->State.FaultAddress);
                KeBugCheckEx(KERNEL_STACK_INPAGE_ERROR,
                             CurrentSp,
                             (ULONG_PTR)StackLimit,
                             Context->State.FaultAddress,
                             0xDEAD5743);
            }

            /*
             * ARM64 DEBUG: Log all data aborts in System Cache region to diagnose stalls.
             */
            if ((ULONG64)Context->State.FaultAddress >= 0xFFFFF98000000000ULL &&
                (ULONG64)Context->State.FaultAddress < 0xFFFFFA8000000000ULL)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                           "[arm64] DA-SYSCACHE: esr=0x%lx far=%p elr=%p DFSC=0x%lx\n",
                           Esr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           Esr & 0x3FULL);
            }

            /*
             * ARM64 DEBUG: Log data aborts in Page Table self-map region (PDE_BASE fault).
             * PTE_BASE = 0xFFFFF68000000000, PTE_TOP = 0xFFFFF6FFFFFFFFFF
             */
            if ((ULONG64)Context->State.FaultAddress >= 0xFFFFF68000000000ULL &&
                (ULONG64)Context->State.FaultAddress <= 0xFFFFF6FFFFFFFFFFULL)
            {
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                           "[arm64] DA-PAGETABLE: esr=0x%lx far=%p elr=%p DFSC=0x%lx write=%d\n",
                           Esr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           Esr & 0x3FULL,
                           (Iss & (1u << 6)) != 0);
            }

            PreviousMode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);
            WriteAccess = (Iss & (1u << 6)) != 0;

            /* ARM64: skip accessed-bit fast path to avoid dereferencing
             * an unmapped PTE alias during bring-up. Fallback to the
             * general fault handler below. */

            {
                ULONG ProcessorIndex = KeGetCurrentProcessorNumber();
                BOOLEAN OwnsAbortGuard = FALSE;
#if DBG && defined(ARM64_TRAP_TRACE)
                LONG GuardSnapshot = -1;
#endif

#if DBG
                /* BUG #15: Log ALL data aborts to low user addresses */
                if ((ULONG64)Context->State.FaultAddress < 0x100000ULL)
                {
                    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                               "[arm64] DA LOW USER: esr=0x%lx far=%p elr=%p write=%d mode=%d spsr=0x%lx\n",
                               Esr,
                               (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                               (PVOID)(ULONG_PTR)Context->State.Elr,
                               WriteAccess,
                               PreviousMode,
                               Context->State.Spsr);
                    /* Debug: Check if ELR >= kernel base directly */
                    if (Context->State.Elr >= 0xFFFF800000000000ULL)
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DA LOW USER DEBUG: ELR is in kernel space! Should be KernelMode! Elr=0x%016llx KERNEL_BASE=0xFFFF800000000000\n",
                                   (unsigned long long)Context->State.Elr);
                    }
                }
#endif

                /* Keep logging minimal in trap path to avoid reentry */
#if DBG && defined(ARM64_TRAP_TRACE)
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA: esr=0x%lx far=%p elr=%p cpu=%lu guard=%ld\n",
                           Esr,
                           (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                           (PVOID)(ULONG_PTR)Context->State.Elr,
                           ProcessorIndex,
                           GuardSnapshot);
#endif

                if (ProcessorIndex < MAXIMUM_PROCESSORS)
                {
                    OwnsAbortGuard = (InterlockedCompareExchange(&KiArm64DataAbortOwner[ProcessorIndex],
                                                                  1,
                                                                  0) == 0);
                    if (!OwnsAbortGuard)
                    {
                        /* Nested data abort - always log this */
                        PVOID LrPointer = (PVOID)(ULONG_PTR)Context->State.Registers.X[30];
                        PVOID SpPointer = (PVOID)(ULONG_PTR)Context->State.Registers.Sp;

                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DA NESTED: esr=0x%lx far=%p elr=%p lr=%p sp=%p cpu=%lu\n",
                                   Esr,
                                   (PVOID)(ULONG_PTR)Context->State.FaultAddress,
                                   (PVOID)(ULONG_PTR)Context->State.Elr,
                                   LrPointer,
                                   SpPointer,
                                   ProcessorIndex);
                        /* Nested abort detected - bugcheck to prevent infinite loop */
                        KeBugCheckEx(PAGE_FAULT_IN_NONPAGED_AREA,
                                     (ULONG_PTR)Context->State.FaultAddress,
                                     (ULONG_PTR)Context->State.Elr,
                                     (ULONG_PTR)LrPointer,
                                     0xDA0DEAD);
                    }
                }

                {
                    ULONG FaultCodeArg = KiArm64BuildFaultCode(FaultStatus, WriteAccess, FALSE, PreviousMode);
                    PVOID AddressArg = (PVOID)(ULONG_PTR)Context->State.FaultAddress;
                    extern volatile LONG MmArmAccessFaultEntryCount;
                    extern volatile PVOID MmArmAccessFaultLastAddress;
                    extern volatile LONG MmArmAccessFaultInFunction;
                    LONG CountBefore = MmArmAccessFaultEntryCount;
                    LONG InFunctionBefore = MmArmAccessFaultInFunction;

                    /*
                     * ARM64: Access Flag faults (DFSC 0x9/0xA/0xB) must be fixed up by
                     * setting AF in the faulting descriptor. Some early boot mappings
                     * can have AF clear, causing an infinite abort loop even though the
                     * translation is otherwise valid.
                     */
                    if ((ULONG_PTR)AddressArg < (ULONG_PTR)MmSystemRangeStart &&
                        (FaultStatus == 0x9 || FaultStatus == 0xA || FaultStatus == 0xB))
                    {
                        if (KiArm64FixupUserAccessFlagFault(AddressArg, FaultStatus))
                        {
                            if (OwnsAbortGuard)
                            {
                                InterlockedExchange(&KiArm64DataAbortOwner[ProcessorIndex], 0);
                            }

                            Context->TrapFramePointer = TrapFrame;
                            Context->ExceptionFramePointer = &Context->ExceptionFrame;
                            KiArm64ClearTrapActive();
                            return TRUE;
                        }
                    }

                    /* Log for user-mode addresses that keep faulting */
                    if ((ULONG64)AddressArg >= 0x000007FFB7000000ULL &&
                        (ULONG64)AddressArg < 0x000007FFB8000000ULL)
                    {
                        static volatile LONG UserFaultLogBudget = 50;
                        if (UserFaultLogBudget > 0)
                        {
                            InterlockedDecrement(&UserFaultLogBudget);
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                       "[arm64] UserDA: ESR=0x%lx DFSC=0x%lx FAR=%p FaultCode=0x%lx write=%d mode=%d\n",
                                       Esr, FaultStatus, AddressArg, FaultCodeArg, WriteAccess, PreviousMode);
                        }
                    }

                    /* Log for low user-space addresses - debugging aid */
                    if ((ULONG64)AddressArg >= 0x30000ULL &&
                        (ULONG64)AddressArg < 0x50000ULL)
                    {
                        static volatile LONG LowUserFaultLogBudget = 5;
                        if (LowUserFaultLogBudget > 0)
                        {
                            InterlockedDecrement(&LowUserFaultLogBudget);
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                       "[arm64] LowUserDA: FAR=%p DFSC=0x%lx write=%d\n",
                                       AddressArg, FaultStatus, WriteAccess);
                        }
                    }

                    

                    /* Only log for specific problem address to reduce output */
                    if ((ULONG64)AddressArg >= 0xFFFF8000BCC00000ULL &&
                        (ULONG64)AddressArg < 0xFFFF8000BD000000ULL)
                    {
                        ULONG64 CurrentSp;
                        __asm__ __volatile__("mov %0, sp" : "=r"(CurrentSp));
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DataAbort: VACB far=%p SP=%p Count=%ld InFunc=%ld\n",
                                   AddressArg, (PVOID)CurrentSp, CountBefore, InFunctionBefore);
                        /* Clear InFunction before call */
                        InterlockedExchange(&MmArmAccessFaultInFunction, 0);
                    }

                    /*
                     * Use MmAccessFault (not MmArmAccessFault) to properly dispatch
                     * the fault to the correct handler. MmAccessFault routes faults
                     * to MmNotPresentFaultSectionView for ROS section views (like
                     * VACB buffers) or MmArmAccessFault for ARM3 allocations.
                     */
                    /* ARM64 DEBUG: Log before MmAccessFault for System Cache faults */
                    if ((ULONG64)AddressArg >= 0xFFFFF98000000000ULL &&
                        (ULONG64)AddressArg < 0xFFFFFA8000000000ULL)
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DA-SYSCACHE: BEFORE MmAccessFault far=%p FaultCode=0x%lx\n",
                                   AddressArg, FaultCodeArg);
                    }

                    /*
                     * ARM64: Clear trap-active flag BEFORE calling MmAccessFault.
                     *
                     * MmAccessFault can legitimately cause nested page faults. For example:
                     * - User page fault on DLL section
                     * - MmNotPresentFaultSectionView calls MmMakeSegmentResident
                     * - MmMakeSegmentResident triggers file I/O
                     * - File I/O accesses System Cache (VACB)
                     * - System Cache page fault on a different address
                     *
                     * If we don't clear KiArm64TrapActive, the second fault will see
                     * the flag set and incorrectly treat it as a recursive exception,
                     * causing a spurious bugcheck.
                     *
                     * The KiArm64DataAbortOwner guard (checked above) still protects
                     * against truly nested aborts (faults in the exception handling path
                     * before reaching MmAccessFault).
                     *
                     * This mirrors the SVC handling (line ~734) which clears the flag
                     * before calling KiSystemService.
                     */
                    KiArm64ClearTrapActive();

                    /*
                     * ARM64: Also clear the data abort owner guard.
                     *
                     * Similar to KiArm64TrapActive, MmAccessFault can cause nested data
                     * aborts that are legitimate (e.g., system cache page faults during
                     * file I/O). If we don't clear this guard, the nested abort will be
                     * incorrectly detected as a recursive fault.
                     *
                     * The guard will be reset by the outer code if OwnsAbortGuard is FALSE
                     * (i.e., we were the ones who set it). Since we clear it here, when
                     * MmAccessFault returns, OwnsAbortGuard will be checked and we'll skip
                     * the redundant clear.
                     */
                    if (OwnsAbortGuard)
                    {
                        InterlockedExchange(&KiArm64DataAbortOwner[ProcessorIndex], 0);
                    }

                    /* ARM64 DEBUG: Log before MmAccessFault for USER address faults */
Status = MmAccessFault(FaultCodeArg, AddressArg, PreviousMode, TrapFrame);

                    /* ARM64 DEBUG: Log after MmAccessFault for System Cache faults */
                    if ((ULONG64)AddressArg >= 0xFFFFF98000000000ULL &&
                        (ULONG64)AddressArg < 0xFFFFFA8000000000ULL)
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DA-SYSCACHE: AFTER MmAccessFault far=%p Status=0x%lx\n",
                                   AddressArg, Status);
                    }

                    /* ARM64 DEBUG: Log after MmAccessFault for Page Table faults */
                    if ((ULONG64)AddressArg >= 0xFFFFF68000000000ULL &&
                        (ULONG64)AddressArg <= 0xFFFFF6FFFFFFFFFFULL)
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DA-PAGETABLE: AFTER MmAccessFault far=%p Status=0x%lx\n",
                                   AddressArg, Status);
                    }

                    /* Log after call only for the problem address */
                    if ((ULONG64)AddressArg >= 0xFFFF8000BCC00000ULL &&
                        (ULONG64)AddressArg < 0xFFFF8000BD000000ULL)
                    {
                        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                   "[arm64] DataAbort: VACB returned Status=0x%lx Count=%ld InFunc=%ld Addr=%p\n",
                                   Status, MmArmAccessFaultEntryCount, MmArmAccessFaultInFunction, MmArmAccessFaultLastAddress);
                    }
                }

                if (OwnsAbortGuard)
                {
                    InterlockedExchange(&KiArm64DataAbortOwner[ProcessorIndex], 0);
                }

#if DBG && defined(ARM64_TRAP_TRACE)
                DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_TRACE_LEVEL,
                           "[arm64] DA exit: status=0x%lx cpu=%lu\n",
                           Status,
                           ProcessorIndex);
#endif
            }

            if (NT_SUCCESS(Status))
            {
                /*
                 * ARM64 CRITICAL: Cache Invalidation After Page Fault
                 *
                 * After successfully handling a page fault, we MUST invalidate the
                 * D-cache for the faulting address BEFORE returning to retry the
                 * instruction.
                 *
                 * Cache Invalidation Strategy:
                 *
                 * For INCOMING data (read faults, DMA/PIO populated pages):
                 *   Use DC IVAC (Invalidate by VA to PoC) - just discard cache lines.
                 *   The data source (ramdisk, disk, DMA) has already written to RAM.
                 *   We want to discard any stale cache data so CPU reads fresh RAM.
                 *
                 *   CRITICAL: DC CIVAC (Clean & Invalidate) is WRONG for incoming data!
                 *   CIVAC writes back dirty cache lines before invalidating. If the
                 *   cache has garbage (uninitialized or from previous mapping), CIVAC
                 *   writes that garbage to RAM, overwriting the good data.
                 *   Example: Cache has 0xE53F, RAM has "MZ" -> CIVAC writes 0xE53F to RAM!
                 *
                 * For OUTGOING data (CPU wrote, DMA needs to read):
                 *   Use DC CIVAC (Clean & Invalidate) - write back dirty lines first.
                 *   We want to ensure CPU writes reach RAM before DMA reads.
                 *
                 * Read faults (data abort, not write) = INCOMING data = DC IVAC
                 * Write faults for COW/demand-zero = page is zeroed, then INCOMING = DC IVAC
                 *
                 * NOTE: DC IVAC may trap at EL0 if SCTLR_EL1.UCI is not set.
                 * Since we're in EL1 (kernel mode), DC IVAC is always permitted.
                 *
                 * Order of operations:
                 * 1. MmAccessFault creates the mapping (PTE now valid)
                 * 2. DSB ISHST + TLBI + DSB ISH + ISB (already done in fault handler)
                 * 3. DC IVAC for entire page (invalidate stale cache)
                 * 4. DSB ISH (ensure cache ops complete)
                 * 5. Return to retry instruction (will read fresh data)
                 */
                {
                    ULONG_PTR Va;

                    /* Align fault address to page boundary */
                    Va = (ULONG_PTR)Context->State.FaultAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);

                    /*
                     * ARM64 Cache Maintenance Restriction:
                     *
                     * DC IVAC (Invalidate by VA to PoC) requires read permission to the
                     * virtual address from the current exception level (EL1). For user-space
                     * pages mapped with certain permissions (e.g., read-only with NotDirty=1),
                     * DC IVAC can fault with a permission error.
                     *
                     * We only perform cache invalidation for KERNEL addresses (SYSCACHE, etc.)
                     * where we control the permissions and know they're accessible from EL1.
                     *
                     * User-space pages don't need this cache invalidation because:
                     * 1. User pages aren't populated by DMA - they're demand-loaded from file
                     * 2. The page fault handler ensures cache coherency differently
                     * 3. MmCreateVirtualMapping handles cache operations for user mappings
                     */
                    if (Va >= (ULONG_PTR)MmSystemRangeStart)
                    {
                        ULONG64 Ctr;
                        ULONG DcacheLineSize;

                        /* Read CTR_EL0 to get D-cache line size */
                        __asm__ __volatile__("mrs %0, ctr_el0" : "=r"(Ctr));
                        DcacheLineSize = 4u << ((Ctr >> 16) & 0xF);

                        /*
                         * Use DC IVAC (Invalidate only) for incoming data.
                         * This discards stale cache lines without writing back garbage.
                         */
                        for (ULONG_PTR offset = 0; offset < PAGE_SIZE; offset += DcacheLineSize)
                        {
                            __asm__ __volatile__("dc ivac, %0" :: "r"(Va + offset) : "memory");
                        }

                        /* Ensure all cache operations complete before returning */
                        __asm__ __volatile__("dsb ish" ::: "memory");
                        __asm__ __volatile__("isb" ::: "memory");

                        /* DEBUG: For SYSCACHE, verify data after cache invalidation */
                        if (Va >= 0xFFFFF98000000000ULL && Va < 0xFFFFFA8000000000ULL)
                        {
                            volatile PUCHAR CheckData = (volatile PUCHAR)Va;
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                       "[arm64] DA-SYSCACHE: AFTER IVAC VA=%p first4=0x%02x 0x%02x 0x%02x 0x%02x\n",
                                       (PVOID)Va, CheckData[0], CheckData[1], CheckData[2], CheckData[3]);
                        }
                    }
                }

                /*
                 * ARM64 DEBUG: Log trap frame X0 for user-space page faults
                 * This helps diagnose register corruption issues where X0 gets
                 * corrupted between page fault handling and instruction retry.
                 */
                {
                    ULONG_PTR FaultVa = (ULONG_PTR)Context->State.FaultAddress & ~(ULONG_PTR)(PAGE_SIZE - 1);
                    if (FaultVa < (ULONG_PTR)MmSystemRangeStart)
                    {
                        static volatile LONG UserFaultX0LogBudget = 20;
                        if (UserFaultX0LogBudget > 0)
                        {
                            InterlockedDecrement(&UserFaultX0LogBudget);
                            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                                       "[arm64] DA-USER-RETURN: VA=%p TrapFrame->X0=0x%llx Context->X0=0x%llx ELR=%p\n",
                                       (PVOID)FaultVa,
                                       (unsigned long long)TrapFrame->X0,
                                       (unsigned long long)Context->State.Registers.X[0],
                                       (PVOID)(ULONG_PTR)TrapFrame->Pc);
                        }
                    }
                }

                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            /* Not resolved by Mm - this is an unhandled data abort. */
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[arm64] DA FAILED: MmAccessFault returned 0x%lx for addr=%p PreviousMode=%d\n",
                       Status, (PVOID)(ULONG_PTR)Context->State.FaultAddress, PreviousMode);
            DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL,
                       "[arm64] DA FAILED: KdDebuggerEnabled=%d KdDebuggerNotPresent=%d\n",
                       KdDebuggerEnabled, KdDebuggerNotPresent);

#ifdef KDBG
            /*
             * Call KDBG to display crash diagnostics (registers, stack trace,
             * modules) for kernel-mode faults. This runs regardless of KD state
             * since KDBG provides valuable crash info even when KD is "enabled"
             * for serial output but no interactive debugger is attached.
             */
            if (PreviousMode == KernelMode)
            {
                EXCEPTION_RECORD64 ExceptionRecord64;
                CONTEXT KdbContext;

                RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
                ExceptionRecord64.ExceptionCode = STATUS_ACCESS_VIOLATION;
                ExceptionRecord64.ExceptionFlags = 0;
                ExceptionRecord64.ExceptionAddress = Context->State.Elr;
                ExceptionRecord64.NumberParameters = 2;
                ExceptionRecord64.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, FALSE);
                ExceptionRecord64.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                RtlZeroMemory(&KdbContext, sizeof(KdbContext));
                KdbContext.ContextFlags = CONTEXT_FULL | CONTEXT_ARM64;

                KeTrapFrameToContext(TrapFrame, Context->ExceptionFramePointer, &KdbContext);

                KdbEnterDebuggerException(&ExceptionRecord64,
                                          PreviousMode,
                                          &KdbContext,
                                          TRUE);
            }
#endif /* KDBG */

            /* If we're in kernel mode and no debugger is attached, bugcheck
             * immediately to avoid recursive faults while trying to log/dispatch.
             * This mirrors amd64 behavior when KD is unavailable during early boot. */
            if ((PreviousMode == KernelMode) && (!KdDebuggerEnabled || KdDebuggerNotPresent))
            {
                KiArm64BugCheckSynchronousException(Context);
                /* not reached */
            }

            /* Otherwise, dispatch an access violation through KiDispatchException
             * so KD can catch first/second chance and print the crash context. */
            {
                EXCEPTION_RECORD ExceptionRecord;
                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_ACCESS_VIOLATION;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 2;
                ExceptionRecord.ExceptionInformation[0] = KiArm64AccessTypeToExceptionInfo(WriteAccess, FALSE);
                ExceptionRecord.ExceptionInformation[1] = (ULONG_PTR)Context->State.FaultAddress;

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    PreviousMode,
                                    TRUE);

                /* Return with updated trap frame (either resumed, or KD/bugcheck handled). */
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }
        }

        case 0x22: /* PC alignment fault */
        case 0x26: /* SP alignment fault */
        {
            EXCEPTION_RECORD ExceptionRecord;
            KPROCESSOR_MODE Mode;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            Mode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_DATATYPE_MISALIGNMENT;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                Mode,
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x2F: /* SError */
        {
            EXCEPTION_RECORD ExceptionRecord;

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_HARDWARE_MEMORY_ERROR;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
                KiArm64ReportUnhandledSyncException(Context, Esr);

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr),
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        case 0x3C: /* BRK instruction */
        {
            KPROCESSOR_MODE Mode = KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr);
#ifdef KDBG
            KD_CONTINUE_TYPE KdbResult = kdHandleException;
#endif

            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

#ifdef KDBG
            /*
             * When KDBG is compiled in, call KdbEnterDebuggerException to
             * handle the breakpoint interactively. KDBG provides a built-in
             * debugger that works even when no external debugger is attached.
             *
             * If KdbEnterDebuggerException returns kdContinue, the breakpoint
             * was handled and we should skip past the BRK instruction.
             * If it returns kdHandleException, we need to dispatch through
             * the normal exception path for an external debugger.
             */
            {
                EXCEPTION_RECORD64 ExceptionRecord64;
                CONTEXT KdbContext;

                /* Build EXCEPTION_RECORD64 for KDBG */
                RtlZeroMemory(&ExceptionRecord64, sizeof(ExceptionRecord64));
                ExceptionRecord64.ExceptionCode = STATUS_BREAKPOINT;
                ExceptionRecord64.ExceptionFlags = 0;
                ExceptionRecord64.ExceptionAddress = Context->State.Elr;
                ExceptionRecord64.NumberParameters = 1;
                ExceptionRecord64.ExceptionInformation[0] = Context->State.Elr;

                /* Build CONTEXT from trap frame for KDBG */
                RtlZeroMemory(&KdbContext, sizeof(KdbContext));
                KdbContext.ContextFlags = CONTEXT_FULL | CONTEXT_ARM64;
                KeTrapFrameToContext(TrapFrame, Context->ExceptionFramePointer, &KdbContext);

                /* Call KDBG to handle the breakpoint */
                KdbResult = KdbEnterDebuggerException(&ExceptionRecord64,
                                                      Mode,
                                                      &KdbContext,
                                                      TRUE);

                /*
                 * If KDBG handled the breakpoint (kdContinue), propagate any
                 * PC changes from KdbContext back to TrapFrame. KDBG may have
                 * advanced PC past the BRK instruction.
                 */
                if (KdbResult == kdContinue)
                {
                    KeContextToTrapFrame(&KdbContext,
                                         Context->ExceptionFramePointer,
                                         TrapFrame,
                                         KdbContext.ContextFlags,
                                         Mode);

                    /*
                     * Ensure PC is advanced past the BRK instruction.
                     * KdbEnterDebuggerException should have done this, but
                     * verify and fix if needed to prevent infinite loops.
                     */
                    if (TrapFrame->Pc == Context->State.Elr)
                    {
                        TrapFrame->Pc += 4;
                    }
                    Context->State.Elr = TrapFrame->Pc;
                    Context->TrapFramePointer = TrapFrame;
                    Context->ExceptionFramePointer = &Context->ExceptionFrame;
                    KiArm64ClearTrapActive();
                    return TRUE;
                }
            }
#endif /* KDBG */

            /*
             * KDBG did not handle the breakpoint (or KDBG is not compiled in).
             * Check if we should skip the breakpoint or dispatch to an external
             * debugger.
             */
            if (!KdDebuggerEnabled || KdDebuggerNotPresent)
            {
                /* No external debugger - skip the BRK instruction */
                TrapFrame->Pc = (ULONG64)((ULONG_PTR)Context->State.Elr + 4);
                Context->State.Elr += 4;
                Context->TrapFramePointer = TrapFrame;
                Context->ExceptionFramePointer = &Context->ExceptionFrame;
                KiArm64ClearTrapActive();
                return TRUE;
            }

            /* External debugger attached - dispatch through normal path */
            {
                EXCEPTION_RECORD ExceptionRecord;

                RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
                ExceptionRecord.ExceptionCode = STATUS_BREAKPOINT;
                ExceptionRecord.ExceptionFlags = 0;
                ExceptionRecord.ExceptionRecord = NULL;
                ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
                ExceptionRecord.NumberParameters = 1;
                ExceptionRecord.ExceptionInformation[0] = (ULONG_PTR)Context->State.Elr;

                KI_ARM64_STAGE_LOGF("[arm64] TrapDiag: forwarding BRK to KD esr=0x%lx elr=%p",
                                    Esr,
                                    (PVOID)(ULONG_PTR)Context->State.Elr);

                KiDispatchException(&ExceptionRecord,
                                    Context->ExceptionFramePointer,
                                    TrapFrame,
                                    Mode,
                                    TRUE);
            }

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }

        default:
        {
            EXCEPTION_RECORD ExceptionRecord;
            TrapFrame = &Context->TrapFrame;
            KiArm64InitializeTrapFrame(Context, TrapFrame);

            /*
             * ALWAYS log unhandled exceptions for debugging, regardless of KD state.
             * This is critical for diagnosing early boot crashes where KD may not
             * be fully initialized or responsive.
             */
            KiArm64ReportUnhandledSyncException(Context, Esr);

            RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
            ExceptionRecord.ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            ExceptionRecord.ExceptionFlags = 0;
            ExceptionRecord.ExceptionRecord = NULL;
            ExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)Context->State.Elr;
            ExceptionRecord.NumberParameters = 0;

            KiDispatchException(&ExceptionRecord,
                                Context->ExceptionFramePointer,
                                TrapFrame,
                                KiArm64PreviousModeFromContext(Context->State.Spsr, Context->State.Elr),
                                TRUE);

            Context->TrapFramePointer = TrapFrame;
            Context->ExceptionFramePointer = &Context->ExceptionFrame;
            KiArm64ClearTrapActive();
            return TRUE;
        }
    }
}
