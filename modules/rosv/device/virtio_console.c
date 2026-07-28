/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Virtio-console MMIO device emulation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements a virtio-console (device ID 3) with multiport support over
 * the virtio MMIO transport.  Each port provides an independent hvcN
 * terminal that a separate rosl instance can connect to.
 *
 * The guest discovers this device through a kernel command line parameter
 * (virtio_mmio.device=0x1000@0xFEB02000:7) and communicates via MMIO
 * register reads/writes that trigger EPT violations.
 *
 * Data flow:
 *   Host -> Guest: RosvVirtioConPortWrite() -> TxBuf -> RX virtqueue -> guest reads hvcN
 *   Guest -> Host: guest writes hvcN -> TX virtqueue -> RxBuf -> RosvVirtioConPortRead()
 */

#include <rosv/rosv.h>
#include <rosv/virtio_blk.h>
#include <rosv/virtio_console.h>
#include <rosv/pty.h>
#include <rosv/vm.h>

#define NDEBUG
#include <debug.h>

/* ---- Helpers ------------------------------------------------------------ */

static BOOLEAN
VirtioConReadDesc(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 DescGpa,
    _In_ USHORT Index,
    _Out_ PVRING_DESC Desc);

/*
 * Map a queue index to a port index.
 * Queues 0,1 = port 0 (data).
 * Queues 2,3 = control.
 * Queues 4,5 = port 1, 6,7 = port 2, etc.
 * Returns (ULONG)-1 for control queues.
 */
static ULONG
VirtioConQueueToPort(
    _In_ ULONG QueueIndex)
{
    if (QueueIndex <= 1)
        return 0;
    if (QueueIndex <= 3)
        return (ULONG)-1;  /* Control queue */
    return (QueueIndex - 4) / 2 + 1;
}

static BOOLEAN
VirtioConIsRxQueue(
    _In_ ULONG QueueIndex)
{
    return (QueueIndex % 2) == 0;
}

static ULONG
VirtioConGetPortRxFreeSpace(
    _Inout_ PROSV_VIRTIO_CON_PORT Port)
{
    KIRQL OldIrql;
    ULONG FreeSpace;

    KeAcquireSpinLock(&Port->RxLock, &OldIrql);
    FreeSpace = ROSV_VIRTIO_CON_PORT_BUF_SIZE - Port->RxCount;
    KeReleaseSpinLock(&Port->RxLock, OldIrql);

    return FreeSpace;
}

static BOOLEAN
VirtioConCalcTxChainLen(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT ChainHead,
    _Out_ PULONG TotalLen)
{
    USHORT CurrentIdx = ChainHead;
    USHORT ChainDepth;
    ULONG Length = 0;

    *TotalLen = 0;

    for (ChainDepth = 0; ChainDepth < Vq->Num; ChainDepth++)
    {
        VRING_DESC Desc;

        if (!VirtioConReadDesc(Vm, Vq->DescGpa, CurrentIdx, &Desc))
            return FALSE;

        if (Desc.Len > 0 && !(Desc.Flags & VRING_DESC_F_WRITE))
        {
            if (Length > MAXULONG - Desc.Len)
                return FALSE;

            Length += Desc.Len;
        }

        if (!(Desc.Flags & VRING_DESC_F_NEXT))
        {
            *TotalLen = Length;
            return TRUE;
        }

        CurrentIdx = Desc.Next;
    }

    ROSV_ERR("virtio-con: TX chain loop detected (head=%u depth=%u)", ChainHead, Vq->Num);
    return FALSE;
}

static BOOLEAN
VirtioConCopyTxChainToBuffer(
    _In_ PROSV_VM Vm,
    _In_ PROSV_VIRTQUEUE Vq,
    _In_ USHORT ChainHead,
    _Out_writes_bytes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesCopied)
{
    USHORT CurrentIdx = ChainHead;
    USHORT ChainDepth;
    ULONG Offset = 0;

    *BytesCopied = 0;

    for (ChainDepth = 0; ChainDepth < Vq->Num; ChainDepth++)
    {
        VRING_DESC Desc;

        if (!VirtioConReadDesc(Vm, Vq->DescGpa, CurrentIdx, &Desc))
            return FALSE;

        if (Desc.Len > 0 && !(Desc.Flags & VRING_DESC_F_WRITE))
        {
            ULONG Remaining = Desc.Len;
            ULONG64 SrcGpa = Desc.Addr;

            if (Offset > BufferSize || Remaining > (BufferSize - Offset))
                return FALSE;

            while (Remaining > 0)
            {
                ULONG Chunk = (Remaining < 256) ? Remaining : 256;
                if (!NT_SUCCESS(RosvMemoryCopyFromGpa(Vm, Buffer + Offset, SrcGpa, Chunk)))
                    return FALSE;

                Offset += Chunk;
                SrcGpa += Chunk;
                Remaining -= Chunk;
            }
        }

        if (!(Desc.Flags & VRING_DESC_F_NEXT))
        {
            *BytesCopied = Offset;
            return TRUE;
        }

        CurrentIdx = Desc.Next;
    }

    return FALSE;
}

