/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WPA2-PSK supplicant integration (RSNA 4-way handshake)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

/* The RSNA supplicant library API (pure crypto + 4-way handshake FSM). */
#include <rsna_supplicant.h>

#define NDEBUG
#include <debug.h>

#define EAPOL_ETHERTYPE     0x888E
#define NWIFI_CREDENTIAL_MAX 128

/* Per-interface supplicant session.  Keeps a copy of the seeded passphrase
 * so a session can be re-initialised per SSID (the PMK is SSID-dependent). */
typedef struct _NWIFI_SUPPLICANT
{
    RSNA_CTX  Ctx;
    BOOLEAN   Initialised;          /* RsnaInit done (PMK derived)              */
    BOOLEAN   PassphraseSet;
    UCHAR     Passphrase[NWIFI_CREDENTIAL_MAX + 1];
    ULONG     PassphraseLen;
    UCHAR     OwnMac[IEEE80211_ADDR_LEN];
    BOOLEAN   KeysInstalled;
    volatile LONG InstallQueued;
    ULONG     Generation;           /* invalidates queued work across sessions */
} NWIFI_SUPPLICANT;

typedef struct _NWIFI_INSTALL_KEYS_CONTEXT
{
    PNWIFI_MSM Msm;
    ULONG Generation;
    UCHAR PeerMac[IEEE80211_ADDR_LEN];
    ULONG EapolLength;
    UCHAR Eapol[256];
} NWIFI_INSTALL_KEYS_CONTEXT, *PNWIFI_INSTALL_KEYS_CONTEXT;

static
BOOLEAN
NwifiHexNibble(
    _In_ UCHAR Character,
    _Out_ PUCHAR Value)
{
    if (Character >= '0' && Character <= '9')
    {
        *Value = Character - '0';
        return TRUE;
    }
    if (Character >= 'a' && Character <= 'f')
    {
        *Value = Character - 'a' + 10;
        return TRUE;
    }
    if (Character >= 'A' && Character <= 'F')
    {
        *Value = Character - 'A' + 10;
        return TRUE;
    }
    return FALSE;
}

/* ===========================================================================
 *  Cipher-key install helpers (build DOT11 cipher-key structs, push down)
 * ===========================================================================
 */

/* Map an RSNA cipher to the dot11 cipher-algorithm id. */
static
DOT11_CIPHER_ALGORITHM
NwifiDot11CipherFromRsna(
    _In_ RSNA_CIPHER Cipher)
{
    return (Cipher == RSNA_CIPHER_TKIP) ? DOT11_CIPHER_ALGO_TKIP
                                        : DOT11_CIPHER_ALGO_CCMP;
}

