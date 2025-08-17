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
#include <internal/cmboot.h>

#ifdef UEFIBOOT
#include <uefildr.h>
#endif

/* Include architecture-specific definitions */
#if defined(_M_ARM64)
#ifndef KI_USER_SHARED_DATA
#define KI_USER_SHARED_DATA     0xFFFF800000000000ULL
#endif
#endif

#include <debug.h>
DBG_DEFAULT_CHANNEL(WINDOWS);

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

    /* Initialize the Loader Block Extension */
    Extension = &WinLdrSystemBlock->Extension;
    LoaderBlock->Extension = Extension;
    Extension->Size = sizeof(LOADER_PARAMETER_EXTENSION);
    Extension->MajorVersion = (VersionToBoot & 0xFF00) >> 8;
    Extension->MinorVersion = (VersionToBoot & 0xFF);
    
#ifdef UEFIBOOT
    /* Set UEFI boot flag */
    Extension->BootViaEFI = TRUE;
    TRACE("WinLdrInitializePhase1: UEFI boot flag set\n");
    
    /* We'll pass framebuffer info later when video is initialized */
#endif

    /* Init three critical lists, used right away */
    InitializeListHead(&LoaderBlock->LoadOrderListHead);
    InitializeListHead(&LoaderBlock->MemoryDescriptorListHead);
    InitializeListHead(&LoaderBlock->BootDriverListHead);

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
    ULONG_PTR PathSeparator;
    PLOADER_PARAMETER_EXTENSION Extension;

    /* Construct SystemRoot and ArcBoot from SystemPath */
    PathSeparator = strstr(BootPath, "\\") - BootPath;
    RtlStringCbCopyNA(ArcBoot, sizeof(ArcBoot), BootPath, PathSeparator);

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

    if (VersionToBoot >= _WIN32_WINNT_VISTA)
    {
        Extension->BootViaWinload = 1;
        Extension->LoaderPerformanceData = PaToVa(&WinLdrSystemBlock->LoaderPerformanceData);

        InitializeListHead(&Extension->BootApplicationPersistentData);
        List_PaToVa(&Extension->BootApplicationPersistentData);
    }

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

    ERR("LoadModule: Entry - File=%s\n", File);
    
    RtlStringCbPrintfA(ProgressString, sizeof(ProgressString), "Loading %s...", File);
#ifdef UEFIBOOT
    ERR("LoadModule: Skipping UiUpdateProgressBar in UEFI\n");
#else
    UiUpdateProgressBar(Percentage, ProgressString);
#endif

    RtlStringCbCopyA(FullFileName, sizeof(FullFileName), Path);
    RtlStringCbCatA(FullFileName, sizeof(FullFileName), File);
    ERR("LoadModule: FullFileName=%s\n", FullFileName);

    NtLdrOutputLoadMsg(FullFileName, NULL);
    ERR("LoadModule: Calling PeLdrLoadImage\n");
    Success = PeLdrLoadImage(FullFileName, MemoryType, &BaseAddress);
    ERR("LoadModule: PeLdrLoadImage returned %d, BaseAddress=%p\n", Success, BaseAddress);
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
    ERR("LoadWindowsCore: Entry\n");

#ifdef UEFIBOOT
    /* Skip options parsing in UEFI mode - they cause crashes */
    ERR("LoadWindowsCore: UEFI - Skipping boot options parsing\n");
#else
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
#endif

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
    ERR("LoadWindowsCore: Loading kernel: %s%s\n", DirPath, KernelFileName);
    KernelBase = LoadModule(LoaderBlock, DirPath, KernelFileName,
                            "ntoskrnl.exe", LoaderSystemCode, KernelDTE, 30);
    ERR("LoadWindowsCore: KernelBase=%p\n", KernelBase);
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

    
    ERR("LoadAndBootWindows: Entry point reached\n");
    
#ifdef UEFIBOOT
    ERR("LoadAndBootWindows: UEFI build detected\n");
