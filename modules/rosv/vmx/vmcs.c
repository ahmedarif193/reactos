/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VMCS lifecycle and configuration
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vmx.h>
#include <rosv/vm.h>
#include <intrin.h>

/* MSR indices */
#define IA32_EFER               0xC0000080
#define IA32_PAT                0x00000277
#define IA32_SYSENTER_CS        0x00000174
#define IA32_SYSENTER_ESP       0x00000175
#define IA32_SYSENTER_EIP       0x00000176
#define IA32_FS_BASE            0xC0000100
#define IA32_GS_BASE            0xC0000101

/* MSR bitmap size: 1 page with 4 sections (1024 bytes each): read low, read high, write low, write high */
#define MSR_BITMAP_SIZE         PAGE_SIZE

/* Segment access rights constants for flat protected mode */
#define SEG_ACCESS_CODE_ER_64   0xA09B  /* 64-bit code: P=1, DPL=0, S=1, Type=Execute/Read, L=1, D=0 */
#define SEG_ACCESS_DATA_RW      0xC093  /* Data: P=1, DPL=0, S=1, Type=Read/Write, G=1, D/B=1 */
#define SEG_ACCESS_TSS_BUSY     0x008B  /* TSS busy: P=1, Type=0xB */
#define SEG_ACCESS_UNUSABLE     0x10000 /* Unusable segment */

/* Helper to read GDT/IDT base and limit */
#include <pshpack1.h>
typedef struct _DESCRIPTOR_TABLE_REG {
    USHORT Limit;
    ULONG64 Base;
} DESCRIPTOR_TABLE_REG;
#include <poppack.h>

static
ULONG
RosvCalcVmxControl(
    _In_ ULONG Desired,
    _In_ ULONG Allowed0,
    _In_ ULONG Allowed1)
{
    /* Bits in allowed0 MUST be 1, bits NOT in allowed1 MUST be 0 */
    ULONG Result = (Allowed0 | Desired) & Allowed1;
    return Result;
}

/* ---- VMCS allocation ---------------------------------------------------- */

NTSTATUS
RosvVmcsAlloc(
    _In_ ULONG RevisionId,
    _Out_ PHYSICAL_ADDRESS *VmcsPhys,
    _Out_ PVOID *VmcsVirt)
{
    PVOID Region;
    PHYSICAL_ADDRESS PhysAddr;
    PHYSICAL_ADDRESS MaxAddr;

    ROSV_TRACE("RosvVmcsAlloc: revision=0x%08X", RevisionId);

    MaxAddr.QuadPart = 0xFFFFFFFFLL;
    Region = MmAllocateContiguousMemory(PAGE_SIZE, MaxAddr);
    if (!Region)
    {
        ROSV_ERR("  Failed to allocate VMCS region (4KB contiguous)");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Region, PAGE_SIZE);
    PhysAddr = MmGetPhysicalAddress(Region);

    /* Write revision ID in first 4 bytes (bit 31 must be 0 for non-shadow VMCS) */
    *(PULONG)Region = RevisionId;

    ROSV_TRACE("  VMCS VA=%p PA=0x%016llX revision=0x%08X",
               Region, PhysAddr.QuadPart, RevisionId);

    *VmcsPhys = PhysAddr;
    *VmcsVirt = Region;

    ROSV_TRACE("RosvVmcsAlloc: success");
    return STATUS_SUCCESS;
}

VOID
RosvVmcsFree(
    _In_ PHYSICAL_ADDRESS VmcsPhys,
    _In_ PVOID VmcsVirt)
{
    VMX_RESULT VmxResult;

    ROSV_TRACE("RosvVmcsFree: VA=%p PA=0x%016llX", VmcsVirt, VmcsPhys.QuadPart);

    /* VMCLEAR before freeing - ensures VMCS data is flushed */
    VmxResult = RosvAsmVmClear(&VmcsPhys);
    if (VmxResult != VmxOk)
    {
        ROSV_ERR("  VMCLEAR failed with result %u during free", (ULONG)VmxResult);
    }
    else
    {
        ROSV_TRACE("  VMCLEAR succeeded");
    }

    if (VmcsVirt)
    {
        MmFreeContiguousMemory(VmcsVirt);
        ROSV_TRACE("  VMCS region freed");
    }
}

