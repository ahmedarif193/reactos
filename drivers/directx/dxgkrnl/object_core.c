/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Handle tables, capability staircase, node affinity and TDR policy
 */

#include "object_core.h"

/* --- handle table ----------------------------------------------------- */

/* A handle packs the slot index in the low bits and a generation above it, so
 * a reused slot never resolves through a handle minted for the old object. */
#define DXGK_HANDLE_INDEX_BITS  16
#define DXGK_HANDLE_INDEX_MASK  ((1UL << DXGK_HANDLE_INDEX_BITS) - 1)

static ULONG
DxgkHandleCoreCompose(
    _In_ ULONG Index,
    _In_ ULONG Generation)
{
    return (Generation << DXGK_HANDLE_INDEX_BITS) | (Index & DXGK_HANDLE_INDEX_MASK);
}

VOID
DxgkHandleCoreInitialize(
    _Out_ PDXGK_HANDLE_TABLE Table)
{
    RtlZeroMemory(Table, sizeof(*Table));
}

NTSTATUS
DxgkHandleCoreAllocate(
    _Inout_ PDXGK_HANDLE_TABLE Table,
    _In_ PVOID Object,
    _In_ DXGK_HANDLE_TYPE Type,
    _Out_ PULONG Handle)
{
    ULONG Index;

    *Handle = 0;
    if (Object == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Type == DxgkHandleTypeNone || Type >= DxgkHandleTypeMax)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < DXGK_HANDLE_CORE_MAX_ENTRIES; ++Index)
    {
        DXGK_HANDLE_ENTRY *Entry = &Table->Entries[Index];

        if (Entry->InUse)
            continue;
        Entry->Object = Object;
        Entry->Type = Type;
        Entry->InUse = TRUE;
        /* Generation starts at 1 so a composed handle is never zero, which
         * callers use to mean "no handle". */
        if (Entry->Generation == 0)
            Entry->Generation = 1;
        Table->LiveCount++;
        *Handle = DxgkHandleCoreCompose(Index, Entry->Generation);
        return STATUS_SUCCESS;
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

NTSTATUS
DxgkHandleCoreResolve(
    _In_ const DXGK_HANDLE_TABLE *Table,
    _In_ ULONG Handle,
    _In_ DXGK_HANDLE_TYPE ExpectedType,
    _Outptr_ PVOID *Object)
{
    const DXGK_HANDLE_ENTRY *Entry;
    ULONG Index;
    ULONG Generation;

    *Object = NULL;
    if (Handle == 0)
        return STATUS_INVALID_HANDLE;
    Index = Handle & DXGK_HANDLE_INDEX_MASK;
    Generation = Handle >> DXGK_HANDLE_INDEX_BITS;
    if (Index >= DXGK_HANDLE_CORE_MAX_ENTRIES)
        return STATUS_INVALID_HANDLE;
    Entry = &Table->Entries[Index];
    if (!Entry->InUse)
        return STATUS_INVALID_HANDLE;
    /* A stale handle carries the generation the slot had when it was minted. */
    if (Entry->Generation != Generation)
        return STATUS_INVALID_HANDLE;
    /* Type confusion is as dangerous as a stale slot: a context handle used
     * as an allocation handle would reinterpret the object. */
    if (Entry->Type != ExpectedType)
        return STATUS_OBJECT_TYPE_MISMATCH;
    *Object = Entry->Object;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkHandleCoreFree(
    _Inout_ PDXGK_HANDLE_TABLE Table,
    _In_ ULONG Handle,
    _In_ DXGK_HANDLE_TYPE ExpectedType)
{
    DXGK_HANDLE_ENTRY *Entry;
    PVOID Object;
    NTSTATUS Status;
    ULONG Index;

    Status = DxgkHandleCoreResolve(Table, Handle, ExpectedType, &Object);
    if (!NT_SUCCESS(Status))
        return Status;
    Index = Handle & DXGK_HANDLE_INDEX_MASK;
    Entry = &Table->Entries[Index];
    Entry->Object = NULL;
    Entry->Type = DxgkHandleTypeNone;
    Entry->InUse = FALSE;
    /* Bump on free so every handle minted for the old object stops resolving
     * immediately, skipping 0 on wrap. */
    Entry->Generation++;
    if (Entry->Generation == 0)
        Entry->Generation = 1;
    Table->LiveCount--;
    return STATUS_SUCCESS;
}

ULONG
DxgkHandleCoreLiveCount(
    _In_ const DXGK_HANDLE_TABLE *Table)
{
    return Table->LiveCount;
}

/* --- capability staircase --------------------------------------------- */

ULONG
DxgkCapsCoreReportedVersion(
    _In_ const DXGK_CAPS_INPUT *Input)
{
    ULONG Reported = Input->MiniportDeclaredLevel;

    if (Input->OsCompletedLevel != 0 && Input->OsCompletedLevel < Reported)
        Reported = Input->OsCompletedLevel;
    if (Input->ProviderCompletedLevel != 0 && Input->ProviderCompletedLevel < Reported)
        Reported = Input->ProviderCompletedLevel;
    if (Input->ConfiguredLevel != 0 && Input->ConfiguredLevel < Reported)
        Reported = Input->ConfiguredLevel;
    return Reported;
}

ULONG
DxgkCapsCoreInterfaceVersionToLevel(
    _In_ ULONG InterfaceVersion)
{
    /*
     * Do not accept a whole high-nibble family. The low bits identify a DDI
     * revision and may imply a different append-only table prefix. List every
     * selector for which we have WDK evidence, including both Windows 10
     * 10240's 0x5022 and later kits' 0x5023 WDDM 2.0 revisions.
     */
    switch (InterfaceVersion)
    {
        case 0x1052:
        case 0x1053:
            return DXGK_CAPS_CORE_LEVEL_WDDM_1_0;
        case 0x2005:
            return DXGK_CAPS_CORE_LEVEL_WDDM_1_1;
        case 0x300E:
            return DXGK_CAPS_CORE_LEVEL_WDDM_1_2;
        case 0x4002:
        case 0x4003:
            return DXGK_CAPS_CORE_LEVEL_WDDM_1_3;
        case 0x5022:
        case 0x5023:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_0;
        case 0x6003:
        case 0x6010:
        case 0x6011:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_1;
        case 0x700A:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_2;
        case 0x8001:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_3;
        case 0x9006:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_4;
        case 0xA00B:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_5;
        case 0xB004:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_6;
        case 0xC004:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_7;
        case 0xD001:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_8;
        case 0xE003:
            return DXGK_CAPS_CORE_LEVEL_WDDM_2_9;
        case 0xF003:
            return DXGK_CAPS_CORE_LEVEL_WDDM_3_0;
        case 0x10004:
            return DXGK_CAPS_CORE_LEVEL_WDDM_3_1;
        case 0x11007:
            return DXGK_CAPS_CORE_LEVEL_WDDM_3_2;
        default:
            return 0;
    }
}

BOOLEAN
DxgkCapsCoreInterfaceVersionAtLeast(
    _In_ ULONG InterfaceVersion,
    _In_ ULONG MinimumLevel)
{
    ULONG InterfaceLevel;

    InterfaceLevel = DxgkCapsCoreInterfaceVersionToLevel(InterfaceVersion);
    return MinimumLevel != 0 &&
           InterfaceLevel >= MinimumLevel;
}

BOOLEAN
DxgkCapsCoreInterfaceVersionInRange(
    _In_ ULONG InterfaceVersion,
    _In_ ULONG MinimumLevel,
    _In_ ULONG MaximumLevel)
{
    ULONG InterfaceLevel;

    if (MinimumLevel == 0 ||
        (MaximumLevel != 0 && MaximumLevel < MinimumLevel))
    {
        return FALSE;
    }

    InterfaceLevel = DxgkCapsCoreInterfaceVersionToLevel(InterfaceVersion);
    return InterfaceLevel >= MinimumLevel &&
           (MaximumLevel == 0 || InterfaceLevel <= MaximumLevel);
}

BOOLEAN
DxgkCapsCoreInterfaceVersionPermitted(
    _In_ ULONG InterfaceVersion,
    _In_ ULONG ConfiguredLevel)
{
    return DxgkCapsCoreInterfaceVersionInRange(
        InterfaceVersion,
        DXGK_CAPS_CORE_LEVEL_WDDM_1_0,
        ConfiguredLevel);
}

BOOLEAN
DxgkCapsCoreRenderSupported(
    _In_ const DXGK_CAPS_INPUT *Input)
{
    /* Render support is a statement about the callbacks a submission actually
     * travels through, not about the version a miniport declares. */
    if (Input->DisplayOnly)
        return FALSE;
    return Input->HasRenderCallbacks;
}

BOOLEAN
DxgkCapsCoreFeatureAvailable(
    _In_ const DXGK_CAPS_INPUT *Input,
    _In_ ULONG RequiredVersion)
{
    return DxgkCapsCoreFeatureAvailableInRange(Input, RequiredVersion, 0);
}

BOOLEAN
DxgkCapsCoreFeatureAvailableInRange(
    _In_ const DXGK_CAPS_INPUT *Input,
    _In_ ULONG MinimumVersion,
    _In_ ULONG MaximumVersion)
{
    ULONG Reported;

    if (MinimumVersion == 0 ||
        (MaximumVersion != 0 && MaximumVersion < MinimumVersion))
    {
        return FALSE;
    }

    Reported = DxgkCapsCoreReportedVersion(Input);
    return Reported >= MinimumVersion &&
           (MaximumVersion == 0 || Reported <= MaximumVersion);
}

/* --- node / engine affinity ------------------------------------------- */

NTSTATUS
DxgkNodeCoreValidateAffinity(
    _In_ ULONG AffinityMask,
    _In_ ULONG NodeCount)
{
    ULONG Valid;

    if (NodeCount == 0 || NodeCount > DXGK_NODE_CORE_MAX_NODES)
        return STATUS_INVALID_PARAMETER;
    /* An empty affinity steers the submission nowhere. */
    if (AffinityMask == 0)
        return STATUS_INVALID_PARAMETER;
    Valid = (NodeCount == 32) ? MAXULONG : ((1UL << NodeCount) - 1);
    /* Naming a node the adapter does not have would index past the engine
     * array the first time the mask is walked. */
    if ((AffinityMask & ~Valid) != 0)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkNodeCoreFirstNode(
    _In_ ULONG AffinityMask,
    _In_ ULONG NodeCount,
    _Out_ PULONG NodeOrdinal)
{
    ULONG Index;

    *NodeOrdinal = 0;
    if (!NT_SUCCESS(DxgkNodeCoreValidateAffinity(AffinityMask, NodeCount)))
        return FALSE;
    for (Index = 0; Index < NodeCount; ++Index)
    {
        if ((AffinityMask & (1UL << Index)) != 0)
        {
            *NodeOrdinal = Index;
            return TRUE;
        }
    }
    return FALSE;
}

ULONG
DxgkNodeCoreCountNodes(
    _In_ ULONG AffinityMask,
    _In_ ULONG NodeCount)
{
    ULONG Count = 0;
    ULONG Index;

    if (!NT_SUCCESS(DxgkNodeCoreValidateAffinity(AffinityMask, NodeCount)))
        return 0;
    for (Index = 0; Index < NodeCount; ++Index)
    {
        if ((AffinityMask & (1UL << Index)) != 0)
            Count++;
    }
    return Count;
}

/* --- TDR policy -------------------------------------------------------- */

NTSTATUS
DxgkTdrCoreInitialize(
    _Out_ PDXGK_TDR_STATE State,
    _In_ ULONG TimeoutMs,
    _In_ ULONG MaxConsecutiveResets)
{
    RtlZeroMemory(State, sizeof(*State));
    if (TimeoutMs == 0)
        return STATUS_INVALID_PARAMETER;
    State->TimeoutMs = TimeoutMs;
    State->MaxConsecutiveResets = MaxConsecutiveResets;
    return STATUS_SUCCESS;
}

VOID
DxgkTdrCoreBeginPacket(
    _Inout_ PDXGK_TDR_STATE State)
{
    State->PacketOutstanding = TRUE;
    State->ElapsedMs = 0;
}

VOID
DxgkTdrCoreCompletePacket(
    _Inout_ PDXGK_TDR_STATE State)
{
    State->PacketOutstanding = FALSE;
    State->ElapsedMs = 0;
    /* Forward progress clears the escalation ladder: resets only escalate
     * while the GPU keeps failing to finish anything. */
    State->ConsecutiveResets = 0;
}

DXGK_TDR_ACTION
DxgkTdrCoreTick(
    _Inout_ PDXGK_TDR_STATE State,
    _In_ ULONG DeltaMs)
{
    if (!State->PacketOutstanding)
        return DxgkTdrActionNone;
    if (State->ElapsedMs > MAXULONG - DeltaMs)
        State->ElapsedMs = MAXULONG;
    else
        State->ElapsedMs += DeltaMs;
    if (State->ElapsedMs < State->TimeoutMs)
        return DxgkTdrActionNone;

    /*
     * Escalate rather than jumping straight to the biggest hammer: try to
     * preempt, then reset the engine, then the adapter, and only remove it
     * once resetting has stopped helping.
     */
    if (State->ConsecutiveResets == 0)
        return DxgkTdrActionPreempt;
    if (State->ConsecutiveResets == 1)
        return DxgkTdrActionResetEngine;
    if (State->MaxConsecutiveResets != 0 && State->ConsecutiveResets >= State->MaxConsecutiveResets)
        return DxgkTdrActionRemoveAdapter;
    return DxgkTdrActionResetAdapter;
}

VOID
DxgkTdrCoreNoteResetOutcome(
    _Inout_ PDXGK_TDR_STATE State,
    _In_ BOOLEAN Succeeded)
{
    State->ElapsedMs = 0;
    if (Succeeded)
    {
        State->ConsecutiveResets++;
        return;
    }
    /* A reset that did not work counts double: the next tick must escalate
     * past the step that just failed rather than retrying it. */
    State->ConsecutiveResets += 2;
}

/* EOF */
