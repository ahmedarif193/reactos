/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Backend-independent symbol provider and bounded cache
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_model.h"

typedef struct _RPERF_SYMBOL_RESULT
{
    ULONGLONG FunctionAddress;
    RPERF_RESOLUTION_KIND Resolution;
    RPERF_SYMBOL_SOURCE_KIND Source;
    RPERF_SYMBOL_STATUS_KIND Status;
    CHAR Name[512];
    CHAR ModuleName[256];
    CHAR SourceFile[1024];
    ULONG SourceLine;
} RPERF_SYMBOL_RESULT;

typedef struct _RPERF_SYMBOLIZATION_SUMMARY
{
    ULONGLONG Attempted;
    ULONGLONG Pdb;
    ULONGLONG RosSym;
    ULONGLONG Dwarf;
    ULONGLONG Coff;
    ULONGLONG Export;
    ULONGLONG ModuleOffset;
    ULONGLONG ImageMissing;
    ULONGLONG IdentityMismatch;
    ULONGLONG SymbolsMissing;
    ULONGLONG LoadErrors;
} RPERF_SYMBOLIZATION_SUMMARY;

typedef struct _RPERF_SYMBOL_PROVIDER RPERF_SYMBOL_PROVIDER;

typedef struct _RPERF_SYMBOL_PROVIDER_OPS
{
    BOOL (*Resolve)(PVOID Context,
                    const RPERF_MODULE *Module,
                    ULONGLONG Address,
                    RPERF_SYMBOL_RESULT *Result);
    BOOL (*QuerySummary)(PVOID Context,
                         RPERF_SYMBOLIZATION_SUMMARY *Summary);
    VOID (*Destroy)(PVOID Context);
} RPERF_SYMBOL_PROVIDER_OPS;

struct _RPERF_SYMBOL_PROVIDER
{
    const RPERF_SYMBOL_PROVIDER_OPS *Ops;
    PVOID Context;
};

typedef VOID (CALLBACK *RPERF_SYMBOL_PROGRESS)(PVOID Context,
                                               SIZE_T Completed,
                                               SIZE_T Total);

RPERF_SYMBOL_PROVIDER *
RperfCreateCachedSymbolProvider(const RPERF_SYMBOL_PROVIDER_OPS *Ops,
                                PVOID Context,
                                SIZE_T MaximumEntries);
VOID RperfDestroySymbolProvider(RPERF_SYMBOL_PROVIDER *Provider);
BOOL RperfQuerySymbolProviderSummary(
    RPERF_SYMBOL_PROVIDER *Provider,
    RPERF_SYMBOLIZATION_SUMMARY *Summary);
BOOL RperfSymbolizeRecording(const RPERF_RECORDING *Source,
                             RPERF_SYMBOL_PROVIDER *Provider,
                             HANDLE CancelEvent,
                             RPERF_SYMBOL_PROGRESS Progress,
                             PVOID ProgressContext,
                             RPERF_RECORDING **Result);
