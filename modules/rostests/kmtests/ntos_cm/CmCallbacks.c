/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite registry filtering (CmRegisterCallbackEx)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define ROOT_KEY_NAME L"\\Registry\\Machine\\Software\\KmtCmCallbacks"
#define KEY_NAME L"TestKey"
#define OTHER_KEY_NAME L"OtherKey"
#define VALUE_NAME L"TestValue"

typedef enum _CALLBACK_MODE
{
    ModeCount,
    ModeBlock,
    ModeBypass,
    ModePostFail,
    ModePostSucceed,
    ModeCallContext,
    ModeObjectContext,
    ModeInternal,
    ModeV1
} CALLBACK_MODE;

typedef struct _CALLBACK_CONTEXT
{
    LARGE_INTEGER Cookie;
    CALLBACK_MODE Mode;
    ULONG PreCount;
    ULONG PostCount;
    ULONG CleanupCount;
    PVOID CleanupContext;
    PVOID SeenObjectContext;
    PVOID SeenCallContext;
    PVOID SeenPreCallContext;
    NTSTATUS SeenStatus;
    BOOLEAN PostObjectNull;
    BOOLEAN V1Checked;
    REG_CREATE_KEY_INFORMATION_V1 V1;
    UNICODE_STRING V1CompleteName;
    WCHAR V1CompleteNameBuffer[64];
    UNICODE_STRING V1RemainingName;
    WCHAR V1RemainingNameBuffer[64];
} CALLBACK_CONTEXT, *PCALLBACK_CONTEXT;

static HANDLE RootKey;
static PVOID RootObject;
static PETHREAD TestThread;
static UNICODE_STRING KeyName = RTL_CONSTANT_STRING(KEY_NAME);
static UNICODE_STRING OtherKeyName = RTL_CONSTANT_STRING(OTHER_KEY_NAME);
static UNICODE_STRING ValueName = RTL_CONSTANT_STRING(VALUE_NAME);

