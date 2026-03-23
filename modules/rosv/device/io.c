/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     I/O port dispatch and common device handlers
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/device.h>
#include <rosv/vm.h>

/* ---- Common I/O port handlers ------------------------------------------- */

#define ROSV_PIT_FREQUENCY_HZ 1193182ULL

/* Log each unhandled I/O port once per direction to avoid flood-level spam. */
static UCHAR g_UnhandledInBitmap[0x10000 / 8];
static UCHAR g_UnhandledOutBitmap[0x10000 / 8];

static
BOOLEAN
RosvIoShouldLogUnhandled(
    _In_ USHORT Port,
    _In_ BOOLEAN IsOut)
{
    PUCHAR Bitmap;
    ULONG ByteIndex;
    UCHAR Mask;

    Bitmap = IsOut ? g_UnhandledOutBitmap : g_UnhandledInBitmap;
    ByteIndex = (ULONG)(Port >> 3);
    Mask = (UCHAR)(1u << (Port & 7));

    if (Bitmap[ByteIndex] & Mask)
        return FALSE;

    Bitmap[ByteIndex] |= Mask;
    return TRUE;
}

/*
 * POST code port (0x80) - Linux writes diagnostic codes here during boot.
 * Just log the value.
 */
static BOOLEAN
RosvPostCodeIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Size);

    *Value = 0x00;
    return TRUE;
}

static BOOLEAN
RosvPostCodeOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    static UCHAR LastPostCode = 0xFF;
    UCHAR Code;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Size);

    Code = (UCHAR)Value;
    if (Code != LastPostCode)
    {
        LastPostCode = Code;
        ROSV_DEBUG("POST code: 0x%02X", Code);
    }

    return TRUE;
}

/*
 * CMOS RTC (0x70/0x71).
 * Port 0x70 = index register (write), port 0x71 = data register (read/write).
 */
static UCHAR CmosIndex = 0;

static
UCHAR
RosvBcdEncode(
    _In_ USHORT Value)
{
    Value %= 100;
    return (UCHAR)(((Value / 10) << 4) | (Value % 10));
}

static
UCHAR
RosvCmosReadRegister(
    _In_ UCHAR Register)
{
    LARGE_INTEGER SystemTime;
    TIME_FIELDS Fields;

    KeQuerySystemTime(&SystemTime);
    RtlTimeToTimeFields(&SystemTime, &Fields);

    switch (Register & 0x7F)
    {
    case 0x00: return RosvBcdEncode((USHORT)Fields.Second);
    case 0x02: return RosvBcdEncode((USHORT)Fields.Minute);
    case 0x04: return RosvBcdEncode((USHORT)Fields.Hour);
    case 0x06: return RosvBcdEncode((USHORT)((Fields.Weekday + 1) % 8));
    case 0x07: return RosvBcdEncode((USHORT)Fields.Day);
    case 0x08: return RosvBcdEncode((USHORT)Fields.Month);
    case 0x09: return RosvBcdEncode((USHORT)(Fields.Year % 100));
    case 0x0A: return 0x26; /* Divider + periodic rate, UIP clear. */
    case 0x0B: return 0x02; /* 24-hour mode, BCD encoding. */
    case 0x0C: return 0x00; /* No pending IRQ flags. */
    case 0x0D: return 0x80; /* RTC battery good. */
    case 0x0E: return 0x00; /* Diagnostic status. */
    case 0x0F: return 0x00; /* Shutdown status. */
    case 0x15: return 0x80; /* Base memory low (640KB = 0x0280). */
    case 0x16: return 0x02; /* Base memory high. */
    case 0x17: return 0x00; /* Extended memory low. */
    case 0x18: return 0xFC; /* Extended memory high (64512KB). */
    case 0x32: return RosvBcdEncode((USHORT)(Fields.Year / 100));
    default:   return 0x00;
    }
}

/* i8042 keyboard controller (PS/2) minimal emulation */
#define I8042_STATUS_OBF         0x01
#define I8042_STATUS_IBF         0x02
#define I8042_STATUS_SYS         0x04
#define I8042_STATUS_CMD         0x08
#define I8042_STATUS_AUX_OBF     0x20

typedef enum _ROSV_I8042_PENDING_WRITE {
    RosvI8042WriteNone = 0,
    RosvI8042WriteCommandByte,
    RosvI8042WriteOutputPort,
    RosvI8042WriteMouseCommand,
    RosvI8042WriteKeyboardParam
} ROSV_I8042_PENDING_WRITE;

#define ROSV_I8042_FIFO_SIZE     32

typedef struct _ROSV_I8042_STATE {
    UCHAR Status;
    UCHAR CommandByte;
    UCHAR OutputPort;
    BOOLEAN KeyboardEnabled;
    BOOLEAN AuxEnabled;
    ROSV_I8042_PENDING_WRITE PendingWrite;
    UCHAR Fifo[ROSV_I8042_FIFO_SIZE];
    ULONG Head;
    ULONG Tail;
    ULONG Count;
} ROSV_I8042_STATE;

static ROSV_I8042_STATE I8042State;

static
VOID
RosvRequestVmResetStop(
    _In_opt_ PROSV_CONSOLE_CONTEXT Console,
    _In_ PCSTR Source,
    _In_ UCHAR Value)
{
    PROSV_VM Vm = (Console != NULL) ? Console->OwnerVm : NULL;

    if (Vm == NULL)
    {
        ROSV_WARN("RESET: request via %s value=0x%02X but no owner VM context",
                  Source,
                  Value);
        return;
    }

    ROSV_WARN("RESET: guest requested reset via %s value=0x%02X; stopping VM",
              Source,
              Value);
    /*
     * TODO(reset-machine): implement full chipset reset semantics
     * (CPU + device model reinitialization + reboot entry) instead of
     * stop-only behavior.
     */
    Vm->Vcpu.Running = FALSE;
    if (Vm->State == RosvVmStateRunning)
        Vm->State = RosvVmStateStopped;
}

static VOID
RosvI8042UpdateStatus(
    VOID)
{
    I8042State.Status &= (UCHAR)~(I8042_STATUS_OBF | I8042_STATUS_IBF | I8042_STATUS_AUX_OBF);
    I8042State.Status |= I8042_STATUS_SYS;

    if (I8042State.Count != 0)
        I8042State.Status |= I8042_STATUS_OBF;
}

static VOID
RosvI8042QueueByte(
    _In_ UCHAR Byte)
{
    if (I8042State.Count >= ROSV_I8042_FIFO_SIZE)
    {
        ROSV_WARN("i8042: FIFO overflow, dropping byte 0x%02X", Byte);
        return;
    }

    I8042State.Fifo[I8042State.Tail] = Byte;
    I8042State.Tail = (I8042State.Tail + 1) % ROSV_I8042_FIFO_SIZE;
    I8042State.Count++;
    RosvI8042UpdateStatus();
}

static UCHAR
RosvI8042PopByte(
    VOID)
{
    UCHAR Byte = 0;

    if (I8042State.Count == 0)
    {
        RosvI8042UpdateStatus();
        return 0;
    }

    Byte = I8042State.Fifo[I8042State.Head];
    I8042State.Head = (I8042State.Head + 1) % ROSV_I8042_FIFO_SIZE;
    I8042State.Count--;
    RosvI8042UpdateStatus();
    return Byte;
}

