/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Object types test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>
#include "ObTypes.hpp"

#define NDEBUG
#include <debug.h>

const UCHAR TypeIndex_Type[] = { 1, 1, 0 };
const UCHAR TypeIndex_Directory[] = { 2, 2, 0 };
const UCHAR TypeIndex_SymbolicLink[] = { 3, 3, 0 };
const UCHAR TypeIndex_Token[] = { 4, 4, 0 };
const UCHAR TypeIndex_Process[] = { 5, 6, 0 };
const UCHAR TypeIndex_Thread[] = { 6, 7, 0 };
const UCHAR TypeIndex_Job[] = { 7, 5, 0 };
const UCHAR TypeIndex_DebugObject[] = { 8, 8, 0 };
const UCHAR TypeIndex_Event[] = { 9, 9, 0 };
const UCHAR TypeIndex_EventPair[] = { 10, 10, 0 };
const UCHAR TypeIndex_Mutant[] = { 11, 11, 0 };
const UCHAR TypeIndex_Callback[] = { 12, 12, 0 };
const UCHAR TypeIndex_Semaphore[] = { 13, 13, 0 };
const UCHAR TypeIndex_Timer[] = { 14, 14, 0 };
const UCHAR TypeIndex_Profile[] = { 15, 15, 0 };
const UCHAR TypeIndex_KeyedEvent[] = { 16, 16, 0 };
const UCHAR TypeIndex_WindowStation[] = { 17, 17, 0 };
const UCHAR TypeIndex_Desktop[] = { 18, 18, 0 };
const UCHAR TypeIndex_Section[] = { 19, 30, 0 };
const UCHAR TypeIndex_Key[] = { 20, 32, 0 };
const UCHAR TypeIndex_Port[] = { 21, 21, 0 };
const UCHAR TypeIndex_WaitablePort[] = { 22, 22, 0 };
const UCHAR TypeIndex_Adapter[] = { 23, 20, 0 };
const UCHAR TypeIndex_Controller[] = { 24, 21, 0 };
const UCHAR TypeIndex_Device[] = { 25, 22, 0 };
const UCHAR TypeIndex_Driver[] = { 26, 23, 0 };
const UCHAR TypeIndex_IoCompletion[] = { 27, 24, 0 };
const UCHAR TypeIndex_File[] = { 28, 25, 0 };
const UCHAR TypeIndex_WmiGuid[] = { 29, 34, 0 };
const UCHAR TypeIndex_FilterConnectionPort[] = { 30, 36, 0 };
const UCHAR TypeIndex_FilterCommunicationPort[] = { 31, 37, 0 };

static
ULONG
GetNtDdiIndex(ULONG NtDdiVersion)
{
    switch (NtDdiVersion)
    {
        case NTDDI_WS03: return 0;
        case NTDDI_VISTA: return 1;
        case NTDDI_VISTASP1: return 1;
        case NTDDI_VISTASP2: return 1;
        case NTDDI_VISTASP3: return 1;
        case NTDDI_WIN7: return 1;
        case NTDDI_WIN11_GE: return 2;
        default:
            trace("Unsupported NTDDI version 0x%lx\n", NtDdiVersion);
            return 0;
    }
}

