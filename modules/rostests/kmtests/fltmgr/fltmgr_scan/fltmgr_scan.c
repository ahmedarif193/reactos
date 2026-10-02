/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Filter Manager data scan section test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <fltkernel.h>

#define NDEBUG
#include <debug.h>

#include "fltmgr_scan.h"

#define TEST_TAG 'cStF'
#define TEST_FILE_SIZE 0x3000

typedef enum _SCAN_MODE
{
    ModeRecord,
    ModeDelayedClose,
    ModeCloseInCallback,
    ModePlain,
    ModeNone
} SCAN_MODE;

typedef struct _SCAN_SECTION
{
    HANDLE FileHandle;
    PFILE_OBJECT FileObject;
    PFLT_CONTEXT Context;
    HANDLE SectionHandle;
    PVOID SectionObject;
    volatile LONG Open;
    NTSTATUS CloseStatus;
    NTSTATUS HandleStatus;
} SCAN_SECTION, *PSCAN_SECTION;

typedef struct _SCAN_RECORD
{
    volatile LONG Count;
    UCHAR Major;
    ULONG Detail;
    PFLT_INSTANCE Instance;
    PFLT_CONTEXT Context;
    PFILE_OBJECT FileObject;
    PETHREAD Thread;
    KIRQL Irql;
} SCAN_RECORD;

typedef struct _CONFLICT_RESULT
{
    NTSTATUS Status;
    ULONG Count;
    UCHAR Major;
    ULONG Detail;
    BOOLEAN Waited;
} CONFLICT_RESULT;

typedef NTSTATUS (*CONFLICT_ROUTINE)(HANDLE Handle, PFILE_OBJECT FileObject);

typedef struct _CONFLICT_OP
{
    PCSTR Name;
    CONFLICT_ROUTINE Routine;
    CONFLICT_RESULT Delayed;
    CONFLICT_RESULT Closed;
    NTSTATUS Plain;
} CONFLICT_OP;

static KMT_MESSAGE_HANDLER TestMessageHandler;

static UNICODE_STRING TestFileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\FltMgrScan.dat");
static UNICODE_STRING EmptyFileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\FltMgrScan0.dat");
static UNICODE_STRING RenameFileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\FltMgrScan2.dat");
static UNICODE_STRING LinkFileName = RTL_CONSTANT_STRING(L"\\SystemRoot\\FltMgrScan3.dat");
static UNICODE_STRING DirectoryName = RTL_CONSTANT_STRING(L"\\SystemRoot");

static WCHAR ServicePath[260];
static PDRIVER_OBJECT TestDriverObject;
static PFLT_FILTER Filter;
static PFLT_INSTANCE TestInstance;
static LONG ContextAllocated, ContextCleaned;
static LONG PipeSeen;
static NTSTATUS PipeStatus;
static SCAN_RECORD Record;
static SCAN_MODE Mode;
static PSCAN_SECTION Current;
static WORK_QUEUE_ITEM WorkItem;
static volatile LONG WorkQueued;
static KEVENT WorkDone;


static
VOID
FLTAPI
ContextCleanup(
    _In_ PFLT_CONTEXT Context,
    _In_ FLT_CONTEXT_TYPE ContextType)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(ContextType);

    InterlockedIncrement(&ContextCleaned);
}

