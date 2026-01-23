/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Memory Management Functions (UEFI)
 * NOTE:        Reworked for robust GetMemoryMap handling and safer sizing.
 * COPYRIGHT:   Original: Copyright 2022 Justin Miller
 *              Rework:   2025 Ahmed ARIF arif193@gmail.com
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>
#include <debug.h>

#ifdef _M_ARM64
#include <reactos/arm64/early_uart.h>
#endif

DBG_DEFAULT_CHANNEL(WARNING);

/* HELPERS *******************************************************************/

#define NEXT_MEMORY_DESCRIPTOR(Descriptor, DescriptorSize) \
    ((EFI_MEMORY_DESCRIPTOR *)((char *)(Descriptor) + (DescriptorSize)))

/* A small, safe extra headroom (in descriptors) to absorb growth caused by
 * allocations we perform *while* fetching the map. */
#define MAP_SLACK_DESCRIPTORS 8

/* When the first probe cannot provide a size, seed to something reasonable. */
#define MAP_FALLBACK_BYTES    (16 * 1024)

/* When building the FreeLdr map we allow a small growth margin to avoid
 * reallocation if firmware adds a couple entries between calls (rare). */
#define FREELDR_EXTRA_DESCS   8

/* EXIT_BOOT_SERVICES stack scratch size (unchanged) */
#define EXIT_STACK_SIZE       0x1000

/* Max “virtual” list size we hand to AddMemoryDescriptor (unchanged semantic) */
#define UNUSED_MAX_DESCRIPTOR_COUNT 10000

/* EXTERNALS *****************************************************************/

extern ULONG LoaderPagesSpanned;
extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern REACTOS_INTERNAL_BGCONTEXT framebufferData;

/* Provided elsewhere */
extern char __ImageBase;

/* From your other unit */
extern ULONG
AddMemoryDescriptor(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR List,
    _In_    ULONG MaxCount,
    _In_    PFN_NUMBER BasePage,
    _In_    PFN_NUMBER PageCount,
    _In_    TYPE_OF_MEMORY MemoryType);

/* GLOBALS *******************************************************************/

EFI_MEMORY_DESCRIPTOR *EfiMemoryMap = NULL;
UINT32                 FreeldrDescCount = 0;

PVOID     OsLoaderBase;
SIZE_T    OsLoaderSize;
EFI_HANDLE PublicBootHandle;

/* Cached FreeLdr-style map for use after ExitBootServices. */
static PFREELDR_MEMORY_DESCRIPTOR CachedFreeldrMem;
static ULONG CachedFreeldrCount;
static BOOLEAN CachedMemoryMapValid;
volatile BOOLEAN BootServicesExitedFlag; /* Non-static, volatile: read by timer.c across translation units */

/* Declared elsewhere */
void _exituefi(VOID);

/* FORWARD DECLS *************************************************************/

static VOID
PUEFI_LoadMemoryMap(
    _Out_ UINTN  *LocMapKey,
    _Out_ UINTN  *LocMapSize,
    _Out_ UINTN  *LocDescriptorSize,
    _Out_ UINT32 *LocDescriptorVersion);

static VOID
UefiSetMemory(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    _In_    ULONG_PTR BaseAddress,
    _In_    PFN_COUNT SizeInPages,
    _In_    TYPE_OF_MEMORY MemoryType);

static TYPE_OF_MEMORY
UefiConvertToFreeldrDesc(_In_ EFI_MEMORY_TYPE EfiMemoryType);

/* IMPLEMENTATION ************************************************************/

/* Robust memory-map loader:
 * - Treats EFI_BUFFER_TOO_SMALL as the normal/expected probe result.
 * - Allocates with slack and retries until EFI_SUCCESS.
 * - Trims returned length to a whole number of descriptors. */