static
NDIS_STATUS
NwifiEncodeCipherKey(
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength,
    _Out_writes_bytes_(EncodedCapacity) PUCHAR Encoded,
    _In_ ULONG EncodedCapacity,
    _Out_ PULONG EncodedLength)
{
    ULONG HeaderLength;

    *EncodedLength = 0;
    RtlZeroMemory(Encoded, EncodedCapacity);
    if (Key == NULL || KeyLength == 0)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    switch (Algorithm)
    {
        case DOT11_CIPHER_ALGO_CCMP:
        {
            PDOT11_KEY_ALGO_CCMP Ccmp = (PDOT11_KEY_ALGO_CCMP)Encoded;

            HeaderLength = FIELD_OFFSET(DOT11_KEY_ALGO_CCMP, ucCCMPKey);
            if (KeyLength != 16 || HeaderLength > EncodedCapacity ||
                KeyLength > EncodedCapacity - HeaderLength)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Ccmp->ulCCMPKeyLength = KeyLength;
            RtlCopyMemory(Ccmp->ucCCMPKey, Key, KeyLength);
            *EncodedLength = HeaderLength + KeyLength;
            return NDIS_STATUS_SUCCESS;
        }

        case DOT11_CIPHER_ALGO_TKIP:
        {
            PDOT11_KEY_ALGO_TKIP_MIC Tkip =
                (PDOT11_KEY_ALGO_TKIP_MIC)Encoded;

            HeaderLength = FIELD_OFFSET(DOT11_KEY_ALGO_TKIP_MIC,
                                        ucTKIPMICKeys);
            if (KeyLength != 32 || HeaderLength > EncodedCapacity ||
                KeyLength > EncodedCapacity - HeaderLength)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Tkip->ulTKIPKeyLength = 16;
            Tkip->ulMICKeyLength = 16;
            RtlCopyMemory(Tkip->ucTKIPMICKeys, Key, KeyLength);
            *EncodedLength = HeaderLength + KeyLength;
            return NDIS_STATUS_SUCCESS;
        }

        case DOT11_CIPHER_ALGO_WEP40:
            if (KeyLength != 5)
                return NDIS_STATUS_INVALID_LENGTH;
            break;

        case DOT11_CIPHER_ALGO_WEP104:
            if (KeyLength != 13)
                return NDIS_STATUS_INVALID_LENGTH;
            break;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (KeyLength > EncodedCapacity)
        return NDIS_STATUS_INVALID_LENGTH;
    RtlCopyMemory(Encoded, Key, KeyLength);
    *EncodedLength = KeyLength;
    return NDIS_STATUS_SUCCESS;
}

/* OID_DOT11_CIPHER_KEY_MAPPING_KEY carries a DOT11_BYTE_ARRAY whose ucBuffer
 * is a DOT11_CIPHER_KEY_MAPPING_KEY_VALUE (peer MAC + inlined key). */
NDIS_STATUS
NwifiInstallPairwiseKey(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_reads_bytes_(6) PUCHAR PeerMac,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength)
{
    PDOT11_BYTE_ARRAY ByteArray;
    PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE Value;
    UCHAR EncodedKey[64];
    ULONG EncodedLength;
    ULONG ValueSize;
    ULONG TotalSize;
    NDIS_STATUS Status;

    Status = NwifiEncodeCipherKey(Algorithm,
                                  Key,
                                  KeyLength,
                                  EncodedKey,
                                  sizeof(EncodedKey),
                                  &EncodedLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    /* DOT11_CIPHER_KEY_MAPPING_KEY_VALUE has a 1-byte ucKey[] placeholder. */
    ValueSize = FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey) + EncodedLength;
    TotalSize = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + ValueSize;

    ByteArray = (PDOT11_BYTE_ARRAY)NwifiAllocate(TotalSize);
    if (ByteArray == NULL)
    {
        RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));
        return NDIS_STATUS_RESOURCES;
    }

    ByteArray->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    ByteArray->Header.Revision = DOT11_CIPHER_KEY_MAPPING_KEY_VALUE_BYTE_ARRAY_REVISION_1;
    ByteArray->Header.Size = sizeof(DOT11_BYTE_ARRAY);
    ByteArray->uNumOfBytes = ValueSize;
    ByteArray->uTotalNumOfBytes = ValueSize;

    Value = (PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE)ByteArray->ucBuffer;
    RtlCopyMemory(Value->PeerMacAddr, PeerMac, IEEE80211_ADDR_LEN);
    Value->AlgorithmId = Algorithm;
    Value->Direction = DOT11_DIR_BOTH;
    Value->bDelete = FALSE;
    Value->bStatic = FALSE;
    Value->usKeyLength = (USHORT)EncodedLength;
    RtlCopyMemory(Value->ucKey, EncodedKey, EncodedLength);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
                                    ByteArray, TotalSize, NULL);
    RtlSecureZeroMemory(ByteArray, TotalSize);
    NwifiFree(ByteArray);
    RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));
    DPRINT1("NWIFI: install pairwise key (cipher %u len %u) -> 0x%08X\n",
            Algorithm, KeyLength, Status);
    return Status;
}