static
NTSTATUS
OpenRelative(
    _In_ PVOID Root,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE RootHandle;
    NTSTATUS Status;

    Status = ObOpenObjectByPointer(Root, OBJ_KERNEL_HANDLE, NULL, KEY_ALL_ACCESS, NULL, KernelMode, &RootHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes, Name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, RootHandle, NULL);
    if (Create)
        Status = ZwCreateKey(Key, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    else
        Status = ZwOpenKey(Key, KEY_ALL_ACCESS, &ObjectAttributes);
    ZwClose(RootHandle);
    return Status;
}

static
NTSTATUS
NTAPI
Callback(
    _In_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PCALLBACK_CONTEXT Ctx = CallbackContext;
    REG_NOTIFY_CLASS NotifyClass = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;
    PREG_CREATE_KEY_INFORMATION_V1 CreateInfo;
    PREG_SET_VALUE_KEY_INFORMATION SetValueInfo;
    PREG_POST_OPERATION_INFORMATION PostInfo;
    PREG_CALLBACK_CONTEXT_CLEANUP_INFORMATION CleanupInfo;
    HANDLE Key;
    PVOID Object;
    NTSTATUS Status;

    if (NotifyClass == RegNtCallbackObjectContextCleanup)
    {
        CleanupInfo = Argument2;
        Ctx->CleanupCount++;
        Ctx->CleanupContext = CleanupInfo->ObjectContext;
        return STATUS_SUCCESS;
    }

    if (PsGetCurrentThread() != TestThread)
        return STATUS_SUCCESS;

    switch (NotifyClass)
    {
        case RegNtPreCreateKeyEx:
        case RegNtPreOpenKeyEx:
            CreateInfo = Argument2;
            if (CreateInfo->RootObject != RootObject)
                break;
            Ctx->PreCount++;
            if (!RtlEqualUnicodeString(CreateInfo->CompleteName, &KeyName, TRUE))
                break;

            if (Ctx->Mode == ModeBlock && NotifyClass == RegNtPreCreateKeyEx)
                return STATUS_ACCESS_DENIED;

            if (Ctx->Mode == ModeBypass && NotifyClass == RegNtPreCreateKeyEx)
            {
                Status = OpenRelative(CreateInfo->RootObject, &OtherKeyName, TRUE, &Key);
                if (!NT_SUCCESS(Status))
                    return Status;
                Status = ObReferenceObjectByHandle(Key, 0, CreateInfo->ObjectType, KernelMode, &Object, NULL);
                ZwClose(Key);
                if (!NT_SUCCESS(Status))
                    return Status;
                *CreateInfo->ResultObject = Object;
                CreateInfo->GrantedAccess = CreateInfo->DesiredAccess;
                *CreateInfo->Disposition = REG_OPENED_EXISTING_KEY;
                return STATUS_CALLBACK_BYPASS;
            }

            if (Ctx->Mode == ModeCallContext)
                CreateInfo->CallContext = Ctx;

            if (Ctx->Mode == ModeInternal)
            {
                Status = OpenRelative(CreateInfo->RootObject, &OtherKeyName, FALSE, &Key);
                if (NT_SUCCESS(Status))
                    ZwClose(Key);
            }

            if (Ctx->Mode == ModeV1 && NotifyClass == RegNtPreCreateKeyEx)
            {
                Ctx->V1 = *CreateInfo;
                Ctx->V1CompleteName.Buffer = Ctx->V1CompleteNameBuffer;
                Ctx->V1CompleteName.MaximumLength = sizeof(Ctx->V1CompleteNameBuffer);
                RtlCopyUnicodeString(&Ctx->V1CompleteName, CreateInfo->CompleteName);
                Ctx->V1RemainingName.Buffer = Ctx->V1RemainingNameBuffer;
                Ctx->V1RemainingName.MaximumLength = sizeof(Ctx->V1RemainingNameBuffer);
                if (CreateInfo->Version == 1 && CreateInfo->RemainingName != NULL)
                    RtlCopyUnicodeString(&Ctx->V1RemainingName, CreateInfo->RemainingName);
                Ctx->V1Checked = TRUE;
            }
            break;

        case RegNtPostCreateKeyEx:
        case RegNtPostOpenKeyEx:
            PostInfo = Argument2;
            CreateInfo = PostInfo->PreInformation;
            if (CreateInfo->RootObject != RootObject)
                break;
            Ctx->PostCount++;
            if (!RtlEqualUnicodeString(CreateInfo->CompleteName, &KeyName, TRUE))
                break;

            Ctx->SeenStatus = PostInfo->Status;
            Ctx->PostObjectNull = (PostInfo->Object == NULL);

            if (Ctx->Mode == ModeCallContext)
            {
                Ctx->SeenCallContext = PostInfo->CallContext;
                Ctx->SeenPreCallContext = CreateInfo->CallContext;
            }

            if (Ctx->Mode == ModeObjectContext && NT_SUCCESS(PostInfo->Status))
            {
                Status = CmSetCallbackObjectContext(PostInfo->Object, &Ctx->Cookie, Ctx, NULL);
                ok_eq_hex(Status, STATUS_SUCCESS);
            }

            if (Ctx->Mode == ModeInternal)
            {
                Status = OpenRelative(CreateInfo->RootObject, &OtherKeyName, FALSE, &Key);
                if (NT_SUCCESS(Status))
                    ZwClose(Key);
            }

            if (Ctx->Mode == ModePostSucceed && NotifyClass == RegNtPostOpenKeyEx && !NT_SUCCESS(PostInfo->Status))
            {
                Status = OpenRelative(CreateInfo->RootObject, &OtherKeyName, FALSE, &Key);
                if (!NT_SUCCESS(Status))
                    break;
                Status = ObReferenceObjectByHandle(Key, 0, CreateInfo->ObjectType, KernelMode, &Object, NULL);
                ZwClose(Key);
                if (!NT_SUCCESS(Status))
                    break;
                *CreateInfo->ResultObject = Object;
                CreateInfo->GrantedAccess = CreateInfo->DesiredAccess;
                PostInfo->Object = Object;
                PostInfo->ReturnStatus = STATUS_SUCCESS;
                return STATUS_CALLBACK_BYPASS;
            }
            break;

        case RegNtPreSetValueKey:
            SetValueInfo = Argument2;
            if (!RtlEqualUnicodeString(SetValueInfo->ValueName, &ValueName, TRUE))
                break;
            Ctx->PreCount++;
            Ctx->SeenObjectContext = SetValueInfo->ObjectContext;
            if (Ctx->Mode == ModeBlock)
                return STATUS_ACCESS_DENIED;
            if (Ctx->Mode == ModeBypass)
                return STATUS_CALLBACK_BYPASS;
            break;

        case RegNtPostSetValueKey:
            PostInfo = Argument2;
            SetValueInfo = PostInfo->PreInformation;
            if (!RtlEqualUnicodeString(SetValueInfo->ValueName, &ValueName, TRUE))
                break;
            Ctx->PostCount++;
            if (Ctx->Mode == ModePostFail)
            {
                PostInfo->ReturnStatus = STATUS_ACCESS_DENIED;
                return STATUS_CALLBACK_BYPASS;
            }
            break;

        case RegNtPreKeyHandleClose:
            if (Ctx->Mode == ModeInternal || Ctx->Mode == ModeCount)
                Ctx->PreCount++;
            break;

        case RegNtPostKeyHandleClose:
            if (Ctx->Mode == ModeInternal || Ctx->Mode == ModeCount)
                Ctx->PostCount++;
            break;

        default:
            break;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
Register(
    _Out_ PCALLBACK_CONTEXT Ctx,
    _In_ CALLBACK_MODE Mode,
    _In_ PCWSTR Altitude)
{
    UNICODE_STRING AltitudeString;

    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->Mode = Mode;
    RtlInitUnicodeString(&AltitudeString, Altitude);
    return CmRegisterCallbackEx(Callback, &AltitudeString, KmtDriverObject, Ctx, &Ctx->Cookie, NULL);
}

static
VOID
DeleteKey(
    _In_ PUNICODE_STRING Name)
{
    HANDLE Key;

    if (NT_SUCCESS(OpenRelative(RootObject, Name, FALSE, &Key)))
    {
        ZwDeleteKey(Key);
        ZwClose(Key);
    }
}

static
VOID
TestRegistration(VOID)
{
    CALLBACK_CONTEXT Ctx, Other;
    LARGE_INTEGER Cookie;
    ULONG Major = 0, Minor = 0;
    NTSTATUS Status;

    CmGetCallbackVersion(&Major, &Minor);
    ok_eq_ulong(Major, 1UL);
    ok_eq_ulong(Minor, 1UL);

    Status = Register(&Ctx, ModeCount, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = Register(&Other, ModeCount, L"380010");
    ok_eq_hex(Status, STATUS_FLT_INSTANCE_ALTITUDE_COLLISION);
    if (NT_SUCCESS(Status)) CmUnRegisterCallback(Other.Cookie);

    Status = CmUnRegisterCallback(Ctx.Cookie);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Cookie = Ctx.Cookie;
    Status = CmUnRegisterCallback(Cookie);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

static
VOID
TestBlockAndBypass(VOID)
{
    CALLBACK_CONTEXT Ctx;
    HANDLE Key;
    UCHAR Buffer[256];
    PKEY_NAME_INFORMATION NameInfo = (PKEY_NAME_INFORMATION)Buffer;
    UNICODE_STRING Name, Expected = RTL_CONSTANT_STRING(ROOT_KEY_NAME L"\\" OTHER_KEY_NAME);
    ULONG Data = 1, Length;
    NTSTATUS Status;

    Status = Register(&Ctx, ModeBlock, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Key = NULL;
    Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    ok_eq_ulong(Ctx.PreCount, 2UL);
    ok_eq_ulong(Ctx.PostCount, 1UL);
    if (NT_SUCCESS(Status)) ZwClose(Key);

    Status = ZwSetValueKey(RootKey, &ValueName, 0, REG_DWORD, &Data, sizeof(Data));
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);

    Status = CmUnRegisterCallback(Ctx.Cookie);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = Register(&Ctx, ModeBypass, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Key = NULL;
    Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwQueryKey(Key, KeyNameInformation, Buffer, sizeof(Buffer), &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Name.Buffer = NameInfo->Name;
            Name.Length = Name.MaximumLength = (USHORT)NameInfo->NameLength;
            ok(RtlEqualUnicodeString(&Name, &Expected, TRUE), "Name = %wZ\n", &Name);
        }
        ZwClose(Key);
    }

    Status = ZwSetValueKey(RootKey, &ValueName, 0, REG_DWORD, &Data, sizeof(Data));
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = ZwQueryValueKey(RootKey, &ValueName, KeyValuePartialInformation, Buffer, sizeof(Buffer), &Length);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    Status = CmUnRegisterCallback(Ctx.Cookie);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = OpenRelative(RootObject, &KeyName, FALSE, &Key);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status)) ZwClose(Key);
    DeleteKey(&OtherKeyName);
}

static
VOID
TestPostOverride(VOID)
{
    CALLBACK_CONTEXT Ctx;
    HANDLE Key;
    UCHAR Buffer[256];
    PKEY_NAME_INFORMATION NameInfo = (PKEY_NAME_INFORMATION)Buffer;
    UNICODE_STRING Name, Expected = RTL_CONSTANT_STRING(ROOT_KEY_NAME L"\\" OTHER_KEY_NAME);
    ULONG Data = 1, Length;
    NTSTATUS Status;

    Status = Register(&Ctx, ModePostFail, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = ZwSetValueKey(RootKey, &ValueName, 0, REG_DWORD, &Data, sizeof(Data));
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    ok_eq_ulong(Ctx.PreCount, 1UL);
    ok_eq_ulong(Ctx.PostCount, 1UL);

    Status = CmUnRegisterCallback(Ctx.Cookie);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ZwDeleteValueKey(RootKey, &ValueName);

    Status = OpenRelative(RootObject, &OtherKeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ZwClose(Key);

    Status = Register(&Ctx, ModePostSucceed, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Key = NULL;
        Status = OpenRelative(RootObject, &KeyName, FALSE, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Ctx.SeenStatus, STATUS_OBJECT_NAME_NOT_FOUND);
        ok_eq_bool(Ctx.PostObjectNull, TRUE);
        if (NT_SUCCESS(Status))
        {
            Status = ZwQueryKey(Key, KeyNameInformation, Buffer, sizeof(Buffer), &Length);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                Name.Buffer = NameInfo->Name;
                Name.Length = Name.MaximumLength = (USHORT)NameInfo->NameLength;
                ok(RtlEqualUnicodeString(&Name, &Expected, TRUE), "Name = %wZ\n", &Name);
            }
            ZwClose(Key);
        }

        Status = CmUnRegisterCallback(Ctx.Cookie);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    DeleteKey(&OtherKeyName);
}

static
VOID
TestContexts(VOID)
{
    CALLBACK_CONTEXT Ctx;
    HANDLE Key;
    ULONG Data = 1;
    NTSTATUS Status;

    Status = Register(&Ctx, ModeCallContext, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Ctx.SeenCallContext, &Ctx);
    ok_eq_pointer(Ctx.SeenPreCallContext, NULL);
    if (NT_SUCCESS(Status)) ZwClose(Key);

    Status = CmUnRegisterCallback(Ctx.Cookie);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = Register(&Ctx, ModeObjectContext, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Status = OpenRelative(RootObject, &KeyName, FALSE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = ZwSetValueKey(Key, &ValueName, 0, REG_DWORD, &Data, sizeof(Data));
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Ctx.SeenObjectContext, &Ctx);
        ok_eq_ulong(Ctx.CleanupCount, 0UL);
        ZwClose(Key);
        ok_eq_ulong(Ctx.CleanupCount, 1UL);
        ok_eq_pointer(Ctx.CleanupContext, &Ctx);
    }

    Status = OpenRelative(RootObject, &KeyName, FALSE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Ctx.CleanupCount = 0;
    Ctx.CleanupContext = NULL;

    Status = CmUnRegisterCallback(Ctx.Cookie);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Ctx.CleanupCount, 1UL);
    ok_eq_pointer(Ctx.CleanupContext, &Ctx);

    if (Key) ZwClose(Key);
    DeleteKey(&KeyName);
}

static
VOID
TestAltitudes(VOID)
{
    CALLBACK_CONTEXT High, Mid, Low;
    HANDLE Key;
    NTSTATUS Status;

    Status = Register(&Mid, ModeBlock, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = Register(&Low, ModeCount, L"370020");
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Register(&High, ModeCount, L"380020.5");
    ok_eq_hex(Status, STATUS_SUCCESS);

    Key = NULL;
    Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_ACCESS_DENIED);
    if (NT_SUCCESS(Status)) ZwClose(Key);

    CmUnRegisterCallback(High.Cookie);
    CmUnRegisterCallback(Mid.Cookie);
    CmUnRegisterCallback(Low.Cookie);

    ok_eq_ulong(High.PreCount, 2UL);
    ok_eq_ulong(High.PostCount, 2UL);
    ok_eq_hex(High.SeenStatus, STATUS_ACCESS_DENIED);
    ok_eq_bool(High.PostObjectNull, TRUE);
    ok_eq_ulong(Mid.PreCount, 2UL);
    ok_eq_ulong(Mid.PostCount, 1UL);
    ok_eq_ulong(Low.PreCount, 1UL);
    ok_eq_ulong(Low.PostCount, 1UL);

    Status = OpenRelative(RootObject, &OtherKeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ZwClose(Key);

    Status = Register(&High, ModeCount, L"380020.5");
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Register(&Mid, ModeInternal, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Register(&Low, ModeCount, L"370020");
    ok_eq_hex(Status, STATUS_SUCCESS);

    Key = NULL;
    Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);

    CmUnRegisterCallback(High.Cookie);
    CmUnRegisterCallback(Mid.Cookie);
    CmUnRegisterCallback(Low.Cookie);

    ok_eq_ulong(High.PreCount, 2UL);
    ok_eq_ulong(High.PostCount, 2UL);
    ok_eq_ulong(Mid.PreCount, 2UL);
    ok_eq_ulong(Mid.PostCount, 2UL);
    ok_eq_ulong(Low.PreCount, 10UL);
    ok_eq_ulong(Low.PostCount, 10UL);

    if (Key) ZwClose(Key);
    DeleteKey(&KeyName);
    DeleteKey(&OtherKeyName);
}

static
VOID
TestCreateInformation(VOID)
{
    PCALLBACK_CONTEXT Ctx;
    HANDLE Key, Second;
    ULONG_PTR Id = 0, SecondId = 1;
    PCUNICODE_STRING Name = NULL;
    PVOID Object, SecondObject;
    UNICODE_STRING Expected = RTL_CONSTANT_STRING(ROOT_KEY_NAME L"\\" KEY_NAME);
    NTSTATUS Status;

    Ctx = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Ctx), 'CmtK');
    ok(Ctx != NULL, "Out of memory\n");
    if (Ctx == NULL) return;

    Status = Register(Ctx, ModeV1, L"380010");
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Ctx, 'CmtK');
        return;
    }

    Key = NULL;
    Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_bool(Ctx->V1Checked, TRUE);
    if (Ctx->V1Checked)
    {
        ok_eq_ulongptr(Ctx->V1.Version, (ULONG_PTR)1);
        ok_eq_pointer(Ctx->V1.RootObject, RootObject);
        ok(Ctx->V1.ObjectType != NULL, "ObjectType is NULL\n");
        ok(RtlEqualUnicodeString(&Ctx->V1CompleteName, &KeyName, TRUE), "CompleteName = %wZ\n", &Ctx->V1CompleteName);
        ok(RtlEqualUnicodeString(&Ctx->V1RemainingName, &KeyName, TRUE), "RemainingName = %wZ\n", &Ctx->V1RemainingName);
        ok_eq_hex(Ctx->V1.Options, (ULONG)REG_OPTION_VOLATILE);
        ok_eq_hex(Ctx->V1.DesiredAccess, (ULONG)KEY_ALL_ACCESS);
        ok_eq_hex(Ctx->V1.Attributes & (OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE), (ULONG)(OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE));
        ok_eq_int(Ctx->V1.CheckAccessMode, KernelMode);
        ok_eq_pointer(Ctx->V1.Transaction, NULL);
        ok(Ctx->V1.Disposition != NULL, "Disposition is NULL\n");
        ok(Ctx->V1.ResultObject != NULL, "ResultObject is NULL\n");
    }

    if (NT_SUCCESS(Status))
    {
        Status = OpenRelative(RootObject, &KeyName, FALSE, &Second);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = ObReferenceObjectByHandle(Key, 0, NULL, KernelMode, &Object, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = ObReferenceObjectByHandle(Second, 0, NULL, KernelMode, &SecondObject, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);

            Status = CmCallbackGetKeyObjectIDEx(&Ctx->Cookie, Object, &Id, &Name, 0);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ok(RtlEqualUnicodeString(Name, &Expected, TRUE), "Name = %wZ\n", Name);
                CmCallbackReleaseKeyObjectIDEx(Name);
            }
            Status = CmCallbackGetKeyObjectIDEx(&Ctx->Cookie, SecondObject, &SecondId, NULL, 0);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulongptr(Id, SecondId);

            Status = CmCallbackGetKeyObjectIDEx(&Ctx->Cookie, Object, &Id, NULL, 1);
            ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

            Name = NULL;
            Status = CmCallbackGetKeyObjectID(&Ctx->Cookie, Object, &Id, &Name);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
                ok(RtlEqualUnicodeString(Name, &Expected, TRUE), "Name = %wZ\n", Name);

            ok_eq_pointer(CmGetBoundTransaction(&Ctx->Cookie, Object), NULL);

            ObDereferenceObject(SecondObject);
            ObDereferenceObject(Object);
            ZwClose(Second);
        }
        ZwClose(Key);
    }

    Status = CmUnRegisterCallback(Ctx->Cookie);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ExFreePoolWithTag(Ctx, 'CmtK');
    DeleteKey(&KeyName);
}

typedef struct _PROBE_CONTEXT
{
    LARGE_INTEGER Cookie;
    ULONG Opens;
    ULONG Closes;
    UNICODE_STRING CompleteName;
    WCHAR CompleteNameBuffer[96];
    UNICODE_STRING RemainingName;
    WCHAR RemainingNameBuffer[96];
    NTSTATUS ReplayStatus;
    NTSTATUS RootStatus;
    HANDLE CloseProcessId;
    BOOLEAN Replaying;
    BOOLEAN Listing;
    ULONG Count;
    ULONG Classes[32];
    BOOLEAN RootMatches[32];
    NTSTATUS Statuses[32];
    WCHAR Names[32][48];
} PROBE_CONTEXT, *PPROBE_CONTEXT;

static
NTSTATUS
NTAPI
ProbeCallback(
    _In_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(ROOT_KEY_NAME);
    PPROBE_CONTEXT Probe = CallbackContext;
    REG_NOTIFY_CLASS NotifyClass = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;
    PREG_OPEN_KEY_INFORMATION_V1 OpenInfo = Argument2;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE RootHandle, Key;
    NTSTATUS Status;

    if (PsGetCurrentThread() != TestThread)
        return STATUS_SUCCESS;

    if (Probe->Listing)
    {
        if ((NotifyClass == RegNtPreCreateKeyEx || NotifyClass == RegNtPreOpenKeyEx) && Probe->Count < 32)
        {
            ULONG Length = min(OpenInfo->CompleteName->Length, 47 * sizeof(WCHAR));

            Probe->Classes[Probe->Count] = NotifyClass;
            Probe->Statuses[Probe->Count] = STATUS_SUCCESS;
            Probe->RootMatches[Probe->Count] = (OpenInfo->RootObject == RootObject);
            RtlCopyMemory(Probe->Names[Probe->Count], OpenInfo->CompleteName->Buffer, Length);
            Probe->Names[Probe->Count][Length / sizeof(WCHAR)] = UNICODE_NULL;
            Probe->Count++;
        }
        else if ((NotifyClass == RegNtPostCreateKeyEx || NotifyClass == RegNtPostOpenKeyEx) && Probe->Count < 32)
        {
            Probe->Classes[Probe->Count] = NotifyClass;
            Probe->Statuses[Probe->Count] = ((PREG_POST_OPERATION_INFORMATION)Argument2)->Status;
            Probe->RootMatches[Probe->Count] = FALSE;
            Probe->Names[Probe->Count][0] = UNICODE_NULL;
            Probe->Count++;
        }
        else if (NotifyClass == RegNtPreKeyHandleClose && Probe->Count < 32)
        {
            Probe->Classes[Probe->Count] = NotifyClass;
            Probe->Statuses[Probe->Count] = STATUS_SUCCESS;
            Probe->RootMatches[Probe->Count] = FALSE;
            Probe->Names[Probe->Count][0] = UNICODE_NULL;
            Probe->Count++;
        }
        return STATUS_SUCCESS;
    }

    if (NotifyClass == RegNtPreKeyHandleClose)
    {
        Probe->Closes++;
        Probe->CloseProcessId = PsGetCurrentProcessId();
        return STATUS_SUCCESS;
    }

    if (NotifyClass != RegNtPreOpenKeyEx || Probe->Replaying ||
        !RtlEqualUnicodeString(OpenInfo->CompleteName, &RootName, TRUE))
    {
        return STATUS_SUCCESS;
    }

    if (Probe->Opens++ != 0)
        return STATUS_SUCCESS;

    Probe->CompleteName.Buffer = Probe->CompleteNameBuffer;
    Probe->CompleteName.MaximumLength = sizeof(Probe->CompleteNameBuffer);
    RtlCopyUnicodeString(&Probe->CompleteName, OpenInfo->CompleteName);
    Probe->RemainingName.Buffer = Probe->RemainingNameBuffer;
    Probe->RemainingName.MaximumLength = sizeof(Probe->RemainingNameBuffer);
    RtlCopyUnicodeString(&Probe->RemainingName, OpenInfo->RemainingName);

    Probe->Replaying = TRUE;
    Status = ObOpenObjectByPointer(OpenInfo->RootObject,
                                   OBJ_KERNEL_HANDLE,
                                   NULL,
                                   KEY_ALL_ACCESS,
                                   OpenInfo->ObjectType,
                                   KernelMode,
                                   &RootHandle);
    Probe->RootStatus = Status;
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   OpenInfo->RemainingName,
                                   OpenInfo->Attributes | OBJ_KERNEL_HANDLE,
                                   RootHandle,
                                   OpenInfo->SecurityDescriptor);
        Status = ZwOpenKey(&Key, OpenInfo->DesiredAccess, &ObjectAttributes);
        if (NT_SUCCESS(Status))
            ZwClose(Key);
        ZwClose(RootHandle);
    }
    Probe->ReplayStatus = Status;
    Probe->Replaying = FALSE;
    return STATUS_SUCCESS;
}

