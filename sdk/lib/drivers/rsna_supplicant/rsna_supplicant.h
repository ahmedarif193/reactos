/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Public API of the self-contained WPA2-PSK (RSNA) supplicant.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * WPA2-Personal supplicant crypto and the IEEE 802.11i 4-Way Handshake state
 * machine.  No NDIS/driver/socket dependency: EAPOL-Key frames are passed in,
 * reply frames are produced out; the caller transmits them and installs the
 * derived keys once the handshake completes.
 */

#ifndef _RSNA_SUPPLICANT_H_
#define _RSNA_SUPPLICANT_H_

#include "rsna_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RSNA_MAC_LEN            6
#define RSNA_SSID_MAX_LEN       32
#define RSNA_PASSPHRASE_MAX     63
#define RSNA_PMK_LEN            32          /* 256-bit PMK                      */
#define RSNA_NONCE_LEN         32

/*
 * PTK split (CCMP / PRF-384 = 48 bytes):
 *   KCK : bytes  0..15  (Key Confirmation Key, used for the EAPOL MIC)
 *   KEK : bytes 16..31  (Key Encryption Key, unwraps the GTK KDE)
 *   TK  : bytes 32..47  (Temporal Key, CCMP pairwise data encryption)
 * For TKIP (PRF-512 = 64 bytes) the TK is 32 bytes (TK 16 + Rx/Tx MIC 8+8).
 */
#define RSNA_KCK_LEN          16
#define RSNA_KEK_LEN          16
#define RSNA_TK_CCMP_LEN      16
#define RSNA_TK_TKIP_LEN      32
#define RSNA_PTK_CCMP_LEN     (RSNA_KCK_LEN + RSNA_KEK_LEN + RSNA_TK_CCMP_LEN)  /* 48 */
#define RSNA_PTK_TKIP_LEN     (RSNA_KCK_LEN + RSNA_KEK_LEN + RSNA_TK_TKIP_LEN)  /* 64 */
#define RSNA_PTK_MAX_LEN      RSNA_PTK_TKIP_LEN
#define RSNA_GTK_MAX_LEN      32

/* Cipher suite the handshake is running for. */
typedef enum _RSNA_CIPHER
{
    RSNA_CIPHER_CCMP = 0,   /* AES-CCMP : PRF-384, HMAC-SHA1 MIC, AES keywrap   */
    RSNA_CIPHER_TKIP = 1    /* TKIP     : PRF-512, HMAC-MD5  MIC, RC4 keywrap   */
} RSNA_CIPHER;

/* State machine states returned by RsnaRxEapol(). */
typedef enum _RSNA_STATE
{
    RSNA_STATE_IDLE = 0,        /* Initialised, PMK ready, no msg1 seen yet     */
    RSNA_STATE_PTK_START,       /* msg1 received, SNonce chosen, msg2 emitted   */
    RSNA_STATE_PTK_NEGOTIATING, /* msg2 sent, waiting for msg3                  */
    RSNA_STATE_COMPLETED,       /* msg3 verified, keys derived, msg4 emitted    */
    RSNA_STATE_FAILED           /* MIC mismatch / protocol error / bad input    */
} RSNA_STATE;

/* Result of RsnaRxEapol()/other calls. */
typedef enum _RSNA_STATUS
{
    RSNA_OK = 0,
    RSNA_ERR_PARAM,             /* bad argument                                 */
    RSNA_ERR_TOO_SMALL,         /* output buffer too small                      */
    RSNA_ERR_MALFORMED,         /* frame not a valid EAPOL-Key                  */
    RSNA_ERR_REPLAY,            /* replay counter not greater than last seen    */
    RSNA_ERR_MIC,               /* Key MIC verification failed                  */
    RSNA_ERR_UNWRAP,            /* GTK key-data unwrap / integrity failed       */
    RSNA_ERR_STATE,             /* message unexpected in the current state      */
    RSNA_ERR_NO_GTK             /* msg3 had no GTK KDE                          */
} RSNA_STATUS;

/* Extracted/derived key material handed back to the caller. */
typedef struct _RSNA_KEYS
{
    RSNA_CIPHER pairwiseCipher;
    rsna_u8     ptk[RSNA_PTK_MAX_LEN];
    rsna_size   ptkLen;

    /* Convenience pointers into ptk[]. */
    const rsna_u8 *kck;         /* ptk + 0  */
    const rsna_u8 *kek;         /* ptk + 16 */
    const rsna_u8 *tk;          /* ptk + 32 */
    rsna_size      tkLen;

    rsna_u8     gtk[RSNA_GTK_MAX_LEN];
    rsna_size   gtkLen;
    rsna_u8     gtkKeyId;       /* group key index from the GTK KDE             */
} RSNA_KEYS;

