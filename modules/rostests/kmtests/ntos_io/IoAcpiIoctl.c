/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite ACPI namespace requests of a device stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <acpiioct.h>
#include <poclass.h>

#define NDEBUG
#include <debug.h>

NTKERNELAPI
PDEVICE_OBJECT
NTAPI
IoGetDeviceAttachmentBaseRef(
    _In_ PDEVICE_OBJECT DeviceObject);

static PDEVICE_OBJECT Target;

static
NTSTATUS
Send(
    _In_ ULONG Code,
    _In_ PVOID Input,
    _In_ ULONG InputLength,
    _Out_ PVOID Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    KEVENT Event;
    PIRP Irp;

    *Information = 0;
    IoStatus.Status = STATUS_UNSUCCESSFUL;
    IoStatus.Information = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(Code, Target, Input, InputLength, Output, OutputLength, FALSE, &Event, &IoStatus);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(Target, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (!NT_ERROR(Status))
        *Information = IoStatus.Information;
    return Status;
}

static
PDEVICE_OBJECT
FindDevice(
    _In_ BOOLEAN Processor)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\ACPI");
    UCHAR Buffer[sizeof(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER) + 64];
    PACPI_EVAL_OUTPUT_BUFFER Result = (PVOID)Buffer;
    UCHAR InputData[32];
    PACPI_ENUM_CHILDREN_INPUT_BUFFER Input = (PVOID)InputData;
    ACPI_EVAL_INPUT_BUFFER Method;
    PDEVICE_OBJECT *List, Found = NULL;
    ULONG_PTR Information;
    PDRIVER_OBJECT Driver;
    ULONG Count = 0, Index;
    NTSTATUS Status;

    Status = ObReferenceObjectByName(&DriverName, OBJ_CASE_INSENSITIVE, NULL, 0, IoDriverObjectType, KernelMode, NULL,
                                     (PVOID *)&Driver);
    if (!NT_SUCCESS(Status))
        return NULL;

    IoEnumerateDeviceObjectList(Driver, NULL, 0, &Count);
    List = ExAllocatePoolWithTag(NonPagedPool, (Count + 16) * sizeof(PDEVICE_OBJECT), 'tpcA');
    if (List != NULL)
    {
        Status = IoEnumerateDeviceObjectList(Driver, List, (Count + 16) * sizeof(PDEVICE_OBJECT), &Count);
        if (NT_SUCCESS(Status))
        {
            for (Index = 0; Index < Count; Index++)
            {
                if (Found == NULL && (List[Index]->Flags & DO_BUS_ENUMERATED_DEVICE) != 0)
                {
                    Target = List[Index];
                    if (Processor)
                    {
                        Method.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
                        Method.MethodNameAsUlong = 'DIH_';
                        RtlZeroMemory(Buffer, sizeof(Buffer));
                        Status = Send(IOCTL_ACPI_EVAL_METHOD, &Method, sizeof(Method), Buffer, sizeof(Buffer), &Information);
                        if (NT_SUCCESS(Status) && Result->Argument[0].Type == ACPI_METHOD_ARGUMENT_STRING &&
                            strcmp((PCSTR)Result->Argument[0].Data, "ACPI0007") == 0)
                        {
                            Found = List[Index];
                            continue;
                        }
                    }
                    else
                    {
                        RtlZeroMemory(InputData, sizeof(InputData));
                        Input->Signature = ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE;
                        Input->Flags = ENUM_CHILDREN_IMMEDIATE_ONLY | ENUM_CHILDREN_NAME_IS_FILTER;
                        Input->NameLength = 5;
                        RtlCopyMemory(Input->Name, "_PRT", 5);
                        Status = Send(IOCTL_ACPI_ENUM_CHILDREN, Input, FIELD_OFFSET(ACPI_ENUM_CHILDREN_INPUT_BUFFER, Name) + 5,
                                      Buffer, sizeof(Buffer), &Information);
                        if (NT_SUCCESS(Status) && ((PACPI_ENUM_CHILDREN_OUTPUT_BUFFER)Buffer)->NumberOfChildren == 1)
                        {
                            Found = List[Index];
                            continue;
                        }
                    }
                }

                ObDereferenceObject(List[Index]);
            }
        }

        ExFreePoolWithTag(List, 'tpcA');
    }

    Target = NULL;
    ObDereferenceObject(Driver);
    return Found;
}

