/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     RTL8139 PCI NIC emulation (minimal probe/bring-up model)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Portions of register layout/behavior are derived from:
 *   QEMU hw/net/rtl8139.c
 *   Copyright (c) 2006 Igor Kovalenko
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"),
 *   to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *   and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions:
 *   The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 */

#include <rosv/rosv.h>
#include <rosv/device.h>
#include <rosv/vm.h>

#define ROSV_RTL8139_VENDOR_ID        0x10EC
#define ROSV_RTL8139_DEVICE_ID        0x8139
#define ROSV_RTL8139_CLASS_REV        0x02000010UL
#define ROSV_RTL8139_BAR0_MASK        0xFFFFFF01UL
#define ROSV_RTL8139_PCI_INT_LINE     11
#define ROSV_RTL8139_PCI_INT_PIN      1

/* Register offsets (from QEMU hw/net/rtl8139.c). */
#define RTL8139_REG_MAC0              0x00
#define RTL8139_REG_MAR0              0x08
#define RTL8139_REG_TX_STATUS0        0x10
#define RTL8139_REG_TX_ADDR0          0x20
#define RTL8139_REG_RX_BUF            0x30
#define RTL8139_REG_CHIP_CMD          0x37
#define RTL8139_REG_RX_BUF_PTR        0x38
#define RTL8139_REG_RX_BUF_ADDR       0x3A
#define RTL8139_REG_INTR_MASK         0x3C
#define RTL8139_REG_INTR_STATUS       0x3E
#define RTL8139_REG_TX_CONFIG         0x40
#define RTL8139_REG_RX_CONFIG         0x44
#define RTL8139_REG_CFG9346           0x50
#define RTL8139_REG_CONFIG1           0x52
#define RTL8139_REG_MEDIA_STATUS      0x58
#define RTL8139_REG_BASIC_MODE_STATUS 0x64

/* 93C46 serial EEPROM bits via CFG9346 register. */
#define RTL8139_EE_DATA_READ          0x01
#define RTL8139_EE_DATA_WRITE         0x02
#define RTL8139_EE_SHIFT_CLK          0x04
#define RTL8139_EE_CS                 0x08

/* ChipCmd bits (from QEMU). */
#define RTL8139_CMD_RESET             0x10
#define RTL8139_CMD_RX_ENB            0x08
#define RTL8139_CMD_TX_ENB            0x04
#define RTL8139_CMD_RX_BUF_EMPTY      0x01

/* Interrupt bits (from QEMU). */
#define RTL8139_INT_RX_OK             0x0001
#define RTL8139_INT_TX_OK             0x0004

#define RTL8139_TX_STATUS_LEN_MASK    0x1FFF
#define RTL8139_TX_STATUS_TX_OK       0x8000

static const UCHAR RosvRtl8139DefaultMac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };

static
VOID
RosvRtl8139SetDwordByte(
    _Inout_ PULONG Target,
    _In_ ULONG Offset,
    _In_ UCHAR Value)
{
    ULONG Shift = (Offset & 3U) * 8U;
    ULONG Mask = 0xFFUL << Shift;
    *Target = (*Target & ~Mask) | ((ULONG)Value << Shift);
}

static
UCHAR
RosvRtl8139GetDwordByte(
    _In_ ULONG Value,
    _In_ ULONG Offset)
{
    ULONG Shift = (Offset & 3U) * 8U;
    return (UCHAR)((Value >> Shift) & 0xFFU);
}


static
USHORT
RosvRtl8139EepromReadWord(
    _In_ PROSV_RTL8139_STATE Nic,
    _In_ UCHAR Address)
{
    switch (Address & 0x3F)
    {
        case 7:
            return (USHORT)Nic->Mac[0] | ((USHORT)Nic->Mac[1] << 8);
        case 8:
            return (USHORT)Nic->Mac[2] | ((USHORT)Nic->Mac[3] << 8);
        case 9:
            return (USHORT)Nic->Mac[4] | ((USHORT)Nic->Mac[5] << 8);
        default:
            return 0;
    }
}

