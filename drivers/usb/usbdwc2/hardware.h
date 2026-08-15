/*
 * PROJECT:     ReactOS Synopsys DWC2 USB Miniport Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DWC2 host-controller register definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * These definitions follow the public Synopsys DWC2 programming model and
 * are intentionally limited to the registers used by this miniport.
 */

#pragma once

#define DWC2_BIT(_Bit)                         (1UL << (_Bit))

/* Core global registers. */
#define DWC2_GAHBCFG                           0x0008
#define DWC2_GUSBCFG                           0x000C
#define DWC2_GRSTCTL                           0x0010
#define DWC2_GINTSTS                           0x0014
#define DWC2_GINTMSK                           0x0018
#define DWC2_GRXFSIZ                           0x0024
#define DWC2_GNPTXFSIZ                         0x0028
#define DWC2_GSNPSID                           0x0040
#define DWC2_GHWCFG2                           0x0048

#define DWC2_GAHBCFG_DMA_EN                    DWC2_BIT(5)
#define DWC2_GAHBCFG_AXI_BURST4_MASK           (3UL << 1)
#define DWC2_GAHBCFG_WAIT_AXI_WRITES           DWC2_BIT(4)
#define DWC2_GAHBCFG_GLBL_INTR_EN              DWC2_BIT(0)

#define DWC2_GUSBCFG_FORCEHOSTMODE             DWC2_BIT(29)

#define DWC2_GRSTCTL_AHBIDLE                   DWC2_BIT(31)
#define DWC2_GRSTCTL_TXFNUM_ALL                (0x10UL << 6)
#define DWC2_GRSTCTL_TXFFLSH                   DWC2_BIT(5)
#define DWC2_GRSTCTL_RXFFLSH                   DWC2_BIT(4)
#define DWC2_GRSTCTL_CSFTRST                   DWC2_BIT(0)

#define DWC2_GINT_WKUPINT                      DWC2_BIT(31)
#define DWC2_GINT_DISCONNINT                   DWC2_BIT(29)
#define DWC2_GINT_HCHINT                       DWC2_BIT(25)
#define DWC2_GINT_PRTINT                       DWC2_BIT(24)
#define DWC2_GINT_SOF                          DWC2_BIT(3)
#define DWC2_GINT_CURMODE_HOST                 DWC2_BIT(0)

#define DWC2_GINT_BASE_MASK                    (DWC2_GINT_WKUPINT | DWC2_GINT_DISCONNINT | DWC2_GINT_HCHINT | DWC2_GINT_PRTINT)

#define DWC2_GSNPSID_MASK                      0xFFFF0000UL
#define DWC2_GSNPSID_VALUE                     0x4F540000UL

#define DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT       14
#define DWC2_GHWCFG2_NUM_HOST_CHAN_MASK        (0xFUL << DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT)

/* Host registers. */
#define DWC2_HCFG                              0x0400
#define DWC2_HFNUM                             0x0408
#define DWC2_HAINT                             0x0414
#define DWC2_HAINTMSK                          0x0418
#define DWC2_HPRT0                             0x0440
#define DWC2_HPTXFSIZ                          0x0100

/* Power and clock gating register. */
#define DWC2_PCGCCTL                           0x0E00

#define DWC2_HCFG_FSLSPCLKSEL_MASK             0x3UL
#define DWC2_HCFG_FSLSPCLKSEL_30_60_MHZ        0x0UL

#define DWC2_HFNUM_FRNUM_MASK                  0x3FFFUL

#define DWC2_HPRT_SPD_SHIFT                    17
#define DWC2_HPRT_SPD_MASK                     (0x3UL << DWC2_HPRT_SPD_SHIFT)
#define DWC2_HPRT_SPD_HIGH                     0
#define DWC2_HPRT_SPD_FULL                     1
#define DWC2_HPRT_SPD_LOW                      2
#define DWC2_HPRT_PWR                          DWC2_BIT(12)
#define DWC2_HPRT_RST                          DWC2_BIT(8)
#define DWC2_HPRT_SUSP                         DWC2_BIT(7)
#define DWC2_HPRT_RES                          DWC2_BIT(6)
#define DWC2_HPRT_OVRCURRCHG                   DWC2_BIT(5)
#define DWC2_HPRT_OVRCURRACT                   DWC2_BIT(4)
#define DWC2_HPRT_ENACHG                       DWC2_BIT(3)
#define DWC2_HPRT_ENA                          DWC2_BIT(2)
#define DWC2_HPRT_CONNDET                      DWC2_BIT(1)
#define DWC2_HPRT_CONNSTS                      DWC2_BIT(0)
#define DWC2_HPRT_W1C_MASK                     (DWC2_HPRT_OVRCURRCHG | DWC2_HPRT_ENACHG | DWC2_HPRT_ENA | DWC2_HPRT_CONNDET)
#define DWC2_HPRT_CHANGE_MASK                  (DWC2_HPRT_OVRCURRCHG | DWC2_HPRT_ENACHG | DWC2_HPRT_CONNDET)

