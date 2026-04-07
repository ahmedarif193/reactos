/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Interrupt handling with MSI-X support
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements interrupt handling for the E1000:
 *   - E1000RegisterInterrupt - Register interrupt with NDIS
 *   - E1000MiniportInterrupt - ISR
 *   - E1000MiniportInterruptDpc - DPC
 *   - MSI-X support via NdisMRegisterInterruptEx
 *   - IVAR register configuration for 82574L
 */

#include "e1000.h"

/* Forward declarations for functions defined later in this file */
VOID
E1000ConfigureIvarRegisters(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PIO_INTERRUPT_MESSAGE_INFO MessageInfo
    );

VOID
E1000ConfigureInterruptThrottling(
    _In_ PE1000_ADAPTER Adapter
    );

VOID
E1000UpdateAdaptiveItr(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG RxPackets,
    _In_ ULONG TxPackets,
    _In_ ULONG RxBytes,
    _In_ ULONG TxBytes
    );

VOID
E1000WriteItr(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG ItrValue
    );

/* ============================================================================
 * E1000RegisterInterrupt - Register interrupt handler with NDIS
 *
 * Attempts to register MSI-X first, falling back to MSI then legacy.
 * ============================================================================ */

NDIS_STATUS
E1000RegisterInterrupt(
    _In_ PE1000_ADAPTER Adapter
    )
{
    NDIS_STATUS Status;
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS IntChars;

    DPRINT("E1000: Registering interrupt - Vector=%u, Level=%u, Shared=%d, HasMsgInt=%d\n",
             Adapter->InterruptVector, Adapter->InterruptLevel, Adapter->InterruptShared,
             Adapter->HasMessageInterrupt);

    /* Initialize interrupt characteristics */
    NdisZeroMemory(&IntChars, sizeof(IntChars));

    IntChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    IntChars.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    IntChars.Header.Size = sizeof(NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS);

    IntChars.InterruptHandler = E1000MiniportInterrupt;
    IntChars.InterruptDpcHandler = E1000MiniportInterruptDpc;
    IntChars.DisableInterruptHandler = E1000MiniportDisableInterruptEx;
    IntChars.EnableInterruptHandler = E1000MiniportEnableInterruptEx;

    /*
     * Determine if we should request MSI/MSI-X.
     *
     * MSI/MSI-X is available if:
     *   1. We detected CM_RESOURCE_INTERRUPT_MESSAGE in the allocated resources, OR
     *   2. Hardware supports MSI-X (82574L PCIe) and we have an MSI-X BAR
     *
     * The key indicator is HasMessageInterrupt - if the PnP manager allocated
     * message-signaled interrupt resources, we should use them.
     */
    /*
     * Determine if we should request MSI/MSI-X.
     *
     * For PCIe devices (like 82574L e1000e), MSI-X is often the ONLY interrupt
     * mechanism available. We must enable MSI support for these devices.
     *
     * For legacy PCI devices (like 82540EM e1000), legacy line-based interrupts
     * work fine and are more compatible.
     */
    if (Adapter->HasMessageInterrupt && Adapter->IsPCIe)
    {
        /* PCIe device with MSI/MSI-X resources - must use message interrupts */
        IntChars.MsiSupported = TRUE;
        IntChars.MsiSyncWithAllMessages = TRUE;

        DPRINT("E1000: PCIe device with MSI-X - requesting message interrupt\n");
    }
    else if (Adapter->HasMessageInterrupt)
    {
        /* PnP manager allocated MSI resources for non-PCIe device */
        IntChars.MsiSupported = TRUE;
        IntChars.MsiSyncWithAllMessages = TRUE;

        DPRINT("E1000: MSI resources available - requesting message interrupt\n");
    }
    else
    {
        IntChars.MsiSupported = FALSE;
        DPRINT("E1000: Using legacy line-based interrupt\n");
    }

    /* Register interrupt with NDIS */
    Status = NdisMRegisterInterruptEx(
                Adapter->MiniportAdapterHandle,
                Adapter,  /* MiniportInterruptContext */
                &IntChars,
                &Adapter->InterruptHandle
                );

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT("E1000: NdisMRegisterInterruptEx failed: 0x%08x\n", Status);

        /* If MSI/MSI-X failed, try again with line-based interrupts */
        if (IntChars.MsiSupported)
        {
            DPRINT("E1000: Falling back to line-based interrupts\n");

            IntChars.MsiSupported = FALSE;

            Status = NdisMRegisterInterruptEx(
                        Adapter->MiniportAdapterHandle,
                        Adapter,
                        &IntChars,
                        &Adapter->InterruptHandle
                        );

            if (Status != NDIS_STATUS_SUCCESS)
            {
                DPRINT("E1000: Line-based interrupt registration also failed: 0x%08x\n", Status);
                return Status;
            }

            Adapter->InterruptMode = E1000InterruptModeLegacy;
        }
        else
        {
            return Status;
        }
    }
    else
    {
        DPRINT("E1000: NdisMRegisterInterruptEx succeeded\n");
        DPRINT("E1000:   MsiSupported=%d, MessageInfoTable=%p, InterruptType=%u\n",
                 IntChars.MsiSupported, IntChars.MessageInfoTable, IntChars.InterruptType);

        /*
         * Determine which mode was used.
         *
         * Check InterruptType first - this is set by NDIS based on the actual
         * interrupt connection (CM_RESOURCE_INTERRUPT_MESSAGE flag).
         * MessageInfoTable may be NULL even for MSI if NDIS doesn't provide
         * detailed message info.
         */
        if (IntChars.InterruptType == NDIS_CONNECT_MESSAGE_BASED)
        {
            /* MSI/MSI-X mode confirmed by NDIS */
            if (IntChars.MessageInfoTable != NULL)
            {
                /* Full MSI-X info available */
                DPRINT1("E1000:   MessageCount=%u, UnifiedIrql=0x%x, MessageInfo=%p\n",
                         IntChars.MessageInfoTable->MessageCount,
                         IntChars.MessageInfoTable->UnifiedIrql,
                         IntChars.MessageInfoTable->MessageInfo);

                if (IntChars.MessageInfoTable->MessageCount > 1)
                {
                    Adapter->InterruptMode = E1000InterruptModeMsix;
                    DPRINT("E1000: SUCCESS - Registered %u MSI-X vectors!\n",
                             IntChars.MessageInfoTable->MessageCount);

                    /* Log individual message info */
                    for (ULONG i = 0; i < IntChars.MessageInfoTable->MessageCount; i++)
                    {
                        PIO_INTERRUPT_MESSAGE_INFO_ENTRY Entry =
                            &IntChars.MessageInfoTable->MessageInfo[i];
                        DPRINT1("E1000:   Vector[%u]: MessageAddr=0x%I64x, Data=0x%x, Vector=%u\n",
                                 i, Entry->MessageAddress.QuadPart, Entry->MessageData, Entry->Vector);
                    }

                    /* Configure IVAR registers for 82574L */
                    E1000ConfigureIvarRegisters(Adapter, IntChars.MessageInfoTable);
                }
                else
                {
                    Adapter->InterruptMode = E1000InterruptModeMsi;
                    DPRINT("E1000: SUCCESS - Registered single MSI vector (with MessageInfo)!\n");

                    if (IntChars.MessageInfoTable->MessageInfo != NULL)
                    {
                        PIO_INTERRUPT_MESSAGE_INFO_ENTRY Entry =
                            &IntChars.MessageInfoTable->MessageInfo[0];
                        DPRINT1("E1000:   Vector[0]: MessageAddr=0x%I64x, Data=0x%x, Vector=%u\n",
                                 Entry->MessageAddress.QuadPart, Entry->MessageData, Entry->Vector);
                    }
                }
            }
            else
            {
                /* MSI mode without detailed info - still using MSI */
                Adapter->InterruptMode = E1000InterruptModeMsi;
                DPRINT("E1000: SUCCESS - Registered MSI interrupt (no MessageInfoTable)!\n");

                /*
                 * For 82574L (PCIe), configure IVAR register for single-vector mode.
                 * Even without MessageInfo, we know we have one MSI vector (vector 0).
                 * All interrupt causes must be routed to MSI-X table entry 0.
                 *
                 * 82574L IVAR register layout (single register at 0xE4):
                 *   Bits 0-2:   Rx Queue 0 Vector (0)
                 *   Bit  3:     Rx Queue 0 Valid
                 *   Bits 8-10:  Tx Queue 0 Vector (0)
                 *   Bit  11:    Tx Queue 0 Valid
                 *   Bits 16-18: Other Causes Vector (0)
                 *   Bit  19:    Other Causes Valid
                 *   Bit  31:    Tx interrupt on write-back (set for performance)
                 */
                if (Adapter->IsPCIe)
                {
                    ULONG Ivar;

                    DPRINT("E1000: Configuring IVAR for single MSI vector (PCIe device)\n");

                    /*
                     * Route all causes to MSI-X vector 0:
                     * - Rx Queue 0 at bits 0-3 (vector 0 + valid bit)
                     * - Tx Queue 0 at bits 8-11 (vector 0 + valid bit)
                     * - Other causes at bits 16-19 (vector 0 + valid bit)
                     * - Bit 31 set for Tx interrupt on every write-back
                     */
                    Ivar = (E1000_IVAR_INT_ALLOC_VALID | 0);                              /* RXQ0 -> Vector 0 */
                    Ivar |= (E1000_IVAR_INT_ALLOC_VALID | 0) << E1000_IVAR_TXQ0_SHIFT;    /* TXQ0 -> Vector 0 */
                    Ivar |= (E1000_IVAR_INT_ALLOC_VALID | 0) << E1000_IVAR_OTHER_SHIFT;   /* Other -> Vector 0 */
                    Ivar |= E1000_IVAR_TX_WB_ON_EITR;                                     /* Tx WB on EITR */

                    E1000_WRITE_REG(Adapter, E1000_REG_IVAR, Ivar);

                    DPRINT1("E1000: IVAR=0x%08x (single vector mode)\n", Ivar);

                    Adapter->TxQueueCount = 1;
                    Adapter->RxQueueCount = 1;
                }
            }
        }
        else
        {
            Adapter->InterruptMode = E1000InterruptModeLegacy;
            DPRINT("E1000: SUCCESS - Registered line-based interrupt (legacy mode)\n");
        }
    }

    /* Configure interrupt throttling */
    E1000ConfigureInterruptThrottling(Adapter);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000DeregisterInterrupt - Deregister interrupt handler
 * ============================================================================ */