#endif

    ERR("LoadAndBootWindows: About to get BootType\n");
    
    /* Retrieve the (mandatory) boot type */
    ArgValue = GetArgumentValue(Argc, Argv, "BootType");
    
    ERR("LoadAndBootWindows: BootType = %s\n", ArgValue ? ArgValue : "(null)");
    if (!ArgValue || !*ArgValue)
    {
        ERR("No 'BootType' value, aborting!\n");
        return EINVAL;
    }

    /* Convert it to an OS version */
    ERR("LoadAndBootWindows: Converting BootType to OS version\n");
    ERR("LoadAndBootWindows: About to call _stricmp with ArgValue=%s\n", ArgValue);
    if (_stricmp(ArgValue, "Windows") == 0 ||
        _stricmp(ArgValue, "Windows2003") == 0)
    {
        OperatingSystemVersion = _WIN32_WINNT_WS03;
        ERR("LoadAndBootWindows: Set OS version to WS03\n");
    }
    else if (_stricmp(ArgValue, "WindowsNT40") == 0)
    {
        OperatingSystemVersion = _WIN32_WINNT_NT4;
        ERR("LoadAndBootWindows: Set OS version to NT4\n");
    }
    else if (_stricmp(ArgValue, "WindowsVista") == 0)
    {
        OperatingSystemVersion = _WIN32_WINNT_VISTA;
        ERR("LoadAndBootWindows: Set OS version to Vista\n");
    }
    else
    {
        ERR("Unknown 'BootType' value '%s', aborting!\n", ArgValue);
        return EINVAL;
    }

    ERR("LoadAndBootWindows: About to get SystemPartition\n");
    /* Retrieve the (mandatory) system partition */
    SystemPartition = GetArgumentValue(Argc, Argv, "SystemPartition");
    ERR("LoadAndBootWindows: SystemPartition = %s\n", SystemPartition ? SystemPartition : "(null)");
    if (!SystemPartition || !*SystemPartition)
    {
        ERR("No 'SystemPartition' specified, aborting!\n");
        return EINVAL;
    }

    /* Let the user know we started loading */
    ERR("LoadAndBootWindows: About to draw UI backdrop\n");
    UiDrawBackdrop(UiGetScreenHeight());
    ERR("LoadAndBootWindows: UI backdrop drawn\n");
    UiDrawStatusText("Loading...");
    ERR("LoadAndBootWindows: Status text drawn\n");
    UiDrawProgressBarCenter("Loading NT...");
    ERR("LoadAndBootWindows: Progress bar drawn\n");

    /* Retrieve the system path */
    ERR("LoadAndBootWindows: About to get SystemPath\n");
    *BootPath = ANSI_NULL;
    ArgValue = GetArgumentValue(Argc, Argv, "SystemPath");
    ERR("LoadAndBootWindows: SystemPath = %s\n", ArgValue ? ArgValue : "(null)");
    if (ArgValue)
        RtlStringCbCopyA(BootPath, sizeof(BootPath), ArgValue);

    /*
     * Check whether BootPath is a full path
     * and if not, create a full boot path.
     *
     * See FsOpenFile for the technique used.
     */
    ERR("LoadAndBootWindows: Checking if BootPath is full path\n");
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

    ERR("LoadAndBootWindows: Final BootPath = '%s'\n", BootPath);
    TRACE("BootPath: '%s'\n", BootPath);

    /* Retrieve the boot options */
    ERR("LoadAndBootWindows: About to get boot options\n");
    *BootOptions = ANSI_NULL;
    ArgValue = GetArgumentValue(Argc, Argv, "Options");
    ERR("LoadAndBootWindows: Options = %s\n", ArgValue ? ArgValue : "(null)");
    if (ArgValue && *ArgValue)
        RtlStringCbCopyA(BootOptions, sizeof(BootOptions), ArgValue);

    /* Append boot-time options */
    ERR("LoadAndBootWindows: About to append boot-time options\n");
    ERR("LoadAndBootWindows: BootOptions before append: len=%d, '%s'\n", strlen(BootOptions), BootOptions);
    AppendBootTimeOptions(BootOptions);
    ERR("LoadAndBootWindows: BootOptions after append: len=%d\n", strlen(BootOptions));
    ERR("LoadAndBootWindows: Final BootOptions = '%s'\n", BootOptions);

#ifdef UEFIBOOT
    /* HACK: Skip all HAL/KERNEL option processing in UEFI mode due to crash */
    ERR("LoadAndBootWindows: UEFI - Skipping ALL option processing due to crash\n");
    goto skip_all_option_processing;
