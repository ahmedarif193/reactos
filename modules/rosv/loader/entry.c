/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Guest initial state setup for Linux boot
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 *
 * Configures the initial CPU state for guest entry, including:
 *  - Building identity-mapped guest page tables (4-level, 2MB pages)
 *  - Setting up a minimal GDT in guest memory
 *  - Configuring VMCS guest-state fields (segments, CR0/3/4, EFER, RIP, RSI)
 *
 * For 64-bit Linux kernels (XLF_KERNEL_64), the guest enters in long mode
 * with paging enabled and identity-mapped page tables covering 0-4GB.
 * For 32-bit kernels, the guest enters in protected mode without paging.
 */

#include <rosv/rosv.h>
#include <rosv/loader.h>
#include <rosv/vmx.h>
#include <rosv/vm.h>

/* GDT entry structure (8 bytes, packed) */
#include <pshpack1.h>
typedef struct _GDT_ENTRY {
    USHORT LimitLow;
    USHORT BaseLow;
    UCHAR BaseMid;
    UCHAR Access;
    UCHAR Granularity;     /* includes limit[19:16] in bits 3:0 */
    UCHAR BaseHigh;
} GDT_ENTRY, *PGDT_ENTRY;
#include <poppack.h>

/* Segment access rights for VMCS encoding */
/* Format: Type(4) | S(1) | DPL(2) | P(1) | reserved(4) | AVL(1) | L(1) | D/B(1) | G(1) */

/* 64-bit code segment: Execute/Read, Conforming=0, DPL=0, Present, Long=1, G=1 */
#define SEG_ACCESS_CODE64   0xA09B
/* 32-bit code segment: Execute/Read, Conforming=0, DPL=0, Present, D=1, G=1 */
#define SEG_ACCESS_CODE32   0xC09B
/* Data segment: Read/Write, DPL=0, Present, D/B=1, G=1 */
#define SEG_ACCESS_DATA     0xC093
/* TSS segment: 64-bit TSS Available, DPL=0, Present */
#define SEG_ACCESS_TSS64    0x008B
/* Unusable segment */
#define SEG_ACCESS_UNUSABLE 0x10000

/* EFER bits */
#define EFER_SCE    (1ULL << 0)   /* System Call Extensions */
#define EFER_LME    (1ULL << 8)   /* Long Mode Enable */
#define EFER_LMA    (1ULL << 10)  /* Long Mode Active */
#define EFER_NXE    (1ULL << 11)  /* No-Execute Enable */

/* CR0 bits */
#define CR0_PE      (1ULL << 0)   /* Protection Enable */
#define CR0_MP      (1ULL << 1)   /* Monitor Coprocessor */
#define CR0_ET      (1ULL << 4)   /* Extension Type */
#define CR0_NE      (1ULL << 5)   /* Numeric Error */
#define CR0_WP      (1ULL << 16)  /* Write Protect */
#define CR0_AM      (1ULL << 18)  /* Alignment Mask */
#define CR0_PG      (1ULL << 31)  /* Paging */

/* CR4 bits */
#define CR4_PAE     (1ULL << 5)   /* Physical Address Extension */
#define CR4_VMXE    (1ULL << 13)  /* VMX Enable (required by CR4_FIXED0) */

/* Page table entry flags */
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_PS          (1ULL << 7)   /* 2MB page (in PDE) */

/*
 * RosvBuildGuestPageTables - Build 4-level identity map for guest
 *
 * Creates identity-mapped page tables in guest memory using 2MB pages
 * to cover the first 4GB of address space. This is what the Linux
 * kernel expects when entering in 64-bit long mode.
 *
 * Layout in guest memory:
 *   0x1000: PML4  (1 entry pointing to PDPT)
 *   0x2000: PDPT  (4 entries pointing to PDs)
 *   0x3000: PD0   (512 entries, 2MB each = 0-1GB)
 *   0x4000: PD1   (512 entries, 2MB each = 1-2GB)
 *   0x5000: PD2   (512 entries, 2MB each = 2-3GB)
 *   0x6000: PD3   (512 entries, 2MB each = 3-4GB)
 */
