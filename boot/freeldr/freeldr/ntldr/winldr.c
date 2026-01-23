/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Windows-compatible NT OS Loader.
 * COPYRIGHT:   Copyright 2006-2019 Aleksey Bragin <aleksey@reactos.org>
 */

#include <freeldr.h>
#include <ndk/ldrtypes.h>
#include "winldr.h"
#include "ntldropts.h"
#include "registry.h"
#include "ramdisk.h"
#include <framebuffer.h>
#include <internal/cmboot.h>

#ifdef UEFIBOOT
/* UEFI boot path helpers. */
#include <uefildr.h>
#include <uefi/uefiarcname.h>
/* Needed for BGRT table detection when running under UEFI. */
#include <drivers/acpi/acpi.h>
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern VOID UefiVideoRefreshBootLogo(VOID);
#endif

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

#if defined(_M_ARM64)
#include <reactos/arm64/early_uart.h>
#endif

ULONG ArcGetDiskCount(VOID);
PARC_DISK_SIGNATURE_EX ArcGetDiskInfo(ULONG Index);

BOOLEAN IsAcpiPresent(VOID);

extern HEADLESS_LOADER_BLOCK LoaderRedirectionInformation;
extern BOOLEAN WinLdrTerminalConnected;
extern VOID WinLdrSetupEms(IN PCSTR BootOptions);

PLOADER_SYSTEM_BLOCK WinLdrSystemBlock;
/**/PCWSTR BootFileSystem = NULL;/**/

BOOLEAN VirtualBias = FALSE;
BOOLEAN SosEnabled = FALSE;
BOOLEAN SafeBoot = FALSE;
BOOLEAN BootLogo = FALSE;
#ifdef _M_IX86
BOOLEAN PaeModeOn = FALSE;
#endif
BOOLEAN NoExecuteEnabled = FALSE;

// debug stuff
VOID DumpMemoryAllocMap(VOID);

/* PE loader import-DLL loading callback */
static VOID
NTAPI
NtLdrImportDllLoadCallback(
    _In_ PCSTR FileName)
{
    NtLdrOutputLoadMsg(FileName, NULL);
}

VOID
NtLdrOutputLoadMsg(
    _In_ PCSTR FileName,
    _In_opt_ PCSTR Description)
{
    if (SosEnabled)
    {
        printf("  %s\n", FileName);
        TRACE("Loading: %s\n", FileName);
    }
    else
    {
        /* Inform the user we load a file */
        CHAR ProgressString[256];

        RtlStringCbPrintfA(ProgressString, sizeof(ProgressString),
                           "Loading %s...",
                           (Description ? Description : FileName));
        // UiSetProgressBarText(ProgressString);
        // UiIndicateProgress();
        UiDrawStatusText(ProgressString);
    }
}

// Init "phase 0"
VOID
AllocateAndInitLPB(
    IN USHORT VersionToBoot,
    OUT PLOADER_PARAMETER_BLOCK* OutLoaderBlock)
{
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    PLOADER_PARAMETER_EXTENSION Extension;

    /* Allocate and zero-init the Loader Parameter Block */
    WinLdrSystemBlock = MmAllocateMemoryWithType(sizeof(LOADER_SYSTEM_BLOCK),
                                                 LoaderSystemBlock);
    if (WinLdrSystemBlock == NULL)
    {
        UiMessageBox("Failed to allocate memory for system block!");
        return;
    }

    RtlZeroMemory(WinLdrSystemBlock, sizeof(LOADER_SYSTEM_BLOCK));

    LoaderBlock = &WinLdrSystemBlock->LoaderBlock;
    LoaderBlock->NlsData = &WinLdrSystemBlock->NlsDataBlock;

    /*
     * Seed loader performance data for the kernel debugger timestamping.
     * We start at the current bootloader-relative time (usually 0 on first call)
     * and set EndTime just before transferring control to the kernel.
     */
    WinLdrSystemBlock->LoaderPerformanceData.StartTime = DbgQueryMicrosecondsSinceBoot();
    WinLdrSystemBlock->LoaderPerformanceData.EndTime = 0;

    /* Initialize the Loader Block Extension */
    Extension = &WinLdrSystemBlock->Extension;
    LoaderBlock->Extension = Extension;
    Extension->Size = sizeof(LOADER_PARAMETER_EXTENSION);
    Extension->MajorVersion = (VersionToBoot & 0xFF00) >> 8;
    Extension->MinorVersion = (VersionToBoot & 0xFF);

    /* Init three critical lists, used right away */
    InitializeListHead(&LoaderBlock->LoadOrderListHead);
    InitializeListHead(&LoaderBlock->MemoryDescriptorListHead);
    InitializeListHead(&LoaderBlock->BootDriverListHead);

    /* Record the firmware type based on the boot method. */
#ifdef UEFIBOOT
    if (GlobalSystemTable != NULL)
    {
        /* We're booting via UEFI */
        LoaderBlock->FirmwareInformation.FirmwareTypeEfi = 1;
        Extension->BootViaEFI = 1;
        TRACE("Setting FirmwareTypeEfi = 1 (UEFI boot detected)\n");
    }
    else
#endif
    {
        /* We're booting via legacy BIOS */
        LoaderBlock->FirmwareInformation.FirmwareTypeEfi = 0;
        Extension->BootViaEFI = 0;
        TRACE("Setting FirmwareTypeEfi = 0 (BIOS boot)\n");
    }

    *OutLoaderBlock = LoaderBlock;
}