#endif

    /*
     * Set the "/HAL=" and "/KERNEL=" options if needed.
     * If already present on the standard "Options=" option line, they take
     * precedence over those passed via the separate "Hal=" and "Kernel="
     * options.
     */
    ERR("LoadAndBootWindows: About to check HAL option\n");
    ERR("LoadAndBootWindows: BootOptions ptr = %p, content = '%s'\n", BootOptions, BootOptions);
#ifdef UEFIBOOT
    /* HACK: Skip NtLdrGetOption in UEFI mode to test if it's causing the crash */
    ERR("LoadAndBootWindows: UEFI - Skipping HAL/KERNEL option checks\n");
    goto skip_hal_kernel_options;
#endif
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
    ERR("LoadAndBootWindows: HAL/KERNEL options processed\n");

#ifdef UEFIBOOT
skip_hal_kernel_options:
skip_all_option_processing:
    /* Skip RAM disk check in UEFI mode as well */
    ERR("LoadAndBootWindows: UEFI - Skipping RAM disk check\n");
    FileName = NULL;
#else
    /* Check if a RAM disk file was given */
    ERR("LoadAndBootWindows: Checking for RAM disk\n");
    FileName = NtLdrGetOptionEx(BootOptions, "RDPATH=", &FileNameLength);
#endif
    if (FileName && (FileNameLength >= 7))
    {
        /* Load the RAM disk */
        Status = RamDiskInitialize(FALSE, BootOptions, SystemPartition);
        if (Status != ESUCCESS)
        {
            FileName += 7; FileNameLength -= 7;
            UiMessageBox("Failed to load RAM disk file '%.*s'",
                         FileNameLength, FileName);
            return Status;
        }
    }

    /* Handle the SOS option */
    ERR("LoadAndBootWindows: Checking SOS option\n");
#ifdef UEFIBOOT
    /* HACK: Force SOS enabled in UEFI mode without checking options */
    ERR("LoadAndBootWindows: UEFI - Forcing SOS enabled\n");
    SosEnabled = TRUE;
#else
    SosEnabled = !!NtLdrGetOption(BootOptions, "SOS");
#endif
    if (SosEnabled)
    {
        ERR("LoadAndBootWindows: SOS enabled, resetting UI\n");
        UiResetForSOS();
    }

    /* Allocate and minimally-initialize the Loader Parameter Block */
    ERR("LoadAndBootWindows: Allocating LPB\n");
    AllocateAndInitLPB(OperatingSystemVersion, &LoaderBlock);
    ERR("LoadAndBootWindows: LPB allocated\n");

    /* Load the system hive */
    ERR("LoadAndBootWindows: Loading system hive from '%s'\n", BootPath);
    UiUpdateProgressBar(15, "Loading system hive...");
    Success = WinLdrInitSystemHive(LoaderBlock, BootPath, FALSE);
    ERR("LoadAndBootWindows: System hive load result: %s\n", Success ? "SUCCESS" : "FAILED");
    ERR("LoadAndBootWindows: After result print\n");
    TRACE("SYSTEM hive %s\n", (Success ? "loaded" : "not loaded"));
    ERR("LoadAndBootWindows: After TRACE\n");
    /* Bail out if failure */
    if (!Success)
    {
        ERR("LoadAndBootWindows: System hive load failed, returning\n");
        return ENOEXEC;
    }
    ERR("LoadAndBootWindows: System hive loaded successfully, continuing\n");

    /* Fixup the version number using data from the registry */
    ERR("LoadAndBootWindows: Fixing up OS version\n");
    if (OperatingSystemVersion == 0)
        OperatingSystemVersion = WinLdrDetectVersion();
    LoaderBlock->Extension->MajorVersion = (OperatingSystemVersion & 0xFF00) >> 8;
    LoaderBlock->Extension->MinorVersion = (OperatingSystemVersion & 0xFF);
    ERR("LoadAndBootWindows: OS version fixed: Major=%d, Minor=%d\n", 
        LoaderBlock->Extension->MajorVersion, LoaderBlock->Extension->MinorVersion);

    /* Load NLS data, OEM font, and prepare boot drivers list */
    ERR("LoadAndBootWindows: Scanning system hive\n");
    Success = WinLdrScanSystemHive(LoaderBlock, BootPath);
    ERR("LoadAndBootWindows: System hive scan result: %s\n", Success ? "SUCCESS" : "FAILED");
    TRACE("SYSTEM hive %s\n", (Success ? "scanned" : "not scanned"));
    /* Bail out if failure */
    if (!Success)
        return ENOEXEC;

    /* Load the Firmware Errata file */
    ERR("LoadAndBootWindows: Loading firmware errata\n");
    Success = WinLdrInitErrataInf(LoaderBlock, OperatingSystemVersion, BootPath);
    ERR("LoadAndBootWindows: Firmware errata result: %s\n", Success ? "SUCCESS" : "FAILED");
    TRACE("Firmware Errata file %s\n", (Success ? "loaded" : "not loaded"));
    /* Not necessarily fatal if not found - carry on going */

    /* Finish loading */
    ERR("LoadAndBootWindows: Calling LoadAndBootWindowsCommon\n");
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

    ERR("LoadAndBootWindowsCommon: START\n");
    
