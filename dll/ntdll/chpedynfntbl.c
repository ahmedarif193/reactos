/*
 * PROJECT:     ReactOS RTL
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 dynamic function tables for the ARM64EC ntdll bridge
 * COPYRIGHT:   Copyright 2022-2025 Timo Kreuzer (timo.kreuzer@reactos.org)
 * COPYRIGHT:   Adaptation Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

#define TAG_RTLDYNFNTBL 'tfDP'

ULONG CDECL ChpeDbgPrint(PCCH Format, ...);

static PVOID NTAPI
ChpepAllocateMemory(SIZE_T Bytes, ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    return RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, Bytes);
}

static VOID NTAPI
ChpepFreeMemory(PVOID Memory, ULONG Tag)
{
    UNREFERENCED_PARAMETER(Tag);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Memory);
}

typedef
_Function_class_(GET_RUNTIME_FUNCTION_CALLBACK)
PRUNTIME_FUNCTION
GET_RUNTIME_FUNCTION_CALLBACK(
    _In_ DWORD64 ControlPc,
    _In_opt_ PVOID Context);
typedef GET_RUNTIME_FUNCTION_CALLBACK *PGET_RUNTIME_FUNCTION_CALLBACK;

typedef
_Function_class_(OUT_OF_PROCESS_FUNCTION_TABLE_CALLBACK)
DWORD
OUT_OF_PROCESS_FUNCTION_TABLE_CALLBACK(
    _In_ HANDLE Process,
    _In_ PVOID TableAddress,
    _Out_ PDWORD Entries,
    _Out_ PRUNTIME_FUNCTION* Functions);
typedef OUT_OF_PROCESS_FUNCTION_TABLE_CALLBACK *POUT_OF_PROCESS_FUNCTION_TABLE_CALLBACK;

typedef enum _FUNCTION_TABLE_TYPE
{
    RF_SORTED = 0x0,
    RF_UNSORTED = 0x1,
    RF_CALLBACK = 0x2,
    RF_KERNEL_DYNAMIC = 0x3,
} FUNCTION_TABLE_TYPE;

typedef struct _DYNAMIC_FUNCTION_TABLE
{
    LIST_ENTRY ListEntry;
    PRUNTIME_FUNCTION FunctionTable;
    LARGE_INTEGER TimeStamp;
    ULONG64 MinimumAddress;
    ULONG64 MaximumAddress;
    ULONG64 BaseAddress;
    PGET_RUNTIME_FUNCTION_CALLBACK Callback;
    PVOID Context;
    PWCHAR OutOfProcessCallbackDll;
    FUNCTION_TABLE_TYPE Type;
    ULONG EntryCount;
    ULONG MaximumEntryCount;
#if (NTDDI_VERSION <= NTDDI_WIN10)
    // FIXME: RTL_BALANCED_NODE is defined in ntdef.h, it's impossible to get included here due to precompiled header
    //RTL_BALANCED_NODE TreeNode;
#else
    //RTL_BALANCED_NODE TreeNodeMin;
    //RTL_BALANCED_NODE TreeNodeMax;
#endif
} DYNAMIC_FUNCTION_TABLE, *PDYNAMIC_FUNCTION_TABLE;

#ifdef _M_ARM64
ULONG
RtlpArm64FunctionLength(
    _In_ ULONG_PTR ImageBase,
    _In_ PRUNTIME_FUNCTION FunctionEntry);
#endif

static
ULONG64
RtlpGetFunctionEndAddress(
    _In_ ULONG64 BaseAddress,
    _In_ PRUNTIME_FUNCTION FunctionEntry)
{
#ifdef _M_ARM64
    return FunctionEntry->BeginAddress + RtlpArm64FunctionLength(BaseAddress, FunctionEntry);
#else
    UNREFERENCED_PARAMETER(BaseAddress);
    return FunctionEntry->EndAddress;
#endif
}

RTL_SRWLOCK ChpepAmd64DynamicFunctionTableLock = { 0 };
LIST_ENTRY ChpepAmd64DynamicFunctionTableList = { &ChpepAmd64DynamicFunctionTableList, &ChpepAmd64DynamicFunctionTableList };

static __inline
VOID
AcquireDynamicFunctionTableLockExclusive()
{
    RtlAcquireSRWLockExclusive(&ChpepAmd64DynamicFunctionTableLock);
}

static __inline
VOID
ReleaseDynamicFunctionTableLockExclusive()
{
    RtlReleaseSRWLockExclusive(&ChpepAmd64DynamicFunctionTableLock);
}

static __inline
VOID
AcquireDynamicFunctionTableLockShared()
{
    RtlAcquireSRWLockShared(&ChpepAmd64DynamicFunctionTableLock);
}

static __inline
VOID
ReleaseDynamicFunctionTableLockShared()
{
    RtlReleaseSRWLockShared(&ChpepAmd64DynamicFunctionTableLock);
}

/*
 * https://docs.microsoft.com/en-us/windows/win32/devnotes/rtlgetfunctiontablelisthead
 */