/* OID_DOT11_CIPHER_DEFAULT_KEY carries a DOT11_CIPHER_DEFAULT_KEY_VALUE
 * directly.  Install the GTK at the given index and select it as default. */
NDIS_STATUS
NwifiInstallGroupKey(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_ UCHAR KeyId,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength)
{
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value;
    UCHAR EncodedKey[64];
    ULONG EncodedLength;
    ULONG ValueSize;
    ULONG DefaultKeyId;
    NDIS_STATUS Status;

    Status = NwifiEncodeCipherKey(Algorithm,
                                  Key,
                                  KeyLength,
                                  EncodedKey,
                                  sizeof(EncodedKey),
                                  &EncodedLength);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    ValueSize = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) + EncodedLength;
    Value = (PDOT11_CIPHER_DEFAULT_KEY_VALUE)NwifiAllocate(ValueSize);
    if (Value == NULL)
    {
        RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));
        return NDIS_STATUS_RESOURCES;
    }

    Value->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Value->Header.Revision = DOT11_CIPHER_DEFAULT_KEY_VALUE_REVISION_1;
    Value->Header.Size = sizeof(DOT11_CIPHER_DEFAULT_KEY_VALUE);
    Value->uKeyIndex = KeyId;
    Value->AlgorithmId = Algorithm;
    /* A group key is not bound to a peer; the all-zero MAC means "group". */
    RtlZeroMemory(Value->MacAddr, IEEE80211_ADDR_LEN);
    Value->bDelete = FALSE;
    Value->bStatic = FALSE;
    Value->usKeyLength = (USHORT)EncodedLength;
    RtlCopyMemory(Value->ucKey, EncodedKey, EncodedLength);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_DEFAULT_KEY,
                                    Value, ValueSize, NULL);
    RtlSecureZeroMemory(Value, ValueSize);
    NwifiFree(Value);
    RtlSecureZeroMemory(EncodedKey, sizeof(EncodedKey));

    if (Status == NDIS_STATUS_SUCCESS)
    {
        DefaultKeyId = KeyId;
        Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                        OID_DOT11_CIPHER_DEFAULT_KEY_ID,
                                        &DefaultKeyId,
                                        sizeof(DefaultKeyId),
                                        NULL);
    }
    DPRINT1("NWIFI: install group key id %u (cipher %u len %u) -> 0x%08X\n",
            KeyId, Algorithm, KeyLength, Status);
    return Status;
}

/* ===========================================================================
 *  Session lifecycle
 * ===========================================================================
 */
