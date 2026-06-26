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

/* Per-interface supplicant session.  Keeps a copy of the seeded passphrase
 * so a session can be re-initialised per SSID (the PMK is SSID-dependent). */
typedef struct _NWIFI_SUPPLICANT
{
    RSNA_CTX  Ctx;
    BOOLEAN   Initialised;          /* RsnaInit done (PMK derived)              */
    BOOLEAN   PassphraseSet;
    UCHAR     Passphrase[RSNA_PASSPHRASE_MAX + 1];
    ULONG     PassphraseLen;
    UCHAR     OwnMac[IEEE80211_ADDR_LEN];
    BOOLEAN   KeysInstalled;

    /* The handshake completes in the RX path (possibly DISPATCH_LEVEL), but
     * key install is a blocking OID needing PASSIVE_LEVEL, so it is deferred
     * to this NDIS IO work item. */
    NDIS_HANDLE InstallWorkItem;
    volatile LONG InstallQueued;
    PNWIFI_MSM  Msm;                /* back-pointer for the work item           */
} NWIFI_SUPPLICANT;

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
    ULONG ValueSize;
    ULONG TotalSize;
    NDIS_STATUS Status;

    if (KeyLength == 0 || KeyLength > 64)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    /* DOT11_CIPHER_KEY_MAPPING_KEY_VALUE has a 1-byte ucKey[] placeholder. */
    ValueSize = FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey) + KeyLength;
    TotalSize = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + ValueSize;

    ByteArray = (PDOT11_BYTE_ARRAY)NwifiAllocate(TotalSize);
    if (ByteArray == NULL)
    {
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
    Value->usKeyLength = (USHORT)KeyLength;
    RtlCopyMemory(Value->ucKey, Key, KeyLength);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
                                    ByteArray, TotalSize, NULL);
    NwifiFree(ByteArray);
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
    ULONG ValueSize;
    ULONG DefaultKeyId;
    NDIS_STATUS Status;

    if (KeyLength == 0 || KeyLength > 64)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    ValueSize = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey) + KeyLength;
    Value = (PDOT11_CIPHER_DEFAULT_KEY_VALUE)NwifiAllocate(ValueSize);
    if (Value == NULL)
    {
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
    Value->usKeyLength = (USHORT)KeyLength;
    RtlCopyMemory(Value->ucKey, Key, KeyLength);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CIPHER_DEFAULT_KEY,
                                    Value, ValueSize, NULL);
    NwifiFree(Value);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        DefaultKeyId = KeyId;
        NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                               OID_DOT11_CIPHER_DEFAULT_KEY_ID,
                               &DefaultKeyId, sizeof(DefaultKeyId), NULL);
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
    PNWIFI_SUPPLICANT Sup = Msm->Supplicant;

    if (Length == 0 || Length > RSNA_PASSPHRASE_MAX)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    /* The session may not exist yet (SET_KEY can precede CONNECT); allocate a
     * placeholder to stash the passphrase until the connect arms it. */
    if (Sup == NULL)
    {
        Sup = (PNWIFI_SUPPLICANT)NwifiAllocate(sizeof(NWIFI_SUPPLICANT));
        if (Sup == NULL)
        {
            return NDIS_STATUS_RESOURCES;
        }
        Msm->Supplicant = Sup;
    }

    RtlCopyMemory(Sup->Passphrase, Passphrase, Length);
    Sup->Passphrase[Length] = 0;
    Sup->PassphraseLen = Length;
    Sup->PassphraseSet = TRUE;
    DPRINT1("NWIFI: supplicant passphrase seeded (%u bytes)\n", Length);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NwifiSupplicantStart(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup = Msm->Supplicant;
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    PDOT11_SSID Ssid = &Msm->Connect.Ssid;
    RSNA_STATUS RStatus;

    /* A passphrase must have been seeded (IOCTL_NWIFI_SET_KEY before connect). */
    if (Sup == NULL || !Sup->PassphraseSet)
    {
        return NDIS_STATUS_FAILURE;
    }

    if (Ssid->uSSIDLength == 0 || Ssid->uSSIDLength > RSNA_SSID_MAX_LEN)
    {
        return NDIS_STATUS_INVALID_DATA;
    }

    /* Derive the PMK from passphrase + SSID (PBKDF2-SHA1, 4096 iterations). */
    RStatus = RsnaInit(&Sup->Ctx,
                       Ssid->ucSSID, Ssid->uSSIDLength,
                       (const char *)Sup->Passphrase, Sup->PassphraseLen);
    if (RStatus != RSNA_OK)
    {
        DPRINT1("NWIFI: RsnaInit failed %d\n", RStatus);
        return NDIS_STATUS_FAILURE;
    }

    {
        static const UCHAR RsnIe[22] = {
            0x30, 0x14, 0x01, 0x00,
            0x00, 0x0F, 0xAC, 0x04,
            0x01, 0x00, 0x00, 0x0F, 0xAC, 0x04,
            0x01, 0x00, 0x00, 0x0F, 0xAC, 0x02,
            0x00, 0x00
        };
        RsnaSetRsnIe(&Sup->Ctx, RsnIe, sizeof(RsnIe));
    }

    /* The supplicant is the station (SPA == our MAC). */
    RtlCopyMemory(Sup->OwnMac, Adapter->MacAddress, IEEE80211_ADDR_LEN);
    RsnaSetStaAddr(&Sup->Ctx, Sup->OwnMac);

    /* Seed the SNonce PRNG with whatever entropy we have (MAC + tick count). */
    {
        UCHAR Seed[IEEE80211_ADDR_LEN + sizeof(ULONG64)];
        ULONG64 Tick = (ULONG64)KeQueryInterruptTime();
        RtlCopyMemory(Seed, Adapter->MacAddress, IEEE80211_ADDR_LEN);
        RtlCopyMemory(Seed + IEEE80211_ADDR_LEN, &Tick, sizeof(Tick));
        RsnaSeed(&Sup->Ctx, Seed, sizeof(Seed));
    }

    /* Allocate the deferred key-install work item (associated with the upper
     * miniport handle so NDIS scopes it to this adapter). */
    if (Sup->InstallWorkItem == NULL && Adapter->MiniportAdapterHandle != NULL)
    {
        Sup->InstallWorkItem =
            NdisAllocateIoWorkItem(Adapter->MiniportAdapterHandle);
    }
    Sup->Msm = Msm;

    Sup->Initialised = TRUE;
    Sup->KeysInstalled = FALSE;
    Sup->InstallQueued = 0;
    DPRINT1("NWIFI: supplicant started for SSID len %u\n", Ssid->uSSIDLength);
    return NDIS_STATUS_SUCCESS;
}

