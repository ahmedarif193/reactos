/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Driver entry point and IOCTL dispatch
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>
#include <rosv/vmx.h>
#include <rosv/loader.h>
#include <rosv/device.h>
#include <rosv/pty.h>
#include <rosv/virtio_console.h>
#include <rosv/net_backend.h>

/* ---- Forward declarations (vm/session.c) -------------------------------- */

NTSTATUS
RosvSessionCreate(
    _In_ PROSV_SESSION_CREATE_REQUEST Request,
    _Out_ PROSV_SESSION_CREATE_RESULT Result);

NTSTATUS
RosvSessionStart(
    _In_ ULONG SessionId);

NTSTATUS
RosvSessionStop(
    _In_ ULONG SessionId);

NTSTATUS
RosvSessionDestroy(
    _In_ ULONG SessionId);

NTSTATUS
RosvSessionAttach(
    _In_ ULONG SessionId,
    _Out_ PROSV_SESSION_INFO Info);

NTSTATUS
RosvSessionDetach(
    _In_ ULONG SessionId);

NTSTATUS
RosvSessionList(
    _Out_ PROSV_SESSION_LIST_RESULT Result,
    _In_ ULONG MaxOutputSize,
    _Out_ PULONG OutputBytes);

VOID
RosvSessionCleanupAll(VOID);

/* ---- Forward declarations (device/fs9p.c) ------------------------------- */

NTSTATUS
RosvFsMountAdd(
    _In_ PROSV_FS_MOUNT_REQUEST Request);

NTSTATUS
RosvFsMountRemove(
    _In_ PROSV_FS_MOUNT_REQUEST Request);

NTSTATUS
RosvFsMountList(
    _Out_ PROSV_FS_MOUNT_LIST Result,
    _In_ ULONG MaxOutputSize,
    _Out_ PULONG OutputBytes);

NTSTATUS
RosvNetForwardAdd(
    _In_ PROSV_NET_FORWARD_REQUEST Request);

NTSTATUS
RosvNetForwardRemove(
    _In_ PROSV_NET_FORWARD_REQUEST Request);

/* ---- Global state ------------------------------------------------------- */

static PROSV_VM g_CurrentVm = NULL;
static PDEVICE_OBJECT g_DeviceObject = NULL;
static VMX_CAPABILITIES g_VmxCaps;

typedef struct _ROSV_NET_STUB_STATE {
    BOOLEAN Attached;
    ULONG BackendType;
    ULONG64 TxPackets;
    ULONG64 TxBytes;
    ULONG64 RxPackets;
    ULONG64 RxBytes;
} ROSV_NET_STUB_STATE;
/* Current implementation uses process-global network backend state. */

static ROSV_NET_STUB_STATE g_NetStub;

/* Disk image state (memory-mapped file for virtio-blk backend) */
static HANDLE g_DiskFileHandle = NULL;
static HANDLE g_DiskFileHandleAlt = NULL;
static HANDLE g_DiskSectionHandle = NULL;
static PVOID  g_DiskMappedBase = NULL;
static SIZE_T g_DiskMappedSize = 0;
static KMUTEX g_StateMutex;

/*
 * Select demand-paged VHDX payload I/O as the default data path.
 * Mapped payload reads remain disabled for this configuration.
 */
#define ROSV_VHDX_USE_DEMAND_PAGED_IO 1

static VOID
RosvDetachDiskInternal(
    _In_opt_ PROSV_VM Vm)
{
    if (Vm != NULL)
    {
        ROSV_TRACE("Disk: detaching, mode=%s, backend=%s",
                   (Vm->VirtioBlk.Mode == ROSV_DISK_MODE_DEMAND_PAGED) ?
                       "demand-paged" : "ramdisk",
                   (Vm->VirtioBlk.BackendType == ROSV_DISK_BACKEND_VHDX) ?
                       "VHDX" : "RAW");

        if (Vm->VirtioBlk.BackendType == ROSV_DISK_BACKEND_VHDX)
        {
            /* In demand-paged mode, BatBase is a standalone allocation that
             * we must free before RosvVhdxClose (which would try to use it) */
            if (Vm->VirtioBlk.Mode == ROSV_DISK_MODE_DEMAND_PAGED &&
                Vm->VhdxState.BatBase != NULL)
            {
                ROSV_TRACE("Disk: freeing demand-paged BAT allocation at %p",
                           Vm->VhdxState.BatBase);
                ExFreePoolWithTag(Vm->VhdxState.BatBase, ROSV_DRIVER_TAG);
                Vm->VhdxState.BatBase = NULL;
            }
            RosvVhdxClose(&Vm->VhdxState);
            ROSV_TRACE("Disk: VHDX state closed");
        }

        /* Clear file handle from VirtioBlk state (the global handle is closed below) */
        Vm->VirtioBlk.DiskFileHandle = NULL;
        Vm->VirtioBlk.DiskFileHandleAlt = NULL;
        RosvVirtioBlkDestroy(&Vm->VirtioBlk);
        ROSV_TRACE("Disk: Virtio-blk device destroyed");

        /* Destroy virtio-net device */
        RosvVirtioNetDestroy(&Vm->VirtioNet);
        ROSV_TRACE("Net: Virtio-net device destroyed");
    }

    /* Ramdisk mode cleanup: unmap view and close section */
    if (g_DiskMappedBase != NULL)
    {
        MmUnmapViewInSystemSpace(g_DiskMappedBase);
        g_DiskMappedBase = NULL;
        g_DiskMappedSize = 0;
        ROSV_TRACE("Disk: view unmapped");
    }

    if (g_DiskSectionHandle != NULL)
    {
        ZwClose(g_DiskSectionHandle);
        g_DiskSectionHandle = NULL;
        ROSV_TRACE("Disk: section handle closed");
    }

    /* File handle is used by both modes */
    if (g_DiskFileHandle != NULL)
    {
        ZwClose(g_DiskFileHandle);
        g_DiskFileHandle = NULL;
        ROSV_TRACE("Disk: file handle closed");
    }
    if (g_DiskFileHandleAlt != NULL)
    {
        ZwClose(g_DiskFileHandleAlt);
        g_DiskFileHandleAlt = NULL;
        ROSV_TRACE("Disk: alt file handle closed");
    }
}

/* ---- IRP_MJ_CREATE ------------------------------------------------------ */

static NTSTATUS
RosvDispatchCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    ROSV_TRACE("Device opened");

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ---- IRP_MJ_CLOSE ------------------------------------------------------- */

static NTSTATUS
RosvDispatchClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    ROSV_TRACE("Device closed");

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ---- IRP_MJ_CLEANUP ----------------------------------------------------- */

