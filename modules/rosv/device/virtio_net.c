/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Virtio-net MMIO device emulation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements a virtio-net device using the MMIO transport (virtio 1.0+).
 * The device presents a virtual network interface to the guest via two
 * virtqueues:
 *   Queue 0 (RX): Host writes packets to guest-provided buffers
 *   Queue 1 (TX): Guest writes packets for the host to transmit
 *
 * The guest discovers this device through a kernel command line parameter
 * (virtio_mmio.device=0x1000@0xFEB01000:6) and communicates via MMIO
 * register reads/writes that trigger EPT violations.
 *
 * Packet path:
 *   Guest TX: guest fills TX virtqueue -> QUEUE_NOTIFY -> we extract
 *             ethernet frames (stripping virtio_net_hdr) -> buffer in
 *             TxRing -> host backend picks up packet
 *   Guest RX: host backend sends packet -> we write into guest RX
 *             virtqueue buffers (prepending virtio_net_hdr) -> raise
 *             interrupt -> guest reads
 */

#include <rosv/rosv.h>
#include <rosv/net_backend.h>
#include <rosv/vm.h>
#include <rosv/virtio_net.h>

#define IP_PROTO_TCP    6

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
typedef struct _ROSV_NET_TRACE_ICMP_ECHO {
    BOOLEAN Valid;
    BOOLEAN IsRequest;
    UCHAR SrcIp[4];
    UCHAR DstIp[4];
    USHORT Id;
    USHORT Seq;
} ROSV_NET_TRACE_ICMP_ECHO, *PROSV_NET_TRACE_ICMP_ECHO;
#endif /* ROSV_ENABLE_TRACE */

FORCEINLINE
ULONG64
RosvVirtioNetReadQpc(
    VOID)
{
    LARGE_INTEGER Counter;
    Counter = KeQueryPerformanceCounter(NULL);
    return (ULONG64)Counter.QuadPart;
}

FORCEINLINE
ULONG64
RosvVirtioNetQpcDeltaUs(
    _In_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG64 StartQpc,
    _In_ ULONG64 EndQpc)
{
    ULONG64 DeltaQpc;
    ULONG64 Freq;

    if (EndQpc < StartQpc || State->PerfFrequency.QuadPart <= 0)
        return 0;

    DeltaQpc = EndQpc - StartQpc;
    Freq = (ULONG64)State->PerfFrequency.QuadPart;
    return (DeltaQpc * 1000000ULL) / Freq;
}

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
static
VOID
RosvVirtioNetDecodeIcmpEcho(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PROSV_NET_TRACE_ICMP_ECHO Info)
{
    ULONG EthType;
    ULONG Ihl;
    const UCHAR *Ip;
    const UCHAR *Icmp;

    RtlZeroMemory(Info, sizeof(*Info));

    if (Data == NULL || Length < 14 + 20 + 8)
        return;

    EthType = ((ULONG)Data[12] << 8) | Data[13];
    if (EthType != 0x0800)
        return;

    Ip = &Data[14];
    if ((Ip[0] >> 4) != 4)
        return;

    Ihl = (ULONG)(Ip[0] & 0x0F) * 4;
    if (Ihl < 20 || Length < 14 + Ihl + 8)
        return;

    if (Ip[9] != 1) /* ICMP */
        return;

    Icmp = Ip + Ihl;
    if (!(Icmp[0] == 8 || Icmp[0] == 0) || Icmp[1] != 0)
        return;

    Info->Valid = TRUE;
    Info->IsRequest = (Icmp[0] == 8) ? TRUE : FALSE;
    RtlCopyMemory(Info->SrcIp, &Ip[12], sizeof(Info->SrcIp));
    RtlCopyMemory(Info->DstIp, &Ip[16], sizeof(Info->DstIp));
    Info->Id = (USHORT)(((USHORT)Icmp[4] << 8) | Icmp[5]);
    Info->Seq = (USHORT)(((USHORT)Icmp[6] << 8) | Icmp[7]);
}
#endif /* ROSV_ENABLE_TRACE */

/* ---- Virtqueue helpers (shared logic with virtio_blk.c) ----------------- */

/*
 * These helpers are structurally identical to the virtio-blk versions.
 * They operate on the same ROSV_VIRTQUEUE and VRING_DESC structures.
 * A future refactor could extract them into a shared virtio_common.c,
 * but for now we keep each device self-contained.
 *
 * Performance optimization: When DescHva/AvailHva/UsedHva are cached
 * (non-NULL), we use direct memory access instead of GPA translation.
 * The HVAs are pre-translated when QUEUE_READY is set.
 */

static BOOLEAN
RosvNetVirtqueueReadDesc(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT Index,
    _Out_ PVRING_DESC Desc)
{
    if (Index >= Vq->Num)
    {
        ROSV_ERR("virtio-net: descriptor index %u >= queue size %u", Index, Vq->Num);
        return FALSE;
    }

    if (Vq->DescHva != NULL)
    {
        /* Fast path: direct memory access through cached HVA */
        PVRING_DESC DescTable = (PVRING_DESC)Vq->DescHva;
        *Desc = DescTable[Index];
    }
    else
    {
        /* Slow path: GPA translation */
        ULONG64 DescGpa = Vq->DescGpa + (ULONG64)Index * sizeof(VRING_DESC);
        NTSTATUS Status = RosvMemoryCopyFromGpa(Vm, Desc, DescGpa, sizeof(VRING_DESC));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to read desc GPA=0x%llX (index=%u) status=0x%08X",
                     DescGpa, Index, Status);
            return FALSE;
        }
    }

    if (Desc->Flags & VRING_DESC_F_INDIRECT)
    {
        ROSV_ERR("virtio-net: VRING_DESC_F_INDIRECT not supported (index=%u)", Index);
        return FALSE;
    }

    if ((Desc->Flags & VRING_DESC_F_NEXT) && Desc->Next >= Vq->Num)
    {
        ROSV_ERR("virtio-net: descriptor %u has NEXT index %u >= queue size %u",
                 Index, Desc->Next, Vq->Num);
        return FALSE;
    }

    return TRUE;
}

static USHORT
RosvNetVirtqueueReadAvailIdx(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq)
{
    if (Vq->AvailHva != NULL)
    {
        /* Fast path: direct read from cached HVA */
        PVRING_AVAIL Avail = (PVRING_AVAIL)Vq->AvailHva;
        return Avail->Idx;
    }
    else
    {
        ULONG64 IdxGpa = Vq->AvailGpa + offsetof(VRING_AVAIL, Idx);
        USHORT Idx;
        NTSTATUS Status = RosvMemoryCopyFromGpa(Vm, &Idx, IdxGpa, sizeof(Idx));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to read avail idx GPA=0x%llX status=0x%08X",
                     IdxGpa, Status);
            return Vq->LastAvailIdx;
        }
        return Idx;
    }
}

static USHORT
RosvNetVirtqueueReadAvailRing(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT RingIndex)
{
    USHORT Wrapped = RingIndex % (USHORT)Vq->Num;

    if (Vq->AvailHva != NULL)
    {
        /* Fast path: direct read from cached HVA.
         * Available ring entries start after the VRING_AVAIL header (4 bytes). */
        PUSHORT RingEntries = (PUSHORT)((PUCHAR)Vq->AvailHva + sizeof(VRING_AVAIL));
        return RingEntries[Wrapped];
    }
    else
    {
        ULONG64 EntryGpa = Vq->AvailGpa + sizeof(VRING_AVAIL) + (ULONG64)Wrapped * sizeof(USHORT);
        USHORT Entry;
        NTSTATUS Status = RosvMemoryCopyFromGpa(Vm, &Entry, EntryGpa, sizeof(Entry));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to read avail ring GPA=0x%llX (idx=%u) status=0x%08X",
                     EntryGpa, RingIndex, Status);
            return 0;
        }
        return Entry;
    }
}

/*
 * Push a single used entry. For batch operations, use StageUsed + FlushUsed.
 */
static BOOLEAN
RosvNetVirtqueuePushUsed(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ ULONG DescChainHead,
    _In_ ULONG BytesWritten)
{
    if (DescChainHead >= Vq->Num)
    {
        ROSV_ERR("virtio-net: PushUsed descriptor id %u >= queue size %u",
                 DescChainHead, Vq->Num);
        return FALSE;
    }

    if (Vq->UsedHva != NULL)
    {
        /* Fast path: direct memory access through cached HVA */
        PVRING_USED Used = (PVRING_USED)Vq->UsedHva;
        USHORT UsedIdx = Used->Idx;
        USHORT Wrapped = UsedIdx % (USHORT)Vq->Num;
        PVRING_USED_ELEM Elems = (PVRING_USED_ELEM)((PUCHAR)Vq->UsedHva + sizeof(VRING_USED));

        Elems[Wrapped].Id = DescChainHead;
        Elems[Wrapped].Len = BytesWritten;
        KeMemoryBarrier();
        Used->Idx = UsedIdx + 1;
        KeMemoryBarrier();
    }
    else
    {
        /* Slow path: GPA translation */
        ULONG64 UsedIdxGpa = Vq->UsedGpa + offsetof(VRING_USED, Idx);
        USHORT UsedIdx;
        USHORT Wrapped;
        ULONG64 ElemGpa;
        VRING_USED_ELEM Elem;
        USHORT NewUsedIdx;
        NTSTATUS Status;

        Status = RosvMemoryCopyFromGpa(Vm, &UsedIdx, UsedIdxGpa, sizeof(UsedIdx));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to read used idx GPA=0x%llX status=0x%08X",
                     UsedIdxGpa, Status);
            return FALSE;
        }
        Wrapped = UsedIdx % (USHORT)Vq->Num;

        ElemGpa = Vq->UsedGpa + sizeof(VRING_USED) + (ULONG64)Wrapped * sizeof(VRING_USED_ELEM);
        Elem.Id = DescChainHead;
        Elem.Len = BytesWritten;
        Status = RosvMemoryCopyToGpa(Vm, ElemGpa, &Elem, sizeof(Elem));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to write used elem GPA=0x%llX (idx=%u) status=0x%08X",
                     ElemGpa, UsedIdx, Status);
            return FALSE;
        }

        KeMemoryBarrier();

        NewUsedIdx = UsedIdx + 1;
        Status = RosvMemoryCopyToGpa(Vm, UsedIdxGpa, &NewUsedIdx, sizeof(NewUsedIdx));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to write used idx GPA=0x%llX status=0x%08X",
                     UsedIdxGpa, Status);
            return FALSE;
        }

        KeMemoryBarrier();
    }
    return TRUE;
}

/*
 * Stage a used-ring entry for deferred commit. Writes the element but does NOT
 * update the used index or issue memory barriers. Call RosvNetVirtqueueFlushUsed
 * after all entries in a batch have been staged.
 */
static BOOLEAN
RosvNetVirtqueueStageUsed(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ ULONG DescChainHead,
    _In_ ULONG BytesWritten,
    _In_ USHORT BaseUsedIdx,
    _In_ ULONG StageIndex)
{
    USHORT Wrapped;

    if (DescChainHead >= Vq->Num)
    {
        ROSV_ERR("virtio-net: StageUsed descriptor id %u >= queue size %u",
                 DescChainHead, Vq->Num);
        return FALSE;
    }

    Wrapped = (USHORT)((BaseUsedIdx + StageIndex) % (USHORT)Vq->Num);

    if (Vq->UsedHva != NULL)
    {
        PVRING_USED_ELEM Elems = (PVRING_USED_ELEM)((PUCHAR)Vq->UsedHva + sizeof(VRING_USED));
        Elems[Wrapped].Id = DescChainHead;
        Elems[Wrapped].Len = BytesWritten;
    }
    else
    {
        ULONG64 ElemGpa = Vq->UsedGpa + sizeof(VRING_USED) +
                          (ULONG64)Wrapped * sizeof(VRING_USED_ELEM);
        VRING_USED_ELEM Elem;
        NTSTATUS Status;
        Elem.Id = DescChainHead;
        Elem.Len = BytesWritten;
        Status = RosvMemoryCopyToGpa(Vm, ElemGpa, &Elem, sizeof(Elem));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to write staged used elem GPA=0x%llX status=0x%08X",
                     ElemGpa, Status);
            return FALSE;
        }
    }

    return TRUE;
}

/*
 * Read the current used index from the virtqueue.
 */
static USHORT
RosvNetVirtqueueReadUsedIdx(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq)
{
    if (Vq->UsedHva != NULL)
    {
        PVRING_USED Used = (PVRING_USED)Vq->UsedHva;
        return Used->Idx;
    }
    else
    {
        ULONG64 UsedIdxGpa = Vq->UsedGpa + offsetof(VRING_USED, Idx);
        USHORT Idx;
        NTSTATUS Status = RosvMemoryCopyFromGpa(Vm, &Idx, UsedIdxGpa, sizeof(Idx));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to read used idx GPA=0x%llX status=0x%08X",
                     UsedIdxGpa, Status);
            return 0;
        }
        return Idx;
    }
}

/*
 * Flush a batch of staged used entries by writing the final used index.
 * Issues one barrier before the index write and one after.
 */