static NTSTATUS
RosvBuildGuestPageTables(
    _In_ PROSV_VM Vm)
{
    PULONG64 Pml4;
    PULONG64 Pdpt;
    PULONG64 Pd;
    ULONG i, j;
    ULONG64 PdAddrs[4] = {
        LINUX_GUEST_PD0_ADDR,
        LINUX_GUEST_PD1_ADDR,
        LINUX_GUEST_PD2_ADDR,
        LINUX_GUEST_PD3_ADDR
    };

    ROSV_TRACE("Building guest identity-mapped page tables (4-level, 2MB pages)");

    /* Map and zero PML4 */
    Pml4 = (PULONG64)RosvMemoryGpaToHva(Vm, LINUX_GUEST_PML4_ADDR);
    if (Pml4 == NULL)
    {
        ROSV_ERR("Failed to map PML4 at GPA 0x%X", LINUX_GUEST_PML4_ADDR);
        return STATUS_INVALID_ADDRESS;
    }
    RtlZeroMemory(Pml4, PAGE_SIZE);

    /* PML4[0] -> PDPT */
    Pml4[0] = LINUX_GUEST_PDPT_ADDR | PTE_PRESENT | PTE_WRITABLE;
    ROSV_DEBUG("  PML4[0] = 0x%llX", Pml4[0]);

    /* Map and zero PDPT */
    Pdpt = (PULONG64)RosvMemoryGpaToHva(Vm, LINUX_GUEST_PDPT_ADDR);
    if (Pdpt == NULL)
    {
        ROSV_ERR("Failed to map PDPT at GPA 0x%X", LINUX_GUEST_PDPT_ADDR);
        return STATUS_INVALID_ADDRESS;
    }
    RtlZeroMemory(Pdpt, PAGE_SIZE);

    /* Set up 4 PDPT entries, each pointing to a PD for 1GB region */
    for (i = 0; i < 4; i++)
    {
        Pdpt[i] = PdAddrs[i] | PTE_PRESENT | PTE_WRITABLE;
        ROSV_DEBUG("  PDPT[%u] = 0x%llX", i, Pdpt[i]);

        /* Map and fill each PD with 512 2MB page entries */
        Pd = (PULONG64)RosvMemoryGpaToHva(Vm, PdAddrs[i]);
        if (Pd == NULL)
        {
            ROSV_ERR("Failed to map PD%u at GPA 0x%llX", i, PdAddrs[i]);
            return STATUS_INVALID_ADDRESS;
        }
        RtlZeroMemory(Pd, PAGE_SIZE);

        for (j = 0; j < 512; j++)
        {
            ULONG64 PhysAddr = ((ULONG64)i * 512 + j) * (2ULL * 1024 * 1024);
            Pd[j] = PhysAddr | PTE_PRESENT | PTE_WRITABLE | PTE_PS;
        }

        ROSV_DEBUG("  PD%u: 512 x 2MB pages covering 0x%llX - 0x%llX",
                   i,
                   (ULONG64)i * (1ULL << 30),
                   (ULONG64)(i + 1) * (1ULL << 30) - 1);
    }

    ROSV_TRACE("Guest page tables built: 4GB identity map using 2MB pages");
    return STATUS_SUCCESS;
}

/*
 * RosvBuildGuestGdt - Build a minimal GDT in guest memory
 *
 * Creates a GDT at the designated guest address with:
 *   Entry 0: Null descriptor
 *   Entry 1: Code segment (64-bit or 32-bit depending on kernel type)
 *   Entry 2: Data segment
 *   Entry 3: TSS descriptor (required by VMX)
 */