static
VOID
RosvRtl8139EepromResetTransaction(
    _Inout_ PROSV_RTL8139_STATE Nic)
{
    Nic->EepromInputStarted = FALSE;
    Nic->EepromOutputActive = FALSE;
    Nic->EepromOpcode = 0;
    Nic->EepromOpcodeBits = 0;
    Nic->EepromAddress = 0;
    Nic->EepromAddressBits = 0;
    Nic->EepromShiftOut = 0;
    Nic->EepromShiftOutBits = 0;
    Nic->EepromDoBit = 0;
}

static
VOID
RosvRtl8139EepromWriteCfg9346(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_ UCHAR Value)
{
    BOOLEAN Cs = ((Value & RTL8139_EE_CS) != 0);
    BOOLEAN Clock = ((Value & RTL8139_EE_SHIFT_CLK) != 0);
    BOOLEAN DataIn = ((Value & RTL8139_EE_DATA_WRITE) != 0);
    BOOLEAN RisingEdge;

    Nic->Cfg9346 = (UCHAR)(Value & ~RTL8139_EE_DATA_READ);

    if (Cs != (BOOLEAN)Nic->EepromPrevCs)
    {
        RosvRtl8139EepromResetTransaction(Nic);
    }

    RisingEdge = (BOOLEAN)(Cs && Clock && !Nic->EepromPrevClock);
    if (RisingEdge)
    {
        if (Nic->EepromOutputActive)
        {
            if (Nic->EepromShiftOutBits > 0)
            {
                Nic->EepromDoBit = (UCHAR)((Nic->EepromShiftOut & 0x8000U) ? 1 : 0);
                Nic->EepromShiftOut <<= 1;
                Nic->EepromShiftOutBits--;
            }
            else
            {
                Nic->EepromDoBit = 0;
            }
        }
        else if (!Nic->EepromInputStarted)
        {
            /*
             * Ignore preamble zeros. Start a command frame when DI first goes high.
             */
            if (DataIn)
            {
                Nic->EepromInputStarted = TRUE;
            }
        }
        else if (Nic->EepromOpcodeBits < 2)
        {
            Nic->EepromOpcode = (UCHAR)((Nic->EepromOpcode << 1) | (DataIn ? 1 : 0));
            Nic->EepromOpcodeBits++;
        }
        else
        {
            /*
             * 93C46 model: 6-bit address after start + 2-bit opcode.
             * If the guest probes with a different addr length first, this still
             * converges to the 93C46 behavior that Linux expects on RTL8139.
             */
            Nic->EepromAddress = (UCHAR)((Nic->EepromAddress << 1) | (DataIn ? 1 : 0));
            Nic->EepromAddressBits++;
            if (Nic->EepromAddressBits == 6)
            {
                if (Nic->EepromOpcode == 0x2) /* READ */
                {
                    Nic->EepromShiftOut = RosvRtl8139EepromReadWord(Nic, Nic->EepromAddress);
                    Nic->EepromShiftOutBits = 16;
                    Nic->EepromOutputActive = TRUE;
                }
                else
                {
                    RosvRtl8139EepromResetTransaction(Nic);
                }
            }
        }
    }

    Nic->EepromPrevClock = (UCHAR)(Cs ? (Clock ? 1 : 0) : 0);
    Nic->EepromPrevCs = (UCHAR)(Cs ? 1 : 0);
}

static
ULONG
RosvRtl8139RxRingBytes(
    _In_ PROSV_RTL8139_STATE Nic)
{
    switch ((Nic->RxConfig >> 11) & 0x3)
    {
        case 0: return 8 * 1024 + 16;
        case 1: return 16 * 1024 + 16;
        case 2: return 32 * 1024 + 16;
        default: return 64 * 1024 + 16;
    }
}

