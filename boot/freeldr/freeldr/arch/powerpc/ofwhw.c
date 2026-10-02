/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Machine detection, time and hardware tree for Open Firmware machines
 */

#include <freeldr.h>
#include <reactos/drivers/bootvid/framebuf.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

OFW_MACHINE_INFO OfwMachine;

/* PReP (IBM 40P and compatibles): ISA/PCI I/O, PCI memory and the bus
 * address of physical page 0 as seen by PCI bus masters. */
#define PREP_ISA_IO_BASE      0x80000000UL
#define PREP_PCI_MEMORY_BASE  0xC0000000UL
#define PREP_PCI_DMA_OFFSET   0x80000000UL
#define PREP_COM1_PORT        0x3F8

static
BOOLEAN
OfwStringStartsWith(_In_ PCSTR String, _In_ ULONG Length, _In_z_ PCSTR Prefix)
{
    SIZE_T PrefixLength = strlen(Prefix);

    return (Length >= PrefixLength) && (memcmp(String, Prefix, PrefixLength) == 0);
}

VOID
OfwDetectMachine(VOID)
{
    OFW_HANDLE Root, Cpus, Cpu;
    CHAR Buffer[64];
    LONG Length;
    ULONG Value;

    RtlZeroMemory(&OfwMachine, sizeof(OfwMachine));
    OfwMachine.MachineType = PPC_MACHINE_UNKNOWN;
    OfwMachine.DcacheLineSize = 32;
    OfwMachine.IcacheLineSize = 32;

    Root = OfwFindDevice("/");
    Length = OfwGetProp(Root, "model", OfwMachine.Model, sizeof(OfwMachine.Model) - 1);
    if (Length < 0)
        Length = 0;
    OfwMachine.Model[Length] = ANSI_NULL;

    /* OpenBIOS names the 40P root "bootrom" and exposes the PReP PCI host
     * bridge at /pci@80000000; that bridge is what identifies PReP. */
    if (OfwFindDevice("/pci@80000000") != OFW_INVALID)
    {
        OfwMachine.MachineType = PPC_MACHINE_PREP;
        OfwMachine.IsaIoPhysicalBase = PREP_ISA_IO_BASE;
        OfwMachine.PciMemoryPhysicalBase = PREP_PCI_MEMORY_BASE;
        OfwMachine.PciDmaOffset = PREP_PCI_DMA_OFFSET;
        OfwMachine.ConsolePort = PREP_COM1_PORT;
        if (OfwMachine.Model[0] == ANSI_NULL)
            RtlStringCbCopyA(OfwMachine.Model, sizeof(OfwMachine.Model), "PReP");
    }

    /* The first CPU node carries the cache geometry and the time base. */
    Cpus = OfwFindDevice("/cpus");
    for (Cpu = (Cpus != OFW_INVALID) ? OfwChild(Cpus) : OFW_INVALID; Cpu != OFW_INVALID; Cpu = OfwPeer(Cpu))
    {
        Length = OfwGetProp(Cpu, "device_type", Buffer, sizeof(Buffer) - 1);
        if (Length <= 0 || !OfwStringStartsWith(Buffer, (ULONG)Length, "cpu"))
            continue;

        if (OfwGetPropCell(Cpu, "timebase-frequency", &Value))
            OfwMachine.TimebaseFrequency = Value;
        if (OfwGetPropCell(Cpu, "clock-frequency", &Value))
            OfwMachine.ProcessorFrequency = Value;
        if (OfwGetPropCell(Cpu, "bus-frequency", &Value))
            OfwMachine.BusFrequency = Value;
        if (OfwGetPropCell(Cpu, "d-cache-block-size", &Value) && Value != 0)
            OfwMachine.DcacheLineSize = Value;
        if (OfwGetPropCell(Cpu, "i-cache-block-size", &Value) && Value != 0)
            OfwMachine.IcacheLineSize = Value;
        if (OfwGetPropCell(Cpu, "d-cache-size", &Value))
            OfwMachine.DcacheSize = Value;
        if (OfwGetPropCell(Cpu, "i-cache-size", &Value))
            OfwMachine.IcacheSize = Value;
        break;
    }

    if (OfwMachine.TimebaseFrequency == 0)
        OfwMachine.TimebaseFrequency = 25000000;

    TRACE("OFW machine '%s' type %lu, timebase %lu Hz, cache lines D%lu I%lu\n", OfwMachine.Model, OfwMachine.MachineType, OfwMachine.TimebaseFrequency, OfwMachine.DcacheLineSize, OfwMachine.IcacheLineSize);
}