static BOOLEAN
RosvI8042In(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    UCHAR Value8;
    ULONG Mask;

    UNREFERENCED_PARAMETER(Context);

    if (Port == 0x64)
    {
        RosvI8042UpdateStatus();
        Value8 = I8042State.Status;
    }
    else if (Port == 0x60)
    {
        Value8 = RosvI8042PopByte();
    }
    else
    {
        return FALSE;
    }

    switch (Size)
    {
        case 1: Mask = 0xFF; break;
        case 2: Mask = 0xFFFF; break;
        default: Mask = 0xFFFFFFFF; break;
    }

    *Value = Value8 & Mask;
    return TRUE;
}

static BOOLEAN
RosvI8042Out(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    PROSV_CONSOLE_CONTEXT Console = (PROSV_CONSOLE_CONTEXT)Context;
    UCHAR Value8;

    UNREFERENCED_PARAMETER(Size);

    Value8 = (UCHAR)Value;

    if (Port == 0x64)
    {
        I8042State.Status |= I8042_STATUS_CMD;

        switch (Value8)
        {
            case 0x20: /* Read command byte */
                RosvI8042QueueByte(I8042State.CommandByte);
                break;

            case 0x60: /* Write command byte (next write to 0x60) */
                I8042State.PendingWrite = RosvI8042WriteCommandByte;
                break;

            case 0xAA: /* Controller self test */
                RosvI8042QueueByte(0x55);
                break;

            case 0xAB: /* Keyboard interface test */
            case 0xA9: /* Mouse interface test */
                RosvI8042QueueByte(0x00);
                break;

            case 0xAD: /* Disable keyboard port */
                I8042State.KeyboardEnabled = FALSE;
                break;

            case 0xAE: /* Enable keyboard port */
                I8042State.KeyboardEnabled = TRUE;
                break;

            case 0xA7: /* Disable aux port */
                I8042State.AuxEnabled = FALSE;
                break;

            case 0xA8: /* Enable aux port */
                I8042State.AuxEnabled = TRUE;
                break;

            case 0xD0: /* Read output port */
                RosvI8042QueueByte(I8042State.OutputPort);
                break;

            case 0xD1: /* Write output port (next write to 0x60) */
                I8042State.PendingWrite = RosvI8042WriteOutputPort;
                break;

            case 0xD4: /* Write aux device command (next write to 0x60) */
                I8042State.PendingWrite = RosvI8042WriteMouseCommand;
                break;

            case 0xFE: /* Pulse output port bit 0 low (CPU reset) */
                RosvRequestVmResetStop(Console, "i8042 cmd 0xFE", Value8);
                break;

            default:
                ROSV_DEBUG("i8042: unhandled command 0x%02X", Value8);
                break;
        }

        I8042State.Status &= (UCHAR)~I8042_STATUS_CMD;
        RosvI8042UpdateStatus();
        return TRUE;
    }

    if (Port == 0x60)
    {
        I8042State.Status &= (UCHAR)~I8042_STATUS_CMD;

        switch (I8042State.PendingWrite)
        {
            case RosvI8042WriteCommandByte:
                I8042State.CommandByte = Value8;
                I8042State.PendingWrite = RosvI8042WriteNone;
                break;

            case RosvI8042WriteOutputPort:
                I8042State.OutputPort = Value8;
                if ((Value8 & 0x01) == 0)
                {
                    /* Output port bit 0 low asserts CPU reset line. */
                    RosvRequestVmResetStop(Console, "i8042 output-port reset line", Value8);
                }
                I8042State.PendingWrite = RosvI8042WriteNone;
                break;

            case RosvI8042WriteMouseCommand:
                /* Basic aux ack */
                RosvI8042QueueByte(0xFA);
                I8042State.PendingWrite = RosvI8042WriteNone;
                break;

            case RosvI8042WriteKeyboardParam:
                RosvI8042QueueByte(0xFA);
                I8042State.PendingWrite = RosvI8042WriteNone;
                break;

            case RosvI8042WriteNone:
            default:
                /* Keyboard device command channel */
                switch (Value8)
                {
                    case 0xFF: /* Reset */
                        RosvI8042QueueByte(0xFA);
                        RosvI8042QueueByte(0xAA);
                        break;

                    case 0xF2: /* Identify */
                        RosvI8042QueueByte(0xFA);
                        RosvI8042QueueByte(0xAB);
                        RosvI8042QueueByte(0x83);
                        break;

                    case 0xEE: /* Echo */
                        RosvI8042QueueByte(0xEE);
                        break;

                    case 0xED: /* Set LEDs */
                    case 0xF0: /* Set scan code set */
                    case 0xF3: /* Set typematic rate/delay */
                        RosvI8042QueueByte(0xFA);
                        I8042State.PendingWrite = RosvI8042WriteKeyboardParam;
                        break;

                    case 0xF4: /* Enable scanning */
                    case 0xF5: /* Disable scanning */
                    case 0xF6: /* Set defaults */
                    default:
                        RosvI8042QueueByte(0xFA);
                        break;
                }
                break;
        }

        RosvI8042UpdateStatus();
        return TRUE;
    }

    return FALSE;
}

/* VGA register model (attribute controller, CRTC, DAC palette). */
typedef struct _ROSV_VGA_STATE {
    UCHAR AttributeIndex;
    BOOLEAN AttributeExpectData;
    UCHAR AttributeAddressMode;
    UCHAR AttributeRegs[0x20];

    UCHAR CrtcIndex;
    UCHAR CrtcRegs[0x20];

    UCHAR SequencerIndex;
    UCHAR SequencerRegs[0x08];

    UCHAR GraphicsIndex;
    UCHAR GraphicsRegs[0x10];

    UCHAR MiscOutput;
    UCHAR FeatureControl;
    UCHAR InputStatusFlip;

    UCHAR DacMask;
    UCHAR DacReadIndex;
    UCHAR DacWriteIndex;
    UCHAR DacReadSubIndex;
    UCHAR DacWriteSubIndex;
    UCHAR DacPalette[256][3];
} ROSV_VGA_STATE;

static ROSV_VGA_STATE VgaState;

static
VOID
RosvVgaReset(
    VOID)
{
    RtlZeroMemory(&VgaState, sizeof(VgaState));
    VgaState.DacMask = 0xFF;
}

static
UCHAR
RosvVgaReadInputStatus1(
    VOID)
{
    UCHAR Value = 0x01; /* Display enable */

    VgaState.InputStatusFlip ^= 0x08; /* Vertical retrace bit toggles */
    Value |= VgaState.InputStatusFlip;

    /* Reading 0x3DA resets AC index/data flip-flop. */
    VgaState.AttributeExpectData = FALSE;
    return Value;
}