NDIS_STATUS
NwifiSupplicantSetPassphrase(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) PUCHAR Passphrase,
    _In_ ULONG Length)
{
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_SUPPLICANT Candidate;

    if (Length == 0 || Length > NWIFI_CREDENTIAL_MAX)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    /* The session may not exist yet (SET_KEY can precede CONNECT); allocate a
     * placeholder to stash the passphrase until the connect arms it. */
    Candidate = (PNWIFI_SUPPLICANT)NwifiAllocate(sizeof(NWIFI_SUPPLICANT));

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL && Candidate != NULL)
    {
        Sup = Candidate;
        Candidate = NULL;
        Msm->Supplicant = Sup;
    }
    if (Sup == NULL)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    if (Sup->Initialised)
    {
        RsnaClear(&Sup->Ctx);
    }
    Sup->Generation++;
    Sup->Initialised = FALSE;
    Sup->KeysInstalled = FALSE;
    Sup->InstallQueued = 0;
    RtlSecureZeroMemory(Sup->Passphrase, sizeof(Sup->Passphrase));
    RtlCopyMemory(Sup->Passphrase, Passphrase, Length);
    Sup->Passphrase[Length] = 0;
    Sup->PassphraseLen = Length;
    Sup->PassphraseSet = TRUE;
    NdisReleaseSpinLock(&Msm->Lock);

    if (Candidate != NULL)
    {
        NwifiFree(Candidate);
    }
    DPRINT1("NWIFI: supplicant passphrase seeded (%u bytes)\n", Length);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NwifiSupplicantStart(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    DOT11_SSID Ssid;
    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;
    UCHAR Credential[NWIFI_CREDENTIAL_MAX];
    ULONG CredentialLength;
    ULONG Generation;
    RSNA_CTX NewCtx;
    RSNA_STATUS RStatus;
    UCHAR RsnIe[22] = {
        0x30, 0x14, 0x01, 0x00,
        0x00, 0x0F, 0xAC, 0x00,
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x00,
        0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02,
        0x00, 0x00
    };

    RtlZeroMemory(&NewCtx, sizeof(NewCtx));
    RtlZeroMemory(Credential, sizeof(Credential));

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->PassphraseSet)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_FAILURE;
    }
    CredentialLength = Sup->PassphraseLen;
    if (CredentialLength > sizeof(Credential))
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_INVALID_LENGTH;
    }
    RtlCopyMemory(Credential, Sup->Passphrase, CredentialLength);
    Generation = Sup->Generation;
    Ssid = Msm->Connect.Ssid;
    AuthAlgorithm = Msm->Connect.AuthAlgorithm;
    UnicastCipher = Msm->Connect.UnicastCipher;
    MulticastCipher = Msm->Connect.MulticastCipher;
    NdisReleaseSpinLock(&Msm->Lock);

    if (AuthAlgorithm != DOT11_AUTH_ALGO_RSNA_PSK)
    {
        RStatus = RSNA_ERR_PARAM;
        goto StartFailed;
    }
    if (Ssid.uSSIDLength == 0 || Ssid.uSSIDLength > RSNA_SSID_MAX_LEN)
    {
        RStatus = RSNA_ERR_PARAM;
        goto StartFailed;
    }

    /* A 64-digit hexadecimal credential is an already-derived 256-bit PSK;
     * ordinary 8..63 byte credentials use PBKDF2-HMAC-SHA1. */
    if (CredentialLength == 64)
    {
        UCHAR Pmk[RSNA_PMK_LEN];
        ULONG Index;

        for (Index = 0; Index < ARRAYSIZE(Pmk); Index++)
        {
            UCHAR High;
            UCHAR Low;

            if (!NwifiHexNibble(Credential[Index * 2], &High) ||
                !NwifiHexNibble(Credential[Index * 2 + 1], &Low))
            {
                RtlSecureZeroMemory(Pmk, sizeof(Pmk));
                RStatus = RSNA_ERR_PARAM;
                goto StartFailed;
            }
            Pmk[Index] = (High << 4) | Low;
        }
        RStatus = RsnaInitWithPmk(&NewCtx,
                                  Ssid.ucSSID, Ssid.uSSIDLength,
                                  Pmk);
        RtlSecureZeroMemory(Pmk, sizeof(Pmk));
    }
    else
    {
        RStatus = RsnaInit(&NewCtx,
                           Ssid.ucSSID, Ssid.uSSIDLength,
                           (const char *)Credential, CredentialLength);
    }
    if (RStatus != RSNA_OK)
    {
        goto StartFailed;
    }

    if (UnicastCipher == DOT11_CIPHER_ALGO_CCMP)
    {
        RsnIe[13] = 4;
    }
    else if (UnicastCipher == DOT11_CIPHER_ALGO_TKIP)
    {
        RsnIe[13] = 2;
    }
    else
    {
        RStatus = RSNA_ERR_PARAM;
        goto StartFailed;
    }
    if (MulticastCipher == DOT11_CIPHER_ALGO_CCMP)
    {
        RsnIe[7] = 4;
    }
    else if (MulticastCipher == DOT11_CIPHER_ALGO_TKIP)
    {
        RsnIe[7] = 2;
    }
    else
    {
        RStatus = RSNA_ERR_PARAM;
        goto StartFailed;
    }
    RStatus = RsnaSetRsnIe(&NewCtx, RsnIe, sizeof(RsnIe));
    if (RStatus != RSNA_OK)
        goto StartFailed;

    /* The supplicant is the station (SPA == our MAC). */
    RStatus = RsnaSetStaAddr(&NewCtx, Adapter->MacAddress);
    if (RStatus != RSNA_OK)
        goto StartFailed;

    /* Seed the SNonce PRNG with whatever entropy we have (MAC + tick count). */
    {
        UCHAR Seed[IEEE80211_ADDR_LEN + sizeof(ULONG64)];
        ULONG64 Tick = (ULONG64)KeQueryInterruptTime();
        RtlCopyMemory(Seed, Adapter->MacAddress, IEEE80211_ADDR_LEN);
        RtlCopyMemory(Seed + IEEE80211_ADDR_LEN, &Tick, sizeof(Tick));
        RsnaSeed(&NewCtx, Seed, sizeof(Seed));
        RtlSecureZeroMemory(Seed, sizeof(Seed));
    }

    NdisAcquireSpinLock(&Msm->Lock);
    if (Msm->Supplicant != Sup || Sup->Generation != Generation ||
        Msm->Connect.AuthAlgorithm != AuthAlgorithm ||
        Msm->Connect.Ssid.uSSIDLength != Ssid.uSSIDLength ||
        RtlCompareMemory(Msm->Connect.Ssid.ucSSID, Ssid.ucSSID, Ssid.uSSIDLength) != Ssid.uSSIDLength)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        RStatus = RSNA_ERR_STATE;
        goto StartFailed;
    }
    if (Sup->Initialised)
    {
        RsnaClear(&Sup->Ctx);
    }
    Sup->Ctx = NewCtx;
    RtlCopyMemory(Sup->OwnMac, Adapter->MacAddress, IEEE80211_ADDR_LEN);
    Sup->Generation++;
    Sup->Initialised = TRUE;
    Sup->KeysInstalled = FALSE;
    Sup->InstallQueued = 0;
    NdisReleaseSpinLock(&Msm->Lock);

    RtlSecureZeroMemory(&NewCtx, sizeof(NewCtx));
    RtlSecureZeroMemory(Credential, sizeof(Credential));
    DPRINT1("NWIFI: supplicant started for SSID len %u\n", Ssid.uSSIDLength);
    return NDIS_STATUS_SUCCESS;

