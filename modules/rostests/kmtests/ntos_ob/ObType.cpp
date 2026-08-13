/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2+ - See COPYING.LIB in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Ob Regressions KM-Test
 * PROGRAMMER:      Aleksey Bragin <aleksey@reactos.org>
 *                  Thomas Faber <thomas.faber@reactos.org>
 */

/* TODO: split this into multiple tests! ObLife, ObHandle, ObName, ... */

#include <kmt_test.h>
#include "ObTypes.hpp"

#define NDEBUG
#include <debug.h>

#define CheckObject(Handle, Pointers, Handles) do                   \
{                                                                   \
    PUBLIC_OBJECT_BASIC_INFORMATION ObjectInfo;                     \
    Status = ZwQueryObject(Handle, ObjectBasicInformation,          \
                            &ObjectInfo, sizeof ObjectInfo, NULL);  \
    ok_eq_hex(Status, STATUS_SUCCESS);                              \
    trace("CheckObject(%p): pointers %lu (logical %lu), handles %lu (expected %lu)\n", Handle, ObjectInfo.PointerCount, (ULONG)(Pointers), ObjectInfo.HandleCount, (ULONG)(Handles)); \
    if (GetNTVersion() < _WIN32_WINNT_WIN8)                         \
        ok_eq_ulong(ObjectInfo.PointerCount, Pointers);            \
    ok_eq_ulong(ObjectInfo.HandleCount, Handles);                   \
} while (0)

#define NUM_OBTYPES 5

typedef struct _MY_OBJECT1
{
    ULONG Something1;
} MY_OBJECT1, *PMY_OBJECT1;

typedef struct _MY_OBJECT2
{
    ULONG Something1;
    ULONG SomeLong[10];
} MY_OBJECT2, *PMY_OBJECT2;

static PVOID                   ObTypes_[NUM_OBTYPES];
static UNICODE_STRING          ObTypeName[NUM_OBTYPES];
static UNICODE_STRING          ObName[NUM_OBTYPES];
static UNICODE_STRING          ObDirectoryName;
static OBJECT_ATTRIBUTES       ObDirectoryAttributes;
static OBJECT_ATTRIBUTES       ObAttributes[NUM_OBTYPES];
static PVOID                   ObBody[NUM_OBTYPES];
static HANDLE                  ObHandle1[NUM_OBTYPES];
static HANDLE                  DirectoryHandle;

typedef struct _COUNTS
{
    USHORT Dump;
    USHORT Open;
    USHORT Close;
    USHORT Delete;
    USHORT Parse;
    USHORT OkayToClose;
    USHORT QueryName;
} COUNTS, *PCOUNTS;
static COUNTS Counts;
static BOOLEAN ExtendedParseCalled;
static OB_EXTENDED_PARSE_PARAMETERS ExtendedParseParameters;
static ULONG ExtendedObjectSecurityMode;

static
VOID
NTAPI
DumpProc(
    IN PVOID Object,
    IN POB_DUMP_CONTROL DumpControl)
{
    DPRINT("DumpProc() called\n");
    ++Counts.Dump;
}

