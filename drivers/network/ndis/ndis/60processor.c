/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60processor.c
 * PURPOSE:     NDIS 6.20 processor topology queries.
 *
 *              Report the kernel's logical processor topology rather than
 *              a uniprocessor answer, so RSS-capable miniports size their
 *              queues from the real system.
 */

#include "ndis6_internal.h"
#include <intrin.h>

#define NDIS6_PROCESSOR_TAG 'iPNn'  /* "nNPi" */

/* NDIS 6.20 layouts; the legacy public NDIS header lacks these types. */
typedef enum _NDIS_PROCESSOR_VENDOR
{
    NdisProcessorVendorUnknown,
    NdisProcessorVendorGenuineIntel,
    NdisProcessorVendorAuthenticAMD
} NDIS_PROCESSOR_VENDOR;

typedef struct _NDIS_PROCESSOR_INFO_EX
{
    PROCESSOR_NUMBER    ProcNum;
    ULONG               SocketId;
    ULONG               CoreId;
    ULONG               HyperThreadId;
    USHORT              NodeId;
    USHORT              NodeDistance;
} NDIS_PROCESSOR_INFO_EX, *PNDIS_PROCESSOR_INFO_EX;

typedef struct _NDIS_SYSTEM_PROCESSOR_INFO_EX
{
    NDIS_OBJECT_HEADER  Header;
    ULONG               Flags;
    NDIS_PROCESSOR_VENDOR ProcessorVendor;
    ULONG               NumSockets;
    ULONG               NumCores;
    ULONG               NumCoresPerSocket;
    ULONG               MaxHyperThreadingProcsPerCore;
    ULONG               ProcessorInfoOffset;
    ULONG               NumberOfProcessors;
    ULONG               ProcessorInfoEntrySize;
} NDIS_SYSTEM_PROCESSOR_INFO_EX, *PNDIS_SYSTEM_PROCESSOR_INFO_EX;

C_ASSERT(sizeof(NDIS_PROCESSOR_INFO_EX) == 20);
C_ASSERT(sizeof(NDIS_SYSTEM_PROCESSOR_INFO_EX) == 40);

#define NDIS_SYSTEM_PROCESSOR_INFO_EX_REVISION_1  1

/* Exported for binaries built against headers that did not inline it. */
ULONG
NTAPI
NdisCurrentProcessorIndex(VOID)
{
    return KeGetCurrentProcessorNumberEx(NULL);
}

static
BOOLEAN
Ndis6ProcessorInMask(
    _In_ const PROCESSOR_NUMBER *Number,
    _In_ const GROUP_AFFINITY   *Mask)
{
    return Number->Group == Mask->Group &&
           Number->Number < sizeof(KAFFINITY) * 8 &&
           (Mask->Mask & ((KAFFINITY)1 << Number->Number)) != 0;
}

static
NDIS_PROCESSOR_VENDOR
Ndis6GetProcessorVendor(VOID)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    int Registers[4];

    __cpuid(Registers, 0);
    if (Registers[1] == 0x756e6547 && Registers[3] == 0x49656e69 &&
        Registers[2] == 0x6c65746e)
    {
        return NdisProcessorVendorGenuineIntel;
    }
    if (Registers[1] == 0x68747541 && Registers[3] == 0x69746e65 &&
        Registers[2] == 0x444d4163)
    {
        return NdisProcessorVendorAuthenticAMD;
    }
#endif
    return NdisProcessorVendorUnknown;
}