ULONG
PpcGetIoBase(VOID)
{
    return OfwMachine.IsaIoPhysicalBase;
}

static
ULONGLONG
PpcReadTimebase(VOID)
{
    ULONG Upper, Lower, Check;

    do
    {
        __asm__ __volatile__("mftbu %0" : "=r"(Upper));
        __asm__ __volatile__("mftb %0" : "=r"(Lower));
        __asm__ __volatile__("mftbu %0" : "=r"(Check));
    } while (Upper != Check);

    return ((ULONGLONG)Upper << 32) | Lower;
}

VOID
StallExecutionProcessor(ULONG Microseconds)
{
    ULONGLONG Start = PpcReadTimebase();
    ULONGLONG Ticks = (ULONGLONG)Microseconds * OfwMachine.TimebaseFrequency / 1000000;

    while (PpcReadTimebase() - Start < Ticks)
        NOTHING;
}

VOID
PpcFlushCacheRange(_In_ PVOID Address, _In_ SIZE_T Length)
{
    ULONG_PTR Line = OfwMachine.DcacheLineSize ? OfwMachine.DcacheLineSize : 32;
    ULONG_PTR Start = (ULONG_PTR)Address & ~(Line - 1);
    ULONG_PTR End = (ULONG_PTR)Address + Length;

    for (; Start < End; Start += Line)
        __asm__ __volatile__("dcbst 0, %0" :: "r"(Start) : "memory");
    __asm__ __volatile__("sync" ::: "memory");

    Start = (ULONG_PTR)Address & ~(Line - 1);
    for (; Start < End; Start += Line)
        __asm__ __volatile__("icbi 0, %0" :: "r"(Start) : "memory");
    __asm__ __volatile__("sync; isync" ::: "memory");
}

/* The MC146818 RTC answers at ISA ports 0x70/0x71; Open Firmware maps the
 * PReP I/O window 1:1 while it owns the MMU. */
static
UCHAR
OfwCmosRead(_In_ UCHAR Register)
{
    volatile UCHAR *Io = (volatile UCHAR *)(ULONG_PTR)OfwMachine.IsaIoPhysicalBase;

    Io[0x70] = Register;
    __asm__ __volatile__("eieio" ::: "memory");
    return Io[0x71];
}

static
USHORT
OfwBcd(_In_ UCHAR Value, _In_ BOOLEAN Binary)
{
    return Binary ? Value : (USHORT)((Value >> 4) * 10 + (Value & 0x0F));
}

TIMEINFO *
OfwGetTime(VOID)
{
    static TIMEINFO TimeInfo;
    UCHAR StatusB;
    BOOLEAN Binary;

    if (OfwMachine.MachineType != PPC_MACHINE_PREP)
    {
        TimeInfo.Year = 2026;
        TimeInfo.Month = 1;
        TimeInfo.Day = 1;
        TimeInfo.Hour = TimeInfo.Minute = TimeInfo.Second = 0;
        return &TimeInfo;
    }

    /* Wait out an update in progress. */
    while (OfwCmosRead(0x0A) & 0x80)
        NOTHING;

    StatusB = OfwCmosRead(0x0B);
    Binary = (StatusB & 0x04) != 0;
    TimeInfo.Second = OfwBcd(OfwCmosRead(0x00), Binary);
    TimeInfo.Minute = OfwBcd(OfwCmosRead(0x02), Binary);
    TimeInfo.Hour = OfwBcd(OfwCmosRead(0x04), Binary);
    TimeInfo.Day = OfwBcd(OfwCmosRead(0x07), Binary);
    TimeInfo.Month = OfwBcd(OfwCmosRead(0x08), Binary);
    TimeInfo.Year = OfwBcd(OfwCmosRead(0x09), Binary);
    TimeInfo.Year += (TimeInfo.Year < 70) ? 2000 : 1900;
    return &TimeInfo;
}

ULONG
OfwGetRelativeTime(VOID)
{
    return OfwMilliseconds() / 1000;
}