static VOID
VirtioConAppendToPortRx(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _Inout_ PROSV_VIRTIO_CON_PORT Port,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    ULONG i;

    if (Length == 0)
        return;

    KeAcquireSpinLock(&Port->RxLock, &OldIrql);

    for (i = 0; i < Length && Port->RxCount < ROSV_VIRTIO_CON_PORT_BUF_SIZE; i++)
    {
        Port->RxBuf[Port->RxHead] = Data[i];
        Port->RxHead = (Port->RxHead + 1) % ROSV_VIRTIO_CON_PORT_BUF_SIZE;
        Port->RxCount++;
    }

    Port->BytesOut += i;
    State->TotalBytesOut += i;

    KeReleaseSpinLock(&Port->RxLock, OldIrql);
}

/* Queue control message for delivery to guest via ctrl receiveq */
static VOID
VirtioConQueueCtrlMsg(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortId,
    _In_ USHORT Event,
    _In_ USHORT Value)
{
    if (State->CtrlPendingCount >= ARRAYSIZE(State->CtrlPending))
    {
        ROSV_ERR("virtio-con: ctrl message queue full, dropping event=%u port=%u",
                 Event, PortId);
        return;
    }

    State->CtrlPending[State->CtrlPendingHead].Id = PortId;
    State->CtrlPending[State->CtrlPendingHead].Event = Event;
    State->CtrlPending[State->CtrlPendingHead].Value = Value;
    State->CtrlPendingHead = (State->CtrlPendingHead + 1) % ARRAYSIZE(State->CtrlPending);
    State->CtrlPendingCount++;

    ROSV_TRACE("virtio-con: queued ctrl msg: port=%u event=%u value=%u (pending=%u)",
               PortId, Event, Value, State->CtrlPendingCount);
}

/* ---- Virtqueue descriptor chain walking --------------------------------- */

static BOOLEAN
VirtioConReadDesc(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 DescGpa,
    _In_ USHORT Index,
    _Out_ PVRING_DESC Desc)
{
    NTSTATUS Status;
    Status = RosvMemoryCopyFromGpa(Vm, Desc,
                                    DescGpa + (ULONG64)Index * sizeof(VRING_DESC),
                                    sizeof(VRING_DESC));
    return NT_SUCCESS(Status);
}

static USHORT
VirtioConReadAvailIdx(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 AvailGpa)
{
    USHORT Idx = 0;
    RosvMemoryCopyFromGpa(Vm, &Idx, AvailGpa + 2, sizeof(Idx));
    return Idx;
}

static USHORT
VirtioConReadAvailRing(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 AvailGpa,
    _In_ USHORT RingIndex,
    _In_ ULONG QueueSize)
{
    USHORT DescIdx = 0;
    ULONG64 Offset = 4 + (ULONG64)(RingIndex % QueueSize) * 2;
    RosvMemoryCopyFromGpa(Vm, &DescIdx, AvailGpa + Offset, sizeof(DescIdx));
    return DescIdx;
}

static VOID
VirtioConWriteUsedEntry(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 UsedGpa,
    _In_ USHORT RingIndex,
    _In_ ULONG QueueSize,
    _In_ ULONG DescId,
    _In_ ULONG Len)
{
    VRING_USED_ELEM Elem;
    ULONG64 Offset;
    USHORT NewIdx;

    Elem.Id = DescId;
    Elem.Len = Len;
    Offset = 4 + (ULONG64)(RingIndex % QueueSize) * sizeof(VRING_USED_ELEM);
    RosvMemoryCopyToGpa(Vm, UsedGpa + Offset, &Elem, sizeof(Elem));

    /* Advance used index */
    NewIdx = RingIndex + 1;
    KeMemoryBarrier();
    RosvMemoryCopyToGpa(Vm, UsedGpa + 2, &NewIdx, sizeof(NewIdx));
}

static USHORT
VirtioConReadUsedIdx(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 UsedGpa)
{
    USHORT Idx = 0;
    RosvMemoryCopyFromGpa(Vm, &Idx, UsedGpa + 2, sizeof(Idx));
    return Idx;
}

/* ---- Process guest TX (guest -> host) ----------------------------------- */

