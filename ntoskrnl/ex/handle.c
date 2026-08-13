/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ex/handle.c
 * PURPOSE:         Generic Executive Handle Tables
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Thomas Weidenmueller <w3seek@reactos.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

LIST_ENTRY HandleTableListHead;
EX_PUSH_LOCK HandleTableListLock;
ULONG ExpFreeListCount;
#define SizeOfHandle(x) (sizeof(HANDLE) * (x))

#define INDEX_TO_HANDLE_VALUE(x) ((x) << HANDLE_TAG_BITS)

C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, FreeLists) == 128);
C_ASSERT(sizeof(HANDLE_TABLE_FREE_LIST) == 128);
C_ASSERT(sizeof(HANDLE_TABLE) == 256);

#ifdef _WIN64
C_ASSERT(sizeof(HANDLE_TABLE_ENTRY) == 16);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE_ENTRY, HighValue) == 8);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE_FREE_LIST, FreeListLock) == 0);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE_FREE_LIST, FirstFreeHandleEntry) == 8);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE_FREE_LIST, LastFreeHandleEntry) == 16);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE_FREE_LIST, HandleCount) == 24);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE_FREE_LIST, HighWaterMark) == 28);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, NextHandleNeedingPool) == 0);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, ExtraInfoPages) == 4);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, TableCode) == 8);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, QuotaProcess) == 16);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, HandleTableList) == 24);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, UniqueProcessId) == 40);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, Flags) == 44);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, HandleContentionEvent) == 48);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, HandleTableLock) == 56);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, ActualEntry) == 128);
C_ASSERT(FIELD_OFFSET(HANDLE_TABLE, DebugInfo) == 160);
#endif

#ifdef _M_ARM64
NTHALAPI ULONG NTAPI HalQueryMaximumProcessorCount(VOID);
#endif

/* PRIVATE FUNCTIONS *********************************************************/

#ifdef _WIN64
#define strtoulptr strtoull
#else
#define strtoulptr strtoul
#endif

FORCEINLINE
ULONG
ExpGetCurrentProcessorNumber(VOID)
{
#ifdef _M_ARM64
    ULONG Number;

    __asm__ __volatile__("ldr %w0, [x18, #" ARM64_KPCR_STRINGIFY(ARM64_KPCR_PRCB_NUMBER) "]" : "=r"(Number) :: "memory");
    return Number;
#else
    return KeGetCurrentProcessorNumber();
#endif
}

CODE_SEG("INIT")
VOID
NTAPI
ExpInitializeHandleTables(VOID)
{
    /* Initialize the list of handle tables and the lock */
    InitializeListHead(&HandleTableListHead);
    ExInitializePushLock(&HandleTableListLock);
#ifdef _M_ARM64
    ExpFreeListCount = HalQueryMaximumProcessorCount();
#else
    ExpFreeListCount = KeQueryMaximumProcessorCount();
#endif
    ASSERT(ExpFreeListCount != 0);
}

PHANDLE_TABLE_ENTRY
NTAPI
ExpLookupHandleTableEntry(IN PHANDLE_TABLE HandleTable,
                          IN EXHANDLE Handle)
{
    ULONG TableLevel;
    ULONG_PTR TableBase;
    PHANDLE_TABLE_ENTRY HandleArray, Entry;
    PVOID *PointerArray;

    /* Clear the tag bits */
    Handle.TagBits = 0;

    /* Check if the handle is in the allocated range */
    if (Handle.Value >= HandleTable->NextHandleNeedingPool)
    {
        return NULL;
    }

    /* Get the table code */
    TableBase = HandleTable->TableCode;

    /* Extract the table level and actual table base */
    TableLevel = (ULONG)(TableBase & 3);
    TableBase &= ~3;

    PointerArray = (PVOID*)TableBase;
    HandleArray = (PHANDLE_TABLE_ENTRY)TableBase;

    /* Check what level we're running at */
    switch (TableLevel)
    {
        case 2:

            /* Get the mid level pointer array */
            PointerArray = PointerArray[Handle.HighIndex];
            ASSERT(PointerArray != NULL);

            /* Fall through */
        case 1:

            /* Get the handle array */
            HandleArray = PointerArray[Handle.MidIndex];
            ASSERT(HandleArray != NULL);

            /* Fall through */
        case 0:

            /* Get the entry using the low index */
            Entry = &HandleArray[Handle.LowIndex];

            /* All done */
            break;

        default:

            ASSERT(FALSE);
            Entry = NULL;
    }

    /* Return the handle entry */
    return Entry;
}

LONG
NTAPI
ExpGetHandleCount(IN PHANDLE_TABLE HandleTable)
{
    LONG HandleCount = 0;
    ULONG i;

    for (i = 0; i < ExpFreeListCount; i++) HandleCount += (LONG)ReadULongAcquire((PULONG)&HandleTable->FreeLists[i].HandleCount);
    return HandleCount;
}

PVOID
NTAPI
ExpAllocateTablePagedPool(IN PEPROCESS Process OPTIONAL,
                          IN SIZE_T Size)
{
    PVOID Buffer;
    NTSTATUS Status;

    /* Do the allocation */
    Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_OBJECT_TABLE);
    if (Buffer)
    {
        /* Clear the memory */
        RtlZeroMemory(Buffer, Size);

        /* Check if we have a process to charge quota */
        if (Process)
        {
            /* Charge quota */
            Status = PsChargeProcessPagedPoolQuota(Process, Size);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Buffer, TAG_OBJECT_TABLE);
                return NULL;
            }
        }
    }

    /* Return the allocated memory */
    return Buffer;
}

PVOID
NTAPI
ExpAllocateTablePagedPoolNoZero(IN PEPROCESS Process OPTIONAL,
                                IN SIZE_T Size)
{
    PVOID Buffer;
    NTSTATUS Status;

    /* Do the allocation */
    Buffer = ExAllocatePoolWithTag(PagedPool, Size, TAG_OBJECT_TABLE);
    if (Buffer)
    {
        /* Check if we have a process to charge quota */
        if (Process)
        {
            /* Charge quota */
            Status = PsChargeProcessPagedPoolQuota(Process, Size);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Buffer, TAG_OBJECT_TABLE);
                return NULL;
            }
        }
    }

    /* Return the allocated memory */
    return Buffer;
}

VOID
NTAPI
ExpFreeTablePagedPool(IN PEPROCESS Process OPTIONAL,
                      IN PVOID Buffer,
                      IN SIZE_T Size)
{
    /* Free the buffer */
    ExFreePoolWithTag(Buffer, TAG_OBJECT_TABLE);
    if (Process)
    {
        /* Release quota */
        PsReturnProcessPagedPoolQuota(Process, Size);
    }
}

VOID
NTAPI
ExpFreeLowLevelTable(IN PEPROCESS Process,
                     IN PHANDLE_TABLE_ENTRY TableEntry)
{
    /* Check if we have an entry */
    if (TableEntry[0].Object)
    {
        /* Free the entry */
        ExpFreeTablePagedPool(Process,
                              TableEntry[0].Object,
                              HANDLE_TABLE_ENTRY_INFO_SIZE);
    }

    /* Free the table */
    ExpFreeTablePagedPool(Process, TableEntry, PAGE_SIZE);
}

