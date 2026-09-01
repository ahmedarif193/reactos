/*
 * PROJECT:     ReactOS Raspberry Pi 3 VC4 user-mode support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private D3DKMT wrapper for Mesa's VC4 winsys
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#ifndef _REACTOS_RPI3VC4KMT_H_
#define _REACTOS_RPI3VC4KMT_H_

#ifndef DXGKDDI_INTERFACE_VERSION
#define DXGKDDI_INTERFACE_VERSION 0xF003
#endif

#include <windef.h>
#include <winbase.h>
#include <d3dkmthk.h>
#include <reactos/rpi3vc4_umd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RPI3VC4KMT_DEVICE RPI3VC4KMT_DEVICE;

typedef struct _RPI3VC4KMT_BO
{
    D3DKMT_HANDLE hAllocation;
    UINT Size;
    PVOID CpuVa;
    ULONG Flags;
} RPI3VC4KMT_BO;

#define RPI3VC4KMT_BO_SHADER             0x00000001u
#define RPI3VC4KMT_BO_CPU_DIRTY          0x00000002u

typedef struct _RPI3VC4KMT_FENCE
{
    D3DKMT_HANDLE hSyncObject;
    UINT64 Value;
    volatile const UINT64 *CpuValue;
} RPI3VC4KMT_FENCE;

typedef struct _RPI3VC4KMT_SUBMIT_CL
{
    const VOID *BinCl;
    UINT BinClSize;
    const VOID *ShaderRec;
    UINT ShaderRecSize;
    UINT ShaderRecCount;
    const VOID *Uniforms;
    UINT UniformsSize;
    const RPI3VC4KMT_BO *const *Bos;
    UINT BoCount;
    USHORT Width;
    USHORT Height;
    UCHAR MinXTile;
    UCHAR MinYTile;
    UCHAR MaxXTile;
    UCHAR MaxYTile;
    RPI3VC4_SUBMIT_RCL_SURFACE ColorRead;
    RPI3VC4_SUBMIT_RCL_SURFACE ColorWrite;
    RPI3VC4_SUBMIT_RCL_SURFACE ZsRead;
    RPI3VC4_SUBMIT_RCL_SURFACE ZsWrite;
    RPI3VC4_SUBMIT_RCL_SURFACE MsaaColorWrite;
    RPI3VC4_SUBMIT_RCL_SURFACE MsaaZsWrite;
    ULONG ClearColor[2];
    ULONG ClearZ;
    UCHAR ClearS;
    ULONG Flags;
} RPI3VC4KMT_SUBMIT_CL;

NTSTATUS
rpi3vc4kmt_open(
    _Outptr_ RPI3VC4KMT_DEVICE **DeviceOut);

VOID
rpi3vc4kmt_close(
    _In_opt_ RPI3VC4KMT_DEVICE *Device);

const RPI3VC4_ESCAPE_INFO *
rpi3vc4kmt_info(
    _In_ const RPI3VC4KMT_DEVICE *Device);

NTSTATUS
rpi3vc4kmt_bo_create(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ UINT Size,
    _Out_ RPI3VC4KMT_BO *Bo);

NTSTATUS
rpi3vc4kmt_bo_create_shader(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ UINT Size,
    _Out_ RPI3VC4KMT_BO *Bo);

NTSTATUS
rpi3vc4kmt_bo_map(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Inout_ RPI3VC4KMT_BO *Bo,
    _Outptr_ PVOID *CpuVaOut);

NTSTATUS
rpi3vc4kmt_bo_invalidate(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_BO *Bo,
    _In_ UINT Offset,
    _In_ UINT Length);

NTSTATUS
rpi3vc4kmt_shared_resource_info(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _Out_writes_bytes_to_opt_(RuntimeDataCapacity, *RuntimeDataSize)
        PVOID RuntimeData,
    _In_ UINT RuntimeDataCapacity,
    _Out_ UINT *RuntimeDataSize);

NTSTATUS
rpi3vc4kmt_bo_open_shared(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ D3DKMT_HANDLE hGlobalShare,
    _In_ UINT Size,
    _Out_ RPI3VC4KMT_BO *Bo,
    _Out_ D3DKMT_HANDLE *hResource);

NTSTATUS
rpi3vc4kmt_bo_close_shared(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Inout_ RPI3VC4KMT_BO *Bo,
    _In_ D3DKMT_HANDLE hResource);

NTSTATUS
rpi3vc4kmt_primary_info(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Out_ D3DKMT_HANDLE *hGlobalShare,
    _Out_ UINT *Width,
    _Out_ UINT *Height,
    _Out_ UINT *Pitch);

NTSTATUS
rpi3vc4kmt_present_primary(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_BO *Primary,
    _In_opt_ HWND Window,
    _In_ const RECT *DirtyRect);

NTSTATUS
rpi3vc4kmt_bo_destroy(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _Inout_ RPI3VC4KMT_BO *Bo);

NTSTATUS
rpi3vc4kmt_submit_cl(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_SUBMIT_CL *Submit,
    _Out_ RPI3VC4KMT_FENCE *FenceOut);

NTSTATUS
rpi3vc4kmt_wait(
    _In_ RPI3VC4KMT_DEVICE *Device,
    _In_ const RPI3VC4KMT_FENCE *Fence,
    _In_ DWORD TimeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* _REACTOS_RPI3VC4KMT_H_ */
