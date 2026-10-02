/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite transacted registry operations
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define ROOT_PATH L"\\Registry\\Machine\\Software\\KmtCmTransactions"

typedef struct _CALLBACK_RECORD
{
    LARGE_INTEGER Cookie;
    PETHREAD Thread;
    ULONG Creates;
    ULONG Opens;
    ULONG Sets;
    PVOID CreateTransaction;
    PVOID CreateBound;
    PVOID OpenTransaction;
    PVOID SetBound;
} CALLBACK_RECORD, *PCALLBACK_RECORD;

static HANDLE RootKey;
static UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"Value");
static UNICODE_STRING NewValueName = RTL_CONSTANT_STRING(L"New");
static UNICODE_STRING OtherValueName = RTL_CONSTANT_STRING(L"Other");

static
NTSTATUS
CreateTransaction(
    _Out_ PHANDLE Transaction)
{
    OBJECT_ATTRIBUTES ObjectAttributes;

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    return ZwCreateTransaction(Transaction,
                               TRANSACTION_ALL_ACCESS,
                               &ObjectAttributes,
                               NULL,
                               NULL,
                               0,
                               0,
                               0,
                               NULL,
                               NULL);
}

static
NTSTATUS
OpenPlain(
    _In_opt_ HANDLE Parent,
    _In_ PCWSTR Name,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING String;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&ObjectAttributes, &String, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
    *Key = NULL;
    return ZwOpenKey(Key, KEY_ALL_ACCESS, &ObjectAttributes);
}

static
NTSTATUS
CreatePlain(
    _In_opt_ HANDLE Parent,
    _In_ PCWSTR Name,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING String;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&ObjectAttributes, &String, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
    *Key = NULL;
    return ZwCreateKey(Key, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, 0, NULL);
}

static
NTSTATUS
OpenTransacted(
    _In_opt_ HANDLE Parent,
    _In_ PCWSTR Name,
    _In_ HANDLE Transaction,
    _Out_ PHANDLE Key)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING String;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&ObjectAttributes, &String, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
    *Key = NULL;
    return ZwOpenKeyTransacted(Key, KEY_ALL_ACCESS, &ObjectAttributes, Transaction);
}

static
NTSTATUS
CreateTransacted(
    _In_opt_ HANDLE Parent,
    _In_ PCWSTR Name,
    _In_ HANDLE Transaction,
    _Out_ PHANDLE Key,
    _Out_opt_ PULONG Disposition)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING String;

    RtlInitUnicodeString(&String, Name);
    InitializeObjectAttributes(&ObjectAttributes, &String, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, Parent, NULL);
    *Key = NULL;
    return ZwCreateKeyTransacted(Key, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, 0, Transaction, Disposition);
}

static
NTSTATUS
SetDword(
    _In_ HANDLE Key,
    _In_ PUNICODE_STRING Name,
    _In_ ULONG Value)
{
    return ZwSetValueKey(Key, Name, 0, REG_DWORD, &Value, sizeof(Value));
}

static
NTSTATUS
QueryDword(
    _In_ HANDLE Key,
    _In_ PUNICODE_STRING Name,
    _Out_ PULONG Value)
{
    UCHAR Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PVOID)Buffer;
    NTSTATUS Status;
    ULONG Length;

    *Value = 0;
    Status = ZwQueryValueKey(Key, Name, KeyValuePartialInformation, Information, sizeof(Buffer), &Length);
    if (NT_SUCCESS(Status) && Information->DataLength == sizeof(ULONG))
    {
        RtlCopyMemory(Value, Information->Data, sizeof(ULONG));
    }
    return Status;
}

static
NTSTATUS
QueryCounts(
    _In_ HANDLE Key,
    _Out_ PULONG SubKeys,
    _Out_ PULONG Values)
{
    UCHAR Buffer[sizeof(KEY_FULL_INFORMATION) + 64 * sizeof(WCHAR)];
    PKEY_FULL_INFORMATION Information = (PVOID)Buffer;
    NTSTATUS Status;
    ULONG Length;

    *SubKeys = *Values = MAXULONG;
    Status = ZwQueryKey(Key, KeyFullInformation, Information, sizeof(Buffer), &Length);
    if (NT_SUCCESS(Status))
    {
        *SubKeys = Information->SubKeys;
        *Values = Information->Values;
    }
    return Status;
}

