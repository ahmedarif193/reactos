/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Linux boot params / zero page builder
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 *
 * Constructs the Linux "zero page" (boot_params structure) at a fixed
 * guest physical address. This is the primary data structure the Linux
 * kernel reads at startup to understand memory layout, command line,
 * and boot loader configuration.
 */

#include <rosv/rosv.h>
#include <rosv/loader.h>
#include <rosv/vm.h>
#include <rosv/virtio_net.h>
#include <rosv/virtio_console.h>

/*
 * RosvBootParamsSetupE820 - Build the E820 memory map
 *
 * Creates a minimal E820 map describing the guest's physical memory:
 *   Entry 0: Low conventional RAM (0 - 0x9FC00)
 *   Entry 1: EBDA reserved area (0x9FC00 - 0xA0000)
 *   Entry 2: Video + ROM hole (0xA0000 - 0x100000)
 *   Entry 3: Extended RAM (0x100000 - top of guest RAM)
 *   Entry 4: Virtio-blk MMIO reserved (0xFEB00000 - 0xFEB01000)
 *   Entry 5: Virtio-net MMIO reserved (0xFEB01000 - 0xFEB02000)
 */
VOID
RosvBootParamsSetupE820(
    _Inout_ PLINUX_BOOT_PARAMS BootParams,
    _In_ ULONG64 RamSize)
{
    PE820_ENTRY Entries;
    UCHAR Count = 0;

    if (BootParams == NULL)
        return;

    Entries = BootParams->E820Table;

    /* Entry 0: Low RAM below EBDA */
    Entries[Count].Address = 0x0;
    Entries[Count].Size    = 0x9FC00;
    Entries[Count].Type    = E820_RAM;
    ROSV_TRACE("E820[%u]: 0x%llX - 0x%llX type=%u (low RAM)",
               Count, Entries[Count].Address,
               Entries[Count].Address + Entries[Count].Size,
               Entries[Count].Type);
    Count++;

    /* Entry 1: EBDA reserved */
    Entries[Count].Address = 0x9FC00;
    Entries[Count].Size    = 0x400;
    Entries[Count].Type    = E820_RESERVED;
    ROSV_TRACE("E820[%u]: 0x%llX - 0x%llX type=%u (EBDA)",
               Count, Entries[Count].Address,
               Entries[Count].Address + Entries[Count].Size,
               Entries[Count].Type);
    Count++;

    /* Entry 2: Video memory + ROM hole (0xA0000 - 0x100000) */
    Entries[Count].Address = 0xA0000;
    Entries[Count].Size    = 0x60000;
    Entries[Count].Type    = E820_RESERVED;
    ROSV_TRACE("E820[%u]: 0x%llX - 0x%llX type=%u (video+ROM)",
               Count, Entries[Count].Address,
               Entries[Count].Address + Entries[Count].Size,
               Entries[Count].Type);
    Count++;

    /* Entry 3: Extended RAM (1MB to top of guest RAM) */
    if (RamSize > 0x100000)
    {
        Entries[Count].Address = 0x100000;
        Entries[Count].Size    = RamSize - 0x100000;
        Entries[Count].Type    = E820_RAM;
        ROSV_TRACE("E820[%u]: 0x%llX - 0x%llX type=%u (extended RAM)",
                   Count, Entries[Count].Address,
                   Entries[Count].Address + Entries[Count].Size,
                   Entries[Count].Type);
        Count++;
    }
    else
    {
        ROSV_WARN("E820: guest RAM 0x%llX too small for extended memory", RamSize);
    }

    /* Entry 4: Virtio-blk MMIO reserved region (0xFEB00000 - 0xFEB01000).
     * This tells the Linux kernel that this physical address range is reserved
     * for MMIO and must not be used as regular RAM. The virtio_mmio.device=
     * kernel parameter directs the virtio driver to probe this address. */
    Entries[Count].Address = ROSV_VIRTIO_MMIO_BASE;
    Entries[Count].Size    = ROSV_VIRTIO_MMIO_SIZE;
    Entries[Count].Type    = E820_RESERVED;
    ROSV_TRACE("E820[%u]: 0x%llX - 0x%llX type=%u (virtio-blk MMIO)",
               Count, Entries[Count].Address,
               Entries[Count].Address + Entries[Count].Size,
               Entries[Count].Type);
    Count++;

    /* Entry 5: Virtio-net MMIO reserved region (0xFEB01000 - 0xFEB02000). */
    Entries[Count].Address = ROSV_VIRTIO_NET_MMIO_BASE;
    Entries[Count].Size    = ROSV_VIRTIO_NET_MMIO_SIZE;
    Entries[Count].Type    = E820_RESERVED;
    ROSV_TRACE("E820[%u]: 0x%llX - 0x%llX type=%u (virtio-net MMIO)",
               Count, Entries[Count].Address,
               Entries[Count].Address + Entries[Count].Size,
               Entries[Count].Type);
    Count++;

    /* Entry 6: Virtio-console MMIO reserved region (0xFEB02000 - 0xFEB03000). */
    Entries[Count].Address = ROSV_VIRTIO_CON_MMIO_BASE;
    Entries[Count].Size    = ROSV_VIRTIO_CON_MMIO_SIZE;
    Entries[Count].Type    = E820_RESERVED;
    ROSV_TRACE("E820[%u]: 0x%llX - 0x%llX type=%u (virtio-con MMIO)",
               Count, Entries[Count].Address,
               Entries[Count].Address + Entries[Count].Size,
               Entries[Count].Type);
    Count++;

    BootParams->E820Entries = Count;
    ROSV_TRACE("E820: %u entries built for %llu MB guest RAM",
               Count, RamSize / (1024 * 1024));
}

