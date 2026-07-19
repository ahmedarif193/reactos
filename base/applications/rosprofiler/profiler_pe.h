/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Bounds-checked PE identity reader
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */
#pragma once

#include "profiler_model.h"

typedef struct _RPERF_PE_IDENTITY
{
    ULONG Architecture;
    ULONG TimeDateStamp;
    ULONG Checksum;
    ULONGLONG ImageSize;
    UCHAR DebugId[16];
    ULONG DebugAge;
    BOOL HasRosSym;
    WCHAR PdbPath[MAX_PATH];
} RPERF_PE_IDENTITY;

BOOL RperfReadPeIdentity(PCWSTR Path, RPERF_PE_IDENTITY *Identity);
BOOL RperfEnrichModuleFromImage(RPERF_MODULE *Module, PCWSTR Path);
