/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Firmware hart discovery, SBI startup and remote interrupts
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntifs.h>
#include <arc/arc.h>
#include <ndk/halfuncs.h>
#include <reactos/riscv64/fdtlib.h>
#include "halp.h"

ULONG_PTR HalpRiscvHartIds[MAXIMUM_PROCESSORS];
ULONG HalpRiscvHartCount;
ULONG HalpRiscvStartedProcessors = 1;
static BOOLEAN HalpRiscvHasSmp;

typedef struct _HAL_RISCV_AP_DATA
{
    ULONG64 StartupSatp;
    ULONG64 KernelSatp;
    ULONG64 Stack;
    ULONG64 Entry;
    ULONG64 Context;
    ULONG64 Pcr;
    ULONG64 Sstatus;
    ULONG64 VirtualEntry;
} HAL_RISCV_AP_DATA;
C_ASSERT(sizeof(HAL_RISCV_AP_DATA) == 64);

extern UCHAR HalpRiscvApTrampoline[], HalpRiscvApTrampolineEnd[];
extern VOID HalpRiscvApVirtualEntry(VOID);
static PULONG64 HalpRiscvStartupPages;
static PHYSICAL_ADDRESS HalpRiscvStartupPhysical;

static BOOLEAN
HalpRiscvCompatibleHart(const RISCV_FDT *Fdt, ULONG Cpu)
{
    static const CHAR *Required[] = {"i", "m", "a", "f", "d", "c", "zicsr", "zifencei"};
    const CHAR *Isa;
    const VOID *Property;
    ULONG Length, Index, Letters = 0;

    Property = RiscvFdtGetProperty(Fdt, Cpu, "mmu-type", &Length);
    if (!RiscvFdtStringListContains(Property, Length, "riscv,sv39") &&
        !RiscvFdtStringListContains(Property, Length, "riscv,sv48") &&
        !RiscvFdtStringListContains(Property, Length, "riscv,sv57")) return FALSE;
    Property = RiscvFdtGetProperty(Fdt, Cpu, "riscv,isa-extensions", &Length);
    if (Property)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Required); ++Index)
            if (!RiscvFdtStringListContains(Property, Length, Required[Index])) return FALSE;
        Property = RiscvFdtGetProperty(Fdt, Cpu, "riscv,isa-base", &Length);
        return RiscvFdtStringListContains(Property, Length, "rv64i");
    }

    /* Legacy ISA strings predate the separate Zicsr/Zifencei extensions.
     * Optional extension names after '_' must not supply base ISA letters. */
    Isa = RiscvFdtGetProperty(Fdt, Cpu, "riscv,isa", &Length);
    if (!Isa || Length < 5 || memcmp(Isa, "rv64", 4)) return FALSE;
    for (Index = 4; Index < Length && Isa[Index] && Isa[Index] != '_'; ++Index)
    {
        if (Isa[Index] == 'g') Letters |= 0x1F;
        else if (Isa[Index] == 'i') Letters |= 1;
        else if (Isa[Index] == 'm') Letters |= 2;
        else if (Isa[Index] == 'a') Letters |= 4;
        else if (Isa[Index] == 'f') Letters |= 8;
        else if (Isa[Index] == 'd') Letters |= 16;
        else if (Isa[Index] == 'c') Letters |= 32;
    }
    return Letters == 0x3F;
}

