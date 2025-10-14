/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL, See COPYING in the top level directory
 * FILE:            hal/halx86/amd64/x86bios.c
 * PURPOSE:
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
//#define NDEBUG
#include <debug.h>

#include <fast486.h>

#pragma pack(push,1)
typedef struct _VBE_INFO_BLOCK
{
    CHAR Signature[4];
    USHORT Version;
    ULONG OemStringPtr;
    ULONG Capabilities;
    ULONG VideoModePtr;
    USHORT TotalMemory;
    UCHAR Reserved[236];
} VBE_INFO_BLOCK, *PVBE_INFO_BLOCK;

typedef struct _VBE_MODE_INFO
{
    USHORT ModeAttributes;
    UCHAR WinAAttributes;
    UCHAR WinBAttributes;
    USHORT WinGranularity;
    USHORT WinSize;
    USHORT WinASegment;
    USHORT WinBSegment;
    ULONG WinFuncPtr;
    USHORT BytesPerScanLine;
    USHORT XResolution;
    USHORT YResolution;
    UCHAR XCharSize;
    UCHAR YCharSize;
    UCHAR NumberOfPlanes;
    UCHAR BitsPerPixel;
    UCHAR NumberOfBanks;
    UCHAR MemoryModel;
    UCHAR BankSize;
    UCHAR NumberOfImagePages;
    UCHAR Reserved1;
    UCHAR RedMaskSize;
    UCHAR RedFieldPosition;
    UCHAR GreenMaskSize;
    UCHAR GreenFieldPosition;
    UCHAR BlueMaskSize;
    UCHAR BlueFieldPosition;
    UCHAR RsvdMaskSize;
    UCHAR RsvdFieldPosition;
    UCHAR DirectColorModeInfo;
    ULONG PhysBasePtr;
    ULONG OffScreenMemOffset;
    USHORT OffScreenMemSize;
    USHORT LinBytesPerScanLine;
    UCHAR BnkNumberOfImagePages;
    UCHAR LinNumberOfImagePages;
    UCHAR LinRedMaskSize;
    UCHAR LinRedFieldPosition;
    UCHAR LinGreenMaskSize;
    UCHAR LinGreenFieldPosition;
    UCHAR LinBlueMaskSize;
    UCHAR LinBlueFieldPosition;
    UCHAR LinRsvdMaskSize;
    UCHAR LinRsvdFieldPosition;
    ULONG MaxPixelClock;
    UCHAR Reserved2[189];
    UCHAR Reserved3[1];
} VBE_MODE_INFO, *PVBE_MODE_INFO;
#pragma pack(pop)

/* GLOBALS *******************************************************************/

/* This page serves as fallback for pages used by Mm */
PFN_NUMBER x86BiosFallbackPfn;

BOOLEAN x86BiosIsInitialized;
LONG x86BiosBufferIsAllocated = 0;
PUCHAR x86BiosMemoryMapping;

/* This the physical address of the bios buffer */
ULONG64 x86BiosBufferPhysical;

#if defined(_M_AMD64)
/*
 * The amd64 HAL does not link in bios.c, so provide the framebuffer state
 * storage here to satisfy the references in the generic BIOS helpers.
 */
HALP_BIOS_DISPLAY_INFORMATION HalpBiosDisplayInformation;
BOOLEAN HalpBiosDisplayInformationValid = FALSE;
#endif

static
ULONG
HalpCreateColorMask(
    _In_ UCHAR MaskSize,
    _In_ UCHAR FieldPosition)
{
    ULONG Mask;

    if (MaskSize == 0 || MaskSize >= 32)
        return 0;

    Mask = (1u << MaskSize) - 1u;
    return Mask << FieldPosition;
}