VOID
NTAPI
ExpFreeHandleTable(IN PHANDLE_TABLE HandleTable)
{
    PEPROCESS Process = HandleTable->QuotaProcess;
    ULONG i, j;
    ULONG_PTR TableCode = HandleTable->TableCode;
    ULONG_PTR TableBase = TableCode & ~3;
    ULONG TableLevel = (ULONG)(TableCode & 3);
    PHANDLE_TABLE_ENTRY Level1, *Level2, **Level3;
    PAGED_CODE();

    /* Check which level we're at */
    if (TableLevel == 0)
    {
        /* Select the first level table base and just free it */
        Level1 = (PVOID)TableBase;
        ExpFreeLowLevelTable(Process, Level1);
    }
    else if (TableLevel == 1)
    {
        /* Select the second level table base */
        Level2 = (PVOID)TableBase;

        /* Loop each mid level entry */
        for (i = 0; i < MID_LEVEL_ENTRIES; i++)
        {
            /* Leave if we've reached the last entry */
            if (!Level2[i]) break;

            /* Free the second level table */
            ExpFreeLowLevelTable(Process, Level2[i]);
        }

        /* Free the second level table */
        ExpFreeTablePagedPool(Process, Level2, PAGE_SIZE);
    }
    else
    {
        /* Select the third level table base */
        Level3 = (PVOID)TableBase;

        /* Loop each high level entry */
        for (i = 0; i < HIGH_LEVEL_ENTRIES; i++)
        {
            /* Leave if we've reached the last entry */
            if (!Level3[i]) break;

            /* Loop each mid level entry */
            for (j = 0; j < MID_LEVEL_ENTRIES; j++)
            {
                /* Leave if we've reached the last entry */
                if (!Level3[i][j]) break;

                /* Free the second level table */
                ExpFreeLowLevelTable(Process, Level3[i][j]);
            }

            /* Free the third level table entry */
            ExpFreeTablePagedPool(Process, Level3[i], PAGE_SIZE);
        }

        /* Free the third level table */
        ExpFreeTablePagedPool(Process,
                              Level3,
                              SizeOfHandle(HIGH_LEVEL_ENTRIES));
    }

    /* Free the actual table and check if we need to release quota */
    ExFreePoolWithTag(HandleTable, TAG_OBJECT_TABLE);
    if (Process)
    {
        /* Release the quota it was taking up */
        PsReturnProcessPagedPoolQuota(Process, sizeof(HANDLE_TABLE));
    }
}

VOID
NTAPI
ExpFreeHandleTableEntry(IN PHANDLE_TABLE HandleTable,
                        IN EXHANDLE Handle,
                        IN PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    PULONG CachedReferenceCount;
    PHANDLE_TABLE_ENTRY NextFree;
    PHANDLE_TABLE_FREE_LIST FreeList;
    ULONG Processor;
    PAGED_CODE();

    /* Sanity checks */
    ASSERT(HandleTableEntry->Object == NULL);
    ASSERT(HandleTableEntry == ExpLookupHandleTableEntry(HandleTable, Handle));

    /* A reused entry must never inherit per-handle state. */
    CachedReferenceCount = ExGetHandleCachedReferenceCount(HandleTable, Handle.GenericHandleOverlay, HandleTableEntry);
    if (CachedReferenceCount) *CachedReferenceCount = 0;

    /* Mark the handle as free */
    Handle.TagBits = 0;

    /* Select the current processor's free list */
    Processor = HandleTable->StrictFIFO ? 0 : ExpGetCurrentProcessorNumber();
    ASSERT(Processor < ExpFreeListCount);
    FreeList = &HandleTable->FreeLists[Processor];

    /* Serialize the native head/tail update */
    ExAcquirePushLockExclusive(&FreeList->FreeListLock);
    HandleTableEntry->NextFreeHandleEntry = NULL;

    if (!HandleTable->StrictFIFO)
    {
        NextFree = FreeList->FirstFreeHandleEntry;
        HandleTableEntry->NextFreeHandleEntry = NextFree;
        if (!NextFree) FreeList->LastFreeHandleEntry = HandleTableEntry;
        FreeList->FirstFreeHandleEntry = HandleTableEntry;
    }
    else
    {
        NextFree = FreeList->LastFreeHandleEntry;
        if (NextFree) NextFree->NextFreeHandleEntry = HandleTableEntry;
        else FreeList->FirstFreeHandleEntry = HandleTableEntry;
        FreeList->LastFreeHandleEntry = HandleTableEntry;
    }

    FreeList->HandleCount--;
    ExReleasePushLockExclusive(&FreeList->FreeListLock);
}

PHANDLE_TABLE
NTAPI
ExpAllocateHandleTable(IN PEPROCESS Process OPTIONAL,
                       IN BOOLEAN NewTable)
{
    PHANDLE_TABLE HandleTable;
    PHANDLE_TABLE_ENTRY HandleTableTable, HandleEntry;
    PHANDLE_TABLE_ENTRY_INFO InfoTable;
    PHANDLE_TABLE_FREE_LIST FreeList;
    SIZE_T HandleTableSize;
    ULONG i;
    NTSTATUS Status;
    PAGED_CODE();

    /* Allocate the native header followed by one free list per processor */
    HandleTableSize = (ExpFreeListCount + 1) * sizeof(HANDLE_TABLE_FREE_LIST);
    HandleTable = ExAllocatePoolWithTag(PagedPoolCacheAligned, HandleTableSize, TAG_OBJECT_TABLE);
    if (!HandleTable) return NULL;
    ASSERT(((ULONG_PTR)HandleTable & (SYSTEM_CACHE_ALIGNMENT_SIZE - 1)) == 0);

    /* Check if we have a process */
    if (Process)
    {
        /* Charge quota */
        Status = PsChargeProcessPagedPoolQuota(Process, sizeof(HANDLE_TABLE));
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(HandleTable, TAG_OBJECT_TABLE);
            return NULL;
        }
    }

    /* Clear the table */
    RtlZeroMemory(HandleTable, HandleTableSize);

    /* Now allocate the first level structures */
    HandleTableTable = ExpAllocateTablePagedPoolNoZero(Process, PAGE_SIZE);
    if (!HandleTableTable)
    {
        /* Failed, free the table */
        ExFreePoolWithTag(HandleTable, TAG_OBJECT_TABLE);

        /* Return the quota it was taking up */
        if (Process)
        {
            PsReturnProcessPagedPoolQuota(Process, sizeof(HANDLE_TABLE));
        }

        return NULL;
    }

    /* Allocate the public audit records followed by our cached-ref counters. */
    InfoTable = ExpAllocateTablePagedPool(Process, HANDLE_TABLE_ENTRY_INFO_SIZE);
    if (!InfoTable)
    {
        ExpFreeTablePagedPool(Process, HandleTableTable, PAGE_SIZE);
        ExFreePoolWithTag(HandleTable, TAG_OBJECT_TABLE);
        if (Process) PsReturnProcessPagedPoolQuota(Process, sizeof(HANDLE_TABLE));
        return NULL;
    }

    /* Write the pointer to our first level structures */
    HandleTable->TableCode = (ULONG_PTR)HandleTableTable;

    /* Initialize the first entry */
    HandleEntry = &HandleTableTable[0];
    HandleEntry->Value = 0;
    HandleEntry->InfoTable = InfoTable;
    HandleEntry->NextFreeHandleEntry = NULL;

    /* Check if this is a new table */
    if (NewTable)
    {
        FreeList = &HandleTable->FreeLists[0];
        FreeList->FirstFreeHandleEntry = &HandleTableTable[1];
        FreeList->LastFreeHandleEntry = &HandleTableTable[LOW_LEVEL_ENTRIES - 1];

        /* Loop every low level entry */
        for (i = 1; i < LOW_LEVEL_ENTRIES - 1; i++)
        {
            /* Set up the free data */
            HandleTableTable[i].Value = 0;
            HandleTableTable[i].NextFreeHandleEntry = &HandleTableTable[i + 1];
        }

        /* Terminate the last entry */
        HandleEntry = &HandleTableTable[LOW_LEVEL_ENTRIES - 1];
        HandleEntry->Value = 0;
        HandleEntry->NextFreeHandleEntry = NULL;
    }

    /* Set the next handle needing pool after our allocated page from above */
    HandleTable->NextHandleNeedingPool = INDEX_TO_HANDLE_VALUE(LOW_LEVEL_ENTRIES);

    /* Setup the rest of the handle table data */
    HandleTable->QuotaProcess = Process;
    HandleTable->UniqueProcessId = PtrToUlong(PsGetCurrentProcess()->UniqueProcessId);
    HandleTable->Flags = 0;
    HandleTable->RaiseUMExceptionOnInvalidHandleClose = (Process != NULL);

    /* Initialize the table and per-processor free-list locks */
    ExInitializePushLock(&HandleTable->HandleTableLock);
    for (i = 0; i < ExpFreeListCount; i++) ExInitializePushLock(&HandleTable->FreeLists[i].FreeListLock);

    /* Initialize the contention event lock and return the lock */
    ExInitializePushLock(&HandleTable->HandleContentionEvent);
    return HandleTable;
}