static BOOLEAN
RosvNetVirtqueueFlushUsed(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT NewUsedIdx)
{
    KeMemoryBarrier();

    if (Vq->UsedHva != NULL)
    {
        PVRING_USED Used = (PVRING_USED)Vq->UsedHva;
        Used->Idx = NewUsedIdx;
    }
    else
    {
        ULONG64 UsedIdxGpa = Vq->UsedGpa + offsetof(VRING_USED, Idx);
        NTSTATUS Status = RosvMemoryCopyToGpa(Vm, UsedIdxGpa, &NewUsedIdx, sizeof(NewUsedIdx));
        if (!NT_SUCCESS(Status))
        {
            ROSV_ERR("virtio-net: failed to flush used idx GPA=0x%llX status=0x%08X",
                     UsedIdxGpa, Status);
            return FALSE;
        }
    }

    KeMemoryBarrier();
    return TRUE;
}

/*
 * Translate and cache the virtqueue ring HVAs when a queue becomes ready.
 * Called from the QUEUE_READY MMIO write handler.
 */
static VOID
RosvNetVirtqueueCacheHva(
    _In_ PROSV_VM Vm,
    _Inout_ PROSV_VIRTQUEUE Vq,
    _In_ ULONG QueueIndex)
{
    Vq->DescHva = NULL;
    Vq->AvailHva = NULL;
    Vq->UsedHva = NULL;

    if (!Vq->DescGpa || !Vq->AvailGpa || !Vq->UsedGpa)
    {
        ROSV_ERR("virtio-net: queue %u cannot cache HVA: ring addresses not set "
                 "(desc=0x%llX avail=0x%llX used=0x%llX)",
                 QueueIndex, Vq->DescGpa, Vq->AvailGpa, Vq->UsedGpa);
        return;
    }

    Vq->DescHva = RosvMemoryGpaToHva(Vm, Vq->DescGpa);
    Vq->AvailHva = RosvMemoryGpaToHva(Vm, Vq->AvailGpa);
    Vq->UsedHva = RosvMemoryGpaToHva(Vm, Vq->UsedGpa);

    if (Vq->DescHva && Vq->AvailHva && Vq->UsedHva)
    {
        ROSV_TRACE("virtio-net: queue %u HVA cached: desc=%p avail=%p used=%p",
                   QueueIndex, Vq->DescHva, Vq->AvailHva, Vq->UsedHva);
    }
    else
    {
        /* If any translation failed, clear all to fall back to GPA path */
        ROSV_WARN("virtio-net: queue %u HVA cache partial failure (desc=%p avail=%p used=%p), "
                  "falling back to GPA translation",
                  QueueIndex, Vq->DescHva, Vq->AvailHva, Vq->UsedHva);
        Vq->DescHva = NULL;
        Vq->AvailHva = NULL;
        Vq->UsedHva = NULL;
    }
}

/* ---- TX checksum offload helpers ---------------------------------------- */

/*
 * Internet checksum (RFC 1071): ones-complement sum of 16-bit words.
 */
static USHORT
RosvVirtioNetCsum16(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Sum = 0;
    ULONG i;

    for (i = 0; i + 3 < Length; i += 4)
    {
        Sum += ((ULONG)Data[i] << 8) | Data[i + 1];
        Sum += ((ULONG)Data[i + 2] << 8) | Data[i + 3];
    }
    if (i + 1 < Length)
    {
        Sum += ((ULONG)Data[i] << 8) | Data[i + 1];
        i += 2;
    }
    if (i < Length)
        Sum += ((ULONG)Data[i] << 8);

    while (Sum > 0xFFFF)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    return (USHORT)(~(USHORT)Sum);
}

/*
 * Finalize a partial checksum for CSUM offload.
 *
 * When the guest sets VIRTIO_NET_HDR_F_NEEDS_CSUM, the checksum field at
 * PacketBuf[CsumStart + CsumOffset] contains the pseudo-header sum.
 * We complete the checksum by summing the data with the checksum field
 * zeroed, adding the saved pseudo-header contribution, and folding.
 */
static VOID
RosvVirtioNetFinalizeCsum(
    _Inout_updates_bytes_(PacketLen) PUCHAR PacketBuf,
    _In_ ULONG PacketLen,
    _In_ USHORT CsumStart,
    _In_ USHORT CsumOffset)
{
    ULONG CsumFieldOff;
    USHORT PartialCsum;
    ULONG Sum;
    ULONG DataLen;
    ULONG i;
    USHORT FinalCsum;

    CsumFieldOff = (ULONG)CsumStart + (ULONG)CsumOffset;
    if (CsumFieldOff + 2 > PacketLen || CsumStart >= PacketLen)
        return;

    /* Save the pseudo-header checksum (network byte order) */
    PartialCsum = (USHORT)(((USHORT)PacketBuf[CsumFieldOff] << 8) |
                            PacketBuf[CsumFieldOff + 1]);

    /* Zero the field before summing */
    PacketBuf[CsumFieldOff] = 0;
    PacketBuf[CsumFieldOff + 1] = 0;

    /* Ones-complement sum of [CsumStart .. end] */
    DataLen = PacketLen - CsumStart;
    Sum = 0;
    for (i = 0; i + 3 < DataLen; i += 4)
    {
        Sum += ((ULONG)PacketBuf[CsumStart + i] << 8) | PacketBuf[CsumStart + i + 1];
        Sum += ((ULONG)PacketBuf[CsumStart + i + 2] << 8) | PacketBuf[CsumStart + i + 3];
    }
    if (i + 1 < DataLen)
    {
        Sum += ((ULONG)PacketBuf[CsumStart + i] << 8) | PacketBuf[CsumStart + i + 1];
        i += 2;
    }
    if (i < DataLen)
        Sum += ((ULONG)PacketBuf[CsumStart + i] << 8);

    /* Add the pseudo-header contribution */
    Sum += PartialCsum;

    while (Sum > 0xFFFF)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    FinalCsum = (USHORT)(~Sum & 0xFFFF);

    PacketBuf[CsumFieldOff] = (UCHAR)((FinalCsum >> 8) & 0xFF);
    PacketBuf[CsumFieldOff + 1] = (UCHAR)(FinalCsum & 0xFF);
}

/* ---- TX TSO segmentation ------------------------------------------------ */

/*
 * Enqueue a single packet into a queue pair's TxRing.
 * Returns TRUE if enqueued, FALSE if ring full.
 * Caller must NOT hold Pair->TxRingLock.
 */
static BOOLEAN
RosvVirtioNetEnqueueTxPacket(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _Inout_ PROSV_VIRTIO_NET_QUEUE_PAIR Pair,
    _In_reads_bytes_(PacketLen) const PUCHAR PacketBuf,
    _In_ ULONG PacketLen)
{
    KIRQL OldIrql;
    BOOLEAN Enqueued = FALSE;

    if (PacketLen == 0 || PacketLen > ROSV_VIRTIO_NET_MAX_PACKET)
        return FALSE;

    KeAcquireSpinLock(&Pair->TxRingLock, &OldIrql);

    if (Pair->TxRingCount < Pair->TxRingCapacity)
    {
        PROSV_VIRTIO_NET_TX_ENTRY Entry = &Pair->TxRing[Pair->TxRingHead];
        ULONG64 EnqueueQpc = RosvVirtioNetReadQpc();

        RtlCopyMemory(Entry->Data, PacketBuf, PacketLen);
        Entry->Length = PacketLen;
        Entry->EnqueueQpc = EnqueueQpc;
        Entry->TraceSeq = ++State->TxTraceSeqNext;
        Pair->TxRingHead = (Pair->TxRingHead + 1) & (Pair->TxRingCapacity - 1);
        Pair->TxRingCount++;
        Pair->TxPackets++;
        Pair->TxBytes += PacketLen;
        State->TxPackets++;
        State->TxBytes += PacketLen;
        Enqueued = TRUE;
    }

    KeReleaseSpinLock(&Pair->TxRingLock, OldIrql);

    if (Enqueued)
        KeSetEvent(&State->TxReadyEvent, IO_NETWORK_INCREMENT, FALSE);

    return Enqueued;
}

/*
 * Perform TCP Segmentation Offload (TSO) for a large TCP packet.
 *
 * Splits a large TCP packet into MSS-sized segments with correct
 * TCP sequence numbers, IP total lengths, and recomputed checksums.
 * Each segment is enqueued into the queue pair's TxRing.
 *
 * Returns the number of segments successfully enqueued.
 */
static ULONG
RosvVirtioNetSegmentTso(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _Inout_ PROSV_VIRTIO_NET_QUEUE_PAIR Pair,
    _In_reads_bytes_(PacketLen) const PUCHAR PacketBuf,
    _In_ ULONG PacketLen,
    _In_ UCHAR GsoType,
    _In_ USHORT GsoSize)
{
    PUCHAR SegBuf = State->TsoSegBuf;
    const ULONG EthHdrLen = 14;
    ULONG IpHdrLen;
    ULONG TcpHdrOff;
    ULONG TcpHdrLen;
    ULONG AllHdrLen;
    ULONG TcpPayloadLen;
    ULONG TcpPayloadOff;
    ULONG OrigSeqNum;
    ULONG PayloadSent;
    ULONG SegCount = 0;
    ULONG Mss;

    UNREFERENCED_PARAMETER(GsoType);

    if (PacketLen < EthHdrLen + 20)
        return 0;
    if ((PacketBuf[14] >> 4) != 4)
        return 0;

    IpHdrLen = (ULONG)(PacketBuf[14] & 0x0F) * 4;
    if (IpHdrLen < 20 || PacketLen < EthHdrLen + IpHdrLen + 20)
        return 0;

    TcpHdrOff = EthHdrLen + IpHdrLen;
    TcpHdrLen = (ULONG)((PacketBuf[TcpHdrOff + 12] >> 4) & 0x0F) * 4;
    if (TcpHdrLen < 20 || PacketLen < TcpHdrOff + TcpHdrLen)
        return 0;

    AllHdrLen = TcpHdrOff + TcpHdrLen;
    TcpPayloadOff = AllHdrLen;
    TcpPayloadLen = PacketLen - AllHdrLen;
    Mss = (ULONG)GsoSize;

    if (Mss == 0 || TcpPayloadLen == 0)
        return 0;

    OrigSeqNum = ((ULONG)PacketBuf[TcpHdrOff + 4] << 24) |
                 ((ULONG)PacketBuf[TcpHdrOff + 5] << 16) |
                 ((ULONG)PacketBuf[TcpHdrOff + 6] << 8) |
                 (ULONG)PacketBuf[TcpHdrOff + 7];

    PayloadSent = 0;

    while (PayloadSent < TcpPayloadLen)
    {
        ULONG ChunkLen = TcpPayloadLen - PayloadSent;
        ULONG SegLen;
        ULONG SegSeqNum;
        USHORT SegIpTotalLen;
        ULONG SegTcpLen;
        ULONG CsumSum;
        USHORT TcpCsum;
        USHORT IpCsum;
        ULONG i;

        if (ChunkLen > Mss)
            ChunkLen = Mss;

        SegLen = AllHdrLen + ChunkLen;
        if (SegLen > ROSV_VIRTIO_NET_MAX_PACKET)
            break;

        /* Copy all headers + this segment's payload */
        RtlCopyMemory(SegBuf, PacketBuf, AllHdrLen);
        RtlCopyMemory(&SegBuf[AllHdrLen],
                       &PacketBuf[TcpPayloadOff + PayloadSent],
                       ChunkLen);

        /* Update IP total length */
        SegIpTotalLen = (USHORT)(IpHdrLen + TcpHdrLen + ChunkLen);
        SegBuf[16] = (UCHAR)((SegIpTotalLen >> 8) & 0xFF);
        SegBuf[17] = (UCHAR)(SegIpTotalLen & 0xFF);

        /* Update TCP sequence number */
        SegSeqNum = OrigSeqNum + PayloadSent;
        SegBuf[TcpHdrOff + 4] = (UCHAR)((SegSeqNum >> 24) & 0xFF);
        SegBuf[TcpHdrOff + 5] = (UCHAR)((SegSeqNum >> 16) & 0xFF);
        SegBuf[TcpHdrOff + 6] = (UCHAR)((SegSeqNum >> 8) & 0xFF);
        SegBuf[TcpHdrOff + 7] = (UCHAR)(SegSeqNum & 0xFF);

        /* Clear FIN/PSH on non-final segments */
        if (PayloadSent + ChunkLen < TcpPayloadLen)
            SegBuf[TcpHdrOff + 13] &= ~0x09;

        /* Recompute IP header checksum */
        SegBuf[EthHdrLen + 10] = 0;
        SegBuf[EthHdrLen + 11] = 0;
        IpCsum = RosvVirtioNetCsum16(&SegBuf[EthHdrLen], IpHdrLen);
        SegBuf[EthHdrLen + 10] = (UCHAR)((IpCsum >> 8) & 0xFF);
        SegBuf[EthHdrLen + 11] = (UCHAR)(IpCsum & 0xFF);

        /* Compute TCP checksum with pseudo-header */
        SegTcpLen = TcpHdrLen + ChunkLen;
        SegBuf[TcpHdrOff + 16] = 0;
        SegBuf[TcpHdrOff + 17] = 0;

        CsumSum = 0;
        CsumSum += ((ULONG)SegBuf[EthHdrLen + 12] << 8) | SegBuf[EthHdrLen + 13];
        CsumSum += ((ULONG)SegBuf[EthHdrLen + 14] << 8) | SegBuf[EthHdrLen + 15];
        CsumSum += ((ULONG)SegBuf[EthHdrLen + 16] << 8) | SegBuf[EthHdrLen + 17];
        CsumSum += ((ULONG)SegBuf[EthHdrLen + 18] << 8) | SegBuf[EthHdrLen + 19];
        CsumSum += IP_PROTO_TCP;
        CsumSum += SegTcpLen;

        for (i = 0; i + 1 < SegTcpLen; i += 2)
            CsumSum += ((ULONG)SegBuf[TcpHdrOff + i] << 8) | SegBuf[TcpHdrOff + i + 1];
        if (i < SegTcpLen)
            CsumSum += ((ULONG)SegBuf[TcpHdrOff + i] << 8);

        while (CsumSum > 0xFFFF)
            CsumSum = (CsumSum & 0xFFFF) + (CsumSum >> 16);

        TcpCsum = (USHORT)(~CsumSum & 0xFFFF);
        if (TcpCsum == 0)
            TcpCsum = 0xFFFF;

        SegBuf[TcpHdrOff + 16] = (UCHAR)((TcpCsum >> 8) & 0xFF);
        SegBuf[TcpHdrOff + 17] = (UCHAR)(TcpCsum & 0xFF);

        /* Pad to minimum Ethernet frame */
        if (SegLen < 60)
        {
            RtlZeroMemory(&SegBuf[SegLen], 60 - SegLen);
            SegLen = 60;
        }

        if (!RosvVirtioNetEnqueueTxPacket(State, Pair, SegBuf, SegLen))
        {
            Pair->TxBackpressure = TRUE;
            break;
        }

        SegCount++;
        PayloadSent += ChunkLen;
    }

    return SegCount;
}