NDIS_STATUS
NTAPI
NdisGetProcessorInformationEx(
    _In_opt_ NDIS_HANDLE                    NdisHandle,
    _Out_opt_ PNDIS_SYSTEM_PROCESSOR_INFO_EX SystemProcessorInfo,
    _Inout_  PSIZE_T                        Size)
{
    PNDIS_SYSTEM_PROCESSOR_INFO_EX Info = SystemProcessorInfo;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX Record;
    PNDIS_PROCESSOR_INFO_EX Cpu;
    PUCHAR Topology;
    PULONG CoresPerSocket;
    ULONG Length = 0, Offset, Count = 0, Index, Group, Bit, Socket, Core, Thread;
    SIZE_T Required;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (Size == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Status = KeQueryLogicalProcessorRelationship(NULL, RelationAll, NULL, &Length);
    if (Status != STATUS_INFO_LENGTH_MISMATCH || Length == 0)
        return NDIS_STATUS_FAILURE;

    Topology = ExAllocatePoolWithTag(NonPagedPool, Length, NDIS6_PROCESSOR_TAG);
    if (Topology == NULL)
        return NDIS_STATUS_RESOURCES;

    Status = KeQueryLogicalProcessorRelationship(NULL, RelationAll,
                                                 (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)Topology,
                                                 &Length);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Topology, NDIS6_PROCESSOR_TAG);
        return NDIS_STATUS_FAILURE;
    }

    for (Offset = 0; Offset < Length; Offset += Record->Size)
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Topology + Offset);
        if (Record->Relationship != RelationGroup)
            continue;
        for (Group = 0; Group < Record->Group.ActiveGroupCount; ++Group)
            Count += Record->Group.GroupInfo[Group].ActiveProcessorCount;
    }

    Required = sizeof(*Info) + (SIZE_T)Count * sizeof(*Cpu);
    if (Info == NULL || *Size < Required)
    {
        *Size = Required;
        ExFreePoolWithTag(Topology, NDIS6_PROCESSOR_TAG);
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }
    if (Count == 0)
    {
        ExFreePoolWithTag(Topology, NDIS6_PROCESSOR_TAG);
        return NDIS_STATUS_FAILURE;
    }

    /* A socket index never exceeds the processor count. */
    CoresPerSocket = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(ULONG), NDIS6_PROCESSOR_TAG);
    if (CoresPerSocket == NULL)
    {
        ExFreePoolWithTag(Topology, NDIS6_PROCESSOR_TAG);
        return NDIS_STATUS_RESOURCES;
    }
    RtlZeroMemory(CoresPerSocket, Count * sizeof(ULONG));

    RtlZeroMemory(Info, Required);
    Info->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Info->Header.Revision = NDIS_SYSTEM_PROCESSOR_INFO_EX_REVISION_1;
    Info->Header.Size = sizeof(*Info);
    Info->ProcessorVendor = Ndis6GetProcessorVendor();
    Info->ProcessorInfoOffset = sizeof(*Info);
    Info->NumberOfProcessors = Count;
    Info->ProcessorInfoEntrySize = sizeof(*Cpu);
    Cpu = (PNDIS_PROCESSOR_INFO_EX)((PUCHAR)Info + Info->ProcessorInfoOffset);

    /* Entries are in processor index order. NodeDistance stays zero: the bus
     * does not supply device NUMA proximity. */
    Index = 0;
    for (Offset = 0; Offset < Length; Offset += Record->Size)
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Topology + Offset);
        if (Record->Relationship != RelationGroup)
            continue;
        for (Group = 0; Group < Record->Group.ActiveGroupCount; ++Group)
        {
            for (Bit = 0; Bit < sizeof(KAFFINITY) * 8; ++Bit)
            {
                if (!(Record->Group.GroupInfo[Group].ActiveProcessorMask & ((KAFFINITY)1 << Bit)))
                    continue;
                ASSERT(Index < Count);
                Cpu[Index].ProcNum.Group = (USHORT)Group;
                Cpu[Index].ProcNum.Number = (UCHAR)Bit;
                ++Index;
            }
        }
    }

    /* Sockets must be numbered before cores so each core can be counted
     * against its socket. */
    Socket = 0;
    for (Offset = 0; Offset < Length; Offset += Record->Size)
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Topology + Offset);
        if (Record->Relationship != RelationProcessorPackage)
            continue;
        for (Index = 0; Index < Count; ++Index)
        {
            for (Group = 0; Group < Record->Processor.GroupCount; ++Group)
            {
                if (Ndis6ProcessorInMask(&Cpu[Index].ProcNum, &Record->Processor.GroupMask[Group]))
                    Cpu[Index].SocketId = Socket;
            }
        }
        ++Socket;
    }
    Info->NumSockets = Socket;

    for (Offset = 0; Offset < Length; Offset += Record->Size)
    {
        Record = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(Topology + Offset);
        if (Record->Relationship == RelationProcessorCore)
        {
            Thread = 0;
            Core = 0;
            for (Index = 0; Index < Count; ++Index)
            {
                for (Group = 0; Group < Record->Processor.GroupCount; ++Group)
                {
                    if (!Ndis6ProcessorInMask(&Cpu[Index].ProcNum, &Record->Processor.GroupMask[Group]))
                        continue;
                    if (Thread == 0)
                        Core = CoresPerSocket[Cpu[Index].SocketId]++;
                    Cpu[Index].CoreId = Core;
                    Cpu[Index].HyperThreadId = Thread++;
                }
            }
            Info->MaxHyperThreadingProcsPerCore = max(Info->MaxHyperThreadingProcsPerCore, Thread);
            ++Info->NumCores;
        }
        else if (Record->Relationship == RelationNumaNode)
        {
            for (Index = 0; Index < Count; ++Index)
            {
                if (Ndis6ProcessorInMask(&Cpu[Index].ProcNum, &Record->NumaNode.GroupMask))
                    Cpu[Index].NodeId = (USHORT)Record->NumaNode.NodeNumber;
            }
        }
    }

    for (Socket = 0; Socket < Info->NumSockets; ++Socket)
        Info->NumCoresPerSocket = max(Info->NumCoresPerSocket, CoresPerSocket[Socket]);

    *Size = Required;
    ExFreePoolWithTag(CoresPerSocket, NDIS6_PROCESSOR_TAG);
    ExFreePoolWithTag(Topology, NDIS6_PROCESSOR_TAG);
    return NDIS_STATUS_SUCCESS;
}