static
VOID
DeleteTree(
    _In_ HANDLE Parent,
    _In_ PCWSTR Name)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 64 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Information = (PVOID)Buffer;
    WCHAR Child[65];
    HANDLE Key;
    ULONG Length, Guard = 0;

    if (!NT_SUCCESS(OpenPlain(Parent, Name, &Key)))
    {
        return;
    }

    while (Guard++ < 32 &&
           NT_SUCCESS(ZwEnumerateKey(Key, 0, KeyBasicInformation, Information, sizeof(Buffer), &Length)))
    {
        Length = min(Information->NameLength, 64 * sizeof(WCHAR));
        RtlCopyMemory(Child, Information->Name, Length);
        Child[Length / sizeof(WCHAR)] = UNICODE_NULL;
        DeleteTree(Key, Child);
    }

    ZwDeleteKey(Key);
    ZwClose(Key);
}

static
VOID
TestCreateCommit(VOID)
{
    UCHAR Buffer[sizeof(KEY_BASIC_INFORMATION) + 64 * sizeof(WCHAR)];
    PKEY_BASIC_INFORMATION Information = (PVOID)Buffer;
    UNICODE_STRING Expected = RTL_CONSTANT_STRING(L"TxKey");
    HANDLE Transaction, Other, Key, Sub, Handle, TransactedRoot;
    ULONG Disposition, Value, SubKeys, Values, Length;
    UNICODE_STRING Name;
    NTSTATUS Status;

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Disposition = 0;
    Status = CreateTransacted(RootKey, L"TxKey", Transaction, &Key, &Disposition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Transaction);
        return;
    }
    ok_eq_ulong(Disposition, (ULONG)REG_CREATED_NEW_KEY);

    Status = OpenPlain(RootKey, L"TxKey", &Handle);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = OpenTransacted(RootKey, L"TxKey", Transaction, &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Disposition = 0;
    Status = CreateTransacted(RootKey, L"TxKey", Transaction, &Handle, &Disposition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Disposition, (ULONG)REG_OPENED_EXISTING_KEY);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = CreateTransaction(&Other);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = OpenTransacted(RootKey, L"TxKey", Other, &Handle);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }
        ZwClose(Other);
    }

    Status = SetDword(Key, &ValueName, 0x1234);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = QueryDword(Key, &ValueName, &Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Value, 0x1234UL);

    Status = CreatePlain(Key, L"Sub", &Sub);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = OpenPlain(RootKey, L"TxKey\\Sub", &Handle);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = QueryCounts(RootKey, &SubKeys, &Values);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(SubKeys, 0UL);
    Status = ZwEnumerateKey(RootKey, 0, KeyBasicInformation, Information, sizeof(Buffer), &Length);
    ok_eq_hex(Status, STATUS_NO_MORE_ENTRIES);

    Status = OpenTransacted(NULL, ROOT_PATH, Transaction, &TransactedRoot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = QueryCounts(TransactedRoot, &SubKeys, &Values);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(SubKeys, 1UL);

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = ZwEnumerateKey(TransactedRoot, 0, KeyBasicInformation, Information, sizeof(Buffer), &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Name.Buffer = Information->Name;
            Name.Length = Name.MaximumLength = (USHORT)Information->NameLength;
            ok(RtlEqualUnicodeString(&Name, &Expected, TRUE), "Enumerated %wZ\n", &Name);
        }
        Status = ZwEnumerateKey(TransactedRoot, 1, KeyBasicInformation, Information, sizeof(Buffer), &Length);
        ok_eq_hex(Status, STATUS_NO_MORE_ENTRIES);

        Status = OpenPlain(TransactedRoot, L"TxKey", &Handle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = QueryDword(Handle, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_hex(Value, 0x1234UL);
            ZwClose(Handle);
        }
        ZwClose(TransactedRoot);
    }

    Status = ZwCommitTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = OpenPlain(RootKey, L"TxKey", &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = QueryDword(Handle, &ValueName, &Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_hex(Value, 0x1234UL);
        ZwClose(Handle);
    }
    Status = OpenPlain(RootKey, L"TxKey\\Sub", &Handle);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    Status = QueryDword(Key, &ValueName, &Value);
    ok_eq_hex(Status, STATUS_TRANSACTION_NOT_ACTIVE);
    Status = SetDword(Key, &NewValueName, 1);
    ok_eq_hex(Status, STATUS_TRANSACTION_NOT_ACTIVE);
    Status = QueryCounts(Key, &SubKeys, &Values);
    ok_eq_hex(Status, STATUS_TRANSACTION_NOT_ACTIVE);
    Status = OpenTransacted(RootKey, L"TxKey", Transaction, &Handle);
    ok_eq_hex(Status, STATUS_TRANSACTION_NOT_ACTIVE);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
    }

    if (Sub != NULL)
    {
        ZwClose(Sub);
    }
    ZwClose(Key);
    ZwClose(Transaction);
}