NTSTATUS
RosvVmcsClear(
    _In_ PHYSICAL_ADDRESS VmcsPhys)
{
    VMX_RESULT VmxResult;

    ROSV_TRACE("RosvVmcsClear: PA=0x%016llX", VmcsPhys.QuadPart);

    VmxResult = RosvAsmVmClear(&VmcsPhys);
    if (VmxResult != VmxOk)
    {
        ROSV_ERR("  VMCLEAR failed with result %u", (ULONG)VmxResult);
        return STATUS_UNSUCCESSFUL;
    }

    ROSV_TRACE("  VMCLEAR succeeded");
    return STATUS_SUCCESS;
}

NTSTATUS
RosvVmcsLoad(
    _In_ PHYSICAL_ADDRESS VmcsPhys)
{
    VMX_RESULT VmxResult;

    ROSV_TRACE("RosvVmcsLoad: PA=0x%016llX", VmcsPhys.QuadPart);

    VmxResult = RosvAsmVmPtrLoad(&VmcsPhys);
    if (VmxResult != VmxOk)
    {
        ROSV_ERR("  VMPTRLD failed with result %u", (ULONG)VmxResult);
        return STATUS_UNSUCCESSFUL;
    }

    ROSV_TRACE("  VMPTRLD succeeded");
    return STATUS_SUCCESS;
}

ULONG64
RosvVmcsRead(
    _In_ VMCS_FIELD Field)
{
    ULONG64 Value = 0;
    VMX_RESULT VmxResult;

    VmxResult = RosvAsmVmRead((ULONG64)Field, &Value);
    if (VmxResult != VmxOk)
    {
        ROSV_ERR("RosvVmcsRead: VMREAD field=0x%X failed result=%u",
                 (ULONG)Field, (ULONG)VmxResult);
    }

    return Value;
}

VOID
RosvVmcsWrite(
    _In_ VMCS_FIELD Field,
    _In_ ULONG64 Value)
{
    VMX_RESULT VmxResult;

    VmxResult = RosvAsmVmWrite((ULONG64)Field, Value);
    if (VmxResult != VmxOk)
    {
        ROSV_ERR("RosvVmcsWrite: VMWRITE field=0x%X value=0x%016llX failed result=%u",
                 (ULONG)Field, Value, (ULONG)VmxResult);
    }
}

/* ---- Host state setup --------------------------------------------------- */

