/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     LGPL-2.0-or-later (https://spdx.org/licenses/LGPL-2.0-or-later)
 * PURPOSE:     Test for LdrQueryProcessModuleInformation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

/*

NTSTATUS
NTAPI
LdrQueryProcessModuleInformation(
    _Out_writes_bytes_to_(Size, *ReturnedSize) PRTL_PROCESS_MODULES ModuleInformation,
    _In_ ULONG Size,
    _Out_opt_ PULONG ReturnedSize
);

*/

#define MODULES_HEADER_SIZE FIELD_OFFSET(RTL_PROCESS_MODULES, Modules)

static
ULONG
ModulesUsedSize(
    _In_ ULONG NumberOfModules)
{
    return MODULES_HEADER_SIZE + NumberOfModules * sizeof(RTL_PROCESS_MODULE_INFORMATION);
}

static
BOOLEAN
CheckModuleList(
    _In_ PRTL_PROCESS_MODULES Modules,
    _In_ ULONG Size,
    _In_ PVOID ExecutableBase)
{
    PRTL_PROCESS_MODULE_INFORMATION Entry;
    BOOLEAN FoundExecutable = FALSE;
    ULONG Index;

    ok(Modules->NumberOfModules >= 1, "NumberOfModules = %lu\n", Modules->NumberOfModules);
    ok(ModulesUsedSize(Modules->NumberOfModules) <= Size,
       "NumberOfModules = %lu does not fit in %lu bytes\n", Modules->NumberOfModules, Size);
    if (ModulesUsedSize(Modules->NumberOfModules) > Size)
        return FALSE;

    for (Index = 0; Index < Modules->NumberOfModules; Index++)
    {
        Entry = &Modules->Modules[Index];
        ok(Entry->ImageBase != NULL, "Module %lu: ImageBase is NULL\n", Index);
        ok(Entry->ImageSize != 0, "Module %lu: ImageSize is 0\n", Index);
        ok(Entry->OffsetToFileName < sizeof(Entry->FullPathName),
           "Module %lu: OffsetToFileName = %u\n", Index, Entry->OffsetToFileName);
        if (Entry->OffsetToFileName < sizeof(Entry->FullPathName))
        {
            ok(Entry->FullPathName[Entry->OffsetToFileName] != '\0',
               "Module %lu: empty file name in '%s'\n", Index, Entry->FullPathName);
            trace("Module %lu: %p %08lx %s\n", Index, Entry->ImageBase, Entry->ImageSize,
                  &Entry->FullPathName[Entry->OffsetToFileName]);
        }
        if (Entry->ImageBase == ExecutableBase)
            FoundExecutable = TRUE;
    }

    ok(FoundExecutable, "Executable base %p is not reported\n", ExecutableBase);
    return TRUE;
}

START_TEST(LdrQueryProcessModuleInformation)
{
    NTSTATUS Status;
    ULONG RequiredSize, ReturnedSize;
    PRTL_PROCESS_MODULES Modules;
    PVOID ExecutableBase;

    ExecutableBase = GetModuleHandleW(NULL);
    ok(ExecutableBase != NULL, "GetModuleHandleW(NULL) failed with %lu\n", GetLastError());

    /* A size query without a buffer reports the required size */
    RequiredSize = 0;
    Status = LdrQueryProcessModuleInformation(NULL, 0, &RequiredSize);
    ok_ntstatus(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(RequiredSize >= ModulesUsedSize(1),
       "RequiredSize = %lu, expected at least %lu\n", RequiredSize, ModulesUsedSize(1));
    ok((RequiredSize - MODULES_HEADER_SIZE) % sizeof(RTL_PROCESS_MODULE_INFORMATION) == 0,
       "RequiredSize = %lu is not a whole number of modules\n", RequiredSize);
    if (RequiredSize < ModulesUsedSize(1))
        return;

    Modules = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize);
    ok(Modules != NULL, "HeapAlloc(%lu) failed\n", RequiredSize);
    if (Modules == NULL)
        return;

    /* A buffer of exactly the reported size succeeds and reports its use */
    ReturnedSize = 0;
    Status = LdrQueryProcessModuleInformation(Modules, RequiredSize, &ReturnedSize);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(ReturnedSize <= RequiredSize, "ReturnedSize = %lu > %lu\n", ReturnedSize, RequiredSize);
    ok(ReturnedSize == ModulesUsedSize(Modules->NumberOfModules),
       "ReturnedSize = %lu, NumberOfModules = %lu\n", ReturnedSize, Modules->NumberOfModules);
    if (NT_SUCCESS(Status))
        CheckModuleList(Modules, RequiredSize, ExecutableBase);

    /* Room for the header alone is not enough */
    ReturnedSize = 0;
    Status = LdrQueryProcessModuleInformation(Modules, MODULES_HEADER_SIZE, &ReturnedSize);
    ok_ntstatus(Status, STATUS_INFO_LENGTH_MISMATCH);
    ok(ReturnedSize >= ModulesUsedSize(1),
       "ReturnedSize = %lu, expected at least %lu\n", ReturnedSize, ModulesUsedSize(1));

    /* The returned size is optional */
    RtlZeroMemory(Modules, RequiredSize);
    Status = LdrQueryProcessModuleInformation(Modules, RequiredSize, NULL);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        CheckModuleList(Modules, RequiredSize, ExecutableBase);

    HeapFree(GetProcessHeap(), 0, Modules);
}