static VOID
VirtioConProcessTx(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG QueueIndex)
{
    PROSV_VM Vm = State->OwnerVm;
    PROSV_VIRTQUEUE Vq = &State->Vq[QueueIndex];
    ULONG PortIndex;
    PROSV_VIRTIO_CON_PORT Port;
    USHORT AvailIdx;
    USHORT UsedIdx;

    if (!Vq->Ready || Vq->Num == 0)
        return;

    PortIndex = VirtioConQueueToPort(QueueIndex);
    if (PortIndex == (ULONG)-1 || PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS)
        return;

    Port = &State->Ports[PortIndex];
    if (!Port->Active)
        return;

    if (InterlockedCompareExchange(&Port->TxProcessing, 1, 0) != 0)
        return;

    AvailIdx = VirtioConReadAvailIdx(Vm, Vq->AvailGpa);
    UsedIdx = VirtioConReadUsedIdx(Vm, Vq->UsedGpa);
    {
    USHORT InitialUsedIdx = UsedIdx;

    while (Vq->LastAvailIdx != AvailIdx)
    {
        USHORT ChainHead;
        ULONG TotalLen = 0;
        UCHAR *PacketBuffer = NULL;
        ULONG BufferedLen = 0;

        ChainHead = VirtioConReadAvailRing(Vm, Vq->AvailGpa,
                                           Vq->LastAvailIdx, Vq->Num);

        if (!VirtioConCalcTxChainLen(Vm, Vq, ChainHead, &TotalLen))
        {
            ROSV_ERR("virtio-con: TX chain parse failed port=%u head=%u", PortIndex, ChainHead);
            VirtioConWriteUsedEntry(Vm, Vq->UsedGpa, UsedIdx, Vq->Num, ChainHead, 0);
            UsedIdx++;
            Vq->LastAvailIdx++;
            continue;
        }

        if (TotalLen > ROSV_VIRTIO_CON_PORT_BUF_SIZE)
        {
            ROSV_ERR("virtio-con: TX packet too large port=%u head=%u len=%u max=%u",
                     PortIndex, ChainHead, TotalLen, ROSV_VIRTIO_CON_PORT_BUF_SIZE);
            VirtioConWriteUsedEntry(Vm, Vq->UsedGpa, UsedIdx, Vq->Num, ChainHead, 0);
            UsedIdx++;
            Vq->LastAvailIdx++;
            continue;
        }

        if (TotalLen > VirtioConGetPortRxFreeSpace(Port))
        {
            if (Port->BoundPty != NULL)
            {
                RosvVconPumpToPty(Port->BoundPty);
            }

            if (TotalLen > VirtioConGetPortRxFreeSpace(Port))
                break;
        }

        if (TotalLen > 0)
        {
            PacketBuffer = (UCHAR *)ExAllocatePoolWithTag(NonPagedPool,
                                                          TotalLen,
                                                          ROSV_DRIVER_TAG);
            if (PacketBuffer == NULL)
            {
                ROSV_ERR("virtio-con: TX staging allocation failed port=%u len=%u",
                         PortIndex, TotalLen);
                break;
            }

            if (!VirtioConCopyTxChainToBuffer(Vm, Vq, ChainHead,
                                              PacketBuffer, TotalLen,
                                              &BufferedLen) ||
                BufferedLen != TotalLen)
            {
                ROSV_ERR("virtio-con: TX copy failed port=%u head=%u len=%u copied=%u",
                         PortIndex, ChainHead, TotalLen, BufferedLen);
                ExFreePoolWithTag(PacketBuffer, ROSV_DRIVER_TAG);
                VirtioConWriteUsedEntry(Vm, Vq->UsedGpa, UsedIdx, Vq->Num, ChainHead, 0);
                UsedIdx++;
                Vq->LastAvailIdx++;
                continue;
            }

            VirtioConAppendToPortRx(State, Port, PacketBuffer, TotalLen);
            ExFreePoolWithTag(PacketBuffer, ROSV_DRIVER_TAG);
        }

        /* Push the original chain head to the used ring */
        VirtioConWriteUsedEntry(Vm, Vq->UsedGpa, UsedIdx, Vq->Num,
                                 ChainHead, TotalLen);
        UsedIdx++;
        Vq->LastAvailIdx++;
    }

    /* Signal interrupt if we consumed any descriptors */
    if (UsedIdx != InitialUsedIdx)
    {
        State->InterruptStatus |= VIRTIO_INT_VRING;
        State->InterruptPending = TRUE;
        ROSV_TRACE("virtio-con: port %u TX done: consumed %u descs, RxCount=%u",
                   PortIndex, (ULONG)(UsedIdx - InitialUsedIdx), Port->RxCount);

        /* If this port is bound to a PTY, pump vcon RxBuf → PTY OutputBuf */
        if (Port->BoundPty != NULL)
        {
            RosvVconPumpToPty(Port->BoundPty);
        }
    }
    } /* InitialUsedIdx scope */

    InterlockedExchange(&Port->TxProcessing, 0);
}

/* ---- Process control TX (guest -> device control) ----------------------- */