static
NTSTATUS
FLTAPI
InstanceSetup(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_SETUP_FLAGS Flags,
    _In_ DEVICE_TYPE VolumeDeviceType,
    _In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(VolumeFilesystemType);

    if (VolumeDeviceType == FILE_DEVICE_NAMED_PIPE)
    {
        PipeStatus = FltRegisterForDataScan(FltObjects->Instance);
        InterlockedIncrement(&PipeSeen);
        return STATUS_FLT_DO_NOT_ATTACH;
    }
    if (VolumeDeviceType != FILE_DEVICE_DISK_FILE_SYSTEM)
    {
        return STATUS_FLT_DO_NOT_ATTACH;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
FLTAPI
InstanceQueryTeardown(
    _In_ PCFLT_RELATED_OBJECTS FltObjects,
    _In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(FltObjects);
    UNREFERENCED_PARAMETER(Flags);

    return STATUS_SUCCESS;
}

static
VOID
CloseSection(
    _Inout_ PSCAN_SECTION Section)
{
    PVOID Object;

    if (InterlockedExchange(&Section->Open, 0) == 0)
    {
        return;
    }

    Section->CloseStatus = FltCloseSectionForDataScan(Section->Context);
    Section->HandleStatus = ObReferenceObjectByHandle(Section->SectionHandle,
                                                      0,
                                                      MmSectionObjectType,
                                                      KernelMode,
                                                      &Object,
                                                      NULL);
    if (NT_SUCCESS(Section->HandleStatus))
    {
        if (Object == Section->SectionObject)
        {
            ZwClose(Section->SectionHandle);
        }
        else
        {
            Section->HandleStatus = STATUS_UNSUCCESSFUL;
        }
        ObDereferenceObject(Object);
    }
    ObDereferenceObject(Section->SectionObject);
}

static
VOID
NTAPI
DelayedClose(
    _In_ PVOID Parameter)
{
    LARGE_INTEGER Delay;

    UNREFERENCED_PARAMETER(Parameter);

    Delay.QuadPart = -10 * 1000 * 100;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    CloseSection(Current);
    KeSetEvent(&WorkDone, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
FLTAPI
SectionNotification(
    _In_ PFLT_INSTANCE Instance,
    _In_ PFLT_CONTEXT SectionContext,
    _In_ PFLT_CALLBACK_DATA Data)
{
    if (InterlockedIncrement(&Record.Count) == 1)
    {
        Record.Instance = Instance;
        Record.Context = SectionContext;
        Record.Thread = PsGetCurrentThread();
        Record.Irql = KeGetCurrentIrql();
        if (Data != NULL)
        {
            Record.Major = Data->Iopb->MajorFunction;
            Record.FileObject = Data->Iopb->TargetFileObject;
            if (Record.Major == IRP_MJ_SET_INFORMATION)
            {
                Record.Detail = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;
            }
            else if (Record.Major == IRP_MJ_FILE_SYSTEM_CONTROL)
            {
                Record.Detail = Data->Iopb->Parameters.FileSystemControl.Common.FsControlCode;
            }
        }
        else
        {
            Record.Major = 0xFF;
        }
    }

    if (Mode == ModeCloseInCallback)
    {
        CloseSection(Current);
    }
    else if (Mode == ModeDelayedClose && InterlockedExchange(&WorkQueued, 1) == 0)
    {
        ExQueueWorkItem(&WorkItem, DelayedWorkQueue);
    }
    return STATUS_SUCCESS;
}

static CONST FLT_OPERATION_REGISTRATION Operations[] =
{
    { IRP_MJ_OPERATION_END }
};

static CONST FLT_CONTEXT_REGISTRATION ContextRegistration[] =
{
    { FLT_SECTION_CONTEXT, 0, ContextCleanup, sizeof(ULONG), TEST_TAG },
    { FLT_CONTEXT_END }
};

static CONST FLT_REGISTRATION Registration =
{
    sizeof(FLT_REGISTRATION),
    FLT_REGISTRATION_VERSION,
    FLTFL_REGISTRATION_DO_NOT_SUPPORT_SERVICE_STOP | FLTFL_REGISTRATION_SUPPORT_NPFS_MSFS,
    ContextRegistration,
    Operations,
    NULL,
    InstanceSetup,
    InstanceQueryTeardown,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    SectionNotification
};

static
NTSTATUS
WriteInstanceRegistry(VOID)
{
    static WCHAR InstanceName[] = L"FltMgrScan Instance";
    static WCHAR Altitude[] = L"320124";
    WCHAR Path[320];
    ULONG Flags = 0;
    NTSTATUS Status;

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances", ServicePath);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE,
                                   Path,
                                   L"DefaultInstance",
                                   REG_SZ,
                                   InstanceName,
                                   sizeof(InstanceName));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlStringCbPrintfW(Path, sizeof(Path), L"%ls\\Instances\\%ls", ServicePath, InstanceName);
    Status = RtlCreateRegistryKey(RTL_REGISTRY_ABSOLUTE, Path);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Altitude", REG_SZ, Altitude, sizeof(Altitude));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return RtlWriteRegistryValue(RTL_REGISTRY_ABSOLUTE, Path, L"Flags", REG_DWORD, &Flags, sizeof(Flags));
}

static
NTSTATUS
OpenFile(
    _In_ PUNICODE_STRING Name,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Disposition,
    _In_ ULONG Options,
    _Out_ PHANDLE Handle)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK Iosb;

    InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    return ZwCreateFile(Handle,
                        Access | SYNCHRONIZE,
                        &ObjectAttributes,
                        &Iosb,
                        NULL,
                        FILE_ATTRIBUTE_NORMAL,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        Disposition,
                        Options | FILE_SYNCHRONOUS_IO_NONALERT,
                        NULL,
                        0);
}

static
VOID
DeleteByName(
    _In_ PUNICODE_STRING Name)
{
    HANDLE Handle;

    if (NT_SUCCESS(OpenFile(Name, DELETE, FILE_OPEN, FILE_DELETE_ON_CLOSE | FILE_NON_DIRECTORY_FILE, &Handle)))
    {
        ZwClose(Handle);
    }
}

static
UCHAR
PatternByte(
    _In_ ULONG Index)
{
    return (UCHAR)(Index * 7 + 1);
}

static
NTSTATUS
OpenTestFile(
    _In_ PUNICODE_STRING Name,
    _In_ ULONG Size,
    _Out_ PSCAN_SECTION Section)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK Iosb;
    PUCHAR Buffer;
    NTSTATUS Status;
    ULONG Index;

    RtlZeroMemory(Section, sizeof(*Section));
    Status = OpenFile(Name,
                      GENERIC_READ | GENERIC_WRITE | DELETE,
                      FILE_OVERWRITE_IF,
                      FILE_NON_DIRECTORY_FILE,
                      &Section->FileHandle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Size != 0)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, Size, TEST_TAG);
        if (Buffer == NULL)
        {
            ZwClose(Section->FileHandle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        for (Index = 0; Index < Size; Index++)
        {
            Buffer[Index] = PatternByte(Index);
        }
        Offset.QuadPart = 0;
        Status = ZwWriteFile(Section->FileHandle, NULL, NULL, NULL, &Iosb, Buffer, Size, &Offset, NULL);
        ExFreePoolWithTag(Buffer, TEST_TAG);
        if (!NT_SUCCESS(Status))
        {
            ZwClose(Section->FileHandle);
            return Status;
        }
    }

    Status = ObReferenceObjectByHandle(Section->FileHandle,
                                       0,
                                       *IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&Section->FileObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Section->FileHandle);
    }
    return Status;
}