static
VOID
PUEFI_LoadMemoryMap(
    _Out_ UINTN  *LocMapKey,
    _Out_ UINTN  *LocMapSize,
    _Out_ UINTN  *LocDescriptorSize,
    _Out_ UINT32 *LocDescriptorVersion)
{
    EFI_STATUS Status;
    UINTN  MapKey = 0, MapSize = 0, DescSize = 0;
    UINT32 DescVer = 0;

    /* First probe: most firmwares answer with EFI_BUFFER_TOO_SMALL and
       the required size in MapSize. */
    Status = GlobalSystemTable->BootServices->GetMemoryMap(&MapSize,
                                                           NULL,
                                                           &MapKey,
                                                           &DescSize,
                                                           &DescVer);

    TRACE("GetMemoryMap initial probe Status=%lx, EFI_BUFFER_TOO_SMALL=%lx\n", (UINTN)Status, (UINTN)EFI_BUFFER_TOO_SMALL);
    TRACE("MapSize=%lu DescSize=%lu after probe\n", (UINTN)MapSize, (UINTN)DescSize);
    
    if (Status != EFI_BUFFER_TOO_SMALL && Status != EFI_SUCCESS)
    {
        /* Some quirky firmwares return INVALID_PARAMETER if Buffer==NULL.
           Seed a fallback and continue with the loop below. */
        TRACE("GetMemoryMap initial probe abnormal: %lx; seeding default\n", (UINTN)Status);
        if (MapSize == 0)
            MapSize = MAP_FALLBACK_BYTES;
        if (DescSize == 0)
            DescSize = sizeof(EFI_MEMORY_DESCRIPTOR);
        DescVer = 1;
    }

    /* Retry loop: allocate with slack, fetch, grow if needed. */
    for (;;)
    {
        if (DescSize == 0)
            DescSize = sizeof(EFI_MEMORY_DESCRIPTOR);

        UINTN Capacity = MapSize + (DescSize * MAP_SLACK_DESCRIPTORS);

        if (EfiMemoryMap)
        {
            GlobalSystemTable->BootServices->FreePool(EfiMemoryMap);
            EfiMemoryMap = NULL;
        }

        Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData,
                                                               Capacity,
                                                               (VOID **)&EfiMemoryMap);
        if (EFI_ERROR(Status) || !EfiMemoryMap)
        {
            TRACE("AllocatePool(EfiMemoryMap, %lu) failed: %lx\n",
                  (UINTN)Capacity, (UINTN)Status);
            UiMessageBoxCritical("Unable to initialize memory manager.");
            /* Hard stop: callers assume a valid map after return. */
            FrLdrBugCheckWithMessage(0, __FILE__, __LINE__,
                                     "AllocatePool for memory map failed: %lx", (UINTN)Status);
        }

        UINTN TmpSize = Capacity;
        Status = GlobalSystemTable->BootServices->GetMemoryMap(&TmpSize,
                                                               EfiMemoryMap,
                                                               &MapKey,
                                                               &DescSize,
                                                               &DescVer);
        if (Status == EFI_SUCCESS)
        {
            /* Trim to whole descriptors to avoid half-tail. */
            TmpSize -= (TmpSize % DescSize);

            *LocMapKey            = MapKey;
            *LocMapSize           = TmpSize;
            *LocDescriptorSize    = DescSize;
            *LocDescriptorVersion = DescVer;

            TRACE("MapKey=%lx MapSize=%lu DescSize=%lu DescVer=%lu\n",
                  (UINTN)MapKey, (UINTN)TmpSize, (UINTN)DescSize, (UINTN)DescVer);
            return;
        }

        if (Status != EFI_BUFFER_TOO_SMALL)
        {
            TRACE("GetMemoryMap failed: %lx\n", (UINTN)Status);
            UiMessageBoxCritical("Unable to initialize memory manager.");
            FrLdrBugCheckWithMessage(0, __FILE__, __LINE__,
                                     "GetMemoryMap failed: %lx", (UINTN)Status);
        }

        /* Grew meanwhile: try again with the size firmware just returned. */
        MapSize = TmpSize + (DescSize * MAP_SLACK_DESCRIPTORS);
    }
}

static
VOID
UefiSetMemory(
    _Inout_ PFREELDR_MEMORY_DESCRIPTOR MemoryMap,
    _In_    ULONG_PTR BaseAddress,
    _In_    PFN_COUNT SizeInPages,
    _In_    TYPE_OF_MEMORY MemoryType)
{
    /* Convert physical address to PFN */
    const PFN_NUMBER BasePage  = (PFN_NUMBER)(BaseAddress >> EFI_PAGE_SHIFT);
    const PFN_NUMBER PageCount = (PFN_NUMBER)SizeInPages;

    FreeldrDescCount = AddMemoryDescriptor(MemoryMap,
                                           UNUSED_MAX_DESCRIPTOR_COUNT,
                                           BasePage,
                                           PageCount,
                                           MemoryType);
}