static VOID
VirtioConProcessCtrlTx(
    _Inout_ PROSV_VIRTIO_CON_STATE State)
{
    PROSV_VM Vm = State->OwnerVm;
    PROSV_VIRTQUEUE Vq = &State->Vq[VIRTIO_CON_QUEUE_CTRL_TX];
    USHORT AvailIdx;
    USHORT UsedIdx;

    if (!Vq->Ready || Vq->Num == 0)
        return;

    AvailIdx = VirtioConReadAvailIdx(Vm, Vq->AvailGpa);
    UsedIdx = VirtioConReadUsedIdx(Vm, Vq->UsedGpa);

    while (Vq->LastAvailIdx != AvailIdx)
    {
        USHORT DescIdx;
        VRING_DESC Desc;
        VIRTIO_CONSOLE_CONTROL Ctrl;

        DescIdx = VirtioConReadAvailRing(Vm, Vq->AvailGpa,
                                          Vq->LastAvailIdx, Vq->Num);

        if (!VirtioConReadDesc(Vm, Vq->DescGpa, DescIdx, &Desc))
        {
            ROSV_ERR("virtio-con: ctrl TX desc read failed");
            Vq->LastAvailIdx++;
            continue;
        }

        if (Desc.Len >= sizeof(VIRTIO_CONSOLE_CONTROL) &&
            !(Desc.Flags & VRING_DESC_F_WRITE))
        {
            RosvMemoryCopyFromGpa(Vm, &Ctrl, Desc.Addr, sizeof(Ctrl));

            ROSV_TRACE("virtio-con: ctrl from guest: port=%u event=%u value=%u",
                       Ctrl.Id, Ctrl.Event, Ctrl.Value);

            switch (Ctrl.Event)
            {
                case VIRTIO_CONSOLE_DEVICE_READY:
                    ROSV_TRACE("virtio-con: guest driver READY (value=%u)", Ctrl.Value);
                    State->DriverReady = (Ctrl.Value != 0);

                    /* Announce each port via PORT_ADD only.
                     * PORT_OPEN is sent in response to PORT_READY (below).
                     * We do NOT send CONSOLE_PORT — all ports remain as plain
                     * vport character devices (/dev/vportNpM), which avoids the
                     * vtermno conflict with pre-existing Hyper-V hvc devices. */
                    if (State->DriverReady)
                    {
                        for (ULONG i = 0; i < ROSV_VIRTIO_CON_MAX_PORTS; i++)
                        {
                            if (State->Ports[i].Active)
                            {
                                VirtioConQueueCtrlMsg(State, i,
                                    VIRTIO_CONSOLE_PORT_ADD, 0);
                            }
                        }
                    }
                    break;

                case VIRTIO_CONSOLE_PORT_READY:
                    if (Ctrl.Id < ROSV_VIRTIO_CON_MAX_PORTS)
                    {
                        ROSV_TRACE("virtio-con: port %u READY → sending PORT_OPEN", Ctrl.Id);
                        /* Respond with PORT_OPEN=1 to mark host as connected.
                         * The guest driver sets host_connected=true on receipt,
                         * making /dev/vportNpM writable. */
                        VirtioConQueueCtrlMsg(State, Ctrl.Id,
                            VIRTIO_CONSOLE_PORT_OPEN, 1);
                    }
                    break;

                case VIRTIO_CONSOLE_PORT_OPEN:
                    if (Ctrl.Id < ROSV_VIRTIO_CON_MAX_PORTS)
                    {
                        State->Ports[Ctrl.Id].Open = (Ctrl.Value != 0);
                        ROSV_TRACE("virtio-con: port %u %s",
                                   Ctrl.Id, Ctrl.Value ? "OPENED" : "CLOSED");
                    }
                    break;

                default:
                    ROSV_TRACE("virtio-con: unhandled ctrl event=%u from guest", Ctrl.Event);
                    break;
            }
        }

        VirtioConWriteUsedEntry(Vm, Vq->UsedGpa, UsedIdx, Vq->Num,
                                 DescIdx, 0);
        UsedIdx++;
        Vq->LastAvailIdx++;
    }
}

/* ---- Flush control messages to guest ------------------------------------ */

static VOID
VirtioConFlushCtrlRx(
    _Inout_ PROSV_VIRTIO_CON_STATE State)
{
    PROSV_VM Vm = State->OwnerVm;
    PROSV_VIRTQUEUE Vq = &State->Vq[VIRTIO_CON_QUEUE_CTRL_RX];
    USHORT AvailIdx;
    USHORT UsedIdx;

    if (!Vq->Ready || Vq->Num == 0 || State->CtrlPendingCount == 0)
        return;

    AvailIdx = VirtioConReadAvailIdx(Vm, Vq->AvailGpa);
    UsedIdx = VirtioConReadUsedIdx(Vm, Vq->UsedGpa);
    {
    USHORT InitialUsedIdx = UsedIdx;

    while (State->CtrlPendingCount > 0 && Vq->LastAvailIdx != AvailIdx)
    {
        USHORT DescIdx;
        VRING_DESC Desc;
        PVIRTIO_CONSOLE_CONTROL Msg;

        DescIdx = VirtioConReadAvailRing(Vm, Vq->AvailGpa,
                                          Vq->LastAvailIdx, Vq->Num);

        if (!VirtioConReadDesc(Vm, Vq->DescGpa, DescIdx, &Desc))
        {
            ROSV_ERR("virtio-con: ctrl RX desc read failed");
            break;
        }

        if (Desc.Len < sizeof(VIRTIO_CONSOLE_CONTROL) ||
            !(Desc.Flags & VRING_DESC_F_WRITE))
        {
            ROSV_ERR("virtio-con: ctrl RX desc too small or not writable (len=%u flags=0x%x)",
                     Desc.Len, Desc.Flags);
            Vq->LastAvailIdx++;
            continue;
        }

        Msg = &State->CtrlPending[State->CtrlPendingTail];
        ROSV_TRACE("virtio-con: sending ctrl to guest: port=%u event=%u value=%u",
                   Msg->Id, Msg->Event, Msg->Value);

        RosvMemoryCopyToGpa(Vm, Desc.Addr, Msg, sizeof(VIRTIO_CONSOLE_CONTROL));

        VirtioConWriteUsedEntry(Vm, Vq->UsedGpa, UsedIdx, Vq->Num,
                                 DescIdx, sizeof(VIRTIO_CONSOLE_CONTROL));
        UsedIdx++;
        Vq->LastAvailIdx++;

        State->CtrlPendingTail = (State->CtrlPendingTail + 1) % ARRAYSIZE(State->CtrlPending);
        State->CtrlPendingCount--;
    }

    if (UsedIdx != InitialUsedIdx)
    {
        ROSV_TRACE("virtio-con: FlushCtrlRx delivered %u ctrl msgs, raising IRQ",
                   (ULONG)(UsedIdx - InitialUsedIdx));
        State->InterruptStatus |= VIRTIO_INT_VRING;
        State->InterruptPending = TRUE;
    }
    } /* InitialUsedIdx scope */
}

/* ---- MMIO register read ------------------------------------------------- */

