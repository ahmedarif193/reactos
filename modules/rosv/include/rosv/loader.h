/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Linux boot protocol structures and loader prototypes
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#pragma once

#include <rosv/rosv.h>

/* ---- Linux boot protocol constants -------------------------------------- */

/* Standard memory layout for bzImage boot */
#define LINUX_BOOT_PARAMS_ADDR      0x00010000  /* Zero page / boot_params */
#define LINUX_CMDLINE_ADDR          0x00020000  /* Command line */
#define LINUX_KERNEL_LOAD_ADDR      0x01000000  /* Protected-mode kernel at 16MB */
#define LINUX_INITRD_ADDR_MAX       0x37FFFFFF  /* Max initrd address */
#define LINUX_HEAP_END_OFFSET       0xFE00

/* bzImage magic numbers */
#define LINUX_BZIMAGE_MAGIC         0x53726448  /* "HdrS" */
#define LINUX_BOOT_FLAG_MAGIC       0xAA55
#define LINUX_OLD_CMDLINE_MAGIC     0xA33F

/* Boot loader ID - we are a custom type-2 hypervisor */
#define ROSV_BOOT_LOADER_TYPE       0xFF
#define ROSV_BOOT_LOADER_ID         0x01

/* Load flags */
#define LINUX_LOADFLAG_LOADED_HIGH  0x01
#define LINUX_LOADFLAG_CAN_USE_HEAP 0x80

/* XLoadFlags */
#define LINUX_XLF_KERNEL_64         0x01

/* Default command line for serial console debugging (ramdisk boot).
 * The virtio_mmio.device= parameter tells Linux where to find the virtio MMIO
 * device. Format: <size>@<baseaddr>:<irq>[:<id>]. Even for ramdisk-only boot,
 * this is harmless - Linux will probe the address and find no device if the
 * MMIO region is not set up by the hypervisor. */
#define ROSV_DEFAULT_CMDLINE \
    "console=ttyS0,115200n8 earlyprintk=serial,ttyS0,115200 nokaslr tsc=reliable"

/* Command line for disk-based boot (virtio-blk root device).
 * Includes virtio_mmio.device= for device discovery and root=/dev/vda for
 * mounting the virtio block device as the root filesystem. */
#define ROSV_DISK_BOOT_CMDLINE \
    "console=ttyS0,115200n8 earlyprintk=serial,ttyS0,115200 nokaslr tsc=reliable " \
    "virtio_mmio.device=0x1000@0xFEB00000:5 root=/dev/vda rootfstype=ext4 rw rootwait"

/* Guest page table layout (identity map for 64-bit entry) */
#define LINUX_GUEST_PML4_ADDR       0x1000
#define LINUX_GUEST_PDPT_ADDR       0x2000
#define LINUX_GUEST_PD0_ADDR        0x3000  /* PD for 0-1GB */
#define LINUX_GUEST_PD1_ADDR        0x4000  /* PD for 1-2GB */
#define LINUX_GUEST_PD2_ADDR        0x5000  /* PD for 2-3GB */
#define LINUX_GUEST_PD3_ADDR        0x6000  /* PD for 3-4GB */
#define LINUX_GUEST_GDT_ADDR        0x7000  /* GDT in guest memory */

/* ---- Virtio MMIO device constants --------------------------------------- */

/* Virtio MMIO region in guest physical address space.
 * Placed at 0xFEB00000, below the IOAPIC (0xFEC00000) and LAPIC (0xFEE00000).
 * This address is within the "PCI hole" (above top of RAM, below 4GB) and
 * must be marked reserved in the E820 map so Linux doesn't try to use it as RAM.
 * These must match ROSV_VIRTIO_BLK_MMIO_BASE/SIZE/IRQ in virtio_blk.h. */
#define ROSV_VIRTIO_MMIO_BASE       0xFEB00000ULL
#define ROSV_VIRTIO_MMIO_SIZE       0x1000ULL   /* 4KB - covers registers + config space */
#define ROSV_VIRTIO_MMIO_IRQ        5           /* Legacy PIC IRQ 5 (commonly available) */

/* Minimum protocol version we require (2.06 for cmdline_size) */
#define LINUX_MIN_PROTOCOL_VERSION  0x0206

/* 64-bit entry offset for protocol >= 2.15 */
#define LINUX_64BIT_ENTRY_OFFSET    0x200

/* E820 memory types */
#define E820_RAM                    1
#define E820_RESERVED               2
#define E820_ACPI                   3
#define E820_NVS                    4
#define E820_UNUSABLE               5

