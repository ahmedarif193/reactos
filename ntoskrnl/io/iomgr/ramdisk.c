/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/io/iomgr/ramdisk.c
 * PURPOSE:         Allows booting from RAM disk
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include <initguid.h>
#include <ntddrdsk.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern KEVENT PiEnumerationFinished;

static
VOID
IopAcquireBootRamdiskGuid(
    _Out_ PGUID DiskGuid)
{
    static const UNICODE_STRING ParametersKey = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\Ramdisk\\Parameters");
    static const UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"BootDiskGuid");
    UNICODE_STRING RamdiskKey = RTL_CONSTANT_STRING(L"Ramdisk");
    UNICODE_STRING ParametersSubKey = RTL_CONSTANT_STRING(L"Ramdisk\\Parameters");
    GUID GuidValue = RAMDISK_BOOTDISK_GUID;
    HANDLE KeyHandle = NULL;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    BOOLEAN HaveGuid = FALSE;

    RtlCreateRegistryKey(RTL_REGISTRY_SERVICES, RamdiskKey.Buffer);
    RtlCreateRegistryKey(RTL_REGISTRY_SERVICES, ParametersSubKey.Buffer);

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&ParametersKey,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        ULONG ResultLength = 0;
        PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = NULL;

        Status = ZwQueryValueKey(KeyHandle,
                                 (PUNICODE_STRING)&ValueName,
                                 KeyValuePartialInformation,
                                 NULL,
                                 0,
                                 &ResultLength);
        if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
        {
            ValueInfo = ExAllocatePoolWithTag(PagedPool, ResultLength, 'giRB');
            if (ValueInfo)
            {
                Status = ZwQueryValueKey(KeyHandle,
                                          (PUNICODE_STRING)&ValueName,
                                          KeyValuePartialInformation,
                                          ValueInfo,
                                          ResultLength,
                                          &ResultLength);
                if (NT_SUCCESS(Status) &&
                    ValueInfo->Type == REG_BINARY &&
                    ValueInfo->DataLength == sizeof(GUID))
                {
                    RtlCopyMemory(&GuidValue, ValueInfo->Data, sizeof(GUID));
                    HaveGuid = TRUE;
                }

                ExFreePoolWithTag(ValueInfo, 'giRB');
            }
        }

        if (!HaveGuid)
        {
            if (!NT_SUCCESS(ExUuidCreate(&GuidValue)))
            {
                GuidValue = RAMDISK_BOOTDISK_GUID;
            }

            ZwSetValueKey(KeyHandle,
                          (PUNICODE_STRING)&ValueName,
                          0,
                          REG_BINARY,
                          &GuidValue,
                          sizeof(GuidValue));
        }

        ZwClose(KeyHandle);
    }
    else
    {
        if (!NT_SUCCESS(ExUuidCreate(&GuidValue)))
        {
            GuidValue = RAMDISK_BOOTDISK_GUID;
        }
    }

    *DiskGuid = GuidValue;
}

/* FUNCTIONS ******************************************************************/

static
BOOLEAN
IopIsLoadOptionDelimiter(CHAR Character)
{
    return (Character == '\0') ||
           (Character == ' ')  ||
           (Character == '\t') ||
           (Character == '\n') ||
           (Character == '\r');
}

static
BOOLEAN
IopParseUnsignedIntegerOption(
    _In_ PCCHAR Value,
    _In_ ULONGLONG MaxValue,
    _Out_ PULONGLONG Result)
{
    ULONGLONG Accumulator = 0;
    BOOLEAN SeenDigit = FALSE;
    CHAR Current;
    ULONG Base = 10;

    if (!Value || !Result)
    {
        return FALSE;
    }

    while ((*Value == ' ') || (*Value == '\t'))
    {
        Value++;
    }

    if (*Value == '+')
    {
        Value++;
    }
    else if (*Value == '-')
    {
        return FALSE;
    }

    if ((Value[0] == '0') && (Value[1] != '\0'))
    {
        CHAR Prefix = Value[1];

        if (Prefix == 'X')
        {
            Base = 16;
            Value += 2;
        }
        else if (Prefix == 'O')
        {
            Base = 8;
            Value += 2;
        }
        else if (Prefix == 'B')
        {
            Base = 2;
            Value += 2;
        }
    }

    while ((Current = *Value) != '\0')
    {
        if (IopIsLoadOptionDelimiter(Current))
        {
            break;
        }

        ULONGLONG DigitValue;

        if ((Current >= '0') && (Current <= '9'))
        {
            DigitValue = (ULONGLONG)(Current - '0');
        }
        else if ((Base == 16) && (Current >= 'A') && (Current <= 'F'))
        {
            DigitValue = 10ull + (ULONGLONG)(Current - 'A');
        }
        else
        {
            return FALSE;
        }

        if (DigitValue >= Base)
        {
            return FALSE;
        }

        if (Accumulator > (MaxValue / Base))
        {
            return FALSE;
        }
        if ((Accumulator == (MaxValue / Base)) && (DigitValue > (MaxValue % Base)))
        {
            return FALSE;
        }

        Accumulator = (Accumulator * Base) + DigitValue;

        SeenDigit = TRUE;
        Value++;
    }

    if (!SeenDigit)
    {
        return FALSE;
    }

    *Result = Accumulator;
    return TRUE;
}