static
TYPE_OF_MEMORY
UefiConvertToFreeldrDesc(_In_ EFI_MEMORY_TYPE EfiMemoryType)
{
    switch (EfiMemoryType)
    {
        case EfiReservedMemoryType:        return LoaderReserve;
        case EfiLoaderCode:                return LoaderLoadedProgram;
        case EfiLoaderData:                return LoaderLoadedProgram;
        case EfiBootServicesCode:          return LoaderFirmwareTemporary;
        case EfiBootServicesData:          return LoaderFirmwareTemporary;
        case EfiRuntimeServicesCode:       return LoaderFirmwarePermanent;
        case EfiRuntimeServicesData:       return LoaderFirmwarePermanent;
        case EfiConventionalMemory:        return LoaderFree;
        case EfiUnusableMemory:            return LoaderBad;
        case EfiACPIReclaimMemory:         return LoaderFirmwareTemporary;
        case EfiACPIMemoryNVS:             return LoaderReserve;
        case EfiMemoryMappedIO:            return LoaderReserve;
        case EfiMemoryMappedIOPortSpace:   return LoaderReserve;
        default:                           return LoaderReserve;
    }
}

PFREELDR_MEMORY_DESCRIPTOR
UefiMemGetMemoryMap(_Out_ ULONG *MemoryMapSize /* OUT: number of entries */)
{
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_GUID EfiLoadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    UINTN MapKey = 0, MapBytes = 0, DescSize = 0;
    UINT32 DescVersion = 0;

    EFI_STATUS Status;
    PFREELDR_MEMORY_DESCRIPTOR FreeldrMem = NULL;

    FreeldrDescCount = 0;
    EfiMemoryMap = NULL;

    /* After ExitBootServices, firmware services are gone. Reuse the last
     * successful map we captured while they were available. */
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices || BootServicesExitedFlag)
    {
        if (CachedMemoryMapValid && CachedFreeldrMem)
        {
            FreeldrDescCount = CachedFreeldrCount;
            if (MemoryMapSize)
                *MemoryMapSize = CachedFreeldrCount;
            return CachedFreeldrMem;
        }

        TRACE("UefiMemGetMemoryMap: BootServices unavailable and no cached map\n");
        return NULL;
    }

    /* Identify our image for base/size and the boot device. */
    Status = GlobalSystemTable->BootServices->HandleProtocol(GlobalImageHandle,
                                                             &EfiLoadedImageProtocol,
                                                             (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage)
    {
        TRACE("HandleProtocol(LOADED_IMAGE) failed: %lx\n", (UINTN)Status);
        UiMessageBoxCritical("Unable to initialize memory manager.");
        return NULL;
    }

    OsLoaderBase     = LoadedImage->ImageBase;
    OsLoaderSize     = LoadedImage->ImageSize;
    PublicBootHandle = LoadedImage->DeviceHandle;

    TRACE("UefiMemGetMemoryMap: Gather memory map\n");
    PUEFI_LoadMemoryMap(&MapKey, &MapBytes, &DescSize, &DescVersion);

    /* Convert the firmware map into FreeLdr's compact descriptor list. */
    const UINT32 EntryCount = (DescSize ? (UINT32)(MapBytes / DescSize) : 0);

    /* Compute buffer size carefully (avoid overflow): */
    const SIZE_T each = sizeof(FREELDR_MEMORY_DESCRIPTOR);
    SIZE_T FreeldrEntriesCap = (SIZE_T)EntryCount + FREELDR_EXTRA_DESCS;
    if (FreeldrEntriesCap < EntryCount)  /* overflow check */
        FreeldrEntriesCap = EntryCount;

    SIZE_T FreeldrBytes = each * FreeldrEntriesCap;

    Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData,
                                                           FreeldrBytes,
                                                           (VOID **)&FreeldrMem);
    if (EFI_ERROR(Status) || !FreeldrMem)
    {
        TRACE("AllocatePool(FreeldrMem %lu bytes) failed: %lx\n",
              (UINTN)FreeldrBytes, (UINTN)Status);
        UiMessageBoxCritical("Unable to initialize memory manager.");
        return NULL;
    }

    /* Zero exactly what we allocated. */
    memset(FreeldrMem, 0, FreeldrBytes);

    /* Walk the EFI map and translate. */
    EFI_MEMORY_DESCRIPTOR *MapEntry = (EFI_MEMORY_DESCRIPTOR *)EfiMemoryMap;
    for (UINT32 i = 0; i < EntryCount; ++i)
    {
        TYPE_OF_MEMORY Mt = UefiConvertToFreeldrDesc(MapEntry->Type);

        /* Try to reserve ConventionalMemory so firmware doesn't reuse it later.
         *
         * NOTE: If pinning fails, we continue treating the memory as LoaderFree.
         * The AllocatePages call may fail for various reasons (memory already
         * allocated by us, firmware quirks, overlapping regions, etc.) that don't
         * mean the memory is truly unavailable. Demoting to LoaderFirmwareTemporary
         * on failure was causing ARM64 UEFI systems (like QEMU virt) to have
         * significantly reduced usable memory, breaking ramdisk allocation.
         *
         * The pinning is a best-effort optimization to prevent firmware from
         * reusing memory between GetMemoryMap and ExitBootServices. If it fails,
         * we accept a small race window rather than losing the memory entirely.
         */
        if (Mt == LoaderFree)
        {
            GlobalSystemTable->BootServices->AllocatePages(AllocateAddress,
                                                           EfiLoaderData,
                                                           MapEntry->NumberOfPages,
                                                           &MapEntry->PhysicalStart);
            /* Ignore errors - memory remains LoaderFree regardless of pinning success */
        }

        /* Track the maximum span of our own image (LoaderLoadedProgram). */
        if (Mt == LoaderLoadedProgram)
        {
            UINTN end = (UINTN)(MapEntry->PhysicalStart +
                                (MapEntry->NumberOfPages << EFI_PAGE_SHIFT));
            PFN_NUMBER last = (PFN_NUMBER)(end >> EFI_PAGE_SHIFT);
            if (last > LoaderPagesSpanned)
                LoaderPagesSpanned = last;
        }

        /* We do not expose LoaderReserve to our allocator. */
        if (Mt != LoaderReserve)
        {
            UefiSetMemory(FreeldrMem,
                          (ULONG_PTR)MapEntry->PhysicalStart,
                          (PFN_COUNT)MapEntry->NumberOfPages,
                          Mt);
        }

        MapEntry = NEXT_MEMORY_DESCRIPTOR(MapEntry, DescSize);
    }

    /* Windows/NT expects page 0 reserved; some UEFI maps leave it free. */
    UefiSetMemory(FreeldrMem, 0, 1, LoaderFirmwarePermanent);

    /* Cache for post-ExitBootServices users (ARM64 MMU setup). */
    CachedFreeldrMem = FreeldrMem;
    CachedFreeldrCount = FreeldrDescCount;
    CachedMemoryMapValid = TRUE;

    *MemoryMapSize = FreeldrDescCount;
    return FreeldrMem;
}