/*
 * RosvBootParamsBuild - Construct boot_params zero page
 *
 * Builds the complete boot_params structure in guest memory:
 *   - Zeroes the 4KB page
 *   - Copies the setup_header from the bzImage
 *   - Sets up the E820 memory map
 *   - Copies command line to a separate GPA
 *   - Sets video mode and hardware subarch
 */
NTSTATUS
RosvBootParamsBuild(
    _Inout_ PROSV_VM Vm,
    _In_ PLINUX_SETUP_HEADER OriginalHeader)
{
    PVOID BootParamsHva;
    PVOID CmdLineHva;
    PLINUX_BOOT_PARAMS BootParams;
    PCCH CmdLine;
    ULONG CmdLineLen;

    if (Vm == NULL || OriginalHeader == NULL)
    {
        ROSV_ERR("RosvBootParamsBuild: invalid parameters (Vm=%p Header=%p)",
                 Vm, OriginalHeader);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvBootParamsBuild: building boot_params at GPA 0x%llX",
               (ULONG64)LINUX_BOOT_PARAMS_ADDR);

    /* Map boot_params GPA to host virtual address */
    BootParamsHva = RosvMemoryGpaToHva(Vm, LINUX_BOOT_PARAMS_ADDR);
    if (BootParamsHva == NULL)
    {
        ROSV_ERR("RosvBootParamsBuild: failed to map boot_params GPA 0x%X",
                 LINUX_BOOT_PARAMS_ADDR);
        return STATUS_INVALID_ADDRESS;
    }

    /* Zero the entire 4KB page */
    RtlZeroMemory(BootParamsHva, PAGE_SIZE);
    BootParams = (PLINUX_BOOT_PARAMS)BootParamsHva;

    /* Copy setup_header from bzImage */
    RtlCopyMemory(&BootParams->Hdr, OriginalHeader, sizeof(LINUX_SETUP_HEADER));
    ROSV_TRACE("RosvBootParamsBuild: copied setup_header (protocol 0x%04X)",
               BootParams->Hdr.Version);

    /* Build E820 memory map */
    RosvBootParamsSetupE820(BootParams, Vm->GuestRamSize);

    ROSV_ASSERT((ROSV_ACPI_RSDP_GPA & 0xFULL) == 0, "ACPI RSDP must be 16-byte aligned");
    BootParams->AcpiRsdpAddr = ROSV_ACPI_RSDP_GPA;

    /* Set up command line */
    if (Vm->Cmdline[0] != '\0')
    {
        CmdLine = Vm->Cmdline;
    }
    else
    {
        CmdLine = ROSV_DEFAULT_CMDLINE;
    }

    CmdLineLen = (ULONG)strlen(CmdLine) + 1;
    if (CmdLineLen > ROSV_CMDLINE_MAX)
        CmdLineLen = ROSV_CMDLINE_MAX;

    /* Map command line GPA */
    CmdLineHva = RosvMemoryGpaToHva(Vm, LINUX_CMDLINE_ADDR);
    if (CmdLineHva == NULL)
    {
        ROSV_ERR("RosvBootParamsBuild: failed to map cmdline GPA 0x%X",
                 LINUX_CMDLINE_ADDR);
        return STATUS_INVALID_ADDRESS;
    }

    RtlCopyMemory(CmdLineHva, CmdLine, CmdLineLen);
    ((PCHAR)CmdLineHva)[CmdLineLen - 1] = '\0'; /* Ensure NUL termination */

    BootParams->Hdr.CmdLinePtr = LINUX_CMDLINE_ADDR;
    BootParams->Hdr.CmdlineSize = CmdLineLen;

    ROSV_TRACE("RosvBootParamsBuild: cmdline at GPA 0x%X: \"%s\"",
               LINUX_CMDLINE_ADDR, (PCCH)CmdLineHva);

    /* Video mode: NORMAL_VGA (text mode) */
    BootParams->Hdr.VidMode = 0xFFFF;

    /* Screen info: VGA text mode 80x25 so Linux's __putstr() actually outputs.
     * Without these, console_init() sets lines=0/cols=0 and __putstr is a no-op.
     * Offsets match Linux's struct screen_info (arch/x86/include/uapi/asm/screen_info.h). */
    BootParams->ScreenInfo[0x06] = 3;       /* orig_video_mode = standard VGA text */
    BootParams->ScreenInfo[0x07] = 80;      /* orig_video_cols */
    BootParams->ScreenInfo[0x0e] = 25;      /* orig_video_lines */
    BootParams->ScreenInfo[0x0f] = 0x01;    /* orig_video_isVGA = VIDEO_TYPE_MDA (no VESA/VGA framebuffer) */
    BootParams->ScreenInfo[0x10] = 16;      /* orig_video_points (font height) low byte */
    BootParams->ScreenInfo[0x11] = 0;       /* orig_video_points high byte */

    /* Hardware subarch: standard PC/AT */
    BootParams->Hdr.HardwareSubarch = 0;

    /* Set alt_mem_k (memory above 1MB in KB, legacy field) */
    if (Vm->GuestRamSize > 0x100000)
    {
        ULONG64 AltMemKb = (Vm->GuestRamSize - 0x100000) / 1024;
        BootParams->AltMemK = (ULONG)(AltMemKb > 0xFFFFFFFF ? 0xFFFFFFFF : AltMemKb);
    }

    /* Store boot params address in VM */
    Vm->BootParamsAddress = LINUX_BOOT_PARAMS_ADDR;

    ROSV_TRACE("RosvBootParamsBuild: SUCCESS - "
               "e820_entries=%u acpi_rsdp=0x%llX cmdline_ptr=0x%X vid_mode=0x%X "
               "type_of_loader=0x%X load_flags=0x%02X",
               BootParams->E820Entries,
               BootParams->AcpiRsdpAddr,
               BootParams->Hdr.CmdLinePtr,
               BootParams->Hdr.VidMode,
               BootParams->Hdr.TypeOfLoader,
               BootParams->Hdr.LoadFlags);

    return STATUS_SUCCESS;
}
