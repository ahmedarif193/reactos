/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/ke/arm64/stubs.c
 * PURPOSE:         Minimal ARM64 kernel support stubs required for linking
 */

#include <ntoskrnl.h>
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

/* TODO(ARM64): Replace these stub implementations with real logic once the
 * debugger, user-mode callbacks, and memory manager are fully brought up. */

/*
 * User-mode protection -> PTE attribute table.
 *
 * Every (non-NOACCESS) entry sets ARM64_PTE_NG: user leaf translations are
 * per-process and must be tagged non-global so the ASID allocator can keep them
 * cached across context switches (a global entry would match any ASID and leak
 * across processes once flush-free switching is enabled). This is the user
 * counterpart to MmProtectToPteMaskKernel[], whose kernel pages stay global.
 * With the ASID allocator disabled (default), every switch still broadcast-
 * flushes, so the NG bit is behaviour-neutral until the allocator is enabled.
 */
const ULONG_PTR MmProtectToPteMask[32] =
{
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE             | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_READWRITE           | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    0,
    PTE_READONLY            | PTE_DISABLE_CACHE        | ARM64_PTE_NG,
    PTE_EXECUTE             | PTE_DISABLE_CACHE        | ARM64_PTE_NG,
    PTE_EXECUTE_READ        | PTE_DISABLE_CACHE        | ARM64_PTE_NG,
    PTE_READWRITE           | PTE_DISABLE_CACHE        | ARM64_PTE_NG,
    PTE_WRITECOPY           | PTE_DISABLE_CACHE        | ARM64_PTE_NG,
    PTE_EXECUTE_READWRITE   | PTE_DISABLE_CACHE        | ARM64_PTE_NG,
    PTE_EXECUTE_WRITECOPY   | PTE_DISABLE_CACHE        | ARM64_PTE_NG,
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE             | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_READWRITE           | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE         | ARM64_PTE_NG,
    0,
    PTE_READONLY            | PTE_WRITECOMBINED_CACHE  | ARM64_PTE_NG,
    PTE_EXECUTE             | PTE_WRITECOMBINED_CACHE  | ARM64_PTE_NG,
    PTE_EXECUTE_READ        | PTE_WRITECOMBINED_CACHE  | ARM64_PTE_NG,
    PTE_READWRITE           | PTE_WRITECOMBINED_CACHE  | ARM64_PTE_NG,
    PTE_WRITECOPY           | PTE_WRITECOMBINED_CACHE  | ARM64_PTE_NG,
    PTE_EXECUTE_READWRITE   | PTE_WRITECOMBINED_CACHE  | ARM64_PTE_NG,
    PTE_EXECUTE_WRITECOPY   | PTE_WRITECOMBINED_CACHE  | ARM64_PTE_NG,
};

/*
 * MmProtectToPteMaskKernel - ARM64 kernel-mode protection to PTE mask table.
 *
 * This table mirrors MmProtectToPteMask[] but uses KERNEL execute semantics:
 *   PXN=0, UXN=1 for executable entries (kernel can execute, user cannot).
 *
 * MmProtectToPteMask[] uses USER execute semantics (PXN=1, UXN=0), which is
 * correct for MI_MAKE_HARDWARE_PTE / MI_MAKE_HARDWARE_PTE_USER. However,
 * MI_MAKE_HARDWARE_PTE_KERNEL also indexes MmProtectToPteMask[], which causes
 * kernel executable pages (kernel .text, drivers) to have PXN=1, making them
 * non-executable at EL1.
 *
 * MI_MAKE_HARDWARE_PTE_KERNEL should use this table instead, or call
 * MiArm64FixupKernelExecutePte() after applying MmProtectToPteMask[].
 *
 * Differences from MmProtectToPteMask[]:
 *   - PTE_EXECUTE (PXN) replaced with PTE_EXECUTE_KERNEL (UXN)
 *   - PTE_EXECUTE_READ (PXN) replaced with PTE_EXECUTE_KERNEL_READ (UXN)
 *   - PTE_EXECUTE_READWRITE (PXN|WRITE) replaced with PTE_EXECUTE_KERNEL_RW (UXN|WRITE)
 *   - PTE_EXECUTE_WRITECOPY (PXN|COW) replaced with PTE_EXECUTE_KERNEL_WC (UXN|COW)
 *   - PTE_READONLY stays (PXN|UXN) -- both NX bits = non-executable, correct for all
 *   - PTE_READWRITE stays (PXN|UXN|WRITE) -- non-executable read-write, correct
 *   - PTE_WRITECOPY stays (PXN|UXN|COW) -- non-executable copy-on-write, correct
 */
