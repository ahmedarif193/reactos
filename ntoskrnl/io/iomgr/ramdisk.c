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
IopParseUnsignedDecimalOption(
    _In_ PCCHAR Value,
    _In_ ULONGLONG MaxValue,
    _Out_ PULONGLONG Result)
{
    ULONGLONG Accumulator = 0;
    BOOLEAN SeenDigit = FALSE;
    CHAR Current;

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

    while ((Current = *Value) != '\0')
    {
        if (IopIsLoadOptionDelimiter(Current))
        {
            break;
        }

        if ((Current < '0') || (Current > '9'))
        {
            return FALSE;
        }

        ULONGLONG DigitValue;

        DigitValue = (ULONGLONG)(Current - '0');
        if (Accumulator > (MaxValue / 10))
        {
            return FALSE;
        }
        if ((Accumulator == (MaxValue / 10)) && (DigitValue > (MaxValue % 10)))
        {
            return FALSE;
        }

        Accumulator = (Accumulator * 10) + DigitValue;

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

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopStartRamdisk(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMEMORY_ALLOCATION_DESCRIPTOR MemoryDescriptor;
    PMEMORY_ALLOCATION_DESCRIPTOR BestDescriptor;
    NTSTATUS Status;
    PCHAR CommandLine, Offset, OffsetValue, Length, LengthValue;
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
               DPFLTR_TRACE_LEVEL,
               "IopStartRamdisk: entry NtBootPath='%s'\n",
               BootPathString);

    // Parse command-line parameters
    //
    CommandLine = LoaderBlock->LoadOptions;
    if (CommandLine)
    {
        _strupr(CommandLine);

        Offset = strstr(CommandLine, "RDIMAGEOFFSET");
        if (Offset)
        {
            OffsetValue = strstr(Offset, "=");
            if (OffsetValue)
            {
                ULONGLONG OffsetCandidate;

                if (!IopParseUnsignedDecimalOption(OffsetValue + 1,
                                                   MAXLONG,
                                                   &OffsetCandidate))
                {
                    KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                                 RD_INVALID_OFFSET,
                                 (ULONG_PTR)(OffsetValue + 1),
                                 0,
                                 0);
                }

                ParsedOffset = (LONG)OffsetCandidate;
                OffsetSpecified = TRUE;
            }
        }

        Length = strstr(CommandLine, "RDIMAGELENGTH");
        if (Length)
        {
            LengthValue = strstr(Length, "=");
            if (LengthValue)
            {
                ULONGLONG LengthCandidate;

                if (!IopParseUnsignedDecimalOption(LengthValue + 1,
                                                   MAXLONGLONG,
                                                   &LengthCandidate))
                {
                    KeBugCheckEx(RAMDISK_BOOT_INITIALIZATION_FAILED,
                                 RD_INVALID_LENGTH,
                                 (ULONG_PTR)(LengthValue + 1),
                                 0,
                                 0);
                }

                ParsedLength = LengthCandidate;
                LengthSpecified = TRUE;
            }
        }
    }

    // Scan memory descriptors and pick the best candidate
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

        Score = AvailableLength;
        if (MemoryDescriptor->MemoryType == LoaderMemoryData)
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
               DPFLTR_TRACE_LEVEL,
               "IopStartRamdisk: using descriptor type %lu base %I64u pages %I64u (%I64u bytes)\n",
               MemoryDescriptor->MemoryType,
               MemoryDescriptor->BasePage,
               MemoryDescriptor->PageCount,
               SelectedDescriptorLength);

    // Setup the input buffer
    //
    RtlZeroMemory(&RamdiskCreate, sizeof(RamdiskCreate));
    RamdiskCreate.Version = sizeof(RamdiskCreate);
    RamdiskCreate.DiskType = RAMDISK_BOOT_DISK;
    RamdiskCreate.BasePage = MemoryDescriptor->BasePage;
    RamdiskCreate.DiskOffset = 0;
    RamdiskCreate.DiskLength.QuadPart = SelectedDescriptorLength;
    RamdiskCreate.DiskGuid = RAMDISK_BOOTDISK_GUID;
    RamdiskCreate.DriveLetter = L'X';
    RamdiskCreate.Options.Fixed = TRUE;
    RamdiskCreate.Options.Readonly = FALSE;
    RamdiskCreate.Options.Hidden = FALSE;
    RamdiskCreate.Options.NoDriveLetter = FALSE;
    RamdiskCreate.Options.NoDosDevice = TRUE;
    RamdiskCreate.Options.ExportAsCd = FALSE;

    if (OffsetSpecified)
    {
        RamdiskCreate.DiskOffset = ParsedOffset;
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "IopStartRamdisk: parsed RDIMAGEOFFSET=%ld\n",
                   RamdiskCreate.DiskOffset);
    }

    RamdiskCreate.DiskLength.QuadPart -= RamdiskCreate.DiskOffset;

    if (LengthSpecified)
    {
        RamdiskCreate.DiskLength.QuadPart = (LONGLONG)ParsedLength;
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "IopStartRamdisk: parsed RDIMAGELENGTH=%I64d\n",
                   RamdiskCreate.DiskLength.QuadPart);
    }

    ASSERT((ULONGLONG)RamdiskCreate.DiskOffset < SelectedDescriptorLength);
    ASSERT((ULONGLONG)RamdiskCreate.DiskLength.QuadPart <=
           SelectedDescriptorLength - (ULONGLONG)RamdiskCreate.DiskOffset);

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_TRACE_LEVEL,
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
               DPFLTR_TRACE_LEVEL,
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
    SymbolicLinkName.Length = 38;
    SymbolicLinkName.MaximumLength = 38 + sizeof(UNICODE_NULL);
    SymbolicLinkName.Buffer = L"\\ArcName\\ramdisk(0)";

    //
    // Create the symbolic link
    //
    RtlInitUnicodeString(&ObjectName, SourceString);
    Status = IoCreateSymbolicLink(&SymbolicLinkName, &ObjectName);
    RtlFreeUnicodeString(&GuidString);
    if (!NT_SUCCESS(Status))
    {
        //
        // Bugcheck -- symlink create failed
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
               DPFLTR_TRACE_LEVEL,
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
    // Wait for ramdisk relations being initialized
    //

    KeWaitForSingleObject(&PiEnumerationFinished, Executive, KernelMode, FALSE, NULL);

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_TRACE_LEVEL,
               "IopStartRamdisk: ramdisk initialization complete\n");

    //
    // We made it
    //
    return STATUS_SUCCESS;
}