StartFailed:
    RtlSecureZeroMemory(&NewCtx, sizeof(NewCtx));
    RtlSecureZeroMemory(Credential, sizeof(Credential));
    DPRINT1("NWIFI: supplicant initialization failed %d\n", RStatus);
    return (RStatus == RSNA_ERR_PARAM) ? NDIS_STATUS_INVALID_DATA
                                      : NDIS_STATUS_FAILURE;
}

NDIS_STATUS
NwifiSupplicantSeedFirmware(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;
    PDOT11_CIPHER_DEFAULT_KEY_VALUE Value;
    UCHAR Credential[NWIFI_CREDENTIAL_MAX];
    ULONG CredentialLength;
    ULONG ValueSize;
    NDIS_STATUS Status;

    RtlZeroMemory(Credential, sizeof(Credential));
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->PassphraseSet ||
        Sup->PassphraseLen == 0 ||
        Sup->PassphraseLen > sizeof(Credential))
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_INVALID_DATA;
    }
    CredentialLength = Sup->PassphraseLen;
    RtlCopyMemory(Credential, Sup->Passphrase, CredentialLength);
    NdisReleaseSpinLock(&Msm->Lock);

    ValueSize = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) +
                CredentialLength;
    Value = (PDOT11_CIPHER_DEFAULT_KEY_VALUE)NwifiAllocate(ValueSize);
    if (Value == NULL)
    {
        RtlSecureZeroMemory(Credential, sizeof(Credential));
        return NDIS_STATUS_RESOURCES;
    }

    Value->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Value->Header.Revision = DOT11_CIPHER_DEFAULT_KEY_VALUE_REVISION_1;
    Value->Header.Size = sizeof(DOT11_CIPHER_DEFAULT_KEY_VALUE);
    Value->uKeyIndex = 0;
    Value->AlgorithmId = DOT11_CIPHER_ALGO_NONE;
    RtlZeroMemory(Value->MacAddr, IEEE80211_ADDR_LEN);
    Value->bDelete = FALSE;
    Value->bStatic = FALSE;
    Value->usKeyLength = (USHORT)CredentialLength;
    RtlCopyMemory(Value->ucKey, Credential, CredentialLength);

    Status = NwifiProtocolDoRequest(Msm->Adapter,
                                    NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_DEFAULT_KEY,
                                    Value,
                                    ValueSize,
                                    NULL);
    RtlSecureZeroMemory(Value, ValueSize);
    NwifiFree(Value);
    RtlSecureZeroMemory(Credential, sizeof(Credential));
    return Status;
}