BOOLEAN
RosvVirtioConMmioRead(
    _In_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _Out_ PULONG64 Value)
{
    ULONG Offset = (ULONG)(GuestPhysicalAddress - ROSV_VIRTIO_CON_MMIO_BASE);

    *Value = 0;

    switch (Offset)
    {
        case VIRTIO_MMIO_MAGIC_VALUE:
            *Value = VIRTIO_MMIO_MAGIC;
            break;

        case VIRTIO_MMIO_VERSION:
            *Value = VIRTIO_MMIO_VERSION_2;
            break;

        case VIRTIO_MMIO_DEVICE_ID:
            *Value = VIRTIO_ID_CONSOLE;
            break;

        case VIRTIO_MMIO_VENDOR_ID:
            *Value = VIRTIO_VENDOR_ROSV;
            break;

        case VIRTIO_MMIO_DEVICE_FEATURES:
            if (State->DeviceFeaturesSelPage == 0)
                *Value = (ULONG)(State->DeviceFeatures & 0xFFFFFFFF);
            else if (State->DeviceFeaturesSelPage == 1)
                *Value = (ULONG)((State->DeviceFeatures >> 32) & 0xFFFFFFFF);
            else
                *Value = 0;
            break;

        case VIRTIO_MMIO_QUEUE_NUM_MAX:
            /* Return 0 for out-of-range queues so driver knows the limit */
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
                *Value = ROSV_VIRTIO_CON_QUEUE_SIZE;
            else
                *Value = 0;
            break;

        case VIRTIO_MMIO_QUEUE_READY:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
                *Value = State->Vq[State->QueueSel].Ready ? 1 : 0;
            break;

        case VIRTIO_MMIO_INTERRUPT_STATUS:
            *Value = State->InterruptStatus;
            break;

        case VIRTIO_MMIO_STATUS:
            *Value = State->Status;
            break;

        case VIRTIO_MMIO_CONFIG_GENERATION:
            *Value = State->ConfigGeneration;
            break;

        default:
            /* Config space reads */
            if (Offset >= VIRTIO_MMIO_CONFIG_SPACE &&
                Offset < VIRTIO_MMIO_CONFIG_SPACE + sizeof(VIRTIO_CONSOLE_CONFIG))
            {
                ULONG CfgOff = Offset - VIRTIO_MMIO_CONFIG_SPACE;
                PUCHAR CfgBytes = (PUCHAR)&State->Config;

                if (Size == 1)
                    *Value = CfgBytes[CfgOff];
                else if (Size == 2 && CfgOff + 2 <= sizeof(VIRTIO_CONSOLE_CONFIG))
                    *Value = *(PUSHORT)(CfgBytes + CfgOff);
                else if (Size == 4 && CfgOff + 4 <= sizeof(VIRTIO_CONSOLE_CONFIG))
                    *Value = *(PULONG)(CfgBytes + CfgOff);
            }
            else
            {
                DPRINT("virtio-con: unhandled MMIO read offset=0x%X size=%u\n", Offset, Size);
            }
            break;
    }

    return TRUE;
}

/* ---- MMIO register write ------------------------------------------------ */

