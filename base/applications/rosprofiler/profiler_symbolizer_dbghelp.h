/* DbgHelp-backed PDB, embedded rossym, DWARF and export resolver. */
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
