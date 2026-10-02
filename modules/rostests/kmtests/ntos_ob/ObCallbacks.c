/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite ObRegisterCallbacks API
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef struct _CALLBACK_RECORD
{
    ULONG PreCount;
    ULONG PostCount;
    OB_OPERATION Operation;
    ULONG KernelHandle;
    PVOID Object;
    POBJECT_TYPE ObjectType;
    ACCESS_MASK DesiredAccess;
    ACCESS_MASK OriginalDesiredAccess;
    PVOID SourceProcess;
    PVOID TargetProcess;
    PVOID PostCallContext;
    NTSTATUS ReturnStatus;
    ACCESS_MASK GrantedAccess;
    ACCESS_MASK ClearAccess;
    ACCESS_MASK AddAccess;
} CALLBACK_RECORD, *PCALLBACK_RECORD;

static CALLBACK_RECORD Record;
static PVOID TestObject;

static
OB_PREOP_CALLBACK_STATUS
NTAPI
PreCallback(
    _In_ PVOID RegistrationContext,
    _Inout_ POB_PRE_OPERATION_INFORMATION Information)
{
    PCALLBACK_RECORD Rec = RegistrationContext;

    if (Information->Object != TestObject)
        return OB_PREOP_SUCCESS;

    Rec->PreCount++;
    Rec->Operation = Information->Operation;
    Rec->KernelHandle = Information->KernelHandle;
    Rec->Object = Information->Object;
    Rec->ObjectType = Information->ObjectType;
    if (Information->Operation == OB_OPERATION_HANDLE_DUPLICATE)
    {
        Rec->DesiredAccess = Information->Parameters->DuplicateHandleInformation.DesiredAccess;
        Rec->OriginalDesiredAccess = Information->Parameters->DuplicateHandleInformation.OriginalDesiredAccess;
        Rec->SourceProcess = Information->Parameters->DuplicateHandleInformation.SourceProcess;
        Rec->TargetProcess = Information->Parameters->DuplicateHandleInformation.TargetProcess;
        Information->Parameters->DuplicateHandleInformation.DesiredAccess &= ~Rec->ClearAccess;
        Information->Parameters->DuplicateHandleInformation.DesiredAccess |= Rec->AddAccess;
    }
    else
    {
        Rec->DesiredAccess = Information->Parameters->CreateHandleInformation.DesiredAccess;
        Rec->OriginalDesiredAccess = Information->Parameters->CreateHandleInformation.OriginalDesiredAccess;
        Information->Parameters->CreateHandleInformation.DesiredAccess &= ~Rec->ClearAccess;
        Information->Parameters->CreateHandleInformation.DesiredAccess |= Rec->AddAccess;
    }
    Information->CallContext = Rec;
    return OB_PREOP_SUCCESS;
}

static
VOID
NTAPI
PostCallback(
    _In_ PVOID RegistrationContext,
    _In_ POB_POST_OPERATION_INFORMATION Information)
{
    PCALLBACK_RECORD Rec = RegistrationContext;

    if (Information->Object != TestObject)
        return;

    Rec->PostCount++;
    Rec->PostCallContext = Information->CallContext;
    Rec->ReturnStatus = Information->ReturnStatus;
    if (Information->Operation == OB_OPERATION_HANDLE_DUPLICATE)
        Rec->GrantedAccess = Information->Parameters->DuplicateHandleInformation.GrantedAccess;
    else
        Rec->GrantedAccess = Information->Parameters->CreateHandleInformation.GrantedAccess;
}

static
ACCESS_MASK
HandleAccess(
    _In_ HANDLE Handle,
    _In_ POBJECT_TYPE ObjectType)
{
    OBJECT_HANDLE_INFORMATION HandleInformation;
    PVOID Object;
    NTSTATUS Status;

    HandleInformation.GrantedAccess = 0;
    Status = ObReferenceObjectByHandle(Handle, 0, ObjectType, KernelMode, &Object, &HandleInformation);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ObDereferenceObject(Object);
    return HandleInformation.GrantedAccess;
}

