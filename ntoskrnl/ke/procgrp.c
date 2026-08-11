/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Processor group, CPU set, node and hetero-policy topology routines
 * COPYRIGHT:   Copyright 2026 ReactOS Portable Systems Group
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE TYPES *************************************************************/

/* Homogeneous scheduling policy. Public headers only forward-declare the type
 * on the platforms that expose it; on a non-heterogeneous system only the
 * "all classes" policy is meaningful. */
typedef enum _KHETERO_CPU_POLICY
{
    KHeteroCpuPolicyAll = 0,
    KHeteroCpuPolicyLarge,
    KHeteroCpuPolicyLargeOrIdle,
    KHeteroCpuPolicySmall,
    KHeteroCpuPolicySmallOrIdle,
    KHeteroCpuPolicyDynamic,
    KHeteroCpuPolicyStaticMax,
    KHeteroCpuPolicyBiasedSmall,
    KHeteroCpuPolicyBiasedLarge,
    KHeteroCpuPolicyDefault,
    KHeteroCpuPolicyMax
} KHETERO_CPU_POLICY;

/* KeSetTimer2 and friends are internal exports not declared by the DDK. */
NTKERNELAPI USHORT NTAPI KeQueryNodeActiveProcessorCount(_In_ USHORT NodeNumber);
NTKERNELAPI PKPRCB NTAPI KeQueryPrcbAddress(_In_ ULONG Number);
NTKERNELAPI ULONG NTAPI KeQueryActiveProcessorAffinity(_Out_ PKAFFINITY_EX Affinity);
NTKERNELAPI NTSTATUS NTAPI KeQueryActiveProcessorAffinity2(_Out_ PGROUP_AFFINITY GroupAffinities, _Inout_ PUSHORT Count);
NTKERNELAPI NTSTATUS NTAPI KeQueryNodeActiveAffinity2(_In_ USHORT NodeNumber, _Out_opt_ PGROUP_AFFINITY GroupAffinities, _Inout_ PUSHORT Count);
NTKERNELAPI NTSTATUS NTAPI KeSetSelectedCpuSetsThread(_Inout_ PKTHREAD Thread, _In_ ULONG CpuSetCount, _In_reads_(CpuSetCount) PULONG64 CpuSetMasks);
NTKERNELAPI KHETERO_CPU_POLICY NTAPI KeQueryHeteroCpuPolicyThread(_In_ PKTHREAD Thread, _In_ LOGICAL UserPolicy);
NTKERNELAPI KHETERO_CPU_POLICY NTAPI KeSetHeteroCpuPolicyThread(_Inout_ PKTHREAD Thread, _In_ KHETERO_CPU_POLICY Policy, _In_ LOGICAL Reset);
#if (NTDDI_VERSION >= NTDDI_WINBLUE)
NTKERNELAPI BOOLEAN NTAPI KeSetTimer2(_Inout_ PKTIMER Timer, _In_ LARGE_INTEGER DueTime, _In_ LONGLONG Period, _In_opt_ PEXT_SET_PARAMETERS Parameters);
#endif
NTKERNELAPI ULONG64 NTAPI KeQueryUnbiasedInterruptTimePrecise(_Out_ PULONG64 QpcTimeStamp);
NTKERNELAPI NTSTATUS NTAPI KeQueryAuxiliaryCounterFrequency(_Out_opt_ PULONG64 AuxiliaryCounterFrequency);

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
USHORT
NTAPI
KeQueryActiveGroupCount(VOID)
{
    /* ReactOS models a single processor group. */
    return 1;
}

/*
 * @implemented
 */
USHORT
NTAPI
KeQueryMaximumGroupCount(VOID)
{
    return 1;
}

/*
 * @implemented
 */
