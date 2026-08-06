/*
 * PROJECT:         ReactOS Windows-Compatible Session Manager
 * LICENSE:         BSD 2-Clause License
 * FILE:            base/system/smss/crashdmp.c
 * PURPOSE:         Main SMSS Code
 * PROGRAMMERS:     Alex Ionescu
 */

/* INCLUDES *******************************************************************/

#include "smss.h"
#include <reactos/dump.h>

#define NDEBUG
#include <debug.h>

#define SMP_DUMP_COPY_SIZE (1024 * 1024)

/* FUNCTIONS ******************************************************************/

static BOOLEAN SmpIsDedicatedCrashDumpActive(VOID)
{
    static const UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\CrashControl");
    static const UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"DedicatedDumpActive");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    ULONG Length;
    NTSTATUS Status;
    struct
    {
        KEY_VALUE_PARTIAL_INFORMATION Information;
        ULONG Value;
    } ValueBuffer;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return FALSE;

    Status = NtQueryValueKey(KeyHandle, (PUNICODE_STRING)&ValueName, KeyValuePartialInformation, &ValueBuffer, sizeof(ValueBuffer), &Length);
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status) || (ValueBuffer.Information.Type != REG_DWORD) || (ValueBuffer.Information.DataLength != sizeof(ULONG)))
        return FALSE;

    return *(PULONG)ValueBuffer.Information.Data != 0;
}

