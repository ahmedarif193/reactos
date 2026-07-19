/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DbgHelp-backed native and portable symbol resolver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_symbolizer.h"

typedef struct _RPERF_DBGHELP_CONFIGURATION
{
    PCWSTR ImageSearchPath;
    PCWSTR SymbolSearchPath;
    BOOL AllowNetwork;
    SIZE_T MaximumCacheEntries;
} RPERF_DBGHELP_CONFIGURATION;

RPERF_SYMBOL_PROVIDER *
RperfCreateDbgHelpSymbolProvider(
    const RPERF_DBGHELP_CONFIGURATION *Configuration);