VOID
OfwHwIdle(VOID)
{
    StallExecutionProcessor(1000);
}

BOOLEAN
IsAcpiPresent(VOID)
{
    return FALSE;
}

static
OFW_HANDLE
OfwFindDisplay(_In_ OFW_HANDLE Node)
{
    OFW_HANDLE Child, Display;
    CHAR Type[16];
    LONG Length;

    Length = OfwGetProp(Node, "device_type", Type, sizeof(Type));
    if (Length >= 7 && memcmp(Type, "display", 7) == 0)
        return Node;

    for (Child = OfwChild(Node); Child != OFW_INVALID; Child = OfwPeer(Child))
    {
        Display = OfwFindDisplay(Child);
        if (Display != OFW_INVALID)
            return Display;
    }
    return OFW_INVALID;
}

static
VOID
OfwDetectPrepInput(_In_ PCONFIGURATION_COMPONENT_DATA BusKey)
{
    PCM_PARTIAL_RESOURCE_LIST Resources, PeripheralResources;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCONFIGURATION_COMPONENT_DATA ControllerKey, PeripheralKey;
    PCM_KEYBOARD_DEVICE_DATA Keyboard;
    ULONG Device, Index, Count, Size;

    /* The PReP board's 8042 is an ISA device, regardless of the CPU ISA.
     * Firmware owns it until handoff; i8042prt will reset both PS/2 ports. */
    if (OfwMachine.MachineType != PPC_MACHINE_PREP ||
        OfwFindDevice("keyboard") == OFW_INVALID)
        return;

    for (Device = 0; Device < 2; Device++)
    {
        Count = Device == 0 ? 3 : 1;
        Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors) +
               Count * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
        Resources = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
        if (!Resources)
            return;
        RtlZeroMemory(Resources, Size);
        Resources->Version = Resources->Revision = 1;
        Resources->Count = Count;
        Descriptor = &Resources->PartialDescriptors[0];
        Descriptor->Type = CmResourceTypeInterrupt;
        Descriptor->Flags = CM_RESOURCE_INTERRUPT_LATCHED;
        Descriptor->u.Interrupt.Level = Descriptor->u.Interrupt.Vector = Device == 0 ? 1 : 12;
        Descriptor->u.Interrupt.Affinity = (KAFFINITY)-1;
        for (Index = 1; Index < Count; Index++)
        {
            Descriptor = &Resources->PartialDescriptors[Index];
            Descriptor->Type = CmResourceTypePort;
            Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
            Descriptor->Flags = CM_RESOURCE_PORT_IO;
            Descriptor->u.Port.Start.QuadPart = Index == 1 ? 0x60 : 0x64;
            Descriptor->u.Port.Length = 1;
        }
        FldrCreateComponentKey(BusKey, ControllerClass,
                               Device == 0 ? KeyboardController : PointerController,
                               Input, 0, 0xFFFFFFFF, NULL, Resources, Size, &ControllerKey);

        Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors);
        if (Device == 0)
            Size += sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) + sizeof(*Keyboard);
        PeripheralResources = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
        if (!PeripheralResources)
            return;
        RtlZeroMemory(PeripheralResources, Size);
        PeripheralResources->Version = PeripheralResources->Revision = 1;
        if (Device == 0)
        {
            PeripheralResources->Count = 1;
            Descriptor = &PeripheralResources->PartialDescriptors[0];
            Descriptor->Type = CmResourceTypeDeviceSpecific;
            Descriptor->u.DeviceSpecificData.DataSize = sizeof(*Keyboard);
            Keyboard = (PCM_KEYBOARD_DEVICE_DATA)(Descriptor + 1);
            Keyboard->Version = Keyboard->Revision = 1;
            Keyboard->Type = 4;
        }
        FldrCreateComponentKey(ControllerKey, PeripheralClass,
                               Device == 0 ? KeyboardPeripheral : PointerPeripheral,
                               Input, 0, 0xFFFFFFFF,
                               Device == 0 ? "PCAT_ENHANCED" : "MICROSOFT PS2 MOUSE",
                               PeripheralResources, Size, &PeripheralKey);
    }
}

/* QEMU standard VGA exposes a pixel-byte-order register in PCI BAR2. The
 * firmware renders big-endian pixels; the NT display stack uses X8R8G8B8.
 * This is a device property, not a consequence of the kernel pointer width. */
