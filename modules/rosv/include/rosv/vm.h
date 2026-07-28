/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VM instance and vCPU structures
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#pragma once

#include <rosv/rosv.h>
#include <rosv/vmx.h>
#include <rosv/ept.h>
#include <rosv/pty.h>
#include <rosv/virtio_blk.h>
#include <rosv/virtio_net.h>
#include <rosv/virtio_console.h>
#include <rosv/vhdx.h>

/* ---- Guest general-purpose register context ----------------------------- */

typedef struct _ROSV_GUEST_REGS {
    ULONG64 Rax;
    ULONG64 Rcx;
    ULONG64 Rdx;
    ULONG64 Rbx;
    ULONG64 Rsp;   /* Saved/restored via VMCS, not push/pop */
    ULONG64 Rbp;
    ULONG64 Rsi;
    ULONG64 Rdi;
    ULONG64 R8;
    ULONG64 R9;
    ULONG64 R10;
    ULONG64 R11;
    ULONG64 R12;
    ULONG64 R13;
    ULONG64 R14;
    ULONG64 R15;
} ROSV_GUEST_REGS, *PROSV_GUEST_REGS;

/* ---- Exit ring buffer --------------------------------------------------- */

typedef struct _ROSV_EXIT_RING {
    ROSV_EXIT_LOG_ENTRY Entries[ROSV_EXIT_LOG_SIZE];
    ULONG Head;             /* Next write index */
    ULONG Count;            /* Total entries written (may exceed buffer size) */
} ROSV_EXIT_RING, *PROSV_EXIT_RING;

/* ---- VMX MSR autoload/store entry (Intel SDM Vol 3, 25.7.2) ------------- */

#define ROSV_MSR_LIST_COUNT     5   /* KERNEL_GS_BASE, STAR, LSTAR, CSTAR, FMASK */

typedef struct DECLSPEC_ALIGN(16) _VMX_MSR_ENTRY {
    ULONG32 MsrIndex;
    ULONG32 Reserved;
    ULONG64 MsrValue;
} VMX_MSR_ENTRY, *PVMX_MSR_ENTRY;

C_ASSERT(sizeof(VMX_MSR_ENTRY) == 16);

/* ---- vCPU structure ----------------------------------------------------- */

struct _ROSV_VCPU {
    /* Identity */
    ULONG VcpuId;
    PROSV_VM Vm;            /* Parent VM */

    /* VMX regions (4KB-aligned, physically contiguous) */
    PVOID VmxonRegion;
    PHYSICAL_ADDRESS VmxonRegionPhys;
    PVOID VmcsRegion;
    PHYSICAL_ADDRESS VmcsRegionPhys;

    /* Guest register state (saved/restored around VM-entry/exit) */
    ROSV_GUEST_REGS GuestRegs;

    /* VMX capabilities (cached per-CPU) */
    VMX_CAPABILITIES VmxCaps;

    /* vCPU thread */
    HANDLE ThreadHandle;
    PKTHREAD ThreadObject;
    BOOLEAN Running;
    BOOLEAN Launched;       /* TRUE after first VMLAUNCH (use VMRESUME after) */

    /* Host state save area */
    ULONG64 HostCr0;
    ULONG64 HostCr3;
    ULONG64 HostCr4;
    ULONG64 HostRsp;

    /* Statistics */
    ULONG64 ExitCount;
    ULONG LastExitReason;
    ROSV_CHECKPOINT LastCheckpoint;

    /* Per-exit-type counters for periodic stats */
    ULONG64 StatExitHlt;
    ULONG64 StatExitPreempt;
    ULONG64 StatExitEpt;
    ULONG64 StatExitIo;
    ULONG64 StatExitMsr;
    ULONG64 StatExitExtInt;
    ULONG64 StatExitIntWin;
    ULONG64 StatExitOther;
    ULONG64 StatTimerInjected;
    ULONG64 StatHltYield;
    ULONG64 StatSpinYield;
    ULONG64 StatHltTicks;       /* QPC ticks spent sleeping in HLT yield */
    ULONG64 StatTotalTicks;     /* QPC ticks total (wall clock) */
    LARGE_INTEGER StatLastReportQpc;

    /* VMX preemption timer reload value (0 = disabled) */
    ULONG PreemptTimerReload;