PLIST_ENTRY
NTAPI
ChpepAmd64GetFunctionTableListHead(void)
{
    return &ChpepAmd64DynamicFunctionTableList;
}

static
VOID
RtlpInsertDynamicFunctionTable(PDYNAMIC_FUNCTION_TABLE DynamicTable)
{
    //LARGE_INTEGER TimeStamp;

    AcquireDynamicFunctionTableLockExclusive();

    /* Insert it into the list */
    InsertTailList(&ChpepAmd64DynamicFunctionTableList, &DynamicTable->ListEntry);

    // TODO: insert into RB-trees

    ReleaseDynamicFunctionTableLockExclusive();
}

BOOLEAN
NTAPI
ChpepAmd64AddFunctionTable(
    _In_ PRUNTIME_FUNCTION FunctionTable,
    _In_ DWORD EntryCount,
    _In_ DWORD64 BaseAddress)
{
    PDYNAMIC_FUNCTION_TABLE dynamicTable;
    ULONG64 PreviousEnd = 0;
    BOOLEAN Sorted = TRUE;
    ULONG i;

    /* Allocate a dynamic function table */
    dynamicTable = ChpepAllocateMemory(sizeof(*dynamicTable), TAG_RTLDYNFNTBL);
    if (dynamicTable == NULL)
    {
        ChpeDbgPrint("Failed to allocate dynamic function table\n");
        return FALSE;
    }

    /* Initialize fields */
    dynamicTable->FunctionTable = FunctionTable;
    dynamicTable->EntryCount = EntryCount;
    dynamicTable->MaximumEntryCount = EntryCount;
    dynamicTable->BaseAddress = BaseAddress;
    dynamicTable->Callback = NULL;
    dynamicTable->Context = NULL;
    dynamicTable->Type = RF_SORTED;

    /* Loop all entries to find the margins */
    dynamicTable->MinimumAddress = EntryCount ? MAXULONGLONG : 0;
    dynamicTable->MaximumAddress = 0;
    for (i = 0; i < EntryCount; i++)
    {
        ULONG64 CurrentEnd = RtlpGetFunctionEndAddress(BaseAddress, &FunctionTable[i]);

        dynamicTable->MinimumAddress = min(dynamicTable->MinimumAddress,
                                           FunctionTable[i].BeginAddress);
        dynamicTable->MaximumAddress = max(dynamicTable->MaximumAddress, CurrentEnd);
        if ((CurrentEnd <= FunctionTable[i].BeginAddress) ||
            ((i != 0) && (FunctionTable[i].BeginAddress < PreviousEnd)))
        {
            Sorted = FALSE;
        }
        PreviousEnd = CurrentEnd;
    }

    dynamicTable->Type = Sorted ? RF_SORTED : RF_UNSORTED;

    if ((dynamicTable->MinimumAddress > MAXULONGLONG - BaseAddress) ||
        (dynamicTable->MaximumAddress > MAXULONGLONG - BaseAddress))
    {
        ChpepFreeMemory(dynamicTable, TAG_RTLDYNFNTBL);
        return FALSE;
    }

    /* Adjust the margins to be absolute addresses */
    dynamicTable->MinimumAddress += BaseAddress;
    dynamicTable->MaximumAddress += BaseAddress;

    /* Insert the table into the list */
    RtlpInsertDynamicFunctionTable(dynamicTable);

    return TRUE;
}