static NTSTATUS
RosvBuildGuestGdt(
    _In_ PROSV_VM Vm,
    _In_ BOOLEAN Is64Bit)
{
    PGDT_ENTRY Gdt;

    Gdt = (PGDT_ENTRY)RosvMemoryGpaToHva(Vm, LINUX_GUEST_GDT_ADDR);
    if (Gdt == NULL)
    {
        ROSV_ERR("Failed to map GDT at GPA 0x%X", LINUX_GUEST_GDT_ADDR);
        return STATUS_INVALID_ADDRESS;
    }
    RtlZeroMemory(Gdt, PAGE_SIZE);

    /* Entry 0: Null descriptor (already zero) */

    /* Entry 1 (selector 0x08): Code segment */
    if (Is64Bit)
    {
        /* 64-bit code: base=0, limit=0, L=1, D=0 */
        Gdt[1].LimitLow = 0xFFFF;
        Gdt[1].BaseLow  = 0;
        Gdt[1].BaseMid  = 0;
        Gdt[1].Access    = 0x9B;  /* P=1, DPL=0, S=1, Type=Execute/Read/Accessed */
        Gdt[1].Granularity = 0xAF; /* G=1, L=1, D=0, limit[19:16]=0xF */
        Gdt[1].BaseHigh = 0;
    }
    else
    {
        /* 32-bit code: base=0, limit=0xFFFFF * 4K */
        Gdt[1].LimitLow = 0xFFFF;
        Gdt[1].BaseLow  = 0;
        Gdt[1].BaseMid  = 0;
        Gdt[1].Access    = 0x9B;
        Gdt[1].Granularity = 0xCF; /* G=1, D=1, limit[19:16]=0xF */
        Gdt[1].BaseHigh = 0;
    }

    /* Entry 2 (selector 0x10): Data segment */
    Gdt[2].LimitLow = 0xFFFF;
    Gdt[2].BaseLow  = 0;
    Gdt[2].BaseMid  = 0;
    Gdt[2].Access    = 0x93;  /* P=1, DPL=0, S=1, Type=Read/Write/Accessed */
    Gdt[2].Granularity = 0xCF; /* G=1, D/B=1, limit[19:16]=0xF */
    Gdt[2].BaseHigh = 0;

    /* Entry 3 (selector 0x18): TSS (64-bit TSS takes 16 bytes / 2 entries) */
    /* Minimal TSS: base=0, limit=0x67 (minimum TSS size), type=0x89 (available 64-bit TSS) */
    Gdt[3].LimitLow = 0x0067;
    Gdt[3].BaseLow  = 0;
    Gdt[3].BaseMid  = 0;
    Gdt[3].Access    = 0x89;  /* P=1, DPL=0, S=0, Type=Available TSS */
    Gdt[3].Granularity = 0x00;
    Gdt[3].BaseHigh = 0;
    /* For 64-bit TSS, entry 4 holds the high 32 bits of base (zero here) */
    RtlZeroMemory(&Gdt[4], sizeof(GDT_ENTRY));

    ROSV_TRACE("Guest GDT built at GPA 0x%X: null + %s code (0x08) + "
               "data (0x10) + TSS (0x18)",
               LINUX_GUEST_GDT_ADDR, Is64Bit ? "64-bit" : "32-bit");

    return STATUS_SUCCESS;
}

/*
 * RosvGuestEntrySetupBzImage - Configure guest state for bzImage boot
 *
 * Sets up the VMCS guest-state area for Linux bzImage kernel entry.
 * For 64-bit kernels: enters in long mode with identity-mapped paging.
 * For 32-bit kernels: enters in protected mode without paging.
 */