    /* Host-wall-clock timer: inject guest timer ticks at ~250Hz real time */
    LARGE_INTEGER TimerHostFrequency;
    LARGE_INTEGER LastTimerHostTick;
    ULONG64 TimerTickIntervalQpc;
    ULONG PendingTimerTicks;
    KTIMER ExitTickTimer;
    KDPC ExitTickDpc;
    BOOLEAN ExitTickArmed;
    ULONG64 ExitTickDpcCount;

    /* HLT yield: signaled when the guest needs to wake from HLT sleep */
    KEVENT HaltWakeEvent;

    /*
     * QPC-based spin safety net: tracks the last time the guest executed
     * HLT.  If too much wall-clock time passes without a HLT, the run
     * loop yields briefly to prevent 100% host CPU.
     */
    LARGE_INTEGER LastHltQpc;

    /* Virtualized guest MSRs that are required while running x64 guest code.
     * These values are swapped into hardware on VM-entry and restored on exit. */
    ULONG64 GuestKernelGsBase;
    ULONG64 GuestStar;
    ULONG64 GuestLstar;
    ULONG64 GuestCstar;
    ULONG64 GuestFmask;
    ULONG64 GuestTscAux;

    /* Host copies restored after each VM-exit. */
    ULONG64 HostKernelGsBase;
    ULONG64 HostStar;
    ULONG64 HostLstar;
    ULONG64 HostCstar;
    ULONG64 HostFmask;

    /*
     * VMX MSR autoload/store lists.  The CPU automatically saves/loads these
     * MSRs during VM-exit/entry so we avoid 15 manual RDMSR/WRMSR per exit.
     *
     * Layout within the allocated page:
     *   [0 .. ROSV_MSR_LIST_COUNT-1]                   = VM-exit store list (guest save)
     *   [ROSV_MSR_LIST_COUNT .. 2*ROSV_MSR_LIST_COUNT-1] = VM-exit load list (host restore)
     *   [2*ROSV_MSR_LIST_COUNT .. 3*ROSV_MSR_LIST_COUNT-1] = VM-entry load list (guest load)
     */
    PVOID MsrListBase;                  /* Virtual address of allocated page */
    PHYSICAL_ADDRESS MsrListPhys;       /* Physical address of the page */
    PVMX_MSR_ENTRY ExitStoreList;       /* Guest MSRs saved on VM-exit */
    PVMX_MSR_ENTRY ExitLoadList;        /* Host MSRs loaded on VM-exit */
    PVMX_MSR_ENTRY EntryLoadList;       /* Guest MSRs loaded on VM-entry */

    /* Host/guest x87+SSE state snapshots (FXSAVE64 format). */
    DECLSPEC_ALIGN(16) UCHAR HostFxState[512];
    DECLSPEC_ALIGN(16) UCHAR GuestFxState[512];
    BOOLEAN GuestFxInitialized;

    /* Exit ring buffer */
    ROSV_EXIT_RING ExitRing;
};

C_ASSERT(FIELD_OFFSET(ROSV_VCPU, GuestRegs) == 0x30);

/* ---- Guest physical memory region --------------------------------------- */

typedef struct _ROSV_MEMORY_REGION {
    PVOID HostVirtualAddress;       /* Kernel virtual address */
    PHYSICAL_ADDRESS HostPhysicalAddress; /* Physical address */
    ULONG64 GuestPhysicalAddress;   /* Guest physical address */
    ULONG64 SizeInBytes;            /* Region size */
    PMDL Mdl;                       /* MDL for pinned pages */
} ROSV_MEMORY_REGION, *PROSV_MEMORY_REGION;

/* ---- I/O APIC state ----------------------------------------------------- */

#define ROSV_IOAPIC_REDIRECTION_ENTRIES 24

typedef struct _ROSV_IOAPIC_STATE {
    /* IOREGSEL latch (register-select index). */
    ULONG IoRegSel;
    /* IOAPIC ID field (4-bit APIC ID stored in bits 27:24). */
    ULONG Id;
    /* Redirection table low/high dwords for INTIN[0..23]. */
    ULONG RedirectionLow[ROSV_IOAPIC_REDIRECTION_ENTRIES];
    ULONG RedirectionHigh[ROSV_IOAPIC_REDIRECTION_ENTRIES];
} ROSV_IOAPIC_STATE, *PROSV_IOAPIC_STATE;

/* ---- Local APIC state --------------------------------------------------- */

