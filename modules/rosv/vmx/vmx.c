/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VMX enable/disable and capability detection
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vmx.h>
#include <intrin.h>

/* MSR indices (from Intel SDM) */
#define IA32_FEATURE_CONTROL        0x0000003A
#define IA32_VMX_BASIC              0x00000480
#define IA32_VMX_PINBASED_CTLS      0x00000481
#define IA32_VMX_PROCBASED_CTLS     0x00000482
#define IA32_VMX_EXIT_CTLS          0x00000483
#define IA32_VMX_ENTRY_CTLS         0x00000484
#define IA32_VMX_MISC               0x00000485
#define IA32_VMX_CR0_FIXED0         0x00000486
#define IA32_VMX_CR0_FIXED1         0x00000487
#define IA32_VMX_CR4_FIXED0         0x00000488
#define IA32_VMX_CR4_FIXED1         0x00000489
#define IA32_VMX_PROCBASED_CTLS2    0x0000048B
#define IA32_VMX_EPT_VPID_CAP      0x0000048C
#define IA32_VMX_TRUE_PINBASED_CTLS  0x0000048D
#define IA32_VMX_TRUE_PROCBASED_CTLS 0x0000048E
#define IA32_VMX_TRUE_EXIT_CTLS      0x0000048F
#define IA32_VMX_TRUE_ENTRY_CTLS     0x00000490

/* IA32_FEATURE_CONTROL bits */
#define FEATURE_CONTROL_LOCK            (1ULL << 0)
#define FEATURE_CONTROL_VMXON_OUTSIDE   (1ULL << 2)

/* CR4 bits */
#define CR4_VMXE    (1ULL << 13)

/* CPUID.1:ECX bit 5 = VMX */
#define CPUID_1_ECX_VMX (1U << 5)

