/*
 * PROJECT:     ReactOS Display Driver Model
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Versioned private win32k <-> dxgkrnl WDDM IOCTL packets
 * COPYRIGHT:   Copyright 2026 ReactOS Contributors
 */

#pragma once

/* Keep this value in sync with rxgkinterface.h. */
#ifndef DXGKRNL_DEVICE_TYPE
#define DXGKRNL_DEVICE_TYPE 0x23
#endif

/*
 * These function numbers belong to the versioned ReactOS-private WDDM bridge.
 * The public D3DKMT structures are deliberately not used as the wire format:
 * they contain caller pointers, while this kernel-to-kernel IOCTL is issued
 * with RequestorMode == KernelMode.  Every variable-length input is captured
 * by win32k and stored inline at a checked byte offset.
 */
#define IOCTL_D3DKMT_CREATECONTEXTVIRTUAL \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x18B, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SUBMITCOMMAND \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x18C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
#define IOCTL_D3DKMT_ISFEATUREENABLED \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1BC, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#define IOCTL_D3DKMT_GETRESOURCEPRESENTPRIVATEDRIVERDATA \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1BD, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_INVALIDATECACHE \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1BE, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RECLAIMALLOCATIONS2 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1BF, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_UPDATEALLOCATIONPROPERTY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C0, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_RECLAIMALLOCATIONS3 \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C1, METHOD_BUFFERED, FILE_ANY_ACCESS)
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
#define IOCTL_D3DKMT_GETDWMVERTICALBLANKEVENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETSYNCREFRESHCOUNTWAITTARGET \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#define IOCTL_D3DKMT_SETCONTEXTINPROCESSPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C4, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETCONTEXTINPROCESSPRIORITY \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C5, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_GETSHAREDRESOURCEADAPTERLUID \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C6, METHOD_BUFFERED, FILE_ANY_ACCESS)
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
#define IOCTL_D3DKMT_QUERYVIDPNEXCLUSIVEOWNERSHIP \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C7, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif

#define RXGK_WDDM_PACKET_VERSION_1            1U
#define RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA      (1024U * 1024U)
#define RXGK_CREATECONTEXTVIRTUAL_PACKET_V1_SIZE 44U
#define RXGK_SUBMITCOMMAND_PACKET_V1_SIZE       48U
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
#define RXGK_ISFEATUREENABLED_PACKET_SIZE       12U
#endif
#define RXGK_GETRESOURCEPRESENTPRIVATE_PACKET_V1_SIZE 24U
#define RXGK_INVALIDATECACHE_PACKET_V1_SIZE      32U
#define RXGK_RECLAIMALLOCATIONS2_PACKET_V1_SIZE  40U
#define RXGK_UPDATEALLOCATIONPROPERTY_PACKET_V1_SIZE 40U
#define RXGK_RECLAIMALLOCATIONS3_PACKET_V1_SIZE  40U
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
#define RXGK_GETDWMVERTICALBLANKEVENT_PACKET_V1_SIZE 32U
#define RXGK_SETSYNCREFRESHCOUNTWAITTARGET_PACKET_V1_SIZE 24U
#endif
#define RXGK_CONTEXTINPROCESSPRIORITY_PACKET_V1_SIZE 16U
#define RXGK_GETSHAREDRESOURCEADAPTERLUID_PACKET_V1_SIZE 32U
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
#define RXGK_QUERYVIDPNEXCLUSIVEOWNERSHIP_PACKET_V1_SIZE 48U
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
#define RXGK_ISFEATUREENABLED_RESULT_VALID_MASK 0x000FU
#endif

#define RXGK_RECLAIMALLOCATIONS2_FLAG_RESOURCE_LIST    0x00000001U
#define RXGK_RECLAIMALLOCATIONS2_FLAG_RETURN_DISCARDED 0x00000002U
#define RXGK_RECLAIMALLOCATIONS2_VALID_FLAGS           0x00000003U

#define RXGK_RECLAIMALLOCATIONS3_FLAG_RESOURCE_LIST    0x00000001U
#define RXGK_RECLAIMALLOCATIONS3_VALID_FLAGS           0x00000001U

