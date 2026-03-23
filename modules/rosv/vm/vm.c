/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VM instance management
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>
#include <rosv/loader.h>
#include <rosv/device.h>
#include <rosv/pty.h>
#include <rosv/net_backend.h>

static volatile LONG g_NextVmId = 0;

/* ---- VM Create ---------------------------------------------------------- */

NTSTATUS
RosvVmCreate(
    _In_ PROSV_VM_CONFIG Config,
    _Out_ PROSV_VM *Vm)
{
    PROSV_VM NewVm;
    NTSTATUS Status;

    ROSV_TRACE("RosvVmCreate: RamSizeMB=%u", Config->RamSizeMB);

    *Vm = NULL;

    /* Validate configuration */
    if (Config->RamSizeMB < 64)
    {
        ROSV_ERR("RosvVmCreate: RamSizeMB=%u is too small (minimum 64 MB)",
                 Config->RamSizeMB);
        return STATUS_INVALID_PARAMETER;
    }

    if (Config->RamSizeMB > 4096)
    {
        ROSV_ERR("RosvVmCreate: RamSizeMB=%u exceeds maximum (4096 MB)",
                 Config->RamSizeMB);
        return STATUS_INVALID_PARAMETER;
    }

    /* Allocate VM structure */
    NewVm = (PROSV_VM)ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(ROSV_VM),
        ROSV_DRIVER_TAG);

    if (NewVm == NULL)
    {
        ROSV_ERR("RosvVmCreate: Failed to allocate ROSV_VM (%u bytes)",
                 (ULONG)sizeof(ROSV_VM));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewVm, sizeof(ROSV_VM));

    /* Initialize basic fields */
    NewVm->VmId = (ULONG)InterlockedIncrement(&g_NextVmId);
    NewVm->State = RosvVmStateCreated;
    NewVm->Config = *Config;
    KeInitializeSpinLock(&NewVm->Lock);
    KeInitializeEvent(&NewVm->StopEvent, NotificationEvent, FALSE);
    ExInitializeRundownProtection(&NewVm->RundownRef);

    /*
     * Initialize IOAPIC state.
     *
     * Firmware reset state keeps redirection entries masked until the OS
     * programs vectors and unmasks selected lines. Injecting IRQ0 on the
     * default vector before Linux installs handlers causes spurious
     * "__common_interrupt: No irq handler for vector" storms.
     */
    NewVm->IoApic.Id = 1;
    {
        ULONG i;
        for (i = 0; i < ROSV_IOAPIC_REDIRECTION_ENTRIES; i++)
        {
            UCHAR Vector = (UCHAR)(0x20 + (i & 0x0F));
            NewVm->IoApic.RedirectionLow[i] = (ULONG)Vector | (1U << 16); /* masked fixed delivery */
            NewVm->IoApic.RedirectionHigh[i] = 0; /* destination APIC ID 0 */
        }

    }

    /* Initialize LAPIC architectural reset state used by MMIO emulation. */
    NewVm->Lapic.Id = 0;
    NewVm->Lapic.Version = 0x00050014;   /* version 0x14, MaxLVT=5 */
    NewVm->Lapic.ApicBaseMsr = 0x00000000FEE00000ULL | (1ULL << 11) | (1ULL << 8);
    NewVm->Lapic.Dfr = 0xFFFFFFFF;       /* flat mode */
    NewVm->Lapic.Svr = 0x000000FF;       /* APIC software-disabled after reset */
    NewVm->Lapic.LvtTimer = (1U << 16);  /* masked after reset */
    NewVm->Lapic.TimerInitialCount = 0;
    NewVm->Lapic.TimerCurrentCount = 0;
    NewVm->Lapic.TimerDivideConfig = 0;
    NewVm->Lapic.TimerStartQpc = 0;
    NewVm->Lapic.TimerDeliveredPeriods = 0;

    /* Initialize vCPU */
    Status = RosvVcpuInitialize(&NewVm->Vcpu, NewVm);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvVmCreate: RosvVcpuInitialize failed, Status=0x%08X", Status);
        ExFreePoolWithTag(NewVm, ROSV_DRIVER_TAG);
        return Status;
    }

    /* Initialize console */
    Status = RosvConsoleInitialize(&NewVm->Console);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvVmCreate: RosvConsoleInitialize failed, Status=0x%08X", Status);
        RosvVcpuDestroy(&NewVm->Vcpu);
        ExFreePoolWithTag(NewVm, ROSV_DRIVER_TAG);
        return Status;
    }

    NewVm->Console->OwnerVm = NewVm;
    RosvRtl8139SetOwnerVm(&NewVm->Console->Rtl8139, NewVm);

    /* Initialize PTY manager */
    Status = RosvPtyManagerInitialize(&NewVm->PtyManager);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvVmCreate: RosvPtyManagerInitialize failed, Status=0x%08X", Status);
        RosvConsoleDestroy(NewVm->Console);
        RosvVcpuDestroy(&NewVm->Vcpu);
        ExFreePoolWithTag(NewVm, ROSV_DRIVER_TAG);
        return Status;
    }

    if (Config->NetBackendType == RosvNetBackendNetio)
    {
        Status = RosvNetBackendCreate(NewVm);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("RosvVmCreate: RosvNetBackendCreate failed, Status=0x%08X", Status);
            RosvPtyManagerDestroy(&NewVm->PtyManager);
            RosvConsoleDestroy(NewVm->Console);
            RosvVcpuDestroy(&NewVm->Vcpu);
            ExFreePoolWithTag(NewVm, ROSV_DRIVER_TAG);
            return Status;
        }
    }
    else
    {
        ROSV_TRACE("RosvVmCreate: net backend=%u, skipping in-kernel netio", Config->NetBackendType);
        NewVm->NetBackend = NULL;
    }

    RosvVmSetCheckpoint(NewVm, RosvCpVmCreated);

    *Vm = NewVm;
    ROSV_TRACE("RosvVmCreate: VM created at %p, VmId=%u, RamSizeMB=%u",
               NewVm, NewVm->VmId, Config->RamSizeMB);

    return STATUS_SUCCESS;
}

