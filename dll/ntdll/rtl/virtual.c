/*
 * PROJECT:     LiberNT NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NTDLL virtual-memory compatibility exports
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#if defined(_WIN64)

NTSTATUS
RtlpGetExtendedParameterZeroBits(PMEM_EXTENDED_PARAMETER ExtendedParameters,
                                 ULONG ExtendedParameterCount,
                                 PULONG_PTR ZeroBits,
                                 PBOOLEAN EcCode,
                                 PUSHORT ImageMachine)
{
    ULONG Index, Present = 0;

    if (ZeroBits)
        *ZeroBits = 0;
    *EcCode = FALSE;
    if (ImageMachine)
        *ImageMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (ExtendedParameterCount && !ExtendedParameters)
        return STATUS_INVALID_PARAMETER;
    if (ExtendedParameterCount > MemExtendedParameterMax)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        for (Index = 0; Index < ExtendedParameterCount; ++Index)
        {
            ULONG Type = ExtendedParameters[Index].Type;

            if (ExtendedParameters[Index].Reserved || Type == MemExtendedParameterInvalidType ||
                Type >= MemExtendedParameterMax || (Present & (1u << Type)))
                _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
            Present |= 1u << Type;

            switch (Type)
            {
                case MemExtendedParameterAddressRequirements:
                {
                    PMEM_ADDRESS_REQUIREMENTS Requirements = ExtendedParameters[Index].Pointer;

                    if (!ZeroBits)
                        break;
                    if (!Requirements)
                        _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                    if (Requirements->Alignment)
                        _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                    if (Requirements->LowestStartingAddress)
                        _SEH2_YIELD(return STATUS_NOT_SUPPORTED);
                    if (Requirements->HighestEndingAddress)
                    {
                        SYSTEM_BASIC_INFORMATION BasicInfo;
                        ULONG_PTR Highest = (ULONG_PTR)Requirements->HighestEndingAddress;

                        if (!NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation,
                                                                 &BasicInfo,
                                                                 sizeof(BasicInfo),
                                                                 NULL)))
                        {
                            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                        }
                        if (((Highest + 1) & (BasicInfo.PageSize - 1)) != 0 || Highest > BasicInfo.MaximumUserModeAddress)
                            _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                        *ZeroBits = Highest;
                    }
                    break;
                }

                case MemExtendedParameterAttributeFlags:
                    if (ExtendedParameters[Index].ULong64 & ~MEM_EXTENDED_PARAMETER_EC_CODE)
                        _SEH2_YIELD(return STATUS_NOT_SUPPORTED);
                    *EcCode = !!(ExtendedParameters[Index].ULong64 & MEM_EXTENDED_PARAMETER_EC_CODE);
                    break;

                case MemExtendedParameterNumaNode:
                    break;

                case MemExtendedParameterImageMachine:
                    if (ImageMachine)
                        *ImageMachine = (USHORT)ExtendedParameters[Index].ULong;
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

static
BOOLEAN
RtlpIs64BitMachine(USHORT Machine)
{
    return Machine == IMAGE_FILE_MACHINE_AMD64 ||
           Machine == IMAGE_FILE_MACHINE_ARM64EC ||
           Machine == IMAGE_FILE_MACHINE_ARM64 ||
           Machine == IMAGE_FILE_MACHINE_IA64;
}

static
NTSTATUS
RtlpQueryProcessArchitecture(HANDLE ProcessHandle,
                             USHORT RequestedMachine,
                             PUSHORT ProcessMachine,
                             PBOOLEAN RequestedMachineSupported)
{
    SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION Machines[8];
    NTSTATUS Status;
    ULONG Index;

    *ProcessMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    *RequestedMachineSupported = FALSE;

    Status = NtQuerySystemInformationEx(SystemSupportedProcessorArchitectures2,
                                        &ProcessHandle,
                                        sizeof(ProcessHandle),
                                        Machines,
                                        sizeof(Machines),
                                        NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    for (Index = 0; Index < RTL_NUMBER_OF(Machines) && Machines[Index].Machine; ++Index)
    {
        if (Machines[Index].Process)
            *ProcessMachine = Machines[Index].Machine;
        if (Machines[Index].UserMode && Machines[Index].Machine == RequestedMachine)
            *RequestedMachineSupported = TRUE;
    }

    if (*ProcessMachine == IMAGE_FILE_MACHINE_UNKNOWN)
        return STATUS_NOT_SUPPORTED;

    return STATUS_SUCCESS;
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
    SECTION_IMAGE_INFORMATION ImageInformation;
    USHORT ImageMachine, ProcessMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    BOOLEAN ImageMachineSupported = FALSE;
    BOOLEAN EcCode;
    NTSTATUS Status;

    Status = RtlpGetExtendedParameterZeroBits(ExtendedParameters,
                                              ExtendedParameterCount,
                                              &ZeroBits,
                                              &EcCode,
                                              &ImageMachine);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(EcCode);

    if (ImageMachine != IMAGE_FILE_MACHINE_UNKNOWN)
    {
        Status = RtlpQueryProcessArchitecture(ProcessHandle,
                                              ImageMachine,
                                              &ProcessMachine,
                                              &ImageMachineSupported);
        if (!NT_SUCCESS(Status))
            return Status;

        if (!ImageMachineSupported)
            return STATUS_NOT_SUPPORTED;

        Status = NtQuerySection(SectionHandle,
                                SectionImageInformation,
                                &ImageInformation,
                                sizeof(ImageInformation),
                                NULL);
        if (!NT_SUCCESS(Status))
            return Status;

        /* ReactOS currently packages ARM64 and ARM64EC images separately, so
         * it cannot select a second architecture from one ARM64X section yet.
         * It can still enforce the observable 32/64-bit view boundary here. */
        if (RtlpIs64BitMachine(ImageMachine) !=
            RtlpIs64BitMachine(ImageInformation.Machine))
        {
            return STATUS_NOT_SUPPORTED;
        }
    }

    Status = NtMapViewOfSection(SectionHandle,
                                ProcessHandle,
                                BaseAddress,
                                ZeroBits,
                                0,
                                SectionOffset,
                                ViewSize,
                                ViewUnmap,
                                AllocationType,
                                Protect);
    if (NT_SUCCESS(Status) &&
        ImageMachine != IMAGE_FILE_MACHINE_UNKNOWN &&
        ImageMachine != ProcessMachine)
    {
        return STATUS_IMAGE_MACHINE_TYPE_MISMATCH;
    }

    return Status;
}

#endif /* _WIN64 */
