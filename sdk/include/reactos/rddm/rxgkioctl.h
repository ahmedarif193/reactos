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
 * These function numbers belong to the ReactOS-private WDDM 2.0 bridge.
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
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
#define IOCTL_D3DKMT_GETDWMVERTICALBLANKEVENT \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_D3DKMT_SETSYNCREFRESHCOUNTWAITTARGET \
    CTL_CODE(DXGKRNL_DEVICE_TYPE, 0x1C3, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
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
#define RXGK_ISFEATUREENABLED_RESULT_VALID_MASK 0x000FU
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
#define RXGK_GETDWMVERTICALBLANKEVENT_PACKET_V1_SIZE 32U
#define RXGK_SETSYNCREFRESHCOUNTWAITTARGET_PACKET_V1_SIZE 24U
#endif
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
#define RXGK_QUERYVIDPNEXCLUSIVEOWNERSHIP_PACKET_V1_SIZE 48U
#endif

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

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
/*
 * Pointer-free WDDM 1.2 DWM vertical-blank event request.
 *
 * EventHandle is an output handle opened in the requesting process. It is a
 * fixed-width scalar on the wire; win32k range-checks it before converting it
 * to D3DKMT_PTR_TYPE and closes it if copying the value to user mode fails.
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

/* Pointer-free WDDM 1.2 DWM refresh-target request. */
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
 * Fixed WDDM 3.2 feature-query packet. The public KMT structure has the same
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
