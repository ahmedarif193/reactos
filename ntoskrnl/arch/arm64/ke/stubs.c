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
MMPTE ValidKernelPteLocal = {.u.Hard.Valid = 1, .u.Hard.Accessed = 1, .u.Hard.Owner = 0};
MMPDE ValidKernelPdeLocal = {.u.Hard.Valid = 1, .u.Hard.Accessed = 1};
MMPTE MmDecommittedPte = {.u.Long = (MM_READWRITE << MM_PTE_SOFTWARE_PROTECTION_BITS)};

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

    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        return;
    }

    __asm__ __volatile__("brk #0xf000" ::: "memory");
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

CODE_SEG("INIT")
VOID
NTAPI
KeStartAllProcessors(
    VOID)
{
    DPRINT1("ARM64 TODO: KeStartAllProcessors is a stub\n");
}
