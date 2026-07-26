/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

/* Reads the $UpCase table into the Volume cache. The table is immutable,
 * so this happens once; every later UpcaseWideString() call reuses it.
 */
NTSTATUS
Volume::LoadUpcaseTable()
{
    NTSTATUS Status;
    PFileRecord UpCaseFile;
    PAttribute UpCaseData;
    ULONG UpCaseDataLength;

    // Get $UpCase file
    Status = MFT->GetFileAttributeFromFileRecordNumber(TypeData,
                                                       NULL,
                                                       _UpCase,
                                                       &UpCaseFile,
                                                       &UpCaseData);
    if (!NT_SUCCESS(Status))
        return Status;

    // Get $DATA attribute contents from $UpCase
    Status = UpCaseFile->ReadAttributeAlloc(UpCaseData,
                                            (PUCHAR*)&UpcaseTable,
                                            &UpCaseDataLength);

    if (NT_SUCCESS(Status) &&
        (UpCaseDataLength % sizeof(WCHAR)) == 0)
    {
        UpcaseTableLength = UpCaseDataLength / sizeof(WCHAR);
    }
    else
    {
        delete[] UpcaseTable;
        UpcaseTable = NULL;
        if (NT_SUCCESS(Status))
            Status = STATUS_FILE_CORRUPT_ERROR;
    }

    delete UpCaseFile;
    return Status;
}

NTSTATUS
Volume::UpcaseWideString(_Inout_ PWSTR WideString,
                         _In_    ULONG Length)
{
    NTSTATUS Status;

    if (!UpcaseTable)
    {
        Status = LoadUpcaseTable();
        if (!NT_SUCCESS(Status))
            return Status;
    }

    // Update each character according to its entry in the $UpCase table
    for (ULONG i = 0; i < Length; i++)
    {
        if (WideString[i] < UpcaseTableLength)
            WideString[i] = UpcaseTable[WideString[i]];
    }

    return STATUS_SUCCESS;
}

NTSTATUS
Volume::CompareFileNames(_In_  PUNICODE_STRING Left,
                         _In_  PUNICODE_STRING Right,
                         _Out_ LONG* Result)
{
    NTSTATUS Status;
    ULONG LeftLength;
    ULONG RightLength;
    ULONG CommonLength;

    if (!Left || !Right || !Result ||
        (Left->Length != 0 && !Left->Buffer) ||
        (Right->Length != 0 && !Right->Buffer) ||
        (Left->Length & (sizeof(WCHAR) - 1)) != 0 ||
        (Right->Length & (sizeof(WCHAR) - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!UpcaseTable)
    {
        Status = LoadUpcaseTable();
        if (!NT_SUCCESS(Status))
            return Status;
    }

    LeftLength = Left->Length / sizeof(WCHAR);
    RightLength = Right->Length / sizeof(WCHAR);
    CommonLength = min(LeftLength, RightLength);

    for (ULONG Index = 0; Index < CommonLength; Index++)
    {
        WCHAR LeftCharacter = Left->Buffer[Index];
        WCHAR RightCharacter = Right->Buffer[Index];

        if (LeftCharacter < UpcaseTableLength)
            LeftCharacter = UpcaseTable[LeftCharacter];
        if (RightCharacter < UpcaseTableLength)
            RightCharacter = UpcaseTable[RightCharacter];

        if (LeftCharacter != RightCharacter)
        {
            *Result = LeftCharacter < RightCharacter ? -1 : 1;
            return STATUS_SUCCESS;
        }
    }

    *Result = LeftLength == RightLength
        ? 0
        : (LeftLength < RightLength ? -1 : 1);
    return STATUS_SUCCESS;
}
