/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     EAPOL-Key wire-format definitions (IEEE 802.1X, IEEE 802.11i).
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * All multi-byte fields are big-endian.  Frames are parsed by byte offset
 * (no packed structs), hence the offset constants below.
 */

#ifndef _RSNA_EAPOL_H_
#define _RSNA_EAPOL_H_

#include "rsna_types.h"

/* ---- IEEE 802.1X EAPOL header ------------------------------------- */

#define EAPOL_VERSION_2001      1
#define EAPOL_VERSION_2004      2
#define EAPOL_VERSION_2010      3

/* EAPOL Packet Type. */
#define EAPOL_TYPE_EAP_PACKET   0
#define EAPOL_TYPE_START        1
#define EAPOL_TYPE_LOGOFF       2
#define EAPOL_TYPE_KEY          3   /* EAPOL-Key (what the 4-way handshake uses) */
#define EAPOL_TYPE_ENCAP_ASF    4

/* Offsets within the EAPOL header (4 bytes). */
#define EAPOL_OFF_VERSION       0
#define EAPOL_OFF_TYPE          1
#define EAPOL_OFF_LENGTH        2   /* u16, length of the body that follows */
#define EAPOL_HDR_LEN           4

/* ---- EAPOL-Key descriptor (body) ---------------------------------- */

/* Descriptor Type. */
#define EAPOL_KEY_DESC_RC4      1   /* legacy */
#define EAPOL_KEY_DESC_RSN      2   /* IEEE 802.11i RSN (WPA2)              */
#define EAPOL_KEY_DESC_WPA      254 /* WPA (TKIP, vendor descriptor)        */

/*
 * Key Information bit field (16-bit, big-endian).
 * Bits 0-2: Key Descriptor Version
 *   1 = HMAC-MD5 MIC + RC4 key wrap (TKIP)
 *   2 = HMAC-SHA1-128 MIC + AES key wrap (CCMP)
 *   3 = AES-128-CMAC MIC + AES key wrap (CCMP, newer)
 */
#define KEYINFO_VERSION_MASK    0x0007
#define KEYINFO_VER_HMAC_MD5_RC4    1
#define KEYINFO_VER_HMAC_SHA1_AES   2
#define KEYINFO_VER_AES_CMAC_AES    3

#define KEYINFO_KEY_TYPE        0x0008  /* 1 = Pairwise, 0 = Group           */
#define KEYINFO_INSTALL         0x0040  /* Install                            */
#define KEYINFO_KEY_ACK         0x0080  /* Key Ack (AP -> STA expects reply)  */
#define KEYINFO_KEY_MIC         0x0100  /* Key MIC present                    */
#define KEYINFO_SECURE          0x0200  /* Secure                             */
#define KEYINFO_ERROR           0x0400  /* Error (MIC failure report)         */
#define KEYINFO_REQUEST         0x0800  /* Request                            */
#define KEYINFO_ENCRYPTED_DATA  0x1000  /* Encrypted Key Data                 */
#define KEYINFO_SMK_MESSAGE     0x2000  /* SMK Message                        */

/*
 * Field offsets inside the EAPOL-Key body (relative to start of body, i.e.
 * EAPOL_HDR_LEN into the frame).  Layout per IEEE 802.11i figure 43h.
 *
 *   off  size  field
 *   0    1     Descriptor Type
 *   1    2     Key Information
 *   3    2     Key Length
 *   5    8     Key Replay Counter
 *   13   32    Key Nonce
 *   45   16    EAPOL Key IV
 *   61   8     Key RSC
 *   69   8     Key ID (reserved)
 *   77   16    Key MIC          (size depends on MIC algorithm; 16 for the
 *                                MD5/SHA1-128 variants used here)
 *   93   2     Key Data Length
 *   95   ...   Key Data
 */
#define KEYDESC_OFF_TYPE            0
#define KEYDESC_OFF_KEYINFO         1
#define KEYDESC_OFF_KEYLEN          3
#define KEYDESC_OFF_REPLAY          5
#define KEYDESC_OFF_NONCE           13
#define KEYDESC_OFF_IV              45
#define KEYDESC_OFF_RSC             61
#define KEYDESC_OFF_KEYID           69
#define KEYDESC_OFF_MIC             77
#define KEYDESC_MIC_LEN             16
#define KEYDESC_OFF_DATALEN         (KEYDESC_OFF_MIC + KEYDESC_MIC_LEN)  /* 93 */
#define KEYDESC_OFF_DATA            (KEYDESC_OFF_DATALEN + 2)            /* 95 */

#define KEYDESC_FIXED_LEN          KEYDESC_OFF_DATA   /* body bytes before Key Data */

#define KEYDESC_NONCE_LEN          32
#define KEYDESC_IV_LEN             16
#define KEYDESC_RSC_LEN            8
#define KEYDESC_REPLAY_LEN         8

/* Full frame length helpers. */
#define EAPOL_KEY_FRAME_FIXED_LEN  (EAPOL_HDR_LEN + KEYDESC_FIXED_LEN)  /* 99 */

/* ---- RSN/WPA Key Data Encapsulation (KDE) ------------------------- */

/*
 * Generic KDE (IEEE 802.11i 8.5.2): an IE with element id 0xDD, length, then
 * an OUI (3 bytes) + data type (1 byte) + data.
 *
 *   id(0xDD) len OUI[3] type data...
 */
#define KDE_IE_VENDOR           0xDD

/* IEEE 802.11 OUI (00-0F-AC) used for RSN KDEs. */
#define KDE_OUI_0               0x00
#define KDE_OUI_1               0x0F
#define KDE_OUI_2               0xAC

/* KDE data types under the 00-0F-AC OUI. */
#define KDE_TYPE_GTK            1   /* GTK KDE (contains the Group Temporal Key) */
#define KDE_TYPE_MAC            3
#define KDE_TYPE_PMKID          4
#define KDE_TYPE_IGTK           9

/*
 * GTK KDE body (after OUI+type):
 *   off 0: 1 byte  - KeyID (bits 0-1) + Tx (bit 2)
 *   off 1: 1 byte  - reserved
 *   off 2: N bytes - GTK
 */
#define GTK_KDE_OFF_KEYID       0
#define GTK_KDE_OFF_GTK         2
#define GTK_KDE_HDR_LEN         2

#endif /* _RSNA_EAPOL_H_ */