static
BOOLEAN
RosvVgaInByte(
    _In_ USHORT Port,
    _Out_ PUCHAR Value)
{
    switch (Port)
    {
        case 0x3BA:
        case 0x3DA:
            *Value = RosvVgaReadInputStatus1();
            return TRUE;

        case 0x3C0:
            *Value = (UCHAR)(VgaState.AttributeIndex |
                             (VgaState.AttributeAddressMode ? 0x20 : 0x00));
            return TRUE;

        case 0x3C1:
            *Value = VgaState.AttributeRegs[VgaState.AttributeIndex & 0x1F];
            return TRUE;

        case 0x3C2:
            *Value = 0x00;
            return TRUE;

        case 0x3C4:
            *Value = VgaState.SequencerIndex;
            return TRUE;

        case 0x3C5:
            *Value = VgaState.SequencerRegs[VgaState.SequencerIndex & 0x07];
            return TRUE;

        case 0x3C6:
            *Value = VgaState.DacMask;
            return TRUE;

        case 0x3C7:
            *Value = VgaState.DacReadIndex;
            return TRUE;

        case 0x3C8:
            *Value = VgaState.DacWriteIndex;
            return TRUE;

        case 0x3C9:
            *Value = VgaState.DacPalette[VgaState.DacReadIndex][VgaState.DacReadSubIndex];
            VgaState.DacReadSubIndex = (UCHAR)((VgaState.DacReadSubIndex + 1) % 3);
            if (VgaState.DacReadSubIndex == 0)
                VgaState.DacReadIndex++;
            return TRUE;

        case 0x3CA:
            *Value = VgaState.FeatureControl;
            return TRUE;

        case 0x3CC:
            *Value = VgaState.MiscOutput;
            return TRUE;

        case 0x3CE:
            *Value = VgaState.GraphicsIndex;
            return TRUE;

        case 0x3CF:
            *Value = VgaState.GraphicsRegs[VgaState.GraphicsIndex & 0x0F];
            return TRUE;

        case 0x3B4:
        case 0x3D4:
            *Value = VgaState.CrtcIndex;
            return TRUE;

        case 0x3B5:
        case 0x3D5:
            *Value = VgaState.CrtcRegs[VgaState.CrtcIndex & 0x1F];
            return TRUE;

        default:
            *Value = 0xFF;
            return TRUE;
    }
}

static
BOOLEAN
RosvVgaOutByte(
    _In_ USHORT Port,
    _In_ UCHAR Value)
{
    switch (Port)
    {
        case 0x3C0:
            if (!VgaState.AttributeExpectData)
            {
                VgaState.AttributeIndex = Value & 0x1F;
                VgaState.AttributeAddressMode = (Value & 0x20) ? 1 : 0;
                VgaState.AttributeExpectData = TRUE;
            }
            else
            {
                VgaState.AttributeRegs[VgaState.AttributeIndex & 0x1F] = Value;
                VgaState.AttributeExpectData = FALSE;
            }
            return TRUE;

        case 0x3C2:
            VgaState.MiscOutput = Value;
            return TRUE;

        case 0x3C4:
            VgaState.SequencerIndex = Value & 0x07;
            return TRUE;

        case 0x3C5:
            VgaState.SequencerRegs[VgaState.SequencerIndex & 0x07] = Value;
            return TRUE;

        case 0x3C6:
            VgaState.DacMask = Value;
            return TRUE;

        case 0x3C7:
            VgaState.DacReadIndex = Value;
            VgaState.DacReadSubIndex = 0;
            return TRUE;

        case 0x3C8:
            VgaState.DacWriteIndex = Value;
            VgaState.DacWriteSubIndex = 0;
            return TRUE;

        case 0x3C9:
            VgaState.DacPalette[VgaState.DacWriteIndex][VgaState.DacWriteSubIndex] = (UCHAR)(Value & 0x3F);
            VgaState.DacWriteSubIndex = (UCHAR)((VgaState.DacWriteSubIndex + 1) % 3);
            if (VgaState.DacWriteSubIndex == 0)
                VgaState.DacWriteIndex++;
            return TRUE;

        case 0x3CE:
            VgaState.GraphicsIndex = Value & 0x0F;
            return TRUE;

        case 0x3CF:
            VgaState.GraphicsRegs[VgaState.GraphicsIndex & 0x0F] = Value;
            return TRUE;

        case 0x3B4:
        case 0x3D4:
            VgaState.CrtcIndex = Value & 0x1F;
            return TRUE;

        case 0x3B5:
        case 0x3D5:
            VgaState.CrtcRegs[VgaState.CrtcIndex & 0x1F] = Value;
            return TRUE;

        default:
            return TRUE;
    }
}

static
BOOLEAN
RosvVgaIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    ULONG Result = 0;
    UCHAR ByteValue = 0xFF;
    ULONG i;

    UNREFERENCED_PARAMETER(Context);

    for (i = 0; i < Size && i < sizeof(ULONG); i++)
    {
        if (!RosvVgaInByte((USHORT)(Port + i), &ByteValue))
            return FALSE;
        Result |= ((ULONG)ByteValue) << (i * 8);
    }

    *Value = Result;
    return TRUE;
}

static
BOOLEAN
RosvVgaOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Context);

    for (i = 0; i < Size && i < sizeof(ULONG); i++)
    {
        if (!RosvVgaOutByte((USHORT)(Port + i), (UCHAR)((Value >> (i * 8)) & 0xFF)))
            return FALSE;
    }

    return TRUE;
}

static BOOLEAN
RosvCmosIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Size);

    if (Port == 0x71)
    {
        *Value = RosvCmosReadRegister(CmosIndex);
    }
    else
    {
        /* Reading index register returns current index */
        *Value = CmosIndex;
    }
    return TRUE;
}

static BOOLEAN
RosvCmosOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Size);

    if (Port == 0x70)
    {
        CmosIndex = (UCHAR)Value;
    }
    /* Writes to 0x71 (data) are silently ignored */
    return TRUE;
}

/*
 * 8259 PIC emulation (master 0x20/0x21, slave 0xA0/0xA1).
 * Keep this intentionally minimal: track OCW1 (IMR) so probe/readback works.
 */
typedef struct _ROSV_PIC_STATE {
    UCHAR Imr;
    UCHAR Irr;
    UCHAR Isr;
    UCHAR VectorBase;
    UCHAR InitStep;
    BOOLEAN NeedIcw4;
    BOOLEAN ReadIsr;
} ROSV_PIC_STATE;

static ROSV_PIC_STATE PicMaster;
static ROSV_PIC_STATE PicSlave;

typedef struct _ROSV_PIT_CHANNEL
{
    USHORT ReloadValue;
    USHORT LatchValue;
    UCHAR AccessMode;
    UCHAR Mode;
    UCHAR Bcd;
    UCHAR WriteFlipFlop;
    UCHAR ReadFlipFlop;
    UCHAR WriteLowByte;
    BOOLEAN LatchPending;
    ULONG64 LoadQpc;
    ULONG64 DeliveredPeriods;
} ROSV_PIT_CHANNEL, *PROSV_PIT_CHANNEL;

static ROSV_PIT_CHANNEL PitChannels[3];
static ULONG64 PitQpcFrequency;
static UCHAR SysCtrlPortB;

static
BOOLEAN
RosvResolveIoApicIrqVector(
    _In_ PROSV_VM Vm,
    _In_ UCHAR Irq,
    _Out_ PUCHAR Vector);

static
BOOLEAN
RosvPitHasPendingIrq0(
    VOID);

static
BOOLEAN
RosvPitConsumeIrq0(
    VOID);

static BOOLEAN
RosvPicIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    ROSV_PIC_STATE *Pic = (Port >= 0xA0) ? &PicSlave : &PicMaster;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Size);

    if ((Port & 1) == 0)
        *Value = Pic->ReadIsr ? Pic->Isr : Pic->Irr;
    else
        *Value = Pic->Imr;

    return TRUE;
}