#ifdef UEFIBOOT
    /* Skip problematic debug in UEFI - string handling causes crashes */
#else
    ERR("LoadAndBootWindowsCommon: OSVersion=%x, BootPath='%s'\n", OperatingSystemVersion, BootPath);
    TRACE("LoadAndBootWindowsCommon()\n");
    ASSERT(OperatingSystemVersion != 0);
#endif

#ifdef _M_IX86
    /* Setup redirection support */
    WinLdrSetupEms(BootOptions);
#endif

    /* Convert BootPath to SystemRoot */
    SystemRoot = strstr(BootPath, "\\");
    ERR("LoadAndBootWindowsCommon: HW detect\n");

    /* Detect hardware */
    UiUpdateProgressBar(20, "Detecting hardware...");
    LoaderBlock->ConfigurationRoot = MachHwDetect(BootOptions);
    ERR("LoadAndBootWindowsCommon: HW done\n");

    /* Initialize the PE loader import-DLL callback, so that we can obtain
     * feedback (for example during SOS) on the PE images that get loaded. */
    PeLdrImportDllLoadCallback = NtLdrImportDllLoadCallback;

    /* Load the operating system core: the Kernel, the HAL and the Kernel Debugger Transport DLL */
    ERR("LoadAndBootWindowsCommon: About to call LoadWindowsCore\n");
    ERR("LoadAndBootWindowsCommon: BootPath=%s\n", BootPath);
    Success = LoadWindowsCore(OperatingSystemVersion,
                              LoaderBlock,
                              BootOptions,
                              BootPath,
                              &KernelDTE);
    ERR("LoadAndBootWindowsCommon: LoadWindowsCore returned %d\n", Success);
    if (!Success)
    {
        /* Reset the PE loader import-DLL callback */
        PeLdrImportDllLoadCallback = NULL;
        ERR("LoadAndBootWindowsCommon: Kernel FAIL\n");
        UiMessageBox("Error loading NTOS core.");
        return ENOEXEC;
    }
    ERR("LoadAndBootWindowsCommon: KERNEL LOADED!\n");

    /* Cleanup INI file */
    IniCleanup();

/****
 **** WE HAVE NOW REACHED THE POINT OF NO RETURN !!
 ****/
    ERR("LoadAndBootWindowsCommon: NO RETURN\n");

    UiSetProgressBarSubset(40, 90); // NTOS goes from 25 to 75%

    /* Load boot drivers */
    ERR("LoadAndBootWindowsCommon: Loading boot drivers\n");
    UiSetProgressBarText("Loading boot drivers...");
    Success = WinLdrLoadBootDrivers(LoaderBlock, BootPath);
    ERR("LoadAndBootWindowsCommon: Boot drivers %s\n", Success ? "OK" : "FAIL");
    TRACE("Boot drivers loading %s\n", Success ? "successful" : "failed");

    UiSetProgressBarSubset(0, 100);

    /* Reset the PE loader import-DLL callback */
    PeLdrImportDllLoadCallback = NULL;

    /* Initialize Phase 1 - no drivers loading anymore */
    ERR("LoadAndBootWindowsCommon: Phase 1 init\n");
    WinLdrInitializePhase1(LoaderBlock,
                           BootOptions,
                           SystemRoot,
                           BootPath,
                           OperatingSystemVersion);
    ERR("LoadAndBootWindowsCommon: Phase 1 done\n");