static
VOID
CloseTestFile(
    _Inout_ PSCAN_SECTION Section)
{
    FILE_DISPOSITION_INFORMATION Disposition;
    IO_STATUS_BLOCK Iosb;

    Disposition.DeleteFile = TRUE;
    ZwSetInformationFile(Section->FileHandle, &Iosb, &Disposition, sizeof(Disposition), FileDispositionInformation);
    ObDereferenceObject(Section->FileObject);
    ZwClose(Section->FileHandle);
}

static
NTSTATUS
AllocateContext(
    _Out_ PFLT_CONTEXT *Context)
{
    NTSTATUS Status;

    Status = FltAllocateContext(Filter, FLT_SECTION_CONTEXT, sizeof(ULONG), PagedPool, Context);
    if (NT_SUCCESS(Status))
    {
        InterlockedIncrement(&ContextAllocated);
    }
    return Status;
}

static
NTSTATUS
CreateSection(
    _Inout_ PSCAN_SECTION Section,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Protection,
    _In_ ULONG Attributes,
    _In_ BOOLEAN KernelHandle,
    _Out_opt_ PLARGE_INTEGER Size)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = FltCreateSectionForDataScan(TestInstance,
                                         Section->FileObject,
                                         Section->Context,
                                         Access,
                                         KernelHandle ? &ObjectAttributes : NULL,
                                         NULL,
                                         Protection,
                                         Attributes,
                                         0,
                                         &Section->SectionHandle,
                                         &Section->SectionObject,
                                         Size);
    if (NT_SUCCESS(Status))
    {
        Section->Open = 1;
    }
    return Status;
}

static
NTSTATUS
ProbeCreate(
    _In_ PFILE_OBJECT FileObject,
    _In_ ACCESS_MASK Access,
    _In_ ULONG Protection,
    _In_ ULONG Attributes)
{
    SCAN_SECTION Section;
    NTSTATUS Status;

    RtlZeroMemory(&Section, sizeof(Section));
    Section.FileObject = FileObject;
    Status = AllocateContext(&Section.Context);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = CreateSection(&Section, Access, Protection, Attributes, TRUE, NULL);
    if (NT_SUCCESS(Status))
    {
        CloseSection(&Section);
    }
    FltReleaseContext(Section.Context);
    return Status;
}

static
NTSTATUS
FindInstance(
    _In_ PFILE_OBJECT FileObject)
{
    PFLT_VOLUME Volume;
    ULONG Count = 0;
    NTSTATUS Status;

    Status = FltGetVolumeFromFileObject(Filter, FileObject, &Volume);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = FltEnumerateInstances(Volume, Filter, &TestInstance, 1, &Count);
    FltObjectDereference(Volume);
    if (NT_SUCCESS(Status) && Count != 1)
    {
        Status = STATUS_NOT_FOUND;
    }
    if (!NT_SUCCESS(Status))
    {
        TestInstance = NULL;
    }
    return Status;
}