static
VOID
TestAbsoluteOpenAndClose(VOID)
{
    UNICODE_STRING Altitude = RTL_CONSTANT_STRING(L"380030");
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(ROOT_KEY_NAME);
    UNICODE_STRING Relative = RTL_CONSTANT_STRING(L"Machine\\Software\\KmtCmCallbacks");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Before, During, Attached;
    PPROBE_CONTEXT Probe;
    KAPC_STATE ApcState;
    NTSTATUS Status;
    ULONG Closes, Index;
    HANDLE Key;

    Probe = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Probe), 'CmtK');
    ok(Probe != NULL, "Out of memory\n");
    if (Probe == NULL) return;
    RtlZeroMemory(Probe, sizeof(*Probe));
    Probe->ReplayStatus = STATUS_UNSUCCESSFUL;

    Status = CmRegisterCallbackEx(ProbeCallback, &Altitude, KmtDriverObject, Probe, &Probe->Cookie, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes, &RootName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
        Status = ZwOpenKey(&Key, KEY_READ, &ObjectAttributes);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Probe->Opens >= 1, "Opens = %lu\n", Probe->Opens);
        ok(RtlEqualUnicodeString(&Probe->CompleteName, &RootName, TRUE), "CompleteName = %wZ\n", &Probe->CompleteName);
        ok(RtlEqualUnicodeString(&Probe->RemainingName, &Relative, TRUE), "RemainingName = %wZ\n", &Probe->RemainingName);
        ok_eq_hex(Probe->RootStatus, STATUS_INVALID_PARAMETER);
        ok_eq_hex(Probe->ReplayStatus, STATUS_INVALID_PARAMETER);
        if (NT_SUCCESS(Status))
        {
            Closes = Probe->Closes;
            ZwClose(Key);
            ok_eq_ulong(Probe->Closes, Closes + 1);
            ok_eq_pointer(Probe->CloseProcessId, PsGetCurrentProcessId());
        }

        Probe->Listing = TRUE;
        Probe->Count = 0;
        Key = NULL;
        Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Probe->Listing = FALSE;
        ok_eq_ulong(Probe->Count, 4UL);
        ok_eq_ulong(Probe->Classes[0], (ULONG)RegNtPreOpenKeyEx);
        ok_eq_ulong(Probe->Classes[1], (ULONG)RegNtPostOpenKeyEx);
        ok_eq_hex(Probe->Statuses[1], STATUS_OBJECT_NAME_NOT_FOUND);
        ok_eq_ulong(Probe->Classes[2], (ULONG)RegNtPreCreateKeyEx);
        ok_eq_ulong(Probe->Classes[3], (ULONG)RegNtPostCreateKeyEx);
        ok_eq_hex(Probe->Statuses[3], STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Key);
        }

        Probe->Listing = TRUE;
        Probe->Count = 0;
        Key = NULL;
        Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Probe->Listing = FALSE;
        ok_eq_ulong(Probe->Count, 5UL);
        ok_eq_ulong(Probe->Classes[0], (ULONG)RegNtPreOpenKeyEx);
        ok_eq_ulong(Probe->Classes[1], (ULONG)RegNtPostOpenKeyEx);
        ok_eq_hex(Probe->Statuses[1], STATUS_SUCCESS);
        ok_eq_ulong(Probe->Classes[2], (ULONG)RegNtPreKeyHandleClose);
        ok_eq_ulong(Probe->Classes[3], (ULONG)RegNtPreCreateKeyEx);
        ok_eq_ulong(Probe->Classes[4], (ULONG)RegNtPostCreateKeyEx);
        ok_eq_hex(Probe->Statuses[4], STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Key);
        }

        Probe->Listing = TRUE;
        Probe->Count = 0;
        Key = NULL;
        Status = OpenRelative(RootObject, &KeyName, FALSE, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Probe->Listing = FALSE;
        ok_eq_ulong(Probe->Count, 2UL);
        ok_eq_ulong(Probe->Classes[0], (ULONG)RegNtPreOpenKeyEx);
        ok_eq_ulong(Probe->Classes[1], (ULONG)RegNtPostOpenKeyEx);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Key);
        }
        DeleteKey(&KeyName);

        Status = CmUnRegisterCallback(Probe->Cookie);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    {
        PCALLBACK_CONTEXT Contexts = ExAllocatePoolWithTag(NonPagedPool, 3 * sizeof(*Contexts), 'CmtK');

        if (Contexts != NULL)
        {
            Status = OpenRelative(RootObject, &OtherKeyName, TRUE, &Key);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status)) ZwClose(Key);

            Status = Register(&Contexts[0], ModeCount, L"380020.5");
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = Register(&Contexts[1], ModeInternal, L"380010");
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = Register(&Contexts[2], ModeCount, L"370020");
            ok_eq_hex(Status, STATUS_SUCCESS);

            RtlZeroMemory(Probe, sizeof(*Probe));
            RtlInitUnicodeString(&Altitude, L"370015");
            Status = CmRegisterCallbackEx(ProbeCallback, &Altitude, KmtDriverObject, Probe, &Probe->Cookie, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);

            Probe->Listing = TRUE;
            Probe->Count = 0;
            Key = NULL;
            Status = OpenRelative(RootObject, &KeyName, TRUE, &Key);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Probe->Listing = FALSE;
            ok_eq_ulong(Probe->Count, 16UL);
            for (Index = 0; Index < 16 && Index < Probe->Count; Index++)
            {
                static const ULONG Expected[16] =
                {
                    RegNtPreOpenKeyEx, RegNtPostOpenKeyEx, RegNtPreKeyHandleClose,
                    RegNtPreOpenKeyEx, RegNtPostOpenKeyEx,
                    RegNtPreOpenKeyEx, RegNtPostOpenKeyEx, RegNtPreKeyHandleClose,
                    RegNtPreOpenKeyEx, RegNtPostOpenKeyEx, RegNtPreKeyHandleClose,
                    RegNtPreCreateKeyEx, RegNtPostCreateKeyEx,
                    RegNtPreOpenKeyEx, RegNtPostOpenKeyEx, RegNtPreKeyHandleClose
                };

                ok(Probe->Classes[Index] == Expected[Index],
                   "Notification %lu is class %lu, expected %lu\n", Index, Probe->Classes[Index], Expected[Index]);
            }
            ok_eq_hex(Probe->Statuses[4], STATUS_OBJECT_NAME_NOT_FOUND);
            ok_eq_hex(Probe->Statuses[12], STATUS_SUCCESS);
            ok_eq_ulong(Contexts[0].PreCount, 2UL);
            ok_eq_ulong(Contexts[0].PostCount, 2UL);
            ok_eq_ulong(Contexts[1].PreCount, 2UL);
            ok_eq_ulong(Contexts[1].PostCount, 2UL);
            ok_eq_ulong(Contexts[2].PreCount, 10UL);
            ok_eq_ulong(Contexts[2].PostCount, 10UL);
            if (NT_SUCCESS(Status)) ZwClose(Key);

            CmUnRegisterCallback(Probe->Cookie);
            CmUnRegisterCallback(Contexts[0].Cookie);
            CmUnRegisterCallback(Contexts[1].Cookie);
            CmUnRegisterCallback(Contexts[2].Cookie);
            DeleteKey(&KeyName);
            DeleteKey(&OtherKeyName);
            ExFreePoolWithTag(Contexts, 'CmtK');
        }
    }
    ExFreePoolWithTag(Probe, 'CmtK');

    Before = PsGetCurrentProcessId();
    KeStackAttachProcess((PVOID)PsInitialSystemProcess, &ApcState);
    During = PsGetCurrentProcessId();
    Attached = PsGetProcessId(PsGetCurrentProcess());
    KeUnstackDetachProcess(&ApcState);
    ok_eq_pointer(During, Before);
    ok_eq_pointer(Attached, PsGetProcessId(PsInitialSystemProcess));
}

START_TEST(CmCallbacks)
{
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(ROOT_KEY_NAME);
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    TestThread = PsGetCurrentThread();

    InitializeObjectAttributes(&ObjectAttributes, &RootName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateKey(&RootKey, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status), "No test key\n"))
        return;

    Status = ObReferenceObjectByHandle(RootKey, 0, NULL, KernelMode, &RootObject, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        TestRegistration();
        TestBlockAndBypass();
        TestPostOverride();
        TestContexts();
        TestAltitudes();
        TestCreateInformation();
        TestAbsoluteOpenAndClose();
        ObDereferenceObject(RootObject);
    }

    ZwDeleteKey(RootKey);
    ZwClose(RootKey);
}