static
VOID
TestCreateRollback(VOID)
{
    HANDLE Transaction, Key, Handle;
    ULONG Value, SubKeys, Values;
    NTSTATUS Status;

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = CreateTransacted(RootKey, L"RbKey", Transaction, &Key, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = SetDword(Key, &ValueName, 7);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = ZwRollbackTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = OpenPlain(RootKey, L"RbKey", &Handle);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }

        Status = QueryDword(Key, &ValueName, &Value);
        ok_eq_hex(Status, STATUS_KEY_DELETED);
        Status = SetDword(Key, &NewValueName, 1);
        ok_eq_hex(Status, STATUS_KEY_DELETED);
        Status = QueryCounts(Key, &SubKeys, &Values);
        ok_eq_hex(Status, STATUS_KEY_DELETED);
        Status = ZwDeleteKey(Key);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = CreateTransacted(RootKey, L"RbKey2", Transaction, &Handle, NULL);
        ok_eq_hex(Status, STATUS_TRANSACTION_NOT_ACTIVE);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }
        ZwClose(Key);
    }
    ZwClose(Transaction);

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = CreateTransacted(RootKey, L"RbKey3", Transaction, &Key, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ZwClose(Transaction);

        Status = OpenPlain(RootKey, L"RbKey3", &Handle);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }
        if (Key != NULL)
        {
            ZwClose(Key);
        }
    }
}

static
VOID
TestExistingKey(VOID)
{
    UCHAR Buffer[sizeof(KEY_VALUE_BASIC_INFORMATION) + 64 * sizeof(WCHAR)];
    PKEY_VALUE_BASIC_INFORMATION Information = (PVOID)Buffer;
    HANDLE Transaction, Plain, Key;
    ULONG Value, SubKeys, Values, Length;
    UNICODE_STRING Name;
    NTSTATUS Status;

    Status = CreatePlain(RootKey, L"Exist", &Plain);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }
    Status = SetDword(Plain, &ValueName, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Plain);
        return;
    }

    Status = OpenTransacted(RootKey, L"Exist", Transaction, &Key);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = SetDword(Key, &ValueName, 2);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = SetDword(Key, &NewValueName, 3);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = QueryDword(Key, &ValueName, &Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Value, 2UL);
        Status = QueryDword(Key, &NewValueName, &Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Value, 3UL);

        Status = QueryDword(Plain, &ValueName, &Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Value, 1UL);
        Status = QueryDword(Plain, &NewValueName, &Value);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

        Status = ZwDeleteValueKey(Key, &ValueName);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = QueryDword(Key, &ValueName, &Value);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        Status = QueryDword(Plain, &ValueName, &Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Value, 1UL);

        Status = QueryCounts(Key, &SubKeys, &Values);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Values, 1UL);
        Status = QueryCounts(Plain, &SubKeys, &Values);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Values, 1UL);

        RtlZeroMemory(Buffer, sizeof(Buffer));
        Status = ZwEnumerateValueKey(Key, 0, KeyValueBasicInformation, Information, sizeof(Buffer), &Length);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Name.Buffer = Information->Name;
            Name.Length = Name.MaximumLength = (USHORT)Information->NameLength;
            ok(RtlEqualUnicodeString(&Name, &NewValueName, TRUE), "Enumerated %wZ\n", &Name);
        }
        Status = ZwEnumerateValueKey(Key, 1, KeyValueBasicInformation, Information, sizeof(Buffer), &Length);
        ok_eq_hex(Status, STATUS_NO_MORE_ENTRIES);

        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);

        Status = QueryDword(Plain, &ValueName, &Value);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        Status = QueryDword(Plain, &NewValueName, &Value);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Value, 3UL);
        Status = SetDword(Plain, &OtherValueName, 5);
        ok_eq_hex(Status, STATUS_SUCCESS);

        ZwClose(Key);
    }

    ZwClose(Transaction);
    ZwClose(Plain);
}

