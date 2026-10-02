/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Small library with probing utilities for thread/process classes information
 * COPYRIGHT:   Copyright 2020 George Bișoc <george.bisoc@reactos.org>
 */

#include "precomp.h"
#include <internal/ps_i.h>

typedef struct _WINDOWS11_STATUS
{
    ULONG InfoClass;
    NTSTATUS ProbeStatus;
    NTSTATUS Status;
} WINDOWS11_STATUS;

static const WINDOWS11_STATUS Windows11QueryProcessStatus[] =
{
    { 18, STATUS_DATATYPE_MISALIGNMENT, STATUS_ACCESS_VIOLATION },
    { 47, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_PARAMETER },
    { 47, STATUS_ACCESS_VIOLATION, STATUS_INVALID_PARAMETER },
    { 76, STATUS_INFO_LENGTH_MISMATCH, STATUS_ACCESS_VIOLATION },
    { 83, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 83, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
};

static const WINDOWS11_STATUS Windows11SetProcessStatus[] =
{
    { 47, STATUS_INFO_LENGTH_MISMATCH, STATUS_DATATYPE_MISALIGNMENT },
    { 47, STATUS_ACCESS_VIOLATION, STATUS_DATATYPE_MISALIGNMENT },
    { 58, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 58, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 64, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 64, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 75, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 75, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 76, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 76, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 83, STATUS_INFO_LENGTH_MISMATCH, STATUS_DATATYPE_MISALIGNMENT },
    { 83, STATUS_ACCESS_VIOLATION, STATUS_DATATYPE_MISALIGNMENT },
};

static const WINDOWS11_STATUS Windows11QueryThreadStatus[] =
{
    { 6, STATUS_DATATYPE_MISALIGNMENT, STATUS_NOT_IMPLEMENTED },
    { 6, STATUS_INFO_LENGTH_MISMATCH, STATUS_NOT_IMPLEMENTED },
    { 6, STATUS_ACCESS_VIOLATION, STATUS_NOT_IMPLEMENTED },
    { 17, STATUS_DATATYPE_MISALIGNMENT, STATUS_ACCESS_VIOLATION },
    { 21, STATUS_DATATYPE_MISALIGNMENT, STATUS_INFO_LENGTH_MISMATCH },
    { 21, STATUS_ACCESS_VIOLATION, STATUS_INFO_LENGTH_MISMATCH },
    { 26, STATUS_DATATYPE_MISALIGNMENT, STATUS_INFO_LENGTH_MISMATCH },
    { 26, STATUS_ACCESS_VIOLATION, STATUS_INFO_LENGTH_MISMATCH },
    { 28, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 28, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 29, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_PARAMETER },
    { 29, STATUS_ACCESS_VIOLATION, STATUS_INVALID_PARAMETER },
    { 31, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 31, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 32, STATUS_DATATYPE_MISALIGNMENT, STATUS_ACCESS_VIOLATION },
    { 34, STATUS_DATATYPE_MISALIGNMENT, STATUS_ACCESS_VIOLATION },
    { 46, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 46, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 47, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 47, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 48, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 48, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 49, STATUS_INFO_LENGTH_MISMATCH, STATUS_ACCESS_VIOLATION },
    { 50, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 50, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 51, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 51, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 52, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 52, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 53, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 53, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
};

static const WINDOWS11_STATUS Windows11SetThreadStatus[] =
{
    { 9, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_PARAMETER },
    { 9, STATUS_ACCESS_VIOLATION, STATUS_INVALID_PARAMETER },
    { 15, STATUS_INFO_LENGTH_MISMATCH, STATUS_NOT_IMPLEMENTED },
    { 15, STATUS_ACCESS_VIOLATION, STATUS_NOT_IMPLEMENTED },
    { 28, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 28, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 29, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_PARAMETER },
    { 29, STATUS_ACCESS_VIOLATION, STATUS_INVALID_PARAMETER },
    { 31, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 31, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 32, STATUS_DATATYPE_MISALIGNMENT, STATUS_INFO_LENGTH_MISMATCH },
    { 32, STATUS_ACCESS_VIOLATION, STATUS_INFO_LENGTH_MISMATCH },
    { 35, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 35, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 36, STATUS_INFO_LENGTH_MISMATCH, STATUS_NOT_SUPPORTED },
    { 36, STATUS_ACCESS_VIOLATION, STATUS_NOT_SUPPORTED },
    { 37, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 37, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 40, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 40, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 41, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 41, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 45, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 45, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 50, STATUS_INFO_LENGTH_MISMATCH, STATUS_ACCESS_DENIED },
    { 50, STATUS_ACCESS_VIOLATION, STATUS_ACCESS_DENIED },
    { 51, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 51, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 52, STATUS_DATATYPE_MISALIGNMENT, STATUS_INVALID_INFO_CLASS },
    { 52, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 53, STATUS_DATATYPE_MISALIGNMENT, STATUS_INFO_LENGTH_MISMATCH },
    { 53, STATUS_ACCESS_VIOLATION, STATUS_INFO_LENGTH_MISMATCH },
    { 54, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 54, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
    { 55, STATUS_INFO_LENGTH_MISMATCH, STATUS_INVALID_INFO_CLASS },
    { 55, STATUS_ACCESS_VIOLATION, STATUS_INVALID_INFO_CLASS },
};

static
BOOLEAN
IsWindows11Status(
    _In_reads_(Count) const WINDOWS11_STATUS *Table,
    _In_ ULONG Count,
    _In_ ULONG InfoClass,
    _In_ NTSTATUS ProbeStatus,
    _In_ NTSTATUS Status)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (Table[Index].InfoClass == InfoClass && Table[Index].ProbeStatus == ProbeStatus && Table[Index].Status == Status)
            return TRUE;
    }
    return FALSE;
}

VOID
QuerySetProcessValidator(
    _In_ ALIGNMENT_PROBE_MODE ValidationMode,
    _In_ ULONG InfoClassIndex,
    _In_ PVOID InfoPointer,
    _In_ ULONG InfoLength,
    _In_ NTSTATUS ExpectedStatus)
{
    NTSTATUS Status, SpecialStatus = STATUS_SUCCESS;

    if ((PsProcessInfoClass[InfoClassIndex].RequiredSizeQUERY == 0) &&
        (PsProcessInfoClass[InfoClassIndex].RequiredSizeSET == 0))
    {
        skip("FIXME: Skipping test for InfoClass %lu, because PsProcessInfoClass[] doesn't have it.\n", InfoClassIndex);
        return;
    }

    /* Before doing anything, check if we want query or set validation */
    switch (ValidationMode)
    {
        case QUERY:
        {
            switch (InfoClassIndex)
            {
                case ProcessWorkingSetWatch:
                {
                    SpecialStatus = STATUS_UNSUCCESSFUL;
                    break;
                }

                case ProcessHandleTracing:
                {
                    SpecialStatus = STATUS_INVALID_PARAMETER;
                    break;
                }

                /*
                 * This class returns an arbitrary size pointed by InformationLength
                 * which equates to the image filename of the process. Such status
                 * is returned in an invalid address query (STATUS_ACCESS_VIOLATION)
                 * where the function expects STATUS_INFO_LENGTH_MISMATCH instead.
                 */
                case ProcessImageFileName:
                {
                    SpecialStatus = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                /* This one works differently from the others */
                case ProcessUserModeIOPL:
                {
                    if (ExpectedStatus == STATUS_INFO_LENGTH_MISMATCH)
                        SpecialStatus = STATUS_ACCESS_VIOLATION;
                    else
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* These classes don't belong in the query group */
                case ProcessBasePriority:
                case ProcessRaisePriority:
                case ProcessExceptionPort:
                case ProcessAccessToken:
                case ProcessLdtSize:
                case ProcessIoPortHandlers:
                case ProcessEnableAlignmentFaultFixup:
                case ProcessAffinityMask:
                {
                    SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                case ProcessForegroundInformation:
                {
                    if (ExpectedStatus != STATUS_DATATYPE_MISALIGNMENT)
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* These classes don't exist in Server 2003 */
                case ProcessImageFileNameWin32:
                {
                    /* Need to fix up the length */
                    if (InfoLength == sizeof(UNICODE_STRING))
                        InfoLength += MAX_PATH * sizeof(WCHAR);
                    /* Fall through */
                }
                case ProcessIoPriority:
                case ProcessTlsInformation:
                case ProcessCycleTime:
                case ProcessPagePriority:
                case ProcessInstrumentationCallback:
                case ProcessThreadStackAllocation:
                case ProcessWorkingSetWatchEx:
                case ProcessImageFileMapping:
                case ProcessAffinityUpdateMode:
                case ProcessMemoryAllocationMode:
                {
                    if (GetNTVersion() < _WIN32_WINNT_VISTA)
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

#ifndef _M_IX86
                case ProcessLdtInformation:
                {
                    SpecialStatus = STATUS_NOT_IMPLEMENTED;
                    break;
                }
#endif
            }

            /* Query the information */
            Status = NtQueryInformationProcess(NtCurrentProcess(),
                                               InfoClassIndex,
                                               InfoPointer,
                                               InfoLength,
                                               NULL);

            /* And probe the results we've got */
            ok(Status == ExpectedStatus || Status == SpecialStatus ||
               IsWindows11Status(Windows11QueryProcessStatus, RTL_NUMBER_OF(Windows11QueryProcessStatus), InfoClassIndex, ExpectedStatus, Status),
                "0x%lx or special status (0x%lx) expected but got 0x%lx for class information %lu in query information process operation!\n", ExpectedStatus, SpecialStatus, Status, InfoClassIndex);
            break;
        }

        case SET:
        {
            switch (InfoClassIndex)
            {
                case ProcessIoPortHandlers:
                {
#ifndef _M_IX86
                    SpecialStatus = STATUS_NOT_IMPLEMENTED;
#else
                    SpecialStatus = STATUS_INVALID_PARAMETER;
#endif
                    break;
                }

                /*
                 * This class returns STATUS_SUCCESS when testing
                 * for STATUS_ACCESS_VIOLATION (setting an invalid address).
                 */
                case ProcessWorkingSetWatch:
                {
                    if (ExpectedStatus == STATUS_ACCESS_VIOLATION)
                        ExpectedStatus = STATUS_SUCCESS;
                    SpecialStatus = STATUS_PORT_ALREADY_SET;
                    break;
                }

                /* This one works differently from the others */
                case ProcessUserModeIOPL:
                {
                    if (ExpectedStatus == STATUS_INFO_LENGTH_MISMATCH)
                        SpecialStatus = STATUS_ACCESS_VIOLATION;
                    else
                        SpecialStatus = STATUS_PRIVILEGE_NOT_HELD;
                    break;
                }

                /* These classes don't belong in the set group */
                case ProcessBasicInformation:
                case ProcessIoCounters:
                case ProcessVmCounters:
                case ProcessTimes:
                case ProcessDebugPort:
                case ProcessPooledUsageAndLimits:
                case ProcessHandleCount:
                case ProcessWow64Information:
                case ProcessImageFileName:
                case ProcessLUIDDeviceMapsEnabled:
                case ProcessDebugObjectHandle:
                case ProcessCookie:
                case ProcessImageInformation:
                {
                    SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* These classes don't exist in Server 2003 */
                case ProcessIoPriority:
                case ProcessTlsInformation:
                case ProcessCycleTime:
                case ProcessPagePriority:
                case ProcessInstrumentationCallback:
                case ProcessThreadStackAllocation:
                case ProcessWorkingSetWatchEx:
                case ProcessImageFileNameWin32:
                case ProcessImageFileMapping:
                case ProcessAffinityUpdateMode:
                case ProcessMemoryAllocationMode:
                {
                    SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* Alignment probing is not performed for these classes */
                case ProcessEnableAlignmentFaultFixup:
                case ProcessPriorityClass:
                case ProcessForegroundInformation:
                {
                    SpecialStatus = STATUS_ACCESS_VIOLATION;
                    break;
                }

#ifndef _M_IX86
                case ProcessLdtInformation:
                case ProcessLdtSize:
                {
                    SpecialStatus = STATUS_NOT_IMPLEMENTED;
                    break;
                }
#endif
            }

            /* Set the information */
            Status = NtSetInformationProcess(NtCurrentProcess(),
                                             InfoClassIndex,
                                             InfoPointer,
                                             InfoLength);

            /* And probe the results we've got */
            ok(Status == ExpectedStatus || Status == SpecialStatus ||
               IsWindows11Status(Windows11SetProcessStatus, RTL_NUMBER_OF(Windows11SetProcessStatus), InfoClassIndex, ExpectedStatus, Status),
                "0x%lx or special status (0x%lx) expected but got 0x%lx for class information %lu in set information process operation!\n", ExpectedStatus, SpecialStatus, Status, InfoClassIndex);
            break;
        }

        default:
            break;
    }
}

VOID
QuerySetThreadValidator(
    _In_ ALIGNMENT_PROBE_MODE ValidationMode,
    _In_ ULONG InfoClassIndex,
    _In_ PVOID InfoPointer,
    _In_ ULONG InfoLength,
    _In_ NTSTATUS ExpectedStatus)
{
    NTSTATUS Status, SpecialStatus = STATUS_SUCCESS;

    /* Before doing anything, check if we want query or set validation */
    switch (ValidationMode)
    {
        case QUERY:
        {
            switch (InfoClassIndex)
            {
                /* These classes don't belong in the query group */
                case ThreadPriority:
                case ThreadBasePriority:
                case ThreadAffinityMask:
                case ThreadImpersonationToken:
                case ThreadEnableAlignmentFaultFixup:
                case ThreadZeroTlsCell:
                case ThreadIdealProcessor:
                case ThreadSetTlsArrayAddress:
                case ThreadSwitchLegacyState:
                {
                    SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* This class supports queries only on Vista and above */
                case ThreadHideFromDebugger:
                {
                    if (GetNTVersion() < _WIN32_WINNT_VISTA)
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* These classes don't exist in Server 2003 SP2 */
                case ThreadEventPair_Reusable:
                case ThreadLastSystemCall:
                case ThreadIoPriority:
                case ThreadCycleTime:
                case ThreadPagePriority:
                case ThreadActualBasePriority:
                case ThreadTebInformation:
                case ThreadCSwitchMon:
                {
                    SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* ThreadNameInformation is Windows 10+, but
                 * ReactOS supports this class, so don't exclude it */
                case ThreadNameInformation:
                {
#ifndef __REACTOS__
                    if (GetNTVersion() < _WIN32_WINNT_WIN10)
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
#else
                    /* This one works differently from the others */
                    if (ExpectedStatus == STATUS_INFO_LENGTH_MISMATCH)
                        ExpectedStatus = STATUS_BUFFER_TOO_SMALL;
#endif
                    break;
                }

                default:
                {
                    /* All of these classes only exist on Windows 7 and above */
                    if ( ((InfoClassIndex >= ThreadCSwitchPmu) &&
                          (GetNTVersion() < _WIN32_WINNT_WIN7)) ||
                         ((InfoClassIndex >= ThreadCpuAccountingInformation) &&
                          (GetNTVersion() < _WIN32_WINNT_WIN8)) ||
                         ((InfoClassIndex >= ThreadSuspendCount) &&
                          (GetNTVersion() < _WIN32_WINNT_WINBLUE)) ||
                         ((InfoClassIndex >= ThreadHeterogeneousCpuPolicy) &&
                          (GetNTVersion() < _WIN32_WINNT_WIN10)) )
                    {
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    }
                }
            }

            /* Query the information */
            Status = NtQueryInformationThread(NtCurrentThread(),
                                              InfoClassIndex,
                                              InfoPointer,
                                              InfoLength,
                                              NULL);

            /* And probe the results we've got */
            ok(Status == ExpectedStatus || Status == SpecialStatus || Status == STATUS_DATATYPE_MISALIGNMENT ||
               IsWindows11Status(Windows11QueryThreadStatus, RTL_NUMBER_OF(Windows11QueryThreadStatus), InfoClassIndex, ExpectedStatus, Status),
                "0x%lx or special status (0x%lx) expected but got 0x%lx for class information %lu in query information thread operation!\n", ExpectedStatus, SpecialStatus, Status, InfoClassIndex);
            break;
        }

        case SET:
        {
            switch (InfoClassIndex)
            {
                /* This class is not implemented in Windows Server 2003 SP2 */
                case ThreadSwitchLegacyState:
                {
                    SpecialStatus = STATUS_NOT_IMPLEMENTED;
                    break;
                }

                /*
                 * This class doesn't take a strict type for size length.
                 * The function happily succeeds on an information length
                 * mismatch scenario with STATUS_SUCCESS.
                 */
                case ThreadHideFromDebugger:
                {
                    SpecialStatus = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                /* These classes don't belong in the set group */
                case ThreadBasicInformation:
                case ThreadTimes:
                case ThreadDescriptorTableEntry:
                case ThreadPerformanceCount:
                case ThreadAmILastThread:
                case ThreadIsIoPending:
                case ThreadIsTerminated:
                {
                    SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* These classes don't exist in Server 2003 SP2 */
                case ThreadEventPair_Reusable:
                case ThreadLastSystemCall:
                case ThreadIoPriority:
                case ThreadCycleTime:
                case ThreadPagePriority:
                case ThreadActualBasePriority:
                case ThreadTebInformation:
                case ThreadCSwitchMon:
                {
                    SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    break;
                }

                /* Alignment probing is not performed for this class */
                case ThreadEnableAlignmentFaultFixup:
                {
                    SpecialStatus = STATUS_ACCESS_VIOLATION;
                    break;
                }

                /* ThreadNameInformation is Windows 10+, but
                 * ReactOS supports this class, so don't exclude it */
                case ThreadNameInformation:
                {
#ifndef __REACTOS__
                    if (GetNTVersion() < _WIN32_WINNT_WIN10)
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
#endif
                    break;
                }

                default:
                {
                    /* All of these classes only exist on Windows 7 and above */
                    if ( ((InfoClassIndex >= ThreadCSwitchPmu) &&
                          (GetNTVersion() < _WIN32_WINNT_WIN7)) ||
                         ((InfoClassIndex >= ThreadCpuAccountingInformation) &&
                          (GetNTVersion() < _WIN32_WINNT_WIN8)) ||
                         ((InfoClassIndex >= ThreadSuspendCount) &&
                          (GetNTVersion() < _WIN32_WINNT_WINBLUE)) ||
                         ((InfoClassIndex >= ThreadHeterogeneousCpuPolicy) &&
                          (GetNTVersion() < _WIN32_WINNT_WIN10)) )
                    {
                        SpecialStatus = STATUS_INVALID_INFO_CLASS;
                    }
                }
            }

            /* Set the information */
            Status = NtSetInformationThread(NtCurrentThread(),
                                            InfoClassIndex,
                                            InfoPointer,
                                            InfoLength);

            /* And probe the results we've got */
            ok(Status == ExpectedStatus || Status == SpecialStatus || Status == STATUS_DATATYPE_MISALIGNMENT || Status == STATUS_SUCCESS ||
               IsWindows11Status(Windows11SetThreadStatus, RTL_NUMBER_OF(Windows11SetThreadStatus), InfoClassIndex, ExpectedStatus, Status),
                "0x%lx or special status (0x%lx) expected but got 0x%lx for class information %lu in set information thread operation!\n", ExpectedStatus, SpecialStatus, Status, InfoClassIndex);
        }

        default:
            break;
    }
}