static BOOLEAN
RosvPicOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    ROSV_PIC_STATE *Pic = (Port >= 0xA0) ? &PicSlave : &PicMaster;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Size);

    if ((Port & 1) == 0)
    {
        UCHAR Command = (UCHAR)Value;
        ULONG i;

        /* ICW1: start initialization sequence. */
        if (Command & 0x10)
        {
            Pic->InitStep = 1;
            Pic->NeedIcw4 = (Command & 0x01) ? TRUE : FALSE;
            Pic->ReadIsr = FALSE;
            Pic->Irr = 0;
            Pic->Isr = 0;
            Pic->Imr = 0;
            return TRUE;
        }

        /* OCW3: select IRR/ISR register for subsequent command-port reads. */
        if ((Command & 0x18) == 0x08)
        {
            if (Command & 0x02)
                Pic->ReadIsr = (Command & 0x01) ? TRUE : FALSE;
            return TRUE;
        }

        /* OCW2 EOI (non-specific/specific): clear one in-service bit. */
        if (Command & 0x20)
        {
            for (i = 0; i < 8; i++)
            {
                UCHAR Bit = (UCHAR)(1u << i);
                if (Pic->Isr & Bit)
                {
                    Pic->Isr &= (UCHAR)~Bit;
                    break;
                }
            }
            return TRUE;
        }
    }
    else
    {
        UCHAR Data = (UCHAR)Value;

        if (Pic->InitStep == 1)
        {
            /* ICW2: base vector */
            Pic->VectorBase = (UCHAR)(Data & 0xF8);
            Pic->InitStep = 2;
            return TRUE;
        }

        if (Pic->InitStep == 2)
        {
            /* ICW3: cascade wiring */
            Pic->InitStep = Pic->NeedIcw4 ? 3 : 0;
            return TRUE;
        }

        if (Pic->InitStep == 3)
        {
            /* ICW4: mode flags */
            Pic->InitStep = 0;
            return TRUE;
        }

        /* OCW1: interrupt mask register */
        Pic->Imr = Data;
    }

    return TRUE;
}

BOOLEAN
RosvPicRequestIrq(
    _In_ UCHAR Irq,
    _Out_ PUCHAR Vector)
{
    ROSV_PIC_STATE *Pic;
    UCHAR Bit;
    UCHAR BaseVector;
    UCHAR IrqLine;

    if (Vector == NULL || Irq >= 16)
        return FALSE;

    if (Irq < 8)
    {
        Pic = &PicMaster;
        IrqLine = Irq;
    }
    else
    {
        /* Slave IRQ path (8-15) requires master cascade IRQ2 delivery. */
        if ((PicMaster.Imr & (1u << 2)) != 0 || (PicMaster.Isr & (1u << 2)) != 0)
            return FALSE;

        Pic = &PicSlave;
        IrqLine = (UCHAR)(Irq - 8);
    }

    Bit = (UCHAR)(1u << IrqLine);
    if ((Pic->Imr & Bit) != 0 || (Pic->Isr & Bit) != 0)
        return FALSE;

    /*
     * Present this IRQ as in-service so guest EOI flow can acknowledge it.
     * This avoids repeated synthetic injections while the same IRQ is active.
     */
    Pic->Irr |= Bit;
    Pic->Irr &= (UCHAR)~Bit;
    Pic->Isr |= Bit;

    if (Irq >= 8)
        PicMaster.Isr |= (1u << 2);

    BaseVector = Pic->VectorBase;
    if (BaseVector == 0)
        BaseVector = (Irq < 8) ? 0x20 : 0x28;

    *Vector = (UCHAR)(BaseVector + IrqLine);
    return TRUE;
}

VOID
RosvPicClearAllIsr(VOID)
{
    /*
     * Clear all in-service bits on both PICs.  Called on LAPIC EOI so that
     * device IRQs routed through the PIC fallback path (when the IOAPIC
     * route is unavailable) can be re-injected.  Without this, the ISR
     * bit stays set forever because the guest sends EOI to the LAPIC,
     * not the PIC.
     */
    PicMaster.Isr = 0;
    PicSlave.Isr = 0;
}

BOOLEAN
RosvPicRequestTimerIrq(
    _Out_ PUCHAR Vector)
{
    UCHAR Bit = 0x01; /* IRQ0 on master PIC */
    UCHAR BaseVector;

    if (Vector == NULL)
        return FALSE;

    /*
     * Do not gate the synthetic scheduler tick on PIC IMR state.
     * Early Linux boot can transiently leave IRQ0 masked while still
     * depending on forward timer progress; suppressing ticks here can
     * halt early boot progress.
     */

    if (PicMaster.Isr & Bit)
    {
        /*
         * The synthetic PIT source is level-less and periodic in ROSV.
         * If the guest did not EOI yet, force-rearm IRQ0 so scheduler
         * time continues to advance instead of wedging timer progress.
         */
        PicMaster.Isr &= (UCHAR)~Bit;
    }

    PicMaster.Irr |= Bit;
    PicMaster.Irr &= (UCHAR)~Bit;
    PicMaster.Isr |= Bit;

    BaseVector = (PicMaster.VectorBase != 0) ? PicMaster.VectorBase : 0x20;
    *Vector = BaseVector;
    return TRUE;
}

BOOLEAN
RosvInterruptRequestIrq(
    _In_ PROSV_VM Vm,
    _In_ UCHAR Irq,
    _Out_ PUCHAR Vector)
{
    /*
     * Try IOAPIC first (guest APIC mode).  Fall back to legacy 8259 PIC
     * when the IOAPIC route is unavailable (entry masked, uninitialized,
     * or guest booted with noapic/nolapic).  This mirrors the dual-path
     * strategy used by RosvInterruptRequestTimer.
     */
    if (RosvResolveIoApicIrqVector(Vm, Irq, Vector))
        return TRUE;

    return RosvPicRequestIrq(Irq, Vector);
}

static
BOOLEAN
RosvResolveIoApicIrqVector(
    _In_ PROSV_VM Vm,
    _In_ UCHAR Irq,
    _Out_ PUCHAR Vector)
{
    ULONG Intin;
    ULONG Low;
    ULONG DeliveryMode;
    UCHAR ResolvedVector;

    if (Vm == NULL || Vector == NULL || Irq >= 16)
        return FALSE;

    /*
     * In APIC mode, legacy IRQ0 (PIT) is delivered via IOAPIC INTIN2 on
     * PC-compatible chipsets. Keep this wiring explicit so timer ticks
     * follow the same route Linux programs in APIC boot paths.
     */
    Intin = (Irq == 0) ? 2 : Irq;
    if (Intin >= ROSV_IOAPIC_REDIRECTION_ENTRIES)
        return FALSE;

    /*
     * APIC delivery path:
     * Route ISA IRQ lines through guest-programmed IOAPIC redirection entries.
     * We inject only if the line is unmasked and uses a fixed-style delivery
     * mode that maps to an external interrupt vector.
     */
    Low = Vm->IoApic.RedirectionLow[Intin];
    if (Low & (1U << 16)) /* Masked */
        return FALSE;

    DeliveryMode = (Low >> 8) & 0x7;
    if (DeliveryMode != 0 && DeliveryMode != 1) /* fixed / lowest-priority */
        return FALSE;

    ResolvedVector = (UCHAR)(Low & 0xFF);
    if (ResolvedVector < 0x20)
        return FALSE;

    *Vector = ResolvedVector;
    return TRUE;
}