/* ---- TX path: guest -> host --------------------------------------------- */

/*
 * Process all pending packets in a TX virtqueue.
 * For each packet:
 *   1. Read the descriptor chain (virtio_net_hdr + data)
 *   2. Handle checksum offload (finalize partial checksums if NEEDS_CSUM set)
 *   3. Handle TSO/GSO (segment large packets into MSS-sized frames)
 *   4. Copy the raw ethernet frame(s) into the queue pair's TxRing buffer
 *   5. Push a used entry to signal completion to the guest
 *
 * The TxRing is consumed by the active host network backend.
 *
 * @param PairIndex  Queue pair index (0..NumQueuePairs-1).
 *                   The TX virtqueue index is 2*PairIndex + 1.
 */
static VOID
RosvVirtioNetProcessTxQueue(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG PairIndex)
{
    PROSV_VM Vm = State->OwnerVm;
    ULONG TxQueueIdx = PairIndex * 2 + 1;
    PROSV_VIRTQUEUE Vq = &State->Vq[TxQueueIdx];
    PROSV_VIRTIO_NET_QUEUE_PAIR Pair = &State->QueuePairs[PairIndex];
    USHORT AvailIdx;
    USHORT DescIdx;
    ULONG ProcessedCount = 0;
    USHORT BaseUsedIdx;
    BOOLEAN RingFull = FALSE;

    ExAcquireFastMutex(&Pair->TxQueueMutex);
    Pair->TxBackpressure = FALSE;

    if (!Vq->Ready)
    {
        ROSV_WARN("virtio-net: TX QUEUE_NOTIFY but queue not ready");
        goto Exit;
    }

    if (!Vq->DescGpa || !Vq->AvailGpa || !Vq->UsedGpa)
    {
        ROSV_ERR("virtio-net: TX QUEUE_NOTIFY but ring addresses not set "
                 "(desc=0x%llX avail=0x%llX used=0x%llX)",
                 Vq->DescGpa, Vq->AvailGpa, Vq->UsedGpa);
        goto Exit;
    }

    KeMemoryBarrier();
    AvailIdx = RosvNetVirtqueueReadAvailIdx(Vm, Vq);
    KeMemoryBarrier();

    {
        USHORT NumPending = (USHORT)(AvailIdx - Vq->LastAvailIdx);
        if (NumPending > (USHORT)Vq->Num)
        {
            ROSV_ERR("virtio-net: TX avail idx jump too large: avail_idx=%u last_avail=%u "
                     "delta=%u queue_size=%u",
                     AvailIdx, Vq->LastAvailIdx, NumPending, Vq->Num);
            goto Exit;
        }
    }

    /* Read base used index once for the entire batch */
    BaseUsedIdx = RosvNetVirtqueueReadUsedIdx(Vm, Vq);

    while (Vq->LastAvailIdx != AvailIdx)
    {
        VRING_DESC Desc;
        VIRTIO_NET_HDR NetHdr;
        USHORT CurrentIdx;
        ULONG TotalDataLen = 0;
        UCHAR StackBuf[ROSV_VIRTIO_NET_MAX_PACKET];
        PUCHAR PacketBuf = StackBuf;
        ULONG PacketBufSize = sizeof(StackBuf);
        ULONG PacketOffset = 0;
        BOOLEAN HeaderRead = FALSE;
        ULONG ChainDepth = 0;

        DescIdx = RosvNetVirtqueueReadAvailRing(Vm, Vq, Vq->LastAvailIdx);

        if (DescIdx >= (USHORT)Vq->Num)
        {
            ROSV_ERR("virtio-net: TX avail ring entry %u has desc index %u >= queue size %u",
                     Vq->LastAvailIdx, DescIdx, Vq->Num);
            Vq->LastAvailIdx++;
            ProcessedCount++;
            continue;
        }

        /* Walk the descriptor chain: first part is virtio_net_hdr, rest is data */
        CurrentIdx = DescIdx;

        while (ChainDepth < Vq->Num)
        {
            NTSTATUS CopyStatus;

            if (!RosvNetVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
            {
                ROSV_ERR("virtio-net: TX failed to read descriptor at index %u", CurrentIdx);
                break;
            }

            if (!HeaderRead)
            {
                /* First buffer should contain at least the virtio_net_hdr */
                if (Desc.Len < sizeof(VIRTIO_NET_HDR))
                {
                    ROSV_ERR("virtio-net: TX first descriptor too small for header: %u < %u",
                             Desc.Len, (ULONG)sizeof(VIRTIO_NET_HDR));
                    break;
                }

                /* Read the header for checksum offload and GSO/TSO */
                CopyStatus = RosvMemoryCopyFromGpa(Vm, &NetHdr, Desc.Addr,
                                                    sizeof(VIRTIO_NET_HDR));
                if (!NT_SUCCESS(CopyStatus))
                {
                    ROSV_ERR("virtio-net: TX failed to read net header GPA=0x%llX status=0x%08X",
                             Desc.Addr, CopyStatus);
                    break;
                }
                HeaderRead = TRUE;

                /* If GSO is active, switch to the larger gather buffer */
                if (NetHdr.GsoType != VIRTIO_NET_HDR_GSO_NONE &&
                    State->TsoGatherBuf != NULL)
                {
                    PacketBuf = State->TsoGatherBuf;
                    PacketBufSize = ROSV_VIRTIO_NET_TSO_MAX_PACKET;
                }

                /* Copy any data after the header in this descriptor */
                if (Desc.Len > sizeof(VIRTIO_NET_HDR))
                {
                    ULONG ExtraLen = Desc.Len - sizeof(VIRTIO_NET_HDR);
                    if (PacketOffset + ExtraLen > PacketBufSize)
                        ExtraLen = PacketBufSize - PacketOffset;

                    if (ExtraLen > 0)
                    {
                        CopyStatus = RosvMemoryCopyFromGpa(Vm,
                                                            PacketBuf + PacketOffset,
                                                            Desc.Addr + sizeof(VIRTIO_NET_HDR),
                                                            ExtraLen);
                        if (!NT_SUCCESS(CopyStatus))
                        {
                            ROSV_ERR("virtio-net: TX copy data after header failed status=0x%08X",
                                     CopyStatus);
                            break;
                        }
                        PacketOffset += ExtraLen;
                    }
                }
            }
            else
            {
                /* Subsequent descriptors are pure data */
                ULONG CopyLen = Desc.Len;
                if (PacketOffset + CopyLen > PacketBufSize)
                    CopyLen = PacketBufSize - PacketOffset;

                if (CopyLen > 0)
                {
                    CopyStatus = RosvMemoryCopyFromGpa(Vm,
                                                        PacketBuf + PacketOffset,
                                                        Desc.Addr,
                                                        CopyLen);
                    if (!NT_SUCCESS(CopyStatus))
                    {
                        ROSV_ERR("virtio-net: TX copy data failed GPA=0x%llX status=0x%08X",
                                 Desc.Addr, CopyStatus);
                        break;
                    }
                    PacketOffset += CopyLen;
                }
            }

            TotalDataLen += Desc.Len;

            if (!(Desc.Flags & VRING_DESC_F_NEXT))
                break;

            CurrentIdx = Desc.Next;
            ChainDepth++;
        }

        /* Buffer the extracted packet in this pair's TxRing for the host backend.
         * Handle checksum offload and TSO/GSO if indicated by virtio_net_hdr. */
        if (PacketOffset > 0)
        {
            BOOLEAN Enqueued = FALSE;

            if (HeaderRead &&
                NetHdr.GsoType != VIRTIO_NET_HDR_GSO_NONE &&
                (NetHdr.GsoType == VIRTIO_NET_HDR_GSO_TCPV4 ||
                 NetHdr.GsoType == VIRTIO_NET_HDR_GSO_TCPV6))
            {
                /* TSO/GSO: segment the large packet into MSS-sized frames */
                ULONG SegCount;

                SegCount = RosvVirtioNetSegmentTso(State, Pair,
                                                    PacketBuf, PacketOffset,
                                                    NetHdr.GsoType,
                                                    NetHdr.GsoSize);
                if (SegCount > 0)
                    Enqueued = TRUE;
                else
                    RingFull = Pair->TxBackpressure;
            }
            else
            {
                /* Non-GSO path: single packet.
                 * Finalize checksum if guest requested CSUM offload. */
                if (HeaderRead &&
                    (NetHdr.Flags & VIRTIO_NET_HDR_F_NEEDS_CSUM))
                {
                    RosvVirtioNetFinalizeCsum(PacketBuf, PacketOffset,
                                              NetHdr.CsumStart,
                                              NetHdr.CsumOffset);
                }

                if (PacketOffset > ROSV_VIRTIO_NET_MAX_PACKET)
                    PacketOffset = ROSV_VIRTIO_NET_MAX_PACKET;

                Enqueued = RosvVirtioNetEnqueueTxPacket(State, Pair,
                                                         PacketBuf,
                                                         PacketOffset);
                if (!Enqueued)
                {
                    Pair->TxBackpressure = TRUE;
                    RingFull = TRUE;
                }

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                if (Enqueued)
                {
                    ROSV_NET_TRACE_ICMP_ECHO EchoInfo;
                    RosvVirtioNetDecodeIcmpEcho(PacketBuf, PacketOffset, &EchoInfo);
                    if (EchoInfo.Valid)
                    {
                        ROSV_TRACE("virtio-net: [PERF] guest->ring pair=%u len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
                                   PairIndex,
                                   PacketOffset,
                                   EchoInfo.IsRequest ? "echo-req" : "echo-reply",
                                   EchoInfo.Id,
                                   EchoInfo.Seq,
                                   EchoInfo.SrcIp[0], EchoInfo.SrcIp[1], EchoInfo.SrcIp[2], EchoInfo.SrcIp[3],
                                   EchoInfo.DstIp[0], EchoInfo.DstIp[1], EchoInfo.DstIp[2], EchoInfo.DstIp[3]);
                    }
                }
#endif
            }

            if (!Enqueued && RingFull)
            {
                ROSV_TRACE("virtio-net: TX pair %u ring full, deferring guest completion "
                           "(pending_desc=%u ring_count=%u)",
                           PairIndex,
                           (USHORT)(AvailIdx - Vq->LastAvailIdx),
                           Pair->TxRingCount);
                break;
            }
        }

        /* Stage used entry (batched - index update deferred until end) */
        RosvNetVirtqueueStageUsed(Vm, Vq, (ULONG)DescIdx, TotalDataLen,
                                  BaseUsedIdx, ProcessedCount);

        Vq->LastAvailIdx++;
        ProcessedCount++;

        if (ProcessedCount > Vq->Num)
        {
            ROSV_ERR("virtio-net: TX processed %u requests (> queue size %u), stopping",
                     ProcessedCount, Vq->Num);
            break;
        }
    }

    /* Flush all staged used entries with a single index update + barrier */
    if (ProcessedCount > 0)
    {
        RosvNetVirtqueueFlushUsed(Vm, Vq, (USHORT)(BaseUsedIdx + ProcessedCount));
    }

    if (ProcessedCount > 0)
    {
        KIRQL IntIrql;
        KeAcquireSpinLock(&State->InterruptLock, &IntIrql);
        State->InterruptStatus |= VIRTIO_INT_VRING;
        State->InterruptPending = TRUE;
        KeMemoryBarrier();
        KeReleaseSpinLock(&State->InterruptLock, IntIrql);
        /* Wake vCPU if halted so it can inject this TX completion interrupt */
        if (State->OwnerVm != NULL)
            KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
    }

Exit:
    ExReleaseFastMutex(&Pair->TxQueueMutex);
}

/* ---- RX path: host -> guest --------------------------------------------- */

/*
 * Try to inject a packet into a guest RX virtqueue.
 * The guest pre-posts receive buffers; we fill the first available one
 * with a virtio_net_hdr prefix followed by the raw ethernet frame.
 *
 * @param PairIndex  Queue pair index (0..NumQueuePairs-1).
 *                   The RX virtqueue index is 2*PairIndex.
 *
 * Returns TRUE if the packet was successfully injected.
 * Returns FALSE if no receive buffers are available (guest hasn't posted any).
 */
static BOOLEAN
RosvVirtioNetInjectToRxQueue(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG PairIndex,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PROSV_VM Vm = State->OwnerVm;
    ULONG RxQueueIdx = PairIndex * 2;
    PROSV_VIRTQUEUE Vq = &State->Vq[RxQueueIdx];
    PROSV_VIRTIO_NET_QUEUE_PAIR Pair = &State->QueuePairs[PairIndex];
    USHORT AvailIdx;
    USHORT DescIdx;
    VRING_DESC Desc;
    VIRTIO_NET_HDR NetHdr;
    NTSTATUS Status;
    ULONG TotalWritten = 0;
    ULONG DataOffset = 0;
    USHORT CurrentIdx;
    ULONG ChainDepth = 0;
    BOOLEAN HeaderWritten = FALSE;
    ULONG RequiredBytes;
    ULONG WritableCapacity = 0;
    BOOLEAN Success = FALSE;

    ExAcquireFastMutex(&Pair->RxQueueMutex);
    if (!Vq->Ready || !Vq->DescGpa || !Vq->AvailGpa || !Vq->UsedGpa)
        goto Exit;

    KeMemoryBarrier();
    AvailIdx = RosvNetVirtqueueReadAvailIdx(Vm, Vq);
    KeMemoryBarrier();

    /* Check if any buffers are available */
    if (Vq->LastAvailIdx == AvailIdx)
        goto Exit;  /* No receive buffers posted by guest */

    DescIdx = RosvNetVirtqueueReadAvailRing(Vm, Vq, Vq->LastAvailIdx);
    if (DescIdx >= (USHORT)Vq->Num)
    {
        ROSV_ERR("virtio-net: RX avail ring entry %u has desc index %u >= queue size %u",
                 Vq->LastAvailIdx, DescIdx, Vq->Num);
        goto Exit;
    }

    /* Prepare a minimal virtio_net_hdr (no offload) */
    RtlZeroMemory(&NetHdr, sizeof(NetHdr));
    NetHdr.Flags = 0;
    NetHdr.GsoType = VIRTIO_NET_HDR_GSO_NONE;

    /* Single-pass RX: validate and write each descriptor in one walk.
     * We read each descriptor once, check WRITE flag, and copy data into it
     * immediately. If the chain is too short for the full packet we bail
     * before pushing the used entry, so the guest never sees partial data.
     * This halves GPA translation cost vs. the old preflight+write approach. */
    RequiredBytes = sizeof(VIRTIO_NET_HDR) + Length;
    CurrentIdx = DescIdx;
    ChainDepth = 0;

    while (ChainDepth < Vq->Num)
    {
        if (!RosvNetVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
        {
            ROSV_ERR("virtio-net: RX failed to read descriptor at index %u", CurrentIdx);
            goto Exit;
        }

        if (!(Desc.Flags & VRING_DESC_F_WRITE))
        {
            ROSV_ERR("virtio-net: RX descriptor %u not writable (flags=0x%X)",
                     CurrentIdx, Desc.Flags);
            goto Exit;
        }

        WritableCapacity += Desc.Len;

        if (!HeaderWritten)
        {
            /* Write the virtio_net_hdr first */
            ULONG HdrCopy = sizeof(VIRTIO_NET_HDR);
            if (HdrCopy > Desc.Len)
                HdrCopy = Desc.Len;

            Status = RosvMemoryCopyToGpa(Vm, Desc.Addr, &NetHdr, HdrCopy);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("virtio-net: RX failed to write net header GPA=0x%llX status=0x%08X",
                         Desc.Addr, Status);
                goto Exit;
            }
            TotalWritten += HdrCopy;
            HeaderWritten = TRUE;

            /* Fill remaining space in this descriptor with packet data */
            if (Desc.Len > HdrCopy && DataOffset < Length)
            {
                ULONG SpaceLeft = Desc.Len - HdrCopy;
                ULONG DataCopy = Length - DataOffset;
                if (DataCopy > SpaceLeft)
                    DataCopy = SpaceLeft;

                Status = RosvMemoryCopyToGpa(Vm, Desc.Addr + HdrCopy,
                                              Data + DataOffset, DataCopy);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("virtio-net: RX data copy failed GPA=0x%llX status=0x%08X",
                             Desc.Addr + HdrCopy, Status);
                    goto Exit;
                }
                TotalWritten += DataCopy;
                DataOffset += DataCopy;
            }
        }
        else
        {
            /* Continue writing packet data */
            if (DataOffset < Length)
            {
                ULONG DataCopy = Length - DataOffset;
                if (DataCopy > Desc.Len)
                    DataCopy = Desc.Len;

                Status = RosvMemoryCopyToGpa(Vm, Desc.Addr,
                                              Data + DataOffset, DataCopy);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("virtio-net: RX data copy failed GPA=0x%llX status=0x%08X",
                             Desc.Addr, Status);
                    goto Exit;
                }
                TotalWritten += DataCopy;
                DataOffset += DataCopy;
            }
        }

        /* All data written? */
        if (DataOffset >= Length && HeaderWritten)
            break;

        if (!(Desc.Flags & VRING_DESC_F_NEXT))
            break;

        CurrentIdx = Desc.Next;
        ChainDepth++;
    }

    if (!HeaderWritten || DataOffset != Length)
    {
        if (WritableCapacity < RequiredBytes)
        {
            ROSV_TRACE("virtio-net: RX chain too small for packet (%u < %u), leaving staged",
                       WritableCapacity, RequiredBytes);
        }
        else
        {
            ROSV_ERR("virtio-net: RX write incomplete (header=%u data=%u/%u)",
                     HeaderWritten ? 1 : 0, DataOffset, Length);
        }
        goto Exit;
    }

    /* Push used entry with total bytes written (header + data) */
    RosvNetVirtqueuePushUsed(Vm, Vq, (ULONG)DescIdx, TotalWritten);
    Vq->LastAvailIdx++;

    Pair->RxPackets++;
    Pair->RxBytes += Length;
    State->RxPackets++;
    State->RxBytes += Length;

    /* Interrupt coalescing: only raise the interrupt when the coalescing
     * policy says so (packet count threshold, time threshold, or guest
     * halted). This reduces VM-exit rate during bulk RX.
     * We always signal HaltWakeEvent regardless of coalescing so that a
     * sleeping guest wakes promptly; ShouldInjectIrq(GuestIsHalted=FALSE)
     * gates only the actual interrupt injection. */
    if (RosvVirtioNetShouldInjectIrq(State, FALSE))
    {
        KIRQL IntIrql;
        KeAcquireSpinLock(&State->InterruptLock, &IntIrql);
        State->InterruptStatus |= VIRTIO_INT_VRING;
        State->InterruptPending = TRUE;
        KeMemoryBarrier();
        KeReleaseSpinLock(&State->InterruptLock, IntIrql);

        RosvVirtioNetRecordIrqInjected(State);

        if (State->OwnerVm != NULL)
            KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
    }
    Success = TRUE;

