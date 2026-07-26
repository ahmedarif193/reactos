/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     C interface for LFS class
 * COPYRIGHT:   Copyright 2026 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

ULONG
NtfsLogFileServiceGetClientMajorVersion(
    _In_ PNtfsLogFileService LFS)
{
    return reinterpret_cast<PLogFileService>(LFS)->ClientMajorVersion;
}

ULONG
NtfsLogFileServiceGetClientMinorVersion(
    _In_ PNtfsLogFileService LFS)
{
    return reinterpret_cast<PLogFileService>(LFS)->ClientMinorVersion;
}

#ifdef __cplusplus
}
#endif