VOID
NwifiSupplicantSetApAddr(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(6) PUCHAR ApMac)
{
    PNWIFI_SUPPLICANT Sup;

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup != NULL && Sup->Initialised)
    {
        RsnaSetApAddr(&Sup->Ctx, ApMac);
    }
    NdisReleaseSpinLock(&Msm->Lock);
}

/*
 * Zeroise key material and disarm the session, but do NOT free the object:
 * the RX path may still hold a raw pointer at DISPATCH_LEVEL on another CPU.
 * It is freed only in NwifiSupplicantFree once the data path is quiesced.
 */
VOID
NwifiSupplicantStop(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup != NULL)
    {
        if (Sup->Initialised)
        {
            RsnaClear(&Sup->Ctx);   /* zeroise PMK / PTK / GTK */
        }
        Sup->Generation++;
        Sup->Initialised = FALSE;
        Sup->KeysInstalled = FALSE;
        Sup->InstallQueued = 0;
        RtlSecureZeroMemory(Sup->Passphrase, sizeof(Sup->Passphrase));
        Sup->PassphraseLen = 0;
        Sup->PassphraseSet = FALSE;
    }
    NdisReleaseSpinLock(&Msm->Lock);
}

/* Fully free the supplicant object.  Only safe once the data path is
 * quiesced (NwifiMsmDestroy at unbind). */
VOID
NwifiSupplicantFree(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup;

    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return;
    }
    Msm->Supplicant = NULL;
    if (Sup->Initialised)
    {
        RsnaClear(&Sup->Ctx);
    }
    NdisReleaseSpinLock(&Msm->Lock);

    RtlSecureZeroMemory(Sup->Passphrase, sizeof(Sup->Passphrase));
    RtlSecureZeroMemory(Sup, sizeof(*Sup));
    NwifiFree(Sup);
}

/* ===========================================================================
 *  Install the derived keys once the 4-way handshake completes
 *
 *  Runs at PASSIVE_LEVEL as a rundown-protected one-shot NDIS IO work item.
 * ===========================================================================
 */
