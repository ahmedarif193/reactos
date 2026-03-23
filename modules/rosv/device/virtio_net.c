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

/* ---- TX path: guest -> host --------------------------------------------- */

/*
 * Process all pending packets in the TX virtqueue (queue 1).
 * For each packet:
 *   1. Read the descriptor chain (virtio_net_hdr + data)
 *   2. Strip the virtio_net_hdr (we don't need GSO/csum offload)
 *   3. Copy the raw ethernet frame into the TxRing buffer
 *   4. Push a used entry to signal completion to the guest
 *
 * The TxRing is consumed by the active host network backend.
 */
static VOID
RosvVirtioNetProcessTxQueue(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    PROSV_VM Vm = State->OwnerVm;
    PROSV_VIRTQUEUE Vq = &State->Vq[VIRTIO_NET_QUEUE_TX];
    USHORT AvailIdx;
    USHORT DescIdx;
    ULONG ProcessedCount = 0;
    KIRQL OldIrql;
    USHORT BaseUsedIdx;
    BOOLEAN RingFull = FALSE;

    ExAcquireFastMutex(&State->TxQueueMutex);
    State->TxBackpressure = FALSE;

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
        UCHAR PacketBuf[ROSV_VIRTIO_NET_MAX_PACKET];
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

                /* Read the header (we mostly ignore it for simple forwarding) */
                CopyStatus = RosvMemoryCopyFromGpa(Vm, &NetHdr, Desc.Addr,
                                                    sizeof(VIRTIO_NET_HDR));
                if (!NT_SUCCESS(CopyStatus))
                {
                    ROSV_ERR("virtio-net: TX failed to read net header GPA=0x%llX status=0x%08X",
                             Desc.Addr, CopyStatus);
                    break;
                }
                HeaderRead = TRUE;

                /* Copy any data after the header in this descriptor */
                if (Desc.Len > sizeof(VIRTIO_NET_HDR))
                {
                    ULONG ExtraLen = Desc.Len - sizeof(VIRTIO_NET_HDR);
                    if (PacketOffset + ExtraLen > sizeof(PacketBuf))
                        ExtraLen = sizeof(PacketBuf) - PacketOffset;

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
                if (PacketOffset + CopyLen > sizeof(PacketBuf))
                    CopyLen = sizeof(PacketBuf) - PacketOffset;

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

        /* Buffer the extracted packet in TxRing for the host backend. */
        if (PacketOffset > 0)
        {
            BOOLEAN Enqueued = FALSE;

            KeAcquireSpinLock(&State->TxRingLock, &OldIrql);

            if (State->TxRingCount < ROSV_VIRTIO_NET_TX_RING_SIZE)
            {
                PROSV_VIRTIO_NET_TX_ENTRY Entry = &State->TxRing[State->TxRingHead];
                ULONG CopyLen = PacketOffset;
                ULONG64 EnqueueQpc = RosvVirtioNetReadQpc();
                ULONG64 TraceSeq;

                if (CopyLen > ROSV_VIRTIO_NET_MAX_PACKET)
                    CopyLen = ROSV_VIRTIO_NET_MAX_PACKET;

                RtlCopyMemory(Entry->Data, PacketBuf, CopyLen);
                Entry->Length = CopyLen;
                Entry->EnqueueQpc = EnqueueQpc;
                TraceSeq = ++State->TxTraceSeqNext;
                Entry->TraceSeq = TraceSeq;
                State->TxRingHead = (State->TxRingHead + 1) & (ROSV_VIRTIO_NET_TX_RING_SIZE - 1);
                State->TxRingCount++;
                State->TxPackets++;
                State->TxBytes += CopyLen;
                Enqueued = TRUE;

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                {
                    ROSV_NET_TRACE_ICMP_ECHO EchoInfo;
                    RosvVirtioNetDecodeIcmpEcho(PacketBuf, CopyLen, &EchoInfo);
                    if (EchoInfo.Valid)
                    {
                        ROSV_TRACE("virtio-net: [PERF] guest->ring tx_seq=%llu ring=%u len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
                                   TraceSeq,
                                   State->TxRingCount,
                                   CopyLen,
                                   EchoInfo.IsRequest ? "echo-req" : "echo-reply",
                                   EchoInfo.Id,
                                   EchoInfo.Seq,
                                   EchoInfo.SrcIp[0], EchoInfo.SrcIp[1], EchoInfo.SrcIp[2], EchoInfo.SrcIp[3],
                                   EchoInfo.DstIp[0], EchoInfo.DstIp[1], EchoInfo.DstIp[2], EchoInfo.DstIp[3]);
                    }
                }
#endif
            }
            else
            {
                State->TxBackpressure = TRUE;
                RingFull = TRUE;
            }

            KeReleaseSpinLock(&State->TxRingLock, OldIrql);

            if (Enqueued)
            {
                /* Wake any waiting NET_RX IOCTL immediately */
                KeSetEvent(&State->TxReadyEvent, IO_NETWORK_INCREMENT, FALSE);
            }
            else if (RingFull)
            {
                ROSV_TRACE("virtio-net: TX ring full, deferring guest completion "
                           "(pending_desc=%u ring_count=%u)",
                           (USHORT)(AvailIdx - Vq->LastAvailIdx),
                           State->TxRingCount);
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
    ExReleaseFastMutex(&State->TxQueueMutex);
}

/* ---- RX path: host -> guest --------------------------------------------- */

/*
 * Try to inject a packet into the guest RX virtqueue (queue 0).
 * The guest pre-posts receive buffers; we fill the first available one
 * with a virtio_net_hdr prefix followed by the raw ethernet frame.
 *
 * Returns TRUE if the packet was successfully injected.
 * Returns FALSE if no receive buffers are available (guest hasn't posted any).
 */
static BOOLEAN
RosvVirtioNetInjectToRxQueue(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    PROSV_VM Vm = State->OwnerVm;
    PROSV_VIRTQUEUE Vq = &State->Vq[VIRTIO_NET_QUEUE_RX];
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

    ExAcquireFastMutex(&State->RxQueueMutex);
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

    /* Preflight the writable capacity before touching guest memory. */
    RequiredBytes = sizeof(VIRTIO_NET_HDR) + Length;
    CurrentIdx = DescIdx;
    ChainDepth = 0;
    while (ChainDepth < Vq->Num)
    {
        if (!RosvNetVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
        {
            ROSV_ERR("virtio-net: RX preflight failed to read descriptor at index %u",
                     CurrentIdx);
            goto Exit;
        }

        if (!(Desc.Flags & VRING_DESC_F_WRITE))
        {
            ROSV_ERR("virtio-net: RX preflight found non-writable descriptor %u (flags=0x%X)",
                     CurrentIdx, Desc.Flags);
            goto Exit;
        }

        if (WritableCapacity < RequiredBytes)
        {
            ULONG Remaining = RequiredBytes - WritableCapacity;
            WritableCapacity += (Desc.Len > Remaining) ? Remaining : Desc.Len;
        }

        if (WritableCapacity >= RequiredBytes)
            break;

        if (!(Desc.Flags & VRING_DESC_F_NEXT))
            break;

        CurrentIdx = Desc.Next;
        ChainDepth++;
    }

    if (WritableCapacity < RequiredBytes)
    {
        ROSV_TRACE("virtio-net: RX chain too small for packet (%u < %u), leaving staged",
                   WritableCapacity, RequiredBytes);
        goto Exit;
    }

    /* Walk the descriptor chain and fill with header + data */
    CurrentIdx = DescIdx;
    ChainDepth = 0;

    while (ChainDepth < Vq->Num)
    {
        if (!RosvNetVirtqueueReadDesc(Vm, Vq, CurrentIdx, &Desc))
        {
            ROSV_ERR("virtio-net: RX failed to read descriptor at index %u", CurrentIdx);
            goto Exit;
        }

        /* RX descriptors must be device-writable */
        if (!(Desc.Flags & VRING_DESC_F_WRITE))
        {
            ROSV_ERR("virtio-net: RX descriptor %u not writable (flags=0x%X)",
                     CurrentIdx, Desc.Flags);
            goto Exit;
        }

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

        if (!(Desc.Flags & VRING_DESC_F_NEXT))
            break;

        CurrentIdx = Desc.Next;
        ChainDepth++;
    }

    if (!HeaderWritten || DataOffset != Length)
    {
        ROSV_ERR("virtio-net: RX write incomplete (header=%u data=%u/%u)",
                 HeaderWritten ? 1 : 0, DataOffset, Length);
        goto Exit;
    }

    /* Push used entry with total bytes written (header + data) */
    RosvNetVirtqueuePushUsed(Vm, Vq, (ULONG)DescIdx, TotalWritten);
    Vq->LastAvailIdx++;

    /* Raise interrupt to notify guest of received packet */
    {
        KIRQL IntIrql;
        KeAcquireSpinLock(&State->InterruptLock, &IntIrql);
        State->InterruptStatus |= VIRTIO_INT_VRING;
        State->InterruptPending = TRUE;
        KeMemoryBarrier();
        KeReleaseSpinLock(&State->InterruptLock, IntIrql);
    }

    /* Wake vCPU if halted so it can inject this RX interrupt */
    if (State->OwnerVm != NULL)
        KeSetEvent(&State->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);

    State->RxPackets++;
    State->RxBytes += Length;
    Success = TRUE;

Exit:
    ExReleaseFastMutex(&State->RxQueueMutex);
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
        Result = ROSV_VIRTIO_QUEUE_SIZE_MAX;
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
            ROSV_WARN("virtio-net: guest selected queue %u (only 0/1 exist)", Val32);
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
        if (Val32 == VIRTIO_NET_QUEUE_TX)
        {
            RosvVirtioNetProcessTxQueue(State);
        }
        else if (Val32 == VIRTIO_NET_QUEUE_RX)
        {
            /* Guest posted new RX buffers. Drain as much staged host traffic as
             * the queue can currently accept. */
            KIRQL OldIrql;
            ULONG Drained = 0;

            for (;;)
            {
                UCHAR LocalBuf[ROSV_VIRTIO_NET_MAX_PACKET];
                ULONG LocalLen;
                ULONG64 LocalEnqueueQpc;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                ULONG64 LocalTraceSeq;
#endif

                /* Dequeue one packet under lock */
                KeAcquireSpinLock(&State->RxLock, &OldIrql);
                if (State->RxRingCount == 0)
                {
                    KeReleaseSpinLock(&State->RxLock, OldIrql);
                    break;
                }
                {
                    PROSV_VIRTIO_NET_TX_ENTRY Entry = &State->RxRing[State->RxRingTail];
                    LocalLen = Entry->Length;
                    LocalEnqueueQpc = Entry->EnqueueQpc;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                    LocalTraceSeq = Entry->TraceSeq;
#endif
                    if (LocalLen > ROSV_VIRTIO_NET_MAX_PACKET)
                        LocalLen = ROSV_VIRTIO_NET_MAX_PACKET;
                    RtlCopyMemory(LocalBuf, Entry->Data, LocalLen);
                    Entry->Length = 0;
                    State->RxRingTail = (State->RxRingTail + 1) & (ROSV_VIRTIO_NET_RX_RING_SIZE - 1);
                    State->RxRingCount--;
                    KeMemoryBarrier();
                }
                KeReleaseSpinLock(&State->RxLock, OldIrql);

                /* Inject to guest without holding the lock */
                if (!RosvVirtioNetInjectToRxQueue(State, LocalBuf, LocalLen))
                {
                    /* No more guest RX buffers available - re-queue the packet */
                    KeAcquireSpinLock(&State->RxLock, &OldIrql);
                    {
                        /* Put it back at the tail (which we just advanced).
                         * Rewind the tail by one slot. */
                        State->RxRingTail = (State->RxRingTail - 1) & (ROSV_VIRTIO_NET_RX_RING_SIZE - 1);
                        {
                            PROSV_VIRTIO_NET_TX_ENTRY Entry = &State->RxRing[State->RxRingTail];
                            RtlCopyMemory(Entry->Data, LocalBuf, LocalLen);
                            Entry->Length = LocalLen;
                            Entry->EnqueueQpc = LocalEnqueueQpc;
#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
                            Entry->TraceSeq = LocalTraceSeq;
#endif
                        }
                        State->RxRingCount++;
                        KeMemoryBarrier();
                    }
                    KeReleaseSpinLock(&State->RxLock, OldIrql);
                    break;
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
                        ROSV_TRACE("virtio-net: [PERF] stage->guest rx_seq=%llu wait_us=%llu len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
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
                ROSV_TRACE("virtio-net: RX ring drained %u packets, %u remaining",
                           Drained, State->RxRingCount);
            }

            if (State->OwnerVm != NULL && State->OwnerVm->NetBackend != NULL)
                RosvNetBackendKick(State->OwnerVm);
        }
        else
        {
            ROSV_WARN("virtio-net: QUEUE_NOTIFY for non-existent queue %u", Val32);
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
            /* Device reset */
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
            RtlZeroMemory(&State->Vq[0], sizeof(ROSV_VIRTQUEUE));
            RtlZeroMemory(&State->Vq[1], sizeof(ROSV_VIRTQUEUE));
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
    ULONG64 EnqueueQpc = 0;
    ULONG64 TraceSeq = 0;

    KeAcquireSpinLock(&State->TxRingLock, &OldIrql);

    if (State->TxRingCount > 0)
    {
        PROSV_VIRTIO_NET_TX_ENTRY Entry = &State->TxRing[State->TxRingTail];

        if (Entry->Length > 0 && Entry->Length <= ROSV_NET_PACKET_MAX)
        {
            PacketOut->Length = Entry->Length;
            RtlCopyMemory(PacketOut->Data, Entry->Data, Entry->Length);
            EnqueueQpc = Entry->EnqueueQpc;
            TraceSeq = Entry->TraceSeq;
            Got = TRUE;
        }

        Entry->Length = 0;
        Entry->EnqueueQpc = 0;
        Entry->TraceSeq = 0;
        State->TxRingTail = (State->TxRingTail + 1) & (ROSV_VIRTIO_NET_TX_RING_SIZE - 1);
        State->TxRingCount--;
        ResumeTx = State->TxBackpressure;
    }

    KeReleaseSpinLock(&State->TxRingLock, OldIrql);

    if (ResumeTx)
        RosvVirtioNetProcessTxQueue(State);

#if defined(ROSV_ENABLE_TRACE) && (ROSV_ENABLE_TRACE != 0)
    if (Got)
    {
        ROSV_NET_TRACE_ICMP_ECHO EchoInfo;
        ULONG64 NowQpc = RosvVirtioNetReadQpc();
        ULONG64 QueueDelayUs = RosvVirtioNetQpcDeltaUs(State, EnqueueQpc, NowQpc);

        RosvVirtioNetDecodeIcmpEcho(PacketOut->Data, PacketOut->Length, &EchoInfo);
        if (EchoInfo.Valid || QueueDelayUs >= 10000)
        {
            ROSV_TRACE("virtio-net: [PERF] ring->netrx tx_seq=%llu delay_us=%llu ring=%u len=%u icmp=%s id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u",
                       TraceSeq,
                       QueueDelayUs,
                       State->TxRingCount,
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
    KIRQL OldIrql;
    ULONG64 StartQpc = RosvVirtioNetReadQpc();
    ULONG64 TraceSeq;

    if (Length == 0 || Length > ROSV_VIRTIO_NET_MAX_PACKET)
    {
        ROSV_ERR("virtio-net: RX inject invalid length %u", Length);
        return FALSE;
    }

    /* First try to inject directly into the RX virtqueue */
    if (RosvVirtioNetInjectToRxQueue(State, Data, Length))
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
        /* Wake the vCPU if it's yielding so it can deliver the interrupt */
        KeSetEvent(&State->RxPendingEvent, IO_NETWORK_INCREMENT, FALSE);
        return TRUE;
    }

    /* No RX buffers available. Enqueue into RX ring for later delivery
     * when the guest posts new RX buffers (QUEUE_NOTIFY on queue 0). */
    KeAcquireSpinLock(&State->RxLock, &OldIrql);

    if (State->RxRingCount >= ROSV_VIRTIO_NET_RX_RING_SIZE)
    {
        /* Ring full. Drop this packet and track in state struct. */
        State->RxDropCount++;
        if (State->RxDropCount <= 16 || (State->RxDropCount % 1024) == 0)
        {
            ROSV_WARN("virtio-net: RX ring full (%u/%u), dropping packet (%u bytes), "
                      "total drops=%u", State->RxRingCount,
                      ROSV_VIRTIO_NET_RX_RING_SIZE, Length, State->RxDropCount);
        }
        KeReleaseSpinLock(&State->RxLock, OldIrql);
        return FALSE;
    }

    {
        PROSV_VIRTIO_NET_TX_ENTRY Entry = &State->RxRing[State->RxRingHead];
        RtlCopyMemory(Entry->Data, Data, Length);
        Entry->Length = Length;
        TraceSeq = ++State->RxTraceSeqNext;
        Entry->TraceSeq = TraceSeq;
        Entry->EnqueueQpc = StartQpc;
        State->RxRingHead = (State->RxRingHead + 1) & (ROSV_VIRTIO_NET_RX_RING_SIZE - 1);
        State->RxRingCount++;
        KeMemoryBarrier();
    }

    KeReleaseSpinLock(&State->RxLock, OldIrql);

    /* Wake the vCPU if it's yielding so it can deliver the packet later */
    KeSetEvent(&State->RxPendingEvent, IO_NETWORK_INCREMENT, FALSE);

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

/* ---- Initialization and lifecycle --------------------------------------- */

NTSTATUS
RosvVirtioNetInitialize(
    _Out_ PROSV_VIRTIO_NET_STATE State,
    _In_ PROSV_VM Vm,
    _In_reads_(6) const UCHAR *MacAddress)
{
    RtlZeroMemory(State, sizeof(ROSV_VIRTIO_NET_STATE));
    KeQueryPerformanceCounter(&State->PerfFrequency);
    if (State->PerfFrequency.QuadPart <= 0)
        State->PerfFrequency.QuadPart = 1;

    State->OwnerVm = Vm;

    /* Set device features:
     * - VERSION_1: required for virtio 1.0
     * - MAC: device provides a MAC address in config space
     * - STATUS: config space has link status field
     * We intentionally do NOT offer MRG_RXBUF, CTRL_VQ, MQ, CSUM, or GSO
     * features to keep the implementation simple. */
    State->DeviceFeatures = VIRTIO_F_VERSION_1 |
                            VIRTIO_NET_F_MAC |
                            VIRTIO_NET_F_STATUS;

    /* Build device configuration */
    RtlCopyMemory(State->Config.Mac, MacAddress, 6);
    State->Config.Status = VIRTIO_NET_S_LINK_UP;
    State->Config.MaxVirtqueuePairs = 1;  /* Single TX/RX pair */
    State->Config.Mtu = 1500;

    /* Initialize spin locks */
    KeInitializeSpinLock(&State->TxRingLock);
    ExInitializeFastMutex(&State->TxQueueMutex);
    KeInitializeSpinLock(&State->RxLock);
    ExInitializeFastMutex(&State->RxQueueMutex);
    KeInitializeSpinLock(&State->InterruptLock);

    /* Initialize TX ring and ready event */
    State->TxRingHead = 0;
    State->TxRingTail = 0;
    State->TxRingCount = 0;
    State->TxBackpressure = FALSE;
    KeInitializeEvent(&State->TxReadyEvent, SynchronizationEvent, FALSE);

    /* Initialize RX ring buffer */
    State->RxRingHead = 0;
    State->RxRingTail = 0;
    State->RxRingCount = 0;
    KeInitializeEvent(&State->RxPendingEvent, SynchronizationEvent, FALSE);

    /* Initialize interrupt coalescing — disabled by default (inject every packet) */
    State->IrqCoalesce.PendingCount = 0;
    State->IrqCoalesce.LastIrqQpc = 0;
    State->IrqCoalesce.CoalesceMaxPackets = 1;  /* 1 = no coalescing */
    State->IrqCoalesce.CoalesceMaxUsec = 0;     /* 0 = no time limit */

    ROSV_ERR("virtio-net: initialized at MMIO 0x%llX-0x%llX, IRQ %u, "
             "MAC=%02X:%02X:%02X:%02X:%02X:%02X, features=0x%llX, qpc_freq=%lld",
             ROSV_VIRTIO_NET_MMIO_BASE,
             ROSV_VIRTIO_NET_MMIO_BASE + ROSV_VIRTIO_NET_MMIO_SIZE - 1,
             ROSV_VIRTIO_NET_IRQ,
             MacAddress[0], MacAddress[1], MacAddress[2],
             MacAddress[3], MacAddress[4], MacAddress[5],
             State->DeviceFeatures,
             State->PerfFrequency.QuadPart);

    return STATUS_SUCCESS;
}

VOID
RosvVirtioNetDestroy(
    _Inout_ PROSV_VIRTIO_NET_STATE State)
{
    ROSV_ERR("virtio-net: destroy (tx=%llu/%llu bytes, rx=%llu/%llu bytes, "
             "tx_drops=%u, rx_drops=%u)",
             State->TxPackets, State->TxBytes,
             State->RxPackets, State->RxBytes,
             State->TxDropCount, State->RxDropCount);

    RtlZeroMemory(State, sizeof(ROSV_VIRTIO_NET_STATE));
}

/* ---- RX backpressure helper --------------------------------------------- */

BOOLEAN
RosvVirtioNetRxRingHasSpace(
    _In_ PROSV_VIRTIO_NET_STATE State)
{
    BOOLEAN HasSpace;
    KeMemoryBarrier();
    HasSpace = (State->RxRingCount < ROSV_VIRTIO_NET_RX_RING_SIZE);
    return HasSpace;
}