PHANDLE_TABLE_ENTRY
NTAPI
ExpAllocateLowLevelTable(IN PHANDLE_TABLE HandleTable,
                         IN BOOLEAN DoInit)
{
    ULONG i;
    PHANDLE_TABLE_ENTRY Low, HandleEntry;
    PHANDLE_TABLE_ENTRY_INFO InfoTable;

    /* Allocate the low level table */
    Low = ExpAllocateTablePagedPoolNoZero(HandleTable->QuotaProcess,
                                          PAGE_SIZE);
    if (!Low) return NULL;

    /* Keep auxiliary data separate so HANDLE_TABLE_ENTRY retains its ABI. */
    InfoTable = ExpAllocateTablePagedPool(HandleTable->QuotaProcess, HANDLE_TABLE_ENTRY_INFO_SIZE);
    if (!InfoTable)
    {
        ExpFreeTablePagedPool(HandleTable->QuotaProcess, Low, PAGE_SIZE);
        return NULL;
    }

    /* Setup the initial entry */
    HandleEntry = &Low[0];
    HandleEntry->Value = 0;
    HandleEntry->InfoTable = InfoTable;
    HandleEntry->NextFreeHandleEntry = (PHANDLE_TABLE_ENTRY)(ULONG_PTR)HandleTable->NextHandleNeedingPool;

    /* Check if we're initializing */
    if (DoInit)
    {
        /* Loop each entry */
        for (i = 1; i < LOW_LEVEL_ENTRIES - 1; i++)
        {
            Low[i].Value = 0;
            Low[i].NextFreeHandleEntry = &Low[i + 1];
        }

        /* Terminate the last entry */
        HandleEntry = &Low[LOW_LEVEL_ENTRIES - 1];
        HandleEntry->NextFreeHandleEntry = NULL;
        HandleEntry->Value = 0;
    }

    /* Return the low level table */
    return Low;
}

PHANDLE_TABLE_ENTRY*
NTAPI
ExpAllocateMidLevelTable(IN PHANDLE_TABLE HandleTable,
                         IN BOOLEAN DoInit,
                         OUT PHANDLE_TABLE_ENTRY *LowTableEntry)
{
    PHANDLE_TABLE_ENTRY *Mid, Low;

    /* Allocate the mid level table */
    Mid = ExpAllocateTablePagedPool(HandleTable->QuotaProcess, PAGE_SIZE);
    if (!Mid) return NULL;

    /* Allocate a new low level for it */
    Low = ExpAllocateLowLevelTable(HandleTable, DoInit);
    if (!Low)
    {
        /* We failed, free the mid table */
        ExpFreeTablePagedPool(HandleTable->QuotaProcess, Mid, PAGE_SIZE);
        return NULL;
    }

    /* Link the tables and return the pointer */
    Mid[0] = Low;
    *LowTableEntry = Low;
    return Mid;
}

BOOLEAN
NTAPI
ExpAllocateHandleTableEntrySlow(IN PHANDLE_TABLE HandleTable,
                                IN PHANDLE_TABLE_FREE_LIST FreeList,
                                IN BOOLEAN DoInit)
{
    ULONG i, j, Index;
    PHANDLE_TABLE_ENTRY Low = NULL, *Mid, **High, *SecondLevel, **ThirdLevel;
    PVOID Value;
    ULONG_PTR TableCode = HandleTable->TableCode;
    ULONG_PTR TableBase = TableCode & ~3;
    ULONG TableLevel = (ULONG)(TableCode & 3);
    PAGED_CODE();

    /* Check how many levels we already have */
    if (TableLevel == 0)
    {
        /* Allocate a mid level, since we only have a low level */
        Mid = ExpAllocateMidLevelTable(HandleTable, DoInit, &Low);
        if (!Mid) return FALSE;

        /* Link up the tables */
        Mid[1] = Mid[0];
        Mid[0] = (PVOID)TableBase;

        /* Write the new level and attempt to change the table code */
        TableBase = ((ULONG_PTR)Mid) | 1;
        Value = InterlockedExchangePointer((PVOID*)&HandleTable->TableCode, (PVOID)TableBase);
    }
    else if (TableLevel == 1)
    {
        /* Setup the 2nd level table */
        SecondLevel = (PVOID)TableBase;

        /* Get if the next index can fit in the table */
        i = HandleTable->NextHandleNeedingPool /
            INDEX_TO_HANDLE_VALUE(LOW_LEVEL_ENTRIES);
        if (i < MID_LEVEL_ENTRIES)
        {
            /* We need to allocate a new table */
            Low = ExpAllocateLowLevelTable(HandleTable, DoInit);
            if (!Low) return FALSE;

            /* Update the table */
            Value = InterlockedExchangePointer((PVOID*)&SecondLevel[i], Low);
            ASSERT(Value == NULL);
        }
        else
        {
            /* We need a new high level table */
            High = ExpAllocateTablePagedPool(HandleTable->QuotaProcess,
                                             SizeOfHandle(HIGH_LEVEL_ENTRIES));
            if (!High) return FALSE;

            /* Allocate a new mid level table as well */
            Mid = ExpAllocateMidLevelTable(HandleTable, DoInit, &Low);
            if (!Mid)
            {
                /* We failed, free the high level table as well */
                ExpFreeTablePagedPool(HandleTable->QuotaProcess,
                                      High,
                                      SizeOfHandle(HIGH_LEVEL_ENTRIES));
                return FALSE;
            }

            /* Link up the tables */
            High[0] = (PVOID)TableBase;
            High[1] = Mid;

            /* Write the new table and change the table code */
            TableBase = ((ULONG_PTR)High) | 2;
            Value = InterlockedExchangePointer((PVOID*)&HandleTable->TableCode,
                                               (PVOID)TableBase);
        }
    }
    else if (TableLevel == 2)
    {
        /* Setup the 3rd level table */
        ThirdLevel = (PVOID)TableBase;

        /* Get the index and check if it can fit */
        i = HandleTable->NextHandleNeedingPool / INDEX_TO_HANDLE_VALUE(MAX_MID_INDEX);
        if (i >= HIGH_LEVEL_ENTRIES) return FALSE;

        /* Check if there's no mid-level table */
        if (!ThirdLevel[i])
        {
            /* Allocate a new mid level table */
            Mid = ExpAllocateMidLevelTable(HandleTable, DoInit, &Low);
            if (!Mid) return FALSE;

            /* Update the table pointer */
            Value = InterlockedExchangePointer((PVOID*)&ThirdLevel[i], Mid);
            ASSERT(Value == NULL);
        }
        else
        {
            /* We have one, check at which index we should insert our entry */
            Index = (HandleTable->NextHandleNeedingPool / INDEX_TO_HANDLE_VALUE(1)) -
                     i * MAX_MID_INDEX;
            j = Index / LOW_LEVEL_ENTRIES;

            /* Allocate a new low level */
            Low = ExpAllocateLowLevelTable(HandleTable, DoInit);
            if (!Low) return FALSE;

            /* Update the table pointer */
            Value = InterlockedExchangePointer((PVOID*)&ThirdLevel[i][j], Low);
            ASSERT(Value == NULL);
        }
    }
    else
    {
        /* Something is really broken */
        ASSERT(FALSE);
    }

    /* Update the index of the next handle */
    InterlockedExchangeAdd((PLONG)&HandleTable->NextHandleNeedingPool, INDEX_TO_HANDLE_VALUE(LOW_LEVEL_ENTRIES));

    /* Check if need to initialize the table */
    if (DoInit)
    {
        /* Append the new page to the selected processor's free list */
        ExAcquirePushLockExclusive(&FreeList->FreeListLock);
        if (FreeList->LastFreeHandleEntry) FreeList->LastFreeHandleEntry->NextFreeHandleEntry = &Low[1];
        else FreeList->FirstFreeHandleEntry = &Low[1];
        FreeList->LastFreeHandleEntry = &Low[LOW_LEVEL_ENTRIES - 1];
        ExReleasePushLockExclusive(&FreeList->FreeListLock);
    }

    /* All done */
    return TRUE;
}