static
VOID
RosvRtl8139RefreshInterruptPending(
    _Inout_ PROSV_RTL8139_STATE Nic)
{
    BOOLEAN WasPending = Nic->InterruptPending;
    Nic->InterruptPending = ((Nic->IntrStatus & Nic->IntrMask) != 0);

    /* Wake vCPU if an interrupt just became pending */
    if (Nic->InterruptPending && !WasPending && Nic->OwnerVm != NULL)
    {
        PROSV_VM Vm = (PROSV_VM)Nic->OwnerVm;
        KeSetEvent(&Vm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
    }
}

static
VOID
RosvRtl8139SignalInterrupt(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_ USHORT InterruptBits)
{
    Nic->IntrStatus |= InterruptBits;
    RosvRtl8139RefreshInterruptPending(Nic);
}

static
BOOLEAN
RosvRtl8139ReadGuestMemory(
    _In_ PROSV_VM Vm,
    _In_ ULONG64 GuestPhysAddress,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONG Done = 0;

    while (Done < Length)
    {
        ULONG64 CurGpa = GuestPhysAddress + Done;
        ULONG PageOffset = (ULONG)(CurGpa & (PAGE_SIZE - 1));
        ULONG Chunk = Length - Done;
        ULONG MaxChunk = PAGE_SIZE - PageOffset;
        PVOID Hva;

        if (Chunk > MaxChunk)
            Chunk = MaxChunk;

        Hva = RosvMemoryGpaToHva(Vm, CurGpa);
        if (Hva == NULL)
            return FALSE;

        RtlCopyMemory(Buffer + Done, Hva, Chunk);
        Done += Chunk;
    }

    return TRUE;
}

static
BOOLEAN
RosvRtl8139WriteRingMemory(
    _In_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Offset,
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    PROSV_VM Vm = (PROSV_VM)Nic->OwnerVm;
    ULONG RingBytes;
    ULONG FirstPart;
    NTSTATUS Status;

    if (Vm == NULL || Nic->RxBuf == 0)
        return FALSE;

    RingBytes = RosvRtl8139RxRingBytes(Nic);
    if (RingBytes == 0 || Offset >= RingBytes)
        return FALSE;

    FirstPart = RingBytes - Offset;
    if (FirstPart > Length)
        FirstPart = Length;

    Status = RosvMemoryCopyToGpa(Vm, (ULONG64)Nic->RxBuf + Offset, Buffer, FirstPart);
    if (!NT_SUCCESS(Status))
        return FALSE;

    if (Length > FirstPart)
    {
        Status = RosvMemoryCopyToGpa(Vm,
                                     (ULONG64)Nic->RxBuf,
                                     Buffer + FirstPart,
                                     Length - FirstPart);
        if (!NT_SUCCESS(Status))
            return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
RosvRtl8139QueueTxPacket(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    KIRQL OldIrql;
    PROSV_NET_PACKET Packet;

    if (Length == 0 || Length > ROSV_NET_PACKET_MAX)
        return FALSE;

    KeAcquireSpinLock(&Nic->NetLock, &OldIrql);
    if (Nic->TxQueueCount >= RTL_NUMBER_OF(Nic->TxQueue))
    {
        KeReleaseSpinLock(&Nic->NetLock, OldIrql);
        return FALSE;
    }

    Packet = &Nic->TxQueue[Nic->TxQueueHead];
    Packet->Length = Length;
    RtlCopyMemory(Packet->Data, Data, Length);

    Nic->TxQueueHead = (Nic->TxQueueHead + 1) % RTL_NUMBER_OF(Nic->TxQueue);
    Nic->TxQueueCount++;

    KeReleaseSpinLock(&Nic->NetLock, OldIrql);
    return TRUE;
}

static
VOID
RosvRtl8139SoftReset(
    _Inout_ PROSV_RTL8139_STATE Nic)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Nic->TxStatus); Index++)
        Nic->TxStatus[Index] = 0;
    for (Index = 0; Index < RTL_NUMBER_OF(Nic->TxAddr); Index++)
        Nic->TxAddr[Index] = 0;

    RtlCopyMemory(Nic->Mac, RosvRtl8139DefaultMac, sizeof(Nic->Mac));
    RtlZeroMemory(Nic->Mar, sizeof(Nic->Mar));
    Nic->RxBuf = 0;
    Nic->RxBufPtr = 0;
    Nic->RxBufAddr = 0;
    Nic->RxPendingCount = 0;
    Nic->IntrMask = 0;
    Nic->IntrStatus = 0;
    Nic->TxConfig = 0;
    Nic->RxConfig = 0;
    Nic->ChipCmd = 0;
    Nic->Cfg9346 = 0;
    Nic->EepromPrevClock = 0;
    Nic->EepromPrevCs = 0;
    Nic->EepromDoBit = 0;
    Nic->EepromInputStarted = FALSE;
    Nic->EepromOutputActive = FALSE;
    Nic->EepromOpcode = 0;
    Nic->EepromOpcodeBits = 0;
    Nic->EepromAddress = 0;
    Nic->EepromAddressBits = 0;
    Nic->EepromShiftOut = 0;
    Nic->EepromShiftOutBits = 0;
    Nic->Config1 = 0x0C;           /* IO+MEM mapped registers available */
    Nic->MediaStatus = 0xD0;       /* Link up pattern as exposed by QEMU */
    Nic->BasicModeStatus = 0x782D; /* Auto-neg completed + link status up */
    Nic->InterruptPending = FALSE;
    Nic->TxQueueHead = 0;
    Nic->TxQueueTail = 0;
    Nic->TxQueueCount = 0;
}