BOOLEAN
RosvVirtioConMmioWrite(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _In_ ULONG64 Value)
{
    ULONG Offset = (ULONG)(GuestPhysicalAddress - ROSV_VIRTIO_CON_MMIO_BASE);
    PROSV_VIRTQUEUE Vq;

    switch (Offset)
    {
        case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
            State->DeviceFeaturesSelPage = (ULONG)Value;
            break;

        case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
            State->DriverFeaturesSelPage = (ULONG)Value;
            break;

        case VIRTIO_MMIO_DRIVER_FEATURES:
            if (State->DriverFeaturesSelPage == 0)
            {
                State->DriverFeatures &= ~0xFFFFFFFFULL;
                State->DriverFeatures |= (Value & 0xFFFFFFFF);
            }
            else if (State->DriverFeaturesSelPage == 1)
            {
                State->DriverFeatures &= 0xFFFFFFFFULL;
                State->DriverFeatures |= ((Value & 0xFFFFFFFF) << 32);
            }
            ROSV_TRACE("virtio-con: driver features = 0x%llX", State->DriverFeatures);
            break;

        case VIRTIO_MMIO_QUEUE_SEL:
            State->QueueSel = (ULONG)Value;
            if (State->QueueSel >= ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                ROSV_ERR("virtio-con: queue select %u out of range (max=%u)",
                         State->QueueSel, ROSV_VIRTIO_CON_NUM_QUEUES);
            }
            break;

        case VIRTIO_MMIO_QUEUE_NUM:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                ULONG Num = (ULONG)Value;
                if (Num > ROSV_VIRTIO_CON_QUEUE_SIZE)
                    Num = ROSV_VIRTIO_CON_QUEUE_SIZE;
                State->Vq[State->QueueSel].Num = Num;
            }
            break;

        case VIRTIO_MMIO_QUEUE_READY:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                State->Vq[State->QueueSel].Ready = (Value != 0);
                ROSV_TRACE("virtio-con: queue %u ready=%u", State->QueueSel, (ULONG)Value);
            }
            break;

        case VIRTIO_MMIO_QUEUE_DESC_LOW:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                Vq = &State->Vq[State->QueueSel];
                Vq->DescGpa = (Vq->DescGpa & 0xFFFFFFFF00000000ULL) | (Value & 0xFFFFFFFF);
            }
            break;

        case VIRTIO_MMIO_QUEUE_DESC_HIGH:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                Vq = &State->Vq[State->QueueSel];
                Vq->DescGpa = (Vq->DescGpa & 0xFFFFFFFFULL) | ((Value & 0xFFFFFFFF) << 32);
            }
            break;

        case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                Vq = &State->Vq[State->QueueSel];
                Vq->AvailGpa = (Vq->AvailGpa & 0xFFFFFFFF00000000ULL) | (Value & 0xFFFFFFFF);
            }
            break;

        case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                Vq = &State->Vq[State->QueueSel];
                Vq->AvailGpa = (Vq->AvailGpa & 0xFFFFFFFFULL) | ((Value & 0xFFFFFFFF) << 32);
            }
            break;

        case VIRTIO_MMIO_QUEUE_USED_LOW:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                Vq = &State->Vq[State->QueueSel];
                Vq->UsedGpa = (Vq->UsedGpa & 0xFFFFFFFF00000000ULL) | (Value & 0xFFFFFFFF);
            }
            break;

        case VIRTIO_MMIO_QUEUE_USED_HIGH:
            if (State->QueueSel < ROSV_VIRTIO_CON_NUM_QUEUES)
            {
                Vq = &State->Vq[State->QueueSel];
                Vq->UsedGpa = (Vq->UsedGpa & 0xFFFFFFFFULL) | ((Value & 0xFFFFFFFF) << 32);
            }
            break;

        case VIRTIO_MMIO_QUEUE_NOTIFY:
        {
            ULONG QueueIdx = (ULONG)Value;
            if (QueueIdx >= ROSV_VIRTIO_CON_NUM_QUEUES)
                break;

            DPRINT("virtio-con: QUEUE_NOTIFY queue=%u\n", QueueIdx);

            if (QueueIdx == VIRTIO_CON_QUEUE_CTRL_TX)
            {
                /* Guest sent control messages */
                VirtioConProcessCtrlTx(State);
                /* Try to flush any pending ctrl responses */
                VirtioConFlushCtrlRx(State);
            }
            else if (QueueIdx == VIRTIO_CON_QUEUE_CTRL_RX)
            {
                /* Guest posted buffers for control receive - flush pending */
                VirtioConFlushCtrlRx(State);
            }
            else if (!VirtioConIsRxQueue(QueueIdx))
            {
                /* TX queue (guest -> host): process outgoing data */
                VirtioConProcessTx(State, QueueIdx);
            }
            else
            {
                /* RX queue (host -> guest): guest posted receive buffers.
                 * Flush any pending host->guest data for this port. */
                ULONG PortIdx = VirtioConQueueToPort(QueueIdx);
                if (PortIdx < ROSV_VIRTIO_CON_MAX_PORTS)
                    RosvVirtioConFlushPortTx(State, PortIdx);
            }
            break;
        }

        case VIRTIO_MMIO_INTERRUPT_ACK:
            State->InterruptStatus &= ~(ULONG)Value;
            break;

        case VIRTIO_MMIO_STATUS:
        {
            ULONG NewStatus = (ULONG)Value;
            if (NewStatus == 0)
            {
                /* Device reset */
                ROSV_TRACE("virtio-con: device RESET");
                State->Status = 0;
                State->DriverFeatures = 0;
                State->InterruptStatus = 0;
                State->InterruptPending = FALSE;
                State->DriverReady = FALSE;
                for (ULONG i = 0; i < ROSV_VIRTIO_CON_NUM_QUEUES; i++)
                {
                    State->Vq[i].Ready = FALSE;
                    State->Vq[i].LastAvailIdx = 0;
                }
            }
            else
            {
                State->Status = NewStatus;
                ROSV_TRACE("virtio-con: status = 0x%X (ACK=%u DRV=%u FEATURESOK=%u DRIVEROK=%u FAILED=%u)",
                           NewStatus,
                           (NewStatus >> 0) & 1,
                           (NewStatus >> 1) & 1,
                           (NewStatus >> 3) & 1,
                           (NewStatus >> 2) & 1,
                           (NewStatus >> 7) & 1);
            }
            break;
        }

        default:
            /* Config space writes (emergency write at offset 0x108) */
            if (Offset == VIRTIO_MMIO_CONFIG_SPACE + FIELD_OFFSET(VIRTIO_CONSOLE_CONFIG, EmergWr))
            {
                /* Emergency write: output character immediately */
                ROSV_TRACE("virtio-con: emergency write: '%c' (0x%02X)",
                           (UCHAR)(Value & 0xFF), (UCHAR)(Value & 0xFF));
            }
            else
            {
                DPRINT("virtio-con: unhandled MMIO write offset=0x%X size=%u value=0x%llX\n",
                       Offset, Size, Value);
            }
            break;
    }

    return TRUE;
}

/* ---- Initialization ----------------------------------------------------- */