const ULONG_PTR MmProtectToPteMaskKernel[32] =
{
    0,
    PTE_READONLY              | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL        | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL_READ   | PTE_ENABLE_CACHE,
    PTE_READWRITE             | PTE_ENABLE_CACHE,
    PTE_WRITECOPY             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL_RW     | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL_WC     | PTE_ENABLE_CACHE,
    0,
    PTE_READONLY              | PTE_DISABLE_CACHE,
    PTE_EXECUTE_KERNEL        | PTE_DISABLE_CACHE,
    PTE_EXECUTE_KERNEL_READ   | PTE_DISABLE_CACHE,
    PTE_READWRITE             | PTE_DISABLE_CACHE,
    PTE_WRITECOPY             | PTE_DISABLE_CACHE,
    PTE_EXECUTE_KERNEL_RW     | PTE_DISABLE_CACHE,
    PTE_EXECUTE_KERNEL_WC     | PTE_DISABLE_CACHE,
    0,
    PTE_READONLY              | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL        | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL_READ   | PTE_ENABLE_CACHE,
    PTE_READWRITE             | PTE_ENABLE_CACHE,
    PTE_WRITECOPY             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL_RW     | PTE_ENABLE_CACHE,
    PTE_EXECUTE_KERNEL_WC     | PTE_ENABLE_CACHE,
    0,
    PTE_READONLY              | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_KERNEL        | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_KERNEL_READ   | PTE_WRITECOMBINED_CACHE,
    PTE_READWRITE             | PTE_WRITECOMBINED_CACHE,
    PTE_WRITECOPY             | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_KERNEL_RW     | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_KERNEL_WC     | PTE_WRITECOMBINED_CACHE,
};

const ULONG MmProtectToValue[32] =
{
    PAGE_NOACCESS,
    PAGE_READONLY,
    PAGE_EXECUTE,
    PAGE_EXECUTE_READ,
    PAGE_READWRITE,
    PAGE_WRITECOPY,
    PAGE_EXECUTE_READWRITE,
    PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_NOCACHE | PAGE_READONLY,
    PAGE_NOCACHE | PAGE_EXECUTE,
    PAGE_NOCACHE | PAGE_EXECUTE_READ,
    PAGE_NOCACHE | PAGE_READWRITE,
    PAGE_NOCACHE | PAGE_WRITECOPY,
    PAGE_NOCACHE | PAGE_EXECUTE_READWRITE,
    PAGE_NOCACHE | PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_GUARD | PAGE_READONLY,
    PAGE_GUARD | PAGE_EXECUTE,
    PAGE_GUARD | PAGE_EXECUTE_READ,
    PAGE_GUARD | PAGE_READWRITE,
    PAGE_GUARD | PAGE_WRITECOPY,
    PAGE_GUARD | PAGE_EXECUTE_READWRITE,
    PAGE_GUARD | PAGE_EXECUTE_WRITECOPY,
    PAGE_NOACCESS,
    PAGE_WRITECOMBINE | PAGE_READONLY,
    PAGE_WRITECOMBINE | PAGE_EXECUTE,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_READ,
    PAGE_WRITECOMBINE | PAGE_READWRITE,
    PAGE_WRITECOMBINE | PAGE_WRITECOPY,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_READWRITE,
    PAGE_WRITECOMBINE | PAGE_EXECUTE_WRITECOPY
};

ULONG_PTR MmGlobalKernelPageDirectory[4096];

/*
 * IMPORTANT: On ARM64, page table descriptors must use the correct type bits.
 * - Table descriptors (L0/L1/L2 pointers) require bits[1:0] = 0b11.
 * - NT also exposes those same descriptor values through recursive PTE-shaped
 *   aliases, so the kernel table template must match the loader ABI exactly:
 *   0x713 = table, Normal-WB, Inner Shareable, Accessed.
 * - Page descriptors (leaf L3 entries) also use bits[1:0] = 0b11 with AF=1.
 *
 * The previous stub values only set Valid/Accessed, which made table
 * descriptors look like invalid/leaf entries to the hardware. That caused
 * early faults when the kernel tried to touch the self-mapped page tables.
 */
