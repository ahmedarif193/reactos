/*
 * PROJECT:     ReactOS Raspberry Pi 5 VC4 user-mode support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tiny private D3DKMT wrapper consumed by the VC4 UMD winsys.
 *
 * This is not a Windows public ABI.  It is a convenience library over the
 * standard D3DKMT calls plus the rpi5vc4 private driver escape contract.
 */

#ifndef _REACTOS_VC4KMT_H_
#define _REACTOS_VC4KMT_H_

#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif

#include <windef.h>
#include <winbase.h>
#include <d3dkmthk.h>
#include <reactos/rpi5vc4_umd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _VC4KMT_DEVICE VC4KMT_DEVICE;

typedef struct _VC4KMT_BO
{
    D3DKMT_HANDLE hAllocation;
    UINT Size;
    PVOID CpuVa;
    ULONG GpuVa;
} VC4KMT_BO;

typedef struct _VC4KMT_FENCE
{
    D3DKMT_HANDLE hSyncObject;
    UINT64 Value;
    volatile const UINT64 *CpuValue;
} VC4KMT_FENCE;

#define VC4KMT_RESOURCE_CPU_DIRTY 0x00000001u
#define VC4KMT_CL_FLAG_FLUSH_CACHE 0x00000001u
#define VC4KMT_CL_FLAG_BCL_INDEPENDENT 0x00000002u
#define VC4KMT_BO_CREATE_CPU_CACHED 0x00000001u

/* This is a WIRE format: it is copied verbatim into the submit escape and
 * parsed by dxgkrnl (DXGK_VIRTGPU_RESOURCE_ENTRY) and by the rpi5vc4
 * miniport, both of which derive the entry stride from the list magic.
 * Do not change its size without versioning all three. */
typedef struct _VC4KMT_RESOURCE
{
    D3DKMT_HANDLE hAllocation;
    ULONG Flags;
} VC4KMT_RESOURCE;

typedef struct _VC4KMT_RESOURCE_OWNER_UPDATE
{
    D3DKMT_HANDLE hAllocation;
    HANDLE ExpectedRuntimeResource;
    HANDLE RuntimeResource;
} VC4KMT_RESOURCE_OWNER_UPDATE;

typedef enum _VC4KMT_ENGINE
{
    Vc4KmtEngine3d = RPI5VC4_NODE_3D,
    Vc4KmtEngineTfu = RPI5VC4_NODE_TFU,
    Vc4KmtEngineCsd = RPI5VC4_NODE_CSD
} VC4KMT_ENGINE;

/*
 * Control-list submission, expressed in absolute V3D GPU virtual addresses
 * (into the always-resident VRAM slab).  The kernel executes it directly —
 * no per-BO relocation or residency list — so the UMD supplies final GPU VAs
 * it already computed via vc4kmt_bo_gpuva().  Qms==0 selects the device's
 * global binner overflow pool (clear-only smoke jobs); a real render supplies
 * the per-job tile allocation (Qma/Qms) and tile state array (Qts).
 */
typedef struct _VC4KMT_CL_SUBMIT
{
    ULONG BclStart;   /* binning list GPU VA; BclStart==BclEnd = no binning */
    ULONG BclEnd;
    ULONG RclStart;   /* render list GPU VA */
    ULONG RclEnd;
    ULONG Qma;        /* tile allocation memory GPU VA */
    ULONG Qms;        /* tile allocation memory size (0 = global pool) */
    ULONG Qts;        /* tile state data array GPU VA */
} VC4KMT_CL_SUBMIT;

typedef struct _VC4KMT_TFU_SUBMIT
{
    ULONG Regs[12];
} VC4KMT_TFU_SUBMIT;

typedef struct _VC4KMT_CSD_SUBMIT
{
    ULONG Cfg[8];
} VC4KMT_CSD_SUBMIT;

NTSTATUS
vc4kmt_open(
    _Outptr_ VC4KMT_DEVICE **DeviceOut);

/*
 * Open the transport as part of an existing WDDM user-mode device.  The
 * adapter is the kernel adapter handle supplied to OpenAdapter, Device is the
 * opaque runtime device handle supplied to CreateDevice, and Callbacks points
 * at that device's D3DDDI_DEVICECALLBACKS table.  Keeping the callback table
 * opaque here avoids making OpenGL-only users of vc4kmt depend on d3dumddi.h.
 */
NTSTATUS
vc4kmt_open_umd(
    _In_ HANDLE Adapter,
    _In_ HANDLE Device,
    _In_ const VOID *Callbacks,
    _Outptr_ VC4KMT_DEVICE **DeviceOut);

VOID
vc4kmt_close(
    _In_opt_ VC4KMT_DEVICE *Device);

const RPI5VC4_ESCAPE_INFO *
vc4kmt_info(
    _In_ const VC4KMT_DEVICE *Device);

NTSTATUS
vc4kmt_bo_create(
    _In_ VC4KMT_DEVICE *Device,
    _In_ UINT Size,
    _Out_ VC4KMT_BO *Bo);

NTSTATUS
vc4kmt_bo_create_ex(
    _In_ VC4KMT_DEVICE *Device,
    _In_ UINT Size,
    _In_ ULONG Flags,
    _Out_ VC4KMT_BO *Bo);