#ifdef UEFIBOOT
    /* Skip UI update in UEFI mode - might crash */
    ERR("LoadAndBootWindowsCommon: Skipping UI update (UEFI)\n");
#else
    UiUpdateProgressBar(100, NULL);
#endif
    ERR("LoadAndBootWindowsCommon: After progress bar\n");

    /* Save entry-point pointer and Loader block VAs */
    ERR("LoadAndBootWindowsCommon: KernelDTE=%p\n", KernelDTE);
    if (KernelDTE == NULL)
    {
        ERR("LoadAndBootWindowsCommon: ERROR - KernelDTE is NULL!\n");
#ifdef UEFIBOOT
        /* Try to continue anyway in UEFI mode */
        ERR("LoadAndBootWindowsCommon: UEFI - Skipping kernel jump due to NULL DTE\n");
        return ESUCCESS;
#endif
    }
    ERR("LoadAndBootWindowsCommon: Getting entry point from DTE\n");
    KiSystemStartup = (KERNEL_ENTRY_POINT)KernelDTE->EntryPoint;
    ERR("LoadAndBootWindowsCommon: KiSystemStartup=%p\n", KiSystemStartup);
    LoaderBlockVA = PaToVa(LoaderBlock);
    ERR("LoadAndBootWindowsCommon: LoaderBlockVA=%p\n", LoaderBlockVA);
    ERR("LoadAndBootWindowsCommon: Entry point ready\n");

    /* "Stop all motors", change videomode */
#ifdef UEFIBOOT
    /* Skip MachPrepareForReactOS in UEFI mode - it crashes */
    ERR("LoadAndBootWindowsCommon: Skipping MachPrepare (UEFI)\n");
#else
    ERR("LoadAndBootWindowsCommon: MachPrepare\n");
    MachPrepareForReactOS();
    ERR("LoadAndBootWindowsCommon: MachPrepare done\n");
#endif

    /* Show the "debug mode" notice if needed */
    /* Match KdInitSystem() conditions */
#ifdef UEFIBOOT
    /* Skip NtLdrGetOption calls in UEFI mode - they crash */
    ERR("LoadAndBootWindowsCommon: Skipping debug banner (UEFI)\n");
#else
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
#endif

    /* Debugging... */
    //DumpMemoryAllocMap();

    /* Do the machine specific initialization */
    ERR("LoadAndBootWindowsCommon: Machine setup\n");
    WinLdrSetupMachineDependent(LoaderBlock);

    /* Map pages and create memory descriptors */
    ERR("LoadAndBootWindowsCommon: Memory layout\n");
    WinLdrSetupMemoryLayout(LoaderBlock);

    /* Set processor context */