VOID
UefiExitBootServices(VOID)
{
    EFI_STATUS Status;
    UINTN MapKey = 0, MapBytes = 0, DescSize = 0;
    UINT32 DescVersion = 0;

    TRACE("Attempting to exit boot services\n");

    /*
     * CRITICAL: Prepare CPU and firmware state BEFORE calling ExitBootServices.
     *
     * 1. Disable the watchdog timer:
     *    The UEFI watchdog is set to 5 minutes by default. While ExitBootServices
     *    should disable it automatically, some firmware implementations have bugs.
     *    Explicitly disabling it prevents any timer-related issues.
     *
     * 2. Clear debug registers (DR0-DR7) and debug-related flags (x86/x64 only):
     *    Some UEFI firmware (notably VirtualBox) may have internal debug state that
     *    triggers #DB (Debug Exception) during ExitBootServices. By clearing all
     *    hardware breakpoint registers, debug status, and the trap flag, we ensure
     *    no stale debug state causes unexpected exceptions inside the firmware.
     *
     * 3. Disable console and serial output (x86/x64 only):
     *    Some UEFI firmware implementations (including certain OVMF builds) may call
     *    back into console/serial protocols during ExitBootServices execution. If these
     *    protocols are still active, it can cause firmware crashes.
     *
     * Note: ARM64 defers serial/console shutdown to after ExitBootServices to keep
     * debug output visible during the transition.
     */

    /* Disable the watchdog timer to prevent any timer-related issues */
    TRACE("Disabling watchdog timer\n");
    GlobalSystemTable->BootServices->SetWatchdogTimer(0, 0, 0, NULL);

#if defined(_M_AMD64) || defined(__x86_64__)
    /* Clear all x64 debug registers to prevent #DB exceptions */
    TRACE("Calling UefiClearDebugState\n");
    UefiClearDebugState();
    TRACE("UefiClearDebugState returned\n");
#endif

#if !defined(_M_ARM64) && !defined(__aarch64__)
    /* On x86/x64, disable serial and console BEFORE ExitBootServices.
     * Some UEFI firmware may call back into these protocols during
     * ExitBootServices execution, causing crashes. */
    UefiSerialDisableFirmware();
    UefiConsMarkBootServicesExited();

    /*
     * WORKAROUND for VirtualBox UEFI firmware crash during ExitBootServices.
     *
     * VirtualBox's UEFI firmware (and potentially other buggy implementations)
     * may crash during ExitBootServices with a page fault at address like
     * CR2=0xFFFFFFFFFFFFFFDD (-35). This appears to be caused by the firmware
     * iterating through internal protocol tracking structures that have become
     * corrupted or contain invalid pointers.
     *
     * By clearing the console protocol pointers in the System Table before
     * calling ExitBootServices, we prevent the firmware from attempting to
     * perform cleanup operations on these protocols that might trigger the bug.
     */
    if (GlobalSystemTable)
    {
        GlobalSystemTable->ConIn = NULL;
        GlobalSystemTable->ConOut = NULL;
        GlobalSystemTable->StdErr = NULL;
    }
#endif /* !_M_ARM64 && !__aarch64__ */

    /* Per spec, fetch a *fresh* map/key immediately before ExitBootServices. */
    PUEFI_LoadMemoryMap(&MapKey, &MapBytes, &DescSize, &DescVersion);

    Status = GlobalSystemTable->BootServices->ExitBootServices(GlobalImageHandle, MapKey);

    /* Spec permits one retry: refetch key and try again. */
    if (EFI_ERROR(Status))
    {
        TRACE("ExitBootServices first attempt failed: %lx, retrying\n", (UINTN)Status);
        PUEFI_LoadMemoryMap(&MapKey, &MapBytes, &DescSize, &DescVersion);
        Status = GlobalSystemTable->BootServices->ExitBootServices(GlobalImageHandle, MapKey);
    }

    if (EFI_ERROR(Status))
    {
        /* Note: TRACE may not work on x86/x64 since serial was disabled above */
        FrLdrBugCheckWithMessage(EXIT_BOOTSERVICES_FAILURE,
                                 __FILE__,
                                 __LINE__,
                                 "ExitBootServices failed: %lx",
                                 (UINTN)Status);
    }
    else
    {
        TRACE("Exited boot services\n");
        BootServicesExitedFlag = TRUE;

        /*
         * NOTE: Cannot use UART here! After ExitBootServices, UEFI's identity
         * mappings are GONE. Any memory access to unmapped addresses will fault.
         * UART output only works AFTER our page tables are set up in mmu_v2.c.
         */
    }
}

VOID
UefiPrepareForReactOS(VOID)
{
    /*
     * Exit UEFI boot services. After this call:
     * - UEFI boot services are no longer available
     * - Memory map is finalized
     * - We have full control of the system
     *
     * On ARM64, _exituefi performs critical cache maintenance after
     * ExitBootServices to ensure instruction cache coherency, and
     * preserves the return address in a callee-saved register to
     * avoid stack corruption issues.
     */
    _exituefi();
}
