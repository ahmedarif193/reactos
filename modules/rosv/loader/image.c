/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Kernel image type detection and load dispatch
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/loader.h>
#include <rosv/vm.h>

#define ROSV_ELFCLASS64            2
#define ROSV_ELFDATA2LSB           1
#define ROSV_EM_X86_64             62
#define ROSV_ET_EXEC               2
#define ROSV_PT_LOAD               1
#define ROSV_ELF64_EHDR_SIZE       64
#define ROSV_ELF64_PHDR_SIZE       56

#define ROSV_IMAGE_FILE_MACHINE_AMD64 0x8664
#define ROSV_IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20B

static __inline USHORT
RosvReadLe16(
    _In_reads_(2) const UCHAR *Ptr)
{
    return (USHORT)Ptr[0] | ((USHORT)Ptr[1] << 8);
}

static __inline ULONG
RosvReadLe32(
    _In_reads_(4) const UCHAR *Ptr)
{
    return (ULONG)Ptr[0] |
           ((ULONG)Ptr[1] << 8) |
           ((ULONG)Ptr[2] << 16) |
           ((ULONG)Ptr[3] << 24);
}

static __inline ULONG64
RosvReadLe64(
    _In_reads_(8) const UCHAR *Ptr)
{
    return (ULONG64)RosvReadLe32(Ptr) |
           ((ULONG64)RosvReadLe32(Ptr + 4) << 32);
}

static BOOLEAN
RosvLooksLikeBzImage(
    _In_reads_bytes_(ImageSize) const UCHAR *Base,
    _In_ ULONG ImageSize)
{
    UCHAR SetupSects;
    ULONG SetupSize;

    if (ImageSize < 1024)
        return FALSE;

    if (RosvReadLe16(Base + 0x1FE) != LINUX_BOOT_FLAG_MAGIC)
        return FALSE;

    if (RosvReadLe32(Base + 0x202) != LINUX_BZIMAGE_MAGIC)
        return FALSE;

    SetupSects = Base[0x1F1];
    if (SetupSects == 0)
        SetupSects = 4;

    SetupSize = ((ULONG)SetupSects + 1) * 512;
    return SetupSize <= ImageSize;
}

static BOOLEAN
RosvLooksLikeElf64(
    _In_reads_bytes_(ImageSize) const UCHAR *Base,
    _In_ ULONG ImageSize)
{
    if (ImageSize < 64)
        return FALSE;

    if (Base[0] != 0x7F || Base[1] != 'E' || Base[2] != 'L' || Base[3] != 'F')
        return FALSE;

    if (Base[4] != ROSV_ELFCLASS64 || Base[5] != ROSV_ELFDATA2LSB)
        return FALSE;

    if (RosvReadLe16(Base + 18) != ROSV_EM_X86_64)
        return FALSE;

    return TRUE;
}

