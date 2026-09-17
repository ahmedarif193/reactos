/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V boot-owned virtual-address reservations
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <mm/ARM3/miarm.h>

MI_SYSTEM_VA_ASSIGNMENT MiSystemVaRegions[AssignedRegionMaximum];
static BOOLEAN MiRiscvVaLayoutAssigned;
static BOOLEAN MiRiscvVaAreasReserved;

#define MI_RISCV_GIB_OFFSET(Value) \
    (MI_DEFAULT_SYSTEM_RANGE_START + ((ULONG64)(Value) << 30))

/* The lower 128 GiB of the Sv39 supervisor half belongs to kernel-managed
 * virtual allocations; slot 0x17F holds the page-table window (ABI-125) and
 * the upper 128 GiB remains the loader-established physical map.
 * Hyperspace has a private root slot per process (ABI-129). */
#define MI_RISCV_SYSTEM_PTE_BASE       MI_RISCV_GIB_OFFSET(0)
#define MI_RISCV_SYSTEM_PTE_SIZE       (1ULL << 30)
#define MI_RISCV_HYPERSPACE_BASE       MI_RISCV_GIB_OFFSET(1)
#define MI_RISCV_HYPERSPACE_SIZE       (1ULL << 30)
#define MI_RISCV_PFN_DATABASE_BASE     MI_RISCV_GIB_OFFSET(2)
#define MI_RISCV_PFN_DATABASE_SIZE     (2ULL << 30)
#define MI_RISCV_NONPAGED_POOL_BASE    MI_RISCV_GIB_OFFSET(4)
#define MI_RISCV_NONPAGED_POOL_SIZE    (8ULL << 30)
#define MI_RISCV_PAGED_POOL_BASE       MI_RISCV_GIB_OFFSET(12)
#define MI_RISCV_PAGED_POOL_SIZE       (8ULL << 30)
#define MI_RISCV_SYSTEM_CACHE_BASE     MI_RISCV_GIB_OFFSET(20)
#define MI_RISCV_SYSTEM_CACHE_SIZE     (16ULL << 30)
#define MI_RISCV_SYSTEM_VIEW_BASE      MI_RISCV_GIB_OFFSET(36)
#define MI_RISCV_SYSTEM_VIEW_SIZE      MI_SYSTEM_VIEW_SIZE
#define MI_RISCV_SESSION_BASE          (MI_RISCV_SYSTEM_VIEW_BASE + MI_RISCV_SYSTEM_VIEW_SIZE)
#define MI_RISCV_SESSION_SIZE          MI_SESSION_SIZE
#define MI_RISCV_KERNEL_STACKS_BASE    MI_RISCV_GIB_OFFSET(38)
#define MI_RISCV_KERNEL_STACKS_SIZE    (1ULL << 30)
#define MI_RISCV_SYSTEM_IMAGES_BASE    MI_RISCV_GIB_OFFSET(39)
#define MI_RISCV_SYSTEM_IMAGES_SIZE    (4ULL << 30)
#define MI_RISCV_BOOT_DATA_BASE        MI_RISCV_GIB_OFFSET(43)
#define MI_RISCV_BOOT_DATA_SIZE        (4ULL << 30)
#define MI_RISCV_HAL_BASE              MI_RISCV_GIB_OFFSET(47)
#define MI_RISCV_HAL_SIZE              (1ULL << 30)
#define MI_RISCV_SHARED_DATA_BASE      MI_RISCV_GIB_OFFSET(48)
#define MI_RISCV_SYSTEM_CACHE_WS_BASE  (MI_RISCV_SHARED_DATA_BASE + (16ULL << 20))
#define MI_RISCV_SYSTEM_CACHE_WS_SIZE  (16ULL << 20)
#define MI_RISCV_PAGE_TABLES_BASE      PTE_BASE
#define MI_RISCV_PAGE_TABLES_SIZE      (1ULL << 30)

C_ASSERT(MI_RISCV_GIB_OFFSET(49) <= PTE_BASE);
C_ASSERT(PTE_TOP < RISCV64_LOADER_DIRECT_MAP_BASE);
C_ASSERT(MI_RISCV_HYPERSPACE_BASE == HYPER_SPACE);
C_ASSERT(MI_RISCV_SESSION_BASE + MI_RISCV_SESSION_SIZE <= MI_RISCV_KERNEL_STACKS_BASE);