static
VOID
ResetRecord(
    _In_ ACCESS_MASK ClearAccess,
    _In_ ACCESS_MASK AddAccess)
{
    RtlZeroMemory(&Record, sizeof(Record));
    Record.ReturnStatus = STATUS_UNSUCCESSFUL;
    Record.ClearAccess = ClearAccess;
    Record.AddAccess = AddAccess;
}

static
VOID
TestRegistration(VOID)
{
    OB_CALLBACK_REGISTRATION Registration;
    OB_OPERATION_REGISTRATION Operation;
    PVOID Handle, SecondHandle;
    NTSTATUS Status;

    ok_eq_uint(ObGetFilterVersion(), OB_FLT_REGISTRATION_VERSION);

    Operation.ObjectType = PsProcessType;
    Operation.Operations = OB_OPERATION_HANDLE_CREATE;
    Operation.PreOperation = PreCallback;
    Operation.PostOperation = PostCallback;

    Registration.Version = OB_FLT_REGISTRATION_VERSION + 1;
    Registration.OperationRegistrationCount = 1;
    RtlInitUnicodeString(&Registration.Altitude, L"385210.25");
    Registration.RegistrationContext = &Record;
    Registration.OperationRegistration = &Operation;

    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(Handle);

    Registration.Version = 0x01FF;
    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(Handle);

    Registration.Version = 0x0200;
    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(Handle);

    Registration.Version = 0x0001;
    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(Handle);

    Registration.Version = OB_FLT_REGISTRATION_VERSION;
    Operation.ObjectType = IoFileObjectType;
    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(Handle);

    Operation.ObjectType = PsProcessType;
    Operation.PreOperation = NULL;
    Operation.PostOperation = NULL;
    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok(!NT_SUCCESS(Status), "Status = 0x%08lx\n", Status);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(Handle);

    Operation.PreOperation = PreCallback;
    Operation.PostOperation = PostCallback;
    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    ok(Handle != NULL, "Handle is NULL\n");

    SecondHandle = NULL;
    Status = ObRegisterCallbacks(&Registration, &SecondHandle);
    ok_eq_hex(Status, STATUS_FLT_INSTANCE_ALTITUDE_COLLISION);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(SecondHandle);

    ObUnRegisterCallbacks(Handle);

    Handle = NULL;
    Status = ObRegisterCallbacks(&Registration, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status)) ObUnRegisterCallbacks(Handle);
}