PHANDLE_TABLE_ENTRY
NTAPI
ExpAllocateHandleTableEntry(IN PHANDLE_TABLE HandleTable,
                            OUT PEXHANDLE NewHandle)
{
    PHANDLE_TABLE_ENTRY Entry, NextEntry, PageHeader;
    PHANDLE_TABLE_FREE_LIST FreeList;
    LONG Count;
    ULONG BaseHandle, CurrentNextHandle, EntryIndex, i, NextHandle, Processor, SelectedProcessor;
    ULONG_PTR PageBase;
    BOOLEAN Result;

    if (HandleTable->Rundown)
    {
        NewHandle->GenericHandleOverlay = NULL;
        return NULL;
    }

    /* Start allocation loop */
    for (;;)
    {
        /* Scan the per-processor free lists, beginning with the current CPU */
        Processor = HandleTable->StrictFIFO ? 0 : ExpGetCurrentProcessorNumber();
        ASSERT(Processor < ExpFreeListCount);
        SelectedProcessor = Processor;
        NextHandle = ReadULongNoFence(&HandleTable->NextHandleNeedingPool);
        for (i = 0; i < ExpFreeListCount; i++)
        {
            FreeList = &HandleTable->FreeLists[Processor];
            Entry = ReadPointerNoFence((PVOID const volatile *)&FreeList->FirstFreeHandleEntry);
            if (Entry != NULL)
            {
                ExAcquirePushLockExclusive(&FreeList->FreeListLock);
                Entry = FreeList->FirstFreeHandleEntry;
                if (Entry != NULL)
                {
                    NextEntry = Entry->NextFreeHandleEntry;
                    FreeList->FirstFreeHandleEntry = NextEntry;
                    if (!NextEntry) FreeList->LastFreeHandleEntry = NULL;
                    Count = ++FreeList->HandleCount;
                    if (Count > FreeList->HighWaterMark) FreeList->HighWaterMark = Count;
                }
                ExReleasePushLockExclusive(&FreeList->FreeListLock);

                if (Entry != NULL)
                {
                    PageBase = (ULONG_PTR)Entry & ~(PAGE_SIZE - 1);
                    PageHeader = (PHANDLE_TABLE_ENTRY)PageBase;
                    BaseHandle = (ULONG)(ULONG_PTR)PageHeader->NextFreeHandleEntry;
                    EntryIndex = (ULONG)(((ULONG_PTR)Entry - PageBase) / sizeof(*Entry));
                    NewHandle->Value = BaseHandle + INDEX_TO_HANDLE_VALUE(EntryIndex);
                    /* Observe the growth frontier after consuming the published entry */
                    CurrentNextHandle = ReadULongAcquire(&HandleTable->NextHandleNeedingPool);
                    if (NewHandle->Value >= CurrentNextHandle)
                    {
                        DPRINT1("EX: invalid free-list entry: table=%p list=%p cpu=%lu entry=%p next=%p page=%p base=%lx index=%lu handle=%Ix frontier=%lx\n",
                                HandleTable, FreeList, Processor, Entry, NextEntry, (PVOID)PageBase, BaseHandle, EntryIndex, NewHandle->Value, CurrentNextHandle);
                    }
                    ASSERT(NewHandle->Value < CurrentNextHandle);
                    return Entry;
                }
            }

            Processor++;
            if (Processor == ExpFreeListCount) Processor = 0;
        }

        /* Grow the table once if no processor list supplied an entry */
        ExAcquirePushLockExclusive(&HandleTable->HandleTableLock);
        Result = TRUE;
        if (NextHandle == ReadULongNoFence(&HandleTable->NextHandleNeedingPool)) Result = ExpAllocateHandleTableEntrySlow(HandleTable, &HandleTable->FreeLists[SelectedProcessor], TRUE);
        ExReleasePushLockExclusive(&HandleTable->HandleTableLock);

        if (!Result)
        {
            NewHandle->GenericHandleOverlay = NULL;
            return NULL;
        }
    }
}

PHANDLE_TABLE
NTAPI
ExCreateHandleTable(IN PEPROCESS Process OPTIONAL)
{
    PHANDLE_TABLE HandleTable;
    PAGED_CODE();

    /* Allocate the handle table */
    HandleTable = ExpAllocateHandleTable(Process, TRUE);
    if (!HandleTable) return NULL;

    /* Acquire the handle table lock */
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&HandleTableListLock);

    /* Insert it into the list */
    InsertTailList(&HandleTableListHead, &HandleTable->HandleTableList);

    /* Release the lock */
    ExReleasePushLockExclusive(&HandleTableListLock);
    KeLeaveCriticalRegion();

    /* Return the handle table */
    return HandleTable;
}