VOID
RosvRtl8139Initialize(
    _Inout_ PROSV_RTL8139_STATE Nic)
{
    RtlZeroMemory(Nic, sizeof(*Nic));
    RtlCopyMemory(Nic->Mac, RosvRtl8139DefaultMac, sizeof(Nic->Mac));
    Nic->PciBar0 = (ROSV_RTL8139_IO_BASE | 1U);
    Nic->PciInterruptLine = ROSV_RTL8139_PCI_INT_LINE;
    Nic->PciInterruptPin = ROSV_RTL8139_PCI_INT_PIN;
    Nic->PciCommand = 0;
    Nic->Bar0Probe = FALSE;
    KeInitializeSpinLock(&Nic->NetLock);
    RosvRtl8139SoftReset(Nic);

    ROSV_TRACE("RTL8139 initialized (BAR0=0x%X, MAC=%02X:%02X:%02X:%02X:%02X:%02X)",
               Nic->PciBar0,
               Nic->Mac[0], Nic->Mac[1], Nic->Mac[2],
               Nic->Mac[3], Nic->Mac[4], Nic->Mac[5]);
}

VOID
RosvRtl8139SetOwnerVm(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_opt_ PVOID Vm)
{
    Nic->OwnerVm = Vm;
}

ULONG
RosvRtl8139PciConfigRead(
    _In_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Offset)
{
    switch (Offset & ~3U)
    {
        case 0x00:
            return ((ULONG)ROSV_RTL8139_DEVICE_ID << 16) | ROSV_RTL8139_VENDOR_ID;

        case 0x04:
            return (ULONG)Nic->PciCommand;

        case 0x08:
            return ROSV_RTL8139_CLASS_REV;

        case 0x0C:
            return 0;

        case 0x10:
            if (Nic->Bar0Probe)
                return ROSV_RTL8139_BAR0_MASK;
            return Nic->PciBar0;

        case 0x2C:
            return ((ULONG)ROSV_RTL8139_DEVICE_ID << 16) | ROSV_RTL8139_VENDOR_ID;

        case 0x3C:
            return (ULONG)Nic->PciInterruptLine | ((ULONG)Nic->PciInterruptPin << 8);

        default:
            return 0;
    }
}

VOID
RosvRtl8139PciConfigWrite(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    switch (Offset & ~3U)
    {
        case 0x04:
            /* IO/MEM/BUSMASTER only for now. */
            Nic->PciCommand = (USHORT)(Value & 0x0007);
            break;

        case 0x10:
            if (Value == 0xFFFFFFFFUL)
            {
                Nic->Bar0Probe = TRUE;
            }
            else
            {
                /*
                 * Keep a fixed BAR in this model. Linux still sees correct
                 * size probing behavior via the Bar0Probe path.
                 */
                Nic->Bar0Probe = FALSE;
                Nic->PciBar0 = (ROSV_RTL8139_IO_BASE | 1U);
            }
            break;

        case 0x3C:
            Nic->PciInterruptLine = (UCHAR)(Value & 0xFF);
            break;

        default:
            break;
    }
}

