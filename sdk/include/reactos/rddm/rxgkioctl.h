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

#define RXGK_WDDM_PACKET_VERSION_1            1U
#define RXGK_WDDM_MAX_PRIVATE_DRIVER_DATA      (1024U * 1024U)
#define RXGK_CREATECONTEXTVIRTUAL_PACKET_V1_SIZE 44U
#define RXGK_SUBMITCOMMAND_PACKET_V1_SIZE       48U

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