static NTSTATUS
RosvGuestEntrySetupBzImage(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    NTSTATUS Status;
    PLINUX_BOOT_PARAMS BootParams;
    PVOID BootParamsHva;
    BOOLEAN Is64Bit;
    ULONG64 GuestCr0, GuestCr4, GuestEfer;

    UNREFERENCED_PARAMETER(Vcpu);

    ROSV_TRACE("RosvGuestEntrySetupBzImage: configuring guest initial state");

    /* Read boot_params to determine kernel type */
    BootParamsHva = RosvMemoryGpaToHva(Vm, Vm->BootParamsAddress);
    if (BootParamsHva == NULL)
    {
        ROSV_ERR("RosvGuestEntrySetupBzImage: failed to map boot_params");
        return STATUS_INVALID_ADDRESS;
    }
    BootParams = (PLINUX_BOOT_PARAMS)BootParamsHva;

    Is64Bit = (BootParams->Hdr.XloadFlags & LINUX_XLF_KERNEL_64) != 0;
    ROSV_TRACE("RosvGuestEntrySetupBzImage: kernel mode = %s", Is64Bit ? "64-bit" : "32-bit");

    /* Build guest GDT */
    Status = RosvBuildGuestGdt(Vm, Is64Bit);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvGuestEntrySetupBzImage: failed to build guest GDT, status=0x%08X", Status);
        return Status;
    }

    if (Is64Bit)
    {
        /* Build identity-mapped page tables for 64-bit entry */
        Status = RosvBuildGuestPageTables(Vm);
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("RosvGuestEntrySetupBzImage: failed to build page tables, status=0x%08X", Status);
            return Status;
        }
    }

    /* ---- VMCS Guest-State Area ---- */

    /* Control registers */
    if (Is64Bit)
    {
        GuestCr0 = CR0_PE | CR0_ET | CR0_NE | CR0_PG;
        GuestCr4 = CR4_PAE | CR4_VMXE;  /* VMXE required by CR4_FIXED0 */
        GuestEfer = EFER_LME | EFER_LMA;

        RosvVmcsWrite(VMCS_GUEST_CR0, GuestCr0);
        RosvVmcsWrite(VMCS_GUEST_CR3, LINUX_GUEST_PML4_ADDR);
        RosvVmcsWrite(VMCS_GUEST_CR4, GuestCr4);
        ROSV_DEBUG("  CR0=0x%llX CR3=0x%X CR4=0x%llX",
                   GuestCr0, LINUX_GUEST_PML4_ADDR, GuestCr4);
    }
    else
    {
        GuestCr0 = CR0_PE | CR0_ET | CR0_NE;
        GuestCr4 = CR4_VMXE;  /* VMXE required by CR4_FIXED0 */
        GuestEfer = 0;

        RosvVmcsWrite(VMCS_GUEST_CR0, GuestCr0);
        RosvVmcsWrite(VMCS_GUEST_CR3, 0);
        RosvVmcsWrite(VMCS_GUEST_CR4, GuestCr4);
        ROSV_DEBUG("  CR0=0x%llX CR3=0 CR4=0x%llX", GuestCr0, GuestCr4);
    }

    /* EFER */
    RosvVmcsWrite(VMCS_GUEST_EFER, GuestEfer);
    ROSV_DEBUG("  EFER=0x%llX", GuestEfer);

    /* Segment registers */
    if (Is64Bit)
    {
        /* CS: 64-bit code segment */
        RosvVmcsWrite(VMCS_GUEST_CS_SEL, 0x08);
        RosvVmcsWrite(VMCS_GUEST_CS_BASE, 0);
        RosvVmcsWrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
        RosvVmcsWrite(VMCS_GUEST_CS_ACCESS, SEG_ACCESS_CODE64);
        ROSV_DEBUG("  CS: sel=0x08 base=0 limit=0xFFFFFFFF access=0x%X", SEG_ACCESS_CODE64);
    }
    else
    {
        /* CS: 32-bit code segment */
        RosvVmcsWrite(VMCS_GUEST_CS_SEL, 0x08);
        RosvVmcsWrite(VMCS_GUEST_CS_BASE, 0);
        RosvVmcsWrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
        RosvVmcsWrite(VMCS_GUEST_CS_ACCESS, SEG_ACCESS_CODE32);
        ROSV_DEBUG("  CS: sel=0x08 base=0 limit=0xFFFFFFFF access=0x%X", SEG_ACCESS_CODE32);
    }

    /* DS, ES, FS, GS, SS: flat data segments */
    RosvVmcsWrite(VMCS_GUEST_DS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_DS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_DS_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_ES_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_ES_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_ES_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_FS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_FS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_FS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_FS_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_GS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_GS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_GS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_GS_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_SS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_SS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_SS_ACCESS, SEG_ACCESS_DATA);

    ROSV_DEBUG("  DS/ES/FS/GS/SS: sel=0x10 base=0 limit=0xFFFFFFFF access=0x%X",
               SEG_ACCESS_DATA);

    /* LDTR: unusable */
    RosvVmcsWrite(VMCS_GUEST_LDTR_SEL, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_LIMIT, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_ACCESS, SEG_ACCESS_UNUSABLE);
    ROSV_DEBUG("  LDTR: unusable");

    /* TR: valid TSS (required by VMX even if unused) */
    RosvVmcsWrite(VMCS_GUEST_TR_SEL, 0x18);
    RosvVmcsWrite(VMCS_GUEST_TR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_TR_LIMIT, 0x67);
    RosvVmcsWrite(VMCS_GUEST_TR_ACCESS, SEG_ACCESS_TSS64);
    ROSV_DEBUG("  TR: sel=0x18 base=0 limit=0x67 access=0x%X", SEG_ACCESS_TSS64);

    /* GDTR */
    RosvVmcsWrite(VMCS_GUEST_GDTR_BASE, LINUX_GUEST_GDT_ADDR);
    RosvVmcsWrite(VMCS_GUEST_GDTR_LIMIT, 5 * 8 - 1); /* 5 entries */
    ROSV_DEBUG("  GDTR: base=0x%X limit=0x%X", LINUX_GUEST_GDT_ADDR, 5 * 8 - 1);

    /* IDTR: empty (interrupts disabled at entry) */
    RosvVmcsWrite(VMCS_GUEST_IDTR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_IDTR_LIMIT, 0);
    ROSV_DEBUG("  IDTR: base=0 limit=0 (no IDT)");

    /* RIP: kernel entry point */
    RosvVmcsWrite(VMCS_GUEST_RIP, Vm->KernelEntryPoint);
    ROSV_DEBUG("  RIP=0x%llX (kernel entry)", Vm->KernelEntryPoint);

    /* RSI: physical address of boot_params */
    Vcpu->GuestRegs.Rsi = Vm->BootParamsAddress;
    ROSV_DEBUG("  RSI=0x%llX (boot_params)", Vm->BootParamsAddress);

    /* RSP: temporary stack below boot_params. The compressed kernel's
     * startup_64 does pushq %rsi to save boot_params before rep movsq.
     * With RSP=0, push wraps to 0xFFFFFFFFFFFFFFF8 which is unmapped.
     * Provide a valid stack at 0x10000 (grows down into free area 0x8000-0xFFFF). */
    RosvVmcsWrite(VMCS_GUEST_RSP, LINUX_BOOT_PARAMS_ADDR);
    ROSV_DEBUG("  RSP=0x%X (temporary stack)", LINUX_BOOT_PARAMS_ADDR);

    /* RFLAGS: only reserved bit 1 set */
    RosvVmcsWrite(VMCS_GUEST_RFLAGS, 0x2);
    ROSV_DEBUG("  RFLAGS=0x2");

    /* DR7: default value */
    RosvVmcsWrite(VMCS_GUEST_DR7, 0x400);
    ROSV_DEBUG("  DR7=0x400");

    /* VMCS link pointer: must be 0xFFFFFFFFFFFFFFFF for no VMCS shadowing */
    RosvVmcsWrite(VMCS_GUEST_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFFULL);
    ROSV_DEBUG("  VMCS_LINK_PTR=0xFFFFFFFFFFFFFFFF");

    /* Guest activity state: active (0) */
    RosvVmcsWrite(VMCS_GUEST_ACTIVITY, 0);

    /* Guest interruptibility state: none */
    RosvVmcsWrite(VMCS_GUEST_INTERRUPTIBILITY, 0);

    /* Pending debug exceptions: none */
    RosvVmcsWrite(VMCS_GUEST_PENDING_DBG, 0);

    /* SYSENTER fields (zeroed) */
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_CS, 0);
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_ESP, 0);
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_EIP, 0);

    /* DebugCtl MSR */
    RosvVmcsWrite(VMCS_GUEST_DEBUGCTL, 0);

    ROSV_TRACE("RosvGuestEntrySetupBzImage: SUCCESS - %s mode, "
               "entry=0x%llX, boot_params=0x%llX, cr3=0x%X",
               Is64Bit ? "64-bit long" : "32-bit protected",
               Vm->KernelEntryPoint, Vm->BootParamsAddress,
               Is64Bit ? LINUX_GUEST_PML4_ADDR : 0);

    return STATUS_SUCCESS;
}

