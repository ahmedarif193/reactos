/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Memory Management Functions
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define NEXT_MEMORY_DESCRIPTOR(Descriptor, DescriptorSize) \
    (EFI_MEMORY_DESCRIPTOR*)((char*)(Descriptor) + (DescriptorSize))
#define EXIT_STACK_SIZE 0x1000
/*
 * A free EFI region can split into pinned and firmware-owned descriptors.
 * Keep slack for those splits, page 0, and the terminating empty entry.
 */
#define FREELDR_DESCRIPTOR_SLACK 16
#define MEMORY_MAP_EXTRA_DESCRIPTORS 32
#define MEMORY_MAP_MAX_RETRIES 8
#define UEFI_ERROR_MASK_32 0x80000000ULL
#define UEFI_ERROR_MASK_64 0x8000000000000000ULL
#define UEFI_STATUS_BUFFER_TOO_SMALL 5

/*
 * Modern firmware can still allocate while completing ExitBootServices().
 * Leave it a small conventional-memory reserve instead of pinning everything.
 */
#define UEFI_FIRMWARE_RESERVE_BYTES (16ULL * 1024 * 1024)
#define UEFI_FIRMWARE_RESERVE_PAGES (UEFI_FIRMWARE_RESERVE_BYTES / EFI_PAGE_SIZE)

#if defined(_M_AMD64)
/* Must match HALP_LOW_STUB_SIZE_IN_PAGES in hal/halx86/include/halp.h. */
#define UEFI_AP_LOW_STUB_PAGES 5
static EFI_PHYSICAL_ADDRESS UefiApLowStubBase;
#endif

ULONG
AddMemoryDescriptor(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR List,
    _In_ ULONG MaxCount,
    _In_ PFN_NUMBER BasePage,
    _In_ PFN_NUMBER PageCount,
    _In_ TYPE_OF_MEMORY MemoryType);

/* GLOBALS *******************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

extern ULONG_PTR VramAddress;
extern ULONG VramSize;

EFI_MEMORY_DESCRIPTOR* EfiMemoryMap = NULL;
UINT32 FreeldrDescCount;
/* Usable FreeldrMem capacity, excluding the terminator slot. */
static UINT32 FreeldrDescMax;
PVOID OsLoaderBase;

/*
 * Set TRUE the moment ExitBootServices succeeds. Volatile + non-static so
 * other translation units (mmu_v2, timer.c, …) can gate firmware-callback
 * code paths and fall back to direct hardware access. Mirrors the working
 * fork at reactosv2_arm64dev.
 */
volatile BOOLEAN BootServicesExitedFlag;
SIZE_T OsLoaderSize;
EFI_HANDLE PublicBootHandle;
PVOID ExitStack;
PVOID EndofExitStack;

void _exituefi(VOID);

/* FUNCTIONS *****************************************************************/

static
BOOLEAN
UefiStatusIsCode(
    _In_ EFI_STATUS Status,
    _In_ UINTN Code)
{
    UINT64 Value = (UINT64)Status;

    /*
     * Some local EDK2-style macros encode errors with bit 31 set even on
     * ARM64, while firmware returns the UEFI-defined bit 63 form. Compare the
     * status code after stripping either encoding so the memory-map retry path
     * accepts real firmware statuses.
     */
    return (Value == (UEFI_ERROR_MASK_32 | Code)) ||
           (Value == (UEFI_ERROR_MASK_64 | Code)) ||
           ((Value & 0xFFFFFFFFULL) == (UEFI_ERROR_MASK_32 | Code));
}