NTSTATUS
RosvVmxDetectCapabilities(
    _Out_ PVMX_CAPABILITIES Caps)
{
    int CpuInfo[4];
    ULONG64 MsrValue;

    if (Caps == NULL)
    {
        ROSV_ERR("RosvVmxDetectCapabilities: Caps is NULL");
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvVmxDetectCapabilities: begin");

    RtlZeroMemory(Caps, sizeof(*Caps));

    /* Step 1: Check CPUID.1:ECX[5] for VMX support */
    __cpuid(CpuInfo, 1);
    ROSV_TRACE("  CPUID.1: ECX=0x%08X EDX=0x%08X",
               (ULONG)CpuInfo[2], (ULONG)CpuInfo[3]);

    if (!(CpuInfo[2] & CPUID_1_ECX_VMX))
    {
        ROSV_ERR("  VMX not supported (CPUID.1:ECX[5]=0)");
        return STATUS_NOT_SUPPORTED;
    }
    ROSV_TRACE("  VMX supported (CPUID.1:ECX[5]=1)");

    /* Step 2: Read IA32_FEATURE_CONTROL */
    MsrValue = __readmsr(IA32_FEATURE_CONTROL);
    ROSV_TRACE("  IA32_FEATURE_CONTROL = 0x%016llX", MsrValue);

    if (!(MsrValue & FEATURE_CONTROL_LOCK))
    {
        ROSV_ERR("  IA32_FEATURE_CONTROL lock bit not set - BIOS did not enable VMX");
        return STATUS_NOT_SUPPORTED;
    }
    ROSV_TRACE("  IA32_FEATURE_CONTROL lock bit set");

    if (!(MsrValue & FEATURE_CONTROL_VMXON_OUTSIDE))
    {
        ROSV_ERR("  VMXON outside SMX not enabled (bit 2=0)");
        return STATUS_NOT_SUPPORTED;
    }
    ROSV_TRACE("  VMXON outside SMX enabled (bit 2=1)");

    /* Step 3: Read IA32_VMX_BASIC */
    MsrValue = __readmsr(IA32_VMX_BASIC);
    ROSV_TRACE("  IA32_VMX_BASIC = 0x%016llX", MsrValue);

    Caps->VmcsRevisionId = (ULONG)(MsrValue & 0x7FFFFFFF);
    Caps->VmcsRegionSize = (ULONG)((MsrValue >> 32) & 0x1FFF);
    Caps->VmcsMemoryType = (BOOLEAN)((MsrValue >> 50) & 0xF);
    Caps->TrueCtlsAvailable = (BOOLEAN)((MsrValue >> 55) & 1);

    ROSV_TRACE("  VMCS revision ID = 0x%08X", Caps->VmcsRevisionId);
    ROSV_TRACE("  VMCS region size = %u bytes", Caps->VmcsRegionSize);
    ROSV_TRACE("  VMCS memory type = %u (0=UC, 6=WB)", Caps->VmcsMemoryType);
    ROSV_TRACE("  True controls available = %u", Caps->TrueCtlsAvailable);

    if (Caps->VmcsRegionSize == 0)
    {
        Caps->VmcsRegionSize = 4096;
        ROSV_TRACE("  VMCS region size 0, defaulting to 4096");
    }

    /* Step 4: Read pin-based controls */
    if (Caps->TrueCtlsAvailable)
    {
        MsrValue = __readmsr(IA32_VMX_TRUE_PINBASED_CTLS);
        ROSV_TRACE("  IA32_VMX_TRUE_PINBASED_CTLS = 0x%016llX", MsrValue);
    }
    else
    {
        MsrValue = __readmsr(IA32_VMX_PINBASED_CTLS);
        ROSV_TRACE("  IA32_VMX_PINBASED_CTLS = 0x%016llX", MsrValue);
    }
    Caps->PinBasedAllowed0 = (ULONG)(MsrValue & 0xFFFFFFFF);
    Caps->PinBasedAllowed1 = (ULONG)(MsrValue >> 32);
    ROSV_TRACE("  Pin-based allowed0=0x%08X allowed1=0x%08X",
               Caps->PinBasedAllowed0, Caps->PinBasedAllowed1);

    /* Step 5: Read primary proc-based controls */
    if (Caps->TrueCtlsAvailable)
    {
        MsrValue = __readmsr(IA32_VMX_TRUE_PROCBASED_CTLS);
        ROSV_TRACE("  IA32_VMX_TRUE_PROCBASED_CTLS = 0x%016llX", MsrValue);
    }
    else
    {
        MsrValue = __readmsr(IA32_VMX_PROCBASED_CTLS);
        ROSV_TRACE("  IA32_VMX_PROCBASED_CTLS = 0x%016llX", MsrValue);
    }
    Caps->ProcBasedAllowed0 = (ULONG)(MsrValue & 0xFFFFFFFF);
    Caps->ProcBasedAllowed1 = (ULONG)(MsrValue >> 32);
    ROSV_TRACE("  Proc-based allowed0=0x%08X allowed1=0x%08X",
               Caps->ProcBasedAllowed0, Caps->ProcBasedAllowed1);

    /* Step 6: Check for secondary proc-based controls */
    Caps->SecondaryCtlsAvailable =
        (Caps->ProcBasedAllowed1 & VMX_PROC_SECONDARY_CTLS) ? TRUE : FALSE;
    ROSV_TRACE("  Secondary controls available = %u", Caps->SecondaryCtlsAvailable);

    if (Caps->SecondaryCtlsAvailable)
    {
        MsrValue = __readmsr(IA32_VMX_PROCBASED_CTLS2);
        ROSV_TRACE("  IA32_VMX_PROCBASED_CTLS2 = 0x%016llX", MsrValue);
        Caps->ProcBased2Allowed0 = (ULONG)(MsrValue & 0xFFFFFFFF);
        Caps->ProcBased2Allowed1 = (ULONG)(MsrValue >> 32);
        ROSV_TRACE("  Proc-based2 allowed0=0x%08X allowed1=0x%08X",
                   Caps->ProcBased2Allowed0, Caps->ProcBased2Allowed1);

        /* Check EPT support */
        if (Caps->ProcBased2Allowed1 & VMX_PROC2_EPT)
        {
            ROSV_TRACE("  EPT supported");
        }
        else
        {
            ROSV_WARN("  EPT NOT supported");
        }

        /* Check unrestricted guest support */
        if (Caps->ProcBased2Allowed1 & VMX_PROC2_UNRESTRICTED_GUEST)
        {
            ROSV_TRACE("  Unrestricted guest supported");
        }
        else
        {
            ROSV_WARN("  Unrestricted guest NOT supported");
        }
    }

    /* Step 7: Read exit controls */
    if (Caps->TrueCtlsAvailable)
    {
        MsrValue = __readmsr(IA32_VMX_TRUE_EXIT_CTLS);
        ROSV_TRACE("  IA32_VMX_TRUE_EXIT_CTLS = 0x%016llX", MsrValue);
    }
    else
    {
        MsrValue = __readmsr(IA32_VMX_EXIT_CTLS);
        ROSV_TRACE("  IA32_VMX_EXIT_CTLS = 0x%016llX", MsrValue);
    }
    Caps->ExitCtlsAllowed0 = (ULONG)(MsrValue & 0xFFFFFFFF);
    Caps->ExitCtlsAllowed1 = (ULONG)(MsrValue >> 32);
    ROSV_TRACE("  Exit allowed0=0x%08X allowed1=0x%08X",
               Caps->ExitCtlsAllowed0, Caps->ExitCtlsAllowed1);

    /* Step 8: Read entry controls */
    if (Caps->TrueCtlsAvailable)
    {
        MsrValue = __readmsr(IA32_VMX_TRUE_ENTRY_CTLS);
        ROSV_TRACE("  IA32_VMX_TRUE_ENTRY_CTLS = 0x%016llX", MsrValue);
    }
    else
    {
        MsrValue = __readmsr(IA32_VMX_ENTRY_CTLS);
        ROSV_TRACE("  IA32_VMX_ENTRY_CTLS = 0x%016llX", MsrValue);
    }
    Caps->EntryCtlsAllowed0 = (ULONG)(MsrValue & 0xFFFFFFFF);
    Caps->EntryCtlsAllowed1 = (ULONG)(MsrValue >> 32);
    ROSV_TRACE("  Entry allowed0=0x%08X allowed1=0x%08X",
               Caps->EntryCtlsAllowed0, Caps->EntryCtlsAllowed1);

    /* Step 9: Read EPT/VPID capabilities */
    if (Caps->SecondaryCtlsAvailable &&
        (Caps->ProcBased2Allowed1 & VMX_PROC2_EPT))
    {
        Caps->EptVpidCap = __readmsr(IA32_VMX_EPT_VPID_CAP);
        ROSV_TRACE("  IA32_VMX_EPT_VPID_CAP = 0x%016llX", Caps->EptVpidCap);
    }

    /* Step 10: Read CR0/CR4 fixed bits */
    Caps->Cr0Fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
    Caps->Cr0Fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);
    Caps->Cr4Fixed0 = __readmsr(IA32_VMX_CR4_FIXED0);
    Caps->Cr4Fixed1 = __readmsr(IA32_VMX_CR4_FIXED1);

    ROSV_TRACE("  CR0 fixed0=0x%016llX fixed1=0x%016llX",
               Caps->Cr0Fixed0, Caps->Cr0Fixed1);
    ROSV_TRACE("  CR4 fixed0=0x%016llX fixed1=0x%016llX",
               Caps->Cr4Fixed0, Caps->Cr4Fixed1);

    ROSV_TRACE("RosvVmxDetectCapabilities: success");
    return STATUS_SUCCESS;
}