MMPTE ValidKernelPte = {
    .u.Hard = {
        .Valid = 1,
        .NotLargePage = 1,   /* ensure type==table/page (0b11) */
        .OsAvailable2 = 1,   /* AttrIndx bit 2: Normal WB (index 4) */
        .Shareability = 3,   /* Inner Shareable */
        .Accessed = 1,       /* AF=1 for leaf PTEs */
        .Writable = 1,
        .Owner = 0,
    }
};
MMPDE ValidKernelPde = {
    .u.Long = ARM64_PTE_TABLE_DESCRIPTOR_ATTRS
};
MMPTE DemandZeroPte = {.u.Long = (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS)};
MMPDE DemandZeroPde = {.u.Long = (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS)};
MMPTE PrototypePte = {
    .u.Soft = {
        .Protection = MM_READWRITE,
        .Prototype = 1,
        .PageFileHigh = MI_PTE_LOOKUP_NEEDED,
    }
};
MMPTE ValidKernelPteLocal = {
    .u.Hard = {
        .Valid = 1,
        .NotLargePage = 1,
        .OsAvailable2 = 1,
        .Shareability = 3,
        .Accessed = 1,
        .Writable = 1,
        .Owner = 0
    }
};
MMPDE ValidKernelPdeLocal = {
    .u.Long = ARM64_PTE_TABLE_DESCRIPTOR_ATTRS
};

/* Template PTE for decommitted page.
 * CRITICAL: Must use MM_DECOMMIT, NOT MM_READWRITE!
 * On ARM64, MMPTE_SOFTWARE.Protection is at bits 1-5, so MM_PTE_SOFTWARE_PROTECTION_BITS = 1.
 * Using MM_READWRITE (0x4) would create value 0x8, which collides with legitimate
 * prototype PTEs that have MM_READWRITE protection.
 * MM_DECOMMIT = MM_GUARDPAGE (0x10) creates unique value 0x20 that won't appear
 * in normal prototype PTEs, making the assertion in MiResolveProtoPteFault valid. */
MMPTE MmDecommittedPte = {.u.Long = (MM_DECOMMIT << MM_PTE_SOFTWARE_PROTECTION_BITS)};

/* TODO(ARM64): The above globals mimic the legacy layouts purely to unblock
 * the build. Replace with real hardware descriptors once paging support is
 * implemented. */

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;

VOID
NTAPI
DbgBreakPointWithStatus(
    _In_ ULONG Status)
{
    UNREFERENCED_PARAMETER(Status);

    /*
     * On ARM64, always trigger BRK when KDBG is compiled in (regardless of
     * whether an external debugger is attached). This allows the integrated
     * kernel debugger to display crash diagnostics (registers, stack trace)
     * during bugchecks. The BRK handler in trapc.c routes the exception to
     * KdbEnterDebuggerException when KDBG is enabled.
     *
     * If no debugger of any kind is available, the trap handler will skip
     * the breakpoint by advancing PC.
     */
#ifdef KDBG
    __asm__ __volatile__("brk #0xf000" ::: "memory");
#else
    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        return;
    }
    __asm__ __volatile__("brk #0xf000" ::: "memory");
#endif
}

VOID
NTAPI
DbgBreakPoint(VOID)
{
    DbgBreakPointWithStatus(STATUS_BREAKPOINT);
}

VOID
NTAPI
RtlpBreakWithStatusInstruction(VOID)
{
    DbgBreakPointWithStatus(STATUS_BREAKPOINT);
}