static NTSTATUS SmpQueryDumpFilePath(_Out_ PUNICODE_STRING NtPath)
{
    static const UNICODE_STRING KeyName = RTL_CONSTANT_STRING(L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\CrashControl");
    static const UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"DumpFile");
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Source;
    UNICODE_STRING Expanded;
    HANDLE KeyHandle;
    ULONG Length;
    NTSTATUS Status;
    WCHAR ExpandedBuffer[MAX_PATH];
    struct
    {
        KEY_VALUE_PARTIAL_INFORMATION Information;
        WCHAR Buffer[MAX_PATH];
    } ValueBuffer;

    InitializeObjectAttributes(&ObjectAttributes, (PUNICODE_STRING)&KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&KeyHandle, KEY_QUERY_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtQueryValueKey(KeyHandle, (PUNICODE_STRING)&ValueName, KeyValuePartialInformation, &ValueBuffer, sizeof(ValueBuffer), &Length);
    NtClose(KeyHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((ValueBuffer.Information.Type != REG_SZ) && (ValueBuffer.Information.Type != REG_EXPAND_SZ))
    {
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    if ((ValueBuffer.Information.DataLength == 0) || (ValueBuffer.Information.DataLength > sizeof(ValueBuffer.Buffer)))
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    Source.Buffer = (PWCHAR)ValueBuffer.Information.Data;
    Source.Length = (USHORT)ValueBuffer.Information.DataLength;
    if (Source.Buffer[(Source.Length / sizeof(WCHAR)) - 1] == UNICODE_NULL)
        Source.Length -= sizeof(WCHAR);
    Source.MaximumLength = Source.Length;

    RtlInitEmptyUnicodeString(&Expanded, ExpandedBuffer, sizeof(ExpandedBuffer));
    Status = RtlExpandEnvironmentStrings_U(SmpDefaultEnvironment, &Source, &Expanded, &Length);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!RtlDosPathNameToNtPathName_U(Expanded.Buffer, NtPath, NULL, NULL))
        return STATUS_OBJECT_PATH_INVALID;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
SmpCheckForCrashDump(IN PUNICODE_STRING FileName)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    FILE_STANDARD_INFORMATION StandardInfo;
    IO_STATUS_BLOCK IoStatus;
    PDUMP_HEADER64 Header = NULL;
    PVOID Buffer = NULL;
    PVOID HeaderAllocation = NULL;
    PVOID BufferAllocation = NULL;
    UNICODE_STRING DumpPath = {0};
    LARGE_INTEGER Offset;
    LARGE_INTEGER Remaining;
    HANDLE PageFileHandle = NULL;
    HANDLE DumpFileHandle = NULL;
    ULONG Length;
    NTSTATUS Status;
    BOOLEAN DumpSaved = FALSE;

    if (SmpIsDedicatedCrashDumpActive())
        return FALSE;

    DPRINT1("SMSS: Inspecting `%wZ' for a crash dump\n", FileName);

    InitializeObjectAttributes(&ObjectAttributes, FileName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&PageFileHandle, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &ObjectAttributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SMSS: Failed to open crash dump pagefile `%wZ' (0x%08lx)\n", FileName, Status);
        return FALSE;
    }

    HeaderAllocation = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, DUMP_HEADER64_SIZE + PAGE_SIZE - 1);
    if (!HeaderAllocation)
    {
        DPRINT1("SMSS: Failed to allocate the crash dump header buffer\n");
        goto Cleanup;
    }
    Header = (PDUMP_HEADER64)ALIGN_UP_BY(HeaderAllocation, PAGE_SIZE);

    Offset.QuadPart = 0;
    Status = NtReadFile(PageFileHandle, NULL, NULL, NULL, &IoStatus, Header, DUMP_HEADER64_SIZE, &Offset, NULL);
    if (!NT_SUCCESS(Status) || (IoStatus.Information != DUMP_HEADER64_SIZE))
    {
        DPRINT1("SMSS: Failed to read crash dump header from `%wZ' (0x%08lx, %Iu bytes)\n", FileName, Status, IoStatus.Information);
        goto Cleanup;
    }

    if ((Header->Signature != DUMP_SIGNATURE64) ||
        (Header->ValidDump != DUMP_VALID_DUMP64) ||
        ((Header->DumpType != DUMP_TYPE_FULL) &&
         (Header->DumpType != DUMP_TYPE_BITMAP_FULL) &&
         (Header->DumpType != DUMP_TYPE_BITMAP_KERNEL)) ||
        (Header->RequiredDumpSpace.QuadPart < DUMP_HEADER64_SIZE))
    {
        DPRINT1("SMSS: No valid crash dump in `%wZ' (%08lx/%08lx, type %lu, size %I64d)\n", FileName, Header->Signature, Header->ValidDump, Header->DumpType, Header->RequiredDumpSpace.QuadPart);
        goto Cleanup;
    }

    DPRINT1("SMSS: Found a valid crash dump in `%wZ'\n", FileName);

    Status = NtQueryInformationFile(PageFileHandle, &IoStatus, &StandardInfo, sizeof(StandardInfo), FileStandardInformation);
    if (!NT_SUCCESS(Status) || (Header->RequiredDumpSpace.QuadPart > StandardInfo.EndOfFile.QuadPart))
    {
        goto Cleanup;
    }

    Status = SmpQueryDumpFilePath(&DumpPath);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SMSS: Failed to query the crash dump path (0x%08lx)\n", Status);
        goto Cleanup;
    }

    InitializeObjectAttributes(&ObjectAttributes, &DumpPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateFile(&DumpFileHandle, FILE_WRITE_DATA | DELETE | SYNCHRONIZE, &ObjectAttributes, &IoStatus, &Header->RequiredDumpSpace, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, FILE_OVERWRITE_IF, FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING, NULL, 0);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("SMSS: Failed to create crash dump `%wZ' (0x%08lx)\n", &DumpPath, Status);
        goto Cleanup;
    }

    BufferAllocation = RtlAllocateHeap(RtlGetProcessHeap(), 0, SMP_DUMP_COPY_SIZE + PAGE_SIZE - 1);
    if (!BufferAllocation)
    {
        Status = STATUS_NO_MEMORY;
        goto Cleanup;
    }
    Buffer = (PVOID)ALIGN_UP_BY(BufferAllocation, PAGE_SIZE);

    Offset.QuadPart = 0;
    Remaining = Header->RequiredDumpSpace;
    while (Remaining.QuadPart)
    {
        Length = (ULONG)min(Remaining.QuadPart, SMP_DUMP_COPY_SIZE);
        Status = NtReadFile(PageFileHandle, NULL, NULL, NULL, &IoStatus, Buffer, Length, &Offset, NULL);
        if (!NT_SUCCESS(Status) || (IoStatus.Information != Length))
            goto Cleanup;

        Status = NtWriteFile(DumpFileHandle, NULL, NULL, NULL, &IoStatus, Buffer, Length, &Offset, NULL);
        if (!NT_SUCCESS(Status) || (IoStatus.Information != Length))
            goto Cleanup;

        Offset.QuadPart += Length;
        Remaining.QuadPart -= Length;
    }

    Status = NtFlushBuffersFile(DumpFileHandle, &IoStatus);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /*
     * The destination is complete at this point. Invalidating the pagefile
     * header only prevents another copy on the next boot.
     */
    DumpSaved = TRUE;

    Header->ValidDump = 0;
    Offset.QuadPart = 0;
    Status = NtWriteFile(PageFileHandle, NULL, NULL, NULL, &IoStatus, Header, DUMP_HEADER64_SIZE, &Offset, NULL);
    if (!NT_SUCCESS(Status) || (IoStatus.Information != DUMP_HEADER64_SIZE))
        goto Cleanup;

    Status = NtFlushBuffersFile(PageFileHandle, &IoStatus);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    DPRINT1("SMSS: Saved crash dump from `%wZ' to `%wZ'\n", FileName, &DumpPath);

Cleanup:
    if (BufferAllocation)
        RtlFreeHeap(RtlGetProcessHeap(), 0, BufferAllocation);
    if (DumpFileHandle)
    {
        if (!DumpSaved)
        {
            FILE_DISPOSITION_INFORMATION Disposition;

            Disposition.DeleteFile = TRUE;
            NtSetInformationFile(DumpFileHandle, &IoStatus, &Disposition, sizeof(Disposition), FileDispositionInformation);
        }
        NtClose(DumpFileHandle);
    }
    if (DumpPath.Buffer)
        RtlFreeUnicodeString(&DumpPath);
    if (HeaderAllocation)
        RtlFreeHeap(RtlGetProcessHeap(), 0, HeaderAllocation);
    if (PageFileHandle)
        NtClose(PageFileHandle);

    return DumpSaved;
}