/* Host-channel registers. */
#define DWC2_HCCHAR(_Channel)                  (0x0500 + (0x20 * (_Channel)))
#define DWC2_HCSPLT(_Channel)                  (0x0504 + (0x20 * (_Channel)))
#define DWC2_HCINT(_Channel)                   (0x0508 + (0x20 * (_Channel)))
#define DWC2_HCINTMSK(_Channel)                (0x050C + (0x20 * (_Channel)))
#define DWC2_HCTSIZ(_Channel)                  (0x0510 + (0x20 * (_Channel)))
#define DWC2_HCDMA(_Channel)                   (0x0514 + (0x20 * (_Channel)))

#define DWC2_HCCHAR_CHENA                      DWC2_BIT(31)
#define DWC2_HCCHAR_CHDIS                      DWC2_BIT(30)
#define DWC2_HCCHAR_ODDFRM                     DWC2_BIT(29)
#define DWC2_HCCHAR_DEVADDR_SHIFT              22
#define DWC2_HCCHAR_MULTICNT_SHIFT             20
#define DWC2_HCCHAR_MULTICNT_ONE               (1UL << DWC2_HCCHAR_MULTICNT_SHIFT)
#define DWC2_HCCHAR_EPTYPE_SHIFT               18
#define DWC2_HCCHAR_LSPDDEV                    DWC2_BIT(17)
#define DWC2_HCCHAR_EPDIR                      DWC2_BIT(15)
#define DWC2_HCCHAR_EPNUM_SHIFT                11
#define DWC2_HCCHAR_MPS_MASK                   0x7FFUL

#define DWC2_HCSPLT_SPLTENA                    DWC2_BIT(31)
#define DWC2_HCSPLT_COMPSPLT                   DWC2_BIT(16)
#define DWC2_HCSPLT_XACTPOS_ALL                (3UL << 14)
#define DWC2_HCSPLT_HUBADDR_SHIFT              7

#define DWC2_HCINT_DATATGLERR                  DWC2_BIT(10)
#define DWC2_HCINT_FRMOVRUN                    DWC2_BIT(9)
#define DWC2_HCINT_BBLERR                      DWC2_BIT(8)
#define DWC2_HCINT_XACTERR                     DWC2_BIT(7)
#define DWC2_HCINT_NYET                        DWC2_BIT(6)
#define DWC2_HCINT_ACK                         DWC2_BIT(5)
#define DWC2_HCINT_NAK                         DWC2_BIT(4)
#define DWC2_HCINT_STALL                       DWC2_BIT(3)
#define DWC2_HCINT_AHBERR                      DWC2_BIT(2)
#define DWC2_HCINT_CHHLTD                      DWC2_BIT(1)
#define DWC2_HCINT_XFERCOMPL                   DWC2_BIT(0)
#define DWC2_HCINT_VALID_MASK                  0x3FFFUL
#define DWC2_HCINT_DRIVER_MASK                 (DWC2_HCINT_DATATGLERR | DWC2_HCINT_FRMOVRUN | DWC2_HCINT_BBLERR | DWC2_HCINT_XACTERR | DWC2_HCINT_NYET | DWC2_HCINT_ACK | DWC2_HCINT_NAK | DWC2_HCINT_STALL | DWC2_HCINT_AHBERR | DWC2_HCINT_CHHLTD | DWC2_HCINT_XFERCOMPL)

#define DWC2_HCTSIZ_PID_SHIFT                  29
#define DWC2_HCTSIZ_PID_DATA0                  0
#define DWC2_HCTSIZ_PID_DATA1                  2
#define DWC2_HCTSIZ_PID_SETUP                  3
#define DWC2_HCTSIZ_PKTCNT_SHIFT               19
#define DWC2_HCTSIZ_PKTCNT_MASK                (0x3FFUL << DWC2_HCTSIZ_PKTCNT_SHIFT)
#define DWC2_HCTSIZ_XFERSIZE_MASK              0x7FFFFUL

#define DWC2_EPTYPE_CONTROL                    0
#define DWC2_EPTYPE_ISOCHRONOUS                1
#define DWC2_EPTYPE_BULK                       2
#define DWC2_EPTYPE_INTERRUPT                  3