#define RXGK_RECLAIM_RESULT_OK             0U
#define RXGK_RECLAIM_RESULT_DISCARDED      1U
#define RXGK_RECLAIM_RESULT_NOT_COMMITTED  2U

/* D3DDDI_CREATECONTEXTFLAGS bits represented by the WDDM 2.0 packet. */
#define RXGK_CREATECONTEXTVIRTUAL_FLAG_NULL_RENDERING        0x00000001U
#define RXGK_CREATECONTEXTVIRTUAL_FLAG_INITIAL_DATA          0x00000002U
#define RXGK_CREATECONTEXTVIRTUAL_FLAG_DISABLE_GPU_TIMEOUT   0x00000004U
#define RXGK_CREATECONTEXTVIRTUAL_FLAG_SYNCHRONIZATION_ONLY  0x00000008U

/* The first end-to-end path implements NullRendering contexts only. */
#define RXGK_CREATECONTEXTVIRTUAL_SUPPORTED_FLAGS            RXGK_CREATECONTEXTVIRTUAL_FLAG_NULL_RENDERING

/* D3DKMT_SUBMITCOMMANDFLAGS bits implemented by the first virtual path. */
#define RXGK_SUBMITCOMMAND_FLAG_NULL_RENDERING               0x00000001U
#define RXGK_SUBMITCOMMAND_FLAG_PRESENT_REDIRECTED           0x00000002U
#define RXGK_SUBMITCOMMAND_SUPPORTED_FLAGS                   0x00000001U

/*
 * PrivateDriverDataOffset is zero iff PrivateDriverDataSize is zero.
 * Otherwise it is sizeof(RXGK_CREATECONTEXTVIRTUAL_PACKET), and the packet
 * size is the structure size plus PrivateDriverDataSize.
 *
 * This structure contains only fixed-width scalar fields.  Its v1 wire size
 * is 44 bytes on x86, amd64, and ARM64.
 */
typedef struct _RXGK_CREATECONTEXTVIRTUAL_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONG       DeviceHandle;
    ULONG       NodeOrdinal;
    ULONG       EngineAffinity;
    ULONG       Flags;
    ULONG       ClientHint;
    ULONG       PrivateDriverDataSize;
    ULONG       PrivateDriverDataOffset;
    ULONG       ContextHandle;
    ULONG       Reserved;
} RXGK_CREATECONTEXTVIRTUAL_PACKET, *PRXGK_CREATECONTEXTVIRTUAL_PACKET;

/*
 * The v1 submit packet represents exactly one broadcast context.  Written
 * primaries, history buffers, and redirected presents are rejected before
 * marshalling and therefore cannot introduce nested pointers here.
 *
 * PrivateDriverDataOffset follows the same canonical rule as above.  The v1
 * wire size is 48 bytes on x86, amd64, and ARM64.
 */
typedef struct _RXGK_SUBMITCOMMAND_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONGLONG   Commands;
    ULONG       CommandLength;
    ULONG       Flags;
    ULONGLONG   PresentHistoryToken;
    ULONG       ContextHandle;
    ULONG       PrivateDriverDataSize;
    ULONG       PrivateDriverDataOffset;
    ULONG       Reserved;
} RXGK_SUBMITCOMMAND_PACKET, *PRXGK_SUBMITCOMMAND_PACKET;

/*
 * Pointer-free WDDM 2.0 resource-present private-data query.
 *
 * PrivateDriverDataSize is the caller's capacity on input and the resource's
 * required size on output.  PrivateDriverDataOffset is zero for a size query;
 * otherwise it is sizeof(RXGK_GETRESOURCEPRESENTPRIVATE_PACKET), and the
 * output bytes immediately follow this fixed-width header.
 */
typedef struct _RXGK_GETRESOURCEPRESENTPRIVATE_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONG       ResourceHandle;
    ULONG       PrivateDriverDataSize;
    ULONG       PrivateDriverDataOffset;
    ULONG       Reserved;
} RXGK_GETRESOURCEPRESENTPRIVATE_PACKET,
 *PRXGK_GETRESOURCEPRESENTPRIVATE_PACKET;

