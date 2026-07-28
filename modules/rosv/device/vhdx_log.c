/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     VHDX log replay for crash recovery
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements log replay as specified in [MS-VHDX] section 6.
 * If the VHDX header's LogGuid is non-null, the log region contains
 * uncommitted writes that must be replayed before the file is safe
 * to use. For the v1 implementation we refuse to open dirty logs
 * and require the user to run chkdsk or Hyper-V first.
 */

#include <rosv/rosv.h>
#include <rosv/vhdx.h>

/**
 * RosvVhdxLogIsClean - Check if the VHDX log is clean.
 *
 * @State: Parsed VHDX state (must have been initialized by RosvVhdxOpen).
 *
 * Returns TRUE if the LogGuid in the active header is GUID_NULL,
 * meaning no log replay is required. Returns FALSE if the log is
 * dirty and must be replayed before the VHDX can be safely accessed.
 */
BOOLEAN
RosvVhdxLogIsClean(
    _In_ PROSV_VHDX_STATE State)
{
    BOOLEAN Clean;

    Clean = RosvIsNullGuid(&State->LogGuid);

    if (Clean)
    {
        ROSV_TRACE("VHDX log: clean (LogGuid is NULL), no replay needed");
    }
    else
    {
        ROSV_ERR("VHDX log: DIRTY (LogGuid={%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X})",
                 State->LogGuid.Data1,
                 State->LogGuid.Data2,
                 State->LogGuid.Data3,
                 State->LogGuid.Data4[0], State->LogGuid.Data4[1],
                 State->LogGuid.Data4[2], State->LogGuid.Data4[3],
                 State->LogGuid.Data4[4], State->LogGuid.Data4[5],
                 State->LogGuid.Data4[6], State->LogGuid.Data4[7]);
    }

    return Clean;
}

/**
 * RosvVhdxLogReplay - Replay the VHDX log for crash recovery.
 *
 * @State: Parsed VHDX state with FileBase pointing to the mapped file.
 *
 * If the log is clean (LogGuid == GUID_NULL), returns STATUS_SUCCESS
 * immediately with no side effects.
 *
 * If the log is dirty, this v1 implementation returns
 * STATUS_MEDIA_WRITE_PROTECTED rather than attempting replay.
 * The user must repair the VHDX externally (chkdsk, Hyper-V, etc.)
 * before it can be used by ROSV. This is the safe approach to avoid
 * data corruption from an incorrect replay implementation.
 *
 * Returns STATUS_SUCCESS if the log is clean.
 * Returns STATUS_MEDIA_WRITE_PROTECTED if the log is dirty.
 */
NTSTATUS
RosvVhdxLogReplay(
    _Inout_ PROSV_VHDX_STATE State)
{
    /* Fast path: if the log is clean, nothing to do */
    if (RosvVhdxLogIsClean(State))
    {
        ROSV_TRACE("VHDX log replay: log is clean, skipping replay");
        return STATUS_SUCCESS;
    }

    /*
     * The log is dirty -- crash recovery data exists that must be
     * replayed before the BAT and metadata regions can be trusted.
     *
     * Log region: offset=0x%llX, length=0x%X
     *
     * A full replay would walk the log from tail to head (wrapping
     * within the log region), verify each VHDX_LOG_ENTRY_HEADER
     * (signature "loge", matching LogGuid, CRC-32C checksum), then
     * process descriptors:
     *   - "desc": copy data sector payload to FileOffset
     *   - "zero": zero out range at FileOffset
     * After replay: clear LogGuid to GUID_NULL, update header checksum,
     * increment sequence number, write to the inactive header slot.
     *
     * For v1 we take the safe route: refuse to open the file.
     */
    ROSV_ERR("VHDX log replay: log is DIRTY (LogOffset=0x%llX, LogLength=0x%X)",
             State->LogOffset, State->LogLength);
    ROSV_ERR("VHDX log is dirty (needs replay). "
             "Please run chkdsk or open in Hyper-V first.");

    State->NeedsLogReplay = TRUE;

    return STATUS_MEDIA_WRITE_PROTECTED;
}