// Init "phase 1"
VOID
WinLdrInitializePhase1(PLOADER_PARAMETER_BLOCK LoaderBlock,
                       PCSTR Options,
                       PCSTR SystemRoot,
                       PCSTR BootPath,
                       USHORT VersionToBoot)
{
    /*
     * Examples of correct options and paths:
     * CHAR Options[] = "/DEBUGPORT=COM1 /BAUDRATE=115200";
     * CHAR Options[] = "/NODEBUG";
     * CHAR SystemRoot[] = "\\WINNT\\";
     * CHAR ArcBoot[] = "multi(0)disk(0)rdisk(0)partition(1)";
     */

    PSTR  LoadOptions, NewLoadOptions;
    CHAR  HalPath[] = "\\";
    CHAR  ArcBoot[MAX_PATH+1];
    CHAR  MiscFiles[MAX_PATH+1];
    ULONG i;
    PLOADER_PARAMETER_EXTENSION Extension;

    /* Prefer the UEFI-specific helpers when we are running under UEFI firmware. */
    /*
     * General rule: derive ArcBoot directly from the finalized BootPath.
     * This ensures ArcBootDeviceName/ArcHalDeviceName reflect the actual
     * device used for the system root (e.g. ramdisk(0), cdrom(0), rdisk(n)).
     */
    {
        PCSTR Sep = strstr(BootPath, "\\");
        if (!Sep)
        {
            RtlStringCbCopyA(ArcBoot, sizeof(ArcBoot), BootPath);
        }
        else
        {
            SIZE_T PrefixLength = (SIZE_T)(Sep - BootPath);
            RtlStringCbCopyNA(ArcBoot, sizeof(ArcBoot), BootPath, PrefixLength);
        }
    }
    TRACE("ArcBoot derived from BootPath: '%s'\n", ArcBoot);

    TRACE("ArcBoot: '%s'\n", ArcBoot);
    TRACE("SystemRoot: '%s'\n", SystemRoot);
    TRACE("Options: '%s'\n", Options);

    /* Fill ARC BootDevice */
    LoaderBlock->ArcBootDeviceName = WinLdrSystemBlock->ArcBootDeviceName;
    RtlStringCbCopyA(LoaderBlock->ArcBootDeviceName, sizeof(WinLdrSystemBlock->ArcBootDeviceName), ArcBoot);
    LoaderBlock->ArcBootDeviceName = PaToVa(LoaderBlock->ArcBootDeviceName);

//
// IMPROVE!!
// SetupBlock->ArcSetupDeviceName must be the path to the setup **SOURCE**,
// and not the setup boot path. Indeed they may differ!!
//
    if (LoaderBlock->SetupLdrBlock)
    {
        PSETUP_LOADER_BLOCK SetupBlock = LoaderBlock->SetupLdrBlock;

        /* Adjust the ARC path in the setup block - Matches ArcBoot path */
        SetupBlock->ArcSetupDeviceName = WinLdrSystemBlock->ArcBootDeviceName;
        SetupBlock->ArcSetupDeviceName = PaToVa(SetupBlock->ArcSetupDeviceName);

        /* Convert the setup block pointer */
        LoaderBlock->SetupLdrBlock = PaToVa(LoaderBlock->SetupLdrBlock);
    }

    /* Fill ARC HalDevice, it matches ArcBoot path */
    LoaderBlock->ArcHalDeviceName = WinLdrSystemBlock->ArcBootDeviceName;
    LoaderBlock->ArcHalDeviceName = PaToVa(LoaderBlock->ArcHalDeviceName);

    /* Fill SystemRoot */
    LoaderBlock->NtBootPathName = WinLdrSystemBlock->NtBootPathName;
    RtlStringCbCopyA(LoaderBlock->NtBootPathName, sizeof(WinLdrSystemBlock->NtBootPathName), SystemRoot);
    LoaderBlock->NtBootPathName = PaToVa(LoaderBlock->NtBootPathName);

    /* Fill NtHalPathName */
    LoaderBlock->NtHalPathName = WinLdrSystemBlock->NtHalPathName;
    RtlStringCbCopyA(LoaderBlock->NtHalPathName, sizeof(WinLdrSystemBlock->NtHalPathName), HalPath);
    LoaderBlock->NtHalPathName = PaToVa(LoaderBlock->NtHalPathName);

    /* Fill LoadOptions and strip the '/' switch symbol in front of each option */
    NewLoadOptions = LoadOptions = LoaderBlock->LoadOptions = WinLdrSystemBlock->LoadOptions;
    RtlStringCbCopyA(LoaderBlock->LoadOptions, sizeof(WinLdrSystemBlock->LoadOptions), Options);

    do
    {
        while (*LoadOptions == '/')
            ++LoadOptions;

        *NewLoadOptions++ = *LoadOptions;
    } while (*LoadOptions++);

    LoaderBlock->LoadOptions = PaToVa(LoaderBlock->LoadOptions);

    /* ARC devices */
    LoaderBlock->ArcDiskInformation = &WinLdrSystemBlock->ArcDiskInformation;
    InitializeListHead(&LoaderBlock->ArcDiskInformation->DiskSignatureListHead);

    /* Populate the ARC disk tables via the UEFI helpers when we booted via UEFI. */
#ifdef UEFIBOOT
    if (GlobalSystemTable != NULL)
    {
        /* Use UEFI-specific ARC disk initialization */
        if (!UefiInitializeArcDisks(LoaderBlock))
        {
            ERR("UefiInitializeArcDisks() failed\n");
        }
    }
    else
#endif
    {
        /* Convert ARC disk information from freeldr to a correct format */
        ULONG DiscCount = ArcGetDiskCount();
        for (i = 0; i < DiscCount; i++)
        {
            PARC_DISK_SIGNATURE_EX ArcDiskSig;

            /* Allocate the ARC structure */
            ArcDiskSig = FrLdrHeapAlloc(sizeof(ARC_DISK_SIGNATURE_EX), 'giSD');
            if (!ArcDiskSig)
            {
                ERR("Failed to allocate ARC structure! Ignoring remaining ARC disks. (i = %lu, DiskCount = %lu)\n",
                    i, DiscCount);
                break;
            }

            /* Copy the data over */
            RtlCopyMemory(ArcDiskSig, ArcGetDiskInfo(i), sizeof(ARC_DISK_SIGNATURE_EX));

            /* Set the ARC Name pointer */
            ArcDiskSig->DiskSignature.ArcName = PaToVa(ArcDiskSig->ArcName);

            /* Insert into the list */
            InsertTailList(&LoaderBlock->ArcDiskInformation->DiskSignatureListHead,
                           &ArcDiskSig->DiskSignature.ListEntry);
        }
    }

    /* Convert all lists to Virtual address */

    /* Convert the ArcDisks list to virtual address */
    List_PaToVa(&LoaderBlock->ArcDiskInformation->DiskSignatureListHead);
    LoaderBlock->ArcDiskInformation = PaToVa(LoaderBlock->ArcDiskInformation);

    /* Convert configuration entries to VA */
    ConvertConfigToVA(LoaderBlock->ConfigurationRoot);
    LoaderBlock->ConfigurationRoot = PaToVa(LoaderBlock->ConfigurationRoot);

    /* Convert all DTE into virtual addresses */
    List_PaToVa(&LoaderBlock->LoadOrderListHead);

    /* This one will be converted right before switching to virtual paging mode */
    //List_PaToVa(&LoaderBlock->MemoryDescriptorListHead);

    /* Convert list of boot drivers */
    List_PaToVa(&LoaderBlock->BootDriverListHead);

    Extension = LoaderBlock->Extension;

    /* FIXME! HACK value for docking profile */
    Extension->Profile.Status = 2;

    /* Check if FreeLdr detected a ACPI table */
    if (IsAcpiPresent())
    {
        /* Set the pointer to something for compatibility */
        Extension->AcpiTable = (PVOID)1;
        // FIXME: Extension->AcpiTableSize;
    }

    /* Pass GOP framebuffer info to the kernel so it can reuse the firmware display. */
#ifdef UEFIBOOT
    {
        extern REACTOS_INTERNAL_BGCONTEXT framebufferData;
        extern PBGRT_TABLE GetBgrtTable(VOID);
        extern PLOADER_PARAMETER_GOP_MODE UefiGopModes;
        extern SIZE_T UefiGopModeCount;
        extern ULONG UefiGopPreferredMode;
        extern PLOADER_PARAMETER_FRAMEBUFFER UefiGopFramebuffers;
        extern ULONG UefiGopFramebufferCount;
        extern BOOLEAN UefiGopModesPermanent;
        extern BOOLEAN UefiGopFramebuffersPermanent;
        extern BOOLEAN UefiIsFramebufferReady(VOID);
        
        if (framebufferData.BaseAddress != 0)
        {
            Extension->GopFramebuffer.FrameBufferBase.QuadPart = framebufferData.BaseAddress;
            Extension->GopFramebuffer.FrameBufferSize = framebufferData.BufferSize;
            Extension->GopFramebuffer.HorizontalResolution = framebufferData.ScreenWidth;
            Extension->GopFramebuffer.VerticalResolution = framebufferData.ScreenHeight;
            Extension->GopFramebuffer.PixelsPerScanLine = framebufferData.PixelsPerScanLine;
            Extension->GopFramebuffer.PixelFormat = framebufferData.PixelFormat;
            Extension->GopFramebuffer.RedMask = framebufferData.RedMask;
            Extension->GopFramebuffer.GreenMask = framebufferData.GreenMask;
            Extension->GopFramebuffer.BlueMask = framebufferData.BlueMask;
            Extension->GopFramebuffer.Reserved = framebufferData.ReservedMask;

            TRACE("Passing GOP framebuffer to kernel:\n");
            TRACE("  BaseAddress: 0x%llx\n", Extension->GopFramebuffer.FrameBufferBase.QuadPart);
            TRACE("  Size: 0x%x\n", Extension->GopFramebuffer.FrameBufferSize);
            TRACE("  Resolution: %dx%d\n", Extension->GopFramebuffer.HorizontalResolution,
                  Extension->GopFramebuffer.VerticalResolution);
            TRACE("  PixelsPerScanLine: %d\n", Extension->GopFramebuffer.PixelsPerScanLine);
            TRACE("  PixelFormat: %d\n", Extension->GopFramebuffer.PixelFormat);
            TRACE("  Masks: R=0x%08x G=0x%08x B=0x%08x\n",
                  Extension->GopFramebuffer.RedMask,
                  Extension->GopFramebuffer.GreenMask,
                  Extension->GopFramebuffer.BlueMask);
        }
        else
        {
            WARN("UEFI GOP framebuffer info missing; graphical miniports will stay disabled\n");
            if (!UefiIsFramebufferReady())
            {
                WARN("UEFI GOP never reported a framebuffer (ramdisk/live boot path?)\n");
            }
        }

        if (UefiGopFramebuffers && UefiGopFramebufferCount)
        {
            SIZE_T bytes = (SIZE_T)UefiGopFramebufferCount * sizeof(LOADER_PARAMETER_FRAMEBUFFER);

            if (!UefiGopFramebuffersPermanent)
            {
                PVOID permanent = MmAllocateMemoryWithType(bytes, LoaderMemoryData);
                if (permanent)
                {
                    RtlCopyMemory(permanent, UefiGopFramebuffers, bytes);
                    UefiGopFramebuffers = permanent;
                    UefiGopFramebuffersPermanent = TRUE;
                    TRACE("Copied %lu framebuffer descriptors into permanent loader memory\n",
                          (unsigned long)UefiGopFramebufferCount);
                }
                else
                {
                    WARN("Failed to allocate permanent storage for GOP framebuffer list; dropping entries\n");
                    UefiGopFramebuffers = NULL;
                    UefiGopFramebufferCount = 0;
                }
            }

            if (UefiGopFramebuffers && UefiGopFramebuffersPermanent)
            {
                Extension->GopFramebuffers = (PLOADER_PARAMETER_FRAMEBUFFER)PaToVa(UefiGopFramebuffers);
                Extension->GopFramebufferCount = UefiGopFramebufferCount;
            }
        }
        else if (Extension->GopFramebuffer.FrameBufferBase.QuadPart != 0)
        {
            Extension->GopFramebuffers = (PLOADER_PARAMETER_FRAMEBUFFER)PaToVa(&Extension->GopFramebuffer);
            Extension->GopFramebufferCount = 1;
        }
        else
        {
            Extension->GopFramebuffers = NULL;
            Extension->GopFramebufferCount = 0;
        }


        /* Pass GOP mode enumeration to the kernel (if available) */
        if (UefiGopModes && UefiGopModeCount)
        {
            SIZE_T bytes = UefiGopModeCount * sizeof(LOADER_PARAMETER_GOP_MODE);

            if (!UefiGopModesPermanent)
            {
                PVOID permanent = MmAllocateMemoryWithType(bytes, LoaderMemoryData);
                if (permanent)
                {
                    RtlCopyMemory(permanent, UefiGopModes, bytes);
                    UefiGopModes = permanent;
                    UefiGopModesPermanent = TRUE;
                    TRACE("Copied %Iu GOP mode descriptors into permanent loader memory\n",
                          UefiGopModeCount);
                }
                else
                {
                    WARN("Failed to allocate permanent storage for GOP mode list; dropping descriptors\n");
                    UefiGopModes = NULL;
                    UefiGopModeCount = 0;
                }
            }

            if (UefiGopModes && UefiGopModesPermanent)
            {
                ULONG GopModeCountClamped;

                Extension->GopModes = (PLOADER_PARAMETER_GOP_MODE)PaToVa(UefiGopModes);
                if (UefiGopModeCount > MAXULONG)
                {
                    WARN("Truncating GOP mode count %Iu to %lu for loader block\n",
                         UefiGopModeCount,
                         (ULONG)MAXULONG);
                    GopModeCountClamped = MAXULONG;
                }
                else
                {
                    GopModeCountClamped = (ULONG)UefiGopModeCount;
                }

                Extension->GopModeCount = GopModeCountClamped;
                Extension->GopPreferredMode = UefiGopPreferredMode;
                TRACE("Passing %Iu GOP modes to kernel (preferred=%lu)\n", UefiGopModeCount, UefiGopPreferredMode);
            }
        }

        /* Pass BGRT info to the kernel for seamless boot logo support. */
        PBGRT_TABLE Bgrt = GetBgrtTable();
        if (Bgrt)
        {
            Extension->BgrtInfo.Valid = TRUE;
            Extension->BgrtInfo.ImageType = Bgrt->ImageType;
            Extension->BgrtInfo.ImageAddress = Bgrt->LogoAddress;
            // Calculate image size from BMP header if needed (for now set to 0)
            Extension->BgrtInfo.ImageSize = 0; // Will be determined later from BMP header
            Extension->BgrtInfo.ImageOffsetX = Bgrt->OffsetX;
            Extension->BgrtInfo.ImageOffsetY = Bgrt->OffsetY;
            
            TRACE("BGRT info passed to kernel:\n");
            TRACE("  ImageType: %u\n", Extension->BgrtInfo.ImageType);
            TRACE("  ImageAddress: 0x%llx\n", Extension->BgrtInfo.ImageAddress);
            TRACE("  ImageOffset: (%lu, %lu)\n", Extension->BgrtInfo.ImageOffsetX,
                  Extension->BgrtInfo.ImageOffsetY);
        }
        else
        {
            Extension->BgrtInfo.Valid = FALSE;
            TRACE("No BGRT table found, boot logo support disabled\n");
        }
    }
#else
    {
        extern FREELDR_FRAMEBUFFER_INFO PcFramebufferInfo;

        if ((PcFramebufferInfo.BaseAddress != 0) &&
            (PcFramebufferInfo.BufferSize != 0) &&
            (PcFramebufferInfo.ScreenWidth != 0) &&
            (PcFramebufferInfo.ScreenHeight != 0))
        {
            Extension->GopFramebuffer.FrameBufferBase.QuadPart = PcFramebufferInfo.BaseAddress;
            Extension->GopFramebuffer.FrameBufferSize = PcFramebufferInfo.BufferSize;
            Extension->GopFramebuffer.HorizontalResolution = PcFramebufferInfo.ScreenWidth;
            Extension->GopFramebuffer.VerticalResolution = PcFramebufferInfo.ScreenHeight;
            Extension->GopFramebuffer.PixelsPerScanLine = PcFramebufferInfo.PixelsPerScanLine;
            Extension->GopFramebuffer.PixelFormat = PcFramebufferInfo.PixelFormat;
            Extension->GopFramebuffer.RedMask = PcFramebufferInfo.RedMask;
            Extension->GopFramebuffer.GreenMask = PcFramebufferInfo.GreenMask;
            Extension->GopFramebuffer.BlueMask = PcFramebufferInfo.BlueMask;
            Extension->GopFramebuffer.Reserved = PcFramebufferInfo.ReservedMask;

            TRACE("Passing VESA framebuffer to kernel:\n");
            TRACE("  BaseAddress: 0x%llx\n", Extension->GopFramebuffer.FrameBufferBase.QuadPart);
            TRACE("  Size: 0x%x\n", Extension->GopFramebuffer.FrameBufferSize);
            TRACE("  Resolution: %dx%d\n", Extension->GopFramebuffer.HorizontalResolution,
                  Extension->GopFramebuffer.VerticalResolution);
            TRACE("  PixelsPerScanLine: %d\n", Extension->GopFramebuffer.PixelsPerScanLine);
            TRACE("  PixelFormat: %d\n", Extension->GopFramebuffer.PixelFormat);
            TRACE("  Masks: R=0x%08x G=0x%08x B=0x%08x\n",
                  Extension->GopFramebuffer.RedMask,
                  Extension->GopFramebuffer.GreenMask,
                  Extension->GopFramebuffer.BlueMask);
        }
    }
#endif


    /*
     * Always provide LoaderPerformanceData so the kernel can use it to
     * continue debugger timestamps seamlessly. Keep BootViaWinload flag
     * for Vista+ semantics.
     */
    if (VersionToBoot >= _WIN32_WINNT_VISTA)
    {
        Extension->BootViaWinload = 1;
    }
    Extension->LoaderPerformanceData = PaToVa(&WinLdrSystemBlock->LoaderPerformanceData);

    InitializeListHead(&Extension->BootApplicationPersistentData);
    List_PaToVa(&Extension->BootApplicationPersistentData);

#ifdef _M_IX86
    /* Set headless block pointer */
    if (WinLdrTerminalConnected)
    {
        Extension->HeadlessLoaderBlock = &WinLdrSystemBlock->HeadlessLoaderBlock;
        RtlCopyMemory(Extension->HeadlessLoaderBlock,
                      &LoaderRedirectionInformation,
                      sizeof(HEADLESS_LOADER_BLOCK));
        Extension->HeadlessLoaderBlock = PaToVa(Extension->HeadlessLoaderBlock);
    }
#endif
    /* Load drivers database */
    RtlStringCbCopyA(MiscFiles, sizeof(MiscFiles), BootPath);
    RtlStringCbCatA(MiscFiles, sizeof(MiscFiles), "AppPatch\\drvmain.sdb");
    Extension->DrvDBImage = PaToVa(WinLdrLoadModule(MiscFiles,
                                                    &Extension->DrvDBSize,
                                                    LoaderRegistryData));

    /* Convert the extension block pointer */
    LoaderBlock->Extension = PaToVa(LoaderBlock->Extension);

    TRACE("WinLdrInitializePhase1() completed\n");
}