/* ---- VM Set Memory ------------------------------------------------------ */

NTSTATUS
RosvVmSetMemory(
    _Inout_ PROSV_VM Vm)
{
    NTSTATUS Status;
    ULONG64 SizeInBytes;

    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    ROSV_TRACE("RosvVmSetMemory: VmId=%u, RamSizeMB=%u",
               Vm->VmId, Vm->Config.RamSizeMB);

    SizeInBytes = (ULONG64)Vm->Config.RamSizeMB * 1024 * 1024;

    Status = RosvMemoryAllocateGuestRam(Vm, SizeInBytes);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvVmSetMemory: RosvMemoryAllocateGuestRam failed, Status=0x%08X",
                 Status);
        return Status;
    }

    Vm->State = RosvVmStateMemorySet;
    RosvVmSetCheckpoint(Vm, RosvCpMemorySet);

    ROSV_TRACE("RosvVmSetMemory: Memory allocated - %u MB (%llu bytes), regions=%u",
               Vm->Config.RamSizeMB, SizeInBytes, Vm->MemoryRegionCount);

    return STATUS_SUCCESS;
}

/* ---- VM Start ----------------------------------------------------------- */

NTSTATUS
RosvVmStart(
    _Inout_ PROSV_VM Vm)
{
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;

    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    ROSV_TRACE("RosvVmStart: VmId=%u, state=%d", Vm->VmId, Vm->State);

    if (Vm->State < RosvVmStateKernelLoaded)
    {
        ROSV_ERR("RosvVmStart: Invalid state %d (need >= KernelLoaded)", Vm->State);
        return STATUS_INVALID_DEVICE_STATE;
    }

    /*
     * boot_params and guest entry state are set up inside the vCPU thread
     * (after VMCS is loaded via VMPTRLD). RosvKernelLoad already built
     * boot_params with the correct setup_header; RosvGuestEntrySetup reads
     * it from guest memory to determine 32/64-bit mode.
     */

    /* Reset stop event */
    KeClearEvent(&Vm->StopEvent);

    if (Vm->NetBackend != NULL)
    {
        Status = RosvNetBackendStart(Vm);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("RosvVmStart: RosvNetBackendStart failed, Status=0x%08X", Status);
            return Status;
        }
    }

    /* Create vCPU thread */
    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

    Status = PsCreateSystemThread(
        &Vm->Vcpu.ThreadHandle,
        THREAD_ALL_ACCESS,
        &ObjectAttributes,
        NULL,
        NULL,
        RosvVcpuThreadProc,
        Vm);

    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvVmStart: PsCreateSystemThread failed, Status=0x%08X", Status);
        RosvNetBackendStop(Vm);
        return Status;
    }

    /* Get thread object for waiting */
    Status = ObReferenceObjectByHandle(
        Vm->Vcpu.ThreadHandle,
        THREAD_ALL_ACCESS,
        NULL,
        KernelMode,
        (PVOID *)&Vm->Vcpu.ThreadObject,
        NULL);

    if (!NT_SUCCESS(Status))
    {
        ROSV_WARN("RosvVmStart: ObReferenceObjectByHandle failed (Status=0x%08X), "
                  "continuing with handle-based wait path", Status);
        Vm->Vcpu.ThreadObject = NULL;
    }

    Vm->State = RosvVmStateRunning;
    RosvVmSetCheckpoint(Vm, RosvCpVmLaunched);

    ROSV_TRACE("RosvVmStart: VM started, vCPU thread created");

    return STATUS_SUCCESS;
}

/* ---- VM Stop ------------------------------------------------------------ */

