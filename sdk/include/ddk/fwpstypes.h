/*
 * fwpstypes.h
 *
 * Windows Filtering Platform — shared kernel-mode types used by both the
 * callout driver API (fwpsk.h) and the kernel management API (fwpmk.h).
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Created for the dev-nt6-1 branch as part of the NT 5.2 -> NT 6.1 upgrade.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#pragma once
#define _FWPSTYPES_

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Basic value representation                                          */
/* ------------------------------------------------------------------ */

typedef enum FWP_DATA_TYPE_ {
  FWP_EMPTY = 0,
  FWP_UINT8,
  FWP_UINT16,
  FWP_UINT32,
  FWP_UINT64,
  FWP_INT8,
  FWP_INT16,
  FWP_INT32,
  FWP_INT64,
  FWP_FLOAT,
  FWP_DOUBLE,
  FWP_BYTE_ARRAY16_TYPE,
  FWP_BYTE_BLOB_TYPE,
  FWP_SID,
  FWP_SECURITY_DESCRIPTOR_TYPE,
  FWP_TOKEN_INFORMATION_TYPE,
  FWP_TOKEN_ACCESS_INFORMATION_TYPE,
  FWP_UNICODE_STRING_TYPE,
  FWP_BYTE_ARRAY6_TYPE,
  FWP_V4_ADDR_MASK = 256,
  FWP_V6_ADDR_MASK,
  FWP_RANGE_TYPE,
  FWP_DATA_TYPE_MAX
} FWP_DATA_TYPE;

typedef struct FWP_BYTE_ARRAY16_ {
  UINT8 byteArray16[16];
} FWP_BYTE_ARRAY16;

typedef struct FWP_BYTE_ARRAY6_ {
  UINT8 byteArray6[6];
} FWP_BYTE_ARRAY6;

typedef struct FWP_BYTE_BLOB_ {
  UINT32 size;
  UINT8* data;
} FWP_BYTE_BLOB;

typedef struct FWP_V4_ADDR_AND_MASK_ {
  UINT32 addr;
  UINT32 mask;
} FWP_V4_ADDR_AND_MASK;

typedef struct FWP_V6_ADDR_AND_MASK_ {
  UINT8  addr[16];
  UINT8  prefixLength;
} FWP_V6_ADDR_AND_MASK;

typedef struct FWP_VALUE0_ {
  FWP_DATA_TYPE type;
  union {
    UINT8                   uint8;
    UINT16                  uint16;
    UINT32                  uint32;
    UINT64*                 uint64;
    INT8                    int8;
    INT16                   int16;
    INT32                   int32;
    INT64*                  int64;
    float                   float32;
    double*                 double64;
    FWP_BYTE_ARRAY16*       byteArray16;
    FWP_BYTE_BLOB*          byteBlob;
    PVOID                   sid;
    FWP_BYTE_BLOB*          sd;
    FWP_BYTE_BLOB*          tokenInformation;
    FWP_BYTE_BLOB*          tokenAccessInformation;
    LPWSTR                  unicodeString;
    FWP_BYTE_ARRAY6*        byteArray6;
  };
} FWP_VALUE0;

typedef struct FWP_RANGE0_ {
  FWP_VALUE0 valueLow;
  FWP_VALUE0 valueHigh;
} FWP_RANGE0;

typedef enum FWP_MATCH_TYPE_ {
  FWP_MATCH_EQUAL = 0,
  FWP_MATCH_GREATER,
  FWP_MATCH_LESS,
  FWP_MATCH_GREATER_OR_EQUAL,
  FWP_MATCH_LESS_OR_EQUAL,
  FWP_MATCH_RANGE,
  FWP_MATCH_FLAGS_ALL_SET,
  FWP_MATCH_FLAGS_ANY_SET,
  FWP_MATCH_FLAGS_NONE_SET,
  FWP_MATCH_EQUAL_CASE_INSENSITIVE,
  FWP_MATCH_NOT_EQUAL,
  FWP_MATCH_TYPE_MAX
} FWP_MATCH_TYPE;

typedef struct FWP_CONDITION_VALUE0_ {
  FWP_DATA_TYPE type;
  union {
    UINT8                 uint8;
    UINT16                uint16;
    UINT32                uint32;
    UINT64*               uint64;
    INT8                  int8;
    INT16                 int16;
    INT32                 int32;
    INT64*                int64;
    float                 float32;
    double*               double64;
    FWP_BYTE_ARRAY16*     byteArray16;
    FWP_BYTE_BLOB*        byteBlob;
    PVOID                 sid;
    FWP_BYTE_BLOB*        sd;
    FWP_BYTE_BLOB*        tokenInformation;
    FWP_BYTE_BLOB*        tokenAccessInformation;
    LPWSTR                unicodeString;
    FWP_BYTE_ARRAY6*      byteArray6;
    FWP_V4_ADDR_AND_MASK* v4AddrMask;
    FWP_V6_ADDR_AND_MASK* v6AddrMask;
    FWP_RANGE0*           rangeValue;
  };
} FWP_CONDITION_VALUE0;

typedef struct FWPM_FILTER_CONDITION0_ {
  GUID                 fieldKey;
  FWP_MATCH_TYPE       matchType;
  FWP_CONDITION_VALUE0 conditionValue;
} FWPM_FILTER_CONDITION0;

