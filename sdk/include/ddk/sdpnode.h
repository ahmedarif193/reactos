/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth SDP node definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __SDPNODE_H__
#define __SDPNODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define __SDPNODE_H__

#if ((NTDDI_VERSION >= NTDDI_VISTA))

                             typedef UCHAR SDP_BOOLEAN;

typedef struct ISdpNodeContainer ISdpNodeContainer;

typedef struct _SDP_NODE_HEADER {
    LIST_ENTRY Link;
    USHORT Type;
    USHORT SpecificType;
} SDP_NODE_HEADER, *PSDP_NODE_HEADER;

typedef union _SDP_NODE_DATA {
    SDP_LARGE_INTEGER_16 int128;
    SDP_ULARGE_INTEGER_16 uint128;
    GUID uuid128;
    ULONG uuid32;
    USHORT uuid16;
    LONGLONG int64;
    ULONGLONG uint64;
    LONG int32;
    ULONG uint32;
    SHORT int16;
    USHORT uint16;
    CHAR int8;
    UCHAR uint8;
    SDP_BOOLEAN boolean;
    PCHAR string;
    PCHAR url;
	SDP_NODE_HEADER sequence;
    SDP_NODE_HEADER alternative;
    ISdpNodeContainer *container;
    struct {
        PUCHAR stream;
        ULONG streamLength;
    };
} SDP_NODE_DATA, *PSDP_NODE_DATA;

typedef struct _SDP_NODE {
	SDP_NODE_HEADER  hdr;
    ULONG DataSize;
	SDP_NODE_DATA u;
	PVOID Reserved;
} SDP_NODE, *PSDP_NODE;

typedef struct _SDP_TREE_ROOT_NODE {
    SDP_NODE RootNode;
} SDP_TREE_ROOT_NODE, *PSDP_TREE_ROOT_NODE;

#endif

#ifdef __cplusplus
}
#endif

#endif
