/*
 * PROJECT:     ReactOS Display Driver Model - Win32k/dxgkrnl Bridge
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3DKMT kernel-mode entry points (IOCTL bridge to dxgkrnl)
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 *
 * OVERVIEW
 * --------
 * This file provides the win32k-side D3DKMT* functions that marshal
 * parameters into METHOD_BUFFERED IOCTLs and send them to dxgkrnl via
 * WddmBridgeSendIoctl.
 *
 * The call chain is:
 *
 *   User-mode D3D/DXGI runtime
 *       |  NtGdiDdDDI* syscall
 *       v
 *   win32ss/gdi/ntgdi/d3dkmt.c   (dispatch via callback table)
 *       |  D3DKMT* call (this file)
 *       v
 *   WddmBridgeSendIoctl  ->  \Device\DxgKrnl  (dxgkrnl.sys)
 */

#include <ntifs.h>
#include <windef.h>
#include <pseh/pseh2.h>
#include "wddm_bridge.h"
#include <d3dkmthk.h>
#include <reactos/rddm/rxgkinterface.h>
#include <reactos/rddm/rxgkioctl.h>
#define NDEBUG
#include <debug.h>

extern NTKERNELAPI PVOID MmHighestUserAddress;

C_ASSERT(sizeof(RXGK_CREATECONTEXTVIRTUAL_PACKET) == RXGK_CREATECONTEXTVIRTUAL_PACKET_V1_SIZE);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, Size) == 0);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, Version) == 4);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, DeviceHandle) == 8);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, NodeOrdinal) == 12);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, EngineAffinity) == 16);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, Flags) == 20);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, ClientHint) == 24);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, PrivateDriverDataSize) == 28);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, PrivateDriverDataOffset) == 32);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, ContextHandle) == 36);
C_ASSERT(FIELD_OFFSET(RXGK_CREATECONTEXTVIRTUAL_PACKET, Reserved) == 40);
C_ASSERT(sizeof(RXGK_SUBMITCOMMAND_PACKET) == RXGK_SUBMITCOMMAND_PACKET_V1_SIZE);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, Size) == 0);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, Version) == 4);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, Commands) == 8);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, CommandLength) == 16);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, Flags) == 20);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, PresentHistoryToken) == 24);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, ContextHandle) == 32);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, PrivateDriverDataSize) == 36);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, PrivateDriverDataOffset) == 40);
C_ASSERT(FIELD_OFFSET(RXGK_SUBMITCOMMAND_PACKET, Reserved) == 44);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_1_SIZE == FIELD_OFFSET(REACTOS_WIN32K_DXGKRNL_INTERFACE, RxgkIntPfnCreateContextVirtual));
C_ASSERT(DXGKRNL_INTERFACE_VERSION_2_SIZE == FIELD_OFFSET(REACTOS_WIN32K_DXGKRNL_INTERFACE, RxgkIntPfnCreateAllocation2));
C_ASSERT(DXGKRNL_INTERFACE_VERSION_3_SIZE == FIELD_OFFSET(REACTOS_WIN32K_DXGKRNL_INTERFACE, RxgkIntPfnGetAllocationPriority));
C_ASSERT(DXGKRNL_INTERFACE_VERSION_5_SIZE == sizeof(REACTOS_WIN32K_DXGKRNL_INTERFACE));
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO, hAllocation) == 0);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, hAllocation) == 0);
#if defined(_WIN64)
C_ASSERT(DXGKRNL_INTERFACE_VERSION_1_SIZE == 536);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_2_SIZE == 552);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_3_SIZE == 560);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_4_SIZE == 568);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_5_SIZE == 576);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO) == 40);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO, pSystemMem) == 8);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO, pPrivateDriverData) == 16);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO, Flags) == 32);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO2) == 96);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, pSystemMem) == 8);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, pPrivateDriverData) == 16);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, Flags) == 32);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, GpuVirtualAddress) == 40);
#if ((DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2) || (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_2))
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, Reserved) == 56);
#else
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, Reserved) == 48);
#endif
C_ASSERT(sizeof(D3DKMT_DESTROYALLOCATION2) == 24);
C_ASSERT(FIELD_OFFSET(D3DKMT_DESTROYALLOCATION2, phAllocationList) == 8);
C_ASSERT(FIELD_OFFSET(D3DKMT_DESTROYALLOCATION2, Flags) == 20);
C_ASSERT(sizeof(D3DKMT_LOCK2) == 24);
C_ASSERT(FIELD_OFFSET(D3DKMT_LOCK2, pData) == 16);
#else
C_ASSERT(DXGKRNL_INTERFACE_VERSION_1_SIZE == 268);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_2_SIZE == 276);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_3_SIZE == 280);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_4_SIZE == 284);
C_ASSERT(DXGKRNL_INTERFACE_VERSION_5_SIZE == 288);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO) == 24);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO, pSystemMem) == 4);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO, pPrivateDriverData) == 8);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO, Flags) == 20);
C_ASSERT(sizeof(D3DDDI_ALLOCATIONINFO2) == 56);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, pSystemMem) == 4);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, pPrivateDriverData) == 8);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, Flags) == 20);
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, GpuVirtualAddress) == 24);
#if ((DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_2) || (D3D_UMD_INTERFACE_VERSION >= D3D_UMD_INTERFACE_VERSION_WDDM2_2))
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, Reserved) == 36);
#else
C_ASSERT(FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, Reserved) == 32);
#endif
C_ASSERT(sizeof(D3DKMT_DESTROYALLOCATION2) == 20);
C_ASSERT(FIELD_OFFSET(D3DKMT_DESTROYALLOCATION2, phAllocationList) == 8);
C_ASSERT(FIELD_OFFSET(D3DKMT_DESTROYALLOCATION2, Flags) == 16);
C_ASSERT(sizeof(D3DKMT_LOCK2) == 16);
C_ASSERT(FIELD_OFFSET(D3DKMT_LOCK2, pData) == 12);
#endif
C_ASSERT(sizeof(D3DKMT_UNLOCK2) == 8);

/* Import the IOCTL code definitions from dxgkrnl */
#define DXGKRNL_DEVICE_TYPE     0x23

#define IOCTL_D3DKMT_OPENADAPTERFROMHDC \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x110, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x111, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x112, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_REGISTERTRIMNOTIFICATION \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x199, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UNREGISTERTRIMNOTIFICATION \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x19A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEKEYEDMUTEX \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x190, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYKEYEDMUTEX \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x191, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENKEYEDMUTEX \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x192, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ACQUIREKEYEDMUTEX \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x193, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RELEASEKEYEDMUTEX \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x194, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEKEYEDMUTEX2 \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x195, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENKEYEDMUTEX2 \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x196, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ACQUIREKEYEDMUTEX2 \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x197, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RELEASEKEYEDMUTEX2 \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x198, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CLOSEADAPTER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x103, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYADAPTERINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x104, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x120, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYDEVICE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x121, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEALLOCATION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x130, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEALLOCATION2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x138, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYALLOCATION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x131, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_LOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x132, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UNLOCK \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x133, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYRESOURCEINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x134, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENRESOURCE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x135, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x136, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RENDER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x140, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_PRESENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x141, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x150, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x151, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETDISPLAYMODELIST \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x105, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETDISPLAYMODE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x160, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_D3DKMT_CREATECONTEXT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x122, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYCONTEXT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x123, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x152, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x153, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ESCAPE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x170, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETVIDPNSOURCEOWNER \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x161, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETDEVICESTATE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x124, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TAG_WDDM_BRIDGE             'BmdW'
#define D3DKMT_BRIDGE_MAX_ALLOCATIONS 4096U
#define D3DKMT_BRIDGE_MAX_PRIVATE_BYTES RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA
#define D3DKMT_BRIDGE_MAX_FIXED_BYTES 65536U
#define D3DKMT_BRIDGE_MAX_ENUM_ADAPTERS 256U

/*
 * Windows 11 reports an unreadable or unwritable caller buffer as
 * STATUS_INVALID_PARAMETER on most D3DKMT entry points rather than letting the
 * access violation reach the caller.  The rule is not uniform -- OpenResource
 * still surfaces STATUS_ACCESS_VIOLATION -- so this is applied at the entry
 * points measured to diverge, never inside the shared capture helpers.
 */
static NTSTATUS
WddmBridgeRejectBadBuffer(
    _In_ NTSTATUS Status)
{
    return (Status == STATUS_ACCESS_VIOLATION) ? STATUS_INVALID_PARAMETER : Status;
}