HANDLE
NTAPI
ExCreateHandle(IN PHANDLE_TABLE HandleTable,
               IN PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    EXHANDLE Handle;
    PHANDLE_TABLE_ENTRY NewEntry;
    PULONG CachedReferenceCount;
    PAGED_CODE();

    /* Start with a clean handle */
    Handle.GenericHandleOverlay = NULL;

    /* Enter a critical region and allocate a new entry */
    KeEnterCriticalRegion();
    NewEntry = ExpAllocateHandleTableEntry(HandleTable, &Handle);
    if (NewEntry)
    {
        /* Clear auxiliary state before publishing a reused entry. */
        CachedReferenceCount = ExGetHandleCachedReferenceCount(HandleTable, Handle.GenericHandleOverlay, NewEntry);
        ASSERT(CachedReferenceCount != NULL);
        if (CachedReferenceCount) *CachedReferenceCount = 0;

        /* Publish the high word before unlocking the low word */
        NewEntry->HighValue = HandleTableEntry->HighValue;
        WritePointerRelease(&NewEntry->Object, (PVOID)(HandleTableEntry->Value | EXHANDLE_TABLE_ENTRY_LOCK_BIT));
    }

    KeLeaveCriticalRegion();

    /* Return the handle value */
    return Handle.GenericHandleOverlay;
}

VOID
NTAPI
ExpBlockOnLockedHandleEntry(IN PHANDLE_TABLE HandleTable,
                            IN PHANDLE_TABLE_ENTRY HandleTableEntry,
                            IN LONG_PTR LockedValue)
{
    (VOID)ExBlockOnAddressPushLock(&HandleTable->HandleContentionEvent, &HandleTableEntry->Object, &LockedValue, sizeof(LockedValue), NULL);
}

BOOLEAN
NTAPI
ExpLockHandleTableEntry(IN PHANDLE_TABLE HandleTable,
                        IN PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    LONG_PTR NewValue, OldValue;

    /* Sanity check */
    ASSERT((KeGetCurrentThread()->CombinedApcDisable != 0) ||
           (KeGetCurrentIrql() == APC_LEVEL));

    /* Start lock loop */
    for (;;)
    {
        /* Get the current value and check if it's locked */
        OldValue = *(volatile LONG_PTR *)&HandleTableEntry->Object;
        if (OldValue & EXHANDLE_TABLE_ENTRY_LOCK_BIT)
        {
            /* It's not locked, remove the lock bit to lock it */
            NewValue = OldValue & ~EXHANDLE_TABLE_ENTRY_LOCK_BIT;
            if (InterlockedCompareExchangePointer(&HandleTableEntry->Object,
                                                  (PVOID)NewValue,
                                                  (PVOID)OldValue) == (PVOID)OldValue)
            {
                /* We locked it, get out */
                return TRUE;
            }

            /* The entry changed before the exchange, so retry the load */
            continue;
        }
        else
        {
            /* We couldn't lock it, bail out if it's been freed */
            if (!OldValue) return FALSE;
        }

        /* It's locked, wait for it to be unlocked */
        ExpBlockOnLockedHandleEntry(HandleTable, HandleTableEntry, OldValue);
    }
}

VOID
NTAPI
ExUnlockHandleTableEntry(IN PHANDLE_TABLE HandleTable,
                         IN PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    LONG_PTR OldValue;
    PAGED_CODE();

    /* Sanity check */
    ASSERT((KeGetCurrentThread()->CombinedApcDisable != 0) ||
           (KeGetCurrentIrql() == APC_LEVEL));

    /* Set the lock bit and make sure it wasn't earlier */
#ifdef _WIN64
    OldValue = InterlockedExchangeAdd64((volatile LONG64 *)&HandleTableEntry->Value, EXHANDLE_TABLE_ENTRY_LOCK_BIT);
#else
    OldValue = InterlockedExchangeAdd((PLONG)&HandleTableEntry->Value, EXHANDLE_TABLE_ENTRY_LOCK_BIT);
#endif
    ASSERT((OldValue & EXHANDLE_TABLE_ENTRY_LOCK_BIT) == 0);

    /* Publish the unlock before waking any recorded waiters */
    KeMemoryBarrier();
    if (HandleTable->HandleContentionEvent.Value) ExfUnblockPushLock(&HandleTable->HandleContentionEvent, NULL);
}

VOID
NTAPI
ExRemoveHandleTable(IN PHANDLE_TABLE HandleTable)
{
    PAGED_CODE();

    /* Acquire the table lock */
    KeEnterCriticalRegion();
    ExAcquirePushLockExclusive(&HandleTableListLock);

    /* Remove the table and reset the list */
    RemoveEntryList(&HandleTable->HandleTableList);
    InitializeListHead(&HandleTable->HandleTableList);

    /* Release the lock */
    ExReleasePushLockExclusive(&HandleTableListLock);
    KeLeaveCriticalRegion();
}

VOID
NTAPI
ExDestroyHandleTable(IN PHANDLE_TABLE HandleTable,
                     IN PVOID DestroyHandleProcedure OPTIONAL)
{
    PAGED_CODE();

    /* Remove the handle from the list */
    ExRemoveHandleTable(HandleTable);

    /* Check if we have a destroy callback */
    if (DestroyHandleProcedure)
    {
        /* FIXME: */
        ASSERT(FALSE);
    }

    /* Free the handle table */
    ExpFreeHandleTable(HandleTable);
}

BOOLEAN
NTAPI
ExDestroyHandle(IN PHANDLE_TABLE HandleTable,
                IN HANDLE Handle,
                IN PHANDLE_TABLE_ENTRY HandleTableEntry OPTIONAL)
{
    EXHANDLE ExHandle;
    PVOID Object;
    PAGED_CODE();

    /* Setup the actual handle value */
    ExHandle.GenericHandleOverlay = Handle;

    /* Enter a critical region and check if we have to lookup the handle */
    KeEnterCriticalRegion();
    if (!HandleTableEntry)
    {
        /* Lookup the entry */
        HandleTableEntry = ExpLookupHandleTableEntry(HandleTable, ExHandle);

        /* Make sure that we found an entry, and that it's valid */
        if (!ExHandle.LowIndex ||
            !(HandleTableEntry) ||
            !(HandleTableEntry->Object))
        {
            /* Invalid handle, fail */
            KeLeaveCriticalRegion();
            return FALSE;
        }

        /* Lock the entry */
        if (!ExpLockHandleTableEntry(HandleTable, HandleTableEntry))
        {
            /* Couldn't lock, fail */
            KeLeaveCriticalRegion();
            return FALSE;
        }
    }
    else
    {
        /* Make sure the handle is locked */
        ASSERT((HandleTableEntry->Value & EXHANDLE_TABLE_ENTRY_LOCK_BIT) == 0);
    }

    /* Clear the handle */
    Object = HandleTableEntry->Object;
    HandleTableEntry->Object = NULL;
    KeMemoryBarrier();

    /* Sanity checks */
    ASSERT(Object != NULL);
    ASSERT((((ULONG_PTR)Object) & EXHANDLE_TABLE_ENTRY_LOCK_BIT) == 0);

    /* Unblock any recorded waiter */
    if (HandleTable->HandleContentionEvent.Value) ExfUnblockPushLock(&HandleTable->HandleContentionEvent, NULL);

    /* Free the actual entry */
    ExpFreeHandleTableEntry(HandleTable, ExHandle, HandleTableEntry);

    /* If we got here, return success */
    KeLeaveCriticalRegion();
    return TRUE;
}

