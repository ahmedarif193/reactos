/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     NTDLL virtual-memory compatibility exports
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64) || defined(_M_ARM64EC)

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtAllocateVirtualMemoryEx(HANDLE ProcessHandle,
                          PVOID *BaseAddress,
                          PSIZE_T RegionSize,
                          ULONG AllocationType,
                          ULONG Protect,
                          PMEM_EXTENDED_PARAMETER ExtendedParameters,
                          ULONG ExtendedParameterCount)
{
    ULONG Index;
    BOOLEAN EcCode = FALSE;
    NTSTATUS Status;

    if (ExtendedParameterCount && !ExtendedParameters)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < ExtendedParameterCount; Index++)
    {
        switch (ExtendedParameters[Index].Type)
        {
            case MemExtendedParameterAttributeFlags:
                if (ExtendedParameters[Index].ULong64 & ~MEM_EXTENDED_PARAMETER_EC_CODE)
                    return STATUS_INVALID_PARAMETER;

                if (ExtendedParameters[Index].ULong64 & MEM_EXTENDED_PARAMETER_EC_CODE)
                    EcCode = TRUE;
                break;

            case MemExtendedParameterNumaNode:
            case MemExtendedParameterImageMachine:
                break;

            default:
                return STATUS_NOT_SUPPORTED;
        }
    }

    Status = NtAllocateVirtualMemory(ProcessHandle,
                                     BaseAddress,
                                     0,
                                     RegionSize,
                                     AllocationType,
                                     Protect);

    if (NT_SUCCESS(Status) &&
        EcCode &&
        ProcessHandle == NtCurrentProcess() &&
        BaseAddress &&
        RegionSize)
    {
        ChpeMarkEcCodeRange(*BaseAddress, *RegionSize);
    }

    return Status;
}

#endif /* _M_ARM64 || _M_ARM64EC */