static
VOID
TestParameters(VOID)
{
    SCAN_SECTION Section, Second, Empty, Directory;
    LARGE_INTEGER Size, Offset, Length;
    PFLT_CONTEXT Found, Other;
    IO_STATUS_BLOCK Iosb;
    SIZE_T ViewSize;
    PVOID Object, Base, ExtraObject;
    HANDLE ExtraHandle;
    BOOLEAN Match;
    NTSTATUS Status;
    LONG Cleaned;
    ULONG Index;

    Status = OpenTestFile(&TestFileName, TEST_FILE_SIZE, &Section);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = FindInstance(Section.FileObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        CloseTestFile(&Section);
        return;
    }

    Status = AllocateContext(&Section.Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        CloseTestFile(&Section);
        return;
    }

    Status = FltCloseSectionForDataScan(Section.Context);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Found = (PVOID)1;
    Status = FltGetSectionContext(TestInstance, Section.FileObject, &Found);
    ok_eq_hex(Status, STATUS_NOT_FOUND);
    ok_eq_pointer(Found, NULL);

    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = FltRegisterForDataScan(TestInstance);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = FltRegisterForDataScan(TestInstance);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_EXECUTE_READ, SEC_COMMIT);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_8);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, 0, SEC_COMMIT);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_8);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_WRITECOPY, SEC_COMMIT);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_8);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_READONLY, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_9);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_RESERVE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_9);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_IMAGE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_9);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT | SEC_NOCACHE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER_9);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT | SEC_FILE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ | SECTION_MAP_WRITE, PAGE_READWRITE, SEC_COMMIT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ProbeCreate(Section.FileObject, SECTION_ALL_ACCESS, PAGE_READONLY, SEC_COMMIT);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = FltCloseSectionForDataScan(Section.Context);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    RtlZeroMemory(&Directory, sizeof(Directory));
    Status = OpenFile(&DirectoryName, FILE_LIST_DIRECTORY, FILE_OPEN, FILE_DIRECTORY_FILE, &Directory.FileHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByHandle(Directory.FileHandle,
                                           0,
                                           *IoFileObjectType,
                                           KernelMode,
                                           (PVOID *)&Directory.FileObject,
                                           NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = ProbeCreate(Directory.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT);
            ok_eq_hex(Status, STATUS_FILE_IS_A_DIRECTORY);
            ObDereferenceObject(Directory.FileObject);
        }
        ZwClose(Directory.FileHandle);
    }

    Status = OpenTestFile(&EmptyFileName, 0, &Empty);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ProbeCreate(Empty.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT);
        ok_eq_hex(Status, STATUS_END_OF_FILE);
        CloseTestFile(&Empty);
    }

    Size.QuadPart = -1;
    Status = CreateSection(&Section, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT, TRUE, &Size);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok(Section.SectionHandle != NULL, "No section handle\n");
        ok(Section.SectionObject != NULL, "No section object\n");
        ok((LONG_PTR)Section.SectionHandle < 0, "Handle %p is not a kernel handle\n", Section.SectionHandle);
        ok_eq_longlong(Size.QuadPart, (LONGLONG)TEST_FILE_SIZE);

        Object = NULL;
        Status = ObReferenceObjectByHandle(Section.SectionHandle,
                                           SECTION_MAP_READ,
                                           MmSectionObjectType,
                                           KernelMode,
                                           &Object,
                                           NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Object, Section.SectionObject);
        if (NT_SUCCESS(Status))
        {
            ObDereferenceObject(Object);
        }

        Found = NULL;
        Status = FltGetSectionContext(TestInstance, Section.FileObject, &Found);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Found, Section.Context);
        if (NT_SUCCESS(Status))
        {
            FltReleaseContext(Found);
        }

        RtlZeroMemory(&Second, sizeof(Second));
        Status = OpenFile(&TestFileName, GENERIC_READ | GENERIC_WRITE, FILE_OPEN, FILE_NON_DIRECTORY_FILE, &Second.FileHandle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = ObReferenceObjectByHandle(Second.FileHandle,
                                               0,
                                               *IoFileObjectType,
                                               KernelMode,
                                               (PVOID *)&Second.FileObject,
                                               NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                Found = NULL;
                Status = FltGetSectionContext(TestInstance, Second.FileObject, &Found);
                ok_eq_hex(Status, STATUS_SUCCESS);
                ok_eq_pointer(Found, Section.Context);
                if (NT_SUCCESS(Status))
                {
                    FltReleaseContext(Found);
                }

                Status = AllocateContext(&Other);
                ok_eq_hex(Status, STATUS_SUCCESS);
                if (NT_SUCCESS(Status))
                {
                    Second.Context = Other;
                    Status = CreateSection(&Second, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT, TRUE, NULL);
                    ok_eq_hex(Status, STATUS_FLT_CONTEXT_ALREADY_DEFINED);
                    CloseSection(&Second);
                    Status = FltCloseSectionForDataScan(Other);
                    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
                    FltReleaseContext(Other);
                }

                Offset.QuadPart = 0;
                Length.QuadPart = 16;
                Status = ZwLockFile(Second.FileHandle, NULL, NULL, NULL, &Iosb, &Offset, &Length, 0, TRUE, TRUE);
                ok_eq_hex(Status, STATUS_SUCCESS);
                ObDereferenceObject(Second.FileObject);
            }
            else
            {
                ZwClose(Second.FileHandle);
                Second.FileHandle = NULL;
            }
        }

        ExtraHandle = NULL;
        ExtraObject = NULL;
        Status = FltCreateSectionForDataScan(TestInstance,
                                             Section.FileObject,
                                             Section.Context,
                                             SECTION_MAP_READ,
                                             NULL,
                                             NULL,
                                             PAGE_READONLY,
                                             SEC_COMMIT,
                                             0,
                                             &ExtraHandle,
                                             &ExtraObject,
                                             NULL);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER_3);
        if (NT_SUCCESS(Status))
        {
            ObDereferenceObject(ExtraObject);
            ZwClose(ExtraHandle);
        }

        Base = NULL;
        ViewSize = 0;
        Status = ZwMapViewOfSection(Section.SectionHandle,
                                    ZwCurrentProcess(),
                                    &Base,
                                    0,
                                    0,
                                    NULL,
                                    &ViewSize,
                                    ViewUnmap,
                                    0,
                                    PAGE_READONLY);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok_eq_size(ViewSize, (SIZE_T)TEST_FILE_SIZE);
            Match = TRUE;
            _SEH2_TRY
            {
                for (Index = 0; Index < TEST_FILE_SIZE; Index++)
                {
                    if (((PUCHAR)Base)[Index] != PatternByte(Index))
                    {
                        Match = FALSE;
                        break;
                    }
                }
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Match = FALSE;
            }
            _SEH2_END;
            ok(Match, "Mapped view does not hold the file data\n");
            ZwUnmapViewOfSection(ZwCurrentProcess(), Base);
        }

        CloseSection(&Section);
        ok_eq_hex(Section.CloseStatus, STATUS_SUCCESS);
        ok_eq_hex(Section.HandleStatus, STATUS_SUCCESS);
        Status = FltCloseSectionForDataScan(Section.Context);
        ok_eq_hex(Status, STATUS_NOT_FOUND);
        Found = (PVOID)1;
        Status = FltGetSectionContext(TestInstance, Section.FileObject, &Found);
        ok_eq_hex(Status, STATUS_NOT_FOUND);
        ok_eq_pointer(Found, NULL);

        Status = ProbeCreate(Section.FileObject, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Second.FileHandle != NULL)
        {
            ZwClose(Second.FileHandle);
        }

        Status = CreateSection(&Section, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT, TRUE, NULL);
        ok_eq_hex(Status, STATUS_INVALID_PARAMETER_3);
        CloseSection(&Section);
    }

    Cleaned = ContextCleaned;
    FltReleaseContext(Section.Context);
    ok_eq_long(ContextCleaned, Cleaned + 1);

    Status = AllocateContext(&Section.Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = CreateSection(&Section, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT, FALSE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok((LONG_PTR)Section.SectionHandle > 0, "Handle %p is not a user handle\n", Section.SectionHandle);
            CloseSection(&Section);
            ok_eq_hex(Section.CloseStatus, STATUS_SUCCESS);
            ok_eq_hex(Section.HandleStatus, STATUS_SUCCESS);
        }
        FltReleaseContext(Section.Context);
    }
    CloseTestFile(&Section);
}

