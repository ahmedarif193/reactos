/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Receive Side Scaling (RSS) support
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements RSS for the 82574L:
 *   - E1000InitializeRss - Initialize RSS hardware
 *   - E1000ConfigureRss - Configure RSS parameters
 *   - E1000DisableRss - Disable RSS
 *
 * The 82574L supports 2 RX queues with Toeplitz hash.
 */

#include "e1000.h"

/* ============================================================================
 * E1000InitializeRss - Initialize RSS subsystem
 *
 * Called during adapter initialization to setup RSS structures.
 * ============================================================================ */

NDIS_STATUS
E1000InitializeRss(
    _In_ PE1000_ADAPTER Adapter
    )
{
    /* RSS is only supported on PCIe devices like 82574L */
    if (!Adapter->IsPCIe)
    {
        Adapter->RssConfig.Enabled = FALSE;
        Adapter->RssConfig.QueueCount = 1;
        return NDIS_STATUS_SUCCESS;
    }

    /* Initialize RSS configuration */
    NdisZeroMemory(&Adapter->RssConfig, sizeof(E1000_RSS_CONFIG));

    Adapter->RssConfig.Enabled = FALSE;  /* Will be enabled via OID */
    Adapter->RssConfig.QueueCount = 2;   /* 82574L supports 2 RX queues */
    Adapter->RssConfig.HashTypes = NDIS_HASH_TYPE_TCP_IPV4;
    Adapter->RssConfig.HashFunction = NDIS_HASH_FUNCTION_TOEPLITZ;

    /* Initialize default hash key (Microsoft-recommended Toeplitz key) */
    {
        static const UCHAR DefaultHashKey[40] = {
            0x6D, 0x5A, 0x56, 0xDA, 0x25, 0x5B, 0x0E, 0xC2,
            0x41, 0x67, 0x25, 0x3D, 0x43, 0xA3, 0x8F, 0xB0,
            0xD0, 0xCA, 0x2B, 0xCB, 0xAE, 0x7B, 0x30, 0xB4,
            0x77, 0xCB, 0x2D, 0xA3, 0x80, 0x30, 0xF2, 0x0C,
            0x6A, 0x42, 0xB7, 0x3B, 0xBE, 0xAC, 0x01, 0xFA
        };
        NdisMoveMemory(Adapter->RssConfig.HashKey, DefaultHashKey, sizeof(DefaultHashKey));
    }

    /* Initialize default indirection table (round-robin between queues) */
    {
        ULONG i;
        for (i = 0; i < E1000_RETA_SIZE * sizeof(ULONG); i++)
        {
            Adapter->RssConfig.IndirectionTable[i] = (UCHAR)(i % Adapter->RssConfig.QueueCount);
        }
    }

    DbgPrint("E1000: RSS initialized - %u queues available\n", Adapter->RssConfig.QueueCount);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000ConfigureRss - Configure RSS parameters from OID
 *
 * Programs the hardware RSS registers based on received parameters.
 * ============================================================================ */

NDIS_STATUS
E1000ConfigureRss(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_RECEIVE_SCALE_PARAMETERS RssParams,
    _In_ ULONG Size
    )
{
    ULONG MrqcValue = 0;
    ULONG i;
    PUCHAR HashKey;
    PUCHAR IndirectionTable;
    ULONG HashKeySize;
    ULONG IndirectionTableSize;

    /* Check if RSS is supported */
    if (!Adapter->IsPCIe)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Validate parameters */
    if (RssParams == NULL || Size < sizeof(NDIS_RECEIVE_SCALE_PARAMETERS))
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Check for disable request */
    if (!(RssParams->Flags & NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED))
    {
        Adapter->RssConfig.BaseCpuNumber = RssParams->BaseCpuNumber;
    }

    /* Handle hash key */
    if (RssParams->HashSecretKeySize > 0 &&
        RssParams->HashSecretKeyOffset + RssParams->HashSecretKeySize <= Size)
    {
        HashKey = (PUCHAR)RssParams + RssParams->HashSecretKeyOffset;
        HashKeySize = RssParams->HashSecretKeySize;

        if (HashKeySize > sizeof(Adapter->RssConfig.HashKey))
        {
            HashKeySize = sizeof(Adapter->RssConfig.HashKey);
        }

        NdisMoveMemory(Adapter->RssConfig.HashKey, HashKey, HashKeySize);
    }

    /* Handle indirection table */
    if (RssParams->IndirectionTableSize > 0 &&
        RssParams->IndirectionTableOffset + RssParams->IndirectionTableSize <= Size)
    {
        IndirectionTable = (PUCHAR)RssParams + RssParams->IndirectionTableOffset;
        IndirectionTableSize = RssParams->IndirectionTableSize;

        if (IndirectionTableSize > sizeof(Adapter->RssConfig.IndirectionTable))
        {
            IndirectionTableSize = sizeof(Adapter->RssConfig.IndirectionTable);
        }

        NdisMoveMemory(Adapter->RssConfig.IndirectionTable, IndirectionTable, IndirectionTableSize);
    }

    /* Save hash types */
    Adapter->RssConfig.HashTypes = RssParams->HashInformation & NDIS_HASH_TYPE_MASK;
    Adapter->RssConfig.HashFunction = RssParams->HashInformation & ~NDIS_HASH_TYPE_MASK;

    /* Check if RSS should be enabled or disabled */
    if (RssParams->Flags & NDIS_RSS_PARAM_FLAG_DISABLE_RSS)
    {
        E1000DisableRss(Adapter);
        return NDIS_STATUS_SUCCESS;
    }

    /* Configure hardware */
    if (Adapter->IoBase != NULL)
    {
        /* Program hash key (RSSRK registers) */
        for (i = 0; i < E1000_RSSRK_SIZE; i++)
        {
            ULONG KeyWord;
            KeyWord = ((ULONG)Adapter->RssConfig.HashKey[i * 4 + 0]) |
                      ((ULONG)Adapter->RssConfig.HashKey[i * 4 + 1] << 8) |
                      ((ULONG)Adapter->RssConfig.HashKey[i * 4 + 2] << 16) |
                      ((ULONG)Adapter->RssConfig.HashKey[i * 4 + 3] << 24);
            E1000_WRITE_REG_ARRAY(Adapter, E1000_REG_RSSRK, i, KeyWord);
        }

        /* Program indirection table (RETA registers) */
        for (i = 0; i < E1000_RETA_SIZE; i++)
        {
            ULONG RetaValue;
            /* Each RETA entry is 4 bytes, each byte selects a queue (0 or 1 for 82574L) */
            RetaValue = ((ULONG)(Adapter->RssConfig.IndirectionTable[i * 4 + 0] & 0x1)) |
                        ((ULONG)(Adapter->RssConfig.IndirectionTable[i * 4 + 1] & 0x1) << 8) |
                        ((ULONG)(Adapter->RssConfig.IndirectionTable[i * 4 + 2] & 0x1) << 16) |
                        ((ULONG)(Adapter->RssConfig.IndirectionTable[i * 4 + 3] & 0x1) << 24);
            E1000_WRITE_REG_ARRAY(Adapter, E1000_REG_RETA, i, RetaValue);
        }

        /* Program MRQC (Multiple Receive Queue Control) */
        MrqcValue = 0;

        /* Enable RSS */
        MrqcValue |= E1000_MRQC_RSS_ENABLE;

        /* Set hash function (Toeplitz) */
        /* MrqcValue |= E1000_MRQC_RSS_FIELD_TOEPLITZ; - default */

        /* Enable hash types */
        if (Adapter->RssConfig.HashTypes & NDIS_HASH_TYPE_TCP_IPV4)
        {
            MrqcValue |= E1000_MRQC_RSS_FIELD_IPV4_TCP;
        }
        if (Adapter->RssConfig.HashTypes & NDIS_HASH_TYPE_TCP_IPV6)
        {
            MrqcValue |= E1000_MRQC_RSS_FIELD_IPV6_TCP;
        }
        if (Adapter->RssConfig.HashTypes & NDIS_HASH_TYPE_TCP_IPV6_EX)
        {
            MrqcValue |= E1000_MRQC_RSS_FIELD_IPV6_TCP_EX;
        }

        E1000_WRITE_REG(Adapter, E1000_REG_MRQC, MrqcValue);

        Adapter->RssConfig.Enabled = TRUE;
    }

    DbgPrint("E1000: RSS configured - HashTypes=0x%08x, Queues=%u\n",
             Adapter->RssConfig.HashTypes, Adapter->RssConfig.QueueCount);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000DisableRss - Disable RSS
 *
 * Turns off RSS and routes all packets to queue 0.
 * ============================================================================ */

VOID
E1000DisableRss(
    _In_ PE1000_ADAPTER Adapter
    )
{
    if (Adapter->IoBase == NULL)
    {
        return;
    }

    /* Disable RSS in MRQC */
    E1000_WRITE_REG(Adapter, E1000_REG_MRQC, 0);

    Adapter->RssConfig.Enabled = FALSE;

    DbgPrint("E1000: RSS disabled\n");
}
