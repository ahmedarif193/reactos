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
vc4kmt_bo_map(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_BO *Bo,
    _Outptr_ PVOID *CpuVaOut);

ULONG
vc4kmt_bo_gpuva(
    _In_ const VC4KMT_BO *Bo);

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
vc4kmt_submit_tfu(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_TFU_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_submit_csd(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_CSD_SUBMIT *Submit,
    _Out_ VC4KMT_FENCE *FenceOut);

NTSTATUS
vc4kmt_wait(
    _In_ VC4KMT_DEVICE *Device,
    _In_ const VC4KMT_FENCE *Fence,
    _In_ DWORD TimeoutMs);

VOID
vc4kmt_fence_destroy(
    _In_ VC4KMT_DEVICE *Device,
    _Inout_ VC4KMT_FENCE *Fence);

#ifdef __cplusplus
}
#endif

#endif /* _REACTOS_VC4KMT_H_ */
