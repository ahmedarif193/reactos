/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 machine initialization for UEFI
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <freeldr.h>
#include <disk.h>
#include <arch/arm64/arm64.h>
#include <uefildr.h>
#include <arch/uefi/machuefi.h>
#include <arch/uefi/uefisym.h>
#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

/* Reference debug channel to avoid unused variable warning */
static inline void UseDebugChannel(void) { (void)DbgDefaultChannel; }

/* External UEFI globals */
extern EFI_SYSTEM_TABLE* GlobalSystemTable;

/* Global ARM64 hardware information - currently unused since we use UEFI directly */
#if 0
static ULONG Arm64ProcessorFeatures = 0;
static ULONG Arm64CacheLineSize = 64;
static BOOLEAN Arm64HwDetectRan = FALSE;
static PCONFIGURATION_COMPONENT_DATA RootNode = NULL;

/* ARM64 cache information */
static ULONG FirstLevelDcacheSize = 0;
static ULONG FirstLevelDcacheFillSize = 0;
static ULONG FirstLevelIcacheSize = 0;
static ULONG FirstLevelIcacheFillSize = 0;
static ULONG SecondLevelDcacheSize = 0;
static ULONG SecondLevelDcacheFillSize = 0;

/* CPU identification information */
static ULONG Arm64ProcessorType = 0;
static ULONG Arm64ProcessorRevision = 0;
static ULONG Arm64ProcessorArchitecture = 8; /* ARMv8 */
#endif

/* Forward declarations */
#if 0
static VOID Arm64DetectCpuFeatures(VOID);
static VOID Arm64DetectCacheInfo(VOID);
#endif

/* ARM64 wrappers are currently unused - we use UEFI functions directly */
#if 0
static VOID Arm64ConsPutChar(int Ch)
{
    /* Use UEFI console implementation */
    UefiConsPutChar(Ch);
}

static BOOLEAN Arm64ConsKbHit(VOID)
{
    /* Use UEFI console implementation */
    return UefiConsKbHit();
}

static int Arm64ConsGetCh(VOID)
{
    /* Use UEFI console implementation */
    return UefiConsGetCh();
}

static VOID Arm64VideoClearScreen(UCHAR Attr)
{
    /* Use UEFI video implementation */
    UefiVideoClearScreen(Attr);
}

static VIDEODISPLAYMODE Arm64VideoSetDisplayMode(char *DisplayMode, BOOLEAN Init)
{
    /* Use UEFI video implementation */
    return UefiVideoSetDisplayMode(DisplayMode, Init);
}

static VOID Arm64VideoGetDisplaySize(PULONG Width, PULONG Height, PULONG Depth)
{
    /* Use UEFI video implementation */
    UefiVideoGetDisplaySize(Width, Height, Depth);
}

static ULONG Arm64VideoGetBufferSize(VOID)
{
    /* Use UEFI video implementation */
    return UefiVideoGetBufferSize();
}

static VOID Arm64VideoGetFontsFromFirmware(PULONG RomFontPointers)
{
    /* Use UEFI video implementation */
    UefiVideoGetFontsFromFirmware(RomFontPointers);
}

static VOID Arm64VideoSetTextCursorPosition(UCHAR X, UCHAR Y)
{
    /* Use UEFI video implementation */
    UefiVideoSetTextCursorPosition(X, Y);
}

static VOID Arm64VideoHideShowTextCursor(BOOLEAN Show)
{
    /* Use UEFI video implementation */
    UefiVideoHideShowTextCursor(Show);
}

static VOID Arm64VideoPutChar(int Ch, UCHAR Attr, unsigned X, unsigned Y)
{
    /* Use UEFI video implementation */
    UefiVideoPutChar(Ch, Attr, X, Y);
}

static VOID Arm64VideoCopyOffScreenBufferToVRAM(PVOID Buffer)
{
    /* Use UEFI video implementation */
    UefiVideoCopyOffScreenBufferToVRAM(Buffer);
}

static BOOLEAN Arm64VideoIsPaletteFixed(VOID)
{
    /* Use UEFI video implementation */
    return UefiVideoIsPaletteFixed();
}