NTSTATUS
RosvVmcsSetupHost(
    _In_ PROSV_VCPU Vcpu)
{
    DESCRIPTOR_TABLE_REG GdtReg, IdtReg;
    USHORT Es, Cs, Ss, Ds, Fs, Gs, Tr;
    ULONG64 Efer, Pat;

    ROSV_TRACE("RosvVmcsSetupHost: begin");

    /* Read current segment selectors via inline asm */
    __asm__ __volatile__("mov %%es, %0" : "=r"(Es));
    __asm__ __volatile__("mov %%cs, %0" : "=r"(Cs));
    __asm__ __volatile__("mov %%ss, %0" : "=r"(Ss));
    __asm__ __volatile__("mov %%ds, %0" : "=r"(Ds));
    __asm__ __volatile__("mov %%fs, %0" : "=r"(Fs));
    __asm__ __volatile__("mov %%gs, %0" : "=r"(Gs));
    __asm__ __volatile__("str %0" : "=r"(Tr));

    ROSV_TRACE("  Host selectors: CS=0x%04X SS=0x%04X DS=0x%04X ES=0x%04X FS=0x%04X GS=0x%04X TR=0x%04X",
               Cs, Ss, Ds, Es, Fs, Gs, Tr);

    /* RPL bits must be 0 for host selectors */
    RosvVmcsWrite(VMCS_HOST_ES_SEL, Es & 0xFFF8);
    RosvVmcsWrite(VMCS_HOST_CS_SEL, Cs & 0xFFF8);
    RosvVmcsWrite(VMCS_HOST_SS_SEL, Ss & 0xFFF8);
    RosvVmcsWrite(VMCS_HOST_DS_SEL, Ds & 0xFFF8);
    RosvVmcsWrite(VMCS_HOST_FS_SEL, Fs & 0xFFF8);
    RosvVmcsWrite(VMCS_HOST_GS_SEL, Gs & 0xFFF8);
    RosvVmcsWrite(VMCS_HOST_TR_SEL, Tr & 0xFFF8);

    /* Host CR0, CR3, CR4 */
    Vcpu->HostCr0 = __readcr0();
    Vcpu->HostCr3 = __readcr3();
    Vcpu->HostCr4 = __readcr4();

    ROSV_TRACE("  Host CR0=0x%016llX CR3=0x%016llX CR4=0x%016llX",
               Vcpu->HostCr0, Vcpu->HostCr3, Vcpu->HostCr4);

    RosvVmcsWrite(VMCS_HOST_CR0, Vcpu->HostCr0);
    RosvVmcsWrite(VMCS_HOST_CR3, Vcpu->HostCr3);
    RosvVmcsWrite(VMCS_HOST_CR4, Vcpu->HostCr4);

    /* GDT and IDT base */
    _sgdt(&GdtReg);
    __sidt(&IdtReg);

    ROSV_TRACE("  Host GDT base=0x%016llX limit=0x%04X",
               GdtReg.Base, GdtReg.Limit);
    ROSV_TRACE("  Host IDT base=0x%016llX limit=0x%04X",
               IdtReg.Base, IdtReg.Limit);

    RosvVmcsWrite(VMCS_HOST_GDTR_BASE, GdtReg.Base);
    RosvVmcsWrite(VMCS_HOST_IDTR_BASE, IdtReg.Base);

    /* TR base - extract from GDT */
    {
        ULONG TrIndex = (Tr & 0xFFF8);
        PUCHAR GdtBase = (PUCHAR)GdtReg.Base;
        ULONG64 TrBase;

        /* TSS descriptor in 64-bit mode is 16 bytes */
        ULONG64 Low = *(PULONG64)(GdtBase + TrIndex);
        ULONG64 High = *(PULONG64)(GdtBase + TrIndex + 8);

        TrBase = ((Low >> 16) & 0xFFFF) |
                 (((Low >> 32) & 0xFF) << 16) |
                 (((Low >> 56) & 0xFF) << 24) |
                 ((High & 0xFFFFFFFF) << 32);

        ROSV_TRACE("  Host TR base=0x%016llX (from GDT[0x%X])", TrBase, TrIndex);
        RosvVmcsWrite(VMCS_HOST_TR_BASE, TrBase);
    }

    /* FS/GS base from MSRs */
    RosvVmcsWrite(VMCS_HOST_FS_BASE, __readmsr(IA32_FS_BASE));
    RosvVmcsWrite(VMCS_HOST_GS_BASE, __readmsr(IA32_GS_BASE));

    ROSV_TRACE("  Host FS_BASE=0x%016llX GS_BASE=0x%016llX",
               __readmsr(IA32_FS_BASE), __readmsr(IA32_GS_BASE));

    /* SYSENTER CS/ESP/EIP */
    RosvVmcsWrite(VMCS_HOST_SYSENTER_CS, __readmsr(IA32_SYSENTER_CS));
    RosvVmcsWrite(VMCS_HOST_SYSENTER_ESP, __readmsr(IA32_SYSENTER_ESP));
    RosvVmcsWrite(VMCS_HOST_SYSENTER_EIP, __readmsr(IA32_SYSENTER_EIP));

    /* IA32_EFER */
    Efer = __readmsr(IA32_EFER);
    RosvVmcsWrite(VMCS_HOST_EFER, Efer);
    ROSV_TRACE("  Host EFER=0x%016llX", Efer);

    /* IA32_PAT */
    Pat = __readmsr(IA32_PAT);
    RosvVmcsWrite(VMCS_HOST_PAT, Pat);
    ROSV_TRACE("  Host PAT=0x%016llX", Pat);

    /* Host RSP and RIP - these are set later by the VM run loop.
     * RIP = address of RosvAsmVmExitHandler (the assembly VM-exit entry point).
     * RSP = top of host stack allocated for this vCPU.
     * For now, set RIP to our assembly exit handler. RSP will be set in vcpu.c.
     */
    RosvVmcsWrite(VMCS_HOST_RIP, (ULONG64)RosvAsmVmExitHandler);
    ROSV_TRACE("  Host RIP=0x%016llX (RosvAsmVmExitHandler)",
               (ULONG64)RosvAsmVmExitHandler);

    ROSV_TRACE("RosvVmcsSetupHost: success");
    return STATUS_SUCCESS;
}

/* ---- Guest state setup -------------------------------------------------- */