static
VOID
TestOperations(
    _In_ POBJECT_TYPE *ObjectType,
    _In_ PVOID Object,
    _In_ ACCESS_MASK AllAccess,
    _In_ ACCESS_MASK ClearAccess,
    _In_ ACCESS_MASK BaseAccess)
{
    OB_CALLBACK_REGISTRATION Registration;
    OB_OPERATION_REGISTRATION Operation;
    PVOID RegistrationHandle = NULL;
    HANDLE Handle, Duplicate;
    NTSTATUS Status;

    Handle = NULL;
    Status = ObOpenObjectByPointer(Object, 0, NULL, AllAccess, *ObjectType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    AllAccess = HandleAccess(Handle, *ObjectType);
    ObCloseHandle(Handle, KernelMode);

    Handle = NULL;
    Status = ObOpenObjectByPointer(Object, 0, NULL, BaseAccess, *ObjectType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    BaseAccess = HandleAccess(Handle, *ObjectType);
    ObCloseHandle(Handle, KernelMode);

    Operation.ObjectType = ObjectType;
    Operation.Operations = OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE;
    Operation.PreOperation = PreCallback;
    Operation.PostOperation = PostCallback;

    Registration.Version = OB_FLT_REGISTRATION_VERSION;
    Registration.OperationRegistrationCount = 1;
    RtlInitUnicodeString(&Registration.Altitude, L"385210.25");
    Registration.RegistrationContext = &Record;
    Registration.OperationRegistration = &Operation;

    Status = ObRegisterCallbacks(&Registration, &RegistrationHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    TestObject = Object;

    ResetRecord(ClearAccess, 0);
    Handle = NULL;
    Status = ObOpenObjectByPointer(Object, 0, NULL, AllAccess, *ObjectType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Record.PreCount, 1UL);
    ok_eq_ulong(Record.PostCount, 1UL);
    ok_eq_hex(Record.Operation, OB_OPERATION_HANDLE_CREATE);
    ok_eq_ulong(Record.KernelHandle, 0UL);
    ok_eq_pointer(Record.Object, Object);
    ok_eq_pointer(Record.ObjectType, *ObjectType);
    ok_eq_hex(Record.DesiredAccess, AllAccess);
    ok_eq_hex(Record.OriginalDesiredAccess, AllAccess);
    ok_eq_pointer(Record.PostCallContext, &Record);
    ok_eq_hex(Record.ReturnStatus, STATUS_SUCCESS);
    ok_eq_hex(Record.GrantedAccess, AllAccess & ~ClearAccess);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(HandleAccess(Handle, *ObjectType), AllAccess & ~ClearAccess);

        ResetRecord(0, 0);
        Duplicate = NULL;
        Status = ZwDuplicateObject(ZwCurrentProcess(), Handle, ZwCurrentProcess(), &Duplicate, 0, 0, DUPLICATE_SAME_ACCESS);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Record.PreCount, 1UL);
        ok_eq_ulong(Record.PostCount, 1UL);
        ok_eq_hex(Record.Operation, OB_OPERATION_HANDLE_DUPLICATE);
        ok_eq_hex(Record.DesiredAccess, AllAccess & ~ClearAccess);
        ok_eq_hex(Record.OriginalDesiredAccess, AllAccess & ~ClearAccess);
        ok_eq_pointer(Record.SourceProcess, PsGetCurrentProcess());
        ok_eq_pointer(Record.TargetProcess, PsGetCurrentProcess());
        ok_eq_pointer(Record.PostCallContext, &Record);
        ok_eq_hex(Record.ReturnStatus, STATUS_SUCCESS);
        ok_eq_hex(Record.GrantedAccess, AllAccess & ~ClearAccess);
        if (NT_SUCCESS(Status))
            ObCloseHandle(Duplicate, KernelMode);

        ObCloseHandle(Handle, KernelMode);
    }

    ResetRecord(0, ClearAccess);
    Handle = NULL;
    Status = ObOpenObjectByPointer(Object, 0, NULL, BaseAccess, *ObjectType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Record.PreCount, 1UL);
    ok_eq_hex(Record.DesiredAccess, BaseAccess);
    ok_eq_hex(Record.GrantedAccess, BaseAccess);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(HandleAccess(Handle, *ObjectType), BaseAccess);
        ObCloseHandle(Handle, KernelMode);
    }

    ResetRecord(0, 0);
    Handle = NULL;
    Status = ObOpenObjectByPointer(Object, OBJ_KERNEL_HANDLE, NULL, BaseAccess, *ObjectType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Record.PreCount, 1UL);
    ok_eq_ulong(Record.PostCount, 1UL);
    ok_eq_ulong(Record.KernelHandle, 1UL);
    if (NT_SUCCESS(Status))
        ObCloseHandle(Handle, KernelMode);

    ObUnRegisterCallbacks(RegistrationHandle);

    ResetRecord(ClearAccess, 0);
    Handle = NULL;
    Status = ObOpenObjectByPointer(Object, 0, NULL, AllAccess, *ObjectType, KernelMode, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Record.PreCount, 0UL);
    ok_eq_ulong(Record.PostCount, 0UL);
    if (NT_SUCCESS(Status))
    {
        ok_eq_hex(HandleAccess(Handle, *ObjectType), AllAccess);
        ObCloseHandle(Handle, KernelMode);
    }

    TestObject = NULL;
}

START_TEST(ObCallbacks)
{
    TestRegistration();
    TestOperations(PsProcessType,
                   PsGetCurrentProcess(),
                   PROCESS_ALL_ACCESS,
                   PROCESS_TERMINATE | PROCESS_VM_WRITE,
                   PROCESS_QUERY_INFORMATION);
    TestOperations(PsThreadType,
                   PsGetCurrentThread(),
                   THREAD_ALL_ACCESS,
                   THREAD_TERMINATE | THREAD_SET_CONTEXT,
                   THREAD_QUERY_INFORMATION);
}
