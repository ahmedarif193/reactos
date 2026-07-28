/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     vCPU run loop and thread
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>
#include <rosv/vmx.h>
#include <rosv/ept.h>
#include <rosv/loader.h>
#include <rosv/device.h>
#include <intrin.h>

/* Virtualized guest/host MSRs swapped on VM-entry/exit. */
#define IA32_KERNEL_GS_BASE     0xC0000102
#define IA32_STAR               0xC0000081
#define IA32_LSTAR              0xC0000082
#define IA32_CSTAR              0xC0000083
#define IA32_FMASK              0xC0000084
#define ROSV_EXIT_TICK_PERIOD_MS 4

typedef struct DECLSPEC_ALIGN(16) _ROSV_FX_STATE {
    UCHAR Bytes[512];
} ROSV_FX_STATE, *PROSV_FX_STATE;

FORCEINLINE
VOID
RosvFxSave(
    _Out_ PROSV_FX_STATE State)
{
    __asm__ __volatile__("fxsave64 %0" : "=m"(*State) : : "memory");
}

FORCEINLINE
VOID
RosvFxRestore(
    _In_ const ROSV_FX_STATE *State)
{
    __asm__ __volatile__("fxrstor64 %0" : : "m"(*State) : "memory");
}

static
VOID
RosvSaveHostFxAndLoadGuestFx(
    _Inout_ PROSV_VCPU Vcpu)
{
    /* TODO(xstate): switch to XSAVE/XRSTOR when guest XSAVE exposure is enabled. */
    RosvFxSave((PROSV_FX_STATE)Vcpu->HostFxState);
    RosvFxRestore((const ROSV_FX_STATE *)Vcpu->GuestFxState);
}

static
VOID
RosvSaveGuestFxAndRestoreHostFx(
    _Inout_ PROSV_VCPU Vcpu)
{
    RosvFxSave((PROSV_FX_STATE)Vcpu->GuestFxState);
    RosvFxRestore((const ROSV_FX_STATE *)Vcpu->HostFxState);
}

static
VOID
RosvVcpuQueueTimerTicks(
    _Inout_ PROSV_VCPU Vcpu)
{
    ROSV_ASSERT(Vcpu != NULL, "Vcpu must not be NULL");
    if (Vcpu == NULL)
        return;

    /*
     * LAPIC timer delivery is modeled from guest-programmed LAPIC registers
     * in the VM-exit path. Keep the legacy host-wall-clock synthetic queue
     * disabled to avoid injecting timer vectors the guest did not arm.
     */
    Vcpu->PendingTimerTicks = 0;
}