Exit:
    ExReleaseFastMutex(&Pair->RxQueueMutex);
    return Success;
}


/* ---- Config space read -------------------------------------------------- */

static ULONG
RosvVirtioNetReadConfig(
    _In_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    ULONG Value = 0;
    ULONG ConfigSize = sizeof(VIRTIO_NET_CONFIG);
    PUCHAR ConfigBytes = (PUCHAR)&State->Config;

    if (Offset >= ConfigSize)
    {
        ROSV_WARN("virtio-net: config read beyond end: offset=0x%X config_size=0x%X",
                  Offset, ConfigSize);
        return 0;
    }

    switch (Size)
    {
    case 1:
        Value = ConfigBytes[Offset];
        break;
    case 2:
        if (Offset + 2 <= ConfigSize)
            Value = *(USHORT *)(ConfigBytes + Offset);
        else
            Value = ConfigBytes[Offset];
        break;
    case 4:
        if (Offset + 4 <= ConfigSize)
            Value = *(ULONG *)(ConfigBytes + Offset);
        else if (Offset + 2 <= ConfigSize)
            Value = *(USHORT *)(ConfigBytes + Offset);
        else
            Value = ConfigBytes[Offset];
        break;
    default:
        ROSV_WARN("virtio-net: unsupported config read size %u", Size);
        break;
    }

    return Value;
}

/* ---- MMIO register read handler ----------------------------------------- */

BOOLEAN
RosvVirtioNetMmioRead(
    _In_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _Out_ PULONG64 Value)
{
    ULONG Offset;
    ULONG Result = 0;
    PROSV_VIRTQUEUE SelectedVq;

    if (GuestPhysicalAddress < ROSV_VIRTIO_NET_MMIO_BASE ||
        GuestPhysicalAddress >= ROSV_VIRTIO_NET_MMIO_BASE + ROSV_VIRTIO_NET_MMIO_SIZE)
    {
        return FALSE;
    }

    Offset = (ULONG)(GuestPhysicalAddress - ROSV_VIRTIO_NET_MMIO_BASE);

    /* Config space starts at offset 0x100 */
    if (Offset >= VIRTIO_MMIO_CONFIG_SPACE)
    {
        Result = RosvVirtioNetReadConfig(State, Offset - VIRTIO_MMIO_CONFIG_SPACE, Size);
        *Value = (ULONG64)Result;
        return TRUE;
    }

    /* Select the appropriate virtqueue for queue-specific registers */
    if (State->QueueSel < VIRTIO_NET_NUM_QUEUES)
        SelectedVq = &State->Vq[State->QueueSel];
    else
        SelectedVq = NULL;

    switch (Offset)
    {
    case VIRTIO_MMIO_MAGIC_VALUE:
        Result = VIRTIO_MMIO_MAGIC;
        break;

    case VIRTIO_MMIO_VERSION:
        Result = VIRTIO_MMIO_VERSION_2;
        break;

    case VIRTIO_MMIO_DEVICE_ID:
        Result = VIRTIO_ID_NET;
        break;

    case VIRTIO_MMIO_VENDOR_ID:
        Result = VIRTIO_VENDOR_ROSV;
        break;

    case VIRTIO_MMIO_DEVICE_FEATURES:
        if (State->DeviceFeaturesSelPage == 0)
            Result = (ULONG)(State->DeviceFeatures & 0xFFFFFFFF);
        else if (State->DeviceFeaturesSelPage == 1)
            Result = (ULONG)(State->DeviceFeatures >> 32);
        else
            Result = 0;
        break;

    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        /* Return 0 for non-existent queues (signals queue not available).
         * Also return 0 for queues beyond NumQueuePairs*2. */
        if (SelectedVq != NULL && State->QueueSel < State->NumQueuePairs * 2)
            Result = ROSV_VIRTIO_QUEUE_SIZE_MAX;
        else
            Result = 0;
        break;

    case VIRTIO_MMIO_QUEUE_READY:
        Result = (SelectedVq && SelectedVq->Ready) ? 1 : 0;
        break;

    case VIRTIO_MMIO_INTERRUPT_STATUS:
        Result = State->InterruptStatus;
        break;

    case VIRTIO_MMIO_STATUS:
        Result = State->Status;
        break;

    case VIRTIO_MMIO_CONFIG_GENERATION:
        Result = State->ConfigGeneration;
        break;

    default:
        Result = 0;
        break;
    }

    *Value = (ULONG64)Result;
    return TRUE;
}

/* ---- MMIO register write handler ---------------------------------------- */