BOOLEAN
NTAPI
HalpTryVbeMode(VOID)
{
    static const USHORT PreferredModes[] =
    {
        /* Probe common high-resolution true-colour modes first (skipping unsupported entries). */
        0x14C, /* 1920x1200 */
        0x14B, /* 1920x1080 */
        0x149, /* 1680x1050 */
        0x148, /* 1400x1050 */
        0x146, /* 1366x768 */
        0x145, /* 1360x768 */
        0x144, /* 1280x1024 */
        0x142, /* 1280x960 */
        0x141, /* 1280x720 */
        0x140, /* 1280x1024 (vendor extensions) */
        0x11F, /* 1600x1200 */
        0x118, /* 1024x768 */
        0x115, /* 800x600 */
        0x112, /* 640x480 */
        0
    };
    USHORT Segment, Offset;
    ULONG BufferSize;
    PVBE_INFO_BLOCK InfoBlock;
    PVBE_MODE_INFO ModeInfo;
    X86_BIOS_REGISTERS Regs;
    BOOLEAN Success = FALSE;
    ULONG Index;

    HalpBiosDisplayInformationValid = FALSE;
    RtlZeroMemory(&HalpBiosDisplayInformation, sizeof(HALP_BIOS_DISPLAY_INFORMATION));

    if (!x86BiosIsInitialized)
    {
        return FALSE;
    }

    BufferSize = PAGE_SIZE;
    if (!NT_SUCCESS(x86BiosAllocateBuffer(&BufferSize, &Segment, &Offset)))
    {
        return FALSE;
    }

    InfoBlock = (PVBE_INFO_BLOCK)(x86BiosMemoryMapping + (Segment << 4) + Offset);
    ModeInfo = (PVBE_MODE_INFO)InfoBlock;

    RtlZeroMemory(InfoBlock, BufferSize);
    InfoBlock->Signature[0] = 'V';
    InfoBlock->Signature[1] = 'B';
    InfoBlock->Signature[2] = 'E';
    InfoBlock->Signature[3] = '2';

    RtlZeroMemory(&Regs, sizeof(Regs));
    Regs.Eax = 0x4F00;
    Regs.SegEs = Segment;
    Regs.Edi = Offset;

    if (!x86BiosCall(0x10, &Regs) || (Regs.Eax & 0xFFFF) != 0x004F)
    {
        goto Cleanup;
    }

    for (Index = 0; PreferredModes[Index] != 0; Index++)
    {
        USHORT Mode = PreferredModes[Index];
        ULONG Pitch;
        ULONG RedMask, GreenMask, BlueMask;

        RtlZeroMemory(ModeInfo, sizeof(VBE_MODE_INFO));

        RtlZeroMemory(&Regs, sizeof(Regs));
        Regs.Eax = 0x4F01;
        Regs.Ecx = Mode;
        Regs.SegEs = Segment;
        Regs.Edi = Offset;

        if (!x86BiosCall(0x10, &Regs) || (Regs.Eax & 0xFFFF) != 0x004F)
        {
            continue;
        }

        if ((ModeInfo->ModeAttributes & 0x0081) != 0x0081)
        {
            continue;
        }

        if ((ModeInfo->LinBytesPerScanLine == 0) && (ModeInfo->BytesPerScanLine == 0))
        {
            continue;
        }

        if (ModeInfo->BitsPerPixel < 32)
        {
            continue;
        }

        RtlZeroMemory(&Regs, sizeof(Regs));
        Regs.Eax = 0x4F02;
        Regs.Ebx = 0x4000 | Mode;

        if (!x86BiosCall(0x10, &Regs) || (Regs.Eax & 0xFFFF) != 0x004F)
        {
            continue;
        }

        Pitch = ModeInfo->LinBytesPerScanLine ? ModeInfo->LinBytesPerScanLine : ModeInfo->BytesPerScanLine;

        if (Pitch == 0 || ModeInfo->YResolution == 0)
        {
            continue;
        }

        RedMask = HalpCreateColorMask(ModeInfo->LinRedMaskSize ? ModeInfo->LinRedMaskSize : ModeInfo->RedMaskSize,
                                      ModeInfo->LinRedFieldPosition ? ModeInfo->LinRedFieldPosition : ModeInfo->RedFieldPosition);
        GreenMask = HalpCreateColorMask(ModeInfo->LinGreenMaskSize ? ModeInfo->LinGreenMaskSize : ModeInfo->GreenMaskSize,
                                        ModeInfo->LinGreenFieldPosition ? ModeInfo->LinGreenFieldPosition : ModeInfo->GreenFieldPosition);
        BlueMask = HalpCreateColorMask(ModeInfo->LinBlueMaskSize ? ModeInfo->LinBlueMaskSize : ModeInfo->BlueMaskSize,
                                       ModeInfo->LinBlueFieldPosition ? ModeInfo->LinBlueFieldPosition : ModeInfo->BlueFieldPosition);

        HalpBiosDisplayInformation.FrameBufferBase.QuadPart = ModeInfo->PhysBasePtr;
        HalpBiosDisplayInformation.Width = ModeInfo->XResolution;
        HalpBiosDisplayInformation.Height = ModeInfo->YResolution;
        HalpBiosDisplayInformation.BitsPerPixel = ModeInfo->BitsPerPixel;
        HalpBiosDisplayInformation.Pitch = Pitch;
        HalpBiosDisplayInformation.PixelsPerScanLine = (ModeInfo->BitsPerPixel != 0)
                                                       ? (Pitch * 8) / ModeInfo->BitsPerPixel
                                                       : ModeInfo->XResolution;
        HalpBiosDisplayInformation.FrameBufferSize = Pitch * ModeInfo->YResolution;
        HalpBiosDisplayInformation.RedMask = RedMask;
        HalpBiosDisplayInformation.GreenMask = GreenMask;
        HalpBiosDisplayInformation.BlueMask = BlueMask;

        if (RedMask == 0x00FF0000 && GreenMask == 0x0000FF00 && BlueMask == 0x000000FF)
        {
            HalpBiosDisplayInformation.PixelFormat = 0;
        }
        else if (RedMask == 0x000000FF && GreenMask == 0x0000FF00 && BlueMask == 0x00FF0000)
        {
            HalpBiosDisplayInformation.PixelFormat = 1;
        }
        else
        {
            HalpBiosDisplayInformation.PixelFormat = 2;
        }

        HalpBiosDisplayInformationValid = TRUE;
        Success = TRUE;
        break;
    }

Cleanup:
    x86BiosFreeBuffer(Segment, Offset);
    return Success;
}
VOID
NTAPI
DbgDumpPage(PUCHAR MemBuffer, USHORT Segment)
{
    ULONG x, y, Offset;

    for (y = 0; y < 0x100; y++)
    {
        for (x = 0; x < 0x10; x++)
        {
            Offset = Segment * 16 + y * 16 + x;
            DbgPrint("%02x ", MemBuffer[Offset]);
        }
        DbgPrint("\n");
    }
}