/*
 * RosvGuestEntrySetupElf64Direct - Configure guest state for direct ELF64 entry
 *
 * Sets up the VMCS guest-state area for entering an ELF64 kernel directly
 * in 64-bit long mode with identity-mapped paging.  The entry point comes
 * from the ELF e_entry field stored in Vm->KernelEntryPoint.
 */
static NTSTATUS
RosvGuestEntrySetupElf64Direct(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    NTSTATUS Status;
    ULONG64 GuestCr0, GuestCr4, GuestEfer;

    UNREFERENCED_PARAMETER(Vcpu);

    ROSV_TRACE("RosvGuestEntrySetupElf64Direct: configuring 64-bit long mode entry");

    /* Build identity-mapped page tables (4GB, 2MB pages) */
    Status = RosvBuildGuestPageTables(Vm);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvGuestEntrySetupElf64Direct: failed to build page tables, status=0x%08X", Status);
        return Status;
    }

    /* Build 64-bit GDT */
    Status = RosvBuildGuestGdt(Vm, TRUE);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvGuestEntrySetupElf64Direct: failed to build guest GDT, status=0x%08X", Status);
        return Status;
    }

    /* ---- VMCS Guest-State Area ---- */

    /* Control registers: 64-bit long mode with paging */
    GuestCr0 = CR0_PE | CR0_ET | CR0_NE | CR0_PG;
    GuestCr4 = CR4_PAE | CR4_VMXE;
    GuestEfer = EFER_LME | EFER_LMA;

    RosvVmcsWrite(VMCS_GUEST_CR0, GuestCr0);
    RosvVmcsWrite(VMCS_GUEST_CR3, LINUX_GUEST_PML4_ADDR);
    RosvVmcsWrite(VMCS_GUEST_CR4, GuestCr4);
    ROSV_DEBUG("  CR0=0x%llX CR3=0x%X CR4=0x%llX",
               GuestCr0, LINUX_GUEST_PML4_ADDR, GuestCr4);

    /* EFER */
    RosvVmcsWrite(VMCS_GUEST_EFER, GuestEfer);
    ROSV_DEBUG("  EFER=0x%llX", GuestEfer);

    /* CS: 64-bit code segment */
    RosvVmcsWrite(VMCS_GUEST_CS_SEL, 0x08);
    RosvVmcsWrite(VMCS_GUEST_CS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_CS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_CS_ACCESS, SEG_ACCESS_CODE64);
    ROSV_DEBUG("  CS: sel=0x08 base=0 limit=0xFFFFFFFF access=0x%X", SEG_ACCESS_CODE64);

    /* DS, ES, FS, GS, SS: flat data segments */
    RosvVmcsWrite(VMCS_GUEST_DS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_DS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_DS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_DS_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_ES_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_ES_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_ES_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_ES_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_FS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_FS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_FS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_FS_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_GS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_GS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_GS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_GS_ACCESS, SEG_ACCESS_DATA);

    RosvVmcsWrite(VMCS_GUEST_SS_SEL, 0x10);
    RosvVmcsWrite(VMCS_GUEST_SS_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_SS_LIMIT, 0xFFFFFFFF);
    RosvVmcsWrite(VMCS_GUEST_SS_ACCESS, SEG_ACCESS_DATA);

    ROSV_DEBUG("  DS/ES/FS/GS/SS: sel=0x10 base=0 limit=0xFFFFFFFF access=0x%X",
               SEG_ACCESS_DATA);

    /* LDTR: unusable */
    RosvVmcsWrite(VMCS_GUEST_LDTR_SEL, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_LIMIT, 0);
    RosvVmcsWrite(VMCS_GUEST_LDTR_ACCESS, SEG_ACCESS_UNUSABLE);
    ROSV_DEBUG("  LDTR: unusable");

    /* TR: valid TSS (required by VMX even if unused) */
    RosvVmcsWrite(VMCS_GUEST_TR_SEL, 0x18);
    RosvVmcsWrite(VMCS_GUEST_TR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_TR_LIMIT, 0x67);
    RosvVmcsWrite(VMCS_GUEST_TR_ACCESS, SEG_ACCESS_TSS64);
    ROSV_DEBUG("  TR: sel=0x18 base=0 limit=0x67 access=0x%X", SEG_ACCESS_TSS64);

    /* GDTR */
    RosvVmcsWrite(VMCS_GUEST_GDTR_BASE, LINUX_GUEST_GDT_ADDR);
    RosvVmcsWrite(VMCS_GUEST_GDTR_LIMIT, 5 * 8 - 1); /* 5 entries */
    ROSV_DEBUG("  GDTR: base=0x%X limit=0x%X", LINUX_GUEST_GDT_ADDR, 5 * 8 - 1);

    /* IDTR: empty (interrupts disabled at entry) */
    RosvVmcsWrite(VMCS_GUEST_IDTR_BASE, 0);
    RosvVmcsWrite(VMCS_GUEST_IDTR_LIMIT, 0);
    ROSV_DEBUG("  IDTR: base=0 limit=0 (no IDT)");

    /* RIP: ELF e_entry */
    RosvVmcsWrite(VMCS_GUEST_RIP, Vm->KernelEntryPoint);
    ROSV_DEBUG("  RIP=0x%llX (ELF e_entry)", Vm->KernelEntryPoint);

    /* RSI: boot_params address (ELF kernels supporting Linux boot protocol use this) */
    Vcpu->GuestRegs.Rsi = Vm->BootParamsAddress;
    ROSV_DEBUG("  RSI=0x%llX (boot_params)", Vm->BootParamsAddress);

    /* RSP: temporary stack (same location as bzImage path) */
    RosvVmcsWrite(VMCS_GUEST_RSP, LINUX_BOOT_PARAMS_ADDR);
    ROSV_DEBUG("  RSP=0x%X (temporary stack)", LINUX_BOOT_PARAMS_ADDR);

    /* RFLAGS: only reserved bit 1 set */
    RosvVmcsWrite(VMCS_GUEST_RFLAGS, 0x2);
    ROSV_DEBUG("  RFLAGS=0x2");

    /* DR7: default value */
    RosvVmcsWrite(VMCS_GUEST_DR7, 0x400);
    ROSV_DEBUG("  DR7=0x400");

    /* VMCS link pointer: must be 0xFFFFFFFFFFFFFFFF for no VMCS shadowing */
    RosvVmcsWrite(VMCS_GUEST_VMCS_LINK_PTR, 0xFFFFFFFFFFFFFFFFULL);
    ROSV_DEBUG("  VMCS_LINK_PTR=0xFFFFFFFFFFFFFFFF");

    /* Guest activity state: active (0) */
    RosvVmcsWrite(VMCS_GUEST_ACTIVITY, 0);

    /* Guest interruptibility state: none */
    RosvVmcsWrite(VMCS_GUEST_INTERRUPTIBILITY, 0);

    /* Pending debug exceptions: none */
    RosvVmcsWrite(VMCS_GUEST_PENDING_DBG, 0);

    /* SYSENTER fields (zeroed) */
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_CS, 0);
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_ESP, 0);
    RosvVmcsWrite(VMCS_GUEST_SYSENTER_EIP, 0);

    /* DebugCtl MSR */
    RosvVmcsWrite(VMCS_GUEST_DEBUGCTL, 0);

    ROSV_TRACE("RosvGuestEntrySetupElf64Direct: SUCCESS - 64-bit long mode, "
               "entry=0x%llX, boot_params=0x%llX, cr3=0x%X",
               Vm->KernelEntryPoint, Vm->BootParamsAddress,
               LINUX_GUEST_PML4_ADDR);

    return STATUS_SUCCESS;
}

/*
 * RosvGuestEntrySetup - Dispatch guest entry setup by boot contract
 *
 * Routes to the appropriate entry setup function based on the boot
 * contract type stored in Vm->BootContract.
 */
NTSTATUS
RosvGuestEntrySetup(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm)
{
    ROSV_TRACE("RosvGuestEntrySetup: BootContract=%u", Vm->BootContract);

    switch ((ROSV_BOOT_CONTRACT)Vm->BootContract)
    {
        case RosvBootContractBzImage:
            return RosvGuestEntrySetupBzImage(Vcpu, Vm);
        case RosvBootContractElf64Direct:
            return RosvGuestEntrySetupElf64Direct(Vcpu, Vm);
        default:
            ROSV_ERR("RosvGuestEntrySetup: unknown boot contract %u", Vm->BootContract);
            return STATUS_NOT_SUPPORTED;
    }
}
