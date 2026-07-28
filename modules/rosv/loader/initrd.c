/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     initrd loader - places initrd in guest memory
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 *
 * Loads an initrd/initramfs image into guest physical memory, placed
 * below InitrdAddrMax and below guest RAM top, page-aligned.
 * Updates boot_params with the initrd location and size.
 */

#include <rosv/rosv.h>
#include <rosv/loader.h>
#include <rosv/vm.h>

static VOID
RosvInitrdResetStream(
    _Inout_ PROSV_VM Vm)
{
    Vm->InitrdStreamActive = FALSE;
    Vm->InitrdStreamAddress = 0;
    Vm->InitrdStreamExpected = 0;
    Vm->InitrdStreamReceived = 0;
}

static NTSTATUS
RosvInitrdComputePlacement(
    _In_ ULONG64 InitrdSize,
    _Inout_ PROSV_VM Vm,
    _Out_ PULONG64 InitrdGpaOut)
{
    PLINUX_BOOT_PARAMS BootParams;
    PVOID BootParamsHva;
    ULONG64 InitrdAddrMax;
    ULONG64 RamTop;
    ULONG64 InitrdSizeAligned;
    ULONG64 InitrdGpa;

    if (Vm->BootParamsAddress == 0)
    {
        ROSV_ERR("RosvInitrd: boot_params not initialized");
        return STATUS_INVALID_DEVICE_STATE;
    }

    BootParamsHva = RosvMemoryGpaToHva(Vm, Vm->BootParamsAddress);
    if (BootParamsHva == NULL)
    {
        ROSV_ERR("RosvInitrd: failed to map boot_params GPA 0x%llX",
                 Vm->BootParamsAddress);
        return STATUS_INVALID_ADDRESS;
    }
    BootParams = (PLINUX_BOOT_PARAMS)BootParamsHva;

    InitrdAddrMax = BootParams->Hdr.InitrdAddrMax;
    if (InitrdAddrMax == 0)
        InitrdAddrMax = LINUX_INITRD_ADDR_MAX;

    RamTop = Vm->GuestRamSize;
    if (RamTop > InitrdAddrMax + 1)
        RamTop = InitrdAddrMax + 1;

    InitrdSizeAligned = (InitrdSize + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (InitrdSizeAligned > RamTop)
    {
        ROSV_ERR("RosvInitrd: size 0x%llX exceeds available RAM 0x%llX",
                 InitrdSizeAligned, RamTop);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    InitrdGpa = (RamTop - InitrdSizeAligned) & ~(PAGE_SIZE - 1);
    if (InitrdGpa < Vm->KernelLoadAddress + Vm->KernelSize)
    {
        ROSV_ERR("RosvInitrd: initrd at 0x%llX overlaps kernel at 0x%llX+0x%llX",
                 InitrdGpa, Vm->KernelLoadAddress, Vm->KernelSize);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *InitrdGpaOut = InitrdGpa;
    return STATUS_SUCCESS;
}

NTSTATUS
RosvInitrdBegin(
    _In_ ULONG64 InitrdSize,
    _Inout_ PROSV_VM Vm)
{
    NTSTATUS Status;
    ULONG64 InitrdGpa;

    if (Vm == NULL || InitrdSize == 0)
        return STATUS_INVALID_PARAMETER;

    if (InitrdSize > MAXULONG)
    {
        ROSV_ERR("RosvInitrdBegin: initrd size %llu exceeds boot_params 32-bit limit",
                 InitrdSize);
        return STATUS_INVALID_PARAMETER;
    }

    if (Vm->InitrdStreamActive)
    {
        ROSV_ERR("RosvInitrdBegin: previous initrd stream still active");
        return STATUS_DEVICE_BUSY;
    }

    Status = RosvInitrdComputePlacement(InitrdSize, Vm, &InitrdGpa);
    if (!NT_SUCCESS(Status))
        return Status;

    Vm->InitrdStreamActive = TRUE;
    Vm->InitrdStreamAddress = InitrdGpa;
    Vm->InitrdStreamExpected = InitrdSize;
    Vm->InitrdStreamReceived = 0;

    ROSV_TRACE("RosvInitrdBegin: stream initialized size=%llu target_gpa=0x%llX",
               InitrdSize, InitrdGpa);
    return STATUS_SUCCESS;
}

NTSTATUS
RosvInitrdAppend(
    _In_ ULONG64 Offset,
    _In_ PVOID ChunkBuffer,
    _In_ ULONG ChunkSize,
    _Inout_ PROSV_VM Vm)
{
    NTSTATUS Status;

    if (Vm == NULL || ChunkBuffer == NULL || ChunkSize == 0)
        return STATUS_INVALID_PARAMETER;

    if (!Vm->InitrdStreamActive)
    {
        ROSV_ERR("RosvInitrdAppend: initrd stream is not active");
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Offset != Vm->InitrdStreamReceived)
    {
        ROSV_ERR("RosvInitrdAppend: non-sequential offset=%llu expected=%llu",
                 Offset, Vm->InitrdStreamReceived);
        return STATUS_INVALID_PARAMETER;
    }

    if (Offset + ChunkSize > Vm->InitrdStreamExpected)
    {
        ROSV_ERR("RosvInitrdAppend: chunk overruns stream (%llu + %u > %llu)",
                 Offset, ChunkSize, Vm->InitrdStreamExpected);
        return STATUS_INVALID_PARAMETER;
    }

    Status = RosvMemoryCopyToGpa(Vm,
                                 Vm->InitrdStreamAddress + Offset,
                                 ChunkBuffer,
                                 ChunkSize);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("RosvInitrdAppend: copy failed at offset=%llu size=%u, Status=0x%08X",
                 Offset, ChunkSize, Status);
        return Status;
    }

    Vm->InitrdStreamReceived += ChunkSize;

    if (Vm->InitrdStreamReceived == Vm->InitrdStreamExpected ||
        ((Vm->InitrdStreamReceived & ((16 * 1024 * 1024) - 1)) == 0))
    {
        ROSV_TRACE("RosvInitrdAppend: progress %llu/%llu bytes",
                   Vm->InitrdStreamReceived, Vm->InitrdStreamExpected);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
RosvInitrdCommit(
    _Inout_ PROSV_VM Vm)
{
    PLINUX_BOOT_PARAMS BootParams;
    PVOID BootParamsHva;

    if (Vm == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!Vm->InitrdStreamActive)
    {
        ROSV_ERR("RosvInitrdCommit: initrd stream is not active");
        return STATUS_INVALID_DEVICE_STATE;
    }

    if (Vm->InitrdStreamReceived != Vm->InitrdStreamExpected)
    {
        ROSV_ERR("RosvInitrdCommit: incomplete stream (%llu/%llu bytes)",
                 Vm->InitrdStreamReceived, Vm->InitrdStreamExpected);
        return STATUS_INVALID_DEVICE_STATE;
    }

    BootParamsHva = RosvMemoryGpaToHva(Vm, Vm->BootParamsAddress);
    if (BootParamsHva == NULL)
    {
        ROSV_ERR("RosvInitrdCommit: failed to map boot_params GPA 0x%llX",
                 Vm->BootParamsAddress);
        return STATUS_INVALID_ADDRESS;
    }
    BootParams = (PLINUX_BOOT_PARAMS)BootParamsHva;

    BootParams->Hdr.RamdiskImage = (ULONG)Vm->InitrdStreamAddress;
    BootParams->Hdr.RamdiskSize = (ULONG)Vm->InitrdStreamExpected;

    Vm->InitrdLoadAddress = Vm->InitrdStreamAddress;
    Vm->InitrdSize = Vm->InitrdStreamExpected;

    ROSV_TRACE("RosvInitrdCommit: initrd loaded at GPA 0x%llX, size %llu bytes",
               Vm->InitrdLoadAddress, Vm->InitrdSize);

    RosvInitrdResetStream(Vm);
    return STATUS_SUCCESS;
}

/*
 * RosvInitrdLoad - Load an initrd image into guest memory
 *
 * Placement strategy:
 *   - Place at the highest page-aligned address that fits below both
 *     the guest RAM top and the kernel's InitrdAddrMax.
 *   - Update boot_params RamdiskImage/RamdiskSize fields.
 */
NTSTATUS
RosvInitrdLoad(
    _In_ PVOID InitrdBuffer,
    _In_ ULONG InitrdSize,
    _Inout_ PROSV_VM Vm)
{
    NTSTATUS Status;

    ROSV_TRACE("RosvInitrdLoad: loading initrd, size=%u bytes", InitrdSize);

    if (InitrdBuffer == NULL || InitrdSize == 0)
    {
        ROSV_ERR("RosvInitrdLoad: invalid initrd buffer=%p size=%u",
                 InitrdBuffer, InitrdSize);
        return STATUS_INVALID_PARAMETER;
    }

    Status = RosvInitrdBegin(InitrdSize, Vm);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RosvInitrdAppend(0, InitrdBuffer, InitrdSize, Vm);
    if (!NT_SUCCESS(Status))
    {
        RosvInitrdResetStream(Vm);
        return Status;
    }

    Status = RosvInitrdCommit(Vm);
    if (!NT_SUCCESS(Status))
    {
        RosvInitrdResetStream(Vm);
        return Status;
    }

    return STATUS_SUCCESS;
}
