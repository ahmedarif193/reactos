/*
 * PROJECT:     ReactOS Native WiFi - RSNA (WPA2-PSK) supplicant
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     WPA2-PSK supplicant: PMK/PTK derivation and 4-Way Handshake.
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Cleanroom implementation from IEEE 802.11i and RFC 2898/3394.
 *
 * 4-Way Handshake (supplicant side):
 *   <- msg1  (A -> S): ANonce, no MIC                         [PTK_START]
 *   -> msg2  (S -> A): SNonce, RSN IE in Key Data, MIC        [PTK_NEGOTIATING]
 *   <- msg3  (A -> S): ANonce, GTK in (encrypted) Key Data, MIC
 *   -> msg4  (S -> A): MIC, empty Key Data                    [COMPLETED]
 */

#include "rsna_supplicant.h"
#include "rsna_eapol.h"
#include "rsna_kdf.h"
#include "rsna_hmac.h"
#include "rsna_keywrap.h"
#include "rsna_rc4.h"
#include "rsna_aes.h"

/* ------------------------------------------------------------------ */
/* small local helpers (no libc dependency in the core)               */
/* ------------------------------------------------------------------ */

static void RsnaMemcpy(void *dst, const void *src, rsna_size n)
{
    rsna_u8 *d = (rsna_u8 *)dst;
    const rsna_u8 *s = (const rsna_u8 *)src;
    rsna_size i;
    for (i = 0; i < n; i++)
        d[i] = s[i];
}

static void RsnaMemset(void *dst, rsna_u8 v, rsna_size n)
{
    rsna_u8 *d = (rsna_u8 *)dst;
    rsna_size i;
    for (i = 0; i < n; i++)
        d[i] = v;
}

static int RsnaMemcmp(const void *a, const void *b, rsna_size n)
{
    const rsna_u8 *x = (const rsna_u8 *)a;
    const rsna_u8 *y = (const rsna_u8 *)b;
    rsna_size i;
    for (i = 0; i < n; i++)
    {
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    }
    return 0;
}

/* Constant-time-ish equality for MICs (avoid early-exit timing leak). */
static int RsnaSecureEqual(const rsna_u8 *a, const rsna_u8 *b, rsna_size n)
{
    rsna_u8 diff = 0;
    rsna_size i;
    for (i = 0; i < n; i++)
        diff |= (rsna_u8)(a[i] ^ b[i]);
    return diff == 0;
}

static rsna_u16 RsnaGetU16(const rsna_u8 *p)
{
    return (rsna_u16)(((rsna_u16)p[0] << 8) | p[1]);
}

static void RsnaPutU16(rsna_u8 *p, rsna_u16 v)
{
    p[0] = (rsna_u8)((v >> 8) & 0xFF);
    p[1] = (rsna_u8)(v & 0xFF);
}

/* Compare two replay counters (8 bytes, big-endian).  >0 if a>b. */
static int RsnaReplayCmp(const rsna_u8 a[8], const rsna_u8 b[8])
{
    return RsnaMemcmp(a, b, 8);
}

/* ------------------------------------------------------------------ */
/* SNonce PRNG                                                        */
/* ------------------------------------------------------------------ */

/*
 * SplitMix64 PRNG for SNonce generation.  Callers should mix in real entropy
 * via RsnaSeed(); a nonce only needs to be non-repeating.
 */
static rsna_u64 RsnaRngNext(RSNA_CTX *ctx)
{
    rsna_u64 z;
    ctx->rngState += 0x9E3779B97F4A7C15ULL;
    z = ctx->rngState;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return z;
}

static void RsnaGenNonce(RSNA_CTX *ctx, rsna_u8 nonce[RSNA_NONCE_LEN])
{
    int i;
    for (i = 0; i < RSNA_NONCE_LEN; i += 8)
    {
        rsna_u64 r = RsnaRngNext(ctx);
        int k;
        for (k = 0; k < 8 && (i + k) < RSNA_NONCE_LEN; k++)
            nonce[i + k] = (rsna_u8)((r >> (8 * k)) & 0xFF);
    }
}

void RsnaSeed(RSNA_CTX *ctx, const rsna_u8 *seed, rsna_size seedLen)
{
    rsna_size i;
    if (ctx == RSNA_NULL)
        return;
    for (i = 0; i < seedLen; i++)
    {
        ctx->rngState ^= seed[i];
        ctx->rngState *= 0x100000001B3ULL;   /* FNV-ish mix */
        ctx->rngState += 0x9E3779B97F4A7C15ULL;
    }
}