VOID
E1000DeregisterInterrupt(
    _In_ PE1000_ADAPTER Adapter
    )
{
    if (Adapter->InterruptHandle != NULL)
    {
        /* Disable interrupts first */
        E1000DisableInterrupts(Adapter);

        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);
        Adapter->InterruptHandle = NULL;

        DPRINT("E1000: Interrupt deregistered\n");
    }
}


/* ============================================================================
 * E1000ConfigureIvarRegisters - Configure IVAR for MSI-X (82574L)
 *
 * Sets up the Interrupt Vector Allocation Registers for multi-queue operation.
 * ============================================================================ */

VOID
E1000ConfigureIvarRegisters(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PIO_INTERRUPT_MESSAGE_INFO MessageInfo
    )
{
    ULONG Ivar = 0;
    ULONG VectorCount;

    if (!Adapter->IsPCIe || MessageInfo == NULL)
    {
        return;
    }

    VectorCount = MessageInfo->MessageCount;

    DPRINT("E1000: Configuring IVAR for %u MSI-X vectors (82574L layout)\n", VectorCount);

    /*
     * 82574L uses a SINGLE IVAR register at 0xE4, NOT separate registers.
     * Layout per Linux e1000e driver:
     *   Bits 0-2:   Rx Queue 0 Vector number
     *   Bit  3:     Rx Queue 0 Valid
     *   Bits 8-10:  Tx Queue 0 Vector number
     *   Bit  11:    Tx Queue 0 Valid
     *   Bits 16-18: Other Causes Vector number
     *   Bit  19:    Other Causes Valid
     *   Bit  31:    Tx interrupt on every write-back
     *
     * Note: 82574L only has a single RX and TX queue despite MSI-X support.
     * Multiple vectors allow separate handling of RX, TX, and Other causes.
     */

    if (VectorCount >= 3)
    {
        /*
         * Preferred MSI-X configuration with 3 vectors:
         *   Vector 0: RX Queue 0
         *   Vector 1: TX Queue 0
         *   Vector 2: Other causes (link, timer, etc.)
         */
        Ivar = (E1000_IVAR_INT_ALLOC_VALID | 0);                              /* RXQ0 -> Vector 0 */
        Ivar |= (E1000_IVAR_INT_ALLOC_VALID | 1) << E1000_IVAR_TXQ0_SHIFT;    /* TXQ0 -> Vector 1 */
        Ivar |= (E1000_IVAR_INT_ALLOC_VALID | 2) << E1000_IVAR_OTHER_SHIFT;   /* Other -> Vector 2 */
        Ivar |= E1000_IVAR_TX_WB_ON_EITR;                                     /* Tx WB on EITR */

        Adapter->TxQueueCount = 1;
        Adapter->RxQueueCount = 1;

        DPRINT("E1000: MSI-X 3-vector mode: RX=0, TX=1, Other=2\n");
    }
    else
    {
        /*
         * Single vector configuration:
         *   All causes -> Vector 0
         */
        Ivar = (E1000_IVAR_INT_ALLOC_VALID | 0);                              /* RXQ0 -> Vector 0 */
        Ivar |= (E1000_IVAR_INT_ALLOC_VALID | 0) << E1000_IVAR_TXQ0_SHIFT;    /* TXQ0 -> Vector 0 */
        Ivar |= (E1000_IVAR_INT_ALLOC_VALID | 0) << E1000_IVAR_OTHER_SHIFT;   /* Other -> Vector 0 */
        Ivar |= E1000_IVAR_TX_WB_ON_EITR;                                     /* Tx WB on EITR */

        Adapter->TxQueueCount = 1;
        Adapter->RxQueueCount = 1;

        DPRINT("E1000: MSI-X single-vector mode: All causes -> Vector 0\n");
    }

    /* Write single IVAR register at 0xE4 */
    E1000_WRITE_REG(Adapter, E1000_REG_IVAR, Ivar);

    DPRINT1("E1000: IVAR=0x%08x, TxQueues=%u, RxQueues=%u\n",
             Ivar, Adapter->TxQueueCount, Adapter->RxQueueCount);
}