static
EFI_STATUS
PUEFI_LoadMemoryMap(
    _Out_ UINTN*  LocMapKey,
    _Out_ UINTN*  LocMapSize,
    _Out_ UINTN*  LocDescriptorSize,
    _Out_ UINT32* LocDescriptorVersion)
{
    EFI_STATUS Status;
    UINTN AllocationSize = 0;
    UINTN DescriptorSizeForSlack;
    ULONG Count = 0;

    Status = GlobalSystemTable->BootServices->GetMemoryMap(LocMapSize,
                                                           EfiMemoryMap,
                                                           LocMapKey,
                                                           LocDescriptorSize,
                                                           LocDescriptorVersion);

    if ((Status != EFI_SUCCESS) &&
        !UefiStatusIsCode(Status, UEFI_STATUS_BUFFER_TOO_SMALL))
    {
        return Status;
    }

    /*
     * Reallocate and retrieve again the needed memory map size. AllocatePool()
     * itself changes the memory map, so reserve a fixed descriptor slack and
     * retry only while firmware reports EFI_BUFFER_TOO_SMALL. Other failures are
     * terminal; retrying with a NULL/failed allocation causes an infinite
     * EFI_INVALID_PARAMETER loop on RPi5 firmware.
     */
    while (UefiStatusIsCode(Status, UEFI_STATUS_BUFFER_TOO_SMALL))
    {
        /* Reallocate the memory map buffer */
        if (EfiMemoryMap)
        {
            GlobalSystemTable->BootServices->FreePool(EfiMemoryMap);
        }

        if (Count >= MEMORY_MAP_MAX_RETRIES)
        {
            return Status;
        }

        DescriptorSizeForSlack = *LocDescriptorSize;
        if (DescriptorSizeForSlack == 0)
            DescriptorSizeForSlack = sizeof(EFI_MEMORY_DESCRIPTOR);

        AllocationSize = *LocMapSize +
                         (DescriptorSizeForSlack *
                          (MEMORY_MAP_EXTRA_DESCRIPTORS + Count));
        EfiMemoryMap = NULL;
        Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData, AllocationSize,
                                                               (VOID**)&EfiMemoryMap);
        if (Status != EFI_SUCCESS)
        {
            EfiMemoryMap = NULL;
            return Status;
        }

        Status = GlobalSystemTable->BootServices->GetMemoryMap(LocMapSize,
                                                               EfiMemoryMap,
                                                               LocMapKey,
                                                               LocDescriptorSize,
                                                               LocDescriptorVersion);
        if ((Status != EFI_SUCCESS) &&
            !UefiStatusIsCode(Status, UEFI_STATUS_BUFFER_TOO_SMALL))
        {
            return Status;
        }
        Count++;
    }

    return Status;
}

static
VOID
UefiSetMemory(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    _In_ ULONG_PTR BaseAddress,
    _In_ PFN_COUNT SizeInPages,
    _In_ TYPE_OF_MEMORY MemoryType)
{
    ULONG_PTR BasePage, PageCount;

    BasePage = BaseAddress / EFI_PAGE_SIZE;
    PageCount = SizeInPages;

    /* Add the memory descriptor */
    FreeldrDescCount = AddMemoryDescriptor(MemoryMap,
                                           FreeldrDescMax,
                                           BasePage,
                                           PageCount,
                                           MemoryType);
}

static
TYPE_OF_MEMORY
UefiConvertToFreeldrDesc(EFI_MEMORY_TYPE EfiMemoryType)
{
    switch (EfiMemoryType)
    {
        case EfiReservedMemoryType:
            return LoaderReserve;
        case EfiLoaderCode:
            return LoaderLoadedProgram;
        case EfiLoaderData:
            return LoaderLoadedProgram;
        case EfiBootServicesCode:
            return LoaderFirmwareTemporary;
        case EfiBootServicesData:
            return LoaderFirmwareTemporary;
        case EfiRuntimeServicesCode:
            return LoaderFirmwarePermanent;
        case EfiRuntimeServicesData:
            return LoaderFirmwarePermanent;
        case EfiConventionalMemory:
            return LoaderFree;
        case EfiUnusableMemory:
            return LoaderBad;
        case EfiACPIReclaimMemory:
            return LoaderSpecialMemory;
        case EfiACPIMemoryNVS:
            return LoaderSpecialMemory;
        case EfiMemoryMappedIO:
            return LoaderReserve;
        case EfiMemoryMappedIOPortSpace:
            return LoaderReserve;
        default:
            break;
    }
    return LoaderReserve;
}