VOID
NwifiSupplicantSetApAddr(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(6) PUCHAR ApMac)
{
    PNWIFI_SUPPLICANT Sup = Msm->Supplicant;

    if (Sup != NULL && Sup->Initialised)
    {
        RsnaSetApAddr(&Sup->Ctx, ApMac);
    }
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
        Sup->Initialised = FALSE;
        Sup->KeysInstalled = FALSE;
        /* Keep PassphraseSet so a reconnect to the same SSID can re-derive. */
    }
    NdisReleaseSpinLock(&Msm->Lock);
}

/* Fully free the supplicant object.  Only safe once the data path is
 * quiesced (NwifiMsmDestroy at unbind). */
VOID
NwifiSupplicantFree(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_SUPPLICANT Sup = Msm->Supplicant;

    if (Sup == NULL)
    {
        return;
    }

    if (Sup->InstallWorkItem != NULL)
    {
        NdisFreeIoWorkItem(Sup->InstallWorkItem);
        Sup->InstallWorkItem = NULL;
    }
    if (Sup->Initialised)
    {
        RsnaClear(&Sup->Ctx);
    }
    RtlSecureZeroMemory(Sup->Passphrase, sizeof(Sup->Passphrase));
    Msm->Supplicant = NULL;
    NwifiFree(Sup);
}

/* ===========================================================================
 *  Install the derived keys once the 4-way handshake completes
 *
 *  Runs at PASSIVE_LEVEL as a deferred NDIS IO work item; see the
 *  InstallWorkItem comment in NWIFI_SUPPLICANT.
 * ===========================================================================
 */