BOOLEAN
HalpRiscvDiscoverHarts(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    RISCV_FDT Fdt;
    ULONG Cpus, Cpu, Length, Index;
    ULONG64 Hart, Size;
    const VOID *Property;
    PRISCV64_LOADER_BLOCK Block = &LoaderBlock->u.Riscv64;

    HalpRiscvHartIds[0] = Block->BootHartId;
    HalpRiscvHartCount = 1;
    HalpRiscvHasSmp = HalpRiscvSbiExtensionAvailable(RISCV_SBI_EXTENSION_HSM) &&
                      HalpRiscvSbiExtensionAvailable(RISCV_SBI_EXTENSION_IPI) &&
                      HalpRiscvSbiExtensionAvailable(RISCV_SBI_EXTENSION_RFENCE);
    if (!HalpRiscvHasSmp) return TRUE;
    if (!RiscvFdtOpen((const VOID *)(ULONG_PTR)Block->DeviceTree,
                      Block->DeviceTreeSize, &Fdt) ||
        !RiscvFdtFindNode(&Fdt, "/cpus", &Cpus, NULL)) return FALSE;

    for (Cpu = RiscvFdtFirstChild(&Fdt, Cpus);
         Cpu != RISCV_FDT_NO_NODE;
         Cpu = RiscvFdtNextSibling(&Fdt, Cpu))
    {
        Property = RiscvFdtGetProperty(&Fdt, Cpu, "device_type", &Length);
        if (!RiscvFdtStringListContains(Property, Length, "cpu")) continue;
        Property = RiscvFdtGetProperty(&Fdt, Cpu, "status", &Length);
        if (Property && !RiscvFdtStringListContains(Property, Length, "okay") &&
            !RiscvFdtStringListContains(Property, Length, "ok")) continue;
        if (!RiscvFdtReadReg(&Fdt, Cpu, Cpus, 0, &Hart, &Size)) return FALSE;
        if (Hart == Block->BootHartId) continue;
        if (!HalpRiscvCompatibleHart(&Fdt, Cpu))
        {
            DbgPrint("RISC-V: skipping hart %I64u without RV64GC/Sv39 support\n", Hart);
            continue;
        }
        for (Index = 0; Index < HalpRiscvHartCount; ++Index)
            if (HalpRiscvHartIds[Index] == Hart) return FALSE;
        if (HalpRiscvHartCount == MAXIMUM_PROCESSORS) continue;
        HalpRiscvHartIds[HalpRiscvHartCount++] = Hart;
    }
    return TRUE;
}

BOOLEAN
NTAPI
HalpRiscvQueryProcessorHartId(ULONG Number, PULONG_PTR HartId)
{
    if (Number >= HalpRiscvHartCount) return FALSE;
    *HartId = HalpRiscvHartIds[Number];
    return TRUE;
}

BOOLEAN
NTAPI
HalStartNextProcessor(PLOADER_PARAMETER_BLOCK LoaderBlock,
                      PKPROCESSOR_STATE State)
{
    PHYSICAL_ADDRESS Low = {{0}}, High, Boundary = {{0}};
    HAL_RISCV_AP_DATA *Data;
    PULONG64 Root, L1, L0, KernelRoot;
    ULONG64 CodePa, Satp;
    SIZE_T CodeSize = HalpRiscvApTrampolineEnd - HalpRiscvApTrampoline;
    RISCV_SBI_RETURN Result;

    UNREFERENCED_PARAMETER(LoaderBlock);
    if (!HalpRiscvHasSmp || HalpRiscvStartedProcessors >= HalpRiscvHartCount)
        return FALSE;
    if (!HalpRiscvStartupPages)
    {
        High.QuadPart = RISCV64_LOADER_PHYSICAL_LIMIT - 1;
        HalpRiscvStartupPages = MmAllocateContiguousMemorySpecifyCache(
            4 * PAGE_SIZE, Low, High, Boundary, MmCached);
        if (!HalpRiscvStartupPages) return FALSE;
        HalpRiscvStartupPhysical = MmGetPhysicalAddress(HalpRiscvStartupPages);
    }
    ASSERT(CodeSize < PAGE_SIZE / 2);
    RtlZeroMemory(HalpRiscvStartupPages, 4 * PAGE_SIZE);
    Root = HalpRiscvStartupPages;
    L1 = Root + PAGE_SIZE / sizeof(ULONG64);
    L0 = L1 + PAGE_SIZE / sizeof(ULONG64);
    CodePa = HalpRiscvStartupPhysical.QuadPart + 3 * PAGE_SIZE;
    Satp = State->SpecialRegisters.Satp;
    KernelRoot = (PULONG64)(ULONG_PTR)(RISCV64_LOADER_DIRECT_MAP_BASE +
        ((Satp & RISCV64_LOADER_SATP_PPN_MASK) << PAGE_SHIFT));
    /* The upper canonical half is shared. Only the startup instruction page
     * is identity-mapped, read/execute, in this private temporary root. */
    RtlCopyMemory(Root + 256, KernelRoot + 256, PAGE_SIZE / 2);
    Root[(CodePa >> 30) & 511] =
        ((HalpRiscvStartupPhysical.QuadPart + PAGE_SIZE) >> 2) | 1;
    L1[(CodePa >> 21) & 511] =
        ((HalpRiscvStartupPhysical.QuadPart + 2 * PAGE_SIZE) >> 2) | 1;
    L0[(CodePa >> 12) & 511] = (CodePa >> 2) | 0x4B; /* V R X A */
    RtlCopyMemory((PUCHAR)Root + 3 * PAGE_SIZE, HalpRiscvApTrampoline, CodeSize);
    Data = (HAL_RISCV_AP_DATA *)((PUCHAR)Root + 3 * PAGE_SIZE + PAGE_SIZE / 2);
    Data->StartupSatp = RISCV64_LOADER_SATP_MODE_SV39 |
                       (HalpRiscvStartupPhysical.QuadPart >> PAGE_SHIFT);
    Data->KernelSatp = Satp;
    Data->Stack = State->ContextFrame.Sp;
    Data->Entry = State->ContextFrame.Pc;
    Data->Context = State->ContextFrame.A0;
    Data->Pcr = State->SpecialRegisters.Sscratch;
    Data->Sstatus = State->SpecialRegisters.Sstatus & ~2ULL;
    Data->VirtualEntry = (ULONG_PTR)HalpRiscvApVirtualEntry;
    ++HalpRiscvStartedProcessors;
    __asm__ __volatile__("fence rw, rw\n\tfence.i" ::: "memory");
    Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_HSM, 0,
        HalpRiscvHartIds[HalpRiscvStartedProcessors - 1], CodePa,
        CodePa + PAGE_SIZE / 2, 0);
    if (Result.Error)
    {
        --HalpRiscvStartedProcessors;
        DbgPrint("RISC-V: hart %Iu start failed: %Id\n",
                 HalpRiscvHartIds[HalpRiscvStartedProcessors], Result.Error);
        return FALSE;
    }
    return TRUE;
}