PFREELDR_MEMORY_DESCRIPTOR
UefiMemGetMemoryMap(ULONG *MemoryMapSize)
{
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    UINT32 DescriptorVersion = 0;
    SIZE_T FreeldrMemMapSize;
    UINTN DescriptorSize = 0;
    EFI_STATUS Status;
    UINTN MapSize = 0;
    UINTN MapKey = 0;
    UINT32 Index;

    EFI_GUID EfiLoadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    PFREELDR_MEMORY_DESCRIPTOR FreeldrMem = NULL;
    EFI_MEMORY_DESCRIPTOR* MapEntry = NULL;
    UINT32 EntryCount = 0;
    FreeldrDescCount = 0;

    if (BootServicesExitedFlag)
    {
        if (MemoryMapSize)
            *MemoryMapSize = 0;
        return NULL;
    }

    Status = GlobalSystemTable->BootServices->HandleProtocol(GlobalImageHandle,
                                                             &EfiLoadedImageProtocol,
                                                             (VOID**)&LoadedImage);
    if (Status != EFI_SUCCESS)
    {
        TRACE("Failed to find LoadedImageHandle with status: %d\n", Status);
        UiMessageBoxCritical("Unable to initialize memory manager.");
        return NULL;
    }
    OsLoaderBase = LoadedImage->ImageBase;
    OsLoaderSize = LoadedImage->ImageSize;
    PublicBootHandle = LoadedImage->DeviceHandle;

    if (EfiMemoryMap)
    {
        GlobalSystemTable->BootServices->FreePool(EfiMemoryMap);
        EfiMemoryMap = NULL;
    }

#if defined(_M_AMD64)
    /*
     * Reserve the real-mode AP trampoline before normal loader allocations
     * can consume the small amount of usable memory below 1 MiB. The HAL
     * later claims this range from the LoaderFirmwareTemporary descriptor.
     */
    if (UefiApLowStubBase == 0)
    {
        EFI_PHYSICAL_ADDRESS LowStubBase = 0xFFFFF;

        Status = GlobalSystemTable->BootServices->AllocatePages(AllocateMaxAddress,
                                                                EfiLoaderData,
                                                                UEFI_AP_LOW_STUB_PAGES,
                                                                &LowStubBase);
        if ((Status == EFI_SUCCESS) && (LowStubBase != 0))
        {
            UefiApLowStubBase = LowStubBase;
            TRACE("Reserved low-memory AP stub at 0x%llx\n", UefiApLowStubBase);
        }
        else
        {
            if (Status == EFI_SUCCESS)
            {
                GlobalSystemTable->BootServices->FreePages(LowStubBase, UEFI_AP_LOW_STUB_PAGES);
                WARN("UEFI returned page zero for the AP stub; SMP may remain unavailable\n");
            }
            else
            {
                WARN("Could not reserve low-memory AP stub (status 0x%llx); SMP may remain unavailable\n",
                     (UINT64)Status);
            }
        }
    }
#endif

    TRACE("UefiMemGetMemoryMap: Gather memory map\n");
    Status = PUEFI_LoadMemoryMap(&MapKey,
                                 &MapSize,
                                 &DescriptorSize,
                                 &DescriptorVersion);
    if ((Status != EFI_SUCCESS) || !EfiMemoryMap || (DescriptorSize == 0))
    {
        TRACE("Failed to load UEFI memory map with status: %d\n", Status);
        UiMessageBoxCritical("Unable to initialize memory manager.");
        return NULL;
    }

    TRACE("Value of MapKey: %d\n", MapKey);
    TRACE("Value of MapSize: %d\n", MapSize);
    TRACE("Value of DescriptorSize: %d\n", DescriptorSize);
    TRACE("Value of DescriptorVersion: %d\n", DescriptorVersion);

    EntryCount = (MapSize / DescriptorSize);

    /*
     * Splitting free EFI regions can produce more FreeLoader descriptors than
     * EFI entries. FreeldrDescMax is the bound used by AddMemoryDescriptor().
     */
    FreeldrMemMapSize = sizeof(FREELDR_MEMORY_DESCRIPTOR) * (EntryCount + FREELDR_DESCRIPTOR_SLACK);
    FreeldrDescMax = EntryCount + FREELDR_DESCRIPTOR_SLACK - 1;
    Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData,
                                                           FreeldrMemMapSize,
                                                           (void**)&FreeldrMem);
    if (Status != EFI_SUCCESS)
    {
        TRACE("Failed to allocate pool with status %d\n", Status);
        UiMessageBoxCritical("Unable to initialize memory manager.");
        return NULL;
    }

    RtlZeroMemory(FreeldrMem, FreeldrMemMapSize);

    /*
     * Leave the reserve at the high end and pin the rest for FreeLoader.
     */
    {
        UINT64 TotalFreePages = 0;
        UINT64 PagesToPin;
        UINT64 PinnedSoFar = 0;

        MapEntry = EfiMemoryMap;
        for (Index = 0; Index < EntryCount; ++Index)
        {
            if (UefiConvertToFreeldrDesc(MapEntry->Type) == LoaderFree)
                TotalFreePages += MapEntry->NumberOfPages;
            MapEntry = NEXT_MEMORY_DESCRIPTOR(MapEntry, DescriptorSize);
        }

        /*
         * Clamp the reserve to current free pages. UefiMemGetMemoryMap() can
         * run more than once, so keep the remaining headroom stable.
         */
        {
            UINT64 Reserve = min(UEFI_FIRMWARE_RESERVE_PAGES, TotalFreePages);
            PagesToPin = TotalFreePages - Reserve;
        }

        MapEntry = EfiMemoryMap;
        for (Index = 0; Index < EntryCount; ++Index)
        {
            TYPE_OF_MEMORY MemoryType = UefiConvertToFreeldrDesc(MapEntry->Type);

            if (MemoryType == LoaderFree)
            {
                EFI_PHYSICAL_ADDRESS RegionBase = MapEntry->PhysicalStart;
                UINT64 RegionPages = MapEntry->NumberOfPages;
                UINT64 PinPages = (PinnedSoFar < PagesToPin) ? min(RegionPages, PagesToPin - PinnedSoFar) : 0;

                /* Pin the part FreeLoader will manage as its own free pool. */
                if (PinPages != 0)
                {
                    EFI_PHYSICAL_ADDRESS PinBase = RegionBase;

                    Status = GlobalSystemTable->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, PinPages, &PinBase);
                    if (Status == EFI_SUCCESS)
                    {
                        PinnedSoFar += PinPages;
                        UefiSetMemory(FreeldrMem, PinBase, PinPages, LoaderFree);
                    }
                    else
                    {
                        /* Firmware refused: don't claim ownership we don't have. */
                        PinPages = 0;
                    }
                }

                /*
                 * FreeLoader will not allocate from LoaderFirmwareTemporary,
                 * and NT can reclaim it after boot.
                 */
                if (PinPages < RegionPages)
                {
                    UefiSetMemory(FreeldrMem, RegionBase + PinPages * EFI_PAGE_SIZE, RegionPages - PinPages, LoaderFirmwareTemporary);
                }
            }
            else if (MemoryType != LoaderReserve)
            {
                /* Preserve non-reserved firmware descriptors. */
                UefiSetMemory(FreeldrMem, MapEntry->PhysicalStart, MapEntry->NumberOfPages, MemoryType);
            }

            MapEntry = NEXT_MEMORY_DESCRIPTOR(MapEntry, DescriptorSize);
        }
    }