static VOID Arm64VideoSetPaletteColor(UCHAR Color, UCHAR Red, UCHAR Green, UCHAR Blue)
{
    /* Use UEFI video implementation */
    UefiVideoSetPaletteColor(Color, Red, Green, Blue);
}

static VOID Arm64VideoGetPaletteColor(UCHAR Color, UCHAR *Red, UCHAR *Green, UCHAR *Blue)
{
    /* Use UEFI video implementation */
    UefiVideoGetPaletteColor(Color, Red, Green, Blue);
}

static VOID Arm64VideoSync(VOID)
{
    /* Use UEFI video implementation */
    UefiVideoSync();
}

static VOID Arm64Beep(VOID)
{
    /* Use UEFI beep implementation */
    UefiPcBeep();
}
#endif

static VOID Arm64PrepareForReactOS(VOID)
{
    TRACE("ARM64: Preparing for ReactOS kernel handoff\n");
    
    /* Disable interrupts */
    Arm64DisableInterrupts();
    
    /* Disable timer interrupts */
    Arm64DisableTimerInterrupt();
    
    /* Complete cache maintenance using ARM64 specific routines */
    Arm64CompleteCacheMaintenance();
    
    /* Refresh loaded-image info before we lose Boot Services */
    UefiInitializeDebugImageInfo();

    /* Use UEFI preparation which exits boot services */
    UefiPrepareForReactOS();

    /* Post-ExitBootServices: it is now safe to install our vectors/timer */
    TRACE("ARM64: Post-ExitBootServices initialization\n");

    /* Install FreeLDR synchronous trap handlers so we log detailed faults */
    Arm64InitializeExceptions();

    /* Initialize generic timer for timekeeping/delays */
    Arm64InitializeTimer();

    /* Final memory barriers */
    Arm64DataMemoryBarrier();
    Arm64InstructionBarrier();

    TRACE("ARM64: Ready for kernel handoff\n");
}

/* ARM64 memory management - using UEFI implementation */
#if 0
static FREELDR_MEMORY_DESCRIPTOR* Arm64GetMemoryMap(PULONG MaxMemoryMapSize)
{
    /* Use UEFI memory management implementation */
    return UefiMemGetMemoryMap(MaxMemoryMapSize);
}
#endif

#if 0
/* ARM64 CPU feature detection */
static VOID Arm64DetectCpuFeatures(VOID)
{
    /* Skip CPU feature detection under UEFI to avoid system register traps */
    /* Use default generic ARM64 features */
    Arm64ProcessorType = 0xD07;  /* Generic Cortex-A57 equivalent */
    Arm64ProcessorRevision = 0;
    Arm64ProcessorFeatures = 0x7;  /* AES, SHA, FP enabled */

    TRACE("ARM64: Using default CPU features under UEFI\n");
    TRACE("ARM64: CPU Type=0x%lx, Revision=%lu, Features=0x%lx\n",
          Arm64ProcessorType, Arm64ProcessorRevision, Arm64ProcessorFeatures);

#if 0
    /* Original system register access code - will be used after ExitBootServices */
    ULONGLONG id_aa64isar0, id_aa64pfr0, midr;

    /* Read processor identification registers */
    midr = ARM64_READ_SYSREG(midr_el1);
    id_aa64isar0 = ARM64_READ_SYSREG(id_aa64isar0_el1);
    id_aa64pfr0 = ARM64_READ_SYSREG(id_aa64pfr0_el1);

    /* Extract processor information */
    Arm64ProcessorType = (ULONG)((midr >> 4) & 0xFFF);
    Arm64ProcessorRevision = (ULONG)(midr & 0xF);

    /* Check for various CPU features */
    Arm64ProcessorFeatures = 0;

    /* Check for AES support */
    if ((id_aa64isar0 & 0xF0) != 0)
    {
        Arm64ProcessorFeatures |= 0x1;
        TRACE("ARM64: AES encryption support detected\n");
    }

    /* Check for SHA support */
    if (((id_aa64isar0 >> 8) & 0xF) != 0)
    {
        Arm64ProcessorFeatures |= 0x2;
        TRACE("ARM64: SHA hash support detected\n");
    }

    /* Check for floating point support */
    if ((id_aa64pfr0 & 0xF) != 0xF)
    {
        Arm64ProcessorFeatures |= 0x4;
        TRACE("ARM64: Floating point support detected\n");
    }

    TRACE("ARM64: CPU Type=0x%lx, Revision=%lu, Features=0x%lx\n",
          Arm64ProcessorType, Arm64ProcessorRevision, Arm64ProcessorFeatures);
#endif
}
#endif