NTSTATUS
NTAPI
KeRaiseUserException(
    _In_ NTSTATUS ExceptionCode)
{
    PTEB Teb = KeGetCurrentThread()->Teb;
    PKTRAP_FRAME TrapFrame = KeGetCurrentThread()->TrapFrame;
    ULONG64 OldPc;

    if ((Teb == NULL) || (TrapFrame == NULL))
    {
        return STATUS_UNSUCCESSFUL;
    }

    _SEH2_TRY
    {
        Teb->ExceptionCode = ExceptionCode;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    /*
     * ARM64 FIX: Convert KeRaiseUserExceptionDispatcher to user address.
     */
    {
        PKTHREAD Thread = KeGetCurrentThread();
        PEPROCESS Process = (PEPROCESS)Thread->ApcState.Process;
        PVOID UserRaiseExceptionDispatcher = KiConvertSystemDllAddressToUser(KeRaiseUserExceptionDispatcher, Process);
        if ((UserRaiseExceptionDispatcher == NULL) &&
            (KeRaiseUserExceptionDispatcher != NULL) &&
            ((ULONG_PTR)KeRaiseUserExceptionDispatcher < (ULONG_PTR)MmSystemRangeStart))
        {
            UserRaiseExceptionDispatcher = KeRaiseUserExceptionDispatcher;
        }
        if (UserRaiseExceptionDispatcher == NULL)
        {
            DPRINT1("[arm64][EXC] unresolved raise dispatcher: proc=%.16s KeRaiseUserExceptionDispatcher=%p SystemDllBase=%p PspSystemDllBase=%p TrapPc=%p ExceptionCode=0x%08lx\n",
                    PsGetCurrentProcess()->ImageFileName,
                    KeRaiseUserExceptionDispatcher,
                    Process ? PspSystemDllBase : NULL,
                    PspSystemDllBase,
                    (PVOID)(ULONG_PTR)TrapFrame->Pc,
                    (ULONG)ExceptionCode);
            return STATUS_UNSUCCESSFUL;
        }

        OldPc = TrapFrame->Pc;
        TrapFrame->Pc = (ULONG64)(ULONG_PTR)UserRaiseExceptionDispatcher;
        return (NTSTATUS)OldPc;
    }
}

VOID
NTAPI
MmInitGlobalKernelPageDirectory(VOID)
{
    /*
     * Populate a minimal kernel PDE template from the current page tables.
     *
     * Notes:
     * - On ARM64 the kernel page tables are 4-level. Common MM expects this
     *   routine to seed a global array with kernel PDEs so future address
     *   spaces can inherit them. Our ARM64 address space creation actually
     *   clones the kernel half of the PXE page directly (see
     *   arch/arm64/mm/procsup.c:MiArchCreateProcessAddressSpace), so this
     *   array is not currently consumed. However, mm/mminit.c still calls
     *   this API. We therefore fill a sensible subset without logging TODOs.
     * - We only mirror the first PDE page that covers MmSystemRangeStart.
     *   This keeps behavior similar to ARM32 and avoids overcommitting a
     *   large global array that is not referenced on ARM64 paths.
     * - PDE_BASE points to the linear self-map of the page directory level.
     *   However, we must access the specific PDE page covering kernel space,
     *   not the base of the region (which corresponds to user space and may
     *   not be valid/mapped).
     * - We skip the PTE_BASE and HYPER_SPACE slots: those are self-map and
     *   hyperspace PDEs managed elsewhere (and can have special semantics).
     * - We never overwrite a non-zero MmGlobalKernelPageDirectory entry: if
     *   something pre-seeded a slot, we keep that, mirroring i386/ARM logic.
     * - Concurrency: runs during early MmInitSystem; single-threaded.
     */
#if 0
    /* Current kernel PDE page (self-mapped view) corresponding to start of kernel space */
    PULONG_PTR CurrentPde = (PULONG_PTR)PAGE_ALIGN(MiAddressToPde(MmSystemRangeStart));
    /* First index of the kernel range within the current PDE page */
    const ULONG start = MiGetPdeOffset(MmSystemRangeStart);
    /* Indices of special regions to be skipped */
    const ULONG pte_off = MiGetPdeOffset((PVOID)PTE_BASE);
    const ULONG hyper_off = MiGetPdeOffset((PVOID)HYPER_SPACE);

    for (ULONG i = start; i < PDE_PER_PAGE; ++i)
    {
        /* Skip the PTE self-map and hyperspace PDEs */
        if ((i == pte_off) || (i == hyper_off))
            continue;

        /* Copy the current PDE entry if our template slot is empty */
        if (!MmGlobalKernelPageDirectory[i] && CurrentPde[i])
        {
            MmGlobalKernelPageDirectory[i] = CurrentPde[i];
        }
    }
#endif
}

/*
 * KeSwitchKernelStack - Switch the current thread to a new (larger) kernel stack.
 *
 * ARM64 Implementation Notes:
 *
 * This function copies the current stack contents to the new stack, adjusts all
 * thread metadata pointers, and then physically adjusts SP to point to the
 * corresponding location on the new stack.
 *
 * The SP adjustment must be done via inline assembly because the compiler
 * cannot know that we're changing the stack pointer underneath it.
 *
 * The function is called from PsConvertToGuiThread inside a guarded region
 * (APCs disabled), so we won't be preempted during the switch.
 *
 * Parameters:
 *   StackBase  - Top (highest address) of the new stack
 *   StackLimit - Bottom (lowest address) of the new stack
 *
 * Returns:
 *   The old StackBase (caller uses this to free the old stack)
 */
PVOID
NTAPI
KeSwitchKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit)
{
    PKTHREAD CurrentThread;
    PVOID OldStackBase;
    ULONG_PTR OldStackLimit;
    ULONG_PTR OldFramePointer;
    LONG_PTR StackOffset;
    SIZE_T StackSize;
    PKIPCR Pcr;

    /* Get the current thread */
    CurrentThread = KeGetCurrentThread();

    /* Save the old stack base for return value */
    OldStackBase = CurrentThread->StackBase;
    OldStackLimit = CurrentThread->StackLimit;
    __asm__ __volatile__("mov %0, x29" : "=r"(OldFramePointer));

    /* Compute size of current stack contents */
    StackSize = (ULONG_PTR)CurrentThread->StackBase - OldStackLimit;
    ASSERT(StackSize <= (ULONG_PTR)StackBase - (ULONG_PTR)StackLimit);

    /* Calculate the offset between old and new stacks */
    StackOffset = (PUCHAR)StackBase - (PUCHAR)CurrentThread->StackBase;

    /*
     * Mask ALL exception/interrupt sources (DAIF: Debug, SError, IRQ, FIQ)
     * BEFORE copying the stack contents.
     *
     * _disable() may only mask IRQ/FIQ (DAIF bits I and F), leaving SError
     * and Debug exceptions unmasked.  For the stack-switch critical section
     * we need the same "mask everything" policy used in trap return to
     * guarantee the old stack is completely quiescent during the copy and
     * the subsequent pointer adjustments.
     *
     * If any asynchronous event fires during the copy, the handler pushes
     * frames onto the old stack; the new stack would then contain a stale
     * snapshot that misses those modifications.
     */
    {
        ULONGLONG _savedDaif;
        __asm__ __volatile__("mrs %0, daif" : "=r"(_savedDaif));
        __asm__ __volatile__("msr daifset, #0xF" ::: "memory");
        __asm__ __volatile__("isb" ::: "memory");

    /* Copy the entire current stack to the new stack */
    RtlCopyMemory((PUCHAR)StackBase - StackSize,
                  (PVOID)OldStackLimit,
                  StackSize);

    /*
     * Relocate the saved frame-pointer chain inside the copied stack.
     *
     * The live x29 is adjusted below, but every caller will later restore its
     * own x29 from the copied frame record. If those saved records still point
     * at the old stack, frame-pointer-relative local accesses after returning
     * from this routine will use freed stack memory.
     */
    {
        ULONG_PTR FramePointer = OldFramePointer;
        ULONG_PTR FramesLeft = StackSize / sizeof(PVOID);

        while ((FramesLeft-- != 0) &&
               (FramePointer >= OldStackLimit) &&
               (FramePointer < (ULONG_PTR)OldStackBase))
        {
            ULONG_PTR NewFramePointer = FramePointer + StackOffset;
            ULONG_PTR PreviousFramePointer = *(PULONG_PTR)NewFramePointer;

            if ((PreviousFramePointer < OldStackLimit) ||
                (PreviousFramePointer >= (ULONG_PTR)OldStackBase))
            {
                break;
            }

            *(PULONG_PTR)NewFramePointer = PreviousFramePointer + StackOffset;

            if (PreviousFramePointer <= FramePointer)
            {
                break;
            }

            FramePointer = PreviousFramePointer;
        }
    }

    /* Adjust thread trap frame pointer to new stack */
    if (CurrentThread->TrapFrame != NULL)
    {
        CurrentThread->TrapFrame = (PKTRAP_FRAME)((PUCHAR)CurrentThread->TrapFrame +
                                                   StackOffset);

        /*
         * ARM64 FIX: Also adjust the linked list pointer inside the trap frame.
         *
         * TrapFrame->TrapFrame points to the PREVIOUS trap frame (set up by
         * the SVC handler in trapc.c before calling KiSystemService). The
         * previous trap frame is also on the kernel stack (it's the thread-init
         * trap frame from KiInitializeContextThread). Since we copied the entire
         * stack, the previous trap frame exists at its old address + StackOffset,
         * but the pointer stored in TrapFrame->TrapFrame still has the old address.
         *
         * We must adjust it so that KiGetLinkedTrapFrame returns the correct
         * address on the new stack.
         *
         * Only adjust if the linked trap frame pointer is non-NULL (it can be
         * NULL for the very first trap frame in the chain).
         */
        if (CurrentThread->TrapFrame->TrapFrame != 0)
        {
            CurrentThread->TrapFrame->TrapFrame += (ULONG64)StackOffset;
        }
    }

    /* Adjust initial stack pointer */
    CurrentThread->InitialStack = (PVOID)((PUCHAR)CurrentThread->InitialStack +
                                          StackOffset);

    /* Update stack limits and mark as large stack */
    CurrentThread->StackBase = StackBase;
    CurrentThread->StackLimit = (ULONG_PTR)StackLimit;
    CurrentThread->LargeStack = TRUE;

    /* Adjust SpBase in the PCR */
    Pcr = (PKIPCR)KeGetPcr();
    if (Pcr != NULL)
    {
        Pcr->Prcb.SpBase = (PVOID)((ULONG_PTR)Pcr->Prcb.SpBase + StackOffset);
    }

    /*
     * Physically adjust SP and FP (X29) to the new stack.
     *
     * Both SP and X29 must be adjusted because:
     * - SP: the hardware stack pointer must point to the new stack
     * - X29 (FP): the frame pointer is used by the compiler to access
     *   local variables and function parameters via [X29, #offset].
     *   Without adjusting FP, all frame-pointer-relative accesses in
     *   the ENTIRE call chain above us would access the old (freed) stack.
     *
     * The saved frame pointer chain was relocated above so each caller restores
     * a frame pointer in the new stack as this function unwinds.
     */
    __asm__ __volatile__(
        "mov x16, sp\n\t"
        "add x16, x16, %0\n\t"
        "mov sp, x16\n\t"
        "add x29, x29, %0\n\t"
        : : "r"(StackOffset) : "x16", "memory"
    );

    /* Restore DAIF to pre-switch state */
    __asm__ __volatile__("msr daif, %0" :: "r"(_savedDaif) : "memory");
    }

    return OldStackBase;
}