VOID
NTAPI
HalInitializeBios(
    _In_ ULONG Phase,
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPFN_NUMBER PfnArray;
    PFN_NUMBER Pfn, Last;
    PMEMORY_ALLOCATION_DESCRIPTOR Descriptor;
    PLIST_ENTRY ListEntry;
    PMDL Mdl;
    ULONG64 PhysicalAddress;

    if (Phase == 0)
    {
        /* Allow initialization to succeed even if the firmware does not expose legacy BIOS services. */
        /* Allocate one page for a fallback mapping */
        PhysicalAddress = HalpAllocPhysicalMemory(LoaderBlock,
                                                  0x100000,
                                                  1,
                                                  FALSE);
        if (PhysicalAddress == 0)
        {
            /* Allocation failed - x86 BIOS services will not be available */
            DPRINT1("HalInitializeBios: Failed to allocate fallback page\n");
            x86BiosIsInitialized = FALSE;
            return;
        }

        x86BiosFallbackPfn = PhysicalAddress / PAGE_SIZE;
        ASSERT(x86BiosFallbackPfn != 0);

        /* Allocate a page for the buffer allocation */
        x86BiosBufferPhysical = HalpAllocPhysicalMemory(LoaderBlock,
                                                        0x100000,
                                                        1,
                                                        FALSE);
        if (x86BiosBufferPhysical == 0)
        {
            /* Allocation failed - x86 BIOS services will not be available */
            DPRINT1("HalInitializeBios: Failed to allocate buffer page\n");
            x86BiosIsInitialized = FALSE;
            return;
        }
    }
    else
    {
        /* Create a flat mapping of the low 1 MB using the fallback page as default. */
        /* Allocate an MDL for 1MB */
        Mdl = IoAllocateMdl(NULL, 0x100000, FALSE, FALSE, NULL);
        if (!Mdl)
        {
            /* MDL allocation failed - x86 BIOS services will not be available */
            DPRINT1("HalInitializeBios: Failed to allocate MDL\n");
            x86BiosIsInitialized = FALSE;
            return;
        }

        /* Get pointer to the pfn array */
        PfnArray = MmGetMdlPfnArray(Mdl);

        /* Fill the array with the fallback page */
        for (Pfn = 0; Pfn < 0x100; Pfn++)
        {
            PfnArray[Pfn] = x86BiosFallbackPfn;
        }

        /* Loop the memory descriptors */
        for (ListEntry = LoaderBlock->MemoryDescriptorListHead.Flink;
             ListEntry != &LoaderBlock->MemoryDescriptorListHead;
             ListEntry = ListEntry->Flink)
        {
            /* Get the memory descriptor */
            Descriptor = CONTAINING_RECORD(ListEntry,
                                           MEMORY_ALLOCATION_DESCRIPTOR,
                                           ListEntry);

            /* Check if the memory is in the low 1 MB range */
            if (Descriptor->BasePage < 0x100)
            {
                /* Check if the memory type is firmware */
                if ((Descriptor->MemoryType == LoaderFirmwarePermanent) ||
                    (Descriptor->MemoryType == LoaderSpecialMemory))
                {
                    /* It's firmware, so map it! */
                    Last = min(Descriptor->BasePage + Descriptor->PageCount, 0x100);
                    for (Pfn = Descriptor->BasePage; Pfn < Last; Pfn++)
                    {
                        /* Set each physical page in the MDL */
                        PfnArray[Pfn] = Pfn;
                    }
                }
            }
        }

        /* Map this page proper, too */
        Pfn = x86BiosBufferPhysical / PAGE_SIZE;
        PfnArray[Pfn] = Pfn;

        Mdl->MdlFlags = MDL_PAGES_LOCKED;

        /* Map the MDL to system space */
        x86BiosMemoryMapping = MmGetSystemAddressForMdlSafe(Mdl, HighPagePriority);
        if (!x86BiosMemoryMapping)
        {
            /* MDL mapping failed - x86 BIOS services will not be available */
            DPRINT1("HalInitializeBios: Failed to map MDL to system space\n");
            IoFreeMdl(Mdl);
            x86BiosIsInitialized = FALSE;
            return;
        }

        DPRINT1("*x86BiosMemoryMapping: %p, %p\n",
                *(PVOID*)x86BiosMemoryMapping, *(PVOID*)(x86BiosMemoryMapping + 8));
        //DbgDumpPage(x86BiosMemoryMapping, 0xc351);

        x86BiosIsInitialized = TRUE;
    }
}

