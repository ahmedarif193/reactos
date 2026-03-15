/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel
 * FILE:            ntoskrnl/io/iomgr/iomgr.c
 * PURPOSE:         I/O Manager Initialization and Misc Utility Functions
 *
 * PROGRAMMERS:     David Welch (welch@mcmail.com)
 */

/* INCLUDES ****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

ULONG IopTraceLevel = 0;
BOOLEAN PnpSystemInit = FALSE;

VOID
NTAPI
IopTimerDispatch(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2
);

BOOLEAN
NTAPI
WmiInitialize(
    VOID);

/* DATA ********************************************************************/

POBJECT_TYPE IoDeviceObjectType = NULL;
POBJECT_TYPE IoFileObjectType = NULL;
extern POBJECT_TYPE IoControllerObjectType;
BOOLEAN IoCountOperations = TRUE;
ULONG IoReadOperationCount = 0;
LARGE_INTEGER IoReadTransferCount = {{0, 0}};
ULONG IoWriteOperationCount = 0;
LARGE_INTEGER IoWriteTransferCount = {{0, 0}};
ULONG IoOtherOperationCount = 0;
LARGE_INTEGER IoOtherTransferCount = {{0, 0}};
KSPIN_LOCK IoStatisticsLock = 0;
ULONG IopAutoReboot;
ULONG IopNumTriageDumpDataBlocks;
PVOID IopTriageDumpDataBlocks[64];

GENERIC_MAPPING IopFileMapping = {
    FILE_GENERIC_READ,
    FILE_GENERIC_WRITE,
    FILE_GENERIC_EXECUTE,
    FILE_ALL_ACCESS};

extern LIST_ENTRY ShutdownListHead;
extern LIST_ENTRY LastChanceShutdownListHead;
extern KSPIN_LOCK ShutdownListLock;
extern POBJECT_TYPE IoAdapterObjectType;
extern ERESOURCE IopDatabaseResource;
ERESOURCE IopSecurityResource;
extern ERESOURCE IopDriverLoadResource;
extern LIST_ENTRY IopDiskFileSystemQueueHead;
extern LIST_ENTRY IopCdRomFileSystemQueueHead;
extern LIST_ENTRY IopTapeFileSystemQueueHead;
extern LIST_ENTRY IopNetworkFileSystemQueueHead;
extern LIST_ENTRY DriverBootReinitListHead;
extern LIST_ENTRY DriverReinitListHead;
extern LIST_ENTRY IopFsNotifyChangeQueueHead;
extern LIST_ENTRY IopErrorLogListHead;
extern LIST_ENTRY IopTimerQueueHead;
extern KDPC IopTimerDpc;
extern KTIMER IopTimer;
extern KSPIN_LOCK IoStatisticsLock;
extern KSPIN_LOCK DriverReinitListLock;
extern KSPIN_LOCK DriverBootReinitListLock;
extern KSPIN_LOCK IopLogListLock;
extern KSPIN_LOCK IopTimerLock;

extern PDEVICE_OBJECT IopErrorLogObject;
extern BOOLEAN PnPBootDriversInitialized;

/* ARM64: Ensure proper SLIST_HEADER alignment */
DECLSPEC_ALIGN(16) GENERAL_LOOKASIDE IoLargeIrpLookaside;
DECLSPEC_ALIGN(16) GENERAL_LOOKASIDE IoSmallIrpLookaside;
DECLSPEC_ALIGN(16) GENERAL_LOOKASIDE IopMdlLookasideList;
extern GENERAL_LOOKASIDE IoCompletionPacketLookaside;

PLOADER_PARAMETER_BLOCK IopLoaderBlock;