static
UCHAR
RosvRtl8139ReadByte(
    _In_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Offset)
{
    ULONG Index;

    if (Offset >= RTL8139_REG_MAC0 && Offset <= RTL8139_REG_MAC0 + 5)
        return Nic->Mac[Offset - RTL8139_REG_MAC0];

    if (Offset >= RTL8139_REG_MAR0 && Offset <= RTL8139_REG_MAR0 + 7)
        return Nic->Mar[Offset - RTL8139_REG_MAR0];

    if (Offset >= RTL8139_REG_TX_STATUS0 && Offset < RTL8139_REG_TX_STATUS0 + 16)
    {
        Index = (Offset - RTL8139_REG_TX_STATUS0) / 4;
        return RosvRtl8139GetDwordByte(Nic->TxStatus[Index], Offset);
    }

    if (Offset >= RTL8139_REG_TX_ADDR0 && Offset < RTL8139_REG_TX_ADDR0 + 16)
    {
        Index = (Offset - RTL8139_REG_TX_ADDR0) / 4;
        return RosvRtl8139GetDwordByte(Nic->TxAddr[Index], Offset);
    }

    if (Offset >= RTL8139_REG_RX_BUF && Offset <= RTL8139_REG_RX_BUF + 3)
        return RosvRtl8139GetDwordByte(Nic->RxBuf, Offset);

    if (Offset == RTL8139_REG_CHIP_CMD)
    {
        UCHAR Value = Nic->ChipCmd;
        if (Nic->RxPendingCount == 0)
            Value |= RTL8139_CMD_RX_BUF_EMPTY;
        return Value;
    }

    if (Offset == RTL8139_REG_RX_BUF_PTR)
        return (UCHAR)(Nic->RxBufPtr & 0xFF);
    if (Offset == RTL8139_REG_RX_BUF_PTR + 1)
        return (UCHAR)((Nic->RxBufPtr >> 8) & 0xFF);

    if (Offset == RTL8139_REG_RX_BUF_ADDR)
        return (UCHAR)(Nic->RxBufAddr & 0xFF);
    if (Offset == RTL8139_REG_RX_BUF_ADDR + 1)
        return (UCHAR)((Nic->RxBufAddr >> 8) & 0xFF);

    if (Offset == RTL8139_REG_INTR_MASK)
        return (UCHAR)(Nic->IntrMask & 0xFF);
    if (Offset == RTL8139_REG_INTR_MASK + 1)
        return (UCHAR)((Nic->IntrMask >> 8) & 0xFF);

    if (Offset == RTL8139_REG_INTR_STATUS)
        return (UCHAR)(Nic->IntrStatus & 0xFF);
    if (Offset == RTL8139_REG_INTR_STATUS + 1)
        return (UCHAR)((Nic->IntrStatus >> 8) & 0xFF);

    if (Offset >= RTL8139_REG_TX_CONFIG && Offset <= RTL8139_REG_TX_CONFIG + 3)
        return RosvRtl8139GetDwordByte(Nic->TxConfig, Offset);

    if (Offset >= RTL8139_REG_RX_CONFIG && Offset <= RTL8139_REG_RX_CONFIG + 3)
        return RosvRtl8139GetDwordByte(Nic->RxConfig, Offset);

    if (Offset == RTL8139_REG_CFG9346)
    {
        return (UCHAR)((Nic->Cfg9346 & ~RTL8139_EE_DATA_READ) |
                       (Nic->EepromDoBit ? RTL8139_EE_DATA_READ : 0));
    }

    if (Offset == RTL8139_REG_CONFIG1)
        return Nic->Config1;

    if (Offset == RTL8139_REG_MEDIA_STATUS)
        return (UCHAR)(Nic->MediaStatus | ((~Nic->BasicModeStatus) & 0x04));

    if (Offset == RTL8139_REG_BASIC_MODE_STATUS)
        return (UCHAR)(Nic->BasicModeStatus & 0xFF);
    if (Offset == RTL8139_REG_BASIC_MODE_STATUS + 1)
        return (UCHAR)((Nic->BasicModeStatus >> 8) & 0xFF);

    return 0xFF;
}