BOOLEAN
NTAPI
ChpepAmd64InstallFunctionTableCallback(
    _In_ DWORD64 TableIdentifier,
    _In_ DWORD64 BaseAddress,
    _In_ DWORD Length,
    _In_ PGET_RUNTIME_FUNCTION_CALLBACK Callback,
    _In_ PVOID Context,
    _In_opt_z_ PCWSTR OutOfProcessCallbackDll)
{
    PDYNAMIC_FUNCTION_TABLE dynamicTable;
    SIZE_T stringLength, allocationSize;

    /* Make sure the identifier is valid */
    if ((TableIdentifier & 3) != 3)
    {
        return FALSE;
    }

    /* Check if we have a DLL name */
    if (OutOfProcessCallbackDll != NULL)
    {
        stringLength = wcslen(OutOfProcessCallbackDll) + 1;
    }
    else
    {
        stringLength = 0;
    }

    /* Calculate required size */
    allocationSize = sizeof(DYNAMIC_FUNCTION_TABLE) + stringLength * sizeof(WCHAR);

    /* Allocate a dynamic function table */
    dynamicTable = ChpepAllocateMemory(allocationSize, TAG_RTLDYNFNTBL);
    if (dynamicTable == NULL)
    {
        ChpeDbgPrint("Failed to allocate dynamic function table\n");
        return FALSE;
    }

    /* Initialize fields */
    dynamicTable->FunctionTable = (PRUNTIME_FUNCTION)TableIdentifier;
    dynamicTable->EntryCount = 0;
    dynamicTable->MaximumEntryCount = 0;
    dynamicTable->BaseAddress = BaseAddress;
    dynamicTable->Callback = Callback;
    dynamicTable->Context = Context;
    dynamicTable->Type = RF_CALLBACK;
    dynamicTable->MinimumAddress = BaseAddress;
    dynamicTable->MaximumAddress = BaseAddress + Length;

    /* If we have a DLL name, copy that, too */
    if (OutOfProcessCallbackDll != NULL)
    {
        dynamicTable->OutOfProcessCallbackDll = (PWCHAR)(dynamicTable + 1);
        RtlCopyMemory(dynamicTable->OutOfProcessCallbackDll,
                      OutOfProcessCallbackDll,
                      stringLength * sizeof(WCHAR));
    }
    else
    {
        dynamicTable->OutOfProcessCallbackDll = NULL;
    }

    /* Insert the table into the list */
    RtlpInsertDynamicFunctionTable(dynamicTable);

    return TRUE;
}

DWORD
NTAPI
ChpepAmd64AddGrowableFunctionTable(
    _Out_ PVOID *DynamicTable,
    _In_ PRUNTIME_FUNCTION FunctionTable,
    _In_ DWORD EntryCount,
    _In_ DWORD MaximumEntryCount,
    _In_ ULONG_PTR RangeBase,
    _In_ ULONG_PTR RangeEnd)
{
    PDYNAMIC_FUNCTION_TABLE dynamicTable;

    *DynamicTable = NULL;

    if (EntryCount > MaximumEntryCount)
    {
        return STATUS_INVALID_PARAMETER;
    }

    dynamicTable = ChpepAllocateMemory(sizeof(*dynamicTable), TAG_RTLDYNFNTBL);
    if (dynamicTable == NULL)
    {
        ChpeDbgPrint("Failed to allocate growable function table\n");
        return STATUS_NO_MEMORY;
    }

    dynamicTable->FunctionTable = FunctionTable;
    dynamicTable->EntryCount = EntryCount;
    dynamicTable->MaximumEntryCount = MaximumEntryCount;
    dynamicTable->BaseAddress = RangeBase;
    dynamicTable->Callback = NULL;
    dynamicTable->Context = NULL;
    dynamicTable->OutOfProcessCallbackDll = NULL;
    dynamicTable->Type = RF_SORTED;
    dynamicTable->MinimumAddress = RangeBase;
    dynamicTable->MaximumAddress = RangeEnd;

    RtlpInsertDynamicFunctionTable(dynamicTable);

    *DynamicTable = dynamicTable;
    return STATUS_SUCCESS;
}

VOID
NTAPI
ChpepAmd64GrowFunctionTable(
    _Inout_ PVOID DynamicTable,
    _In_ DWORD NewEntryCount)
{
    PDYNAMIC_FUNCTION_TABLE dynamicTable = DynamicTable;
    PDYNAMIC_FUNCTION_TABLE currentTable;
    PLIST_ENTRY listLink;

    if (dynamicTable == NULL)
    {
        return;
    }

    AcquireDynamicFunctionTableLockExclusive();

    for (listLink = ChpepAmd64DynamicFunctionTableList.Flink;
         listLink != &ChpepAmd64DynamicFunctionTableList;
         listLink = listLink->Flink)
    {
        currentTable = CONTAINING_RECORD(listLink, DYNAMIC_FUNCTION_TABLE, ListEntry);
        if (currentTable == dynamicTable)
        {
            if ((NewEntryCount > currentTable->EntryCount) &&
                (NewEntryCount <= currentTable->MaximumEntryCount))
            {
                currentTable->EntryCount = NewEntryCount;
            }
            break;
        }
    }

    ReleaseDynamicFunctionTableLockExclusive();
}

