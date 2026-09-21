/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntdrvmgmt.c
 * PURPOSE:     Driver memory management interfaces
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#include <nvs/nt/mint.h>

extern KMUTANT MmSystemLoadLock;
extern UCHAR MmDisablePagingExecutive;
PLDR_DATA_TABLE_ENTRY NTAPI MiLookupDataTableEntry(IN PVOID Address);

MM_DRIVER_VERIFIER_DATA MmVerifierData;
LIST_ENTRY MiVerifierDriverAddedThunkListHead;
ULONG MiActiveVerifierThunks;
WCHAR MmVerifyDriverBuffer[512] = {0};
ULONG MmVerifyDriverBufferLength = sizeof(MmVerifyDriverBuffer);
ULONG MmVerifyDriverBufferType = REG_NONE;
ULONG MmVerifyDriverLevel = -1;
PVOID MmTriageActionTaken;
PVOID KernelVerifier;

VOID
NTAPI
MmUnlockPageableImageSection(IN PVOID ImageSectionHandle)
{
    UNREFERENCED_PARAMETER(ImageSectionHandle);
    ASSERT(MmDisablePagingExecutive);
}

VOID
NTAPI
MmLockPageableSectionByHandle(IN PVOID ImageSectionHandle)
{
    UNREFERENCED_PARAMETER(ImageSectionHandle);
    ASSERT(MmDisablePagingExecutive);
}

PVOID
NTAPI
MmLockPageableDataSection(IN PVOID AddressWithinSection)
{
    ASSERT(MmDisablePagingExecutive);
    return AddressWithinSection;
}

ULONG
NTAPI
MmTrimAllSystemPageableMemory(IN ULONG PurgeTransitionList)
{
    UNIMPLEMENTED;
    return 0;
}

NTSTATUS
NTAPI
MmAddVerifierThunks(IN PVOID ThunkBuffer,
                    IN ULONG ThunkBufferSize)
{
    PDRIVER_VERIFIER_THUNK_PAIRS ThunkTable;
    ULONG ThunkCount;
    PDRIVER_SPECIFIED_VERIFIER_THUNKS DriverThunks;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    PVOID ModuleBase, ModuleEnd;
    ULONG i;
    NTSTATUS Status = STATUS_SUCCESS;
    PAGED_CODE();

    if (!MiVerifierDriverAddedThunkListHead.Flink) return STATUS_NOT_SUPPORTED;

    ThunkCount = ThunkBufferSize / sizeof(DRIVER_VERIFIER_THUNK_PAIRS);
    if (!ThunkCount) return STATUS_INVALID_PARAMETER_1;

    DriverThunks = ExAllocatePoolWithTag(PagedPool,
                                         sizeof(*DriverThunks) +
                                         ThunkCount *
                                         sizeof(DRIVER_VERIFIER_THUNK_PAIRS),
                                         'tVmM');
    if (!DriverThunks) return STATUS_INSUFFICIENT_RESOURCES;

    ThunkTable = (PDRIVER_VERIFIER_THUNK_PAIRS)(DriverThunks + 1);
    RtlCopyMemory(ThunkTable,
                  ThunkBuffer,
                  ThunkCount * sizeof(DRIVER_VERIFIER_THUNK_PAIRS));

    KeEnterCriticalRegion();
    KeWaitForSingleObject(&MmSystemLoadLock,
                          WrVirtualMemory,
                          KernelMode,
                          FALSE,
                          NULL);

    LdrEntry = MiLookupDataTableEntry(ThunkTable->PristineRoutine);
    if (!LdrEntry)
    {
        Status = STATUS_INVALID_PARAMETER_2;
        goto Cleanup;
    }

    ModuleBase = LdrEntry->DllBase;
    ModuleEnd = (PVOID)((ULONG_PTR)LdrEntry->DllBase + LdrEntry->SizeOfImage);

    if (LdrEntry->LoadedImports == (PVOID)(ULONG_PTR)-1)
    {
        Status = STATUS_INVALID_PARAMETER_2;
        goto Cleanup;
    }

    for (i = 0; i < ThunkCount; i++)
    {
        if (((ULONG_PTR)ThunkTable->PristineRoutine < (ULONG_PTR)ModuleBase) ||
            ((ULONG_PTR)ThunkTable->PristineRoutine >= (ULONG_PTR)ModuleEnd))
        {
            Status = STATUS_INVALID_PARAMETER_2;
            goto Cleanup;
        }
    }

    DriverThunks->DataTableEntry = LdrEntry;
    DriverThunks->NumberOfThunks = ThunkCount;
    MiActiveVerifierThunks++;
    InsertTailList(&MiVerifierDriverAddedThunkListHead,
                   &DriverThunks->ListEntry);
    DriverThunks = NULL;

Cleanup:

    KeReleaseMutant(&MmSystemLoadLock, MUTANT_INCREMENT, FALSE, FALSE);
    KeLeaveCriticalRegion();

    if (DriverThunks) ExFreePoolWithTag(DriverThunks, 'tVmM');
    return Status;
}

LOGICAL
NTAPI
MmIsDriverVerifying(IN PDRIVER_OBJECT DriverObject)
{
    PLDR_DATA_TABLE_ENTRY LdrEntry;

    LdrEntry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;
    if (!LdrEntry) return FALSE;

    return (LdrEntry->Flags & LDRP_IMAGE_VERIFYING) ? TRUE: FALSE;
}

LOGICAL
NTAPI
MmIsDriverVerifyingByAddress(
    _In_ PVOID AddressWithinSection)
{
    UNREFERENCED_PARAMETER(AddressWithinSection);

    return FALSE;
}

NTSTATUS
NTAPI
MmIsVerifierEnabled(OUT PULONG VerifierFlags)
{
    if (MiVerifierDriverAddedThunkListHead.Flink)
    {
        *VerifierFlags = MmVerifierData.Level;
        return STATUS_SUCCESS;
    }

    *VerifierFlags = 0;
    return STATUS_NOT_SUPPORTED;
}