static BOOLEAN
WinLdrLoadDeviceDriver(PLIST_ENTRY LoadOrderListHead,
                       PCSTR BootPath,
                       PUNICODE_STRING FilePath,
                       ULONG Flags,
                       PLDR_DATA_TABLE_ENTRY *DriverDTE)
{
    CHAR FullPath[1024];
    CHAR DriverPath[1024];
    CHAR DllName[1024];
    PCHAR DriverNamePos;
    BOOLEAN Success;
    PVOID DriverBase = NULL;

    // Separate the path to file name and directory path
    RtlStringCbPrintfA(DriverPath, sizeof(DriverPath), "%wZ", FilePath);
    DriverNamePos = strrchr(DriverPath, '\\');
    if (DriverNamePos != NULL)
    {
        // Copy the name
        RtlStringCbCopyA(DllName, sizeof(DllName), DriverNamePos+1);

        // Cut out the name from the path
        *(DriverNamePos+1) = ANSI_NULL;
    }
    else
    {
        // There is no directory in the path
        RtlStringCbCopyA(DllName, sizeof(DllName), DriverPath);
        *DriverPath = ANSI_NULL;
    }

    TRACE("DriverPath: '%s', DllName: '%s', LPB\n", DriverPath, DllName);

    // Check if driver is already loaded
    Success = PeLdrCheckForLoadedDll(LoadOrderListHead, DllName, DriverDTE);
    if (Success)
    {
        // We've got the pointer to its DTE, just return success
        return TRUE;
    }

    // It's not loaded, we have to load it
    RtlStringCbPrintfA(FullPath, sizeof(FullPath), "%s%wZ", BootPath, FilePath);

    NtLdrOutputLoadMsg(FullPath, NULL);
    Success = PeLdrLoadImage(FullPath, LoaderBootDriver, &DriverBase);
    if (!Success)
    {
        ERR("PeLdrLoadImage('%s') failed\n", DllName);
        return FALSE;
    }

    // Allocate a DTE for it
    Success = PeLdrAllocateDataTableEntry(LoadOrderListHead,
                                          DllName,
                                          DllName,
                                          PaToVa(DriverBase),
                                          DriverDTE);
    if (!Success)
    {
        /* Cleanup and bail out */
        ERR("PeLdrAllocateDataTableEntry('%s') failed\n", DllName);
        MmFreeMemory(DriverBase);
        return FALSE;
    }

    /* Init security cookie */
    PeLdrInitSecurityCookie(*DriverDTE);

    // Modify any flags, if needed
    (*DriverDTE)->Flags |= Flags;

    // Look for any dependencies it may have, and load them too
    RtlStringCbPrintfA(FullPath, sizeof(FullPath), "%s%s", BootPath, DriverPath);
    Success = PeLdrScanImportDescriptorTable(LoadOrderListHead, FullPath, *DriverDTE);
    if (!Success)
    {
        /* Cleanup and bail out */
        ERR("PeLdrScanImportDescriptorTable('%s') failed\n", FullPath);
        PeLdrFreeDataTableEntry(*DriverDTE);
        MmFreeMemory(DriverBase);
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
WinLdrLoadBootDrivers(PLOADER_PARAMETER_BLOCK LoaderBlock,
                      PCSTR BootPath)
{
    PLIST_ENTRY NextBd;
    PBOOT_DRIVER_NODE DriverNode;
    PBOOT_DRIVER_LIST_ENTRY BootDriver;
    BOOLEAN Success;
    BOOLEAN ret = TRUE;
#if defined(_M_ARM64) || defined(__aarch64__)
    static const PCWSTR WinLdrArm64UnsupportedBootDrivers[] =
    {
        L"usbuhci"
    };
#endif

    /* Walk through the boot drivers list */
    NextBd = LoaderBlock->BootDriverListHead.Flink;
    while (NextBd != &LoaderBlock->BootDriverListHead)
    {
        DriverNode = CONTAINING_RECORD(NextBd,
                                       BOOT_DRIVER_NODE,
                                       ListEntry.Link);
        BootDriver = &DriverNode->ListEntry;

        /* Get the next list entry as we may remove the current one on failure */
        NextBd = BootDriver->Link.Flink;

#if defined(_M_ARM64) || defined(__aarch64__)
        {
            BOOLEAN Skip = FALSE;
            for (ULONG Index = 0; Index < RTL_NUMBER_OF(WinLdrArm64UnsupportedBootDrivers); Index++)
            {
                UNICODE_STRING Name;
                RtlInitUnicodeString(&Name, WinLdrArm64UnsupportedBootDrivers[Index]);
                if (RtlEqualUnicodeString(&DriverNode->Name, &Name, TRUE))
                {
                    TRACE("Skipping unsupported ARM64 boot driver '%wZ'\n", &DriverNode->Name);
                    Skip = TRUE;
                    break;
                }
            }
            if (Skip)
            {
                RemoveEntryList(&BootDriver->Link);
                continue;
            }
        }
#endif

        TRACE("BootDriver %wZ DTE %08X RegPath: %wZ\n",
              &BootDriver->FilePath, BootDriver->LdrEntry,
              &BootDriver->RegistryPath);

        // Paths are relative (FIXME: Are they always relative?)

        /* Load it */
        UiIndicateProgress();
        Success = WinLdrLoadDeviceDriver(&LoaderBlock->LoadOrderListHead,
                                         BootPath,
                                         &BootDriver->FilePath,
                                         0,
                                         &BootDriver->LdrEntry);
        if (Success)
        {
            /* Convert the addresses to VA since we are not going to use them anymore */
            BootDriver->RegistryPath.Buffer = PaToVa(BootDriver->RegistryPath.Buffer);
            BootDriver->FilePath.Buffer = PaToVa(BootDriver->FilePath.Buffer);
            BootDriver->LdrEntry = PaToVa(BootDriver->LdrEntry);

            if (DriverNode->Group.Buffer)
                DriverNode->Group.Buffer = PaToVa(DriverNode->Group.Buffer);
            DriverNode->Name.Buffer = PaToVa(DriverNode->Name.Buffer);
        }
        else
        {
            /* Loading failed: cry loudly */
            ERR("Cannot load boot driver '%wZ'!\n", &BootDriver->FilePath);
            UiMessageBox("Cannot load boot driver '%wZ'!", &BootDriver->FilePath);
            ret = FALSE;

            /* Remove it from the list and try to continue */
            RemoveEntryList(&BootDriver->Link);
        }
    }

    return ret;
}

PVOID
WinLdrLoadModule(PCSTR ModuleName,
                 PULONG Size,
                 TYPE_OF_MEMORY MemoryType)
{
    ULONG FileId;
    PVOID PhysicalBase;
    FILEINFORMATION FileInfo;
    ULONG FileSize;
    ARC_STATUS Status;
    ULONG BytesRead;

    *Size = 0;

    /* Open the image file */
    NtLdrOutputLoadMsg(ModuleName, NULL);
    Status = ArcOpen((PSTR)ModuleName, OpenReadOnly, &FileId);
    if (Status != ESUCCESS)
    {
        /* In case of errors, we just return, without complaining to the user */
        WARN("Error while opening '%s', Status: %u\n", ModuleName, Status);
        return NULL;
    }

    /* Retrieve its size */
    Status = ArcGetFileInformation(FileId, &FileInfo);
    if (Status != ESUCCESS)
    {
        ArcClose(FileId);
        return NULL;
    }
    FileSize = FileInfo.EndingAddress.LowPart;
    *Size = FileSize;

    /* Allocate memory */
    PhysicalBase = MmAllocateMemoryWithType(FileSize, MemoryType);
    if (PhysicalBase == NULL)
    {
        ERR("Could not allocate memory for '%s'\n", ModuleName);
        ArcClose(FileId);
        return NULL;
    }

    /* Load the whole file */
    Status = ArcRead(FileId, PhysicalBase, FileSize, &BytesRead);
    ArcClose(FileId);
    if (Status != ESUCCESS)
    {
        WARN("Error while reading '%s', Status: %u\n", ModuleName, Status);
        return NULL;
    }

    TRACE("Loaded %s at 0x%x with size 0x%x\n", ModuleName, PhysicalBase, FileSize);

    return PhysicalBase;
}

USHORT
WinLdrDetectVersion(VOID)
{
    LONG rc;
    HKEY hKey;

    rc = RegOpenKey(CurrentControlSetKey, L"Control\\Terminal Server", &hKey);
    if (rc != ERROR_SUCCESS)
    {
        /* Key doesn't exist; assume NT 4.0 */
        return _WIN32_WINNT_NT4;
    }
    RegCloseKey(hKey);

    /* We may here want to read the value of ProductVersion */
    return _WIN32_WINNT_WS03;
}

static
PVOID
LoadModule(
    IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PCCH Path,
    IN PCCH File,
    IN PCCH ImportName, // BaseDllName
    IN TYPE_OF_MEMORY MemoryType,
    OUT PLDR_DATA_TABLE_ENTRY *Dte,
    IN ULONG Percentage)
{
    BOOLEAN Success;
    CHAR FullFileName[MAX_PATH];
    CHAR ProgressString[256];
    PVOID BaseAddress;

    RtlStringCbPrintfA(ProgressString, sizeof(ProgressString), "Loading %s...", File);
    UiUpdateProgressBar(Percentage, ProgressString);

    RtlStringCbCopyA(FullFileName, sizeof(FullFileName), Path);
    RtlStringCbCatA(FullFileName, sizeof(FullFileName), File);

    NtLdrOutputLoadMsg(FullFileName, NULL);
    Success = PeLdrLoadImage(FullFileName, MemoryType, &BaseAddress);
    if (!Success)
    {
        ERR("PeLdrLoadImage('%s') failed\n", File);
        return NULL;
    }
    TRACE("%s loaded successfully at %p\n", File, BaseAddress);

    Success = PeLdrAllocateDataTableEntry(&LoaderBlock->LoadOrderListHead,
                                          ImportName,
                                          FullFileName,
                                          PaToVa(BaseAddress),
                                          Dte);
    if (!Success)
    {
        /* Cleanup and bail out */
        ERR("PeLdrAllocateDataTableEntry('%s') failed\n", FullFileName);
        MmFreeMemory(BaseAddress);
        return NULL;
    }

    /* Init security cookie */
    PeLdrInitSecurityCookie(*Dte);

    return BaseAddress;
}

#ifdef _M_IX86
static
BOOLEAN
WinLdrIsPaeSupported(
    _In_ USHORT OperatingSystemVersion,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PCSTR BootOptions,
    _In_ PCSTR HalFileName,
    _Inout_updates_bytes_(KernelFileNameSize) _Always_(_Post_z_)
         PSTR KernelFileName,
    _In_ SIZE_T KernelFileNameSize)
{
    BOOLEAN PaeEnabled = FALSE;
    BOOLEAN PaeDisabled = FALSE;
    BOOLEAN Result;

    if ((OperatingSystemVersion > _WIN32_WINNT_NT4) &&
        NtLdrGetOption(BootOptions, "PAE"))
    {
        /* We found the PAE option */
        PaeEnabled = TRUE;
    }

    Result = PaeEnabled;

    if ((OperatingSystemVersion > _WIN32_WINNT_WIN2K) &&
        NtLdrGetOption(BootOptions, "NOPAE"))
    {
        PaeDisabled = TRUE;
    }

    if (SafeBoot)
        PaeDisabled = TRUE;

    TRACE("PaeEnabled %X, PaeDisabled %X\n", PaeEnabled, PaeDisabled);

    if (PaeDisabled)
        Result = FALSE;

    /* Enable PAE if DEP is enabled */
    if (NoExecuteEnabled)
        Result = TRUE;

    // TODO: checks for CPU support, hotplug memory support ... other tests
    // TODO: select kernel name ("ntkrnlpa.exe" or "ntoskrnl.exe"), or,
    // if KernelFileName is a user-specified kernel file, check whether it
    // has, if PAE needs to be enabled, the IMAGE_FILE_LARGE_ADDRESS_AWARE
    // Characteristics bit set, and that the HAL image has a similar support.

    if (Result) UNIMPLEMENTED;

    return Result;
}
#endif /* _M_IX86 */

static
BOOLEAN
LoadWindowsCore(IN USHORT OperatingSystemVersion,
                IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
                IN PCSTR BootOptions,
                IN PCSTR BootPath,
                IN OUT PLDR_DATA_TABLE_ENTRY* KernelDTE)
{
    BOOLEAN Success;
    PCSTR Option;
    ULONG OptionLength;
    PVOID KernelBase, HalBase, KdDllBase = NULL;
    PLDR_DATA_TABLE_ENTRY HalDTE, KdDllDTE = NULL;
    CHAR DirPath[MAX_PATH];
    CHAR HalFileName[MAX_PATH];
    CHAR KernelFileName[MAX_PATH];
    CHAR KdDllName[MAX_PATH];

    if (!KernelDTE) return FALSE;

    /* Initialize SystemRoot\System32 path */
    RtlStringCbCopyA(DirPath, sizeof(DirPath), BootPath);
    RtlStringCbCatA(DirPath, sizeof(DirPath), "system32\\");

    /* Parse the boot options */
    TRACE("LoadWindowsCore: BootOptions '%s'\n", BootOptions);

#ifdef _M_IX86
    if (NtLdrGetOption(BootOptions, "3GB"))
    {
        /* We found the 3GB option. */
        FIXME("LoadWindowsCore: 3GB - TRUE (not implemented)\n");
        VirtualBias = TRUE;
    }
    // TODO: "USERVA=" for XP/2k3
#endif

    if ((OperatingSystemVersion > _WIN32_WINNT_NT4) &&
        (NtLdrGetOption(BootOptions, "SAFEBOOT") ||
         NtLdrGetOption(BootOptions, "SAFEBOOT:")))
    {
        /* We found the SAFEBOOT option. */
        FIXME("LoadWindowsCore: SAFEBOOT - TRUE (not implemented)\n");
        SafeBoot = TRUE;
    }

    if ((OperatingSystemVersion > _WIN32_WINNT_WIN2K) &&
        NtLdrGetOption(BootOptions, "BOOTLOGO"))
    {
        /* We found the BOOTLOGO option. */
        FIXME("LoadWindowsCore: BOOTLOGO - TRUE (not implemented)\n");
        BootLogo = TRUE;
    }

    /* Check the (NO)EXECUTE options */
    if ((OperatingSystemVersion > _WIN32_WINNT_WIN2K) &&
        !LoaderBlock->SetupLdrBlock)
    {
        /* Disable NX by default on x86, otherwise enable it */
#ifdef _M_IX86
        NoExecuteEnabled = FALSE;
#else
        NoExecuteEnabled = TRUE;
#endif

#ifdef _M_IX86
        /* Check the options in decreasing order of precedence */
        if (NtLdrGetOption(BootOptions, "NOEXECUTE=OPTIN")  ||
            NtLdrGetOption(BootOptions, "NOEXECUTE=OPTOUT") ||
            NtLdrGetOption(BootOptions, "NOEXECUTE=ALWAYSON"))
        {
            NoExecuteEnabled = TRUE;
        }
        else if (NtLdrGetOption(BootOptions, "NOEXECUTE=ALWAYSOFF"))
            NoExecuteEnabled = FALSE;
        else
#else
        /* Only the following two options really apply for x64 and other platforms */
#endif
        if (NtLdrGetOption(BootOptions, "NOEXECUTE"))
            NoExecuteEnabled = TRUE;
        else if (NtLdrGetOption(BootOptions, "EXECUTE"))
            NoExecuteEnabled = FALSE;

#ifdef _M_IX86
        /* Disable DEP in SafeBoot mode for x86 only */
        if (SafeBoot)
            NoExecuteEnabled = FALSE;
#endif
    }
    TRACE("NoExecuteEnabled %X\n", NoExecuteEnabled);

    /*
     * Select the HAL and KERNEL file names.
     * Check for any "/HAL=" or "/KERNEL=" override option.
     *
     * See the following links to know how the file names are actually chosen:
     * https://www.geoffchappell.com/notes/windows/boot/bcd/osloader/detecthal.htm
     * https://www.geoffchappell.com/notes/windows/boot/bcd/osloader/hal.htm
     * https://www.geoffchappell.com/notes/windows/boot/bcd/osloader/kernel.htm
     */
    /* Default HAL and KERNEL file names */
    RtlStringCbCopyA(HalFileName   , sizeof(HalFileName)   , "hal.dll");
    RtlStringCbCopyA(KernelFileName, sizeof(KernelFileName), "ntoskrnl.exe");

    Option = NtLdrGetOptionEx(BootOptions, "HAL=", &OptionLength);
    if (Option && (OptionLength > 4))
    {
        /* Retrieve the HAL file name */
        Option += 4; OptionLength -= 4;
        RtlStringCbCopyNA(HalFileName, sizeof(HalFileName), Option, OptionLength);
        _strlwr(HalFileName);
    }

    Option = NtLdrGetOptionEx(BootOptions, "KERNEL=", &OptionLength);
    if (Option && (OptionLength > 7))
    {
        /* Retrieve the KERNEL file name */
        Option += 7; OptionLength -= 7;
        RtlStringCbCopyNA(KernelFileName, sizeof(KernelFileName), Option, OptionLength);
        _strlwr(KernelFileName);
    }

#ifdef _M_IX86
    /* Check for PAE support and select the adequate kernel image */
    PaeModeOn = WinLdrIsPaeSupported(OperatingSystemVersion,
                                     LoaderBlock,
                                     BootOptions,
                                     HalFileName,
                                     KernelFileName,
                                     sizeof(KernelFileName));
    if (PaeModeOn) FIXME("WinLdrIsPaeSupported: PaeModeOn\n");
#endif

    TRACE("HAL file = '%s' ; Kernel file = '%s'\n", HalFileName, KernelFileName);

    /*
     * Load the core NT files: Kernel, HAL and KD transport DLL.
     * Cheat about their base DLL name so as to satisfy the imports/exports,
     * even if the corresponding underlying files do not have the same names
     * -- this happens e.g. with UP vs. MP kernel, standard vs. ACPI hal, or
     * different KD transport DLLs.
     */

    /* Load the Kernel */
    KernelBase = LoadModule(LoaderBlock, DirPath, KernelFileName,
                            "ntoskrnl.exe", LoaderSystemCode, KernelDTE, 30);
    if (!KernelBase)
    {
        ERR("LoadModule('%s') failed\n", KernelFileName);
        UiMessageBox("Could not load %s", KernelFileName);
        return FALSE;
    }

    /* Load the HAL */
    HalBase = LoadModule(LoaderBlock, DirPath, HalFileName,
                         "hal.dll", LoaderHalCode, &HalDTE, 35);
    if (!HalBase)
    {
        ERR("LoadModule('%s') failed\n", HalFileName);
        UiMessageBox("Could not load %s", HalFileName);
        PeLdrFreeDataTableEntry(*KernelDTE);
        MmFreeMemory(KernelBase);
        return FALSE;
    }

    /* Load the Kernel Debugger Transport DLL */
    if (OperatingSystemVersion > _WIN32_WINNT_WIN2K)
    {
        /*
         * According to http://www.nynaeve.net/?p=173 :
         * "[...] Another enhancement that could be done Microsoft-side would be
         * a better interface for replacing KD transport modules. Right now, due
         * to the fact that ntoskrnl is static linked to KDCOM.DLL, the OS loader
         * has a hardcoded hack that interprets the KD type in the OS loader options,
         * loads one of the (hardcoded filenames) "kdcom.dll", "kd1394.dll", or
         * "kdusb2.dll" modules, and inserts them into the loaded module list under
         * the name "kdcom.dll". [...]"
         */

        /*
         * A Kernel Debugger Transport DLL is always loaded for Windows XP+ :
         * either the standard KDCOM.DLL (by default): IsCustomKdDll == FALSE
         * or an alternative user-provided one via the /DEBUGPORT= option:
         * IsCustomKdDll == TRUE if it does not specify the default KDCOM.
         */
        BOOLEAN IsCustomKdDll = FALSE;

        /* Check whether there is a DEBUGPORT option */
        Option = NtLdrGetOptionEx(BootOptions, "DEBUGPORT=", &OptionLength);
        if (Option && (OptionLength > 10))
        {
            /* Move to the debug port name */
            Option += 10; OptionLength -= 10;

            /*
             * Parse the port name.
             * Format: /DEBUGPORT=COM[0-9]
             * or: /DEBUGPORT=FILE:\Device\HarddiskX\PartitionY\debug.log
             * or: /DEBUGPORT=FOO
             * If we only have /DEBUGPORT= (i.e. without any port name),
             * default to "COM".
             */

            /* Get the actual length of the debug port
             * until the next whitespace or colon. */
            OptionLength = (ULONG)strcspn(Option, " \t:");

            if ((OptionLength == 0) ||
                ( (OptionLength >= 3) && (_strnicmp(Option, "COM", 3) == 0) &&
                 ((OptionLength == 3) || ('0' <= Option[3] && Option[3] <= '9')) ))
            {
                /* The standard KDCOM.DLL is used */
            }
            else
            {
                /* A custom KD DLL is used */
                IsCustomKdDll = TRUE;
            }
        }
        if (!IsCustomKdDll)
        {
            Option = "COM"; OptionLength = 3;
        }

        RtlStringCbPrintfA(KdDllName, sizeof(KdDllName), "kd%.*s.dll",
                           OptionLength, Option);
        _strlwr(KdDllName);

        /* Load the KD DLL. Override its base DLL name to the default "KDCOM.DLL". */
        KdDllBase = LoadModule(LoaderBlock, DirPath, KdDllName,
                               "kdcom.dll", LoaderSystemCode, &KdDllDTE, 40);
        if (!KdDllBase)
        {
            /* If we failed to load a custom KD DLL, fall back to the standard one */
            if (IsCustomKdDll)
            {
                /* The custom KD DLL being optional, just ignore the failure */
                WARN("LoadModule('%s') failed\n", KdDllName);

                IsCustomKdDll = FALSE;
                RtlStringCbCopyA(KdDllName, sizeof(KdDllName), "kdcom.dll");

                KdDllBase = LoadModule(LoaderBlock, DirPath, KdDllName,
                                       "kdcom.dll", LoaderSystemCode, &KdDllDTE, 40);
            }

            if (!KdDllBase)
            {
                /* Ignore the failure; we will fail later when scanning the
                 * kernel import tables, if it really needs the KD DLL. */
                ERR("LoadModule('%s') failed\n", KdDllName);
            }
        }
    }

    /* Load all referenced DLLs for Kernel, HAL and Kernel Debugger Transport DLL */
    Success = PeLdrScanImportDescriptorTable(&LoaderBlock->LoadOrderListHead, DirPath, *KernelDTE);
    if (!Success)
    {
        UiMessageBox("Could not load %s", KernelFileName);
        goto Quit;
    }
    Success = PeLdrScanImportDescriptorTable(&LoaderBlock->LoadOrderListHead, DirPath, HalDTE);
    if (!Success)
    {
        UiMessageBox("Could not load %s", HalFileName);
        goto Quit;
    }
    if (KdDllDTE)
    {
        Success = PeLdrScanImportDescriptorTable(&LoaderBlock->LoadOrderListHead, DirPath, KdDllDTE);
        if (!Success)
        {
            UiMessageBox("Could not load %s", KdDllName);
            goto Quit;
        }
    }

Quit:
    if (!Success)
    {
        /* Cleanup and bail out */
        if (KdDllDTE)
            PeLdrFreeDataTableEntry(KdDllDTE);
        if (KdDllBase) // Optional
            MmFreeMemory(KdDllBase);

        PeLdrFreeDataTableEntry(HalDTE);
        MmFreeMemory(HalBase);

        PeLdrFreeDataTableEntry(*KernelDTE);
        MmFreeMemory(KernelBase);
    }

    return Success;
}

static
BOOLEAN
WinLdrInitErrataInf(
    IN OUT PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN USHORT OperatingSystemVersion,
    IN PCSTR SystemRoot)
{
    LONG rc;
    HKEY hKey;
    ULONG BufferSize;
    ULONG FileSize;
    PVOID PhysicalBase;
    WCHAR szFileName[80];
    CHAR ErrataFilePath[MAX_PATH];

    /* Open either the 'BiosInfo' (Windows <= 2003) or the 'Errata' (Vista+) key */
    if (OperatingSystemVersion >= _WIN32_WINNT_VISTA)
    {
        rc = RegOpenKey(CurrentControlSetKey, L"Control\\Errata", &hKey);
    }
    else // (OperatingSystemVersion <= _WIN32_WINNT_WS03)
    {
        rc = RegOpenKey(CurrentControlSetKey, L"Control\\BiosInfo", &hKey);
    }
    if (rc != ERROR_SUCCESS)
    {
        WARN("Could not open the BiosInfo/Errata registry key (Error %u)\n", (int)rc);
        return FALSE;
    }

    /* Retrieve the INF file name value */
    BufferSize = sizeof(szFileName);
    rc = RegQueryValue(hKey, L"InfName", NULL, (PUCHAR)szFileName, &BufferSize);
    if (rc != ERROR_SUCCESS)
    {
        WARN("Could not retrieve the InfName value (Error %u)\n", (int)rc);
        RegCloseKey(hKey);
        return FALSE;
    }

    // TODO: "SystemBiosDate"

    RegCloseKey(hKey);

    RtlStringCbPrintfA(ErrataFilePath, sizeof(ErrataFilePath), "%s%s%S",
                       SystemRoot, "inf\\", szFileName);

    /* Load the INF file */
    PhysicalBase = WinLdrLoadModule(ErrataFilePath, &FileSize, LoaderRegistryData);
    if (!PhysicalBase)
    {
        WARN("Could not load '%s'\n", ErrataFilePath);
        return FALSE;
    }

    LoaderBlock->Extension->EmInfFileImage = PaToVa(PhysicalBase);
    LoaderBlock->Extension->EmInfFileSize  = FileSize;

    return TRUE;
}

ARC_STATUS
LoadAndBootWindows(
    IN ULONG Argc,
    IN PCHAR Argv[],
    IN PCHAR Envp[])
{
    ARC_STATUS Status;
    PCSTR ArgValue;
    PCSTR SystemPartition;
    PCSTR FileName;
    ULONG FileNameLength;
    BOOLEAN Success;
    USHORT OperatingSystemVersion;
    PLOADER_PARAMETER_BLOCK LoaderBlock;
    CHAR BootPath[MAX_PATH];
    CHAR FilePath[MAX_PATH];
    CHAR BootOptions[256];

    /* Retrieve the (mandatory) boot type */
    ArgValue = GetArgumentValue(Argc, Argv, "BootType");
    if (!ArgValue || !*ArgValue)
    {
        ERR("No 'BootType' value, aborting!\n");
        return EINVAL;
    }

    /* Convert it to an OS version */
    if (_stricmp(ArgValue, "Windows") == 0 ||
        _stricmp(ArgValue, "Windows2003") == 0)
    {
        OperatingSystemVersion = _WIN32_WINNT_WS03;
    }
    else if (_stricmp(ArgValue, "WindowsNT40") == 0)
    {
        OperatingSystemVersion = _WIN32_WINNT_NT4;
    }
    else if (_stricmp(ArgValue, "WindowsVista") == 0)
    {
        OperatingSystemVersion = _WIN32_WINNT_VISTA;
    }
    else
    {
        ERR("Unknown 'BootType' value '%s', aborting!\n", ArgValue);
        return EINVAL;
    }

    /* Retrieve the (mandatory) system partition */
    SystemPartition = GetArgumentValue(Argc, Argv, "SystemPartition");
    if (!SystemPartition || !*SystemPartition)
    {
        ERR("No 'SystemPartition' specified, aborting!\n");
        return EINVAL;
    }

    /* Let the user know we started loading */
    UiDrawBackdrop(UiGetScreenHeight());
    UiDrawStatusText("Loading...");
    UiDrawProgressBarCenter("Loading NT...");
#ifdef UEFIBOOT
    /* Ensure firmware BGRT logo is displayed also in non-RAMDISK LiveCD paths. */
    extern BOOLEAN UefiVideoDisplayBootLogo(VOID);
    (void)UefiVideoDisplayBootLogo();
#endif

    /* Retrieve the system path */
    *BootPath = ANSI_NULL;
    ArgValue = GetArgumentValue(Argc, Argv, "SystemPath");
    if (ArgValue)
        RtlStringCbCopyA(BootPath, sizeof(BootPath), ArgValue);

    /*
     * Check whether BootPath is a full path
     * and if not, create a full boot path.
     *
     * See FsOpenFile for the technique used.
     */
    if (strrchr(BootPath, ')') == NULL)
    {
        /* Temporarily save the boot path */
        RtlStringCbCopyA(FilePath, sizeof(FilePath), BootPath);

        /* This is not a full path: prepend the SystemPartition */
        RtlStringCbCopyA(BootPath, sizeof(BootPath), SystemPartition);

        /* Append a path separator if needed */
        if (*FilePath != '\\' && *FilePath != '/')
            RtlStringCbCatA(BootPath, sizeof(BootPath), "\\");

        /* Append the remaining path */
        RtlStringCbCatA(BootPath, sizeof(BootPath), FilePath);
    }

    /* Append a path separator if needed */
    if (!*BootPath || BootPath[strlen(BootPath) - 1] != '\\')
        RtlStringCbCatA(BootPath, sizeof(BootPath), "\\");

    TRACE("BootPath: '%s'\n", BootPath);

    /* Retrieve the boot options */
    *BootOptions = ANSI_NULL;
    ArgValue = GetArgumentValue(Argc, Argv, "Options");
    if (ArgValue && *ArgValue)
        RtlStringCbCopyA(BootOptions, sizeof(BootOptions), ArgValue);

    /* Append boot-time options */
    AppendBootTimeOptions(BootOptions);

    /*
     * Set the "/HAL=" and "/KERNEL=" options if needed.
     * If already present on the standard "Options=" option line, they take
     * precedence over those passed via the separate "Hal=" and "Kernel="
     * options.
     */
    if (!NtLdrGetOption(BootOptions, "HAL="))
    {
        /*
         * Not found in the options, try to retrieve the
         * separate value and append it to the options.
         */
        ArgValue = GetArgumentValue(Argc, Argv, "Hal");
        if (ArgValue && *ArgValue)
        {
            RtlStringCbCatA(BootOptions, sizeof(BootOptions), " /HAL=");
            RtlStringCbCatA(BootOptions, sizeof(BootOptions), ArgValue);
        }
    }
    if (!NtLdrGetOption(BootOptions, "KERNEL="))
    {
        /*
         * Not found in the options, try to retrieve the
         * separate value and append it to the options.
         */
        ArgValue = GetArgumentValue(Argc, Argv, "Kernel");
        if (ArgValue && *ArgValue)
        {
            RtlStringCbCatA(BootOptions, sizeof(BootOptions), " /KERNEL=");
            RtlStringCbCatA(BootOptions, sizeof(BootOptions), ArgValue);
        }
    }

    TRACE("BootOptions: '%s'\n", BootOptions);

    BOOLEAN ForceRamDisk = FALSE;
    PCSTR RamDiskSizeOpt;
    ULONG RamDiskSizeOptLen = 0;

    RamDiskSizeOpt = NtLdrGetOptionEx(BootOptions, "RDRAMSIZE=", &RamDiskSizeOptLen);
    if (RamDiskSizeOpt &&
        RamDiskSizeOptLen >= (sizeof("RDRAMSIZE=") - 1) &&
        _strnicmp(RamDiskSizeOpt, "RDRAMSIZE=", sizeof("RDRAMSIZE=") - 1) == 0)
    {
        ForceRamDisk = TRUE;
    }

    /* Check if a RAM disk file was given */
    FileName = NtLdrGetOptionEx(BootOptions, "RDPATH=", &FileNameLength);
    if (FileName &&
        FileNameLength >= (sizeof("RDPATH=") - 1) &&
        _strnicmp(FileName, "RDPATH=", sizeof("RDPATH=") - 1) == 0)
    {
        ForceRamDisk = TRUE;
    }

    if (ForceRamDisk)
    {
        /* Load the RAM disk, either from explicit RDPATH or the system partition */
        Status = RamDiskInitialize(FALSE, BootOptions, SystemPartition);
        if (Status != ESUCCESS)
        {
            if (!FileName && Status == ENOMEM)
            {
                /* Fallback to read-only media when RAM disk allocation fails. */
                static const PCSTR OptionsToRemove[] =
                {
                    "/RDRAMSIZE=",
                    "/RDPATH=",
                    "/RDIMAGEOFFSET=",
                    "/RDIMAGELENGTH=",
                    NULL
                };
                PCSTR SubPath;
                CHAR SubPathCopy[sizeof(BootPath)];

                WARN("RAM disk allocation failed; continuing with read-only media.\n");
                UiDrawStatusText("RAM disk allocation failed; using read-only media.");

                NtLdrUpdateLoadOptions(BootOptions,
                                       sizeof(BootOptions),
                                       TRUE,
                                       NULL,
                                       OptionsToRemove);

                SubPath = strchr(BootPath, '\\');
                if (!SubPath)
                    SubPath = "\\";
                RtlStringCbCopyA(SubPathCopy, sizeof(SubPathCopy), SubPath);

                RtlStringCbCopyA(BootPath, sizeof(BootPath), SystemPartition);
                if (SubPathCopy[0] != '\\' && SubPathCopy[0] != '/')
                    RtlStringCbCatA(BootPath, sizeof(BootPath), "\\");
                RtlStringCbCatA(BootPath, sizeof(BootPath), SubPathCopy);
                TRACE("BootPath fallback to '%s'\n", BootPath);
            }
            else
            {
                if (FileName && (FileNameLength >= 7))
                {
                    FileName += 7;
                    FileNameLength -= 7;
                    UiMessageBox("Failed to load RAM disk file '%.*s'",
                                 FileNameLength, FileName);
                }
                else
                {
                    UiMessageBox("Failed to expand LiveCD into RAM.");
                }
                return Status;
            }
        }
        else if (RamDiskGetRequestedSize() != 0)
        {
            CHAR OffsetOption[32];
            CHAR LengthOption[64];
            PCSTR OptionsToAdd[3];
            PCSTR OptionsToRemove[3];
            CHAR NewBootPath[sizeof(BootPath)];
            PCSTR SubPath;

            {
                /* NT5-style handoff: expose the real filesystem volume start/length */
                ULONGLONG ImageOffset64 = (ULONGLONG)RamDiskGetImageOffset();
                ULONGLONG VolumeOffset64 = RamDiskGetVolumeOffset();
                ULONGLONG ImageLength64 = RamDiskGetImageLength();
                ULONGLONG EffectiveOffset64 = ImageOffset64 + VolumeOffset64;

                if (EffectiveOffset64 > (ULONGLONG)MAXLONG)
                {
                    WARN("Computed /RDIMAGEOFFSET=%llu exceeds 32-bit loader limit\n",
                         EffectiveOffset64);
                    UiMessageBox("Computed RAM disk offset exceeds 2GB limit; boot aborted.");
                    return EINVAL;
                }

                ULONGLONG VisibleLength64 = (ImageLength64 > EffectiveOffset64)
                                             ? (ImageLength64 - EffectiveOffset64)
                                             : 0;
                ULONG Offset32 = (ULONG)EffectiveOffset64;

                RtlStringCbPrintfA(OffsetOption,
                                   sizeof(OffsetOption),
                                   "/RDIMAGEOFFSET=%lu",
                                   Offset32);
                RtlStringCbPrintfA(LengthOption,
                                   sizeof(LengthOption),
                                   "/RDIMAGELENGTH=%llu",
                                   VisibleLength64);
            }

            OptionsToAdd[0] = OffsetOption;
            OptionsToAdd[1] = LengthOption;
            OptionsToAdd[2] = NULL;

            OptionsToRemove[0] = "/RDIMAGEOFFSET=";
            OptionsToRemove[1] = "/RDIMAGELENGTH=";
            OptionsToRemove[2] = NULL;

            NtLdrUpdateLoadOptions(BootOptions,
                                   sizeof(BootOptions),
                                   TRUE,
                                   OptionsToAdd,
                                   OptionsToRemove);

            SubPath = strchr(BootPath, '\\');
            if (!SubPath)
                SubPath = "\\";

            RtlStringCbCopyA(NewBootPath, sizeof(NewBootPath), "ramdisk(0)");
            RtlStringCbCatA(NewBootPath, sizeof(NewBootPath), SubPath);
            RtlStringCbCopyA(BootPath, sizeof(BootPath), NewBootPath);
            TRACE("BootPath re-targeted to '%s'\n", BootPath);
        }
    }

    /* Handle the SOS option */
    SosEnabled = !!NtLdrGetOption(BootOptions, "SOS");
    if (SosEnabled)
        UiResetForSOS();

    /* Allocate and minimally-initialize the Loader Parameter Block */
    AllocateAndInitLPB(OperatingSystemVersion, &LoaderBlock);

    /* Load the system hive */
    UiUpdateProgressBar(15, "Loading system hive...");
    Success = WinLdrInitSystemHive(LoaderBlock, BootPath, FALSE);
    TRACE("SYSTEM hive %s\n", (Success ? "loaded" : "not loaded"));
    /* Bail out if failure */
    if (!Success)
        return ENOEXEC;

    /* Fixup the version number using data from the registry */
    if (OperatingSystemVersion == 0)
        OperatingSystemVersion = WinLdrDetectVersion();
    LoaderBlock->Extension->MajorVersion = (OperatingSystemVersion & 0xFF00) >> 8;
    LoaderBlock->Extension->MinorVersion = (OperatingSystemVersion & 0xFF);

    /* Load NLS data, OEM font, and prepare boot drivers list */
    Success = WinLdrScanSystemHive(LoaderBlock, BootPath);
    TRACE("SYSTEM hive %s\n", (Success ? "scanned" : "not scanned"));
    /* Bail out if failure */
    if (!Success)
        return ENOEXEC;

    /* Load the Firmware Errata file */
    Success = WinLdrInitErrataInf(LoaderBlock, OperatingSystemVersion, BootPath);
    TRACE("Firmware Errata file %s\n", (Success ? "loaded" : "not loaded"));
    /* Not necessarily fatal if not found - carry on going */

    /* Finish loading */
    return LoadAndBootWindowsCommon(OperatingSystemVersion,
                                    LoaderBlock,
                                    BootOptions,
                                    BootPath);
}

ARC_STATUS
LoadAndBootWindowsCommon(
    IN USHORT OperatingSystemVersion,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PCSTR BootOptions,
    IN PCSTR BootPath)
{
    PLOADER_PARAMETER_BLOCK LoaderBlockVA;
    BOOLEAN Success;
    PLDR_DATA_TABLE_ENTRY KernelDTE;
    KERNEL_ENTRY_POINT KiSystemStartup;
    PCSTR SystemRoot;

    TRACE("LoadAndBootWindowsCommon()\n");

    ASSERT(OperatingSystemVersion != 0);

#ifdef _M_IX86
    /* Setup redirection support */
    WinLdrSetupEms(BootOptions);
#endif

    /* Convert BootPath to SystemRoot */
    SystemRoot = strstr(BootPath, "\\");

    /* Detect hardware */
    UiUpdateProgressBar(20, "Detecting hardware...");
    LoaderBlock->ConfigurationRoot = MachHwDetect(BootOptions);

    /* Initialize the PE loader import-DLL callback, so that we can obtain
     * feedback (for example during SOS) on the PE images that get loaded. */
    PeLdrImportDllLoadCallback = NtLdrImportDllLoadCallback;

    /* Load the operating system core: the Kernel, the HAL and the Kernel Debugger Transport DLL */
    Success = LoadWindowsCore(OperatingSystemVersion,
                              LoaderBlock,
                              BootOptions,
                              BootPath,
                              &KernelDTE);
    if (!Success)
    {
        /* Reset the PE loader import-DLL callback */
        PeLdrImportDllLoadCallback = NULL;

        UiMessageBox("Error loading NTOS core.");
        return ENOEXEC;
    }

    /* Cleanup INI file */
    IniCleanup();

/****
 **** WE HAVE NOW REACHED THE POINT OF NO RETURN !!
 ****/

    UiSetProgressBarSubset(40, 90); // NTOS goes from 25 to 75%

    /* Load boot drivers */
    UiSetProgressBarText("Loading boot drivers...");
    Success = WinLdrLoadBootDrivers(LoaderBlock, BootPath);
    TRACE("Boot drivers loading %s\n", Success ? "successful" : "failed");

    UiSetProgressBarSubset(0, 100);

    /* Reset the PE loader import-DLL callback */
    PeLdrImportDllLoadCallback = NULL;

    /* Initialize Phase 1 - no drivers loading anymore */
    WinLdrInitializePhase1(LoaderBlock,
                           BootOptions,
                           SystemRoot,
                           BootPath,
                           OperatingSystemVersion);

    UiUpdateProgressBar(100, NULL);
#ifdef UEFIBOOT
    UiClearProgressBar();
    UefiVideoRefreshBootLogo();
#endif

    /* Save entry-point pointer and Loader block VAs */
    KiSystemStartup = (KERNEL_ENTRY_POINT)KernelDTE->EntryPoint;
    LoaderBlockVA = PaToVa(LoaderBlock);

    /* "Stop all motors", change videomode */
    MachPrepareForReactOS();

    /* Show the "debug mode" notice if needed */
    /* Match KdInitSystem() conditions */
    if (!NtLdrGetOption(BootOptions, "CRASHDEBUG") &&
        !NtLdrGetOption(BootOptions, "NODEBUG") &&
        !!NtLdrGetOption(BootOptions, "DEBUG"))
    {
        /* Check whether there is a DEBUGPORT option */
        PCSTR DebugPort;
        ULONG DebugPortLength = 0;
        DebugPort = NtLdrGetOptionEx(BootOptions, "DEBUGPORT=", &DebugPortLength);
        if (DebugPort != NULL && DebugPortLength > 10)
        {
            /* Move to the debug port name */
            DebugPort += 10; DebugPortLength -= 10;
        }
        else
        {
            /* Default to COM */
            DebugPort = "COM"; DebugPortLength = 3;
        }

        /* It is booting in debug mode, show the banner */
        TuiPrintf("You need to connect a debugger on port %.*s\n"
                  "For more information, visit https://reactos.org/wiki/Debugging.\n",
                  DebugPortLength, DebugPort);
    }

    /* Debugging... */
    //DumpMemoryAllocMap();

    /* Do the machine specific initialization */
    WinLdrSetupMachineDependent(LoaderBlock);

    /* Map pages and create memory descriptors */
    WinLdrSetupMemoryLayout(LoaderBlock);

    /* Set processor context */
    WinLdrSetProcessorContext(OperatingSystemVersion);

    /* Save final value of LoaderPagesSpanned */
    {
        PFN_NUMBER LoaderPages = MmGetLoaderPagesSpanned();
#if defined(_M_IX86)
        if (LoaderPages > MI_LOADER_MAX_KERNEL_MAPPING_PAGES)
        {
            TRACE("WinLdr: Clamping LoaderPagesSpanned from 0x%Ix to 0x%Ix\n",
                  LoaderPages,
                  (PFN_NUMBER)MI_LOADER_MAX_KERNEL_MAPPING_PAGES);
            LoaderPages = MI_LOADER_MAX_KERNEL_MAPPING_PAGES;
        }
#endif
        LoaderBlock->Extension->LoaderPagesSpanned = LoaderPages;
    }

    TRACE("Hello from paged mode, KiSystemStartup %p, LoaderBlockVA %p!\n",
          KiSystemStartup, LoaderBlockVA);

#if defined(_M_ARM64)
    EarlyUartPuts("\r\n[WinLdr] Before zeroing SharedUserData\r\n");
#endif

    /* Zero KI_USER_SHARED_DATA page */
    {
        volatile PVOID SharedUserDataVa = (PVOID)KI_USER_SHARED_DATA;
        RtlZeroMemory((PVOID)SharedUserDataVa, MM_PAGE_SIZE);
    }

#if defined(_M_ARM64)
    EarlyUartPuts("[WinLdr] SharedUserData zeroed\r\n");
#endif

    WinLdrpDumpMemoryDescriptors(LoaderBlockVA);
    WinLdrpDumpBootDriver(LoaderBlockVA);
#ifndef _M_AMD64
    WinLdrpDumpArcDisks(LoaderBlockVA);
#endif

#if defined(_M_ARM64)
    EarlyUartPuts("[WinLdr] Dumps done\r\n");
#endif

    /*
     * Finalize loader performance data: record how long the bootloader ran
     * so the kernel debugger can maintain a continuous timestamp.
     */
    WinLdrSystemBlock->LoaderPerformanceData.EndTime = DbgQueryMicrosecondsSinceBoot();

#if defined(_M_ARM64)
    EarlyUartPuts("[WinLdr] KiSystemStartup = 0x");
    EarlyUartPutHex((UINT64)(ULONG_PTR)KiSystemStartup, 16);
    EarlyUartPuts("\r\n[WinLdr] LoaderBlockVA = 0x");
    EarlyUartPutHex((UINT64)(ULONG_PTR)LoaderBlockVA, 16);
    EarlyUartPuts("\r\n[WinLdr] JUMPING TO KERNEL NOW!\r\n");
#endif

    /* Pass control */
    (*KiSystemStartup)(LoaderBlockVA);

    UNREACHABLE; // return ESUCCESS;
}

VOID
WinLdrpDumpMemoryDescriptors(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY NextMd;
    PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;

    NextMd = LoaderBlock->MemoryDescriptorListHead.Flink;

    while (NextMd != &LoaderBlock->MemoryDescriptorListHead)
    {
        MemoryDescriptor = CONTAINING_RECORD(NextMd, MEMORY_ALLOCATION_DESCRIPTOR, ListEntry);

        TRACE("BP %08X PC %04X MT %d\n", MemoryDescriptor->BasePage,
            MemoryDescriptor->PageCount, MemoryDescriptor->MemoryType);

        /* Highlight ramdisk-related memory types for debugging */
        if (MemoryDescriptor->MemoryType == LoaderXIPRom)
        {
            ERR(">>> RAMDISK DESCRIPTOR: BasePage=0x%lX PageCount=0x%lX PhysAddr=0x%I64X Size=%I64u bytes\n",
                MemoryDescriptor->BasePage,
                MemoryDescriptor->PageCount,
                (ULONGLONG)MemoryDescriptor->BasePage << PAGE_SHIFT,
                (ULONGLONG)MemoryDescriptor->PageCount << PAGE_SHIFT);
        }

        NextMd = MemoryDescriptor->ListEntry.Flink;
    }
}

VOID
WinLdrpDumpBootDriver(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY NextBd;
    PBOOT_DRIVER_LIST_ENTRY BootDriver;

    NextBd = LoaderBlock->BootDriverListHead.Flink;

    while (NextBd != &LoaderBlock->BootDriverListHead)
    {
        BootDriver = CONTAINING_RECORD(NextBd, BOOT_DRIVER_LIST_ENTRY, Link);

        TRACE("BootDriver %wZ DTE %08X RegPath: %wZ\n", &BootDriver->FilePath,
            BootDriver->LdrEntry, &BootDriver->RegistryPath);

        NextBd = BootDriver->Link.Flink;
    }
}

VOID
WinLdrpDumpArcDisks(PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PLIST_ENTRY NextBd;
    PARC_DISK_SIGNATURE ArcDisk;

    NextBd = LoaderBlock->ArcDiskInformation->DiskSignatureListHead.Flink;

    while (NextBd != &LoaderBlock->ArcDiskInformation->DiskSignatureListHead)
    {
        ArcDisk = CONTAINING_RECORD(NextBd, ARC_DISK_SIGNATURE, ListEntry);

        TRACE("ArcDisk %s checksum: 0x%X, signature: 0x%X\n",
            ArcDisk->ArcName, ArcDisk->CheckSum, ArcDisk->Signature);

        NextBd = ArcDisk->ListEntry.Flink;
    }
}