static
NTSTATUS
OpRead(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK Iosb;
    UCHAR Buffer[16];

    UNREFERENCED_PARAMETER(FileObject);

    Offset.QuadPart = 0;
    return ZwReadFile(Handle, NULL, NULL, NULL, &Iosb, Buffer, sizeof(Buffer), &Offset, NULL);
}

static
NTSTATUS
OpWrite(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK Iosb;
    UCHAR Buffer[16] = { 0 };

    UNREFERENCED_PARAMETER(FileObject);

    Offset.QuadPart = 0;
    return ZwWriteFile(Handle, NULL, NULL, NULL, &Iosb, Buffer, sizeof(Buffer), &Offset, NULL);
}

static
NTSTATUS
OpWriteExtend(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK Iosb;
    UCHAR Buffer[16] = { 0 };

    UNREFERENCED_PARAMETER(FileObject);

    Offset.QuadPart = TEST_FILE_SIZE + 0x1000;
    return ZwWriteFile(Handle, NULL, NULL, NULL, &Iosb, Buffer, sizeof(Buffer), &Offset, NULL);
}

static
NTSTATUS
OpReadNonCached(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK Iosb;
    HANDLE Direct;
    PVOID Buffer;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(FileObject);

    Status = OpenFile(&TestFileName,
                      GENERIC_READ,
                      FILE_OPEN,
                      FILE_NO_INTERMEDIATE_BUFFERING | FILE_NON_DIRECTORY_FILE,
                      &Direct);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, 0x1000, TEST_TAG);
    if (Buffer == NULL)
    {
        ZwClose(Direct);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Offset.QuadPart = 0;
    Status = ZwReadFile(Direct, NULL, NULL, NULL, &Iosb, Buffer, 0x1000, &Offset, NULL);
    ExFreePoolWithTag(Buffer, TEST_TAG);
    ZwClose(Direct);
    return Status;
}

static
NTSTATUS
OpWriteNonCached(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    LARGE_INTEGER Offset;
    IO_STATUS_BLOCK Iosb;
    HANDLE Direct;
    PVOID Buffer;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(FileObject);

    Status = OpenFile(&TestFileName,
                      GENERIC_WRITE,
                      FILE_OPEN,
                      FILE_NO_INTERMEDIATE_BUFFERING | FILE_NON_DIRECTORY_FILE,
                      &Direct);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, 0x1000, TEST_TAG);
    if (Buffer == NULL)
    {
        ZwClose(Direct);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Buffer, 0x1000);
    Offset.QuadPart = 0;
    Status = ZwWriteFile(Direct, NULL, NULL, NULL, &Iosb, Buffer, 0x1000, &Offset, NULL);
    ExFreePoolWithTag(Buffer, TEST_TAG);
    ZwClose(Direct);
    return Status;
}

static
NTSTATUS
SetEndOfFile(
    HANDLE Handle,
    LONGLONG Size)
{
    FILE_END_OF_FILE_INFORMATION Information;
    IO_STATUS_BLOCK Iosb;

    Information.EndOfFile.QuadPart = Size;
    return ZwSetInformationFile(Handle, &Iosb, &Information, sizeof(Information), FileEndOfFileInformation);
}

static
NTSTATUS
OpTruncate(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(FileObject);

    return SetEndOfFile(Handle, 0x1000);
}

static
NTSTATUS
OpTruncateZero(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(FileObject);

    return SetEndOfFile(Handle, 0);
}

static
NTSTATUS
OpExtend(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(FileObject);

    return SetEndOfFile(Handle, TEST_FILE_SIZE + 0x2000);
}

static
NTSTATUS
OpSameSize(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(FileObject);

    return SetEndOfFile(Handle, TEST_FILE_SIZE);
}

static
NTSTATUS
OpTrimAfterExtend(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(FileObject);

    Status = SetEndOfFile(Handle, TEST_FILE_SIZE + 0x2000);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    return SetEndOfFile(Handle, TEST_FILE_SIZE + 0x1000);
}

static
NTSTATUS
OpAllocationKeep(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    FILE_ALLOCATION_INFORMATION Information;
    IO_STATUS_BLOCK Iosb;

    UNREFERENCED_PARAMETER(FileObject);

    Information.AllocationSize.QuadPart = TEST_FILE_SIZE;
    return ZwSetInformationFile(Handle, &Iosb, &Information, sizeof(Information), FileAllocationInformation);
}