static
VOID
TestDelete(VOID)
{
    HANDLE Transaction, Plain, Key, Handle;
    NTSTATUS Status;

    Status = CreatePlain(RootKey, L"Del", &Plain);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }
    ZwClose(Plain);

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = OpenTransacted(RootKey, L"Del", Transaction, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = ZwDeleteKey(Key);
            ok_eq_hex(Status, STATUS_SUCCESS);

            Status = OpenPlain(RootKey, L"Del", &Handle);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ZwClose(Handle);
            }
            Status = OpenTransacted(RootKey, L"Del", Transaction, &Handle);
            ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
            if (NT_SUCCESS(Status))
            {
                ZwClose(Handle);
            }
            ZwClose(Key);
        }

        Status = ZwRollbackTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = OpenPlain(RootKey, L"Del", &Handle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = OpenTransacted(RootKey, L"Del", Transaction, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = ZwDeleteKey(Key);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ZwClose(Key);
        }
        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = OpenPlain(RootKey, L"Del", &Handle);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }
        ZwClose(Transaction);
    }
}

static
ULONG
QueryOutcome(
    _In_ HANDLE Transaction)
{
    TRANSACTION_BASIC_INFORMATION Basic;

    RtlZeroMemory(&Basic, sizeof(Basic));
    ZwQueryInformationTransaction(Transaction, TransactionBasicInformation, &Basic, sizeof(Basic), NULL);
    return Basic.Outcome;
}