BOOLEAN
RosvVirtioNetMmioWrite(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _In_ ULONG64 Value)
{
    ULONG Offset;
    ULONG Val32 = (ULONG)(Value & 0xFFFFFFFF);
    PROSV_VIRTQUEUE SelectedVq;

    if (GuestPhysicalAddress < ROSV_VIRTIO_NET_MMIO_BASE ||
        GuestPhysicalAddress >= ROSV_VIRTIO_NET_MMIO_BASE + ROSV_VIRTIO_NET_MMIO_SIZE)
    {
        return FALSE;
    }

    Offset = (ULONG)(GuestPhysicalAddress - ROSV_VIRTIO_NET_MMIO_BASE);

    /* Config space writes - currently read-only for virtio-net */
    if (Offset >= VIRTIO_MMIO_CONFIG_SPACE)
    {
        return TRUE;
    }

    /* Select the appropriate virtqueue */
    if (State->QueueSel < VIRTIO_NET_NUM_QUEUES)
        SelectedVq = &State->Vq[State->QueueSel];
    else
        SelectedVq = NULL;

    switch (Offset)
    {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        State->DeviceFeaturesSelPage = Val32;
        break;

    case VIRTIO_MMIO_DRIVER_FEATURES:
        if (State->DriverFeaturesSelPage == 0)
        {
            State->DriverFeatures = (State->DriverFeatures & 0xFFFFFFFF00000000ULL) | Val32;
        }
        else if (State->DriverFeaturesSelPage == 1)
        {
            State->DriverFeatures = (State->DriverFeatures & 0x00000000FFFFFFFFULL) |
                                    ((ULONG64)Val32 << 32);
        }
        break;

    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        State->DriverFeaturesSelPage = Val32;
        break;

    case VIRTIO_MMIO_QUEUE_SEL:
        State->QueueSel = Val32;
        if (Val32 >= VIRTIO_NET_NUM_QUEUES)
        {
            ROSV_TRACE("virtio-net: guest selected queue %u (NumQueues=%u)",
                       Val32, VIRTIO_NET_NUM_QUEUES);
        }
        break;

    case VIRTIO_MMIO_QUEUE_NUM:
        if (SelectedVq != NULL)
        {
            if (Val32 > ROSV_VIRTIO_QUEUE_SIZE_MAX)
            {
                ROSV_ERR("virtio-net: guest requested queue size %u > max %u",
                         Val32, ROSV_VIRTIO_QUEUE_SIZE_MAX);
                Val32 = ROSV_VIRTIO_QUEUE_SIZE_MAX;
            }
            SelectedVq->Num = Val32;
            ROSV_TRACE("virtio-net: queue %u size set to %u", State->QueueSel, Val32);
        }
        break;

    case VIRTIO_MMIO_QUEUE_READY:
        if (SelectedVq != NULL)
        {
            SelectedVq->Ready = (Val32 != 0);
            ROSV_TRACE("virtio-net: queue %u ready = %u", State->QueueSel, Val32);
            if (SelectedVq->Ready)
            {
                ROSV_TRACE("virtio-net: queue %u configured: num=%u desc=0x%llX avail=0x%llX used=0x%llX",
                           State->QueueSel, SelectedVq->Num, SelectedVq->DescGpa,
                           SelectedVq->AvailGpa, SelectedVq->UsedGpa);
                /* Pre-translate ring GPAs to HVAs for fast-path access */
                RosvNetVirtqueueCacheHva(State->OwnerVm, SelectedVq, State->QueueSel);
            }
            else
            {
                /* Queue disabled - invalidate cached HVAs */
                SelectedVq->DescHva = NULL;
                SelectedVq->AvailHva = NULL;
                SelectedVq->UsedHva = NULL;
            }
        }
        break;

    case VIRTIO_MMIO_QUEUE_NOTIFY:
        /* Dispatch based on queue index: even=RX, odd=TX per virtio spec.
         * The value written is the queue index (not QueueSel). */
        if (Val32 < VIRTIO_NET_NUM_QUEUES && Val32 < State->NumQueuePairs * 2)
        {
            ULONG NotifyPairIdx = VIRTIO_NET_QUEUE_PAIR(Val32);

            if (VIRTIO_NET_QUEUE_IS_TX(Val32))
            {
                /* TX queue notification */
                RosvVirtioNetProcessTxQueue(State, NotifyPairIdx);
            }
            else
            {
                /* RX queue notification -- guest posted new RX buffers.
                 * Drain as much staged host traffic from this pair's RxRing
                 * as the queue can currently accept. */
                PROSV_VIRTIO_NET_QUEUE_PAIR NotifyPair = &State->QueuePairs[NotifyPairIdx];
                KIRQL OldIrql;
                ULONG Drained = 0;

                for (;;)
                {
                    UCHAR LocalBuf[ROSV_VIRTIO_NET_MAX_PACKET];
                    ULONG LocalLen;
                    ULONG64 LocalEnqueueQpc;
                    BOOLEAN FromDrain = FALSE;
                    BOOLEAN RetireDrain = FALSE;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                    ULONG64 LocalTraceSeq;
#endif

                    /* Dequeue one packet under lock */
                    KeAcquireSpinLock(&NotifyPair->RxLock, &OldIrql);
                    if (NotifyPair->RxDrainCount == 0 &&
                        NotifyPair->RxRingCount == 0)
                    {
                        KeReleaseSpinLock(&NotifyPair->RxLock, OldIrql);
                        break;
                    }
                    if (NotifyPair->RxDrainCount > 0)
                    {
                        PROSV_VIRTIO_NET_TX_ENTRY Entry = &NotifyPair->RxDrainRing[NotifyPair->RxDrainTail];

                        FromDrain = TRUE;
                        LocalLen = Entry->Length;
                        LocalEnqueueQpc = Entry->EnqueueQpc;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                        LocalTraceSeq = Entry->TraceSeq;
#endif
                        if (LocalLen > ROSV_VIRTIO_NET_MAX_PACKET)
                            LocalLen = ROSV_VIRTIO_NET_MAX_PACKET;
                        RtlCopyMemory(LocalBuf, Entry->Data, LocalLen);
                        Entry->Length = 0;
                        NotifyPair->RxDrainTail = (NotifyPair->RxDrainTail + 1) & (NotifyPair->RxDrainCapacity - 1);
                        NotifyPair->RxDrainCount--;
                        RetireDrain = (NotifyPair->RxDrainCount == 0);
                        KeMemoryBarrier();
                    }
                    else
                    {
                        PROSV_VIRTIO_NET_TX_ENTRY Entry = &NotifyPair->RxRing[NotifyPair->RxRingTail];
                        LocalLen = Entry->Length;
                        LocalEnqueueQpc = Entry->EnqueueQpc;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                        LocalTraceSeq = Entry->TraceSeq;
#endif
                        if (LocalLen > ROSV_VIRTIO_NET_MAX_PACKET)
                            LocalLen = ROSV_VIRTIO_NET_MAX_PACKET;
                        RtlCopyMemory(LocalBuf, Entry->Data, LocalLen);
                        Entry->Length = 0;
                        NotifyPair->RxRingTail = (NotifyPair->RxRingTail + 1) & (NotifyPair->RxRingCapacity - 1);
                        NotifyPair->RxRingCount--;
                        KeMemoryBarrier();
                    }
                    KeReleaseSpinLock(&NotifyPair->RxLock, OldIrql);

                    /* Inject to guest without holding the lock */
                    if (!RosvVirtioNetInjectToRxQueue(State, NotifyPairIdx, LocalBuf, LocalLen))
                    {
                        /* No more guest RX buffers available - re-queue the packet */
                        KeAcquireSpinLock(&NotifyPair->RxLock, &OldIrql);
                        if (FromDrain)
                        {
                            NotifyPair->RxDrainTail = (NotifyPair->RxDrainTail - 1) & (NotifyPair->RxDrainCapacity - 1);
                            {
                                PROSV_VIRTIO_NET_TX_ENTRY Entry = &NotifyPair->RxDrainRing[NotifyPair->RxDrainTail];
                                RtlCopyMemory(Entry->Data, LocalBuf, LocalLen);
                                Entry->Length = LocalLen;
                                Entry->EnqueueQpc = LocalEnqueueQpc;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                                Entry->TraceSeq = LocalTraceSeq;
#endif
                            }
                            NotifyPair->RxDrainCount++;
                        }
                        else
                        {
                            NotifyPair->RxRingTail = (NotifyPair->RxRingTail - 1) & (NotifyPair->RxRingCapacity - 1);
                            {
                                PROSV_VIRTIO_NET_TX_ENTRY Entry = &NotifyPair->RxRing[NotifyPair->RxRingTail];
                                RtlCopyMemory(Entry->Data, LocalBuf, LocalLen);
                                Entry->Length = LocalLen;
                                Entry->EnqueueQpc = LocalEnqueueQpc;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                                Entry->TraceSeq = LocalTraceSeq;
#endif
                            }
                            NotifyPair->RxRingCount++;
                        }
                        KeMemoryBarrier();
                        KeReleaseSpinLock(&NotifyPair->RxLock, OldIrql);
                        break;
                    }

                    if (FromDrain && RetireDrain)
                    {
                        PROSV_VIRTIO_NET_TX_ENTRY RetiredRing = NULL;

                        KeAcquireSpinLock(&NotifyPair->RxLock, &OldIrql);
                        if (NotifyPair->RxDrainCount == 0 &&
                            NotifyPair->RxDrainRing != NULL)
                        {
                            RetiredRing = NotifyPair->RxDrainRing;
                            NotifyPair->RxDrainRing = NULL;
                            NotifyPair->RxDrainCapacity = 0;
                            NotifyPair->RxDrainTail = 0;
                        }
                        KeReleaseSpinLock(&NotifyPair->RxLock, OldIrql);

                        if (RetiredRing != NULL)
                            ExFreePoolWithTag(RetiredRing, 'rRvR');
                    }

                    Drained++;

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                    {
                        ROSV_NET_TRACE_ICMP_ECHO EchoInfo;
                        ULONG64 NowQpc = RosvVirtioNetReadQpc();
                        ULONG64 StageWaitUs = RosvVirtioNetQpcDeltaUs(State, LocalEnqueueQpc, NowQpc);

                        RosvVirtioNetDecodeIcmpEcho(LocalBuf, LocalLen, &EchoInfo);

                        if (EchoInfo.Valid || StageWaitUs >= 10000)
                        {
                            ROSV_TRACE("virtio-net: [PERF] stage->guest pair=%u rx_seq=%llu wait_us=%llu len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
                                       NotifyPairIdx,
                                       LocalTraceSeq,
                                       StageWaitUs,
                                       LocalLen,
                                       EchoInfo.Valid ? (EchoInfo.IsRequest ? "echo-req" : "echo-reply") : "n/a",
                                       EchoInfo.Valid ? EchoInfo.Id : 0,
                                       EchoInfo.Valid ? EchoInfo.Seq : 0,
                                       EchoInfo.Valid ? EchoInfo.SrcIp[0] : 0,
                                       EchoInfo.Valid ? EchoInfo.SrcIp[1] : 0,
                                       EchoInfo.Valid ? EchoInfo.SrcIp[2] : 0,
                                       EchoInfo.Valid ? EchoInfo.SrcIp[3] : 0,
                                       EchoInfo.Valid ? EchoInfo.DstIp[0] : 0,
                                       EchoInfo.Valid ? EchoInfo.DstIp[1] : 0,
                                       EchoInfo.Valid ? EchoInfo.DstIp[2] : 0,
                                       EchoInfo.Valid ? EchoInfo.DstIp[3] : 0);
                        }
                    }
#endif
                }

                if (Drained > 1)
                {
                    ROSV_TRACE("virtio-net: RX pair %u ring drained %u packets, %u remaining",
                               NotifyPairIdx, Drained, NotifyPair->RxRingCount);
                }

                if (State->OwnerVm != NULL && State->OwnerVm->NetBackend != NULL)
                    RosvNetBackendKick(State->OwnerVm);
            }
        }
        else
        {
            ROSV_WARN("virtio-net: QUEUE_NOTIFY for non-existent queue %u (max=%u)",
                      Val32, State->NumQueuePairs * 2);
        }
        break;

    case VIRTIO_MMIO_INTERRUPT_ACK:
    {
        KIRQL IntIrql;
        BOOLEAN StillPending;
        KeAcquireSpinLock(&State->InterruptLock, &IntIrql);
        KeMemoryBarrier();
        State->InterruptStatus &= ~Val32;
        StillPending = (State->InterruptStatus != 0);
        State->InterruptPending = StillPending;
        KeMemoryBarrier();
        KeReleaseSpinLock(&State->InterruptLock, IntIrql);
        if (StillPending)
        {
            /* Wake vCPU if halted - unacknowledged status bits remain */
            if (State->OwnerVm != NULL)
                KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
        }
        break;
    }

    case VIRTIO_MMIO_STATUS:
        if (Val32 == 0)
        {
            ULONG i;
            /* Device reset -- zero all virtqueues */
            ROSV_TRACE("virtio-net: device RESET");
            State->Status = 0;
            State->DriverFeatures = 0;
            {
                KIRQL IntIrql;
                KeAcquireSpinLock(&State->InterruptLock, &IntIrql);
                State->InterruptStatus = 0;
                State->InterruptPending = FALSE;
                KeMemoryBarrier();
                KeReleaseSpinLock(&State->InterruptLock, IntIrql);
            }
            State->QueueSel = 0;
            State->DeviceFeaturesSelPage = 0;
            State->DriverFeaturesSelPage = 0;
            for (i = 0; i < VIRTIO_NET_NUM_QUEUES; i++)
                RtlZeroMemory(&State->Vq[i], sizeof(ROSV_VIRTQUEUE));
        }
        else
        {
            ULONG OldStatus = State->Status;
            State->Status = Val32;
            ROSV_TRACE("virtio-net: STATUS = 0x%02X (was 0x%02X)%s%s%s%s%s",
                       Val32, OldStatus,
                       (Val32 & VIRTIO_STATUS_ACKNOWLEDGE) ? " ACK" : "",
                       (Val32 & VIRTIO_STATUS_DRIVER) ? " DRIVER" : "",
                       (Val32 & VIRTIO_STATUS_FEATURES_OK) ? " FEATURES_OK" : "",
                       (Val32 & VIRTIO_STATUS_DRIVER_OK) ? " DRIVER_OK" : "",
                       (Val32 & VIRTIO_STATUS_FAILED) ? " FAILED" : "");

            if ((Val32 & VIRTIO_STATUS_FEATURES_OK) && !(OldStatus & VIRTIO_STATUS_FEATURES_OK))
            {
                ULONG64 UnsupportedFeatures = State->DriverFeatures & ~State->DeviceFeatures;
                if (UnsupportedFeatures)
                {
                    ROSV_WARN("virtio-net: driver requested unsupported features: 0x%llX",
                              UnsupportedFeatures);
                    State->Status &= ~(ULONG)VIRTIO_STATUS_FEATURES_OK;
                }
                else
                {
                    ROSV_TRACE("virtio-net: feature negotiation OK (driver=0x%llX)",
                               State->DriverFeatures);
                }
            }
        }
        break;

    case VIRTIO_MMIO_QUEUE_DESC_LOW:
        if (SelectedVq != NULL)
            SelectedVq->DescGpa = (SelectedVq->DescGpa & 0xFFFFFFFF00000000ULL) | Val32;
        break;

    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
        if (SelectedVq != NULL)
            SelectedVq->DescGpa = (SelectedVq->DescGpa & 0x00000000FFFFFFFFULL) |
                                  ((ULONG64)Val32 << 32);
        break;

    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
        if (SelectedVq != NULL)
            SelectedVq->AvailGpa = (SelectedVq->AvailGpa & 0xFFFFFFFF00000000ULL) | Val32;
        break;

    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
        if (SelectedVq != NULL)
            SelectedVq->AvailGpa = (SelectedVq->AvailGpa & 0x00000000FFFFFFFFULL) |
                                   ((ULONG64)Val32 << 32);
        break;

    case VIRTIO_MMIO_QUEUE_USED_LOW:
        if (SelectedVq != NULL)
            SelectedVq->UsedGpa = (SelectedVq->UsedGpa & 0xFFFFFFFF00000000ULL) | Val32;
        break;

    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        if (SelectedVq != NULL)
            SelectedVq->UsedGpa = (SelectedVq->UsedGpa & 0x00000000FFFFFFFFULL) |
                                  ((ULONG64)Val32 << 32);
        break;

    default:
        break;
    }

    return TRUE;
}