/* INIT FUNCTIONS ************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
IopInitLookasideLists(VOID)
{
    ULONG LargeIrpSize, SmallIrpSize, MdlSize;
    LONG i;
    PKPRCB Prcb;
    PGENERAL_LOOKASIDE CurrentList = NULL;

    /* Calculate the sizes */
    LargeIrpSize = sizeof(IRP) + (8 * sizeof(IO_STACK_LOCATION));
    SmallIrpSize = sizeof(IRP) + sizeof(IO_STACK_LOCATION);
    MdlSize = sizeof(MDL) + (23 * sizeof(PFN_NUMBER));

    /* Initialize the Lookaside List for I\O Completion */
    ExInitializeSystemLookasideList(&IoCompletionPacketLookaside,
                                    NonPagedPool,
                                    sizeof(IOP_MINI_COMPLETION_PACKET),
                                    IOC_TAG1,
                                    32,
                                    &ExSystemLookasideListHead);

    /* Initialize the Lookaside List for Large IRPs */
    ExInitializeSystemLookasideList(&IoLargeIrpLookaside,
                                    NonPagedPool,
                                    LargeIrpSize,
                                    IO_LARGEIRP,
                                    64,
                                    &ExSystemLookasideListHead);


    /* Initialize the Lookaside List for Small IRPs */
    ExInitializeSystemLookasideList(&IoSmallIrpLookaside,
                                    NonPagedPool,
                                    SmallIrpSize,
                                    IO_SMALLIRP,
                                    32,
                                    &ExSystemLookasideListHead);

    /* Initialize the Lookaside List for MDLs */
    ExInitializeSystemLookasideList(&IopMdlLookasideList,
                                    NonPagedPool,
                                    MdlSize,
                                    TAG_MDL,
                                    128,
                                    &ExSystemLookasideListHead);

    /* Allocate the global lookaside list buffer */
    CurrentList = ExAllocatePoolWithTag(NonPagedPool,
                                        4 * KeNumberProcessors *
                                        sizeof(GENERAL_LOOKASIDE),
                                        TAG_IO);

    /* Loop all processors */
    for (i = 0; i < KeNumberProcessors; i++)
    {
        /* Get the PRCB for this CPU */
        Prcb = KiProcessorBlock[i];
        DPRINT("Setting up lookaside for CPU: %x, PRCB: %p\n", i, Prcb);

        /* Write IRP credit limit */
        Prcb->LookasideIrpFloat = 512 / KeNumberProcessors;

        /* Set the I/O Completion List */
        Prcb->PPLookasideList[LookasideCompletionList].L = &IoCompletionPacketLookaside;
        if (CurrentList)
        {
            /* Initialize the Lookaside List for mini-packets */
            ExInitializeSystemLookasideList(CurrentList,
                                            NonPagedPool,
                                            sizeof(IOP_MINI_COMPLETION_PACKET),
                                            IO_SMALLIRP_CPU,
                                            32,
                                            &ExSystemLookasideListHead);
            Prcb->PPLookasideList[LookasideCompletionList].P = CurrentList;
            CurrentList++;

        }
        else
        {
            Prcb->PPLookasideList[LookasideCompletionList].P = &IoCompletionPacketLookaside;
        }

        /* Set the Large IRP List */
        Prcb->PPLookasideList[LookasideLargeIrpList].L = &IoLargeIrpLookaside;
        if (CurrentList)
        {
            /* Initialize the Lookaside List for Large IRPs */
            ExInitializeSystemLookasideList(CurrentList,
                                            NonPagedPool,
                                            LargeIrpSize,
                                            IO_LARGEIRP_CPU,
                                            64,
                                            &ExSystemLookasideListHead);
            Prcb->PPLookasideList[LookasideLargeIrpList].P = CurrentList;
            CurrentList++;

        }
        else
        {
            Prcb->PPLookasideList[LookasideLargeIrpList].P = &IoLargeIrpLookaside;
        }

        /* Set the Small IRP List */
        Prcb->PPLookasideList[LookasideSmallIrpList].L = &IoSmallIrpLookaside;
        if (CurrentList)
        {
            /* Initialize the Lookaside List for Small IRPs */
            ExInitializeSystemLookasideList(CurrentList,
                                            NonPagedPool,
                                            SmallIrpSize,
                                            IO_SMALLIRP_CPU,
                                            32,
                                            &ExSystemLookasideListHead);
            Prcb->PPLookasideList[LookasideSmallIrpList].P = CurrentList;
            CurrentList++;

        }
        else
        {
            Prcb->PPLookasideList[LookasideSmallIrpList].P = &IoSmallIrpLookaside;
        }

        /* Set the MDL Completion List */
        Prcb->PPLookasideList[LookasideMdlList].L = &IopMdlLookasideList;
        if (CurrentList)
        {
            /* Initialize the Lookaside List for MDLs */
            ExInitializeSystemLookasideList(CurrentList,
                                            NonPagedPool,
                                            SmallIrpSize,
                                            TAG_MDL,
                                            128,
                                            &ExSystemLookasideListHead);

            Prcb->PPLookasideList[LookasideMdlList].P = CurrentList;
            CurrentList++;

        }
        else
        {
            Prcb->PPLookasideList[LookasideMdlList].P = &IopMdlLookasideList;
        }
    }
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
IopCreateObjectTypes(VOID)
{
    UNICODE_STRING Name;

    /*
     * ARM64 FIX: Use separate OBJECT_TYPE_INITIALIZER variables for each object type.
     *
     * CRITICAL BUG DISCOVERED: When reusing a single ObjectTypeInitializer variable
     * on ARM64 with LLVM/Clang, the compiler generates incorrect code that causes
     * the structure pointer to be offset by 8 bytes on subsequent calls.
     *
     * Symptoms observed:
     * - Adapter object type creation succeeded with correct structure content
     * - Controller object type creation failed because the pointer passed to
     *   ObCreateObjectType was pointing 8 bytes INTO the structure instead of
     *   at the beginning
     * - At address FFFFF88003733C58, the first 16 bytes were:
     *   Adapter:    70 00 01 00 00 01 00 00 89 00 12 00 16 01 12 00 (correct)
     *   Controller: 89 00 12 00 16 01 12 00 a0 00 12 00 ff 01 1f 00 (offset by 8!)
     *
     * Root cause: Likely an ARM64 LLVM compiler bug in stack frame management or
     * structure address calculation when the same stack variable is reused and
     * passed by reference multiple times.
     *
     * Solution: Use separate variables with distinct stack locations to force the
     * compiler to generate correct addressing code for each call.
     */

    /* Create the Adapter object type */
    {
        OBJECT_TYPE_INITIALIZER AdapterTypeInitializer;
        RtlZeroMemory(&AdapterTypeInitializer, sizeof(AdapterTypeInitializer));

        RtlInitUnicodeString(&Name, L"Adapter");
        AdapterTypeInitializer.Length = sizeof(AdapterTypeInitializer);
        AdapterTypeInitializer.PoolType = NonPagedPool;
        AdapterTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
        AdapterTypeInitializer.ValidAccessMask = FILE_ALL_ACCESS;
        AdapterTypeInitializer.UseDefaultObject = TRUE;
        AdapterTypeInitializer.GenericMapping = IopFileMapping;
        AdapterTypeInitializer.CaseInsensitive = TRUE;

        if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                           &AdapterTypeInitializer,
                                           NULL,
                                           &IoAdapterObjectType)))
        {
            DPRINT1("Failed to create Adapter object type\n");
            return FALSE;
        }
    }

    /* Create the Controller object type */
    {
        OBJECT_TYPE_INITIALIZER ControllerTypeInitializer;
        RtlZeroMemory(&ControllerTypeInitializer, sizeof(ControllerTypeInitializer));

        RtlInitUnicodeString(&Name, L"Controller");
        ControllerTypeInitializer.Length = sizeof(ControllerTypeInitializer);
        ControllerTypeInitializer.PoolType = NonPagedPool;
        ControllerTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
        ControllerTypeInitializer.ValidAccessMask = FILE_ALL_ACCESS;
        ControllerTypeInitializer.UseDefaultObject = TRUE;
        ControllerTypeInitializer.GenericMapping = IopFileMapping;
        ControllerTypeInitializer.DefaultNonPagedPoolCharge = sizeof(CONTROLLER_OBJECT);
        ControllerTypeInitializer.CaseInsensitive = TRUE;

        if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                           &ControllerTypeInitializer,
                                           NULL,
                                           &IoControllerObjectType)))
        {
            DPRINT1("Failed to create Controller object type\n");
            return FALSE;
        }
    }

    /* Create the Device object type */
    {
        OBJECT_TYPE_INITIALIZER DeviceTypeInitializer;
        RtlZeroMemory(&DeviceTypeInitializer, sizeof(DeviceTypeInitializer));

        RtlInitUnicodeString(&Name, L"Device");
        DeviceTypeInitializer.Length = sizeof(DeviceTypeInitializer);
        DeviceTypeInitializer.PoolType = NonPagedPool;
        DeviceTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
        DeviceTypeInitializer.ValidAccessMask = FILE_ALL_ACCESS;
        DeviceTypeInitializer.UseDefaultObject = TRUE;
        DeviceTypeInitializer.GenericMapping = IopFileMapping;
        DeviceTypeInitializer.DefaultNonPagedPoolCharge = sizeof(DEVICE_OBJECT);
        DeviceTypeInitializer.DeleteProcedure = IopDeleteDevice;
        DeviceTypeInitializer.ParseProcedure = IopParseDevice;
        DeviceTypeInitializer.SecurityProcedure = IopGetSetSecurityObject;
        DeviceTypeInitializer.CaseInsensitive = TRUE;

        if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                           &DeviceTypeInitializer,
                                           NULL,
                                           &IoDeviceObjectType)))
        {
            DPRINT1("Failed to create Device object type\n");
            return FALSE;
        }
    }

    /* Create the Driver object type */
    {
        OBJECT_TYPE_INITIALIZER DriverTypeInitializer;
        RtlZeroMemory(&DriverTypeInitializer, sizeof(DriverTypeInitializer));

        RtlInitUnicodeString(&Name, L"Driver");
        DriverTypeInitializer.Length = sizeof(DriverTypeInitializer);
        DriverTypeInitializer.PoolType = NonPagedPool;
        DriverTypeInitializer.InvalidAttributes = OBJ_OPENLINK;
        DriverTypeInitializer.ValidAccessMask = FILE_ALL_ACCESS;
        DriverTypeInitializer.UseDefaultObject = TRUE;
        DriverTypeInitializer.GenericMapping = IopFileMapping;
        DriverTypeInitializer.DefaultNonPagedPoolCharge = sizeof(DRIVER_OBJECT);
        DriverTypeInitializer.DeleteProcedure = IopDeleteDriver;
        DriverTypeInitializer.CaseInsensitive = TRUE;

        if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                           &DriverTypeInitializer,
                                           NULL,
                                           &IoDriverObjectType)))
        {
            DPRINT1("Failed to create Driver object type\n");
            return FALSE;
        }
    }

    /* Create the I/O Completion object type */
    {
        OBJECT_TYPE_INITIALIZER IoCompletionTypeInitializer;
        RtlZeroMemory(&IoCompletionTypeInitializer, sizeof(IoCompletionTypeInitializer));

        RtlInitUnicodeString(&Name, L"IoCompletion");
        IoCompletionTypeInitializer.Length = sizeof(IoCompletionTypeInitializer);
        IoCompletionTypeInitializer.PoolType = NonPagedPool;
        IoCompletionTypeInitializer.DefaultNonPagedPoolCharge = sizeof(KQUEUE);
        IoCompletionTypeInitializer.ValidAccessMask = IO_COMPLETION_ALL_ACCESS;
        IoCompletionTypeInitializer.InvalidAttributes = OBJ_OPENLINK | OBJ_PERMANENT;
        IoCompletionTypeInitializer.GenericMapping = IopCompletionMapping;
        IoCompletionTypeInitializer.DeleteProcedure = IopDeleteIoCompletion;
        IoCompletionTypeInitializer.CaseInsensitive = FALSE;
        IoCompletionTypeInitializer.UseDefaultObject = TRUE;

        if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                           &IoCompletionTypeInitializer,
                                           NULL,
                                           &IoCompletionType)))
        {
            DPRINT1("Failed to create IoCompletion object type\n");
            return FALSE;
        }
    }

    /* Create the File object type */
    {
        OBJECT_TYPE_INITIALIZER FileTypeInitializer;
        RtlZeroMemory(&FileTypeInitializer, sizeof(FileTypeInitializer));

        RtlInitUnicodeString(&Name, L"File");
        FileTypeInitializer.Length = sizeof(FileTypeInitializer);
        FileTypeInitializer.PoolType = NonPagedPool;
        FileTypeInitializer.DefaultNonPagedPoolCharge = sizeof(FILE_OBJECT);
        FileTypeInitializer.InvalidAttributes = OBJ_OPENLINK | OBJ_EXCLUSIVE;
        FileTypeInitializer.MaintainHandleCount = TRUE;
        FileTypeInitializer.ValidAccessMask = FILE_ALL_ACCESS;
        FileTypeInitializer.GenericMapping = IopFileMapping;
        FileTypeInitializer.CloseProcedure = IopCloseFile;
        FileTypeInitializer.DeleteProcedure = IopDeleteFile;
        FileTypeInitializer.SecurityProcedure = IopGetSetSecurityObject;
        FileTypeInitializer.QueryNameProcedure = IopQueryName;
        FileTypeInitializer.ParseProcedure = IopParseFile;
        FileTypeInitializer.UseDefaultObject = FALSE;
        FileTypeInitializer.CaseInsensitive = TRUE;

        if (!NT_SUCCESS(ObCreateObjectType(&Name,
                                           &FileTypeInitializer,
                                           NULL,
                                           &IoFileObjectType)))
        {
            DPRINT1("Failed to create File object type\n");
            return FALSE;
        }
    }

    /* Success */
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
IopCreateRootDirectories(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING DirName;
    HANDLE Handle;
    NTSTATUS Status;

    /* Create the '\Driver' object directory */
    RtlInitUnicodeString(&DirName, L"\\Driver");
    InitializeObjectAttributes(&ObjectAttributes,
                               &DirName,
                               OBJ_PERMANENT,
                               NULL,
                               NULL);
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create \\Driver directory: 0x%lx\n", Status);
        return FALSE;
    }
    NtClose(Handle);

    /* Create the '\FileSystem' object directory */
    RtlInitUnicodeString(&DirName, L"\\FileSystem");
    InitializeObjectAttributes(&ObjectAttributes,
                               &DirName,
                               OBJ_PERMANENT,
                               NULL,
                               NULL);
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create \\FileSystem directory: 0x%lx\n", Status);
        return FALSE;
    }
    NtClose(Handle);

    /* Create the '\FileSystem' object directory */
    RtlInitUnicodeString(&DirName, L"\\FileSystem\\Filters");
    InitializeObjectAttributes(&ObjectAttributes,
                               &DirName,
                               OBJ_PERMANENT,
                               NULL,
                               NULL);
    Status = NtCreateDirectoryObject(&Handle,
                                     DIRECTORY_ALL_ACCESS,
                                     &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create \\FileSystem\\Filters directory: 0x%lx\n", Status);
        return FALSE;
    }
    NtClose(Handle);

    /* Return success */
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
IopMarkBootPartition(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    STRING DeviceString;
    CHAR Buffer[256];
    UNICODE_STRING DeviceName;
    NTSTATUS Status;
    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_OBJECT FileObject;
    ULONG RetryCount;
    LARGE_INTEGER Timeout;
    BOOLEAN IsCdromBoot;

    /* Build the ARC device name */
    sprintf(Buffer, "\\ArcName\\%s", LoaderBlock->ArcBootDeviceName);
    RtlInitAnsiString(&DeviceString, Buffer);
    Status = RtlAnsiStringToUnicodeString(&DeviceName, &DeviceString, TRUE);
    if (!NT_SUCCESS(Status)) return FALSE;

    /* Check if this is a CD-ROM boot */
    IsCdromBoot = (strstr(LoaderBlock->ArcBootDeviceName, "cdrom") != NULL);

    /* Try to open the boot device with retries for CD-ROM */
    InitializeObjectAttributes(&ObjectAttributes,
                               &DeviceName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    /* CD-ROM devices may need time to enumerate, retry up to 10 times with 100ms delay */
    RetryCount = IsCdromBoot ? 10 : 1;
    Timeout.QuadPart = -100 * 10000; /* 100ms in 100ns units, negative for relative */

    for (ULONG i = 0; i < RetryCount; i++)
    {
        Status = ZwOpenFile(&FileHandle,
                            FILE_READ_ATTRIBUTES,
                            &ObjectAttributes,
                            &IoStatusBlock,
                            0,
                            FILE_NON_DIRECTORY_FILE);

        if (NT_SUCCESS(Status))
        {
            if (i > 0)
            {
                DPRINT1("IopMarkBootPartition: CD-ROM device found after %lu retries\n", i);
            }
            break;
        }

        /* If not found and more retries available, wait and try again */
        if ((Status == STATUS_OBJECT_NAME_NOT_FOUND ||
             Status == STATUS_OBJECT_PATH_NOT_FOUND ||
             Status == STATUS_NO_SUCH_DEVICE) &&
            i + 1 < RetryCount)
        {
            DPRINT1("IopMarkBootPartition: CD-ROM device not ready (attempt %lu/%lu), waiting 100ms...\n",
                    i + 1, RetryCount);
            KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
            continue;
        }

        /* Last attempt failed or non-retryable error */
        break;
    }

    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        DPRINT1("IopMarkBootPartition: Failed to open boot device after %lu attempts (Status=0x%08lx)\n",
                RetryCount, Status);
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE,
                     (ULONG_PTR)&DeviceName,
                     Status,
                     0,
                     0);
    }

    /* Get the DO */
    Status = ObReferenceObjectByHandle(FileHandle,
                                       0,
                                       IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        /* Fail */
        RtlFreeUnicodeString(&DeviceName);
        return FALSE;
    }

    /* Mark it as the boot partition */
    FileObject->DeviceObject->Flags |= DO_SYSTEM_BOOT_PARTITION;

    /* Save a copy of the DO for the I/O Error Logger */
    ObReferenceObject(FileObject->DeviceObject);
    IopErrorLogObject = FileObject->DeviceObject;

    /* Cleanup and return success */
    RtlFreeUnicodeString(&DeviceName);
    NtClose(FileHandle);
    ObDereferenceObject(FileObject);
    return TRUE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