#if defined(_M_AMD64)
    /* Make the pre-allocated range visible to HalpAllocPhysicalMemory(). */
    if (UefiApLowStubBase != 0)
    {
        UefiSetMemory(FreeldrMem,
                      UefiApLowStubBase,
                      UEFI_AP_LOW_STUB_PAGES,
                      LoaderFirmwareTemporary);
    }
#endif

    /* Windows expects the first page to be reserved, otherwise it asserts.
     * However it can be just a free page on some UEFI systems. */
    UefiSetMemory(FreeldrMem, 0x000000, 1, LoaderFirmwarePermanent);

    if (VramAddress != 0 && VramSize != 0)
    {
        ULONG_PTR FbBasePage = VramAddress / EFI_PAGE_SIZE;
        ULONG_PTR FbEndPage = (VramAddress + VramSize + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE;

        for (Index = 0; Index < FreeldrDescCount; Index++)
        {
            ULONG_PTR DescStart = FreeldrMem[Index].BasePage;
            ULONG_PTR DescEnd = DescStart + FreeldrMem[Index].PageCount;
            ULONG_PTR ReserveEnd;

            if (FreeldrMem[Index].MemoryType != LoaderFree || FbBasePage < DescStart || FbBasePage >= DescEnd)
                continue;

            ReserveEnd = (FbEndPage < DescEnd) ? FbEndPage : DescEnd;
            UefiSetMemory(FreeldrMem, FbBasePage * EFI_PAGE_SIZE, (PFN_COUNT)(ReserveEnd - FbBasePage), LoaderFirmwarePermanent);
            break;
        }
    }

    *MemoryMapSize = FreeldrDescCount;
    return FreeldrMem;
}