NTSTATUS
RosvVmStop(
    _Inout_ PROSV_VM Vm)
{
    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    ROSV_TRACE("RosvVmStop: VmId=%u, state=%d, exitCount=%llu",
               Vm->VmId, Vm->State, Vm->Vcpu.ExitCount);

    if (Vm->State != RosvVmStateRunning)
    {
        ROSV_WARN("RosvVmStop: VM not running (state=%d)", Vm->State);
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* Signal the vCPU thread to stop */
    Vm->Vcpu.Running = FALSE;

    /* Wait for the vCPU thread to exit */
    if (Vm->Vcpu.ThreadObject != NULL)
    {
        ROSV_TRACE("RosvVmStop: Waiting for vCPU thread to exit...");
        KeWaitForSingleObject(
            Vm->Vcpu.ThreadObject,
            Executive,
            KernelMode,
            FALSE,
            NULL);

        ObDereferenceObject(Vm->Vcpu.ThreadObject);
        Vm->Vcpu.ThreadObject = NULL;
    }
    else if (Vm->Vcpu.ThreadHandle != NULL)
    {
        PETHREAD ThreadObject = NULL;
        NTSTATUS RefStatus;

        ROSV_TRACE("RosvVmStop: ThreadObject missing, resolving from thread handle...");
        RefStatus = ObReferenceObjectByHandle(
            Vm->Vcpu.ThreadHandle,
            THREAD_ALL_ACCESS,
            NULL,
            KernelMode,
            (PVOID *)&ThreadObject,
            NULL);
        if (NT_SUCCESS(RefStatus))
        {
            KeWaitForSingleObject(
                ThreadObject,
                Executive,
                KernelMode,
                FALSE,
                NULL);
            ObDereferenceObject(ThreadObject);
        }
        else
        {
            ROSV_WARN("RosvVmStop: cannot resolve thread object from handle, Status=0x%08X",
                      RefStatus);
        }
    }

    if (Vm->Vcpu.ThreadHandle != NULL)
    {
        ZwClose(Vm->Vcpu.ThreadHandle);
        Vm->Vcpu.ThreadHandle = NULL;
    }

    /* Stop console */
    if (Vm->Console != NULL)
    {
        ROSV_TRACE("RosvVmStop: Stopping console");
    }

    RosvNetBackendStop(Vm);

    Vm->State = RosvVmStateStopped;

    ROSV_TRACE("RosvVmStop: VM stopped, total exits=%llu, lastReason=%u",
               Vm->Vcpu.ExitCount, Vm->Vcpu.LastExitReason);
    RosvVmDumpCheckpoints(Vm);

    return STATUS_SUCCESS;
}

/* ---- VM Destroy --------------------------------------------------------- */

VOID
RosvVmDestroy(
    _Inout_ PROSV_VM Vm)
{
    if (Vm == NULL)
    {
        ROSV_WARN("RosvVmDestroy: called with NULL Vm");
        return;
    }

    ROSV_TRACE("RosvVmDestroy: VmId=%u, state=%d", Vm->VmId, Vm->State);

    /* Stop VM if still running */
    if (Vm->State == RosvVmStateRunning)
    {
        ROSV_TRACE("RosvVmDestroy: VM still running, stopping first");
        RosvVmStop(Vm);
    }

    /* Destroy PTY manager */
    RosvPtyManagerDestroy(&Vm->PtyManager);

    RosvNetBackendDestroy(Vm);

    /* Destroy console */
    if (Vm->Console != NULL)
    {
        RosvConsoleDestroy(Vm->Console);
        Vm->Console = NULL;
    }

    /* Free MSR bitmap */
    if (Vm->MsrBitmap != NULL)
    {
        MmFreeContiguousMemory(Vm->MsrBitmap);
        Vm->MsrBitmap = NULL;
    }

    /* Free guest memory and EPT */
    RosvMemoryFreeGuestRam(Vm);

    /* Destroy vCPU */
    RosvVcpuDestroy(&Vm->Vcpu);

    ROSV_TRACE("RosvVmDestroy: VM %u destroyed", Vm->VmId);

    /* Free VM structure */
    ExFreePoolWithTag(Vm, ROSV_DRIVER_TAG);
}

BOOLEAN
RosvVmAcquireReference(
    _Inout_ PROSV_VM Vm)
{
    if (Vm == NULL)
        return FALSE;

    return ExAcquireRundownProtection(&Vm->RundownRef);
}

VOID
RosvVmReleaseReference(
    _Inout_ PROSV_VM Vm)
{
    if (Vm == NULL)
        return;

    ExReleaseRundownProtection(&Vm->RundownRef);
}

VOID
RosvVmWaitForReferencesReleased(
    _Inout_ PROSV_VM Vm)
{
    if (Vm == NULL)
        return;

    ExWaitForRundownProtectionRelease(&Vm->RundownRef);
}

/* ---- VM Get State ------------------------------------------------------- */

ROSV_VM_STATE
RosvVmGetState(
    _In_ PROSV_VM Vm)
{
    ROSV_TRACE("RosvVmGetState: VmId=%u, state=%d", Vm->VmId, Vm->State);
    return Vm->State;
}