NTSTATUS
RosvVirtioConInitialize(
    _Out_ PROSV_VIRTIO_CON_STATE State,
    _In_ PROSV_VM Vm,
    _In_ ULONG NumPorts)
{
    RtlZeroMemory(State, sizeof(*State));
    State->OwnerVm = Vm;

    if (NumPorts == 0 || NumPorts > ROSV_VIRTIO_CON_MAX_PORTS)
        NumPorts = ROSV_VIRTIO_CON_MAX_PORTS;

    /* Device features: VERSION_1 + MULTIPORT + SIZE */
    State->DeviceFeatures = VIRTIO_F_VERSION_1 |
                            VIRTIO_CONSOLE_F_MULTIPORT |
                            VIRTIO_CONSOLE_F_SIZE;

    /* Config space */
    State->Config.Cols = 80;
    State->Config.Rows = 24;
    State->Config.MaxNrPorts = NumPorts;
    State->Config.EmergWr = 0;

    /* Initialize ports */
    for (ULONG i = 0; i < NumPorts; i++)
    {
        State->Ports[i].Active = TRUE;
        State->Ports[i].IsConsole = FALSE;  /* No console port — use vportNpM */
        KeInitializeSpinLock(&State->Ports[i].TxLock);
        KeInitializeSpinLock(&State->Ports[i].RxLock);
    }
    State->ActivePortCount = NumPorts;

    ROSV_TRACE("virtio-con: initialized at GPA 0x%llX IRQ %u with %u ports",
               ROSV_VIRTIO_CON_MMIO_BASE, ROSV_VIRTIO_CON_IRQ, NumPorts);

    return STATUS_SUCCESS;
}

VOID
RosvVirtioConDestroy(
    _Inout_ PROSV_VIRTIO_CON_STATE State)
{
    ROSV_TRACE("virtio-con: destroy (in=%llu out=%llu)",
               State->TotalBytesIn, State->TotalBytesOut);
    RtlZeroMemory(State, sizeof(*State));
}

/* ---- Interrupt management ----------------------------------------------- */

BOOLEAN
RosvVirtioConHasPendingInterrupt(
    _In_ PROSV_VIRTIO_CON_STATE State)
{
    return State->InterruptPending;
}

VOID
RosvVirtioConClearPendingInterrupt(
    _Inout_ PROSV_VIRTIO_CON_STATE State)
{
    State->InterruptPending = FALSE;
}

/* ---- Host I/O: write to port (host -> guest) ---------------------------- */

NTSTATUS
RosvVirtioConPortWrite(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten)
{
    PROSV_VIRTIO_CON_PORT Port;
    KIRQL OldIrql;
    ULONG Written = 0;

    *BytesWritten = 0;

    if (PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS)
        return STATUS_INVALID_PARAMETER;

    Port = &State->Ports[PortIndex];
    if (!Port->Active)
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireSpinLock(&Port->TxLock, &OldIrql);

    for (ULONG i = 0; i < Length; i++)
    {
        if (Port->TxCount >= ROSV_VIRTIO_CON_PORT_BUF_SIZE)
            break;

        Port->TxBuf[Port->TxHead] = Data[i];
        Port->TxHead = (Port->TxHead + 1) % ROSV_VIRTIO_CON_PORT_BUF_SIZE;
        Port->TxCount++;
        Written++;
    }

    KeReleaseSpinLock(&Port->TxLock, OldIrql);

    *BytesWritten = Written;
    Port->BytesIn += Written;
    State->TotalBytesIn += Written;

    /* Try to flush into guest RX virtqueue immediately */
    if (Written > 0)
        RosvVirtioConFlushPortTx(State, PortIndex);

    return STATUS_SUCCESS;
}

/* ---- Host I/O: read from port (guest -> host) --------------------------- */

NTSTATUS
RosvVirtioConPortRead(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex,
    _Out_writes_bytes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead)
{
    PROSV_VIRTIO_CON_PORT Port;
    KIRQL OldIrql;
    ULONG Read = 0;

    *BytesRead = 0;

    if (PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS)
        return STATUS_INVALID_PARAMETER;

    Port = &State->Ports[PortIndex];
    if (!Port->Active)
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireSpinLock(&Port->RxLock, &OldIrql);

    while (Read < BufferSize && Port->RxCount > 0)
    {
        Buffer[Read++] = Port->RxBuf[Port->RxTail];
        Port->RxTail = (Port->RxTail + 1) % ROSV_VIRTIO_CON_PORT_BUF_SIZE;
        Port->RxCount--;
    }

    KeReleaseSpinLock(&Port->RxLock, OldIrql);

    *BytesRead = Read;
    return STATUS_SUCCESS;
}

ULONG
RosvVirtioConPortQueryTxSpace(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex)
{
    PROSV_VIRTIO_CON_PORT Port;
    KIRQL OldIrql;
    ULONG FreeSpace;

    if (PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS)
        return 0;

    Port = &State->Ports[PortIndex];
    if (!Port->Active)
        return 0;

    KeAcquireSpinLock(&Port->TxLock, &OldIrql);
    FreeSpace = ROSV_VIRTIO_CON_PORT_BUF_SIZE - Port->TxCount;
    KeReleaseSpinLock(&Port->TxLock, OldIrql);

    return FreeSpace;
}

