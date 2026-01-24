/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Pause/Restart and Reset handlers
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements adapter control functions:
 *   - E1000MiniportPause - Pause data path
 *   - E1000MiniportRestart - Resume data path
 *   - E1000MiniportResetEx - Reset adapter
 *   - E1000MiniportCheckForHangEx - Hung check
 */

#include "e1000.h"

/* ============================================================================
 * E1000MiniportPause - Pause the miniport adapter
 *
 * Called by NDIS when the data path needs to be temporarily stopped.
 * Must complete all pending sends and stop indicating receives.
 * ============================================================================ */

NDIS_STATUS
NTAPI
E1000MiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    ULONG i;

    UNREFERENCED_PARAMETER(PauseParameters);

    DPRINT1("E1000: MiniportPause\n");

    /* Set pausing flag */
    InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_PAUSING);

    /* Disable interrupts to stop new work */
    E1000DisableInterrupts(Adapter);

    /* Process any pending TX completions */
    for (i = 0; i < Adapter->TxQueueCount; i++)
    {
        E1000ProcessTxCompletions(&Adapter->TxQueues[i]);
    }

    /* Wait for pending operations to complete */
    /* In a real implementation, we would wait for in-flight operations */

    /* Stop transmit engine */
    if (Adapter->IoBase != NULL)
    {
        E1000_WRITE_REG(Adapter, E1000_REG_TCTL, 0);
    }

    /* Clear pausing, set paused */
    InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_ADAPTER_PAUSING);
    InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_PAUSED);

    DPRINT1("E1000: MiniportPause complete\n");

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000MiniportRestart - Restart the miniport adapter
 *
 * Called by NDIS to resume the data path after a pause.
 * ============================================================================ */

NDIS_STATUS
NTAPI
E1000MiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    ULONG TctlValue;

    UNREFERENCED_PARAMETER(RestartParameters);

    DPRINT1("E1000: MiniportRestart\n");

    /* Check that we were paused */
    if (!(InterlockedCompareExchange(&Adapter->Flags, 0, 0) & E1000_FLAG_ADAPTER_PAUSED))
    {
        DPRINT("E1000: MiniportRestart called but not paused\n");
        return NDIS_STATUS_FAILURE;
    }

    /* Re-enable transmit engine */
    if (Adapter->IoBase != NULL)
    {
        TctlValue = E1000_TCTL_EN |          /* Enable TX */
                    E1000_TCTL_PSP |         /* Pad short packets */
                    E1000_TCTL_CT_IEEE |     /* Collision threshold */
                    E1000_TCTL_COLD_FD;      /* Collision distance (full duplex) */

        E1000_WRITE_REG(Adapter, E1000_REG_TCTL, TctlValue);
    }

    /* Clear paused flag */
    InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_ADAPTER_PAUSED);

    /* Re-enable interrupts */
    E1000EnableInterrupts(Adapter);

    DPRINT1("E1000: MiniportRestart complete\n");

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000MiniportResetEx - Reset the miniport adapter
 *
 * Called by NDIS when the adapter needs to be reset (usually due to error).
 * ============================================================================ */