static
NTSTATUS
NTAPI
OpenProc(
    IN OB_OPEN_REASON OpenReason,
    IN PEPROCESS Process,
    IN PVOID Object,
    IN ACCESS_MASK GrantedAccess,
    IN ULONG HandleCount)
{
    DPRINT("OpenProc() 0x%p, OpenReason %d, HandleCount %lu, AccessMask 0x%lX\n",
        Object, OpenReason, HandleCount, GrantedAccess);
    ++Counts.Open;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
OpenProc_NT6(
    _In_ OB_OPEN_REASON OpenReason,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ PACCESS_MASK GrantedAccess,
    _In_ ULONG HandleCount)
{
    DPRINT("OpenProc() 0x%p, OpenReason %d, HandleCount %lu, AccessMask 0x%lX\n",
        Object, OpenReason, HandleCount, *GrantedAccess);
    ++Counts.Open;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
CloseProc(
    IN PEPROCESS Process,
    IN PVOID Object,
    IN ACCESS_MASK GrantedAccess,
    IN ULONG ProcessHandleCount,
    IN ULONG SystemHandleCount)
{
    DPRINT("CloseProc() 0x%p, ProcessHandleCount %lu, SystemHandleCount %lu, AccessMask 0x%lX\n",
        Object, ProcessHandleCount, SystemHandleCount, GrantedAccess);
    ++Counts.Close;
}

static
VOID
NTAPI
CloseProc_NT10(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    DPRINT("CloseProc_NT10() 0x%p, ProcessHandleCount %Iu, SystemHandleCount %Iu\n", Object, ProcessHandleCount, SystemHandleCount);
    ++Counts.Close;
}

static
VOID
NTAPI
DeleteProc(
    IN PVOID Object)
{
    DPRINT("DeleteProc() 0x%p\n", Object);
    ++Counts.Delete;
}

static
NTSTATUS
NTAPI
ParseProc(
    IN PVOID ParseObject,
    IN PVOID ObjectType,
    IN OUT PACCESS_STATE AccessState,
    IN KPROCESSOR_MODE AccessMode,
    IN ULONG Attributes,
    IN OUT PUNICODE_STRING CompleteName,
    IN OUT PUNICODE_STRING RemainingName,
    IN OUT PVOID Context OPTIONAL,
    IN PSECURITY_QUALITY_OF_SERVICE SecurityQos OPTIONAL,
    OUT PVOID *Object)
{
    DPRINT("ParseProc() called\n");
    *Object = NULL;

    ++Counts.Parse;
    return STATUS_OBJECT_NAME_NOT_FOUND;//STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
ParseProcEx(
    _In_ PVOID ParseObject,
    _In_ PVOID ObjectType,
    _Inout_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ ULONG Attributes,
    _Inout_ PUNICODE_STRING CompleteName,
    _Inout_ PUNICODE_STRING RemainingName,
    _Inout_opt_ PVOID Context,
    _In_opt_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _In_ POB_EXTENDED_PARSE_PARAMETERS ExtendedParameters,
    _Out_ PVOID *Object)
{
    DPRINT("ParseProcEx() called\n");
    *Object = NULL;
    ExtendedParseCalled = TRUE;
    if (ExtendedParameters != NULL) ExtendedParseParameters = *ExtendedParameters;
    ++Counts.Parse;
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static
BOOLEAN
NTAPI
OkayToCloseProc(
    IN PEPROCESS Process OPTIONAL,
    IN PVOID Object,
    IN HANDLE Handle,
    IN KPROCESSOR_MODE AccessMode)
{
    DPRINT("OkayToCloseProc() 0x%p, Handle 0x%p, AccessMask 0x%lX\n",
        Object, Handle, AccessMode);
    ++Counts.OkayToClose;
    return TRUE;
}

static
NTSTATUS
NTAPI
QueryNameProc(
    IN PVOID Object,
    IN BOOLEAN HasObjectName,
    OUT POBJECT_NAME_INFORMATION ObjectNameInfo,
    IN ULONG Length,
    OUT PULONG ReturnLength,
    IN KPROCESSOR_MODE AccessMode)
{
    DPRINT("QueryNameProc() 0x%p, HasObjectName %d, Len %lu, AccessMask 0x%lX\n",
        Object, HasObjectName, Length, AccessMode);
    ++Counts.QueryName;

    ObjectNameInfo = NULL;
    ReturnLength = 0;
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

template<typename Initializer>
static
VOID
SetCommonCallbacks(
    Initializer& TypeInitializer)
{
    TypeInitializer.DeleteProcedure = DeleteProc;
    TypeInitializer.DumpProcedure = DumpProc;
    TypeInitializer.OkayToCloseProcedure = OkayToCloseProc;
    TypeInitializer.QueryNameProcedure = QueryNameProc;
}

template<unsigned NtDdiVersion>
struct TObCallbackSetter
{
    template<typename Initializer>
    static
    VOID
    Set(
        Initializer& TypeInitializer)
    {
        using OPEN_PROCEDURE = decltype(TypeInitializer.OpenProcedure);
        SetCommonCallbacks(TypeInitializer);
        TypeInitializer.OpenProcedure = (OPEN_PROCEDURE)OpenProc_NT6;
        TypeInitializer.CloseProcedure = CloseProc;
        TypeInitializer.ParseProcedure = ParseProc;
    }
};

template<>
struct TObCallbackSetter<NTDDI_WS03>
{
    template<typename Initializer>
    static
    VOID
    Set(
        Initializer& TypeInitializer)
    {
        SetCommonCallbacks(TypeInitializer);
        TypeInitializer.OpenProcedure = OpenProc;
        TypeInitializer.CloseProcedure = CloseProc;
        TypeInitializer.ParseProcedure = ParseProc;
    }
};

template<>
struct TObCallbackSetter<NTDDI_WIN11_GE>
{
    template<typename Initializer>
    static
    VOID
    Set(
        Initializer& TypeInitializer)
    {
        SetCommonCallbacks(TypeInitializer);
        TypeInitializer.OpenProcedure = OpenProc_NT6;
        TypeInitializer.CloseProcedure = CloseProc_NT10;
        TypeInitializer.UseExtendedParameters = TRUE;
        TypeInitializer.ParseProcedureEx = ParseProcEx;
    }
};

typedef NTSTATUS
(NTAPI *POB_CREATE_OBJECT_TYPE_EX)(
    _In_ PUNICODE_STRING TypeName,
    _In_ POBJECT_TYPE_INITIALIZER ObjectTypeInitializer,
    _In_opt_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_opt_ LONG_PTR WaitObjectInfo,
    _Out_ POBJECT_TYPE *ObjectType);

template<unsigned NtDdiVersion>
static
NTSTATUS
ObtCreateObjectTypeForVersion(
    ULONG Index,
    PUNICODE_STRING TypeName,
    TOBJECT_TYPE_INITIALIZER<NtDdiVersion>& TypeInitializer,
    TOBJECT_TYPE<NtDdiVersion>** ObjectType)
{
    UNREFERENCED_PARAMETER(Index);
    return ObCreateObjectType(TypeName, (POBJECT_TYPE_INITIALIZER)&TypeInitializer, NULL, (POBJECT_TYPE*)ObjectType);
}

template<>
NTSTATUS
ObtCreateObjectTypeForVersion<NTDDI_WIN11_GE>(
    ULONG Index,
    PUNICODE_STRING TypeName,
    TOBJECT_TYPE_INITIALIZER<NTDDI_WIN11_GE>& TypeInitializer,
    TOBJECT_TYPE<NTDDI_WIN11_GE>** ObjectType)
{
    struct EXTENDED_INITIALIZER
    {
        TOBJECT_TYPE_INITIALIZER<NTDDI_WIN11_GE> TypeInitializer;
        ULONG SeMandatoryLabelMask;
        ULONG SeTrustConstraintMask;
    };
    static EXTENDED_INITIALIZER ExtendedInitializer;
    POB_CREATE_OBJECT_TYPE_EX CreateObjectTypeEx;
    SECURITY_DESCRIPTOR_RELATIVE TypeSecurityDescriptor;
    ULONG ReturnLength;
    NTSTATUS Status;

    if (Index != NUM_OBTYPES - 1) return ObCreateObjectType(TypeName, (POBJECT_TYPE_INITIALIZER)&TypeInitializer, NULL, (POBJECT_TYPE*)ObjectType);

    static_assert(sizeof(ExtendedInitializer) == 128, "Unexpected extended object type initializer size");
    RtlZeroMemory(&ExtendedInitializer, sizeof(ExtendedInitializer));
    ExtendedInitializer.TypeInitializer = TypeInitializer;
    ExtendedInitializer.TypeInitializer.Length = sizeof(ExtendedInitializer);
    ExtendedInitializer.TypeInitializer.SeTrustConstraintMaskPresent = TRUE;
    ExtendedInitializer.TypeInitializer.PoolType = NonPagedPoolNx;
    ExtendedInitializer.TypeInitializer.DefaultPagedPoolCharge = 0x456;
    ExtendedInitializer.TypeInitializer.DefaultNonPagedPoolCharge = 0x123;
    ExtendedInitializer.TypeInitializer.WaitObjectFlagMask = 0x80000000;
    ExtendedInitializer.TypeInitializer.WaitObjectFlagOffset = 0x12;
    ExtendedInitializer.TypeInitializer.WaitObjectPointerOffset = 0x34;
    ExtendedInitializer.SeMandatoryLabelMask = 0x13579bdf;
    ExtendedInitializer.SeTrustConstraintMask = 0x2468ace0;
    ExtendedObjectSecurityMode = MAXULONG;
    ReturnLength = MAXULONG;
    Status = ZwQuerySystemInformation(SystemObjectSecurityMode, &ExtendedObjectSecurityMode, 0, &ReturnLength);
    trace("SystemObjectSecurityMode size 0: status 0x%08lx, mode 0x%08lx, return %lu\n", Status, ExtendedObjectSecurityMode, ReturnLength);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok_eq_ulong(ExtendedObjectSecurityMode, MAXULONG);
    ok_eq_ulong(ReturnLength, sizeof(ExtendedObjectSecurityMode));
    ExtendedObjectSecurityMode = MAXULONG;
    ReturnLength = MAXULONG;
    Status = ZwQuerySystemInformation(SystemObjectSecurityMode, &ExtendedObjectSecurityMode, sizeof(ExtendedObjectSecurityMode) - 1, &ReturnLength);
    trace("SystemObjectSecurityMode size 3: status 0x%08lx, mode 0x%08lx, return %lu\n", Status, ExtendedObjectSecurityMode, ReturnLength);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok_eq_ulong(ExtendedObjectSecurityMode, MAXULONG);
    ok_eq_ulong(ReturnLength, sizeof(ExtendedObjectSecurityMode));
    ExtendedObjectSecurityMode = MAXULONG;
    ReturnLength = MAXULONG;
    Status = ZwQuerySystemInformation(SystemObjectSecurityMode, &ExtendedObjectSecurityMode, sizeof(ExtendedObjectSecurityMode) + 1, &ReturnLength);
    trace("SystemObjectSecurityMode size 5: status 0x%08lx, mode 0x%08lx, return %lu\n", Status, ExtendedObjectSecurityMode, ReturnLength);
    ok_eq_hex(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok_eq_ulong(ExtendedObjectSecurityMode, MAXULONG);
    ok_eq_ulong(ReturnLength, sizeof(ExtendedObjectSecurityMode));
    ExtendedObjectSecurityMode = 0;
    ReturnLength = 0;
    Status = ZwQuerySystemInformation(SystemObjectSecurityMode, &ExtendedObjectSecurityMode, sizeof(ExtendedObjectSecurityMode), &ReturnLength);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(ExtendedObjectSecurityMode));
    if (!NT_SUCCESS(Status)) return Status;
    trace("SystemObjectSecurityMode = %lu\n", ExtendedObjectSecurityMode);
    RtlZeroMemory(&TypeSecurityDescriptor, sizeof(TypeSecurityDescriptor));
    TypeSecurityDescriptor.Revision = SECURITY_DESCRIPTOR_REVISION;
    TypeSecurityDescriptor.Control = SE_SELF_RELATIVE | SE_DACL_PRESENT;
    CreateObjectTypeEx = (POB_CREATE_OBJECT_TYPE_EX)KmtGetSystemRoutineAddress(L"ObCreateObjectTypeEx");
    ok(CreateObjectTypeEx != NULL, "ObCreateObjectTypeEx is not exported\n");
    if (CreateObjectTypeEx == NULL) return STATUS_PROCEDURE_NOT_FOUND;
    return CreateObjectTypeEx(TypeName, (POBJECT_TYPE_INITIALIZER)&ExtendedInitializer, (PSECURITY_DESCRIPTOR)&TypeSecurityDescriptor, 0x9b, (POBJECT_TYPE*)ObjectType);
}

template<unsigned NtDdiVersion>
static
VOID
ObtCheckExtendedObjectType(
    TOBJECT_TYPE<NtDdiVersion>*)
{
}

template<>
VOID
ObtCheckExtendedObjectType<NTDDI_WIN11_GE>(
    TOBJECT_TYPE<NTDDI_WIN11_GE>* ObjectType)
{
    ULONG HeaderCharge = sizeof(OBJECT_HEADER) + sizeof(OBJECT_HEADER_NAME_INFO) + sizeof(OBJECT_HEADER_HANDLE_INFO);
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;
    BOOLEAN SecurityDescriptorAllocated = TRUE;
    BOOLEAN DaclPresent = FALSE;
    BOOLEAN DaclDefaulted = TRUE;
    PACL Dacl = (PACL)(ULONG_PTR)-1;
    NTSTATUS Status;

    ok_eq_uint(ObjectType->TypeInfo.Length, 128);
    ok_eq_ulong(ObjectType->TypeInfo.PoolType, NonPagedPoolNx);
    ok_eq_ulong(ObjectType->TypeInfo.DefaultPagedPoolCharge, 0x456);
    ok_eq_ulong(ObjectType->TypeInfo.DefaultNonPagedPoolCharge, 0x123 + HeaderCharge);
    ok_eq_hex(ObjectType->TypeInfo.WaitObjectFlagMask, 0x80000000);
    ok_eq_uint(ObjectType->TypeInfo.WaitObjectFlagOffset, 0x12);
    ok_eq_uint(ObjectType->TypeInfo.WaitObjectPointerOffset, 0x34);
    ok_eq_pointer(ObjectType->DefaultObject, UlongToPtr(0x9b));
    ok_eq_hex(ObjectType->SeMandatoryLabelMask, 0x13579bdf);
    ok_eq_hex(ObjectType->SeTrustConstraintMask, 0x2468ace0);

    Status = ObGetObjectSecurity(ObjectType, &SecurityDescriptor, &SecurityDescriptorAllocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(SecurityDescriptorAllocated, FALSE);
    if (ExtendedObjectSecurityMode)
    {
        ok(SecurityDescriptor != NULL, "Object type security descriptor was not installed\n");
        if (SecurityDescriptor != NULL)
        {
            Status = RtlGetDaclSecurityDescriptor(SecurityDescriptor, &DaclPresent, &Dacl, &DaclDefaulted);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_bool(DaclPresent, TRUE);
            ok_eq_pointer(Dacl, NULL);
            ok_eq_bool(DaclDefaulted, FALSE);
        }
    }
    else
    {
        ok_eq_pointer(SecurityDescriptor, NULL);
    }
    if (SecurityDescriptor != NULL) ObReleaseObjectSecurity(SecurityDescriptor, SecurityDescriptorAllocated);
}

template<unsigned NtDdiVersion>
static
VOID
ObtCheckDefaultObjectTypeSecurity(
    TOBJECT_TYPE<NtDdiVersion>*)
{
}

template<>
VOID
ObtCheckDefaultObjectTypeSecurity<NTDDI_WIN11_GE>(
    TOBJECT_TYPE<NTDDI_WIN11_GE>* ObjectType)
{
    PSID ExpectedSids[] = {SeExports->SeWorldSid, SeExports->SeAliasAdminsSid, SeExports->SeLocalSystemSid};
    PSECURITY_DESCRIPTOR SecurityDescriptor = NULL;
    BOOLEAN SecurityDescriptorAllocated = TRUE;
    BOOLEAN DaclPresent = FALSE;
    BOOLEAN DaclDefaulted = TRUE;
    PACCESS_ALLOWED_ACE Ace;
    PVOID AcePointer;
    PACL Dacl = NULL;
    NTSTATUS Status;
    ULONG Index;

    Status = ObGetObjectSecurity(ObjectType, &SecurityDescriptor, &SecurityDescriptorAllocated);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(SecurityDescriptorAllocated, FALSE);
    if (ExtendedObjectSecurityMode)
    {
        ok(SecurityDescriptor != NULL, "Default object type security descriptor was not installed\n");
        if (SecurityDescriptor != NULL)
        {
            Status = RtlGetDaclSecurityDescriptor(SecurityDescriptor, &DaclPresent, &Dacl, &DaclDefaulted);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_bool(DaclPresent, TRUE);
            ok(Dacl != NULL, "Default object type DACL is missing\n");
            ok_eq_bool(DaclDefaulted, FALSE);
            if (Dacl != NULL)
            {
                ok_eq_uint(Dacl->AceCount, RTL_NUMBER_OF(ExpectedSids));
                for (Index = 0; Index < min(Dacl->AceCount, RTL_NUMBER_OF(ExpectedSids)); Index++)
                {
                    AcePointer = NULL;
                    Status = RtlGetAce(Dacl, Index, &AcePointer);
                    ok_eq_hex(Status, STATUS_SUCCESS);
                    if (!NT_SUCCESS(Status)) continue;
                    Ace = (PACCESS_ALLOWED_ACE)AcePointer;
                    ok_eq_uint(Ace->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
                    ok_eq_hex(Ace->Mask, OBJECT_TYPE_ALL_ACCESS);
                    ok(RtlEqualSid(&Ace->SidStart, ExpectedSids[Index]), "ACE %lu SID does not match\n", Index);
                }
            }
        }
    }
    else
    {
        ok_eq_pointer(SecurityDescriptor, NULL);
    }
    if (SecurityDescriptor != NULL) ObReleaseObjectSecurity(SecurityDescriptor, SecurityDescriptorAllocated);
}

template<unsigned NtDdiVersion>
static
NTSTATUS
ObtCreateObjectTypes(VOID)
{
    static TOBJECT_TYPE_INITIALIZER<NtDdiVersion> ObTypeInitializer[NUM_OBTYPES];
    using OBJECT_TYPE = TOBJECT_TYPE<NtDdiVersion>;
    OBJECT_TYPE** ObTypes = reinterpret_cast<OBJECT_TYPE**>(&ObTypes_);
    INT i;
    NTSTATUS Status;
    struct
    {
        WCHAR DirectoryName[sizeof "\\ObjectTypes\\" - 1];
        WCHAR TypeName[15];
    } Name;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE ObjectTypeHandle;
    UNICODE_STRING ObjectPath;
    RtlCopyMemory(&Name.DirectoryName, L"\\ObjectTypes\\", sizeof Name.DirectoryName);

    for (i = 0; i < NUM_OBTYPES; ++i)
    {
        Status = RtlStringCbPrintfW(Name.TypeName, sizeof Name.TypeName, L"MyObjectType%x", i);
        ASSERT(NT_SUCCESS(Status));
        RtlInitUnicodeString(&ObTypeName[i], Name.TypeName);
        DPRINT("Creating object type %wZ\n", &ObTypeName[i]);

        RtlZeroMemory(&ObTypeInitializer[i], sizeof ObTypeInitializer[i]);
        ObTypeInitializer[i].Length = sizeof ObTypeInitializer[i];
        ObTypeInitializer[i].PoolType = NonPagedPool;
        ObTypeInitializer[i].MaintainHandleCount = TRUE;
        ObTypeInitializer[i].ValidAccessMask = OBJECT_TYPE_ALL_ACCESS;

        // Test for invalid parameter
        // FIXME: Make it more exact, to see which params Win2k3 checks existence of.
        // Vista+: This triggers a DbgBreakPoint() in the kernel
        if (NtDdiVersion <= NTDDI_WS03)
        {
            Status = ObCreateObjectType(&ObTypeName[i], (POBJECT_TYPE_INITIALIZER)&ObTypeInitializer[i], NULL, (POBJECT_TYPE*)&ObTypes[i]);
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
        }

        TObCallbackSetter<NtDdiVersion>::Set(ObTypeInitializer[i]);
        //ObTypeInitializer[i].SecurityProcedure = SecurityProc;

        Status = ObtCreateObjectTypeForVersion<NtDdiVersion>(i, &ObTypeName[i], ObTypeInitializer[i], &ObTypes[i]);
        if (Status == STATUS_OBJECT_NAME_COLLISION)
        {
            /* as we cannot delete the object types, get a pointer if they
             * already exist */
            RtlInitUnicodeString(&ObjectPath, Name.DirectoryName);
            InitializeObjectAttributes(&ObjectAttributes, &ObjectPath, OBJ_KERNEL_HANDLE, NULL, NULL);
            Status = ObOpenObjectByName(&ObjectAttributes, NULL, KernelMode, NULL, 0, NULL, &ObjectTypeHandle);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok(ObjectTypeHandle != NULL, "ObjectTypeHandle = NULL\n");
            if (!skip(Status == STATUS_SUCCESS && ObjectTypeHandle, "No handle\n"))
            {
                Status = ObReferenceObjectByHandle(ObjectTypeHandle, 0, NULL, KernelMode, (PVOID*)&ObTypes[i], NULL);
                ok_eq_hex(Status, STATUS_SUCCESS);
                if (!skip(Status == STATUS_SUCCESS && ObTypes[i], "blah\n"))
                {
                    TObCallbackSetter<NtDdiVersion>::Set(ObTypes[i]->TypeInfo);
                }
                Status = ZwClose(ObjectTypeHandle);
            }
        }

        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(ObTypes[i] != NULL, "ObType = NULL\n");
    }

    if (!skip(ObTypes[0] != NULL, "No default object type\n")) ObtCheckDefaultObjectTypeSecurity<NtDdiVersion>(ObTypes[0]);
    if (!skip(ObTypes[NUM_OBTYPES - 1] != NULL, "No extended object type\n")) ObtCheckExtendedObjectType<NtDdiVersion>(ObTypes[NUM_OBTYPES - 1]);

    return STATUS_SUCCESS;
}

static
VOID
ObtCreateDirectory(VOID)
{
    NTSTATUS Status;

    RtlInitUnicodeString(&ObDirectoryName, L"\\ObtDirectory");
    InitializeObjectAttributes(&ObDirectoryAttributes, &ObDirectoryName, OBJ_KERNEL_HANDLE | OBJ_PERMANENT | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwCreateDirectoryObject(&DirectoryHandle, DELETE, &ObDirectoryAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckObject(DirectoryHandle, 3LU, 1LU);
}

#define CheckCounts(OpenCount, CloseCount, DeleteCount, ParseCount, \
                        OkayToCloseCount, QueryNameCount) do        \
{                                                                   \
    ok_eq_uint(Counts.Open, OpenCount);                             \
    if (Counts.Open != OpenCount) __debugbreak(); \
    ok_eq_uint(Counts.Close, CloseCount);                           \
    ok_eq_uint(Counts.Delete, DeleteCount);                         \
    ok_eq_uint(Counts.Parse, ParseCount);                           \
    ok_eq_uint(Counts.OkayToClose, OkayToCloseCount);               \
    ok_eq_uint(Counts.QueryName, QueryNameCount);                   \
} while (0)

#define SaveCounts(Save) memcpy(&Save, &Counts, sizeof Counts)

/* TODO: make this the same as NUM_OBTYPES */
#define NUM_OBTYPES2 2

template<unsigned NtDdiVersion>
static
VOID
ObtTestExtendedParse(VOID)
{
}

template<>
VOID
ObtTestExtendedParse<NTDDI_WIN11_GE>(VOID)
{
    NTSTATUS Status;
    PVOID Object = NULL;
    USHORT PreviousParseCount = Counts.Parse;
    UNICODE_STRING ParseName = RTL_CONSTANT_STRING(L"\\ObtDirectory\\MyObject1\\Child");

    ExtendedParseCalled = FALSE;
    RtlZeroMemory(&ExtendedParseParameters, sizeof(ExtendedParseParameters));
    Status = ObReferenceObjectByName(&ParseName, OBJ_CASE_INSENSITIVE, NULL, 0, (POBJECT_TYPE)ObTypes_[0], KernelMode, NULL, &Object);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    ok_eq_pointer(Object, NULL);
    ok_eq_uint(Counts.Parse, PreviousParseCount + 1);
    ok(ExtendedParseCalled, "Extended parse callback was not called\n");
    ok_eq_uint(ExtendedParseParameters.Length, sizeof(ExtendedParseParameters));
    ok_eq_hex(ExtendedParseParameters.RestrictedAccessMask, MAXULONG);
    ok_eq_pointer(ExtendedParseParameters.Silo, NULL);
}

template<unsigned NtDdiVersion>
static
VOID
ObtCreateObjects(VOID)
{
    NTSTATUS Status;
    WCHAR Name[NUM_OBTYPES2][MAX_PATH];
    COUNTS SaveCounts;
    INT i;
    ACCESS_MASK Access[NUM_OBTYPES2] = { STANDARD_RIGHTS_ALL, GENERIC_ALL };
    ULONG ObjectSize[NUM_OBTYPES2] = { sizeof(MY_OBJECT1), sizeof(MY_OBJECT2) };

    // Create two objects
    for (i = 0; i < NUM_OBTYPES2; ++i)
    {
        ASSERT(sizeof Name[i] == MAX_PATH * sizeof(WCHAR));
        Status = RtlStringCbPrintfW(Name[i], sizeof Name[i], L"\\ObtDirectory\\MyObject%d", i + 1);
        ASSERT(Status == STATUS_SUCCESS);
        RtlInitUnicodeString(&ObName[i], Name[i]);
        InitializeObjectAttributes(&ObAttributes[i], &ObName[i], OBJ_CASE_INSENSITIVE, NULL, NULL);
    }
    CheckObject(DirectoryHandle, 3LU, 1LU);

    for (i = 0; i < NUM_OBTYPES2; ++i)
    {
        Status = ObCreateObject(KernelMode, (POBJECT_TYPE)ObTypes_[i], &ObAttributes[i], KernelMode, NULL, ObjectSize[i], 0L, 0L, &ObBody[i]);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    SaveCounts(SaveCounts);

    // Insert them
    for (i = 0; i < NUM_OBTYPES2; ++i)
    {
        CheckObject(DirectoryHandle, 3LU + i, 1LU);
        Status = ObInsertObject(ObBody[i], NULL, Access[i], 0, &ObBody[i], &ObHandle1[i]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(ObBody[i] != NULL, "Object body = NULL\n");
        ok(ObHandle1[i] != NULL, "Handle = NULL\n");
        CheckObject(ObHandle1[i], 3LU, 1LU);
        CheckCounts(SaveCounts.Open + 1, SaveCounts.Close, SaveCounts.Delete, SaveCounts.Parse, SaveCounts.OkayToClose, SaveCounts.QueryName);
        SaveCounts(SaveCounts);
        CheckObject(DirectoryHandle, 4LU + i, 1LU);
    }

    //DPRINT1("%d %d %d %d %d %d %d\n", DumpCount, OpenCount, CloseCount, DeleteCount, ParseCount, OkayToCloseCount, QueryNameCount);
    CheckCounts(SaveCounts.Open, SaveCounts.Close, SaveCounts.Delete, SaveCounts.Parse, SaveCounts.OkayToClose, SaveCounts.QueryName);
    ObtTestExtendedParse<NtDdiVersion>();
}

static
VOID
ObtClose(
    BOOLEAN Clean,
    BOOLEAN AlternativeMethod)
{
    PVOID* ObTypes = ObTypes_;
    NTSTATUS Status;
    LONG_PTR Ret;
    PVOID TypeObject;
    INT i;
    UNICODE_STRING ObPathName[NUM_OBTYPES];
    WCHAR Name[MAX_PATH];

    if ((GetNTVersion() >= _WIN32_WINNT_WIN8) && ObBody[0] && ObHandle1[0])
    {
        COUNTS ProbeCounts = Counts;
        HANDLE ProbeHandle = NULL;

        CheckObject(ObHandle1[0], 3LU, 1LU);
        Status = ObOpenObjectByPointer(ObBody[0], OBJ_KERNEL_HANDLE, NULL, 0, (POBJECT_TYPE)ObTypes_[0], KernelMode, &ProbeHandle);
        trace("Second-handle open returned 0x%08lx, handle %p\n", Status, ProbeHandle);
        if (NT_SUCCESS(Status))
        {
            CheckObject(ObHandle1[0], 4LU, 2LU);
            Ret = ObReferenceObject(ObBody[0]);
            trace("Second-handle ObReferenceObject returned %Id\n", Ret);
            Ret = ObDereferenceObject(ObBody[0]);
            trace("Second-handle ObDereferenceObject returned %Id\n", Ret);
            Status = ZwClose(ProbeHandle);
            trace("Second-handle close returned 0x%08lx\n", Status);
            CheckObject(ObHandle1[0], 3LU, 1LU);
        }
        Counts = ProbeCounts;
    }

    // Close what we have opened and free what we allocated
    for (i = 0; i < NUM_OBTYPES2; ++i)
    {
        if (!skip(ObBody[i] != NULL, "Nothing to dereference\n"))
        {
            if (ObHandle1[i]) CheckObject(ObHandle1[i], 3LU, 1LU);
            Ret = ObReferenceObject(ObBody[i]);
            trace("ObReferenceObject[%lu] returned %Id\n", i, Ret);
            if (ObHandle1[i]) CheckObject(ObHandle1[i], 4LU, 1LU);
            Ret = ObDereferenceObject(ObBody[i]);
            trace("ObDereferenceObject[%lu] returned %Id\n", i, Ret);
            if (GetNTVersion() < _WIN32_WINNT_WIN8) ok_eq_longptr(Ret, (LONG_PTR)2);
            if (ObHandle1[i]) CheckObject(ObHandle1[i], 3LU, 1LU);
            ObBody[i] = NULL;
        }
        if (!skip(ObHandle1[i] != NULL, "Nothing to close\n"))
        {
            Status = ZwClose(ObHandle1[i]);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ObHandle1[i] = NULL;
        }
    }

    if (skip(Clean, "Not cleaning up, as requested. Use ObTypeClean to clean up\n"))
        return;

    // Now we have to get rid of a directory object
    // Since it is permanent, we have to firstly make it temporary
    // and only then kill
    // (this procedure is described in DDK)
    if (!skip(DirectoryHandle != NULL, "No directory handle\n"))
    {
        CheckObject(DirectoryHandle, 3LU, 1LU);

        Status = ZwMakeTemporaryObject(DirectoryHandle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        CheckObject(DirectoryHandle, 3LU, 1LU);

        Status = ZwClose(DirectoryHandle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            DirectoryHandle = NULL;
    }

    /* we don't delete the object types we created. It makes Windows unstable.
     * TODO: perhaps make it work in ROS anyway */
    return;
    if (!AlternativeMethod)
    {
        for (i = 0; i < NUM_OBTYPES; ++i)
            if (!skip(ObTypes[i] != NULL, "No object type to delete\n"))
            {
                Ret = ObDereferenceObject(ObTypes[i]);
                ok_eq_longptr(Ret, (LONG_PTR)0);
                ObTypes[i] = NULL;
            }
    }
    else
    {
        for (i = 0; i < NUM_OBTYPES; ++i)
        {
            if (!skip(ObTypes[i] != NULL, "No object type to delete\n"))
            {
                Status = RtlStringCbPrintfW(Name, sizeof Name, L"\\ObjectTypes\\MyObjectType%d", i);
                RtlInitUnicodeString(&ObPathName[i], Name);
                Status = ObReferenceObjectByName(&ObPathName[i], OBJ_CASE_INSENSITIVE, NULL, 0L, NULL, KernelMode, NULL, &TypeObject);

                Ret = ObDereferenceObject(TypeObject);
                ok_eq_longptr(Ret, (LONG_PTR)2);
                Ret = ObDereferenceObject(TypeObject);
                ok_eq_longptr(Ret, (LONG_PTR)1);
                DPRINT("Reference Name %wZ = %p, ObTypes[%d] = %p\n",
                    ObPathName[i], TypeObject, i, ObTypes[i]);
                ObTypes[i] = NULL;
            }
        }
    }
}

template<unsigned NtDdiVersion>
static
VOID
TestObjectType_(
    IN BOOLEAN Clean)
{
    PVOID* ObTypes = ObTypes_;
    NTSTATUS Status;

    RtlZeroMemory(&Counts, sizeof Counts);

    Status = ObtCreateObjectTypes<NtDdiVersion>();
    DPRINT("ObtCreateObjectTypes() %s\n", NT_SUCCESS(Status) ? "succeeded" : "failed");

    ObtCreateDirectory();
    DPRINT("ObtCreateDirectory() done\n");

    if (!skip(ObTypes[0] != NULL, "No object types!\n"))
        ObtCreateObjects<NtDdiVersion>();
    DPRINT("ObtCreateObjects() done\n");

    ObtClose(Clean, FALSE);
}

static
VOID
TestObjectType(
    IN BOOLEAN Clean)
{
    ULONG NtDdiVersion = GetNTDDIVersion();

    switch (NtDdiVersion)
    {
        case NTDDI_WS03:
            TestObjectType_<NTDDI_WS03>(Clean);
            return;
        case NTDDI_VISTA:
            TestObjectType_<NTDDI_VISTA>(Clean);
            return;
        case NTDDI_VISTASP1:
        case NTDDI_VISTASP2:
        case NTDDI_VISTASP3:
            TestObjectType_<NTDDI_VISTASP1>(Clean);
            return;
        case NTDDI_WIN7:
            TestObjectType_<NTDDI_WIN7>(Clean);
            return;
        case NTDDI_WIN11_GE:
            TestObjectType_<NTDDI_WIN11_GE>(Clean);
            return;
        default:
            skip(FALSE, "Unsupported NTDDI version: 0x%lx\n", NtDdiVersion);
            return;
    }
}

START_TEST(ObType)
{
    TestObjectType(TRUE);
}

/* run this to see the objects created in user mode */
START_TEST(ObTypeNoClean)
{
    TestObjectType(FALSE);
}

/* run this to clean up after ObTypeNoClean */
START_TEST(ObTypeClean)
{
    ObtClose(TRUE, FALSE);
}