NTSTATUS
RosvVirtioConPortUnread(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesUnwritten)
{
    PROSV_VIRTIO_CON_PORT Port;
    KIRQL OldIrql;
    ULONG Unwritten = 0;
    ULONG i;

    if (BytesUnwritten != NULL)
        *BytesUnwritten = 0;

    if (Length == 0)
        return STATUS_SUCCESS;

    if (PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS || Data == NULL)
        return STATUS_INVALID_PARAMETER;

    Port = &State->Ports[PortIndex];
    if (!Port->Active)
        return STATUS_INVALID_DEVICE_STATE;

    KeAcquireSpinLock(&Port->RxLock, &OldIrql);

    for (i = Length; i > 0 && Port->RxCount < ROSV_VIRTIO_CON_PORT_BUF_SIZE; i--)
    {
        Port->RxTail = (Port->RxTail + ROSV_VIRTIO_CON_PORT_BUF_SIZE - 1) %
                       ROSV_VIRTIO_CON_PORT_BUF_SIZE;
        Port->RxBuf[Port->RxTail] = Data[i - 1];
        Port->RxCount++;
        Unwritten++;
    }

    KeReleaseSpinLock(&Port->RxLock, OldIrql);

    if (BytesUnwritten != NULL)
        *BytesUnwritten = Unwritten;

    return (Unwritten == Length) ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

VOID
RosvVirtioConKickTx(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex)
{
    ULONG TxQueueIdx;

    if (PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS)
        return;

    TxQueueIdx = VIRTIO_CON_QUEUE_TX(PortIndex);
    if (TxQueueIdx >= ROSV_VIRTIO_CON_NUM_QUEUES)
        return;

    VirtioConProcessTx(State, TxQueueIdx);
}

/* ---- Flush host->guest data into RX virtqueue --------------------------- */

VOID
RosvVirtioConFlushPortTx(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex)
{
    PROSV_VM Vm = State->OwnerVm;
    ULONG RxQueueIdx;
    PROSV_VIRTQUEUE Vq;
    PROSV_VIRTIO_CON_PORT Port;
    USHORT AvailIdx, UsedIdx;
    KIRQL OldIrql;
    BOOLEAN Flushed = FALSE;

    if (PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS)
        return;

    Port = &State->Ports[PortIndex];
    if (!Port->Active || Port->TxCount == 0)
        return;

    RxQueueIdx = VIRTIO_CON_QUEUE_RX(PortIndex);
    if (RxQueueIdx >= ROSV_VIRTIO_CON_NUM_QUEUES)
        return;

    Vq = &State->Vq[RxQueueIdx];
    if (!Vq->Ready || Vq->Num == 0)
        return;

    AvailIdx = VirtioConReadAvailIdx(Vm, Vq->AvailGpa);
    UsedIdx = VirtioConReadUsedIdx(Vm, Vq->UsedGpa);

    while (Vq->LastAvailIdx != AvailIdx && Port->TxCount > 0)
    {
        USHORT DescIdx;
        VRING_DESC Desc;
        ULONG Written = 0;

        DescIdx = VirtioConReadAvailRing(Vm, Vq->AvailGpa,
                                          Vq->LastAvailIdx, Vq->Num);

        if (!VirtioConReadDesc(Vm, Vq->DescGpa, DescIdx, &Desc))
            break;

        if (!(Desc.Flags & VRING_DESC_F_WRITE) || Desc.Len == 0)
        {
            /* Not a writable buffer, skip */
            Vq->LastAvailIdx++;
            continue;
        }

        /* Copy data from TxBuf into this descriptor */
        KeAcquireSpinLock(&Port->TxLock, &OldIrql);

        {
            ULONG CopyLen = Desc.Len;
            if (CopyLen > Port->TxCount)
                CopyLen = Port->TxCount;

            /* Build a contiguous chunk to copy */
            UCHAR TmpBuf[256];
            ULONG TotalCopied = 0;

            while (TotalCopied < CopyLen)
            {
                ULONG Chunk = CopyLen - TotalCopied;
                if (Chunk > sizeof(TmpBuf))
                    Chunk = sizeof(TmpBuf);

                for (ULONG i = 0; i < Chunk; i++)
                {
                    TmpBuf[i] = Port->TxBuf[Port->TxTail];
                    Port->TxTail = (Port->TxTail + 1) % ROSV_VIRTIO_CON_PORT_BUF_SIZE;
                    Port->TxCount--;
                }

                RosvMemoryCopyToGpa(Vm, Desc.Addr + TotalCopied, TmpBuf, Chunk);
                TotalCopied += Chunk;
            }
            Written = TotalCopied;
        }

        KeReleaseSpinLock(&Port->TxLock, OldIrql);

        VirtioConWriteUsedEntry(Vm, Vq->UsedGpa, UsedIdx, Vq->Num,
                                 DescIdx, Written);
        UsedIdx++;
        Vq->LastAvailIdx++;
        Flushed = TRUE;
    }

    if (Flushed)
    {
        State->InterruptStatus |= VIRTIO_INT_VRING;
        State->InterruptPending = TRUE;
    }
}

/* ---- Port allocation for PTY binding ------------------------------------ */

ULONG
RosvVirtioConAllocatePort(
    _Inout_ PROSV_VIRTIO_CON_STATE State)
{
    /* Skip port 0 (reserved for primary console). Allocate from port 1+. */
    for (ULONG i = 1; i < State->ActivePortCount; i++)
    {
        if (State->Ports[i].Active && !State->Ports[i].Allocated)
        {
            State->Ports[i].Allocated = TRUE;
            ROSV_TRACE("virtio-con: allocated port %u for PTY binding", i);
            return i;
        }
    }

    ROSV_ERR("virtio-con: no free ports for PTY binding (active=%u)", State->ActivePortCount);
    return (ULONG)-1;
}

VOID
RosvVirtioConReleasePort(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex)
{
    if (PortIndex >= ROSV_VIRTIO_CON_MAX_PORTS)
        return;

    State->Ports[PortIndex].Allocated = FALSE;
    State->Ports[PortIndex].BoundPty = NULL;
    ROSV_TRACE("virtio-con: released port %u", PortIndex);
}