VOID
NTAPI
HalInitializeProcessor(ULONG Number, PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNREFERENCED_PARAMETER(LoaderBlock);
    ASSERT(Number < HalpRiscvStartedProcessors);
    HalpRiscvStartClock();
    KiRiscvSetInterruptEnabled(RISCV_HAL_SIE_STIE | RISCV_HAL_SIE_SSIE, TRUE);
}

BOOLEAN NTAPI HalAllProcessorsStarted(VOID)
{
    /* Kernel startup waits for every successful HSM request before this call. */
    if (HalpRiscvStartupPages)
    {
        MmFreeContiguousMemory(HalpRiscvStartupPages);
        HalpRiscvStartupPages = NULL;
    }
    return TRUE;
}

VOID
NTAPI
HalRequestIpi(KAFFINITY Targets)
{
    ULONG Number;
    RISCV_SBI_RETURN Result;
    for (Number = 0; Targets; ++Number, Targets >>= 1)
    {
        if (!(Targets & 1)) continue;
        ASSERT(Number < HalpRiscvStartedProcessors);
        /* hart_mask_base allows sparse IDs, including IDs above XLEN. */
        Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_IPI, 0,
                                  1, HalpRiscvHartIds[Number], 0, 0);
        if (Result.Error)
            KeBugCheckEx(HAL_INITIALIZATION_FAILED, RISCV_SBI_EXTENSION_IPI,
                         Result.Error, Number, HalpRiscvHartIds[Number]);
    }
}

VOID
NTAPI
HalpRiscvRemoteFence(KAFFINITY Targets, PVOID Address, SIZE_T Size, BOOLEAN Instruction)
{
    ULONG Number;
    RISCV_SBI_RETURN Result;
    __asm__ __volatile__("fence rw, rw" ::: "memory");
    for (Number = 0; Targets; ++Number, Targets >>= 1)
    {
        if (!(Targets & 1)) continue;
        ASSERT(Number < HalpRiscvStartedProcessors);
        Result = HalpRiscvSbiCall(RISCV_SBI_EXTENSION_RFENCE, Instruction ? 0 : 1,
            1, HalpRiscvHartIds[Number], (ULONG_PTR)Address, Size);
        if (Result.Error)
            KeBugCheckEx(HAL_INITIALIZATION_FAILED, RISCV_SBI_EXTENSION_RFENCE,
                         Result.Error, Number, Instruction);
    }
}