/*
 * Forward declarations for ARM64 kernel initialization functions
 */
VOID
NTAPI
KiInitializePcr(
    _In_ ULONG ProcessorNumber,
    _Inout_ PKIPCR Pcr,
    _In_ PKTHREAD IdleThread,
    _In_ BOOLEAN SetCurrentPcr,
    _In_opt_ PVOID PanicStack,
    _In_ PVOID DpcStack);

VOID
NTAPI
KiInitializeSystem(
    _Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);

/*
 * ARM64 AP (Application Processor) info structure for SMP boot.
 * This mirrors the x86 APINFO structure but with ARM64-specific fields.
 */
typedef struct _ARM64_APINFO
{
    DECLSPEC_ALIGN(PAGE_SIZE) UCHAR IdtData[PAGE_SIZE];  /* Reserved for future IDT-like use */
    KIPCR Pcr;
    ETHREAD Thread;
} ARM64_APINFO, *PARM64_APINFO;

CODE_SEG("INIT")
VOID
NTAPI
KeStartAllProcessors(
    VOID)
{
#ifndef CONFIG_SMP
    return;
#else
    PVOID KernelStack;
    PVOID DPCStack;
    PARM64_APINFO APInfo;
    ULONG ProcessorCount;
    ULONG MaximumProcessors;

    {
        extern BOOLEAN SmpDbgEnabled;
        if (KeLoaderBlock != NULL && KeLoaderBlock->LoadOptions != NULL &&
            strstr(KeLoaderBlock->LoadOptions, "SMPDIAG") != NULL)
            SmpDbgEnabled = TRUE;
    }

    /*
     * ARM64 SMP Boot Implementation
     *
     * This function is called to start all secondary processors (APs).
     * For each AP, we:
     * 1. Allocate and initialize a PCR (Processor Control Region)
     * 2. Create kernel and DPC stacks
     * 3. Set up the processor state for initial entry
     * 4. Call HalStartNextProcessor to wake the AP via PSCI CPU_ON
     *
     * The HAL handles the actual PSCI interaction and trampoline setup.
     */

    {
        extern ULONG64 KiArm64BootSErrorDisr;
        if (KiArm64BootSErrorDisr != 0)
        {
            DPRINT1("[arm64] Boot-latched SError consumed at KeInitExceptions: DISR_EL1=0x%llx\n",
                    KiArm64BootSErrorDisr);
        }
    }

    /* Start with the system maximum processor count */
    MaximumProcessors = MAXIMUM_PROCESSORS;

    /*
     * Honor the boot-time processor-count limits parsed from the kernel command
     * line in ExpLoadInitialProcess (/NUMPROC=N, and /ONECPU which sets
     * KeNumprocSpecified = 1). KeBootprocSpecified caps boot-time starts the
     * same way. The loop below also stops naturally once the HAL reports no
     * more enabled MADT entries, so this only ever lowers the cap.
     */
    if ((KeNumprocSpecified != 0) && (KeNumprocSpecified < MaximumProcessors))
        MaximumProcessors = KeNumprocSpecified;
    if ((KeBootprocSpecified != 0) && (KeBootprocSpecified < MaximumProcessors))
        MaximumProcessors = KeBootprocSpecified;

    DPRINT1("[arm64] KeStartAllProcessors: Max=%lu (numproc=%lu bootproc=%lu)\n",
            MaximumProcessors, KeNumprocSpecified, KeBootprocSpecified);

    /* Start from processor 1 since BSP (processor 0) is already running */
    for (ProcessorCount = 1; ProcessorCount < MaximumProcessors; ++ProcessorCount)
    {
        PKIPCR CurrentPcr;

        KernelStack = NULL;
        DPCStack = NULL;
        APInfo = NULL;

        /* Allocate structures for a new CPU */
        APInfo = ExAllocatePoolZero(NonPagedPool, sizeof(*APInfo), TAG_KERNEL);
        if (!APInfo)
        {
            DPRINT1("[arm64] KeStartAllProcessors: Failed to allocate APInfo for CPU %lu\n",
                    ProcessorCount);
            break;
        }
        ASSERT(ALIGN_DOWN_POINTER_BY(APInfo, PAGE_SIZE) == APInfo);

        KernelStack = MmCreateKernelStack(FALSE, 0);
        if (!KernelStack)
        {
            DPRINT1("[arm64] KeStartAllProcessors: Failed to create kernel stack for CPU %lu\n",
                    ProcessorCount);
            break;
        }

        DPCStack = MmCreateKernelStack(FALSE, 0);
        if (!DPCStack)
        {
            DPRINT1("[arm64] KeStartAllProcessors: Failed to create DPC stack for CPU %lu\n",
                    ProcessorCount);
            break;
        }

        /* Initialize a new PCR for this AP */
        CurrentPcr = KeGetPcr();
        KiInitializePcr(ProcessorCount,
                        &APInfo->Pcr,
                        (PKTHREAD)&APInfo->Thread,
                        FALSE,
                        NULL,   /* ARM64 doesn't use separate panic stack here */
                        DPCStack);
        ASSERT(KeGetPcr() == CurrentPcr);

        /* Set up processor state for AP initialization */
        {
            PKPROCESSOR_STATE ProcessorState = &APInfo->Pcr.Prcb.ProcessorState;
            RtlZeroMemory(ProcessorState, sizeof(*ProcessorState));

            /*
             * For ARM64, we need to set up the context frame with:
             * - Pc: Entry point (KiInitializeSystem or similar)
             * - Sp: Kernel stack pointer
             * - X0: First argument (LoaderBlock)
             *
             * The HAL trampoline will:
             * 1. Enable MMU with proper page tables
             * 2. Set up stack and registers from this context
             * 3. Jump to the kernel entry point
             */
            ProcessorState->ContextFrame.Pc = (DWORD64)KiInitializeSystem;
            ProcessorState->ContextFrame.Sp = (DWORD64)ALIGN_DOWN_BY((ULONG_PTR)KernelStack - PAGE_SIZE, 16);

            /* Store ARM64 system registers if needed */
            ProcessorState->ArchState.Ttbr0_El1 = 0; /* HAL will use BSP's value */
            ProcessorState->ArchState.Ttbr1_El1 = 0; /* HAL will use BSP's value */

            /* Update LoaderBlock for this processor */
            KeLoaderBlock->KernelStack = (ULONG_PTR)KernelStack;
            KeLoaderBlock->Prcb = (ULONG_PTR)&APInfo->Pcr.Prcb;
            KeLoaderBlock->Thread = (ULONG_PTR)APInfo->Pcr.Prcb.IdleThread;

            /*
             * ARM64-specific: Update Arm64Block with AP's PCR and stacks.
             * KiInitializeSystem uses Arm64Block->PcrPage to find the PCR.
             * Without this, the AP would use the BSP's PcrPage and corrupt it.
             */
            KeLoaderBlock->u.Arm64.PcrPage = (ULONG_PTR)&APInfo->Pcr;
            KeLoaderBlock->u.Arm64.PanicStack = 0;
            KeLoaderBlock->u.Arm64.InterruptStack = (ULONG_PTR)DPCStack;

            DPRINT("[arm64] KeStartAllProcessors: Attempting to start CPU %lu\n",
                    ProcessorCount);

            /* Call HAL to start the processor */
            if (!HalStartNextProcessor(KeLoaderBlock, ProcessorState))
            {
                DPRINT1("[arm64] KeStartAllProcessors: HalStartNextProcessor failed for CPU %lu\n",
                        ProcessorCount);
                break;
            }

            /* Wait for AP to signal it has started (with timeout) */
            {
                volatile ULONG ApWait;
                for (ApWait = 0; ApWait < 10000000; ApWait++)
                {
                    if (KeLoaderBlock->Prcb == 0)
                        break;
                    KeMemoryBarrier();
                    YieldProcessor();
                }
                if (KeLoaderBlock->Prcb != 0)
                {
                    DPRINT1("[arm64] KeStartAllProcessors: CPU %lu AP handshake timeout (Prcb still set)\n",
                            ProcessorCount);
                    KeLoaderBlock->Prcb = 0; /* Reset for next attempt */
                    APInfo = NULL;
                    KernelStack = NULL;
                    DPCStack = NULL;
                    break;
                }
            }

            DPRINT1("[arm64] KeStartAllProcessors: CPU %lu started successfully\n",
                    ProcessorCount);

            APInfo = NULL;
            KernelStack = NULL;
            DPCStack = NULL;
        }
    }

    /* Clean up if last attempt failed */
    ProcessorCount--;

    if (APInfo)
        ExFreePoolWithTag(APInfo, TAG_KERNEL);
    if (KernelStack)
        MmDeleteKernelStack(KernelStack, FALSE);
    if (DPCStack)
        MmDeleteKernelStack(DPCStack, FALSE);

    DPRINT1("[arm64] KeStartAllProcessors: Successfully started %lu APs\n", ProcessorCount);

    {
        extern KSCHEDULER_SUBNODE KiNode0SubNode;
        ULONG HighestCpu;
        KiNode0SubNode.Affinity = KeActiveProcessors;
        KiNode0SubNode.SiblingMask = KeActiveProcessors;
        KiNode0SubNode.NonParkedSet = KeActiveProcessors;
        KiNode0SubNode.Lowest = 0;
        if (BitScanReverseAffinity(&HighestCpu, KeActiveProcessors))
            KiNode0SubNode.Highest = HighestCpu;
    }

    /*
     * Update process affinities now that all CPUs are online.
     *
     * The System process (PsInitialSystemProcess) and Idle process
     * (PsIdleProcess) were created during Phase 0 init when only CPU 0
     * was active, so their Pcb.Affinity == 0x1.  All threads created in
     * these processes inherit that single-CPU affinity, which means the
     * scheduler never dispatches work to secondary CPUs.
     *
     * Fix: set their affinity to KeActiveProcessors and update all
     * existing threads so the scheduler can use every online CPU.
     */
    if (KeActiveProcessors != 0 && KeNumberProcessors > 1)
    {
        KAFFINITY FullAffinity = KeActiveProcessors;
        PLIST_ENTRY Entry;
        PKTHREAD Thread;

        DPRINT1("[arm64] KeStartAllProcessors: Updating process affinities to 0x%Ix\n",
                FullAffinity);

        /* Update idle process */
        if (PsIdleProcess != NULL)
        {
            PsIdleProcess->Pcb.Affinity = FullAffinity;
        }

        /* Update system process and all its threads */
        if (PsInitialSystemProcess != NULL)
        {
            KLOCK_QUEUE_HANDLE LockHandle;
            KAFFINITY OldAffinity;

            KiAcquireProcessLockRaiseToSynch(&PsInitialSystemProcess->Pcb, &LockHandle);

            OldAffinity = PsInitialSystemProcess->Pcb.Affinity;
            PsInitialSystemProcess->Pcb.Affinity = FullAffinity;

            /* Walk all threads in the System process and update their affinity */
            for (Entry = PsInitialSystemProcess->Pcb.ThreadListHead.Flink;
                 Entry != &PsInitialSystemProcess->Pcb.ThreadListHead;
                 Entry = Entry->Flink)
            {
                Thread = CONTAINING_RECORD(Entry, KTHREAD, ThreadListEntry);
                if (Thread->Affinity == OldAffinity)
                {
                    Thread->Affinity = FullAffinity;
                    Thread->UserAffinity = FullAffinity;
                }
            }

            KiReleaseProcessLock(&LockHandle);

            DPRINT1("[arm64] KeStartAllProcessors: System process affinity 0x%Ix -> 0x%Ix\n",
                    OldAffinity, FullAffinity);
        }
    }
#endif
}