static
NTSTATUS
OpZeroData(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    FILE_ZERO_DATA_INFORMATION Information;
    IO_STATUS_BLOCK Iosb;

    UNREFERENCED_PARAMETER(FileObject);

    Information.FileOffset.QuadPart = 0;
    Information.BeyondFinalZero.QuadPart = 0x1000;
    return ZwFsControlFile(Handle, NULL, NULL, NULL, &Iosb, FSCTL_SET_ZERO_DATA, &Information, sizeof(Information), NULL, 0);
}

static
NTSTATUS
OpMapWrite(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    SIZE_T ViewSize = 0;
    PVOID Base = NULL;
    HANDLE Section;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(FileObject);

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, &ObjectAttributes, NULL, PAGE_READWRITE, SEC_COMMIT, Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ZwMapViewOfSection(Section, ZwCurrentProcess(), &Base, 0, 0, NULL, &ViewSize, ViewUnmap, 0, PAGE_READWRITE);
    if (NT_SUCCESS(Status))
    {
        _SEH2_TRY
        {
            *(PUCHAR)Base = 0x5A;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        ZwUnmapViewOfSection(ZwCurrentProcess(), Base);
    }
    ZwClose(Section);
    return Status;
}

static
NTSTATUS
OpAllocation(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    FILE_ALLOCATION_INFORMATION Information;
    IO_STATUS_BLOCK Iosb;

    UNREFERENCED_PARAMETER(FileObject);

    Information.AllocationSize.QuadPart = 0;
    return ZwSetInformationFile(Handle, &Iosb, &Information, sizeof(Information), FileAllocationInformation);
}

static
NTSTATUS
OpDelete(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    FILE_DISPOSITION_INFORMATION Information;
    IO_STATUS_BLOCK Iosb;

    UNREFERENCED_PARAMETER(FileObject);

    Information.DeleteFile = TRUE;
    return ZwSetInformationFile(Handle, &Iosb, &Information, sizeof(Information), FileDispositionInformation);
}

static
NTSTATUS
OpBasic(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    FILE_BASIC_INFORMATION Information;
    IO_STATUS_BLOCK Iosb;

    UNREFERENCED_PARAMETER(FileObject);

    RtlZeroMemory(&Information, sizeof(Information));
    Information.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    return ZwSetInformationFile(Handle, &Iosb, &Information, sizeof(Information), FileBasicInformation);
}

static
NTSTATUS
SetName(
    HANDLE Handle,
    PUNICODE_STRING Name,
    FILE_INFORMATION_CLASS Class)
{
    union
    {
        FILE_RENAME_INFORMATION Information;
        UCHAR Buffer[sizeof(FILE_RENAME_INFORMATION) + 64 * sizeof(WCHAR)];
    } Rename;
    IO_STATUS_BLOCK Iosb;

    RtlZeroMemory(&Rename, sizeof(Rename));
    Rename.Information.ReplaceIfExists = TRUE;
    Rename.Information.FileNameLength = Name->Length;
    RtlCopyMemory(Rename.Information.FileName, Name->Buffer, Name->Length);
    return ZwSetInformationFile(Handle, &Iosb, &Rename, sizeof(Rename), Class);
}

static
NTSTATUS
OpRename(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(FileObject);

    return SetName(Handle, &RenameFileName, FileRenameInformation);
}

static
NTSTATUS
OpLink(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(FileObject);

    return SetName(Handle, &LinkFileName, FileLinkInformation);
}

static
NTSTATUS
OpLock(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    LARGE_INTEGER Offset, Length;
    IO_STATUS_BLOCK Iosb;

    UNREFERENCED_PARAMETER(FileObject);

    Offset.QuadPart = 0;
    Length.QuadPart = 16;
    return ZwLockFile(Handle, NULL, NULL, NULL, &Iosb, &Offset, &Length, 0, TRUE, TRUE);
}

static
NTSTATUS
OpFlush(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    IO_STATUS_BLOCK Iosb;

    UNREFERENCED_PARAMETER(FileObject);

    return ZwFlushBuffersFile(Handle, &Iosb);
}

static
NTSTATUS
OpenAgain(
    ACCESS_MASK Access,
    ULONG Disposition,
    ULONG Options)
{
    HANDLE Handle;
    NTSTATUS Status;

    Status = OpenFile(&TestFileName, Access, Disposition, Options | FILE_NON_DIRECTORY_FILE, &Handle);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }
    return Status;
}

static
NTSTATUS
OpOpenWrite(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(FileObject);

    return OpenAgain(GENERIC_WRITE, FILE_OPEN, 0);
}

static
NTSTATUS
OpOverwrite(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(FileObject);

    return OpenAgain(GENERIC_WRITE, FILE_OVERWRITE, 0);
}

static
NTSTATUS
OpSupersede(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(FileObject);

    return OpenAgain(GENERIC_WRITE | DELETE, FILE_SUPERSEDE, 0);
}

static
NTSTATUS
OpDeleteOnClose(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    UNREFERENCED_PARAMETER(Handle);
    UNREFERENCED_PARAMETER(FileObject);

    return OpenAgain(DELETE, FILE_OPEN, FILE_DELETE_ON_CLOSE);
}