#define GetTypeIndex(TypeName, NtDdiVersion) \
    (TypeIndex_ ## TypeName[GetNtDdiIndex(NtDdiVersion)])

static
NTSTATUS
GetObjectTypeStatus(
    IN PCWSTR TypeName,
    OUT POBJECT_TYPE *ObjectType)
{
    NTSTATUS Status;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    POBJECT_TYPE TypeObjectType;
    HANDLE Handle = NULL;

    *ObjectType = NULL;

    RtlInitUnicodeString(&Name, TypeName);
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    TypeObjectType = ObGetObjectType(*PsProcessType);
    if (TypeObjectType == NULL) return STATUS_UNSUCCESSFUL;
    Status = ObOpenObjectByName(&ObjectAttributes, TypeObjectType, KernelMode, NULL, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ObReferenceObjectByHandle(Handle, 0, NULL, KernelMode, (PVOID*)ObjectType, NULL);
    ZwClose(Handle);
    return Status;
}

static
POBJECT_TYPE
GetObjectType(
    IN PCWSTR TypeName)
{
    NTSTATUS Status;
    POBJECT_TYPE ObjectType;

    Status = GetObjectTypeStatus(TypeName, &ObjectType);
    ok(Status == STATUS_SUCCESS, "GetObjectTypeStatus failed for '%S': %08x\n", TypeName, Status);
    ok(ObjectType != NULL, "ObjectType = NULL for '%S'\n", TypeName);
    return ObjectType;
}

#define ok_eq_ustr(value, expected) ok(RtlEqualUnicodeString(value, expected, FALSE), #value " = \"%wZ\", expected \"%wZ\"\n", value, expected)

enum WIN11_CALLBACK_FLAGS
{
    W11_OPEN = 0x01,
    W11_CLOSE = 0x02,
    W11_DELETE = 0x04,
    W11_PARSE = 0x08,
    W11_SECURITY = 0x10,
    W11_QUERY = 0x20,
    W11_OKAY_TO_CLOSE = 0x40
};

typedef struct _WIN11_OBJECT_TYPE_EXPECTATION
{
    PCSTR Name;
    USHORT Length;
    USHORT ObjectTypeFlags;
    ULONG ObjectTypeCode;
    ULONG RetainAccess;
    ULONG DefaultPagedPoolCharge;
    ULONG DefaultNonPagedPoolCharge;
    ULONG WaitObjectFlagMask;
    USHORT WaitObjectFlagOffset;
    USHORT WaitObjectPointerOffset;
    UCHAR CallbackFlags;
} WIN11_OBJECT_TYPE_EXPECTATION, *PWIN11_OBJECT_TYPE_EXPECTATION;

#ifdef _M_ARM64
static const WIN11_OBJECT_TYPE_EXPECTATION Win11ObjectTypeExpectations[] =
{
    { "Type",                    120, 0x0024, 0x000, 0x000000,    0,  312, 0x00000000,  0,  0, W11_SECURITY },
    { "Directory",               120, 0x000d, 0x000, 0x000000,   88,  344, 0x00000000,  0,  0, W11_CLOSE | W11_DELETE | W11_SECURITY },
    { "SymbolicLink",            120, 0x0105, 0x000, 0x000000,   88,   40, 0x00000000,  0,  0, W11_DELETE | W11_PARSE | W11_SECURITY },
    { "Token",                   128, 0x000e, 0x200, 0x000000,   88,    0, 0x00000000,  0,  0, W11_DELETE | W11_SECURITY },
    { "Process",                 128, 0x00ca, 0x020, 0x101000, 4096, 2392, 0x00000000,  0,  0, W11_OPEN | W11_CLOSE | W11_DELETE | W11_SECURITY },
    { "Thread",                  128, 0x00ca, 0x004, 0x101800,    0, 1984, 0x00000000,  0,  0, W11_OPEN | W11_DELETE | W11_SECURITY },
    { "Job",                     128, 0x0008, 0x800, 0x000000,    0, 1920, 0x00000000,  0,  0, W11_CLOSE | W11_DELETE | W11_SECURITY },
    { "DebugObject",             120, 0x0008, 0x000, 0x000000,    0,   88, 0x00000000,  0,  0, W11_CLOSE | W11_DELETE | W11_SECURITY },
    { "Event",                   120, 0x0000, 0x010, 0x000000,    0,  112, 0x00000000,  0,  0, W11_SECURITY },
    { "Mutant",                  120, 0x0000, 0x040, 0x000000,    0,  144, 0x00000000,  0,  0, W11_DELETE | W11_SECURITY },
    { "Callback",                120, 0x0004, 0x000, 0x000000,    0,   88, 0x00000000,  0,  0, W11_DELETE | W11_SECURITY },
    { "Semaphore",               120, 0x0000, 0x008, 0x000000,    0,  120, 0x00000000,  0,  0, W11_SECURITY },
    { "Timer",                   120, 0x0000, 0x000, 0x000000,    0,  416, 0x00000000,  0,  0, W11_DELETE | W11_SECURITY },
    { "Profile",                 120, 0x0000, 0x000, 0x000000,    0,  424, 0x00000000,  0,  0, W11_DELETE | W11_SECURITY },
    { "KeyedEvent",              120, 0x0004, 0x000, 0x000000,   88,    0, 0x00000000,  0,  0, W11_SECURITY },
    { "WindowStation",           120, 0x0018, 0x000, 0x000000,    0,  272, 0x00000000,  0,  0, W11_OPEN | W11_CLOSE | W11_DELETE | W11_PARSE | W11_SECURITY | W11_OKAY_TO_CLOSE },
    { "Desktop",                 120, 0x0058, 0x000, 0x000000,    0,  360, 0x00000000,  0,  0, W11_OPEN | W11_CLOSE | W11_DELETE | W11_SECURITY | W11_OKAY_TO_CLOSE },
    { "Section",                 120, 0x0004, 0x080, 0x000000,  152,    0, 0x00000000,  0,  0, W11_OPEN | W11_CLOSE | W11_DELETE | W11_SECURITY },
    { "Key",                     120, 0x010d, 0x100, 0x000000,  200,    0, 0x00000000,  0,  0, W11_CLOSE | W11_DELETE | W11_PARSE | W11_SECURITY | W11_QUERY },
    { "Adapter",                 120, 0x0004, 0x000, 0x000000,    0,   88, 0x00000000,  0,  0, W11_SECURITY },
    { "Controller",              120, 0x0004, 0x000, 0x000000,    0,  160, 0x00000000,  0,  0, W11_SECURITY },
    { "Device",                  120, 0x0105, 0x000, 0x000000,    0,  424, 0x00000000,  0,  0, W11_DELETE | W11_PARSE | W11_SECURITY },
    { "Driver",                  120, 0x0005, 0x000, 0x000000,    0,  424, 0x00000000,  0,  0, W11_DELETE | W11_SECURITY },
    { "IoCompletion",            120, 0x0081, 0x000, 0x000000,    0,  168, 0x00000000,  0,  0, W11_CLOSE | W11_DELETE | W11_SECURITY },
    { "File",                    120, 0x0111, 0x001, 0x000000, 1024,  384, 0x10000000, 80, 32, W11_CLOSE | W11_DELETE | W11_PARSE | W11_SECURITY | W11_QUERY },
    { "WmiGuid",                 120, 0x0008, 0x000, 0x000000,    0,  256, 0x00000000,  0,  0, W11_DELETE | W11_SECURITY },
    { "FilterConnectionPort",    120, 0x000c, 0x000, 0x000000,    0,  432, 0x00000000,  0,  0, W11_CLOSE | W11_DELETE | W11_SECURITY },
    { "FilterCommunicationPort", 120, 0x0004, 0x000, 0x000000,    0,  432, 0x00000000,  0,  0, W11_CLOSE | W11_DELETE | W11_SECURITY }
};

static
VOID
TestGrantedAccessUpgrades(VOID)
{
    OBJECT_BASIC_INFORMATION BasicInfo;
    OBJECT_ATTRIBUTES ObjectAttributes;
    CLIENT_ID ClientId;
    ULONG ReturnLength;
    HANDLE Handle;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    ClientId.UniqueProcess = PsGetCurrentProcessId();
    ClientId.UniqueThread = NULL;

    Status = ZwOpenProcess(&Handle, PROCESS_QUERY_INFORMATION, &ObjectAttributes, &ClientId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQueryObject(Handle, ObjectBasicInformation, &BasicInfo, sizeof(BasicInfo), &ReturnLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ok_eq_hex(BasicInfo.GrantedAccess, PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION);
        ZwClose(Handle);
    }

    Status = ZwOpenProcess(&Handle, PROCESS_VM_OPERATION | PROCESS_VM_WRITE, &ObjectAttributes, &ClientId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQueryObject(Handle, ObjectBasicInformation, &BasicInfo, sizeof(BasicInfo), &ReturnLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ok_eq_hex(BasicInfo.GrantedAccess, PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_LIMITED_INFORMATION);
        ZwClose(Handle);
    }

    Status = ZwOpenProcess(&Handle, PROCESS_SET_INFORMATION, &ObjectAttributes, &ClientId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQueryObject(Handle, ObjectBasicInformation, &BasicInfo, sizeof(BasicInfo), &ReturnLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ok_eq_hex(BasicInfo.GrantedAccess, PROCESS_SET_INFORMATION | PROCESS_SET_LIMITED_INFORMATION);
        ZwClose(Handle);
    }

    ClientId.UniqueProcess = PsGetCurrentProcessId();
    ClientId.UniqueThread = PsGetCurrentThreadId();
    Status = ZwOpenThread(&Handle, THREAD_QUERY_INFORMATION | THREAD_SET_INFORMATION | THREAD_SUSPEND_RESUME, &ObjectAttributes, &ClientId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQueryObject(Handle, ObjectBasicInformation, &BasicInfo, sizeof(BasicInfo), &ReturnLength);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status)) ok_eq_hex(BasicInfo.GrantedAccess, THREAD_QUERY_INFORMATION | THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_INFORMATION | THREAD_SET_LIMITED_INFORMATION | THREAD_SUSPEND_RESUME | THREAD_RESUME);
        ZwClose(Handle);
    }
}
#endif

template<typename ObjectType>
static
VOID
CheckWin11ObjectTypeMetadata(
    ObjectType*,
    PCSTR)
{
}

#ifdef _M_ARM64
static
VOID
CheckWin11ObjectTypeMetadata(
    TOBJECT_TYPE<NTDDI_WIN11_GE>* ObjectType,
    PCSTR TypeName)
{
    ULONG Index;
    ULONG ExpectedMandatoryLabelMask = 0;
    const WIN11_OBJECT_TYPE_EXPECTATION *Expected = NULL;

    for (Index = 0; Index < RTL_NUMBER_OF(Win11ObjectTypeExpectations); Index++)
    {
        if (!strcmp(TypeName, Win11ObjectTypeExpectations[Index].Name)) Expected = &Win11ObjectTypeExpectations[Index];
    }
    ok(Expected != NULL, "No Win11 metadata expectation for %s\n", TypeName);
    if (Expected == NULL) return;
    ok_eq_ulong(ObjectType->TypeInfo.Length, Expected->Length);
    ok_eq_hex(ObjectType->TypeInfo.ObjectTypeFlags, Expected->ObjectTypeFlags);
    ok_eq_hex(ObjectType->TypeInfo.ObjectTypeCode, Expected->ObjectTypeCode);
    ok_eq_hex(ObjectType->TypeInfo.RetainAccess, Expected->RetainAccess);
    ok_eq_ulong(ObjectType->TypeInfo.DefaultPagedPoolCharge, Expected->DefaultPagedPoolCharge);
    ok_eq_ulong(ObjectType->TypeInfo.DefaultNonPagedPoolCharge, Expected->DefaultNonPagedPoolCharge);
    ok_eq_hex(ObjectType->TypeInfo.WaitObjectFlagMask, Expected->WaitObjectFlagMask);
    ok_eq_ulong(ObjectType->TypeInfo.WaitObjectFlagOffset, Expected->WaitObjectFlagOffset);
    ok_eq_ulong(ObjectType->TypeInfo.WaitObjectPointerOffset, Expected->WaitObjectPointerOffset);
    ok_eq_bool(ObjectType->TypeInfo.DumpProcedure != NULL, FALSE);
    ok_eq_bool(ObjectType->TypeInfo.OpenProcedure != NULL, !!(Expected->CallbackFlags & W11_OPEN));
    ok_eq_bool(ObjectType->TypeInfo.CloseProcedure != NULL, !!(Expected->CallbackFlags & W11_CLOSE));
    ok_eq_bool(ObjectType->TypeInfo.DeleteProcedure != NULL, !!(Expected->CallbackFlags & W11_DELETE));
    ok_eq_bool(ObjectType->TypeInfo.ParseProcedure != NULL, !!(Expected->CallbackFlags & W11_PARSE));
    ok_eq_bool(ObjectType->TypeInfo.SecurityProcedure != NULL, !!(Expected->CallbackFlags & W11_SECURITY));
    ok_eq_bool(ObjectType->TypeInfo.QueryNameProcedure != NULL, !!(Expected->CallbackFlags & W11_QUERY));
    ok_eq_bool(ObjectType->TypeInfo.OkayToCloseProcedure != NULL, !!(Expected->CallbackFlags & W11_OKAY_TO_CLOSE));
    if (!strcmp(TypeName, "Token") || !strcmp(TypeName, "Job")) ExpectedMandatoryLabelMask = 1;
    if (!strcmp(TypeName, "Process") || !strcmp(TypeName, "Thread")) ExpectedMandatoryLabelMask = 3;
    ok_eq_hex(ObjectType->SeMandatoryLabelMask, ExpectedMandatoryLabelMask);
    ok_eq_hex(ObjectType->SeTrustConstraintMask, 0);
}
#endif

#define CheckObjectType(TypeName, Variable, Flags, InvalidAttr,                     \
                        ReadMapping, WriteMapping, ExecMapping, AllMapping,         \
                        ValidMask) do                                               \
{                                                                                   \
    OBJECT_TYPE* ObjectType;                                                        \
    UNICODE_STRING Name;                                                            \
    ULONG Key;                                                                      \
    BOOLEAN UseDefault = ((Flags) & OBT_NO_DEFAULT) == 0;                           \
    BOOLEAN CustomSecurityProc = ((Flags) & OBT_CUSTOM_SECURITY_PROC) != 0;         \
    BOOLEAN SecurityRequired = ((Flags) & OBT_SECURITY_REQUIRED) != 0;              \
    BOOLEAN CaseInsensitive = ((Flags) & OBT_CASE_INSENSITIVE) != 0;                \
    BOOLEAN MaintainTypeList = ((Flags) & OBT_MAINTAIN_TYPE_LIST) != 0;             \
    BOOLEAN MaintainHandleCount = ((Flags) & OBT_MAINTAIN_HANDLE_COUNT) != 0;       \
    POOL_TYPE PoolType = ((Flags) & OBT_PAGED_POOL) ? PagedPool : ((NtDdiVersion >= NTDDI_WIN11_GE) ? NonPagedPoolNx : NonPagedPool); \
    ULONG ExpectedLength = ((NtDdiVersion >= NTDDI_WIN11_GE) && ((Flags) & OBT_LENGTH_128)) ? 128 : sizeof(OBJECT_TYPE_INITIALIZER); \
    BOOLEAN CustomKey = ((Flags) & OBT_CUSTOM_KEY) != 0;                            \
    ULONG Index = GetTypeIndex(TypeName, NtDdiVersion);                             \
                                                                                    \
    trace(#TypeName "\n");                                                          \
    ObjectType = (OBJECT_TYPE*)GetObjectType(L"\\ObjectTypes\\" L ## #TypeName);    \
    ok(ObjectType != NULL, "ObjectType = NULL\n");                                  \
    if (!skip(ObjectType != NULL, "No ObjectType\n"))                               \
    {                                                                               \
        ok(!Variable || (OBJECT_TYPE*)Variable == (OBJECT_TYPE*)ObjectType,         \
           #Variable "is %p, expected %p\n", Variable, ObjectType);                 \
        CheckWin11ObjectTypeMetadata(ObjectType, #TypeName);                       \
        RtlInitUnicodeString(&Name, L ## #TypeName);                                \
        ok_eq_ustr(&ObjectType->Name, &Name);                                       \
        if (Index)                                                                 \
            ok_eq_ulong(ObjectType->Index, Index);                                 \
        else                                                                       \
            ok(ObjectType->Index != 0, "Object type index is zero\n");             \
        /* apparently, File and WaitablePort are evil and have other stuff          \
         * in DefaultObject. All others are NULL */                                 \
        if (UseDefault)                                                             \
            ok_eq_pointer(ObjectType->DefaultObject, ObpDefaultObject);             \
        /*ok(ObjectType->TotalNumberOfObjects >= 1,                                 \
           "Number of objects = %lu\n", ObjectType->TotalNumberOfObjects);          \
        ok(ObjectType->TotalNumberOfHandles >= ObjectType->TotalNumberOfObjects,    \
           "%lu objects, but %lu handles\n",                                        \
           ObjectType->TotalNumberOfObjects, ObjectType->TotalNumberOfHandles);*/   \
        ok(ObjectType->HighWaterNumberOfObjects >= ObjectType->TotalNumberOfObjects,\
           "%lu objects, but high water %lu\n",                                     \
           ObjectType->TotalNumberOfObjects, ObjectType->HighWaterNumberOfObjects); \
        ok(ObjectType->HighWaterNumberOfHandles >= ObjectType->TotalNumberOfHandles,\
           "%lu handles, but high water %lu\n",                                     \
           ObjectType->TotalNumberOfHandles, ObjectType->HighWaterNumberOfHandles); \
        ok_eq_ulong(ObjectType->TypeInfo.Length, ExpectedLength);                   \
        ok_eq_bool(ObjectType->TypeInfo.UseDefaultObject, UseDefault);              \
        ok_eq_bool(ObjectType->TypeInfo.CaseInsensitive, CaseInsensitive);          \
        ok_eq_hex(ObjectType->TypeInfo.InvalidAttributes, InvalidAttr);             \
        ok_eq_hex(ObjectType->TypeInfo.GenericMapping.GenericRead, ReadMapping);    \
        ok_eq_hex(ObjectType->TypeInfo.GenericMapping.GenericWrite, WriteMapping);  \
        ok_eq_hex(ObjectType->TypeInfo.GenericMapping.GenericExecute, ExecMapping); \
        ok_eq_hex(ObjectType->TypeInfo.GenericMapping.GenericAll, AllMapping);      \
        ok_eq_hex(ObjectType->TypeInfo.ValidAccessMask, ValidMask);                 \
        ok_eq_bool(ObjectType->TypeInfo.SecurityRequired, SecurityRequired);        \
        ok_eq_bool(ObjectType->TypeInfo.MaintainHandleCount, MaintainHandleCount);  \
        ok_eq_bool(ObjectType->TypeInfo.MaintainTypeList, MaintainTypeList);        \
        ok_eq_ulong(ObjectType->TypeInfo.PoolType, PoolType);                       \
        /* DefaultPagedPoolCharge */                                                \
        /* DefaultNonPagedPoolCharge */                                             \
        /* DumpProcedure */                                                         \
        /* OpenProcedure */                                                         \
        /* CloseProcedure */                                                        \
        /* DeleteProcedure */                                                       \
        /* ParseProcedure */                                                        \
        if (CustomSecurityProc)                                                     \
            ok(ObjectType->TypeInfo.SecurityProcedure != NULL,                      \
               "No Security Proc\n");                                               \
        else                                                                        \
            ok_eq_pointer(ObjectType->TypeInfo.SecurityProcedure,                   \
                          SeDefaultObjectMethod);                                   \
        /* QueryNameProcedure */                                                    \
        /* OkayToCloseProcedure */                                                  \
        Key = *(PULONG)#TypeName;                                                   \
        if (sizeof(#TypeName) <= 4) Key |= ' ' << 24;                               \
        if (sizeof(#TypeName) <= 3) Key |= ' ' << 16;                               \
        if (sizeof(#TypeName) <= 2) Key |= ' ' <<  8;                               \
        if (!CustomKey)                                                             \
            ok_eq_hex(ObjectType->Key, Key);                                        \
        ObDereferenceObject(ObjectType);                                            \
    }                                                                               \
} while (0)

#define ObpDirectoryObjectType      NULL
#define ObpSymbolicLinkObjectType   NULL
#define DbgkDebugObjectType         NULL
#define ExEventPairObjectType       NULL
#define ExMutantObjectType          NULL
#define ExCallbackObjectType        NULL
#define ExTimerObjectType           NULL
#define ExProfileObjectType         NULL
#define ExpKeyedEventObjectType     NULL
#define CmpKeyObjectType            NULL
#define LpcWaitablePortObjectType   NULL
#define IoControllerObjectType      NULL
#define IoCompletionObjectType      NULL
#define WmipGuidObjectType          NULL

#define OBT_NO_DEFAULT              0x01
#define OBT_CUSTOM_SECURITY_PROC    0x02
#define OBT_SECURITY_REQUIRED       0x04
#define OBT_CASE_INSENSITIVE        0x08
#define OBT_MAINTAIN_TYPE_LIST      0x10
#define OBT_MAINTAIN_HANDLE_COUNT   0x20
#define OBT_PAGED_POOL              0x40
#define OBT_CUSTOM_KEY              0x80
#define OBT_LENGTH_128              0x100

#define TAG(x) RtlUlongByteSwap(x)

static
VOID
TestFileWaitObject(VOID)
{
    UNICODE_STRING NullName = RTL_CONSTANT_STRING(L"\\Device\\Null");
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PFILE_OBJECT FileObject = NULL;
    PVOID OriginalFsContext2;
    ULONG OriginalFlags;
    LONG OriginalEventState;
    KEVENT IndirectEvent;
    LARGE_INTEGER ZeroTimeout;
    HANDLE FileHandle = NULL;
    HANDLE Handles[1];
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes, &NullName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwOpenFile(&FileHandle, FILE_READ_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatusBlock, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ObReferenceObjectByHandle(FileHandle, SYNCHRONIZE, *IoFileObjectType, KernelMode, (PVOID*)&FileObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(FileHandle);
        return;
    }

    OriginalFsContext2 = FileObject->FsContext2;
    OriginalFlags = FileObject->Flags;
    OriginalEventState = KeReadStateEvent(&FileObject->Event);
    ZeroTimeout.QuadPart = 0;
    Handles[0] = FileHandle;

    FileObject->Flags &= ~FO_INDIRECT_WAIT_OBJECT;
    KeSetEvent(&FileObject->Event, IO_NO_INCREMENT, FALSE);
    Status = ZwWaitForSingleObject(FileHandle, FALSE, &ZeroTimeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ZwWaitForMultipleObjects(1, Handles, WaitAny, FALSE, &ZeroTimeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    KeClearEvent(&FileObject->Event);
    Status = ZwWaitForSingleObject(FileHandle, FALSE, &ZeroTimeout);
    ok_eq_hex(Status, STATUS_TIMEOUT);

    KeInitializeEvent(&IndirectEvent, NotificationEvent, TRUE);
    FileObject->FsContext2 = &IndirectEvent;
    FileObject->Flags |= FO_INDIRECT_WAIT_OBJECT;
    Status = ZwWaitForSingleObject(FileHandle, FALSE, &ZeroTimeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ZwWaitForMultipleObjects(1, Handles, WaitAny, FALSE, &ZeroTimeout);
    ok_eq_hex(Status, STATUS_SUCCESS);

    FileObject->FsContext2 = OriginalFsContext2;
    FileObject->Flags = OriginalFlags;
    if (OriginalEventState) KeSetEvent(&FileObject->Event, IO_NO_INCREMENT, FALSE);
    else KeClearEvent(&FileObject->Event);
    ObDereferenceObject(FileObject);
    Status = ZwClose(FileHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

template<unsigned NtDdiVersion>
static
VOID
TestObjectTypes(VOID)
{
    using OBJECT_TYPE = TOBJECT_TYPE<NtDdiVersion>;
    static OBJECT_TYPE* ObpTypeObjectType;
    static OBJECT_TYPE* ObpDefaultObject;
    using OB_SECURITY_METHOD = decltype(ObpTypeObjectType->TypeInfo.SecurityProcedure);
    static OB_SECURITY_METHOD SeDefaultObjectMethod;

    ObpTypeObjectType = (OBJECT_TYPE*)GetObjectType(L"\\ObjectTypes\\Type");
    if (skip(ObpTypeObjectType != NULL, "No Type object type\n"))
        return;

    ObpDefaultObject = (OBJECT_TYPE*)ObpTypeObjectType->DefaultObject;
    ok(ObpDefaultObject != NULL, "No ObpDefaultObject\n");
    SeDefaultObjectMethod = ObpTypeObjectType->TypeInfo.SecurityProcedure;
    ok(SeDefaultObjectMethod != NULL, "No SeDefaultObjectMethod\n");

#ifdef _PROPER_NT_NDK_EXPORTS
#define ObpTypeObjectType *ObpTypeObjectType
#define ObpDirectoryObjectType *ObpDirectoryObjectType
#define ObpSymbolicLinkObjectType *ObpSymbolicLinkObjectType
#define PsJobType *PsJobType
#define DbgkDebugObjectType *DbgkDebugObjectType
#define ExEventPairObjectType *ExEventPairObjectType
#define ExMutantObjectType *ExMutantObjectType
#define ExCallbackObjectType *ExCallbackObjectType
#define ExTimerObjectType *ExTimerObjectType
#define ExProfileObjectType *ExProfileObjectType
#define ExpKeyedEventObjectType *ExpKeyedEventObjectType
#define ExWindowStationObjectType *ExWindowStationObjectType
#define ExDesktopObjectType *ExDesktopObjectType
#define MmSectionObjectType *MmSectionObjectType
#define CmpKeyObjectType *CmpKeyObjectType
#define LpcPortObjectType *LpcPortObjectType
#define LpcWaitablePortObjectType *LpcWaitablePortObjectType
#define IoAdapterObjectType *IoAdapterObjectType
#define IoControllerObjectType *IoControllerObjectType
#define IoDeviceObjectType *IoDeviceObjectType
#define IoDriverObjectType *IoDriverObjectType
#define IoCompletionObjectType *IoCompletionObjectType
#define WmipGuidObjectType *WmipGuidObjectType
#endif

#define DIR_SEC_REQUIRED ((NtDdiVersion >= NTDDI_VISTA) ? OBT_SECURITY_REQUIRED : 0)
#define TOKEN_GENERIC_READ ((NtDdiVersion >= NTDDI_VISTA) ? 0x02001a : 0x020008)
#define TOKEN_GENERIC_WRITE ((NtDdiVersion >= NTDDI_VISTA) ? 0x0201e0 : 0x0200e0)
#define TOKEN_GENERIC_EXECUTE ((NtDdiVersion >= NTDDI_VISTA) ? 0x020005 : 0x020000)
#define PROCESS_GENERIC_WRITE ((NtDdiVersion >= NTDDI_VISTA) ? 0x020bea : 0x020beb)
#define PROCESS_GENERIC_EXECUTE ((NtDdiVersion >= NTDDI_VISTA) ? 0x121001 : 0x120000)
#define PROCESS_GENERIC_ALL ((NtDdiVersion >= NTDDI_VISTA) ? 0x001fffff : 0x1f0fff)
#define THREAD_GENERIC_WRITE ((NtDdiVersion >= NTDDI_VISTA) ? 0x020437 : 0x020037)
#define THREAD_GENERIC_EXECUTE ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0x121800 : ((NtDdiVersion >= NTDDI_VISTA) ? 0x120800 : 0x120000))
#define THREAD_GENERIC_ALL ((NtDdiVersion >= NTDDI_VISTA) ? 0x1fffff : 0x1f03ff)
#define JOB_GENERIC_ALL ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0x1f003f : ((NtDdiVersion >= NTDDI_VISTA) ? 0x1f001f : 0x1f03ff))
#define KEY_GENERIC_EXECUTE ((NtDdiVersion >= NTDDI_VISTA) ? 0x020039 : 0x020019)
#define SYMBOLIC_LINK_VALID_MASK ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0x000fffff : 0x000f0001)
#define KEY_FLAGS (OBT_CUSTOM_SECURITY_PROC | OBT_SECURITY_REQUIRED | OBT_PAGED_POOL | ((NtDdiVersion >= NTDDI_WIN11_GE) ? OBT_CASE_INSENSITIVE : 0))
#define CALLBACK_FLAGS ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0 : OBT_NO_DEFAULT)
#define IO_COMPLETION_FLAGS (OBT_CASE_INSENSITIVE | ((NtDdiVersion >= NTDDI_WIN11_GE) ? OBT_NO_DEFAULT : 0))
#define FILTER_CONNECTION_FLAGS (OBT_SECURITY_REQUIRED | ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0 : OBT_NO_DEFAULT))
#define FILTER_COMMUNICATION_FLAGS ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0 : OBT_NO_DEFAULT)
#define WMI_GENERIC_ALL ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0x121fff : 0x120fff)
#define WMI_VALID_MASK ((NtDdiVersion >= NTDDI_WIN11_GE) ? 0x1f1fff : 0x1f0fff)

    CheckObjectType(Type, ObpTypeObjectType,                    OBT_MAINTAIN_TYPE_LIST | OBT_CUSTOM_KEY,    0x100,  0x020000, 0x020000, 0x020000, 0x0f0001, 0x1f0001);
        ok_eq_hex(ObpTypeObjectType->Key, TAG('ObjT'));
    CheckObjectType(Directory, ObpDirectoryObjectType,          DIR_SEC_REQUIRED | OBT_CASE_INSENSITIVE | OBT_PAGED_POOL,      0x100,  0x020003, 0x02000c, 0x020003, 0x0f000f, 0x0f000f);
    CheckObjectType(SymbolicLink, ObpSymbolicLinkObjectType,    OBT_CASE_INSENSITIVE | OBT_PAGED_POOL,      0x100,  0x020001, 0x020000, 0x020001, 0x0f0001, SYMBOLIC_LINK_VALID_MASK);
    CheckObjectType(Token, *SeTokenObjectType,                   OBT_SECURITY_REQUIRED | OBT_PAGED_POOL | OBT_LENGTH_128, 0x100, TOKEN_GENERIC_READ, TOKEN_GENERIC_WRITE, TOKEN_GENERIC_EXECUTE, 0x0f01ff, 0x1f01ff);
    CheckObjectType(Process, *PsProcessType,                     OBT_NO_DEFAULT | OBT_SECURITY_REQUIRED | OBT_LENGTH_128, 0x0b0, 0x020410, PROCESS_GENERIC_WRITE, PROCESS_GENERIC_EXECUTE, PROCESS_GENERIC_ALL, PROCESS_GENERIC_ALL);
    CheckObjectType(Thread, *PsThreadType,                       OBT_NO_DEFAULT | OBT_SECURITY_REQUIRED | OBT_LENGTH_128, 0x0b0, 0x020048, THREAD_GENERIC_WRITE, THREAD_GENERIC_EXECUTE, THREAD_GENERIC_ALL, THREAD_GENERIC_ALL);
    CheckObjectType(Job, PsJobType,                             OBT_NO_DEFAULT | OBT_SECURITY_REQUIRED | OBT_LENGTH_128, 0x000, 0x020004, 0x02000b, 0x120000, JOB_GENERIC_ALL, JOB_GENERIC_ALL);
    CheckObjectType(DebugObject, DbgkDebugObjectType,           OBT_NO_DEFAULT | OBT_SECURITY_REQUIRED,     0x000,  0x020001, 0x020002, 0x120000, 0x1f000f, 0x1f000f);
    CheckObjectType(Event, *ExEventObjectType,                   OBT_NO_DEFAULT,                             0x100,  0x020001, 0x020002, 0x120000, 0x1f0003, 0x1f0003);
    if (NtDdiVersion >= NTDDI_WIN11_GE)
    {
        NTSTATUS Status;
        POBJECT_TYPE EventPairObjectType;
        PKMT_RESPONSE EventPairResponse;

        Status = GetObjectTypeStatus(L"\\ObjectTypes\\EventPair", &EventPairObjectType);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        ok_eq_pointer(EventPairObjectType, NULL);
        EventPairResponse = KmtUserModeCallback(QueryEventPairBehavior, NULL);
        if (!skip(EventPairResponse != NULL, "No EventPair user-mode response\n"))
        {
            ok_eq_hex(EventPairResponse->EventPair.CreateStatus, STATUS_NOT_IMPLEMENTED);
            ok_eq_pointer(EventPairResponse->EventPair.CreateHandle, UlongToHandle(0x55555555));
            ok_eq_hex(EventPairResponse->EventPair.OpenStatus, STATUS_NOT_IMPLEMENTED);
            ok_eq_pointer(EventPairResponse->EventPair.OpenHandle, UlongToHandle(0x55555555));
            ok_eq_hex(EventPairResponse->EventPair.SetHighStatus, STATUS_NOT_IMPLEMENTED);
            ok_eq_hex(EventPairResponse->EventPair.SetHighWaitLowStatus, STATUS_NOT_IMPLEMENTED);
            ok_eq_hex(EventPairResponse->EventPair.SetLowStatus, STATUS_NOT_IMPLEMENTED);
            ok_eq_hex(EventPairResponse->EventPair.SetLowWaitHighStatus, STATUS_NOT_IMPLEMENTED);
            ok_eq_hex(EventPairResponse->EventPair.WaitHighStatus, STATUS_NOT_IMPLEMENTED);
            ok_eq_hex(EventPairResponse->EventPair.WaitLowStatus, STATUS_NOT_IMPLEMENTED);
            KmtFreeCallbackResponse(EventPairResponse);
        }
    }
    else
    {
        CheckObjectType(EventPair, ExEventPairObjectType,       0,                                          0x100,  0x120000, 0x120000, 0x120000, 0x1f0000, 0x1f0000);
    }
    CheckObjectType(Mutant, ExMutantObjectType,                 OBT_NO_DEFAULT,                             0x100,  0x020001, 0x020000, 0x120000, 0x1f0001, 0x1f0001);
    CheckObjectType(Callback, ExCallbackObjectType,             CALLBACK_FLAGS,                            0x100,  0x020000, 0x020001, 0x120000, 0x1f0001, 0x1f0001);
    CheckObjectType(Semaphore, *ExSemaphoreObjectType,           OBT_NO_DEFAULT,                             0x100,  0x020001, 0x020002, 0x120000, 0x1f0003, 0x1f0003);
    CheckObjectType(Timer, ExTimerObjectType,                   OBT_NO_DEFAULT,                             0x100,  0x020001, 0x020002, 0x120000, 0x1f0003, 0x1f0003);
    CheckObjectType(Profile, ExProfileObjectType,               OBT_NO_DEFAULT,                             0x100,  0x020001, 0x020001, 0x020001, 0x0f0001, 0x0f0001);
    CheckObjectType(KeyedEvent, ExpKeyedEventObjectType,        OBT_PAGED_POOL,                             0x000,  0x020001, 0x020002, 0x020000, 0x0f0003, 0x1f0003);
    CheckObjectType(WindowStation, ExWindowStationObjectType,   OBT_NO_DEFAULT | OBT_SECURITY_REQUIRED | OBT_MAINTAIN_HANDLE_COUNT,
                                                                                                            0x130,  0x020303, 0x02001c, 0x020060, 0x0f037f, 0x0f037f);
    CheckObjectType(Desktop, ExDesktopObjectType,               OBT_NO_DEFAULT | OBT_SECURITY_REQUIRED | OBT_MAINTAIN_HANDLE_COUNT,
                                                                                                            0x130,  0x020041, 0x0200be, 0x020100, 0x0f01ff, 0x0f01ff);
    CheckObjectType(Section, MmSectionObjectType,               OBT_PAGED_POOL,                             0x100,  0x020005, 0x020002, 0x020008, 0x0f001f, 0x1f001f);
    CheckObjectType(Key, CmpKeyObjectType,                      KEY_FLAGS,
                                                                                                            0x030,  0x020019, 0x020006, KEY_GENERIC_EXECUTE, 0x0f003f, 0x1f003f);
    if (NtDdiVersion <= NTDDI_WS03)
    {
        // 0x7b2 is used for Server 2003 SP2 RTM, it seems it was changed to 0xfb2 in some patch level.
        CheckObjectType(Port, LpcPortObjectType,                    OBT_PAGED_POOL,                             0xfb2,  0x020001, 0x010001, 0x000000, 0x1f0001, 0x1f0001);
        // 0x7b2 is used for Server 2003 SP2 RTM, it seems it was changed to 0xfb2 in some patch level.
        CheckObjectType(WaitablePort, LpcWaitablePortObjectType,    OBT_NO_DEFAULT,                             0xfb2,  0x020001, 0x010001, 0x000000, 0x1f0001, 0x1f0001);
    }
    CheckObjectType(Adapter, IoAdapterObjectType,               0,                                          0x100,  0x120089, 0x120116, 0x1200a0, 0x1f01ff, 0x1f01ff);
    CheckObjectType(Controller, IoControllerObjectType,         0,                                          0x100,  0x120089, 0x120116, 0x1200a0, 0x1f01ff, 0x1f01ff);
    CheckObjectType(Device, IoDeviceObjectType,                 OBT_CUSTOM_SECURITY_PROC | OBT_CASE_INSENSITIVE,
                                                                                                            0x100,  0x120089, 0x120116, 0x1200a0, 0x1f01ff, 0x1f01ff);
    CheckObjectType(Driver, IoDriverObjectType,                 OBT_CASE_INSENSITIVE,                       0x100,  0x120089, 0x120116, 0x1200a0, 0x1f01ff, 0x1f01ff);
    CheckObjectType(IoCompletion, IoCompletionObjectType,       IO_COMPLETION_FLAGS,                       0x110,  0x020001, 0x020002, 0x120000, 0x1f0003, 0x1f0003);
    CheckObjectType(File, *IoFileObjectType,                     OBT_NO_DEFAULT | OBT_CUSTOM_SECURITY_PROC | OBT_CASE_INSENSITIVE | OBT_MAINTAIN_HANDLE_COUNT,
                                                                                                            0x130,  0x120089, 0x120116, 0x1200a0, 0x1f01ff, 0x1f01ff);
    if (NtDdiVersion >= NTDDI_WIN11_GE) TestFileWaitObject();
#ifdef _M_ARM64
    if (NtDdiVersion >= NTDDI_WIN11_GE) TestGrantedAccessUpgrades();
#endif
    CheckObjectType(WmiGuid, WmipGuidObjectType,                OBT_NO_DEFAULT | OBT_CUSTOM_SECURITY_PROC | OBT_SECURITY_REQUIRED,
                                                                                                            0x100,  0x000001, 0x000002, 0x000010, WMI_GENERIC_ALL, WMI_VALID_MASK);
    CheckObjectType(FilterConnectionPort, NULL,                 FILTER_CONNECTION_FLAGS,                   0x100,  0x020001, 0x010001, 0x000000, 0x1f0001, 0x1f0001);
    CheckObjectType(FilterCommunicationPort, NULL,              FILTER_COMMUNICATION_FLAGS,                0x100,  0x020001, 0x010001, 0x000000, 0x1f0001, 0x1f0001);

    // exported but not created
    ok_eq_pointer(IoDeviceHandlerObjectType, NULL);

    // my Win7/x64 additionally has:
    // ALPC Port
    // EtwConsumer
    // EtwRegistration
    // IoCompletionReserve
    // PcwObject
    // PowerRequest
    // Session
    // TmEn
    // TmRm
    // TmTm
    // TmTx
    // TpWorkerFactory
    // UserApcReserve
    // ... and does not have:
    // Port
    // WaitablePort

    ObDereferenceObject(ObpTypeObjectType);
}

START_TEST(ObTypes)
{
    ULONG NtDdiVersion = GetNTDDIVersion();

    switch (NtDdiVersion)
    {
        case NTDDI_WS03:
            TestObjectTypes<NTDDI_WS03>();
            return;
        case NTDDI_VISTA:
            TestObjectTypes<NTDDI_VISTA>();
            return;
        case NTDDI_VISTASP1:
        case NTDDI_VISTASP2:
        case NTDDI_VISTASP3:
            TestObjectTypes<NTDDI_VISTASP1>();
            return;
        case NTDDI_WIN7:
            TestObjectTypes<NTDDI_WIN7>();
            return;
        case NTDDI_WIN11_GE:
            TestObjectTypes<NTDDI_WIN11_GE>();
            return;
        default:
            skip(FALSE, "Unsupported NTDDI version: 0x%lx\n", NtDdiVersion);
            return;
    }
}