static
VOID
RosvRtl8139WriteByte(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Offset,
    _In_ UCHAR Value)
{
    ULONG Index;

    if (Offset >= RTL8139_REG_MAC0 && Offset <= RTL8139_REG_MAC0 + 5)
    {
        Nic->Mac[Offset - RTL8139_REG_MAC0] = Value;
        return;
    }

    if (Offset >= RTL8139_REG_MAR0 && Offset <= RTL8139_REG_MAR0 + 7)
    {
        Nic->Mar[Offset - RTL8139_REG_MAR0] = Value;
        return;
    }

    if (Offset >= RTL8139_REG_TX_STATUS0 && Offset < RTL8139_REG_TX_STATUS0 + 16)
    {
        Index = (Offset - RTL8139_REG_TX_STATUS0) / 4;
        RosvRtl8139SetDwordByte(&Nic->TxStatus[Index], Offset, Value);
        return;
    }

    if (Offset >= RTL8139_REG_TX_ADDR0 && Offset < RTL8139_REG_TX_ADDR0 + 16)
    {
        Index = (Offset - RTL8139_REG_TX_ADDR0) / 4;
        RosvRtl8139SetDwordByte(&Nic->TxAddr[Index], Offset, Value);
        return;
    }

    if (Offset >= RTL8139_REG_RX_BUF && Offset <= RTL8139_REG_RX_BUF + 3)
    {
        RosvRtl8139SetDwordByte(&Nic->RxBuf, Offset, Value);
        return;
    }

    if (Offset == RTL8139_REG_CHIP_CMD)
    {
        if ((Value & RTL8139_CMD_RESET) != 0)
            RosvRtl8139SoftReset(Nic);

        Nic->ChipCmd = Value & (RTL8139_CMD_RX_ENB | RTL8139_CMD_TX_ENB);
        return;
    }

    if (Offset == RTL8139_REG_RX_BUF_PTR)
    {
        Nic->RxBufPtr = (Nic->RxBufPtr & 0xFF00) | Value;
        return;
    }
    if (Offset == RTL8139_REG_RX_BUF_PTR + 1)
    {
        Nic->RxBufPtr = (Nic->RxBufPtr & 0x00FF) | ((USHORT)Value << 8);
        if (Nic->RxPendingCount > 0)
            Nic->RxPendingCount--;
        return;
    }

    if (Offset == RTL8139_REG_RX_BUF_ADDR)
    {
        Nic->RxBufAddr = (Nic->RxBufAddr & 0xFF00) | Value;
        return;
    }
    if (Offset == RTL8139_REG_RX_BUF_ADDR + 1)
    {
        Nic->RxBufAddr = (Nic->RxBufAddr & 0x00FF) | ((USHORT)Value << 8);
        return;
    }

    if (Offset == RTL8139_REG_INTR_MASK)
    {
        Nic->IntrMask = (Nic->IntrMask & 0xFF00) | Value;
        RosvRtl8139RefreshInterruptPending(Nic);
        return;
    }
    if (Offset == RTL8139_REG_INTR_MASK + 1)
    {
        Nic->IntrMask = (Nic->IntrMask & 0x00FF) | ((USHORT)Value << 8);
        RosvRtl8139RefreshInterruptPending(Nic);
        return;
    }

    if (Offset == RTL8139_REG_INTR_STATUS)
    {
        Nic->IntrStatus &= (USHORT)~Value;
        RosvRtl8139RefreshInterruptPending(Nic);
        return;
    }
    if (Offset == RTL8139_REG_INTR_STATUS + 1)
    {
        Nic->IntrStatus &= (USHORT)~((USHORT)Value << 8);
        RosvRtl8139RefreshInterruptPending(Nic);
        return;
    }

    if (Offset >= RTL8139_REG_TX_CONFIG && Offset <= RTL8139_REG_TX_CONFIG + 3)
    {
        RosvRtl8139SetDwordByte(&Nic->TxConfig, Offset, Value);
        return;
    }

    if (Offset >= RTL8139_REG_RX_CONFIG && Offset <= RTL8139_REG_RX_CONFIG + 3)
    {
        RosvRtl8139SetDwordByte(&Nic->RxConfig, Offset, Value);
        return;
    }

    if (Offset == RTL8139_REG_CFG9346)
    {
        RosvRtl8139EepromWriteCfg9346(Nic, Value);
        return;
    }

    if (Offset == RTL8139_REG_CONFIG1)
    {
        Nic->Config1 = (UCHAR)((Value & ~0x0C) | (Nic->Config1 & 0x0C));
        return;
    }
}