static
BOOLEAN
RosvLapicTimerRouteActive(
    _In_ PROSV_VM Vm)
{
    ROSV_ASSERT(Vm != NULL, "Vm must not be NULL");
    if (Vm == NULL)
        return FALSE;

    /*
     * Consider LAPIC timer delivery active only after the APIC is globally
     * enabled, software-enabled, and the timer itself is programmed/unmasked.
     * Until then, PIT bootstrap routing remains required for early boot time.
     */
    if ((Vm->Lapic.ApicBaseMsr & (1ULL << 11)) == 0)
        return FALSE;

    if ((Vm->Lapic.Svr & (1U << 8)) == 0)
        return FALSE;

    if (Vm->Lapic.TimerInitialCount == 0)
        return FALSE;

    if (Vm->Lapic.LvtTimer & (1U << 16))
        return FALSE;

    return TRUE;
}

BOOLEAN
RosvInterruptRequestTimer(
    _In_ PROSV_VM Vm,
    _Out_ PUCHAR Vector)
{
    static ULONG LegacyBootstrapCount;
    BOOLEAN IoApicTimerRoutable;

    IoApicTimerRoutable = RosvResolveIoApicIrqVector(Vm, 0, Vector);
    if (!IoApicTimerRoutable)
    {
        /*
         * Keep legacy PIT bootstrap alive while IOAPIC IRQ0 route is still
         * unresolved, unless LAPIC timer delivery is already active.
         */
        if (RosvLapicTimerRouteActive(Vm))
            return FALSE;

        /*
         * Early Linux bootstrap still expects legacy IRQ0 semantics. Route the
         * synthetic PIT edge through PIC IRQ0 so vectoring follows guest PIC
         * programming (normally 0x20 after remap), instead of forcing 0x30.
         */
        if (!RosvPicRequestTimerIrq(Vector))
            return FALSE;

        if (LegacyBootstrapCount < 8 || (LegacyBootstrapCount % 1024) == 0)
        {
            ROSV_TRACE("Timer bootstrap route: vector=0x%02X (lapic_svr=0x%08X ioapic_rte2_low=0x%08X)",
                       *Vector,
                       Vm->Lapic.Svr,
                       Vm->IoApic.RedirectionLow[2]);
        }
        LegacyBootstrapCount++;
    }

    return RosvPitConsumeIrq0();
}

BOOLEAN
RosvInterruptHasPendingTimer(
    _In_ PROSV_VM Vm)
{
    UCHAR Vector;

    if (!RosvResolveIoApicIrqVector(Vm, 0, &Vector) &&
        RosvLapicTimerRouteActive(Vm))
    {
        return FALSE;
    }

    return RosvPitHasPendingIrq0();
}

/*
 * PCI configuration space (Type 1).
 *  - 0xCF8..0xCFB: CONFIG_ADDRESS
 *  - 0xCFC..0xCFF: CONFIG_DATA
 * We currently expose one emulated device:
 *   Bus 0, Device 3, Function 0: RTL8139 NIC.
 */
static ULONG PciConfigAddr = 0;

#define ROSV_RTL8139_PCI_BUS        0
#define ROSV_RTL8139_PCI_DEVICE     3
#define ROSV_RTL8139_PCI_FUNCTION   0

#define ROSV_HOSTBR_PCI_BUS         0
#define ROSV_HOSTBR_PCI_DEVICE      0
#define ROSV_HOSTBR_PCI_FUNCTION    0
#define ROSV_HOSTBR_VENDOR_ID       0x8086
#define ROSV_HOSTBR_DEVICE_ID       0x1237
#define ROSV_HOSTBR_CLASS_REV       0x06000000UL

static USHORT HostBridgeCommand;

static
ULONG
RosvIoMaskForSize(
    _In_ UCHAR Size)
{
    switch (Size)
    {
        case 1: return 0xFF;
        case 2: return 0xFFFF;
        default: return 0xFFFFFFFF;
    }
}

static
ULONG
RosvIoSliceRead32(
    _In_ ULONG RegisterValue,
    _In_ USHORT BasePort,
    _In_ USHORT Port,
    _In_ UCHAR Size)
{
    ULONG Shift = (ULONG)(Port - BasePort) * 8;
    ULONG Mask = RosvIoMaskForSize(Size);
    return (RegisterValue >> Shift) & Mask;
}

static
ULONG
RosvIoSliceWrite32(
    _In_ ULONG RegisterValue,
    _In_ USHORT BasePort,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    ULONG Shift = (ULONG)(Port - BasePort) * 8;
    ULONG Mask = RosvIoMaskForSize(Size);
    ULONG FieldMask = Mask << Shift;
    return (RegisterValue & ~FieldMask) | ((Value & Mask) << Shift);
}

static
BOOLEAN
RosvPciDecodeAddress(
    _In_ ULONG ConfigAddress,
    _Out_ PUCHAR Bus,
    _Out_ PUCHAR Device,
    _Out_ PUCHAR Function,
    _Out_ PULONG RegisterOffset)
{
    if ((ConfigAddress & 0x80000000UL) == 0)
        return FALSE;

    *Bus = (UCHAR)((ConfigAddress >> 16) & 0xFF);
    *Device = (UCHAR)((ConfigAddress >> 11) & 0x1F);
    *Function = (UCHAR)((ConfigAddress >> 8) & 0x07);
    *RegisterOffset = ConfigAddress & 0xFC;
    return TRUE;
}

static
ULONG64
RosvPitReadQpc(
    VOID)
{
    LARGE_INTEGER Counter;

    if (PitQpcFrequency == 0)
    {
        LARGE_INTEGER Frequency;
        Counter = KeQueryPerformanceCounter(&Frequency);
        PitQpcFrequency = (ULONG64)Frequency.QuadPart;
        return (ULONG64)Counter.QuadPart;
    }

    Counter = KeQueryPerformanceCounter(NULL);
    return (ULONG64)Counter.QuadPart;
}

static
ULONG
RosvPitEffectiveReload(
    _In_ PROSV_PIT_CHANNEL Channel)
{
    return (Channel->ReloadValue == 0) ? 0x10000UL : (ULONG)Channel->ReloadValue;
}

static
ULONG64
RosvPitElapsedTicks(
    _In_ PROSV_PIT_CHANNEL Channel)
{
    ULONG64 NowQpc;
    ULONG64 DeltaQpc;

    if (PitQpcFrequency == 0)
        return 0;

    NowQpc = RosvPitReadQpc();
    if (NowQpc <= Channel->LoadQpc)
        return 0;

    DeltaQpc = NowQpc - Channel->LoadQpc;
    return (DeltaQpc * ROSV_PIT_FREQUENCY_HZ) / PitQpcFrequency;
}

static
USHORT
RosvPitCurrentCount(
    _In_ PROSV_PIT_CHANNEL Channel)
{
    ULONG Reload = RosvPitEffectiveReload(Channel);
    ULONG64 Elapsed = RosvPitElapsedTicks(Channel);
    ULONG Count;

    switch (Channel->Mode)
    {
        case 0: /* Interrupt on terminal count */
        case 1: /* Hardware retriggerable one-shot */
        case 4: /* Software triggered strobe */
        case 5: /* Hardware triggered strobe */
            if (Elapsed >= Reload)
                Count = 0;
            else
                Count = Reload - (ULONG)Elapsed;
            break;

        case 2: /* Rate generator */
        case 3: /* Square wave generator */
        default:
            Count = Reload - (ULONG)(Elapsed % Reload);
            break;
    }

    if (Count == 0x10000UL)
        return 0;
    return (USHORT)Count;
}