ULONG
NTAPI
KeGetProcessorIndexFromNumber(
    _In_ PPROCESSOR_NUMBER ProcessorNumber)
{
    if ((ProcessorNumber->Reserved != 0) ||
        (ProcessorNumber->Group != 0) ||
        (ProcessorNumber->Number >= (UCHAR)(sizeof(KAFFINITY) * 8)) ||
        (ProcessorNumber->Number >= (UCHAR)KeNumberProcessors))
    {
        return INVALID_PROCESSOR_INDEX;
    }

    return ProcessorNumber->Number;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeGetProcessorNumberFromIndex(
    _In_ ULONG ProcessorIndex,
    _Out_ PPROCESSOR_NUMBER ProcessorNumber)
{
    if (ProcessorIndex >= (ULONG)KeNumberProcessors)
        return STATUS_INVALID_PARAMETER;

    ProcessorNumber->Group = 0;
    ProcessorNumber->Number = (UCHAR)ProcessorIndex;
    ProcessorNumber->Reserved = 0;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
SIZE_T
NTAPI
KeSizeOfAffinityEx(
    _In_ USHORT Count)
{
    return FIELD_OFFSET(KAFFINITY_EX, Bitmap) + ((SIZE_T)Count * sizeof(KAFFINITY));
}

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeAffinityEx(
    _Out_ PKAFFINITY_EX Affinity)
{
    Affinity->Count = 1;
    Affinity->Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    Affinity->Reserved = 0;
    RtlZeroMemory(Affinity->Bitmap, Affinity->Size * sizeof(Affinity->Bitmap[0]));
}

/*
 * @implemented
 */
VOID
NTAPI
KeReinitializeAffinityEx(
    _Inout_ PKAFFINITY_EX Affinity)
{
    RtlZeroMemory(Affinity->Bitmap, Affinity->Count * sizeof(Affinity->Bitmap[0]));
    Affinity->Count = 1;
}

/*
 * @implemented
 */
VOID
NTAPI
KeAddGroupAffinityEx(
    _Inout_ PKAFFINITY_EX Affinity,
    _In_ USHORT GroupNumber,
    _In_ KAFFINITY ProcessorMask)
{
    if (GroupNumber >= Affinity->Size)
        return;

    if (GroupNumber >= Affinity->Count)
        Affinity->Count = GroupNumber + 1;

    Affinity->Bitmap[GroupNumber] |= ProcessorMask;
}

/*
 * @implemented
 */
VOID
NTAPI
KeRemoveGroupAffinityEx(
    _Inout_ PKAFFINITY_EX Affinity,
    _In_ USHORT GroupNumber,
    _In_ KAFFINITY ProcessorMask)
{
    if (GroupNumber < Affinity->Count)
        Affinity->Bitmap[GroupNumber] &= ~ProcessorMask;
}

/*
 * @implemented
 */
VOID
NTAPI
KeAddProcessorAffinityEx(
    _Inout_ PKAFFINITY_EX Affinity,
    _In_ ULONG ProcessorIndex)
{
    ULONG GroupNumber = ProcessorIndex / (sizeof(KAFFINITY) * 8);
    ULONG Number = ProcessorIndex % (sizeof(KAFFINITY) * 8);

    KeAddGroupAffinityEx(Affinity, (USHORT)GroupNumber, (KAFFINITY)1 << Number);
}

/*
 * @implemented
 */
VOID
NTAPI
KeRemoveProcessorAffinityEx(
    _Inout_ PKAFFINITY_EX Affinity,
    _In_ ULONG ProcessorIndex)
{
    ULONG GroupNumber = ProcessorIndex / (sizeof(KAFFINITY) * 8);
    ULONG Number = ProcessorIndex % (sizeof(KAFFINITY) * 8);

    KeRemoveGroupAffinityEx(Affinity, (USHORT)GroupNumber, (KAFFINITY)1 << Number);
}

/*
 * @implemented
 */
LOGICAL
NTAPI
KeCheckProcessorAffinityEx(
    _In_ PKAFFINITY_EX Affinity,
    _In_ ULONG ProcessorIndex)
{
    ULONG GroupNumber = ProcessorIndex / (sizeof(KAFFINITY) * 8);
    ULONG Number = ProcessorIndex % (sizeof(KAFFINITY) * 8);

    if (GroupNumber >= Affinity->Count)
        return FALSE;

    return (Affinity->Bitmap[GroupNumber] & ((KAFFINITY)1 << Number)) != 0;
}

/*
 * @implemented
 */
LOGICAL
NTAPI
KeIsEmptyAffinityEx(
    _In_ PKAFFINITY_EX Affinity)
{
    USHORT GroupNumber;

    for (GroupNumber = 0; GroupNumber < Affinity->Count; GroupNumber++)
    {
        if (Affinity->Bitmap[GroupNumber] != 0)
            return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
ULONG
NTAPI
KeCountSetBitsAffinityEx(
    _In_ PKAFFINITY_EX Affinity)
{
    KAFFINITY ProcessorMask;
    ULONG BitCount = 0;
    USHORT GroupNumber;

    for (GroupNumber = 0; GroupNumber < Affinity->Count; GroupNumber++)
    {
        ProcessorMask = Affinity->Bitmap[GroupNumber];
        while (ProcessorMask != 0)
        {
            ProcessorMask &= ProcessorMask - 1;
            BitCount++;
        }
    }

    return BitCount;
}

/*
 * @implemented
 */
ULONG
NTAPI
KeFindFirstSetLeftAffinityEx(
    _In_ PKAFFINITY_EX Affinity)
{
    PROCESSOR_NUMBER ProcessorNumber;
    ULONG BitNumber;
    USHORT GroupNumber;

    for (GroupNumber = Affinity->Count; GroupNumber != 0;)
    {
        GroupNumber--;
        if (BitScanReverseAffinity(&BitNumber, Affinity->Bitmap[GroupNumber]))
        {
            ProcessorNumber.Group = GroupNumber;
            ProcessorNumber.Number = (UCHAR)BitNumber;
            ProcessorNumber.Reserved = 0;
            return KeGetProcessorIndexFromNumber(&ProcessorNumber);
        }
    }

    return INVALID_PROCESSOR_INDEX;
}

/*
 * @implemented
 */
LOGICAL
NTAPI
KeAndAffinityEx(
    _In_ PKAFFINITY_EX Affinity1,
    _In_ PKAFFINITY_EX Affinity2,
    _Out_opt_ PKAFFINITY_EX Result)
{
    KAFFINITY ProcessorMask;
    LOGICAL NonEmpty = FALSE;
    USHORT Count;
    USHORT GroupNumber;

    Count = min(Affinity1->Count, Affinity2->Count);

    if (Result == NULL)
    {
        for (GroupNumber = 0; GroupNumber < Count; GroupNumber++)
        {
            if ((Affinity1->Bitmap[GroupNumber] & Affinity2->Bitmap[GroupNumber]) != 0)
                return TRUE;
        }

        return FALSE;
    }

    if (Count > KAFFINITY_EX_INITIALIZED_GROUPS)
        Count = KAFFINITY_EX_INITIALIZED_GROUPS;

    Result->Count = Count;
    Result->Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    Result->Reserved = 0;

    for (GroupNumber = 0; GroupNumber < Count; GroupNumber++)
    {
        ProcessorMask = Affinity1->Bitmap[GroupNumber] & Affinity2->Bitmap[GroupNumber];
        Result->Bitmap[GroupNumber] = ProcessorMask;
        if (ProcessorMask != 0)
            NonEmpty = TRUE;
    }

    for (; GroupNumber < KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
        Result->Bitmap[GroupNumber] = 0;

    return NonEmpty;
}

/*
 * @implemented
 */
VOID
NTAPI
KeCopyAffinityEx(
    _Out_ PKAFFINITY_EX Destination,
    _In_ PKAFFINITY_EX Source)
{
    USHORT Count = Source->Count;
    USHORT GroupNumber;

    if (Count > KAFFINITY_EX_INITIALIZED_GROUPS)
        Count = KAFFINITY_EX_INITIALIZED_GROUPS;

    Destination->Reserved = 0;
    Destination->Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    Destination->Count = Count;

    for (GroupNumber = 0; GroupNumber < Count; GroupNumber++)
        Destination->Bitmap[GroupNumber] = Source->Bitmap[GroupNumber];

    for (; GroupNumber < KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
        Destination->Bitmap[GroupNumber] = 0;
}

/*
 * @implemented
 */
LOGICAL
NTAPI
KeIsEqualAffinityEx(
    _In_ PKAFFINITY_EX Affinity1,
    _In_ PKAFFINITY_EX Affinity2)
{
    PKAFFINITY_EX LargerAffinity;
    USHORT CommonCount;
    USHORT GroupNumber;

    if (Affinity1->Count >= Affinity2->Count)
    {
        CommonCount = Affinity2->Count;
        LargerAffinity = Affinity1;
    }
    else
    {
        CommonCount = Affinity1->Count;
        LargerAffinity = Affinity2;
    }

    for (GroupNumber = 0; GroupNumber < CommonCount; GroupNumber++)
    {
        if (Affinity1->Bitmap[GroupNumber] != Affinity2->Bitmap[GroupNumber])
            return FALSE;
    }

    for (; GroupNumber < LargerAffinity->Count; GroupNumber++)
    {
        if (LargerAffinity->Bitmap[GroupNumber] != 0)
            return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
LOGICAL
NTAPI
KeIsSingleGroupAffinityEx(
    _In_ PKAFFINITY_EX Affinity,
    _Out_opt_ PUSHORT Group)
{
    USHORT LocalGroup;
    USHORT GroupNumber;

    if (Group == NULL)
        Group = &LocalGroup;

    *Group = KAFFINITY_EX_STATIC_GROUPS;

    for (GroupNumber = 0; GroupNumber < Affinity->Count; GroupNumber++)
    {
        if (Affinity->Bitmap[GroupNumber] == 0)
            continue;

        if (*Group != KAFFINITY_EX_STATIC_GROUPS)
            return FALSE;

        *Group = GroupNumber;
    }

    return *Group != KAFFINITY_EX_STATIC_GROUPS;
}

/*
 * @implemented
 */
LOGICAL
NTAPI
KeIsSubsetAffinityEx(
    _In_ PKAFFINITY_EX Affinity1,
    _In_ PKAFFINITY_EX Affinity2)
{
    USHORT CommonCount;
    USHORT GroupNumber;

    CommonCount = min(Affinity1->Count, Affinity2->Count);

    for (GroupNumber = 0; GroupNumber < CommonCount; GroupNumber++)
    {
        if ((Affinity1->Bitmap[GroupNumber] & ~Affinity2->Bitmap[GroupNumber]) != 0)
            return FALSE;
    }

    for (; GroupNumber < Affinity1->Count; GroupNumber++)
    {
        if (Affinity1->Bitmap[GroupNumber] != 0)
            return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
LOGICAL
NTAPI
KeOrAffinityEx(
    _In_ PKAFFINITY_EX Affinity1,
    _In_ PKAFFINITY_EX Affinity2,
    _Out_opt_ PKAFFINITY_EX Result)
{
    PKAFFINITY_EX LargerAffinity;
    KAFFINITY ProcessorMask;
    LOGICAL NonEmpty = FALSE;
    USHORT CommonCount;
    USHORT Count;
    USHORT GroupNumber;

    if (Result == NULL)
    {
        for (GroupNumber = 0; GroupNumber < Affinity1->Count; GroupNumber++)
        {
            if (Affinity1->Bitmap[GroupNumber] != 0)
                return TRUE;
        }

        for (GroupNumber = 0; GroupNumber < Affinity2->Count; GroupNumber++)
        {
            if (Affinity2->Bitmap[GroupNumber] != 0)
                return TRUE;
        }

        return FALSE;
    }

    if (Affinity1->Count >= Affinity2->Count)
    {
        Count = Affinity1->Count;
        CommonCount = Affinity2->Count;
        LargerAffinity = Affinity1;
    }
    else
    {
        Count = Affinity2->Count;
        CommonCount = Affinity1->Count;
        LargerAffinity = Affinity2;
    }

    if (Count > KAFFINITY_EX_INITIALIZED_GROUPS)
        Count = KAFFINITY_EX_INITIALIZED_GROUPS;
    if (CommonCount > Count)
        CommonCount = Count;

    Result->Count = Count;
    Result->Size = KAFFINITY_EX_INITIALIZED_GROUPS;
    Result->Reserved = 0;

    for (GroupNumber = 0; GroupNumber < CommonCount; GroupNumber++)
    {
        ProcessorMask = Affinity1->Bitmap[GroupNumber] | Affinity2->Bitmap[GroupNumber];
        Result->Bitmap[GroupNumber] = ProcessorMask;
        if (ProcessorMask != 0)
            NonEmpty = TRUE;
    }

    for (; GroupNumber < Count; GroupNumber++)
    {
        ProcessorMask = LargerAffinity->Bitmap[GroupNumber];
        Result->Bitmap[GroupNumber] = ProcessorMask;
        if (ProcessorMask != 0)
            NonEmpty = TRUE;
    }

    for (; GroupNumber < KAFFINITY_EX_INITIALIZED_GROUPS; GroupNumber++)
        Result->Bitmap[GroupNumber] = 0;

    return NonEmpty;
}

/*
 * @implemented
 */
KAFFINITY
NTAPI
KeQueryGroupAffinity(
    _In_ USHORT GroupNumber)
{
    if (GroupNumber != 0)
        return 0;

    return KeActiveProcessors;
}

/*
 * @implemented
 */
ULONG
NTAPI
KeQueryActiveProcessorAffinity(
    _Out_ PKAFFINITY_EX Affinity)
{
    KeInitializeAffinityEx(Affinity);
    Affinity->Bitmap[0] = KeActiveProcessors;

    return KeQueryActiveProcessorCount(NULL);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeQueryActiveProcessorAffinity2(
    _Out_ PGROUP_AFFINITY GroupAffinities,
    _Inout_ PUSHORT Count)
{
    if (Count == NULL)
        return STATUS_INVALID_PARAMETER;

    if ((GroupAffinities == NULL) || (*Count < 1))
    {
        *Count = 1;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(&GroupAffinities[0], sizeof(GROUP_AFFINITY));
    GroupAffinities[0].Mask = KeActiveProcessors;
    GroupAffinities[0].Group = 0;
    *Count = 1;

    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
KAFFINITY
NTAPI
KeQueryGroupAffinityEx(
    _In_ PKAFFINITY_EX Affinity,
    _In_ USHORT GroupNumber)
{
    if (GroupNumber >= Affinity->Count)
        return 0;

    return Affinity->Bitmap[GroupNumber];
}

/*
 * @implemented
 */
VOID
NTAPI
KeQueryNodeActiveAffinity(
    _In_ USHORT NodeNumber,
    _Out_opt_ PGROUP_AFFINITY Affinity,
    _Out_opt_ PUSHORT Count)
{
    /* Node 0 owns every active processor; other nodes are empty. */
    if (Affinity != NULL)
    {
        RtlZeroMemory(Affinity, sizeof(*Affinity));
        if (NodeNumber == 0)
        {
            Affinity->Mask = KeActiveProcessors;
            Affinity->Group = 0;
        }
    }

    if (Count != NULL)
        *Count = (NodeNumber == 0) ? (USHORT)KeQueryActiveProcessorCount(NULL) : 0;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeQueryNodeActiveAffinity2(
    _In_ USHORT NodeNumber,
    _Out_opt_ PGROUP_AFFINITY GroupAffinities,
    _Inout_ PUSHORT Count)
{
    if (Count == NULL)
        return STATUS_INVALID_PARAMETER;

    if (NodeNumber != 0)
    {
        *Count = 0;
        return STATUS_SUCCESS;
    }

    /* Node 0 owns every active processor, i.e. the system affinity */
    return KeQueryActiveProcessorAffinity2(GroupAffinities, Count);
}

/*
 * @implemented
 */
USHORT
NTAPI
KeQueryNodeActiveProcessorCount(
    _In_ USHORT NodeNumber)
{
    if (NodeNumber != 0)
        return 0;

    return (USHORT)KeQueryActiveProcessorCount(NULL);
}

/*
 * @implemented
 */
USHORT
NTAPI
KeQueryNodeMaximumProcessorCount(
    _In_ USHORT NodeNumber)
{
    if (NodeNumber != 0)
        return 0;

    return (USHORT)KeQueryMaximumProcessorCount();
}

/*
 * @implemented
 */
PKPRCB
NTAPI
KeQueryPrcbAddress(
    _In_ ULONG Number)
{
    if (Number >= (ULONG)KeNumberProcessors)
        return NULL;

    return KiProcessorBlock[Number];
}

/*
 * @implemented
 *
 * Builds the SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX records describing the
 * single-group / single-node topology, honoring the length-probe protocol.
 */
NTSTATUS
NTAPI
KeQueryLogicalProcessorRelationship(
    _In_opt_ PPROCESSOR_NUMBER ProcessorNumber,
    _In_ LOGICAL_PROCESSOR_RELATIONSHIP RelationshipType,
    _Out_writes_bytes_opt_(*Length) PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Information,
    _Inout_ PULONG Length)
{
    const ULONG CoreSize = FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor.GroupMask) + sizeof(GROUP_AFFINITY);
    const ULONG NumaSize = FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, NumaNode) + sizeof(NUMA_NODE_RELATIONSHIP);
    const ULONG GroupSize = FIELD_OFFSET(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Group.GroupInfo) + sizeof(PROCESSOR_GROUP_INFO);
    KAFFINITY ActiveMask = KeActiveProcessors;
    ULONG ActiveCount = KeQueryActiveProcessorCount(NULL);
    ULONG MaximumCount = KeQueryMaximumProcessorCount();
    KAFFINITY CoreMask = ActiveMask;
    ULONG CoreCount = ActiveCount;
    ULONG RequiredLength = 0;
    ULONG Offset = 0;
    PUCHAR Buffer = (PUCHAR)Information;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Record;
    ULONG Index;

    if (Length == NULL)
        return STATUS_INVALID_PARAMETER;

    /* An explicit processor restricts the enumerated cores to that processor. */
    if (ProcessorNumber != NULL)
    {
        if ((ProcessorNumber->Group != 0) ||
            (ProcessorNumber->Number >= (8 * sizeof(KAFFINITY))) ||
            !((ActiveMask >> ProcessorNumber->Number) & 1))
        {
            return STATUS_INVALID_PARAMETER;
        }

        CoreMask = (KAFFINITY)1 << ProcessorNumber->Number;
        CoreCount = 1;
    }

    /* Compute the required byte count for the requested relationship(s). */
    if ((RelationshipType == RelationProcessorCore) || (RelationshipType == RelationAll))
        RequiredLength += CoreSize * CoreCount;
    if ((RelationshipType == RelationProcessorPackage) || (RelationshipType == RelationAll))
        RequiredLength += CoreSize;
    if ((RelationshipType == RelationNumaNode) || (RelationshipType == RelationAll))
        RequiredLength += NumaSize;
    if ((RelationshipType == RelationGroup) || (RelationshipType == RelationAll))
        RequiredLength += GroupSize;

    if ((Information == NULL) || (*Length < RequiredLength))
    {
        *Length = RequiredLength;
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    /* Processor cores: one record per active core in the enumerated set. */
    if ((RelationshipType == RelationProcessorCore) || (RelationshipType == RelationAll))
    {
        for (Index = 0; Index < (8 * sizeof(KAFFINITY)); Index++)
        {
            if (!((CoreMask >> Index) & 1))
                continue;

            Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Buffer + Offset);
            RtlZeroMemory(Record, CoreSize);
            Record->Relationship = RelationProcessorCore;
            Record->Size = CoreSize;
            Record->Processor.Flags = 0;
            Record->Processor.EfficiencyClass = 0;
            Record->Processor.GroupCount = 1;
            Record->Processor.GroupMask[0].Mask = (KAFFINITY)1 << Index;
            Record->Processor.GroupMask[0].Group = 0;
            Offset += CoreSize;
        }
    }

    /* Single physical package spanning all active processors. */
    if ((RelationshipType == RelationProcessorPackage) || (RelationshipType == RelationAll))
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Buffer + Offset);
        RtlZeroMemory(Record, CoreSize);
        Record->Relationship = RelationProcessorPackage;
        Record->Size = CoreSize;
        Record->Processor.Flags = 0;
        Record->Processor.EfficiencyClass = 0;
        Record->Processor.GroupCount = 1;
        Record->Processor.GroupMask[0].Mask = ActiveMask;
        Record->Processor.GroupMask[0].Group = 0;
        Offset += CoreSize;
    }

    /* Single NUMA node (node 0). */
    if ((RelationshipType == RelationNumaNode) || (RelationshipType == RelationAll))
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Buffer + Offset);
        RtlZeroMemory(Record, NumaSize);
        Record->Relationship = RelationNumaNode;
        Record->Size = NumaSize;
        Record->NumaNode.NodeNumber = 0;
        Record->NumaNode.GroupMask.Mask = ActiveMask;
        Record->NumaNode.GroupMask.Group = 0;
        Offset += NumaSize;
    }

    /* Single processor group (group 0). */
    if ((RelationshipType == RelationGroup) || (RelationshipType == RelationAll))
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Buffer + Offset);
        RtlZeroMemory(Record, GroupSize);
        Record->Relationship = RelationGroup;
        Record->Size = GroupSize;
        Record->Group.MaximumGroupCount = 1;
        Record->Group.ActiveGroupCount = 1;
        Record->Group.GroupInfo[0].MaximumProcessorCount = (UCHAR)MaximumCount;
        Record->Group.GroupInfo[0].ActiveProcessorCount = (UCHAR)ActiveCount;
        Record->Group.GroupInfo[0].ActiveProcessorMask = ActiveMask;
        Offset += GroupSize;
    }

    *Length = Offset;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetSystemGroupAffinityThread(
    _In_ PGROUP_AFFINITY Affinity,
    _Out_opt_ PGROUP_AFFINITY PreviousAffinity)
{
    KAFFINITY OldAffinity;

    /* Only group 0 exists; apply the mask via the existing affinity path. */
    OldAffinity = KeSetSystemAffinityThreadEx(Affinity->Mask);

    if (PreviousAffinity != NULL)
    {
        RtlZeroMemory(PreviousAffinity, sizeof(*PreviousAffinity));
        PreviousAffinity->Mask = OldAffinity;
        PreviousAffinity->Group = 0;
    }
}

/*
 * @implemented
 */
VOID
NTAPI
KeRevertToUserGroupAffinityThread(
    _In_ PGROUP_AFFINITY PreviousAffinity)
{
    KeRevertToUserAffinityThreadEx(PreviousAffinity->Mask);
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeSetSelectedCpuSetsThread(
    _Inout_ PKTHREAD Thread,
    _In_ ULONG CpuSetCount,
    _In_reads_(CpuSetCount) PULONG64 CpuSetMasks)
{
    KAFFINITY Active = KeActiveProcessors;
    ULONG i;

    if (Thread == NULL)
        return STATUS_INVALID_PARAMETER;

    /* An empty selection clears any restriction, which is the honest default. */
    if (CpuSetCount == 0)
        return STATUS_SUCCESS;

    if (CpuSetMasks == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Only group 0 exists, so no other group may carry a selection. */
    for (i = 1; i < CpuSetCount; i++)
    {
        if (CpuSetMasks[i] != 0)
            return STATUS_NOT_SUPPORTED;
    }

    /* A selection covering every active processor imposes no real restriction.
     * A proper subset would require per-thread CPU-set state that cannot be
     * stored without changing the KTHREAD layout, so it is honestly rejected. */
    if ((CpuSetMasks[0] & Active) == Active)
        return STATUS_SUCCESS;

    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
KHETERO_CPU_POLICY
NTAPI
KeQueryHeteroCpuPolicyThread(
    _In_ PKTHREAD Thread,
    _In_ LOGICAL UserPolicy)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(UserPolicy);

    /* Homogeneous hardware: only the "all classes" policy is meaningful. */
    return KHeteroCpuPolicyAll;
}

/*
 * @implemented
 */
KHETERO_CPU_POLICY
NTAPI
KeSetHeteroCpuPolicyThread(
    _Inout_ PKTHREAD Thread,
    _In_ KHETERO_CPU_POLICY Policy,
    _In_ LOGICAL Reset)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Policy);
    UNREFERENCED_PARAMETER(Reset);

    /* No scheduling classes to bias; report the effective policy. */
    return KHeteroCpuPolicyAll;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeSetTargetProcessorDpcEx(
    _Inout_ PKDPC Dpc,
    _In_ PPROCESSOR_NUMBER ProcNumber)
{
    if ((ProcNumber->Group != 0) ||
        (ProcNumber->Number >= (UCHAR)KeNumberProcessors))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeSetTargetProcessorDpc(Dpc, (CCHAR)ProcNumber->Number);
    return STATUS_SUCCESS;
}

#if (NTDDI_VERSION >= NTDDI_WINBLUE)

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeSetTimer2(
    _Inout_ PKTIMER Timer,
    _In_ LARGE_INTEGER DueTime,
    _In_ LONGLONG Period,
    _In_opt_ PEXT_SET_PARAMETERS Parameters)
{
    /* Coalescing tolerance / no-wake hints are advisory and ignored here. */
    UNREFERENCED_PARAMETER(Parameters);

    return KeSetTimerEx(Timer, DueTime, KiExtTimerPeriodToMilliseconds(Period), NULL);
}

#endif /* NTDDI_VERSION >= NTDDI_WINBLUE */

/*
 * @implemented
 */
ULONG64
NTAPI
KeQueryTotalCycleTimeProcess(
    _Inout_ PKPROCESS Process,
    _Out_ PULONG64 CycleTimeStamp)
{
    if (CycleTimeStamp != NULL)
        *CycleTimeStamp = (ULONG64)KeQueryPerformanceCounter(NULL).QuadPart;

    return (ULONG64)InterlockedCompareExchange64((PLONG64)&Process->CycleTime, 0, 0);
}

/*
 * @implemented
 */
ULONG64
NTAPI
KeQueryTotalCycleTimeThread(
    _Inout_ PKTHREAD Thread,
    _Out_ PULONG64 CycleTimeStamp)
{
    if (CycleTimeStamp != NULL)
        *CycleTimeStamp = (ULONG64)KeQueryPerformanceCounter(NULL).QuadPart;

    return Thread->CycleTime;
}

/*
 * @implemented
 */
ULONGLONG
NTAPI
KeQueryUnbiasedInterruptTime(VOID)
{
    /* Without connected-standby bias tracking, unbiased == interrupt time. */
    return KeQueryInterruptTime();
}

/*
 * @implemented
 */
ULONG64
NTAPI
KeQueryUnbiasedInterruptTimePrecise(
    _Out_ PULONG64 QpcTimeStamp)
{
    *QpcTimeStamp = (ULONG64)KeQueryPerformanceCounter(NULL).QuadPart;
    return KeQueryInterruptTime();
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeQueryAuxiliaryCounterFrequency(
    _Out_opt_ PULONG64 AuxiliaryCounterFrequency)
{
    /* No dedicated auxiliary counter for cross-timestamping is exposed. */
    if (AuxiliaryCounterFrequency != NULL)
        *AuxiliaryCounterFrequency = 0;

    return STATUS_NOT_SUPPORTED;
}