static NTSTATUS
RosvDispatchCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    NTSTATUS WaitStatus;

    UNREFERENCED_PARAMETER(DeviceObject);

    WaitStatus = KeWaitForSingleObject(&g_StateMutex,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    if (!NT_SUCCESS(WaitStatus))
    {
        ROSV_ERR("Cleanup: failed to acquire state mutex, Status=0x%08X", WaitStatus);
        Irp->IoStatus.Status = WaitStatus;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return WaitStatus;
    }

    ROSV_TRACE("Device cleanup - handle closed, VM continues running");

    /* Detach from any attached sessions */
    RosvSessionCleanupAll();

    /* VM and disk persist across handle close — multiple rosl instances
     * can connect to the same running VM.  Only an explicit DESTROY_VM
     * IOCTL (or driver unload) stops the VM and frees resources. */

    KeReleaseMutex(&g_StateMutex, FALSE);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ---- IOCTL Dispatch ----------------------------------------------------- */

static NTSTATUS
RosvDispatchIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS WaitStatus;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG IoControlCode;
    PVOID InputBuffer;
    PVOID OutputBuffer;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG OutputBytes = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    IoControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;
    InputBuffer = Irp->AssociatedIrp.SystemBuffer;
    OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
    InputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    /* NET_TX and NET_RX are hot-path IOCTLs — skip global mutex.
     * They use per-device spinlocks (TxRingLock, RxLock) internally. */
    if (IoControlCode == ROSV_IOCTL_NET_TX)
        goto NetFastPathTx;
    if (IoControlCode == ROSV_IOCTL_NET_RX)
        goto NetFastPathRx;

    WaitStatus = KeWaitForSingleObject(&g_StateMutex,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    if (!NT_SUCCESS(WaitStatus))
    {
        ROSV_ERR("IOCTL dispatch: failed to acquire state mutex, Status=0x%08X", WaitStatus);
        Irp->IoStatus.Status = WaitStatus;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return WaitStatus;
    }

    if (IoControlCode != ROSV_IOCTL_CONSOLE_READ &&
        IoControlCode != ROSV_IOCTL_NET_RX &&
        IoControlCode != ROSV_IOCTL_NET_GET_STATS &&
        IoControlCode != ROSV_IOCTL_GET_STATE &&
        IoControlCode != ROSV_IOCTL_GET_VM_STATS &&
        IoControlCode != ROSV_IOCTL_PTY_READ &&
        IoControlCode != ROSV_IOCTL_INITRD_CHUNK)
    {
        ROSV_DEBUG("IOCTL dispatch: code=0x%08X inLen=%u outLen=%u",
                   IoControlCode, InputLength, OutputLength);
    }

    switch (IoControlCode)
    {
        /* ---- CREATE_VM -------------------------------------------------- */
        case ROSV_IOCTL_CREATE_VM:
        {
            PROSV_VM_CONFIG Config;
            PROSV_VM_CREATE_RESULT Result;

            ROSV_TRACE("IOCTL: CREATE_VM");

            if (g_CurrentVm != NULL)
            {
                ROSV_ERR("CREATE_VM: A VM already exists (single-VM limit)");
                Status = STATUS_DEVICE_BUSY;
                break;
            }

            if (InputLength < sizeof(ROSV_VM_CONFIG))
            {
                ROSV_ERR("CREATE_VM: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_VM_CONFIG));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_VM_CREATE_RESULT))
            {
                ROSV_ERR("CREATE_VM: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_VM_CREATE_RESULT));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Config = (PROSV_VM_CONFIG)InputBuffer;
            ROSV_TRACE("CREATE_VM: RamSizeMB=%u NetBackend=%u", Config->RamSizeMB, Config->NetBackendType);

            Status = RosvVmCreate(Config, &g_CurrentVm);

            Result = (PROSV_VM_CREATE_RESULT)OutputBuffer;
            Result->Status = Status;
            Result->VmId = NT_SUCCESS(Status) ? g_CurrentVm->VmId : 0;
            OutputBytes = sizeof(ROSV_VM_CREATE_RESULT);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("CREATE_VM: VM created, VmId=%u", Result->VmId);
            }
            else
            {
                ROSV_ERR("CREATE_VM: RosvVmCreate failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- SET_MEMORY ------------------------------------------------- */
        case ROSV_IOCTL_SET_MEMORY:
        {
            ROSV_TRACE("IOCTL: SET_MEMORY");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("SET_MEMORY: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State != RosvVmStateCreated)
            {
                ROSV_ERR("SET_MEMORY: Invalid state %d (expected Created)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            /* Allow caller to override RAM size (for retry with less RAM) */
            if (InputLength >= sizeof(ROSV_VM_CONFIG))
            {
                PROSV_VM_CONFIG NewCfg = (PROSV_VM_CONFIG)Irp->AssociatedIrp.SystemBuffer;
                if (NewCfg->RamSizeMB > 0)
                {
                    if (NewCfg->RamSizeMB < 64 || NewCfg->RamSizeMB > 4096)
                    {
                        ROSV_ERR("SET_MEMORY: Invalid RamSizeMB=%u (must be 64..4096)",
                                 NewCfg->RamSizeMB);
                        Status = STATUS_INVALID_PARAMETER;
                        break;
                    }

                    if (NewCfg->RamSizeMB != g_CurrentVm->Config.RamSizeMB)
                    {
                        ROSV_TRACE("SET_MEMORY: Overriding RAM from %u MB to %u MB",
                                   g_CurrentVm->Config.RamSizeMB, NewCfg->RamSizeMB);
                        g_CurrentVm->Config.RamSizeMB = NewCfg->RamSizeMB;
                    }
                }
            }

            Status = RosvVmSetMemory(g_CurrentVm);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("SET_MEMORY: Memory set to %u MB",
                           g_CurrentVm->Config.RamSizeMB);
            }
            else
            {
                ROSV_ERR("SET_MEMORY: RosvVmSetMemory failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- LOAD_KERNEL ------------------------------------------------ */
        case ROSV_IOCTL_LOAD_KERNEL:
        {
            ROSV_KERNEL_IMAGE_TYPE ImageType;

            ROSV_TRACE("IOCTL: LOAD_KERNEL, inputLen=%u", InputLength);

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("LOAD_KERNEL: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State < RosvVmStateMemorySet)
            {
                ROSV_ERR("LOAD_KERNEL: Invalid state %d (need >= MemorySet)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength == 0 || InputBuffer == NULL)
            {
                ROSV_ERR("LOAD_KERNEL: No kernel data provided");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            ImageType = RosvKernelImageUnknown;
            Status = RosvKernelLoad(InputBuffer, InputLength, g_CurrentVm, &ImageType);

            if (NT_SUCCESS(Status))
            {
                g_CurrentVm->State = RosvVmStateKernelLoaded;
                g_CurrentVm->InitrdLoadAddress = 0;
                g_CurrentVm->InitrdSize = 0;
                g_CurrentVm->InitrdStreamActive = FALSE;
                g_CurrentVm->InitrdStreamAddress = 0;
                g_CurrentVm->InitrdStreamExpected = 0;
                g_CurrentVm->InitrdStreamReceived = 0;
                RosvVmSetCheckpoint(g_CurrentVm, RosvCpKernelLoaded);
                ROSV_TRACE("LOAD_KERNEL: %s kernel loaded, %u bytes, entry=0x%llX",
                           RosvKernelImageTypeName(ImageType),
                           InputLength,
                           g_CurrentVm->KernelEntryPoint);
            }
            else
            {
                ROSV_ERR("LOAD_KERNEL: %s kernel load failed, Status=0x%08X",
                         RosvKernelImageTypeName(ImageType),
                         Status);
            }
            break;
        }

        /* ---- LOAD_INITRD ------------------------------------------------ */
        case ROSV_IOCTL_LOAD_INITRD:
        {
            ROSV_TRACE("IOCTL: LOAD_INITRD, inputLen=%u", InputLength);

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("LOAD_INITRD: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State < RosvVmStateKernelLoaded)
            {
                ROSV_ERR("LOAD_INITRD: Invalid state %d (need >= KernelLoaded)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength == 0 || InputBuffer == NULL)
            {
                ROSV_ERR("LOAD_INITRD: No initrd data provided");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvInitrdLoad(InputBuffer, InputLength, g_CurrentVm);

            if (NT_SUCCESS(Status))
            {
                RosvVmSetCheckpoint(g_CurrentVm, RosvCpInitrdLoaded);
                ROSV_TRACE("LOAD_INITRD: Initrd loaded, %u bytes at GPA=0x%llX",
                           InputLength, g_CurrentVm->InitrdLoadAddress);
            }
            else
            {
                ROSV_ERR("LOAD_INITRD: RosvInitrdLoad failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- INITRD_BEGIN (chunked path) -------------------------------- */
        case ROSV_IOCTL_INITRD_BEGIN:
        {
            PROSV_INITRD_BEGIN_REQUEST Request;

            ROSV_TRACE("IOCTL: INITRD_BEGIN, inputLen=%u", InputLength);

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("INITRD_BEGIN: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State < RosvVmStateKernelLoaded)
            {
                ROSV_ERR("INITRD_BEGIN: Invalid state %d (need >= KernelLoaded)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_INITRD_BEGIN_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("INITRD_BEGIN: Invalid input buffer");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Request = (PROSV_INITRD_BEGIN_REQUEST)InputBuffer;
            if (Request->Flags != 0 || Request->TotalSize == 0)
            {
                ROSV_ERR("INITRD_BEGIN: Invalid flags=%u or size=%llu",
                         Request->Flags, Request->TotalSize);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvInitrdBegin(Request->TotalSize, g_CurrentVm);
            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("INITRD_BEGIN: stream ready for %llu bytes",
                           Request->TotalSize);
            }
            else
            {
                ROSV_ERR("INITRD_BEGIN: RosvInitrdBegin failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- INITRD_CHUNK (chunked path) -------------------------------- */
        case ROSV_IOCTL_INITRD_CHUNK:
        {
            PROSV_INITRD_CHUNK_REQUEST Request;
            ULONG HeaderSize;

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("INITRD_CHUNK: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State < RosvVmStateKernelLoaded)
            {
                ROSV_ERR("INITRD_CHUNK: Invalid state %d (need >= KernelLoaded)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            HeaderSize = FIELD_OFFSET(ROSV_INITRD_CHUNK_REQUEST, Data);
            if (InputLength < HeaderSize || InputBuffer == NULL)
            {
                ROSV_ERR("INITRD_CHUNK: Input buffer too small (%u < %u)",
                         InputLength, HeaderSize);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Request = (PROSV_INITRD_CHUNK_REQUEST)InputBuffer;
            if (Request->Flags != 0 ||
                Request->DataLength == 0 ||
                Request->DataLength > InputLength - HeaderSize)
            {
                ROSV_ERR("INITRD_CHUNK: Invalid chunk flags=%u dataLen=%u inLen=%u",
                         Request->Flags, Request->DataLength, InputLength);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvInitrdAppend(Request->Offset,
                                      Request->Data,
                                      Request->DataLength,
                                      g_CurrentVm);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("INITRD_CHUNK: RosvInitrdAppend failed at offset=%llu, Status=0x%08X",
                         Request->Offset, Status);
            }
            break;
        }

        /* ---- INITRD_COMMIT (chunked path) ------------------------------- */
        case ROSV_IOCTL_INITRD_COMMIT:
        {
            ROSV_TRACE("IOCTL: INITRD_COMMIT");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("INITRD_COMMIT: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State < RosvVmStateKernelLoaded)
            {
                ROSV_ERR("INITRD_COMMIT: Invalid state %d (need >= KernelLoaded)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            Status = RosvInitrdCommit(g_CurrentVm);
            if (NT_SUCCESS(Status))
            {
                RosvVmSetCheckpoint(g_CurrentVm, RosvCpInitrdLoaded);
                ROSV_TRACE("INITRD_COMMIT: Initrd loaded, %llu bytes at GPA=0x%llX",
                           g_CurrentVm->InitrdSize, g_CurrentVm->InitrdLoadAddress);
            }
            else
            {
                ROSV_ERR("INITRD_COMMIT: RosvInitrdCommit failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- SET_CMDLINE ------------------------------------------------ */
        case ROSV_IOCTL_SET_CMDLINE:
        {
            ULONG CmdLineLen;
            PLINUX_BOOT_PARAMS BootParams;
            PVOID CmdLineHva;

            ROSV_TRACE("IOCTL: SET_CMDLINE, inputLen=%u", InputLength);

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("SET_CMDLINE: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength == 0 || InputBuffer == NULL)
            {
                ROSV_ERR("SET_CMDLINE: No cmdline data provided");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (InputLength >= ROSV_CMDLINE_MAX)
            {
                ROSV_ERR("SET_CMDLINE: Cmdline too long (%u >= %u)",
                         InputLength, ROSV_CMDLINE_MAX);
                Status = STATUS_BUFFER_OVERFLOW;
                break;
            }

            RtlZeroMemory(g_CurrentVm->Cmdline, ROSV_CMDLINE_MAX);
            RtlCopyMemory(g_CurrentVm->Cmdline, InputBuffer, InputLength);
            g_CurrentVm->Cmdline[InputLength] = '\0';

            ROSV_TRACE("SET_CMDLINE: \"%s\"", g_CurrentVm->Cmdline);

            /*
             * If boot_params were already built (common path: LOAD_KERNEL before
             * SET_CMDLINE), patch the in-guest cmdline fields in place so Linux
             * sees the updated value without rebuilding boot_params (which would
             * wipe initrd fields set by LOAD_INITRD).
             */
            if (g_CurrentVm->BootParamsAddress != 0)
            {
                BootParams = (PLINUX_BOOT_PARAMS)RosvMemoryGpaToHva(
                    g_CurrentVm,
                    g_CurrentVm->BootParamsAddress);
                CmdLineHva = RosvMemoryGpaToHva(g_CurrentVm, LINUX_CMDLINE_ADDR);

                if (BootParams != NULL && CmdLineHva != NULL)
                {
                    CmdLineLen = (ULONG)strlen(g_CurrentVm->Cmdline) + 1;
                    if (CmdLineLen > ROSV_CMDLINE_MAX)
                        CmdLineLen = ROSV_CMDLINE_MAX;

                    RtlCopyMemory(CmdLineHva, g_CurrentVm->Cmdline, CmdLineLen);
                    ((PCHAR)CmdLineHva)[CmdLineLen - 1] = '\0';

                    BootParams->Hdr.CmdLinePtr = LINUX_CMDLINE_ADDR;
                    BootParams->Hdr.CmdlineSize = CmdLineLen;

                    ROSV_TRACE("SET_CMDLINE: boot_params updated (ptr=0x%X size=%u)",
                               BootParams->Hdr.CmdLinePtr,
                               BootParams->Hdr.CmdlineSize);
                }
                else
                {
                    ROSV_WARN("SET_CMDLINE: boot_params/cmdline GPA mapping failed, "
                              "new cmdline will apply on next LOAD_KERNEL");
                }
            }

            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- START_VM --------------------------------------------------- */
        case ROSV_IOCTL_START_VM:
        {
            ROSV_TRACE("IOCTL: START_VM");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("START_VM: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State < RosvVmStateKernelLoaded)
            {
                ROSV_ERR("START_VM: Invalid state %d (need >= KernelLoaded)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State == RosvVmStateRunning)
            {
                ROSV_ERR("START_VM: VM is already running");
                Status = STATUS_DEVICE_BUSY;
                break;
            }

            /* Initialize virtio-net device (always present, provides guest eth0).
             * Uses the fixed MAC address expected by the guest image/NAT setup. */
            {
                static const UCHAR VirtioNetMac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
                NTSTATUS NetStatus;

                NetStatus = RosvVirtioNetInitialize(&g_CurrentVm->VirtioNet,
                                                     g_CurrentVm,
                                                     VirtioNetMac);
                if (!NT_SUCCESS(NetStatus))
                {
                    ROSV_ERR("START_VM: RosvVirtioNetInitialize failed, Status=0x%08X",
                             NetStatus);
                    /* Non-fatal: continue without network */
                }
            }

            /* Initialize virtio-console for multi-port terminal access */
            {
                NTSTATUS ConStatus;
                ConStatus = RosvVirtioConInitialize(&g_CurrentVm->VirtioCon,
                                                     g_CurrentVm,
                                                     ROSV_VIRTIO_CON_MAX_PORTS);
                if (!NT_SUCCESS(ConStatus))
                {
                    ROSV_ERR("START_VM: RosvVirtioConInitialize failed, Status=0x%08X",
                             ConStatus);
                    /* Non-fatal: continue without virtio-console */
                }
                else
                {
                    ROSV_TRACE("START_VM: virtio-console initialized with %u ports",
                               ROSV_VIRTIO_CON_MAX_PORTS);
                }
            }

            Status = RosvVmStart(g_CurrentVm);

            if (NT_SUCCESS(Status))
            {
                g_NetStub.Attached = (g_CurrentVm->NetBackend != NULL) ? TRUE : FALSE;
                g_NetStub.BackendType = (g_CurrentVm->NetBackend != NULL) ?
                    RosvNetBackendNetio : RosvNetBackendNone;
                ROSV_TRACE("START_VM: VM started");
            }
            else
            {
                ROSV_ERR("START_VM: RosvVmStart failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- STOP_VM ---------------------------------------------------- */
        case ROSV_IOCTL_STOP_VM:
        {
            ROSV_TRACE("IOCTL: STOP_VM");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("STOP_VM: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->State != RosvVmStateRunning)
            {
                ROSV_ERR("STOP_VM: Invalid state %d (expected Running)",
                         g_CurrentVm->State);
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            Status = RosvVmStop(g_CurrentVm);

            if (NT_SUCCESS(Status))
            {
                g_NetStub.Attached = FALSE;
                g_NetStub.BackendType = RosvNetBackendNone;
                ROSV_TRACE("STOP_VM: VM stopped");
            }
            else
            {
                ROSV_ERR("STOP_VM: RosvVmStop failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- DESTROY_VM ------------------------------------------------- */
        case ROSV_IOCTL_DESTROY_VM:
        {
            PROSV_VM VmToDestroy;

            ROSV_TRACE("IOCTL: DESTROY_VM");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("DESTROY_VM: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            VmToDestroy = g_CurrentVm;
            g_CurrentVm = NULL;
            g_NetStub.Attached = FALSE;
            g_NetStub.BackendType = RosvNetBackendNone;

            RosvVmWaitForReferencesReleased(VmToDestroy);

            if (VmToDestroy->State == RosvVmStateRunning)
            {
                ROSV_TRACE("DESTROY_VM: VM is running, stopping before teardown");
                RosvVmStop(VmToDestroy);
            }

            if (g_DiskMappedBase != NULL || g_DiskSectionHandle != NULL ||
                g_DiskFileHandle != NULL || g_DiskFileHandleAlt != NULL)
            {
                ROSV_TRACE("DESTROY_VM: Detaching disk backend");
                RosvDetachDiskInternal(VmToDestroy);
            }

            RosvVmDestroy(VmToDestroy);

            ROSV_TRACE("DESTROY_VM: VM destroyed");
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- GET_STATE -------------------------------------------------- */
        case ROSV_IOCTL_GET_STATE:
        {
            PROSV_VM_STATE_INFO StateInfo;

            if (OutputLength < sizeof(ROSV_VM_STATE_INFO))
            {
                ROSV_ERR("GET_STATE: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_VM_STATE_INFO));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            StateInfo = (PROSV_VM_STATE_INFO)OutputBuffer;

            if (g_CurrentVm == NULL)
            {
                ROSV_TRACE("GET_STATE: No VM exists, returning zeroed state");
                RtlZeroMemory(StateInfo, sizeof(ROSV_VM_STATE_INFO));
            }
            else
            {
                StateInfo->State = g_CurrentVm->State;
                StateInfo->ExitCount = g_CurrentVm->Vcpu.ExitCount;
                StateInfo->LastExitReason = g_CurrentVm->Vcpu.LastExitReason;
                StateInfo->LastCheckpoint = g_CurrentVm->Vcpu.LastCheckpoint;
            }

            OutputBytes = sizeof(ROSV_VM_STATE_INFO);
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- GET_VM_STATS ----------------------------------------------- */
        case ROSV_IOCTL_GET_VM_STATS:
        {
            PROSV_VM_STATS Stats;

            if (OutputLength < sizeof(ROSV_VM_STATS))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Stats = (PROSV_VM_STATS)OutputBuffer;

            if (g_CurrentVm == NULL)
            {
                RtlZeroMemory(Stats, sizeof(ROSV_VM_STATS));
            }
            else
            {
                Stats->ExitCount     = g_CurrentVm->Vcpu.ExitCount;
                Stats->ExitHlt       = g_CurrentVm->Vcpu.StatExitHlt;
                Stats->ExitPreempt   = g_CurrentVm->Vcpu.StatExitPreempt;
                Stats->ExitEpt       = g_CurrentVm->Vcpu.StatExitEpt;
                Stats->ExitIo        = g_CurrentVm->Vcpu.StatExitIo;
                Stats->ExitMsr       = g_CurrentVm->Vcpu.StatExitMsr;
                Stats->ExitExtInt    = g_CurrentVm->Vcpu.StatExitExtInt;
                Stats->ExitIntWin    = g_CurrentVm->Vcpu.StatExitIntWin;
                Stats->ExitOther     = g_CurrentVm->Vcpu.StatExitOther;
                Stats->TimerInjected = g_CurrentVm->Vcpu.StatTimerInjected;
                Stats->HltYield      = g_CurrentVm->Vcpu.StatHltYield;
                Stats->SpinYield     = g_CurrentVm->Vcpu.StatSpinYield;
                Stats->HltTicks      = g_CurrentVm->Vcpu.StatHltTicks;
                {
                    LARGE_INTEGER Now = KeQueryPerformanceCounter(NULL);
                    Stats->TotalTicks = (ULONG64)Now.QuadPart;
                }
            }

            OutputBytes = sizeof(ROSV_VM_STATS);
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- GET_LOG ---------------------------------------------------- */
        case ROSV_IOCTL_GET_LOG:
        {
            ULONG MaxEntries;
            ULONG EntriesReturned = 0;

            ROSV_TRACE("IOCTL: GET_LOG");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("GET_LOG: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (OutputLength < sizeof(ROSV_EXIT_LOG_ENTRY))
            {
                ROSV_ERR("GET_LOG: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_EXIT_LOG_ENTRY));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            MaxEntries = OutputLength / sizeof(ROSV_EXIT_LOG_ENTRY);

            Status = RosvExitLogRead(
                &g_CurrentVm->Vcpu.ExitRing,
                (PROSV_EXIT_LOG_ENTRY)OutputBuffer,
                MaxEntries,
                &EntriesReturned);

            OutputBytes = EntriesReturned * sizeof(ROSV_EXIT_LOG_ENTRY);

            ROSV_TRACE("GET_LOG: Returned %u entries (%u bytes)",
                       EntriesReturned, OutputBytes);
            break;
        }

        /* ---- CONSOLE_READ ----------------------------------------------- */
        case ROSV_IOCTL_CONSOLE_READ:
        {
            ULONG BytesRead = 0;

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("CONSOLE_READ: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->Console == NULL)
            {
                ROSV_ERR("CONSOLE_READ: Console context is NULL");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (OutputLength == 0 || OutputBuffer == NULL)
            {
                ROSV_ERR("CONSOLE_READ: No output buffer provided");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvConsoleRead(
                g_CurrentVm->Console,
                (PCHAR)OutputBuffer,
                OutputLength,
                &BytesRead);

            if (NT_SUCCESS(Status))
            {
                OutputBytes = BytesRead;

                /*
                 * Avoid per-poll noise when userspace is idle; the launcher
                 * polls frequently and zero-length reads are expected.
                 */
                if (BytesRead > 0)
                {
                    ROSV_TRACE("CONSOLE_READ: Returned %u bytes", BytesRead);
                }
            }
            else
            {
                ROSV_ERR("CONSOLE_READ: RosvConsoleRead failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- CONSOLE_WRITE ---------------------------------------------- */
        case ROSV_IOCTL_CONSOLE_WRITE:
        {
            ROSV_TRACE("IOCTL: CONSOLE_WRITE, inLen=%u", InputLength);

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("CONSOLE_WRITE: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_CurrentVm->Console == NULL)
            {
                ROSV_ERR("CONSOLE_WRITE: Console context is NULL");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength == 0 || InputBuffer == NULL)
            {
                ROSV_ERR("CONSOLE_WRITE: No input data provided");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            RosvUartPushInput(
                &g_CurrentVm->Console->Uart,
                (PUCHAR)InputBuffer,
                InputLength);

            ROSV_TRACE("CONSOLE_WRITE: Injected %u bytes into UART RX FIFO", InputLength);
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- NET_ATTACH ----------------------------------------------- */
        case ROSV_IOCTL_NET_ATTACH:
        {
            PROSV_NET_ATTACH_REQUEST Attach;

            ROSV_TRACE("IOCTL: NET_ATTACH, inLen=%u", InputLength);

            if (g_CurrentVm == NULL)
            {
                /* Expected during startup: netd polls before rosl creates the VM. */
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_NET_ATTACH_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("NET_ATTACH: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_NET_ATTACH_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Attach = (PROSV_NET_ATTACH_REQUEST)InputBuffer;
            /* Next step: bind backend-specific runtime state here, or enforce
             * a strict backend contract at attach time. */
            if (g_CurrentVm->NetBackend != NULL && RosvNetBackendIsActive(g_CurrentVm))
            {
                ROSV_WARN("NET_ATTACH: internal netio backend owns networking");
                Status = STATUS_DEVICE_BUSY;
                break;
            }
            g_NetStub.Attached = TRUE;
            g_NetStub.BackendType = Attach->BackendType;
            g_NetStub.TxPackets = 0;
            g_NetStub.TxBytes = 0;
            g_NetStub.RxPackets = 0;
            g_NetStub.RxBytes = 0;
            if (g_CurrentVm->Console != NULL &&
                (Attach->GuestMac[0] | Attach->GuestMac[1] | Attach->GuestMac[2] |
                 Attach->GuestMac[3] | Attach->GuestMac[4] | Attach->GuestMac[5]) != 0)
            {
                RtlCopyMemory(g_CurrentVm->Console->Rtl8139.Mac,
                              Attach->GuestMac,
                              sizeof(Attach->GuestMac));
            }

            ROSV_TRACE("NET_ATTACH: backend=%u flags=0x%08X",
                       Attach->BackendType, Attach->Flags);
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- NET_DETACH ----------------------------------------------- */
        case ROSV_IOCTL_NET_DETACH:
        {
            ROSV_TRACE("IOCTL: NET_DETACH");

            g_NetStub.Attached = FALSE;
            g_NetStub.BackendType = RosvNetBackendNone;
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- NET_TX --------------------------------------------------- */
        NetFastPathTx:
        case ROSV_IOCTL_NET_TX:
        {
            PROSV_NET_PACKET Packet;
            ULONG MinLength;
            PROSV_VM LocalVm = NULL;
            BOOLEAN VmReferenced = FALSE;

            if (!g_NetStub.Attached)
            {
                Status = STATUS_DEVICE_NOT_READY;
                break;
            }

            if (InputLength < sizeof(ULONG) || InputBuffer == NULL)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Packet = (PROSV_NET_PACKET)InputBuffer;
            if (Packet->Length > ROSV_NET_PACKET_MAX)
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }

            MinLength = FIELD_OFFSET(ROSV_NET_PACKET, Data) + Packet->Length;
            if (InputLength < MinLength)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            LocalVm = g_CurrentVm;
            KeMemoryBarrier();
            if (LocalVm == NULL || !RosvVmAcquireReference(LocalVm))
            {
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }
            VmReferenced = TRUE;

            if (LocalVm->Console == NULL)
            {
                Status = STATUS_INVALID_DEVICE_STATE;
                goto NetFastPathTxDone;
            }

            if (LocalVm->NetBackend != NULL)
            {
                Status = STATUS_DEVICE_BUSY;
                goto NetFastPathTxDone;
            }

            /* Inject packet to guest via virtio-net (preferred) or RTL8139 (legacy).
             * Legacy NET_TX from a userspace backend means "host -> guest",
             * which is the RX path from the guest's perspective. */
            if (!RosvVirtioNetInjectRxPacket(&LocalVm->VirtioNet,
                                              Packet->Data,
                                              Packet->Length))
            {
                /* Fall back to RTL8139 if virtio-net cannot accept */
                if (!RosvRtl8139InjectRxPacket(&LocalVm->Console->Rtl8139,
                                               Packet->Data,
                                               Packet->Length))
                {
                    Status = STATUS_RETRY;
                    goto NetFastPathTxDone;
                }
            }

            g_NetStub.RxPackets++;
            g_NetStub.RxBytes += Packet->Length;
            Status = STATUS_SUCCESS;
NetFastPathTxDone:
            if (VmReferenced)
                RosvVmReleaseReference(LocalVm);
            break;
        }

        /* ---- NET_RX --------------------------------------------------- */
        NetFastPathRx:
        case ROSV_IOCTL_NET_RX:
        {
            PROSV_NET_PACKET Packet;
            ULONG PacketBytes = 0;
            PROSV_VM LocalVm = NULL;
            BOOLEAN VmReferenced = FALSE;

            if (!g_NetStub.Attached)
            {
                Status = STATUS_DEVICE_NOT_READY;
                break;
            }

            if (OutputLength < sizeof(ULONG) || OutputBuffer == NULL)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            LocalVm = g_CurrentVm;
            KeMemoryBarrier();
            if (LocalVm == NULL || !RosvVmAcquireReference(LocalVm))
            {
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }
            VmReferenced = TRUE;

            if (LocalVm->Console == NULL)
            {
                Status = STATUS_INVALID_DEVICE_STATE;
                goto NetFastPathRxDone;
            }

            if (LocalVm->NetBackend != NULL)
            {
                Status = STATUS_DEVICE_BUSY;
                goto NetFastPathRxDone;
            }

            if (OutputLength < sizeof(ROSV_NET_PACKET))
            {
                ROSV_ERR("NET_RX: output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_NET_PACKET));
                Status = STATUS_BUFFER_TOO_SMALL;
                goto NetFastPathRxDone;
            }

            Packet = (PROSV_NET_PACKET)OutputBuffer;
            /* Dequeue from virtio-net first (preferred), then RTL8139 (legacy).
             * Legacy NET_RX for a userspace backend means "guest -> host",
             * which is the TX path from the guest's perspective.
             *
             * If no packet is available, block on TxReadyEvent for up to 2ms
             * so a userspace backend doesn't have to poll with Sleep().
             * This eliminates the 20-80ms Sleep() overshoot latency. */
            if (!RosvVirtioNetDequeueTxPacket(&LocalVm->VirtioNet, Packet) &&
                !RosvRtl8139DequeueTxPacket(&LocalVm->Console->Rtl8139, Packet))
            {
                /* No packet available — if VM is running, block on TxReadyEvent
                 * with 2ms timeout to avoid polling with Sleep().
                 * No mutex held — no contention with vCPU thread. */
                if (LocalVm->State == RosvVmStateRunning)
                {
                    LARGE_INTEGER Timeout;

                    Timeout.QuadPart = -20000LL; /* 2ms in 100ns units, relative */
                    KeWaitForSingleObject(&LocalVm->VirtioNet.TxReadyEvent,
                                          Executive, KernelMode, FALSE, &Timeout);

                    if (!g_NetStub.Attached || LocalVm->Console == NULL)
                    {
                        Packet->Length = 0;
                        OutputBytes = sizeof(Packet->Length);
                        Status = STATUS_SUCCESS;
                        goto NetFastPathRxDone;
                    }

                    /* Try again after wakeup */
                    if (!RosvVirtioNetDequeueTxPacket(&LocalVm->VirtioNet, Packet) &&
                        !RosvRtl8139DequeueTxPacket(&LocalVm->Console->Rtl8139, Packet))
                    {
                        Packet->Length = 0;
                        OutputBytes = sizeof(Packet->Length);
                        Status = STATUS_SUCCESS;
                        goto NetFastPathRxDone;
                    }
                }
                else
                {
                    Packet->Length = 0;
                    OutputBytes = sizeof(Packet->Length);
                    Status = STATUS_SUCCESS;
                    goto NetFastPathRxDone;
                }
            }

            PacketBytes = FIELD_OFFSET(ROSV_NET_PACKET, Data) + Packet->Length;
            if (PacketBytes > OutputLength)
            {
                ROSV_ERR("NET_RX: packet (%u B) exceeds output buffer (%u B)",
                         PacketBytes, OutputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                goto NetFastPathRxDone;
            }

            OutputBytes = PacketBytes;
            g_NetStub.TxPackets++;
            g_NetStub.TxBytes += Packet->Length;
            Status = STATUS_SUCCESS;
NetFastPathRxDone:
            if (VmReferenced)
                RosvVmReleaseReference(LocalVm);
            break;
        }

        /* ---- NET_GET_STATS -------------------------------------------- */
        case ROSV_IOCTL_NET_GET_STATS:
        {
            PROSV_NET_STATS Stats;

            if (OutputLength < sizeof(ROSV_NET_STATS) || OutputBuffer == NULL)
            {
                ROSV_ERR("NET_GET_STATS: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_NET_STATS));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Stats = (PROSV_NET_STATS)OutputBuffer;
            RtlZeroMemory(Stats, sizeof(*Stats));
            if (g_CurrentVm != NULL && RosvNetBackendIsActive(g_CurrentVm))
            {
                Stats->Attached = 1;
                Stats->BackendType = RosvNetBackendNetio;
                RosvNetBackendQueryStats(g_CurrentVm,
                                         &Stats->TxPackets,
                                         &Stats->TxBytes,
                                         &Stats->RxPackets,
                                         &Stats->RxBytes);
            }
            else
            {
                Stats->Attached = g_NetStub.Attached ? 1 : 0;
                Stats->BackendType = g_NetStub.BackendType;
                Stats->TxPackets = g_NetStub.TxPackets;
                Stats->TxBytes = g_NetStub.TxBytes;
                Stats->RxPackets = g_NetStub.RxPackets;
                Stats->RxBytes = g_NetStub.RxBytes;
            }
            /* Stats remain intentionally small for now; adding drops, retries,
             * queue depth, and IRQ counters would improve observability. */

            OutputBytes = sizeof(*Stats);
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- PTY_CREATE ------------------------------------------------- */
        case ROSV_IOCTL_PTY_CREATE:
        {
            PROSV_PTY_CREATE_REQUEST PtyCreateReq;
            PROSV_PTY_CREATE_RESULT PtyCreateResult;

            ROSV_TRACE("IOCTL: PTY_CREATE");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_CREATE: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_PTY_CREATE_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_CREATE: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_PTY_CREATE_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_PTY_CREATE_RESULT) || OutputBuffer == NULL)
            {
                ROSV_ERR("PTY_CREATE: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_PTY_CREATE_RESULT));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyCreateReq = (PROSV_PTY_CREATE_REQUEST)InputBuffer;
            PtyCreateResult = (PROSV_PTY_CREATE_RESULT)OutputBuffer;

            Status = RosvPtyCreate(&g_CurrentVm->PtyManager,
                                   PtyCreateReq->InitialRows,
                                   PtyCreateReq->InitialCols,
                                   &PtyCreateResult->PtyIndex,
                                   &PtyCreateResult->ReaderIndex);

            PtyCreateResult->VconPort = (ULONG)-1;
            PtyCreateResult->Status = Status;
            OutputBytes = sizeof(ROSV_PTY_CREATE_RESULT);

            if (NT_SUCCESS(Status))
            {
                /*
                 * Allocate a vcon port and bind it to this PTY.
                 * Each PTY gets its own independent vcon port for guest I/O.
                 * Serial S0 (UART) is NOT involved — it remains service-only.
                 */
                ULONG VconPort = RosvVirtioConAllocatePort(&g_CurrentVm->VirtioCon);
                if (VconPort != (ULONG)-1)
                {
                    PROSV_PTY_STATE NewPty = &g_CurrentVm->PtyManager.Ptys[PtyCreateResult->PtyIndex];
                    NewPty->BoundVconPort = VconPort;
                    NewPty->BoundVconState = &g_CurrentVm->VirtioCon;
                    g_CurrentVm->VirtioCon.Ports[VconPort].BoundPty = NewPty;
                    PtyCreateResult->VconPort = VconPort;
                    ROSV_TRACE("PTY_CREATE: PTY %u bound to vcon port %u",
                               PtyCreateResult->PtyIndex, VconPort);
                }
                else
                {
                    ROSV_ERR("PTY_CREATE: no free vcon port, destroying PTY %u",
                             PtyCreateResult->PtyIndex);
                    RosvPtyDestroy(&g_CurrentVm->PtyManager, PtyCreateResult->PtyIndex);
                    PtyCreateResult->PtyIndex = 0;
                    PtyCreateResult->ReaderIndex = 0;
                    PtyCreateResult->Status = STATUS_INSUFFICIENT_RESOURCES;
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                }

                if (NT_SUCCESS(Status))
                {
                    ROSV_TRACE("PTY_CREATE: allocated PTY %u with vcon port %u",
                               PtyCreateResult->PtyIndex, PtyCreateResult->VconPort);
                }
            }
            else
            {
                ROSV_ERR("PTY_CREATE: RosvPtyCreate failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- PTY_DESTROY ------------------------------------------------ */
        case ROSV_IOCTL_PTY_DESTROY:
        {
            PULONG PtyDestroyIndex;

            ROSV_TRACE("IOCTL: PTY_DESTROY");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_DESTROY: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ULONG) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_DESTROY: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ULONG));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyDestroyIndex = (PULONG)InputBuffer;
            if (*PtyDestroyIndex >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_DESTROY: index %u out of range", *PtyDestroyIndex);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Release bound vcon port if this PTY has one */
            {
                PROSV_PTY_STATE DestroyPty = &g_CurrentVm->PtyManager.Ptys[*PtyDestroyIndex];
                if (DestroyPty->BoundVconPort != (ULONG)-1)
                {
                    g_CurrentVm->VirtioCon.Ports[DestroyPty->BoundVconPort].BoundPty = NULL;
                    RosvVirtioConReleasePort(&g_CurrentVm->VirtioCon, DestroyPty->BoundVconPort);
                    ROSV_TRACE("PTY_DESTROY: released vcon port %u for PTY %u",
                               DestroyPty->BoundVconPort, *PtyDestroyIndex);
                    DestroyPty->BoundVconPort = (ULONG)-1;
                    DestroyPty->BoundVconState = NULL;
                }
            }

            Status = RosvPtyDestroy(&g_CurrentVm->PtyManager, *PtyDestroyIndex);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("PTY_DESTROY: PTY %u destroyed", *PtyDestroyIndex);
            }
            else
            {
                ROSV_ERR("PTY_DESTROY: failed for index %u, Status=0x%08X",
                         *PtyDestroyIndex, Status);
            }
            break;
        }

        /* ---- PTY_WRITE (host -> guest input) ---------------------------- */
        case ROSV_IOCTL_PTY_WRITE:
        {
            PROSV_PTY_IO_REQUEST PtyIoReq;
            PROSV_PTY_IO_RESULT PtyIoResult;
            ULONG PtyBytesXferred = 0;
            ULONG HeaderSize;

            ROSV_TRACE("IOCTL: PTY_WRITE, inLen=%u", InputLength);

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_WRITE: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < FIELD_OFFSET(ROSV_PTY_IO_REQUEST, Data) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_WRITE: Input buffer too small (%u)", InputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyIoReq = (PROSV_PTY_IO_REQUEST)InputBuffer;
            HeaderSize = FIELD_OFFSET(ROSV_PTY_IO_REQUEST, Data);
            if (PtyIoReq->DataLength > InputLength - HeaderSize)
            {
                ROSV_ERR("PTY_WRITE: Input buffer truncated (%u < %u+%u)",
                         InputLength, HeaderSize, PtyIoReq->DataLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (PtyIoReq->PtyIndex >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_WRITE: index %u out of range", PtyIoReq->PtyIndex);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (OutputLength < FIELD_OFFSET(ROSV_PTY_IO_RESULT, Data) || OutputBuffer == NULL)
            {
                ROSV_ERR("PTY_WRITE: Output buffer too small (%u)", OutputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Status = RosvPtyMasterWrite(
                &g_CurrentVm->PtyManager.Ptys[PtyIoReq->PtyIndex],
                PtyIoReq->Data,
                PtyIoReq->DataLength,
                &PtyBytesXferred);

            /* Pump PTY InputBuf -> bound vcon port so the guest receives host input */
            if (NT_SUCCESS(Status))
            {
                PROSV_PTY_STATE WritePty = &g_CurrentVm->PtyManager.Ptys[PtyIoReq->PtyIndex];
                if (WritePty->BoundVconPort != (ULONG)-1)
                {
                    RosvPtyPumpToVcon(WritePty);
                }
                else if (g_CurrentVm->Console != NULL)
                {
                    /* Fallback: legacy UART path for unbound PTYs */
                    RosvConsolePumpPtyInput(g_CurrentVm->Console);
                }
            }

            PtyIoResult = (PROSV_PTY_IO_RESULT)OutputBuffer;
            PtyIoResult->BytesTransferred = PtyBytesXferred;
            PtyIoResult->Status = Status;
            OutputBytes = FIELD_OFFSET(ROSV_PTY_IO_RESULT, Data);
            break;
        }

        /* ---- PTY_READ (guest output -> host) ---------------------------- */
        case ROSV_IOCTL_PTY_READ:
        {
            PROSV_PTY_IO_RESULT PtyReadResult;
            ULONG PtyReadXferred = 0;
            ULONG PtyMaxData;
            ULONG PtyReadPtyIdx;
            ULONG PtyReadRdrIdx;

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_READ: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            /*
             * Accept two input formats for backward compatibility:
             * - New: ROSV_PTY_READ_REQUEST (8 bytes: PtyIndex + ReaderIndex)
             * - Legacy: single ULONG PtyIndex (4 bytes, ReaderIndex defaults to 0)
             */
            if (InputLength >= sizeof(ROSV_PTY_READ_REQUEST) && InputBuffer != NULL)
            {
                PROSV_PTY_READ_REQUEST PtyReadReq = (PROSV_PTY_READ_REQUEST)InputBuffer;
                PtyReadPtyIdx = PtyReadReq->PtyIndex;
                PtyReadRdrIdx = PtyReadReq->ReaderIndex;
            }
            else if (InputLength >= sizeof(ULONG) && InputBuffer != NULL)
            {
                PtyReadPtyIdx = *(PULONG)InputBuffer;
                PtyReadRdrIdx = 0;
            }
            else
            {
                ROSV_ERR("PTY_READ: Input buffer too small (%u)", InputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (PtyReadPtyIdx >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_READ: PTY index %u out of range", PtyReadPtyIdx);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (PtyReadRdrIdx >= ROSV_PTY_MAX_READERS)
            {
                ROSV_ERR("PTY_READ: reader index %u out of range", PtyReadRdrIdx);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (OutputLength < FIELD_OFFSET(ROSV_PTY_IO_RESULT, Data) + 1 ||
                OutputBuffer == NULL)
            {
                ROSV_ERR("PTY_READ: Output buffer too small (%u)", OutputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyReadResult = (PROSV_PTY_IO_RESULT)OutputBuffer;
            PtyMaxData = OutputLength - FIELD_OFFSET(ROSV_PTY_IO_RESULT, Data);

            Status = RosvPtyMasterRead(
                &g_CurrentVm->PtyManager.Ptys[PtyReadPtyIdx],
                PtyReadRdrIdx,
                PtyReadResult->Data,
                PtyMaxData,
                &PtyReadXferred);

            PtyReadResult->BytesTransferred = PtyReadXferred;
            PtyReadResult->Status = Status;
            OutputBytes = FIELD_OFFSET(ROSV_PTY_IO_RESULT, Data) + PtyReadXferred;
            break;
        }

        /* ---- PTY_RESIZE ------------------------------------------------- */
        case ROSV_IOCTL_PTY_RESIZE:
        {
            PROSV_PTY_RESIZE_REQUEST PtyResizeReq;

            ROSV_TRACE("IOCTL: PTY_RESIZE");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_RESIZE: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_PTY_RESIZE_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_RESIZE: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_PTY_RESIZE_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyResizeReq = (PROSV_PTY_RESIZE_REQUEST)InputBuffer;
            if (PtyResizeReq->PtyIndex >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_RESIZE: index %u out of range", PtyResizeReq->PtyIndex);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvPtySetWinsize(
                &g_CurrentVm->PtyManager.Ptys[PtyResizeReq->PtyIndex],
                &PtyResizeReq->Winsize);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("PTY_RESIZE: PTY %u resized to %ux%u",
                           PtyResizeReq->PtyIndex,
                           PtyResizeReq->Winsize.ws_col,
                           PtyResizeReq->Winsize.ws_row);
            }
            else
            {
                ROSV_ERR("PTY_RESIZE: failed for PTY %u, Status=0x%08X",
                         PtyResizeReq->PtyIndex, Status);
            }
            break;
        }

        /* ---- PTY_GET_TERMIOS -------------------------------------------- */
        case ROSV_IOCTL_PTY_GET_TERMIOS:
        {
            PULONG PtyTermiosIdx;
            PROSV_TERMIOS PtyTermiosOut;

            ROSV_TRACE("IOCTL: PTY_GET_TERMIOS");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_GET_TERMIOS: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ULONG) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_GET_TERMIOS: Input buffer too small (%u)", InputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_TERMIOS) || OutputBuffer == NULL)
            {
                ROSV_ERR("PTY_GET_TERMIOS: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_TERMIOS));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyTermiosIdx = (PULONG)InputBuffer;
            if (*PtyTermiosIdx >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_GET_TERMIOS: index %u out of range", *PtyTermiosIdx);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            PtyTermiosOut = (PROSV_TERMIOS)OutputBuffer;
            Status = RosvPtyGetTermios(
                &g_CurrentVm->PtyManager.Ptys[*PtyTermiosIdx],
                PtyTermiosOut);

            if (NT_SUCCESS(Status))
            {
                OutputBytes = sizeof(ROSV_TERMIOS);
                ROSV_TRACE("PTY_GET_TERMIOS: PTY %u returned", *PtyTermiosIdx);
            }
            else
            {
                ROSV_ERR("PTY_GET_TERMIOS: failed for PTY %u, Status=0x%08X",
                         *PtyTermiosIdx, Status);
            }
            break;
        }

        /* ---- PTY_SET_TERMIOS -------------------------------------------- */
        case ROSV_IOCTL_PTY_SET_TERMIOS:
        {
            PROSV_PTY_TERMIOS_REQUEST PtyTermiosSetReq;

            ROSV_TRACE("IOCTL: PTY_SET_TERMIOS");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_SET_TERMIOS: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_PTY_TERMIOS_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_SET_TERMIOS: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_PTY_TERMIOS_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyTermiosSetReq = (PROSV_PTY_TERMIOS_REQUEST)InputBuffer;
            if (PtyTermiosSetReq->PtyIndex >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_SET_TERMIOS: index %u out of range",
                         PtyTermiosSetReq->PtyIndex);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvPtySetTermios(
                &g_CurrentVm->PtyManager.Ptys[PtyTermiosSetReq->PtyIndex],
                PtyTermiosSetReq->Action,
                &PtyTermiosSetReq->Termios);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("PTY_SET_TERMIOS: PTY %u updated (action=%u)",
                           PtyTermiosSetReq->PtyIndex, PtyTermiosSetReq->Action);
            }
            else
            {
                ROSV_ERR("PTY_SET_TERMIOS: failed for PTY %u, Status=0x%08X",
                         PtyTermiosSetReq->PtyIndex, Status);
            }
            break;
        }

        /* ---- PTY_SIGNAL ------------------------------------------------- */
        case ROSV_IOCTL_PTY_SIGNAL:
        {
            PROSV_PTY_SIGNAL_REQUEST PtySigReq;

            ROSV_TRACE("IOCTL: PTY_SIGNAL");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_SIGNAL: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_PTY_SIGNAL_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_SIGNAL: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_PTY_SIGNAL_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtySigReq = (PROSV_PTY_SIGNAL_REQUEST)InputBuffer;
            if (PtySigReq->PtyIndex >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_SIGNAL: index %u out of range", PtySigReq->PtyIndex);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvPtySendSignal(
                &g_CurrentVm->PtyManager.Ptys[PtySigReq->PtyIndex],
                PtySigReq->Signal);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("PTY_SIGNAL: PTY %u signal %u sent",
                           PtySigReq->PtyIndex, PtySigReq->Signal);
            }
            else
            {
                ROSV_ERR("PTY_SIGNAL: failed for PTY %u, Status=0x%08X",
                         PtySigReq->PtyIndex, Status);
            }
            break;
        }

        /* ---- PTY_FLUSH -------------------------------------------------- */
        case ROSV_IOCTL_PTY_FLUSH:
        {
            PROSV_PTY_FLUSH_REQUEST PtyFlushReq;

            ROSV_TRACE("IOCTL: PTY_FLUSH");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_FLUSH: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_PTY_FLUSH_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_FLUSH: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_PTY_FLUSH_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyFlushReq = (PROSV_PTY_FLUSH_REQUEST)InputBuffer;
            if (PtyFlushReq->PtyIndex >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_FLUSH: index %u out of range", PtyFlushReq->PtyIndex);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Status = RosvPtyFlush(
                &g_CurrentVm->PtyManager.Ptys[PtyFlushReq->PtyIndex],
                PtyFlushReq->Queue);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("PTY_FLUSH: PTY %u queue %u flushed",
                           PtyFlushReq->PtyIndex, PtyFlushReq->Queue);
            }
            else
            {
                ROSV_ERR("PTY_FLUSH: failed for PTY %u, Status=0x%08X",
                         PtyFlushReq->PtyIndex, Status);
            }
            break;
        }

        /* ---- PTY_GET_INFO ----------------------------------------------- */
        case ROSV_IOCTL_PTY_GET_INFO:
        {
            PULONG PtyInfoIndex;
            PROSV_PTY_INFO PtyInfoOut;

            ROSV_TRACE("IOCTL: PTY_GET_INFO");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_GET_INFO: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ULONG) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_GET_INFO: Input buffer too small (%u)", InputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_PTY_INFO) || OutputBuffer == NULL)
            {
                ROSV_ERR("PTY_GET_INFO: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_PTY_INFO));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyInfoIndex = (PULONG)InputBuffer;
            if (*PtyInfoIndex >= ROSV_PTY_MAX)
            {
                ROSV_ERR("PTY_GET_INFO: index %u out of range", *PtyInfoIndex);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            PtyInfoOut = (PROSV_PTY_INFO)OutputBuffer;
            Status = RosvPtyGetInfo(
                &g_CurrentVm->PtyManager.Ptys[*PtyInfoIndex],
                PtyInfoOut);

            if (NT_SUCCESS(Status))
            {
                OutputBytes = sizeof(ROSV_PTY_INFO);
                ROSV_TRACE("PTY_GET_INFO: PTY %u info returned", *PtyInfoIndex);
            }
            else
            {
                ROSV_ERR("PTY_GET_INFO: failed for PTY %u, Status=0x%08X",
                         *PtyInfoIndex, Status);
            }
            break;
        }

        /* ---- PTY_ATTACH ------------------------------------------------- */
        case ROSV_IOCTL_PTY_ATTACH:
        {
            PROSV_PTY_ATTACH_REQUEST PtyAttachReq;
            PROSV_PTY_ATTACH_RESULT PtyAttachResult;

            ROSV_TRACE("IOCTL: PTY_ATTACH");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_ATTACH: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_PTY_ATTACH_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_ATTACH: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_PTY_ATTACH_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_PTY_ATTACH_RESULT) || OutputBuffer == NULL)
            {
                ROSV_ERR("PTY_ATTACH: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_PTY_ATTACH_RESULT));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyAttachReq = (PROSV_PTY_ATTACH_REQUEST)InputBuffer;
            PtyAttachResult = (PROSV_PTY_ATTACH_RESULT)OutputBuffer;

            Status = RosvPtyAttachReader(&g_CurrentVm->PtyManager,
                                         PtyAttachReq->PtyIndex,
                                         &PtyAttachResult->ReaderIndex);
            PtyAttachResult->PtyIndex = PtyAttachReq->PtyIndex;
            PtyAttachResult->Status = Status;
            OutputBytes = sizeof(ROSV_PTY_ATTACH_RESULT);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("PTY_ATTACH: PTY %u reader %u attached",
                           PtyAttachReq->PtyIndex, PtyAttachResult->ReaderIndex);
            }
            else
            {
                ROSV_ERR("PTY_ATTACH: PTY %u failed, Status=0x%08X",
                         PtyAttachReq->PtyIndex, Status);
            }
            break;
        }

        /* ---- PTY_DETACH ------------------------------------------------- */
        case ROSV_IOCTL_PTY_DETACH:
        {
            PROSV_PTY_DETACH_REQUEST PtyDetachReq;

            ROSV_TRACE("IOCTL: PTY_DETACH");

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("PTY_DETACH: No VM exists");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (InputLength < sizeof(ROSV_PTY_DETACH_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("PTY_DETACH: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_PTY_DETACH_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            PtyDetachReq = (PROSV_PTY_DETACH_REQUEST)InputBuffer;
            Status = RosvPtyDetachReader(&g_CurrentVm->PtyManager,
                                         PtyDetachReq->PtyIndex,
                                         PtyDetachReq->ReaderIndex);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("PTY_DETACH: PTY %u reader %u detached",
                           PtyDetachReq->PtyIndex, PtyDetachReq->ReaderIndex);
            }
            else
            {
                ROSV_ERR("PTY_DETACH: PTY %u reader %u failed, Status=0x%08X",
                         PtyDetachReq->PtyIndex, PtyDetachReq->ReaderIndex, Status);
            }
            break;
        }

        /* ---- SESSION_CREATE --------------------------------------------- */
        case ROSV_IOCTL_SESSION_CREATE:
        {
            PROSV_SESSION_CREATE_REQUEST Req;
            PROSV_SESSION_CREATE_RESULT Res;

            ROSV_TRACE("IOCTL: SESSION_CREATE");

            if (InputLength < sizeof(ROSV_SESSION_CREATE_REQUEST))
            {
                ROSV_ERR("SESSION_CREATE: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_SESSION_CREATE_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_SESSION_CREATE_RESULT))
            {
                ROSV_ERR("SESSION_CREATE: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_SESSION_CREATE_RESULT));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Req = (PROSV_SESSION_CREATE_REQUEST)InputBuffer;
            Res = (PROSV_SESSION_CREATE_RESULT)OutputBuffer;

            Status = RosvSessionCreate(Req, Res);
            OutputBytes = sizeof(ROSV_SESSION_CREATE_RESULT);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("SESSION_CREATE: Session %u created, PTY %u",
                           Res->SessionId, Res->PtyIndex);
            }
            else
            {
                ROSV_ERR("SESSION_CREATE: Failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- SESSION_START ---------------------------------------------- */
        case ROSV_IOCTL_SESSION_START:
        {
            ULONG SessionId;

            ROSV_TRACE("IOCTL: SESSION_START");

            if (InputLength < sizeof(ULONG))
            {
                ROSV_ERR("SESSION_START: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ULONG));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            SessionId = *(PULONG)InputBuffer;
            Status = RosvSessionStart(SessionId);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("SESSION_START: Session %u started", SessionId);
            }
            else
            {
                ROSV_ERR("SESSION_START: Session %u failed, Status=0x%08X",
                         SessionId, Status);
            }
            break;
        }

        /* ---- SESSION_STOP ----------------------------------------------- */
        case ROSV_IOCTL_SESSION_STOP:
        {
            ULONG SessionId;

            ROSV_TRACE("IOCTL: SESSION_STOP");

            if (InputLength < sizeof(ULONG))
            {
                ROSV_ERR("SESSION_STOP: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ULONG));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            SessionId = *(PULONG)InputBuffer;
            Status = RosvSessionStop(SessionId);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("SESSION_STOP: Session %u stopped", SessionId);
            }
            else
            {
                ROSV_ERR("SESSION_STOP: Session %u failed, Status=0x%08X",
                         SessionId, Status);
            }
            break;
        }

        /* ---- SESSION_DESTROY -------------------------------------------- */
        case ROSV_IOCTL_SESSION_DESTROY:
        {
            ULONG SessionId;

            ROSV_TRACE("IOCTL: SESSION_DESTROY");

            if (InputLength < sizeof(ULONG))
            {
                ROSV_ERR("SESSION_DESTROY: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ULONG));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            SessionId = *(PULONG)InputBuffer;
            Status = RosvSessionDestroy(SessionId);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("SESSION_DESTROY: Session %u destroyed", SessionId);
            }
            else
            {
                ROSV_ERR("SESSION_DESTROY: Session %u failed, Status=0x%08X",
                         SessionId, Status);
            }
            break;
        }

        /* ---- SESSION_LIST ----------------------------------------------- */
        case ROSV_IOCTL_SESSION_LIST:
        {
            ROSV_TRACE("IOCTL: SESSION_LIST");

            if (OutputLength < FIELD_OFFSET(ROSV_SESSION_LIST_RESULT, Sessions))
            {
                ROSV_ERR("SESSION_LIST: Output buffer too small (%u)", OutputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Status = RosvSessionList(
                (PROSV_SESSION_LIST_RESULT)OutputBuffer,
                OutputLength,
                &OutputBytes);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("SESSION_LIST: Returned %u bytes", OutputBytes);
            }
            else
            {
                ROSV_ERR("SESSION_LIST: Failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- SESSION_ATTACH --------------------------------------------- */
        case ROSV_IOCTL_SESSION_ATTACH:
        {
            ULONG SessionId;
            PROSV_SESSION_INFO Info;

            ROSV_TRACE("IOCTL: SESSION_ATTACH");

            if (InputLength < sizeof(ULONG))
            {
                ROSV_ERR("SESSION_ATTACH: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ULONG));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_SESSION_INFO))
            {
                ROSV_ERR("SESSION_ATTACH: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_SESSION_INFO));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            SessionId = *(PULONG)InputBuffer;
            Info = (PROSV_SESSION_INFO)OutputBuffer;

            Status = RosvSessionAttach(SessionId, Info);
            if (NT_SUCCESS(Status))
            {
                OutputBytes = sizeof(ROSV_SESSION_INFO);
                ROSV_TRACE("SESSION_ATTACH: Attached to session %u, PTY %u",
                           SessionId, Info->PrimaryPtyIndex);
            }
            else
            {
                ROSV_ERR("SESSION_ATTACH: Session %u failed, Status=0x%08X",
                         SessionId, Status);
            }
            break;
        }

        /* ---- SESSION_DETACH --------------------------------------------- */
        case ROSV_IOCTL_SESSION_DETACH:
        {
            ULONG SessionId;

            ROSV_TRACE("IOCTL: SESSION_DETACH");

            if (InputLength < sizeof(ULONG))
            {
                ROSV_ERR("SESSION_DETACH: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ULONG));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            SessionId = *(PULONG)InputBuffer;
            Status = RosvSessionDetach(SessionId);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("SESSION_DETACH: Detached from session %u", SessionId);
            }
            else
            {
                ROSV_ERR("SESSION_DETACH: Session %u failed, Status=0x%08X",
                         SessionId, Status);
            }
            break;
        }

        /* ---- FS_MOUNT --------------------------------------------------- */
        case ROSV_IOCTL_FS_MOUNT:
        {
            PROSV_FS_MOUNT_REQUEST MountReq;

            ROSV_TRACE("IOCTL: FS_MOUNT");

            if (InputLength < sizeof(ROSV_FS_MOUNT_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("FS_MOUNT: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_FS_MOUNT_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            MountReq = (PROSV_FS_MOUNT_REQUEST)InputBuffer;
            Status = RosvFsMountAdd(MountReq);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("FS_MOUNT: Mount added");
            }
            else
            {
                ROSV_ERR("FS_MOUNT: Failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- FS_UNMOUNT ------------------------------------------------- */
        case ROSV_IOCTL_FS_UNMOUNT:
        {
            PROSV_FS_MOUNT_REQUEST MountReq;

            ROSV_TRACE("IOCTL: FS_UNMOUNT");

            if (InputLength < sizeof(ROSV_FS_MOUNT_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("FS_UNMOUNT: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_FS_MOUNT_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            MountReq = (PROSV_FS_MOUNT_REQUEST)InputBuffer;
            Status = RosvFsMountRemove(MountReq);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("FS_UNMOUNT: Mount removed");
            }
            else
            {
                ROSV_ERR("FS_UNMOUNT: Failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- FS_LIST ---------------------------------------------------- */
        case ROSV_IOCTL_FS_LIST:
        {
            ROSV_TRACE("IOCTL: FS_LIST");

            if (OutputLength < sizeof(ROSV_FS_MOUNT_LIST) || OutputBuffer == NULL)
            {
                ROSV_ERR("FS_LIST: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_FS_MOUNT_LIST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Status = RosvFsMountList(
                (PROSV_FS_MOUNT_LIST)OutputBuffer,
                OutputLength,
                &OutputBytes);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("FS_LIST: Returned %u bytes", OutputBytes);
            }
            else
            {
                ROSV_ERR("FS_LIST: Failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- NET_FORWARD ------------------------------------------------ */
        case ROSV_IOCTL_NET_FORWARD:
        {
            PROSV_NET_FORWARD_REQUEST FwdReq;

            ROSV_TRACE("IOCTL: NET_FORWARD");

            if (InputLength < sizeof(ROSV_NET_FORWARD_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("NET_FORWARD: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_NET_FORWARD_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            FwdReq = (PROSV_NET_FORWARD_REQUEST)InputBuffer;
            Status = RosvNetForwardAdd(FwdReq);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("NET_FORWARD: Forward added host:%u -> guest:%u",
                           FwdReq->HostPort, FwdReq->GuestPort);
            }
            else
            {
                ROSV_ERR("NET_FORWARD: Failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- NET_UNFORWARD ---------------------------------------------- */
        case ROSV_IOCTL_NET_UNFORWARD:
        {
            PROSV_NET_FORWARD_REQUEST FwdReq;

            ROSV_TRACE("IOCTL: NET_UNFORWARD");

            if (InputLength < sizeof(ROSV_NET_FORWARD_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("NET_UNFORWARD: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_NET_FORWARD_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            FwdReq = (PROSV_NET_FORWARD_REQUEST)InputBuffer;
            Status = RosvNetForwardRemove(FwdReq);

            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("NET_UNFORWARD: Forward removed host:%u", FwdReq->HostPort);
            }
            else
            {
                ROSV_ERR("NET_UNFORWARD: Failed, Status=0x%08X", Status);
            }
            break;
        }

        /* ---- ATTACH_DISK ------------------------------------------------ */
        case ROSV_IOCTL_ATTACH_DISK:
        {
            PROSV_DISK_ATTACH_REQUEST DiskReq;
            PROSV_DISK_ATTACH_RESULT DiskRes;
            UNICODE_STRING FilePath;
            WCHAR NtPathBuf[300];
            OBJECT_ATTRIBUTES ObjAttrs;
            HANDLE FileHandle = NULL;
            HANDLE DemandFileHandle = NULL;
            HANDLE DemandFileHandleAlt = NULL;
            IO_STATUS_BLOCK IoStatusBlock;
            FILE_STANDARD_INFORMATION FileInfo;
            PVOID SectionObject = NULL;
            PVOID SectionObjectRef = NULL;
            PVOID MappedBase = NULL;
            SIZE_T ViewSize = 0;
            PVOID BatCopy = NULL;
            SIZE_T BatCopyBytes = 0;

            ROSV_TRACE("IOCTL: ATTACH_DISK");

            if (InputLength < sizeof(ROSV_DISK_ATTACH_REQUEST) || InputBuffer == NULL)
            {
                ROSV_ERR("ATTACH_DISK: Input buffer too small (%u < %u)",
                         InputLength, (ULONG)sizeof(ROSV_DISK_ATTACH_REQUEST));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_DISK_ATTACH_RESULT) || OutputBuffer == NULL)
            {
                ROSV_ERR("ATTACH_DISK: Output buffer too small (%u < %u)",
                         OutputLength, (ULONG)sizeof(ROSV_DISK_ATTACH_RESULT));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (g_CurrentVm == NULL)
            {
                ROSV_ERR("ATTACH_DISK: No VM created");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            if (g_DiskMappedBase != NULL || g_DiskSectionHandle != NULL ||
                g_DiskFileHandle != NULL || g_DiskFileHandleAlt != NULL)
            {
                ROSV_ERR("ATTACH_DISK: a disk is already attached");
                Status = STATUS_DEVICE_BUSY;
                break;
            }

            DiskReq = (PROSV_DISK_ATTACH_REQUEST)InputBuffer;
            DiskRes = (PROSV_DISK_ATTACH_RESULT)OutputBuffer;

            /* Validate path length */
            if (DiskReq->PathLength == 0 || DiskReq->PathLength >= ROSV_DISK_PATH_MAX)
            {
                ROSV_ERR("ATTACH_DISK: Invalid path length %u", DiskReq->PathLength);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            DiskReq->Path[DiskReq->PathLength] = L'\0';
            ROSV_TRACE("ATTACH_DISK: Path=\"%S\" (len=%u)", DiskReq->Path, DiskReq->PathLength);

            /* Reject paths with traversal components */
            if (wcsstr(DiskReq->Path, L"..") != NULL)
            {
                ROSV_ERR("ATTACH_DISK: path traversal rejected");
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Build NT path from the user-supplied wide-char path.
             * rosl.exe sends a Win32 path like C:\path\rootfs.img.
             * We need to convert it to \??\C:\path\rootfs.img for ZwOpenFile. */
            {
                UNICODE_STRING WinPath;
                UNICODE_STRING NtPrefix;

                RtlInitUnicodeString(&WinPath, DiskReq->Path);

                /* Prepend \??\ to make it an NT path */
                RtlInitUnicodeString(&NtPrefix, L"\\??\\");
                FilePath.Buffer = NtPathBuf;
                FilePath.Length = 0;
                FilePath.MaximumLength = sizeof(NtPathBuf);
                RtlCopyUnicodeString(&FilePath, &NtPrefix);
                RtlAppendUnicodeStringToString(&FilePath, &WinPath);

                ROSV_TRACE("ATTACH_DISK: NT path = \"%wZ\"", &FilePath);

                InitializeObjectAttributes(&ObjAttrs, &FilePath,
                                           OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                           NULL, NULL);
            }

            /* Open the disk image file read-only.
             * Guest writes are handled by the in-memory COW sector cache,
             * so the backing file is not modified in this mode.
             * Writeback mode, when enabled, should use write-capable handles
             * and FLUSH as the durability boundary.
             */
            Status = ZwOpenFile(&FileHandle,
                                GENERIC_READ | SYNCHRONIZE,
                                &ObjAttrs, &IoStatusBlock,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("ATTACH_DISK: ZwOpenFile failed, Status=0x%08X", Status);
                break;
            }

            /* Get file size */
            Status = ZwQueryInformationFile(FileHandle, &IoStatusBlock, &FileInfo,
                                            sizeof(FileInfo), FileStandardInformation);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("ATTACH_DISK: ZwQueryInformationFile failed, Status=0x%08X", Status);
                ZwClose(FileHandle);
                break;
            }

            ROSV_TRACE("ATTACH_DISK: File size = %llu bytes", FileInfo.EndOfFile.QuadPart);

            /* Create a read-only section for format parsing.
             * RAW backend keeps this mapped.
             * VHDX demand mode copies BAT metadata, then retires this mapping and
             * serves payload reads via explicit ZwReadFile from a dedicated handle. */
            Status = ZwCreateSection(&SectionObject,
                                     SECTION_MAP_READ | SECTION_QUERY,
                                     NULL, NULL, PAGE_READONLY, SEC_COMMIT, FileHandle);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("ATTACH_DISK: ZwCreateSection failed, Status=0x%08X", Status);
                ZwClose(FileHandle);
                break;
            }

            /*
             * Map the section into system space, not caller process space.
             * The vCPU thread is a system thread and must access this mapping
             * regardless of which process issued ATTACH_DISK.
             */
            Status = ObReferenceObjectByHandle((HANDLE)SectionObject,
                                               SECTION_MAP_READ,
                                               NULL,
                                               KernelMode,
                                               &SectionObjectRef,
                                               NULL);
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("ATTACH_DISK: ObReferenceObjectByHandle(section) failed, Status=0x%08X", Status);
                ZwClose(SectionObject);
                ZwClose(FileHandle);
                break;
            }

            ViewSize = (SIZE_T)FileInfo.EndOfFile.QuadPart;
            Status = MmMapViewInSystemSpace(SectionObjectRef, &MappedBase, &ViewSize);
            ObDereferenceObject(SectionObjectRef);
            SectionObjectRef = NULL;
            if (!NT_SUCCESS(Status))
            {
                ROSV_ERR("ATTACH_DISK: MmMapViewInSystemSpace failed, Status=0x%08X", Status);
                ZwClose(SectionObject);
                ZwClose(FileHandle);
                break;
            }

            ROSV_TRACE("ATTACH_DISK: Mapped at VA=%p, size=%llu bytes",
                       MappedBase, (ULONG64)ViewSize);

            /* Detect disk format and initialize backend */
            if (RosvVhdxIsVhdx(MappedBase, (ULONG64)ViewSize))
            {
                /* VHDX format detected — parse and use VHDX backend */
                ROSV_TRACE("ATTACH_DISK: Detected VHDX format, parsing...");

                Status = RosvVhdxOpen(&g_CurrentVm->VhdxState, MappedBase, (ULONG64)ViewSize);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("ATTACH_DISK: RosvVhdxOpen failed, Status=0x%08X", Status);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                /* Check log cleanliness */
                if (g_CurrentVm->VhdxState.NeedsLogReplay)
                {
                    Status = RosvVhdxLogReplay(&g_CurrentVm->VhdxState);
                    if (!NT_SUCCESS(Status))
                    {
                        ROSV_ERR("ATTACH_DISK: VHDX log replay failed/needed, Status=0x%08X", Status);
                        RosvVhdxClose(&g_CurrentVm->VhdxState);
                        MmUnmapViewInSystemSpace(MappedBase);
                        ZwClose(SectionObject);
                        ZwClose(FileHandle);
                        break;
                    }
                }

                if (g_CurrentVm->VhdxState.BatEntryCount == 0 ||
                    g_CurrentVm->VhdxState.BatBase == NULL)
                {
                    ROSV_ERR("ATTACH_DISK: invalid BAT metadata (entries=%u base=%p)",
                             g_CurrentVm->VhdxState.BatEntryCount,
                             g_CurrentVm->VhdxState.BatBase);
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                if ((ULONG64)g_CurrentVm->VhdxState.BatEntryCount >
                    (MAXULONG_PTR / sizeof(ULONG64)))
                {
                    ROSV_ERR("ATTACH_DISK: BAT entry count overflow (%u)",
                             g_CurrentVm->VhdxState.BatEntryCount);
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                /*
                 * Demand-paged VHDX backend (default path):
                 * keep metadata in memory, retire payload mapping, and serve block
                 * reads through synchronous ZwReadFile.
                 */
#if ROSV_VHDX_USE_DEMAND_PAGED_IO
                BatCopyBytes = (SIZE_T)g_CurrentVm->VhdxState.BatEntryCount * sizeof(ULONG64);
                if ((ULONG64)BatCopyBytes > g_CurrentVm->VhdxState.BatLength)
                {
                    ROSV_ERR("ATTACH_DISK: BAT copy size overflow (%zu > %u)",
                             BatCopyBytes,
                             g_CurrentVm->VhdxState.BatLength);
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                BatCopy = ExAllocatePoolWithTag(NonPagedPool, BatCopyBytes, ROSV_DRIVER_TAG);
                if (BatCopy == NULL)
                {
                    ROSV_ERR("ATTACH_DISK: failed to allocate BAT copy (%zu bytes)",
                             BatCopyBytes);
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                RtlCopyMemory(BatCopy, g_CurrentVm->VhdxState.BatBase, BatCopyBytes);

                Status = RosvVirtioBlkInitialize(&g_CurrentVm->VirtioBlk, g_CurrentVm,
                                                 NULL, (ULONG64)ViewSize,
                                                 FALSE /* read-write */,
                                                 ROSV_DISK_BACKEND_VHDX);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("ATTACH_DISK: RosvVirtioBlkInitialize (VHDX demand-paged) failed, "
                             "Status=0x%08X", Status);
                    ExFreePoolWithTag(BatCopy, ROSV_DRIVER_TAG);
                    BatCopy = NULL;
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                /*
                 * Open runtime demand handles as synchronous file objects.
                 * Demand I/O is serialized by the worker thread and uses explicit
                 * byte offsets, so this keeps file-object semantics predictable.
                 * Later on, these handles must grow GENERIC_WRITE with clear
                 * sync/ordering rules for writeback correctness.
                 */
                Status = ZwOpenFile(&DemandFileHandle,
                                    GENERIC_READ | SYNCHRONIZE,
                                    &ObjAttrs,
                                    &IoStatusBlock,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_NON_DIRECTORY_FILE |
                                    FILE_NO_INTERMEDIATE_BUFFERING |
                                    FILE_SYNCHRONOUS_IO_NONALERT);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("ATTACH_DISK: ZwOpenFile(runtime-demand) failed, Status=0x%08X", Status);
                    RosvVirtioBlkDestroy(&g_CurrentVm->VirtioBlk);
                    ExFreePoolWithTag(BatCopy, ROSV_DRIVER_TAG);
                    BatCopy = NULL;
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                Status = ZwOpenFile(&DemandFileHandleAlt,
                                    GENERIC_READ | SYNCHRONIZE,
                                    &ObjAttrs,
                                    &IoStatusBlock,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    FILE_NON_DIRECTORY_FILE |
                                    FILE_NO_INTERMEDIATE_BUFFERING |
                                    FILE_SYNCHRONOUS_IO_NONALERT);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("ATTACH_DISK: ZwOpenFile(runtime-demand-alt) failed, Status=0x%08X", Status);
                    ZwClose(DemandFileHandle);
                    DemandFileHandle = NULL;
                    RosvVirtioBlkDestroy(&g_CurrentVm->VirtioBlk);
                    ExFreePoolWithTag(BatCopy, ROSV_DRIVER_TAG);
                    BatCopy = NULL;
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                g_CurrentVm->VhdxState.BatBase = BatCopy;
                g_CurrentVm->VhdxState.BatLength = (ULONG)BatCopyBytes;

                g_CurrentVm->VirtioBlk.Mode = ROSV_DISK_MODE_DEMAND_PAGED;
                g_CurrentVm->VirtioBlk.DiskFileHandle = DemandFileHandle;
                g_CurrentVm->VirtioBlk.DiskFileHandleAlt = DemandFileHandleAlt;
                g_CurrentVm->VirtioBlk.PreferredReadHandle = 0;
                DemandFileHandle = NULL;
                DemandFileHandleAlt = NULL;

                g_CurrentVm->VhdxState.FileBase = NULL;
                g_CurrentVm->VhdxState.MetadataBase = NULL;
                if (MappedBase != NULL)
                {
                    MmUnmapViewInSystemSpace(MappedBase);
                    MappedBase = NULL;
                    ViewSize = 0;
                }
                if (SectionObject != NULL)
                {
                    ZwClose(SectionObject);
                    SectionObject = NULL;
                }
                if (FileHandle != NULL)
                {
                    ZwClose(FileHandle); /* retire metadata parsing handle */
                    FileHandle = NULL;
                }
                FileHandle = g_CurrentVm->VirtioBlk.DiskFileHandle;

                ROSV_TRACE("ATTACH_DISK: VHDX demand-paged backend active, "
                           "virtual_size=%llu MB, block_size=%u, BAT=%u entries, bat_bytes=%zu (payload mapping retired, preferred_handle=%s)",
                           g_CurrentVm->VhdxState.VirtualDiskSize / (1024*1024),
                           g_CurrentVm->VhdxState.BlockSize,
                           g_CurrentVm->VhdxState.BatEntryCount,
                           BatCopyBytes,
                           (g_CurrentVm->VirtioBlk.PreferredReadHandle != 0) ? "alt" : "primary");
                DiskRes->DiskSizeBytes = g_CurrentVm->VhdxState.VirtualDiskSize;

                g_DiskFileHandle = FileHandle;
                g_DiskFileHandleAlt = g_CurrentVm->VirtioBlk.DiskFileHandleAlt;
                g_DiskSectionHandle = NULL;
                g_DiskMappedBase = NULL;
                g_DiskMappedSize = 0;
#else
                Status = RosvVirtioBlkInitialize(&g_CurrentVm->VirtioBlk, g_CurrentVm,
                                                 MappedBase, (ULONG64)ViewSize,
                                                 FALSE /* read-write */,
                                                 ROSV_DISK_BACKEND_VHDX);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("ATTACH_DISK: RosvVirtioBlkInitialize (VHDX mapped) failed, Status=0x%08X", Status);
                    RosvVhdxClose(&g_CurrentVm->VhdxState);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                DiskRes->DiskSizeBytes = g_CurrentVm->VhdxState.VirtualDiskSize;
                g_DiskFileHandle = FileHandle;
                g_DiskFileHandleAlt = NULL;
                g_DiskSectionHandle = SectionObject;
                g_DiskMappedBase = MappedBase;
                g_DiskMappedSize = ViewSize;
                ROSV_TRACE("ATTACH_DISK: VHDX mapped backend active, virtual_size=%llu MB, block_size=%u",
                           g_CurrentVm->VhdxState.VirtualDiskSize / (1024 * 1024),
                           g_CurrentVm->VhdxState.BlockSize / (1024 * 1024));
#endif
            }
            else
            {
                /* Raw flat image — read-only mapping, COW sector cache handles writes */
                ROSV_TRACE("ATTACH_DISK: Raw image format, using ramdisk backend");

                Status = RosvVirtioBlkInitialize(&g_CurrentVm->VirtioBlk, g_CurrentVm,
                                                 MappedBase, (ULONG64)ViewSize,
                                                 FALSE /* read-write */,
                                                 ROSV_DISK_BACKEND_RAW);
                if (!NT_SUCCESS(Status))
                {
                    ROSV_ERR("ATTACH_DISK: RosvVirtioBlkInitialize (RAW) failed, Status=0x%08X", Status);
                    MmUnmapViewInSystemSpace(MappedBase);
                    ZwClose(SectionObject);
                    ZwClose(FileHandle);
                    break;
                }

                DiskRes->DiskSizeBytes = (ULONG64)ViewSize;

                /* Save handles for cleanup - ramdisk keeps all mappings */
                g_DiskFileHandle = FileHandle;
                g_DiskFileHandleAlt = NULL;
                g_DiskSectionHandle = SectionObject;
                g_DiskMappedBase = MappedBase;
                g_DiskMappedSize = ViewSize;
            }

            /* Fill result */
            DiskRes->DiskIndex = 0; /* First disk = /dev/vda */
            DiskRes->Status = STATUS_SUCCESS;
            DiskRes->DiskMode = (ULONG)g_CurrentVm->VirtioBlk.Mode;
            DiskRes->BackendType = g_CurrentVm->VirtioBlk.BackendType;
            OutputBytes = sizeof(ROSV_DISK_ATTACH_RESULT);
            Status = STATUS_SUCCESS;

            ROSV_TRACE("ATTACH_DISK: Success - disk 0, %llu bytes, %s, %s",
                       DiskRes->DiskSizeBytes,
                       (g_CurrentVm->VirtioBlk.Mode == ROSV_DISK_MODE_DEMAND_PAGED) ?
                           "demand-paged" : "ramdisk",
                       (g_CurrentVm->VirtioBlk.BackendType == ROSV_DISK_BACKEND_VHDX) ?
                           "VHDX" : "RAW");
            break;
        }

        /* ---- DETACH_DISK ------------------------------------------------ */
        case ROSV_IOCTL_DETACH_DISK:
        {
            ROSV_TRACE("IOCTL: DETACH_DISK");

            if (g_DiskMappedBase == NULL &&
                g_DiskSectionHandle == NULL &&
                g_DiskFileHandle == NULL &&
                g_DiskFileHandleAlt == NULL)
            {
                ROSV_WARN("DETACH_DISK: No disk is attached");
                Status = STATUS_SUCCESS;  /* Idempotent */
                break;
            }

            RosvDetachDiskInternal(g_CurrentVm);

            ROSV_TRACE("DETACH_DISK: Disk detached successfully");
            Status = STATUS_SUCCESS;
            break;
        }

        /* ---- VCON_PORT_WRITE -------------------------------------------- */
        case ROSV_IOCTL_VCON_PORT_WRITE:
        {
            PROSV_VCON_PORT_IO Req;
            ULONG BytesWritten = 0;
            PROSV_VM LocalVm;

            if (InputLength < FIELD_OFFSET(ROSV_VCON_PORT_IO, Data) || InputBuffer == NULL)
            {
                ROSV_ERR("VCON_PORT_WRITE: Input buffer too small (%u)", InputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Req = (PROSV_VCON_PORT_IO)InputBuffer;

            if (Req->Length == 0 || Req->Length > sizeof(Req->Data))
            {
                ROSV_ERR("VCON_PORT_WRITE: Invalid length %u", Req->Length);
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (InputLength < FIELD_OFFSET(ROSV_VCON_PORT_IO, Data) + Req->Length)
            {
                ROSV_ERR("VCON_PORT_WRITE: Input buffer too small for data (%u < %u)",
                         InputLength,
                         (ULONG)(FIELD_OFFSET(ROSV_VCON_PORT_IO, Data) + Req->Length));
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            LocalVm = g_CurrentVm;
            KeMemoryBarrier();
            if (LocalVm == NULL)
            {
                ROSV_ERR("VCON_PORT_WRITE: No VM running");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            Status = RosvVirtioConPortWrite(&LocalVm->VirtioCon,
                                            Req->PortIndex,
                                            Req->Data,
                                            Req->Length,
                                            &BytesWritten);
            if (NT_SUCCESS(Status))
            {
                ROSV_TRACE("VCON_PORT_WRITE: port %u, %u/%u bytes queued",
                           Req->PortIndex, BytesWritten, Req->Length);
            }
            else
            {
                ROSV_ERR("VCON_PORT_WRITE: port %u failed, Status=0x%08X",
                         Req->PortIndex, Status);
            }
            break;
        }

        /* ---- VCON_PORT_READ --------------------------------------------- */
        case ROSV_IOCTL_VCON_PORT_READ:
        {
            PROSV_VCON_PORT_IO Req;
            PROSV_VCON_PORT_IO Res;
            ULONG BytesRead = 0;
            PROSV_VM LocalVm;

            if (InputLength < FIELD_OFFSET(ROSV_VCON_PORT_IO, Data) || InputBuffer == NULL)
            {
                ROSV_ERR("VCON_PORT_READ: Input buffer too small (%u)", InputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            if (OutputLength < sizeof(ROSV_VCON_PORT_IO) || OutputBuffer == NULL)
            {
                ROSV_ERR("VCON_PORT_READ: Output buffer too small (%u)", OutputLength);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Req = (PROSV_VCON_PORT_IO)InputBuffer;
            Res = (PROSV_VCON_PORT_IO)OutputBuffer;

            LocalVm = g_CurrentVm;
            KeMemoryBarrier();
            if (LocalVm == NULL)
            {
                ROSV_ERR("VCON_PORT_READ: No VM running");
                Status = STATUS_INVALID_DEVICE_STATE;
                break;
            }

            Status = RosvVirtioConPortRead(&LocalVm->VirtioCon,
                                           Req->PortIndex,
                                           Res->Data,
                                           sizeof(Res->Data),
                                           &BytesRead);
            if (NT_SUCCESS(Status))
            {
                Res->PortIndex = Req->PortIndex;
                Res->Length = BytesRead;
                OutputBytes = FIELD_OFFSET(ROSV_VCON_PORT_IO, Data) + BytesRead;

                if (BytesRead > 0)
                {
                    RosvVirtioConKickTx(&LocalVm->VirtioCon, Req->PortIndex);
                }
            }
            else
            {
                ROSV_ERR("VCON_PORT_READ: port %u failed, Status=0x%08X",
                         Req->PortIndex, Status);
            }
            break;
        }

        /* ---- Unknown IOCTL ---------------------------------------------- */
        default:
            ROSV_WARN("Unknown IOCTL code: 0x%08X", IoControlCode);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    if (IoControlCode != ROSV_IOCTL_NET_TX && IoControlCode != ROSV_IOCTL_NET_RX)
        KeReleaseMutex(&g_StateMutex, FALSE);

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = NT_SUCCESS(Status) ? OutputBytes : 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* ---- Driver Unload ------------------------------------------------------ */

static VOID
RosvDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    NTSTATUS WaitStatus;
    UNICODE_STRING SymlinkName;

    UNREFERENCED_PARAMETER(DriverObject);

    WaitStatus = KeWaitForSingleObject(&g_StateMutex,
                                       Executive,
                                       KernelMode,
                                       FALSE,
                                       NULL);
    if (!NT_SUCCESS(WaitStatus))
    {
        ROSV_ERR("Driver unload: failed to acquire state mutex, Status=0x%08X", WaitStatus);
    }

    ROSV_TRACE("Driver unloading");

    if (g_CurrentVm != NULL)
    {
        PROSV_VM VmToDestroy = g_CurrentVm;

        g_CurrentVm = NULL;
        g_NetStub.Attached = FALSE;
        g_NetStub.BackendType = RosvNetBackendNone;

        RosvVmWaitForReferencesReleased(VmToDestroy);

        ROSV_TRACE("Destroying active VM before unload");
        if (VmToDestroy->State == RosvVmStateRunning)
        {
            ROSV_TRACE("VM is still running, stopping it first");
            RosvVmStop(VmToDestroy);
        }

        if (g_DiskMappedBase != NULL || g_DiskSectionHandle != NULL ||
            g_DiskFileHandle != NULL || g_DiskFileHandleAlt != NULL)
        {
            ROSV_TRACE("Unloading: detaching disk backend");
            RosvDetachDiskInternal(VmToDestroy);
        }

        RosvVmDestroy(VmToDestroy);
    }
    else if (g_DiskMappedBase != NULL || g_DiskSectionHandle != NULL ||
             g_DiskFileHandle != NULL || g_DiskFileHandleAlt != NULL)
    {
        ROSV_TRACE("Unloading: detaching orphaned disk backend");
        RosvDetachDiskInternal(NULL);
    }

    RtlInitUnicodeString(&SymlinkName, ROSV_SYMLINK_NAME);
    IoDeleteSymbolicLink(&SymlinkName);

    if (g_DeviceObject != NULL)
    {
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }

    if (NT_SUCCESS(WaitStatus))
    {
        KeReleaseMutex(&g_StateMutex, FALSE);
    }

    ROSV_TRACE("ROSV Hypervisor driver unloaded");
}

/* ---- Driver Entry ------------------------------------------------------- */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    UNICODE_STRING DeviceName;
    UNICODE_STRING SymlinkName;

    UNREFERENCED_PARAMETER(RegistryPath);

    ROSV_TRACE("=== ROSV Hypervisor driver loading ===");

    /* Probe VMX capability */
    Status = RosvVmxDetectCapabilities(&g_VmxCaps);
    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("VMX capability probe failed: Status=0x%08X", Status);
        ROSV_ERR("This CPU does not support VMX or VMX is disabled in BIOS");
        return STATUS_NOT_SUPPORTED;
    }

    ROSV_TRACE("VMX capabilities detected:");
    ROSV_TRACE("  VMCS revision ID:    0x%X", g_VmxCaps.VmcsRevisionId);
    ROSV_TRACE("  VMCS region size:    %u bytes", g_VmxCaps.VmcsRegionSize);
    ROSV_TRACE("  True controls:       %s", g_VmxCaps.TrueCtlsAvailable ? "yes" : "no");
    ROSV_TRACE("  Secondary controls:  %s", g_VmxCaps.SecondaryCtlsAvailable ? "yes" : "no");
    ROSV_TRACE("  EPT support:         %s",
               (g_VmxCaps.ProcBased2Allowed1 & VMX_PROC2_EPT) ? "yes" : "no");
    ROSV_TRACE("  Unrestricted guest:  %s",
               (g_VmxCaps.ProcBased2Allowed1 & VMX_PROC2_UNRESTRICTED_GUEST) ? "yes" : "no");

    /* Create device object */
    RtlInitUnicodeString(&DeviceName, ROSV_DEVICE_NAME);
    Status = IoCreateDevice(
        DriverObject,
        0,
        &DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject);

    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("IoCreateDevice failed: 0x%08X", Status);
        return Status;
    }

    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    KeInitializeMutex(&g_StateMutex, 0);
    ROSV_TRACE("Device created: %wZ", &DeviceName);

    /* Create symbolic link for user-mode access */
    RtlInitUnicodeString(&SymlinkName, ROSV_SYMLINK_NAME);
    Status = IoCreateSymbolicLink(&SymlinkName, &DeviceName);

    if (!NT_SUCCESS(Status))
    {
        ROSV_ERR("IoCreateSymbolicLink failed: 0x%08X", Status);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        return Status;
    }

    ROSV_TRACE("Symbolic link created: %wZ", &SymlinkName);

    /* Set up dispatch routines */
    DriverObject->MajorFunction[IRP_MJ_CREATE] = RosvDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = RosvDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = RosvDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RosvDispatchIoctl;
    DriverObject->DriverUnload = RosvDriverUnload;
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    ROSV_TRACE("ROSV Hypervisor driver loaded successfully, VMX revision=0x%X",
               g_VmxCaps.VmcsRevisionId);

    return STATUS_SUCCESS;
}