#define E820_MAX_ENTRIES            128

/* ---- Linux boot protocol structures ------------------------------------- */
/* Based on Documentation/x86/boot.rst from Linux kernel source */

#include <pshpack1.h>

typedef struct _E820_ENTRY {
    ULONG64 Address;
    ULONG64 Size;
    ULONG Type;
} E820_ENTRY, *PE820_ENTRY;

typedef struct _LINUX_SETUP_HEADER {
    UCHAR SetupSects;          /* 0x1F1 */
    USHORT RootFlags;          /* 0x1F2 */
    ULONG SysSize;             /* 0x1F4 */
    USHORT RamSize;            /* 0x1F8 */
    USHORT VidMode;            /* 0x1FA */
    USHORT RootDev;            /* 0x1FC */
    USHORT BootFlag;           /* 0x1FE - must be 0xAA55 */
    USHORT Jump;               /* 0x200 */
    ULONG Header;              /* 0x202 - must be "HdrS" (0x53726448) */
    USHORT Version;            /* 0x206 */
    ULONG RealModeSwitch;      /* 0x208 */
    USHORT StartSysSeg;        /* 0x20C */
    USHORT KernelVersion;      /* 0x20E */
    UCHAR TypeOfLoader;        /* 0x210 */
    UCHAR LoadFlags;           /* 0x211 */
    USHORT SetupMoveSize;      /* 0x212 */
    ULONG Code32Start;         /* 0x214 */
    ULONG RamdiskImage;        /* 0x218 */
    ULONG RamdiskSize;         /* 0x21C */
    ULONG BootsectKludge;      /* 0x220 */
    USHORT HeapEndPtr;         /* 0x224 */
    UCHAR ExtLoaderVer;        /* 0x226 */
    UCHAR ExtLoaderType;       /* 0x227 */
    ULONG CmdLinePtr;          /* 0x228 */
    ULONG InitrdAddrMax;       /* 0x22C */
    ULONG KernelAlignment;     /* 0x230 */
    UCHAR RelocatableKernel;   /* 0x234 */
    UCHAR MinAlignment;        /* 0x235 */
    USHORT XloadFlags;         /* 0x236 */
    ULONG CmdlineSize;         /* 0x238 */
    ULONG HardwareSubarch;     /* 0x23C */
    ULONG64 HardwareSubarchData; /* 0x240 */
    ULONG PayloadOffset;       /* 0x248 */
    ULONG PayloadLength;       /* 0x24C */
    ULONG64 SetupData;         /* 0x250 */
    ULONG64 PrefAddress;       /* 0x258 */
    ULONG InitSize;            /* 0x260 */
    ULONG HandoverOffset;      /* 0x264 */
    ULONG KernelInfoOffset;    /* 0x268 */
} LINUX_SETUP_HEADER, *PLINUX_SETUP_HEADER;

/*
 * Linux boot_params (zero page) - partial definition covering key fields.
 * The full structure is 4096 bytes (one page).
 */
typedef struct _LINUX_BOOT_PARAMS {
    /* 0x000 - screen_info (partial) */
    UCHAR ScreenInfo[64];

    /* 0x040 - apm_bios_info */
    UCHAR ApmBiosInfo[20];

    /* 0x054 - padding */
    UCHAR Pad1[4];

    /* 0x058 - tboot_addr */
    ULONG64 TbootAddr;

    /* 0x060 - ist_info */
    UCHAR IstInfo[16];

    /* 0x070 - ACPI RSDP physical address */
    ULONG64 AcpiRsdpAddr;

    /* 0x078 - padding */
    UCHAR Pad2[8];

    /* 0x080 - hd0_info / hd1_info */
    UCHAR HdInfo[32];

    /* 0x0A0 - sys_desc_table */
    UCHAR SysDescTable[16];

    /* 0x0B0 - olpc_ofw_header */
    UCHAR OlpcOfwHeader[16];

    /* 0x0C0 - ext_ramdisk_image / ext_ramdisk_size / ext_cmd_line_ptr */
    ULONG ExtRamdiskImage;     /* 0x0C0 */
    ULONG ExtRamdiskSize;      /* 0x0C4 */
    ULONG ExtCmdLinePtr;       /* 0x0C8 */

    /* 0x0CC - padding */
    UCHAR Pad3[116];

    /* 0x140 - edid_info */
    UCHAR EdidInfo[128];

    /* 0x1C0 - efi_info */
    UCHAR EfiInfo[32];

    /* 0x1E0 - alt_mem_k, scratch, e820_entries */
    ULONG AltMemK;             /* 0x1E0 */
    ULONG Scratch;             /* 0x1E4 */
    UCHAR E820Entries;         /* 0x1E8 */
    UCHAR EddBufEntries;      /* 0x1E9 */
    UCHAR EddMbrSigBufEntries; /* 0x1EA */
    UCHAR KbdStatus;           /* 0x1EB */
    UCHAR SecureBoot;          /* 0x1EC */

    /* 0x1ED - padding */
    UCHAR Pad4[2];

    /* 0x1EF - sentinel */
    UCHAR Sentinel;            /* 0x1EF */

    /* 0x1F0 - padding before setup_header */
    UCHAR Pad5[1];

    /* 0x1F1 - setup_header */
    LINUX_SETUP_HEADER Hdr;

    /* Padding to offset 0x290 (0x290 - 0x26C = 0x24 = 36) */
    UCHAR Pad6[36];

    /* 0x290 - edd_mbr_sig_buffer */
    ULONG EddMbrSigBuffer[16]; /* 0x290 */

    /* 0x2D0 - e820_table */
    E820_ENTRY E820Table[E820_MAX_ENTRIES]; /* 0x2D0, 20 bytes each */

    /* Remaining padding to fill out 4096 bytes */
    UCHAR Pad7[816];
} LINUX_BOOT_PARAMS, *PLINUX_BOOT_PARAMS;