/* ============================================================================
 * E1000ConfigureInterruptThrottling - Set up interrupt coalescing
 *
 * Configures initial ITR values based on adapter settings.
 * Default is dynamic mode which will auto-adjust based on traffic.
 * ============================================================================ */

VOID
E1000ConfigureInterruptThrottling(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG ItrValue;

    /*
     * Configure interrupt throttle rate (ITR)
     * ITR register value = 1000000000 / (desired_int_per_sec * 256)
     *
     * Common values:
     *   20000 = ~195 int/sec (bulk latency - high throughput)
     *   8000  = ~488 int/sec (low latency)
     *   2000  = ~1953 int/sec (lowest latency)
     *
     * Note: Hardware interprets ITR value differently on different models.
     * 82574L uses 256ns units, so ITR=20000 means 20000*256ns = 5.12ms between
     * interrupts, which is ~195 int/sec.
     *
     * For initial configuration, use a balanced value and enable dynamic mode.
     */

    /* Initialize adaptive ITR state */
    Adapter->ItrSetting = E1000_ITR_SETTING_DYNAMIC;
    Adapter->TotalRxPackets = 0;
    Adapter->TotalTxPackets = 0;
    Adapter->TotalRxBytes = 0;
    Adapter->TotalTxBytes = 0;
    KeQuerySystemTime(&Adapter->LastItrUpdateTime);

    /* Start with medium latency value */
    ItrValue = 8000;  /* ~488 int/sec - balanced starting point */
    Adapter->CurrentItr = ItrValue;

    /* Set global ITR */
    E1000_WRITE_REG(Adapter, E1000_REG_ITR, ItrValue);

    /* For 82574L, set per-vector throttle registers */
    if (Adapter->IsPCIe)
    {
        E1000_WRITE_REG(Adapter, E1000_REG_EITR0, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR1, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR2, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR3, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR4, ItrValue);
    }

    DPRINT("E1000: Interrupt throttling set to %u (256ns units), mode=DYNAMIC\n", ItrValue);
}