static
VOID
RosvRtl8139HandleTxKick(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Queue,
    _In_ ULONG TxStatus)
{
    PROSV_VM Vm = (PROSV_VM)Nic->OwnerVm;
    ULONG Length = TxStatus & RTL8139_TX_STATUS_LEN_MASK;
    UCHAR Frame[ROSV_NET_PACKET_MAX];

    if (Length > ROSV_NET_PACKET_MAX)
        Length = ROSV_NET_PACKET_MAX;

    if ((Nic->ChipCmd & RTL8139_CMD_TX_ENB) != 0 &&
        Length > 0 &&
        Queue < RTL_NUMBER_OF(Nic->TxAddr) &&
        Vm != NULL &&
        Nic->TxAddr[Queue] != 0 &&
        RosvRtl8139ReadGuestMemory(Vm, Nic->TxAddr[Queue], Frame, Length))
    {
        RosvRtl8139QueueTxPacket(Nic, Frame, Length);
    }

    Nic->TxStatus[Queue] = (TxStatus & ~RTL8139_TX_STATUS_TX_OK) | RTL8139_TX_STATUS_TX_OK;
    RosvRtl8139SignalInterrupt(Nic, RTL8139_INT_TX_OK);
}

BOOLEAN
RosvRtl8139IoIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    PROSV_RTL8139_STATE Nic = (PROSV_RTL8139_STATE)Context;
    ULONG Base;
    ULONG Offset;
    ULONG Index;
    ULONG Result = 0;

    if (Nic == NULL || Value == NULL || (Size != 1 && Size != 2 && Size != 4))
        return FALSE;

    Base = Nic->PciBar0 & ~0x3U;
    if (Port < Base || (ULONG)Port + Size > Base + ROSV_RTL8139_IO_SIZE)
    {
        *Value = 0xFFFFFFFF;
        return FALSE;
    }

    Offset = Port - Base;
    for (Index = 0; Index < Size; Index++)
    {
        Result |= ((ULONG)RosvRtl8139ReadByte(Nic, Offset + Index)) << (Index * 8);
    }

    *Value = Result;
    return TRUE;
}

BOOLEAN
RosvRtl8139IoOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    PROSV_RTL8139_STATE Nic = (PROSV_RTL8139_STATE)Context;
    ULONG Base;
    ULONG Offset;
    ULONG Index;

    if (Nic == NULL || (Size != 1 && Size != 2 && Size != 4))
        return FALSE;

    Base = Nic->PciBar0 & ~0x3U;
    if (Port < Base || (ULONG)Port + Size > Base + ROSV_RTL8139_IO_SIZE)
        return FALSE;

    Offset = Port - Base;

    /*
     * Fast path for aligned TxStatus dword writes: emulate immediate transmit
     * completion so the guest driver can progress without a full TX engine.
     */
    if (Size == 4 &&
        Offset >= RTL8139_REG_TX_STATUS0 &&
        Offset < RTL8139_REG_TX_STATUS0 + 16 &&
        (Offset & 3U) == 0)
    {
        ULONG Queue = (Offset - RTL8139_REG_TX_STATUS0) / 4;
        RosvRtl8139HandleTxKick(Nic, Queue, Value);
        return TRUE;
    }

    for (Index = 0; Index < Size; Index++)
    {
        UCHAR ByteValue = (UCHAR)((Value >> (Index * 8)) & 0xFF);
        RosvRtl8139WriteByte(Nic, Offset + Index, ByteValue);
    }

    return TRUE;
}