#include <poppack.h>

/* Verify the structure is exactly one page */
C_ASSERT(sizeof(LINUX_BOOT_PARAMS) == 4096);
C_ASSERT(FIELD_OFFSET(LINUX_BOOT_PARAMS, AcpiRsdpAddr) == 0x070);
C_ASSERT(FIELD_OFFSET(LINUX_BOOT_PARAMS, Hdr) == 0x1F1);
C_ASSERT(FIELD_OFFSET(LINUX_BOOT_PARAMS, E820Table) == 0x2D0);

/* ---- Boot contract ------------------------------------------------------ */
/* Describes how the guest kernel expects to be entered. Drives entry.c dispatch. */

typedef enum _ROSV_BOOT_CONTRACT {
    RosvBootContractNone = 0,       /* Not yet determined */
    RosvBootContractBzImage,        /* Linux bzImage boot protocol (RSI=boot_params) */
    RosvBootContractElf64Direct     /* Direct ELF64 entry (PVH-like, 64-bit long mode) */
} ROSV_BOOT_CONTRACT;

/* ---- Function prototypes (loader/) -------------------------------------- */

/* image.c - kernel image type detection and dispatch */
typedef enum _ROSV_KERNEL_IMAGE_TYPE {
    RosvKernelImageUnknown = 0,
    RosvKernelImageBzImage,
    RosvKernelImageElf64,
    RosvKernelImagePe64
} ROSV_KERNEL_IMAGE_TYPE;

PCSTR
RosvKernelImageTypeName(
    _In_ ROSV_KERNEL_IMAGE_TYPE Type);

ROSV_KERNEL_IMAGE_TYPE
RosvKernelImageDetect(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize);

NTSTATUS
RosvKernelLoad(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Inout_ PROSV_VM Vm,
    _Out_opt_ ROSV_KERNEL_IMAGE_TYPE *DetectedType);

/* bzimage.c - bzImage parser and loader */
NTSTATUS
RosvBzImageLoad(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvBzImageValidate(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Out_ PLINUX_SETUP_HEADER Header);

/* initrd.c - initrd loader */
NTSTATUS
RosvInitrdLoad(
    _In_ PVOID InitrdBuffer,
    _In_ ULONG InitrdSize,
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvInitrdBegin(
    _In_ ULONG64 InitrdSize,
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvInitrdAppend(
    _In_ ULONG64 Offset,
    _In_ PVOID ChunkBuffer,
    _In_ ULONG ChunkSize,
    _Inout_ PROSV_VM Vm);

NTSTATUS
RosvInitrdCommit(
    _Inout_ PROSV_VM Vm);

/* bootparams.c - Linux boot params / zero page builder */
NTSTATUS
RosvBootParamsBuild(
    _Inout_ PROSV_VM Vm,
    _In_ PLINUX_SETUP_HEADER OriginalHeader);

VOID
RosvBootParamsSetupE820(
    _Inout_ PLINUX_BOOT_PARAMS BootParams,
    _In_ ULONG64 RamSize);

/* entry.c - Guest initial state setup */
NTSTATUS
RosvGuestEntrySetup(
    _Inout_ PROSV_VCPU Vcpu,
    _In_ PROSV_VM Vm);
