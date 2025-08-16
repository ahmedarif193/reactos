/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 CPU Detection and Initialization
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Feature flags for ARM64 */
#define ARM64_FEATURE_FP        0x00000001
#define ARM64_FEATURE_ADVSIMD   0x00000002
#define ARM64_FEATURE_SVE       0x00000004
#define ARM64_FEATURE_SVE2      0x00000008
#define ARM64_FEATURE_SHA1      0x00000010
#define ARM64_FEATURE_SHA256    0x00000020
#define ARM64_FEATURE_SHA512    0x00000040
#define ARM64_FEATURE_SHA3      0x00000080
#define ARM64_FEATURE_SM3       0x00000100
#define ARM64_FEATURE_SM4       0x00000200
#define ARM64_FEATURE_ATOMICS   0x00000400
#define ARM64_FEATURE_RDM       0x00000800
#define ARM64_FEATURE_CRC32     0x00001000
#define ARM64_FEATURE_DCPOP     0x00002000
#define ARM64_FEATURE_JSCVT     0x00004000
#define ARM64_FEATURE_FCMA      0x00008000

/* Physical address size flags */
#define ARM64_PA_SIZE_32        0x00010000
#define ARM64_PA_SIZE_36        0x00020000
#define ARM64_PA_SIZE_40        0x00040000
#define ARM64_PA_SIZE_42        0x00080000
#define ARM64_PA_SIZE_44        0x00100000
#define ARM64_PA_SIZE_48        0x00200000
#define ARM64_PA_SIZE_52        0x00400000

/* CPU Type definitions */
#define CPU_ARM64               0xAA64

/* GLOBALS *******************************************************************/

/* CPU Features detected */
ULONG KeARM64Features = 0;
ULONG KeARM64CacheType = 0;
ULONG KeARM64CacheSize = 0;

/* CPU Identification */
ULONG KeARM64ProcessorId = 0;
ULONG KeARM64ProcessorRevision = 0;
ULONG KeARM64ImplementerCode = 0;
ULONG KeARM64VariantCode = 0;

/* FUNCTIONS *****************************************************************/

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    PKPCR Pcr;
    
    /* Get PCR from TPIDR_EL1 */
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    
    /* Return processor number */
    return Pcr->Prcb->Number;
}

ULONG64
NTAPI
KiReadSystemRegister(IN ULONG Register)
{
    ULONG64 Value = 0;
    
    /* Read ARM64 system registers */
    switch (Register)
    {
        case 0: /* MIDR_EL1 - Main ID Register */
            __asm__ __volatile__("mrs %0, midr_el1" : "=r" (Value));
            break;
            
        case 1: /* ID_AA64PFR0_EL1 - Processor Feature Register 0 */
            __asm__ __volatile__("mrs %0, id_aa64pfr0_el1" : "=r" (Value));
            break;
            
        case 2: /* ID_AA64MMFR0_EL1 - Memory Model Feature Register 0 */
            __asm__ __volatile__("mrs %0, id_aa64mmfr0_el1" : "=r" (Value));
            break;
            
        case 3: /* CTR_EL0 - Cache Type Register */
            __asm__ __volatile__("mrs %0, ctr_el0" : "=r" (Value));
            break;
            
        case 4: /* DCZID_EL0 - Data Cache Zero ID Register */
            __asm__ __volatile__("mrs %0, dczid_el0" : "=r" (Value));
            break;
    }
    
    return Value;
}

VOID
NTAPI
KiDetectARM64Features(VOID)
{
    ULONG64 PfrValue, MmfrValue;
    
    /* Read Processor Feature Register */
    PfrValue = KiReadSystemRegister(1);
    
    /* Check for Floating Point support */
    if ((PfrValue & 0xF) != 0xF)
    {
        KeARM64Features |= ARM64_FEATURE_FP;
    }
    
    /* Check for Advanced SIMD (NEON) support */
    if (((PfrValue >> 20) & 0xF) != 0xF)
    {
        KeARM64Features |= ARM64_FEATURE_ADVSIMD;
    }
    
    /* Check for SVE (Scalable Vector Extension) support */
    if (((PfrValue >> 32) & 0xF) != 0)
    {
        KeARM64Features |= ARM64_FEATURE_SVE;
    }
    
    /* Read Memory Model Feature Register */
    MmfrValue = KiReadSystemRegister(2);
    
    /* Check Physical Address Range */
    switch (MmfrValue & 0xF)
    {
        case 0: /* 32 bits, 4GB */
            KeARM64Features |= ARM64_PA_SIZE_32;
            break;
        case 1: /* 36 bits, 64GB */
            KeARM64Features |= ARM64_PA_SIZE_36;
            break;
        case 2: /* 40 bits, 1TB */
            KeARM64Features |= ARM64_PA_SIZE_40;
            break;
        case 3: /* 42 bits, 4TB */
            KeARM64Features |= ARM64_PA_SIZE_42;
            break;
        case 4: /* 44 bits, 16TB */
            KeARM64Features |= ARM64_PA_SIZE_44;
            break;
        case 5: /* 48 bits, 256TB */
            KeARM64Features |= ARM64_PA_SIZE_48;
            break;
        case 6: /* 52 bits, 4PB */
            KeARM64Features |= ARM64_PA_SIZE_52;
            break;
    }
    
    DPRINT1("ARM64 Features detected: 0x%08lx\n", KeARM64Features);
}