NTSTATUS
RosvVmcsSetupGuest(
    _In_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    ULONG64 GuestCr0, GuestCr4;

    ROSV_TRACE("RosvVmcsSetupGuest: begin, entry=0x%016llX",
               Vm->KernelEntryPoint);

    /* Guest CR0: apply platform fixed bits around PE|ET|NE (no paging initially) */
    GuestCr0 = Vcpu->VmxCaps.Cr0Fixed0 | 0x00000031; /* PE=1, ET=1, NE=1 */
    GuestCr0 &= Vcpu->VmxCaps.Cr0Fixed1;
    RosvVmcsWrite(VMCS_GUEST_CR0, GuestCr0);
    ROSV_TRACE("  Guest CR0=0x%016llX (PE|ET|NE + fixed bits)", GuestCr0);

    /* Guest CR3: 0 (no paging initially, or set by boot protocol) */
    RosvVmcsWrite(VMCS_GUEST_CR3, 0);
    ROSV_TRACE("  Guest CR3=0x0");

    /* Guest CR4: apply platform fixed bits but exclude VMXE (bit 13).
     * The host owns VMXE via CR4_MASK/CR4_READ_SHADOW, so the actual
     * guest CR4 must NOT have VMXE set. */
    GuestCr4 = Vcpu->VmxCaps.Cr4Fixed0 & ~0x2000ULL; /* exclude VMXE */
    GuestCr4 &= Vcpu->VmxCaps.Cr4Fixed1;
    RosvVmcsWrite(VMCS_GUEST_CR4, GuestCr4);
    ROSV_TRACE("  Guest CR4=0x%016llX (fixed bits, no VMXE)", GuestCr4);

    /* Guest DR7: default value */
    RosvVmcsWrite(VMCS_GUEST_DR7, 0x00000400);

    /* Guest RFLAGS: bit 1 (reserved, must be 1) */
    RosvVmcsWrite(VMCS_GUEST_RFLAGS, 0x00000002);
    ROSV_TRACE("  Guest RFLAGS=0x00000002");

    /* Guest RIP/RSP from boot params */
    RosvVmcsWrite(VMCS_GUEST_RIP, Vm->KernelEntryPoint);
    ROSV_TRACE("  Guest RIP=0x%016llX", Vm->KernelEntryPoint);

    /* RSP: Linux boot protocol sets sp at boot_params + 0x1000 (or custom) */
    RosvVmcsWrite(VMCS_GUEST_RSP, 0);
    ROSV_TRACE("  Guest RSP=0x0 (will be set by boot protocol)");

    /* Flat 32-bit code segment */
    RosvVmcsWrite(VMCS_GUEST_CS_SEL, 0x0010);
    RosvVmcsWrite(VMCS_GUEST_CS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_CS_ACCESS, 0xC09B); /* P=1, DPL=0, S=1, Code ER, G=1, D/B=1 */
    ROSV_TRACE("  Guest CS: sel=0x10 base=0 limit=0xFFFFFFFF access=0xC09B");

    /* Flat 32-bit data segment */
    RosvVmcsWrite(VMCS_GUEST_DS_SEL, 0x0018);
    RosvVmcsWrite(VMCS_GUEST_DS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_DS_ACCESS, 0xC093); /* P=1, DPL=0, S=1, Data RW, G=1, D/B=1 */

    RosvVmcsWrite(VMCS_GUEST_ES_SEL, 0x0018);
    RosvVmcsWrite(VMCS_GUEST_ES_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_ES_ACCESS, 0xC093);

    RosvVmcsWrite(VMCS_GUEST_SS_SEL, 0x0018);
    RosvVmcsWrite(VMCS_GUEST_SS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_SS_ACCESS, 0xC093);

    RosvVmcsWrite(VMCS_GUEST_FS_SEL, 0x0018);
    RosvVmcsWrite(VMCS_GUEST_FS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_FS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_FS_ACCESS, 0xC093);

    RosvVmcsWrite(VMCS_GUEST_GS_SEL, 0x0018);
    RosvVmcsWrite(VMCS_GUEST_GS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_GS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_GS_ACCESS, 0xC093);

    ROSV_TRACE("  Guest data segments: sel=0x18 flat 4GB");

    /* LDTR: unusable */
    RosvVmcsWrite(VMCS_GUEST_LDTR_SEL, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_LIMIT, 0xFFFF);
    RosvVmcsWrite(VMCS_GUEST_LDTR_ACCESS, SEG_ACCESS_UNUSABLE);
    ROSV_TRACE("  Guest LDTR: unusable");

    /* TR: valid TSS descriptor (required by VMX, even in 32-bit) */
    RosvVmcsWrite(VMCS_GUEST_TR_SEL, 0x0020);
    RosvVmcsWrite(VMCS_GUEST_TR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_TR_LIMIT, 0xFFFF);
    RosvVmcsWrite(VMCS_GUEST_TR_ACCESS, SEG_ACCESS_TSS_BUSY);
    ROSV_TRACE("  Guest TR: sel=0x20 access=0x008B (busy TSS)");

    /* GDT: initially a simple 5-entry GDT at a known location */
    RosvVmcsWrite(VMCS_GUEST_GDTR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_GDTR_LIMIT, 0xFFFF);
    ROSV_TRACE("  Guest GDTR: base=0 limit=0xFFFF");

    /* IDT: initially empty */
    RosvVmcsWrite(VMCS_GUEST_IDTR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_IDTR_LIMIT, 0xFFFF);
    ROSV_TRACE("  Guest IDTR: base=0 limit=0xFFFF");

    /* VMCS link pointer: 0xFFFFFFFFFFFFFFFF (no linked VMCS) */
    RosvVmcsWrite(VMCS_GUEST_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFFULL);
    ROSV_TRACE("  Guest VMCS link ptr = 0xFFFFFFFFFFFFFFFF");

    /* Guest IA32_EFER: 0 (32-bit protected mode, no long mode) */
    RosvVmcsWrite(VMCS_GUEST_EFER, 0);
    ROSV_TRACE("  Guest EFER=0x0");

    /* Guest IA32_PAT: default value */
    RosvVmcsWrite(VMCS_GUEST_PAT, 0x0007040600070406ULL);
    ROSV_TRACE("  Guest PAT=0x0007040600070406 (default)");

    /* IA32_DEBUGCTL */
    RosvVmcsWrite(VMCS_GUEST_DEBUGCTL, 0);

    /* SYSENTER fields */
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_CS, 0);
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_ESP, 0);
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_EIP, 0);

    /* Activity state: 0 = active */
    RosvVmcsWrite(VMCS_GUEST_ACTIVITY, 0);

    /* Interruptibility state: 0 = none */
    RosvVmcsWrite(VMCS_GUEST_INTERRUPTIBILITY, 0);

    /* Pending debug exceptions: 0 */
    RosvVmcsWrite(VMCS_GUEST_PENDING_DBG, 0);

    ROSV_TRACE("RosvVmcsSetupGuest: success");
    return STATUS_SUCCESS;
}

