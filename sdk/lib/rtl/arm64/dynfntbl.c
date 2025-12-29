/*
 * PROJECT:     ReactOS RTL
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Dynamic function table support routines (ARM64)
 * COPYRIGHT:   Copyright 2022-2025 Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#define TAG_RTLDYNFNTBL 'tfDP'

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
#if (NTDDI_VERSION <= NTDDI_WIN10)
    // FIXME: RTL_BALANCED_NODE is defined in ntdef.h, it's impossible to get included here due to precompiled header
    //RTL_BALANCED_NODE TreeNode;
#else
    //RTL_BALANCED_NODE TreeNodeMin;
    //RTL_BALANCED_NODE TreeNodeMax;
#endif
} DYNAMIC_FUNCTION_TABLE, *PDYNAMIC_FUNCTION_TABLE;

static volatile LONG RtlpDynamicFunctionTableLock = 0;
LIST_ENTRY RtlpDynamicFunctionTableList = { &RtlpDynamicFunctionTableList, &RtlpDynamicFunctionTableList };

static __inline
VOID
AcquireDynamicFunctionTableLockExclusive()
{
    while (InterlockedCompareExchange(&RtlpDynamicFunctionTableLock, 1, 0) != 0)
    {
#if defined(_M_ARM64) || defined(__aarch64__)
        __asm__ __volatile__("yield");
#endif
    }
}

static __inline
VOID
ReleaseDynamicFunctionTableLockExclusive()
{
    InterlockedExchange(&RtlpDynamicFunctionTableLock, 0);
}

static __inline
VOID
AcquireDynamicFunctionTableLockShared()
{
    AcquireDynamicFunctionTableLockExclusive();
}

static __inline
VOID
ReleaseDynamicFunctionTableLockShared()
{
    ReleaseDynamicFunctionTableLockExclusive();
}

/*
 * https://docs.microsoft.com/en-us/windows/win32/devnotes/rtlgetfunctiontablelisthead
 */
PLIST_ENTRY
NTAPI
RtlGetFunctionTableListHead(void)
{
    return &RtlpDynamicFunctionTableList;
}

typedef enum _ARM64_FNPDATA_FLAGS
{
    PdataRefToFullXdata = 0,
    PdataPackedUnwindFunction = 1,
    PdataPackedUnwindFragment = 2
} ARM64_FNPDATA_FLAGS;

typedef union _ARM64_UNWIND_XDATA_HEADER
{
    ULONG HeaderData;
    struct
    {
        ULONG FunctionLength : 18;
        ULONG Version : 2;
        ULONG ExceptionDataPresent : 1;
        ULONG EpilogInHeader : 1;
        ULONG EpilogCount : 5;
        ULONG CodeWords : 5;
    };
} ARM64_UNWIND_XDATA_HEADER, *PARM64_UNWIND_XDATA_HEADER;

static __inline ULONG
RtlpArm64FunctionLength(
    _In_ const RUNTIME_FUNCTION *FunctionEntry,
    _In_ ULONG64 ImageBase)
{
    if ((FunctionEntry != NULL) &&
        (FunctionEntry->Flag == PdataRefToFullXdata))
    {
        const ARM64_UNWIND_XDATA_HEADER *Xdata;

        Xdata = (const ARM64_UNWIND_XDATA_HEADER *)RVA(FunctionEntry->UnwindData,
                                                       ImageBase);
        return Xdata->FunctionLength << 2;
    }

    /* Packed format length is encoded in 4-byte units. */
    return FunctionEntry->FunctionLength << 2;
}

static
VOID
RtlpInsertDynamicFunctionTable(PDYNAMIC_FUNCTION_TABLE DynamicTable)
{
    AcquireDynamicFunctionTableLockExclusive();

    /* Insert it into the list */
    InsertTailList(&RtlpDynamicFunctionTableList, &DynamicTable->ListEntry);

    // TODO: insert into RB-trees

    ReleaseDynamicFunctionTableLockExclusive();
}

