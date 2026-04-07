/*
 * fwpvi.h
 *
 * Windows Filtering Platform — version identifiers and well-known GUIDs
 * for built-in providers, sublayers, and layers.
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
#define _FWPVI_

#ifdef __cplusplus
extern "C" {
#endif

/* Base WFP version constants */
#define FWP_V1          0x01
#define FWPM_SESSION_FLAG_DYNAMIC           0x00000001
#define FWPM_SESSION_FLAG_RESERVED          0x10000000

/* Built-in sublayer weights — higher value means higher priority */
#define FWPM_SUBLAYER_WEIGHT_UNIVERSAL      0xFFFF
#define FWPM_SUBLAYER_WEIGHT_IPSEC          0xFFFE
#define FWPM_SUBLAYER_WEIGHT_TCPIP_AUTOTUNE 0xFFF0

/*
 * Well-known GUIDs for Microsoft-reserved providers, sublayers, and
 * layers. Fill in as callers actually need them — declared as extern so
 * the BFE stub service can define the corresponding storage.
 */
extern const GUID FWPM_PROVIDER_MICROSOFT;
extern const GUID FWPM_PROVIDER_IKEEXT;
extern const GUID FWPM_PROVIDER_IPSEC_DOSP_CONFIG;
extern const GUID FWPM_SUBLAYER_UNIVERSAL;
extern const GUID FWPM_SUBLAYER_IPSEC_TUNNEL;
extern const GUID FWPM_SUBLAYER_IPSEC_FORWARD_OUTBOUND_TUNNEL;

#ifdef __cplusplus
}
#endif

/* EOF */