static
PCCHAR
IopLocateOptionValue(
    _In_ PCCHAR CommandLine,
    _In_ PCSTR OptionName)
{
    SIZE_T NameLength = strlen(OptionName);
    PCCHAR Cursor = CommandLine;

    while ((Cursor = strstr(Cursor, OptionName)) != NULL)
    {
        PCCHAR Value;

        if ((Cursor != CommandLine) &&
            !IopIsLoadOptionDelimiter(Cursor[-1]))
        {
            Cursor += NameLength;
            continue;
        }

        Value = Cursor + NameLength;
        while ((*Value == ' ') || (*Value == '\t')) Value++;

        if (*Value != '=')
        {
            Cursor += NameLength;
            continue;
        }

        Value++;
        while ((*Value == ' ') || (*Value == '\t')) Value++;

        return Value;
    }

    return NULL;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopStartRamdisk(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;
    PMEMORY_ALLOCATION_DESCRIPTOR BestDescriptor;
    NTSTATUS Status;
    PCHAR CommandLine, OffsetValue, LengthValue;
    HANDLE DriverHandle;
    RAMDISK_CREATE_INPUT RamdiskCreate;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING GuidString, SymbolicLinkName, ObjectName;
    PLIST_ENTRY ListHead, NextEntry;
    OBJECT_ATTRIBUTES ObjectAttributes;
    WCHAR SourceString[54];
    LONG ParsedOffset = 0;
    ULONGLONG ParsedLength = 0;
    ULONGLONG BestScore;
    ULONGLONG SelectedDescriptorLength;
    BOOLEAN OffsetSpecified = FALSE;
    BOOLEAN LengthSpecified = FALSE;

    //
    PCSTR BootPathString = (LoaderBlock && LoaderBlock->NtBootPathName)
                           ? LoaderBlock->NtBootPathName
                           : "<null>";

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_INFO_LEVEL,
               "IopStartRamdisk: entry NtBootPath='%s'\n",
               BootPathString);

    // Parse command-line parameters
    //
    CommandLine = LoaderBlock->LoadOptions;
    if (CommandLine)
    {
        _strupr(CommandLine);

        OffsetValue = IopLocateOptionValue(CommandLine, "RDIMAGEOFFSET");
        if (OffsetValue)
        {
            ULONGLONG OffsetCandidate;

            if (!IopParseUnsignedIntegerOption(OffsetValue,
                                               MAXLONG,
                                               &OffsetCandidate))
            {
                KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                             RD_INVALID_OFFSET,
                             (ULONG_PTR)OffsetValue,
                             0,
                             0);
            }

            ParsedOffset = (LONG)OffsetCandidate;
            OffsetSpecified = TRUE;
        }

        LengthValue = IopLocateOptionValue(CommandLine, "RDIMAGELENGTH");
        if (LengthValue)
        {
            ULONGLONG LengthCandidate;

            if (!IopParseUnsignedIntegerOption(LengthValue,
                                               MAXLONGLONG,
                                               &LengthCandidate))
            {
                KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                             RD_INVALID_LENGTH,
                             (ULONG_PTR)LengthValue,
                             0,
                             0);
            }

            ParsedLength = LengthCandidate;
            LengthSpecified = TRUE;
        }
    }

    //
    // Scan memory descriptors and pick the best candidate.
    //
    // Selection priority (highest to lowest):
    // 1. LoaderXIPRom with exact length match (legacy compatibility)
    // 2. LoaderXIPRom with larger length
    // 3. LoaderMemoryData with exact length match (preferred for new allocations)
    // 4. LoaderMemoryData with larger length (least preferred, more likely to be wrong)
    //
    // The "exact match" heuristic helps avoid selecting the wrong LoaderMemoryData
    // descriptor when multiple such descriptors exist (e.g., BGRT image, registry hives).
    //
    MemoryDescriptor = NULL;
    BestDescriptor = NULL;
    BestScore = 0;
    ListHead = &LoaderBlock->MemoryDescriptorListHead;
    NextEntry = ListHead->Flink;
    while (NextEntry != ListHead)
    {
        ULONGLONG CurrentDescriptorLength;
        ULONGLONG AvailableLength;
        ULONGLONG RequiredLength;
        ULONGLONG Score;
        BOOLEAN IsExactMatch;

        MemoryDescriptor = CONTAINING_RECORD(NextEntry,
                                             MEMORY_ALLOCATION_DESCRIPTOR,
                                             ListEntry);

        if ((MemoryDescriptor->MemoryType != LoaderXIPRom) &&
            (MemoryDescriptor->MemoryType != LoaderMemoryData))
        {
            NextEntry = NextEntry->Flink;
            continue;
        }

        CurrentDescriptorLength = (ULONGLONG)MemoryDescriptor->PageCount << PAGE_SHIFT;
        if ((ULONGLONG)ParsedOffset >= CurrentDescriptorLength)
        {
            NextEntry = NextEntry->Flink;
            continue;
        }

        AvailableLength = CurrentDescriptorLength - (ULONGLONG)ParsedOffset;
        RequiredLength = LengthSpecified ? ParsedLength : AvailableLength;
        if ((RequiredLength == 0) || (RequiredLength > AvailableLength))
        {
            NextEntry = NextEntry->Flink;
            continue;
        }

        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "IopStartRamdisk: candidate descriptor type=%lu basePage=0x%lx pages=%lu (len=%llu) avail=%llu req=%llu\n",
                   MemoryDescriptor->MemoryType,
                   MemoryDescriptor->BasePage,
                   MemoryDescriptor->PageCount,
                   CurrentDescriptorLength,
                   AvailableLength,
                   RequiredLength);

        //
        // Compute selection score:
        // - Bit 63: LoaderXIPRom (highest priority type)
        // - Bit 62: Exact length match (prefer descriptors sized exactly for the ramdisk)
        // - Bits 0-61: Available length (tiebreaker, prefer larger)
        //
        IsExactMatch = LengthSpecified && (AvailableLength == RequiredLength);
        Score = (AvailableLength & 0x3FFFFFFFFFFFFFFFULL);
        if (IsExactMatch)
            Score |= (1ull << 62);
        if (MemoryDescriptor->MemoryType == LoaderXIPRom)
            Score |= (1ull << 63);

        if (!BestDescriptor || (Score > BestScore))
        {
            BestDescriptor = MemoryDescriptor;
            BestScore = Score;
        }

        NextEntry = NextEntry->Flink;
    }

    if (!BestDescriptor)
    {
        ULONGLONG LengthParam64 = LengthSpecified ? ParsedLength : 0;
        ULONG_PTR LengthParam = (LengthParam64 > (ULONGLONG)(ULONG_PTR)-1)
                                ? (ULONG_PTR)-1
                                : (ULONG_PTR)LengthParam64;

        KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                     RD_NO_XIPROM_DESCRIPTOR,
                     (ULONG_PTR)ParsedOffset,
                     LengthParam,
                     0);
    }

    MemoryDescriptor = BestDescriptor;
    SelectedDescriptorLength = (ULONGLONG)MemoryDescriptor->PageCount << PAGE_SHIFT;

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "IopStartRamdisk: SELECTED descriptor type=%lu basePage=0x%lx pages=%lu (%llu bytes) score=0x%llx\n",
               MemoryDescriptor->MemoryType,
               MemoryDescriptor->BasePage,
               MemoryDescriptor->PageCount,
               SelectedDescriptorLength,
               BestScore);

    // Setup the input buffer
    //
    RtlZeroMemory(&RamdiskCreate, sizeof(RamdiskCreate));
    RamdiskCreate.Version = sizeof(RamdiskCreate);
    RamdiskCreate.DiskType = RAMDISK_BOOT_DISK;
    RamdiskCreate.BasePage = MemoryDescriptor->BasePage;
    RamdiskCreate.DiskOffset = 0;
    RamdiskCreate.DiskLength.QuadPart = SelectedDescriptorLength;
    IopAcquireBootRamdiskGuid(&RamdiskCreate.DiskGuid);
    RamdiskCreate.DriveLetter = L'X';
    RamdiskCreate.Options.Fixed = TRUE;
    RamdiskCreate.Options.Readonly = FALSE;
    RamdiskCreate.Options.Hidden = FALSE;
    RamdiskCreate.Options.NoDriveLetter = FALSE;
    RamdiskCreate.Options.NoDosDevice = TRUE;
    /* Default to disk semantics (FAT-friendly 512 BPS). The ramdisk
       driver will enable ExportAsCd automatically if it detects a
       true El-Torito ISO (CD001 at LBA 16) or if RDEXPORTASCD is set
       on the boot command line. */
    RamdiskCreate.Options.ExportAsCd = FALSE;

    if (OffsetSpecified)
    {
        RamdiskCreate.DiskOffset = ParsedOffset;
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_INFO_LEVEL,
                   "IopStartRamdisk: parsed RDIMAGEOFFSET=%ld\n",
                   RamdiskCreate.DiskOffset);
    }

    RamdiskCreate.DiskLength.QuadPart -= RamdiskCreate.DiskOffset;

    if (LengthSpecified)
    {
        RamdiskCreate.DiskLength.QuadPart = (LONGLONG)ParsedLength;
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_INFO_LEVEL,
                   "IopStartRamdisk: parsed RDIMAGELENGTH=%I64d\n",
                   RamdiskCreate.DiskLength.QuadPart);
    }

    ASSERT((ULONGLONG)RamdiskCreate.DiskOffset < SelectedDescriptorLength);
    ASSERT((ULONGLONG)RamdiskCreate.DiskLength.QuadPart <=
           SelectedDescriptorLength - (ULONGLONG)RamdiskCreate.DiskOffset);

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_INFO_LEVEL,
               "IopStartRamdisk: effective length %I64u, offset %ld\n",
               RamdiskCreate.DiskLength.QuadPart,
               RamdiskCreate.DiskOffset);

    //
    // Setup object attributes
    //
    RtlInitUnicodeString(&ObjectName, L"\\Device\\Ramdisk");
    InitializeObjectAttributes(&ObjectAttributes,
                               &ObjectName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    //
    // Open a handle to the driver
    //
    Status = ZwOpenFile(&DriverHandle,
                        GENERIC_ALL | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (!(NT_SUCCESS(Status)) || !(NT_SUCCESS(IoStatusBlock.Status)))
    {
        //
        // Bugcheck -- no driver
        //
        KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                     RD_NO_RAMDISK_DRIVER,
                     IoStatusBlock.Status,
                     0,
                     0);
    }

    //
    // Send create command
    //
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_INFO_LEVEL,
               "IopStartRamdisk: issuing FSCTL_CREATE_RAM_DISK (%I64u bytes, drive %wc)\n",
               RamdiskCreate.DiskLength.QuadPart,
               RamdiskCreate.DriveLetter);

    Status = ZwDeviceIoControlFile(DriverHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   FSCTL_CREATE_RAM_DISK,
                                   &RamdiskCreate,
                                   sizeof(RamdiskCreate),
                                   NULL,
                                   0);
    ZwClose(DriverHandle);
    if (!(NT_SUCCESS(Status)) || !(NT_SUCCESS(IoStatusBlock.Status)))
    {
        //
        // Bugcheck -- driver failed
        //
        KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                     RD_FSCTL_FAILED,
                     IoStatusBlock.Status,
                     0,
                     0);
    }

    //
    // Convert the GUID
    //
    Status = RtlStringFromGUID(&RamdiskCreate.DiskGuid, &GuidString);
    if (!NT_SUCCESS(Status))
    {
        //
        // Bugcheck -- GUID convert failed
        //
        KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                     RD_GUID_CONVERT_FAILED,
                     Status,
                     0,
                     0);
    }

    //
    // Build the symbolic link name and target
    //
    _snwprintf(SourceString,
               sizeof(SourceString)/sizeof(WCHAR),
               L"\\Device\\Ramdisk%wZ",
               &GuidString);

    //
    // Publish boot ramdisk metadata now that the device path is known.
    //
    {
        WCHAR DiskSubKey[128];
        WCHAR DosLinkBuffer[32];
        WCHAR VolumeValueName[72];
        ULONG DosLetterValue;

        RtlCreateRegistryKey(RTL_REGISTRY_SERVICES, L"Ramdisk");
        RtlCreateRegistryKey(RTL_REGISTRY_SERVICES, L"Ramdisk\\Parameters");
        RtlCreateRegistryKey(RTL_REGISTRY_SERVICES, L"Ramdisk\\Parameters\\Disks");

        if (_snwprintf(DiskSubKey,
                        RTL_NUMBER_OF(DiskSubKey),
                        L"Ramdisk\\Parameters\\Disks\\%wZ",
                        &GuidString) >= 0)
        {
            DiskSubKey[RTL_NUMBER_OF(DiskSubKey) - 1] = UNICODE_NULL;

            if (NT_SUCCESS(RtlCreateRegistryKey(RTL_REGISTRY_SERVICES, DiskSubKey)))
            {
                DosLetterValue = (ULONG)RamdiskCreate.DriveLetter;

                RtlWriteRegistryValue(RTL_REGISTRY_SERVICES,
                                      DiskSubKey,
                                      L"DiskGuid",
                                      REG_BINARY,
                                      &RamdiskCreate.DiskGuid,
                                      sizeof(GUID));

                RtlWriteRegistryValue(RTL_REGISTRY_SERVICES,
                                      DiskSubKey,
                                      L"DriveLetter",
                                      REG_DWORD,
                                      &DosLetterValue,
                                      sizeof(DosLetterValue));

                RtlWriteRegistryValue(RTL_REGISTRY_SERVICES,
                                      DiskSubKey,
                                      L"ServiceName",
                                      REG_SZ,
                                      L"ramdisk",
                                      sizeof(L"ramdisk"));

                RtlWriteRegistryValue(RTL_REGISTRY_SERVICES,
                                      DiskSubKey,
                                      L"DevicePath",
                                      REG_SZ,
                                      SourceString,
                                      (ULONG)((wcslen(SourceString) + 1) * sizeof(WCHAR)));

                if (_snwprintf(DosLinkBuffer,
                                RTL_NUMBER_OF(DosLinkBuffer),
                                L"\\DosDevices\\%wc:",
                                RamdiskCreate.DriveLetter ? RamdiskCreate.DriveLetter : L'X') >= 0)
                {
                    DosLinkBuffer[RTL_NUMBER_OF(DosLinkBuffer) - 1] = UNICODE_NULL;

                    RtlWriteRegistryValue(RTL_REGISTRY_SERVICES,
                                          DiskSubKey,
                                          L"SuggestedDosLink",
                                          REG_SZ,
                                          DosLinkBuffer,
                                          (ULONG)((wcslen(DosLinkBuffer) + 1) * sizeof(WCHAR)));
                }
            }
        }

        RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, L"\\Registry\\Machine\\System\\MountedDevices");

        if (_snwprintf(DosLinkBuffer,
                        RTL_NUMBER_OF(DosLinkBuffer),
                        L"\\DosDevices\\%wc:",
                        RamdiskCreate.DriveLetter ? RamdiskCreate.DriveLetter : L'X') >= 0)
        {
            DosLinkBuffer[RTL_NUMBER_OF(DosLinkBuffer) - 1] = UNICODE_NULL;

            RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE,
                                  L"\\Registry\\Machine\\System\\MountedDevices",
                                  DosLinkBuffer,
                                  REG_BINARY,
                                  &RamdiskCreate.DiskGuid,
                                  sizeof(GUID));
        }

        if (_snwprintf(VolumeValueName,
                        RTL_NUMBER_OF(VolumeValueName),
                        L"\\??\\Volume%wZ",
                        &GuidString) >= 0)
        {
            VolumeValueName[RTL_NUMBER_OF(VolumeValueName) - 1] = UNICODE_NULL;

            RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE,
                                  L"\\Registry\\Machine\\System\\MountedDevices",
                                  VolumeValueName,
                                  REG_BINARY,
                                  &RamdiskCreate.DiskGuid,
                                  sizeof(GUID));
        }

        RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE,
                              L"\\Registry\\Machine\\System\\Setup",
                              L"SystemPartition",
                              REG_SZ,
                              SourceString,
                              (ULONG)((wcslen(SourceString) + 1) * sizeof(WCHAR)));
    }

    SymbolicLinkName.Length = 38;
    SymbolicLinkName.MaximumLength = 38 + sizeof(UNICODE_NULL);
    SymbolicLinkName.Buffer = L"\\ArcName\\ramdisk(0)";

    //
    // Create the symbolic link. If it already exists, treat as success.
    //
    RtlInitUnicodeString(&ObjectName, SourceString);
    Status = IoCreateSymbolicLink(&SymbolicLinkName, &ObjectName);
    RtlFreeUnicodeString(&GuidString);
    if (!NT_SUCCESS(Status) &&
        Status != STATUS_OBJECT_NAME_EXISTS &&
        Status != STATUS_OBJECT_NAME_COLLISION)
    {
        //
        // Bugcheck -- symlink create failed for a reason we cannot ignore
        //
        KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                     RD_SYMLINK_CREATE_FAILED,
                     Status,
                     0,
                     0);
    }

    //
    // ReactOS hack (drive letter should not be hardcoded, and maybe set by mountmgr.sys)
    //
    {
        ANSI_STRING AnsiPath;
        CHAR Buffer[256];
        UNICODE_STRING NtSystemRoot;

        AnsiPath.Length = sprintf(Buffer, "X:%s", LoaderBlock->NtBootPathName);
        AnsiPath.MaximumLength = AnsiPath.Length + 1;
        AnsiPath.Buffer = Buffer;
        RtlInitEmptyUnicodeString(&NtSystemRoot,
                                  SharedUserData->NtSystemRoot,
                                  sizeof(SharedUserData->NtSystemRoot));
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_INFO_LEVEL,
               "IopStartRamdisk: setting NtSystemRoot to '%s'\n",
               Buffer);

        Status = RtlAnsiStringToUnicodeString(&NtSystemRoot, &AnsiPath, FALSE);
        if (!NT_SUCCESS(Status))
        {
            KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                         RD_SYSROOT_INIT_FAILED,
                         Status,
                         0,
                         0);
        }
    }

    //
    // Wait for ramdisk relations being initialized, but do not block forever.
    // Some UEFI platforms (e.g., VirtualBox) may stall storage enumeration
    // (CDROM queues frozen) and never signal PiEnumerationFinished, causing
    // an apparent idle hang during RAMDISK boots. Use a bounded wait and
    // continue boot even if PnP enumeration hasn't completed yet.
    //
    {
        LARGE_INTEGER Timeout;
        ULONG WaitMs = 2000; // default 2 seconds
        /* Read optional override: HKLM\System\CurrentControlSet\Control\Session Manager\RamdiskBootPnPWaitMs */
        {
            HANDLE Key;
            OBJECT_ATTRIBUTES oa;
            UNICODE_STRING KeyPath = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Session Manager");
            UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"RamdiskBootPnPWaitMs");
            InitializeObjectAttributes(&oa, &KeyPath, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
            if (NT_SUCCESS(ZwOpenKey(&Key, KEY_READ, &oa)))
            {
                ULONG len = 0;
                PKEY_VALUE_PARTIAL_INFORMATION info = NULL;
                NTSTATUS qs = ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, NULL, 0, &len);
                if ((qs == STATUS_BUFFER_TOO_SMALL || qs == STATUS_BUFFER_OVERFLOW) && len)
                {
                    info = ExAllocatePoolWithTag(PagedPool, len, 'wrPR');
                    if (info && NT_SUCCESS(ZwQueryValueKey(Key, &ValueName, KeyValuePartialInformation, info, len, &len)))
                    {
                        if (info->Type == REG_DWORD && info->DataLength >= sizeof(ULONG))
                        {
                            ULONG v = *(PULONG)info->Data;
                            if (v > 60000) v = 60000; /* clamp to 60s */
                            WaitMs = v;
                        }
                    }
                    if (info) ExFreePoolWithTag(info, 'wrPR');
                }
                ZwClose(Key);
            }
        }
        Timeout.QuadPart = (WaitMs == 0) ? 0 : -(LONGLONG)WaitMs * 10 * 1000;
        NTSTATUS WaitStatus = KeWaitForSingleObject(&PiEnumerationFinished,
                                                    Executive,
                                                    KernelMode,
                                                    FALSE,
                                                    &Timeout);
        if (!NT_SUCCESS(WaitStatus))
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_WARNING_LEVEL,
                       "IopStartRamdisk: PiEnumerationFinished wait status=0x%08X; continuing\n",
                       WaitStatus);
        }
        else
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_INFO_LEVEL,
                       "IopStartRamdisk: ramdisk enumeration complete\n");
        }
    }

    //
    // We made it
    //
    return STATUS_SUCCESS;
}