NDIS_STATUS
NTAPI
E1000MiniportResetEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status;
    ULONG i;

    DPRINT1("E1000: MiniportResetEx\n");

    /* Set reset flag */
    InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_RESET);

    /* Disable interrupts */
    E1000DisableInterrupts(Adapter);

    /* Complete pending sends with error */
    for (i = 0; i < Adapter->TxQueueCount; i++)
    {
        PE1000_TX_QUEUE TxQueue = &Adapter->TxQueues[i];
        ULONG j;

        NdisAcquireSpinLock(&TxQueue->Lock);

        for (j = 0; j < TxQueue->Count; j++)
        {
            PE1000_TX_BUFFER TxBuffer = &TxQueue->Buffers[j];

            if (TxBuffer->NetBufferList != NULL)
            {
                NET_BUFFER_LIST_STATUS(TxBuffer->NetBufferList) = NDIS_STATUS_RESET_IN_PROGRESS;
                NdisMSendNetBufferListsComplete(
                    Adapter->MiniportAdapterHandle,
                    TxBuffer->NetBufferList,
                    0
                    );
                TxBuffer->NetBufferList = NULL;
                TxBuffer->Flags = 0;
            }
        }

        /* Reset queue state */
        TxQueue->Head = 0;
        TxQueue->Tail = 0;
        TxQueue->Available = TxQueue->Count;

        NdisReleaseSpinLock(&TxQueue->Lock);
    }

    /* Reset hardware */
    Status = E1000ResetHardware(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: MiniportResetEx - Hardware reset failed 0x%08x\n", Status);
        InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_ADAPTER_RESET);
        return Status;
    }

    /* Restore link */
    E1000SetupLink(Adapter);

    /* Restore packet filter */
    E1000SetPacketFilter(Adapter, Adapter->PacketFilter);

    /* Restore multicast list */
    E1000SetMulticastList(Adapter, Adapter->MulticastList[0], Adapter->MulticastCount);

    /* Reinitialize descriptor rings */
    for (i = 0; i < Adapter->TxQueueCount; i++)
    {
        PE1000_TX_QUEUE TxQueue = &Adapter->TxQueues[i];

        /* Clear and reprogram TX descriptors */
        NdisZeroMemory((PVOID)TxQueue->Descriptors,
                       sizeof(E1000_TRANSMIT_DESCRIPTOR) * TxQueue->Count);

        if (i == 0)
        {
            E1000_WRITE_REG(Adapter, E1000_REG_TDBAL, (ULONG)TxQueue->DescriptorsPa.LowPart);
            E1000_WRITE_REG(Adapter, E1000_REG_TDBAH, (ULONG)TxQueue->DescriptorsPa.HighPart);
            E1000_WRITE_REG(Adapter, E1000_REG_TDLEN,
                            sizeof(E1000_TRANSMIT_DESCRIPTOR) * TxQueue->Count);
            E1000_WRITE_REG(Adapter, E1000_REG_TDH, 0);
            E1000_WRITE_REG(Adapter, E1000_REG_TDT, 0);
        }
    }

    for (i = 0; i < Adapter->RxQueueCount; i++)
    {
        PE1000_RX_QUEUE RxQueue = &Adapter->RxQueues[i];

        /* Reprogram RX descriptors */
        if (i == 0)
        {
            E1000_WRITE_REG(Adapter, E1000_REG_RDBAL, (ULONG)RxQueue->DescriptorsPa.LowPart);
            E1000_WRITE_REG(Adapter, E1000_REG_RDBAH, (ULONG)RxQueue->DescriptorsPa.HighPart);
            E1000_WRITE_REG(Adapter, E1000_REG_RDLEN,
                            sizeof(E1000_RECEIVE_DESCRIPTOR) * RxQueue->Count);
            E1000_WRITE_REG(Adapter, E1000_REG_RDH, 0);
            E1000_WRITE_REG(Adapter, E1000_REG_RDT, RxQueue->Count - 1);
        }
    }

    /* Clear reset flag */
    InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_ADAPTER_RESET);

    /* Re-enable interrupts */
    E1000EnableInterrupts(Adapter);

    /* MAC address will be preserved */
    *AddressingReset = FALSE;

    DPRINT1("E1000: MiniportResetEx complete\n");

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000MiniportCheckForHangEx - Check if adapter is hung
 *
 * Called periodically by NDIS to check adapter health.
 * Return TRUE to trigger a reset.
 * ============================================================================ */

BOOLEAN
NTAPI
E1000MiniportCheckForHangEx(
    _In_ NDIS_HANDLE MiniportAdapterContext
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    PE1000_TX_QUEUE TxQueue;
    LARGE_INTEGER CurrentTime;
    LARGE_INTEGER Timeout;
    BOOLEAN Hung = FALSE;

    /* Don't check if paused or resetting */
    if (InterlockedCompareExchange(&Adapter->Flags, 0, 0) &
        (E1000_FLAG_ADAPTER_PAUSED | E1000_FLAG_ADAPTER_RESET))
    {
        return FALSE;
    }

    /* Check for TX stuck condition */
    TxQueue = &Adapter->TxQueues[0];

    if (TxQueue->Head != TxQueue->Tail)
    {
        /* There are pending transmits - check if too old */
        KeQuerySystemTime(&CurrentTime);

        /* Timeout after 10 seconds of no progress */
        Timeout.QuadPart = TxQueue->LastCompletionTime.QuadPart + (10 * 10000000LL);

        if (CurrentTime.QuadPart > Timeout.QuadPart)
        {
            DPRINT1("E1000: TX appears hung - Head=%u, Tail=%u\n",
                     TxQueue->Head, TxQueue->Tail);
            Hung = TRUE;
        }
    }

    /* Check hardware status */
    if (Adapter->IoBase != NULL && !Hung)
    {
        ULONG StatusReg;

        StatusReg = E1000_READ_REG(Adapter, E1000_REG_STATUS);

        /* Check for PHY reset stuck */
        if (StatusReg == 0xFFFFFFFF)
        {
            DPRINT1("E1000: Hardware not responding (STATUS=0xFFFFFFFF)\n");
            Hung = TRUE;
        }
    }

    return Hung;
}