/*
 * Pointer-free WDDM 2.0 cache-maintenance request.
 *
 * Offset and Length are always 64-bit on the wire.  This keeps the bridge
 * layout identical across x86, amd64, and ARM64 and prevents a WOW64 caller's
 * SIZE_T layout from leaking into the kernel-to-kernel contract.
 */
typedef struct _RXGK_INVALIDATECACHE_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONG       DeviceHandle;
    ULONG       AllocationHandle;
    ULONGLONG   Offset;
    ULONGLONG   Length;
} RXGK_INVALIDATECACHE_PACKET, *PRXGK_INVALIDATECACHE_PACKET;

/*
 * Pointer-free WDDM 2.0 asynchronous reclaim request.
 *
 * HandlesOffset identifies NumAllocations ULONG handles immediately after
 * this header.  If RETURN_DISCARDED is set, DiscardedOffset identifies an
 * equally sized ULONG array immediately after the handles; otherwise it is
 * zero.  RESOURCE_LIST selects kernel resource handles rather than allocation
 * handles.  PagingFenceValue is the paging-queue fence produced by reclaim.
 */
typedef struct _RXGK_RECLAIMALLOCATIONS2_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONG       PagingQueueHandle;
    ULONG       NumAllocations;
    ULONG       Flags;
    ULONG       HandlesOffset;
    ULONG       DiscardedOffset;
    ULONG       Reserved;
    ULONGLONG   PagingFenceValue;
} RXGK_RECLAIMALLOCATIONS2_PACKET,
 *PRXGK_RECLAIMALLOCATIONS2_PACKET;

/*
 * Pointer-free WDDM 2.1 allocation-property update.
 *
 * The public D3DDDI_UPDATEALLOCPROPERTY layout contains a target-dependent
 * 64-bit alignment hole.  Carry every field as a fixed-width scalar so a
 * WOW64 caller and the native kernel use the same 40-byte wire contract.
 */
typedef struct _RXGK_UPDATEALLOCATIONPROPERTY_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONG       PagingQueueHandle;
    ULONG       AllocationHandle;
    ULONG       SupportedSegmentSet;
    ULONG       PreferredSegmentValue;
    ULONG       PropertyFlagsValue;
    ULONG       PropertyMaskValue;
    ULONGLONG   PagingFenceValue;
} RXGK_UPDATEALLOCATIONPROPERTY_PACKET,
 *PRXGK_UPDATEALLOCATIONPROPERTY_PACKET;

/*
 * Pointer-free WDDM 2.1 three-state reclaim request.
 *
 * ResultsOffset always identifies NumAllocations ULONG result values after
 * the inline handle array.  Keeping this separate from the WDDM 2.0 packet
 * preserves the v1 Boolean-discard contract byte for byte.
 */
typedef struct _RXGK_RECLAIMALLOCATIONS3_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONG       PagingQueueHandle;
    ULONG       NumAllocations;
    ULONG       Flags;
    ULONG       HandlesOffset;
    ULONG       ResultsOffset;
    ULONG       Reserved;
    ULONGLONG   PagingFenceValue;
} RXGK_RECLAIMALLOCATIONS3_PACKET,
 *PRXGK_RECLAIMALLOCATIONS3_PACKET;

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
/*
 * Pointer-free WDDM 1.2 DWM vertical-blank event request.
 *
 * EventHandle is an output handle opened in the requesting process.  It is a
 * fixed-width scalar on the wire; the win32k side range-checks it before
 * converting it to D3DKMT_PTR_TYPE and owns close-on-copyout-failure rollback.
 */
typedef struct _RXGK_GETDWMVERTICALBLANKEVENT_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONG       AdapterHandle;
    ULONG       DeviceHandle;
    ULONG       VidPnSourceId;
    ULONG       Reserved;
    ULONGLONG   EventHandle;
} RXGK_GETDWMVERTICALBLANKEVENT_PACKET,
 *PRXGK_GETDWMVERTICALBLANKEVENT_PACKET;

/*
 * Pointer-free WDDM 1.2 DWM refresh-target request.
 */