/* ============================================================================
 * E1000WriteItr - Write ITR value to hardware registers
 *
 * Updates the ITR register and per-vector EITR registers for 82574L.
 * ============================================================================ */

VOID
E1000WriteItr(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG ItrValue
    )
{
    if (Adapter->IoBase == NULL)
    {
        return;
    }

    /* Update global ITR */
    E1000_WRITE_REG(Adapter, E1000_REG_ITR, ItrValue);

    /* For 82574L, update per-vector throttle registers */
    if (Adapter->IsPCIe)
    {
        E1000_WRITE_REG(Adapter, E1000_REG_EITR0, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR1, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR2, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR3, ItrValue);
        E1000_WRITE_REG(Adapter, E1000_REG_EITR4, ItrValue);
    }

    Adapter->CurrentItr = ItrValue;
}


/* ============================================================================
 * E1000UpdateAdaptiveItr - Dynamically adjust ITR based on traffic patterns
 *
 * Implements adaptive interrupt moderation following Linux e1000e semantics.
 * Analyzes bytes_per_packet and packet rate to determine optimal interrupt rate:
 *
 *   - lowest_latency: Very small packets or low traffic -> highest int rate
 *   - low_latency: Moderate traffic -> balanced int rate
 *   - bulk_latency: Large packets/high throughput -> lowest int rate
 *
 * Linux e1000e uses these ITR values (in 256ns units):
 *   - lowest_latency: 70000 (~56 int/sec, very aggressive coalescing)
 *   - low_latency:    20000 (~195 int/sec)
 *   - bulk_latency:    4000 (~976 int/sec, minimal coalescing)
 *
 * Note: Lower ITR value = higher interrupt rate (less coalescing)
 *       Higher ITR value = lower interrupt rate (more coalescing)
 *
 * The Linux algorithm uses bytes_per_int thresholds with hysteresis.
 * ============================================================================ */

