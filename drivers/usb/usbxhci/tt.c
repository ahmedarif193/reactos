/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Transaction Translator helpers for xHCI
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "usbxhci.h"

#define USBPORT_NO_HUB_ADDRESS 0xFFFF
#define XHCI_DEFAULT_TT_THINK_TIME 0

static
ULONG
XHCI_GetHubThinkTime(
    _In_opt_ PXHCI_DEVICE_SLOT HubSlot)
{
    if (HubSlot && HubSlot->HasTtInfo)
        return HubSlot->TtThinkTime & 0x3;

    return XHCI_DEFAULT_TT_THINK_TIME;
}

BOOLEAN
XHCI_EndpointNeedsTt(
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties)
{
    if (!EndpointProperties)
        return FALSE;

    /* USBPORT resolves the TT hub (nearest high-speed ancestor hub) while
     * walking the device topology; without one the root port talks to the
     * device directly and no split transactions are involved. */
    if (EndpointProperties->TtHubAddr == 0 ||
        EndpointProperties->TtHubAddr == USBPORT_NO_HUB_ADDRESS ||
        EndpointProperties->TtPortNumber == 0)
    {
        return FALSE;
    }

    return EndpointProperties->DeviceSpeed == UsbLowSpeed ||
           EndpointProperties->DeviceSpeed == UsbFullSpeed;
}

VOID
XHCI_ApplyTtInfo(
    _In_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
    _In_opt_ PXHCI_DEVICE_SLOT HubSlot,
    _Inout_ PXHCI_SLOT_CONTEXT SlotContext)
{
    ULONG TtInfo;

    if (!EndpointProperties || !SlotContext)
        return;

    if (!XHCI_EndpointNeedsTt(EndpointProperties))
    {
        SlotContext->TtInfo = 0;
        return;
    }

    if (!HubSlot || HubSlot->SlotId == 0)
    {
        SlotContext->TtInfo = 0;
        return;
    }

    TtInfo = (ULONG)(HubSlot->SlotId & XHCI_SLOT_TT_SLOT_MASK);
    TtInfo |= (((ULONG)EndpointProperties->TtPortNumber << XHCI_SLOT_TT_PORT_SHIFT) &
               XHCI_SLOT_TT_PORT_MASK);
    TtInfo |= (XHCI_GetHubThinkTime(HubSlot) << XHCI_SLOT_TT_THINK_TIME_SHIFT) &
              XHCI_SLOT_TT_THINK_TIME_MASK;

    SlotContext->TtInfo = TtInfo;
}