/* ---- VM-execution controls setup ---------------------------------------- */

NTSTATUS
RosvVmcsSetupControls(
    _In_ PVMX_CAPABILITIES Caps,
    _In_ PROSV_VM Vm)
{
    ULONG PinBased, ProcBased, ProcBased2, ExitCtls, EntryCtls;
    PVOID MsrBitmap;
    PHYSICAL_ADDRESS MsrBitmapPhys, MaxAddr;
    ULONG ExceptionBitmap;

    ROSV_TRACE("RosvVmcsSetupControls: begin");

    /* ---- Pin-based controls ---- */
    PinBased = RosvCalcVmxControl(
        VMX_PIN_EXTERNAL_INT | VMX_PIN_NMI_EXITING | VMX_PIN_PREEMPT_TIMER,
        Caps->PinBasedAllowed0,
        Caps->PinBasedAllowed1);

    RosvVmcsWrite(VMCS_CTRL_PIN_BASED, PinBased);
    ROSV_TRACE("  Pin-based controls = 0x%08X (preempt_timer=%s)",
               PinBased,
               (PinBased & VMX_PIN_PREEMPT_TIMER) ? "yes" : "no");

    /* ---- Primary proc-based controls ---- */
    ProcBased = RosvCalcVmxControl(
        VMX_PROC_HLT_EXIT |
        VMX_PROC_MWAIT_EXIT |
        VMX_PROC_MONITOR_EXIT |
        VMX_PROC_PAUSE_EXIT |
        VMX_PROC_UNCOND_IO_EXIT |
        VMX_PROC_USE_MSR_BITMAPS |
        VMX_PROC_SECONDARY_CTLS,
        Caps->ProcBasedAllowed0,
        Caps->ProcBasedAllowed1);
    /*
     * RDTSC exiting disabled: each RDTSC VM-exit costs ~500-2000 cycles,
     * and Linux guests execute RDTSC extremely frequently (gettimeofday,
     * scheduling, etc.). The preemption timer provides reliable periodic
     * VM-exits for timer/interrupt delivery without this overhead.
     */

    RosvVmcsWrite(VMCS_CTRL_PROC_BASED, ProcBased);
    ROSV_TRACE("  Primary proc-based controls = 0x%08X", ProcBased);

    /* ---- Secondary proc-based controls ---- */
    if (Caps->SecondaryCtlsAvailable)
    {
        ProcBased2 = RosvCalcVmxControl(
            VMX_PROC2_EPT |
            VMX_PROC2_UNRESTRICTED_GUEST |
            VMX_PROC2_RDTSCP |
            VMX_PROC2_INVPCID |
            VMX_PROC2_XSAVES,
            Caps->ProcBased2Allowed0,
            Caps->ProcBased2Allowed1);

        RosvVmcsWrite(VMCS_CTRL_PROC_BASED2, ProcBased2);
        ROSV_TRACE("  Secondary proc-based controls = 0x%08X", ProcBased2);

        if (ProcBased2 & VMX_PROC2_EPT)
        {
            ROSV_TRACE("  EPT enabled");
        }
        if (ProcBased2 & VMX_PROC2_UNRESTRICTED_GUEST)
        {
            ROSV_TRACE("  Unrestricted guest enabled");
        }
    }
    else
    {
        ProcBased2 = 0;
        ROSV_WARN("  No secondary controls available");
    }

    /* ---- VM-exit controls ---- */
    ExitCtls = RosvCalcVmxControl(
        VMX_EXIT_HOST_ADDR_SPACE_64 |
        VMX_EXIT_SAVE_EFER |
        VMX_EXIT_LOAD_EFER |
        VMX_EXIT_SAVE_PAT |
        VMX_EXIT_LOAD_PAT |
        VMX_EXIT_SAVE_PREEMPT_TIMER,
        Caps->ExitCtlsAllowed0,
        Caps->ExitCtlsAllowed1);
    /*
     * Keep ACK_INT_ON_EXIT disabled. With external-interrupt exiting active,
     * native host interrupt dispatch (IDT + ISR-owned EOI ordering) must stay
     * in the HAL/ntoskrnl path.
     */
    if (ExitCtls & VMX_EXIT_ACK_INT_ON_EXIT)
    {
        ROSV_ERR("VMX exit controls forced ACK_INT_ON_EXIT; unsupported in current host interrupt model");
        return STATUS_NOT_SUPPORTED;
    }

    RosvVmcsWrite(VMCS_CTRL_EXIT, ExitCtls);
    ROSV_TRACE("  VM-exit controls = 0x%08X", ExitCtls);

    /* ---- VM-entry controls ---- */
    {
        ULONG EntryDesired = VMX_ENTRY_LOAD_EFER | VMX_ENTRY_LOAD_PAT;

        /* 64-bit guest requires IA-32e mode guest bit */
        if (Vm->Is64BitKernel)
        {
            EntryDesired |= VMX_ENTRY_IA32E_GUEST;
            ROSV_TRACE("  VM-entry: setting IA32E_GUEST for 64-bit kernel");
        }

        EntryCtls = RosvCalcVmxControl(
            EntryDesired,
            Caps->EntryCtlsAllowed0,
            Caps->EntryCtlsAllowed1);
    }

    RosvVmcsWrite(VMCS_CTRL_ENTRY, EntryCtls);
    ROSV_TRACE("  VM-entry controls = 0x%08X", EntryCtls);

    /* ---- Exception bitmap ---- */
    /* Only intercept #UD, #DF, #GP - real error conditions during boot.
     * #PF must NOT be intercepted: the Linux compressed kernel uses a custom
     * page-fault handler to lazily build identity mappings during decompression.
     * Intercepting #PF prevents the guest's handler from running and crashes it. */
    /*
     * Only intercept #DF (double fault) for catastrophic debugging.
     * #UD and #GP must NOT be intercepted - the guest kernel needs to handle
     * them natively (SIGSEGV delivery, instruction emulation, etc.).
     * Intercepting #GP was causing VM termination when userspace triggered
     * normal protection faults (e.g., systemd startup).
     */
    ExceptionBitmap = (1U << 8); /* #DF only */
    RosvVmcsWrite(VMCS_CTRL_EXCEPTION_BITMAP, ExceptionBitmap);
    ROSV_TRACE("  Exception bitmap = 0x%08X (#DF only)", ExceptionBitmap);

    /* Page-fault error code mask/match: not used since #PF is not intercepted */
    RosvVmcsWrite(VMCS_CTRL_PF_ERROR_MASK, 0);
    RosvVmcsWrite(VMCS_CTRL_PF_ERROR_MATCH, 0);

    /* CR3 target count: 0 (all CR3 loads cause exit) */
    RosvVmcsWrite(VMCS_CTRL_CR3_TARGET_COUNT, 0);

    /* CR0/CR4 guest/host masks and read shadows.
     * Mask bit=1 means host-owned: guest reads get shadow, writes cause exit.
     * VMXE (bit 13) must be host-owned because CR4_FIXED0 requires it set
     * in the real CR4, but the guest shouldn't see it. */
    RosvVmcsWrite(VMCS_CTRL_CR0_MASK, 0);     /* No CR0 bits owned by host */
    RosvVmcsWrite(VMCS_CTRL_CR0_READ_SHADOW, 0);
    RosvVmcsWrite(VMCS_CTRL_CR4_MASK, 0x2000);  /* Host owns VMXE (bit 13) */
    RosvVmcsWrite(VMCS_CTRL_CR4_READ_SHADOW, 0); /* Guest sees VMXE=0 */

    /* ---- MSR bitmap ---- */
    /* Allocate a 4KB MSR bitmap page. All bits = 1 means all MSR accesses exit. */
    MaxAddr.QuadPart = 0xFFFFFFFFLL;
    MsrBitmap = MmAllocateContiguousMemory(PAGE_SIZE, MaxAddr);
    if (!MsrBitmap)
    {
        ROSV_ERR("  Failed to allocate MSR bitmap");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Set all bits to 1: all MSR reads/writes cause VM-exit */
    RtlFillMemory(MsrBitmap, PAGE_SIZE, 0xFF);

    MsrBitmapPhys = MmGetPhysicalAddress(MsrBitmap);

    RosvVmcsWrite(VMCS_CTRL_MSR_BITMAP, MsrBitmapPhys.QuadPart);
    Vm->MsrBitmap = MsrBitmap;
    ROSV_TRACE("  MSR bitmap VA=%p PA=0x%016llX (all MSRs exit)",
               MsrBitmap, MsrBitmapPhys.QuadPart);

    /* No MSR store/load on exit/entry */
    RosvVmcsWrite(VMCS_CTRL_EXIT_MSR_STORE_COUNT, 0);
    RosvVmcsWrite(VMCS_CTRL_EXIT_MSR_LOAD_COUNT, 0);
    RosvVmcsWrite(VMCS_CTRL_ENTRY_MSR_LOAD_COUNT, 0);

    /* ---- EPT pointer ---- */
    if (ProcBased2 & VMX_PROC2_EPT)
    {
        RosvVmcsWrite(VMCS_CTRL_EPTP, Vm->EptContext.Eptp.Value);
        ROSV_TRACE("  EPTP = 0x%016llX", Vm->EptContext.Eptp.Value);
    }

    /* TSC offset: 0 (pass through TSC) */
    RosvVmcsWrite(VMCS_CTRL_TSC_OFFSET, 0);

    /* Enable VMX preemption timer when supported to guarantee periodic exits
     * even when the guest executes without intercepted operations. */
    if (PinBased & VMX_PIN_PREEMPT_TIMER)
    {
        ULONG64 VmxMisc = __readmsr(0x00000485); /* IA32_VMX_MISC */
        ULONG TimerShift = (ULONG)(VmxMisc & 0x1F);
        ULONG TimerReload = (ULONG)(16000000UL >> TimerShift);

        if (TimerReload == 0)
            TimerReload = 1;

        RosvVmcsWrite(VMCS_GUEST_PREEMPT_TIMER, TimerReload);
        Vm->Vcpu.PreemptTimerReload = TimerReload;
        ROSV_TRACE("  Preemption timer reload=%u (shift=%u)", TimerReload, TimerShift);
    }
    else
    {
        Vm->Vcpu.PreemptTimerReload = 0;
    }

    /* No VPID for now */

    ROSV_TRACE("RosvVmcsSetupControls: success");
    return STATUS_SUCCESS;
}