static
VOID
NTAPI
NwifiSupplicantInstallKeysWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_     NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNWIFI_INSTALL_KEYS_CONTEXT Context =
        (PNWIFI_INSTALL_KEYS_CONTEXT)WorkItemContext;
    PNWIFI_MSM Msm;
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_ADAPTER Adapter;
    RSNA_KEYS Keys;
    DOT11_CIPHER_ALGORITHM Cipher;
    DOT11_MAC_ADDRESS Bssid;
    NDIS_STATUS Status;

    if (Context == NULL || Context->Msm == NULL)
    {
        return;
    }
    Msm = Context->Msm;
    Adapter = Msm->Adapter;
    RtlZeroMemory(&Keys, sizeof(Keys));
    RtlZeroMemory(Bssid, sizeof(Bssid));

    /* Re-validate under the MSM lock: a disconnect/disassoc may have stopped the
     * supplicant between queueing and running this item. */
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || Sup->Generation != Context->Generation ||
        !Sup->Initialised || Sup->KeysInstalled)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        goto Exit;
    }
    if (RsnaGetKeys(&Sup->Ctx, &Keys) != RSNA_OK)
    {
        Sup->InstallQueued = 0;
        NdisReleaseSpinLock(&Msm->Lock);
        goto Exit;
    }
    RtlCopyMemory(Bssid, Msm->CurrentBssid, IEEE80211_ADDR_LEN);
    NdisReleaseSpinLock(&Msm->Lock);

    Cipher = NwifiDot11CipherFromRsna(Keys.pairwiseCipher);

    /* Pairwise temporal key (PTK TK) for the AP, then the group key (GTK). */
    Status = NwifiInstallPairwiseKey(Adapter, Cipher, Bssid,
                                     (PUCHAR)Keys.tk, (ULONG)Keys.tkLen);

    if (Status == NDIS_STATUS_SUCCESS && Keys.gtkLen != 0)
    {
        Status = NwifiInstallGroupKey(Adapter, Cipher, Keys.gtkKeyId,
                                      (PUCHAR)Keys.gtk, (ULONG)Keys.gtkLen);
    }
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Msm->Lock);
        Sup = Msm->Supplicant;
        if (Sup != NULL && Sup->Generation == Context->Generation)
        {
            Sup->KeysInstalled = FALSE;
            Sup->InstallQueued = 0;
        }
        NdisReleaseSpinLock(&Msm->Lock);
        NwifiQueueNotification(Adapter->InterfaceIndex,
                               NwifiNotifyConnectComplete,
                               Status);
        DPRINT1("NWIFI: WPA2 key installation failed 0x%08X\n", Status);
        goto Exit;
    }

    /* A disconnect can invalidate the session while the blocking key OIDs run. */
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || Sup->Generation != Context->Generation ||
        !Sup->Initialised || Sup->KeysInstalled)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        goto Exit;
    }
    Sup->KeysInstalled = TRUE;
    Sup->InstallQueued = 0;
    NdisReleaseSpinLock(&Msm->Lock);

    Status = NwifiInjectL2Frame(Adapter,
                                Context->PeerMac,
                                EAPOL_ETHERTYPE,
                                Context->Eapol,
                                Context->EapolLength,
                                FALSE);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisAcquireSpinLock(&Msm->Lock);
        Sup = Msm->Supplicant;
        if (Sup != NULL && Sup->Generation == Context->Generation)
            Sup->KeysInstalled = FALSE;
        NdisReleaseSpinLock(&Msm->Lock);
        NwifiQueueNotification(Adapter->InterfaceIndex,
                               NwifiNotifyConnectComplete,
                               Status);
        DPRINT1("NWIFI: WPA2 message 4 send failed 0x%08X\n", Status);
        goto Exit;
    }

    NwifiMsmLinkUp(Msm, TRUE);
    DPRINT1("NWIFI: WPA2 4-way handshake complete; secure link up\n");

Exit:
    RtlSecureZeroMemory(&Keys, sizeof(Keys));
    RtlSecureZeroMemory(Context, sizeof(*Context));
    NwifiFree(Context);
    NwifiMsmCompleteWorkItem(Msm, NdisIoWorkItemHandle);
}

/* ===========================================================================
 *  EAPOL-Key RX feed
 * ===========================================================================
 */