static
NTSTATUS
OpWriteSection(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Section;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(FileObject);

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateSection(&Section, SECTION_ALL_ACCESS, &ObjectAttributes, NULL, PAGE_READWRITE, SEC_COMMIT, Handle);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Section);
    }
    return Status;
}

static
NTSTATUS
OpFilterTruncate(
    HANDLE Handle,
    PFILE_OBJECT FileObject)
{
    FILE_END_OF_FILE_INFORMATION Information;

    UNREFERENCED_PARAMETER(Handle);

    Information.EndOfFile.QuadPart = 0x1000;
    return FltSetInformationFile(TestInstance, FileObject, &Information, sizeof(Information), FileEndOfFileInformation);
}

#define NOTIFIED(Major, Detail) { STATUS_SUCCESS, 1, Major, Detail, TRUE }, { STATUS_SUCCESS, 1, Major, Detail, FALSE }
#define QUIET { STATUS_SUCCESS, 0, 0, 0, FALSE }, { STATUS_SUCCESS, 0, 0, 0, FALSE }

static CONST CONFLICT_OP ConflictOps[] =
{
    { "Read", OpRead, QUIET, STATUS_SUCCESS },
    { "ReadNonCached", OpReadNonCached, QUIET, STATUS_SUCCESS },
    { "Write", OpWrite, QUIET, STATUS_SUCCESS },
    { "WriteExtend", OpWriteExtend, QUIET, STATUS_SUCCESS },
    { "WriteNonCached", OpWriteNonCached, NOTIFIED(IRP_MJ_WRITE, 0), STATUS_SUCCESS },
    { "Truncate", OpTruncate, NOTIFIED(IRP_MJ_SET_INFORMATION, FileEndOfFileInformation), STATUS_USER_MAPPED_FILE },
    { "TruncateZero", OpTruncateZero, NOTIFIED(IRP_MJ_SET_INFORMATION, FileEndOfFileInformation), STATUS_USER_MAPPED_FILE },
    { "SameSize", OpSameSize, QUIET, STATUS_SUCCESS },
    { "Extend", OpExtend, QUIET, STATUS_SUCCESS },
    { "TrimAfterExtend", OpTrimAfterExtend, NOTIFIED(IRP_MJ_SET_INFORMATION, FileEndOfFileInformation), STATUS_USER_MAPPED_FILE },
    { "Allocation", OpAllocation, NOTIFIED(IRP_MJ_SET_INFORMATION, FileAllocationInformation), STATUS_USER_MAPPED_FILE },
    { "AllocationKeep", OpAllocationKeep, QUIET, STATUS_SUCCESS },
    { "Delete", OpDelete, QUIET, STATUS_CANNOT_DELETE },
    { "Basic", OpBasic, QUIET, STATUS_SUCCESS },
    { "Rename", OpRename, QUIET, STATUS_SUCCESS },
    { "Link", OpLink, QUIET, STATUS_SUCCESS },
    { "Lock", OpLock, QUIET, STATUS_SUCCESS },
    { "Flush", OpFlush, QUIET, STATUS_SUCCESS },
    { "ZeroData", OpZeroData, QUIET, STATUS_SUCCESS },
    { "OpenWrite", OpOpenWrite, QUIET, STATUS_SUCCESS },
    { "Overwrite", OpOverwrite, QUIET, STATUS_USER_MAPPED_FILE },
    { "Supersede", OpSupersede, QUIET, STATUS_USER_MAPPED_FILE },
    { "DeleteOnClose", OpDeleteOnClose, QUIET, STATUS_SUCCESS },
    { "WriteSection", OpWriteSection, QUIET, STATUS_SUCCESS },
    { "MapWrite", OpMapWrite, QUIET, STATUS_SUCCESS },
    { "FilterTruncate", OpFilterTruncate, NOTIFIED(IRP_MJ_SET_INFORMATION, FileEndOfFileInformation), STATUS_USER_MAPPED_FILE },
};