VOID
UefiExitBootServices(VOID)
{
    UINTN MapKey = 0;
    UINTN MapSize = 0;
    EFI_STATUS Status;
    UINTN DescriptorSize = 0;
    UINT32 DescriptorVersion = 0;

    TRACE("Attempting to exit bootsevices\n");

    /*
     * Disable the firmware watchdog *before* ExitBootServices. The UEFI
     * spec is supposed to disarm it as a side-effect of EBS, but several
     * firmware implementations leave the timer armed and then dispatch a
     * stale callback after EBS — which triggers a synchronous abort once
     * the firmware code is no longer mapped. Doing it explicitly is cheap
     * insurance.
     */
    if (GlobalSystemTable && GlobalSystemTable->BootServices)
    {
        GlobalSystemTable->BootServices->SetWatchdogTimer(0, 0, 0, NULL);
    }

    UefiVideoPrepareForExitBootServices();

    Status = PUEFI_LoadMemoryMap(&MapKey,
                                 &MapSize,
                                 &DescriptorSize,
                                 &DescriptorVersion);
    if (Status != EFI_SUCCESS)
    {
        FrLdrBugCheckWithMessage(EXIT_BOOTSERVICES_FAILURE,
                                 __FILE__,
                                 __LINE__,
                                 "Failed to get memory map before exit boot services: %d",
                                 Status);
    }

    Status = GlobalSystemTable->BootServices->ExitBootServices(GlobalImageHandle, MapKey);
    /*
     * UEFI spec permits one retry — but the map key is invalidated as a side
     * effect, so we must refetch the map (and a fresh key) before retrying,
     * not just call EBS twice with the same stale key.
     */
    if (Status != EFI_SUCCESS)
    {
        Status = PUEFI_LoadMemoryMap(&MapKey,
                                     &MapSize,
                                     &DescriptorSize,
                                     &DescriptorVersion);
        if (Status != EFI_SUCCESS)
        {
            FrLdrBugCheckWithMessage(EXIT_BOOTSERVICES_FAILURE,
                                     __FILE__,
                                     __LINE__,
                                     "Failed to get retry memory map before exit boot services: %d",
                                     Status);
        }
        Status = GlobalSystemTable->BootServices->ExitBootServices(GlobalImageHandle, MapKey);
    }

    if (Status != EFI_SUCCESS)
    {
        TRACE("Failed to exit boot services with status: %d\n", Status);
        FrLdrBugCheckWithMessage(EXIT_BOOTSERVICES_FAILURE,
                                 __FILE__,
                                 __LINE__,
                                 "Failed to exit boot services with status: %d",
                                 Status);
    }
    else
    {
        BootServicesExitedFlag = TRUE;
        UefiVideoExitBootServices();
        TRACE("Exited bootservices\n");
    }
}

VOID
UefiPrepareForReactOS(VOID)
{
    _exituefi();
}