static
VOID
RosvVcpuExitTickDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PROSV_VCPU Vcpu = (PROSV_VCPU)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Vcpu != NULL && Vcpu->Running)
    {
        Vcpu->ExitTickDpcCount++;
        /* Wake the vCPU thread if it is sleeping in HLT yield.
         * KeSetEvent at DISPATCH_LEVEL is safe and wakes a PASSIVE waiter. */
        KeSetEvent(&Vcpu->HaltWakeEvent, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * MSR autoload/store list indices.  Must match the order entries are
 * populated in RosvMsrListSetup().
 */
#define MSR_IDX_KERNEL_GS_BASE  0
#define MSR_IDX_STAR            1
#define MSR_IDX_LSTAR           2
#define MSR_IDX_CSTAR           3
#define MSR_IDX_FMASK           4

static
VOID
RosvMsrListPopulateEntry(
    _Out_ PVMX_MSR_ENTRY Entry,
    _In_ ULONG MsrIndex,
    _In_ ULONG64 Value)
{
    Entry->MsrIndex = MsrIndex;
    Entry->Reserved = 0;
    Entry->MsrValue = Value;
}

/*
 * Allocate one physically-contiguous page and carve it into three MSR lists:
 *   [0..4]   = VM-exit store list (guest MSRs saved by CPU on exit)
 *   [5..9]   = VM-exit load list  (host MSRs loaded by CPU on exit)
 *   [10..14] = VM-entry load list (guest MSRs loaded by CPU on entry)
 *
 * Wire the physical addresses and counts into the active VMCS.
 */
static
NTSTATUS
RosvMsrListSetup(
    _Inout_ PROSV_VCPU Vcpu)
{
    PHYSICAL_ADDRESS MaxAddr;
    PVMX_MSR_ENTRY Base;
    PHYSICAL_ADDRESS Phys;
    ULONG i;

    static const ULONG MsrIndices[ROSV_MSR_LIST_COUNT] = {
        IA32_KERNEL_GS_BASE,
        IA32_STAR,
        IA32_LSTAR,
        IA32_CSTAR,
        IA32_FMASK
    };

    MaxAddr.QuadPart = 0xFFFFFFFFLL;
    Base = (PVMX_MSR_ENTRY)MmAllocateContiguousMemory(PAGE_SIZE, MaxAddr);
    if (Base == NULL)
    {
        ROSV_ERR("RosvMsrListSetup: failed to allocate MSR list page");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Base, PAGE_SIZE);
    Phys = MmGetPhysicalAddress(Base);

    Vcpu->MsrListBase = Base;
    Vcpu->MsrListPhys = Phys;
    Vcpu->ExitStoreList = &Base[0];
    Vcpu->ExitLoadList  = &Base[ROSV_MSR_LIST_COUNT];
    Vcpu->EntryLoadList = &Base[2 * ROSV_MSR_LIST_COUNT];

    /* Populate all three lists with the same MSR indices. */
    for (i = 0; i < ROSV_MSR_LIST_COUNT; i++)
    {
        /* VM-exit store: CPU writes guest values here on exit.
         * Seed with current guest values so the first exit has sane data. */
        RosvMsrListPopulateEntry(&Vcpu->ExitStoreList[i], MsrIndices[i], 0);

        /* VM-exit load: CPU loads host values from here on exit. */
        RosvMsrListPopulateEntry(&Vcpu->ExitLoadList[i], MsrIndices[i], 0);

        /* VM-entry load: CPU loads guest values from here on entry. */
        RosvMsrListPopulateEntry(&Vcpu->EntryLoadList[i], MsrIndices[i], 0);
    }

    /* Fill host MSR values from captured Vcpu fields. */
    Vcpu->ExitLoadList[MSR_IDX_KERNEL_GS_BASE].MsrValue = Vcpu->HostKernelGsBase;
    Vcpu->ExitLoadList[MSR_IDX_STAR].MsrValue            = Vcpu->HostStar;
    Vcpu->ExitLoadList[MSR_IDX_LSTAR].MsrValue           = Vcpu->HostLstar;
    Vcpu->ExitLoadList[MSR_IDX_CSTAR].MsrValue           = Vcpu->HostCstar;
    Vcpu->ExitLoadList[MSR_IDX_FMASK].MsrValue           = Vcpu->HostFmask;

    /* Fill guest MSR values from Vcpu fields (all zero at init). */
    Vcpu->EntryLoadList[MSR_IDX_KERNEL_GS_BASE].MsrValue = Vcpu->GuestKernelGsBase;
    Vcpu->EntryLoadList[MSR_IDX_STAR].MsrValue            = Vcpu->GuestStar;
    Vcpu->EntryLoadList[MSR_IDX_LSTAR].MsrValue           = Vcpu->GuestLstar;
    Vcpu->EntryLoadList[MSR_IDX_CSTAR].MsrValue           = Vcpu->GuestCstar;
    Vcpu->EntryLoadList[MSR_IDX_FMASK].MsrValue           = Vcpu->GuestFmask;

    /* Wire into VMCS: physical addresses and counts. */
    {
        ULONG64 ExitStorePhys = Phys.QuadPart;
        ULONG64 ExitLoadPhys  = Phys.QuadPart + (ROSV_MSR_LIST_COUNT * sizeof(VMX_MSR_ENTRY));
        ULONG64 EntryLoadPhys = Phys.QuadPart + (2 * ROSV_MSR_LIST_COUNT * sizeof(VMX_MSR_ENTRY));

        RosvVmcsWrite(VMCS_CTRL_EXIT_MSR_STORE, ExitStorePhys);
        RosvVmcsWrite(VMCS_CTRL_EXIT_MSR_STORE_COUNT, ROSV_MSR_LIST_COUNT);

        RosvVmcsWrite(VMCS_CTRL_EXIT_MSR_LOAD, ExitLoadPhys);
        RosvVmcsWrite(VMCS_CTRL_EXIT_MSR_LOAD_COUNT, ROSV_MSR_LIST_COUNT);

        RosvVmcsWrite(VMCS_CTRL_ENTRY_MSR_LOAD, EntryLoadPhys);
        RosvVmcsWrite(VMCS_CTRL_ENTRY_MSR_LOAD_COUNT, ROSV_MSR_LIST_COUNT);

        ROSV_TRACE("RosvMsrListSetup: MSR autoload lists configured (%u MSRs each)", ROSV_MSR_LIST_COUNT);
        ROSV_TRACE("  exit-store PA=0x%llX, exit-load PA=0x%llX, entry-load PA=0x%llX",
                   ExitStorePhys, ExitLoadPhys, EntryLoadPhys);
    }

    return STATUS_SUCCESS;
}

static
VOID
RosvMsrListTeardown(
    _Inout_ PROSV_VCPU Vcpu)
{
    if (Vcpu->MsrListBase != NULL)
    {
        ROSV_TRACE("RosvMsrListTeardown: freeing MSR list page at %p", Vcpu->MsrListBase);
        MmFreeContiguousMemory(Vcpu->MsrListBase);
        Vcpu->MsrListBase = NULL;
        Vcpu->ExitStoreList = NULL;
        Vcpu->ExitLoadList = NULL;
        Vcpu->EntryLoadList = NULL;
    }
}

/*
 * After VM-exit, the CPU has written the guest MSR values into the
 * exit-store list.  Propagate them to:
 *   1) The entry-load list — so VM-entry restores the exact guest MSR
 *      state.  This is critical for SWAPGS: the guest swaps GS_BASE
 *      and KERNEL_GS_BASE without WRMSR, so the WRMSR exit handler
 *      never updates the entry-load list for those changes.
 *   2) The Vcpu shadow fields — so RDMSR/WRMSR exit handlers see
 *      current values.
 */
FORCEINLINE
VOID
RosvMsrListSyncGuestFromExitStore(
    _Inout_ PROSV_VCPU Vcpu)
{
    ULONG i;

    /* Copy exit-store → entry-load so VM-entry reloads correct guest MSRs. */
    for (i = 0; i < ROSV_MSR_LIST_COUNT; i++)
    {
        Vcpu->EntryLoadList[i].MsrValue = Vcpu->ExitStoreList[i].MsrValue;
    }

    /* Sync to shadow fields for exit handler visibility. */
    Vcpu->GuestKernelGsBase = Vcpu->ExitStoreList[MSR_IDX_KERNEL_GS_BASE].MsrValue;
    Vcpu->GuestStar         = Vcpu->ExitStoreList[MSR_IDX_STAR].MsrValue;
    Vcpu->GuestLstar        = Vcpu->ExitStoreList[MSR_IDX_LSTAR].MsrValue;
    Vcpu->GuestCstar        = Vcpu->ExitStoreList[MSR_IDX_CSTAR].MsrValue;
    Vcpu->GuestFmask        = Vcpu->ExitStoreList[MSR_IDX_FMASK].MsrValue;
}

/* ---- vCPU Initialize ---------------------------------------------------- */

NTSTATUS
RosvVcpuInitialize(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    ROSV_TRACE("RosvVcpuInitialize: Vcpu=%p, Vm=%p (VmId=%u)",
               Vcpu, Vm, Vm->VmId);

    RtlZeroMemory(&Vcpu->GuestRegs, sizeof(ROSV_GUEST_REGS));
    RtlZeroMemory(&Vcpu->ExitRing, sizeof(ROSV_EXIT_RING));

    Vcpu->VcpuId = 0;
    Vcpu->Vm = Vm;
    Vcpu->Running = FALSE;
    Vcpu->Launched = FALSE;
    Vcpu->ExitCount = 0;
    Vcpu->LastExitReason = 0;
    Vcpu->LastCheckpoint = RosvCpVmCreated;
    Vcpu->PreemptTimerReload = 0;
    Vcpu->TimerTickIntervalQpc = 0;
    Vcpu->PendingTimerTicks = 0;
    KeInitializeTimerEx(&Vcpu->ExitTickTimer, SynchronizationTimer);
    KeInitializeDpc(&Vcpu->ExitTickDpc, RosvVcpuExitTickDpc, Vcpu);
    KeSetTargetProcessorDpc(&Vcpu->ExitTickDpc, 0);
    Vcpu->ExitTickArmed = FALSE;
    Vcpu->ExitTickDpcCount = 0;
    KeInitializeEvent(&Vcpu->HaltWakeEvent, SynchronizationEvent, FALSE);
    Vcpu->LastHltQpc.QuadPart = 0;
    Vcpu->GuestKernelGsBase = 0;
    Vcpu->GuestStar = 0;
    Vcpu->GuestLstar = 0;
    Vcpu->GuestCstar = 0;
    Vcpu->GuestFmask = 0;
    Vcpu->GuestTscAux = 0;
    Vcpu->HostKernelGsBase = 0;
    Vcpu->HostStar = 0;
    Vcpu->HostLstar = 0;
    Vcpu->HostCstar = 0;
    Vcpu->HostFmask = 0;
    Vcpu->MsrListBase = NULL;
    Vcpu->MsrListPhys.QuadPart = 0;
    Vcpu->ExitStoreList = NULL;
    Vcpu->ExitLoadList = NULL;
    Vcpu->EntryLoadList = NULL;
    RtlZeroMemory(Vcpu->HostFxState, sizeof(Vcpu->HostFxState));
    RtlZeroMemory(Vcpu->GuestFxState, sizeof(Vcpu->GuestFxState));
    Vcpu->GuestFxInitialized = FALSE;
    Vcpu->ThreadHandle = NULL;
    Vcpu->ThreadObject = NULL;
    Vcpu->VmxonRegion = NULL;
    Vcpu->VmcsRegion = NULL;

    ROSV_TRACE("RosvVcpuInitialize: vCPU initialized");

    return STATUS_SUCCESS;
}

/* ---- vCPU Destroy ------------------------------------------------------- */

VOID
RosvVcpuDestroy(
    _Inout_ PROSV_VCPU Vcpu)
{
    ROSV_TRACE("RosvVcpuDestroy: Vcpu=%p, exitCount=%llu",
               Vcpu, Vcpu->ExitCount);

    if (Vcpu->Running)
    {
        ROSV_WARN("RosvVcpuDestroy: vCPU still marked as running!");
    }

    RosvMsrListTeardown(Vcpu);

    /* Thread handles should already be cleaned up by RosvVmStop */
    Vcpu->Vm = NULL;

    ROSV_TRACE("RosvVcpuDestroy: vCPU destroyed");
}

/* ---- vCPU Start (legacy, not used directly -- vm.c calls thread create) - */

NTSTATUS
RosvVcpuStart(
    _Inout_ PROSV_VCPU Vcpu)
{
    ROSV_TRACE("RosvVcpuStart: Vcpu=%p (use RosvVmStart instead)", Vcpu);
    return STATUS_SUCCESS;
}

/* ---- vCPU Stop ---------------------------------------------------------- */

NTSTATUS
RosvVcpuStop(
    _Inout_ PROSV_VCPU Vcpu)
{
    ROSV_TRACE("RosvVcpuStop: Vcpu=%p, setting Running=FALSE", Vcpu);
    Vcpu->Running = FALSE;
    return STATUS_SUCCESS;
}

/* ---- vCPU Thread -------------------------------------------------------- */

VOID
RosvVcpuThreadProc(
    _In_ PVOID Context)
{
    PROSV_VM Vm;
    PROSV_VCPU Vcpu;
    BOOLEAN AffinityPinned = FALSE;
    NTSTATUS Status;
    ULONG ExitReason;
    ULONG64 ExitQualification;
    ULONG64 GuestRip;
    ULONG InstructionLength;
    BOOLEAN Continue;


    Vm = (PROSV_VM)Context;
    if (Vm == NULL)
    {
        ROSV_ERR("vCPU thread: NULL context");
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);
        return;
    }

    Vcpu = &Vm->Vcpu;

    ROSV_TRACE("vCPU thread started, VmId=%u", Vm->VmId);

    /* Pin to CPU 0 to avoid VMX migration complexity */
    KeSetSystemAffinityThread(1);
    AffinityPinned = TRUE;
    ROSV_TRACE("vCPU thread pinned to CPU 0");

    /* ---- Step 1: Enable VMX (VMXON) ------------------------------------ */

    Status = RosvVmxDetectCapabilities(&Vcpu->VmxCaps);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: RosvVmxDetectCapabilities failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto Exit;
    }

    Status = RosvVmxEnable(
        &Vcpu->VmxCaps,
        &Vcpu->VmxonRegionPhys,
        &Vcpu->VmxonRegion);

    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: RosvVmxEnable (VMXON) failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto Exit;
    }

    ROSV_TRACE("vCPU thread: VMX enabled (VMXON), VmxonRegion=%p PhysAddr=0x%llX",
               Vcpu->VmxonRegion, Vcpu->VmxonRegionPhys.QuadPart);

    /* ---- Step 2: Setup VMCS -------------------------------------------- */

    /* Allocate VMCS region */
    Status = RosvVmcsAlloc(
        Vcpu->VmxCaps.VmcsRevisionId,
        &Vcpu->VmcsRegionPhys,
        &Vcpu->VmcsRegion);

    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: RosvVmcsAlloc failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto DisableVmx;
    }

    ROSV_TRACE("vCPU thread: VMCS allocated at %p, PhysAddr=0x%llX",
               Vcpu->VmcsRegion, Vcpu->VmcsRegionPhys.QuadPart);

    /* VMCLEAR */
    Status = RosvVmcsClear(Vcpu->VmcsRegionPhys);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: VMCLEAR failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto FreeVmcs;
    }

    ROSV_TRACE("vCPU thread: VMCLEAR done");

    /* VMPTRLD */
    Status = RosvVmcsLoad(Vcpu->VmcsRegionPhys);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: VMPTRLD failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto FreeVmcs;
    }

    ROSV_TRACE("vCPU thread: VMPTRLD done, VMCS is now active");

    /* Configure host state */
    Status = RosvVmcsSetupHost(Vcpu);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: RosvVmcsSetupHost failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto ClearVmcs;
    }

    ROSV_TRACE("vCPU thread: Host state configured");

    /* Capture host MSRs that must be restored after every VM-exit. */
    Vcpu->HostKernelGsBase = __readmsr(IA32_KERNEL_GS_BASE);
    Vcpu->HostStar = __readmsr(IA32_STAR);
    Vcpu->HostLstar = __readmsr(IA32_LSTAR);
    Vcpu->HostCstar = __readmsr(IA32_CSTAR);
    Vcpu->HostFmask = __readmsr(IA32_FMASK);
    RosvFxSave((PROSV_FX_STATE)Vcpu->HostFxState);
    RtlCopyMemory(Vcpu->GuestFxState, Vcpu->HostFxState, sizeof(Vcpu->GuestFxState));
    Vcpu->GuestFxInitialized = TRUE;
    /* Reset x87 control + MXCSR to architectural defaults for guest userspace. */
    *(PUSHORT)&Vcpu->GuestFxState[0] = 0x037F;
    *(PULONG)&Vcpu->GuestFxState[24] = 0x00001F80;
    ROSV_TRACE("vCPU thread: Captured host syscall/GS MSRs");

    /* Configure guest state (reads boot_params from guest memory for 32/64-bit detection) */
    Status = RosvGuestEntrySetup(Vcpu, Vm);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: RosvGuestEntrySetup failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto ClearVmcs;
    }

    ROSV_TRACE("vCPU thread: Guest state configured (%s mode)",
               Vm->Is64BitKernel ? "64-bit" : "32-bit");

    /* Write a marker to VGA buffer (GPA 0xB8000) to verify EPT mapping */
    {
        PVOID VgaMarker = RosvMemoryGpaToHva(Vm, 0xB8000);
        if (VgaMarker)
        {
            /* Write "ROSV" as VGA chars: 'R' 0x07 'O' 0x07 'S' 0x07 'V' 0x07 */
            UCHAR MarkerData[] = {'R',0x07, 'O',0x07, 'S',0x07, 'V',0x07};
            RtlCopyMemory(VgaMarker, MarkerData, sizeof(MarkerData));
            ROSV_TRACE("vCPU thread: Wrote VGA marker at GPA 0xB8000");
        }
    }

    /* Guest RAM mappings stay pinned for runtime I/O stability. */

    /* Configure VM-execution, VM-exit, and VM-entry controls */
    Status = RosvVmcsSetupControls(&Vcpu->VmxCaps, Vm);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: RosvVmcsSetupControls failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto ClearVmcs;
    }

    ROSV_TRACE("vCPU thread: VM-execution controls configured");

    /* Set up VMX MSR autoload/store lists (replaces manual RDMSR/WRMSR).
     * Must come after RosvVmcsSetupControls which sets counts to 0, and
     * after host MSRs are captured into Vcpu fields. */
    Status = RosvMsrListSetup(Vcpu);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("vCPU thread: RosvMsrListSetup failed, Status=0x%08X", Status);
        Vm->State = RosvVmStateError;
        goto ClearVmcs;
    }

    RosvVmSetCheckpoint(Vm, RosvCpVmcsConfigured);

    /* ---- Step 3: Run loop ---------------------------------------------- */

    Vcpu->Running = TRUE;
    Vcpu->Launched = FALSE;

    /* Initialize host-wall-clock timer for ~250Hz guest timer injection */
    Vcpu->LastTimerHostTick = KeQueryPerformanceCounter(&Vcpu->TimerHostFrequency);
    Vcpu->LastHltQpc = Vcpu->LastTimerHostTick;
    ROSV_ASSERT(Vcpu->TimerHostFrequency.QuadPart > 0,
                "KeQueryPerformanceCounter returned non-positive frequency");
    if (Vcpu->TimerHostFrequency.QuadPart <= 0)
    {
        Vcpu->TimerTickIntervalQpc = 1;
    }
    else
    {
        Vcpu->TimerTickIntervalQpc =
            (ULONG64)(Vcpu->TimerHostFrequency.QuadPart / ROSV_TIMER_TICK_HZ);
    }
    if (Vcpu->TimerTickIntervalQpc == 0)
        Vcpu->TimerTickIntervalQpc = 1;
    Vcpu->PendingTimerTicks = 0;
    Vcpu->StatLastReportQpc = Vcpu->LastTimerHostTick;

    {
        LARGE_INTEGER DueTime;
        /*
         * TODO(vmx-timer): Remove this host tick fallback after nested VMX
         * preemption-timer behavior is proven stable on all target hosts.
         */
        DueTime.QuadPart = -(LONGLONG)(ROSV_EXIT_TICK_PERIOD_MS * 10 * 1000);
        ROSV_ASSERT(DueTime.QuadPart < 0, "Exit tick due time must be relative-negative");
        KeSetTargetProcessorDpc(&Vcpu->ExitTickDpc, 0);
        KeSetTimerEx(&Vcpu->ExitTickTimer,
                     DueTime,
                     ROSV_EXIT_TICK_PERIOD_MS,
                     &Vcpu->ExitTickDpc);
        Vcpu->ExitTickArmed = TRUE;
        ROSV_TRACE("vCPU thread: Host exit tick timer armed (%u ms)",
                   ROSV_EXIT_TICK_PERIOD_MS);
    }

    /* Flush EPT TLB before first VM-entry (INVEPT requires VMX to be active) */
    RosvAsmInvept(INVEPT_SINGLE_CONTEXT, Vm->EptContext.Eptp.Value);

    ROSV_TRACE("vCPU thread: Entering run loop");

    while (Vcpu->Running)
    {
        /* Enter the guest via assembly stub.
         * First entry uses VMLAUNCH, subsequent entries use VMRESUME.
         * After a successful VM-entry + VM-exit, the VMCS is in "launched"
         * state and VMLAUNCH would fail with error 4 (non-clear VMCS). */
        if (Vcpu->PreemptTimerReload != 0)
        {
            /*
             * Keep VMX preemption timer armed on every VM-entry.
             * This guarantees a periodic VM-exit source even if the previous
             * expiration raced with another exit reason.
             */
            RosvVmcsWrite(VMCS_GUEST_PREEMPT_TIMER, Vcpu->PreemptTimerReload);
        }

        /*
         * CLI before VM-entry: prevent host interrupts from arriving between
         * the last instruction before VMLAUNCH/VMRESUME and the VM-entry itself.
         * Without this, every pending host interrupt causes an immediate
         * EXTERNAL_INT VM-exit, starving the guest.
         */
        __asm__ __volatile__("cli" ::: "memory");

        ROSV_ASSERT(Vcpu->GuestFxInitialized, "Guest FX state must be initialized before VM-entry");
        /* MSR save/restore is handled by VMX autoload lists (no manual RDMSR/WRMSR). */
        RosvSaveHostFxAndLoadGuestFx(Vcpu);
        if (!Vcpu->Launched)
            RosvAsmVmRun(Vcpu);
        else
            RosvAsmVmRunResume(Vcpu);
        RosvSaveGuestFxAndRestoreHostFx(Vcpu);
        /*
         * VM-exit returns with host IF cleared. Re-enable host interrupts so
         * pending host ISRs fire naturally (no ACK_INT_ON_EXIT: the interrupt
         * that caused the EXTERNAL_INT exit is still in the IRR and will be
         * delivered through the host IDT once we STI).
         */
        __asm__ __volatile__("sti" ::: "memory");

        /*
         * We are back from VM-exit. The assembly stub saved guest GPRs
         * into Vcpu->GuestRegs. The VMX MSR exit-store list has captured
         * the guest MSR values automatically. Sync them into Vcpu fields
         * so exit handlers can read/modify them.
         */
        RosvMsrListSyncGuestFromExitStore(Vcpu);
        Vcpu->ExitCount++;
        Vcpu->Launched = TRUE;

        /* Read exit reason from VMCS */
        ExitReason = (ULONG)RosvVmcsRead(VMCS_RO_EXIT_REASON);
        Vcpu->LastExitReason = ExitReason & 0xFFFF; /* Low 16 bits = basic exit reason */
        if (Vcpu->LastExitReason == VMX_EXIT_EXTERNAL_INT)
        {
            /*
             * External interrupt exits can be very frequent. Avoid extra VMREAD
             * traffic in this path; qualification/instruction metadata is not
             * consumed for dispatch correctness.
             */
            ExitQualification = 0;
            GuestRip = 0;
            InstructionLength = 0;
        }
        else
        {
            ExitQualification = RosvVmcsRead(VMCS_RO_EXIT_QUALIFICATION);
            GuestRip = RosvVmcsRead(VMCS_GUEST_RIP);
            InstructionLength = (ULONG)RosvVmcsRead(VMCS_RO_EXIT_INSTR_LENGTH);
        }

        /* Per-exit-type counter */
        switch (Vcpu->LastExitReason)
        {
            case VMX_EXIT_HLT:         Vcpu->StatExitHlt++; break;
            case VMX_EXIT_PREEMPT_TIMER: Vcpu->StatExitPreempt++; break;
            case VMX_EXIT_EPT_VIOLATION: Vcpu->StatExitEpt++; break;
            case VMX_EXIT_IO:          Vcpu->StatExitIo++; break;
            case VMX_EXIT_RDMSR:
            case VMX_EXIT_WRMSR:       Vcpu->StatExitMsr++; break;
            case VMX_EXIT_EXTERNAL_INT: Vcpu->StatExitExtInt++; break;
            case VMX_EXIT_INT_WINDOW:  Vcpu->StatExitIntWin++; break;
            default:                   Vcpu->StatExitOther++; break;
        }

        /* First exit checkpoint */
        if (Vcpu->ExitCount == 1)
        {
            ROSV_TRACE("vCPU thread: First VM-exit! reason=%u qual=0x%llX rip=0x%llX",
                       Vcpu->LastExitReason, ExitQualification, GuestRip);
            RosvVmSetCheckpoint(Vm, RosvCpFirstExit);
        }

        /* Check for VM-entry failure (bit 31 set in exit reason) */
        if (ExitReason & (1U << 31))
        {
            ROSV_ERR("vCPU thread: VM-ENTRY FAILURE! reason=0x%08X qual=0x%llX",
                     ExitReason, ExitQualification);
            RosvVmxDumpVmcs();
            RosvVmxDumpFailure();
            Vm->State = RosvVmStateError;
            Vcpu->Running = FALSE;
            break;
        }

        if (Vcpu->LastExitReason != VMX_EXIT_EXTERNAL_INT)
        {
            /* Log to ring buffer */
            RosvExitLogRecord(
                &Vcpu->ExitRing,
                Vcpu->LastExitReason,
                ExitQualification,
                GuestRip,
                InstructionLength);

            /* Queue timer ticks from host wall-clock so guest time doesn't stall on exit-heavy workloads. */
            RosvVcpuQueueTimerTicks(Vcpu);
        }

        /* Dispatch the exit to the handler */
        Continue = RosvVmExitDispatch(Vcpu,
                                      Vcpu->LastExitReason,
                                      ExitQualification,
                                      GuestRip,
                                      InstructionLength);
        if (!Continue)
        {
            ROSV_TRACE("vCPU thread: Exit handler requested stop, reason=%u rip=0x%llX",
                       Vcpu->LastExitReason, GuestRip);
            Vcpu->Running = FALSE;
            if (Vm->State == RosvVmStateRunning)
                Vm->State = RosvVmStateStopped;
        }
        else
        {
            if (Vcpu->LastExitReason != VMX_EXIT_EXTERNAL_INT)
            {
                RosvVmTryInjectPendingInterrupts(Vcpu, Vm);
            }
        }

        /*
         * Periodic stats dump (~every 1 second of host wall-clock).
         */
        {
            LARGE_INTEGER StatsNow = KeQueryPerformanceCounter(NULL);
            ULONG64 StatsElapsed = (ULONG64)(StatsNow.QuadPart - Vcpu->StatLastReportQpc.QuadPart);
            ULONG64 StatsThreshold = (ULONG64)(Vcpu->TimerHostFrequency.QuadPart);

            if (StatsElapsed >= StatsThreshold && StatsThreshold > 0)
            {
                static ULONG64 PrevExitCount;
                static ULONG64 PrevHlt, PrevPreempt, PrevEpt, PrevIo, PrevMsr, PrevExtInt, PrevIntWin, PrevOther;
                static ULONG64 PrevTimer, PrevHltYield, PrevSpinYield;

                ULONG64 dExit   = Vcpu->ExitCount - PrevExitCount;
                ULONG64 dHlt    = Vcpu->StatExitHlt - PrevHlt;
                ULONG64 dPreempt= Vcpu->StatExitPreempt - PrevPreempt;
                ULONG64 dEpt    = Vcpu->StatExitEpt - PrevEpt;
                ULONG64 dIo     = Vcpu->StatExitIo - PrevIo;
                ULONG64 dMsr    = Vcpu->StatExitMsr - PrevMsr;
                ULONG64 dExtInt = Vcpu->StatExitExtInt - PrevExtInt;
                ULONG64 dIntWin = Vcpu->StatExitIntWin - PrevIntWin;
                ULONG64 dOther  = Vcpu->StatExitOther - PrevOther;
                ULONG64 dTimer  = Vcpu->StatTimerInjected - PrevTimer;
                ULONG64 dHltY   = Vcpu->StatHltYield - PrevHltYield;
                ULONG64 dSpinY  = Vcpu->StatSpinYield - PrevSpinYield;

                DbgPrint("[ROSV:STATS] exits/s=%llu hlt=%llu preempt=%llu ept=%llu io=%llu "
                         "msr=%llu extint=%llu intwin=%llu other=%llu timer_inj=%llu "
                         "hlt_yield=%llu spin_yield=%llu\n",
                         dExit, dHlt, dPreempt, dEpt, dIo, dMsr, dExtInt, dIntWin, dOther,
                         dTimer, dHltY, dSpinY);

                PrevExitCount = Vcpu->ExitCount;
                PrevHlt       = Vcpu->StatExitHlt;
                PrevPreempt   = Vcpu->StatExitPreempt;
                PrevEpt       = Vcpu->StatExitEpt;
                PrevIo        = Vcpu->StatExitIo;
                PrevMsr       = Vcpu->StatExitMsr;
                PrevExtInt    = Vcpu->StatExitExtInt;
                PrevIntWin    = Vcpu->StatExitIntWin;
                PrevOther     = Vcpu->StatExitOther;
                PrevTimer     = Vcpu->StatTimerInjected;
                PrevHltYield  = Vcpu->StatHltYield;
                PrevSpinYield = Vcpu->StatSpinYield;
                Vcpu->StatLastReportQpc = StatsNow;
            }
        }

        /*
         * VMX preemption timer is one-shot.
         *
         * Re-arm on VMX_EXIT_PREEMPT_TIMER as usual. Also re-arm if the timer
         * value is observed as zero on any other exit reason (priority race:
         * another exit can win over PREEMPT_TIMER at expiration time).
         * Without this guard, periodic host-side exit guarantees can stop.
         */
        if (Vcpu->PreemptTimerReload != 0)
        {
            ULONG64 PreemptValue = RosvVmcsRead(VMCS_GUEST_PREEMPT_TIMER);

            if (Vcpu->LastExitReason == VMX_EXIT_PREEMPT_TIMER ||
                PreemptValue == 0)
            {
                RosvVmcsWrite(VMCS_GUEST_PREEMPT_TIMER, Vcpu->PreemptTimerReload);
            }
        }

        /*
         * Safety-net: detect guest spin (no HLT for >500ms wall-clock).
         *
         * If the guest hasn't executed HLT in 500ms of real time, it is
         * likely stuck in a polling loop (e.g., a serial driver spin).
         * Yield the host CPU briefly (1ms) to avoid pinning a host core
         * at 100%.  This is a backstop -- the root cause should be fixed
         * in the device emulation layer.
         */
        if (Vcpu->LastExitReason != VMX_EXIT_HLT &&
            Vcpu->LastExitReason != VMX_EXIT_EXTERNAL_INT)
        {
            LARGE_INTEGER Now, Freq;
            ULONG64 ElapsedQpc;
            ULONG64 ThresholdQpc;

            Now = KeQueryPerformanceCounter(&Freq);
            ElapsedQpc = (ULONG64)(Now.QuadPart - Vcpu->LastHltQpc.QuadPart);
            /* 500ms threshold = Freq/2.
             * This is a last-resort safety net for pathological polling loops,
             * not a general scheduler for host-side network work. Yielding after
             * only a couple of milliseconds throttles normal kernel boot. */
            ThresholdQpc = (ULONG64)(Freq.QuadPart >> 1);

            if (ElapsedQpc > ThresholdQpc)
            {
                static ULONG SpinYieldCount;
                LARGE_INTEGER YieldTimeout;

                Vcpu->StatSpinYield++;
                SpinYieldCount++;
                if (SpinYieldCount <= 3 || (SpinYieldCount % 65536) == 0)
                {
                    ROSV_TRACE("vCPU yield #%u: guest busy >500ms "
                               "(reason=%u rip=0x%llX elapsed=%llu thresh=%llu)",
                               SpinYieldCount,
                               Vcpu->LastExitReason,
                               RosvVmcsRead(VMCS_GUEST_RIP),
                               ElapsedQpc,
                               ThresholdQpc);
                }

                /* Yield host CPU until new RX work appears (RxPendingEvent)
                 * or 1ms timeout. Using an event instead of fixed sleep lets
                 * the vCPU wake immediately when there's network work,
                 * eliminating the ~15ms timer-tick sleep overhead. */
                YieldTimeout.QuadPart = -(LONGLONG)(1 * 1000 * 10); /* 1ms */
                KeWaitForSingleObject(&Vm->VirtioNet.RxPendingEvent,
                                      Executive, KernelMode, FALSE, &YieldTimeout);

                /* Reset the timer so we don't yield every single exit */
                Vcpu->LastHltQpc = KeQueryPerformanceCounter(&Freq);
            }
        }

    }

    ROSV_TRACE("vCPU thread: Run loop ended, total exits=%llu", Vcpu->ExitCount);

    /* ---- Step 4: Teardown ---------------------------------------------- */

    if (Vcpu->ExitTickArmed)
    {
        ROSV_TRACE("vCPU thread: Disarming host exit tick timer (dpc_count=%llu)",
                   Vcpu->ExitTickDpcCount);
        KeCancelTimer(&Vcpu->ExitTickTimer);
        if (AffinityPinned)
        {
            KeRevertToUserAffinityThread();
            AffinityPinned = FALSE;
            ROSV_TRACE("vCPU thread: Reverted CPU affinity before DPC flush");
        }
        KeFlushQueuedDpcs();
        Vcpu->ExitTickArmed = FALSE;
    }

    /* Free MSR autoload lists before VMCLEAR (VMCS references them). */
    RosvMsrListTeardown(Vcpu);

ClearVmcs:
    ROSV_TRACE("vCPU thread: Clearing VMCS");
    RosvVmcsClear(Vcpu->VmcsRegionPhys);

FreeVmcs:
    if (Vcpu->VmcsRegion != NULL)
    {
        ROSV_TRACE("vCPU thread: Freeing VMCS region");
        RosvVmcsFree(Vcpu->VmcsRegionPhys, Vcpu->VmcsRegion);
        Vcpu->VmcsRegion = NULL;
    }

DisableVmx:
    ROSV_TRACE("vCPU thread: Disabling VMX (VMXOFF)");
    RosvVmxDisable(Vcpu->VmxonRegionPhys, Vcpu->VmxonRegion);
    Vcpu->VmxonRegion = NULL;

Exit:
    if (AffinityPinned)
    {
        KeRevertToUserAffinityThread();
        AffinityPinned = FALSE;
        ROSV_TRACE("vCPU thread: Reverted CPU affinity");
    }

    ROSV_TRACE("vCPU thread: Exiting, total exits=%llu, lastReason=%u",
               Vcpu->ExitCount, Vcpu->LastExitReason);

    PsTerminateSystemThread(STATUS_SUCCESS);
}