VOID
E1000UpdateAdaptiveItr(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG RxPackets,
    _In_ ULONG TxPackets,
    _In_ ULONG RxBytes,
    _In_ ULONG TxBytes
    )
{
    ULONG TotalPackets;
    ULONG TotalBytes;
    ULONG BytesPerPacket;
    ULONG NewItrSetting;
    ULONG NewItrValue;
    ULONG CurrentSetting;

    /*
     * ITR values following Linux e1000e semantics.
     * These are in 256ns units for the hardware register.
     *
     * Linux values from netdev.c e1000_update_itr():
     *   lowest_latency = 70000 (very aggressive coalescing, low int rate)
     *   low_latency    = 20000 (balanced)
     *   bulk_latency   = 4000  (minimal coalescing, high int rate for throughput)
     *
     * IMPORTANT: In e1000e, LOWER values mean HIGHER interrupt rates.
     * This is counter-intuitive but matches the hardware behavior.
     */
    static const ULONG ItrValues[] = {
        5000,   /* lowest_latency: ~780 int/sec - responsive for small packets */
        10000,  /* low_latency: ~390 int/sec - balanced */
        20000   /* bulk_latency: ~195 int/sec - throughput optimized */
    };

    /* Only adjust if in dynamic mode */
    if (Adapter->ItrSetting != E1000_ITR_SETTING_DYNAMIC)
    {
        return;
    }

    /* Calculate traffic characteristics for this poll interval */
    TotalPackets = RxPackets + TxPackets;
    TotalBytes = RxBytes + TxBytes;

    /* If no work done, keep current setting */
    if (TotalPackets == 0)
    {
        return;
    }

    /* Calculate bytes per packet (average packet size) */
    BytesPerPacket = TotalBytes / TotalPackets;

    /*
     * Determine ITR mode using Linux e1000e-style thresholds.
     *
     * Linux uses a complex state machine with these key thresholds:
     *   - bytes_per_packet > 8000: TSO packets -> bulk_latency
     *   - bytes_per_packet > 1200 with high byte count: bulk_latency
     *   - bytes_per_packet < 512 with few packets: lowest_latency
     *   - Otherwise: low_latency (balanced)
     *
     * Simplified adaptation for Windows NDIS:
     *   - Very small packets (<256 bytes): lowest_latency for responsiveness
     *   - Large packets (>1200 bytes): bulk_latency for throughput
     *   - Medium packets with high rate: low_latency (balanced)
     *   - Medium packets with low rate: lowest_latency
     *
     * Hysteresis: We use the current setting as a baseline and only
     * change if conditions strongly warrant it. This prevents oscillation.
     */
    CurrentSetting = Adapter->ItrSetting;
    NewItrSetting = CurrentSetting;

    if (BytesPerPacket > 1200)
    {
        /*
         * Large packets indicate bulk transfer (file copy, streaming).
         * Use bulk_latency for maximum throughput.
         */
        NewItrSetting = E1000_ITR_SETTING_BULK_LATENCY;
    }
    else if (BytesPerPacket < 256)
    {
        /*
         * Very small packets (ACKs, keep-alives, interactive).
         * Use lowest_latency for quick response.
         */
        NewItrSetting = E1000_ITR_SETTING_LOWEST_LATENCY;
    }
    else
    {
        /*
         * Medium-sized packets - balanced mode.
         * Use packet rate to distinguish interactive vs bulk.
         */
        if (TotalPackets > 8)
        {
            /* High packet rate - use low_latency (more coalescing) */
            NewItrSetting = E1000_ITR_SETTING_LOW_LATENCY;
        }
        else
        {
            /* Low packet rate - stay responsive */
            NewItrSetting = E1000_ITR_SETTING_LOWEST_LATENCY;
        }
    }

    /*
     * Apply hysteresis: Only change ITR if the new setting differs
     * from current. This prevents rapid oscillation between modes.
     *
     * Linux adds extra hysteresis by tracking previous setting and
     * requiring sustained traffic patterns. We keep it simple here.
     */
    NewItrValue = ItrValues[NewItrSetting];
    if (NewItrValue != Adapter->CurrentItr)
    {
        E1000WriteItr(Adapter, NewItrValue);
    }
}


/* ============================================================================
 * E1000DisableInterrupts - Disable all interrupts
 * ============================================================================ */

VOID
E1000DisableInterrupts(
    _In_ PE1000_ADAPTER Adapter
    )
{
    if (Adapter->IoBase == NULL)
    {
        return;
    }

    /* Disable all interrupt causes */
    E1000_WRITE_REG(Adapter, E1000_REG_IMC, 0xFFFFFFFF);

    /* For 82574L, also disable extended interrupts */
    if (Adapter->IsPCIe)
    {
        E1000_WRITE_REG(Adapter, E1000_REG_EIMC, 0xFFFFFFFF);
    }

    /* Clear any pending interrupts */
    E1000_READ_REG(Adapter, E1000_REG_ICR);

    InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_INTERRUPT_ENABLED);
}


/* ============================================================================
 * E1000EnableInterrupts - Enable interrupts
 * ============================================================================ */

