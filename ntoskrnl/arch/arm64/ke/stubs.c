/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/stubs.c
 * PURPOSE:         Minimal ARM64 kernel support stubs required for linking
 */

#include <ntoskrnl.h>
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

#define ARM64_STUB() UNIMPLEMENTED_DBGBREAK()

/* TODO(ARM64): Replace these stub implementations with real logic once the
 * debugger, user-mode callbacks, and memory manager are fully brought up. */

const ULONG_PTR MmProtectToPteMask[32] =
{
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE,
    PTE_EXECUTE             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE,
    PTE_READWRITE           | PTE_ENABLE_CACHE,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE,
    0,
    PTE_READONLY            | PTE_DISABLE_CACHE,
    PTE_EXECUTE             | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_DISABLE_CACHE,
    PTE_READWRITE           | PTE_DISABLE_CACHE,
    PTE_WRITECOPY           | PTE_DISABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_DISABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_DISABLE_CACHE,
    0,
    PTE_READONLY            | PTE_ENABLE_CACHE,
    PTE_EXECUTE             | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READ        | PTE_ENABLE_CACHE,
    PTE_READWRITE           | PTE_ENABLE_CACHE,
    PTE_WRITECOPY           | PTE_ENABLE_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_ENABLE_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_ENABLE_CACHE,
    0,
    PTE_READONLY            | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE             | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_READ        | PTE_WRITECOMBINED_CACHE,
    PTE_READWRITE           | PTE_WRITECOMBINED_CACHE,
    PTE_WRITECOPY           | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_READWRITE   | PTE_WRITECOMBINED_CACHE,
    PTE_EXECUTE_WRITECOPY   | PTE_WRITECOMBINED_CACHE,
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
 * - Table descriptors (L0/L1/L2 pointers) require bits[1:0] = 0b11 and AF=0.
 *   In our abstract HARDWARE_PTE, this corresponds to Valid=1 and
 *   NotLargePage=1, with Accessed cleared.
 * - Page descriptors (leaf L3 entries) also use bits[1:0] = 0b11 with AF=1.
 *   We model that by setting Valid=1, NotLargePage=1 and Accessed=1.
 *
 * The previous stub values only set Valid/Accessed, which made table
 * descriptors look like invalid/leaf entries to the hardware. That caused
 * early faults when the kernel tried to touch the self-mapped page tables.
 */
MMPTE ValidKernelPte = {
    .u.Hard = {
        .Valid = 1,
        .NotLargePage = 1,   /* ensure type==table/page (0b11) */
        .Accessed = 1,       /* AF=1 for leaf PTEs */
        .Writable = 1,
        .Owner = 0,
    }
};
/* Ensure leaf PTEs default to Normal WB (MAIR index 4) */
__attribute__((constructor))
static void KeArm64InitValidKernelPte(void)
{
    /* Bits [4:2] are AttrIndx. Use 0b100 (index 4) to match loader MAIR. */
    ValidKernelPte.u.Long |= ((ULONGLONG)4ULL << ARM64_PTE_CACHE_SHIFT);
    /* Make default leaf mappings Inner Shareable to avoid alias issues */
    ValidKernelPte.u.Long |= (3ULL << 8);
    ValidKernelPteLocal.u.Long |= ((ULONGLONG)4ULL << ARM64_PTE_CACHE_SHIFT);
    ValidKernelPteLocal.u.Long |= (3ULL << 8);
}
MMPDE ValidKernelPde = {
    .u.Hard = {
        .Valid = 1,
        .NotLargePage = 1,   /* AF must be 0 for table entries */
        .Accessed = 0,
    }
};
MMPTE DemandZeroPte = {.u.Long = (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS)};
MMPDE DemandZeroPde = {.u.Long = (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS)};
MMPTE PrototypePte = {.u.Long = (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS) | PTE_PROTOTYPE | (MI_PTE_LOOKUP_NEEDED << PAGE_SHIFT)};
MMPTE ValidKernelPteLocal = {
    .u.Hard = {
        .Valid = 1,
        .NotLargePage = 1,
        .Accessed = 1,
        .Writable = 1,
        .Owner = 0
    }
};
MMPDE ValidKernelPdeLocal = {.u.Hard.Valid = 1, .u.Hard.Accessed = 1};

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

    OldPc = TrapFrame->Pc;
    TrapFrame->Pc = (ULONG64)(ULONG_PTR)KeRaiseUserExceptionDispatcher;
    return (NTSTATUS)OldPc;
}