typedef struct _ROSV_LAPIC_STATE {
    ULONG Id;
    ULONG Version;
    ULONG64 ApicBaseMsr;
    ULONG Tpr;
    ULONG Ldr;
    ULONG Dfr;
    ULONG Svr;
    ULONG LvtTimer;
    ULONG TimerInitialCount;
    ULONG TimerCurrentCount;
    ULONG TimerDivideConfig;
    ULONG64 TimerStartQpc;
    ULONG64 TimerDeliveredPeriods;
    ULONG ErrorStatus;
} ROSV_LAPIC_STATE, *PROSV_LAPIC_STATE;

/* ---- VM instance -------------------------------------------------------- */

#define ROSV_MAX_MEMORY_REGIONS     16
#define ROSV_CMDLINE_MAX            1024
#define ROSV_TIMER_TICK_HZ          250
#define ROSV_TIMER_MAX_BACKLOG      8
/*
 * Firmware table physical locations in guest memory.
 * Keep these centralized so memory/table publishing and Linux boot_params
 * wiring cannot drift apart.
 */
#define ROSV_MP_FLOATING_PTR_GPA    0x000F0000ULL
#define ROSV_MP_CONFIG_TABLE_GPA    0x000F0100ULL
#define ROSV_ACPI_RSDP_GPA          0x000F2000ULL
#define ROSV_ACPI_XSDT_GPA          0x000F2100ULL
#define ROSV_ACPI_RSDT_GPA          0x000F2200ULL
#define ROSV_ACPI_MADT_GPA          0x000F2300ULL
C_ASSERT((ROSV_ACPI_RSDP_GPA & 0xFULL) == 0);

struct _ROSV_VM {
    /* Identity */
    ULONG VmId;
    ROSV_VM_STATE State;

    /* Configuration */
    ROSV_VM_CONFIG Config;
    CHAR Cmdline[ROSV_CMDLINE_MAX];

    /* Memory */
    ROSV_MEMORY_REGION MemoryRegions[ROSV_MAX_MEMORY_REGIONS];
    ULONG MemoryRegionCount;
    PVOID GuestRam;                 /* Base VA of guest RAM allocation */
    ULONG64 GuestRamSize;           /* In bytes */

    /* EPT */
    ROSV_EPT_CONTEXT EptContext;

    /* MSR bitmap (4KB, physically contiguous, used in VMCS) */
    PVOID MsrBitmap;

    /* I/O APIC state (MMIO-emulated, shared by all interrupt sources). */
    ROSV_IOAPIC_STATE IoApic;
    /* Local APIC state (MMIO-emulated). */
    ROSV_LAPIC_STATE Lapic;

    /* vCPU (single-core for now) */
    ROSV_VCPU Vcpu;

    /* Loaded kernel/initrd info */
    BOOLEAN Is64BitKernel;          /* TRUE if kernel has XLF_KERNEL_64 */
    ULONG BootContract;             /* ROSV_BOOT_CONTRACT - drives entry setup dispatch */
    ULONG64 KernelEntryPoint;
    ULONG64 KernelLoadAddress;
    ULONG64 KernelSize;
    ULONG64 InitrdLoadAddress;
    ULONG64 InitrdSize;
    BOOLEAN InitrdStreamActive;     /* TRUE while chunked initrd upload in progress */
    ULONG64 InitrdStreamAddress;    /* Target GPA for chunked upload */
    ULONG64 InitrdStreamExpected;   /* Total bytes expected by BEGIN */
    ULONG64 InitrdStreamReceived;   /* Bytes received via CHUNK */
    ULONG64 BootParamsAddress;

    /* Console context (forward declared, defined in device.h) */
    PROSV_CONSOLE_CONTEXT Console;

    /* Virtio-blk device instance (optional, active when DiskImageBase != NULL) */
    ROSV_VIRTIO_BLK_STATE VirtioBlk;

    /* Virtio-net device instance (always active; provides guest eth0) */
    ROSV_VIRTIO_NET_STATE VirtioNet;

    /* Virtio-console device instance (multi-port terminal for rosl sessions) */
    ROSV_VIRTIO_CON_STATE VirtioCon;

    /* In-kernel network backend state (opaque, defined in net_backend.c). */
    struct _ROSV_NET_BACKEND_STATE *NetBackend;

    /* VHDX state (parsed when disk backend is VHDX) */
    ROSV_VHDX_STATE VhdxState;