/* Supplicant context.  Defined here so it can be stack or pool allocated. */
typedef struct _RSNA_CTX
{
    /* Configuration / identity. */
    rsna_u8     pmk[RSNA_PMK_LEN];
    int         pmkValid;

    rsna_u8     ssid[RSNA_SSID_MAX_LEN];
    rsna_size   ssidLen;

    rsna_u8     aa[RSNA_MAC_LEN];           /* Authenticator (AP) MAC           */
    rsna_u8     spa[RSNA_MAC_LEN];          /* Supplicant (STA) MAC             */
    int         haveAa;
    int         haveSpa;

    /* The STA's own RSN IE (sent in msg2 Key Data so the AP can verify it). */
    rsna_u8     staRsnIe[64];
    rsna_size   staRsnIeLen;

    /* Negotiated cipher (drives PRF length, MIC algorithm, keywrap method). */
    RSNA_CIPHER cipher;
    int         keyDescVer;                 /* from Key Information field       */

    /* Handshake transient state. */
    RSNA_STATE  state;
    RSNA_STATUS lastError;                  /* detail behind RSNA_STATE_FAILED  */
    rsna_u8     anonce[RSNA_NONCE_LEN];     /* AP nonce (from msg1)             */
    rsna_u8     snonce[RSNA_NONCE_LEN];     /* STA nonce                        */
    int         haveAnonce;
    int         haveSnonce;

    rsna_u8     replayCounter[8];           /* last replay counter answered     */
    int         haveReplay;

    /* Derived keys (valid once state == COMPLETED). */
    RSNA_KEYS   keys;

    /* SNonce PRNG state (seed via RsnaSeed()). */
    rsna_u64    rngState;
} RSNA_CTX;

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

/*
 * Initialise a context and derive the PMK = PBKDF2-HMAC-SHA1(passphrase,
 * ssid, 4096, 256 bits).  Returns RSNA_OK or RSNA_ERR_PARAM.
 */
RSNA_STATUS RsnaInit(RSNA_CTX *ctx,
                     const rsna_u8 *ssid, rsna_size ssidLen,
                     const char *passphrase, rsna_size passLen);

/* Initialise from a pre-computed 32-byte PMK; does not run PBKDF2. */
RSNA_STATUS RsnaInitWithPmk(RSNA_CTX *ctx,
                            const rsna_u8 *ssid, rsna_size ssidLen,
                            const rsna_u8 pmk[RSNA_PMK_LEN]);

/* Set the AP (authenticator) MAC address (AA). */
RSNA_STATUS RsnaSetApAddr(RSNA_CTX *ctx, const rsna_u8 aa[RSNA_MAC_LEN]);

/* Set the station (supplicant) MAC address (SPA). */
RSNA_STATUS RsnaSetStaAddr(RSNA_CTX *ctx, const rsna_u8 spa[RSNA_MAC_LEN]);

/*
 * Set the STA's own RSN IE (as sent in the (Re)Association Request); it is
 * echoed in msg2's Key Data.  If omitted, msg2 carries empty Key Data.
 */
RSNA_STATUS RsnaSetRsnIe(RSNA_CTX *ctx, const rsna_u8 *ie, rsna_size ieLen);

/* Seed the internal SNonce PRNG (mix in time/MAC/etc. for entropy). */
void RsnaSeed(RSNA_CTX *ctx, const rsna_u8 *seed, rsna_size seedLen);

/*
 * Feed a received EAPOL-Key frame (starting at the EAPOL header, without the
 * MAC header).  On input *outLen is the capacity of 'out'; on return it is
 * the length of the reply frame to transmit (msg2/msg4), or 0 if none.
 * Returns the new state; on RSNA_STATE_FAILED, RsnaLastError() gives detail.
 */
RSNA_STATE RsnaRxEapol(RSNA_CTX *ctx,
                       const rsna_u8 *in, rsna_size inLen,
                       rsna_u8 *out, rsna_size *outLen);

/* Last detailed status produced by RsnaRxEapol(). */
RSNA_STATUS RsnaLastError(const RSNA_CTX *ctx);

/* Current state. */
RSNA_STATE RsnaGetState(const RSNA_CTX *ctx);

/*
 * Retrieve the derived keys.  Returns RSNA_OK and fills *keys, or
 * RSNA_ERR_STATE if the handshake has not completed.
 */
RSNA_STATUS RsnaGetKeys(const RSNA_CTX *ctx, RSNA_KEYS *keys);

/* Read just the PMK (e.g. for caching). Returns RSNA_ERR_STATE if not derived. */
RSNA_STATUS RsnaGetPmk(const RSNA_CTX *ctx, rsna_u8 pmk[RSNA_PMK_LEN]);

/*
 * Reset the handshake state, keeping PMK, SSID, addresses and RSN IE
 * (e.g. for rekey or reassociation).
 */
void RsnaReset(RSNA_CTX *ctx);

/* Zeroise all secret material in the context. */
void RsnaClear(RSNA_CTX *ctx);

/* ------------------------------------------------------------------ */
/* Lower-level primitives exposed for testing / advanced callers.     */
/* ------------------------------------------------------------------ */

/*
 * Derive the PTK from the PMK and the handshake nonces/addresses, per
 * IEEE 802.11i 8.5.1.2:
 *   PTK = PRF-X(PMK, "Pairwise key expansion",
 *               MIN(AA,SPA) || MAX(AA,SPA) ||
 *               MIN(ANonce,SNonce) || MAX(ANonce,SNonce))
 * ptkLen selects the width (48 for CCMP, 64 for TKIP).
 */
void RsnaDerivePtk(const rsna_u8 pmk[RSNA_PMK_LEN],
                   const rsna_u8 aa[RSNA_MAC_LEN],
                   const rsna_u8 spa[RSNA_MAC_LEN],
                   const rsna_u8 anonce[RSNA_NONCE_LEN],
                   const rsna_u8 snonce[RSNA_NONCE_LEN],
                   rsna_u8 *ptk, rsna_size ptkLen);

#ifdef __cplusplus
}
#endif

#endif /* _RSNA_SUPPLICANT_H_ */