VOID
E1000EnableInterrupts(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG InterruptMask;

    if (Adapter->IoBase == NULL)
    {
        return;
    }

    /*
     * Build interrupt mask for causes we care about:
     *   - TXDW:   Transmit descriptor written back
     *   - TXQE:   Transmit queue empty
     *   - LSC:    Link status change
     *   - RXDMT0: Receive descriptor minimum threshold
     *   - RXO:    Receive overrun
     *   - RXT0:   Receive timer (coalescing)
     */
    InterruptMask = E1000_IMS_TXDW |
                    E1000_IMS_LSC |
                    E1000_IMS_RXDMT0 |
                    E1000_IMS_RXO |
                    E1000_IMS_RXT0;

    /* Store mask for later reference */
    Adapter->InterruptMask = InterruptMask;

    /* Clear pending interrupts first */
    E1000_READ_REG(Adapter, E1000_REG_ICR);

    /* Enable interrupts */
    E1000_WRITE_REG(Adapter, E1000_REG_IMS, InterruptMask);

    /*
     * For 82574L (PCIe) with MSI or MSI-X, enable extended interrupts.
     * The 82574L uses the Extended Interrupt Mask Set (EIMS) register
     * in addition to the standard IMS register for MSI/MSI-X mode.
     */
    if (Adapter->IsPCIe &&
        (Adapter->InterruptMode == E1000InterruptModeMsix ||
         Adapter->InterruptMode == E1000InterruptModeMsi))
    {
        ULONG Eims;

        /*
         * Enable extended interrupt causes.
         * For single-vector mode, all causes route to vector 0.
         */
        Eims = E1000_EIMS_RXQ0 | E1000_EIMS_TXQ0 | E1000_EIMS_OTHER;

        if (Adapter->InterruptMode == E1000InterruptModeMsix &&
            Adapter->RxQueueCount > 1)
        {
            Eims |= E1000_EIMS_RXQ1 | E1000_EIMS_TXQ1;
        }

        E1000_WRITE_REG(Adapter, E1000_REG_EIMS, Eims);

        /*
         * Configure Extended Interrupt Auto Clear (EIAC).
         * This register controls automatic clearing of extended interrupt
         * causes when ICR is read. For MSI-X mode, this allows the hardware
         * to auto-clear the interrupt causes.
         */
        E1000_WRITE_REG(Adapter, E1000_REG_EIAC, Eims);
    }

    InterlockedOr(&Adapter->Flags, E1000_FLAG_INTERRUPT_ENABLED);
}


/* ============================================================================
 * E1000MiniportInterrupt - Interrupt Service Routine (ISR)
 *
 * Called at DIRQL when an interrupt fires.
 * Must quickly determine if interrupt is ours and queue DPC.
 * ============================================================================ */

BOOLEAN
NTAPI
E1000MiniportInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportInterruptContext;
    ULONG IcrRaw, Icr;

    /* Default: don't queue DPC and target no processors */
    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;

    if (Adapter == NULL || Adapter->IoBase == NULL)
    {
        return FALSE;  /* Not our interrupt */
    }

    /* Read and clear the Interrupt Cause Register */
    IcrRaw = E1000_READ_REG(Adapter, E1000_REG_ICR);

    /* Check if this is our interrupt */
    if (IcrRaw == 0 || IcrRaw == 0xFFFFFFFF)
    {
        return FALSE;  /* Not our interrupt or hardware not responding */
    }

    /*
     * Mask any causes we don't care about.
     * For MSI-X mode, we need to check BOTH legacy bits AND queue bits.
     *
     * 82574L ICR bit layout:
     *   Bits 0-7:   Legacy interrupt causes (TXDW, TXQE, LSC, RXT0, etc.)
     *   Bits 20-24: Queue interrupt causes (RXQ0, RXQ1, TXQ0, TXQ1, OTHER)
     *   Bit 31:     INT_ASSERTED (any interrupt asserted)
     *
     * The MSI-X queue bits (20-24) are set when the corresponding queue
     * has work, but the legacy bits may also be set simultaneously.
     */
    if (Adapter->InterruptMode == E1000InterruptModeMsi ||
        Adapter->InterruptMode == E1000InterruptModeMsix)
    {
        /*
         * For MSI-X/MSI mode, accept interrupt if:
         * - Any legacy bit we care about is set, OR
         * - Any queue bit is set (RXQ0, TXQ0, OTHER)
         */
        ULONG QueueMask = E1000_IMS_RXQ0 | E1000_IMS_TXQ0 | E1000_IMS_OTHER |
                          E1000_IMS_RXQ1 | E1000_IMS_TXQ1;
        Icr = IcrRaw & (Adapter->InterruptMask | QueueMask);
    }
    else
    {
        /* Legacy mode: only check legacy bits */
        Icr = IcrRaw & Adapter->InterruptMask;
    }

    if (Icr == 0)
    {
        /*
         * If INT_ASSERTED is set but no cause bits we recognize,
         * this might be a valid interrupt with an unhandled cause.
         * In MSI-X mode, always claim the interrupt if INT_ASSERTED.
         */
        if ((IcrRaw & E1000_IMS_INT_ASSERTED) &&
            (Adapter->InterruptMode == E1000InterruptModeMsi ||
             Adapter->InterruptMode == E1000InterruptModeMsix))
        {
            /* Claim it anyway for MSI-X to prevent re-assertion */
            Icr = IcrRaw & ~E1000_IMS_INT_ASSERTED;
            if (Icr == 0)
            {
                Icr = E1000_IMS_TXDW;  /* Assume TX completion */
            }
        }
        else
        {
            return FALSE;  /* Interrupt not for causes we're monitoring */
        }
    }

    /* Disable interrupts immediately to prevent storm */
    E1000_WRITE_REG(Adapter, E1000_REG_IMC, 0xFFFFFFFF);

    /* Store ICR value for DPC to process */
    InterlockedExchange((LONG*)&Adapter->InterruptPending, Icr);

    /* Queue the DPC */
    *QueueDefaultInterruptDpc = TRUE;
    *TargetProcessors = 1;  /* Target processor 0 */

    return TRUE;  /* Interrupt was ours */
}