BOOLEAN
NTAPI
MiRiscvSystemVaLayoutAssigned(VOID)
{
    return __atomic_load_n(&MiRiscvVaLayoutAssigned, __ATOMIC_ACQUIRE);
}

CODE_SEG("INIT")
VOID
NTAPI
MiInitializeKernelVaLayout(
    _In_ const LOADER_PARAMETER_BLOCK *LoaderBlock)
{
    MI_SYSTEM_VA_ASSIGNMENT Regions[AssignedRegionMaximum] = {0};
    const PRISCV64_LOADER_BLOCK RiscvBlock =
        LoaderBlock ? (PRISCV64_LOADER_BLOCK)&LoaderBlock->u.Riscv64 : NULL;
    NTSTATUS Status;

#define MI_RISCV_SET_REGION(Name, Base, Size)            \
    do                                                   \
    {                                                    \
        Regions[AssignedRegion##Name].BaseAddress =      \
            (PVOID)(ULONG_PTR)(Base);                    \
        Regions[AssignedRegion##Name].NumberOfBytes =    \
            (Size);                                      \
    } while (0)

    if ((RiscvBlock == NULL) ||
        (RiscvBlock->Version != RISCV64_LOADER_BLOCK_VERSION) ||
        (RiscvBlock->Size != sizeof(*RiscvBlock)) ||
        (RiscvBlock->DirectMapBase != RISCV64_LOADER_DIRECT_MAP_BASE) ||
        (RiscvBlock->DirectMapSize != RISCV64_LOADER_DIRECT_MAP_SIZE))
    {
        KeBugCheckEx(MEMORY_MANAGEMENT,
                     0x52564C59,
                     (ULONG_PTR)RiscvBlock,
                     RiscvBlock ? RiscvBlock->Version : 0,
                     STATUS_REVISION_MISMATCH);
    }

    MI_RISCV_SET_REGION(SystemPtes, MI_RISCV_SYSTEM_PTE_BASE, MI_RISCV_SYSTEM_PTE_SIZE);
    MI_RISCV_SET_REGION(HyperSpace, MI_RISCV_HYPERSPACE_BASE, MI_RISCV_HYPERSPACE_SIZE);
    MI_RISCV_SET_REGION(SharedData, MI_RISCV_SHARED_DATA_BASE, PAGE_SIZE);
    MI_RISCV_SET_REGION(SystemCacheWs, MI_RISCV_SYSTEM_CACHE_WS_BASE, MI_RISCV_SYSTEM_CACHE_WS_SIZE);
    MI_RISCV_SET_REGION(PfnDatabase, MI_RISCV_PFN_DATABASE_BASE, MI_RISCV_PFN_DATABASE_SIZE);
    MI_RISCV_SET_REGION(NonPagedPool, MI_RISCV_NONPAGED_POOL_BASE, MI_RISCV_NONPAGED_POOL_SIZE);
    MI_RISCV_SET_REGION(PagedPool, MI_RISCV_PAGED_POOL_BASE, MI_RISCV_PAGED_POOL_SIZE);
    MI_RISCV_SET_REGION(SystemCache, MI_RISCV_SYSTEM_CACHE_BASE, MI_RISCV_SYSTEM_CACHE_SIZE);
    MI_RISCV_SET_REGION(SystemView, MI_RISCV_SYSTEM_VIEW_BASE, MI_RISCV_SYSTEM_VIEW_SIZE);
    MI_RISCV_SET_REGION(Session, MI_RISCV_SESSION_BASE, MI_RISCV_SESSION_SIZE);
    MI_RISCV_SET_REGION(KernelStacks, MI_RISCV_KERNEL_STACKS_BASE, MI_RISCV_KERNEL_STACKS_SIZE);
    MI_RISCV_SET_REGION(SystemImages, MI_RISCV_SYSTEM_IMAGES_BASE, MI_RISCV_SYSTEM_IMAGES_SIZE);
    MI_RISCV_SET_REGION(BootData, MI_RISCV_BOOT_DATA_BASE, MI_RISCV_BOOT_DATA_SIZE);
    MI_RISCV_SET_REGION(Hal, MI_RISCV_HAL_BASE, MI_RISCV_HAL_SIZE);
    MI_RISCV_SET_REGION(PageTables, MI_RISCV_PAGE_TABLES_BASE, MI_RISCV_PAGE_TABLES_SIZE);
    MI_RISCV_SET_REGION(PhysicalMap,
                        RiscvBlock->DirectMapBase,
                        RiscvBlock->DirectMapSize - (64ULL << 10));

    Status = MiRiscvPublishSystemVaLayout(Regions,
                                         RTL_NUMBER_OF(Regions));
    if (!NT_SUCCESS(Status))
    {
        KeBugCheckEx(MEMORY_MANAGEMENT,
                     0x52564C59,
                     Status,
                     RiscvBlock->DirectMapBase,
                     RiscvBlock->DirectMapSize);
    }

#undef MI_RISCV_SET_REGION
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
MiRiscvPublishSystemVaLayout(
    _In_reads_(RegionCount) const MI_SYSTEM_VA_ASSIGNMENT *Regions,
    _In_ ULONG RegionCount)
{
    MI_SYSTEM_VA_ASSIGNMENT Candidate[AssignedRegionMaximum];
    const MI_ASSIGNED_REGION_TYPES Required[] =
    {
        AssignedRegionNonPagedPool, AssignedRegionPagedPool,
        AssignedRegionSystemCache, AssignedRegionSystemPtes,
        AssignedRegionPfnDatabase, AssignedRegionHyperSpace,
        AssignedRegionKernelStacks, AssignedRegionSystemImages,
        AssignedRegionPhysicalMap, AssignedRegionSharedData,
        AssignedRegionSystemView, AssignedRegionSystemCacheWs,
        AssignedRegionBootData, AssignedRegionPageTables
    };
    ULONG i, j;

    /* Boot-only, serialized by the boot processor. These are kernel-owned
     * inputs, not a loader wire structure or a fault-safe user buffer. */
    if (MiRiscvSystemVaLayoutAssigned())
        return STATUS_INVALID_DEVICE_STATE;
    if ((Regions == NULL) || (RegionCount != AssignedRegionMaximum))
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(Candidate, Regions, sizeof(Candidate));
    for (i = 0; i < AssignedRegionMaximum; ++i)
    {
        ULONG_PTR Base = (ULONG_PTR)Candidate[i].BaseAddress;
        ULONG64 Size = Candidate[i].NumberOfBytes;

        if (Size == 0)
        {
            if (Base != 0) return STATUS_INVALID_PARAMETER;
            continue;
        }

        if ((Base < MI_DEFAULT_SYSTEM_RANGE_START) ||
            (Base > MI_HIGHEST_SYSTEM_ADDRESS) ||
            ((Base | Size) & (PAGE_SIZE - 1)) ||
            (Size - 1 > MI_HIGHEST_SYSTEM_ADDRESS - Base))
        {
            return STATUS_INVALID_PARAMETER;
        }

        for (j = 0; j < i; ++j)
        {
            ULONG_PTR OtherBase = (ULONG_PTR)Candidate[j].BaseAddress;
            ULONG64 OtherSize = Candidate[j].NumberOfBytes;

            if (OtherSize && (Base < OtherBase + OtherSize) && (OtherBase < Base + Size))
                return STATUS_CONFLICTING_ADDRESSES;
        }
    }

    for (i = 0; i < RTL_NUMBER_OF(Required); ++i)
    {
        if (Candidate[Required[i]].NumberOfBytes == 0)
            return STATUS_INVALID_PARAMETER;
    }

    /* The page-table window is mandatory and fixed (ABI-125). */
    if ((Candidate[AssignedRegionPageTables].BaseAddress != (PVOID)PTE_BASE) ||
        (Candidate[AssignedRegionPageTables].NumberOfBytes != (1ULL << 30)))
        return STATUS_INVALID_PARAMETER;
    if (Candidate[AssignedRegionPagedPool].NumberOfBytes < MI_MIN_INIT_PAGED_POOLSIZE)
        return STATUS_INVALID_PARAMETER;

    RtlCopyMemory(MiSystemVaRegions, Candidate, sizeof(Candidate));
    MmPagedPoolStart = Candidate[AssignedRegionPagedPool].BaseAddress;
    MmNonPagedPoolEnd = Add2Ptr(Candidate[AssignedRegionNonPagedPool].BaseAddress, Candidate[AssignedRegionNonPagedPool].NumberOfBytes);
    MmSystemCacheStart = Candidate[AssignedRegionSystemCache].BaseAddress;
    MmSystemCacheWorkingSetList = Candidate[AssignedRegionSystemCacheWs].BaseAddress;
    __atomic_store_n(&MiRiscvVaLayoutAssigned, TRUE, __ATOMIC_RELEASE);
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
MiRiscvReserveSystemMemoryAreas(VOID)
{
    ULONG i;
    NTSTATUS Status;
    PVOID Base;
    PMEMORY_AREA Area;

    if (!MiRiscvSystemVaLayoutAssigned() || MiRiscvVaAreasReserved)
        KeBugCheckEx(MEMORY_MANAGEMENT, 0x52565641, 0, 0, STATUS_INVALID_DEVICE_STATE);

    MmLockAddressSpace(MmGetKernelAddressSpace());
    /* ARM3-owned insertion trusts its caller to exclude existing ranges.
     * Preflight all reservations before consuming any static area slots. */
    for (i = 0; i < AssignedRegionMaximum; ++i)
    {
        if (MiSystemVaRegions[i].NumberOfBytes == 0) continue;
        if (!MmIsAddressRangeFree(MmGetKernelAddressSpace(), MiSystemVaRegions[i].BaseAddress, MiSystemVaRegions[i].NumberOfBytes))
        {
            MmUnlockAddressSpace(MmGetKernelAddressSpace());
            KeBugCheckEx(MEMORY_MANAGEMENT, 0x52565641, i, (ULONG_PTR)MiSystemVaRegions[i].BaseAddress, STATUS_CONFLICTING_ADDRESSES);
        }
    }

    for (i = 0; i < AssignedRegionMaximum; ++i)
    {
        if (MiSystemVaRegions[i].NumberOfBytes == 0) continue;
        /* The legacy section mapper allocates individual file-cache views
         * here. An ARM3 reservation would make every VACB allocation conflict. */
        if (i == AssignedRegionSystemCache) continue;
        Base = MiSystemVaRegions[i].BaseAddress;

        /* Reserve ownership in the legacy allocator, not hardware access
         * permissions. Image-section PTE protections are applied separately. */
        Status = MmCreateMemoryArea(MmGetKernelAddressSpace(), MEMORY_AREA_OWNED_BY_ARM3 | MEMORY_AREA_STATIC, &Base, MiSystemVaRegions[i].NumberOfBytes, (i == AssignedRegionSystemImages) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &Area, 0, PAGE_SIZE);
        if (!NT_SUCCESS(Status) || (Base != MiSystemVaRegions[i].BaseAddress))
        {
            MmUnlockAddressSpace(MmGetKernelAddressSpace());
            KeBugCheckEx(MEMORY_MANAGEMENT, 0x52565641, i, (ULONG_PTR)Base, NT_SUCCESS(Status) ? STATUS_CONFLICTING_ADDRESSES : Status);
        }
    }
    MiRiscvVaAreasReserved = TRUE;
    MmUnlockAddressSpace(MmGetKernelAddressSpace());
}

CODE_SEG("INIT")
VOID
NTAPI
MiRiscvDumpSystemVaLayout(VOID)
{
    ULONG i;

    if (!MiRiscvSystemVaLayoutAssigned())
    {
        DPRINT1("RISC-V system VA layout is unassigned\n");
        return;
    }

    for (i = 0; i < AssignedRegionMaximum; ++i)
    {
        if (MiSystemVaRegions[i].NumberOfBytes == 0) continue;
        DPRINT1("RISC-V VA region %lu: %p - %p\n", i, MiSystemVaRegions[i].BaseAddress, Add2Ptr(MiSystemVaRegions[i].BaseAddress, MiSystemVaRegions[i].NumberOfBytes - 1));
    }
}