static
BOOLEAN
OfwSetDisplayByteOrder(_In_ OFW_HANDLE Display)
{
    ULONG Vendor, Device, Revision, Registers[6 * 5], Cells[7];
    LONG Length;
    ULONG Index, Base;
    OFW_HANDLE Mmu;
    volatile ULONG *Control;

    if (OfwMachine.MachineType != PPC_MACHINE_PREP ||
        !OfwGetPropCell(Display, "vendor-id", &Vendor) || Vendor != 0x1234 ||
        !OfwGetPropCell(Display, "device-id", &Device) || Device != 0x1111 ||
        !OfwGetPropCell(Display, "revision-id", &Revision) || Revision < 2)
        return FALSE;
    Length = OfwGetProp(Display, "assigned-addresses", Registers, sizeof(Registers));
    if (Length <= 0 || Length > sizeof(Registers))
        return FALSE;
    for (Index = 0; Index + 5 <= (ULONG)Length / sizeof(ULONG); Index += 5)
    {
        if ((OfwCellToHost(Registers[Index]) & 0xFF) == 0x18 &&
            OfwCellToHost(Registers[Index + 4]) >= 0x608)
            break;
    }
    if (Index + 5 > (ULONG)Length / sizeof(ULONG))
        return FALSE;
    /* OpenBIOS maps PReP PCI MMIO 1:1 when it opens the display. Validate
     * that existing mapping; mapping the same BAR twice can fail the OF
     * virtual-address claim and must never be mistaken for a usable VA. */
    Base = OfwCellToHost(Registers[Index + 2]);
    if (OfwCellToHost(Registers[Index + 1]) != 0 || Base >= 0x3F000000 ||
        !OfwGetPropCell(OfwFindDevice("/chosen"), "mmu", &Mmu))
        return FALSE;
    Base += OfwMachine.PciMemoryPhysicalBase;
    Cells[0] = (ULONG)(ULONG_PTR)"translate";
    Cells[1] = Mmu;
    Cells[2] = Base;
    if (OfwCall("call-method", 3, 4, Cells) != 0 || Cells[3] != 0 ||
        Cells[4] == 0 || Cells[6] != Base)
        return FALSE;
    Control = (volatile ULONG *)(ULONG_PTR)(Base + 0x604);
    *Control = 0x1E1E1E1E;
    __asm__ __volatile__("eieio" ::: "memory");
    return *Control == 0x1E1E1E1E;
}