#if 0
/* ARM64 cache information detection - Enhanced */
static VOID Arm64DetectCacheInfo(VOID)
{
    /* Skip cache detection under UEFI to avoid system register traps */
    /* Use default values for now */
    Arm64CacheLineSize = 64;  /* Common default */
    FirstLevelDcacheSize = 32768;  /* 32KB default */
    FirstLevelDcacheFillSize = 64;
    FirstLevelIcacheSize = 32768;  /* 32KB default */
    FirstLevelIcacheFillSize = 64;
    SecondLevelDcacheSize = 262144;  /* 256KB default */
    SecondLevelDcacheFillSize = 64;

    TRACE("ARM64: Using default cache configuration under UEFI\n");
    TRACE("ARM64: L1 DCache=%lu KB, ICache=%lu KB, Line=%lu bytes\n",
          FirstLevelDcacheSize / 1024, FirstLevelIcacheSize / 1024, Arm64CacheLineSize);

#if 0
    /* Original system register access code - will be used after ExitBootServices */
    ULONGLONG ctr, ccsidr, clidr;

    /* Read cache type register (not strictly needed here) */
    ctr = ARM64_READ_SYSREG(ctr_el0); (void)ctr;

    /* Get cache line size using U-Boot method */
    Arm64CacheLineSize = (ULONG)Arm64GetCacheLineSize();

    /* Read cache level ID register */
    clidr = ARM64_READ_SYSREG(clidr_el1);
    ULONG loc = (ULONG)((clidr >> 24) & 0x7);  /* Level of Coherency */

    TRACE("ARM64: Cache LoC=%lu\n", loc);

    /* Detect L1 data cache */
    ARM64_WRITE_SYSREG(csselr_el1, 0);  /* L1 data cache */
    ARM64_ISB();

    ccsidr = ARM64_READ_SYSREG(ccsidr_el1);

    /* Calculate cache parameters using U-Boot method */
    ULONG line_size = 4 << ((ccsidr & 0x7) + 2);
    ULONG ways = ((ccsidr >> 3) & 0x3FF) + 1;
    ULONG sets = ((ccsidr >> 13) & 0x7FFF) + 1;

    FirstLevelDcacheSize = ways * sets * line_size;
    FirstLevelDcacheFillSize = line_size;

    /* Detect L1 instruction cache */
    ARM64_WRITE_SYSREG(csselr_el1, 1);  /* L1 instruction cache */
    ARM64_ISB();

    ccsidr = ARM64_READ_SYSREG(ccsidr_el1);

    line_size = 4 << ((ccsidr & 0x7) + 2);
    ways = ((ccsidr >> 3) & 0x3FF) + 1;
    sets = ((ccsidr >> 13) & 0x7FFF) + 1;

    FirstLevelIcacheSize = ways * sets * line_size;
    FirstLevelIcacheFillSize = line_size;

    /* Check for L2 cache */
    if (loc >= 2) {
        ARM64_WRITE_SYSREG(csselr_el1, 2);  /* L2 unified cache */
        ARM64_ISB();

        ccsidr = ARM64_READ_SYSREG(ccsidr_el1);

        line_size = 4 << ((ccsidr & 0x7) + 2);
        ways = ((ccsidr >> 3) & 0x3FF) + 1;
        sets = ((ccsidr >> 13) & 0x7FFF) + 1;

        SecondLevelDcacheSize = ways * sets * line_size;
        SecondLevelDcacheFillSize = line_size;

        TRACE("ARM64: L2 Cache=%lu KB, Line=%lu bytes\n",
              SecondLevelDcacheSize / 1024, line_size);
    }

    TRACE("ARM64: L1 DCache=%lu KB, ICache=%lu KB, Line=%lu bytes\n",
          FirstLevelDcacheSize / 1024, FirstLevelIcacheSize / 1024, Arm64CacheLineSize);

    /* Reset cache selection register */
    ARM64_WRITE_SYSREG(csselr_el1, 0);
    ARM64_ISB();
#endif
}
#endif