static
VOID
TestConflicts(VOID)
{
    HANDLE Transaction, Other, Plain, Key, Handle;
    ULONG Value;
    NTSTATUS Status;

    Status = CreatePlain(RootKey, L"Conflict", &Plain);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }
    Status = SetDword(Plain, &ValueName, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = OpenTransacted(RootKey, L"Conflict", Transaction, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = QueryDword(Key, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = SetDword(Plain, &OtherValueName, 5);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeUndetermined);
            Status = QueryDword(Key, &OtherValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Value, 5UL);

            Status = SetDword(Key, &ValueName, 2);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = QueryDword(Plain, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Value, 1UL);
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeUndetermined);

            Status = SetDword(Plain, &OtherValueName, 6);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
            Status = QueryDword(Key, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_TRANSACTION_NOT_ACTIVE);
            Status = QueryDword(Plain, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Value, 1UL);
            ZwClose(Key);
        }
        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        ZwClose(Transaction);
    }

    Transaction = Other = NULL;
    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = CreateTransaction(&Other);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Transaction != NULL && Other != NULL)
    {
        Status = OpenTransacted(RootKey, L"Conflict", Transaction, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        Status = OpenTransacted(RootKey, L"Conflict", Other, &Handle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (Key != NULL && Handle != NULL)
        {
            Status = SetDword(Key, &ValueName, 2);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = SetDword(Handle, &ValueName, 3);
            ok_eq_hex(Status, STATUS_TRANSACTIONAL_CONFLICT);
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeUndetermined);
            ok_eq_ulong(QueryOutcome(Other), (ULONG)TransactionOutcomeUndetermined);
            Status = QueryDword(Handle, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Value, 1UL);
            Status = ZwCommitTransaction(Other, TRUE);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = QueryDword(Plain, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Value, 1UL);
            Status = ZwCommitTransaction(Transaction, TRUE);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = QueryDword(Plain, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Value, 2UL);
        }
        if (Key != NULL)
        {
            ZwClose(Key);
        }
        if (Handle != NULL)
        {
            ZwClose(Handle);
        }
    }
    if (Transaction != NULL)
    {
        ZwClose(Transaction);
    }
    if (Other != NULL)
    {
        ZwClose(Other);
    }

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = CreateTransacted(Plain, L"Child", Transaction, &Key, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = CreatePlain(Plain, L"Sibling", &Handle);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                ZwClose(Handle);
            }
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeUndetermined);

            Status = CreatePlain(Plain, L"Child", &Handle);
            ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
            if (NT_SUCCESS(Status))
            {
                ZwClose(Handle);
            }
            ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeUndetermined);
            Status = OpenPlain(Plain, L"Child", &Handle);
            ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
            if (NT_SUCCESS(Status))
            {
                ZwClose(Handle);
            }
            Status = ZwRollbackTransaction(Transaction, TRUE);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ZwClose(Key);
        }
        ZwClose(Transaction);
    }

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = OpenTransacted(Plain, L"Sibling", Transaction, &Key);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            Status = ZwDeleteKey(Key);
            ok_eq_hex(Status, STATUS_SUCCESS);
            Status = QueryDword(Key, &ValueName, &Value);
            ok_eq_hex(Status, STATUS_KEY_DELETED);

            Status = OpenPlain(Plain, L"Sibling", &Handle);
            ok_eq_hex(Status, STATUS_SUCCESS);
            if (NT_SUCCESS(Status))
            {
                Status = SetDword(Handle, &ValueName, 9);
                ok_eq_hex(Status, STATUS_SUCCESS);
                ok_eq_ulong(QueryOutcome(Transaction), (ULONG)TransactionOutcomeAborted);
                ZwClose(Handle);
            }
            ZwClose(Key);
        }
        Status = ZwCommitTransaction(Transaction, TRUE);
        ok_eq_hex(Status, STATUS_TRANSACTION_ALREADY_ABORTED);
        Status = OpenPlain(Plain, L"Sibling", &Handle);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Handle);
        }
        ZwClose(Transaction);
    }

    ZwClose(Plain);
}

static
NTSTATUS
NTAPI
RegistryCallback(
    _In_ PVOID CallbackContext,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2)
{
    PCALLBACK_RECORD Record = CallbackContext;
    REG_NOTIFY_CLASS Class = (REG_NOTIFY_CLASS)(ULONG_PTR)Argument1;
    PREG_CREATE_KEY_INFORMATION Create = Argument2;
    PREG_SET_VALUE_KEY_INFORMATION Set = Argument2;

    if (Record->Thread != PsGetCurrentThread())
    {
        return STATUS_SUCCESS;
    }

    switch (Class)
    {
        case RegNtPreCreateKeyEx:
            Record->Creates++;
            Record->CreateTransaction = Create->Transaction;
            Record->CreateBound = CmGetBoundTransaction(&Record->Cookie, Create->RootObject);
            break;

        case RegNtPreOpenKeyEx:
            Record->Opens++;
            Record->OpenTransaction = Create->Transaction;
            break;

        case RegNtPreSetValueKey:
            Record->Sets++;
            Record->SetBound = CmGetBoundTransaction(&Record->Cookie, Set->Object);
            break;

        default:
            break;
    }
    return STATUS_SUCCESS;
}