/* ---- Interrupt management ----------------------------------------------- */

BOOLEAN
RosvVirtioNetHasPendingInterrupt(
    _In_ PROSV_VIRTIO_NET_STATE State)
{
    KeMemoryBarrier();
    return State->InterruptPending;
}

VOID
RosvVirtioNetClearPendingInterrupt(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    KIRQL IntIrql;
    KeAcquireSpinLock(&State->InterruptLock, &IntIrql);
    State->InterruptPending = FALSE;
    KeMemoryBarrier();
    KeReleaseSpinLock(&State->InterruptLock, IntIrql);
}

VOID
RosvVirtioNetOnGuestEoi(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    /* Re-assert interrupt if there are still pending status bits.
     * This handles the case where the guest EOI'd but we still have
     * unacknowledged interrupt status. */
    KIRQL IntIrql;
    BOOLEAN StillPending;

    KeAcquireSpinLock(&State->InterruptLock, &IntIrql);
    StillPending = (State->InterruptStatus != 0);
    if (StillPending)
        State->InterruptPending = TRUE;
    KeMemoryBarrier();
    KeReleaseSpinLock(&State->InterruptLock, IntIrql);

    if (StillPending)
    {
        /* Wake vCPU if halted so it can re-inject after EOI */
        if (State->OwnerVm != NULL)
            KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
    }
}

/* ---- Interrupt coalescing ------------------------------------------------ */

BOOLEAN
RosvVirtioNetShouldInjectIrq(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_ BOOLEAN GuestIsHalted)
{
    PROSV_VIRTIO_NET_IRQ_COALESCE Coal = &State->IrqCoalesce;

    /* Always wake a halted guest immediately */
    if (GuestIsHalted)
        return TRUE;

    /* No coalescing configured (MaxPackets <= 1): always inject */
    if (Coal->CoalesceMaxPackets <= 1 && Coal->CoalesceMaxUsec == 0)
        return TRUE;

    Coal->PendingCount++;

    /* Packet count threshold reached */
    if (Coal->CoalesceMaxPackets > 1 && Coal->PendingCount >= Coal->CoalesceMaxPackets)
        return TRUE;

    /* Time threshold reached */
    if (Coal->CoalesceMaxUsec > 0 && Coal->LastIrqQpc != 0)
    {
        LARGE_INTEGER Now = KeQueryPerformanceCounter(NULL);
        LONG64 DeltaQpc = Now.QuadPart - Coal->LastIrqQpc;
        LONG64 FreqQpc = State->PerfFrequency.QuadPart;

        if (FreqQpc > 0)
        {
            /* Convert delta to microseconds: DeltaQpc * 1000000 / FreqQpc */
            ULONG64 DeltaUsec = (ULONG64)DeltaQpc * 1000000ULL / (ULONG64)FreqQpc;
            if (DeltaUsec >= Coal->CoalesceMaxUsec)
                return TRUE;
        }
    }

    /* First packet ever — inject to establish the baseline */
    if (Coal->LastIrqQpc == 0)
        return TRUE;

    return FALSE;
}

VOID
RosvVirtioNetRecordIrqInjected(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    PROSV_VIRTIO_NET_IRQ_COALESCE Coal = &State->IrqCoalesce;
    LARGE_INTEGER Now = KeQueryPerformanceCounter(NULL);

    Coal->PendingCount = 0;
    Coal->LastIrqQpc = Now.QuadPart;
}

/* ---- TX dequeue (host picks up guest-sent packets) ---------------------- */

BOOLEAN
RosvVirtioNetDequeueTxPacket(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _Out_ PROSV_NET_PACKET PacketOut)
{
    KIRQL OldIrql;
    BOOLEAN Got = FALSE;
    BOOLEAN ResumeTx = FALSE;
    ULONG ResumePairIdx = 0;
    ULONG DequeuePairIdx = 0;
    ULONG64 EnqueueQpc = 0;
    ULONG64 TraceSeq = 0;
    ULONG PairIdx;

    /* Round-robin dequeue across all active queue pairs.
     * Check each pair's TxRing and return the first available packet.
     * This ensures all TX queues are serviced fairly by the host backend. */
    for (PairIdx = 0; PairIdx < State->NumQueuePairs; PairIdx++)
    {
        PROSV_VIRTIO_NET_QUEUE_PAIR Pair = &State->QueuePairs[PairIdx];
        PROSV_VIRTIO_NET_TX_ENTRY RetiredRing = NULL;

        KeAcquireSpinLock(&Pair->TxRingLock, &OldIrql);

        if (Pair->TxDrainCount > 0)
        {
            PROSV_VIRTIO_NET_TX_ENTRY Entry = &Pair->TxDrainRing[Pair->TxDrainTail];

            if (Entry->Length > 0 && Entry->Length <= ROSV_NET_PACKET_MAX)
            {
                PacketOut->Length = Entry->Length;
                RtlCopyMemory(PacketOut->Data, Entry->Data, Entry->Length);
                EnqueueQpc = Entry->EnqueueQpc;
                TraceSeq = Entry->TraceSeq;
                Got = TRUE;
                DequeuePairIdx = PairIdx;
            }

            Entry->Length = 0;
            Entry->EnqueueQpc = 0;
            Entry->TraceSeq = 0;
            Pair->TxDrainTail = (Pair->TxDrainTail + 1) & (Pair->TxDrainCapacity - 1);
            Pair->TxDrainCount--;
            if (Pair->TxDrainCount == 0)
            {
                RetiredRing = Pair->TxDrainRing;
                Pair->TxDrainRing = NULL;
                Pair->TxDrainCapacity = 0;
                Pair->TxDrainTail = 0;
            }
        }
        else if (Pair->TxRingCount > 0)
        {
            PROSV_VIRTIO_NET_TX_ENTRY Entry = &Pair->TxRing[Pair->TxRingTail];

            if (Entry->Length > 0 && Entry->Length <= ROSV_NET_PACKET_MAX)
            {
                PacketOut->Length = Entry->Length;
                RtlCopyMemory(PacketOut->Data, Entry->Data, Entry->Length);
                EnqueueQpc = Entry->EnqueueQpc;
                TraceSeq = Entry->TraceSeq;
                Got = TRUE;
                DequeuePairIdx = PairIdx;
            }

            Entry->Length = 0;
            Entry->EnqueueQpc = 0;
            Entry->TraceSeq = 0;
            Pair->TxRingTail = (Pair->TxRingTail + 1) & (Pair->TxRingCapacity - 1);
            Pair->TxRingCount--;
            ResumeTx = Pair->TxBackpressure;
            ResumePairIdx = PairIdx;
        }

        KeReleaseSpinLock(&Pair->TxRingLock, OldIrql);

        if (RetiredRing != NULL)
            ExFreePoolWithTag(RetiredRing, 'tRvR');

        if (Got)
            break;
    }

    if (ResumeTx)
        RosvVirtioNetProcessTxQueue(State, ResumePairIdx);

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
    if (Got)
    {
        ROSV_NET_TRACE_ICMP_ECHO EchoInfo;
        ULONG64 NowQpc = RosvVirtioNetReadQpc();
        ULONG64 QueueDelayUs = RosvVirtioNetQpcDeltaUs(State, EnqueueQpc, NowQpc);

        RosvVirtioNetDecodeIcmpEcho(PacketOut->Data, PacketOut->Length, &EchoInfo);
        if (EchoInfo.Valid || QueueDelayUs >= 10000)
        {
            ROSV_TRACE("virtio-net: [PERF] ring->netrx tx_seq=%llu delay_us=%llu pair=%u len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
                       TraceSeq,
                       QueueDelayUs,
                       DequeuePairIdx,
                       PacketOut->Length,
                       EchoInfo.Valid ? (EchoInfo.IsRequest ? "echo-req" : "echo-reply") : "n/a",
                       EchoInfo.Valid ? EchoInfo.Id : 0,
                       EchoInfo.Valid ? EchoInfo.Seq : 0,
                       EchoInfo.Valid ? EchoInfo.SrcIp[0] : 0,
                       EchoInfo.Valid ? EchoInfo.SrcIp[1] : 0,
                       EchoInfo.Valid ? EchoInfo.SrcIp[2] : 0,
                       EchoInfo.Valid ? EchoInfo.SrcIp[3] : 0,
                       EchoInfo.Valid ? EchoInfo.DstIp[0] : 0,
                       EchoInfo.Valid ? EchoInfo.DstIp[1] : 0,
                       EchoInfo.Valid ? EchoInfo.DstIp[2] : 0,
                       EchoInfo.Valid ? EchoInfo.DstIp[3] : 0);
        }
    }
#else
    UNREFERENCED_PARAMETER(TraceSeq);
    UNREFERENCED_PARAMETER(EnqueueQpc);
    UNREFERENCED_PARAMETER(DequeuePairIdx);
#endif

    return Got;
}

/* ---- RX inject (host pushes packet to guest) ---------------------------- */

