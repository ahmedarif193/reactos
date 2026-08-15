/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     NTDLL virtual-memory compatibility exports
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64) || defined(_M_AMD64)

static NTSTATUS
RtlpGetExtendedParameterZeroBits(PMEM_EXTENDED_PARAMETER ExtendedParameters,
                                 ULONG ExtendedParameterCount,
                                 PULONG_PTR ZeroBits,
                                 PBOOLEAN EcCode)
{
    ULONG Index, Present = 0;

    *ZeroBits = 0;
    *EcCode = FALSE;
    if (ExtendedParameterCount && !ExtendedParameters)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        for (Index = 0; Index < ExtendedParameterCount; ++Index)
        {
            ULONG Type = ExtendedParameters[Index].Type;

            if (ExtendedParameters[Index].Reserved || Type >= 32 || (Present & (1u << Type)))
                _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
            Present |= 1u << Type;

            switch (Type)
            {
                case MemExtendedParameterAddressRequirements:
                {
                    PMEM_ADDRESS_REQUIREMENTS Requirements = ExtendedParameters[Index].Pointer;

                    if (!Requirements)
                        _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                    if (Requirements->LowestStartingAddress || Requirements->Alignment)
                        _SEH2_YIELD(return STATUS_NOT_SUPPORTED);
                    if (Requirements->HighestEndingAddress)
                    {
                        *ZeroBits = (ULONG_PTR)Requirements->HighestEndingAddress | 0xffff;
                        if (*ZeroBits < 0xffff)
                            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                    }
                    break;
                }

                case MemExtendedParameterAttributeFlags:
                    if (ExtendedParameters[Index].ULong64 & ~MEM_EXTENDED_PARAMETER_EC_CODE)
                        _SEH2_YIELD(return STATUS_NOT_SUPPORTED);
                    *EcCode = !!(ExtendedParameters[Index].ULong64 & MEM_EXTENDED_PARAMETER_EC_CODE);
                    break;

                case MemExtendedParameterNumaNode:
                case MemExtendedParameterImageMachine:
                    break;

                default:
                    _SEH2_YIELD(return STATUS_NOT_SUPPORTED);
            }
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}

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
    ULONG_PTR ZeroBits;
    BOOLEAN EcCode;
    NTSTATUS Status;

    Status = RtlpGetExtendedParameterZeroBits(ExtendedParameters, ExtendedParameterCount, &ZeroBits, &EcCode);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = NtAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);
#if defined(_M_ARM64)
    if (NT_SUCCESS(Status) && EcCode && ProcessHandle == NtCurrentProcess())
        ChpeMarkEcCodeRange(*BaseAddress, *RegionSize);
#else
    UNREFERENCED_PARAMETER(EcCode);
#endif
    return Status;
}

NTSTATUS
NTAPI
NtCreateSectionEx(PHANDLE SectionHandle,
                  ACCESS_MASK DesiredAccess,
                  POBJECT_ATTRIBUTES ObjectAttributes,
                  PLARGE_INTEGER MaximumSize,
                  ULONG SectionPageProtection,
                  ULONG AllocationAttributes,
                  HANDLE FileHandle,
                  PMEM_EXTENDED_PARAMETER ExtendedParameters,
                  ULONG ExtendedParameterCount)
{
    if (ExtendedParameterCount && !ExtendedParameters)
        return STATUS_INVALID_PARAMETER;
    if (ExtendedParameterCount)
        return STATUS_NOT_SUPPORTED;
    return NtCreateSection(SectionHandle, DesiredAccess, ObjectAttributes, MaximumSize, SectionPageProtection, AllocationAttributes, FileHandle);
}

NTSTATUS
NTAPI
NtMapViewOfSectionEx(HANDLE SectionHandle,
                     HANDLE ProcessHandle,
                     PVOID *BaseAddress,
                     PLARGE_INTEGER SectionOffset,
                     PSIZE_T ViewSize,
                     ULONG AllocationType,
                     ULONG Protect,
                     PMEM_EXTENDED_PARAMETER ExtendedParameters,
                     ULONG ExtendedParameterCount)
{
    ULONG_PTR ZeroBits;
    BOOLEAN EcCode;
    NTSTATUS Status;

    Status = RtlpGetExtendedParameterZeroBits(ExtendedParameters, ExtendedParameterCount, &ZeroBits, &EcCode);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(EcCode);
    return NtMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress, ZeroBits, 0, SectionOffset, ViewSize, ViewUnmap, AllocationType, Protect);
}

NTSTATUS
NTAPI
NtSetInformationVirtualMemory(HANDLE ProcessHandle,
                              ULONG InformationClass,
                              ULONG_PTR NumberOfEntries,
                              PVOID VirtualAddresses,
                              PVOID Information,
                              ULONG InformationLength)
{
    UNREFERENCED_PARAMETER(ProcessHandle);
    UNREFERENCED_PARAMETER(InformationClass);
    UNREFERENCED_PARAMETER(Information);
    UNREFERENCED_PARAMETER(InformationLength);

    if (NumberOfEntries && !VirtualAddresses)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
NtUnmapViewOfSectionEx(HANDLE ProcessHandle, PVOID BaseAddress, ULONG Flags)
{
    if (Flags & ~1u)
        return STATUS_INVALID_PARAMETER_3;
    return NtUnmapViewOfSection(ProcessHandle, BaseAddress);
}

#endif /* _M_ARM64 || _M_AMD64 */
