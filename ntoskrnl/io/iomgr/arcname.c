/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/io/iomgr/arcname.c
* PURPOSE:         ARC Path Initialization Functions
* PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
*                  Eric Kohl
*                  Pierre Schweitzer (pierre.schweitzer@reactos.org)
*/

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <stdarg.h>
#include <limits.h>
#include <ntstrsafe.h>
#define NDEBUG
#include <debug.h>

#define ARC_TRACE(fmt, ...) DPRINT("ARC: " fmt, ##__VA_ARGS__)
#define ARC_WARN(fmt, ...) DPRINT1("ARC: " fmt, ##__VA_ARGS__)

static
NTSTATUS
IopFormatString(
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...);

static
BOOLEAN
IopParseArcNumberComponent(
    _In_reads_or_z_(MAXULONG) PCSTR String,
    _Out_ PULONG Value)
{
    ULONG Result = 0;
    BOOLEAN HaveDigit = FALSE;
    PCSTR Ptr = String;

    while ((*Ptr >= '0') && (*Ptr <= '9'))
    {
        Result = (Result * 10) + (ULONG)(*Ptr - '0');
        Ptr++;
        HaveDigit = TRUE;
    }

    if (!HaveDigit || (*Ptr != ')'))
    {
        return FALSE;
    }

    *Value = Result;
    return TRUE;
}

static
BOOLEAN
IopExtractArcDiskNumbers(
    _In_z_ PCSTR ArcName,
    _Out_ PULONG DiskNumber,
    _Out_ PULONG PartitionNumber)
{
    PCSTR DiskPtr;
    PCSTR PartitionPtr;

    DiskPtr = strstr(ArcName, "rdisk(");
    if (!DiskPtr)
    {
        DiskPtr = strstr(ArcName, "disk(");
    }

    PartitionPtr = strstr(ArcName, "partition(");

    if (!DiskPtr || !PartitionPtr)
    {
        return FALSE;
    }

    if (DiskPtr[0] == 'r')
    {
        DiskPtr += strlen("rdisk(");
    }
    else
    {
        DiskPtr += strlen("disk(");
    }

    if (!IopParseArcNumberComponent(DiskPtr, DiskNumber))
    {
        return FALSE;
    }

    PartitionPtr += strlen("partition(");
    if (!IopParseArcNumberComponent(PartitionPtr, PartitionNumber))
    {
        return FALSE;
    }

    return TRUE;
}

