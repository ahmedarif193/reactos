/*
 * PROJECT:     ReactOS Win32 Base API
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Get/SetProcessInformation
 * COPYRIGHT:   Copyright 1996, 1998 Alexandre Julliard
 */

#include "k32_vista.h"

#define NDEBUG
#include <debug.h>

BOOL
WINAPI
GetProcessInformation(
    _In_ HANDLE hProcess,
    _In_ PROCESS_INFORMATION_CLASS ProcessInformationClass,
    _Out_writes_bytes_(ProcessInformationSize) PVOID ProcessInformation,
    _In_ DWORD ProcessInformationSize)
{
    NTSTATUS Status;

    switch (ProcessInformationClass)
    {
        case ProcessMemoryPriority:
            Status = NtQueryInformationProcess(hProcess, ProcessPagePriority, ProcessInformation, ProcessInformationSize, NULL);
            break;

        case ProcessMachineTypeInfo:
        {
            PROCESS_MACHINE_INFORMATION *MachineInformation = ProcessInformation;
            SECTION_IMAGE_INFORMATION ImageInformation;
            USHORT Machine;

            if (ProcessInformationSize != sizeof(*MachineInformation))
            {
                SetLastError(ERROR_BAD_LENGTH);
                return FALSE;
            }

            Status = NtQueryInformationProcess(hProcess, ProcessImageInformation, &ImageInformation, sizeof(ImageInformation), NULL);
            if (!NT_SUCCESS(Status))
                break;

            Machine = ImageInformation.Machine;
            if (Machine == IMAGE_FILE_MACHINE_ARM64EC)
                Machine = IMAGE_FILE_MACHINE_AMD64;

            MachineInformation->ProcessMachine = Machine;
            MachineInformation->Res0 = 0;
            MachineInformation->MachineAttributes = UserEnabled;
            if (Machine == IMAGE_FILE_MACHINE_NATIVE)
                MachineInformation->MachineAttributes |= KernelEnabled;
            else if (Machine == IMAGE_FILE_MACHINE_I386)
                MachineInformation->MachineAttributes |= Wow64Container;
            break;
        }

        default:
            DPRINT1("GetProcessInformation: unsupported class %d\n", ProcessInformationClass);
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
SetProcessInformation(
    _In_ HANDLE hProcess,
    _In_ PROCESS_INFORMATION_CLASS ProcessInformationClass,
    _In_reads_bytes_(ProcessInformationSize) PVOID ProcessInformation,
    _In_ DWORD ProcessInformationSize)
{
    NTSTATUS Status;

    switch (ProcessInformationClass)
    {
        case ProcessMemoryPriority:
            Status = NtSetInformationProcess(hProcess, ProcessPagePriority, ProcessInformation, ProcessInformationSize);
            break;

        default:
            DPRINT1("SetProcessInformation: unsupported class %d\n", ProcessInformationClass);
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
    }

    if (!NT_SUCCESS(Status))
    {
        BaseSetLastNTError(Status);
        return FALSE;
    }
    return TRUE;
}