VOID
NTAPI
KiGetCacheInformation(VOID)
{
    ULONG64 CtrValue;
    ULONG DminLine, IminLine;
    
    /* Read Cache Type Register */
    CtrValue = KiReadSystemRegister(3);
    
    /* Extract cache line sizes */
    DminLine = (CtrValue >> 16) & 0xF;
    IminLine = CtrValue & 0xF;
    
    /* Calculate actual cache line sizes in bytes */
    KeARM64CacheSize = 4 << DminLine;  /* Data cache line size */
    
    /* Determine cache type from upper bits */
    KeARM64CacheType = (CtrValue >> 24) & 0xF;
    
    DPRINT1("ARM64 Cache: Type=%lu, DCache Line=%lu bytes\n", 
            KeARM64CacheType, KeARM64CacheSize);
}

VOID
NTAPI
KiIdentifyProcessor(VOID)
{
    ULONG64 MidrValue;
    
    /* Read Main ID Register */
    MidrValue = KiReadSystemRegister(0);
    
    /* Extract processor information */
    KeARM64ImplementerCode = (MidrValue >> 24) & 0xFF;
    KeARM64VariantCode = (MidrValue >> 20) & 0xF;
    KeARM64ProcessorId = (MidrValue >> 4) & 0xFFF;
    KeARM64ProcessorRevision = MidrValue & 0xF;
    
    /* Set processor name based on implementer */
    switch (KeARM64ImplementerCode)
    {
        case 0x41: /* ARM */
            switch (KeARM64ProcessorId)
            {
                case 0xD03: /* Cortex-A53 */
                    DPRINT1("ARM Cortex-A53 detected\n");
                    break;
                case 0xD07: /* Cortex-A57 */
                    DPRINT1("ARM Cortex-A57 detected\n");
                    break;
                case 0xD08: /* Cortex-A72 */
                    DPRINT1("ARM Cortex-A72 detected\n");
                    break;
                case 0xD09: /* Cortex-A73 */
                    DPRINT1("ARM Cortex-A73 detected\n");
                    break;
                case 0xD0A: /* Cortex-A75 */
                    DPRINT1("ARM Cortex-A75 detected\n");
                    break;
                case 0xD0B: /* Cortex-A76 */
                    DPRINT1("ARM Cortex-A76 detected\n");
                    break;
                case 0xD0C: /* Neoverse N1 */
                    DPRINT1("ARM Neoverse N1 detected\n");
                    break;
                case 0xD0D: /* Cortex-A77 */
                    DPRINT1("ARM Cortex-A77 detected\n");
                    break;
                case 0xD40: /* Cortex-A76AE */
                    DPRINT1("ARM Cortex-A76AE detected\n");
                    break;
                case 0xD41: /* Cortex-A78 */
                    DPRINT1("ARM Cortex-A78 detected\n");
                    break;
                default:
                    DPRINT1("Unknown ARM processor: 0x%03lx\n", KeARM64ProcessorId);
                    break;
            }
            break;
            
        case 0x42: /* Broadcom */
            DPRINT1("Broadcom ARM processor detected\n");
            break;
            
        case 0x43: /* Cavium */
            DPRINT1("Cavium ARM processor detected\n");
            break;
            
        case 0x44: /* DEC */
            DPRINT1("DEC ARM processor detected\n");
            break;
            
        case 0x48: /* HiSilicon */
            DPRINT1("HiSilicon ARM processor detected\n");
            break;
            
        case 0x4E: /* NVIDIA */
            DPRINT1("NVIDIA ARM processor detected\n");
            break;
            
        case 0x50: /* Applied Micro */
            DPRINT1("Applied Micro ARM processor detected\n");
            break;
            
        case 0x51: /* Qualcomm */
            DPRINT1("Qualcomm ARM processor detected\n");
            break;
            
        case 0x53: /* Samsung */
            DPRINT1("Samsung ARM processor detected\n");
            break;
            
        case 0x56: /* Marvell */
            DPRINT1("Marvell ARM processor detected\n");
            break;
            
        case 0x61: /* Apple */
            DPRINT1("Apple ARM processor detected\n");
            break;
            
        case 0x66: /* Faraday */
            DPRINT1("Faraday ARM processor detected\n");
            break;
            
        case 0x69: /* Intel */
            DPRINT1("Intel ARM processor detected\n");
            break;
            
        default:
            DPRINT1("Unknown implementer: 0x%02lx\n", KeARM64ImplementerCode);
            break;
    }
    
    DPRINT1("Processor: Implementer=0x%02lx, Variant=0x%x, PartNum=0x%03lx, Rev=0x%x\n",
            KeARM64ImplementerCode, KeARM64VariantCode, 
            KeARM64ProcessorId, KeARM64ProcessorRevision);
}