/* ------------------------------------------------------------------ */
/* Classify action / verdict                                           */
/* ------------------------------------------------------------------ */

typedef enum FWP_ACTION_TYPE_ {
  FWP_ACTION_BLOCK                = 0x00001001,
  FWP_ACTION_PERMIT               = 0x00001002,
  FWP_ACTION_CALLOUT_TERMINATING  = 0x00005003,
  FWP_ACTION_CALLOUT_INSPECTION   = 0x00006004,
  FWP_ACTION_CALLOUT_UNKNOWN      = 0x00004005,
  FWP_ACTION_CONTINUE             = 0x00004006,
  FWP_ACTION_NONE                 = 0x00004007,
  FWP_ACTION_NONE_NO_MATCH        = 0x00004008
} FWP_ACTION_TYPE;

#define FWP_ACTION_FLAG_TERMINATING 0x00001000
#define FWP_ACTION_FLAG_NON_TERMINATING 0x00002000
#define FWP_ACTION_FLAG_CALLOUT 0x00004000

typedef struct FWPS_ACTION0_ {
  FWP_ACTION_TYPE type;
  GUID            calloutKey;
} FWPS_ACTION0;

/* classifyOut flags (written back into FWPS_CLASSIFY_OUT0) */
#define FWPS_CLASSIFY_OUT_FLAG_ABSORB            0x00000001
#define FWPS_CLASSIFY_OUT_FLAG_CONTINUE          0x00000002
#define FWPS_CLASSIFY_OUT_FLAG_NO_MORE_DATA      0x00000004
#define FWPS_CLASSIFY_OUT_FLAG_RESERVED          0x00000008

typedef struct FWPS_CLASSIFY_OUT0_ {
  UINT32  actionType;   /* a FWP_ACTION_TYPE value, written by the callout */
  UINT64  outContext;
  UINT64  filterId;
  UINT32  rights;
  UINT32  flags;
  UINT32  reserved;
} FWPS_CLASSIFY_OUT0;

typedef struct FWPS_INCOMING_VALUES0_ {
  UINT16      layerId;
  UINT32      valueCount;
  FWP_VALUE0* incomingValue;
} FWPS_INCOMING_VALUES0;

/*
 * FWPS_INCOMING_METADATA_VALUES0 — stub layout.
 *
 * The real Microsoft DDK definition has ~30 fields covering interface
 * indices, flow handles, token blobs, transport endpoints, etc. Most of
 * them are opaque to a callout driver that only wants to inspect/permit
 * packets; what matters is that the *type exists* so drivers can write
 * `const FWPS_INCOMING_METADATA_VALUES0* inMetaValues` in their classify
 * callback signature and access the flags field to check which metadata
 * are present.
 *
 * Fill in the full Microsoft layout when a caller actually needs a
 * specific field — track that work in the NETSTACK.md support matrix.
 */
typedef struct FWPS_INCOMING_METADATA_VALUES0_ {
  UINT32 currentMetadataValues;
  UINT32 flags;
  UINT64 reserved;
  FWP_BYTE_BLOB* discardModule;
  UINT32 discardReason;
  UINT32 flowHandle;
  UINT32 ipHeaderSize;
  UINT32 transportHeaderSize;
  PVOID  processPath;
  UINT64 token;
  UINT64 processId;
  UINT32 sourceInterfaceIndex;
  UINT32 destinationInterfaceIndex;
  UINT32 compartmentId;
  FWP_BYTE_BLOB* fragmentData;
  UINT32 fragmentIdentification;
  UINT16 fragmentOffset;
  UINT32 fragmentLength;
  UINT32 pathMtu;
  UINT64 completionHandle;
  UINT32 transportEndpointHandle;
  UINT32 sourceControlPortEndpoint;
  UINT32 destinationControlPortEndpoint;
  FWP_BYTE_BLOB* tokenForUserMode;
  FWP_BYTE_BLOB* tokenForAccessCheck;
  PVOID  sid;
  FWP_BYTE_BLOB* reauthReason;
  PVOID  originalProfileId;
  PVOID  currentProfileId;
  UINT32 reauthLastAuthorizingFilterId;
} FWPS_INCOMING_METADATA_VALUES0;

/* Common layer IDs (Vista+) — small subset, enough for stubs */
#define FWPS_LAYER_INBOUND_IPPACKET_V4              0
#define FWPS_LAYER_INBOUND_IPPACKET_V4_DISCARD      1
#define FWPS_LAYER_INBOUND_IPPACKET_V6              2
#define FWPS_LAYER_INBOUND_IPPACKET_V6_DISCARD      3
#define FWPS_LAYER_OUTBOUND_IPPACKET_V4             4
#define FWPS_LAYER_OUTBOUND_IPPACKET_V4_DISCARD     5
#define FWPS_LAYER_OUTBOUND_IPPACKET_V6             6
#define FWPS_LAYER_OUTBOUND_IPPACKET_V6_DISCARD     7
#define FWPS_LAYER_ALE_AUTH_CONNECT_V4              48
#define FWPS_LAYER_ALE_AUTH_CONNECT_V6              49
#define FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V4          44
#define FWPS_LAYER_ALE_AUTH_RECV_ACCEPT_V6          45

#ifdef __cplusplus
}
#endif

/* EOF */