static
NTSTATUS
Enumerate(
    _In_ ULONG Signature,
    _In_ ULONG Flags,
    _In_opt_ PCSTR Name,
    _In_ ULONG InputLength,
    _Out_ PVOID Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    UCHAR Buffer[128];
    PACPI_ENUM_CHILDREN_INPUT_BUFFER Input = (PVOID)Buffer;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Input->Signature = Signature;
    Input->Flags = Flags;
    if (Name != NULL)
    {
        Input->NameLength = (ULONG)strlen(Name) + 1;
        RtlCopyMemory(Input->Name, Name, Input->NameLength);
        InputLength = max(sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER),
                          FIELD_OFFSET(ACPI_ENUM_CHILDREN_INPUT_BUFFER, Name) + Input->NameLength);
    }

    return Send(IOCTL_ACPI_ENUM_CHILDREN, Input, InputLength, Output, OutputLength, Information);
}

static
NTSTATUS
Evaluate(
    _In_ ULONG Method,
    _Out_ PVOID Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    ACPI_EVAL_INPUT_BUFFER Input;

    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.MethodNameAsUlong = Method;
    return Send(IOCTL_ACPI_EVAL_METHOD, &Input, sizeof(Input), Output, OutputLength, Information);
}

START_TEST(IoAcpiIoctl)
{
    UCHAR Buffer[1024], Second[1024];
    PACPI_ENUM_CHILDREN_OUTPUT_BUFFER Children = (PVOID)Buffer;
    PACPI_EVAL_OUTPUT_BUFFER Result = (PVOID)Buffer, Large;
    ACPI_EVAL_INPUT_BUFFER_EX InputEx;
    PACPI_ENUM_CHILD Child;
    ULONG_PTR Information, Full;
    CHAR Self[128], Path[256];
    PWSTR List = NULL;
    ULONG Index, Count, Round;
    NTSTATUS Status;
    size_t Length;

    Status = IoGetDeviceInterfaces(&GUID_DEVICE_PROCESSOR, NULL, 0, &List);
    ok(NT_SUCCESS(Status) && List != NULL && List[0] != UNICODE_NULL, "no processor device interface: status=%lx\n", Status);
    if (NT_SUCCESS(Status) && List != NULL)
        ExFreePool(List);

    Target = FindDevice(TRUE);
    if (skip(Target != NULL, "no processor device\n"))
        return;

    RtlFillMemory(Buffer, sizeof(Buffer), 0xEE);
    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_SUCCESS, "enumerate: status=%lx\n", Status);
    Full = Information;
    Self[0] = ANSI_NULL;
    Count = 0;
    if (NT_SUCCESS(Status))
    {
        ok(Children->Signature == ACPI_ENUM_CHILDREN_OUTPUT_BUFFER_SIGNATURE, "enumerate: signature=%lx\n", Children->Signature);
        ok(Children->NumberOfChildren >= 1, "enumerate: count=%lu\n", Children->NumberOfChildren);
        Count = Children->NumberOfChildren;
        Child = Children->Children;
        Length = FIELD_OFFSET(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER, Children);
        for (Index = 0; Index < Count && Length + FIELD_OFFSET(ACPI_ENUM_CHILD, Name) <= Information; Index++)
        {
            ok(Child->NameLength >= 2 && Child->NameLength < 128, "entry %lu: length=%lu\n", Index, Child->NameLength);
            if (Child->NameLength < 2 || Child->NameLength >= 128)
                break;

            ok(Child->Name[Child->NameLength - 1] == ANSI_NULL, "entry %lu: name is not terminated\n", Index);
            ok(strlen(Child->Name) + 1 == Child->NameLength, "entry %lu: length %lu, text %s\n", Index, Child->NameLength, Child->Name);
            ok(Child->Name[0] == '\\', "entry %lu: name=%s\n", Index, Child->Name);
            ok(strstr(Child->Name, "_.") == NULL, "entry %lu: padded segment in %s\n", Index, Child->Name);
            if (Index == 0)
            {
                ok(Child->Flags == 1, "entry 0: flags=%lx name=%s\n", Child->Flags, Child->Name);
                RtlStringCbCopyA(Self, sizeof(Self), Child->Name);
            }
            else
            {
                ok(Child->Flags == 0 || Child->Flags == 1, "entry %lu: flags=%lx\n", Index, Child->Flags);
                ok(strncmp(Child->Name, Self, strlen(Self)) == 0 && Child->Name[strlen(Self)] == '.',
                   "entry %lu: %s is not below %s\n", Index, Child->Name, Self);
            }

            Length += FIELD_OFFSET(ACPI_ENUM_CHILD, Name) + Child->NameLength;
            Child = ACPI_ENUM_CHILD_NEXT(Child);
        }

        ok(Index == Count, "enumerate: walked %lu of %lu entries\n", Index, Count);
        ok(Length == Information, "enumerate: information=%Iu, entries end at %Iu\n", Information, Length);
    }

    RtlFillMemory(Second, sizeof(Second), 0xEE);
    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       FIELD_OFFSET(ACPI_ENUM_CHILDREN_INPUT_BUFFER, Name), Second, sizeof(Second), &Information);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "short input: status=%lx\n", Status);

    Status = Enumerate(0x12345678, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL, sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_INVALID_PARAMETER_1, "wrong signature: status=%lx\n", Status);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, 0, NULL, sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_ACPI_INVALID_DATA, "no flags: status=%lx information=%Iu\n", Status, Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY | ENUM_CHILDREN_MULTILEVEL, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, sizeof(Second), &Information);
    ok(Status == STATUS_ACPI_INVALID_DATA, "both depths: status=%lx information=%Iu\n", Status, Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY | 0x10, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, sizeof(Second), &Information);
    ok(Status == STATUS_SUCCESS && Information == Full, "unknown flag: status=%lx information=%Iu\n", Status, Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY | ENUM_CHILDREN_NAME_IS_FILTER,
                       "_UID", 0, Second, sizeof(Second), &Information);
    ok(Status == STATUS_SUCCESS, "filter: status=%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Children = (PVOID)Second;
        ok(Children->NumberOfChildren == 1, "filter: count=%lu information=%Iu\n", Children->NumberOfChildren, Information);
        Length = strlen(Children->Children[0].Name);
        ok(Children->Children[0].NameLength == Length + 1 && Length > 4 &&
           strcmp(Children->Children[0].Name + Length - 4, "_UID") == 0,
           "filter: length=%lu name=%.60s\n", Children->Children[0].NameLength, Children->Children[0].Name);
        ok(Children->Children[0].Flags == 0, "filter: flags=%lx\n", Children->Children[0].Flags);
        Children = (PVOID)Buffer;
    }

    if (Self[0] != ANSI_NULL)
    {
        Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, Self, 0, Second,
                           sizeof(Second), &Information);
        ok(Status == STATUS_OBJECT_NAME_INVALID, "own path %s: status=%lx information=%Iu\n", Self, Status, Information);
    }

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_MULTILEVEL, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, sizeof(Second), &Information);
    ok(Status == STATUS_SUCCESS, "multilevel: status=%lx\n", Status);
    ok(Information >= Full, "multilevel: information=%Iu, immediate %Iu\n", Information, Full);
    if (NT_SUCCESS(Status))
    {
        PACPI_ENUM_CHILD Other;
        ULONG Inner, Wrong = 0;
        BOOLEAN Parent;

        Children = (PVOID)Second;
        Child = Children->Children;
        for (Index = 0; Index < Children->NumberOfChildren; Index++)
        {
            Parent = FALSE;
            Other = Children->Children;
            Length = strlen(Child->Name);
            for (Inner = 0; Inner < Children->NumberOfChildren; Inner++)
            {
                if (strncmp(Other->Name, Child->Name, Length) == 0 && Other->Name[Length] == '.')
                    Parent = TRUE;
                Other = ACPI_ENUM_CHILD_NEXT(Other);
            }

            if (((Child->Flags & ACPI_OBJECT_HAS_CHILDREN) != 0) != Parent || (Child->Flags & ~ACPI_OBJECT_HAS_CHILDREN) != 0)
                Wrong++;
            Child = ACPI_ENUM_CHILD_NEXT(Child);
        }

        ok(Wrong == 0, "multilevel: %lu of %lu entries have a wrong children flag\n", Wrong, Children->NumberOfChildren);
        Children = (PVOID)Buffer;
    }

    RtlFillMemory(Second, sizeof(Second), 0xEE);
    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, sizeof(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER), &Information);
    ok(Status == STATUS_BUFFER_OVERFLOW, "small output: status=%lx\n", Status);
    ok(Information == sizeof(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER), "small output: information=%Iu\n", Information);
    ok(((PACPI_ENUM_CHILDREN_OUTPUT_BUFFER)Second)->Signature == ACPI_ENUM_CHILDREN_OUTPUT_BUFFER_SIGNATURE,
       "small output: signature=%lx\n", ((PACPI_ENUM_CHILDREN_OUTPUT_BUFFER)Second)->Signature);
    ok(((PACPI_ENUM_CHILDREN_OUTPUT_BUFFER)Second)->NumberOfChildren == Full, "small output: required=%lu, full size %Iu\n",
       ((PACPI_ENUM_CHILDREN_OUTPUT_BUFFER)Second)->NumberOfChildren, Full);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, (ULONG)Full, &Information);
    ok(Status == STATUS_SUCCESS && Information == Full, "exact output: status=%lx information=%Iu\n", Status, Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, (ULONG)Full - 1, &Information);
    ok(Status == STATUS_BUFFER_OVERFLOW, "one byte short: status=%lx\n", Status);
    ok(Information == sizeof(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER), "one byte short: information=%Iu\n", Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, sizeof(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER) - 1, &Information);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "output below the fixed part: status=%lx information=%Iu\n", Status, Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, FIELD_OFFSET(ACPI_ENUM_CHILDREN_OUTPUT_BUFFER, Children),
                       &Information);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "output of the two counters: status=%lx information=%Iu\n", Status, Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), NULL, 0, &Information);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "no output: status=%lx information=%Iu\n", Status, Information);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                       sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, 4, &Information);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "tiny output: status=%lx\n", Status);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, "XXXX", 0, Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "missing child: status=%lx\n", Status);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, "_UID", 0, Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_SUCCESS, "existing child: status=%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Children = (PVOID)Second;
        Length = strlen(Children->Children[0].Name);
        ok(Children->NumberOfChildren == 1 && Children->Children[0].Flags == 0 && Length > 4 &&
           strcmp(Children->Children[0].Name + Length - 4, "_UID") == 0,
           "existing child: count=%lu flags=%lx name=%.60s information=%Iu\n", Children->NumberOfChildren,
           Children->Children[0].Flags, Children->Children[0].Name, Information);
        Children = (PVOID)Buffer;
    }

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, "AB.CD", 0, Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_OBJECT_NAME_INVALID, "dotted name: status=%lx\n", Status);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, "ABCDE", 0, Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_OBJECT_NAME_INVALID, "five character name: status=%lx\n", Status);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, "AB", 0, Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "two character name: status=%lx\n", Status);

    Status = Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, "\\_SB", 0, Second,
                       sizeof(Second), &Information);
    ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "root path: status=%lx\n", Status);

    RtlFillMemory(Buffer, sizeof(Buffer), 0xEE);
    Status = Evaluate('DIU_', Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_SUCCESS, "_UID: status=%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(Result->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE, "_UID: signature=%lx\n", Result->Signature);
        ok(Result->Count == 1, "_UID: count=%lu\n", Result->Count);
        ok(Result->Length == Information, "_UID: length=%lu information=%Iu\n", Result->Length, Information);
        ok(Result->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER, "_UID: type=%u\n", Result->Argument[0].Type);
        ok(Result->Argument[0].DataLength == sizeof(ULONG), "_UID: data length=%u\n", Result->Argument[0].DataLength);
        ok(Information == 20, "_UID: information=%Iu\n", Information);
    }

    RtlFillMemory(Buffer, sizeof(Buffer), 0xEE);
    Status = Evaluate('DIH_', Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_SUCCESS, "_HID: status=%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(Result->Count == 1, "_HID: count=%lu\n", Result->Count);
        ok(Result->Argument[0].Type == ACPI_METHOD_ARGUMENT_STRING, "_HID: type=%u\n", Result->Argument[0].Type);
        ok(Result->Argument[0].DataLength == strlen((PCSTR)Result->Argument[0].Data) + 1, "_HID: data length=%u text=%s\n",
           Result->Argument[0].DataLength, (PCSTR)Result->Argument[0].Data);
        ok(Result->Length == Information, "_HID: length=%lu information=%Iu\n", Result->Length, Information);
        ok(Information == FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) +
                          Result->Argument[0].DataLength,
           "_HID: information=%Iu data length=%u\n", Information, Result->Argument[0].DataLength);
    }

    Status = Evaluate('ZYX_', Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "missing method: status=%lx\n", Status);

    RtlFillMemory(Buffer, sizeof(Buffer), 0xEE);
    Status = Evaluate('DIH_', Buffer, sizeof(ACPI_EVAL_OUTPUT_BUFFER), &Information);
    ok(Status == STATUS_BUFFER_OVERFLOW, "_HID small output: status=%lx\n", Status);
    ok(Information == sizeof(ACPI_EVAL_OUTPUT_BUFFER), "_HID small output: information=%Iu\n", Information);
    if (Status == STATUS_BUFFER_OVERFLOW)
        ok(Result->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE && Result->Length > sizeof(ACPI_EVAL_OUTPUT_BUFFER),
           "_HID small output: signature=%lx length=%lu\n", Result->Signature, Result->Length);

    Status = Evaluate('DIH_', Buffer, 4, &Information);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "_HID tiny output: status=%lx\n", Status);

    Status = Evaluate('DIH_', Buffer, sizeof(ACPI_EVAL_OUTPUT_BUFFER) - 1, &Information);
    ok(Status == STATUS_BUFFER_TOO_SMALL, "_HID output below the fixed part: status=%lx information=%Iu\n", Status, Information);

    Status = Evaluate('DIH_', Buffer, sizeof(ACPI_EVAL_OUTPUT_BUFFER) + 4, &Information);
    ok(Status == STATUS_BUFFER_OVERFLOW && Information == sizeof(ACPI_EVAL_OUTPUT_BUFFER),
       "_HID four bytes short: status=%lx information=%Iu\n", Status, Information);

    Status = Evaluate('DIU_', Buffer, sizeof(ACPI_EVAL_OUTPUT_BUFFER), &Information);
    ok(Status == STATUS_SUCCESS && Information == sizeof(ACPI_EVAL_OUTPUT_BUFFER), "_UID exact output: status=%lx information=%Iu\n",
       Status, Information);

    RtlZeroMemory(&InputEx, sizeof(InputEx));
    InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
    RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), "_UID");
    Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_SUCCESS, "extended _UID: status=%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        ok(Result->Signature == ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE && Result->Length == Information && Result->Count == 1,
           "extended _UID: signature=%lx length=%lu count=%lu information=%Iu\n", Result->Signature, Result->Length,
           Result->Count, Information);
        ok(Result->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER && Result->Argument[0].DataLength == sizeof(ULONGLONG) &&
           Information == 24,
           "extended _UID: type=%u data length=%u information=%Iu raw=%08lx %08lx %08lx\n", Result->Argument[0].Type,
           Result->Argument[0].DataLength, Information, ((PULONG)Buffer)[3], ((PULONG)Buffer)[4], ((PULONG)Buffer)[5]);
    }

    if (Self[0] != ANSI_NULL)
    {
        RtlStringCbPrintfA(Path, sizeof(Path), "%s._UID", Self);
        RtlZeroMemory(&InputEx, sizeof(InputEx));
        InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
        RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), Path);
        Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
        ok(Status == STATUS_SUCCESS && Information == 24, "extended full path %s: status=%lx information=%Iu\n", Path, Status,
           Information);
    }

    RtlZeroMemory(&InputEx, sizeof(InputEx));
    InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
    RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), "XXXX._UID");
    Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "extended missing path: status=%lx\n", Status);

    RtlZeroMemory(&InputEx, sizeof(InputEx));
    InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
    RtlFillMemory(InputEx.MethodName, sizeof(InputEx.MethodName), 'A');
    Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_OBJECT_NAME_INVALID, "extended unterminated name: status=%lx\n", Status);

    RtlZeroMemory(&InputEx, sizeof(InputEx));
    InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
    Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_NO_SUCH_DEVICE, "extended empty name: status=%lx information=%Iu\n", Status, Information);

    RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), "_HID");
    Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_SUCCESS && Information == Result->Length && Result->Argument[0].Type == ACPI_METHOD_ARGUMENT_STRING,
       "extended _HID: status=%lx information=%Iu type=%u length=%u\n", Status, Information, Result->Argument[0].Type,
       Result->Argument[0].DataLength);

    Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx) - 1, Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "extended short input: status=%lx\n", Status);

    RtlZeroMemory(&InputEx, sizeof(InputEx));
    InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), "_UID");
    Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_INVALID_PARAMETER_1, "extended with the short signature: status=%lx\n", Status);

    InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
    Status = Send(IOCTL_ACPI_EVAL_METHOD, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_INVALID_PARAMETER_1, "short with the extended signature: status=%lx\n", Status);

    Status = Send(IOCTL_ACPI_EVAL_METHOD, &InputEx, sizeof(ULONG), Buffer, sizeof(Buffer), &Information);
    ok(Status == STATUS_INFO_LENGTH_MISMATCH, "four byte input: status=%lx\n", Status);

    Status = Evaluate('DIU_', NULL, 0, &Information);
    ok(Status == STATUS_SUCCESS && Information == 0, "_UID without output: status=%lx information=%Iu\n", Status, Information);

    if (Self[0] != ANSI_NULL)
    {
        RtlZeroMemory(&InputEx, sizeof(InputEx));
        InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
        RtlStringCbPrintfA(InputEx.MethodName, sizeof(InputEx.MethodName), "%s.XXXX", Self);
        Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
        ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "extended missing full path: status=%lx\n", Status);

        RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), "\\_SB.XXXX._UID");
        Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
        ok(Status == STATUS_OBJECT_NAME_NOT_FOUND, "extended path of a missing device: status=%lx\n", Status);

        RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), "\\_SB");
        Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Buffer, sizeof(Buffer), &Information);
        ok(Status == STATUS_OBJECT_PATH_INVALID, "extended path of the parent: status=%lx\n", Status);
    }

    for (Round = 0; Round < 500; Round++)
    {
        if (!NT_SUCCESS(Enumerate(ACPI_ENUM_CHILDREN_INPUT_BUFFER_SIGNATURE, ENUM_CHILDREN_IMMEDIATE_ONLY, NULL,
                                  sizeof(ACPI_ENUM_CHILDREN_INPUT_BUFFER), Second, sizeof(Second), &Information)) ||
            Information != Full ||
            !NT_SUCCESS(Evaluate('DIU_', Buffer, sizeof(Buffer), &Information)) ||
            Information != 20)
        {
            break;
        }
    }
    ok(Round == 500, "torture stopped at round %lu\n", Round);

    ObDereferenceObject(Target);

    Target = FindDevice(FALSE);
    Large = ExAllocatePoolWithTag(NonPagedPool, 0x10000, 'tpcA');
    if (!skip(Target != NULL && Large != NULL, "no device with an interrupt routing package\n"))
    {
        RtlZeroMemory(&InputEx, sizeof(InputEx));
        InputEx.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
        RtlStringCbCopyA(InputEx.MethodName, sizeof(InputEx.MethodName), "_PRT");
        Status = Send(IOCTL_ACPI_EVAL_METHOD_EX, &InputEx, sizeof(InputEx), Large, 0x10000, &Information);
        ok(Status == STATUS_SUCCESS, "routing: status=%lx\n", Status);
        if (NT_SUCCESS(Status))
        {
            PACPI_METHOD_ARGUMENT Entry = Large->Argument;
            PACPI_METHOD_ARGUMENT Field = (PACPI_METHOD_ARGUMENT)Entry->Data;

            ok(Large->Count >= 1 && Large->Length == Information, "routing: count=%lu length=%lu information=%Iu\n",
               Large->Count, Large->Length, Information);
            ok(Entry->Type == ACPI_METHOD_ARGUMENT_PACKAGE, "routing entry: type=%u length=%u\n", Entry->Type, Entry->DataLength);
            ok(Field->Type == ACPI_METHOD_ARGUMENT_INTEGER && Field->DataLength == sizeof(ULONGLONG),
               "routing field: type=%u length=%u\n", Field->Type, Field->DataLength);
            Field = ACPI_METHOD_NEXT_ARGUMENT(Field);
            ok(Field->Type == ACPI_METHOD_ARGUMENT_INTEGER && Field->DataLength == sizeof(ULONGLONG),
               "routing second field: type=%u length=%u\n", Field->Type, Field->DataLength);
        }

        RtlFillMemory(Large, 0x10000, 0xEE);
        Status = Evaluate('TRP_', Large, 0x10000, &Information);
        ok(Status == STATUS_SUCCESS, "short routing: status=%lx\n", Status);
        if (NT_SUCCESS(Status))
        {
            PACPI_METHOD_ARGUMENT Entry = Large->Argument;
            PACPI_METHOD_ARGUMENT Field = (PACPI_METHOD_ARGUMENT)Entry->Data;

            ok(Entry->Type == ACPI_METHOD_ARGUMENT_PACKAGE, "short routing entry: type=%u length=%u\n", Entry->Type,
               Entry->DataLength);
            ok(Field->Type == ACPI_METHOD_ARGUMENT_INTEGER && Field->DataLength == sizeof(ULONG),
               "short routing field: type=%u length=%u\n", Field->Type, Field->DataLength);
        }
    }

    if (Large != NULL)
        ExFreePoolWithTag(Large, 'tpcA');
    if (Target != NULL)
        ObDereferenceObject(Target);
}