    /* PTY manager (defined in pty.h) */
    ROSV_PTY_MANAGER PtyManager;

    /* Boot checkpoint timestamps */
    LARGE_INTEGER CheckpointTimes[RosvCpShellPrompt + 1];

    /* Synchronization */
    KSPIN_LOCK Lock;
    KEVENT StopEvent;
    EX_RUNDOWN_REF RundownRef;   /* Protects lock-free users during VM teardown */
};

/* ---- Function prototypes (vm/) ------------------------------------------ */

/* vm.c - VM instance management */
NTSTATUS
RosvVmCreate(
    _In_ PROSV_VM_CONFIG Config,
    _Out_ PROSV_VM *Vm);

VOID
RosvVmDestroy(
    _Inout_ PROSV_VM Vm);

BOOLEAN
RosvVmAcquireReference(
    _Inout_ PROSV_VM Vm);

VOID
RosvVmReleaseReference(
    _Inout_ PROSV_VM Vm);

VOID
RosvVmWaitForReferencesReleased(
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvVmSetMemory(
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvVmStart(
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvVmStop(
    _Inout_ PROSV_VM Vm);

ROSV_VM_STATE
RosvVmGetState(
    _In_ PROSV_VM Vm);

/* vcpu.c - vCPU run loop and thread */
NTSTATUS
RosvVcpuInitialize(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm);

VOID
RosvVcpuDestroy(
    _Inout_ PROSV_VCPU Vcpu);

NTSTATUS
RosvVcpuStart(
    _Inout_ PROSV_VCPU Vcpu);

NTSTATUS
RosvVcpuStop(
    _Inout_ PROSV_VCPU Vcpu);

VOID
RosvVcpuThreadProc(
    _In_ PVOID Context);

/* memory.c - Guest physical memory allocator (MDL-based, no permanent VA) */
NTSTATUS
RosvMemoryAllocateGuestRam(
    _Inout_ PROSV_VM Vm,
    _In_ ULONG64 SizeInBytes);

VOID
RosvMemoryFreeGuestRam(
    _Inout_ PROSV_VM Vm);

VOID
RosvMemoryUnmapAll(
    _Inout_ PROSV_VM Vm);

VOID
RosvMemoryVerifyMappings(
    _In_ PROSV_VM Vm);

PVOID
RosvMemoryGpaToHva(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress);

NTSTATUS
RosvMemoryCopyToGpa(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ const VOID *Source,
    _In_ SIZE_T Length);

NTSTATUS
RosvMemoryCopyFromGpa(
    _In_ PROSV_VM Vm,
    _Out_writes_bytes_(Length) VOID *Destination,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ SIZE_T Length);

/* vm_debug.c - Boot checkpoints and exit logging */
VOID
RosvVmSetCheckpoint(
    _Inout_ PROSV_VM Vm,
    _In_ ROSV_CHECKPOINT Checkpoint);

VOID
RosvVmDumpCheckpoints(
    _In_ PROSV_VM Vm);

/* vm_log.c - Exit ring buffer */
VOID
RosvExitLogRecord(
    _Inout_ PROSV_EXIT_RING Ring,
    _In_ ULONG ExitReason,
    _In_ ULONG64 ExitQualification,
    _In_ ULONG64 GuestRip,
    _In_ ULONG InstructionLength);

NTSTATUS
RosvExitLogRead(
    _In_ PROSV_EXIT_RING Ring,
    _Out_writes_(MaxEntries) PROSV_EXIT_LOG_ENTRY Buffer,
    _In_ ULONG MaxEntries,
    _Out_ PULONG EntriesReturned);

VOID
RosvExitLogDump(
    _In_ PROSV_EXIT_RING Ring,
    _In_ ULONG NumEntries);

VOID
RosvExitLogDumpSummary(
    _In_ PROSV_EXIT_RING Ring);

/* exit.c - Guest register access by index (used by MMIO emulation) */
ULONG64
RosvGetGuestRegByIndex(
    _In_ PROSV_GUEST_REGS Regs,
    _In_ ULONG Index);

VOID
RosvSetGuestRegByIndex(
    _Inout_ PROSV_GUEST_REGS Regs,
    _In_ ULONG Index,
    _In_ ULONG64 Value);

VOID
RosvVmTryInjectPendingInterrupts(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm);