static
VOID
TestCallbacks(VOID)
{
    UNICODE_STRING Altitude = RTL_CONSTANT_STRING(L"380010.4242");
    UCHAR Buffer[sizeof(TRANSACTION_ENLISTMENTS_INFORMATION) + 4 * sizeof(TRANSACTION_ENLISTMENT_PAIR)];
    PTRANSACTION_ENLISTMENTS_INFORMATION Enlistments = (PVOID)Buffer;
    HANDLE Transaction, TransactedRoot, Key, Plain;
    PVOID TransactionObject;
    CALLBACK_RECORD Record;
    NTSTATUS Status;
    ULONG Length;

    Status = CreateTransaction(&Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    Status = ObReferenceObjectByHandle(Transaction,
                                       0,
                                       *TmTransactionObjectType,
                                       KernelMode,
                                       &TransactionObject,
                                       NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Transaction);
        return;
    }

    RtlZeroMemory(&Record, sizeof(Record));
    Record.Thread = PsGetCurrentThread();
    Status = CmRegisterCallbackEx(RegistryCallback, &Altitude, KmtDriverObject, &Record, &Record.Cookie, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = CreateTransacted(RootKey, L"CbKey", Transaction, &Key, NULL);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Record.Creates, 1UL);
        ok_eq_pointer(Record.CreateTransaction, TransactionObject);
        ok_eq_pointer(Record.CreateBound, NULL);
        if (NT_SUCCESS(Status))
        {
            Status = SetDword(Key, &ValueName, 1);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Record.Sets, 1UL);
            ok_eq_pointer(Record.SetBound, TransactionObject);
            ZwClose(Key);
        }

        Status = OpenTransacted(NULL, ROOT_PATH, Transaction, &TransactedRoot);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Record.Opens >= 1, "Record.Opens = %lu\n", Record.Opens);
        ok_eq_pointer(Record.OpenTransaction, TransactionObject);
        if (NT_SUCCESS(Status))
        {
            Record.CreateTransaction = Record.CreateBound = (PVOID)(ULONG_PTR)1;
            Status = CreatePlain(TransactedRoot, L"CbKey2", &Key);
            ok_eq_hex(Status, STATUS_SUCCESS);
            ok_eq_ulong(Record.Creates, 2UL);
            ok_eq_pointer(Record.CreateTransaction, NULL);
            ok_eq_pointer(Record.CreateBound, TransactionObject);
            if (NT_SUCCESS(Status))
            {
                ZwClose(Key);
            }
            ZwClose(TransactedRoot);
        }

        Status = OpenPlain(RootKey, L"CbKey2", &Plain);
        ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
        if (NT_SUCCESS(Status))
        {
            ZwClose(Plain);
        }

        Record.SetBound = (PVOID)(ULONG_PTR)1;
        Status = SetDword(RootKey, &ValueName, 1);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_pointer(Record.SetBound, NULL);

        Status = CmUnRegisterCallback(Record.Cookie);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Length = 0;
    Status = ZwQueryInformationTransaction(Transaction,
                                           TransactionEnlistmentInformation,
                                           Enlistments,
                                           sizeof(Buffer),
                                           &Length);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Enlistments->NumberOfEnlistments, 1UL);

    Status = ZwRollbackTransaction(Transaction, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = OpenPlain(RootKey, L"CbKey", &Plain);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Plain);
    }

    ObDereferenceObject(TransactionObject);
    ZwClose(Transaction);
}

static
VOID
TestParameters(VOID)
{
    HANDLE Key;
    NTSTATUS Status;

    Status = CreateTransacted(RootKey, L"Bad", NULL, &Key, NULL);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Key);
    }
    Status = CreateTransacted(RootKey, L"Bad", RootKey, &Key, NULL);
    ok_eq_hex(Status, STATUS_OBJECT_TYPE_MISMATCH);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Key);
    }
    Status = OpenTransacted(RootKey, L"Bad", RootKey, &Key);
    ok_eq_hex(Status, STATUS_OBJECT_TYPE_MISMATCH);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Key);
    }
}

START_TEST(CmTransactions)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING RootName = RTL_CONSTANT_STRING(ROOT_PATH);
    NTSTATUS Status;

    DeleteTree(NULL, ROOT_PATH);

    InitializeObjectAttributes(&ObjectAttributes, &RootName, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = ZwCreateKey(&RootKey, KEY_ALL_ACCESS, &ObjectAttributes, 0, NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    TestCreateCommit();
    TestCreateRollback();
    TestExistingKey();
    TestDelete();
    TestConflicts();
    TestCallbacks();
    TestParameters();

    ZwClose(RootKey);
    DeleteTree(NULL, ROOT_PATH);
}