#if 0
static VOID Arm64GetExtendedBIOSData(PULONG ExtendedBIOSDataArea, PULONG ExtendedBIOSDataSize)
{
    /* Use UEFI implementation */
    UefiGetExtendedBIOSData(ExtendedBIOSDataArea, ExtendedBIOSDataSize);
}

static UCHAR Arm64GetFloppyCount(VOID)
{
    /* Use UEFI implementation */
    return UefiGetFloppyCount();
}
#endif

#if 0
static BOOLEAN Arm64DiskReadLogicalSectors(IN UCHAR DriveNumber,
                                           IN ULONGLONG SectorNumber,
                                           IN ULONG SectorCount,
                                           OUT PVOID Buffer)
{
    /* Use UEFI disk I/O implementation */
    return UefiDiskReadLogicalSectors(DriveNumber, SectorNumber, SectorCount, Buffer);
}

static BOOLEAN Arm64DiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry)
{
    /* Use UEFI implementation */
    return UefiDiskGetDriveGeometry(DriveNumber, Geometry);
}

static ULONG Arm64DiskGetCacheableBlockCount(UCHAR DriveNumber)
{
    /* Use UEFI implementation */
    return UefiDiskGetCacheableBlockCount(DriveNumber);
}
#endif

#if 0
static PCONFIGURATION_COMPONENT_DATA Arm64HwDetect(const CHAR* Options)
{
    if (Arm64HwDetectRan)
        return RootNode;
    
    Arm64HwDetectRan = TRUE;
    
    TRACE("ARM64: Hardware detection started\n");
    
    /* Detect ARM64 specific CPU features and cache information */
    Arm64DetectCpuFeatures();
    Arm64DetectCacheInfo();
    
    /* Use UEFI hardware detection for most components */
    RootNode = UefiHwDetect(Options);
    
    /* Add ARM64 specific hardware information to the tree */
    if (RootNode)
    {
        /* Add ARM64 processor information to existing tree */
        PCONFIGURATION_COMPONENT_DATA ProcessorKey;
        CHAR Buffer[128];
        
        /* Find or create processor node */
        FldrCreateComponentKey(RootNode,
                               ProcessorClass,
                               CentralProcessor,
                               0,
                               0,
                               0,
                               "ARM64 Processor",
                               NULL,
                               0,
                               &ProcessorKey);
        
        /* Add processor identifier */
        RtlStringCbPrintfA(Buffer, sizeof(Buffer),
                           "ARM64 Family %lu Model %lu Stepping %lu",
                           Arm64ProcessorArchitecture,
                           Arm64ProcessorType,
                           Arm64ProcessorRevision);
        
        /* Identifier is already set by FldrCreateComponentKey; optional extra info is skipped */
        
        /* Add cache information if available */
        if (FirstLevelDcacheSize > 0)
        {
            PCONFIGURATION_COMPONENT_DATA CacheKey;
            FldrCreateComponentKey(ProcessorKey,
                                   CacheClass,
                                   PrimaryDcache,
                                   0,
                                   0,
                                   FirstLevelDcacheSize,
                                   "L1 Data Cache",
                                   NULL,
                                   0,
                                   &CacheKey);
        }
        
        if (FirstLevelIcacheSize > 0)
        {
            PCONFIGURATION_COMPONENT_DATA CacheKey;
            FldrCreateComponentKey(ProcessorKey,
                                   CacheClass,
                                   PrimaryIcache,
                                   0,
                                   0,
                                   FirstLevelIcacheSize,
                                   "L1 Instruction Cache",
                                   NULL,
                                   0,
                                   &CacheKey);
        }
    }
    
    /* Initialize RAMDISK if available */
    RamDiskInitialize(TRUE, NULL, NULL);
    
    TRACE("ARM64: Hardware detection completed\n");
    
    return RootNode;
}
#endif