IoInitSystem(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    LARGE_INTEGER ExpireTime;
    NTSTATUS Status;
    CHAR Buffer[256];
    ANSI_STRING NtBootPath, RootString;

    /* Initialize empty NT Boot Path */
    RtlInitEmptyAnsiString(&NtBootPath, Buffer, sizeof(Buffer));

    /* Initialize the lookaside lists */
    IopInitLookasideLists();

    /* Initialize all locks and lists */
    ExInitializeResourceLite(&IopDatabaseResource);
    ExInitializeResourceLite(&IopSecurityResource);
    ExInitializeResourceLite(&IopDriverLoadResource);
    InitializeListHead(&IopDiskFileSystemQueueHead);
    InitializeListHead(&IopCdRomFileSystemQueueHead);
    InitializeListHead(&IopTapeFileSystemQueueHead);
    InitializeListHead(&IopNetworkFileSystemQueueHead);
    InitializeListHead(&DriverBootReinitListHead);
    InitializeListHead(&DriverReinitListHead);
    InitializeListHead(&ShutdownListHead);
    InitializeListHead(&LastChanceShutdownListHead);
    InitializeListHead(&IopFsNotifyChangeQueueHead);
    InitializeListHead(&IopErrorLogListHead);
    KeInitializeSpinLock(&IoStatisticsLock);
    KeInitializeSpinLock(&DriverReinitListLock);
    KeInitializeSpinLock(&DriverBootReinitListLock);
    KeInitializeSpinLock(&ShutdownListLock);
    KeInitializeSpinLock(&IopLogListLock);

    /* Initialize PnP notifications */
    PiInitializeNotifications();

    /* Initialize the reserve IRP */
    if (!IopInitializeReserveIrp(&IopReserveIrpAllocator))
    {
        DPRINT1("IopInitializeReserveIrp failed!\n");
        return FALSE;
    }

    /* Initialize Timer List Lock */
    KeInitializeSpinLock(&IopTimerLock);

    /* Initialize Timer List */
    InitializeListHead(&IopTimerQueueHead);

    /*
     * ARM64 CRITICAL FIX: Initialize the timer structures but DO NOT start the timer yet.
     *
     * PROBLEM: Starting the timer before creating object types can cause bugcheck 0x9
     * (IRQL_NOT_GREATER_OR_EQUAL) on ARM64. Here's why:
     *
     * 1. KeSetTimerEx starts the timer, which can fire at any time
     * 2. When the timer DPC fires, it raises IRQL to DISPATCH_LEVEL
     * 3. IopCreateObjectTypes() calls ObCreateObjectType()
     * 4. ObCreateObjectType() calls ObpEnterObjectTypeMutex()
     * 5. ObpEnterObjectTypeMutex() has ASSERT(KeGetCurrentIrql() <= APC_LEVEL)
     * 6. If the timer fired and IRQL is DISPATCH_LEVEL, the ASSERT fails
     * 7. On ARM64, failed ASSERTs call KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL)
     *
     * SOLUTION: Initialize the DPC and timer structures early, but defer starting
     * the timer until after object types are created. This ensures IRQL remains
     * at PASSIVE_LEVEL during object type creation.
     */
    ExpireTime.QuadPart = -10000000;
    KeInitializeDpc(&IopTimerDpc, IopTimerDispatch, NULL);
    KeInitializeTimerEx(&IopTimer, SynchronizationTimer);

    /* Create Object Types (MUST happen at PASSIVE_LEVEL / <= APC_LEVEL) */
    if (!IopCreateObjectTypes())
    {
        DPRINT1("IopCreateObjectTypes failed!\n");
        return FALSE;
    }

    /*
     * Now that object types are created, it's safe to start the timer.
     * The timer DPC can now fire and raise IRQL without interfering with
     * object type creation which requires IRQL <= APC_LEVEL.
     */
    KeSetTimerEx(&IopTimer, ExpireTime, 1000, &IopTimerDpc);

    /* Create Object Directories */
    if (!IopCreateRootDirectories())
    {
        DPRINT1("IopCreateRootDirectories failed!\n");
        return FALSE;
    }

    /* Initialize PnP manager */
    IopInitializePlugPlayServices();

    /* Initialize SHIM engine */
    ApphelpCacheInitialize();

    /* Initialize WMI */
    WmiInitialize();

    /* Initialize HAL Root Bus Driver */
    HalInitPnpDriver();

    /* Reenumerate what HAL has added (synchronously)
     * This function call should eventually become a 2nd stage of the PnP initialization */
    PiQueueDeviceAction(IopRootDeviceNode->PhysicalDeviceObject,
                        PiActionEnumRootDevices,
                        NULL,
                        NULL);

    /* Make loader block available for the whole kernel */
    IopLoaderBlock = LoaderBlock;

    /* Load boot start drivers */
    IopInitializeBootDrivers();

    DPRINT1("BOOT DRIVERS LOADED\n");

    /* Call back drivers that asked for */
    IopReinitializeBootDrivers();

    /* Check if this was a ramdisk boot */
    if (!_strnicmp(LoaderBlock->ArcBootDeviceName, "ramdisk(0)", 10))
    {
        /* Initialize the ramdisk driver */
        IopStartRamdisk(LoaderBlock);
    }

    /* No one should need loader block any longer */
    IopLoaderBlock = NULL;

    /* Create ARC names for boot devices */
    Status = IopCreateArcNames(LoaderBlock);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopCreateArcNames failed: %lx\n", Status);
        return FALSE;
    }

    /* Mark the system boot partition */
    if (!IopMarkBootPartition(LoaderBlock))
    {
        DPRINT1("IopMarkBootPartition failed!\n");
        KeBugCheckEx(INACCESSIBLE_BOOT_DEVICE, 0, 0, 0, 0);
        return FALSE;
    }

    /* The disk subsystem is initialized here and the SystemRoot is set too.
     * We can finally load other drivers from the boot volume. */
    PnPBootDriversInitialized = TRUE;

    /* Load system start drivers */
    IopInitializeSystemDrivers();
    PnpSystemInit = TRUE;

    /* Reinitialize drivers that requested it */
    IopReinitializeDrivers();

    /* Convert SystemRoot from ARC to NT path */
    Status = IopReassignSystemRoot(LoaderBlock, &NtBootPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IopReassignSystemRoot failed: %lx\n", Status);
        return FALSE;
    }

    /* Set the ANSI_STRING for the root path */
    RootString.MaximumLength = NtSystemRoot.MaximumLength / sizeof(WCHAR);
    RootString.Length = 0;
    RootString.Buffer = ExAllocatePoolWithTag(PagedPool,
                                              RootString.MaximumLength,
                                              TAG_IO);

    /* Convert the path into the ANSI_STRING */
    Status = RtlUnicodeStringToAnsiString(&RootString, &NtSystemRoot, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlUnicodeStringToAnsiString failed: %lx\n", Status);
        return FALSE;
    }

    /* Assign drive letters */
    IoAssignDriveLetters(LoaderBlock,
                         &NtBootPath,
                         (PUCHAR)RootString.Buffer,
                         &RootString);

    /* Update system root */
    Status = RtlAnsiStringToUnicodeString(&NtSystemRoot, &RootString, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RtlAnsiStringToUnicodeString failed: %lx\n", Status);
        return FALSE;
    }

    /* Load the System DLL and its entrypoints */
    Status = PsLocateSystemDll();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PsLocateSystemDll failed: %lx\n", Status);
        return FALSE;
    }

    /* Return success */
    return TRUE;
}

BOOLEAN
NTAPI
IoInitializeCrashDump(IN HANDLE PageFileHandle)
{
    UNIMPLEMENTED;
    return FALSE;
}

/* EOF */
