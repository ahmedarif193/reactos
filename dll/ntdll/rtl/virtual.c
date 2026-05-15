/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTDLL virtual-memory compatibility exports
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

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

    if (ExtendedParameterCount && !ExtendedParameters)
        return STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < ExtendedParameterCount; Index++)
    {
        switch (ExtendedParameters[Index].Type)
        {
            case MemExtendedParameterAttributeFlags:
                if (ExtendedParameters[Index].ULong64 & ~MEM_EXTENDED_PARAMETER_EC_CODE)
                    return STATUS_INVALID_PARAMETER;
                break;

            case MemExtendedParameterNumaNode:
            case MemExtendedParameterImageMachine:
                break;

            default:
                return STATUS_NOT_SUPPORTED;
        }
    }

    return NtAllocateVirtualMemory(ProcessHandle,
                                   BaseAddress,
                                   0,
                                   RegionSize,
                                   AllocationType,
                                   Protect);
}

#endif /* _M_ARM64 */