BOOLEAN
NwifiSupplicantRxEapol(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) PUCHAR Eapol,
    _In_ ULONG Length,
    _In_ PUCHAR SrcMac,
    _In_ PUCHAR DstMac)
{
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_INSTALL_KEYS_CONTEXT Context = NULL;
    UCHAR OutFrame[256];
    rsna_size OutLen = sizeof(OutFrame);
    RSNA_STATE NewState = RSNA_STATE_IDLE;
    RSNA_STATUS Error = RSNA_OK;
    ULONG Generation = 0;
    BOOLEAN QueueInstall = FALSE;
    BOOLEAN DeferReply = FALSE;
    BOOLEAN ReplyUnencrypted = TRUE;
    NDIS_STATUS SendStatus;

    UNREFERENCED_PARAMETER(DstMac);

    RtlZeroMemory(OutFrame, sizeof(OutFrame));

    /* No supplicant armed (open network, or PSK not seeded): consume the EAPOL
     * frame anyway so it never leaks up to TCP/IP as bogus Ethernet. */
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->Initialised)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return TRUE;
    }

    /* The AP (authenticator) MAC is the frame source. */
    RsnaSetApAddr(&Sup->Ctx, SrcMac);

    NewState = RsnaRxEapol(&Sup->Ctx, Eapol, Length, OutFrame, &OutLen);
    Error = RsnaLastError(&Sup->Ctx);
    if (NewState == RSNA_STATE_COMPLETED &&
        !Sup->KeysInstalled && Sup->InstallQueued == 0)
    {
        Sup->InstallQueued = 1;
        Generation = Sup->Generation;
        QueueInstall = TRUE;
    }
    DeferReply = (NewState == RSNA_STATE_COMPLETED &&
                  !Sup->KeysInstalled);
    if (NewState == RSNA_STATE_COMPLETED && Sup->KeysInstalled)
        ReplyUnencrypted = FALSE;
    NdisReleaseSpinLock(&Msm->Lock);

    DPRINT1("NWIFI: sup eapol in %lu out %lu state %d err %d\n",
            Length, (ULONG)OutLen, NewState, Error);

    /* Message 2 is sent before a mapping key exists. Message 4 is held for the
     * PASSIVE_LEVEL worker, which installs PTK/GTK first and sends it protected. */
    if (OutLen != 0 && !DeferReply)
    {
        SendStatus = NwifiInjectL2Frame(Msm->Adapter,
                                        SrcMac,
                                        EAPOL_ETHERTYPE,
                                        OutFrame,
                                        (ULONG)OutLen,
                                        ReplyUnencrypted);
        if (SendStatus != NDIS_STATUS_SUCCESS)
        {
            NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                                   NwifiNotifyConnectComplete,
                                   SendStatus);
        }
    }

    if (QueueInstall)
    {
        Context = (PNWIFI_INSTALL_KEYS_CONTEXT)
            NwifiAllocate(sizeof(*Context));
        if (Context != NULL)
        {
            if (OutLen == 0 || OutLen > sizeof(Context->Eapol))
            {
                NwifiFree(Context);
                Context = NULL;
            }
        }
        if (Context != NULL)
        {
            Context->Msm = Msm;
            Context->Generation = Generation;
            RtlCopyMemory(Context->PeerMac, SrcMac, sizeof(Context->PeerMac));
            Context->EapolLength = (ULONG)OutLen;
            RtlCopyMemory(Context->Eapol, OutFrame, Context->EapolLength);
        }
        if (Context == NULL ||
            !NwifiMsmQueueWorkItem(Msm,
                                   NwifiSupplicantInstallKeysWorker,
                                   Context))
        {
            NwifiFree(Context);
            NdisAcquireSpinLock(&Msm->Lock);
            Sup = Msm->Supplicant;
            if (Sup != NULL && Sup->Generation == Generation)
            {
                Sup->InstallQueued = 0;
            }
            NdisReleaseSpinLock(&Msm->Lock);
            NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                                   NwifiNotifyConnectComplete,
                                   NDIS_STATUS_RESOURCES);
        }
    }
    else if (NewState == RSNA_STATE_FAILED)
    {
        DPRINT1("NWIFI: 4-way handshake failed (err %d)\n", Error);
        NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                               NwifiNotifyConnectComplete, NDIS_STATUS_FAILURE);
    }

    RtlSecureZeroMemory(OutFrame, sizeof(OutFrame));
    return TRUE;        /* EAPOL frames are always consumed by the supplicant. */
}