static NTSTATUS
WddmBridgeSafeCopyFrom(
    _Out_writes_bytes_(Size) PVOID Destination,
    _In_reads_bytes_(Size) CONST VOID *Source,
    _In_ SIZE_T Size)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Size == 0)
        return STATUS_SUCCESS;

    if (Destination == NULL || Source == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForRead(Source, Size, 1);
        RtlCopyMemory(Destination, Source, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static NTSTATUS
WddmBridgeSafeCopyTo(
    _Out_writes_bytes_(Size) PVOID Destination,
    _In_reads_bytes_(Size) CONST VOID *Source,
    _In_ SIZE_T Size)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Size == 0)
        return STATUS_SUCCESS;

    if (Destination == NULL || Source == NULL)
        return STATUS_INVALID_PARAMETER;

    _SEH2_TRY
    {
        if (ExGetPreviousMode() != KernelMode)
            ProbeForWrite(Destination, Size, 1);
        RtlCopyMemory(Destination, Source, Size);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static NTSTATUS
WddmBridgeSafeProbeForWrite(
    _Out_writes_bytes_(Size) PVOID Destination,
    _In_ SIZE_T Size)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (Size == 0)
        return STATUS_SUCCESS;

    if (Destination == NULL)
        return STATUS_INVALID_PARAMETER;

    if (ExGetPreviousMode() == KernelMode)
        return STATUS_SUCCESS;

    _SEH2_TRY
    {
        ProbeForWrite(Destination, Size, 1);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

static NTSTATUS
WddmBridgeSizeAdd(
    _In_ SIZE_T Left,
    _In_ SIZE_T Right,
    _Out_ SIZE_T *Size)
{
    if (Size == NULL || Left > ((SIZE_T)-1) - Right)
        return STATUS_INVALID_PARAMETER;

    *Size = Left + Right;
    if (*Size > MAXULONG)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static NTSTATUS
WddmBridgeSizeForCount(
    _In_ UINT Count,
    _In_ SIZE_T ElementSize,
    _Out_ SIZE_T *Size)
{
    if (Size == NULL || ElementSize == 0)
        return STATUS_INVALID_PARAMETER;

    if ((SIZE_T)Count > (((SIZE_T)-1) / ElementSize))
        return STATUS_INVALID_PARAMETER;

    *Size = (SIZE_T)Count * ElementSize;
    return STATUS_SUCCESS;
}

static NTSTATUS
WddmBridgeCaptureFixedIoctl(
    _In_ ULONG IoControlCode,
    _Inout_updates_bytes_(Size) PVOID UserData,
    _In_ ULONG Size,
    _In_ BOOLEAN CopyBack)
{
    PVOID Captured;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (UserData == NULL || Size == 0 || Size > D3DKMT_BRIDGE_MAX_FIXED_BYTES)
        return STATUS_INVALID_PARAMETER;

    Captured = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_WDDM_BRIDGE);
    if (Captured == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = WddmBridgeSafeCopyFrom(Captured, UserData, Size);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (CopyBack)
    {
        Status = WddmBridgeSafeProbeForWrite(UserData, Size);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Status = WddmBridgeSendIoctlWithInformation(IoControlCode, Captured, Size, CopyBack ? Captured : NULL, CopyBack ? Size : 0, &Information);
    if (NT_SUCCESS(Status) && CopyBack && Information != Size)
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status) && CopyBack)
        Status = WddmBridgeSafeCopyTo(UserData, Captured, Size);

Cleanup:
    ExFreePoolWithTag(Captured, TAG_WDDM_BRIDGE);
    return Status;
}

static NTSTATUS
WddmBridgeCaptureArray(
    _In_opt_ CONST VOID *UserBuffer,
    _In_ UINT Count,
    _In_ SIZE_T ElementSize,
    _In_ UINT MaximumCount,
    _In_ BOOLEAN CopyInput,
    _In_ BOOLEAN ProbeOutput,
    _Outptr_result_bytebuffer_(*BufferSize) PVOID *CapturedBuffer,
    _Out_ SIZE_T *BufferSize)
{
    NTSTATUS Status;

    if (CapturedBuffer == NULL || BufferSize == NULL)
        return STATUS_INVALID_PARAMETER;

    *CapturedBuffer = NULL;
    *BufferSize = 0;
    if (Count == 0)
        return STATUS_SUCCESS;
    if (Count > MaximumCount || UserBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSizeForCount(Count, ElementSize, BufferSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (ProbeOutput)
    {
        Status = WddmBridgeSafeProbeForWrite((PVOID)UserBuffer, *BufferSize);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    *CapturedBuffer = ExAllocatePoolWithTag(NonPagedPool, *BufferSize, TAG_WDDM_BRIDGE);
    if (*CapturedBuffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(*CapturedBuffer, *BufferSize);
    if (!CopyInput)
        return STATUS_SUCCESS;

    Status = WddmBridgeSafeCopyFrom(*CapturedBuffer, UserBuffer, *BufferSize);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(*CapturedBuffer, TAG_WDDM_BRIDGE);
        *CapturedBuffer = NULL;
        *BufferSize = 0;
    }

    return Status;
}

static BOOLEAN
WddmBridgeIsKernelPointerForUser(
    _In_opt_ CONST VOID *Pointer)
{
    return ExGetPreviousMode() != KernelMode && Pointer != NULL && Pointer > MmHighestUserAddress;
}

static NTSTATUS
WddmBridgeOpenAdapterIoctl(
    _In_ ULONG IoControlCode,
    _Inout_updates_bytes_(Size) PVOID UserData,
    _In_ ULONG Size,
    _In_ ULONG HandleOffset)
{
    D3DKMT_CLOSEADAPTER CloseAdapter;
    D3DKMT_HANDLE AdapterHandle = 0;
    PVOID Captured;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (UserData == NULL || Size < sizeof(AdapterHandle) || Size > D3DKMT_BRIDGE_MAX_FIXED_BYTES || HandleOffset > Size - sizeof(AdapterHandle))
        return STATUS_INVALID_PARAMETER;

    Captured = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_WDDM_BRIDGE);
    if (Captured == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = WddmBridgeSafeCopyFrom(Captured, UserData, Size);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeSafeProbeForWrite(UserData, Size);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    *(D3DKMT_HANDLE *)((PUCHAR)Captured + HandleOffset) = 0;
    Status = WddmBridgeSendIoctlWithInformation(IoControlCode, Captured, Size, Captured, Size, &Information);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    AdapterHandle = *(D3DKMT_HANDLE *)((PUCHAR)Captured + HandleOffset);
    if (Information != Size || AdapterHandle == 0)
    {
        Status = Information != Size ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_HANDLE;
        goto Rollback;
    }

    Status = WddmBridgeSafeCopyTo(UserData, Captured, Size);
    if (NT_SUCCESS(Status))
        goto Cleanup;

Rollback:
    if (AdapterHandle != 0)
    {
        CloseAdapter.hAdapter = AdapterHandle;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("WddmBridgeOpenAdapterIoctl: rollback of adapter 0x%X failed with 0x%08lX\n", AdapterHandle, CleanupStatus);
    }

Cleanup:
    ExFreePoolWithTag(Captured, TAG_WDDM_BRIDGE);
    return Status;
}

/* ---- Adapter management -------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromHdc(
    _Inout_ D3DKMT_OPENADAPTERFROMHDC *pData)
{
    return WddmBridgeOpenAdapterIoctl(IOCTL_D3DKMT_OPENADAPTERFROMHDC, pData, sizeof(*pData), FIELD_OFFSET(D3DKMT_OPENADAPTERFROMHDC, hAdapter));
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromGdiDisplayName(
    _Inout_ D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME *pData)
{
    return WddmBridgeOpenAdapterIoctl(IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME, pData, sizeof(*pData), FIELD_OFFSET(D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME, hAdapter));
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromDeviceName(
    _Inout_ D3DKMT_OPENADAPTERFROMDEVICENAME *pData)
{
    D3DKMT_OPENADAPTERFROMDEVICENAME Captured;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    PCWSTR UserDeviceName;
    WCHAR DeviceName[260];
    ULONG_PTR Information = 0;
    ULONG Index;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    UserDeviceName = Captured.pDeviceName;
    if (UserDeviceName == NULL || WddmBridgeIsKernelPointerForUser(UserDeviceName) || (ULONG_PTR)UserDeviceName > MAXULONG_PTR - sizeof(DeviceName))
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < RTL_NUMBER_OF(DeviceName); ++Index)
    {
        Status = WddmBridgeSafeCopyFrom(&DeviceName[Index], &UserDeviceName[Index], sizeof(DeviceName[Index]));
        if (!NT_SUCCESS(Status))
            return Status;
        if (DeviceName[Index] == L'\0')
            break;
    }
    if (Index == 0 || Index == RTL_NUMBER_OF(DeviceName))
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        return Status;
    Captured.pDeviceName = DeviceName;
    Captured.hAdapter = 0;
    RtlZeroMemory(&Captured.AdapterLuid, sizeof(Captured.AdapterLuid));
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_OPENADAPTERFROMDEVICENAME, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(Captured) || Captured.hAdapter == 0)
        Status = Information != sizeof(Captured) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_HANDLE;
    if (NT_SUCCESS(Status))
    {
        Captured.pDeviceName = UserDeviceName;
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));
    }
    if (!NT_SUCCESS(Status) && Captured.hAdapter != 0)
    {
        CloseAdapter.hAdapter = Captured.hAdapter;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTOpenAdapterFromDeviceName: rollback of adapter 0x%X failed with 0x%08lX\n", Captured.hAdapter, CleanupStatus);
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTCloseAdapter(
    _In_ CONST D3DKMT_CLOSEADAPTER *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_CLOSEADAPTER, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTQueryAdapterInfo(
    _In_ CONST D3DKMT_QUERYADAPTERINFO *pData)
{
    D3DKMT_QUERYADAPTERINFO Captured;
    PVOID PrivateDriverData = NULL;
    PVOID UserPrivateDriverData;
    SIZE_T PrivateDriverDataSize = 0;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    UserPrivateDriverData = Captured.pPrivateDriverData;
    Status = WddmBridgeCaptureArray(UserPrivateDriverData, Captured.PrivateDriverDataSize, sizeof(UCHAR), D3DKMT_BRIDGE_MAX_PRIVATE_BYTES, TRUE, TRUE, &PrivateDriverData, &PrivateDriverDataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.pPrivateDriverData = PrivateDriverData;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_QUERYADAPTERINFO, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status) && PrivateDriverData != NULL)
        Status = WddmBridgeSafeCopyTo(UserPrivateDriverData, PrivateDriverData, PrivateDriverDataSize);

    if (PrivateDriverData != NULL)
        ExFreePoolWithTag(PrivateDriverData, TAG_WDDM_BRIDGE);
    return Status;
}

/* ---- Device management --------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateDevice(
    _Inout_ D3DKMT_CREATEDEVICE *pData)
{
    D3DKMT_CREATEDEVICE Captured;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.hDevice = 0;
    Captured.pCommandBuffer = NULL;
    Captured.CommandBufferSize = 0;
    Captured.pAllocationList = NULL;
    Captured.AllocationListSize = 0;
    Captured.pPatchLocationList = NULL;
    Captured.PatchLocationListSize = 0;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_CREATEDEVICE, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(Captured) || Captured.hDevice == 0)
        Status = Information != sizeof(Captured) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_HANDLE;
    else if (WddmBridgeIsKernelPointerForUser(Captured.pCommandBuffer) || WddmBridgeIsKernelPointerForUser(Captured.pAllocationList) || WddmBridgeIsKernelPointerForUser(Captured.pPatchLocationList))
        Status = STATUS_INVALID_ADDRESS;
    else
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));

    if (!NT_SUCCESS(Status) && Captured.hDevice != 0)
    {
        DestroyDevice.hDevice = Captured.hDevice;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYDEVICE, &DestroyDevice, sizeof(DestroyDevice), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTCreateDevice: rollback of device 0x%X failed with 0x%08lX\n", Captured.hDevice, CleanupStatus);
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTDestroyDevice(
    _In_ CONST D3DKMT_DESTROYDEVICE *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_DESTROYDEVICE, (PVOID)pData, sizeof(*pData), FALSE);
}

/* ---- Allocation management ----------------------------------------------- */

typedef struct _WDDM_BRIDGE_ALLOCATION_INFO_VIEW
{
    D3DKMT_HANDLE hAllocation;
    PVOID pSystemMem;
    PVOID pPrivateDriverData;
    UINT PrivateDriverDataSize;
    UINT Flags;
    D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress;
} WDDM_BRIDGE_ALLOCATION_INFO_VIEW, *PWDDM_BRIDGE_ALLOCATION_INFO_VIEW;

/* Decode the raw transport word because this tree deliberately carries newer
 * WDDM bits even when the compilation target exposes them as Reserved. */
C_ASSERT(sizeof(D3DKMT_CREATEALLOCATIONFLAGS) == sizeof(UINT));
#define WDDM_CA_FLAG_CREATE_RESOURCE       0x00000001U
#define WDDM_CA_FLAG_CREATE_SHARED         0x00000002U
#define WDDM_CA_FLAG_EXISTING_SYSMEM       0x00000020U
#define WDDM_CA_FLAG_CROSS_ADAPTER         0x00000800U
#define WDDM_CA_FLAG_STANDARD_ALLOCATION   0x00010000U
#define WDDM_CA_FLAG_EXISTING_SECTION      0x00020000U
#define WDDM_CA_KNOWN_FLAGS_MASK           0x003FFFFFU
/* Keep documented-but-unimplemented inputs out of this mask so they reach the
 * STATUS_NOT_SUPPORTED gate instead of being misclassified as malformed. */
#define WDDM_CA_INVALID_INPUT_FLAGS_MASK   0x00001308U
#define WDDM_CA_SUPPORTED_BASE_FLAGS_MASK  (WDDM_CA_FLAG_CREATE_RESOURCE | WDDM_CA_FLAG_CREATE_SHARED)
#define WDDM_CA_STANDARD_REQUIRED_MASK     (WDDM_CA_FLAG_CREATE_SHARED | WDDM_CA_FLAG_CROSS_ADAPTER | WDDM_CA_FLAG_STANDARD_ALLOCATION)
#define WDDM_CA_STANDARD_SOURCE_MASK       (WDDM_CA_FLAG_EXISTING_SYSMEM | WDDM_CA_FLAG_EXISTING_SECTION)

static NTSTATUS
WddmBridgeValidateCreateAllocationFlags(
    _In_ CONST D3DKMT_CREATEALLOCATIONFLAGS *Flags,
    _Out_ PBOOLEAN StandardAllocation)
{
    UINT RawFlags;
    UINT StandardSources;

    RtlCopyMemory(&RawFlags, Flags, sizeof(RawFlags));
    *StandardAllocation = (RawFlags & WDDM_CA_FLAG_STANDARD_ALLOCATION) != 0;
    if ((RawFlags & ~WDDM_CA_KNOWN_FLAGS_MASK) != 0 || (RawFlags & WDDM_CA_INVALID_INPUT_FLAGS_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if ((RawFlags & WDDM_CA_FLAG_CREATE_SHARED) != 0 && (RawFlags & WDDM_CA_FLAG_CREATE_RESOURCE) == 0)
        return STATUS_INVALID_PARAMETER;
    if (*StandardAllocation)
    {
        StandardSources = RawFlags & WDDM_CA_STANDARD_SOURCE_MASK;
        if ((RawFlags & WDDM_CA_STANDARD_REQUIRED_MASK) != WDDM_CA_STANDARD_REQUIRED_MASK || StandardSources == 0 || StandardSources == WDDM_CA_STANDARD_SOURCE_MASK)
            return STATUS_INVALID_PARAMETER;
        return STATUS_SUCCESS;
    }
    if ((RawFlags & WDDM_CA_STANDARD_SOURCE_MASK) != 0)
        return STATUS_INVALID_PARAMETER;
    if ((RawFlags & ~WDDM_CA_SUPPORTED_BASE_FLAGS_MASK) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static VOID
WddmBridgeReadAllocationInfo(
    _In_ PVOID AllocationInfo,
    _In_ BOOLEAN UseAllocationInfo2,
    _In_ UINT Index,
    _Out_ PWDDM_BRIDGE_ALLOCATION_INFO_VIEW View)
{
    RtlZeroMemory(View, sizeof(*View));
    if (UseAllocationInfo2)
    {
        D3DDDI_ALLOCATIONINFO2 *Info = &((D3DDDI_ALLOCATIONINFO2 *)AllocationInfo)[Index];

        View->hAllocation = Info->hAllocation;
        View->pSystemMem = (PVOID)Info->pSystemMem;
        View->pPrivateDriverData = Info->pPrivateDriverData;
        View->PrivateDriverDataSize = Info->PrivateDriverDataSize;
        View->Flags = Info->Flags.Value;
        View->GpuVirtualAddress = Info->GpuVirtualAddress;
    }
    else
    {
        D3DDDI_ALLOCATIONINFO *Info = &((D3DDDI_ALLOCATIONINFO *)AllocationInfo)[Index];

        View->hAllocation = Info->hAllocation;
        View->pSystemMem = (PVOID)Info->pSystemMem;
        View->pPrivateDriverData = Info->pPrivateDriverData;
        View->PrivateDriverDataSize = Info->PrivateDriverDataSize;
        View->Flags = Info->Flags.Value;
    }
}

static VOID
WddmBridgeSetAllocationPrivateData(
    _Inout_ PVOID AllocationInfo,
    _In_ BOOLEAN UseAllocationInfo2,
    _In_ UINT Index,
    _In_opt_ PVOID PrivateDriverData)
{
    if (UseAllocationInfo2)
        ((D3DDDI_ALLOCATIONINFO2 *)AllocationInfo)[Index].pPrivateDriverData = PrivateDriverData;
    else
        ((D3DDDI_ALLOCATIONINFO *)AllocationInfo)[Index].pPrivateDriverData = PrivateDriverData;
}

static NTSTATUS
WddmBridgeValidateAllocationInfo(
    _In_ CONST D3DDDI_ALLOCATIONINFO *Info)
{
    if ((Info->Flags.Value & ~0x3U) != 0 || ((Info->Flags.Value & 0x2U) != 0 && (Info->Flags.Value & 0x1U) == 0))
        return STATUS_INVALID_PARAMETER;
    if ((Info->Flags.Value & 0x3U) != 0)
        return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

static NTSTATUS
WddmBridgeValidateAllocationInfo2(
    _Inout_ D3DDDI_ALLOCATIONINFO2 *Info)
{
    CONST ULONG_PTR *Tail = (CONST ULONG_PTR *)((CONST UCHAR *)Info + sizeof(*Info) - (6 * sizeof(ULONG_PTR)));
    UINT Index;

    if ((Info->Flags.Value & ~0x7U) != 0)
        return STATUS_INVALID_PARAMETER;
    for (Index = 1; Index < 6; ++Index)
    {
        if (Tail[Index] != 0)
            return STATUS_INVALID_PARAMETER;
    }
    if ((Info->Flags.Value & 0x4U) == 0 && Tail[0] != 0)
        return STATUS_INVALID_PARAMETER;
    if ((Info->Flags.Value & 0x2U) != 0 && (Info->Flags.Value & 0x1U) == 0)
        return STATUS_INVALID_PARAMETER;
    if ((Info->Flags.Value & 0x7U) != 0)
        return STATUS_NOT_SUPPORTED;

    /* This field is output-only.  Zero is the physical-addressing/common-prefix
     * result, not a fabricated mapping. Virtual-only inputs above stay gated
     * until dxgkrnl can return a real process GPU virtual address. */
    Info->GpuVirtualAddress = 0;
    return STATUS_SUCCESS;
}

static VOID
WddmBridgeScrubCreateAllocationOutputs(
    _Inout_ D3DKMT_CREATEALLOCATION *UserCreateAllocation,
    _Inout_updates_bytes_(AllocationCount * AllocationInfoStride) PVOID UserAllocationInfo,
    _In_ UINT AllocationCount,
    _In_ SIZE_T AllocationInfoStride,
    _In_ BOOLEAN UseAllocationInfo2,
    _In_ D3DKMT_HANDLE SafeResourceHandle)
{
    D3DKMT_HANDLE ZeroHandle = 0;
    D3DGPU_VIRTUAL_ADDRESS ZeroGpuVirtualAddress = 0;
    UINT Index;

    for (Index = 0; Index < AllocationCount; ++Index)
    {
        (VOID)WddmBridgeSafeCopyTo((PUCHAR)UserAllocationInfo + ((SIZE_T)Index * AllocationInfoStride), &ZeroHandle, sizeof(ZeroHandle));
        if (UseAllocationInfo2)
            (VOID)WddmBridgeSafeCopyTo((PUCHAR)UserAllocationInfo + ((SIZE_T)Index * AllocationInfoStride) + FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, GpuVirtualAddress), &ZeroGpuVirtualAddress, sizeof(ZeroGpuVirtualAddress));
    }
    (VOID)WddmBridgeSafeCopyTo((PUCHAR)UserCreateAllocation + FIELD_OFFSET(D3DKMT_CREATEALLOCATION, hResource), &SafeResourceHandle, sizeof(SafeResourceHandle));
    (VOID)WddmBridgeSafeCopyTo((PUCHAR)UserCreateAllocation + FIELD_OFFSET(D3DKMT_CREATEALLOCATION, hGlobalShare), &ZeroHandle, sizeof(ZeroHandle));
}

static NTSTATUS
WddmBridgeCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *pData,
    _In_ BOOLEAN UseAllocationInfo2)
{
    typedef struct _WDDM_ALLOCATION_PRIVATE_CAPTURE { PVOID UserBuffer; PVOID OriginalBuffer; UINT Size; } WDDM_ALLOCATION_PRIVATE_CAPTURE, *PWDDM_ALLOCATION_PRIVATE_CAPTURE;
    D3DKMT_CREATEALLOCATION Captured;
    D3DKMT_DESTROYALLOCATION DestroyAllocation;
    PVOID AllocationInfo = NULL;
    PVOID UserAllocationInfo;
    D3DKMT_HANDLE *CreatedHandles = NULL;
    PVOID *AllocationPrivateBuffers = NULL;
    PWDDM_ALLOCATION_PRIVATE_CAPTURE AllocationPrivateCapture = NULL;
    PVOID PrivateDriverData = NULL;
    PVOID PrivateRuntimeData = NULL;
    SIZE_T AllocationInfoSize;
    SIZE_T AllocationPrivateCaptureSize;
    SIZE_T CreatedHandlesSize;
    SIZE_T PointerArraySize;
    SIZE_T AllocationInfoStride;
    SIZE_T TotalPrivateSize = 0;
    ULONG_PTR Information = 0;
    D3DKMT_HANDLE InputDevice;
    D3DKMT_HANDLE InputResource;
    UINT InputAllocationCount;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;
    BOOLEAN IoctlSucceeded = FALSE;
    BOOLEAN InputCreatesResource;
    BOOLEAN StandardAllocation;
    UINT i;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeValidateCreateAllocationFlags(&Captured.Flags, &StandardAllocation);
    if (!NT_SUCCESS(Status))
        return Status;

    UserAllocationInfo = UseAllocationInfo2 ? (PVOID)Captured.pAllocationInfo2 : (PVOID)Captured.pAllocationInfo;
    InputDevice = Captured.hDevice;
    InputResource = Captured.hResource;
    InputAllocationCount = Captured.NumAllocations;
    InputCreatesResource = Captured.Flags.CreateResource != 0;

    if (Captured.NumAllocations == 0 ||
        Captured.NumAllocations > D3DKMT_BRIDGE_MAX_ALLOCATIONS ||
        UserAllocationInfo == NULL ||
        (InputCreatesResource && InputResource != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    AllocationInfoStride = UseAllocationInfo2 ? sizeof(D3DDDI_ALLOCATIONINFO2) : sizeof(D3DDDI_ALLOCATIONINFO);
    Status = WddmBridgeSizeForCount(InputAllocationCount, AllocationInfoStride, &AllocationInfoSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSizeForCount(InputAllocationCount, sizeof(*AllocationPrivateBuffers), &PointerArraySize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSizeForCount(InputAllocationCount, sizeof(*AllocationPrivateCapture), &AllocationPrivateCaptureSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSizeForCount(InputAllocationCount, sizeof(*CreatedHandles), &CreatedHandlesSize);
    if (!NT_SUCCESS(Status))
        return Status;

    AllocationInfo = ExAllocatePoolWithTag(NonPagedPool, AllocationInfoSize, TAG_WDDM_BRIDGE);
    if (AllocationInfo == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    AllocationPrivateBuffers = ExAllocatePoolWithTag(NonPagedPool, PointerArraySize, TAG_WDDM_BRIDGE);
    if (AllocationPrivateBuffers == NULL)
    {
        ExFreePoolWithTag(AllocationInfo, TAG_WDDM_BRIDGE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    AllocationPrivateCapture = ExAllocatePoolWithTag(NonPagedPool, AllocationPrivateCaptureSize, TAG_WDDM_BRIDGE);
    if (AllocationPrivateCapture == NULL)
    {
        ExFreePoolWithTag(AllocationPrivateBuffers, TAG_WDDM_BRIDGE);
        ExFreePoolWithTag(AllocationInfo, TAG_WDDM_BRIDGE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    CreatedHandles = ExAllocatePoolWithTag(NonPagedPool, CreatedHandlesSize, TAG_WDDM_BRIDGE);
    if (CreatedHandles == NULL)
    {
        ExFreePoolWithTag(AllocationPrivateCapture, TAG_WDDM_BRIDGE);
        ExFreePoolWithTag(AllocationPrivateBuffers, TAG_WDDM_BRIDGE);
        ExFreePoolWithTag(AllocationInfo, TAG_WDDM_BRIDGE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(AllocationPrivateBuffers, PointerArraySize);
    RtlZeroMemory(AllocationPrivateCapture, AllocationPrivateCaptureSize);
    RtlZeroMemory(CreatedHandles, CreatedHandlesSize);

    Status = WddmBridgeSafeCopyFrom(AllocationInfo, UserAllocationInfo, AllocationInfoSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSafeProbeForWrite(UserAllocationInfo, AllocationInfoSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEALLOCATION, hResource), sizeof(Captured.hResource));
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEALLOCATION, hGlobalShare), sizeof(Captured.hGlobalShare));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    for (i = 0; i < InputAllocationCount; ++i)
    {
        Status = UseAllocationInfo2 ? WddmBridgeValidateAllocationInfo2(&((D3DDDI_ALLOCATIONINFO2 *)AllocationInfo)[i]) : WddmBridgeValidateAllocationInfo(&((D3DDDI_ALLOCATIONINFO *)AllocationInfo)[i]);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    if (StandardAllocation)
    {
        if (Captured.pStandardAllocation == NULL || Captured.PrivateDriverDataSize != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        PrivateDriverData = ExAllocatePoolWithTag(NonPagedPool, sizeof(D3DKMT_CREATESTANDARDALLOCATION), TAG_WDDM_BRIDGE);
        if (PrivateDriverData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Status = WddmBridgeSafeCopyFrom(PrivateDriverData, Captured.pStandardAllocation, sizeof(D3DKMT_CREATESTANDARDALLOCATION));
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        if (((CONST D3DKMT_CREATESTANDARDALLOCATION *)PrivateDriverData)->Flags.Value != 0 ||
            ((CONST D3DKMT_CREATESTANDARDALLOCATION *)PrivateDriverData)->Type < D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP ||
            ((CONST D3DKMT_CREATESTANDARDALLOCATION *)PrivateDriverData)->Type >= D3DKMT_STANDARDALLOCATIONTYPE_MAX ||
            Captured.NumAllocations != 1 || Captured.hResource != 0)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }
    else if (Captured.PrivateDriverDataSize != 0)
    {
        if (Captured.pPrivateDriverData == NULL ||
            Captured.PrivateDriverDataSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        PrivateDriverData = ExAllocatePoolWithTag(NonPagedPool, Captured.PrivateDriverDataSize, TAG_WDDM_BRIDGE);
        if (PrivateDriverData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        Status = WddmBridgeSafeCopyFrom(PrivateDriverData, Captured.pPrivateDriverData, Captured.PrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Captured.pPrivateDriverData = PrivateDriverData;
        TotalPrivateSize += Captured.PrivateDriverDataSize;
    }
    else
    {
        Captured.pPrivateDriverData = NULL;
    }

    if (Captured.PrivateRuntimeDataSize != 0)
    {
        if (Captured.pPrivateRuntimeData == NULL ||
            Captured.PrivateRuntimeDataSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES ||
            TotalPrivateSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES - Captured.PrivateRuntimeDataSize)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        PrivateRuntimeData = ExAllocatePoolWithTag(NonPagedPool, Captured.PrivateRuntimeDataSize, TAG_WDDM_BRIDGE);
        if (PrivateRuntimeData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        Status = WddmBridgeSafeCopyFrom(PrivateRuntimeData, Captured.pPrivateRuntimeData, Captured.PrivateRuntimeDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Captured.pPrivateRuntimeData = PrivateRuntimeData;
        TotalPrivateSize += Captured.PrivateRuntimeDataSize;
    }
    else
    {
        Captured.pPrivateRuntimeData = NULL;
    }

    for (i = 0; i < InputAllocationCount; ++i)
    {
        WDDM_BRIDGE_ALLOCATION_INFO_VIEW View;

        WddmBridgeReadAllocationInfo(AllocationInfo, UseAllocationInfo2, i, &View);
        AllocationPrivateCapture[i].UserBuffer = View.pPrivateDriverData;
        AllocationPrivateCapture[i].Size = View.PrivateDriverDataSize;
        /* User pSystemMem is only legal as EXISTINGHEAP backing; dxgkrnl
         * probes and locks the pages in-context. */
        if (ExGetPreviousMode() != KernelMode && View.pSystemMem != NULL &&
            !(StandardAllocation &&
              ((CONST D3DKMT_CREATESTANDARDALLOCATION *)Captured.pStandardAllocation)->Type == D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        if (View.PrivateDriverDataSize == 0)
        {
            WddmBridgeSetAllocationPrivateData(AllocationInfo, UseAllocationInfo2, i, NULL);
            continue;
        }

        if (View.pPrivateDriverData == NULL || View.PrivateDriverDataSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES || TotalPrivateSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES - View.PrivateDriverDataSize)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }

        AllocationPrivateBuffers[i] = ExAllocatePoolWithTag(NonPagedPool, View.PrivateDriverDataSize, TAG_WDDM_BRIDGE);
        if (AllocationPrivateBuffers[i] == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        Status = WddmBridgeSafeCopyFrom(AllocationPrivateBuffers[i], View.pPrivateDriverData, View.PrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        AllocationPrivateCapture[i].OriginalBuffer = ExAllocatePoolWithTag(NonPagedPool, View.PrivateDriverDataSize, TAG_WDDM_BRIDGE);
        if (AllocationPrivateCapture[i].OriginalBuffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlCopyMemory(AllocationPrivateCapture[i].OriginalBuffer, AllocationPrivateBuffers[i], View.PrivateDriverDataSize);
        Status = WddmBridgeSafeProbeForWrite(AllocationPrivateCapture[i].UserBuffer, AllocationPrivateCapture[i].Size);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        WddmBridgeSetAllocationPrivateData(AllocationInfo, UseAllocationInfo2, i, AllocationPrivateBuffers[i]);
        TotalPrivateSize += View.PrivateDriverDataSize;
    }

    if (UseAllocationInfo2)
        Captured.pAllocationInfo2 = (D3DDDI_ALLOCATIONINFO2 *)AllocationInfo;
    else
        Captured.pAllocationInfo = (D3DDDI_ALLOCATIONINFO *)AllocationInfo;

    Status = WddmBridgeSendIoctlWithInformation(UseAllocationInfo2 ? IOCTL_D3DKMT_CREATEALLOCATION2 : IOCTL_D3DKMT_CREATEALLOCATION, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    IoctlSucceeded = TRUE;
    for (i = 0; i < InputAllocationCount; ++i)
    {
        WDDM_BRIDGE_ALLOCATION_INFO_VIEW View;

        WddmBridgeReadAllocationInfo(AllocationInfo, UseAllocationInfo2, i, &View);
        CreatedHandles[i] = View.hAllocation;
    }
    if (Information != sizeof(Captured) || Captured.NumAllocations != InputAllocationCount)
    {
        Status = STATUS_INFO_LENGTH_MISMATCH;
        goto Rollback;
    }

    for (i = 0; i < InputAllocationCount; ++i)
    {
        if (CreatedHandles[i] == 0)
        {
            Status = STATUS_INVALID_HANDLE;
            goto Rollback;
        }
    }

    if (Captured.hDevice != InputDevice || (InputCreatesResource && Captured.hResource == 0) || (!InputCreatesResource && Captured.hResource != InputResource))
    {
        Status = STATUS_INVALID_HANDLE;
        goto Rollback;
    }

    for (i = 0; NT_SUCCESS(Status) && i < InputAllocationCount; ++i)
    {
        if (AllocationPrivateCapture[i].Size != 0)
            Status = WddmBridgeSafeCopyTo(AllocationPrivateCapture[i].UserBuffer, AllocationPrivateBuffers[i], AllocationPrivateCapture[i].Size);
    }
    for (i = 0; NT_SUCCESS(Status) && i < InputAllocationCount; ++i)
        Status = WddmBridgeSafeCopyTo((PUCHAR)UserAllocationInfo + ((SIZE_T)i * AllocationInfoStride), &CreatedHandles[i], sizeof(CreatedHandles[i]));
    if (UseAllocationInfo2)
    {
        for (i = 0; NT_SUCCESS(Status) && i < InputAllocationCount; ++i)
        {
            D3DGPU_VIRTUAL_ADDRESS GpuVirtualAddress = ((D3DDDI_ALLOCATIONINFO2 *)AllocationInfo)[i].GpuVirtualAddress;

            Status = WddmBridgeSafeCopyTo((PUCHAR)UserAllocationInfo + ((SIZE_T)i * AllocationInfoStride) + FIELD_OFFSET(D3DDDI_ALLOCATIONINFO2, GpuVirtualAddress), &GpuVirtualAddress, sizeof(GpuVirtualAddress));
        }
    }
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEALLOCATION, hResource), &Captured.hResource, sizeof(Captured.hResource));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEALLOCATION, hGlobalShare), &Captured.hGlobalShare, sizeof(Captured.hGlobalShare));
    if (NT_SUCCESS(Status))
        goto Cleanup;

Rollback:
    if (IoctlSucceeded)
    {
        UINT CreatedHandleCount = 0;

        RtlZeroMemory(&DestroyAllocation, sizeof(DestroyAllocation));
        DestroyAllocation.hDevice = InputDevice;
        if (InputCreatesResource && Captured.hResource != 0)
        {
            DestroyAllocation.hResource = Captured.hResource;
            CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYALLOCATION, &DestroyAllocation, sizeof(DestroyAllocation), NULL, 0);
        }
        else
        {
            for (i = 0; i < InputAllocationCount; ++i)
            {
                if (CreatedHandles[i] != 0)
                    CreatedHandles[CreatedHandleCount++] = CreatedHandles[i];
            }
            DestroyAllocation.phAllocationList = CreatedHandles;
            DestroyAllocation.AllocationCount = CreatedHandleCount;
            CleanupStatus = CreatedHandleCount == 0 ? STATUS_SUCCESS : WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYALLOCATION, &DestroyAllocation, sizeof(DestroyAllocation), NULL, 0);
        }
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTCreateAllocation: rollback failed with 0x%08lX\n", CleanupStatus);
        for (i = 0; i < InputAllocationCount; ++i)
        {
            if (AllocationPrivateCapture[i].OriginalBuffer != NULL)
                (VOID)WddmBridgeSafeCopyTo(AllocationPrivateCapture[i].UserBuffer, AllocationPrivateCapture[i].OriginalBuffer, AllocationPrivateCapture[i].Size);
        }
        WddmBridgeScrubCreateAllocationOutputs(pData, UserAllocationInfo, InputAllocationCount, AllocationInfoStride, UseAllocationInfo2, InputCreatesResource ? 0 : InputResource);
    }

Cleanup:
    if (AllocationPrivateBuffers != NULL)
    {
        for (i = 0; i < InputAllocationCount; ++i)
        {
            if (AllocationPrivateCapture != NULL && AllocationPrivateCapture[i].OriginalBuffer != NULL)
                ExFreePoolWithTag(AllocationPrivateCapture[i].OriginalBuffer, TAG_WDDM_BRIDGE);
            if (AllocationPrivateBuffers[i] != NULL)
                ExFreePoolWithTag(AllocationPrivateBuffers[i], TAG_WDDM_BRIDGE);
        }

        ExFreePoolWithTag(AllocationPrivateBuffers, TAG_WDDM_BRIDGE);
    }

    if (AllocationPrivateCapture != NULL)
        ExFreePoolWithTag(AllocationPrivateCapture, TAG_WDDM_BRIDGE);

    if (PrivateRuntimeData != NULL)
        ExFreePoolWithTag(PrivateRuntimeData, TAG_WDDM_BRIDGE);

    if (PrivateDriverData != NULL)
        ExFreePoolWithTag(PrivateDriverData, TAG_WDDM_BRIDGE);

    if (AllocationInfo != NULL)
        ExFreePoolWithTag(AllocationInfo, TAG_WDDM_BRIDGE);

    if (CreatedHandles != NULL)
        ExFreePoolWithTag(CreatedHandles, TAG_WDDM_BRIDGE);

    return Status;
}

NTSTATUS
APIENTRY
D3DKMTCreateAllocation(
    _Inout_ D3DKMT_CREATEALLOCATION *pData)
{
    return WddmBridgeCreateAllocation(pData, FALSE);
}

NTSTATUS
APIENTRY
D3DKMTCreateAllocation2(
    _Inout_ D3DKMT_CREATEALLOCATION *pData)
{
    return WddmBridgeCreateAllocation(pData, TRUE);
}

NTSTATUS
APIENTRY
D3DKMTDestroyAllocation(
    _In_ CONST D3DKMT_DESTROYALLOCATION *pData)
{
    D3DKMT_DESTROYALLOCATION Captured;
    D3DKMT_HANDLE *AllocationList = NULL;
    SIZE_T AllocationListSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeCaptureArray(Captured.phAllocationList, Captured.AllocationCount, sizeof(*AllocationList), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&AllocationList, &AllocationListSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.phAllocationList = AllocationList;
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYALLOCATION, &Captured, sizeof(Captured), NULL, 0);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTDestroyAllocation2(
    _In_ CONST D3DKMT_DESTROYALLOCATION2 *pData)
{
    D3DKMT_DESTROYALLOCATION2 Captured;
    D3DKMT_DESTROYALLOCATION DestroyAllocation;
    D3DKMT_HANDLE *AllocationList = NULL;
    SIZE_T AllocationListSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.hDevice == 0)
        return STATUS_INVALID_PARAMETER;
    if ((Captured.Flags.Value & ~0x3u) != 0)
        return STATUS_INVALID_PARAMETER;
    if ((Captured.Flags.Value & 0x2u) != 0)
        return STATUS_NOT_SUPPORTED;

    Status = WddmBridgeCaptureArray(Captured.phAllocationList, Captured.AllocationCount, sizeof(*AllocationList), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&AllocationList, &AllocationListSize);
    if (!NT_SUCCESS(Status))
        return Status;

    DestroyAllocation.hDevice = Captured.hDevice;
    DestroyAllocation.hResource = Captured.hResource;
    DestroyAllocation.phAllocationList = AllocationList;
    DestroyAllocation.AllocationCount = Captured.AllocationCount;
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYALLOCATION, &DestroyAllocation, sizeof(DestroyAllocation), NULL, 0);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTQueryResourceInfo(
    _Inout_ D3DKMT_QUERYRESOURCEINFO *pData)
{
    D3DKMT_QUERYRESOURCEINFO Captured;
    PVOID PrivateRuntimeData = NULL;
    PVOID UserPrivateRuntimeData;
    SIZE_T PrivateRuntimeDataSize = 0;
    UINT PrivateRuntimeDataCapacity;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        return Status;

    UserPrivateRuntimeData = Captured.pPrivateRuntimeData;
    PrivateRuntimeDataCapacity = UserPrivateRuntimeData != NULL ? Captured.PrivateRuntimeDataSize : 0;
    Captured.PrivateRuntimeDataSize = PrivateRuntimeDataCapacity;
    Status = WddmBridgeCaptureArray(UserPrivateRuntimeData, PrivateRuntimeDataCapacity, sizeof(UCHAR), D3DKMT_BRIDGE_MAX_PRIVATE_BYTES, TRUE, TRUE, &PrivateRuntimeData, &PrivateRuntimeDataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.pPrivateRuntimeData = PrivateRuntimeData;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, PrivateRuntimeDataSize), sizeof(Captured.PrivateRuntimeDataSize));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, TotalPrivateDriverDataSize), sizeof(Captured.TotalPrivateDriverDataSize));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, ResourcePrivateDriverDataSize), sizeof(Captured.ResourcePrivateDriverDataSize));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, NumAllocations), sizeof(Captured.NumAllocations));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_QUERYRESOURCEINFO, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status) && UserPrivateRuntimeData != NULL && Captured.PrivateRuntimeDataSize > PrivateRuntimeDataCapacity)
        Status = STATUS_INVALID_BUFFER_SIZE;
    if (NT_SUCCESS(Status) && UserPrivateRuntimeData != NULL && Captured.PrivateRuntimeDataSize != 0)
        Status = WddmBridgeSafeCopyTo(UserPrivateRuntimeData, PrivateRuntimeData, Captured.PrivateRuntimeDataSize);
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, PrivateRuntimeDataSize), &Captured.PrivateRuntimeDataSize, sizeof(Captured.PrivateRuntimeDataSize));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, TotalPrivateDriverDataSize), &Captured.TotalPrivateDriverDataSize, sizeof(Captured.TotalPrivateDriverDataSize));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, ResourcePrivateDriverDataSize), &Captured.ResourcePrivateDriverDataSize, sizeof(Captured.ResourcePrivateDriverDataSize));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYRESOURCEINFO, NumAllocations), &Captured.NumAllocations, sizeof(Captured.NumAllocations));

Cleanup:
    if (PrivateRuntimeData != NULL)
        ExFreePoolWithTag(PrivateRuntimeData, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTOpenResource(
    _Inout_ D3DKMT_OPENRESOURCE *pData)
{
    D3DKMT_OPENRESOURCE Captured;
    D3DDDI_OPENALLOCATIONINFO *OpenAllocationInfo = NULL;
    D3DDDI_OPENALLOCATIONINFO *UserOpenAllocationInfo;
    PVOID PrivateRuntimeData = NULL;
    PVOID ResourcePrivateDriverData = NULL;
    PVOID TotalPrivateDriverData = NULL;
    PVOID UserPrivateRuntimeData;
    PVOID UserResourcePrivateDriverData;
    PVOID UserTotalPrivateDriverData;
    SIZE_T OpenAllocationInfoSize = 0;
    SIZE_T PrivateRuntimeDataSize = 0;
    SIZE_T ResourcePrivateDriverDataSize = 0;
    SIZE_T TotalPrivateDriverDataSize = 0;
    UINT AllocationCapacity;
    UINT PrivateRuntimeDataCapacity;
    UINT ResourcePrivateDriverDataCapacity;
    UINT TotalPrivateDriverDataCapacity;
    ULONG_PTR Information = 0;
    BOOLEAN KernelOpenSucceeded = FALSE;
    NTSTATUS Status;
    UINT Index;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    UserOpenAllocationInfo = Captured.pOpenAllocationInfo;
    UserPrivateRuntimeData = Captured.pPrivateRuntimeData;
    UserResourcePrivateDriverData = Captured.pResourcePrivateDriverData;
    UserTotalPrivateDriverData = Captured.pTotalPrivateDriverDataBuffer;
    AllocationCapacity = Captured.NumAllocations;
    PrivateRuntimeDataCapacity = Captured.PrivateRuntimeDataSize;
    ResourcePrivateDriverDataCapacity = Captured.ResourcePrivateDriverDataSize;
    TotalPrivateDriverDataCapacity = Captured.TotalPrivateDriverDataBufferSize;

    Status = WddmBridgeCaptureArray(UserOpenAllocationInfo, AllocationCapacity, sizeof(*OpenAllocationInfo), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, TRUE, (PVOID *)&OpenAllocationInfo, &OpenAllocationInfoSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeCaptureArray(UserPrivateRuntimeData, PrivateRuntimeDataCapacity, sizeof(UCHAR), D3DKMT_BRIDGE_MAX_PRIVATE_BYTES, TRUE, TRUE, &PrivateRuntimeData, &PrivateRuntimeDataSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeCaptureArray(UserResourcePrivateDriverData, ResourcePrivateDriverDataCapacity, sizeof(UCHAR), D3DKMT_BRIDGE_MAX_PRIVATE_BYTES, TRUE, TRUE, &ResourcePrivateDriverData, &ResourcePrivateDriverDataSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeCaptureArray(UserTotalPrivateDriverData, TotalPrivateDriverDataCapacity, sizeof(UCHAR), D3DKMT_BRIDGE_MAX_PRIVATE_BYTES, TRUE, TRUE, &TotalPrivateDriverData, &TotalPrivateDriverDataSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Captured.pOpenAllocationInfo = OpenAllocationInfo;
    Captured.pPrivateRuntimeData = PrivateRuntimeData;
    Captured.pResourcePrivateDriverData = ResourcePrivateDriverData;
    Captured.pTotalPrivateDriverDataBuffer = TotalPrivateDriverData;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_OPENRESOURCE, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (Status == STATUS_BUFFER_OVERFLOW)
        Status = STATUS_BUFFER_TOO_SMALL;
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        if (Information != sizeof(Captured))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            goto Cleanup;
        }
        Captured.pOpenAllocationInfo = UserOpenAllocationInfo;
        Captured.pPrivateRuntimeData = UserPrivateRuntimeData;
        Captured.pResourcePrivateDriverData = UserResourcePrivateDriverData;
        Captured.pTotalPrivateDriverDataBuffer = UserTotalPrivateDriverData;
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));
        if (NT_SUCCESS(Status))
            Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    KernelOpenSucceeded = TRUE;
    if (Information != sizeof(Captured))
    {
        Status = STATUS_INFO_LENGTH_MISMATCH;
        goto Cleanup;
    }
    if (Captured.NumAllocations > AllocationCapacity || Captured.PrivateRuntimeDataSize > PrivateRuntimeDataCapacity || Captured.ResourcePrivateDriverDataSize > ResourcePrivateDriverDataCapacity || Captured.TotalPrivateDriverDataBufferSize > TotalPrivateDriverDataCapacity)
    {
        Status = STATUS_INVALID_BUFFER_SIZE;
        goto Cleanup;
    }

    for (Index = 0; Index < Captured.NumAllocations; ++Index)
    {
        ULONG_PTR KernelBufferStart = (ULONG_PTR)TotalPrivateDriverData;
        ULONG_PTR KernelBufferEnd;
        ULONG_PTR KernelPrivateData = (ULONG_PTR)OpenAllocationInfo[Index].pPrivateDriverData;
        ULONG_PTR UserBufferStart = (ULONG_PTR)UserTotalPrivateDriverData;
        SIZE_T PrivateDataOffset;

        if (OpenAllocationInfo[Index].pPrivateDriverData == NULL)
        {
            if (OpenAllocationInfo[Index].PrivateDriverDataSize != 0)
            {
                Status = STATUS_INVALID_ADDRESS;
                goto Cleanup;
            }
            continue;
        }
        if (TotalPrivateDriverData == NULL || UserTotalPrivateDriverData == NULL || KernelBufferStart > (ULONG_PTR)-1 - Captured.TotalPrivateDriverDataBufferSize)
        {
            Status = STATUS_INVALID_ADDRESS;
            goto Cleanup;
        }
        KernelBufferEnd = KernelBufferStart + Captured.TotalPrivateDriverDataBufferSize;
        if (KernelPrivateData < KernelBufferStart || KernelPrivateData > KernelBufferEnd || OpenAllocationInfo[Index].PrivateDriverDataSize > KernelBufferEnd - KernelPrivateData)
        {
            Status = STATUS_INVALID_ADDRESS;
            goto Cleanup;
        }
        PrivateDataOffset = KernelPrivateData - KernelBufferStart;
        if (UserBufferStart > (ULONG_PTR)-1 - PrivateDataOffset)
        {
            Status = STATUS_INVALID_ADDRESS;
            goto Cleanup;
        }
        OpenAllocationInfo[Index].pPrivateDriverData = (PVOID)(UserBufferStart + PrivateDataOffset);
    }

    if (Captured.PrivateRuntimeDataSize != 0)
        Status = WddmBridgeSafeCopyTo(UserPrivateRuntimeData, PrivateRuntimeData, Captured.PrivateRuntimeDataSize);
    if (NT_SUCCESS(Status) && Captured.ResourcePrivateDriverDataSize != 0)
        Status = WddmBridgeSafeCopyTo(UserResourcePrivateDriverData, ResourcePrivateDriverData, Captured.ResourcePrivateDriverDataSize);
    if (NT_SUCCESS(Status) && Captured.TotalPrivateDriverDataBufferSize != 0)
        Status = WddmBridgeSafeCopyTo(UserTotalPrivateDriverData, TotalPrivateDriverData, Captured.TotalPrivateDriverDataBufferSize);
    if (NT_SUCCESS(Status) && Captured.NumAllocations != 0)
        Status = WddmBridgeSafeCopyTo(UserOpenAllocationInfo, OpenAllocationInfo, (SIZE_T)Captured.NumAllocations * sizeof(*OpenAllocationInfo));
    if (NT_SUCCESS(Status))
    {
        Captured.pOpenAllocationInfo = UserOpenAllocationInfo;
        Captured.pPrivateRuntimeData = UserPrivateRuntimeData;
        Captured.pResourcePrivateDriverData = UserResourcePrivateDriverData;
        Captured.pTotalPrivateDriverDataBuffer = UserTotalPrivateDriverData;
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));
    }

Cleanup:
    if (KernelOpenSucceeded && !NT_SUCCESS(Status) && Captured.hResource != 0)
    {
        D3DKMT_DESTROYALLOCATION Rollback;
        NTSTATUS RollbackStatus;

        RtlZeroMemory(&Rollback, sizeof(Rollback));
        Rollback.hDevice = Captured.hDevice;
        Rollback.hResource = Captured.hResource;
        RollbackStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYALLOCATION, &Rollback, sizeof(Rollback), NULL, 0);
        if (!NT_SUCCESS(RollbackStatus))
            DPRINT1("D3DKMTOpenResource: rollback of resource 0x%08lx failed 0x%08lx\n", Captured.hResource, RollbackStatus);
    }
    if (TotalPrivateDriverData != NULL)
        ExFreePoolWithTag(TotalPrivateDriverData, TAG_WDDM_BRIDGE);
    if (ResourcePrivateDriverData != NULL)
        ExFreePoolWithTag(ResourcePrivateDriverData, TAG_WDDM_BRIDGE);
    if (PrivateRuntimeData != NULL)
        ExFreePoolWithTag(PrivateRuntimeData, TAG_WDDM_BRIDGE);
    if (OpenAllocationInfo != NULL)
        ExFreePoolWithTag(OpenAllocationInfo, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTGetSharedPrimaryHandle(
    _Inout_ D3DKMT_GETSHAREDPRIMARYHANDLE *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE, pData, sizeof(*pData), TRUE);
}

/* ---- Rendering ----------------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTRender(
    _Inout_ D3DKMT_RENDER *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_RENDER, pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTPresent(
    _Inout_ D3DKMT_PRESENT *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_PRESENT, pData, sizeof(*pData), TRUE));
}

/* ---- Memory locking ------------------------------------------------------ */

NTSTATUS
APIENTRY
D3DKMTLock(
    _Inout_ D3DKMT_LOCK *pData)
{
    D3DKMT_LOCK Captured;
    D3DKMT_UNLOCK Unlock;
    D3DKMT_HANDLE AllocationHandle;
    UINT *Pages = NULL;
    CONST UINT *UserPages;
    SIZE_T PagesSize = 0;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    UserPages = Captured.pPages;
    Status = WddmBridgeCaptureArray(Captured.pPages, Captured.NumPages, sizeof(*Pages), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&Pages, &PagesSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Captured.pPages = Pages;
    Captured.pData = NULL;
    Captured.GpuVirtualAddress = 0;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_LOCK, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    else if (Captured.pData == NULL || WddmBridgeIsKernelPointerForUser(Captured.pData))
        Status = STATUS_INVALID_ADDRESS;
    else
    {
        Captured.pPages = UserPages;
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));
    }

    if (!NT_SUCCESS(Status) && Captured.pData != NULL)
    {
        AllocationHandle = Captured.hAllocation;
        Unlock.hDevice = Captured.hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &AllocationHandle;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_UNLOCK, &Unlock, sizeof(Unlock), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTLock: rollback of allocation 0x%X failed with 0x%08lX\n", Captured.hAllocation, CleanupStatus);
    }

Cleanup:
    if (Pages != NULL)
        ExFreePoolWithTag(Pages, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTLock2(
    _Inout_ D3DKMT_LOCK2 *pData)
{
    D3DKMT_LOCK2 Captured;
    D3DKMT_LOCK Lock;
    D3DKMT_UNLOCK Unlock;
    D3DKMT_HANDLE AllocationHandle;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.hDevice == 0 || Captured.hAllocation == 0)
        return STATUS_INVALID_PARAMETER;
    if (Captured.Flags.Value != 0)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_LOCK2, pData), sizeof(Captured.pData));
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&Lock, sizeof(Lock));
    Lock.hDevice = Captured.hDevice;
    Lock.hAllocation = Captured.hAllocation;
    Lock.Flags.LockEntire = 1;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_LOCK, &Lock, sizeof(Lock), &Lock, sizeof(Lock), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(Lock))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    else if (Lock.pData == NULL || WddmBridgeIsKernelPointerForUser(Lock.pData))
        Status = STATUS_INVALID_ADDRESS;
    else
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_LOCK2, pData), &Lock.pData, sizeof(Lock.pData));

    if (!NT_SUCCESS(Status))
    {
        AllocationHandle = Captured.hAllocation;
        Unlock.hDevice = Captured.hDevice;
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &AllocationHandle;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_UNLOCK, &Unlock, sizeof(Unlock), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTLock2: rollback of allocation 0x%X failed with 0x%08lX\n", Captured.hAllocation, CleanupStatus);
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTUnlock(
    _In_ CONST D3DKMT_UNLOCK *pData)
{
    D3DKMT_UNLOCK Captured;
    D3DKMT_HANDLE *AllocationList = NULL;
    SIZE_T AllocationListSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeCaptureArray(Captured.phAllocations, Captured.NumAllocations, sizeof(*AllocationList), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&AllocationList, &AllocationListSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.phAllocations = AllocationList;
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_UNLOCK, &Captured, sizeof(Captured), NULL, 0);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTUnlock2(
    _In_ CONST D3DKMT_UNLOCK2 *pData)
{
    D3DKMT_UNLOCK2 Captured;
    D3DKMT_UNLOCK Unlock;
    D3DKMT_HANDLE AllocationHandle;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.hDevice == 0 || Captured.hAllocation == 0)
        return STATUS_INVALID_PARAMETER;

    AllocationHandle = Captured.hAllocation;
    Unlock.hDevice = Captured.hDevice;
    Unlock.NumAllocations = 1;
    Unlock.phAllocations = &AllocationHandle;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_UNLOCK, &Unlock, sizeof(Unlock), NULL, 0);
}

/* ---- Synchronisation objects --------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObject(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObject(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT, (PVOID)pData, sizeof(*pData), FALSE);
}

/* ---- Display mode management --------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTGetDisplayModeList(
    _Inout_ D3DKMT_GETDISPLAYMODELIST *pData)
{
    D3DKMT_GETDISPLAYMODELIST Captured;
    D3DKMT_DISPLAYMODE *ModeList = NULL;
    D3DKMT_DISPLAYMODE *UserModeList;
    SIZE_T ModeListSize = 0;
    UINT Capacity;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        return Status;

    UserModeList = Captured.pModeList;
    Capacity = Captured.ModeCount;
    if (UserModeList != NULL && Capacity != 0)
    {
        Status = WddmBridgeCaptureArray(UserModeList, Capacity, sizeof(*ModeList), D3DKMT_BRIDGE_MAX_ALLOCATIONS, FALSE, TRUE, (PVOID *)&ModeList, &ModeListSize);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    Captured.pModeList = ModeList;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_GETDISPLAYMODELIST, ModeCount), sizeof(Captured.ModeCount));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_GETDISPLAYMODELIST, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (Status == STATUS_BUFFER_OVERFLOW)
        Status = STATUS_BUFFER_TOO_SMALL;
    if ((NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL) && Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status) && Captured.ModeCount > Capacity && ModeList != NULL)
        Status = STATUS_INVALID_BUFFER_SIZE;
    if (NT_SUCCESS(Status) && Captured.ModeCount != 0 && ModeList != NULL)
        Status = WddmBridgeSafeCopyTo(UserModeList, ModeList, (SIZE_T)Captured.ModeCount * sizeof(*ModeList));
    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
    {
        NTSTATUS CopyStatus = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_GETDISPLAYMODELIST, ModeCount), &Captured.ModeCount, sizeof(Captured.ModeCount));
        if (!NT_SUCCESS(CopyStatus))
            Status = CopyStatus;
    }

Cleanup:
    if (ModeList != NULL)
        ExFreePoolWithTag(ModeList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSetDisplayMode(
    _In_ CONST D3DKMT_SETDISPLAYMODE *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_SETDISPLAYMODE, (PVOID)pData, sizeof(*pData), FALSE);
}

/* ---- Context management -------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateContext(
    _Inout_ D3DKMT_CREATECONTEXT *pData)
{
    D3DKMT_CREATECONTEXT Captured;
    D3DKMT_DESTROYCONTEXT DestroyContext;
    PVOID PrivateDriverData = NULL;
    PVOID UserPrivateDriverData;
    SIZE_T PrivateDriverDataSize = 0;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    UserPrivateDriverData = Captured.pPrivateDriverData;
    Status = WddmBridgeCaptureArray(UserPrivateDriverData, Captured.PrivateDriverDataSize, sizeof(UCHAR), D3DKMT_BRIDGE_MAX_PRIVATE_BYTES, TRUE, FALSE, &PrivateDriverData, &PrivateDriverDataSize);
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.pPrivateDriverData = PrivateDriverData;
    Captured.hContext = 0;
    Captured.pCommandBuffer = NULL;
    Captured.CommandBufferSize = 0;
    Captured.pAllocationList = NULL;
    Captured.AllocationListSize = 0;
    Captured.pPatchLocationList = NULL;
    Captured.PatchLocationListSize = 0;
    Captured.CommandBuffer = 0;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_CREATECONTEXT, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    if (Information != sizeof(Captured) || Captured.hContext == 0)
        Status = Information != sizeof(Captured) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_HANDLE;
    else if (WddmBridgeIsKernelPointerForUser(Captured.pCommandBuffer) || WddmBridgeIsKernelPointerForUser(Captured.pAllocationList) || WddmBridgeIsKernelPointerForUser(Captured.pPatchLocationList))
        Status = STATUS_INVALID_ADDRESS;
    else
    {
        Captured.pPrivateDriverData = UserPrivateDriverData;
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));
    }

    if (!NT_SUCCESS(Status) && Captured.hContext != 0)
    {
        DestroyContext.hContext = Captured.hContext;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYCONTEXT, &DestroyContext, sizeof(DestroyContext), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTCreateContext: rollback of context 0x%X failed with 0x%08lX\n", Captured.hContext, CleanupStatus);
    }

Cleanup:
    if (PrivateDriverData != NULL)
        ExFreePoolWithTag(PrivateDriverData, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTDestroyContext(
    _In_ CONST D3DKMT_DESTROYCONTEXT *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_DESTROYCONTEXT, (PVOID)pData, sizeof(*pData), FALSE);
}

/* ---- Synchronisation object management ----------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT Captured;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.hSyncObject = 0;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(Captured) || Captured.hSyncObject == 0)
        Status = Information != sizeof(Captured) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_HANDLE;
    else
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));

    if (!NT_SUCCESS(Status) && Captured.hSyncObject != 0)
    {
        DestroySync.hSyncObject = Captured.hSyncObject;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT, &DestroySync, sizeof(DestroySync), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTCreateSynchronizationObject: rollback of sync object 0x%X failed with 0x%08lX\n", Captured.hSyncObject, CleanupStatus);
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTDestroySynchronizationObject(
    _In_ CONST D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT, (PVOID)pData, sizeof(*pData), FALSE);
}

/* ---- Escape -------------------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTEscape(
    _In_ CONST D3DKMT_ESCAPE *pData)
{
    D3DKMT_ESCAPE Captured;
    PVOID PrivateDriverData = NULL;
    PVOID UserPrivateDriverData;
    SIZE_T PrivateDriverDataSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    UserPrivateDriverData = Captured.pPrivateDriverData;
    Status = WddmBridgeCaptureArray(UserPrivateDriverData, Captured.PrivateDriverDataSize, sizeof(UCHAR), D3DKMT_BRIDGE_MAX_PRIVATE_BYTES, TRUE, TRUE, &PrivateDriverData, &PrivateDriverDataSize);
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.pPrivateDriverData = PrivateDriverData;
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_ESCAPE, &Captured, sizeof(Captured), NULL, 0);
    if (NT_SUCCESS(Status) && PrivateDriverData != NULL)
        Status = WddmBridgeSafeCopyTo(UserPrivateDriverData, PrivateDriverData, PrivateDriverDataSize);
    if (PrivateDriverData != NULL)
        ExFreePoolWithTag(PrivateDriverData, TAG_WDDM_BRIDGE);
    return Status;
}

/* ---- VidPn source ownership ---------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner(
    _In_ CONST D3DKMT_SETVIDPNSOURCEOWNER *pData)
{
    D3DKMT_SETVIDPNSOURCEOWNER Captured;
    D3DKMT_VIDPNSOURCEOWNER_TYPE *OwnerTypes = NULL;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID *SourceIds = NULL;
    SIZE_T OwnerTypesSize = 0;
    SIZE_T SourceIdsSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    /* A zero count is a release-all request; the arrays are then ignored. */
    if (Captured.VidPnSourceCount > D3DKMT_BRIDGE_MAX_ALLOCATIONS ||
        (Captured.VidPnSourceCount != 0 && (Captured.pType == NULL || Captured.pVidPnSourceId == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Captured.VidPnSourceCount != 0 && Captured.pType != NULL && Captured.pVidPnSourceId != NULL)
    {
        Status = WddmBridgeCaptureArray(Captured.pType, Captured.VidPnSourceCount, sizeof(*OwnerTypes), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&OwnerTypes, &OwnerTypesSize);
        Status = WddmBridgeRejectBadBuffer(Status);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Status = WddmBridgeCaptureArray(Captured.pVidPnSourceId, Captured.VidPnSourceCount, sizeof(*SourceIds), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&SourceIds, &SourceIdsSize);
        Status = WddmBridgeRejectBadBuffer(Status);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Captured.pType = OwnerTypes;
        Captured.pVidPnSourceId = SourceIds;
    }
    else
    {
        Captured.pType = NULL;
        Captured.pVidPnSourceId = NULL;
    }

    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_SETVIDPNSOURCEOWNER, &Captured, sizeof(Captured), NULL, 0);

Cleanup:
    if (SourceIds != NULL)
        ExFreePoolWithTag(SourceIds, TAG_WDDM_BRIDGE);
    if (OwnerTypes != NULL)
        ExFreePoolWithTag(OwnerTypes, TAG_WDDM_BRIDGE);
    return Status;
}

/* ---- Device state -------------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTGetDeviceState(
    _Inout_ D3DKMT_GETDEVICESTATE *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_GETDEVICESTATE, pData, sizeof(*pData), TRUE);
}

/* ---- Additional D3DKMT operations ---------------------------------------- */
/* These cover the remaining SSDT entries not yet bridged above.              */

/* WDDM 1.2 additions */
#define IOCTL_D3DKMT_ENUMADAPTERS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x100, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_ENUMADAPTERS2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x101, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENADAPTERFROMLUID \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x102, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OFFERALLOCATIONS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RECLAIMALLOCATIONS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETVIDPNSOURCEOWNER1 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETVIDPNSOURCEOWNER2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_OPENSYNCHRONIZATIONOBJECT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1B8, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* WDDM 2.0 residency / paging queue / video memory */
#define IOCTL_D3DKMT_MAKERESIDENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x180, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_EVICT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x181, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEPAGINGQUEUE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x186, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYPAGINGQUEUE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x187, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYVIDEOMEMORYINFO \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x188, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* WDDM 2.0 GPU virtual addressing */
#define IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x182, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x183, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x184, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x185, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x189, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x18A, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_PREPAREMAPGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x18E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DXGKRNL_PREPARERESERVEGPUVIRTUALADDRESS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x18F, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_D3DKMT_CHECKMONITORPOWERSTATE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x168, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x165, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CHECKOCCLUSION \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x169, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_CREATEOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_DESTROYOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_FLIPOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UPDATEOVERLAY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1A3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x126, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x125, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETMULTISAMPLEMETHODLIST \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETPRESENTHISTORY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x142, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETRUNTIMEDATA \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x171, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETSCANLINE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x167, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_INVALIDATEACTIVEVIDPN \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x178, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_POLLDISPLAYCHILDREN \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x179, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x163, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_QUERYSTATISTICS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x172, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RELEASEPROCESSVIDPNSOURCEOWNERS \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETALLOCATIONPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x162, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETALLOCATIONPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x18D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x177, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETGAMMARAMP \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETQUEUEDLIMIT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORIDLE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x16B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x166, METHOD_BUFFERED, FILE_ANY_ACCESS)

NTSTATUS
APIENTRY
D3DKMTCheckMonitorPowerState(
    _In_ CONST D3DKMT_CHECKMONITORPOWERSTATE *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_CHECKMONITORPOWERSTATE, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTCheckOcclusion(
    _In_ CONST D3DKMT_CHECKOCCLUSION *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_CHECKOCCLUSION, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTCreateOverlay(
    _Inout_ D3DKMT_CREATEOVERLAY *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_CREATEOVERLAY, pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTDestroyOverlay(
    _In_ CONST D3DKMT_DESTROYOVERLAY *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_DESTROYOVERLAY, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTFlipOverlay(
    _In_ CONST D3DKMT_FLIPOVERLAY *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_FLIPOVERLAY, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTUpdateOverlay(
    _In_ CONST D3DKMT_UPDATEOVERLAY *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_UPDATEOVERLAY, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTGetContextSchedulingPriority(
    _Inout_ D3DKMT_GETCONTEXTSCHEDULINGPRIORITY *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY, pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTSetContextSchedulingPriority(
    _In_ CONST D3DKMT_SETCONTEXTSCHEDULINGPRIORITY *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTGetMultisampleMethodList(
    _Inout_ D3DKMT_GETMULTISAMPLEMETHODLIST *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_GETMULTISAMPLEMETHODLIST, pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTGetPresentHistory(
    _Inout_ D3DKMT_GETPRESENTHISTORY *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_GETPRESENTHISTORY, pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTGetRuntimeData(
    _In_ CONST D3DKMT_GETRUNTIMEDATA *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_GETRUNTIMEDATA, (PVOID)pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTGetScanLine(
    _Inout_ D3DKMT_GETSCANLINE *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_GETSCANLINE, pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTInvalidateActiveVidPn(
    _In_ CONST D3DKMT_INVALIDATEACTIVEVIDPN *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_INVALIDATEACTIVEVIDPN, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTPollDisplayChildren(
    _In_ CONST D3DKMT_POLLDISPLAYCHILDREN *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_POLLDISPLAYCHILDREN, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTQueryAllocationResidency(
    _In_ CONST D3DKMT_QUERYALLOCATIONRESIDENCY *pData)
{
    D3DKMT_QUERYALLOCATIONRESIDENCY Captured;
    D3DKMT_HANDLE *AllocationList = NULL;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS *ResidencyStatus = NULL;
    D3DKMT_ALLOCATIONRESIDENCYSTATUS *UserResidencyStatus;
    SIZE_T AllocationListSize = 0;
    SIZE_T ResidencyStatusSize = 0;
    UINT ResidencyStatusCount;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    if ((Captured.hResource != 0 && (Captured.AllocationCount != 0 || Captured.phAllocationList != NULL || Captured.pResidencyStatus == NULL)) || (Captured.hResource == 0 && (Captured.AllocationCount == 0 || Captured.AllocationCount > D3DKMT_BRIDGE_MAX_ALLOCATIONS || Captured.phAllocationList == NULL || Captured.pResidencyStatus == NULL)))
        return STATUS_INVALID_PARAMETER;

    UserResidencyStatus = Captured.pResidencyStatus;
    ResidencyStatusCount = Captured.hResource != 0 ? 1 : Captured.AllocationCount;
    Status = WddmBridgeCaptureArray(Captured.phAllocationList, Captured.AllocationCount, sizeof(*AllocationList), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&AllocationList, &AllocationListSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeCaptureArray(UserResidencyStatus, ResidencyStatusCount, sizeof(*ResidencyStatus), D3DKMT_BRIDGE_MAX_ALLOCATIONS, FALSE, TRUE, (PVOID *)&ResidencyStatus, &ResidencyStatusSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Captured.phAllocationList = AllocationList;
    Captured.pResidencyStatus = ResidencyStatus;
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_QUERYALLOCATIONRESIDENCY, &Captured, sizeof(Captured), NULL, 0);
    if (NT_SUCCESS(Status) && ResidencyStatus != NULL)
        Status = WddmBridgeSafeCopyTo(UserResidencyStatus, ResidencyStatus, ResidencyStatusSize);

Cleanup:
    if (ResidencyStatus != NULL)
        ExFreePoolWithTag(ResidencyStatus, TAG_WDDM_BRIDGE);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTQueryStatistics(
    _In_ CONST D3DKMT_QUERYSTATISTICS *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_QUERYSTATISTICS, (PVOID)pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTReleaseProcessVidPnSourceOwners(
    _In_ HANDLE hProcess)
{
    if (!hProcess) return STATUS_INVALID_PARAMETER;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_RELEASEPROCESSVIDPNSOURCEOWNERS,
                               &hProcess, sizeof(hProcess), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTGetAllocationPriority(
    _In_ CONST D3DKMT_GETALLOCATIONPRIORITY *pData)
{
    D3DKMT_GETALLOCATIONPRIORITY Captured;
    D3DKMT_HANDLE *AllocationList = NULL;
    UINT *Priorities = NULL;
    UINT *UserPriorities;
    UINT PriorityCount;
    SIZE_T AllocationListSize = 0;
    SIZE_T PrioritiesSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Captured.hResource != 0 && (Captured.AllocationCount != 0 || Captured.phAllocationList != NULL || Captured.pPriorities == NULL)) || (Captured.hResource == 0 && (Captured.AllocationCount == 0 || Captured.AllocationCount > D3DKMT_BRIDGE_MAX_ALLOCATIONS || Captured.phAllocationList == NULL || Captured.pPriorities == NULL)))
        return STATUS_INVALID_PARAMETER;
    UserPriorities = Captured.pPriorities;
    PriorityCount = Captured.hResource != 0 ? 1 : Captured.AllocationCount;
    Status = WddmBridgeCaptureArray(Captured.phAllocationList, Captured.AllocationCount, sizeof(*AllocationList), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&AllocationList, &AllocationListSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeCaptureArray(UserPriorities, PriorityCount, sizeof(*Priorities), D3DKMT_BRIDGE_MAX_ALLOCATIONS, FALSE, TRUE, (PVOID *)&Priorities, &PrioritiesSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Captured.phAllocationList = AllocationList;
    Captured.pPriorities = Priorities;
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_GETALLOCATIONPRIORITY, &Captured, sizeof(Captured), NULL, 0);
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo(UserPriorities, Priorities, PrioritiesSize);

Cleanup:
    if (Priorities != NULL)
        ExFreePoolWithTag(Priorities, TAG_WDDM_BRIDGE);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSetAllocationPriority(
    _In_ CONST D3DKMT_SETALLOCATIONPRIORITY *pData)
{
    D3DKMT_SETALLOCATIONPRIORITY Captured;
    D3DKMT_HANDLE *AllocationList = NULL;
    UINT *Priorities = NULL;
    SIZE_T AllocationListSize = 0;
    SIZE_T PrioritiesSize = 0;
    UINT PriorityCount;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    if ((Captured.hResource != 0 && (Captured.AllocationCount != 0 || Captured.phAllocationList != NULL || Captured.pPriorities == NULL)) || (Captured.hResource == 0 && (Captured.AllocationCount == 0 || Captured.AllocationCount > D3DKMT_BRIDGE_MAX_ALLOCATIONS || Captured.phAllocationList == NULL || Captured.pPriorities == NULL)))
        return STATUS_INVALID_PARAMETER;

    PriorityCount = Captured.hResource != 0 ? 1 : Captured.AllocationCount;
    Status = WddmBridgeCaptureArray(Captured.phAllocationList, Captured.AllocationCount, sizeof(*AllocationList), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&AllocationList, &AllocationListSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeCaptureArray(Captured.pPriorities, PriorityCount, sizeof(*Priorities), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&Priorities, &PrioritiesSize);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Captured.phAllocationList = AllocationList;
    Captured.pPriorities = Priorities;
    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_SETALLOCATIONPRIORITY, &Captured, sizeof(Captured), NULL, 0);

Cleanup:
    if (Priorities != NULL)
        ExFreePoolWithTag(Priorities, TAG_WDDM_BRIDGE);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSetDisplayPrivateDriverFormat(
    _In_ CONST D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_SETDISPLAYPRIVATEDRIVERFORMAT, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTSetGammaRamp(
    _In_ CONST D3DKMT_SETGAMMARAMP *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_SETGAMMARAMP, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTSetQueuedLimit(
    _In_ CONST D3DKMT_SETQUEUEDLIMIT *pData)
{
    /* D3DKMT_GET_QUEUEDLIMIT_* returns the limit in the same struct, so the
     * result has to travel back to the caller. */
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_SETQUEUEDLIMIT, (PVOID)pData, sizeof(*pData), TRUE);
}

NTSTATUS
APIENTRY
D3DKMTWaitForIdle(
    _In_ CONST D3DKMT_WAITFORIDLE *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_WAITFORIDLE, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTWaitForVerticalBlankEvent(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTCheckVidPnExclusiveOwnership(
    _In_ CONST D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP, (PVOID)pData, sizeof(*pData), FALSE);
}

/* ---- WDDM 1.2 additions ------------------------------------------------ */

NTSTATUS
APIENTRY
D3DKMTEnumAdapters(
    _Inout_ CONST D3DKMT_ENUMADAPTERS *pData)
{
    D3DKMT_ENUMADAPTERS Captured;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;
    ULONG Index;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PVOID)pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.NumAdapters = 0;
    RtlZeroMemory(Captured.Adapters, sizeof(Captured.Adapters));
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_ENUMADAPTERS, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && (Information != sizeof(Captured) || Captured.NumAdapters > MAX_ENUM_ADAPTERS))
        Status = Information != sizeof(Captured) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_BUFFER_SIZE;
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PVOID)pData, &Captured, sizeof(Captured));

    if (!NT_SUCCESS(Status))
    {
        for (Index = 0; Index < MAX_ENUM_ADAPTERS; ++Index)
        {
            if (Captured.Adapters[Index].hAdapter == 0)
                continue;
            CloseAdapter.hAdapter = Captured.Adapters[Index].hAdapter;
            CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
            if (!NT_SUCCESS(CleanupStatus))
                DPRINT1("D3DKMTEnumAdapters: rollback of adapter 0x%X failed with 0x%08lX\n", CloseAdapter.hAdapter, CleanupStatus);
        }
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTOpenAdapterFromLuid(
    _Inout_ CONST D3DKMT_OPENADAPTERFROMLUID *pData)
{
    return WddmBridgeOpenAdapterIoctl(IOCTL_D3DKMT_OPENADAPTERFROMLUID, (PVOID)pData, sizeof(*pData), FIELD_OFFSET(D3DKMT_OPENADAPTERFROMLUID, hAdapter));
}

/*
 * D3DKMTEnumAdapters2 — two-pass enumeration into a caller-provided array.
 *
 * Unlike EnumAdapters (which carries an inline Adapters[] array marshalled by
 * METHOD_BUFFERED), EnumAdapters2 uses an external pAdapters pointer.  Marshal
 * it through a nonpaged kernel buffer so dxgkrnl writes to a kernel address,
 * then copy the result back into the caller's array under SEH.
 */
NTSTATUS
APIENTRY
D3DKMTEnumAdapters2(
    _Inout_ CONST D3DKMT_ENUMADAPTERS2 *pData)
{
    D3DKMT_ENUMADAPTERS2 Captured;
    D3DKMT_ADAPTERINFO  *KernelAdapters = NULL;
    D3DKMT_ADAPTERINFO  *UserAdapters;
    SIZE_T               BufSize;
    ULONG_PTR            Information = 0;
    ULONG                Capacity;
    ULONG                Index;
    NTSTATUS             Status;

    if (!pData)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    UserAdapters = Captured.pAdapters;
    Capacity = Captured.NumAdapters;

    /* Pass 1: count query (no output array). */
    if (Captured.pAdapters == NULL || Captured.NumAdapters == 0)
    {
        Captured.pAdapters = NULL;
        Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS2, NumAdapters), sizeof(Captured.NumAdapters));
        if (!NT_SUCCESS(Status))
            return Status;
        Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_ENUMADAPTERS2, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
        if (Status == STATUS_BUFFER_OVERFLOW)
            Status = STATUS_BUFFER_TOO_SMALL;
        if ((NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL) && Information != sizeof(Captured))
            Status = STATUS_INFO_LENGTH_MISMATCH;
        if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
        {
            NTSTATUS CopyStatus = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS2, NumAdapters), &Captured.NumAdapters, sizeof(Captured.NumAdapters));
            if (!NT_SUCCESS(CopyStatus))
                Status = CopyStatus;
        }
        return Status;
    }

    if (Captured.NumAdapters > 256)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSizeForCount(Captured.NumAdapters, sizeof(D3DKMT_ADAPTERINFO), &BufSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSafeProbeForWrite(UserAdapters, BufSize);
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS2, NumAdapters), sizeof(Captured.NumAdapters));
    if (!NT_SUCCESS(Status))
        return Status;

    KernelAdapters = ExAllocatePoolWithTag(NonPagedPool, BufSize, TAG_WDDM_BRIDGE);
    if (KernelAdapters == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(KernelAdapters, BufSize);
    Captured.pAdapters = KernelAdapters;

    /* Pass 2: fill the (kernel) buffer, then copy back to the user array. */
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_ENUMADAPTERS2, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (Status == STATUS_BUFFER_OVERFLOW)
        Status = STATUS_BUFFER_TOO_SMALL;
    if ((NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL) && Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status) && Captured.NumAdapters > Capacity)
        Status = STATUS_INVALID_BUFFER_SIZE;
    if (NT_SUCCESS(Status))
    {
        Status = WddmBridgeSafeCopyTo(UserAdapters, KernelAdapters, (SIZE_T)Captured.NumAdapters * sizeof(D3DKMT_ADAPTERINFO));
    }
    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
    {
        NTSTATUS CopyStatus = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS2, NumAdapters), &Captured.NumAdapters, sizeof(Captured.NumAdapters));
        if (!NT_SUCCESS(CopyStatus))
            Status = CopyStatus;
    }

    if (!NT_SUCCESS(Status))
    {
        for (Index = 0; Index < Capacity; ++Index)
        {
            D3DKMT_CLOSEADAPTER CloseAdapter;

            if (KernelAdapters[Index].hAdapter == 0)
                continue;
            CloseAdapter.hAdapter = KernelAdapters[Index].hAdapter;
            WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
        }
    }

    ExFreePoolWithTag(KernelAdapters, TAG_WDDM_BRIDGE);
    return Status;
}

/*
 * D3DKMTEnumAdapters3 -- WDDM 2.7 filtered enumeration.
 *
 * EnumAdapters2 reports every adapter.  The filter here removes the two classes
 * Windows leaves out of a default enumeration so that older applications do not
 * trip over adapters they cannot use: compute-only adapters, and display-only
 * adapters (display support without render support).  An adapter that is
 * filtered out still had a handle opened for it by the enumeration, so it must
 * be closed here rather than leaked back to the caller.
 */
static BOOLEAN
WddmBridgeAdapterPassesFilter(
    _In_ D3DKMT_HANDLE hAdapter,
    _In_ D3DKMT_ENUMADAPTERS_FILTER Filter)
{
    D3DKMT_QUERYADAPTERINFO Query;
    D3DKMT_ADAPTERTYPE AdapterType;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    RtlZeroMemory(&AdapterType, sizeof(AdapterType));
    RtlZeroMemory(&Query, sizeof(Query));
    Query.hAdapter = hAdapter;
    Query.Type = KMTQAITYPE_ADAPTERTYPE;
    Query.pPrivateDriverData = &AdapterType;
    Query.PrivateDriverDataSize = sizeof(AdapterType);

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_QUERYADAPTERINFO, &Query, sizeof(Query), &Query, sizeof(Query), &Information);
    /* An adapter that cannot describe itself is not one of the classes the
     * filter excludes, so keep it rather than silently dropping it. */
    if (!NT_SUCCESS(Status))
        return TRUE;

    if (AdapterType.ComputeOnly && !Filter.IncludeComputeOnly)
        return FALSE;
    if (AdapterType.DisplaySupported && !AdapterType.RenderSupported && !Filter.IncludeDisplayOnly)
        return FALSE;
    return TRUE;
}

NTSTATUS
APIENTRY
D3DKMTEnumAdapters3(
    _Inout_ D3DKMT_ENUMADAPTERS3 *pData)
{
    D3DKMT_ENUMADAPTERS3 Captured;
    D3DKMT_ENUMADAPTERS2 Query;
    D3DKMT_ADAPTERINFO *KernelAdapters = NULL;
    D3DKMT_ADAPTERINFO *UserAdapters;
    D3DKMT_CLOSEADAPTER CloseAdapter;
    SIZE_T BufSize;
    ULONG_PTR Information = 0;
    ULONG Capacity;
    ULONG Enumerated;
    ULONG Kept = 0;
    ULONG Index;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((Captured.Filter.Value & ~(ULONGLONG)0x3) != 0)
        return STATUS_INVALID_PARAMETER;

    UserAdapters = Captured.pAdapters;
    Capacity = Captured.NumAdapters;

    /*
     * Enumerate into a kernel array first even for a count-only query: the
     * count the caller wants is the count *after* filtering, which cannot be
     * known without looking at each adapter.
     */
    Status = WddmBridgeSizeForCount(D3DKMT_BRIDGE_MAX_ENUM_ADAPTERS, sizeof(D3DKMT_ADAPTERINFO), &BufSize);
    if (!NT_SUCCESS(Status))
        return Status;
    KernelAdapters = ExAllocatePoolWithTag(NonPagedPool, BufSize, TAG_WDDM_BRIDGE);
    if (KernelAdapters == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(KernelAdapters, BufSize);

    RtlZeroMemory(&Query, sizeof(Query));
    Query.NumAdapters = D3DKMT_BRIDGE_MAX_ENUM_ADAPTERS;
    Query.pAdapters = KernelAdapters;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_ENUMADAPTERS2, &Query, sizeof(Query), &Query, sizeof(Query), &Information);
    if (Status == STATUS_BUFFER_OVERFLOW)
        Status = STATUS_BUFFER_TOO_SMALL;
    if (NT_SUCCESS(Status) && Information != sizeof(Query))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Enumerated = Query.NumAdapters;
    if (Enumerated > D3DKMT_BRIDGE_MAX_ENUM_ADAPTERS)
    {
        Status = STATUS_INFO_LENGTH_MISMATCH;
        goto Cleanup;
    }

    /* Compact the survivors to the front and close what the filter removed. */
    for (Index = 0; Index < Enumerated; ++Index)
    {
        if (KernelAdapters[Index].hAdapter == 0)
            continue;
        if (WddmBridgeAdapterPassesFilter(KernelAdapters[Index].hAdapter, Captured.Filter))
        {
            KernelAdapters[Kept++] = KernelAdapters[Index];
            continue;
        }
        CloseAdapter.hAdapter = KernelAdapters[Index].hAdapter;
        WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
    }

    if (UserAdapters == NULL || Capacity == 0)
    {
        /* Count-only query: nothing is handed out, so nothing may stay open. */
        Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS3, NumAdapters), sizeof(Captured.NumAdapters));
        Status = WddmBridgeRejectBadBuffer(Status);
        if (NT_SUCCESS(Status))
        {
            Captured.NumAdapters = Kept;
            Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS3, NumAdapters), &Captured.NumAdapters, sizeof(Captured.NumAdapters));
        }
        for (Index = 0; Index < Kept; ++Index)
        {
            CloseAdapter.hAdapter = KernelAdapters[Index].hAdapter;
            WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
        }
        goto Cleanup;
    }

    if (Capacity < Kept)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto CloseKept;
    }

    Status = WddmBridgeSizeForCount(Kept, sizeof(D3DKMT_ADAPTERINFO), &BufSize);
    if (!NT_SUCCESS(Status))
        goto CloseKept;
    Status = WddmBridgeSafeProbeForWrite(UserAdapters, BufSize);
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        goto CloseKept;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS3, NumAdapters), sizeof(Captured.NumAdapters));
    Status = WddmBridgeRejectBadBuffer(Status);
    if (!NT_SUCCESS(Status))
        goto CloseKept;

    Status = WddmBridgeSafeCopyTo(UserAdapters, KernelAdapters, BufSize);
    if (!NT_SUCCESS(Status))
        goto CloseKept;
    Captured.NumAdapters = Kept;
    Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ENUMADAPTERS3, NumAdapters), &Captured.NumAdapters, sizeof(Captured.NumAdapters));
    if (NT_SUCCESS(Status))
        goto Cleanup;

CloseKept:
    /* The caller never received these handles, so it can never close them. */
    for (Index = 0; Index < Kept; ++Index)
    {
        CloseAdapter.hAdapter = KernelAdapters[Index].hAdapter;
        WddmBridgeSendIoctl(IOCTL_D3DKMT_CLOSEADAPTER, &CloseAdapter, sizeof(CloseAdapter), NULL, 0);
    }

Cleanup:
    ExFreePoolWithTag(KernelAdapters, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTOfferAllocations(
    _In_ CONST D3DKMT_OFFERALLOCATIONS *pData)
{
    D3DKMT_OFFERALLOCATIONS Captured;
    D3DKMT_HANDLE *Handles = NULL;
    SIZE_T HandlesSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.NumAllocations > D3DKMT_BRIDGE_MAX_ALLOCATIONS)
        return STATUS_INVALID_PARAMETER;

    if (Captured.pResources != NULL && Captured.HandleList == NULL)
    {
        Status = WddmBridgeCaptureArray(Captured.pResources, Captured.NumAllocations, sizeof(*Handles), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&Handles, &HandlesSize);
        if (NT_SUCCESS(Status))
            Captured.pResources = Handles;
    }
    else if (Captured.HandleList != NULL && Captured.pResources == NULL)
    {
        Status = WddmBridgeCaptureArray(Captured.HandleList, Captured.NumAllocations, sizeof(*Handles), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&Handles, &HandlesSize);
        if (NT_SUCCESS(Status))
            Captured.HandleList = Handles;
    }
    else
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (NT_SUCCESS(Status))
        Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_OFFERALLOCATIONS, &Captured, sizeof(Captured), NULL, 0);
    if (Handles != NULL)
        ExFreePoolWithTag(Handles, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTReclaimAllocations(
    _Inout_ CONST D3DKMT_RECLAIMALLOCATIONS *pData)
{
    D3DKMT_RECLAIMALLOCATIONS Captured;
    D3DKMT_HANDLE *Handles = NULL;
    BOOL *Discarded = NULL;
    BOOL *UserDiscarded;
    SIZE_T HandlesSize = 0;
    SIZE_T DiscardedSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.NumAllocations > D3DKMT_BRIDGE_MAX_ALLOCATIONS)
        return STATUS_INVALID_PARAMETER;

    UserDiscarded = Captured.pDiscarded;
    if (Captured.pResources != NULL && Captured.HandleList == NULL)
    {
        Status = WddmBridgeCaptureArray(Captured.pResources, Captured.NumAllocations, sizeof(*Handles), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&Handles, &HandlesSize);
        if (NT_SUCCESS(Status))
            Captured.pResources = Handles;
    }
    else if (Captured.HandleList != NULL && Captured.pResources == NULL)
    {
        Status = WddmBridgeCaptureArray(Captured.HandleList, Captured.NumAllocations, sizeof(*Handles), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&Handles, &HandlesSize);
        if (NT_SUCCESS(Status))
            Captured.HandleList = Handles;
    }
    else
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    if (NT_SUCCESS(Status) && UserDiscarded != NULL)
    {
        Status = WddmBridgeCaptureArray(UserDiscarded, Captured.NumAllocations, sizeof(*Discarded), D3DKMT_BRIDGE_MAX_ALLOCATIONS, FALSE, TRUE, (PVOID *)&Discarded, &DiscardedSize);
        if (NT_SUCCESS(Status))
            Captured.pDiscarded = Discarded;
    }
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_RECLAIMALLOCATIONS, &Captured, sizeof(Captured), NULL, 0);
    if (NT_SUCCESS(Status) && Discarded != NULL)
        Status = WddmBridgeSafeCopyTo(UserDiscarded, Discarded, DiscardedSize);

    if (Discarded != NULL)
        ExFreePoolWithTag(Discarded, TAG_WDDM_BRIDGE);
    if (Handles != NULL)
        ExFreePoolWithTag(Handles, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner1(
    _In_ CONST D3DKMT_SETVIDPNSOURCEOWNER1 *pData)
{
    D3DKMT_SETVIDPNSOURCEOWNER1 Captured;
    D3DKMT_VIDPNSOURCEOWNER_TYPE *OwnerTypes = NULL;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID *SourceIds = NULL;
    SIZE_T OwnerTypesSize = 0;
    SIZE_T SourceIdsSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    /* A zero count is a release-all request; the arrays are then ignored. */
    if (Captured.Version0.VidPnSourceCount > D3DKMT_BRIDGE_MAX_ALLOCATIONS ||
        (Captured.Version0.VidPnSourceCount != 0 && (Captured.Version0.pType == NULL || Captured.Version0.pVidPnSourceId == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Captured.Version0.VidPnSourceCount != 0 && Captured.Version0.pType != NULL && Captured.Version0.pVidPnSourceId != NULL)
    {
        Status = WddmBridgeCaptureArray(Captured.Version0.pType, Captured.Version0.VidPnSourceCount, sizeof(*OwnerTypes), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&OwnerTypes, &OwnerTypesSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Status = WddmBridgeCaptureArray(Captured.Version0.pVidPnSourceId, Captured.Version0.VidPnSourceCount, sizeof(*SourceIds), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&SourceIds, &SourceIdsSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Captured.Version0.pType = OwnerTypes;
        Captured.Version0.pVidPnSourceId = SourceIds;
    }
    else
    {
        Captured.Version0.pType = NULL;
        Captured.Version0.pVidPnSourceId = NULL;
    }

    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_SETVIDPNSOURCEOWNER1, &Captured, sizeof(Captured), NULL, 0);

Cleanup:
    if (SourceIds != NULL)
        ExFreePoolWithTag(SourceIds, TAG_WDDM_BRIDGE);
    if (OwnerTypes != NULL)
        ExFreePoolWithTag(OwnerTypes, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSetVidPnSourceOwner2(
    _In_ CONST D3DKMT_SETVIDPNSOURCEOWNER2 *pData)
{
    D3DKMT_SETVIDPNSOURCEOWNER2 Captured;
    D3DKMT_VIDPNSOURCEOWNER_TYPE *OwnerTypes = NULL;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID *SourceIds = NULL;
    D3DKMT_PTR_TYPE *NtHandles = NULL;
    SIZE_T OwnerTypesSize = 0;
    SIZE_T SourceIdsSize = 0;
    SIZE_T NtHandlesSize = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.Version1.Version0.VidPnSourceCount > D3DKMT_BRIDGE_MAX_ALLOCATIONS ||
        (Captured.Version1.Version0.VidPnSourceCount == 0 && (Captured.Version1.Version0.pType != NULL || Captured.Version1.Version0.pVidPnSourceId != NULL || Captured.pVidPnSourceNtHandles != NULL)) ||
        (Captured.Version1.Version0.VidPnSourceCount != 0 && (Captured.Version1.Version0.pType == NULL || Captured.Version1.Version0.pVidPnSourceId == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Captured.Version1.Version0.VidPnSourceCount != 0 && Captured.Version1.Version0.pType != NULL && Captured.Version1.Version0.pVidPnSourceId != NULL)
    {
        Status = WddmBridgeCaptureArray(Captured.Version1.Version0.pType, Captured.Version1.Version0.VidPnSourceCount, sizeof(*OwnerTypes), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&OwnerTypes, &OwnerTypesSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Status = WddmBridgeCaptureArray(Captured.Version1.Version0.pVidPnSourceId, Captured.Version1.Version0.VidPnSourceCount, sizeof(*SourceIds), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&SourceIds, &SourceIdsSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Captured.Version1.Version0.pType = OwnerTypes;
        Captured.Version1.Version0.pVidPnSourceId = SourceIds;
        if (Captured.pVidPnSourceNtHandles != NULL)
        {
            Status = WddmBridgeCaptureArray(Captured.pVidPnSourceNtHandles, Captured.Version1.Version0.VidPnSourceCount, sizeof(*NtHandles), D3DKMT_BRIDGE_MAX_ALLOCATIONS, TRUE, FALSE, (PVOID *)&NtHandles, &NtHandlesSize);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
            Captured.pVidPnSourceNtHandles = NtHandles;
        }
    }
    else
    {
        Captured.Version1.Version0.pType = NULL;
        Captured.Version1.Version0.pVidPnSourceId = NULL;
        Captured.pVidPnSourceNtHandles = NULL;
    }

    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_SETVIDPNSOURCEOWNER2, &Captured, sizeof(Captured), NULL, 0);

Cleanup:
    if (NtHandles != NULL)
        ExFreePoolWithTag(NtHandles, TAG_WDDM_BRIDGE);
    if (SourceIds != NULL)
        ExFreePoolWithTag(SourceIds, TAG_WDDM_BRIDGE);
    if (OwnerTypes != NULL)
        ExFreePoolWithTag(OwnerTypes, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTWaitForVerticalBlankEvent2(
    _In_ CONST D3DKMT_WAITFORVERTICALBLANKEVENT2 *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT2, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTCreateSynchronizationObject2(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT2 *pData)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Captured;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT DestroySync;
    ULONG_PTR Information = 0;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.hDevice == 0)
        return STATUS_INVALID_PARAMETER;
    if (Captured.Info.Type <= 0 || Captured.Info.Type >= D3DDDI_SYNCHRONIZATION_TYPE_LIMIT)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.hSyncObject = 0;
    Captured.Info.SharedHandle = 0;
    if (Captured.Info.Type == D3DDDI_MONITORED_FENCE)
    {
        Captured.Info.MonitoredFence.FenceValueCPUVirtualAddress = NULL;
        Captured.Info.MonitoredFence.FenceValueGPUVirtualAddress = 0;
    }
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT2, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(Captured) || Captured.hSyncObject == 0)
        Status = Information != sizeof(Captured) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_INVALID_HANDLE;
    else if (Captured.Info.Type == D3DDDI_MONITORED_FENCE && (Captured.Info.MonitoredFence.FenceValueCPUVirtualAddress == NULL || WddmBridgeIsKernelPointerForUser(Captured.Info.MonitoredFence.FenceValueCPUVirtualAddress)))
        Status = STATUS_INVALID_ADDRESS;
    else
        Status = WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));

    if (!NT_SUCCESS(Status) && Captured.hSyncObject != 0)
    {
        DestroySync.hSyncObject = Captured.hSyncObject;
        CleanupStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT, &DestroySync, sizeof(DestroySync), NULL, 0);
        if (!NT_SUCCESS(CleanupStatus))
            DPRINT1("D3DKMTCreateSynchronizationObject2: rollback of sync object 0x%X failed with 0x%08lX\n", Captured.hSyncObject, CleanupStatus);
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTOpenSynchronizationObject(
    _Inout_ D3DKMT_OPENSYNCHRONIZATIONOBJECT *pData)
{
    D3DKMT_OPENSYNCHRONIZATIONOBJECT Captured;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.hSharedHandle == 0)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeProbeForWrite(pData, sizeof(*pData));
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.hSyncObject = 0;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_OPENSYNCHRONIZATIONOBJECT, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(Captured))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Captured.hSyncObject == 0)
        return STATUS_INVALID_HANDLE;
    return WddmBridgeSafeCopyTo(pData, &Captured, sizeof(Captured));
}

NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObject2(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2, (PVOID)pData, sizeof(*pData), FALSE);
}

NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObject2(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 *pData)
{
    return WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2, (PVOID)pData, sizeof(*pData), FALSE);
}

/* ---- WDDM 2.0 additions ------------------------------------------------ */

NTSTATUS
APIENTRY
D3DKMTMakeResident(
    _Inout_ D3DDDI_MAKERESIDENT *pData)
{
    D3DDDI_MAKERESIDENT Captured;
    D3DKMT_HANDLE *AllocationList = NULL;
    UINT *PriorityList = NULL;
    SIZE_T AllocationListSize = 0;
    SIZE_T PriorityListSize = 0;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    if (Captured.NumAllocations > D3DKMT_BRIDGE_MAX_ALLOCATIONS || (Captured.NumAllocations != 0 && Captured.AllocationList == NULL) || Captured.Flags.Reserved != 0)
        return STATUS_INVALID_PARAMETER;

    if (Captured.NumAllocations != 0)
    {
        Status = WddmBridgeSizeForCount(Captured.NumAllocations, sizeof(*AllocationList), &AllocationListSize);
        if (!NT_SUCCESS(Status))
            return Status;

        AllocationList = ExAllocatePoolWithTag(NonPagedPool, AllocationListSize, TAG_WDDM_BRIDGE);
        if (AllocationList == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = WddmBridgeSafeCopyFrom(AllocationList, Captured.AllocationList, AllocationListSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        if (Captured.PriorityList != NULL)
        {
            Status = WddmBridgeSizeForCount(Captured.NumAllocations, sizeof(*PriorityList), &PriorityListSize);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            PriorityList = ExAllocatePoolWithTag(NonPagedPool, PriorityListSize, TAG_WDDM_BRIDGE);
            if (PriorityList == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }

            Status = WddmBridgeSafeCopyFrom(PriorityList, Captured.PriorityList, PriorityListSize);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
        }
    }

    Captured.AllocationList = AllocationList;
    Captured.PriorityList = PriorityList;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAKERESIDENT, NumAllocations), sizeof(Captured.NumAllocations));
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAKERESIDENT, PagingFenceValue), sizeof(Captured.PagingFenceValue));
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAKERESIDENT, NumBytesToTrim), sizeof(Captured.NumBytesToTrim));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_MAKERESIDENT, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAKERESIDENT, NumAllocations), &Captured.NumAllocations, sizeof(Captured.NumAllocations));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAKERESIDENT, PagingFenceValue), &Captured.PagingFenceValue, sizeof(Captured.PagingFenceValue));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAKERESIDENT, NumBytesToTrim), &Captured.NumBytesToTrim, sizeof(Captured.NumBytesToTrim));

    /* The IRP completes with STATUS_SUCCESS (an IRP cannot complete as
     * PENDING); a nonzero paging fence in the output is the wire encoding
     * for queued paging work, surfaced as the native STATUS_PENDING. */
    if (Status == STATUS_SUCCESS && Captured.PagingFenceValue != 0)
        Status = STATUS_PENDING;

Cleanup:
    if (PriorityList != NULL)
        ExFreePoolWithTag(PriorityList, TAG_WDDM_BRIDGE);
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTEvict(
    _Inout_ D3DKMT_EVICT *pData)
{
    D3DKMT_EVICT Captured;
    D3DKMT_HANDLE *AllocationList = NULL;
    SIZE_T AllocationListSize = 0;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    if (Captured.NumAllocations > D3DKMT_BRIDGE_MAX_ALLOCATIONS || (Captured.NumAllocations != 0 && Captured.AllocationList == NULL) || Captured.Flags.Reserved != 0)
        return STATUS_INVALID_PARAMETER;

    if (Captured.NumAllocations != 0)
    {
        Status = WddmBridgeSizeForCount(Captured.NumAllocations, sizeof(*AllocationList), &AllocationListSize);
        if (!NT_SUCCESS(Status))
            return Status;

        AllocationList = ExAllocatePoolWithTag(NonPagedPool, AllocationListSize, TAG_WDDM_BRIDGE);
        if (AllocationList == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = WddmBridgeSafeCopyFrom(AllocationList, Captured.AllocationList, AllocationListSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Captured.AllocationList = AllocationList;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_EVICT, NumBytesToTrim), sizeof(Captured.NumBytesToTrim));
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_EVICT, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_EVICT, NumBytesToTrim), &Captured.NumBytesToTrim, sizeof(Captured.NumBytesToTrim));

Cleanup:
    if (AllocationList != NULL)
        ExFreePoolWithTag(AllocationList, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTQueryVideoMemoryInfo(
    _Inout_ D3DKMT_QUERYVIDEOMEMORYINFO *pData)
{
    D3DKMT_QUERYVIDEOMEMORYINFO Captured;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, Budget), sizeof(Captured.Budget));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, CurrentUsage), sizeof(Captured.CurrentUsage));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, CurrentReservation), sizeof(Captured.CurrentReservation));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, AvailableForReservation), sizeof(Captured.AvailableForReservation));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_QUERYVIDEOMEMORYINFO, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && Information != sizeof(Captured))
        Status = STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, Budget), &Captured.Budget, sizeof(Captured.Budget));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, CurrentUsage), &Captured.CurrentUsage, sizeof(Captured.CurrentUsage));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, CurrentReservation), &Captured.CurrentReservation, sizeof(Captured.CurrentReservation));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_QUERYVIDEOMEMORYINFO, AvailableForReservation), &Captured.AvailableForReservation, sizeof(Captured.AvailableForReservation));
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTCreatePagingQueue(
    _Inout_ D3DKMT_CREATEPAGINGQUEUE *pData)
{
    D3DKMT_CREATEPAGINGQUEUE Captured;
    D3DDDI_DESTROYPAGINGQUEUE DestroyPagingQueue;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.hDevice == 0 || Captured.PhysicalAdapterIndex != 0 || Captured.Priority < D3DDDI_PAGINGQUEUE_PRIORITY_BELOW_NORMAL || Captured.Priority > D3DDDI_PAGINGQUEUE_PRIORITY_ABOVE_NORMAL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE, hPagingQueue), sizeof(Captured.hPagingQueue));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE, hSyncObject), sizeof(Captured.hSyncObject));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE, FenceValueCPUVirtualAddress), sizeof(Captured.FenceValueCPUVirtualAddress));
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.hPagingQueue = 0;
    Captured.hSyncObject = 0;
    Captured.FenceValueCPUVirtualAddress = NULL;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_CREATEPAGINGQUEUE, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && (Information != sizeof(Captured) || Captured.hPagingQueue == 0 || Captured.hSyncObject == 0 || Captured.FenceValueCPUVirtualAddress == NULL || (ULONG_PTR)Captured.FenceValueCPUVirtualAddress > (ULONG_PTR)MmHighestUserAddress))
        Status = STATUS_INVALID_DEVICE_STATE;
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE, hPagingQueue), &Captured.hPagingQueue, sizeof(Captured.hPagingQueue));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE, hSyncObject), &Captured.hSyncObject, sizeof(Captured.hSyncObject));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATEPAGINGQUEUE, FenceValueCPUVirtualAddress), &Captured.FenceValueCPUVirtualAddress, sizeof(Captured.FenceValueCPUVirtualAddress));

    if (!NT_SUCCESS(Status) && Captured.hPagingQueue != 0)
    {
        NTSTATUS DestroyStatus;

        DestroyPagingQueue.hPagingQueue = Captured.hPagingQueue;
        DestroyStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYPAGINGQUEUE, &DestroyPagingQueue, sizeof(DestroyPagingQueue), NULL, 0);
        if (!NT_SUCCESS(DestroyStatus))
            DPRINT1("D3DKMTCreatePagingQueue: output validation failed 0x%08lX and queue cleanup failed 0x%08lX\n", Status, DestroyStatus);
    }

    return Status;
}