NTSTATUS
vc4kmt_bo_create_resource_ex(
    _In_ VC4KMT_DEVICE *Device,
    _In_ UINT Size,
    _In_ ULONG Flags,
    _In_opt_ HANDLE RuntimeResource,
    _Out_ VC4KMT_BO *Bo);

NTSTATUS
vc4kmt_bo_create_resource_private_ex(
    _In_ VC4KMT_DEVICE *Device,
    _In_ UINT Size,
    _In_ ULONG Flags,
    _In_opt_ HANDLE RuntimeResource,
    _In_reads_bytes_opt_(ResourcePrivateDataSize)
        const VOID *ResourcePrivateData,
    _In_ UINT ResourcePrivateDataSize,
    _Out_ VC4KMT_BO *Bo);

NTSTATUS
vc4kmt_bo_adopt_resource(
    _In_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hAllocation,
    _In_ UINT Size,
    _In_ HANDLE RuntimeResource,
    _Out_ VC4KMT_BO *Bo);

NTSTATUS
vc4kmt_bo_rebind_resource_owners(
    _In_ VC4KMT_DEVICE *Device,
    _In_reads_(UpdateCount) const VC4KMT_RESOURCE_OWNER_UPDATE *Updates,
    _In_ UINT UpdateCount);

NTSTATUS
vc4kmt_bo_map(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo,
    _Outptr_ PVOID *CpuVaOut);

NTSTATUS
vc4kmt_bo_invalidate(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_BO *Bo,
    _In_ UINT Offset,
    _In_ UINT Length);

ULONG
vc4kmt_bo_gpuva(
    _In_ const VC4KMT_BO *Bo);

NTSTATUS
vc4kmt_shared_resource_info(
    _In_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _Out_writes_bytes_to_opt_(RuntimeDataCapacity, *RuntimeDataSize)
        PVOID RuntimeData,
    _In_ UINT RuntimeDataCapacity,
    _Out_ UINT *RuntimeDataSize);

NTSTATUS
vc4kmt_bo_open_shared(
    _In_ VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_ UINT Size,
    _Out_ VC4KMT_BO *Bo,
    _Out_ D3DKMT_HANDLE *hResource);

NTSTATUS
vc4kmt_bo_close_shared(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo,
    _In_ D3DKMT_HANDLE hResource);

NTSTATUS
vc4kmt_primary_gpuva(
    _In_ VC4KMT_DEVICE *Device,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Pitch,
    _Out_ ULONG *GpuVaOut);

D3DKMT_HANDLE
vc4kmt_primary_allocation(
    _In_ const VC4KMT_DEVICE *Device);

HANDLE
vc4kmt_context(
    _In_ const VC4KMT_DEVICE *Device,
    _In_ VC4KMT_ENGINE Engine);

NTSTATUS
vc4kmt_primary_present(
    _In_ VC4KMT_DEVICE *Device,
    _In_ HWND Window);

VOID
vc4kmt_primary_invalidate(
    _In_opt_ VC4KMT_DEVICE *Device);

NTSTATUS
vc4kmt_bo_destroy(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo);

NTSTATUS
vc4kmt_submit_cl(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CL_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_submit_cl_resources(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CL_SUBMIT *Submit,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_submit_cl_resources_ex(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CL_SUBMIT *Submit,
    _In_ ULONG Flags,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_submit_tfu(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_TFU_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_submit_tfu_resources(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_TFU_SUBMIT *Submit,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_submit_csd(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CSD_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_submit_csd_resources(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CSD_SUBMIT *Submit,
    _In_reads_opt_(ResourceCount) const VC4KMT_RESOURCE *Resources,
    _In_ UINT ResourceCount,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_wait(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_FENCE *Fence,
    _In_ DWORD TimeoutMs);

NTSTATUS
vc4kmt_wait_many(
    _In_ VC4KMT_DEVICE *Device,
    _In_reads_opt_(FenceCount) const VC4KMT_FENCE *Fences,
    _In_ UINT FenceCount,
    _In_ DWORD TimeoutMs);

NTSTATUS
vc4kmt_wait_async(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_FENCE *Fence,
    _In_ HANDLE CompletionEvent);

NTSTATUS
vc4kmt_wait_async_many(
    _In_ VC4KMT_DEVICE *Device,
    _In_reads_opt_(FenceCount) const VC4KMT_FENCE *Fences,
    _In_ UINT FenceCount,
    _In_ HANDLE CompletionEvent);

NTSTATUS
vc4kmt_wait_gpu(
    _In_ VC4KMT_DEVICE *Device,
    _In_ VC4KMT_ENGINE Engine,
    _In_ const VC4KMT_FENCE *Fence);

NTSTATUS
vc4kmt_wait_gpu_many(
    _In_ VC4KMT_DEVICE *Device,
    _In_ VC4KMT_ENGINE Engine,
    _In_reads_opt_(FenceCount) const VC4KMT_FENCE *Fences,
    _In_ UINT FenceCount);

VOID
vc4kmt_fence_destroy(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_FENCE *Fence);

#ifdef __cplusplus
}
#endif

#endif /* _REACTOS_VC4KMT_H_ */
