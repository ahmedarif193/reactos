#include <windows.h>
#include <winternl.h>

PVOID
WINAPI
VirtualAlloc2(HANDLE Process,
              PVOID BaseAddress,
              SIZE_T Size,
              ULONG AllocationType,
              ULONG PageProtection,
              PMEM_EXTENDED_PARAMETER ExtendedParameters,
              ULONG ParameterCount)
{
    NTSTATUS Status;

    if (!Process)
        Process = GetCurrentProcess();

    Status = NtAllocateVirtualMemoryEx(Process,
                                       &BaseAddress,
                                       &Size,
                                       AllocationType,
                                       PageProtection,
                                       ExtendedParameters,
                                       ParameterCount);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return NULL;
    }

    return BaseAddress;
}