#if 0
static VOID Arm64HwIdle(VOID)
{
    /* Use UEFI idle implementation with ARM64 WFI fallback */
    UefiHwIdle();
    
    /* ARM64 wait for interrupt instruction as backup */
    __asm__ volatile ("wfi" ::: "memory");
}
#endif

/* Simple disk reading for UEFI */
static ARC_STATUS Arm64DiskOpen(CHAR* Path, OPENMODE OpenMode, ULONG* FileId)
{
    /* Just return success for now */
    *FileId = 1;
    return ESUCCESS;
}

static ARC_STATUS Arm64DiskClose(ULONG FileId)
{
    return ESUCCESS;
}

static ARC_STATUS Arm64DiskGetFileInformation(ULONG FileId, FILEINFORMATION* Information)
{
    RtlZeroMemory(Information, sizeof(FILEINFORMATION));
    return ESUCCESS;
}

/* Global state for disk reading */
static ULONGLONG CurrentDiskPosition = 0;

static ARC_STATUS Arm64DiskRead(ULONG FileId, VOID* Buffer, ULONG N, ULONG* Count)
{
    extern UCHAR FrldrBootDrive;
    ULONG SectorSize = 512; /* Standard sector size */
    ULONGLONG StartSector;
    ULONG SectorCount;
    ULONG BytesToRead;
    
    /* Calculate sectors to read */
    StartSector = CurrentDiskPosition / SectorSize;
    BytesToRead = N;
    SectorCount = (BytesToRead + SectorSize - 1) / SectorSize;
    
    /* Try to read the sectors */
    if (UefiDiskReadLogicalSectors(FrldrBootDrive, StartSector, SectorCount, Buffer))
    {
        *Count = BytesToRead;
        CurrentDiskPosition += BytesToRead;
        return ESUCCESS;
    }
    
    *Count = 0;
    return EIO;
}

static ARC_STATUS Arm64DiskSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE SeekMode)
{
    switch (SeekMode)
    {
        case SeekAbsolute:
            CurrentDiskPosition = Position->QuadPart;
            break;
        case SeekRelative:
            CurrentDiskPosition += Position->QuadPart;
            break;
        default:
            return EINVAL;
    }
    
    Position->QuadPart = CurrentDiskPosition;
    return ESUCCESS;
}

static const DEVVTBL Arm64DiskVtbl =
{
    Arm64DiskClose,
    Arm64DiskGetFileInformation,
    Arm64DiskOpen,
    Arm64DiskRead,
    Arm64DiskSeek,
};

#if 0
static BOOLEAN Arm64InitializeBootDevices(VOID)
{
    /* Use UEFI boot device initialization */
    return UefiInitializeBootDevices();
}

static TIMEINFO* Arm64GetTime(VOID)
{
    /* Use UEFI time services */
    return UefiGetTime();
}
#endif

