/*
 * ipsec.h
 *
 * IPsec WFP kernel types. Minimal set needed by callout drivers that want
 * to inspect or modify IPsec-protected traffic.
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
#define _IPSEC_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum IPSEC_TRAFFIC_TYPE_ {
  IPSEC_TRAFFIC_TYPE_TRANSPORT = 0,
  IPSEC_TRAFFIC_TYPE_TUNNEL,
  IPSEC_TRAFFIC_TYPE_MAX
} IPSEC_TRAFFIC_TYPE;

typedef enum IPSEC_CIPHER_TYPE_ {
  IPSEC_CIPHER_TYPE_DES = 1,
  IPSEC_CIPHER_TYPE_3DES,
  IPSEC_CIPHER_TYPE_AES_128,
  IPSEC_CIPHER_TYPE_AES_192,
  IPSEC_CIPHER_TYPE_AES_256,
  IPSEC_CIPHER_TYPE_MAX
} IPSEC_CIPHER_TYPE;

typedef enum IPSEC_AUTH_TYPE_ {
  IPSEC_AUTH_TYPE_MD5 = 1,
  IPSEC_AUTH_TYPE_SHA_1,
  IPSEC_AUTH_TYPE_SHA_256,
  IPSEC_AUTH_TYPE_AES_128,
  IPSEC_AUTH_TYPE_AES_192,
  IPSEC_AUTH_TYPE_AES_256,
  IPSEC_AUTH_TYPE_MAX
} IPSEC_AUTH_TYPE;

typedef enum IPSEC_TRANSFORM_TYPE_ {
  IPSEC_TRANSFORM_AH = 1,
  IPSEC_TRANSFORM_ESP_AUTH,
  IPSEC_TRANSFORM_ESP_CIPHER,
  IPSEC_TRANSFORM_ESP_AUTH_AND_CIPHER,
  IPSEC_TRANSFORM_ESP_AUTH_FW,
  IPSEC_TRANSFORM_TYPE_MAX
} IPSEC_TRANSFORM_TYPE;

typedef enum IPSEC_POLICY_FLAG_ {
  IPSEC_POLICY_FLAG_NONE                      = 0x00000000,
  IPSEC_POLICY_FLAG_NAT_ENCAP_ALLOW_PEER_BEHIND_NAT = 0x00000001,
  IPSEC_POLICY_FLAG_DONT_NEGOTIATE_SECOND_LIFETIME   = 0x00000002,
  IPSEC_POLICY_FLAG_DONT_NEGOTIATE_BYTE_LIFETIME     = 0x00000004,
  IPSEC_POLICY_FLAG_MAX                              = 0x00000008
} IPSEC_POLICY_FLAG;

#ifdef __cplusplus
}
#endif

/* EOF */