static
VOID
NTAPI
NwifiSupplicantInstallKeysWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_     NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNWIFI_MSM Msm = (PNWIFI_MSM)WorkItemContext;
    PNWIFI_SUPPLICANT Sup;
    PNWIFI_ADAPTER Adapter;
    RSNA_KEYS Keys;
    DOT11_CIPHER_ALGORITHM Cipher;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    if (Msm == NULL)
    {
        return;
    }
    Adapter = Msm->Adapter;

    /* Re-validate under the MSM lock: a disconnect/disassoc may have stopped the
     * supplicant between queueing and running this item. */
    NdisAcquireSpinLock(&Msm->Lock);
    Sup = Msm->Supplicant;
    if (Sup == NULL || !Sup->Initialised || Sup->KeysInstalled)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return;
    }
    if (RsnaGetKeys(&Sup->Ctx, &Keys) != RSNA_OK)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return;
    }
    Sup->KeysInstalled = TRUE;
    NdisReleaseSpinLock(&Msm->Lock);

    Cipher = NwifiDot11CipherFromRsna(Keys.pairwiseCipher);

    /* Pairwise temporal key (PTK TK) for the AP, then the group key (GTK). */
    NwifiInstallPairwiseKey(Adapter, Cipher, Msm->CurrentBssid,
                            (PUCHAR)Keys.tk, (ULONG)Keys.tkLen);

    if (Keys.gtkLen != 0)
    {
        NwifiInstallGroupKey(Adapter, Cipher, Keys.gtkKeyId,
                             (PUCHAR)Keys.gtk, (ULONG)Keys.gtkLen);
    }

    /* Handshake done and keys installed: bring the secure link up. */
    NdisAcquireSpinLock(&Msm->Lock);
    Msm->State = NwifiMsmConnectedSecure;
    RtlCopyMemory(Adapter->Bssid, Msm->CurrentBssid, IEEE80211_ADDR_LEN);
    Adapter->Ssid = Msm->CurrentSsid;
    NdisReleaseSpinLock(&Msm->Lock);

    NdisAcquireSpinLock(&Adapter->DataLock);
    Adapter->Associated = TRUE;
    NdisReleaseSpinLock(&Adapter->DataLock);

    NwifiIndicateLinkState(Adapter, TRUE);
    NwifiQueueNotification(Adapter->InterfaceIndex, NwifiNotifyConnectComplete,
                           NDIS_STATUS_SUCCESS);
    NwifiQueueNotification(Adapter->InterfaceIndex, NwifiNotifyLinkUp,
                           NDIS_STATUS_SUCCESS);
    DPRINT1("NWIFI: WPA2 4-way handshake complete; secure link up\n");
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
    PNWIFI_SUPPLICANT Sup = Msm->Supplicant;
    UCHAR OutFrame[256];
    rsna_size OutLen = sizeof(OutFrame);
    RSNA_STATE NewState;

    UNREFERENCED_PARAMETER(DstMac);

    DPRINT1("NWIFI: sup rx eapol len %lu sup %p\n", Length, Sup);

    /* No supplicant armed (open network, or PSK not seeded): consume the EAPOL
     * frame anyway so it never leaks up to TCP/IP as bogus Ethernet. */
    if (Sup == NULL || !Sup->Initialised)
    {
        return TRUE;
    }

    /* The AP (authenticator) MAC is the frame source. */
    RsnaSetApAddr(&Sup->Ctx, SrcMac);

    NewState = RsnaRxEapol(&Sup->Ctx, Eapol, Length, OutFrame, &OutLen);
    DPRINT1("NWIFI: sup eapol in %lu out %lu state %d err %d\n", Length, (ULONG)OutLen, NewState, RsnaLastError(&Sup->Ctx));

    /* Transmit any reply (msg2 after msg1, msg4 after msg3) back to the AP as
     * an EAPOL frame, sent in the clear (the link is not yet encrypted). */
    if (OutLen != 0)
    {
        NwifiInjectL2Frame(Msm->Adapter, SrcMac, EAPOL_ETHERTYPE,
                           OutFrame, (ULONG)OutLen, TRUE);
    }

    if (NewState == RSNA_STATE_COMPLETED && !Sup->KeysInstalled)
    {
        /* Install the derived keys at PASSIVE_LEVEL via the work item;
         * queue exactly once. */
        if (Sup->InstallWorkItem != NULL &&
            InterlockedCompareExchange(&Sup->InstallQueued, 1, 0) == 0)
        {
            NdisQueueIoWorkItem(Sup->InstallWorkItem,
                                NwifiSupplicantInstallKeysWorker, Msm);
        }
    }
    else if (NewState == RSNA_STATE_FAILED)
    {
        DPRINT1("NWIFI: 4-way handshake failed (err %d)\n",
                RsnaLastError(&Sup->Ctx));
        NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                               NwifiNotifyConnectComplete, NDIS_STATUS_FAILURE);
    }

    return TRUE;        /* EAPOL frames are always consumed by the supplicant. */
}