NTSTATUS
APIENTRY
D3DKMTDestroyPagingQueue(
    _Inout_ D3DDDI_DESTROYPAGINGQUEUE *pData)
{
    D3DDDI_DESTROYPAGINGQUEUE Captured;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYPAGINGQUEUE, &Captured, sizeof(Captured), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTReserveGpuVirtualAddress(
    _Inout_ D3DDDI_RESERVEGPUVIRTUALADDRESS *pData)
{
    D3DDDI_RESERVEGPUVIRTUALADDRESS Captured;
    D3DDDI_RESERVEGPUVIRTUALADDRESS Commit;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, VirtualAddress), sizeof(Captured.VirtualAddress));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, PagingFenceValue), sizeof(Captured.PagingFenceValue));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_DXGKRNL_PREPARERESERVEGPUVIRTUALADDRESS, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && (Information != sizeof(Captured) || Captured.VirtualAddress == 0))
        Status = STATUS_INVALID_DEVICE_STATE;
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, VirtualAddress), &Captured.VirtualAddress, sizeof(Captured.VirtualAddress));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DDDI_RESERVEGPUVIRTUALADDRESS, PagingFenceValue), &Captured.PagingFenceValue, sizeof(Captured.PagingFenceValue));
    if (!NT_SUCCESS(Status))
        return Status;

    Commit = Captured;
    Commit.BaseAddress = Captured.VirtualAddress;
    Information = 0;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_RESERVEGPUVIRTUALADDRESS, &Commit, sizeof(Commit), &Commit, sizeof(Commit), &Information);
    if (NT_SUCCESS(Status) && (Information != sizeof(Commit) || Commit.VirtualAddress != Captured.VirtualAddress))
        Status = STATUS_INVALID_DEVICE_STATE;
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTMapGpuVirtualAddress(
    _Inout_ D3DDDI_MAPGPUVIRTUALADDRESS *pData)
{
    D3DDDI_MAPGPUVIRTUALADDRESS Captured;
    D3DDDI_MAPGPUVIRTUALADDRESS Commit;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, VirtualAddress), sizeof(Captured.VirtualAddress));
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, PagingFenceValue), sizeof(Captured.PagingFenceValue));
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_DXGKRNL_PREPAREMAPGPUVIRTUALADDRESS, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information != sizeof(Captured) || Captured.VirtualAddress == 0)
        return STATUS_INVALID_DEVICE_STATE;
    Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, VirtualAddress), &Captured.VirtualAddress, sizeof(Captured.VirtualAddress));
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DDDI_MAPGPUVIRTUALADDRESS, PagingFenceValue), &Captured.PagingFenceValue, sizeof(Captured.PagingFenceValue));
    if (!NT_SUCCESS(Status))
        return Status;

    Commit = Captured;
    Commit.BaseAddress = Captured.VirtualAddress;
    Information = 0;
    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_MAPGPUVIRTUALADDRESS, &Commit, sizeof(Commit), &Commit, sizeof(Commit), &Information);
    if (NT_SUCCESS(Status) && (Information != sizeof(Commit) || Commit.VirtualAddress != Captured.VirtualAddress))
        Status = STATUS_INVALID_DEVICE_STATE;
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTFreeGpuVirtualAddress(
    _In_ CONST D3DKMT_FREEGPUVIRTUALADDRESS *pData)
{
    D3DKMT_FREEGPUVIRTUALADDRESS Captured;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_FREEGPUVIRTUALADDRESS, &Captured, sizeof(Captured), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTUpdateGpuVirtualAddress(
    _In_ CONST D3DKMT_UPDATEGPUVIRTUALADDRESS *pData)
{
    D3DKMT_UPDATEGPUVIRTUALADDRESS Captured;
    D3DDDI_UPDATEGPUVIRTUALADDRESS_OPERATION *Operations = NULL;
    SIZE_T OperationsSize;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.NumOperations == 0 || Captured.NumOperations > D3DKMT_BRIDGE_MAX_ALLOCATIONS || Captured.Operations == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSizeForCount(Captured.NumOperations, sizeof(*Operations), &OperationsSize);
    if (!NT_SUCCESS(Status))
        return Status;
    Operations = ExAllocatePoolWithTag(NonPagedPool, OperationsSize, TAG_WDDM_BRIDGE);
    if (Operations == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = WddmBridgeSafeCopyFrom(Operations, Captured.Operations, OperationsSize);
    if (NT_SUCCESS(Status))
    {
        Captured.Operations = Operations;
        Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_UPDATEGPUVIRTUALADDRESS, &Captured, sizeof(Captured), NULL, 0);
    }
    ExFreePoolWithTag(Operations, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSignalSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Captured;
    D3DKMT_HANDLE ObjectHandles[D3DDDI_MAX_OBJECT_SIGNALED];
    UINT64 FenceValues[D3DDDI_MAX_OBJECT_SIGNALED];
    SIZE_T HandleArraySize;
    SIZE_T FenceArraySize;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.ObjectCount == 0 || Captured.ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED || Captured.ObjectHandleArray == NULL || Captured.FenceValueArray == NULL)
        return STATUS_INVALID_PARAMETER;
    if ((Captured.Flags.Value & ~0x00000004UL) != 0)
        return STATUS_NOT_SUPPORTED;

    Status = WddmBridgeSizeForCount(Captured.ObjectCount, sizeof(ObjectHandles[0]), &HandleArraySize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSizeForCount(Captured.ObjectCount, sizeof(FenceValues[0]), &FenceArraySize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeCopyFrom(ObjectHandles, Captured.ObjectHandleArray, HandleArraySize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeCopyFrom(FenceValues, Captured.FenceValueArray, FenceArraySize);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.ObjectHandleArray = ObjectHandles;
    Captured.FenceValueArray = FenceValues;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU, &Captured, sizeof(Captured), NULL, 0);
}

NTSTATUS
APIENTRY
D3DKMTWaitForSynchronizationObjectFromCpu(
    _In_ CONST D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU *pData)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Captured;
    D3DKMT_HANDLE ObjectHandles[D3DDDI_MAX_OBJECT_WAITED_ON];
    UINT64 FenceValues[D3DDDI_MAX_OBJECT_WAITED_ON];
    SIZE_T HandleArraySize;
    SIZE_T FenceArraySize;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.ObjectCount == 0 || Captured.ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON || Captured.ObjectHandleArray == NULL || Captured.FenceValueArray == NULL)
        return STATUS_INVALID_PARAMETER;
    if ((Captured.Flags.Value & ~1UL) != 0)
        return STATUS_NOT_SUPPORTED;

    Status = WddmBridgeSizeForCount(Captured.ObjectCount, sizeof(ObjectHandles[0]), &HandleArraySize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSizeForCount(Captured.ObjectCount, sizeof(FenceValues[0]), &FenceArraySize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeCopyFrom(ObjectHandles, Captured.ObjectHandleArray, HandleArraySize);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WddmBridgeSafeCopyFrom(FenceValues, Captured.FenceValueArray, FenceArraySize);
    if (!NT_SUCCESS(Status))
        return Status;

    Captured.ObjectHandleArray = ObjectHandles;
    Captured.FenceValueArray = FenceValues;
    return WddmBridgeSendIoctl(IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU, &Captured, sizeof(Captured), NULL, 0);
}

/* ---- WDDM 2.0 virtual-context command path ------------------------------ */

NTSTATUS
APIENTRY
D3DKMTCreateContextVirtual(
    _Inout_ D3DKMT_CREATECONTEXTVIRTUAL *pData)
{
    D3DKMT_CREATECONTEXTVIRTUAL Captured;
    PRXGK_CREATECONTEXTVIRTUAL_PACKET Packet;
    SIZE_T PacketSize;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    if ((Captured.Flags.Value & ~RXGK_CREATECONTEXTVIRTUAL_SUPPORTED_FLAGS) != 0 || (Captured.Flags.Value & RXGK_CREATECONTEXTVIRTUAL_FLAG_NULL_RENDERING) == 0)
        return STATUS_NOT_SUPPORTED;

    if (Captured.hDevice == 0 ||
        Captured.PrivateDriverDataSize > RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA ||
        (Captured.PrivateDriverDataSize != 0 &&
         Captured.pPrivateDriverData == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WddmBridgeSizeAdd(sizeof(*Packet), Captured.PrivateDriverDataSize, &PacketSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WddmBridgeSafeProbeForWrite((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATECONTEXTVIRTUAL, hContext), sizeof(Captured.hContext));
    if (!NT_SUCCESS(Status))
        return Status;

    Packet = ExAllocatePoolWithTag(NonPagedPool, PacketSize, TAG_WDDM_BRIDGE);
    if (Packet == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Packet, PacketSize);
    Packet->Size = (ULONG)PacketSize;
    Packet->Version = RXGK_WDDM_PACKET_VERSION_1;
    Packet->DeviceHandle = Captured.hDevice;
    Packet->NodeOrdinal = Captured.NodeOrdinal;
    Packet->EngineAffinity = Captured.EngineAffinity;
    Packet->Flags = Captured.Flags.Value;
    Packet->ClientHint = (ULONG)Captured.ClientHint;
    Packet->PrivateDriverDataSize = Captured.PrivateDriverDataSize;
    if (Captured.PrivateDriverDataSize != 0)
    {
        Packet->PrivateDriverDataOffset = sizeof(*Packet);
        Status = WddmBridgeSafeCopyFrom((PUCHAR)Packet + Packet->PrivateDriverDataOffset, Captured.pPrivateDriverData, Captured.PrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_CREATECONTEXTVIRTUAL, Packet, (ULONG)PacketSize, Packet, sizeof(*Packet), &Information);
    if (NT_SUCCESS(Status) && (Information != sizeof(*Packet) || Packet->ContextHandle == 0))
        Status = STATUS_INVALID_DEVICE_STATE;
    if (NT_SUCCESS(Status))
        Status = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_CREATECONTEXTVIRTUAL, hContext), &Packet->ContextHandle, sizeof(Packet->ContextHandle));
    if (!NT_SUCCESS(Status) && Packet->ContextHandle != 0)
    {
        D3DKMT_DESTROYCONTEXT DestroyContext;
        NTSTATUS DestroyStatus;

        RtlZeroMemory(&DestroyContext, sizeof(DestroyContext));
        DestroyContext.hContext = Packet->ContextHandle;
        DestroyStatus = WddmBridgeSendIoctl(IOCTL_D3DKMT_DESTROYCONTEXT, &DestroyContext, sizeof(DestroyContext), NULL, 0);
        if (!NT_SUCCESS(DestroyStatus))
            DPRINT1("D3DKMTCreateContextVirtual: output validation failed 0x%08lX and context cleanup failed 0x%08lX\n", Status, DestroyStatus);
    }

Cleanup:
    ExFreePoolWithTag(Packet, TAG_WDDM_BRIDGE);
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTSubmitCommand(
    _In_ CONST D3DKMT_SUBMITCOMMAND *pData)
{
    D3DKMT_SUBMITCOMMAND Captured;
    PRXGK_SUBMITCOMMAND_PACKET Packet;
    ULONG FlagsValue;
    SIZE_T PacketSize;
    NTSTATUS Status;

    C_ASSERT(sizeof(D3DKMT_SUBMITCOMMANDFLAGS) == sizeof(FlagsValue));

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured));
    if (!NT_SUCCESS(Status))
        return Status;

    RtlCopyMemory(&FlagsValue, &Captured.Flags, sizeof(FlagsValue));

    if (Captured.BroadcastContextCount == 0 ||
        Captured.BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT ||
        Captured.NumPrimaries > D3DDDI_MAX_WRITTEN_PRIMARIES)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Captured.BroadcastContextCount != 1 ||
        Captured.NumPrimaries != 0 ||
        Captured.NumHistoryBuffers != 0 ||
        Captured.PresentHistoryToken != 0 ||
        (FlagsValue & ~RXGK_SUBMITCOMMAND_SUPPORTED_FLAGS) != 0 ||
        (FlagsValue & RXGK_SUBMITCOMMAND_FLAG_NULL_RENDERING) == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Captured.BroadcastContext[0] == 0 ||
        Captured.Commands == 0 ||
        Captured.CommandLength == 0 ||
        Captured.Commands > MAXULONGLONG - Captured.CommandLength ||
        Captured.PrivateDriverDataSize > RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA ||
        (Captured.PrivateDriverDataSize != 0 &&
         Captured.pPrivateDriverData == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WddmBridgeSizeAdd(sizeof(*Packet), Captured.PrivateDriverDataSize, &PacketSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Packet = ExAllocatePoolWithTag(NonPagedPool, PacketSize, TAG_WDDM_BRIDGE);
    if (Packet == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Packet, PacketSize);
    Packet->Size = (ULONG)PacketSize;
    Packet->Version = RXGK_WDDM_PACKET_VERSION_1;
    Packet->Commands = Captured.Commands;
    Packet->CommandLength = Captured.CommandLength;
    Packet->Flags = FlagsValue;
    Packet->PresentHistoryToken = Captured.PresentHistoryToken;
    Packet->ContextHandle = Captured.BroadcastContext[0];
    Packet->PrivateDriverDataSize = Captured.PrivateDriverDataSize;
    if (Captured.PrivateDriverDataSize != 0)
    {
        Packet->PrivateDriverDataOffset = sizeof(*Packet);
        Status = WddmBridgeSafeCopyFrom((PUCHAR)Packet + Packet->PrivateDriverDataOffset, Captured.pPrivateDriverData, Captured.PrivateDriverDataSize);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    Status = WddmBridgeSendIoctl(IOCTL_D3DKMT_SUBMITCOMMAND, Packet, (ULONG)PacketSize, NULL, 0);

Cleanup:
    ExFreePoolWithTag(Packet, TAG_WDDM_BRIDGE);
    return Status;
}

/* ---- Keyed mutexes ------------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTCreateKeyedMutex(
    _Inout_ D3DKMT_CREATEKEYEDMUTEX *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_CREATEKEYEDMUTEX, pData, sizeof(*pData), TRUE));
}

NTSTATUS
APIENTRY
D3DKMTCreateKeyedMutex2(
    _Inout_ D3DKMT_CREATEKEYEDMUTEX2 *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_CREATEKEYEDMUTEX2, pData, sizeof(*pData), TRUE));
}

NTSTATUS
APIENTRY
D3DKMTOpenKeyedMutex(
    _Inout_ D3DKMT_OPENKEYEDMUTEX *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_OPENKEYEDMUTEX, pData, sizeof(*pData), TRUE));
}

NTSTATUS
APIENTRY
D3DKMTOpenKeyedMutex2(
    _Inout_ D3DKMT_OPENKEYEDMUTEX2 *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_OPENKEYEDMUTEX2, pData, sizeof(*pData), TRUE));
}

NTSTATUS
APIENTRY
D3DKMTDestroyKeyedMutex(
    _In_ CONST D3DKMT_DESTROYKEYEDMUTEX *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_DESTROYKEYEDMUTEX, (PVOID)pData, sizeof(*pData), FALSE));
}

/*
 * Acquire can block for as long as the caller's timeout allows, so the timeout
 * pointer must not be dereferenced in the kernel through a user address: it is
 * captured here and the captured copy travels with the request.
 */
NTSTATUS
APIENTRY
D3DKMTAcquireKeyedMutex(
    _Inout_ D3DKMT_ACQUIREKEYEDMUTEX *pData)
{
    D3DKMT_ACQUIREKEYEDMUTEX Captured;
    LARGE_INTEGER Timeout;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeRejectBadBuffer(WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured)));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.pTimeout != NULL)
    {
        Status = WddmBridgeRejectBadBuffer(WddmBridgeSafeCopyFrom(&Timeout, Captured.pTimeout, sizeof(Timeout)));
        if (!NT_SUCCESS(Status))
            return Status;
        Captured.pTimeout = &Timeout;
    }

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_ACQUIREKEYEDMUTEX, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && Status != STATUS_TIMEOUT && Information != sizeof(Captured))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (NT_SUCCESS(Status) && Status != STATUS_TIMEOUT)
    {
        NTSTATUS CopyStatus = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ACQUIREKEYEDMUTEX, FenceValue),
                                                   &Captured.FenceValue, sizeof(Captured.FenceValue));
        if (!NT_SUCCESS(CopyStatus))
            Status = CopyStatus;
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTAcquireKeyedMutex2(
    _Inout_ D3DKMT_ACQUIREKEYEDMUTEX2 *pData)
{
    D3DKMT_ACQUIREKEYEDMUTEX2 Captured;
    LARGE_INTEGER Timeout;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = WddmBridgeRejectBadBuffer(WddmBridgeSafeCopyFrom(&Captured, pData, sizeof(Captured)));
    if (!NT_SUCCESS(Status))
        return Status;
    if (Captured.PrivateRuntimeDataSize != 0 && Captured.pPrivateRuntimeData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Captured.PrivateRuntimeDataSize > D3DKMT_BRIDGE_MAX_PRIVATE_BYTES)
        return STATUS_INVALID_PARAMETER;
    if (Captured.pTimeout != NULL)
    {
        Status = WddmBridgeRejectBadBuffer(WddmBridgeSafeCopyFrom(&Timeout, Captured.pTimeout, sizeof(Timeout)));
        if (!NT_SUCCESS(Status))
            return Status;
        Captured.pTimeout = &Timeout;
    }
    if (Captured.PrivateRuntimeDataSize != 0)
    {
        Status = WddmBridgeRejectBadBuffer(WddmBridgeSafeProbeForWrite(Captured.pPrivateRuntimeData, Captured.PrivateRuntimeDataSize));
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = WddmBridgeSendIoctlWithInformation(IOCTL_D3DKMT_ACQUIREKEYEDMUTEX2, &Captured, sizeof(Captured), &Captured, sizeof(Captured), &Information);
    if (NT_SUCCESS(Status) && Status != STATUS_TIMEOUT)
    {
        NTSTATUS CopyStatus = WddmBridgeSafeCopyTo((PUCHAR)pData + FIELD_OFFSET(D3DKMT_ACQUIREKEYEDMUTEX2, FenceValue),
                                                   &Captured.FenceValue, sizeof(Captured.FenceValue));
        if (!NT_SUCCESS(CopyStatus))
            Status = CopyStatus;
    }
    return Status;
}

NTSTATUS
APIENTRY
D3DKMTReleaseKeyedMutex(
    _Inout_ D3DKMT_RELEASEKEYEDMUTEX *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_RELEASEKEYEDMUTEX, pData, sizeof(*pData), FALSE));
}

NTSTATUS
APIENTRY
D3DKMTReleaseKeyedMutex2(
    _Inout_ D3DKMT_RELEASEKEYEDMUTEX2 *pData)
{
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_RELEASEKEYEDMUTEX2, pData, sizeof(*pData), FALSE));
}


/* ---- Trim notification --------------------------------------------------- */

NTSTATUS
APIENTRY
D3DKMTRegisterTrimNotification(
    _Inout_ D3DKMT_REGISTERTRIMNOTIFICATION *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_REGISTERTRIMNOTIFICATION, pData, sizeof(*pData), TRUE));
}

NTSTATUS
APIENTRY
D3DKMTUnregisterTrimNotification(
    _Inout_ D3DKMT_UNREGISTERTRIMNOTIFICATION *pData)
{
    return WddmBridgeRejectBadBuffer(WddmBridgeCaptureFixedIoctl(IOCTL_D3DKMT_UNREGISTERTRIMNOTIFICATION, pData, sizeof(*pData), FALSE));
}

/* EOF */
