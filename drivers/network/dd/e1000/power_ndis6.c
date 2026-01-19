/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Power management
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements power management:
 *   - E1000SetPower - Handle power state transitions
 *   - E1000ConfigureWakeOnLan - Setup Wake-on-LAN
 */

#include "e1000.h"

/* ============================================================================
 * E1000SetPower - Handle power state transition
 *
 * Transitions the adapter to the specified power state.
 * ============================================================================ */

NDIS_STATUS
E1000SetPower(
    _In_ PE1000_ADAPTER Adapter,
    _In_ NDIS_DEVICE_POWER_STATE PowerState
    )
{
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    DbgPrint("E1000: SetPower - Transitioning from D%d to D%d\n",
             Adapter->NdisPowerState - NdisDeviceStateD0,
             PowerState - NdisDeviceStateD0);

    if (PowerState == Adapter->NdisPowerState)
    {
        /* Already in requested state */
        return NDIS_STATUS_SUCCESS;
    }

    switch (PowerState)
    {
        case NdisDeviceStateD0:
            /* Wake up - restore full operation */
            Status = E1000PowerUp(Adapter);
            break;

        case NdisDeviceStateD1:
        case NdisDeviceStateD2:
            /* Light/Deep sleep - reduce power but keep wake capability */
            Status = E1000PowerDown(Adapter, PowerState);
            break;

        case NdisDeviceStateD3:
            /* Off - configure wake-on-LAN if enabled, then power down */
            if (Adapter->WakeOnMagicPacket || Adapter->WakeOnLinkChange)
            {
                E1000ConfigureWakeOnLan(Adapter);
            }
            Status = E1000PowerDown(Adapter, PowerState);
            break;

        default:
            Status = NDIS_STATUS_INVALID_PARAMETER;
            break;
    }

    if (Status == NDIS_STATUS_SUCCESS)
    {
        Adapter->NdisPowerState = PowerState;
        Adapter->CurrentPowerState = (E1000_POWER_STATE)(PowerState - NdisDeviceStateD0);
    }

    return Status;
}


/* ============================================================================
 * E1000PowerUp - Restore adapter to D0 state
 * ============================================================================ */

NDIS_STATUS
E1000PowerUp(
    _In_ PE1000_ADAPTER Adapter
    )
{
    NDIS_STATUS Status;

    DbgPrint("E1000: PowerUp - Restoring to D0\n");

    /* Reset and reinitialize hardware */
    Status = E1000ResetHardware(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("E1000: PowerUp failed - Hardware reset error 0x%08x\n", Status);
        return Status;
    }

    /* Restore link */
    Status = E1000SetupLink(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("E1000: PowerUp warning - Link setup returned 0x%08x\n", Status);
        /* Non-fatal - link may come up later */
    }

    /* Restore packet filter */
    E1000SetPacketFilter(Adapter, Adapter->PacketFilter);

    /* Restore multicast list */
    E1000SetMulticastList(Adapter, Adapter->MulticastList[0], Adapter->MulticastCount);

    /* Re-enable interrupts */
    E1000EnableInterrupts(Adapter);

    /* Mark as started */
    InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_STARTED);

    DbgPrint("E1000: PowerUp complete\n");

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000PowerDown - Transition adapter to low power state
 * ============================================================================ */

NDIS_STATUS
E1000PowerDown(
    _In_ PE1000_ADAPTER Adapter,
    _In_ NDIS_DEVICE_POWER_STATE PowerState
    )
{
    DbgPrint("E1000: PowerDown - Entering D%d\n", PowerState - NdisDeviceStateD0);

    /* Mark as not started */
    InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_ADAPTER_STARTED);

    /* Disable interrupts */
    E1000DisableInterrupts(Adapter);

    if (Adapter->IoBase != NULL)
    {
        /* Stop transmit and receive */
        E1000_WRITE_REG(Adapter, E1000_REG_TCTL, 0);
        E1000_WRITE_REG(Adapter, E1000_REG_RCTL, 0);

        /* For D3, we might need to set device to lowest power */
        if (PowerState == NdisDeviceStateD3)
        {
            ULONG CtrlValue;

            /* Disable receiver and transmitter */
            CtrlValue = E1000_READ_REG(Adapter, E1000_REG_CTRL);
            CtrlValue &= ~(E1000_CTRL_SLU);  /* Clear Set Link Up */
            E1000_WRITE_REG(Adapter, E1000_REG_CTRL, CtrlValue);
        }
    }

    DbgPrint("E1000: PowerDown complete\n");

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000ConfigureWakeOnLan - Setup Wake-on-LAN
 *
 * Configures the hardware for wake-on-LAN capability.
 * ============================================================================ */

VOID
E1000ConfigureWakeOnLan(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG WucValue = 0;
    ULONG CtrlValue;

    if (Adapter->IoBase == NULL)
    {
        return;
    }

    DbgPrint("E1000: Configuring Wake-on-LAN - Magic=%d, Link=%d\n",
             Adapter->WakeOnMagicPacket, Adapter->WakeOnLinkChange);

    /* Read current CTRL register */
    CtrlValue = E1000_READ_REG(Adapter, E1000_REG_CTRL);

    /* Build WUC (Wake Up Control) register value */
    WucValue = E1000_WUC_PME_EN;  /* Enable PME# assertion */

    if (Adapter->WakeOnMagicPacket)
    {
        WucValue |= E1000_WUC_MAGIC_PACKET;
    }

    if (Adapter->WakeOnLinkChange)
    {
        WucValue |= E1000_WUC_LINK_CHANGE;
    }

    /* Program WUC register */
    E1000_WRITE_REG(Adapter, E1000_REG_WUC, WucValue);

    /* Clear any pending wake status */
    E1000_WRITE_REG(Adapter, E1000_REG_WUFC, 0);

    /* For magic packet wake, ensure MAC address is in RAL/RAH */
    /* (Should already be programmed, but ensure it's valid) */
    if (Adapter->WakeOnMagicPacket)
    {
        ULONG RalValue, RahValue;

        RalValue = ((ULONG)Adapter->CurrentMacAddress[0]) |
                   ((ULONG)Adapter->CurrentMacAddress[1] << 8) |
                   ((ULONG)Adapter->CurrentMacAddress[2] << 16) |
                   ((ULONG)Adapter->CurrentMacAddress[3] << 24);

        RahValue = ((ULONG)Adapter->CurrentMacAddress[4]) |
                   ((ULONG)Adapter->CurrentMacAddress[5] << 8) |
                   E1000_RAH_AV;  /* Address Valid */

        E1000_WRITE_REG(Adapter, E1000_REG_RAL0, RalValue);
        E1000_WRITE_REG(Adapter, E1000_REG_RAH0, RahValue);
    }

    /* Enable PHY wake up if supported */
    if (Adapter->IsPCIe)
    {
        /* 82574L has additional wake-up capabilities via PHY */
        /* For now, just ensure the basic wake-up is configured */
    }

    DbgPrint("E1000: Wake-on-LAN configured - WUC=0x%08x\n", WucValue);
}