BOOLEAN
NTAPI
RtlAddFunctionTable(
    _In_ PRUNTIME_FUNCTION FunctionTable,
    _In_ DWORD EntryCount,
    _In_ DWORD64 BaseAddress)
{
    PDYNAMIC_FUNCTION_TABLE dynamicTable;
    ULONG i;

    /* Allocate a dynamic function table */
    dynamicTable = RtlpAllocateMemory(sizeof(*dynamicTable), TAG_RTLDYNFNTBL);
    if (dynamicTable == NULL)
    {
        DPRINT1("Failed to allocate dynamic function table\n");
        return FALSE;
    }

    /* Initialize fields */
    dynamicTable->FunctionTable = FunctionTable;
    dynamicTable->EntryCount = EntryCount;
    dynamicTable->BaseAddress = BaseAddress;
    dynamicTable->Callback = NULL;
    dynamicTable->Context = NULL;
    dynamicTable->Type = RF_UNSORTED;

    /* Loop all entries to find the margins */
    dynamicTable->MinimumAddress = ULONG64_MAX;
    dynamicTable->MaximumAddress = 0;
    for (i = 0; i < EntryCount; i++)
    {
        ULONG FunctionLength = RtlpArm64FunctionLength(&FunctionTable[i], BaseAddress);
        ULONG64 EntryStart = BaseAddress + FunctionTable[i].BeginAddress;
        ULONG64 EntryEnd = EntryStart + FunctionLength;

        dynamicTable->MinimumAddress = min(dynamicTable->MinimumAddress,
                                           EntryStart);
        dynamicTable->MaximumAddress = max(dynamicTable->MaximumAddress,
                                           EntryEnd);
    }

    /* Insert the table into the list */
    RtlpInsertDynamicFunctionTable(dynamicTable);

    return TRUE;
}

BOOLEAN
NTAPI
RtlInstallFunctionTableCallback(
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
    dynamicTable = RtlpAllocateMemory(allocationSize, TAG_RTLDYNFNTBL);
    if (dynamicTable == NULL)
    {
        DPRINT1("Failed to allocate dynamic function table\n");
        return FALSE;
    }

    /* Initialize fields */
    dynamicTable->FunctionTable = (PRUNTIME_FUNCTION)TableIdentifier;
    dynamicTable->EntryCount = 0;
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

BOOLEAN
NTAPI
RtlDeleteFunctionTable(
    _In_ PRUNTIME_FUNCTION FunctionTable)
{
    PLIST_ENTRY listLink;
    PDYNAMIC_FUNCTION_TABLE dynamicTable;
    BOOL removed = FALSE;

    AcquireDynamicFunctionTableLockExclusive();

    /* Loop all tables to find the one to delete */
    for (listLink = RtlpDynamicFunctionTableList.Flink;
         listLink != &RtlpDynamicFunctionTableList;
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
        RtlpFreeMemory(dynamicTable, TAG_RTLDYNFNTBL);
    }

    return removed;
}

PRUNTIME_FUNCTION
NTAPI
RtlpLookupDynamicFunctionEntry(
    _In_ DWORD64 ControlPc,
    _Out_ PDWORD64 ImageBase,
    _In_ PUNWIND_HISTORY_TABLE HistoryTable)
{
    PLIST_ENTRY listLink;
    PDYNAMIC_FUNCTION_TABLE dynamicTable;
    PRUNTIME_FUNCTION functionTable, foundEntry = NULL;
    PGET_RUNTIME_FUNCTION_CALLBACK callback;
    DWORD64 ipOffset;
    ULONG i;

    UNREFERENCED_PARAMETER(HistoryTable);

    AcquireDynamicFunctionTableLockShared();

    /* Loop all tables to find the one matching ControlPc */
    for (listLink = RtlpDynamicFunctionTableList.Flink;
         listLink != &RtlpDynamicFunctionTableList;
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

            /* Loop all entries in the function table */
            functionTable = dynamicTable->FunctionTable;
            ipOffset = ControlPc - dynamicTable->BaseAddress;
            for (i = 0; i < dynamicTable->EntryCount; i++)
            {
                ULONG FunctionEnd = functionTable[i].BeginAddress +
                                    RtlpArm64FunctionLength(&functionTable[i],
                                                            dynamicTable->BaseAddress);

                /* Check if this entry contains the address */
                if ((ipOffset >= functionTable[i].BeginAddress) &&
                    (ipOffset < FunctionEnd))
                {
                    foundEntry = &functionTable[i];
                    *ImageBase = dynamicTable->BaseAddress;
                    goto Exit;
                }
            }
        }
    }

Exit:

    ReleaseDynamicFunctionTableLockShared();

    return foundEntry;
}