VOID
NTAPI
ChpepAmd64DeleteGrowableFunctionTable(
    _In_ PVOID DynamicTable)
{
    PDYNAMIC_FUNCTION_TABLE dynamicTable = DynamicTable;
    PLIST_ENTRY listLink;
    BOOLEAN removed = FALSE;

    if (dynamicTable == NULL)
    {
        return;
    }

    AcquireDynamicFunctionTableLockExclusive();

    for (listLink = ChpepAmd64DynamicFunctionTableList.Flink;
         listLink != &ChpepAmd64DynamicFunctionTableList;
         listLink = listLink->Flink)
    {
        if (listLink == &dynamicTable->ListEntry)
        {
            RemoveEntryList(&dynamicTable->ListEntry);
            removed = TRUE;
            break;
        }
    }

    ReleaseDynamicFunctionTableLockExclusive();

    if (removed)
    {
        ChpepFreeMemory(dynamicTable, TAG_RTLDYNFNTBL);
    }
}

BOOLEAN
NTAPI
ChpepAmd64DeleteFunctionTable(
    _In_ PRUNTIME_FUNCTION FunctionTable)
{
    PLIST_ENTRY listLink;
    PDYNAMIC_FUNCTION_TABLE dynamicTable;
    BOOL removed = FALSE;

    AcquireDynamicFunctionTableLockExclusive();

    /* Loop all tables to find the one to delete */
    for (listLink = ChpepAmd64DynamicFunctionTableList.Flink;
         listLink != &ChpepAmd64DynamicFunctionTableList;
         listLink = listLink->Flink)
    {
        dynamicTable = CONTAINING_RECORD(listLink, DYNAMIC_FUNCTION_TABLE, ListEntry);

        if (dynamicTable->FunctionTable == FunctionTable)
        {
            RemoveEntryList(&dynamicTable->ListEntry);
            removed = TRUE;
            break;
        }
    }

    ReleaseDynamicFunctionTableLockExclusive();

    /* If we were successful, free the memory */
    if (removed)
    {
        ChpepFreeMemory(dynamicTable, TAG_RTLDYNFNTBL);
    }

    return removed;
}

PRUNTIME_FUNCTION
NTAPI
ChpepAmd64LookupDynamicFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _In_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    PLIST_ENTRY listLink;
    PDYNAMIC_FUNCTION_TABLE dynamicTable;
    PRUNTIME_FUNCTION functionTable, foundEntry = NULL;
    PGET_RUNTIME_FUNCTION_CALLBACK callback;
    DWORD64 ipOffset;
    ULONG indexLow, indexHigh, indexMid;

    UNREFERENCED_PARAMETER(HistoryTable);

    AcquireDynamicFunctionTableLockShared();

    /* Loop all tables to find the one matching ControlPc */
    for (listLink = ChpepAmd64DynamicFunctionTableList.Flink;
         listLink != &ChpepAmd64DynamicFunctionTableList;
         listLink = listLink->Flink)
    {
        dynamicTable = CONTAINING_RECORD(listLink, DYNAMIC_FUNCTION_TABLE, ListEntry);

        if ((ControlPc >= dynamicTable->MinimumAddress) &&
            (ControlPc < dynamicTable->MaximumAddress))
        {
            /* Check if there is a callback */
            callback = dynamicTable->Callback;
            if (callback != NULL)
            {
                PVOID context = dynamicTable->Context;

                *ImageBase = dynamicTable->BaseAddress;
                ReleaseDynamicFunctionTableLockShared();
                return callback(ControlPc, context);
            }

            /* Growable and validated ordered tables use a logarithmic lookup. */
            functionTable = dynamicTable->FunctionTable;
            ipOffset = ControlPc - dynamicTable->BaseAddress;
            if (dynamicTable->Type == RF_SORTED)
            {
                indexLow = 0;
                indexHigh = dynamicTable->EntryCount;
                while (indexLow < indexHigh)
                {
                    indexMid = indexLow + (indexHigh - indexLow) / 2;
                    if (ipOffset < functionTable[indexMid].BeginAddress)
                    {
                        indexHigh = indexMid;
                    }
                    else if (ipOffset >= RtlpGetFunctionEndAddress(dynamicTable->BaseAddress, &functionTable[indexMid]))
                    {
                        indexLow = indexMid + 1;
                    }
                    else
                    {
                        foundEntry = &functionTable[indexMid];
                        *ImageBase = dynamicTable->BaseAddress;
                        goto Exit;
                    }
                }
            }
            else
            {
                for (indexMid = 0; indexMid < dynamicTable->EntryCount; indexMid++)
                {
                    if ((ipOffset >= functionTable[indexMid].BeginAddress) &&
                        (ipOffset < RtlpGetFunctionEndAddress(dynamicTable->BaseAddress, &functionTable[indexMid])))
                    {
                        foundEntry = &functionTable[indexMid];
                        *ImageBase = dynamicTable->BaseAddress;
                        goto Exit;
                    }
                }
            }
        }
    }

Exit:

    ReleaseDynamicFunctionTableLockShared();

    return foundEntry;
}