/* ------------------------------------------------------------------ */
/* PTK derivation (IEEE 802.11i 8.5.1.2)                              */
/* ------------------------------------------------------------------ */

void RsnaDerivePtk(const rsna_u8 pmk[RSNA_PMK_LEN],
                   const rsna_u8 aa[RSNA_MAC_LEN],
                   const rsna_u8 spa[RSNA_MAC_LEN],
                   const rsna_u8 anonce[RSNA_NONCE_LEN],
                   const rsna_u8 snonce[RSNA_NONCE_LEN],
                   rsna_u8 *ptk, rsna_size ptkLen)
{
    rsna_u8 data[2 * RSNA_MAC_LEN + 2 * RSNA_NONCE_LEN];
    rsna_size pos = 0;
    const rsna_u8 *addrMin, *addrMax;
    const rsna_u8 *nonceMin, *nonceMax;

    /* MIN(AA,SPA) || MAX(AA,SPA) */
    if (RsnaMemcmp(aa, spa, RSNA_MAC_LEN) < 0)
    {
        addrMin = aa; addrMax = spa;
    }
    else
    {
        addrMin = spa; addrMax = aa;
    }
    RsnaMemcpy(&data[pos], addrMin, RSNA_MAC_LEN); pos += RSNA_MAC_LEN;
    RsnaMemcpy(&data[pos], addrMax, RSNA_MAC_LEN); pos += RSNA_MAC_LEN;

    /* MIN(ANonce,SNonce) || MAX(ANonce,SNonce) */
    if (RsnaMemcmp(anonce, snonce, RSNA_NONCE_LEN) < 0)
    {
        nonceMin = anonce; nonceMax = snonce;
    }
    else
    {
        nonceMin = snonce; nonceMax = anonce;
    }
    RsnaMemcpy(&data[pos], nonceMin, RSNA_NONCE_LEN); pos += RSNA_NONCE_LEN;
    RsnaMemcpy(&data[pos], nonceMax, RSNA_NONCE_LEN); pos += RSNA_NONCE_LEN;

    RsnaPrf(pmk, RSNA_PMK_LEN, "Pairwise key expansion",
            data, pos, ptk, ptkLen * 8);
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                    */
/* ------------------------------------------------------------------ */

static void RsnaResetHandshake(RSNA_CTX *ctx)
{
    ctx->state = RSNA_STATE_IDLE;
    ctx->haveAnonce = 0;
    ctx->haveSnonce = 0;
    ctx->haveReplay = 0;
    RsnaMemset(ctx->anonce, 0, RSNA_NONCE_LEN);
    RsnaMemset(ctx->snonce, 0, RSNA_NONCE_LEN);
    RsnaMemset(ctx->replayCounter, 0, 8);
    RsnaMemset(&ctx->keys, 0, sizeof(ctx->keys));
    ctx->lastError = RSNA_OK;
}

RSNA_STATUS RsnaInitWithPmk(RSNA_CTX *ctx,
                            const rsna_u8 *ssid, rsna_size ssidLen,
                            const rsna_u8 pmk[RSNA_PMK_LEN])
{
    if (ctx == RSNA_NULL || pmk == RSNA_NULL)
        return RSNA_ERR_PARAM;
    if (ssidLen > RSNA_SSID_MAX_LEN)
        return RSNA_ERR_PARAM;

    RsnaMemset(ctx, 0, sizeof(*ctx));

    RsnaMemcpy(ctx->pmk, pmk, RSNA_PMK_LEN);
    ctx->pmkValid = 1;

    if (ssid != RSNA_NULL && ssidLen > 0)
    {
        RsnaMemcpy(ctx->ssid, ssid, ssidLen);
        ctx->ssidLen = ssidLen;
    }

    ctx->cipher = RSNA_CIPHER_CCMP;
    ctx->rngState = 0x243F6A8885A308D3ULL;   /* pi fractional bits as a base */
    RsnaResetHandshake(ctx);
    return RSNA_OK;
}

RSNA_STATUS RsnaInit(RSNA_CTX *ctx,
                     const rsna_u8 *ssid, rsna_size ssidLen,
                     const char *passphrase, rsna_size passLen)
{
    rsna_u8 pmk[RSNA_PMK_LEN];
    RSNA_STATUS st;

    if (ctx == RSNA_NULL || ssid == RSNA_NULL || passphrase == RSNA_NULL)
        return RSNA_ERR_PARAM;
    if (ssidLen == 0 || ssidLen > RSNA_SSID_MAX_LEN)
        return RSNA_ERR_PARAM;
    if (passLen < 8 || passLen > RSNA_PASSPHRASE_MAX)
        return RSNA_ERR_PARAM;

    /* PMK = PBKDF2(HMAC-SHA1, passphrase, SSID, 4096, 256 bits). */
    if (RsnaPbkdf2Sha1((const rsna_u8 *)passphrase, passLen,
                       ssid, ssidLen, 4096, pmk, RSNA_PMK_LEN) != 0)
    {
        return RSNA_ERR_PARAM;
    }

    st = RsnaInitWithPmk(ctx, ssid, ssidLen, pmk);
    RsnaMemset(pmk, 0, sizeof(pmk));
    return st;
}

RSNA_STATUS RsnaSetApAddr(RSNA_CTX *ctx, const rsna_u8 aa[RSNA_MAC_LEN])
{
    if (ctx == RSNA_NULL || aa == RSNA_NULL)
        return RSNA_ERR_PARAM;
    RsnaMemcpy(ctx->aa, aa, RSNA_MAC_LEN);
    ctx->haveAa = 1;
    return RSNA_OK;
}

RSNA_STATUS RsnaSetStaAddr(RSNA_CTX *ctx, const rsna_u8 spa[RSNA_MAC_LEN])
{
    if (ctx == RSNA_NULL || spa == RSNA_NULL)
        return RSNA_ERR_PARAM;
    RsnaMemcpy(ctx->spa, spa, RSNA_MAC_LEN);
    ctx->haveSpa = 1;
    return RSNA_OK;
}

RSNA_STATUS RsnaSetRsnIe(RSNA_CTX *ctx, const rsna_u8 *ie, rsna_size ieLen)
{
    if (ctx == RSNA_NULL)
        return RSNA_ERR_PARAM;
    if (ie == RSNA_NULL || ieLen == 0)
    {
        ctx->staRsnIeLen = 0;
        return RSNA_OK;
    }
    if (ieLen > sizeof(ctx->staRsnIe))
        return RSNA_ERR_PARAM;
    RsnaMemcpy(ctx->staRsnIe, ie, ieLen);
    ctx->staRsnIeLen = ieLen;
    return RSNA_OK;
}

void RsnaReset(RSNA_CTX *ctx)
{
    if (ctx == RSNA_NULL)
        return;
    RsnaResetHandshake(ctx);
}

void RsnaClear(RSNA_CTX *ctx)
{
    if (ctx == RSNA_NULL)
        return;
    RsnaMemset(ctx, 0, sizeof(*ctx));
}

RSNA_STATE RsnaGetState(const RSNA_CTX *ctx)
{
    if (ctx == RSNA_NULL)
        return RSNA_STATE_FAILED;
    return ctx->state;
}

RSNA_STATUS RsnaLastError(const RSNA_CTX *ctx)
{
    if (ctx == RSNA_NULL)
        return RSNA_ERR_PARAM;
    return ctx->lastError;
}

RSNA_STATUS RsnaGetPmk(const RSNA_CTX *ctx, rsna_u8 pmk[RSNA_PMK_LEN])
{
    if (ctx == RSNA_NULL || pmk == RSNA_NULL)
        return RSNA_ERR_PARAM;
    if (!ctx->pmkValid)
        return RSNA_ERR_STATE;
    RsnaMemcpy(pmk, ctx->pmk, RSNA_PMK_LEN);
    return RSNA_OK;
}

RSNA_STATUS RsnaGetKeys(const RSNA_CTX *ctx, RSNA_KEYS *keys)
{
    if (ctx == RSNA_NULL || keys == RSNA_NULL)
        return RSNA_ERR_PARAM;
    if (ctx->state != RSNA_STATE_COMPLETED)
        return RSNA_ERR_STATE;
    RsnaMemcpy(keys, &ctx->keys, sizeof(*keys));
    /* Re-point the convenience pointers into the caller's copy of ptk[]. */
    keys->kck = keys->ptk + 0;
    keys->kek = keys->ptk + RSNA_KCK_LEN;
    keys->tk  = keys->ptk + RSNA_KCK_LEN + RSNA_KEK_LEN;
    return RSNA_OK;
}

/* ------------------------------------------------------------------ */
/* MIC computation                                                   */
/* ------------------------------------------------------------------ */

/*
 * EAPOL-Key MIC over the whole frame with the MIC field zeroed: HMAC-MD5
 * (TKIP) or HMAC-SHA1 truncated to 128 bits (CCMP), per the Key Descriptor
 * Version.
 */
static void RsnaComputeMic(int keyDescVer, const rsna_u8 *kck,
                           const rsna_u8 *frame, rsna_size frameLen,
                           rsna_u8 mic[KEYDESC_MIC_LEN])
{
    if (keyDescVer == KEYINFO_VER_HMAC_MD5_RC4)
    {
        RsnaHmacMd5(kck, RSNA_KCK_LEN, frame, frameLen, mic);
    }
    else
    {
        /* HMAC-SHA1, truncated to 128 bits (first 16 bytes). */
        rsna_u8 full[RSNA_SHA1_DIGEST_LEN];
        RsnaHmacSha1(kck, RSNA_KCK_LEN, frame, frameLen, full);
        RsnaMemcpy(mic, full, KEYDESC_MIC_LEN);
    }
}

/* ------------------------------------------------------------------ */
/* GTK KDE extraction                                                */
/* ------------------------------------------------------------------ */

/*
 * Find the GTK KDE in the (decrypted) msg3 Key Data and copy the GTK into
 * ctx->keys:
 *   0xDD len 00-0F-AC 01 <keyid+tx byte> <reserved> <GTK...>
 */
static RSNA_STATUS RsnaExtractGtk(RSNA_CTX *ctx,
                                  const rsna_u8 *kd, rsna_size kdLen)
{
    rsna_size pos = 0;

    while (pos + 2 <= kdLen)
    {
        rsna_u8 id  = kd[pos];
        rsna_u8 len = kd[pos + 1];
        const rsna_u8 *body;
        rsna_size bodyLen;

        if (id == 0x00 && len == 0x00)
            break;                              /* padding */

        if (pos + 2 + len > kdLen)
            break;                              /* truncated element */

        body = &kd[pos + 2];
        bodyLen = len;

        if (id == KDE_IE_VENDOR && bodyLen >= 4 &&
            body[0] == KDE_OUI_0 && body[1] == KDE_OUI_1 && body[2] == KDE_OUI_2)
        {
            rsna_u8 kdeType = body[3];
            const rsna_u8 *kdeData = body + 4;
            rsna_size kdeDataLen = bodyLen - 4;

            if (kdeType == KDE_TYPE_GTK && kdeDataLen >= GTK_KDE_HDR_LEN)
            {
                rsna_size gtkLen = kdeDataLen - GTK_KDE_HDR_LEN;
                if (gtkLen == 0 || gtkLen > RSNA_GTK_MAX_LEN)
                    return RSNA_ERR_NO_GTK;

                ctx->keys.gtkKeyId = (rsna_u8)(kdeData[GTK_KDE_OFF_KEYID] & 0x03);
                RsnaMemcpy(ctx->keys.gtk, kdeData + GTK_KDE_OFF_GTK, gtkLen);
                ctx->keys.gtkLen = gtkLen;
                return RSNA_OK;
            }
        }

        pos += 2 + len;
    }

    return RSNA_ERR_NO_GTK;
}

/* ------------------------------------------------------------------ */
/* Key Data decryption (msg3)                                         */
/* ------------------------------------------------------------------ */

/*
 * Decrypt the Key Data field of msg3.
 *   CCMP variant (ver 2/3): NIST AES Key Unwrap with KEK; output is 8 bytes
 *                           shorter than input (must be a multiple of 8).
 *   TKIP variant (ver 1)  : RC4 with key = KeyIV(16) || KEK(16), drop 256.
 */
static RSNA_STATUS RsnaDecryptKeyData(RSNA_CTX *ctx,
                                      const rsna_u8 *iv,
                                      const rsna_u8 *enc, rsna_size encLen,
                                      rsna_u8 *plain, rsna_size *outLen)
{
    const rsna_u8 *kek = ctx->keys.ptk + RSNA_KCK_LEN;

    if (ctx->keyDescVer == KEYINFO_VER_HMAC_MD5_RC4)
    {
        /* TKIP: RC4(KeyIV || KEK), skip first 256 keystream bytes. */
        RSNA_RC4_CTX rc4;
        rsna_u8 rc4key[KEYDESC_IV_LEN + RSNA_KEK_LEN];

        RsnaMemcpy(rc4key, iv, KEYDESC_IV_LEN);
        RsnaMemcpy(rc4key + KEYDESC_IV_LEN, kek, RSNA_KEK_LEN);

        RsnaRc4Init(&rc4, rc4key, sizeof(rc4key));
        RsnaRc4Skip(&rc4, 256);
        RsnaRc4Crypt(&rc4, enc, plain, encLen);
        RsnaMemset(rc4key, 0, sizeof(rc4key));

        *outLen = encLen;
        return RSNA_OK;
    }
    else
    {
        /* CCMP: AES Key Unwrap with a 128-bit KEK. encLen must be a multiple
         * of 8 and >= 16 (one IV block + at least one data block). */
        rsna_size n;
        if (encLen < 16 || (encLen % 8) != 0)
            return RSNA_ERR_UNWRAP;
        n = (encLen / 8) - 1;
        if (RsnaAesUnwrap(kek, 128, enc, n, plain) != 0)
            return RSNA_ERR_UNWRAP;
        *outLen = n * 8;
        return RSNA_OK;
    }
}

/* ------------------------------------------------------------------ */
/* Reply-frame construction                                          */
/* ------------------------------------------------------------------ */

/*
 * Build msg2 or msg4.  nonce: SNonce for msg2, NULL (all-zero) for msg4.
 * keyData: RSN IE for msg2, empty for msg4.  The MIC is computed last over
 * the whole frame with the MIC field zeroed.  Returns the total frame
 * length, or 0 if 'out' is too small.
 */
static rsna_size RsnaBuildReply(RSNA_CTX *ctx,
                                rsna_u16 keyInfo,
                                const rsna_u8 *nonce,
                                const rsna_u8 *keyData, rsna_size keyDataLen,
                                rsna_u8 *out, rsna_size outCap)
{
    rsna_size bodyLen = KEYDESC_FIXED_LEN + keyDataLen;
    rsna_size frameLen = EAPOL_HDR_LEN + bodyLen;
    rsna_u8 *body;
    rsna_u8 mic[KEYDESC_MIC_LEN];

    if (frameLen > outCap)
        return 0;

    RsnaMemset(out, 0, frameLen);

    /* EAPOL header. */
    out[EAPOL_OFF_VERSION] = EAPOL_VERSION_2004;
    out[EAPOL_OFF_TYPE]    = EAPOL_TYPE_KEY;
    RsnaPutU16(&out[EAPOL_OFF_LENGTH], (rsna_u16)bodyLen);

    body = out + EAPOL_HDR_LEN;

    /* Descriptor type matches the negotiated descriptor. */
    body[KEYDESC_OFF_TYPE] = (ctx->keyDescVer == KEYINFO_VER_HMAC_MD5_RC4)
                                 ? EAPOL_KEY_DESC_WPA : EAPOL_KEY_DESC_RSN;

    RsnaPutU16(&body[KEYDESC_OFF_KEYINFO], keyInfo);

    /* RSN sets Key Length to zero in messages 2 and 4.  Legacy WPA echoes
     * the pairwise cipher key length instead. */
    RsnaPutU16(&body[KEYDESC_OFF_KEYLEN],
               (body[KEYDESC_OFF_TYPE] == EAPOL_KEY_DESC_RSN) ? 0 :
               ((ctx->cipher == RSNA_CIPHER_TKIP) ? RSNA_TK_TKIP_LEN
                                                  : RSNA_TK_CCMP_LEN));

    /* Echo the AP's replay counter. */
    RsnaMemcpy(&body[KEYDESC_OFF_REPLAY], ctx->replayCounter, KEYDESC_REPLAY_LEN);

    /* Key Nonce: SNonce for msg2; all-zero (from the memset) for msg4. */
    if (nonce != RSNA_NULL)
        RsnaMemcpy(&body[KEYDESC_OFF_NONCE], nonce, KEYDESC_NONCE_LEN);

    /* Key Data. */
    RsnaPutU16(&body[KEYDESC_OFF_DATALEN], (rsna_u16)keyDataLen);
    if (keyDataLen > 0 && keyData != RSNA_NULL)
        RsnaMemcpy(&body[KEYDESC_OFF_DATA], keyData, keyDataLen);

    /* MIC over the whole frame with the MIC field (still) zeroed. */
    if (keyInfo & KEYINFO_KEY_MIC)
    {
        RsnaComputeMic(ctx->keyDescVer, ctx->keys.ptk, out, frameLen, mic);
        RsnaMemcpy(&body[KEYDESC_OFF_MIC], mic, KEYDESC_MIC_LEN);
    }

    return frameLen;
}

/* ------------------------------------------------------------------ */
/* Handshake message handlers                                         */
/* ------------------------------------------------------------------ */

/*
 * msg1 (ANonce, Pairwise, Ack, no MIC): generate the SNonce, derive the PTK,
 * build msg2.
 */
static RSNA_STATE RsnaHandleMsg1(RSNA_CTX *ctx,
                                 const rsna_u8 *body,
                                 rsna_u8 *out, rsna_size *outLen)
{
    rsna_size cap = (out != RSNA_NULL) ? *outLen : 0;
    rsna_size n;
    rsna_u16 keyInfo;

    if (!ctx->pmkValid || !ctx->haveAa || !ctx->haveSpa)
    {
        ctx->lastError = RSNA_ERR_STATE;
        return (ctx->state = RSNA_STATE_FAILED);
    }

    /* Capture the ANonce and the replay counter to echo in msg2. */
    RsnaMemcpy(ctx->anonce, &body[KEYDESC_OFF_NONCE], RSNA_NONCE_LEN);
    ctx->haveAnonce = 1;
    RsnaMemcpy(ctx->replayCounter, &body[KEYDESC_OFF_REPLAY], 8);
    ctx->haveReplay = 1;

    /* Pick the cipher/PRF width from the Key Descriptor Version. */
    if (ctx->keyDescVer == KEYINFO_VER_HMAC_MD5_RC4)
    {
        ctx->cipher = RSNA_CIPHER_TKIP;
        ctx->keys.ptkLen = RSNA_PTK_TKIP_LEN;
        ctx->keys.tkLen = RSNA_TK_TKIP_LEN;
    }
    else
    {
        ctx->cipher = RSNA_CIPHER_CCMP;
        ctx->keys.ptkLen = RSNA_PTK_CCMP_LEN;
        ctx->keys.tkLen = RSNA_TK_CCMP_LEN;
    }
    ctx->keys.pairwiseCipher = ctx->cipher;

    /* Fresh SNonce, then derive the PTK. */
    RsnaGenNonce(ctx, ctx->snonce);
    ctx->haveSnonce = 1;

    RsnaDerivePtk(ctx->pmk, ctx->aa, ctx->spa, ctx->anonce, ctx->snonce,
                  ctx->keys.ptk, ctx->keys.ptkLen);

    /* Build msg2: Pairwise + MIC, Key Data = STA RSN IE. */
    keyInfo = (rsna_u16)(ctx->keyDescVer & KEYINFO_VERSION_MASK);
    keyInfo |= KEYINFO_KEY_TYPE | KEYINFO_KEY_MIC;

    n = RsnaBuildReply(ctx, keyInfo, ctx->snonce,
                       (ctx->staRsnIeLen > 0) ? ctx->staRsnIe : RSNA_NULL,
                       ctx->staRsnIeLen, out, cap);
    if (n == 0 && out != RSNA_NULL)
    {
        ctx->lastError = RSNA_ERR_TOO_SMALL;
        return (ctx->state = RSNA_STATE_FAILED);
    }

    if (outLen != RSNA_NULL)
        *outLen = n;

    ctx->lastError = RSNA_OK;
    ctx->state = RSNA_STATE_PTK_NEGOTIATING;
    return ctx->state;
}

/*
 * msg3 (ANonce, Pairwise, Ack, Install, MIC, encrypted Key Data): verify the
 * MIC, decrypt and extract the GTK, build msg4.
 */
static RSNA_STATE RsnaHandleMsg3(RSNA_CTX *ctx,
                                 const rsna_u8 *frame, rsna_size frameLen,
                                 const rsna_u8 *body, rsna_u16 keyInfo,
                                 rsna_u8 *out, rsna_size *outLen)
{
    rsna_size cap = (out != RSNA_NULL) ? *outLen : 0;
    rsna_u8 rxMic[KEYDESC_MIC_LEN];
    rsna_u8 calcMic[KEYDESC_MIC_LEN];
    rsna_u8 *frameCopy;
    rsna_u8 frameBuf[512];
    rsna_u16 kdLen;
    const rsna_u8 *kd;
    rsna_u8 plainKd[512];
    rsna_size plainLen = 0;
    RSNA_STATUS st;
    rsna_size n;
    rsna_u16 replyInfo;

    if (ctx->state != RSNA_STATE_PTK_NEGOTIATING)
    {
        ctx->lastError = RSNA_ERR_STATE;
        return (ctx->state = RSNA_STATE_FAILED);
    }

    /* ANonce in msg3 must match msg1's ANonce (prevents nonce reuse attacks). */
    if (RsnaMemcmp(&body[KEYDESC_OFF_NONCE], ctx->anonce, RSNA_NONCE_LEN) != 0)
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        return (ctx->state = RSNA_STATE_FAILED);
    }

    /* ---- verify the Key MIC over the frame with MIC field zeroed ---- */
    if (frameLen > sizeof(frameBuf))
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        return (ctx->state = RSNA_STATE_FAILED);
    }
    RsnaMemcpy(frameBuf, frame, frameLen);
    frameCopy = frameBuf;

    /* Save received MIC, zero the field in the copy. */
    RsnaMemcpy(rxMic, &body[KEYDESC_OFF_MIC], KEYDESC_MIC_LEN);
    RsnaMemset(frameCopy + EAPOL_HDR_LEN + KEYDESC_OFF_MIC, 0, KEYDESC_MIC_LEN);

    RsnaComputeMic(ctx->keyDescVer, ctx->keys.ptk, frameCopy, frameLen, calcMic);
    if (!RsnaSecureEqual(rxMic, calcMic, KEYDESC_MIC_LEN))
    {
        ctx->lastError = RSNA_ERR_MIC;
        return (ctx->state = RSNA_STATE_FAILED);
    }

    /* Commit the replay counter only after MIC verification; it is echoed
     * in msg4 and becomes the new replay baseline. */
    RsnaMemcpy(ctx->replayCounter, &body[KEYDESC_OFF_REPLAY], 8);

    /* ---- decrypt Key Data and extract the GTK ---- */
    kdLen = RsnaGetU16(&body[KEYDESC_OFF_DATALEN]);
    kd = &body[KEYDESC_OFF_DATA];

    if ((rsna_size)(KEYDESC_OFF_DATA + kdLen) > (frameLen - EAPOL_HDR_LEN))
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        return (ctx->state = RSNA_STATE_FAILED);
    }
    if (kdLen > sizeof(plainKd))
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        return (ctx->state = RSNA_STATE_FAILED);
    }

    if (keyInfo & KEYINFO_ENCRYPTED_DATA)
    {
        st = RsnaDecryptKeyData(ctx, &body[KEYDESC_OFF_IV], kd, kdLen,
                                plainKd, &plainLen);
        if (st != RSNA_OK)
        {
            ctx->lastError = st;
            return (ctx->state = RSNA_STATE_FAILED);
        }
    }
    else
    {
        RsnaMemcpy(plainKd, kd, kdLen);
        plainLen = kdLen;
    }

    st = RsnaExtractGtk(ctx, plainKd, plainLen);
    if (st != RSNA_OK)
    {
        ctx->lastError = st;
        return (ctx->state = RSNA_STATE_FAILED);
    }

    /* Wipe the decrypted key data buffer. */
    RsnaMemset(plainKd, 0, sizeof(plainKd));

    /* Set up the key convenience pointers. */
    ctx->keys.kck = ctx->keys.ptk + 0;
    ctx->keys.kek = ctx->keys.ptk + RSNA_KCK_LEN;
    ctx->keys.tk  = ctx->keys.ptk + RSNA_KCK_LEN + RSNA_KEK_LEN;

    /* ---- build msg4: Pairwise + MIC + Secure, empty Key Data ---- */
    replyInfo = (rsna_u16)(ctx->keyDescVer & KEYINFO_VERSION_MASK);
    replyInfo |= KEYINFO_KEY_TYPE | KEYINFO_KEY_MIC | KEYINFO_SECURE;

    /* msg4 carries an all-zero Key Nonce. */
    n = RsnaBuildReply(ctx, replyInfo, RSNA_NULL, RSNA_NULL, 0, out, cap);
    if (n == 0 && out != RSNA_NULL)
    {
        ctx->lastError = RSNA_ERR_TOO_SMALL;
        return (ctx->state = RSNA_STATE_FAILED);
    }
    if (outLen != RSNA_NULL)
        *outLen = n;

    ctx->lastError = RSNA_OK;
    ctx->state = RSNA_STATE_COMPLETED;
    return ctx->state;
}