static
NTSTATUS
IopCreateArcBootAliasFallback(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ BOOLEAN RamdiskBoot)
{
    CHAR ArcBuffer[128];
    ANSI_STRING ArcAnsi;
    UNICODE_STRING ArcUnicode;
    UNICODE_STRING Candidates[8];
    ULONG CandidateCount = 0;
    WCHAR HarddiskBuffer[64];
    UNICODE_STRING HarddiskString;
    UNICODE_STRING RamdiskDeviceString = RTL_CONSTANT_STRING(L"\\Device\\Ramdisk0");
    BOOLEAN RamdiskCandidatePresent = FALSE;
    BOOLEAN RamdiskInitialized = FALSE;
    ULONG DiskNumber;
    ULONG PartitionNumber;
    NTSTATUS Status;

    ARC_WARN("CreateArcNames fallback enter Boot=%s RamdiskBoot=%d\n",
             LoaderBlock->ArcBootDeviceName,
             RamdiskBoot);

    Status = IopFormatString(ArcBuffer,
                             sizeof(ArcBuffer),
                             "\\ArcName\\%s",
                             LoaderBlock->ArcBootDeviceName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlInitAnsiString(&ArcAnsi, ArcBuffer);
    Status = RtlAnsiStringToUnicodeString(&ArcUnicode, &ArcAnsi, TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (RamdiskBoot && CandidateCount < RTL_NUMBER_OF(Candidates))
    {
        RtlInitUnicodeString(&Candidates[CandidateCount++], L"\\Device\\Ramdisk0");
        RamdiskCandidatePresent = TRUE;
    }

    if (CandidateCount < RTL_NUMBER_OF(Candidates))
    {
        RtlInitUnicodeString(&Candidates[CandidateCount++], L"\\Device\\CdRom0");
    }

    if (!RamdiskBoot && CandidateCount < RTL_NUMBER_OF(Candidates))
    {
        /* Consider the ramdisk even if the boot path isn't explicitly ramdisk. */
        RtlInitUnicodeString(&Candidates[CandidateCount++], L"\\Device\\Ramdisk0");
    }

    if (IopExtractArcDiskNumbers(LoaderBlock->ArcBootDeviceName,
                                 &DiskNumber,
                                 &PartitionNumber))
    {
        Status = RtlStringCbPrintfW(HarddiskBuffer,
                                    sizeof(HarddiskBuffer),
                                    L"\\Device\\Harddisk%lu\\Partition%lu",
                                    DiskNumber,
                                    PartitionNumber);
        if (NT_SUCCESS(Status))
        {
            HarddiskString.Buffer = HarddiskBuffer;
            HarddiskString.Length = (USHORT)(wcslen(HarddiskBuffer) * sizeof(WCHAR));
            HarddiskString.MaximumLength = sizeof(HarddiskBuffer);

            if (CandidateCount < RTL_NUMBER_OF(Candidates))
            {
                Candidates[CandidateCount++] = HarddiskString;
            }
        }
    }

    if (CandidateCount < RTL_NUMBER_OF(Candidates))
    {
        RtlInitUnicodeString(&Candidates[CandidateCount++], L"\\Device\\Harddisk0\\Partition1");
    }

    if (CandidateCount < RTL_NUMBER_OF(Candidates))
    {
        RtlInitUnicodeString(&Candidates[CandidateCount++], L"\\Device\\HarddiskVolume1");
    }

    Status = STATUS_UNSUCCESSFUL;
    for (ULONG Index = 0; Index < CandidateCount; Index++)
    {
        UNICODE_STRING *TargetString = &Candidates[Index];
        PFILE_OBJECT FileObject;
        PDEVICE_OBJECT DeviceObject;
        NTSTATUS QueryStatus;
        NTSTATUS LinkStatus;

RetryOpen:
        QueryStatus = IoGetDeviceObjectPointer(TargetString,
                                               FILE_READ_ATTRIBUTES,
                                               &FileObject,
                                               &DeviceObject);
        if (!NT_SUCCESS(QueryStatus))
        {
            if (!RamdiskInitialized &&
                RtlEqualUnicodeString(TargetString, &RamdiskDeviceString, TRUE))
            {
                RamdiskInitialized = TRUE;
                ARC_WARN("CreateArcNames fallback observed missing %wZ; proceeding without ramdisk initialization\n",
                         TargetString);
            }

            ARC_WARN("CreateArcNames fallback failed to open %wZ (0x%08lx)\n",
                     TargetString,
                     QueryStatus);
            continue;
        }

        ObDereferenceObject(FileObject);

        LinkStatus = IoAssignArcName(&ArcUnicode, TargetString);
        if (NT_SUCCESS(LinkStatus) ||
            LinkStatus == STATUS_OBJECT_NAME_EXISTS ||
            LinkStatus == STATUS_OBJECT_NAME_COLLISION)
        {
            ARC_WARN("CreateArcNames fallback mapped %s -> %wZ\n",
                     LoaderBlock->ArcBootDeviceName,
                     TargetString);
            Status = STATUS_SUCCESS;
            break;
        }

        ARC_WARN("CreateArcNames fallback IoAssignArcName failed (0x%08lx) for %wZ\n",
                 LinkStatus,
                 TargetString);
    }

    ARC_WARN("CreateArcNames fallback status=0x%08lx RamdiskCandidatePresent=%d\n",
             Status,
             RamdiskCandidatePresent);

    if (!NT_SUCCESS(Status) && RamdiskBoot && RamdiskCandidatePresent)
    {
        UNICODE_STRING RamdiskTarget;
        NTSTATUS LinkStatus;

        ARC_WARN("CreateArcNames fallback attempting direct map to \\Device\\Ramdisk0\n");
        RtlInitUnicodeString(&RamdiskTarget, L"\\Device\\Ramdisk0");
        LinkStatus = IoAssignArcName(&ArcUnicode, &RamdiskTarget);
        if (NT_SUCCESS(LinkStatus) ||
            LinkStatus == STATUS_OBJECT_NAME_EXISTS ||
            LinkStatus == STATUS_OBJECT_NAME_COLLISION)
        {
            ARC_WARN("CreateArcNames fallback mapped %s -> %wZ (direct)\n",
                     LoaderBlock->ArcBootDeviceName,
                     &RamdiskTarget);
            Status = STATUS_SUCCESS;
        }
        else
        {
            ARC_WARN("CreateArcNames fallback direct IoAssignArcName failed (0x%08lx) for %wZ\n",
                     LinkStatus,
                     &RamdiskTarget);
        }
    }

    RtlFreeUnicodeString(&ArcUnicode);
    return Status;
}

/* GLOBALS *******************************************************************/

/* Persist for the life of the kernel; allocated during boot and never freed. */
UNICODE_STRING IoArcHalDeviceName, IoArcBootDeviceName;
/* Loader ARC name is referenced only during boot while IRQL is low. */
PCHAR IoLoaderArcBootDeviceName;

static
NTSTATUS
IopFormatString(
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    NTSTATUS Status;
    va_list Args;

    va_start(Args, Format);
    Status = RtlStringCbVPrintfA(Buffer, BufferSize, Format, Args);
    va_end(Args);

    return Status;
}

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesCd(IN PLOADER_PARAMETER_BLOCK LoaderBlock);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesDisk(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN BOOLEAN SingleDisk,
                      OUT PBOOLEAN FoundBoot);

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNames(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    SIZE_T Length;
    NTSTATUS Status;
    NTSTATUS DiskStatus;
    CHAR Buffer[128];
    BOOLEAN SingleDisk;
    BOOLEAN FoundBoot = FALSE;
    UNICODE_STRING SystemDevice, LoaderPathNameW, BootDeviceName;
    PARC_DISK_INFORMATION ArcDiskInfo = LoaderBlock->ArcDiskInformation;
    PLIST_ENTRY ListHead;
    ANSI_STRING ArcSystemString, ArcString, LanmanRedirector, LoaderPathNameA;
    BOOLEAN RamdiskBoot = (_strnicmp(LoaderBlock->ArcBootDeviceName, "ramdisk(0)", 10) == 0);

    ListHead = &ArcDiskInfo->DiskSignatureListHead;
    /* Check if the list contains exactly one disk entry. */
    SingleDisk = (ListHead->Flink != ListHead) && (ListHead->Flink->Flink == ListHead);

    ARC_TRACE("CreateArcNames start Boot=%s Hal=%s SingleDisk=%d RamdiskBoot=%d\n",
              LoaderBlock->ArcBootDeviceName,
              LoaderBlock->ArcHalDeviceName,
              SingleDisk,
              RamdiskBoot);

    /* Create the global HAL partition name */
    Status = IopFormatString(Buffer,
                             sizeof(Buffer),
                             "\\ArcName\\%s",
                             LoaderBlock->ArcHalDeviceName);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlInitAnsiString(&ArcString, Buffer);
    Status = RtlAnsiStringToUnicodeString(&IoArcHalDeviceName, &ArcString, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Create the global system partition name */
    Status = IopFormatString(Buffer,
                             sizeof(Buffer),
                             "\\ArcName\\%s",
                             LoaderBlock->ArcBootDeviceName);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlInitAnsiString(&ArcString, Buffer);
    Status = RtlAnsiStringToUnicodeString(&IoArcBootDeviceName, &ArcString, TRUE);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Allocate memory for the string */
    Length = strlen(LoaderBlock->ArcBootDeviceName) + sizeof(ANSI_NULL);
    IoLoaderArcBootDeviceName = ExAllocatePoolWithTag(PagedPool,
                                                      Length,
                                                      TAG_IO);
    if (IoLoaderArcBootDeviceName)
    {
        /* Copy the name */
        RtlCopyMemory(IoLoaderArcBootDeviceName,
                      LoaderBlock->ArcBootDeviceName,
                      Length);
    }

    /* Check if we only found a disk, but we're booting from CD-ROM */
    if ((SingleDisk) && strstr(LoaderBlock->ArcBootDeviceName, "cdrom"))
    {
        /* Then disable single-disk mode, since there's a CD drive out there */
        SingleDisk = FALSE;
        ARC_TRACE("CreateArcNames detected boot-from-CD, forcing multi-disk enumeration\n");
    }

    /* Build the boot strings */
    RtlInitAnsiString(&ArcSystemString, LoaderBlock->ArcHalDeviceName);

    /* If we are doing remote booting */
    if (IoRemoteBootClient)
    {
        /* Yes, we have found boot device */
        FoundBoot = TRUE;

        /* Get NT device name */
        RtlInitAnsiString(&LanmanRedirector, "\\Device\\LanmanRedirector");
        Status = RtlAnsiStringToUnicodeString(&SystemDevice, &LanmanRedirector, TRUE);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        /* Get ARC booting device name (in net(0) something) */
        Status = IopFormatString(Buffer,
                                 sizeof(Buffer),
                                 "\\ArcName\\%s",
                                 LoaderBlock->ArcBootDeviceName);
        if (!NT_SUCCESS(Status))
        {
            RtlFreeUnicodeString(&SystemDevice);
            return Status;
        }
        RtlInitAnsiString(&ArcString, Buffer);
        Status = RtlAnsiStringToUnicodeString(&BootDeviceName, &ArcString, TRUE);
        if (NT_SUCCESS(Status))
        {
            /* Map ARC to NT name */
            NTSTATUS LinkStatus = IoAssignArcName(&BootDeviceName, &SystemDevice);
            if (!NT_SUCCESS(LinkStatus))
            {
                ARC_WARN("IoAssignArcName failed (0x%08lx) for %wZ -> %wZ\n",
                         LinkStatus,
                         &BootDeviceName,
                         &SystemDevice);
            }
            RtlFreeUnicodeString(&BootDeviceName);

            /* Now, get loader path name */
            RtlInitAnsiString(&LoaderPathNameA, LoaderBlock->NtHalPathName);
            Status = RtlAnsiStringToUnicodeString(&LoaderPathNameW, &LoaderPathNameA, TRUE);
            if (!NT_SUCCESS(Status))
            {
                RtlFreeUnicodeString(&SystemDevice);
                return Status;
            }

            /* And set it has system partition */
            IopStoreSystemPartitionInformation(&SystemDevice, &LoaderPathNameW);
            RtlFreeUnicodeString(&LoaderPathNameW);
        }

        RtlFreeUnicodeString(&SystemDevice);

        /* Don't quit here, even if everything went fine!
         * We need IopCreateArcNamesDisk to properly map
         * devices with symlinks.
         * It will return success if the mapping process went fine
         * even if it didn't find boot device.
         * It won't reset boot device finding status as well.
         */
    }

    /* Loop every disk and try to find boot disk */
    DiskStatus = IopCreateArcNamesDisk(LoaderBlock, SingleDisk, &FoundBoot);
    Status = DiskStatus;
    ARC_TRACE("CreateArcNames disk phase -> Status=0x%08lx FoundBoot=%d\n",
              DiskStatus,
              FoundBoot);

    /* Try the CD path if the boot disk was not found or disk mapping failed */
    if (!FoundBoot || !NT_SUCCESS(DiskStatus))
    {
        NTSTATUS CdStatus;

        if (!NT_SUCCESS(DiskStatus))
        {
            ARC_WARN("CreateArcNames disk phase failed (Status=0x%08lx); attempting CD enumeration\n",
                     DiskStatus);
        }

        CdStatus = IopCreateArcNamesCd(LoaderBlock);
        ARC_TRACE("CreateArcNames cd phase -> Status=0x%08lx\n", CdStatus);

        /* Prefer a successful CD mapping result */
        if (NT_SUCCESS(CdStatus))
        {
            Status = CdStatus;
        }
        else if (!NT_SUCCESS(DiskStatus))
        {
            Status = CdStatus;
        }
    }

    if (!NT_SUCCESS(Status) || !FoundBoot)
    {
        NTSTATUS FallbackStatus;

        FallbackStatus = IopCreateArcBootAliasFallback(LoaderBlock, RamdiskBoot);
        if (NT_SUCCESS(FallbackStatus))
        {
            Status = STATUS_SUCCESS;
            FoundBoot = TRUE;
        }
        else
        {
            ARC_WARN("CreateArcNames fallback mapping failed (0x%08lx)\n", FallbackStatus);
        }
    }

    /*
     * When booting from a ramdisk, tolerate missing physical media
     * (e.g. legacy IDE ports with no attached disks/CDs). Treat
     * STATUS_OBJECT_PATH_NOT_FOUND as success so initialization
     * continues, since the ramdisk provides the boot volume.
     */
    if (!NT_SUCCESS(Status) && RamdiskBoot)
    {
        BOOLEAN mapped = FALSE;
        BOOLEAN tolerateMissingMedia = FALSE;
        NTSTATUS originalStatus = Status;

        if (Status == STATUS_OBJECT_PATH_NOT_FOUND ||
            Status == STATUS_OBJECT_NAME_NOT_FOUND ||
            Status == STATUS_NO_SUCH_DEVICE)
        {
            ARC_WARN("CreateArcNames tolerating disk/CD lookup failure (Status=0x%08lx) due to ramdisk boot; attempting direct ramdisk map\n",
                     Status);
            tolerateMissingMedia = TRUE;
        }

        /* Try to bind the ARC path to the ramdisk device explicitly */
        {
            ANSI_STRING ArcNameAnsi;
            UNICODE_STRING ArcNameUnicode;
            UNICODE_STRING RamdiskDevice;
            NTSTATUS formatStatus;

            formatStatus = IopFormatString(Buffer,
                                           sizeof(Buffer),
                                           "\\ArcName\\%s",
                                           LoaderBlock->ArcBootDeviceName);
            if (NT_SUCCESS(formatStatus))
            {
                RtlInitAnsiString(&ArcNameAnsi, Buffer);
                formatStatus = RtlAnsiStringToUnicodeString(&ArcNameUnicode, &ArcNameAnsi, TRUE);
                if (NT_SUCCESS(formatStatus))
                {
                    NTSTATUS linkStatus;

                    RtlInitUnicodeString(&RamdiskDevice, L"\\Device\\Ramdisk0");
                    linkStatus = IoAssignArcName(&ArcNameUnicode, &RamdiskDevice);
                    if (NT_SUCCESS(linkStatus))
                    {
                        ARC_WARN("CreateArcNames mapped %s to \\Device\\Ramdisk0 for ramdisk boot\n",
                                 LoaderBlock->ArcBootDeviceName);
                        Status = STATUS_SUCCESS;
                        FoundBoot = TRUE;
                        mapped = TRUE;
                    }
                    else
                    {
                        ARC_WARN("CreateArcNames ramdisk fallback IoAssignArcName failed (0x%08lx)\n",
                                 linkStatus);
                        Status = linkStatus;
                    }

                    RtlFreeUnicodeString(&ArcNameUnicode);
                }
                else
                {
                    Status = formatStatus;
                }
            }
            else
            {
                Status = formatStatus;
            }
        }

        if (!mapped)
        {
            if (tolerateMissingMedia)
            {
                ARC_WARN("CreateArcNames ramdisk fallback could not establish ARC alias; preserving failure 0x%08lx\n",
                         originalStatus);
                Status = originalStatus;
            }
        }
    }

    /* Return success */
    ARC_TRACE("CreateArcNames done -> Status=0x%08lx FoundBoot=%d\n",
              Status,
              FoundBoot);
    return Status;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesCd(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PLIST_ENTRY NextEntry;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    LARGE_INTEGER StartingOffset;
    IO_STATUS_BLOCK IoStatusBlock;
    PULONG PartitionBuffer = NULL;
    CHAR Buffer[128], ArcBuffer[128];
    BOOLEAN NotEnabledPresent = FALSE;
    BOOLEAN ArcLinkCreated = FALSE;
    BOOLEAN LinkAttempted = FALSE;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    ANSI_STRING DeviceStringA, ArcNameStringA, BootArcStringA, CandidateArcStringA;
    PWSTR SymbolicLinkList, lSymbolicLinkList;
    PARC_DISK_SIGNATURE ArcDiskSignature = NULL;
    UNICODE_STRING DeviceStringW, ArcNameStringW;
    ULONG DiskNumber, CdRomCount, CheckSum, i, EnabledDisks = 0;
    NTSTATUS LastLinkStatus = STATUS_SUCCESS;
    PARC_DISK_INFORMATION ArcDiskInformation = LoaderBlock->ArcDiskInformation;

    /* Get all the Cds present in the system */
    CdRomCount = IoGetConfigurationInformation()->CdRomCount;
    ARC_TRACE("CreateArcNamesCd start, reported CdRomCount=%lu\n", CdRomCount);

    /* Get enabled Cds and check if result matches
     * For the record, enabled Cds (or even disk) are Cds/disks
     * that have been successfully handled by MountMgr driver
     * and that already own their device name. This is the "new" way
     * to handle them, that came with NT5.
     * Currently, Windows 2003 provides an ARC names creation based
     * on both enabled drives and not enabled drives (lack from
     * the driver).
     * Given the current ReactOS state, that's good for us.
     * To sum up, this is NOT a hack or whatsoever.
     */
    Status = IopFetchConfigurationInformation(&SymbolicLinkList,
                                              GUID_DEVINTERFACE_CDROM,
                                              CdRomCount,
                                              &EnabledDisks);
    if (!NT_SUCCESS(Status))
    {
        NotEnabledPresent = TRUE;
        ARC_WARN("IopFetchConfigurationInformation(CD) failed 0x%08lx; will probe legacy paths\n",
                 Status);
    }
    /* Save symbolic link list address in order to free it after */
    lSymbolicLinkList = SymbolicLinkList;
    /* For the moment, we won't fail */
    Status = STATUS_SUCCESS;

    RtlInitAnsiString(&BootArcStringA, LoaderBlock->ArcBootDeviceName);

    /* Browse all the ARC devices trying to find the one matching boot device */
    for (NextEntry = ArcDiskInformation->DiskSignatureListHead.Flink;
         NextEntry != &ArcDiskInformation->DiskSignatureListHead;
         NextEntry = NextEntry->Flink)
    {
        ArcDiskSignature = CONTAINING_RECORD(NextEntry,
                                             ARC_DISK_SIGNATURE,
                                             ListEntry);

        RtlInitAnsiString(&CandidateArcStringA, ArcDiskSignature->ArcName);
        if (RtlEqualString(&BootArcStringA, &CandidateArcStringA, TRUE))
        {
            break;
        }

        ArcDiskSignature = NULL;
    }

    /* Not found... Not booting from a Cd */
    if (!ArcDiskSignature)
    {
        ARC_TRACE("CreateArcNamesCd: no ARC entry matches boot device %s\n",
                  LoaderBlock->ArcBootDeviceName);
        goto Cleanup;
    }

    /* Allocate needed space for reading Cd */
    PartitionBuffer = ExAllocatePoolWithTag(NonPagedPoolCacheAligned, 2048, TAG_IO);
    if (!PartitionBuffer)
    {
        ARC_WARN("CreateArcNamesCd: failed allocating checksum buffer\n");
        /* Here, we fail, BUT we return success, some Microsoft joke */
        goto Cleanup;
    }

    /* If we have more enabled Cds, take that into account */
    if (EnabledDisks > CdRomCount)
    {
        CdRomCount = EnabledDisks;
    }

    /* If we'll have to browse for none enabled Cds, fix higher count */
    if (NotEnabledPresent && !EnabledDisks)
    {
        CdRomCount += 5;
    }

    /* Finally, if in spite of all that work, we still don't have Cds, fall back to \Device\CdRomX */
    if (!CdRomCount)
    {
        ULONG cdIndex;

        for (cdIndex = 0; cdIndex < 4; ++cdIndex)
        {
            Status = IopFormatString(Buffer,
                                     sizeof(Buffer),
                                     "\\Device\\CdRom%lu",
                                     cdIndex);
            if (!NT_SUCCESS(Status))
                continue;

            RtlInitAnsiString(&DeviceStringA, Buffer);
            Status = RtlAnsiStringToUnicodeString(&DeviceStringW, &DeviceStringA, TRUE);
            if (!NT_SUCCESS(Status))
                continue;

            Status = IoGetDeviceObjectPointer(&DeviceStringW,
                                              FILE_READ_ATTRIBUTES,
                                              &FileObject,
                                              &DeviceObject);
            if (NT_SUCCESS(Status))
            {
                Status = IopFormatString(ArcBuffer,
                                         sizeof(ArcBuffer),
                                         "\\ArcName\\%s",
                                         ArcDiskSignature->ArcName);
                if (NT_SUCCESS(Status))
                {
                    RtlInitAnsiString(&ArcNameStringA, ArcBuffer);
                    Status = RtlAnsiStringToUnicodeString(&ArcNameStringW, &ArcNameStringA, TRUE);
                    if (NT_SUCCESS(Status))
                    {
                        NTSTATUS LinkStatus = IoAssignArcName(&ArcNameStringW, &DeviceStringW);
                        if (NT_SUCCESS(LinkStatus))
                        {
                            ArcLinkCreated = TRUE;
                            LastLinkStatus = STATUS_SUCCESS;
                        }
                        else
                        {
                            ARC_WARN("CreateArcNamesCd fallback IoAssignArcName failed (0x%08lx) for %wZ -> %wZ\n",
                                     LinkStatus,
                                     &ArcNameStringW,
                                     &DeviceStringW);
                            LastLinkStatus = LinkStatus;
                        }
                        RtlFreeUnicodeString(&ArcNameStringW);
                    }
                }

                ObDereferenceObject(FileObject);
                if (ArcLinkCreated)
                {
                    RtlFreeUnicodeString(&DeviceStringW);
                    Status = STATUS_SUCCESS;
                    goto Cleanup;
                }
            }

            RtlFreeUnicodeString(&DeviceStringW);
        }

        if (!ArcLinkCreated && NT_SUCCESS(Status))
            Status = STATUS_NO_SUCH_DEVICE;

        goto Cleanup;
    }

    /* Start browsing Cds */
    for (DiskNumber = 0, EnabledDisks = 0; DiskNumber < CdRomCount; DiskNumber++)
    {
        /* Check if we have an enabled disk */
        if (lSymbolicLinkList && *lSymbolicLinkList != UNICODE_NULL)
        {
            /* Create its device name using first symbolic link */
            RtlInitUnicodeString(&DeviceStringW, lSymbolicLinkList);
            /* Then, update symbolic links list */
            lSymbolicLinkList += wcslen(lSymbolicLinkList) + (sizeof(UNICODE_NULL) / sizeof(WCHAR));

            /* Get its associated device object and file object */
            Status = IoGetDeviceObjectPointer(&DeviceStringW,
                                              FILE_READ_ATTRIBUTES,
                                              &FileObject,
                                              &DeviceObject);
            /* Failure? Good bye! */
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            /* Now, we'll ask the device its device number */
            Irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                                DeviceObject,
                                                NULL,
                                                0,
                                                &DeviceNumber,
                                                sizeof(DeviceNumber),
                                                FALSE,
                                                &Event,
                                                &IoStatusBlock);
            /* Failure? Good bye! */
            if (!Irp)
            {
                /* Dereference file object before leaving */
                ObDereferenceObject(FileObject);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }

            /* Call the driver, and wait for it if needed */
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            Status = IoCallDriver(DeviceObject, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = IoStatusBlock.Status;
            }
            if (!NT_SUCCESS(Status))
            {
                ObDereferenceObject(FileObject);
                goto Cleanup;
            }

            /* Finally, build proper device name */
            Status = IopFormatString(Buffer,
                                     sizeof(Buffer),
                                     "\\Device\\CdRom%lu",
                                     DeviceNumber.DeviceNumber);
            if (!NT_SUCCESS(Status))
            {
                ObDereferenceObject(FileObject);
                goto Cleanup;
            }
            RtlInitAnsiString(&DeviceStringA, Buffer);
            Status = RtlAnsiStringToUnicodeString(&DeviceStringW, &DeviceStringA, TRUE);
            if (!NT_SUCCESS(Status))
            {
                ObDereferenceObject(FileObject);
                goto Cleanup;
            }

            ARC_TRACE("CreateArcNamesCd: using enabled device %wZ (DeviceNumber=%lu)\n",
                      &DeviceStringW,
                      DeviceNumber.DeviceNumber);
        }
        else
        {
            /* Create device name for the cd */
            Status = IopFormatString(Buffer,
                                     sizeof(Buffer),
                                     "\\Device\\CdRom%lu",
                                     EnabledDisks++);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }
            RtlInitAnsiString(&DeviceStringA, Buffer);
            Status = RtlAnsiStringToUnicodeString(&DeviceStringW, &DeviceStringA, TRUE);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            ARC_TRACE("CreateArcNamesDisk: using enabled device %wZ (DeviceNumber=%lu)\n",
                      &DeviceStringW,
                      DeviceNumber.DeviceNumber);

            /* Get its device object */
            Status = IoGetDeviceObjectPointer(&DeviceStringW,
                                              FILE_READ_ATTRIBUTES,
                                              &FileObject,
                                              &DeviceObject);
            if (!NT_SUCCESS(Status))
            {
                ARC_WARN("CreateArcNamesDisk: IoGetDeviceObjectPointer failed for legacy device %wZ (status=0x%08lx)\n",
                         &DeviceStringW,
                         Status);
                RtlFreeUnicodeString(&DeviceStringW);
                goto Cleanup;
            }

            ARC_TRACE("CreateArcNamesCd: probing legacy device %wZ\n", &DeviceStringW);
        }

        /* Initiate data for reading cd and compute checksum */
        StartingOffset.QuadPart = 0x8000;
        CheckSum = 0;
        Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
                                           DeviceObject,
                                           PartitionBuffer,
                                           2048,
                                           &StartingOffset,
                                           &Event,
                                           &IoStatusBlock);
        if (Irp)
        {
            /* Call the driver, and wait for it if needed */
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            Status = IoCallDriver(DeviceObject, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = IoStatusBlock.Status;
            }

            /* If reading succeeded, compute checksum by adding data, 2048 bytes checksum */
            if (NT_SUCCESS(Status))
            {
                for (i = 0; i < 2048 / sizeof(ULONG); i++)
                {
                    CheckSum += PartitionBuffer[i];
                }
            }
        }

        /* Dereference file object */
        ObDereferenceObject(FileObject);

        /* If checksums are matching, we have the proper cd */
        if (CheckSum + ArcDiskSignature->CheckSum == 0)
        {
            /* Create ARC name */
            Status = IopFormatString(ArcBuffer,
                                     sizeof(ArcBuffer),
                                     "\\ArcName\\%s",
                                     LoaderBlock->ArcBootDeviceName);
            if (!NT_SUCCESS(Status))
            {
                RtlFreeUnicodeString(&DeviceStringW);
                goto Cleanup;
            }
            RtlInitAnsiString(&ArcNameStringA, ArcBuffer);
            Status = RtlAnsiStringToUnicodeString(&ArcNameStringW, &ArcNameStringA, TRUE);
            if (NT_SUCCESS(Status))
            {
                NTSTATUS LinkStatus;
                /* Create symbolic link */
                LinkAttempted = TRUE;
                LinkStatus = IoAssignArcName(&ArcNameStringW, &DeviceStringW);
                if (!NT_SUCCESS(LinkStatus))
                {
                    ARC_WARN("IoAssignArcName failed (0x%08lx) for %wZ -> %wZ\n",
                             LinkStatus,
                             &ArcNameStringW,
                             &DeviceStringW);
                    LastLinkStatus = LinkStatus;
                }
                else
                {
                    ArcLinkCreated = TRUE;
                    ARC_TRACE("CreateArcNamesCd: mapped %wZ -> %wZ\n",
                              &ArcNameStringW,
                              &DeviceStringW);
                }
                RtlFreeUnicodeString(&ArcNameStringW);
                if (ArcLinkCreated)
                {
                    ARC_TRACE("CreateArcNamesCd: boot device confirmed via checksum\n");
                }
            }

            /* Release string and continue enumeration unless mapping succeeded */
            RtlFreeUnicodeString(&DeviceStringW);
            if (ArcLinkCreated)
            {
                goto Cleanup;
            }

            continue;
        }

        /* Free string before trying another disk */
        RtlFreeUnicodeString(&DeviceStringW);
    }

Cleanup:
    if (PartitionBuffer)
    {
        ExFreePoolWithTag(PartitionBuffer, TAG_IO);
    }

    if (SymbolicLinkList)
    {
        ExFreePool(SymbolicLinkList);
    }

    if (!ArcLinkCreated && LinkAttempted && NT_SUCCESS(Status) && !NT_SUCCESS(LastLinkStatus))
    {
        Status = LastLinkStatus;
    }

    ARC_TRACE("CreateArcNamesCd done -> Status=0x%08lx ArcLinkCreated=%d LinkAttempted=%d\n",
              Status,
              ArcLinkCreated,
              LinkAttempted);
    return Status;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
IopCreateArcNamesDisk(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN BOOLEAN SingleDisk,
                      OUT PBOOLEAN FoundBoot)
{
    PIRP Irp;
    PVOID Data;
    KEVENT Event;
    NTSTATUS Status;
    PLIST_ENTRY NextEntry;
    PFILE_OBJECT FileObject;
    DISK_GEOMETRY DiskGeometry;
    PDEVICE_OBJECT DeviceObject;
    LARGE_INTEGER StartingOffset;
    PULONG PartitionBuffer = NULL;
    IO_STATUS_BLOCK IoStatusBlock;
    CHAR Buffer[128], ArcBuffer[128];
    BOOLEAN NotEnabledPresent = FALSE;
    BOOLEAN ArcLinkCreated = FALSE;
    BOOLEAN LinkAttempted = FALSE;
    STORAGE_DEVICE_NUMBER DeviceNumber;
    PARC_DISK_SIGNATURE ArcDiskSignature;
    PWSTR SymbolicLinkList, lSymbolicLinkList;
    PDRIVE_LAYOUT_INFORMATION_EX DriveLayout = NULL;
    UNICODE_STRING DeviceStringW, ArcNameStringW, HalPathStringW;
    ULONG DiskNumber, DiskCount, CheckSum, i, SigValue, EnabledDisks = 0;
    BOOLEAN SigVerified;
    NTSTATUS LastLinkStatus = STATUS_SUCCESS;
    PARC_DISK_INFORMATION ArcDiskInformation = LoaderBlock->ArcDiskInformation;
    ANSI_STRING ArcBootString, ArcSystemString, DeviceStringA, ArcNameStringA, HalPathStringA;

    /* Initialise device number */
    DeviceNumber.DeviceNumber = ULONG_MAX;
    /* Get all the disks present in the system */
    DiskCount = IoGetConfigurationInformation()->DiskCount;

    /* Get enabled disks and check if result matches */
    Status = IopFetchConfigurationInformation(&SymbolicLinkList,
                                              GUID_DEVINTERFACE_DISK,
                                              DiskCount,
                                              &EnabledDisks);
    if (!NT_SUCCESS(Status))
    {
        NotEnabledPresent = TRUE;
        ARC_WARN("IopFetchConfigurationInformation(Disk) failed 0x%08lx; will probe legacy disks\n",
                 Status);
    }

    ARC_TRACE("CreateArcNamesDisk start: DiskCount=%lu EnabledReported=%lu SingleDisk=%d NotEnabledFallback=%d\n",
              DiskCount,
              EnabledDisks,
              SingleDisk,
              NotEnabledPresent);

    /* Save symbolic link list address in order to free it after */
    lSymbolicLinkList = SymbolicLinkList;

    /* Build the boot strings */
    RtlInitAnsiString(&ArcBootString, LoaderBlock->ArcBootDeviceName);
    RtlInitAnsiString(&ArcSystemString, LoaderBlock->ArcHalDeviceName);

    /* If we have more enabled disks, take that into account */
    if (EnabledDisks > DiskCount)
    {
        DiskCount = EnabledDisks;
    }

    /* If we'll have to browse for none enabled disks, fix higher count */
    if (NotEnabledPresent && !EnabledDisks)
    {
        DiskCount += 20;
    }

    /* Finally, if in spite of all that work, we still don't have disks, leave */
    if (!DiskCount)
    {
        goto Cleanup;
    }

    /* Start browsing disks */
    for (DiskNumber = 0; DiskNumber < DiskCount; DiskNumber++)
    {
        ASSERT(DriveLayout == NULL);
        ARC_TRACE("CreateArcNamesDisk: probing disk%lu (symbolicAvailable=%d)\n",
                  DiskNumber,
                  (lSymbolicLinkList && *lSymbolicLinkList != UNICODE_NULL));

        /* Check if we have an enabled disk */
        if (lSymbolicLinkList && *lSymbolicLinkList != UNICODE_NULL)
        {
            /* Create its device name using first symbolic link */
            RtlInitUnicodeString(&DeviceStringW, lSymbolicLinkList);
            /* Then, update symbolic links list */
            lSymbolicLinkList += wcslen(lSymbolicLinkList) + (sizeof(UNICODE_NULL) / sizeof(WCHAR));

            /* Get its associated device object and file object */
            Status = IoGetDeviceObjectPointer(&DeviceStringW,
                                              FILE_READ_ATTRIBUTES,
                                              &FileObject,
                                              &DeviceObject);
            if (NT_SUCCESS(Status))
            {
                /* Now, we'll ask the device its device number */
                Irp = IoBuildDeviceIoControlRequest(IOCTL_STORAGE_GET_DEVICE_NUMBER,
                                                    DeviceObject,
                                                    NULL,
                                                    0,
                                                    &DeviceNumber,
                                                    sizeof(DeviceNumber),
                                                    FALSE,
                                                    &Event,
                                                    &IoStatusBlock);
                /* Missing resources is a shame... No need to go farther */
                if (!Irp)
                {
                    ObDereferenceObject(FileObject);
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    goto Cleanup;
                }

                /* Call the driver, and wait for it if needed */
                KeInitializeEvent(&Event, NotificationEvent, FALSE);
                Status = IoCallDriver(DeviceObject, Irp);
                if (Status == STATUS_PENDING)
                {
                    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                    Status = IoStatusBlock.Status;
                }

                /* If we didn't get the appropriate data, just skip that disk */
                if (!NT_SUCCESS(Status))
                {
                    ARC_WARN("CreateArcNamesDisk: IOCTL_STORAGE_GET_DEVICE_NUMBER failed for %wZ (status=0x%08lx)\n",
                             &DeviceStringW,
                             Status);
                    ObDereferenceObject(FileObject);
                    continue;
                }
            }
            else
            {
                ARC_WARN("CreateArcNamesDisk: IoGetDeviceObjectPointer failed (status=0x%08lx) for enabled path %wZ\n",
                         Status,
                         &DeviceStringW);
            }

            /* End of enabled disks enumeration */
            if (NotEnabledPresent && *lSymbolicLinkList == UNICODE_NULL)
            {
                /* No enabled disk worked, reset field */
                if (DeviceNumber.DeviceNumber == ULONG_MAX)
                {
                    DeviceNumber.DeviceNumber = 0;
                }

                /* Update disk number to enable the following not enabled disks */
                if (DeviceNumber.DeviceNumber > DiskNumber)
                {
                    DiskNumber = DeviceNumber.DeviceNumber;
                }

                /* Increase a bit more */
                DiskCount = DiskNumber + 20;
            }
        }
        else
        {
            /* Create device name for the disk */
            Status = IopFormatString(Buffer,
                                     sizeof(Buffer),
                                     "\\Device\\Harddisk%lu\\Partition0",
                                     DiskNumber);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }
            RtlInitAnsiString(&DeviceStringA, Buffer);
            Status = RtlAnsiStringToUnicodeString(&DeviceStringW, &DeviceStringA, TRUE);
            if (!NT_SUCCESS(Status))
            {
                goto Cleanup;
            }

            /* Get its device object */
            Status = IoGetDeviceObjectPointer(&DeviceStringW,
                                              FILE_READ_ATTRIBUTES,
                                              &FileObject,
                                              &DeviceObject);

            ARC_TRACE("CreateArcNamesDisk: probing legacy device %wZ\n", &DeviceStringW);
            if (!NT_SUCCESS(Status))
            {
                ARC_WARN("CreateArcNamesDisk: IoGetDeviceObjectPointer failed (status=0x%08lx) for legacy path %wZ\n",
                         Status,
                         &DeviceStringW);
            }

            RtlFreeUnicodeString(&DeviceStringW);
            /* This is a security measure, to ensure DiskNumber will be used */
            DeviceNumber.DeviceNumber = ULONG_MAX;
        }

        /* Something failed somewhere earlier, just skip the disk */
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        /* Let's ask the disk for its geometry */
        Irp = IoBuildDeviceIoControlRequest(IOCTL_DISK_GET_DRIVE_GEOMETRY,
                                            DeviceObject,
                                            NULL,
                                            0,
                                            &DiskGeometry,
                                            sizeof(DiskGeometry),
                                            FALSE,
                                            &Event,
                                            &IoStatusBlock);
        /* Missing resources is a shame... No need to go farther */
        if (!Irp)
        {
            ObDereferenceObject(FileObject);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        /* Call the driver, and wait for it if needed */
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }
        /* Failure, skip disk */
        if (!NT_SUCCESS(Status))
        {
            ARC_WARN("CreateArcNamesDisk: IOCTL_DISK_GET_DRIVE_GEOMETRY failed (status=0x%08lx)\n",
                     Status);
            ObDereferenceObject(FileObject);
            continue;
        }

        /* Read the partition table */
        Status = IoReadPartitionTableEx(DeviceObject,
                                        &DriveLayout);
        if (!NT_SUCCESS(Status))
        {
            ARC_WARN("CreateArcNamesDisk: IoReadPartitionTableEx failed (status=0x%08lx)\n",
                     Status);
            ObDereferenceObject(FileObject);
            continue;
        }

        /* Ensure we have at least 512 bytes per sector */
        if (DiskGeometry.BytesPerSector < 512)
        {
            DiskGeometry.BytesPerSector = 512;
        }

        /* Check MBR type against EZ Drive type */
        StartingOffset.QuadPart = 0;
        HalExamineMBR(DeviceObject, DiskGeometry.BytesPerSector, 0x55, &Data);
        if (Data)
        {
            /* If MBR is of the EZ Drive type, we'll read after it */
            StartingOffset.QuadPart = DiskGeometry.BytesPerSector;
            ExFreePool(Data);
        }

        /* Allocate for reading enough data for checksum */
        PartitionBuffer = ExAllocatePoolWithTag(NonPagedPoolCacheAligned, DiskGeometry.BytesPerSector, TAG_IO);
        if (!PartitionBuffer)
        {
            ObDereferenceObject(FileObject);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        /* Read the first sector for computing checksum */
        Irp = IoBuildSynchronousFsdRequest(IRP_MJ_READ,
                                           DeviceObject,
                                           PartitionBuffer,
                                           DiskGeometry.BytesPerSector,
                                           &StartingOffset,
                                           &Event,
                                           &IoStatusBlock);
        if (!Irp)
        {
            ExFreePoolWithTag(PartitionBuffer, TAG_IO);
            ObDereferenceObject(FileObject);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        /* Call the driver to perform reading */
        KeInitializeEvent(&Event, NotificationEvent, FALSE);
        Status = IoCallDriver(DeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }

        /* If reading succeeded, calculate checksum by adding data */
        if (NT_SUCCESS(Status))
        {
            for (i = 0, CheckSum = 0; i < 512 / sizeof(ULONG); i++)
            {
                CheckSum += PartitionBuffer[i];
            }
        }

        /* Release now unnecessary resources */
        ExFreePoolWithTag(PartitionBuffer, TAG_IO);
        ObDereferenceObject(FileObject);

        /* If we failed, release drive layout before going to next disk */
        if (!NT_SUCCESS(Status))
        {
            ExFreePool(DriveLayout);
            DriveLayout = NULL;
            continue;
        }

        /* Browse each ARC disk */
        for (NextEntry = ArcDiskInformation->DiskSignatureListHead.Flink;
             NextEntry != &ArcDiskInformation->DiskSignatureListHead;
             NextEntry = NextEntry->Flink)
        {
            ArcDiskSignature = CONTAINING_RECORD(NextEntry,
                                                 ARC_DISK_SIGNATURE,
                                                 ListEntry);

            SigValue = 0;
            SigVerified = IopVerifyDiskSignature(DriveLayout, ArcDiskSignature, &SigValue);
            ARC_TRACE("CreateArcNamesDisk: disk%lu ARC=%s SigVerified=%d SigValue=0x%08lx CheckSum=0x%08lx\n",
                      (DeviceNumber.DeviceNumber != ULONG_MAX) ? DeviceNumber.DeviceNumber : DiskNumber,
                      ArcDiskSignature->ArcName,
                      SigVerified,
                      SigValue,
                      CheckSum);

            /*
             * If this is the only MBR disk in the ARC list and detected
             * in the device tree, just go ahead and create the ArcName link.
             * Otherwise, check whether the signatures and checksums match
             * before creating the ArcName link.
             */
            if ((SingleDisk && (DiskCount == 1) &&
                 (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR)) ||
                (SigVerified && (ArcDiskSignature->CheckSum + CheckSum == 0)))
            {
                /* Create device name */
                Status = IopFormatString(Buffer,
                                         sizeof(Buffer),
                                         "\\Device\\Harddisk%lu\\Partition0",
                                         (DeviceNumber.DeviceNumber != ULONG_MAX) ? DeviceNumber.DeviceNumber : DiskNumber);
                if (!NT_SUCCESS(Status))
                {
                    goto Cleanup;
                }
                RtlInitAnsiString(&DeviceStringA, Buffer);
                Status = RtlAnsiStringToUnicodeString(&DeviceStringW, &DeviceStringA, TRUE);
                if (!NT_SUCCESS(Status))
                {
                    goto Cleanup;
                }

                /* Create ARC name */
                Status = IopFormatString(ArcBuffer,
                                         sizeof(ArcBuffer),
                                         "\\ArcName\\%s",
                                         ArcDiskSignature->ArcName);
                if (!NT_SUCCESS(Status))
                {
                    RtlFreeUnicodeString(&DeviceStringW);
                    goto Cleanup;
                }
                RtlInitAnsiString(&ArcNameStringA, ArcBuffer);
                Status = RtlAnsiStringToUnicodeString(&ArcNameStringW, &ArcNameStringA, TRUE);
                if (!NT_SUCCESS(Status))
                {
                    RtlFreeUnicodeString(&DeviceStringW);
                    goto Cleanup;
                }

                NTSTATUS LinkStatus;
                /* Link both */
                LinkAttempted = TRUE;
                LinkStatus = IoAssignArcName(&ArcNameStringW, &DeviceStringW);
                if (!NT_SUCCESS(LinkStatus))
                {
                    ARC_WARN("IoAssignArcName failed (0x%08lx) for %wZ -> %wZ\n",
                             LinkStatus,
                             &ArcNameStringW,
                             &DeviceStringW);
                    LastLinkStatus = LinkStatus;
                }
                else
                {
                    ArcLinkCreated = TRUE;
                    ARC_TRACE("CreateArcNamesDisk: mapped %wZ -> %wZ\n",
                              &ArcNameStringW,
                              &DeviceStringW);
                }

                /* And release strings */
                RtlFreeUnicodeString(&ArcNameStringW);
                RtlFreeUnicodeString(&DeviceStringW);

                /* Now, browse each partition */
                ARC_TRACE("CreateArcNamesDisk: disk%lu partitions=%lu style=%lu\n",
                          (DeviceNumber.DeviceNumber != ULONG_MAX) ? DeviceNumber.DeviceNumber : DiskNumber,
                          DriveLayout->PartitionCount,
                          DriveLayout->PartitionStyle);
                for (i = 1; i <= DriveLayout->PartitionCount; i++)
                {
                    /* Create device name */
                    Status = IopFormatString(Buffer,
                                             sizeof(Buffer),
                                             "\\Device\\Harddisk%lu\\Partition%lu",
                                             (DeviceNumber.DeviceNumber != ULONG_MAX) ? DeviceNumber.DeviceNumber : DiskNumber,
                                             i);
                    if (!NT_SUCCESS(Status))
                    {
                        goto Cleanup;
                    }
                    RtlInitAnsiString(&DeviceStringA, Buffer);
                    Status = RtlAnsiStringToUnicodeString(&DeviceStringW, &DeviceStringA, TRUE);
                    if (!NT_SUCCESS(Status))
                    {
                        goto Cleanup;
                    }

                    PPARTITION_INFORMATION_EX PartitionEntry = &DriveLayout->PartitionEntry[i - 1];
                    if (PartitionEntry->PartitionLength.QuadPart == 0)
                    {
                        RtlFreeUnicodeString(&DeviceStringW);
                        continue;
                    }

                    /* Create partial ARC name */
                    Status = IopFormatString(ArcBuffer,
                                             sizeof(ArcBuffer),
                                             "%spartition(%lu)",
                                             ArcDiskSignature->ArcName,
                                             i);
                    if (!NT_SUCCESS(Status))
                    {
                        ARC_WARN("CreateArcNamesDisk: ARC partition name too long: %s\n",
                                 ArcDiskSignature->ArcName);
                        RtlFreeUnicodeString(&DeviceStringW);
                        goto Cleanup;
                    }
                    RtlInitAnsiString(&ArcNameStringA, ArcBuffer);

                    /* Is that boot device? */
                    if (RtlEqualString(&ArcNameStringA, &ArcBootString, TRUE))
                    {
                        ARC_TRACE("CreateArcNamesDisk: partition %s identified as boot device\n",
                                  ArcDiskSignature->ArcName);
                        *FoundBoot = TRUE;
                    }

                    /* Is that system partition? */
                    if (RtlEqualString(&ArcNameStringA, &ArcSystemString, TRUE))
                    {
                        /* Create HAL path name */
                        RtlInitAnsiString(&HalPathStringA, LoaderBlock->NtHalPathName);
                        Status = RtlAnsiStringToUnicodeString(&HalPathStringW, &HalPathStringA, TRUE);
                        if (!NT_SUCCESS(Status))
                        {
                            RtlFreeUnicodeString(&DeviceStringW);
                            goto Cleanup;
                        }

                        /* Then store those information to registry */
                        IopStoreSystemPartitionInformation(&DeviceStringW, &HalPathStringW);
                        RtlFreeUnicodeString(&HalPathStringW);
                    }

                    /* Create complete ARC name */
                    Status = IopFormatString(ArcBuffer,
                                             sizeof(ArcBuffer),
                                             "\\ArcName\\%spartition(%lu)",
                                             ArcDiskSignature->ArcName,
                                             i);
                    if (!NT_SUCCESS(Status))
                    {
                        RtlFreeUnicodeString(&DeviceStringW);
                        goto Cleanup;
                    }
                    RtlInitAnsiString(&ArcNameStringA, ArcBuffer);
                    Status = RtlAnsiStringToUnicodeString(&ArcNameStringW, &ArcNameStringA, TRUE);
                    if (!NT_SUCCESS(Status))
                    {
                        RtlFreeUnicodeString(&DeviceStringW);
                        goto Cleanup;
                    }

                    /* Link device name & ARC name */
                    NTSTATUS LinkStatus;
                    LinkAttempted = TRUE;
                    LinkStatus = IoAssignArcName(&ArcNameStringW, &DeviceStringW);
                    if (!NT_SUCCESS(LinkStatus))
                    {
                        ARC_WARN("IoAssignArcName failed (0x%08lx) for %wZ -> %wZ\n",
                                 LinkStatus,
                                 &ArcNameStringW,
                                 &DeviceStringW);
                        LastLinkStatus = LinkStatus;
                    }
                    else
                    {
                        ArcLinkCreated = TRUE;
                        ARC_TRACE("CreateArcNamesDisk: mapped %wZ -> %wZ (partition)\n",
                                  &ArcNameStringW,
                                  &DeviceStringW);
                    }

                    /* Release strings */
                    RtlFreeUnicodeString(&ArcNameStringW);
                    RtlFreeUnicodeString(&DeviceStringW);
                }
            }
            else
            {
                /* Debugging feedback: Warn in case there's a valid partition,
                 * a matching signature, BUT a non-matching checksum: this can
                 * be the sign of a duplicate signature, or even worse a virus
                 * played with the partition table. */
                if (ArcDiskSignature->ValidPartitionTable &&
                    SigVerified &&
                    (ArcDiskSignature->Signature == SigValue) &&
                    (ArcDiskSignature->CheckSum + CheckSum != 0))
                {
                    ARC_WARN("CreateArcNamesDisk: duplicate disk signature or mismatched checksum detected for %s\n",
                             ArcDiskSignature->ArcName);
                }
            }
        }

        /* Finally, release drive layout */
        ExFreePool(DriveLayout);
        DriveLayout = NULL;
    }

    if (NT_SUCCESS(Status) && !ArcLinkCreated && LinkAttempted && !NT_SUCCESS(LastLinkStatus))
    {
        Status = LastLinkStatus;
    }
    else if (NT_SUCCESS(Status))
    {
        Status = STATUS_SUCCESS;
    }

Cleanup:
    if (DriveLayout)
    {
        ExFreePool(DriveLayout);
    }

    if (SymbolicLinkList)
    {
        ExFreePool(SymbolicLinkList);
    }

    ARC_TRACE("CreateArcNamesDisk done -> Status=0x%08lx ArcLinkCreated=%d LinkAttempted=%d\n",
              Status,
              ArcLinkCreated,
              LinkAttempted);
    return Status;
}

CODE_SEG("INIT")
_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
NTAPI
IopReassignSystemRoot(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      OUT PANSI_STRING NtBootPath)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;
    CHAR Buffer[256];
    WCHAR ArcNameBuffer[64];
    ANSI_STRING TargetString, ArcString, TempString, NewTargetAnsi;
    UNICODE_STRING LinkName = {0}, TargetName = {0}, ArcName = {0}, NewTargetName = {0};
    HANDLE LinkHandle = NULL;
    ULONG ResultLength = 0;
    BOOLEAN ArcNameAllocated = FALSE;

    RtlInitEmptyAnsiString(&ArcString, NULL, 0);

    /* Create the Unicode name for the current ARC boot device */
    Status = IopFormatString(Buffer,
                             sizeof(Buffer),
                             "\\ArcName\\%s",
                             LoaderBlock->ArcBootDeviceName);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlInitAnsiString(&TargetString, Buffer);
    Status = RtlAnsiStringToUnicodeString(&TargetName, &TargetString, TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Initialize the attributes and open the link */
    InitializeObjectAttributes(&ObjectAttributes,
                               &TargetName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenSymbolicLinkObject(&LinkHandle,
                                      SYMBOLIC_LINK_ALL_ACCESS,
                                      &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&TargetName);
        return Status;
    }

    /* Query the current \SystemRoot link target */
    ArcName.Buffer = ArcNameBuffer;
    ArcName.Length = 0;
    ArcName.MaximumLength = sizeof(ArcNameBuffer);

    Status = NtQuerySymbolicLinkObject(LinkHandle, &ArcName, &ResultLength);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        if (ResultLength > USHRT_MAX)
        {
            Status = STATUS_NAME_TOO_LONG;
            goto Cleanup;
        }

        ArcName.Buffer = ExAllocatePoolWithTag(PagedPool, ResultLength, TAG_IO);
        if (!ArcName.Buffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        ArcNameAllocated = TRUE;
        ArcName.Length = 0;
        ArcName.MaximumLength = (USHORT)ResultLength;
        Status = NtQuerySymbolicLinkObject(LinkHandle, &ArcName, &ResultLength);
    }
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    /* Convert it to ANSI */
    Status = RtlUnicodeStringToAnsiString(&ArcString, &ArcName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    /* Close the link handle and free the name */
    ObCloseHandle(LinkHandle, KernelMode);
    LinkHandle = NULL;
    RtlFreeUnicodeString(&TargetName);
    TargetName.Buffer = NULL;

    /* Setup the system root name again */
    RtlInitAnsiString(&TempString, "\\SystemRoot");
    Status = RtlAnsiStringToUnicodeString(&LinkName, &TempString, TRUE);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    /* Open the symbolic link for it */
    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenSymbolicLinkObject(&LinkHandle,
                                      SYMBOLIC_LINK_ALL_ACCESS,
                                      &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    Status = NtMakeTemporaryObject(LinkHandle);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    ObCloseHandle(LinkHandle, KernelMode);
    LinkHandle = NULL;

    /* Now create the new name for it */
    Status = IopFormatString(Buffer,
                             sizeof(Buffer),
                             "%s%s",
                             ArcString.Buffer,
                             LoaderBlock->NtBootPathName);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    /* Remove any trailing path separators */
    if (*Buffer != ANSI_NULL)
    {
        SIZE_T Length = strlen(Buffer);
        while ((Length > 0) &&
               ((Buffer[Length - 1] == '\\') || (Buffer[Length - 1] == '/')))
        {
            Buffer[--Length] = ANSI_NULL;
        }
    }

    /* Copy it into the passed parameter */
    RtlInitAnsiString(&NewTargetAnsi, Buffer);
    if (NtBootPath->MaximumLength < NewTargetAnsi.Length + sizeof(ANSI_NULL))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }
    RtlCopyString(NtBootPath, &NewTargetAnsi);
    if (NtBootPath->Buffer && NtBootPath->MaximumLength > NtBootPath->Length)
    {
        NtBootPath->Buffer[NtBootPath->Length] = ANSI_NULL;
    }

    /* Setup the Unicode-name for the new symbolic link value */
    Status = RtlAnsiStringToUnicodeString(&NewTargetName, &NewTargetAnsi, TRUE);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    InitializeObjectAttributes(&ObjectAttributes,
                               &LinkName,
                               OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
                               NULL,
                               NULL);
    Status = NtCreateSymbolicLinkObject(&LinkHandle,
                                        SYMBOLIC_LINK_ALL_ACCESS,
                                        &ObjectAttributes,
                                        &NewTargetName);

Cleanup:
    if (LinkHandle)
    {
        ObCloseHandle(LinkHandle, KernelMode);
    }
    if (NewTargetName.Buffer)
    {
        RtlFreeUnicodeString(&NewTargetName);
    }
    if (LinkName.Buffer)
    {
        RtlFreeUnicodeString(&LinkName);
    }
    if (TargetName.Buffer)
    {
        RtlFreeUnicodeString(&TargetName);
    }
    if (ArcString.Buffer)
    {
        RtlFreeAnsiString(&ArcString);
    }
    if (ArcNameAllocated && ArcName.Buffer)
    {
        ExFreePool(ArcName.Buffer);
    }

    return Status;
}

BOOLEAN
IopVerifyDiskSignature(
    _In_ PDRIVE_LAYOUT_INFORMATION_EX DriveLayout,
    _In_ PARC_DISK_SIGNATURE ArcDiskSignature,
    _Out_ PULONG Signature)
{
    /* Fail if the partition table is invalid */
    if (!ArcDiskSignature->ValidPartitionTable)
        return FALSE;

    /* If the partition style is MBR */
    if (DriveLayout->PartitionStyle == PARTITION_STYLE_MBR)
    {
        /* Check the MBR signature */
        if (DriveLayout->Mbr.Signature == ArcDiskSignature->Signature)
        {
            /* And return it */
            if (Signature)
                *Signature = DriveLayout->Mbr.Signature;
            return TRUE;
        }
    }
    /* If the partition style is GPT */
    else if (DriveLayout->PartitionStyle == PARTITION_STYLE_GPT)
    {
        /* Verify whether the signature is GPT and compare the GUID */
        if (ArcDiskSignature->IsGpt &&
            IsEqualGUID((PGUID)&ArcDiskSignature->GptSignature, &DriveLayout->Gpt.DiskId))
        {
            /* There is no signature to return, just zero it */
            if (Signature)
                *Signature = 0;
            return TRUE;
        }
    }

    /* If we get there, something went wrong, so fail */
    return FALSE;
}

/* EOF */