/* Initialize machine abstraction for ARM64 */
VOID Arm64MachInit(const char *CmdLine)
{
    /* Use early debug output to pinpoint crash */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: MachInit entry\r\n");

    /* Install exception vectors immediately so we log early faults */
    Arm64InitializeExceptions();

    /* Gather loaded-image information for future backtraces */
    UefiInitializeDebugImageInfo();

    /* ARM64 UEFI: Disable screen debug output to keep console clean */
    DebugDisableScreenPort();

    /* Timer initialization deferred - will be done on first use */
    /* Arm64InitializeTimer() is called lazily from timer functions */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: Deferring timer init\r\n");

    /* Skip TRACE until we know it's safe */
    /* TRACE("ARM64: Initializing machine abstraction layer\n"); */

    /* Skip other low-level initializations until after ExitBootServices */
    /* Arm64InitializeTimer(); - Skip, can trap under UEFI */
    /* Arm64InitializeMMU(); - Skip, UEFI manages memory */

    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: About to clear MachVtbl\r\n");

    /* Clear the machine vtable */
    RtlZeroMemory(&MachVtbl, sizeof(MachVtbl));

    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: MachVtbl cleared\r\n");

    /* Console functions - Use UEFI directly */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: Setting console functions\r\n");
    MachVtbl.ConsPutChar = UefiConsPutChar;
    MachVtbl.ConsKbHit = UefiConsKbHit;
    MachVtbl.ConsGetCh = UefiConsGetCh;

    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: Console functions set\r\n");

    /* Video functions - These all route to UEFI implementations */
    /* Check if these might be calling something that's not initialized yet */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: Setting video functions - using UEFI implementations\r\n");

    /* Use direct UEFI function pointers instead of ARM64 wrappers for now */
    MachVtbl.VideoClearScreen = UefiVideoClearScreen;
    MachVtbl.VideoSetDisplayMode = UefiVideoSetDisplayMode;
    MachVtbl.VideoGetDisplaySize = UefiVideoGetDisplaySize;
    MachVtbl.VideoGetBufferSize = UefiVideoGetBufferSize;
    MachVtbl.VideoGetFontsFromFirmware = UefiVideoGetFontsFromFirmware;
    MachVtbl.VideoSetTextCursorPosition = UefiVideoSetTextCursorPosition;
    MachVtbl.VideoHideShowTextCursor = UefiVideoHideShowTextCursor;
    MachVtbl.VideoPutChar = UefiVideoPutChar;
    MachVtbl.VideoCopyOffScreenBufferToVRAM = UefiVideoCopyOffScreenBufferToVRAM;
    MachVtbl.VideoIsPaletteFixed = UefiVideoIsPaletteFixed;
    MachVtbl.VideoSetPaletteColor = UefiVideoSetPaletteColor;
    MachVtbl.VideoGetPaletteColor = UefiVideoGetPaletteColor;
    MachVtbl.VideoSync = UefiVideoSync;

    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: Video functions set\r\n");

    /* System functions - Use UEFI directly for most */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: Setting system functions\r\n");

    MachVtbl.Beep = UefiPcBeep;
    MachVtbl.PrepareForReactOS = Arm64PrepareForReactOS;
    MachVtbl.GetMemoryMap = UefiMemGetMemoryMap;
    MachVtbl.GetExtendedBIOSData = UefiGetExtendedBIOSData;
    MachVtbl.GetFloppyCount = UefiGetFloppyCount;

    /* Disk functions */
    MachVtbl.DiskReadLogicalSectors = UefiDiskReadLogicalSectors;
    MachVtbl.DiskGetDriveGeometry = UefiDiskGetDriveGeometry;
    MachVtbl.DiskGetCacheableBlockCount = UefiDiskGetCacheableBlockCount;

    /* Hardware detection and management */
    MachVtbl.HwDetect = UefiHwDetect;
    MachVtbl.HwIdle = UefiHwIdle;
    MachVtbl.InitializeBootDevices = UefiInitializeBootDevices;
    MachVtbl.GetTime = UefiGetTime;

    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: All functions set\r\n");

    /* TRACE("ARM64: Machine abstraction layer initialized\n"); - Skip TRACE for now */

    /* Note: UefiInitializeVideo() will be called from the common UEFI path in uefisetup.c */
    /* Don't call it here to avoid double initialization */

    /* Skip hardware detection here - it will be called when needed */
    /* Arm64HwDetect(NULL); */

    /* TRACE("ARM64: Machine vtable configured\n"); - Skip TRACE for now */

    /* Initialize GOP (Graphics Output Protocol) for ARM64 */
    /* REMOVED: GOP initialization is deferred until first use in UefiVideoSetDisplayMode */
    /* This avoids issues with double initialization and ensures proper sequencing */
    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: Deferring GOP initialization to first use\r\n");

    if (GlobalSystemTable && GlobalSystemTable->ConOut)
        GlobalSystemTable->ConOut->OutputString(GlobalSystemTable->ConOut, L"ARM64: MachInit complete, returning\r\n");

    /* Use the debug channel to avoid warning */
    UseDebugChannel();
}