BOOLEAN
RosvVirtioNetInjectRxPacket(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    /* Host-side RX injection always targets pair 0 for now.
     * Future: RSS/flow-steering could hash the packet to select a pair. */
    PROSV_VIRTIO_NET_QUEUE_PAIR Pair = &State->QueuePairs[0];
    KIRQL OldIrql;
    ULONG64 StartQpc = RosvVirtioNetReadQpc();
    ULONG64 TraceSeq;

    if (Length == 0 || Length > ROSV_VIRTIO_NET_MAX_PACKET)
    {
        ROSV_ERR("virtio-net: RX inject invalid length %u", Length);
        return FALSE;
    }

    /* First try to inject directly into the RX virtqueue (pair 0) */
    if (RosvVirtioNetInjectToRxQueue(State, 0, Data, Length))
    {
        TraceSeq = ++State->RxTraceSeqNext;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
        {
            ROSV_NET_TRACE_ICMP_ECHO EchoInfo;
            RosvVirtioNetDecodeIcmpEcho(Data, Length, &EchoInfo);
            if (EchoInfo.Valid)
            {
                ULONG64 EndQpc = RosvVirtioNetReadQpc();
                ULONG64 InjectUs = RosvVirtioNetQpcDeltaUs(State, StartQpc, EndQpc);

                ROSV_TRACE("virtio-net: [PERF] nettx->guest rx_seq=%llu direct_us=%llu len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
                           TraceSeq,
                           InjectUs,
                           Length,
                           EchoInfo.IsRequest ? "echo-req" : "echo-reply",
                           EchoInfo.Id,
                           EchoInfo.Seq,
                           EchoInfo.SrcIp[0], EchoInfo.SrcIp[1], EchoInfo.SrcIp[2], EchoInfo.SrcIp[3],
                           EchoInfo.DstIp[0], EchoInfo.DstIp[1], EchoInfo.DstIp[2], EchoInfo.DstIp[3]);
            }
        }
#else
        UNREFERENCED_PARAMETER(TraceSeq);
        UNREFERENCED_PARAMETER(StartQpc);
#endif
        /* Wake both the network-yield wait and the HLT-idle wait so
         * an otherwise idle guest takes the RX interrupt immediately
         * instead of waiting for the fallback HLT timeout. */
        KeSetEvent(&State->RxPendingEvent, IO_NETWORK_INCREMENT, FALSE);
        KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
        return TRUE;
    }

    /* No RX buffers available. Enqueue into pair 0's RX ring for later delivery
     * when the guest posts new RX buffers (QUEUE_NOTIFY on queue 0). */
    KeAcquireSpinLock(&Pair->RxLock, &OldIrql);

    if (Pair->RxRingCount >= Pair->RxRingCapacity)
    {
        /* Ring full. Drop this packet and track in state struct. */
        Pair->RxDropCount++;
        State->RxDropCount++;
        if (State->RxDropCount <= 16 || (State->RxDropCount % 1024) == 0)
        {
            ROSV_WARN("virtio-net: RX ring full (%u/%u), dropping packet (%u bytes), "
                      "total drops=%u", Pair->RxRingCount,
                      Pair->RxRingCapacity, Length, State->RxDropCount);
        }
        KeReleaseSpinLock(&Pair->RxLock, OldIrql);
        return FALSE;
    }

    {
        PROSV_VIRTIO_NET_TX_ENTRY Entry = &Pair->RxRing[Pair->RxRingHead];
        RtlCopyMemory(Entry->Data, Data, Length);
        Entry->Length = Length;
        TraceSeq = ++State->RxTraceSeqNext;
        Entry->TraceSeq = TraceSeq;
        Entry->EnqueueQpc = StartQpc;
        Pair->RxRingHead = (Pair->RxRingHead + 1) & (Pair->RxRingCapacity - 1);
        Pair->RxRingCount++;
        KeMemoryBarrier();
    }

    KeReleaseSpinLock(&Pair->RxLock, OldIrql);

    /* Wake both the network-yield wait and the HLT-idle wait so the
     * guest notices staged RX work without the 10 ms HLT timeout tail. */
    KeSetEvent(&State->RxPendingEvent, IO_NETWORK_INCREMENT, FALSE);
    KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
    {
        ROSV_NET_TRACE_ICMP_ECHO EchoInfo;
        RosvVirtioNetDecodeIcmpEcho(Data, Length, &EchoInfo);
        if (EchoInfo.Valid)
        {
            ROSV_TRACE("virtio-net: [PERF] nettx->stage rx_seq=%llu len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
                       TraceSeq,
                       Length,
                       EchoInfo.IsRequest ? "echo-req" : "echo-reply",
                       EchoInfo.Id,
                       EchoInfo.Seq,
                       EchoInfo.SrcIp[0], EchoInfo.SrcIp[1], EchoInfo.SrcIp[2], EchoInfo.SrcIp[3],
                       EchoInfo.DstIp[0], EchoInfo.DstIp[1], EchoInfo.DstIp[2], EchoInfo.DstIp[3]);
        }
    }
#endif

    return TRUE;
}

/* ---- Dynamic ring resize ------------------------------------------------ */

/*
 * Allocate a ring buffer of the given entry count.
 * Returns NULL on failure. Tag distinguishes TX ('tRvR') from RX ('rRvR').
 */
static PROSV_VIRTIO_NET_TX_ENTRY
RosvVirtioNetAllocRing(
    _In_ ULONG EntryCount,
    _In_ ULONG Tag)
{
    SIZE_T Bytes = (SIZE_T)EntryCount * sizeof(ROSV_VIRTIO_NET_TX_ENTRY);
    PROSV_VIRTIO_NET_TX_ENTRY Ring;

    Ring = (PROSV_VIRTIO_NET_TX_ENTRY)ExAllocatePoolWithTag(NonPagedPool, Bytes, Tag);
    if (Ring != NULL)
        RtlZeroMemory(Ring, Bytes);
    return Ring;
}

/*
 * Attempt to resize a ring (TX or RX). Called from the DPC timer callback
 * at DISPATCH_LEVEL. A fresh ring is allocated first, then swapped in under
 * the spin lock. If the old ring still has live entries, it becomes a
 * drain-only ring and is freed after consumers retire it.
 *
 * Returns TRUE if resize happened.
 */
static BOOLEAN
RosvVirtioNetResizeRing(
    _Inout_ PROSV_VIRTIO_NET_TX_ENTRY *RingPtr,
    _Inout_ PULONG Capacity,
    _Inout_ PULONG Head,
    _Inout_ PULONG Tail,
    _Inout_ PULONG Count,
    _Inout_ PROSV_VIRTIO_NET_TX_ENTRY *DrainRingPtr,
    _Inout_ PULONG DrainCapacity,
    _Inout_ PULONG DrainTail,
    _Inout_ PULONG DrainCount,
    _In_ PKSPIN_LOCK Lock,
    _In_ ULONG NewCapacity,
    _In_ ULONG Tag,
    _In_ const char *Name)
{
    PROSV_VIRTIO_NET_TX_ENTRY NewRing;
    PROSV_VIRTIO_NET_TX_ENTRY OldRing;
    ULONG OldCapacity;
    ULONG OldTail;
    ULONG OldCount;
    KIRQL OldIrql;

    /* Validate power-of-2 and bounds */
    if ((NewCapacity & (NewCapacity - 1)) != 0 ||
        NewCapacity < ROSV_VIRTIO_NET_RING_SIZE_MIN ||
        NewCapacity > ROSV_VIRTIO_NET_RING_SIZE_MAX)
    {
        return FALSE;
    }

    /* Pre-allocate before taking the lock */
    NewRing = RosvVirtioNetAllocRing(NewCapacity, Tag);
    if (NewRing == NULL)
    {
        ROSV_WARN("virtio-net: %s resize alloc failed (%u entries)", Name, NewCapacity);
        return FALSE;
    }

    KeAcquireSpinLock(Lock, &OldIrql);

    OldRing = *RingPtr;
    OldCapacity = *Capacity;
    OldTail = *Tail;
    OldCount = *Count;

    if (*DrainRingPtr != NULL ||
        OldRing == NULL ||
        OldCapacity == NewCapacity)
    {
        KeReleaseSpinLock(Lock, OldIrql);
        ExFreePoolWithTag(NewRing, Tag);
        return FALSE;
    }

    *RingPtr = NewRing;
    *Capacity = NewCapacity;
    *Head = 0;
    *Tail = 0;
    *Count = 0;

    if (OldCount > 0)
    {
        *DrainRingPtr = OldRing;
        *DrainCapacity = OldCapacity;
        *DrainTail = OldTail;
        *DrainCount = OldCount;
        OldRing = NULL;
    }

    KeReleaseSpinLock(Lock, OldIrql);

    if (OldRing != NULL)
        ExFreePoolWithTag(OldRing, Tag);

    if (OldCount > 0)
    {
        ROSV_TRACE("virtio-net: %s resized %u -> %u (draining %u entries)",
                   Name, OldCapacity, NewCapacity, OldCount);
    }
    else
    {
        ROSV_TRACE("virtio-net: %s resized %u -> %u (idle swap)",
                   Name, OldCapacity, NewCapacity);
    }
    return TRUE;
}

/*
 * DPC callback invoked every 1 second by the ring resize timer.
 * Checks TX and RX ring occupancy (pair 0) and triggers grow/shrink as needed.
 *
 * Runs at DISPATCH_LEVEL. Allocation from NonPagedPool is safe here.
 */
static
KDEFERRED_ROUTINE RosvVirtioNetRingResizeDpcRoutine;

static
VOID
NTAPI
RosvVirtioNetRingResizeDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PROSV_VIRTIO_NET_STATE State = (PROSV_VIRTIO_NET_STATE)DeferredContext;
    PROSV_VIRTIO_NET_QUEUE_PAIR Pair;
    ULONG TxCount, TxCap, RxCount, RxCap;
    ULONG TxPct, RxPct;
    BOOLEAN TxDrainPending, RxDrainPending;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (State == NULL)
        return;

    /* Operate on pair 0 for now (future: iterate all active pairs) */
    Pair = &State->QueuePairs[0];

    /* Snapshot current ring state (no lock needed for read of ULONG on x64;
     * worst case we see a slightly stale value, which is fine for heuristics). */
    KeMemoryBarrier();
    TxCount = Pair->TxRingCount;
    TxCap   = Pair->TxRingCapacity;
    RxCount = Pair->RxRingCount;
    RxCap   = Pair->RxRingCapacity;
    TxDrainPending = (Pair->TxDrainRing != NULL);
    RxDrainPending = (Pair->RxDrainRing != NULL);

    /* Update high watermarks (relaxed, for stats only) */
    if (TxCount > State->TxRingHighWatermark)
        State->TxRingHighWatermark = TxCount;
    if (RxCount > State->RxRingHighWatermark)
        State->RxRingHighWatermark = RxCount;

    /* ---- TX ring resize decision ---- */
    TxPct = (TxCap > 0) ? (TxCount * 100 / TxCap) : 0;

    if (TxDrainPending)
    {
        State->TxGrowCounter = 0;
        State->TxShrinkCounter = 0;
    }
    else if (TxPct > ROSV_VIRTIO_NET_RING_GROW_PCT)
    {
        State->TxShrinkCounter = 0;
        State->TxGrowCounter++;
        if (State->TxGrowCounter >= ROSV_VIRTIO_NET_RING_GROW_CHECKS &&
            TxCap < ROSV_VIRTIO_NET_RING_SIZE_MAX)
        {
            ULONG NewCap = TxCap * 2;
            if (NewCap > ROSV_VIRTIO_NET_RING_SIZE_MAX)
                NewCap = ROSV_VIRTIO_NET_RING_SIZE_MAX;

            if (RosvVirtioNetResizeRing(
                    &Pair->TxRing, &Pair->TxRingCapacity,
                    &Pair->TxRingHead, &Pair->TxRingTail,
                    &Pair->TxRingCount,
                    &Pair->TxDrainRing, &Pair->TxDrainCapacity,
                    &Pair->TxDrainTail, &Pair->TxDrainCount,
                    &Pair->TxRingLock,
                    NewCap, 'tRvR', "TX"))
            {
                State->RingResizeCount++;
            }
            State->TxGrowCounter = 0;
        }
    }
    else if (TxPct < ROSV_VIRTIO_NET_RING_SHRINK_PCT)
    {
        State->TxGrowCounter = 0;
        State->TxShrinkCounter++;
        if (State->TxShrinkCounter >= ROSV_VIRTIO_NET_RING_SHRINK_SECS &&
            TxCap > ROSV_VIRTIO_NET_TX_RING_SHRINK_FLOOR)
        {
            ULONG NewCap = TxCap / 2;
            if (NewCap < ROSV_VIRTIO_NET_TX_RING_SHRINK_FLOOR)
                NewCap = ROSV_VIRTIO_NET_TX_RING_SHRINK_FLOOR;

            if (RosvVirtioNetResizeRing(
                    &Pair->TxRing, &Pair->TxRingCapacity,
                    &Pair->TxRingHead, &Pair->TxRingTail,
                    &Pair->TxRingCount,
                    &Pair->TxDrainRing, &Pair->TxDrainCapacity,
                    &Pair->TxDrainTail, &Pair->TxDrainCount,
                    &Pair->TxRingLock,
                    NewCap, 'tRvR', "TX"))
            {
                State->RingResizeCount++;
            }
            State->TxShrinkCounter = 0;
        }
    }
    else
    {
        /* Occupancy in the neutral zone: reset both counters */
        State->TxGrowCounter = 0;
        State->TxShrinkCounter = 0;
    }

    /* ---- RX ring resize decision ---- */
    RxPct = (RxCap > 0) ? (RxCount * 100 / RxCap) : 0;

    if (RxDrainPending)
    {
        State->RxGrowCounter = 0;
        State->RxShrinkCounter = 0;
    }
    else if (RxPct > ROSV_VIRTIO_NET_RING_GROW_PCT)
    {
        State->RxShrinkCounter = 0;
        State->RxGrowCounter++;
        if (State->RxGrowCounter >= ROSV_VIRTIO_NET_RING_GROW_CHECKS &&
            RxCap < ROSV_VIRTIO_NET_RING_SIZE_MAX)
        {
            ULONG NewCap = RxCap * 2;
            if (NewCap > ROSV_VIRTIO_NET_RING_SIZE_MAX)
                NewCap = ROSV_VIRTIO_NET_RING_SIZE_MAX;

            if (RosvVirtioNetResizeRing(
                    &Pair->RxRing, &Pair->RxRingCapacity,
                    &Pair->RxRingHead, &Pair->RxRingTail,
                    &Pair->RxRingCount,
                    &Pair->RxDrainRing, &Pair->RxDrainCapacity,
                    &Pair->RxDrainTail, &Pair->RxDrainCount,
                    &Pair->RxLock,
                    NewCap, 'rRvR', "RX"))
            {
                State->RingResizeCount++;
            }
            State->RxGrowCounter = 0;
        }
    }
    else if (RxPct < ROSV_VIRTIO_NET_RING_SHRINK_PCT)
    {
        State->RxGrowCounter = 0;
        State->RxShrinkCounter++;
        if (State->RxShrinkCounter >= ROSV_VIRTIO_NET_RING_SHRINK_SECS &&
            RxCap > ROSV_VIRTIO_NET_RING_SIZE_MIN)
        {
            ULONG NewCap = RxCap / 2;
            if (NewCap < ROSV_VIRTIO_NET_RING_SIZE_MIN)
                NewCap = ROSV_VIRTIO_NET_RING_SIZE_MIN;

            if (RosvVirtioNetResizeRing(
                    &Pair->RxRing, &Pair->RxRingCapacity,
                    &Pair->RxRingHead, &Pair->RxRingTail,
                    &Pair->RxRingCount,
                    &Pair->RxDrainRing, &Pair->RxDrainCapacity,
                    &Pair->RxDrainTail, &Pair->RxDrainCount,
                    &Pair->RxLock,
                    NewCap, 'rRvR', "RX"))
            {
                State->RingResizeCount++;
            }
            State->RxShrinkCounter = 0;
        }
    }
    else
    {
        State->RxGrowCounter = 0;
        State->RxShrinkCounter = 0;
    }
}