BOOLEAN
RosvRtl8139DequeueTxPacket(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _Out_ PROSV_NET_PACKET PacketOut)
{
    KIRQL OldIrql;

    if (Nic == NULL || PacketOut == NULL)
        return FALSE;

    KeAcquireSpinLock(&Nic->NetLock, &OldIrql);
    if (Nic->TxQueueCount == 0)
    {
        KeReleaseSpinLock(&Nic->NetLock, OldIrql);
        return FALSE;
    }

    *PacketOut = Nic->TxQueue[Nic->TxQueueTail];
    Nic->TxQueueTail = (Nic->TxQueueTail + 1) % RTL_NUMBER_OF(Nic->TxQueue);
    Nic->TxQueueCount--;

    KeReleaseSpinLock(&Nic->NetLock, OldIrql);

    return TRUE;
}

BOOLEAN
RosvRtl8139InjectRxPacket(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    UCHAR Header[4];
    UCHAR Crc[4] = { 0 };
    ULONG RingBytes;
    ULONG WriteOffset;
    ULONG PacketBytes;
    ULONG Advance;
    USHORT PacketStatus = 0x0001; /* RxOK */
    USHORT PacketLength;

    if (Nic == NULL || Data == NULL || Length == 0 || Length > ROSV_NET_PACKET_MAX)
        return FALSE;

    if ((Nic->ChipCmd & RTL8139_CMD_RX_ENB) == 0 || Nic->RxBuf == 0 || Nic->OwnerVm == NULL)
        return FALSE;

    RingBytes = RosvRtl8139RxRingBytes(Nic);
    PacketBytes = 4 + Length + 4; /* status + length + payload + CRC */
    Advance = (PacketBytes + 3) & ~3U;
    if (Advance >= RingBytes)
        return FALSE;

    WriteOffset = Nic->RxBufAddr % RingBytes;
    PacketLength = (USHORT)(Length + 4); /* includes CRC as expected by driver */

    Header[0] = (UCHAR)(PacketStatus & 0xFF);
    Header[1] = (UCHAR)((PacketStatus >> 8) & 0xFF);
    Header[2] = (UCHAR)(PacketLength & 0xFF);
    Header[3] = (UCHAR)((PacketLength >> 8) & 0xFF);

    if (!RosvRtl8139WriteRingMemory(Nic, WriteOffset, Header, sizeof(Header)))
        return FALSE;
    WriteOffset = (WriteOffset + sizeof(Header)) % RingBytes;

    if (!RosvRtl8139WriteRingMemory(Nic, WriteOffset, Data, Length))
        return FALSE;
    WriteOffset = (WriteOffset + Length) % RingBytes;

    if (!RosvRtl8139WriteRingMemory(Nic, WriteOffset, Crc, sizeof(Crc)))
        return FALSE;

    Nic->RxBufAddr = (USHORT)((Nic->RxBufAddr + Advance) % RingBytes);
    Nic->RxPendingCount++;
    RosvRtl8139SignalInterrupt(Nic, RTL8139_INT_RX_OK);
    return TRUE;
}

BOOLEAN
RosvRtl8139HasPendingInterrupt(
    _In_ PROSV_RTL8139_STATE Nic)
{
    KIRQL OldIrql;
    BOOLEAN Pending;

    if (Nic == NULL)
        return FALSE;

    KeAcquireSpinLock(&Nic->NetLock, &OldIrql);
    Pending = Nic->InterruptPending;
    KeReleaseSpinLock(&Nic->NetLock, OldIrql);
    return Pending;
}

VOID
RosvRtl8139ClearPendingInterrupt(
    _Inout_ PROSV_RTL8139_STATE Nic)
{
    KIRQL OldIrql;

    if (Nic == NULL)
        return;

    KeAcquireSpinLock(&Nic->NetLock, &OldIrql);
    Nic->InterruptPending = FALSE;
    KeReleaseSpinLock(&Nic->NetLock, OldIrql);
}
