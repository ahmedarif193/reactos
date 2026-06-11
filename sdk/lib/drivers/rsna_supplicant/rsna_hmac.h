/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HMAC-SHA1 and HMAC-MD5 (RFC 2104) - cleanroom implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _RSNA_HMAC_H_
#define _RSNA_HMAC_H_

#include "rsna_types.h"
#include "rsna_sha1.h"
#include "rsna_md5.h"

/*
 * HMAC-SHA1 (RFC 2104).  Output is RSNA_SHA1_DIGEST_LEN (20) bytes.
 * The Vector variant takes the message as non-contiguous chunks.
 */
void RsnaHmacSha1(const rsna_u8 *key, rsna_size keyLen,
                  const rsna_u8 *data, rsna_size dataLen,
                  rsna_u8 mac[RSNA_SHA1_DIGEST_LEN]);

void RsnaHmacSha1Vector(const rsna_u8 *key, rsna_size keyLen,
                        rsna_size numElem,
                        const rsna_u8 *const *addr, const rsna_size *len,
                        rsna_u8 mac[RSNA_SHA1_DIGEST_LEN]);

/* HMAC-MD5 (RFC 2104).  Output is RSNA_MD5_DIGEST_LEN (16) bytes. */
void RsnaHmacMd5(const rsna_u8 *key, rsna_size keyLen,
                 const rsna_u8 *data, rsna_size dataLen,
                 rsna_u8 mac[RSNA_MD5_DIGEST_LEN]);

#endif /* _RSNA_HMAC_H_ */