VOID
RosvVirtioNetStartResizeTimer(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    LARGE_INTEGER DueTime;

    if (State->RingResizeTimerActive)
        return;

    KeInitializeTimer(&State->RingResizeTimer);
    KeInitializeDpc(&State->RingResizeDpc,
                    RosvVirtioNetRingResizeDpcRoutine,
                    State);

    /* Period = 1 second. DueTime is relative (negative = relative in 100ns units). */
    DueTime.QuadPart = -10000000LL;  /* -1 second */
    KeSetTimerEx(&State->RingResizeTimer,
                 DueTime,
                 1000,  /* 1000 ms period */
                 &State->RingResizeDpc);

    State->RingResizeTimerActive = TRUE;
    ROSV_TRACE("virtio-net: ring resize timer started (1-second interval)");
}

VOID
RosvVirtioNetStopResizeTimer(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    if (!State->RingResizeTimerActive)
        return;

    KeCancelTimer(&State->RingResizeTimer);
    KeFlushQueuedDpcs();
    State->RingResizeTimerActive = FALSE;
    ROSV_TRACE("virtio-net: ring resize timer stopped");
}

/* ---- Initialization and lifecycle --------------------------------------- */

NTSTATUS
RosvVirtioNetInitialize(
    _Out_ PROSV_VIRTIO_NET_STATE State,
    _In_ PROSV_VM Vm,
    _In_reads_(6) const UCHAR *MacAddress)
{
    ULONG i;

    RtlZeroMemory(State, sizeof(ROSV_VIRTIO_NET_STATE));
    KeQueryPerformanceCounter(&State->PerfFrequency);
    if (State->PerfFrequency.QuadPart <= 0)
        State->PerfFrequency.QuadPart = 1;

    State->OwnerVm = Vm;

    /* Multi-queue setup: advertise ROSV_NET_MAX_QUEUE_PAIRS TX/RX pairs.
     * When the guest negotiates VIRTIO_NET_F_MQ, it reads MaxVirtqueuePairs
     * from config space and activates up to that many pairs.
     * Without MQ negotiation, only pair 0 (queues 0/1) is used. */
    /* TODO: Enable multi-queue (ROSV_NET_MAX_QUEUE_PAIRS) once MQ config space
     * layout, per-pair QUEUE_NOTIFY dispatch, and round-robin TX dequeue are
     * validated against the Linux virtio-net driver. The infrastructure is in
     * place (QueuePairs[], pair-indexed ProcessTxQueue/InjectToRxQueue) but
     * advertising VIRTIO_NET_F_MQ broke guest driver init. */
    State->NumQueuePairs = 1;

    /* Set device features.
     *
     * TODO: Re-enable these once the backend properly handles them:
     *   - VIRTIO_NET_F_CSUM: need RosvVirtioNetFinalizeCsum() to compute
     *     partial checksum at [CsumStart..end] and write at CsumStart+CsumOffset.
     *     Also set VIRTIO_NET_HDR_F_DATA_VALID on RX inject.
     *   - VIRTIO_NET_F_GUEST_CSUM: requires CSUM to be working first.
     *   - VIRTIO_NET_F_HOST_TSO4/TSO6: need RosvVirtioNetSegmentTso() to split
     *     large TCP segments by MSS. Backend must accept >MTU payloads via WSK.
     *     Also needs TsoGatherBuf (64KB) allocation in init.
     *   - VIRTIO_NET_F_MQ: see NumQueuePairs TODO above. Config space must
     *     expose MaxVirtqueuePairs at the correct offset per virtio spec. */
    State->DeviceFeatures = VIRTIO_F_VERSION_1 |
                            VIRTIO_NET_F_MAC |
                            VIRTIO_NET_F_STATUS;

    /* Build device configuration */
    RtlCopyMemory(State->Config.Mac, MacAddress, 6);
    State->Config.Status = VIRTIO_NET_S_LINK_UP;
    State->Config.MaxVirtqueuePairs = (USHORT)State->NumQueuePairs;
    State->Config.Mtu = 1500;

    /* Initialize shared locks and events */
    KeInitializeSpinLock(&State->InterruptLock);
    KeInitializeEvent(&State->TxReadyEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&State->RxPendingEvent, SynchronizationEvent, FALSE);

    /* Initialize active queue-pair state.
     * Only queue pairs exposed to the guest allocate ring buffers.
     * The resize timer can grow/shrink pair 0's rings at runtime. */
    for (i = 0; i < State->NumQueuePairs; i++)
    {
        PROSV_VIRTIO_NET_QUEUE_PAIR Pair = &State->QueuePairs[i];

        KeInitializeSpinLock(&Pair->TxRingLock);
        ExInitializeFastMutex(&Pair->TxQueueMutex);
        KeInitializeSpinLock(&Pair->RxLock);
        ExInitializeFastMutex(&Pair->RxQueueMutex);

        Pair->TxRingHead = 0;
        Pair->TxRingTail = 0;
        Pair->TxRingCount = 0;
        Pair->TxDrainRing = NULL;
        Pair->TxDrainCapacity = 0;
        Pair->TxDrainTail = 0;
        Pair->TxDrainCount = 0;
        Pair->TxBackpressure = FALSE;
        Pair->RxRingHead = 0;
        Pair->RxRingTail = 0;
        Pair->RxRingCount = 0;
        Pair->RxDrainRing = NULL;
        Pair->RxDrainCapacity = 0;
        Pair->RxDrainTail = 0;
        Pair->RxDrainCount = 0;

        /* Dynamically allocate TX ring at initial size */
        Pair->TxRingCapacity = ROSV_VIRTIO_NET_RING_SIZE_INIT;
        Pair->TxRing = RosvVirtioNetAllocRing(Pair->TxRingCapacity, 'tRvR');
        if (Pair->TxRing == NULL)
        {
            ROSV_ERR("virtio-net: failed to allocate TX ring for pair %u (%u entries)",
                     i, Pair->TxRingCapacity);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Dynamically allocate RX ring at initial size */
        Pair->RxRingCapacity = ROSV_VIRTIO_NET_RING_SIZE_INIT;
        Pair->RxRing = RosvVirtioNetAllocRing(Pair->RxRingCapacity, 'rRvR');
        if (Pair->RxRing == NULL)
        {
            ROSV_ERR("virtio-net: failed to allocate RX ring for pair %u (%u entries)",
                     i, Pair->RxRingCapacity);
            ExFreePoolWithTag(Pair->TxRing, 'tRvR');
            Pair->TxRing = NULL;
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /* Initialize resize counters and stats */
    State->TxGrowCounter = 0;
    State->TxShrinkCounter = 0;
    State->RxGrowCounter = 0;
    State->RxShrinkCounter = 0;
    State->TxRingHighWatermark = 0;
    State->RxRingHighWatermark = 0;
    State->RingResizeCount = 0;
    State->RingResizeTimerActive = FALSE;

    /* Allocate TSO scratch buffers for GSO packet gathering and segmentation */
    State->TsoGatherBuf = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                                         ROSV_VIRTIO_NET_TSO_MAX_PACKET,
                                                         'gTvR');
    State->TsoSegBuf = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool,
                                                      ROSV_VIRTIO_NET_MAX_PACKET,
                                                      'sTvR');
    if (State->TsoGatherBuf == NULL || State->TsoSegBuf == NULL)
    {
        ROSV_WARN("virtio-net: TSO buffer allocation failed, GSO will be disabled");
        if (State->TsoGatherBuf != NULL)
        {
            ExFreePoolWithTag(State->TsoGatherBuf, 'gTvR');
            State->TsoGatherBuf = NULL;
        }
        if (State->TsoSegBuf != NULL)
        {
            ExFreePoolWithTag(State->TsoSegBuf, 'sTvR');
            State->TsoSegBuf = NULL;
        }
    }

    /* Initialize interrupt coalescing.
     * Batch up to 12 RX packets per interrupt, with a 75 us timer-based flush
     * to bound latency when traffic is light. Both QEMU and VBox coalesce
     * interrupts similarly. A halted guest is always woken immediately
     * (see RosvVirtioNetShouldInjectIrq). */
    State->IrqCoalesce.PendingCount = 0;
    State->IrqCoalesce.LastIrqQpc = 0;
    State->IrqCoalesce.CoalesceMaxPackets = 12;
    State->IrqCoalesce.CoalesceMaxUsec = 75;

    /* Start the periodic ring resize timer */
    RosvVirtioNetStartResizeTimer(State);

    ROSV_ERR("virtio-net: initialized at MMIO 0x%llX-0x%llX, IRQ %u, "
             "MAC=%02X:%02X:%02X:%02X:%02X:%02X, features=0x%llX, "
             "num_queue_pairs=%u, qpc_freq=%lld, ring_init=%u",
             ROSV_VIRTIO_NET_MMIO_BASE,
             ROSV_VIRTIO_NET_MMIO_BASE + ROSV_VIRTIO_NET_MMIO_SIZE - 1,
             ROSV_VIRTIO_NET_IRQ,
             MacAddress[0], MacAddress[1], MacAddress[2],
             MacAddress[3], MacAddress[4], MacAddress[5],
             State->DeviceFeatures,
             State->NumQueuePairs,
             State->PerfFrequency.QuadPart,
             ROSV_VIRTIO_NET_RING_SIZE_INIT);

    return STATUS_SUCCESS;
}

VOID
RosvVirtioNetDestroy(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    ULONG i;

    ROSV_ERR("virtio-net: destroy (tx=%llu/%llu bytes, rx=%llu/%llu bytes, "
             "tx_drops=%u, rx_drops=%u, num_queue_pairs=%u, "
             "tx_hwm=%u, rx_hwm=%u, resizes=%u)",
             State->TxPackets, State->TxBytes,
             State->RxPackets, State->RxBytes,
             State->TxDropCount, State->RxDropCount,
             State->NumQueuePairs,
             State->TxRingHighWatermark, State->RxRingHighWatermark,
             State->RingResizeCount);

    /* Stop the ring resize timer before freeing ring buffers */
    RosvVirtioNetStopResizeTimer(State);

    /* Free TSO scratch buffers */
    if (State->TsoGatherBuf != NULL)
    {
        ExFreePoolWithTag(State->TsoGatherBuf, 'gTvR');
        State->TsoGatherBuf = NULL;
    }
    if (State->TsoSegBuf != NULL)
    {
        ExFreePoolWithTag(State->TsoSegBuf, 'sTvR');
        State->TsoSegBuf = NULL;
    }

    /* Free dynamically allocated ring buffers for all pairs */
    for (i = 0; i < ROSV_NET_MAX_QUEUE_PAIRS; i++)
    {
        PROSV_VIRTIO_NET_QUEUE_PAIR Pair = &State->QueuePairs[i];
        if (Pair->TxRing != NULL)
        {
            ExFreePoolWithTag(Pair->TxRing, 'tRvR');
            Pair->TxRing = NULL;
        }
        if (Pair->TxDrainRing != NULL)
        {
            ExFreePoolWithTag(Pair->TxDrainRing, 'tRvR');
            Pair->TxDrainRing = NULL;
        }
        if (Pair->RxRing != NULL)
        {
            ExFreePoolWithTag(Pair->RxRing, 'rRvR');
            Pair->RxRing = NULL;
        }
        if (Pair->RxDrainRing != NULL)
        {
            ExFreePoolWithTag(Pair->RxDrainRing, 'rRvR');
            Pair->RxDrainRing = NULL;
        }
    }

    RtlZeroMemory(State, sizeof(ROSV_VIRTIO_NET_STATE));
}

/* ---- RX backpressure helper --------------------------------------------- */

BOOLEAN
RosvVirtioNetRxRingHasSpace(
    _In_ PROSV_VIRTIO_NET_STATE State)
{
    /* Check pair 0 (host-side RX injection targets pair 0) */
    BOOLEAN HasSpace;
    KeMemoryBarrier();
    HasSpace = (State->QueuePairs[0].RxRingCount < State->QueuePairs[0].RxRingCapacity);
    return HasSpace;
}