PHANDLE_TABLE_ENTRY
NTAPI
ExMapHandleToPointer(IN PHANDLE_TABLE HandleTable,
                     IN HANDLE Handle)
{
    EXHANDLE ExHandle;
    PHANDLE_TABLE_ENTRY HandleTableEntry;
    PAGED_CODE();

    /* Set the handle value */
    ExHandle.GenericHandleOverlay = Handle;

    /* Fail if we got an invalid index */
    if (!(ExHandle.Index & (LOW_LEVEL_ENTRIES - 1))) return NULL;

    /* Do the lookup */
    HandleTableEntry = ExpLookupHandleTableEntry(HandleTable, ExHandle);
    if (!HandleTableEntry) return NULL;

    /* Lock it */
    if (!ExpLockHandleTableEntry(HandleTable, HandleTableEntry)) return NULL;

    /* Return the entry */
    return HandleTableEntry;
}

PULONG
NTAPI
ExGetHandleCachedReferenceCount(IN PHANDLE_TABLE HandleTable,
                                IN HANDLE Handle,
                                IN PHANDLE_TABLE_ENTRY HandleTableEntry)
{
    EXHANDLE ExHandle;
    PHANDLE_TABLE_ENTRY LowLevelTable;
    PHANDLE_TABLE_ENTRY_INFO InfoTable;
    PULONG CachedReferenceCounts;

    ExHandle.GenericHandleOverlay = Handle;
    ExHandle.TagBits = 0;

    if (HandleTableEntry != ExpLookupHandleTableEntry(HandleTable, ExHandle)) return NULL;

    LowLevelTable = HandleTableEntry - ExHandle.LowIndex;
    InfoTable = LowLevelTable[0].InfoTable;
    if (!InfoTable) return NULL;

    CachedReferenceCounts = (PULONG)((PUCHAR)InfoTable + LOW_LEVEL_ENTRIES * sizeof(*InfoTable));
    return &CachedReferenceCounts[ExHandle.LowIndex];
}

PHANDLE_TABLE
NTAPI
ExDupHandleTable(IN PEPROCESS Process,
                 IN PHANDLE_TABLE HandleTable,
                 IN PEX_DUPLICATE_HANDLE_CALLBACK DupHandleProcedure,
                 IN ULONG_PTR Mask)
{
    PHANDLE_TABLE NewTable;
    PHANDLE_TABLE_FREE_LIST FreeList;
    EXHANDLE Handle;
    PHANDLE_TABLE_ENTRY HandleTableEntry, NewEntry;
    BOOLEAN Failed = FALSE;
    PAGED_CODE();

    /* Allocate the duplicated copy */
    NewTable = ExpAllocateHandleTable(Process, FALSE);
    if (!NewTable) return NULL;

    /* Loop each entry */
    while (NewTable->NextHandleNeedingPool <
           HandleTable->NextHandleNeedingPool)
    {
        /* Insert it into the duplicated copy */
        if (!ExpAllocateHandleTableEntrySlow(NewTable, &NewTable->FreeLists[0], FALSE))
        {
            /* Insert failed, free the new copy and return */
            ExpFreeHandleTable(NewTable);
            return NULL;
        }
    }

    /* Setup the initial handle table data */
    FreeList = &NewTable->FreeLists[0];
    NewTable->ExtraInfoPages = 0;
    NewTable->Duplicated = TRUE;

    /* Setup the first handle value  */
    Handle.Value = INDEX_TO_HANDLE_VALUE(1);

    /* Enter a critical region and lookup the new entry */
    KeEnterCriticalRegion();
    while ((NewEntry = ExpLookupHandleTableEntry(NewTable, Handle)))
    {
        /* Lookup the old entry */
        HandleTableEntry = ExpLookupHandleTableEntry(HandleTable, Handle);

        /* Loop each entry */
        do
        {
            /* Check if it doesn't match the audit mask */
            if (!(HandleTableEntry->Value & Mask))
            {
                /* Free it since we won't use it */
                Failed = TRUE;
            }
            else
            {
                /* Lock the entry */
                if (!ExpLockHandleTableEntry(HandleTable, HandleTableEntry))
                {
                    /* Free it since we can't lock it, so we won't use it */
                    Failed = TRUE;
                }
                else
                {
                    /* Copy the handle value */
                    *NewEntry = *HandleTableEntry;

                    /* Call the duplicate callback */
                    if (DupHandleProcedure(Process,
                                           HandleTable,
                                           HandleTableEntry,
                                           NewEntry))
                    {
                        /* Clear failure flag */
                        Failed = FALSE;

                        /* Lock the entry and increase this free list's count */
                        NewEntry->Value |= EXHANDLE_TABLE_ENTRY_LOCK_BIT;
                        FreeList->HandleCount++;
                    }
                    else
                    {
                        /* Duplication callback refused, fail */
                        Failed = TRUE;
                    }
                }
            }

            /* Check if we failed earlier and need to free */
            if (Failed)
            {
                /* Free this entry */
                NewEntry->Value = 0;
                NewEntry->NextFreeHandleEntry = NULL;
                if (FreeList->LastFreeHandleEntry) FreeList->LastFreeHandleEntry->NextFreeHandleEntry = NewEntry;
                else FreeList->FirstFreeHandleEntry = NewEntry;
                FreeList->LastFreeHandleEntry = NewEntry;
            }

            /* Increase the handle value and move to the next entry */
            Handle.Value += INDEX_TO_HANDLE_VALUE(1);
            NewEntry++;
            HandleTableEntry++;
        } while (Handle.Value % INDEX_TO_HANDLE_VALUE(LOW_LEVEL_ENTRIES));

        /* We're done, skip the last entry */
        Handle.Value += INDEX_TO_HANDLE_VALUE(1);
    }

    /* Record the initial high-water mark */
    FreeList->HighWaterMark = FreeList->HandleCount;

    /* Acquire the table lock and insert this new table into the list */
    ExAcquirePushLockExclusive(&HandleTableListLock);
    InsertTailList(&HandleTableListHead, &NewTable->HandleTableList);
    ExReleasePushLockExclusive(&HandleTableListLock);

    /* Leave the critical region we entered previously and return the table */
    KeLeaveCriticalRegion();
    return NewTable;
}

BOOLEAN
NTAPI
ExChangeHandle(IN PHANDLE_TABLE HandleTable,
               IN HANDLE Handle,
               IN PEX_CHANGE_HANDLE_CALLBACK ChangeRoutine,
               IN ULONG_PTR Context)
{
    EXHANDLE ExHandle;
    PHANDLE_TABLE_ENTRY HandleTableEntry;
    BOOLEAN Result = FALSE;
    PAGED_CODE();

    /* Set the handle value */
    ExHandle.GenericHandleOverlay = Handle;

    /* Find the entry for this handle */
    HandleTableEntry = ExpLookupHandleTableEntry(HandleTable, ExHandle);

    /* Make sure that we found an entry, and that it's valid */
    if (!ExHandle.LowIndex ||
        !(HandleTableEntry) ||
        !(HandleTableEntry->Object))
    {
        /* It isn't, fail */
        return FALSE;
    }

    /* Enter a critical region */
    KeEnterCriticalRegion();

    /* Try locking the handle entry */
    if (ExpLockHandleTableEntry(HandleTable, HandleTableEntry))
    {
        /* Call the change routine and unlock the entry */
        Result = ChangeRoutine(HandleTableEntry, Context);
        ExUnlockHandleTableEntry(HandleTable, HandleTableEntry);
    }

    /* Leave the critical region and return the callback result */
    KeLeaveCriticalRegion();
    return Result;
}

