/*
 * fwpmtypes.h
 *
 * Windows Filtering Platform — management types shared by the kernel-side
 * management API (fwpmk.h) and, eventually, the user-mode fwpuclnt.h.
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
#define _FWPMTYPES_

#include <fwpstypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FWPM_DISPLAY_DATA0_ {
  LPWSTR name;
  LPWSTR description;
} FWPM_DISPLAY_DATA0;

typedef enum FWPM_PROVIDER_CONTEXT_TYPE_ {
  FWPM_IPSEC_KEYING_CONTEXT = 0,
  FWPM_IPSEC_IKE_QM_TRANSPORT_CONTEXT,
  FWPM_IPSEC_IKE_QM_TUNNEL_CONTEXT,
  FWPM_IPSEC_AUTHIP_QM_TRANSPORT_CONTEXT,
  FWPM_IPSEC_AUTHIP_QM_TUNNEL_CONTEXT,
  FWPM_IPSEC_IKE_MM_CONTEXT,
  FWPM_IPSEC_AUTHIP_MM_CONTEXT,
  FWPM_CLASSIFY_OPTIONS_CONTEXT,
  FWPM_GENERAL_CONTEXT,
  FWPM_IPSEC_IKEV2_QM_TUNNEL_CONTEXT,
  FWPM_IPSEC_IKEV2_MM_CONTEXT,
  FWPM_IPSEC_DOSP_CONTEXT,
  FWPM_PROVIDER_CONTEXT_TYPE_MAX
} FWPM_PROVIDER_CONTEXT_TYPE;

typedef struct FWPM_PROVIDER0_ {
  GUID               providerKey;
  FWPM_DISPLAY_DATA0 displayData;
  UINT32             flags;
  FWP_BYTE_BLOB      providerData;
  LPWSTR             serviceName;
} FWPM_PROVIDER0;

typedef struct FWPM_SUBLAYER0_ {
  GUID               subLayerKey;
  FWPM_DISPLAY_DATA0 displayData;
  UINT32             flags;
  GUID*              providerKey;
  FWP_BYTE_BLOB      providerData;
  UINT16             weight;
} FWPM_SUBLAYER0;

typedef struct FWPM_CALLOUT0_ {
  GUID               calloutKey;
  FWPM_DISPLAY_DATA0 displayData;
  UINT32             flags;
  GUID*              providerKey;
  FWP_BYTE_BLOB      providerData;
  GUID               applicableLayer;
  UINT32             calloutId;
} FWPM_CALLOUT0;

typedef struct FWPM_FILTER0_ {
  GUID                    filterKey;
  FWPM_DISPLAY_DATA0      displayData;
  UINT32                  flags;
  GUID*                   providerKey;
  FWP_BYTE_BLOB           providerData;
  GUID                    layerKey;
  GUID                    subLayerKey;
  FWP_VALUE0              weight;
  UINT32                  numFilterConditions;
  FWPM_FILTER_CONDITION0* filterCondition;
  FWPS_ACTION0            action;
  union {
    UINT64                rawContext;
    GUID                  providerContextKey;
  };
  GUID*                   reserved;
  UINT64                  filterId;
  FWP_VALUE0              effectiveWeight;
} FWPM_FILTER0;

/* Kernel-side transaction-management flags */
#define FWPM_TXN_READ_ONLY                      0x00000001
#define FWPM_TXN_READ_WRITE                     0x00000002

#ifdef __cplusplus
}
#endif

/* EOF */