NTSTATUS
NTAPI
x86BiosAllocateBuffer(
    _In_ ULONG *Size,
    _In_ USHORT *Segment,
    _In_ USHORT *Offset)
{
    /* Check if the system is initialized and the buffer is large enough */
    if (!x86BiosIsInitialized)
    {
        /* x86 BIOS services not available (UEFI system) */
        return STATUS_NOT_SUPPORTED;
    }
    
    if (*Size > PAGE_SIZE)
    {
        /* Buffer too large */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Check if the buffer is already allocated */
    if (InterlockedBitTestAndSet(&x86BiosBufferIsAllocated, 0))
    {
        /* Buffer was already allocated, fail */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* The buffer is sufficient, return hardcoded address and size */
    *Size = PAGE_SIZE;
    *Segment = x86BiosBufferPhysical / 16;
    *Offset = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
x86BiosFreeBuffer(
    _In_ USHORT Segment,
    _In_ USHORT Offset)
{
    if (!x86BiosIsInitialized)
    {
        /* x86 BIOS services not available (UEFI system) */
        return STATUS_NOT_SUPPORTED;
    }
    
    /* Check if the address matches the buffer that x86BiosAllocateBuffer handed out. */
    if ((Segment != (x86BiosBufferPhysical / 16)) || (Offset != 0))
    {
        /* Invalid segment/offset */
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if the buffer was allocated */
    if (!InterlockedBitTestAndReset(&x86BiosBufferIsAllocated, 0))
    {
        /* It was not, fail */
        return STATUS_INVALID_PARAMETER;
    }

    /* Buffer is freed, nothing more to do */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
x86BiosReadMemory(
    _In_ USHORT Segment,
    _In_ USHORT Offset,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    ULONG_PTR Address;

    /* Calculate the physical address */
    Address = (Segment << 4) + Offset;

    /* Check if it's valid */
    if (!x86BiosIsInitialized)
    {
        /* x86 BIOS services not available (UEFI system) */
        return STATUS_NOT_SUPPORTED;
    }
    
    if ((Address + Size) > 0x100000)
    {
        /* Invalid address range */
        return STATUS_INVALID_PARAMETER;
    }

    /* Copy the memory to the buffer */
    RtlCopyMemory(Buffer, x86BiosMemoryMapping + Address, Size);

    /* Return success */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
x86BiosWriteMemory(
    _In_ USHORT Segment,
    _In_ USHORT Offset,
    _In_reads_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size)
{
    ULONG_PTR Address;

    /* Calculate the physical address */
    Address = (Segment << 4) + Offset;

    /* Check if it's valid */
    if (!x86BiosIsInitialized)
    {
        /* x86 BIOS services not available (UEFI system) */
        return STATUS_NOT_SUPPORTED;
    }
    
    if ((Address + Size) > 0x100000)
    {
        /* Invalid address range */
        return STATUS_INVALID_PARAMETER;
    }

    /* Copy the memory from the buffer */
    RtlCopyMemory(x86BiosMemoryMapping + Address, Buffer, Size);

    /* Return success */
    return STATUS_SUCCESS;
}

static
VOID
FASTCALL
x86MemRead(
    PFAST486_STATE State,
    ULONG Address,
    PVOID Buffer,
    ULONG Size)
{
    /* Validate the address range */
    if (((ULONG64)Address + Size) < 0x100000)
    {
        RtlCopyMemory(Buffer, x86BiosMemoryMapping + Address, Size);
    }
    else
    {
        RtlFillMemory(Buffer, Size, 0xCC);
        DPRINT1("x86MemRead: invalid read at 0x%lx (size 0x%lx)\n", Address, Size);
    }
}

static
VOID
FASTCALL
x86MemWrite(
    PFAST486_STATE State,
    ULONG Address,
    PVOID Buffer,
    ULONG Size)
{
    /* Validate the address range */
    if (((ULONG64)Address + Size) < 0x100000)
    {
        RtlCopyMemory(x86BiosMemoryMapping + Address, Buffer, Size);
    }
    else
    {
        DPRINT1("x86MemWrite: invalid write at 0x%lx (size 0x%lx)\n", Address, Size);
    }
}

static
BOOLEAN
ValidatePort(
    USHORT Port,
    UCHAR Size,
    BOOLEAN IsWrite)
{
    switch (Port)
    {
        // VGA: https://wiki.osdev.org/VGA_Hardware#Port_0x3C0
        case 0x3C0: return (Size == 1) && IsWrite;
        case 0x3C1: return (Size == 1) && !IsWrite;
        case 0x3C2: return (Size == 1) && IsWrite;
        case 0x3C4: return IsWrite;
        case 0x3C5: return (Size <= 2);
        case 0x3C7: return (Size == 1) && IsWrite;
        case 0x3CC: return (Size == 1) && !IsWrite;
        case 0x3CE: return IsWrite;
        case 0x3CF: return (Size <= 2);
        case 0x3D4: return IsWrite;
        case 0x3D5: return (Size <= 2);
        case 0x3C6: return (Size == 1);
        case 0x3C8: return (Size == 1) && IsWrite;
        case 0x3C9: return (Size == 1);
        case 0x3DA: return (Size == 1) && !IsWrite;

        // OVMF debug messages used by VBox / QEMU
        // https://www.virtualbox.org/svn/vbox/trunk/src/VBox/Devices/EFI/Firmware/OvmfPkg/README
        case 0x402: return (Size == 1) && IsWrite;

        // BOCHS VBE: https://forum.osdev.org/viewtopic.php?f=1&t=14639
        case 0x1CE: return (Size == 1) && IsWrite;
        case 0x1CF: return (Size == 1);

        // CHECKME!
        case 0x3B6: return (Size <= 2);
    }

    /* Allow but report unknown ports, we trust the BIOS for now */
    DPRINT1("Unknown port 0x%x, size %d, write %d\n", Port, Size, IsWrite);
    return TRUE;
}

static
VOID
FASTCALL
x86IoRead(
    PFAST486_STATE State,
    USHORT Port,
    PVOID Buffer,
    ULONG DataCount,
    UCHAR DataSize)
{
    /* Validate the port */
    if (!ValidatePort(Port, DataSize, FALSE))
    {
        DPRINT1("Invalid IO port read access (port: 0x%x, count: 0x%x)\n", Port, DataSize);
    }

    switch (DataSize)
    {
        case 1: READ_PORT_BUFFER_UCHAR((PUCHAR)(ULONG_PTR)Port, Buffer, DataCount); return;
        case 2: READ_PORT_BUFFER_USHORT((PUSHORT)(ULONG_PTR)Port, Buffer, DataCount); return;
        case 4: READ_PORT_BUFFER_ULONG((PULONG)(ULONG_PTR)Port, Buffer, DataCount); return;
    }
}

static
VOID
FASTCALL
x86IoWrite(
    PFAST486_STATE State,
    USHORT Port,
    PVOID Buffer,
    ULONG DataCount,
    UCHAR DataSize)
{
    /* Validate the port */
    if (!ValidatePort(Port, DataSize, TRUE))
    {
        DPRINT1("Invalid IO port write access (port: 0x%x, count: 0x%x)\n", Port, DataSize);
    }

    switch (DataSize)
    {
        case 1: WRITE_PORT_BUFFER_UCHAR((PUCHAR)(ULONG_PTR)Port, Buffer, DataCount); return;
        case 2: WRITE_PORT_BUFFER_USHORT((PUSHORT)(ULONG_PTR)Port, Buffer, DataCount); return;
        case 4: WRITE_PORT_BUFFER_ULONG((PULONG)(ULONG_PTR)Port, Buffer, DataCount); return;
    }
}

static
VOID
FASTCALL
x86BOP(
    PFAST486_STATE State,
    UCHAR BopCode)
{
    ASSERT(FALSE);
}

static
UCHAR
FASTCALL
x86IntAck (
    PFAST486_STATE State)
{
    ASSERT(FALSE);
    return 0;
}

BOOLEAN
NTAPI
x86BiosCall(
    _In_ ULONG InterruptNumber,
    _Inout_ PX86_BIOS_REGISTERS Registers)
{
    const ULONG StackBase = 0x2000;
    FAST486_STATE EmulatorContext;
    ULONG FlatIp;
    PUCHAR InstructionPointer;

    if (!x86BiosIsInitialized)
    {
        DPRINT1("x86BiosCall: NOT_SUPPORTED (UEFI)\n");
        return FALSE;
    }

    /* Initialize the emulator context */
    Fast486Initialize(&EmulatorContext,
                      x86MemRead,
                      x86MemWrite,
                      x86IoRead,
                      x86IoWrite,
                      x86BOP,
                      x86IntAck,
                      NULL,  // FpuCallback,
                      NULL); // Tlb

    /* Copy the registers */
    EmulatorContext.GeneralRegs[FAST486_REG_EAX].Long = Registers->Eax;
    EmulatorContext.GeneralRegs[FAST486_REG_EBX].Long = Registers->Ebx;
    EmulatorContext.GeneralRegs[FAST486_REG_ECX].Long = Registers->Ecx;
    EmulatorContext.GeneralRegs[FAST486_REG_EDX].Long = Registers->Edx;
    EmulatorContext.GeneralRegs[FAST486_REG_ESI].Long = Registers->Esi;
    EmulatorContext.GeneralRegs[FAST486_REG_EDI].Long = Registers->Edi;
    EmulatorContext.SegmentRegs[FAST486_REG_DS].Selector = Registers->SegDs;
    EmulatorContext.SegmentRegs[FAST486_REG_ES].Selector = Registers->SegEs;

    /* Set Eflags */
    EmulatorContext.Flags.Long = 0;
    EmulatorContext.Flags.AlwaysSet = 1;
    EmulatorContext.Flags.If = 1;

    /* Set up the INT stub */
    FlatIp = StackBase - 4;
    InstructionPointer = x86BiosMemoryMapping + FlatIp;
    InstructionPointer[0] = 0xCD; // INT instruction
    InstructionPointer[1] = (UCHAR)InterruptNumber;
    InstructionPointer[2] = 0x90; // NOP. We will stop at this address.

    /* Set the stack pointer */
    Fast486SetStack(&EmulatorContext, 0, StackBase - 8);

    /* Start execution at the INT stub */
    Fast486ExecuteAt(&EmulatorContext, 0x00, FlatIp);

    while (TRUE)
    {
        /* Get the current flat IP */
        FlatIp = (EmulatorContext.SegmentRegs[FAST486_REG_CS].Selector << 4) +
                 EmulatorContext.InstPtr.Long;

        /* Make sure we haven't left the allowed memory range */
        if (FlatIp >= 0x100000)
        {
            DPRINT1("x86BiosCall: invalid IP (0x%lx) during BIOS execution\n", FlatIp);
            return FALSE;
        }

        /* Check if we returned from our int stub */
        if (FlatIp == (StackBase - 2))
        {
            /* We are done! */
            break;
        }

        /* Emulate one instruction */
        Fast486StepInto(&EmulatorContext);
    }

    /* Copy the registers back */
    Registers->Eax = EmulatorContext.GeneralRegs[FAST486_REG_EAX].Long;
    Registers->Ebx = EmulatorContext.GeneralRegs[FAST486_REG_EBX].Long;
    Registers->Ecx = EmulatorContext.GeneralRegs[FAST486_REG_ECX].Long;
    Registers->Edx = EmulatorContext.GeneralRegs[FAST486_REG_EDX].Long;
    Registers->Esi = EmulatorContext.GeneralRegs[FAST486_REG_ESI].Long;
    Registers->Edi = EmulatorContext.GeneralRegs[FAST486_REG_EDI].Long;
    Registers->SegDs = EmulatorContext.SegmentRegs[FAST486_REG_DS].Selector;
    Registers->SegEs = EmulatorContext.SegmentRegs[FAST486_REG_ES].Selector;

    return TRUE;
}

BOOLEAN
NTAPI
HalpProgramVgaMode12(VOID)
{
    ULONG i;
    PHYSICAL_ADDRESS VgaPhysical;
    PUCHAR VgaBase;

    HalpBiosDisplayInformationValid = FALSE;
    RtlZeroMemory(&HalpBiosDisplayInformation, sizeof(HALP_BIOS_DISPLAY_INFORMATION));

    DPRINT1("HalpBiosDisplayReset: Programming VGA mode 0x12 directly\n");

    /* Reset Attribute Controller */
    (VOID)READ_PORT_UCHAR((PUCHAR)0x3DA);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x00);

    /* Synchronous reset on */
    WRITE_PORT_UCHAR((PUCHAR)0x3C4, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3C5, 0x01);

    /* Miscellaneous Output Register */
    WRITE_PORT_UCHAR((PUCHAR)0x3C2, 0xE3);

    /* Sequencer registers */
    WRITE_PORT_UCHAR((PUCHAR)0x3C4, 0x01);
    WRITE_PORT_UCHAR((PUCHAR)0x3C5, 0x01);
    WRITE_PORT_UCHAR((PUCHAR)0x3C4, 0x02);
    WRITE_PORT_UCHAR((PUCHAR)0x3C5, 0x0F);
    WRITE_PORT_UCHAR((PUCHAR)0x3C4, 0x03);
    WRITE_PORT_UCHAR((PUCHAR)0x3C5, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3C4, 0x04);
    WRITE_PORT_UCHAR((PUCHAR)0x3C5, 0x06);

    /* Synchronous reset off */
    WRITE_PORT_UCHAR((PUCHAR)0x3C4, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3C5, 0x03);

    /* Unlock CRTC registers */
    WRITE_PORT_UCHAR((PUCHAR)0x3D4, 0x11);
    WRITE_PORT_UCHAR((PUCHAR)0x3D5, 0x00);

    static const UCHAR crtc_regs[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF
    };

    for (i = 0; i < sizeof(crtc_regs); i++)
    {
        WRITE_PORT_UCHAR((PUCHAR)0x3D4, (UCHAR)i);
        WRITE_PORT_UCHAR((PUCHAR)0x3D5, crtc_regs[i]);
    }

    /* Graphics Controller registers */
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x01);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x02);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x03);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x04);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x05);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x06);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x05);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x07);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0x0F);
    WRITE_PORT_UCHAR((PUCHAR)0x3CE, 0x08);
    WRITE_PORT_UCHAR((PUCHAR)0x3CF, 0xFF);

    /* Attribute Controller registers */
    static const UCHAR attr_regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
    };

    (VOID)READ_PORT_UCHAR((PUCHAR)0x3DA);
    for (i = 0; i < 16; i++)
    {
        WRITE_PORT_UCHAR((PUCHAR)0x3C0, (UCHAR)i);
        WRITE_PORT_UCHAR((PUCHAR)0x3C0, attr_regs[i]);
    }
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x10);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x01);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x11);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x12);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x0F);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x13);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x14);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)0x3C0, 0x20);

    /* Set DAC mask */
    WRITE_PORT_UCHAR((PUCHAR)0x3C6, 0xFF);

    VgaPhysical.QuadPart = 0xA0000;
    VgaBase = (PUCHAR)MmMapIoSpace(VgaPhysical, 0x10000, MmNonCached);

    if (VgaBase)
    {
        ULONG j;

        WRITE_PORT_UCHAR((PUCHAR)0x3C4, 0x02);
        WRITE_PORT_UCHAR((PUCHAR)0x3C5, 0x0F);

        for (j = 0; j < 0x10000; j++)
        {
            WRITE_REGISTER_UCHAR(VgaBase + j, 0x00);
        }

        MmUnmapIoSpace(VgaBase, 0x10000);
    }
    else
    {
        DPRINT1("HalpBiosDisplayReset: Failed to map VGA memory\n");
    }

    DPRINT1("HalpBiosDisplayReset: VGA graphics mode initialization complete\n");
    return TRUE;
}

#ifdef _M_AMD64
BOOLEAN
NTAPI
HalpBiosDisplayReset(VOID)
{
    if (!x86BiosIsInitialized)
    {
        DPRINT("HalpBiosDisplayReset: x86 BIOS services are unavailable\n");
    }

    return HalpProgramVgaMode12();
}
#endif // _M_AMD64
