/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     bzImage parser and loader
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 *
 * Implements the Linux x86 boot protocol for loading bzImage kernels
 * into guest physical memory. References:
 *   - Documentation/x86/boot.rst in Linux source
 *   - arch/x86/include/uapi/asm/bootparam.h
 */

#include <rosv/rosv.h>
#include <rosv/loader.h>
#include <rosv/vm.h>

/*
 * RosvBzImageValidate - Validate a bzImage and extract setup header
 *
 * Checks the boot sector magic, setup header signature, and protocol
 * version. Copies the setup_header to the caller-provided buffer.
 */
NTSTATUS
RosvBzImageValidate(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Out_ PLINUX_SETUP_HEADER Header)
{
    PUCHAR Base = (PUCHAR)ImageBuffer;
    PLINUX_SETUP_HEADER SetupHdr;
    UCHAR SetupSects;
    ULONG SetupSize;

    if (ImageBuffer == NULL || Header == NULL)
    {
        ROSV_ERR("RosvBzImageValidate: invalid parameters (ImageBuffer=%p Header=%p)",
                 ImageBuffer, Header);
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Header, sizeof(*Header));

    /* Need at least the boot sector + 1 setup sector */
    if (ImageSize < 1024)
    {
        ROSV_ERR("bzImage too small: %u bytes (need >= 1024)", ImageSize);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Check boot sector magic at offset 0x1FE */
    if (*(PUSHORT)(Base + 0x1FE) != LINUX_BOOT_FLAG_MAGIC)
    {
        ROSV_ERR("Invalid boot flag: 0x%04X (expected 0xAA55)",
                 *(PUSHORT)(Base + 0x1FE));
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Setup header starts at offset 0x1F1 in the boot sector */
    SetupHdr = (PLINUX_SETUP_HEADER)(Base + 0x1F1);

    /* Check "HdrS" magic at offset 0x202 */
    if (SetupHdr->Header != LINUX_BZIMAGE_MAGIC)
    {
        ROSV_ERR("Missing HdrS signature: 0x%08X (expected 0x%08X)",
                 SetupHdr->Header, LINUX_BZIMAGE_MAGIC);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Check protocol version */
    if (SetupHdr->Version < LINUX_MIN_PROTOCOL_VERSION)
    {
        ROSV_ERR("Boot protocol version 0x%04X too old (need >= 0x%04X)",
                 SetupHdr->Version, LINUX_MIN_PROTOCOL_VERSION);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    /* Validate setup sectors count */
    SetupSects = SetupHdr->SetupSects;
    if (SetupSects == 0)
        SetupSects = 4; /* Default per boot protocol */

    SetupSize = (SetupSects + 1) * 512;
    if (ImageSize < SetupSize)
    {
        ROSV_ERR("Image size %u < required setup size %u (setup_sects=%u)",
                 ImageSize, SetupSize, SetupSects);
        return STATUS_INVALID_IMAGE_FORMAT;
    }

    ROSV_TRACE("bzImage validated: protocol=0x%04X setup_sects=%u "
               "syssize=0x%X loadflags=0x%02X xloadflags=0x%04X",
               SetupHdr->Version, SetupSects,
               SetupHdr->SysSize, SetupHdr->LoadFlags,
               SetupHdr->XloadFlags);

    if (SetupHdr->Version >= 0x020F)
    {
        ROSV_TRACE("  kernel_alignment=0x%X relocatable=%u "
                   "pref_address=0x%llX init_size=0x%X",
                   SetupHdr->KernelAlignment,
                   SetupHdr->RelocatableKernel,
                   SetupHdr->PrefAddress,
                   SetupHdr->InitSize);
    }

    /* Copy the header out */
    RtlCopyMemory(Header, SetupHdr, sizeof(*Header));
    return STATUS_SUCCESS;
}

/*
 * RosvBzImageLoad - Parse and load a bzImage into guest memory
 *
 * Steps:
 *  1. Validate the image header
 *  2. Calculate kernel offset and size
 *  3. Determine load address (PrefAddress or default 0x100000)
 *  4. Copy protected-mode kernel to guest memory via GPA->HVA mapping
 *  5. Store kernel metadata in VM context
 */
NTSTATUS
RosvBzImageLoad(
    _In_ PVOID ImageBuffer,
    _In_ ULONG ImageSize,
    _Inout_ PROSV_VM Vm)
{
    NTSTATUS Status;
    LINUX_SETUP_HEADER Header;
    PUCHAR Base = (PUCHAR)ImageBuffer;
    UCHAR SetupSects;
    ULONG SetupSize;
    ULONG KernelSize;
    ULONG64 LoadAddr;
    BOOLEAN Is64Bit;

    if (ImageBuffer == NULL || Vm == NULL)
    {
        ROSV_ERR("RosvBzImageLoad: invalid parameters (ImageBuffer=%p Vm=%p)",
                 ImageBuffer, Vm);
        return STATUS_INVALID_PARAMETER;
    }

    ROSV_TRACE("RosvBzImageLoad: loading bzImage, size=%u bytes", ImageSize);

    /* Step 1: Validate */
    Status = RosvBzImageValidate(ImageBuffer, ImageSize, &Header);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvBzImageLoad: validation failed, status=0x%08X", Status);
        return Status;
    }

    /* Step 2: Calculate kernel offset and size */
    SetupSects = Header.SetupSects;
    if (SetupSects == 0)
        SetupSects = 4;

    SetupSize = (SetupSects + 1) * 512;
    KernelSize = ImageSize - SetupSize;

    ROSV_TRACE("RosvBzImageLoad: setup_size=%u kernel_size=%u",
               SetupSize, KernelSize);

    /* Step 3: Determine load address */
    if (Header.Version >= 0x020F && Header.RelocatableKernel && Header.PrefAddress != 0)
    {
        LoadAddr = Header.PrefAddress;
        ROSV_TRACE("RosvBzImageLoad: using preferred address 0x%llX", LoadAddr);
    }
    else
    {
        LoadAddr = LINUX_KERNEL_LOAD_ADDR;
        ROSV_TRACE("RosvBzImageLoad: using default address 0x%llX", LoadAddr);
    }

    /* Verify load address is within guest RAM */
    if (LoadAddr + KernelSize > Vm->GuestRamSize)
    {
        ROSV_ERR("RosvBzImageLoad: kernel at 0x%llX + 0x%X exceeds guest RAM 0x%llX",
                 LoadAddr, KernelSize, Vm->GuestRamSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Step 4: Copy protected-mode kernel to guest memory (chunk-aware) */
    Status = RosvMemoryCopyToGpa(Vm, LoadAddr, Base + SetupSize, KernelSize);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvBzImageLoad: failed to copy kernel to GPA 0x%llX, Status=0x%08X",
                 LoadAddr, Status);
        return Status;
    }
    ROSV_TRACE("RosvBzImageLoad: copied %u bytes of kernel to GPA 0x%llX",
               KernelSize, LoadAddr);

    /* Step 5: Store kernel info and setup header in VM context */
    Vm->KernelLoadAddress = LoadAddr;
    Vm->KernelSize = KernelSize;

    /* Check if this is a 64-bit kernel */
    Is64Bit = (Header.XloadFlags & LINUX_XLF_KERNEL_64) != 0;
    Vm->Is64BitKernel = Is64Bit;

    /* Calculate entry point */
    if (Is64Bit && Header.Version >= 0x020F)
    {
        /* 64-bit entry is at kernel_load_addr + 0x200 */
        Vm->KernelEntryPoint = LoadAddr + LINUX_64BIT_ENTRY_OFFSET;
        ROSV_TRACE("RosvBzImageLoad: 64-bit kernel, entry=0x%llX",
                   Vm->KernelEntryPoint);
    }
    else
    {
        /* 32-bit entry is at Code32Start or default 0x100000 */
        Vm->KernelEntryPoint = Header.Code32Start ? Header.Code32Start : 0x100000;
        ROSV_TRACE("RosvBzImageLoad: 32-bit kernel, entry=0x%llX",
                   Vm->KernelEntryPoint);
    }

    /* Set boot contract and params address */
    Vm->BootContract = RosvBootContractBzImage;
    Vm->BootParamsAddress = LINUX_BOOT_PARAMS_ADDR;

    /* Now build boot_params with the original header */
    /* Set up our loader fields before building boot params */
    Header.TypeOfLoader = ROSV_BOOT_LOADER_TYPE;
    Header.LoadFlags |= LINUX_LOADFLAG_LOADED_HIGH;
    Header.HeapEndPtr = LINUX_HEAP_END_OFFSET;
    Header.LoadFlags |= LINUX_LOADFLAG_CAN_USE_HEAP;

    Status = RosvBootParamsBuild(Vm, &Header);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvBzImageLoad: failed to build boot params, status=0x%08X", Status);
        return Status;
    }

    ROSV_TRACE("RosvBzImageLoad: SUCCESS - kernel at GPA 0x%llX, entry 0x%llX, "
               "boot_params at GPA 0x%llX, %s mode",
               Vm->KernelLoadAddress, Vm->KernelEntryPoint,
               Vm->BootParamsAddress, Is64Bit ? "64-bit" : "32-bit");

    return STATUS_SUCCESS;
}