VOID
NTAPI
RtlGetCallersAddress(
    _Out_opt_ PVOID *CallersAddress,
    _Out_opt_ PVOID *CallersCaller)
{
    if (CallersAddress != NULL)
    {
        *CallersAddress = NULL;
    }

    if (CallersCaller != NULL)
    {
        *CallersCaller = NULL;
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
     * - PDE_BASE points to the linear self-map of the page directory level;
     *   treating it as an array allows us to read current kernel PDE entries
     *   without walking the hierarchy. Each entry is 8 bytes and there are
     *   PDE_PER_PAGE (512) entries per PDE page on ARM64.
     * - We skip the PTE_BASE and HYPER_SPACE slots: those are self-map and
     *   hyperspace PDEs managed elsewhere (and can have special semantics).
     * - We never overwrite a non-zero MmGlobalKernelPageDirectory entry: if
     *   something pre-seeded a slot, we keep that, mirroring i386/ARM logic.
     * - Concurrency: runs during early MmInitSystem; single-threaded.
     */
    /* Current kernel PDE page (self-mapped view) */
    PULONG_PTR CurrentPde = (PULONG_PTR)PDE_BASE;
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
}

PVOID
NTAPI
KeSwitchKernelStack(
    _In_ PVOID StackBase,
    _In_ PVOID StackLimit)
{
    UNREFERENCED_PARAMETER(StackBase);
    UNREFERENCED_PARAMETER(StackLimit);
    ARM64_STUB();
    return StackBase;
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
    PVOID KernelStack;
    PVOID DPCStack;
    PARM64_APINFO APInfo;
    ULONG ProcessorCount;
    ULONG MaximumProcessors;

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

    /* Start with the system maximum processor count */
    MaximumProcessors = MAXIMUM_PROCESSORS;

    /*
     * TODO: Limit processors based on command line options when available.
     * For now, we use the compiled-in maximum. Command-line processor
     * limiting would require kernel command-line parsing integration:
     * - /NUMPROC=N: Maximum number of processors to use
     * - /ONECPU: Use only one processor (equivalent to /NUMPROC=1)
     *
     * These would set KeNumprocSpecified and KeBootprocSpecified.
     */

    DPRINT1("[arm64] KeStartAllProcessors: Max=%lu\n", MaximumProcessors);

    /* Start from processor 1 since BSP (processor 0) is already running */
    for (ProcessorCount = 1; ProcessorCount < MaximumProcessors; ++ProcessorCount)
    {
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
        KiInitializePcr(ProcessorCount,
                        &APInfo->Pcr,
                        (PKTHREAD)&APInfo->Thread,
                        NULL,   /* ARM64 doesn't use separate panic stack here */
                        DPCStack);

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
            ProcessorState->ContextFrame.Sp = (DWORD64)KernelStack;

            /* Store ARM64 system registers if needed */
            ProcessorState->ArchState.Ttbr0_El1 = 0; /* HAL will use BSP's value */
            ProcessorState->ArchState.Ttbr1_El1 = 0; /* HAL will use BSP's value */

            /* Update LoaderBlock for this processor */
            KeLoaderBlock->KernelStack = (ULONG_PTR)KernelStack;
            KeLoaderBlock->Prcb = (ULONG_PTR)&APInfo->Pcr.Prcb;
            KeLoaderBlock->Thread = (ULONG_PTR)APInfo->Pcr.Prcb.IdleThread;

            DPRINT1("[arm64] KeStartAllProcessors: Attempting to start CPU %lu\n",
                    ProcessorCount);

            /* Call HAL to start the processor */
            if (!HalStartNextProcessor(KeLoaderBlock, ProcessorState))
            {
                DPRINT1("[arm64] KeStartAllProcessors: HalStartNextProcessor failed for CPU %lu\n",
                        ProcessorCount);
                break;
            }

            /* Wait for AP to signal it has started */
            while (KeLoaderBlock->Prcb != 0)
            {
                KeMemoryBarrier();
                YieldProcessor();
            }

            DPRINT1("[arm64] KeStartAllProcessors: CPU %lu started successfully\n",
                    ProcessorCount);
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
}