static
VOID
RunConflict(
    _In_ CONST CONFLICT_OP *Op,
    _In_ SCAN_MODE RunMode)
{
    CONST CONFLICT_RESULT *Expected = RunMode == ModeDelayedClose ? &Op->Delayed : &Op->Closed;
    PCSTR ModeName = RunMode == ModeDelayedClose ? "delayed" :
                     RunMode == ModePlain ? "plain" :
                     RunMode == ModeNone ? "none" : "closed";
    OBJECT_ATTRIBUTES ObjectAttributes;
    SCAN_SECTION Section;
    PFILE_OBJECT FileObject;
    LARGE_INTEGER Timeout;
    HANDLE Handle, Plain;
    BOOLEAN Waited;
    NTSTATUS Status, OpStatus;
    ULONG Target;

    Status = OpenTestFile(&TestFileName, TEST_FILE_SIZE, &Section);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = AllocateContext(&Section.Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        CloseTestFile(&Section);
        return;
    }

    Status = OpenFile(&TestFileName, GENERIC_READ | GENERIC_WRITE | DELETE, FILE_OPEN, FILE_NON_DIRECTORY_FILE, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        FltReleaseContext(Section.Context);
        CloseTestFile(&Section);
        return;
    }
    Status = ObReferenceObjectByHandle(Handle, 0, *IoFileObjectType, KernelMode, (PVOID *)&FileObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Handle);
        FltReleaseContext(Section.Context);
        CloseTestFile(&Section);
        return;
    }

    if (RunMode == ModeNone)
    {
        RtlZeroMemory(&Record, sizeof(Record));
        OpStatus = Op->Routine(Handle, FileObject);
        ok(OpStatus == STATUS_SUCCESS && Record.Count == 0, "%s/%s: status=0x%08lx count=%ld\n", Op->Name, ModeName, OpStatus, Record.Count);
    }
    else if (RunMode == ModePlain)
    {
        InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = ZwCreateSection(&Plain, SECTION_MAP_READ, &ObjectAttributes, NULL, PAGE_READONLY, SEC_COMMIT, Section.FileHandle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            RtlZeroMemory(&Record, sizeof(Record));
            OpStatus = Op->Routine(Handle, FileObject);
            ok(OpStatus == Op->Plain && Record.Count == 0, "%s/%s: status=0x%08lx count=%ld\n", Op->Name, ModeName, OpStatus, Record.Count);
            ZwClose(Plain);
        }
    }
    else
    {
        Status = CreateSection(&Section, SECTION_MAP_READ, PAGE_READONLY, SEC_COMMIT, TRUE, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            RtlZeroMemory(&Record, sizeof(Record));
            WorkQueued = 0;
            KeClearEvent(&WorkDone);
            Current = &Section;
            Mode = RunMode;

            OpStatus = Op->Routine(Handle, FileObject);

            Waited = (RunMode == ModeDelayedClose && Section.Open == 0);
            Mode = ModeRecord;
            if (WorkQueued)
            {
                Timeout.QuadPart = -10 * 1000 * 1000 * 10;
                KeWaitForSingleObject(&WorkDone, Executive, KernelMode, FALSE, &Timeout);
            }

            Target = Record.FileObject == FileObject ? 1 : Record.FileObject == Section.FileObject ? 2 : 0;
            ok(OpStatus == Expected->Status && (ULONG)Record.Count == Expected->Count && Record.Major == Expected->Major &&
               Record.Detail == Expected->Detail && Waited == Expected->Waited,
               "%s/%s: status=0x%08lx count=%ld major=0x%02x detail=0x%lx waited=%d\n",
               Op->Name, ModeName, OpStatus, Record.Count, Record.Major, Record.Detail, Waited);
            if (Record.Count != 0)
            {
                ok_eq_pointer(Record.Instance, TestInstance);
                ok_eq_pointer(Record.Context, Section.Context);
                ok_eq_pointer(Record.Thread, PsGetCurrentThread());
                ok_eq_uint(Record.Irql, PASSIVE_LEVEL);
            }

            CloseSection(&Section);
            Current = NULL;
        }
    }

    ObDereferenceObject(FileObject);
    ZwClose(Handle);
    FltReleaseContext(Section.Context);
    CloseTestFile(&Section);
    DeleteByName(&TestFileName);
    DeleteByName(&RenameFileName);
    DeleteByName(&LinkFileName);
}

static
VOID
TestConflicts(VOID)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(ConflictOps); Index++)
    {
        RunConflict(&ConflictOps[Index], ModeDelayedClose);
        RunConflict(&ConflictOps[Index], ModeCloseInCallback);
        RunConflict(&ConflictOps[Index], ModePlain);
        RunConflict(&ConflictOps[Index], ModeNone);
    }
}

static
VOID
TestRegister(VOID)
{
    NTSTATUS Status;

    KeInitializeEvent(&WorkDone, NotificationEvent, FALSE);
    ExInitializeWorkItem(&WorkItem, DelayedClose, NULL);

    Status = WriteInstanceRegistry();
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = FltRegisterFilter(TestDriverObject, &Registration, &Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        Filter = NULL;
        return;
    }

    Status = FltStartFiltering(Filter);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(PipeSeen == 0 || PipeStatus == STATUS_NOT_SUPPORTED, "Named pipe volume: 0x%08lx\n", PipeStatus);
}

static
VOID
TestRun(VOID)
{
    if (Filter == NULL)
    {
        return;
    }

    TestParameters();
    if (TestInstance != NULL)
    {
        TestConflicts();
        FltObjectDereference(TestInstance);
        TestInstance = NULL;
    }
}

static
VOID
TestUnregister(VOID)
{
    if (Filter == NULL)
    {
        return;
    }

    FltUnregisterFilter(Filter);
    Filter = NULL;
    ok_eq_long(ContextCleaned, ContextAllocated);
}

NTSTATUS
TestEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PCUNICODE_STRING RegistryPath,
    OUT PCWSTR *DeviceName,
    IN OUT INT *Flags)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    TestDriverObject = DriverObject;
    if (RegistryPath->Length >= sizeof(ServicePath))
    {
        return STATUS_NAME_TOO_LONG;
    }
    RtlCopyMemory(ServicePath, RegistryPath->Buffer, RegistryPath->Length);
    ServicePath[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    *DeviceName = L"FltMgrScan";
    KmtRegisterMessageHandler(0, NULL, TestMessageHandler);
    return STATUS_SUCCESS;
}

VOID
TestUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(DriverObject);

    TestUnregister();
}

static
NTSTATUS
TestMessageHandler(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    switch (ControlCode)
    {
        case IOCTL_FLTSCAN_REGISTER:
            TestRegister();
            break;

        case IOCTL_FLTSCAN_RUN:
            TestRun();
            break;

        case IOCTL_FLTSCAN_UNREGISTER:
            TestUnregister();
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}