/* ============================================================================
 * E1000MiniportInterruptDpc - Deferred Procedure Call for interrupt processing
 *
 * Called at DISPATCH_LEVEL to process the interrupt.
 *
 * Implements NAPI-style budget-based processing:
 *   - TX completion is always processed fully (cheap operation)
 *   - RX processing is limited by budget to prevent monopolizing CPU
 *   - If budget is exhausted, interrupts are NOT re-enabled to allow
 *     NDIS to reschedule the DPC
 *   - Adaptive ITR is updated based on traffic patterns
 * ============================================================================ */

VOID
NTAPI
E1000MiniportInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportInterruptContext;
    ULONG Icr;
    BOOLEAN ProcessRx = FALSE;
    BOOLEAN ProcessTx = FALSE;
    BOOLEAN LinkChange = FALSE;
    ULONG RxBudget;
    ULONG RxWorkDone = 0;
    ULONG TxCompleted = 0;
    ULONG TotalRxBytes = 0;
    ULONG TotalTxBytes = 0;
    BOOLEAN BudgetExhausted = FALSE;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(NdisReserved2);

    if (Adapter == NULL)
    {
        return;
    }

    /*
     * Determine RX budget from NDIS throttle parameters if available.
     * NDIS 6.20+ provides NDIS_RECEIVE_THROTTLE_PARAMETERS to limit
     * how many packets we should indicate per DPC.
     *
     * If not available or no limit, use default budget.
     */
    RxBudget = E1000_RX_DEFAULT_BUDGET;

    /*
     * Check for NDIS receive throttle parameters (NDIS 6.20+).
     *
     * If NDIS provides throttle parameters, honor the MaxNblsToIndicate limit.
     * This allows NDIS to control how many packets we process per DPC,
     * implementing fair scheduling across multiple adapters.
     */
#ifdef NDIS_SUPPORT_NDIS620
    if (ReceiveThrottleParameters != NULL)
    {
        PNDIS_RECEIVE_THROTTLE_PARAMETERS ThrottleParams =
            (PNDIS_RECEIVE_THROTTLE_PARAMETERS)ReceiveThrottleParameters;

        if (ThrottleParams->MaxNblsToIndicate != NDIS_INDICATE_ALL_NBLS)
        {
            RxBudget = ThrottleParams->MaxNblsToIndicate;
            if (RxBudget == 0)
            {
                RxBudget = 1;  /* Ensure at least 1 packet processed */
            }
        }
    }