static
ULONG64
RosvPitExpiredPeriods(
    _In_ PROSV_PIT_CHANNEL Channel)
{
    ULONG Reload;
    ULONG64 Elapsed;

    Reload = RosvPitEffectiveReload(Channel);
    if (Reload == 0)
        return 0;

    Elapsed = RosvPitElapsedTicks(Channel);

    switch (Channel->Mode)
    {
        case 0: /* Interrupt on terminal count */
        case 1: /* Hardware retriggerable one-shot */
        case 4: /* Software triggered strobe */
        case 5: /* Hardware triggered strobe */
            return (Elapsed >= (ULONG64)Reload) ? 1ULL : 0ULL;

        case 2: /* Rate generator */
        case 3: /* Square wave generator */
        default:
            return Elapsed / (ULONG64)Reload;
    }
}

static
BOOLEAN
RosvPitHasPendingIrq0(
    VOID)
{
    PROSV_PIT_CHANNEL Channel0 = &PitChannels[0];
    ULONG64 ExpiredPeriods = RosvPitExpiredPeriods(Channel0);

    return (ExpiredPeriods > Channel0->DeliveredPeriods);
}

static
BOOLEAN
RosvPitConsumeIrq0(
    VOID)
{
    PROSV_PIT_CHANNEL Channel0 = &PitChannels[0];
    ULONG64 ExpiredPeriods = RosvPitExpiredPeriods(Channel0);

    if (ExpiredPeriods <= Channel0->DeliveredPeriods)
        return FALSE;

    /* Coalesce backlog to one pending edge per injection opportunity. */
    if (ExpiredPeriods > Channel0->DeliveredPeriods + 1)
        Channel0->DeliveredPeriods = ExpiredPeriods - 1;

    Channel0->DeliveredPeriods++;
    return TRUE;
}

static
BOOLEAN
RosvPitChannel2Out(
    VOID)
{
    PROSV_PIT_CHANNEL Channel2 = &PitChannels[2];
    ULONG Reload;
    ULONG64 Elapsed;

    if ((SysCtrlPortB & 0x01) == 0)
        return FALSE; /* Gate 2 disabled */

    Reload = RosvPitEffectiveReload(Channel2);
    if (Reload <= 1)
        return TRUE;

    Elapsed = RosvPitElapsedTicks(Channel2);

    if (Channel2->Mode == 3)
    {
        ULONG Phase = (ULONG)(Elapsed % Reload);
        return (Phase < (Reload / 2));
    }

    if (Channel2->Mode == 2)
    {
        return ((Elapsed % Reload) != (Reload - 1));
    }

    return (((Elapsed / Reload) & 1ULL) != 0);
}

static
VOID
RosvPitLoadCounter(
    _Inout_ PROSV_PIT_CHANNEL Channel)
{
    Channel->LoadQpc = RosvPitReadQpc();
    Channel->LatchPending = FALSE;
    Channel->ReadFlipFlop = 0;
    Channel->DeliveredPeriods = 0;
}

static
UCHAR
RosvPitReadChannel(
    _In_ UCHAR ChannelIndex)
{
    PROSV_PIT_CHANNEL Channel;
    USHORT Count;
    UCHAR ByteValue;

    if (ChannelIndex >= RTL_NUMBER_OF(PitChannels))
        return 0xFF;

    Channel = &PitChannels[ChannelIndex];
    Count = Channel->LatchPending ? Channel->LatchValue : RosvPitCurrentCount(Channel);

    switch (Channel->AccessMode)
    {
        case 1: /* LSB only */
            ByteValue = (UCHAR)(Count & 0xFF);
            Channel->LatchPending = FALSE;
            Channel->ReadFlipFlop = 0;
            break;

        case 2: /* MSB only */
            ByteValue = (UCHAR)((Count >> 8) & 0xFF);
            Channel->LatchPending = FALSE;
            Channel->ReadFlipFlop = 0;
            break;

        case 3: /* LSB then MSB */
        default:
            if (Channel->ReadFlipFlop == 0)
            {
                ByteValue = (UCHAR)(Count & 0xFF);
                Channel->ReadFlipFlop = 1;
            }
            else
            {
                ByteValue = (UCHAR)((Count >> 8) & 0xFF);
                Channel->ReadFlipFlop = 0;
                Channel->LatchPending = FALSE;
            }
            break;
    }

    return ByteValue;
}

static
VOID
RosvPitWriteChannel(
    _In_ UCHAR ChannelIndex,
    _In_ UCHAR Value)
{
    PROSV_PIT_CHANNEL Channel;

    if (ChannelIndex >= RTL_NUMBER_OF(PitChannels))
        return;

    Channel = &PitChannels[ChannelIndex];

    switch (Channel->AccessMode)
    {
        case 1: /* LSB only */
            Channel->ReloadValue = (Channel->ReloadValue & 0xFF00) | Value;
            Channel->WriteFlipFlop = 0;
            RosvPitLoadCounter(Channel);
            break;

        case 2: /* MSB only */
            Channel->ReloadValue = (Channel->ReloadValue & 0x00FF) | ((USHORT)Value << 8);
            Channel->WriteFlipFlop = 0;
            RosvPitLoadCounter(Channel);
            break;

        case 3: /* LSB then MSB */
        default:
            if (Channel->WriteFlipFlop == 0)
            {
                Channel->WriteLowByte = Value;
                Channel->WriteFlipFlop = 1;
            }
            else
            {
                Channel->ReloadValue = ((USHORT)Value << 8) | Channel->WriteLowByte;
                Channel->WriteFlipFlop = 0;
                RosvPitLoadCounter(Channel);
            }
            break;
    }
}

static
VOID
RosvPitControlWrite(
    _In_ UCHAR Value)
{
    UCHAR ChannelIndex = (Value >> 6) & 0x3;
    UCHAR AccessMode = (Value >> 4) & 0x3;
    UCHAR Mode = (Value >> 1) & 0x7;
    PROSV_PIT_CHANNEL Channel;

    if (ChannelIndex >= RTL_NUMBER_OF(PitChannels))
    {
        /* Read-back command (8254 extension) is ignored by this minimal model. */
        return;
    }

    Channel = &PitChannels[ChannelIndex];
    if (Mode >= 6)
        Mode -= 4; /* 6->2, 7->3 aliases */

    if (AccessMode == 0)
    {
        /* Counter latch command */
        Channel->LatchValue = RosvPitCurrentCount(Channel);
        Channel->LatchPending = TRUE;
        Channel->ReadFlipFlop = 0;
        return;
    }

    Channel->AccessMode = AccessMode;
    Channel->Mode = Mode;
    Channel->Bcd = Value & 1;
    Channel->WriteFlipFlop = 0;
    Channel->ReadFlipFlop = 0;
    Channel->LatchPending = FALSE;
    Channel->DeliveredPeriods = 0;
}

static BOOLEAN
RosvPitIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    ULONG Result = 0;
    ULONG i;

    UNREFERENCED_PARAMETER(Context);

    if (Port >= 0x40 && Port <= 0x42)
    {
        for (i = 0; i < Size && i < sizeof(ULONG); i++)
        {
            Result |= ((ULONG)RosvPitReadChannel((UCHAR)(Port - 0x40))) << (i * 8);
        }
        *Value = Result;
        return TRUE;
    }

    if (Port == 0x43)
    {
        *Value = 0;
        return TRUE;
    }

    *Value = RosvIoMaskForSize(Size);
    return TRUE;
}

static BOOLEAN
RosvPitOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    ULONG i;

    UNREFERENCED_PARAMETER(Context);

    if (Port >= 0x40 && Port <= 0x42)
    {
        for (i = 0; i < Size && i < sizeof(ULONG); i++)
        {
            RosvPitWriteChannel((UCHAR)(Port - 0x40), (UCHAR)((Value >> (i * 8)) & 0xFF));
        }
        return TRUE;
    }

    if (Port == 0x43)
    {
        RosvPitControlWrite((UCHAR)Value);
        return TRUE;
    }

    return TRUE;
}