static BOOLEAN
RosvLooksLikePe64(
    _In_reads_bytes_(ImageSize) const UCHAR *Base,
    _In_ ULONG ImageSize)
{
    ULONG PeOff;

    if (ImageSize < 0x40)
        return FALSE;

    if (Base[0] != 'M' || Base[1] != 'Z')
        return FALSE;

    PeOff = RosvReadLe32(Base + 0x3C);
    if (PeOff > ImageSize || ImageSize - PeOff < 0x1A)
        return FALSE;

    if (RosvReadLe32(Base + PeOff) != 0x00004550) /* "PE\0\0" */
        return FALSE;

    if (RosvReadLe16(Base + PeOff + 4) != ROSV_IMAGE_FILE_MACHINE_AMD64)
        return FALSE;

    if (RosvReadLe16(Base + PeOff + 24) != ROSV_IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return FALSE;

    return TRUE;
}

static NTSTATUS
RosvKernelLoadElf64(
    _In_reads_bytes_(ImageSize) PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Inout_ PROSV_VM Vm)
{
    NTSTATUS Status;
    const UCHAR *Base = (const UCHAR *)ImageBuffer;
    ULONG64 Entry;
    USHORT ElfType;
    ULONG64 PhOff;
    USHORT PhEntSize;
    USHORT PhNum;
    USHORT i;
    ULONG64 LowestPaddr;
    ULONG64 HighestEnd;
    BOOLEAN FoundLoad;
    PVOID BootParamsHva;
    PVOID CmdLineHva;
    PLINUX_BOOT_PARAMS BootParams;
    PCCH CmdLine;
    ULONG CmdLineLen;

    if (!RosvLooksLikeElf64(Base, ImageSize))
    {
        ROSV_ERR("RosvKernelLoadElf64: ELF64 magic/class/endian/machine check failed");
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Read ELF64 header fields */
    ElfType = RosvReadLe16(Base + 16);
    Entry = RosvReadLe64(Base + 24);
    PhOff = RosvReadLe64(Base + 32);
    PhEntSize = RosvReadLe16(Base + 54);
    PhNum = RosvReadLe16(Base + 56);

    ROSV_TRACE("RosvKernelLoadElf64: e_type=%u e_entry=0x%llX e_phoff=0x%llX "
               "e_phentsize=%u e_phnum=%u",
               ElfType, Entry, PhOff, PhEntSize, PhNum);

    /* Validate e_type == ET_EXEC */
    if (ElfType != ROSV_ET_EXEC)
    {
        ROSV_ERR("RosvKernelLoadElf64: unsupported e_type %u (expected ET_EXEC=%u)",
                 ElfType, ROSV_ET_EXEC);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Validate program header table fits in image */
    if (PhEntSize < ROSV_ELF64_PHDR_SIZE)
    {
        ROSV_ERR("RosvKernelLoadElf64: e_phentsize %u too small (need >= %u)",
                 PhEntSize, ROSV_ELF64_PHDR_SIZE);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    if (PhNum == 0)
    {
        ROSV_ERR("RosvKernelLoadElf64: no program headers (e_phnum=0)");
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    if (PhOff + (ULONG64)PhEntSize * PhNum > ImageSize)
    {
        ROSV_ERR("RosvKernelLoadElf64: program header table extends beyond image "
                 "(phoff=0x%llX, %u*%u=%llu, image_size=%u)",
                 PhOff, PhNum, PhEntSize,
                 (ULONG64)PhEntSize * PhNum, ImageSize);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Iterate PT_LOAD segments */
    LowestPaddr = (ULONG64)-1;
    HighestEnd = 0;
    FoundLoad = FALSE;

    for (i = 0; i < PhNum; i++)
    {
        const UCHAR *Phdr = Base + PhOff + (ULONG64)i * PhEntSize;
        ULONG PType;
        ULONG64 POffset;
        ULONG64 PPaddr;
        ULONG64 PFilesz;
        ULONG64 PMemsz;

        PType = RosvReadLe32(Phdr + 0);
        POffset = RosvReadLe64(Phdr + 8);
        PPaddr = RosvReadLe64(Phdr + 24);
        PFilesz = RosvReadLe64(Phdr + 32);
        PMemsz = RosvReadLe64(Phdr + 40);

        ROSV_DEBUG("  PHDR[%u]: type=%u flags=0x%X offset=0x%llX vaddr=0x%llX "
                   "paddr=0x%llX filesz=0x%llX memsz=0x%llX",
                   i, PType,
                   RosvReadLe32(Phdr + 4),
                   POffset,
                   RosvReadLe64(Phdr + 16),
                   PPaddr,
                   PFilesz,
                   PMemsz);

        if (PType != ROSV_PT_LOAD)
            continue;

        /* Validate file bounds */
        if (POffset + PFilesz > ImageSize)
        {
            ROSV_ERR("RosvKernelLoadElf64: PHDR[%u] file data extends beyond image "
                     "(offset=0x%llX + filesz=0x%llX > image_size=%u)",
                     i, POffset, PFilesz, ImageSize);
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        /* Validate guest memory bounds */
        if (PPaddr + PMemsz > Vm->GuestRamSize)
        {
            ROSV_ERR("RosvKernelLoadElf64: PHDR[%u] exceeds guest RAM "
                     "(paddr=0x%llX + memsz=0x%llX > ram_size=0x%llX)",
                     i, PPaddr, PMemsz, Vm->GuestRamSize);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Validate memsz >= filesz */
        if (PMemsz < PFilesz)
        {
            ROSV_ERR("RosvKernelLoadElf64: PHDR[%u] memsz 0x%llX < filesz 0x%llX",
                     i, PMemsz, PFilesz);
            return STATUS_INVALID_IMAGE_FORMAT;
        }

        /* Copy file data to guest physical memory */
        if (PFilesz > 0)
        {
            Status = RosvMemoryCopyToGpa(Vm, PPaddr, Base + POffset, (SIZE_T)PFilesz);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("RosvKernelLoadElf64: PHDR[%u] failed to copy %llu bytes "
                         "to GPA 0x%llX, Status=0x%08X",
                         i, PFilesz, PPaddr, Status);
                return Status;
            }
            ROSV_DEBUG("  PHDR[%u]: copied 0x%llX bytes to GPA 0x%llX",
                       i, PFilesz, PPaddr);
        }

        /* Zero BSS region (memsz > filesz) - page-aware loop since
         * GpaToHva returns per-page HVAs and BSS can span multiple
         * non-contiguous physical pages */
        if (PMemsz > PFilesz)
        {
            ULONG64 BssGpa = PPaddr + PFilesz;
            ULONG64 BssSize = PMemsz - PFilesz;
            ULONG64 BssRemaining = BssSize;
            ULONG64 CurrentGpa = BssGpa;

            ROSV_TRACE("Zeroing BSS: GPA 0x%llX, size 0x%llX", BssGpa, BssSize);

            while (BssRemaining > 0)
            {
                PVOID ChunkHva;
                ULONG64 PageOffset = CurrentGpa & (PAGE_SIZE - 1);
                ULONG64 ChunkSize = PAGE_SIZE - PageOffset;

                if (ChunkSize > BssRemaining)
                    ChunkSize = BssRemaining;

                ChunkHva = RosvMemoryGpaToHva(Vm, CurrentGpa);
                if (ChunkHva == NULL)
                {
                    ROSV_ERR("Failed to map GPA 0x%llX for BSS zeroing", CurrentGpa);
                    return STATUS_UNSUCCESSFUL;
                }

                RtlZeroMemory(ChunkHva, (SIZE_T)ChunkSize);

                CurrentGpa += ChunkSize;
                BssRemaining -= ChunkSize;
            }

            ROSV_DEBUG("  PHDR[%u]: zeroed 0x%llX bytes of BSS at GPA 0x%llX",
                       i, BssSize, BssGpa);
        }

        /* Track extent of loaded segments */
        if (PPaddr < LowestPaddr)
            LowestPaddr = PPaddr;
        if (PPaddr + PMemsz > HighestEnd)
            HighestEnd = PPaddr + PMemsz;
        FoundLoad = TRUE;
    }

    if (!FoundLoad)
    {
        ROSV_ERR("RosvKernelLoadElf64: no PT_LOAD segments found in %u program headers",
                 PhNum);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Set VM kernel metadata */
    Vm->KernelEntryPoint = Entry;
    Vm->KernelLoadAddress = LowestPaddr;
    Vm->KernelSize = HighestEnd - LowestPaddr;
    Vm->Is64BitKernel = TRUE;
    Vm->BootContract = RosvBootContractElf64Direct;

    ROSV_TRACE("RosvKernelLoadElf64: kernel loaded - entry=0x%llX load_addr=0x%llX "
               "size=0x%llX", Vm->KernelEntryPoint, Vm->KernelLoadAddress,
               Vm->KernelSize);

    /* Build boot params - ELF kernels still need E820 and command line */
    Vm->BootParamsAddress = LINUX_BOOT_PARAMS_ADDR;

    BootParamsHva = RosvMemoryGpaToHva(Vm, LINUX_BOOT_PARAMS_ADDR);
    if (BootParamsHva == NULL)
    {
        ROSV_ERR("RosvKernelLoadElf64: failed to map boot_params GPA 0x%X",
                 LINUX_BOOT_PARAMS_ADDR);
        return STATUS_INVALID_ADDRESS;
    }

    RtlZeroMemory(BootParamsHva, PAGE_SIZE);
    BootParams = (PLINUX_BOOT_PARAMS)BootParamsHva;

    /* E820 memory map */
    RosvBootParamsSetupE820(BootParams, Vm->GuestRamSize);
    ROSV_ASSERT((ROSV_ACPI_RSDP_GPA & 0xFULL) == 0, "ACPI RSDP must be 16-byte aligned");
    BootParams->AcpiRsdpAddr = ROSV_ACPI_RSDP_GPA;

    /* Command line */
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

    CmdLineHva = RosvMemoryGpaToHva(Vm, LINUX_CMDLINE_ADDR);
    if (CmdLineHva == NULL)
    {
        ROSV_ERR("RosvKernelLoadElf64: failed to map cmdline GPA 0x%X",
                 LINUX_CMDLINE_ADDR);
        return STATUS_INVALID_ADDRESS;
    }

    RtlCopyMemory(CmdLineHva, CmdLine, CmdLineLen);
    ((PCHAR)CmdLineHva)[CmdLineLen - 1] = '\0';

    BootParams->Hdr.CmdLinePtr = LINUX_CMDLINE_ADDR;
    BootParams->Hdr.CmdlineSize = CmdLineLen;
    BootParams->Hdr.Version = LINUX_MIN_PROTOCOL_VERSION;
    BootParams->Hdr.XloadFlags |= LINUX_XLF_KERNEL_64;
    BootParams->Hdr.TypeOfLoader = ROSV_BOOT_LOADER_TYPE;

    ROSV_TRACE("RosvKernelLoadElf64: boot_params at GPA 0x%X - "
               "e820_entries=%u acpi_rsdp=0x%llX cmdline=0x%X version=0x%04X xloadflags=0x%04X",
               LINUX_BOOT_PARAMS_ADDR, BootParams->E820Entries,
               BootParams->AcpiRsdpAddr,
               BootParams->Hdr.CmdLinePtr, BootParams->Hdr.Version,
               BootParams->Hdr.XloadFlags);

    ROSV_TRACE("RosvKernelLoadElf64: SUCCESS - ELF64 kernel entry=0x%llX "
               "load=[0x%llX-0x%llX] boot_contract=Elf64Direct",
               Vm->KernelEntryPoint, Vm->KernelLoadAddress,
               Vm->KernelLoadAddress + Vm->KernelSize);

    return STATUS_SUCCESS;
}

static NTSTATUS
RosvKernelLoadPe64(
    _In_reads_bytes_(ImageSize) PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Inout_ PROSV_VM Vm)
{
    UNREFERENCED_PARAMETER(Vm);

    if (!RosvLooksLikePe64((const UCHAR *)ImageBuffer, ImageSize))
        return STATUS_INVALID_IMAGE_FORMAT;

    ROSV_ERR("RosvKernelLoadPe64: PE64 kernel detected, but PE boot path is not implemented yet");
    return STATUS_NOT_SUPPORTED;
}

PCSTR
RosvKernelImageTypeName(
    _In_ ROSV_KERNEL_IMAGE_TYPE Type)
{
    switch (Type)
    {
        case RosvKernelImageBzImage:
            return "bzImage";
        case RosvKernelImageElf64:
            return "ELF64";
        case RosvKernelImagePe64:
            return "PE64";
        default:
            return "unknown";
    }
}

ROSV_KERNEL_IMAGE_TYPE
RosvKernelImageDetect(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize)
{
    const UCHAR *Base = (const UCHAR *)ImageBuffer;

    if (Base == NULL || ImageSize < 2)
        return RosvKernelImageUnknown;

    if (RosvLooksLikeBzImage(Base, ImageSize))
        return RosvKernelImageBzImage;

    if (RosvLooksLikeElf64(Base, ImageSize))
        return RosvKernelImageElf64;

    if (RosvLooksLikePe64(Base, ImageSize))
        return RosvKernelImagePe64;

    return RosvKernelImageUnknown;
}

NTSTATUS
RosvKernelLoad(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Inout_ PROSV_VM Vm,
    _Out_opt_ ROSV_KERNEL_IMAGE_TYPE *DetectedType)
{
    ROSV_KERNEL_IMAGE_TYPE Type;

    if (ImageBuffer == NULL || ImageSize == 0 || Vm == NULL)
        return STATUS_INVALID_PARAMETER;

    Type = RosvKernelImageDetect(ImageBuffer, ImageSize);
    if (DetectedType != NULL)
        *DetectedType = Type;

    ROSV_TRACE("RosvKernelLoad: detected image type %s, size=%u bytes",
               RosvKernelImageTypeName(Type), ImageSize);

    switch (Type)
    {
        case RosvKernelImageBzImage:
            return RosvBzImageLoad(ImageBuffer, ImageSize, Vm);

        case RosvKernelImageElf64:
            return RosvKernelLoadElf64(ImageBuffer, ImageSize, Vm);

        case RosvKernelImagePe64:
            return RosvKernelLoadPe64(ImageBuffer, ImageSize, Vm);

        default:
            ROSV_ERR("RosvKernelLoad: unknown kernel image format");
            return STATUS_INVALID_IMAGE_FORMAT;
    }
}