#else
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
#endif

    /* Get the interrupt cause that was saved in ISR */
    Icr = (ULONG)InterlockedExchange((LONG*)&Adapter->InterruptPending, 0);

    if (Icr == 0)
    {
        /* Re-enable interrupts and return */
        E1000EnableInterrupts(Adapter);
        return;
    }

    /* Determine which types of work to process based on ICR bits */
    if (Icr & (E1000_IMS_TXDW | E1000_IMS_TXQE | E1000_IMS_TXQ0 | E1000_IMS_TXQ1))
    {
        ProcessTx = TRUE;
    }

    if (Icr & (E1000_IMS_RXT0 | E1000_IMS_RXDMT0 | E1000_IMS_RXO | E1000_IMS_RXQ0 | E1000_IMS_RXQ1))
    {
        ProcessRx = TRUE;
    }

    if (Icr & E1000_IMS_LSC)
    {
        LinkChange = TRUE;
    }

    /* Handle receive overrun - log this as it indicates a problem */
    if (Icr & E1000_IMS_RXO)
    {
        InterlockedIncrement64((LONG64*)&Adapter->Statistics.RxNoBuffer);
    }

    /*
     * Process TX completions unconditionally and fully.
     * Linux e1000e always calls e1000_clean_tx_irq() in its NAPI poll handler
     * regardless of which ICR bits triggered the interrupt. TX completion is
     * cheap and ensures descriptors are reclaimed promptly for new sends.
     */
    E1000ProcessTxCompletions(&Adapter->TxQueues[0]);

    /* Track TX bytes for adaptive ITR */
    TotalTxBytes = (ULONG)Adapter->TxQueues[0].BytesSent;
    TxCompleted += (ULONG)Adapter->TxQueues[0].PacketsSent;

    if (Adapter->TxQueueCount > 1)
    {
        E1000ProcessTxCompletions(&Adapter->TxQueues[1]);
        TotalTxBytes += (ULONG)Adapter->TxQueues[1].BytesSent;
        TxCompleted += (ULONG)Adapter->TxQueues[1].PacketsSent;
    }

    /*
     * Process received packets with budget enforcement.
     * This is the key NAPI-style optimization - we limit how many
     * packets we process per DPC to prevent CPU monopolization
     * and allow fair scheduling.
     */
    RxWorkDone = E1000IndicateReceive(&Adapter->RxQueues[0], RxBudget);
    TotalRxBytes += (ULONG)Adapter->RxQueues[0].BytesReceived;

    if (Adapter->RxQueueCount > 1 && RxWorkDone < RxBudget)
    {
        ULONG RemainingBudget = RxBudget - RxWorkDone;
        RxWorkDone += E1000IndicateReceive(&Adapter->RxQueues[1], RemainingBudget);
        TotalRxBytes += (ULONG)Adapter->RxQueues[1].BytesReceived;
    }

    /* Check if we exhausted the RX budget */
    if (RxWorkDone >= RxBudget)
    {
        BudgetExhausted = TRUE;
        Adapter->MoreNblsPending = TRUE;
    }
    else
    {
        Adapter->MoreNblsPending = FALSE;
    }

    DBG_UNREFERENCED_LOCAL_VARIABLE(ProcessTx);
    DBG_UNREFERENCED_LOCAL_VARIABLE(ProcessRx);

    /* Handle link status change - log this event as it's important */
    if (LinkChange)
    {
        E1000UpdateLinkStatus(Adapter);
    }

    /*
     * Update adaptive interrupt throttling based on traffic patterns.
     * This helps optimize latency vs throughput trade-off dynamically.
     */
    E1000UpdateAdaptiveItr(Adapter, RxWorkDone, TxCompleted, TotalRxBytes, TotalTxBytes);

    /*
     * Re-enable interrupts only if we did not exhaust the budget.
     *
     * If budget was exhausted (BudgetExhausted == TRUE), there is likely
     * more work pending. By not re-enabling interrupts, NDIS will reschedule
     * this DPC to continue processing without interrupt overhead.
     *
     * This is the key to NAPI-style efficiency - during high traffic, we
     * process in batches without interrupt storm.
     */
    if (!BudgetExhausted)
    {
        E1000EnableInterrupts(Adapter);
    }
    /*
     * If budget exhausted, interrupts stay disabled.
     * NDIS will call us again with ReceiveThrottleParameters
     * when it's ready for more packets. We'll re-enable interrupts
     * when we finally process all pending work.
     */
}


/* ============================================================================
 * E1000MiniportDisableInterruptEx - Disable interrupts (called from NDIS)
 * ============================================================================ */

VOID
NTAPI
E1000MiniportDisableInterruptEx(
    _In_ NDIS_HANDLE MiniportInterruptContext
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportInterruptContext;

    if (Adapter != NULL)
    {
        E1000DisableInterrupts(Adapter);
    }
}


/* ============================================================================
 * E1000MiniportEnableInterruptEx - Enable interrupts (called from NDIS)
 * ============================================================================ */

VOID
NTAPI
E1000MiniportEnableInterruptEx(
    _In_ NDIS_HANDLE MiniportInterruptContext
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportInterruptContext;

    if (Adapter != NULL)
    {
        E1000EnableInterrupts(Adapter);
    }
}


/* ============================================================================
 * E1000EnableMsixInterrupts - Enable MSI-X mode (advanced setup)
 *
 * This performs additional setup for MSI-X interrupt mode.
 * ============================================================================ */

NDIS_STATUS
E1000EnableMsixInterrupts(
    _In_ PE1000_ADAPTER Adapter
    )
{
    if (Adapter->InterruptMode != E1000InterruptModeMsix)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* MSI-X is already configured during registration */
    /* Additional setup could be done here if needed */

    return NDIS_STATUS_SUCCESS;
}