static BOOLEAN
RosvPciConfigIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    PROSV_CONSOLE_CONTEXT Console = (PROSV_CONSOLE_CONTEXT)Context;
    UCHAR Bus;
    UCHAR Device;
    UCHAR Function;
    ULONG RegisterOffset;
    ULONG RegisterValue;

    if (Port >= 0xCF8 && Port <= 0xCFB)
    {
        *Value = RosvIoSliceRead32(PciConfigAddr, 0xCF8, Port, Size);
    }
    else if (Port >= 0xCFC && Port <= 0xCFF &&
             Console != NULL &&
             RosvPciDecodeAddress(PciConfigAddr, &Bus, &Device, &Function, &RegisterOffset))
    {
        if (Bus == ROSV_HOSTBR_PCI_BUS &&
            Device == ROSV_HOSTBR_PCI_DEVICE &&
            Function == ROSV_HOSTBR_PCI_FUNCTION)
        {
            switch (RegisterOffset & ~3U)
            {
                case 0x00:
                    RegisterValue =
                        ((ULONG)ROSV_HOSTBR_DEVICE_ID << 16) | ROSV_HOSTBR_VENDOR_ID;
                    break;
                case 0x04:
                    RegisterValue = HostBridgeCommand;
                    break;
                case 0x08:
                    RegisterValue = ROSV_HOSTBR_CLASS_REV;
                    break;
                default:
                    RegisterValue = 0;
                    break;
            }

            *Value = RosvIoSliceRead32(RegisterValue, 0xCFC, Port, Size);
        }
        else if (Bus == ROSV_RTL8139_PCI_BUS &&
                 Device == ROSV_RTL8139_PCI_DEVICE &&
                 Function == ROSV_RTL8139_PCI_FUNCTION)
        {
            RegisterValue = RosvRtl8139PciConfigRead(&Console->Rtl8139, RegisterOffset);
            *Value = RosvIoSliceRead32(RegisterValue, 0xCFC, Port, Size);
        }
        else
        {
            *Value = RosvIoMaskForSize(Size);
        }
    }
    else
    {
        *Value = RosvIoMaskForSize(Size);
    }

    return TRUE;
}

static BOOLEAN
RosvPciConfigOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    PROSV_CONSOLE_CONTEXT Console = (PROSV_CONSOLE_CONTEXT)Context;
    UCHAR Bus;
    UCHAR Device;
    UCHAR Function;
    ULONG RegisterOffset;
    ULONG OldValue;
    ULONG NewValue;

    if (Port >= 0xCF8 && Port <= 0xCFB)
    {
        PciConfigAddr = RosvIoSliceWrite32(PciConfigAddr, 0xCF8, Port, Size, Value);
    }
    else if (Port >= 0xCFC && Port <= 0xCFF &&
             Console != NULL &&
             RosvPciDecodeAddress(PciConfigAddr, &Bus, &Device, &Function, &RegisterOffset))
    {
        if (Bus == ROSV_HOSTBR_PCI_BUS &&
            Device == ROSV_HOSTBR_PCI_DEVICE &&
            Function == ROSV_HOSTBR_PCI_FUNCTION)
        {
            switch (RegisterOffset & ~3U)
            {
                case 0x04:
                    OldValue = HostBridgeCommand;
                    break;
                default:
                    OldValue = 0;
                    break;
            }

            NewValue = RosvIoSliceWrite32(OldValue, 0xCFC, Port, Size, Value);
            if ((RegisterOffset & ~3U) == 0x04)
                HostBridgeCommand = (USHORT)(NewValue & 0xFFFF);
        }
        else if (Bus == ROSV_RTL8139_PCI_BUS &&
                 Device == ROSV_RTL8139_PCI_DEVICE &&
                 Function == ROSV_RTL8139_PCI_FUNCTION)
        {
            OldValue = RosvRtl8139PciConfigRead(&Console->Rtl8139, RegisterOffset);
            NewValue = RosvIoSliceWrite32(OldValue, 0xCFC, Port, Size, Value);
            RosvRtl8139PciConfigWrite(&Console->Rtl8139, RegisterOffset, NewValue);
        }
    }
    return TRUE;
}

/*
 * System control port B (0x61).
 * Return 0x00 - timer/speaker control, all off.
 */
static BOOLEAN
RosvSysCtrlIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    UCHAR Value8;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Port);

    Value8 = SysCtrlPortB & 0x0F;

    /* Bit 4 is DRAM refresh clock (legacy). Toggle it to satisfy polling code. */
    if ((RosvPitElapsedTicks(&PitChannels[1]) & 1ULL) != 0)
        Value8 |= 0x10;

    /* Bit 5 reflects PIT channel 2 OUT. */
    if (RosvPitChannel2Out())
        Value8 |= 0x20;

    *Value = Value8 & RosvIoMaskForSize(Size);
    return TRUE;
}

static BOOLEAN
RosvSysCtrlOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    UCHAR OldValue;
    UCHAR NewValue;

    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Size);

    OldValue = SysCtrlPortB;
    NewValue = (UCHAR)Value;
    SysCtrlPortB = (SysCtrlPortB & ~0x03) | (NewValue & 0x03);

    /* Restart channel 2 timing when gate2 transitions low->high. */
    if (((OldValue & 0x01) == 0) && ((SysCtrlPortB & 0x01) != 0))
    {
        PitChannels[2].LoadQpc = RosvPitReadQpc();
    }

    return TRUE;
}

/*
 * Reset control register (0xCF9).
 * If the guest writes here, it wants to reset/halt.
 */
static BOOLEAN
RosvResetIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Size);

    *Value = 0x00;
    return TRUE;
}

static BOOLEAN
RosvResetOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    PROSV_CONSOLE_CONTEXT Console = (PROSV_CONSOLE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Size);

    if ((Value & 0x04) != 0)
    {
        /*
         * 0xCF9 reset control:
         * bit 2 (RST_CPU) requests processor reset.
         * Treat this as a machine reset request for now.
         */
        RosvRequestVmResetStop(Console, "port 0xCF9", (UCHAR)Value);
    }
    return TRUE;
}

/*
 * DMA controller page registers and misc legacy ports.
 * 0x00-0x0F: DMA controller 1, 0xC0-0xDF: DMA controller 2
 * 0x40-0x43: PIT timer (8254)
 * These are accessed during early Linux boot but we just sink/source them.
 */
static BOOLEAN
RosvLegacyIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Size);

    *Value = 0x00;
    return TRUE;
}

static BOOLEAN
RosvLegacyOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Value);

    return TRUE;
}

/* ---- I/O dispatch table management -------------------------------------- */