typedef struct _RXGK_SETSYNCREFRESHCOUNTWAITTARGET_PACKET
{
    ULONG Size;
    ULONG Version;
    ULONG AdapterHandle;
    ULONG DeviceHandle;
    ULONG VidPnSourceId;
    ULONG TargetSyncRefreshCount;
} RXGK_SETSYNCREFRESHCOUNTWAITTARGET_PACKET,
 *PRXGK_SETSYNCREFRESHCOUNTWAITTARGET_PACKET;
#endif

/*
 * Pointer-free WDDM 1.3 context-in-process scheduling-priority request.
 *
 * Priority is zero on a get request and receives the current value on output.
 * A set request accepts only the native relative classes 0 and 1.  This wire
 * contract deliberately stays separate from ordinary context priority, whose
 * public range and scheduling semantics are different.
 */
typedef struct _RXGK_CONTEXTINPROCESSPRIORITY_PACKET
{
    ULONG Size;
    ULONG Version;
    ULONG ContextHandle;
    LONG Priority;
} RXGK_CONTEXTINPROCESSPRIORITY_PACKET,
 *PRXGK_CONTEXTINPROCESSPRIORITY_PACKET;

/*
 * Pointer-free WDDM 1.2 shared-resource adapter query.
 *
 * NtHandle is always 64-bit on the wire even though HANDLE follows the native
 * architecture in the public structure.  The current resource manager
 * implements legacy global-share handles only; preserving this field in the
 * versioned packet lets a future NT object-backed sharing implementation add
 * that path without changing the win32k/dxgkrnl ABI.
 */
typedef struct _RXGK_GETSHAREDRESOURCEADAPTERLUID_PACKET
{
    ULONG Size;
    ULONG Version;
    ULONG GlobalShareHandle;
    ULONG Reserved;
    ULONGLONG NtHandle;
    ULONG AdapterLuidLowPart;
    LONG AdapterLuidHighPart;
} RXGK_GETSHAREDRESOURCEADAPTERLUID_PACKET,
 *PRXGK_GETSHAREDRESOURCEADAPTERLUID_PACKET;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
/*
 * Pointer-free WDDM 2.0 VidPn exclusive-ownership query.
 *
 * Win32k validates and references the caller's process handle, maps the
 * window centre to a GDI display source, and holds the process reference
 * across the synchronous IOCTL.  The wire therefore carries only the stable
 * process ID and display identity; dxgkrnl resolves the process again while
 * the win32k reference prevents PID reuse.
 *
 * A successful query with no matching exclusive owner returns
 * ResultVidPnSourceId == D3DDDI_ID_UNINITIALIZED, a zero LUID, and
 * D3DKMT_VIDPNSOURCEOWNER_UNOWNED.
 */
typedef struct _RXGK_QUERYVIDPNEXCLUSIVEOWNERSHIP_PACKET
{
    ULONG       Size;
    ULONG       Version;
    ULONGLONG   ProcessId;
    ULONG       QueryVidPnSourceId;
    ULONG       QueryAdapterLuidLowPart;
    LONG        QueryAdapterLuidHighPart;
    ULONG       Reserved;
    ULONG       ResultVidPnSourceId;
    ULONG       ResultAdapterLuidLowPart;
    LONG        ResultAdapterLuidHighPart;
    ULONG       OwnerType;
} RXGK_QUERYVIDPNEXCLUSIVEOWNERSHIP_PACKET,
 *PRXGK_QUERYVIDPNEXCLUSIVEOWNERSHIP_PACKET;
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 3200)
/*
 * Fixed WDDM 3.2 feature-query packet.  The public KMT structure has the same
 * width, but this private form makes the kernel-to-kernel boundary explicit
 * and prevents a future public-header change from silently adding a pointer.
 */
typedef struct _RXGK_ISFEATUREENABLED_PACKET
{
    ULONG  AdapterHandle;
    ULONG  FeatureId;
    USHORT ResultVersion;
    USHORT ResultValue;
} RXGK_ISFEATUREENABLED_PACKET, *PRXGK_ISFEATUREENABLED_PACKET;
#endif