static
VOID
OfwDetectDisplay(_In_ PCONFIGURATION_COMPONENT_DATA SystemKey)
{
    OFW_HANDLE Display, Mmu;
    CHAR Path[256];
    ULONG Address, Width, Height, Depth, Stride, Size;
    ULONG Cells[7];
    PCM_PARTIAL_RESOURCE_LIST Resources;
    PCM_FRAMEBUF_DEVICE_DATA FrameBuffer;
    PCONFIGURATION_COMPONENT_DATA BusKey, ControllerKey;

    Display = OfwFindDisplay(OfwFindDevice("/"));
    if (Display == OFW_INVALID || OfwPackageToPath(Display, Path, sizeof(Path)) < 0)
        return;

    /* A serial console need not have opened the display. Keep this instance
     * open until handoff: closing it may disable the firmware's video mode. */
    if (OfwOpen(Path) == OFW_INVALID ||
        !OfwGetPropCell(Display, "address", &Address) ||
        !OfwGetPropCell(Display, "width", &Width) ||
        !OfwGetPropCell(Display, "height", &Height) ||
        !OfwGetPropCell(Display, "depth", &Depth) ||
        !OfwGetPropCell(Display, "linebytes", &Stride))
        return;

    TRACE("OFW display %s: %lux%lux%lu, stride %lu, address %08lx\n",
          Path, Width, Height, Depth, Stride, Address);
    if (Depth != 32 || Width == 0 || Height == 0 || Width > MAXULONG / 4 ||
        Stride < Width * 4 || Stride % 4 != 0 || Height > MAXULONG / Stride)
    {
        ERR("Unsupported Open Firmware framebuffer format\n");
        return;
    }

    if (!OfwSetDisplayByteOrder(Display))
    {
        ERR("No supported pixel-byte-order handoff for %s\n", Path);
        return;
    }

    /* The address property is in the firmware's virtual address space. The
     * 32-bit OF MMU translate method returns (physical, mode, success). */
    if (!OfwGetPropCell(OfwFindDevice("/chosen"), "mmu", &Mmu))
        return;
    Cells[0] = (ULONG)(ULONG_PTR)"translate";
    Cells[1] = Mmu;
    Cells[2] = Address;
    if (OfwCall("call-method", 3, 4, Cells) != 0 || Cells[3] != 0 || Cells[4] == 0)
        return;
    Address = Cells[6];
    if (Address == 0 || Address % PAGE_SIZE != 0 || Stride * Height > MAXULONG - Address)
        return;

    /* Internal resources are already CPU physical addresses, independent of
     * the PCI host bridge's bus-memory translation. */
    FldrCreateComponentKey(SystemKey, AdapterClass, MultiFunctionAdapter,
                           0, 0, 0xFFFFFFFF, "Internal", NULL, 0, &BusKey);

    Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors[2]) + sizeof(*FrameBuffer);
    Resources = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
    if (!Resources)
        return;
    RtlZeroMemory(Resources, Size);
    Resources->Version = 1;
    Resources->Revision = 2;
    Resources->Count = 2;
    Resources->PartialDescriptors[0].Type = CmResourceTypeMemory;
    Resources->PartialDescriptors[0].ShareDisposition = CmResourceShareDeviceExclusive;
    Resources->PartialDescriptors[0].Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Resources->PartialDescriptors[0].u.Memory.Start.QuadPart = Address;
    Resources->PartialDescriptors[0].u.Memory.Length = Stride * Height;
    Resources->PartialDescriptors[1].Type = CmResourceTypeDeviceSpecific;
    Resources->PartialDescriptors[1].u.DeviceSpecificData.DataSize = sizeof(*FrameBuffer);
    FrameBuffer = (PCM_FRAMEBUF_DEVICE_DATA)&Resources->PartialDescriptors[2];
    FrameBuffer->Version = 1;
    FrameBuffer->Revision = 5;
    FrameBuffer->ScreenWidth = FrameBuffer->LogicalWidth = Width;
    FrameBuffer->ScreenHeight = FrameBuffer->LogicalHeight = Height;
    FrameBuffer->PixelsPerScanLine = Stride / 4;
    FrameBuffer->BitsPerPixel = Depth;
    FrameBuffer->PixelMasks.RedMask = 0x00FF0000;
    FrameBuffer->PixelMasks.GreenMask = 0x0000FF00;
    FrameBuffer->PixelMasks.BlueMask = 0x000000FF;
    FrameBuffer->PixelMasks.ReservedMask = 0xFF000000;

    FldrCreateComponentKey(BusKey, ControllerClass, DisplayController,
                           Output | ConsoleOut, 0, 0xFFFFFFFF,
                           "Open Firmware Framebuffer", Resources, Size, &ControllerKey);
}

PCONFIGURATION_COMPONENT_DATA
OfwHwDetect(_In_opt_ PCSTR Options)
{
    PCONFIGURATION_COMPONENT_DATA SystemKey, BusKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    ULONG Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors);
    CHAR Identifier[96];

    UNREFERENCED_PARAMETER(Options);

    RtlStringCbPrintfA(Identifier, sizeof(Identifier), "PowerPC %s", OfwMachine.Model);
    FldrCreateSystemKey(&SystemKey, Identifier);

    PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
    if (PartialResourceList)
    {
        RtlZeroMemory(PartialResourceList, Size);
        PartialResourceList->Version = 1;
        PartialResourceList->Revision = 1;
        FldrCreateComponentKey(SystemKey, AdapterClass, MultiFunctionAdapter, 0, 0, 0xFFFFFFFF, (OfwMachine.MachineType == PPC_MACHINE_PREP) ? "ISA" : "Internal", PartialResourceList, Size, &BusKey);
        OfwDetectPrepInput(BusKey);
    }

    OfwDetectDisplay(SystemKey);
    return SystemKey;
}

VOID
OfwPrepareForReactOS(VOID)
{
    /* The kernel loader switches the MMU away from the firmware in
     * PpcJumpToKernel; nothing may call Open Firmware after that point. */
    TRACE("OfwPrepareForReactOS\n");
}