NTSTATUS
RosvIoInitialize(
    _Inout_ PROSV_CONSOLE_CONTEXT Console)
{
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Now;
    ULONG i;

    Console->IoHandlerCount = 0;
    RtlZeroMemory(Console->IoHandlers, sizeof(Console->IoHandlers));

    /* Initialize 8259 PIC state */
    RtlZeroMemory(&PicMaster, sizeof(PicMaster));
    PicMaster.Imr = 0xFF; /* All IRQs masked initially */
    PicMaster.ReadIsr = FALSE;
    RtlZeroMemory(&PicSlave, sizeof(PicSlave));
    PicSlave.Imr = 0xFF;
    PicSlave.ReadIsr = FALSE;

    /* PCI config address register reset value */
    PciConfigAddr = 0;
    HostBridgeCommand = 0;

    /* Initialize RTL8139 model before exposing PCI config space. */
    RosvRtl8139Initialize(&Console->Rtl8139);

    /* Initialize i8042 keyboard controller state. */
    RtlZeroMemory(&I8042State, sizeof(I8042State));
    I8042State.CommandByte = 0x47;  /* Translation + keyboard IRQ enabled by default */
    I8042State.OutputPort = 0x03;   /* A20 enabled, reset line deasserted */
    I8042State.KeyboardEnabled = TRUE;
    I8042State.AuxEnabled = FALSE;
    RosvI8042UpdateStatus();
    RosvVgaReset();

    /* Initialize PIT channels and PPI port B state. */
    RtlZeroMemory(PitChannels, sizeof(PitChannels));
    Now = KeQueryPerformanceCounter(&Frequency);
    PitQpcFrequency = (ULONG64)Frequency.QuadPart;
    for (i = 0; i < RTL_NUMBER_OF(PitChannels); i++)
    {
        PitChannels[i].AccessMode = 3; /* LSB then MSB */
        PitChannels[i].Mode = 3;       /* Square wave */
        PitChannels[i].LoadQpc = (ULONG64)Now.QuadPart;
    }
    SysCtrlPortB = 0;

    ROSV_TRACE("RosvIoInitialize: registering common I/O handlers");

    /* POST code port */
    RosvIoRegisterHandler(Console, 0x80, 1,
                          RosvPostCodeIn, RosvPostCodeOut, NULL);

    /* CMOS RTC */
    RosvIoRegisterHandler(Console, 0x70, 2,
                          RosvCmosIn, RosvCmosOut, NULL);

    /* PIC master */
    RosvIoRegisterHandler(Console, 0x20, 2,
                          RosvPicIn, RosvPicOut, NULL);

    /* PIC slave */
    RosvIoRegisterHandler(Console, 0xA0, 2,
                          RosvPicIn, RosvPicOut, NULL);

    /* System control port B */
    RosvIoRegisterHandler(Console, 0x61, 1,
                          RosvSysCtrlIn, RosvSysCtrlOut, NULL);

    /* i8042 keyboard controller (data/status-command ports) */
    RosvIoRegisterHandler(Console, 0x60, 1,
                          RosvI8042In, RosvI8042Out, Console);
    RosvIoRegisterHandler(Console, 0x64, 1,
                          RosvI8042In, RosvI8042Out, Console);

    /* VGA CRTC/attribute/DAC ports (0x3B0-0x3DF) */
    RosvIoRegisterHandler(Console, 0x3B0, 0x30,
                          RosvVgaIn, RosvVgaOut, NULL);

    /* PCI configuration space (Type 1 address register 0xCF8..0xCFB) */
    RosvIoRegisterHandler(Console, 0xCF8, 4,
                          RosvPciConfigIn, RosvPciConfigOut, Console);

    /* PCI configuration space (Type 1 data window) */
    RosvIoRegisterHandler(Console, 0xCFC, 4,
                          RosvPciConfigIn, RosvPciConfigOut, Console);

    /* RTL8139 I/O BAR window */
    RosvIoRegisterHandler(Console,
                          ROSV_RTL8139_IO_BASE,
                          ROSV_RTL8139_IO_SIZE,
                          RosvRtl8139IoIn,
                          RosvRtl8139IoOut,
                          &Console->Rtl8139);

    /*
     * Reset control (0xCF9) overlaps PCI CONFIG_ADDRESS byte lane.
     * Keep it after PCI registration so config probing on 0xCF8..0xCFB wins.
     */
    RosvIoRegisterHandler(Console, 0xCF9, 1,
                          RosvResetIn, RosvResetOut, Console);

    /* PIT timer (8254) - 0x40-0x43 */
    RosvIoRegisterHandler(Console, 0x40, 4,
                          RosvPitIn, RosvPitOut, NULL);

    /* DMA controller 1 - 0x00-0x0F */
    RosvIoRegisterHandler(Console, 0x00, 16,
                          RosvLegacyIn, RosvLegacyOut, NULL);

    /* DMA page registers - 0x80-0x8F (overlaps POST, POST registered first wins) */
    RosvIoRegisterHandler(Console, 0x81, 15,
                          RosvLegacyIn, RosvLegacyOut, NULL);

    /* DMA controller 2 - 0xC0-0xDF */
    RosvIoRegisterHandler(Console, 0xC0, 32,
                          RosvLegacyIn, RosvLegacyOut, NULL);

    ROSV_TRACE("RosvIoInitialize: %u I/O handlers registered", Console->IoHandlerCount);
    return STATUS_SUCCESS;
}

BOOLEAN
RosvIoRegisterHandler(
    _Inout_ PROSV_CONSOLE_CONTEXT Console,
    _In_ USHORT PortBase,
    _In_ USHORT PortCount,
    _In_ PROSV_IO_IN_HANDLER InHandler,
    _In_ PROSV_IO_OUT_HANDLER OutHandler,
    _In_ PVOID Context)
{
    PROSV_IO_HANDLER Handler;

    if (Console->IoHandlerCount >= ROSV_MAX_IO_HANDLERS)
    {
        ROSV_ERR("RosvIoRegisterHandler: handler table full (%u/%u)",
                 Console->IoHandlerCount, ROSV_MAX_IO_HANDLERS);
        return FALSE;
    }

    Handler = &Console->IoHandlers[Console->IoHandlerCount];
    Handler->PortBase = PortBase;
    Handler->PortCount = PortCount;
    Handler->InHandler = InHandler;
    Handler->OutHandler = OutHandler;
    Handler->Context = Context;
    Console->IoHandlerCount++;

    ROSV_TRACE("I/O handler registered: ports 0x%X-0x%X (%u total)",
               PortBase, PortBase + PortCount - 1, Console->IoHandlerCount);
    return TRUE;
}

BOOLEAN
RosvIoHandleIn(
    _In_ PROSV_CONSOLE_CONTEXT Console,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    ULONG i;

    for (i = 0; i < Console->IoHandlerCount; i++)
    {
        PROSV_IO_HANDLER H = &Console->IoHandlers[i];
        if (Port >= H->PortBase && Port < H->PortBase + H->PortCount)
        {
            if (H->InHandler)
                return H->InHandler(H->Context, Port, Size, Value);
        }
    }

    if (RosvIoShouldLogUnhandled(Port, FALSE))
        ROSV_WARN("Unhandled I/O IN: port=0x%X size=%u", Port, Size);
    *Value = 0xFFFFFFFF;
    return FALSE;
}

BOOLEAN
RosvIoHandleOut(
    _In_ PROSV_CONSOLE_CONTEXT Console,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    ULONG i;

    for (i = 0; i < Console->IoHandlerCount; i++)
    {
        PROSV_IO_HANDLER H = &Console->IoHandlers[i];
        if (Port >= H->PortBase && Port < H->PortBase + H->PortCount)
        {
            if (H->OutHandler)
                return H->OutHandler(H->Context, Port, Size, Value);
        }
    }

    if (RosvIoShouldLogUnhandled(Port, TRUE))
        ROSV_WARN("Unhandled I/O OUT: port=0x%X size=%u value=0x%X", Port, Size, Value);
    return FALSE;
}