/* ------------------------------------------------------------------ */
/* Top-level dispatch                                                 */
/* ------------------------------------------------------------------ */

RSNA_STATE RsnaRxEapol(RSNA_CTX *ctx,
                       const rsna_u8 *in, rsna_size inLen,
                       rsna_u8 *out, rsna_size *outLen)
{
    const rsna_u8 *body;
    rsna_size bodyLen;
    rsna_u16 keyInfo;
    rsna_u8 descType;
    int isPairwise, hasMic, hasAck, hasInstall, hasSecure, hasEncrypted;
    rsna_size noTx = 0;

    if (ctx == RSNA_NULL || in == RSNA_NULL)
    {
        if (ctx) ctx->lastError = RSNA_ERR_PARAM;
        return RSNA_STATE_FAILED;
    }
    if (outLen == RSNA_NULL)
        outLen = &noTx;

    /* ---- basic EAPOL framing checks ---- */
    if (inLen < EAPOL_KEY_FRAME_FIXED_LEN)
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        *outLen = 0;
        return ctx->state;          /* ignore runt; keep state */
    }
    if (in[EAPOL_OFF_TYPE] != EAPOL_TYPE_KEY)
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        *outLen = 0;
        return ctx->state;
    }

    bodyLen = RsnaGetU16(&in[EAPOL_OFF_LENGTH]);
    if ((rsna_size)(EAPOL_HDR_LEN + bodyLen) > inLen ||
        bodyLen < KEYDESC_FIXED_LEN)
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        *outLen = 0;
        return ctx->state;
    }

    body = in + EAPOL_HDR_LEN;
    descType = body[KEYDESC_OFF_TYPE];
    if (descType != EAPOL_KEY_DESC_RSN && descType != EAPOL_KEY_DESC_WPA)
    {
        ctx->lastError = RSNA_ERR_MALFORMED;
        *outLen = 0;
        return ctx->state;
    }

    keyInfo = RsnaGetU16(&body[KEYDESC_OFF_KEYINFO]);
    ctx->keyDescVer = keyInfo & KEYINFO_VERSION_MASK;

    isPairwise   = (keyInfo & KEYINFO_KEY_TYPE) ? 1 : 0;
    hasMic       = (keyInfo & KEYINFO_KEY_MIC) ? 1 : 0;
    hasAck       = (keyInfo & KEYINFO_KEY_ACK) ? 1 : 0;
    hasInstall   = (keyInfo & KEYINFO_INSTALL) ? 1 : 0;
    hasSecure    = (keyInfo & KEYINFO_SECURE) ? 1 : 0;
    hasEncrypted = (keyInfo & KEYINFO_ENCRYPTED_DATA) ? 1 : 0;
    (void)hasSecure;
    (void)hasEncrypted;

    /* The supplicant only consumes frames the AP sends (Key Ack set). */
    if (!hasAck)
    {
        ctx->lastError = RSNA_ERR_STATE;
        *outLen = 0;
        return ctx->state;
    }

    /* Replay counters: msg1 sets the baseline; msg3 must be strictly greater.
     * Exception: a MIC'd frame with the *same* counter while COMPLETED is the
     * AP retransmitting msg3 (lost msg4) -> re-emit msg4. */
    if (ctx->haveReplay && hasMic)
    {
        int cmp = RsnaReplayCmp(&body[KEYDESC_OFF_REPLAY], ctx->replayCounter);

        if (cmp == 0 && ctx->state == RSNA_STATE_COMPLETED && isPairwise)
        {
            rsna_u16 replyInfo = (rsna_u16)(ctx->keyDescVer & KEYINFO_VERSION_MASK);
            rsna_size n;
            replyInfo |= KEYINFO_KEY_TYPE | KEYINFO_KEY_MIC | KEYINFO_SECURE;
            n = RsnaBuildReply(ctx, replyInfo, RSNA_NULL, RSNA_NULL, 0,
                               out, (out != RSNA_NULL) ? *outLen : 0);
            *outLen = n;
            ctx->lastError = RSNA_OK;
            return ctx->state;
        }

        if (cmp <= 0)
        {
            ctx->lastError = RSNA_ERR_REPLAY;
            *outLen = 0;
            return ctx->state;      /* silently drop the replay */
        }
    }

    /* msg1: Pairwise, Ack, no MIC.
     * msg3: Pairwise, Ack, MIC, Install (and usually Secure + Encrypted). */
    if (isPairwise && hasAck && !hasMic)
    {
        /* message 1 */
        return RsnaHandleMsg1(ctx, body, out, outLen);
    }
    else if (isPairwise && hasAck && hasMic)
    {
        /* message 3.  The replay counter is committed only after the MIC is
         * verified, so a forged msg3 cannot lock out the legitimate one. */
        (void)hasInstall;
        return RsnaHandleMsg3(ctx, in, EAPOL_HDR_LEN + bodyLen,
                              body, keyInfo, out, outLen);
    }

    /* Group-key handshake and other messages are not handled. */
    ctx->lastError = RSNA_ERR_STATE;
    *outLen = 0;
    return ctx->state;
}