VOID
NTAPI
KiSetProcessorType(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    
    /* Set the processor type information */
    Prcb->CpuType = CPU_ARM64;
    Prcb->CpuID = 1;  /* Enable CPUID support flag */
    Prcb->CpuStep = (USHORT)((KeARM64VariantCode << 8) | KeARM64ProcessorRevision);
    
    /* Set processor features */
    Prcb->FeatureBits = KeARM64Features;
}

VOID
NTAPI
KiInitializeCpu(IN PKPRCB Prcb)
{
    /* Detect processor features */
    KiDetectARM64Features();
    
    /* Get cache information */
    KiGetCacheInformation();
    
    /* Identify the processor */
    KiIdentifyProcessor();
    
    /* Set processor type in PRCB */
    KiSetProcessorType();
    
    /* Enable CPU features */
    
    /* Enable floating point and NEON if available */
    if (KeARM64Features & ARM64_FEATURE_FP)
    {
        /* Enable FP/SIMD access from EL0 and EL1 */
        ULONG64 CpacrValue;
        __asm__ __volatile__("mrs %0, cpacr_el1" : "=r" (CpacrValue));
        CpacrValue |= (3 << 20);  /* FPEN bits */
        __asm__ __volatile__("msr cpacr_el1, %0" : : "r" (CpacrValue));
        __asm__ __volatile__("isb");
    }
    
    /* Initialize performance monitoring */
    KiInitializePerformanceMonitoring();
    
    /* Initialize debug registers */
    KiInitializeDebugRegisters();
    
    DPRINT1("ARM64 CPU initialized\n");
}

VOID
NTAPI
KiInitializePerformanceMonitoring(VOID)
{
    /* Enable cycle counter for all exception levels */
    ULONG64 PmcrValue;
    
    /* Read Performance Monitor Control Register */
    __asm__ __volatile__("mrs %0, pmcr_el0" : "=r" (PmcrValue));
    
    /* Enable cycle counter and reset */
    PmcrValue |= (1 << 0) | (1 << 2);  /* E and C bits */
    
    /* Write back */
    __asm__ __volatile__("msr pmcr_el0, %0" : : "r" (PmcrValue));
    
    /* Enable user access to cycle counter */
    __asm__ __volatile__("msr pmuserenr_el0, %0" : : "r" (1ULL));
    
    __asm__ __volatile__("isb");
}

VOID
NTAPI
KiInitializeDebugRegisters(VOID)
{
    /* Clear all breakpoint registers */
    __asm__ __volatile__("msr dbgbvr0_el1, xzr");
    __asm__ __volatile__("msr dbgbvr1_el1, xzr");
    __asm__ __volatile__("msr dbgbvr2_el1, xzr");
    __asm__ __volatile__("msr dbgbvr3_el1, xzr");
    __asm__ __volatile__("msr dbgbvr4_el1, xzr");
    __asm__ __volatile__("msr dbgbvr5_el1, xzr");
    
    /* Clear all watchpoint registers */
    __asm__ __volatile__("msr dbgwvr0_el1, xzr");
    __asm__ __volatile__("msr dbgwvr1_el1, xzr");
    __asm__ __volatile__("msr dbgwvr2_el1, xzr");
    __asm__ __volatile__("msr dbgwvr3_el1, xzr");
    
    __asm__ __volatile__("isb");
}

/* Forward declarations */
VOID NTAPI KiInitializePerformanceMonitoring(VOID);
VOID NTAPI KiInitializeDebugRegisters(VOID);

/* Performance monitoring stub - ARM64 doesn't have x86 perf counters */
VOID
NTAPI
Ki386PerfEnd(VOID)
{
    /* No-op on ARM64 */
    return;
}

BOOLEAN
NTAPI
KeInvalidateAllCaches(VOID)
{
    /* Invalidate all ARM64 caches */
    __asm__ __volatile__(
        /* Invalidate instruction cache */
        "ic iallu\n\t"
        /* Ensure completion */
        "dsb sy\n\t"
        "isb"
    );
    
    /* Data cache clean and invalidate would require iterating through cache levels */
    /* For now, just ensure memory barriers */
    __asm__ __volatile__("dmb sy");
    
    return TRUE;
}