VOID
NTAPI
ExSweepHandleTable(IN PHANDLE_TABLE HandleTable,
                   IN PEX_SWEEP_HANDLE_CALLBACK EnumHandleProcedure,
                   IN PVOID Context)
{
    EXHANDLE Handle;
    PHANDLE_TABLE_ENTRY HandleTableEntry;
    PAGED_CODE();

    /* Set the initial value and loop the entries */
    Handle.Value = INDEX_TO_HANDLE_VALUE(1);
    while ((HandleTableEntry = ExpLookupHandleTableEntry(HandleTable, Handle)))
    {
        /* Loop each handle */
        do
        {
            /* Lock the entry */
            if (ExpLockHandleTableEntry(HandleTable, HandleTableEntry))
            {
                /* Notify the callback routine */
                EnumHandleProcedure(HandleTableEntry,
                                    Handle.GenericHandleOverlay,
                                    Context);
            }

            /* Go to the next handle and entry */
            Handle.Value += INDEX_TO_HANDLE_VALUE(1);
            HandleTableEntry++;
        } while (Handle.Value % INDEX_TO_HANDLE_VALUE(LOW_LEVEL_ENTRIES));

        /* Skip past the last entry */
        Handle.Value += INDEX_TO_HANDLE_VALUE(1);
    }
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
ExEnumHandleTable(IN PHANDLE_TABLE HandleTable,
                  IN PEX_ENUM_HANDLE_CALLBACK EnumHandleProcedure,
                  IN OUT PVOID Context,
                  OUT PHANDLE EnumHandle OPTIONAL)
{
    EXHANDLE Handle;
    PHANDLE_TABLE_ENTRY HandleTableEntry;
    BOOLEAN Result = FALSE;
    PAGED_CODE();

    /* Enter a critical region */
    KeEnterCriticalRegion();

    /* Set the initial value and loop the entries */
    Handle.Value = 0;
    while ((HandleTableEntry = ExpLookupHandleTableEntry(HandleTable, Handle)))
    {
        /* Validate the entry */
        if (Handle.LowIndex && HandleTableEntry->Object)
        {
            /* Lock the entry */
            if (ExpLockHandleTableEntry(HandleTable, HandleTableEntry))
            {
                /* Notify the callback routine */
                Result = EnumHandleProcedure(HandleTableEntry,
                                             Handle.GenericHandleOverlay,
                                             Context);

                /* Unlock it */
                ExUnlockHandleTableEntry(HandleTable, HandleTableEntry);

                /* Was this the one looked for? */
                if (Result)
                {
                    /* If so, return it if requested */
                    if (EnumHandle) *EnumHandle = Handle.GenericHandleOverlay;
                    break;
                }
            }
        }

        /* Go to the next entry */
        Handle.Value += INDEX_TO_HANDLE_VALUE(1);
    }

    /* Leave the critical region and return callback result */
    KeLeaveCriticalRegion();
    return Result;
}

#if DBG && defined(KDBG)

#include <kdbg/kdb.h>

BOOLEAN ExpKdbgExtHandle(ULONG Argc, PCHAR Argv[])
{
    USHORT i;
    char *endptr;
    HANDLE ProcessId;
    EXHANDLE ExHandle;
    PLIST_ENTRY Entry;
    PLIST_ENTRY NextEntry;
    PEPROCESS Process;
    WCHAR KeyPath[256];
    PHANDLE_TABLE HandleTable;
    POBJECT_HEADER ObjectHeader;
    PHANDLE_TABLE_ENTRY TableEntry;
    LONG HandleCount, ListHandleCount;
    ULONG Processor;
    ULONG TableCount = 0;

    if (Argc > 1)
    {
        /* Get EPROCESS address or PID */
        i = 0;
        while (Argv[1][i])
        {
            if (!isdigit(Argv[1][i]))
            {
                i = 0;
                break;
            }

            ++i;
        }

        if (i == 0)
        {
            if (!KdbpGetHexNumber(Argv[1], (PVOID)&Process))
            {
                KdbpPrint("Invalid parameter: %s\n", Argv[1]);
                return TRUE;
            }

            /* In the end, we always want a PID */
            if (!NT_SUCCESS(KdbpSafeReadMemory(&ProcessId, &Process->UniqueProcessId, sizeof(ProcessId))))
            {
                KdbpPrint("Unreadable EPROCESS: %p\n", Process);
                return TRUE;
            }
        }
        else
        {
            ProcessId = (HANDLE)strtoulptr(Argv[1], &endptr, 10);
            if (*endptr != '\0')
            {
                KdbpPrint("Invalid parameter: %s\n", Argv[1]);
                return TRUE;
            }
        }
    }
    else
    {
        ProcessId = PsGetCurrentProcessId();
    }

    Entry = HandleTableListHead.Flink;
    while (Entry != &HandleTableListHead &&
           Entry != NULL &&
           TableCount++ < 65536)
    {
        HANDLE_TABLE HandleTableSnapshot;

        /* Only return matching PID
         * 0 matches everything
         */
        HandleTable = CONTAINING_RECORD(Entry, HANDLE_TABLE, HandleTableList);
        if (!NT_SUCCESS(KdbpSafeReadMemory(&HandleTableSnapshot, HandleTable, sizeof(HandleTableSnapshot))))
        {
            KdbpPrint("%p: <unreadable handle table>\n", HandleTable);
            break;
        }
        NextEntry = HandleTableSnapshot.HandleTableList.Flink;

        if (ProcessId != 0 && HandleTableSnapshot.UniqueProcessId != HandleToUlong(ProcessId))
            goto NextTable;

        KdbpPrint("\n");

        HandleCount = 0;
        for (Processor = 0; Processor < ExpFreeListCount; Processor++)
        {
            if (!NT_SUCCESS(KdbpSafeReadMemory(&ListHandleCount, &HandleTable->FreeLists[Processor].HandleCount, sizeof(ListHandleCount))))
            {
                HandleCount = -1;
                break;
            }
            HandleCount += ListHandleCount;
        }
        KdbpPrint("Handle table at %p with %d entries in use\n", HandleTable, HandleCount);

        ExHandle.Value = 0;
        while ((TableEntry = ExpLookupHandleTableEntry(HandleTable, ExHandle)))
        {
            HANDLE_TABLE_ENTRY TableEntrySnapshot;

            if (!NT_SUCCESS(KdbpSafeReadMemory(&TableEntrySnapshot, TableEntry, sizeof(TableEntrySnapshot))))
            {
                KdbpPrint("%p: <unreadable handle entry %p>\n", ExHandle.Value, TableEntry);
                break;
            }

            if (ExHandle.LowIndex && TableEntrySnapshot.Object)
            {
                OBJECT_HEADER ObjectHeaderSnapshot;
                OBJECT_TYPE ObjectTypeSnapshot;
                POBJECT_TYPE ObjectType;

                ObjectHeader = (POBJECT_HEADER)
                    ((ULONG_PTR)TableEntrySnapshot.Object & ~OBJ_HANDLE_ATTRIBUTES);
                if (!NT_SUCCESS(KdbpSafeReadMemory(&ObjectHeaderSnapshot, ObjectHeader, sizeof(ObjectHeaderSnapshot))))
                {
                    KdbpPrint("%p: ObjectHeader %p is unreadable\n", ExHandle.Value, ObjectHeader);
                    goto NextHandle;
                }
                ObjectType = ObTypeIndexTable[ObjectHeaderSnapshot.TypeIndex ^ ObHeaderCookie ^ (UCHAR)((ULONG_PTR)ObjectHeader >> 8)];

                KdbpPrint("%p: Object: %p GrantedAccess: %x Entry: %p\n", ExHandle.Value, &ObjectHeader->Body, TableEntrySnapshot.GrantedAccess, TableEntry);
                KdbpPrint("Object: %p Type: (%p) ", &ObjectHeader->Body, ObjectType);
                if (ObjectType != NULL && NT_SUCCESS(KdbpSafeReadMemory(&ObjectTypeSnapshot, ObjectType, sizeof(ObjectTypeSnapshot))))
                {
                    KdbpPrintUnicodeString(&ObjectTypeSnapshot.Name);
                }
                else
                {
                    KdbpPrint("<unreadable>");
                }
                KdbpPrint("\n");
                KdbpPrint("\tObjectHeader: %p\n", ObjectHeader);
                KdbpPrint("\t\tHandleCount: %u PointerCount: %u\n", ObjectHeaderSnapshot.HandleCount, ObjectHeaderSnapshot.PointerCount);

                /* Specific objects debug prints */

                /* For file, display path */
                if (ObjectType == IoFileObjectType)
                {
                    FILE_OBJECT FileObjectSnapshot;

                    KdbpPrint("\t\t\tName: ");
                    if (NT_SUCCESS(KdbpSafeReadMemory(&FileObjectSnapshot, &ObjectHeader->Body, sizeof(FileObjectSnapshot))))
                    {
                        KdbpPrintUnicodeString(&FileObjectSnapshot.FileName);
                    }
                    else
                    {
                        KdbpPrint("<unreadable>");
                    }
                    KdbpPrint("\n");
                }

                /* For directory, and win32k objects, display object name */
                else if (ObjectType == ObpDirectoryObjectType ||
                         ObjectType == ExWindowStationObjectType ||
                         ObjectType == ExDesktopObjectType ||
                         ObjectType == MmSectionObjectType)
                {
                    if (ObjectHeaderSnapshot.InfoMask & OBP_NAME_INFO_MASK)
                    {
                        OBJECT_HEADER_NAME_INFO ObjectNameSnapshot;
                        POBJECT_HEADER_NAME_INFO ObjectNameInfo;

                        ObjectNameInfo = (POBJECT_HEADER_NAME_INFO)((PCHAR)ObjectHeader - ObpInfoMaskToOffset[ObjectHeaderSnapshot.InfoMask & (OBP_CREATOR_INFO_MASK | OBP_NAME_INFO_MASK)]);
                        KdbpPrint("\t\t\tName: ");
                        if (NT_SUCCESS(KdbpSafeReadMemory(&ObjectNameSnapshot, ObjectNameInfo, sizeof(ObjectNameSnapshot))))
                        {
                            KdbpPrintUnicodeString(&ObjectNameSnapshot.Name);
                        }
                        else
                        {
                            KdbpPrint("<unreadable>");
                        }
                        KdbpPrint("\n");
                    }
                }

                /* For registry keys, display full path */
                else if (ObjectType == CmpKeyObjectType)
                {
                    CM_KEY_BODY KeyBodySnapshot;
                    PCM_KEY_CONTROL_BLOCK CurrentKcb;
                    ULONG KeyDepth = 0;
                    ULONG FirstCharacter = RTL_NUMBER_OF(KeyPath) - 1;
                    BOOLEAN Complete = TRUE;

                    KeyPath[FirstCharacter] = UNICODE_NULL;
                    if (!NT_SUCCESS(KdbpSafeReadMemory(&KeyBodySnapshot, &ObjectHeader->Body, sizeof(KeyBodySnapshot))))
                    {
                        KdbpPrint("\t\t\tName: <unreadable key body>\n");
                        goto NextHandle;
                    }
                    CurrentKcb = KeyBodySnapshot.KeyControlBlock;
                    while (CurrentKcb != NULL && KeyDepth++ < 256)
                    {
                        CM_KEY_CONTROL_BLOCK KcbSnapshot;
                        CM_NAME_CONTROL_BLOCK NameSnapshot;
                        ULONG CharacterCount;
                        ULONG Character;

                        if (!NT_SUCCESS(KdbpSafeReadMemory(&KcbSnapshot, CurrentKcb, sizeof(KcbSnapshot))) ||
                            KcbSnapshot.Delete ||
                            KcbSnapshot.NameBlock == NULL ||
                            !NT_SUCCESS(KdbpSafeReadMemory(&NameSnapshot, KcbSnapshot.NameBlock, sizeof(NameSnapshot))))
                        {
                            Complete = FALSE;
                            break;
                        }
                        CharacterCount = NameSnapshot.Compressed ?
                            NameSnapshot.NameLength :
                            NameSnapshot.NameLength / sizeof(WCHAR);
                        if (CharacterCount > FirstCharacter || FirstCharacter - CharacterCount == 0)
                        {
                            Complete = FALSE;
                            break;
                        }
                        FirstCharacter -= CharacterCount;
                        if (NameSnapshot.Compressed)
                        {
                            UCHAR CompressedName[RTL_NUMBER_OF(KeyPath)];

                            if (!NT_SUCCESS(KdbpSafeReadMemory(CompressedName, KcbSnapshot.NameBlock->Name, CharacterCount)))
                            {
                                Complete = FALSE;
                                break;
                            }
                            for (Character = 0; Character < CharacterCount; Character++)
                                KeyPath[FirstCharacter + Character] = CompressedName[Character];
                        }
                        else if (!NT_SUCCESS(KdbpSafeReadMemory(&KeyPath[FirstCharacter], KcbSnapshot.NameBlock->Name, CharacterCount * sizeof(WCHAR))))
                        {
                            Complete = FALSE;
                            break;
                        }
                        KeyPath[--FirstCharacter] = OBJ_NAME_PATH_SEPARATOR;
                        CurrentKcb = KcbSnapshot.ParentKcb;
                    }
                    if (KeyDepth >= 256)
                        Complete = FALSE;

                    KdbpPrint("\t\t\tName: ");
                    if (!Complete)
                        KdbpPrint("<truncated or unreadable>");
                    if (FirstCharacter < RTL_NUMBER_OF(KeyPath) - 1)
                    {
                        UNICODE_STRING KeyPathString;

                        KeyPathString.Buffer = &KeyPath[FirstCharacter];
                        KeyPathString.Length = (USHORT)
                            ((RTL_NUMBER_OF(KeyPath) - 1 - FirstCharacter) * sizeof(WCHAR));
                        KeyPathString.MaximumLength = KeyPathString.Length + sizeof(WCHAR);
                        KdbpPrintUnicodeString(&KeyPathString);
                    }
                    KdbpPrint("\n");
                }
            }

NextHandle:
            if (ExHandle.Value > MAXULONG_PTR - INDEX_TO_HANDLE_VALUE(1))
                break;
            ExHandle.Value += INDEX_TO_HANDLE_VALUE(1);
            if (KdbpIsOutputAborted())
                return TRUE;
        }

NextTable:
        if (NextEntry == Entry)
        {
            KdbpPrint("!handle: self-linked handle-table entry %p.\n", Entry);
            break;
        }
        Entry = NextEntry;
    }
    if (TableCount >= 65536)
        KdbpPrint("!handle: handle-table enumeration stopped at 65536 entries.\n");

    return TRUE;
}

#endif // DBG && defined(KDBG)