#ifdef UEFIBOOT
    /* For UEFI, attempt processor context setup with debug */
    ERR("LoadAndBootWindowsCommon: UEFI - Setting processor context\n");
    
    /* Call WinLdrSetProcessorContext - let's see where it crashes */
    ERR("LoadAndBootWindowsCommon: About to call WinLdrSetProcessorContext\n");
    WinLdrSetProcessorContext(OperatingSystemVersion);
    ERR("LoadAndBootWindowsCommon: WinLdrSetProcessorContext returned!\n");
    
    /* Save final value of LoaderPagesSpanned - skip in UEFI for now since Extension is virtual */
    ERR("LoadAndBootWindowsCommon: LoaderBlock=%p\n", LoaderBlock);
    ERR("LoadAndBootWindowsCommon: UEFI - Skipping LoaderPagesSpanned (Extension is virtual)\n");
    
    /* Jump to kernel */
    ERR("LoadAndBootWindowsCommon: JUMPING TO KERNEL (UEFI)!\n");
    ERR("LoadAndBootWindowsCommon: KiSystemStartup=%p\n", KiSystemStartup);
    ERR("LoadAndBootWindowsCommon: LoaderBlock=%p (physical)\n", LoaderBlock);
    ERR("LoadAndBootWindowsCommon: LoaderBlockVA=%p (virtual)\n", LoaderBlockVA);
    
    /* Set up kernel virtual mappings */
    ERR("LoadAndBootWindowsCommon: Setting up kernel virtual mappings\n");
    if (!WinLdrSetupKernelVirtualMapping(LoaderBlock))
    {
        ERR("LoadAndBootWindowsCommon: ERROR - Failed to set up virtual mappings!\n");
        while(1) __asm__ __volatile__("hlt");
    }
    
    /* Get the PxeBase for new page tables */
    extern PHARDWARE_PTE PxeBase;  /* Defined in arch/amd64/winldr.c */
    if (!PxeBase)
    {
        ERR("LoadAndBootWindowsCommon: ERROR - PxeBase is NULL!\n");
        while(1) __asm__ __volatile__("hlt");
    }
    
    /* Update LoaderBlockVA to use virtual address */
    LoaderBlockVA = PaToVa(LoaderBlock);
    
    /* Now switch to kernel page tables and jump to kernel in one operation */
    ERR("LoadAndBootWindowsCommon: >>> JUMPING TO KERNEL <<<\n");
    ERR("LoadAndBootWindowsCommon: PxeBase=%p\n", PxeBase);
    ERR("LoadAndBootWindowsCommon: KiSystemStartup=%p\n", KiSystemStartup);
    ERR("LoadAndBootWindowsCommon: LoaderBlockVA=%p\n", LoaderBlockVA);
    
    /* First, let's try jumping without switching CR3 to see if kernel is accessible */
    ERR("LoadAndBootWindowsCommon: Testing kernel accessibility without CR3 switch\n");
    
    /* Check if we can read from kernel entry point */
    volatile UCHAR testByte = *(UCHAR*)((ULONG64)KiSystemStartup & ~KSEG0_BASE);
    ERR("LoadAndBootWindowsCommon: Read byte %02x from physical kernel entry\n", testByte);
    
    /* For now, just jump to the physical address of the kernel */
    typedef VOID (NTAPI *KERNEL_ENTRY_POINT)(PLOADER_PARAMETER_BLOCK LoaderBlock);
    KERNEL_ENTRY_POINT PhysicalKernelEntry = (KERNEL_ENTRY_POINT)((ULONG64)KiSystemStartup & ~KSEG0_BASE);
    ERR("LoadAndBootWindowsCommon: Jumping to physical kernel at %p\n", PhysicalKernelEntry);
    ERR("LoadAndBootWindowsCommon: With LoaderBlock at %p\n", LoaderBlock);
    
    /* Try jumping to physical kernel with physical loader block */
    PhysicalKernelEntry(LoaderBlock);
    
    /* Should never get here */
    ERR("LoadAndBootWindowsCommon: ERROR - Kernel returned!\n");
    while(1) __asm__ __volatile__("hlt");
#else
    ERR("LoadAndBootWindowsCommon: CPU context\n");
    WinLdrSetProcessorContext(OperatingSystemVersion);
    ERR("LoadAndBootWindowsCommon: CPU context SET\n");

    /* Save final value of LoaderPagesSpanned */
    LoaderBlock->Extension->LoaderPagesSpanned = MmGetLoaderPagesSpanned();
    ERR("LoadAndBootWindowsCommon: Pages spanned saved\n");

    ERR("LoadAndBootWindowsCommon: Paged mode OK\n");
    TRACE("Hello from paged mode, KiSystemStartup %p, LoaderBlockVA %p!\n",
          KiSystemStartup, LoaderBlockVA);

    /* Zero KI_USER_SHARED_DATA page */
    ERR("LoadAndBootWindowsCommon: Zeroing USER_SHARED_DATA\n");
    RtlZeroMemory((PVOID)KI_USER_SHARED_DATA, MM_PAGE_SIZE);
    ERR("LoadAndBootWindowsCommon: USER_SHARED_DATA zeroed\n");

    WinLdrpDumpMemoryDescriptors(LoaderBlockVA);
    WinLdrpDumpBootDriver(LoaderBlockVA);
#ifndef _M_AMD64
    WinLdrpDumpArcDisks(LoaderBlockVA);
#endif

    /* Pass control */
    ERR("LoadAndBootWindowsCommon: JUMPING TO KERNEL!\n");
    (*KiSystemStartup)(LoaderBlockVA);
#endif
    ERR("LoadAndBootWindowsCommon: KERNEL RETURNED?!\n");

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