NTSTATUS
RosvVmxEnable(
    _In_ PVMX_CAPABILITIES Caps,
    _Out_ PHYSICAL_ADDRESS *VmxonRegionPhys,
    _Out_ PVOID *VmxonRegionVirt)
{
    PVOID Region;
    PHYSICAL_ADDRESS PhysAddr;
    PHYSICAL_ADDRESS MaxAddr;
    ULONG64 Cr0, Cr4;
    VMX_RESULT VmxResult;

    if (Caps == NULL || VmxonRegionPhys == NULL || VmxonRegionVirt == NULL)
    {
        ROSV_ERR("RosvVmxEnable: invalid parameters (Caps=%p PhysOut=%p VirtOut=%p)",
                 Caps, VmxonRegionPhys, VmxonRegionVirt);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvVmxEnable: begin, revision=0x%08X", Caps->VmcsRevisionId);

    /* Step 1: Allocate 4KB-aligned VMXON region */
    MaxAddr.QuadPart = 0xFFFFFFFFLL; /* Must be below 4GB for some platforms */
    Region = MmAllocateContiguousMemory(PAGE_SIZE, MaxAddr);
    if (!Region)
    {
        ROSV_ERR("  Failed to allocate VMXON region (4KB contiguous)");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Region, PAGE_SIZE);
    PhysAddr = MmGetPhysicalAddress(Region);

    ROSV_TRACE("  VMXON region VA=%p PA=0x%016llX", Region, PhysAddr.QuadPart);

    /* Step 2: Write VMCS revision ID to first 4 bytes */
    *(PULONG)Region = Caps->VmcsRevisionId;
    ROSV_TRACE("  Wrote revision ID 0x%08X to VMXON region", Caps->VmcsRevisionId);

    /* Step 3: Adjust CR0 for VMX fixed bits */
    Cr0 = __readcr0();
    ROSV_TRACE("  Current CR0 = 0x%016llX", Cr0);
    Cr0 |= Caps->Cr0Fixed0;
    Cr0 &= Caps->Cr0Fixed1;
    __writecr0(Cr0);
    ROSV_TRACE("  Adjusted CR0 = 0x%016llX (fixed0=0x%llX, fixed1=0x%llX)",
               Cr0, Caps->Cr0Fixed0, Caps->Cr0Fixed1);

    /* Step 4: Set CR4.VMXE (bit 13) and adjust for VMX fixed bits */
    Cr4 = __readcr4();
    ROSV_TRACE("  Current CR4 = 0x%016llX", Cr4);
    Cr4 |= CR4_VMXE;
    Cr4 |= Caps->Cr4Fixed0;
    Cr4 &= Caps->Cr4Fixed1;
    __writecr4(Cr4);
    ROSV_TRACE("  Adjusted CR4 = 0x%016llX (VMXE set, fixed bits applied)", Cr4);

    /* Step 5: Execute VMXON */
    VmxResult = RosvAsmVmxOn(&PhysAddr);
    if (VmxResult != VmxOk)
    {
        ROSV_ERR("  VMXON failed with result %u (1=CF/invalid, 2=ZF/valid)",
                 (ULONG)VmxResult);

        /* Revert CR4.VMXE */
        Cr4 = __readcr4();
        Cr4 &= ~CR4_VMXE;
        __writecr4(Cr4);

        MmFreeContiguousMemory(Region);
        return STATUS_UNSUCCESSFUL;
    }

    ROSV_TRACE("  VMXON succeeded");

    *VmxonRegionPhys = PhysAddr;
    *VmxonRegionVirt = Region;

    ROSV_TRACE("RosvVmxEnable: success");
    return STATUS_SUCCESS;
}

VOID
RosvVmxDisable(
    _In_ PHYSICAL_ADDRESS VmxonRegionPhys,
    _In_ PVOID VmxonRegionVirt)
{
    VMX_RESULT VmxResult;
    ULONG64 Cr4;

    ROSV_TRACE("RosvVmxDisable: begin, VA=%p PA=0x%016llX",
               VmxonRegionVirt, VmxonRegionPhys.QuadPart);

    /* Step 1: VMXOFF */
    VmxResult = RosvAsmVmxOff();
    if (VmxResult != VmxOk)
    {
        ROSV_ERR("  VMXOFF failed with result %u", (ULONG)VmxResult);
    }
    else
    {
        ROSV_TRACE("  VMXOFF succeeded");
    }

    /* Step 2: Clear CR4.VMXE */
    Cr4 = __readcr4();
    Cr4 &= ~CR4_VMXE;
    __writecr4(Cr4);
    ROSV_TRACE("  CR4.VMXE cleared, CR4=0x%016llX", Cr4);

    /* Step 3: Free VMXON region */
    if (VmxonRegionVirt)
    {
        MmFreeContiguousMemory(VmxonRegionVirt);
        ROSV_TRACE("  VMXON region freed");
    }

    ROSV_TRACE("RosvVmxDisable: done");
}
