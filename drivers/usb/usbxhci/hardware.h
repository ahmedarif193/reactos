/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef XHCI_ALIGN_UP
#define XHCI_ALIGN_UP(_v, _a) (((_v) + ((_a) - 1)) & ~((_a) - 1))
#endif

#define XHCI_MAX_PORTS 32
#define XHCI_MAX_SLOTS 255
#define XHCI_MAX_ENDPOINTS 32
#define XHCI_MAX_SCRATCHPADS 1024
#define XHCI_MAX_DEVICE_ADDRESS 127

#define XHCI_STATIC_EP_RING_TRBS 32
#define XHCI_EXTERNAL_EP_RING_TRBS 64

#define XHCI_COMMAND_RING_TRBS 256
#define XHCI_EVENT_RING_SEGMENT_TRBS 64
#define XHCI_ERST_MAX_ENTRIES 8
#define XHCI_EVENT_RING_TRBS (XHCI_EVENT_RING_SEGMENT_TRBS * XHCI_ERST_MAX_ENTRIES)

#define XHCI_HCS1_MAX_SLOTS(x)     ((x) & 0xFF)
#define XHCI_HCS1_MAX_INTERRUPTS(x)(((x) >> 8) & 0x7FF)
#define XHCI_HCS1_PPC(x)          (((x) >> 3) & 0x1)
#define XHCI_HCS1_MAX_PORTS(x)     (((x) >> 24) & 0xFF)

#define XHCI_HCS2_IST(x)           ((x) & 0xF)
#define XHCI_HCS2_ERST_MAX(x)      (((x) >> 4) & 0xF)
#define XHCI_HCS2_MAX_SCRATCH(x)   (((((x) >> 21) & 0x1F) << 5) | (((x) >> 27) & 0x1F))

#define XHCI_HCS3_U1_LATENCY(x)    ((x) & 0xFF)
#define XHCI_HCS3_U2_LATENCY(x)    (((x) >> 16) & 0xFFFF)

#define XHCI_HCC_64BIT_ADDR(x)     ((x) & 0x1)
#define XHCI_HCC_64B_CONTEXT(x)    (((x) >> 2) & 0x1)
#define XHCI_HCC_PORT_INDICATORS(x)(((x) >> 4) & 0x1)
#define XHCI_HCC_LIGHT_RESET(x)    (((x) >> 5) & 0x1)
#define XHCI_HCC_EXT_CAP_PTR(x)    (((x) >> 16) & 0xFFFF)

#define XHCI_EXT_CAP_ID(cap)       ((cap) & 0xFF)
#define XHCI_EXT_CAP_NEXT(cap)     (((cap) >> 8) & 0xFF)
#define XHCI_EXT_CAP_ID_LEGACY     0x01
#define XHCI_EXT_CAP_ID_PROTOCOL   0x02
#define XHCI_LEGACY_SUPPORT_OFFSET 0x00
#define XHCI_LEGACY_CONTROL_OFFSET 0x04
#define XHCI_HC_BIOS_OWNED         (1u << 16)
#define XHCI_HC_OS_OWNED           (1u << 24)
#define XHCI_LEGACY_DISABLE_SMI   (((0x7u) << 1) | ((0xFFu) << 5) | ((0x7u) << 17))
#define XHCI_LEGACY_SMI_EVENTS     (0x7u << 29)

/*
 * Supported Protocol extended capability (xHCI 1.0+).
 *
 * Each instance describes a protocol revision (USB 2.x, USB 3.x, ...)
 * and the contiguous range of ports that support it.
 */
typedef struct _XHCI_PROTOCOL_CAPABILITY {
    volatile ULONG Revision;
    volatile ULONG NameString;
    volatile ULONG PortInfo;
} XHCI_PROTOCOL_CAPABILITY, *PXHCI_PROTOCOL_CAPABILITY;

#define XHCI_EXT_PORT_MAJOR(_rev)    (((_rev) >> 24) & 0xFF)
#define XHCI_EXT_PORT_MINOR(_rev)    (((_rev) >> 16) & 0xFF)
#define XHCI_EXT_PORT_OFFSET(_info)  ((_info) & 0xFF)
#define XHCI_EXT_PORT_COUNT(_info)   (((_info) >> 8) & 0xFF)

#define XHCI_USBCMD_RS             0x00000001
#define XHCI_USBCMD_HCRST          0x00000002
#define XHCI_USBCMD_INTE           0x00000004
#define XHCI_USBCMD_HSEE           0x00000008
#define XHCI_USBCMD_LHCRST         0x00000080
#define XHCI_USBCMD_CSS            0x00000100
#define XHCI_USBCMD_CRS            0x00000200
#define XHCI_USBCMD_EWE            0x00000400
#define XHCI_USBCMD_EU3S           0x00000800

#define XHCI_USBSTS_HCH            0x00000001
#define XHCI_USBSTS_HSE            0x00000004
#define XHCI_USBSTS_EINT           0x00000008
#define XHCI_USBSTS_PCD            0x00000010
#define XHCI_USBSTS_SSS            0x00000100
#define XHCI_USBSTS_RSS            0x00000200
#define XHCI_USBSTS_SRE            0x00000400
#define XHCI_USBSTS_CNR            0x00000800
#define XHCI_USBSTS_HCE            0x00001000

#define XHCI_PORTSC_CCS            0x00000001
#define XHCI_PORTSC_PED            0x00000002
#define XHCI_PORTSC_OCA            0x00000008
#define XHCI_PORTSC_PR             0x00000010
#define XHCI_PORTSC_PLS_MASK       0x000001E0
#define XHCI_PORTSC_PLS_SHIFT      5
#define XHCI_PORTSC_PP             0x00000200
#define XHCI_PORTSC_SPEED_MASK     0x00003C00
#define XHCI_PORTSC_SPEED_SHIFT    10
#define XHCI_PORTSC_SPEED_FULL     0x1
#define XHCI_PORTSC_SPEED_LOW      0x2
#define XHCI_PORTSC_SPEED_HIGH     0x3
#define XHCI_PORTSC_SPEED_SUPER    0x4
#define XHCI_PORTSC_PLS(value)    (((value) << XHCI_PORTSC_PLS_SHIFT) & XHCI_PORTSC_PLS_MASK)
#define XHCI_PORTSC_PIC_MASK       0x0000C000
#define XHCI_PORTSC_LWS            0x00010000
#define XHCI_PORTSC_CSC            0x00020000
#define XHCI_PORTSC_PEC            0x00040000
#define XHCI_PORTSC_WRC            0x00080000
#define XHCI_PORTSC_OCC            0x00100000
#define XHCI_PORTSC_PRC            0x00200000
#define XHCI_PORTSC_PLC            0x00400000
#define XHCI_PORTSC_CEC            0x00800000
#define XHCI_PORTSC_CHANGE_MASK   (XHCI_PORTSC_CSC  | \
                                   XHCI_PORTSC_PEC  | \
                                   XHCI_PORTSC_WRC  | \
                                   XHCI_PORTSC_OCC  | \
                                   XHCI_PORTSC_PRC  | \
                                   XHCI_PORTSC_PLC  | \
                                   XHCI_PORTSC_CEC)
#define XHCI_PORTSC_CAS            0x01000000
#define XHCI_PORTSC_WCE            0x02000000
#define XHCI_PORTSC_WDE            0x04000000
#define XHCI_PORTSC_WOE            0x08000000
#define XHCI_PORTSC_DR             0x40000000
#define XHCI_PORTSC_WPR            0x80000000
#define XHCI_PORTSC_WRITE_MASK    (0x80FF03FF | XHCI_PORTSC_WCE | \
                                   XHCI_PORTSC_WDE | XHCI_PORTSC_WOE)

/* Per‑port power management (PORTPMSC) – SuperSpeed ports */
#define XHCI_PORTPMSC_U1_TIMEOUT_MASK   0x0000FF00u
#define XHCI_PORTPMSC_U1_TIMEOUT_SHIFT  8
#define XHCI_PORTPMSC_U2_TIMEOUT_MASK   0xFFFF0000u
#define XHCI_PORTPMSC_U2_TIMEOUT_SHIFT  16

#define XHCI_TRB_TYPE_SHIFT        10
#define XHCI_TRB_TYPE_MASK         (0x3F << XHCI_TRB_TYPE_SHIFT)

/* Transfer ring TRB types */
#define XHCI_TRB_TYPE_NORMAL        1
#define XHCI_TRB_TYPE_SETUP_STAGE   2
#define XHCI_TRB_TYPE_DATA_STAGE    3
#define XHCI_TRB_TYPE_STATUS_STAGE  4
#define XHCI_TRB_TYPE_ISOCH         5
#define XHCI_TRB_TYPE_LINK          6
#define XHCI_TRB_TYPE_EVENT_DATA    7
#define XHCI_TRB_TYPE_TR_NOOP       8

/* Isochronous-specific control bits */
#define XHCI_TRB_SIA               0x80000000u  /* Start Isoch Asap */

/* Command ring TRB types */
#define XHCI_TRB_TYPE_ENABLE_SLOT   9
#define XHCI_TRB_TYPE_DISABLE_SLOT 10
#define XHCI_TRB_TYPE_ADDRESS_DEV  11
#define XHCI_TRB_TYPE_CONFIG_EP    12
#define XHCI_TRB_TYPE_EVAL_CTX     13
#define XHCI_TRB_TYPE_RESET_EP     14
#define XHCI_TRB_TYPE_STOP_EP      15
#define XHCI_TRB_TYPE_SET_DEQ      16
#define XHCI_TRB_TYPE_RESET_DEV    17
#define XHCI_TRB_TYPE_FORCE_EVENT  18
#define XHCI_TRB_TYPE_NEG_BANDWIDTH 19
#define XHCI_TRB_TYPE_SET_LT       20
#define XHCI_TRB_TYPE_GET_BW       21
#define XHCI_TRB_TYPE_FORCE_HEADER 22
#define XHCI_TRB_TYPE_CMD_NOOP     23

/* Event TRB types */
#define XHCI_TRB_TYPE_TRANSFER_EVENT       32
#define XHCI_TRB_TYPE_COMMAND_COMPLETION   33
#define XHCI_TRB_TYPE_PORT_STATUS_CHANGE   34
#define XHCI_TRB_TYPE_BANDWIDTH_EVENT      35
#define XHCI_TRB_TYPE_DOORBELL_EVENT       36

#define XHCI_TRB_CYCLE             0x1
#define XHCI_TRB_TOGGLE_CYCLE      (1 << 1)
#define XHCI_TRB_CHAIN_BIT         (1 << 4)
#define XHCI_TRB_IOC               (1 << 5)
#define XHCI_TRB_IDT               (1 << 6)
#define XHCI_TRB_BEI               (1 << 9) // Transfer TRB: Block Event Interrupt
#define XHCI_TRB_DC                (1 << 9) // Configure Endpoint Command TRB: Deconfigure
#define XHCI_TRB_DIR_IN            (1 << 16)
#define XHCI_TRB_INTR_TARGET_SHIFT 22

#define XHCI_TRB_TRT_MASK          (3 << 16)
#define XHCI_TRB_TRT_NO_DATA       (0 << 16)
#define XHCI_TRB_TRT_OUT           (1 << 16)
#define XHCI_TRB_TRT_IN            (2 << 16)

#define XHCI_TRB_TO_SLOT_ID(ctrl)  (((ctrl) >> 24) & 0xFF)
#define XHCI_TRB_TO_EP_ID(ctrl)    (((ctrl) >> 16) & 0x1F)
#define XHCI_COMMAND_SLOT_FIELD(SlotId)   ((ULONG)(SlotId) << 24)
#define XHCI_COMMAND_EP_FIELD(EpId)       ((ULONG)(EpId) << 16)

#define XHCI_ENDPOINT_TYPE_INVALID        0
#define XHCI_ENDPOINT_TYPE_ISOCH_OUT      1
#define XHCI_ENDPOINT_TYPE_BULK_OUT       2
#define XHCI_ENDPOINT_TYPE_INTERRUPT_OUT  3
#define XHCI_ENDPOINT_TYPE_CONTROL        4
#define XHCI_ENDPOINT_TYPE_ISOCH_IN       5
#define XHCI_ENDPOINT_TYPE_BULK_IN        6
#define XHCI_ENDPOINT_TYPE_INTERRUPT_IN   7

#define XHCI_IMAN_IP               0x1
#define XHCI_IMAN_IE               0x2
#define XHCI_ERDP_BUSY             0x8
#define XHCI_IMOD_DEFAULT          0x000001F4

#define XHCI_PAGE_SIZE_4K          0x00000001

typedef struct _XHCI_CAPABILITY_REGISTERS {
    volatile UCHAR CapLength;
    volatile UCHAR Reserved;
    volatile USHORT HciVersion;
    volatile ULONG HcsParams1;
    volatile ULONG HcsParams2;
    volatile ULONG HcsParams3;
    volatile ULONG HccParams;
    volatile ULONG DbOff;
    volatile ULONG Rtsoff;
    volatile ULONG HccParams2;
} XHCI_CAPABILITY_REGISTERS, *PXHCI_CAPABILITY_REGISTERS;

typedef struct _XHCI_PORT_REGISTER {
    volatile ULONG PortStatusAndControl;
    volatile ULONG PortPowerManagement;
    volatile ULONG PortLinkInfo;
    volatile ULONG PortHardwareLC;
} XHCI_PORT_REGISTER, *PXHCI_PORT_REGISTER;

typedef struct _XHCI_OPERATIONAL_REGISTERS {
    volatile ULONG UsbCmd;
    volatile ULONG UsbSts;
    volatile ULONG PageSize;
    volatile ULONG Reserved0;
    volatile ULONG Reserved1;
    volatile ULONG DeviceNotificationControl;
    volatile ULONG CrCrLow;
    volatile ULONG CrCrHigh;
    volatile ULONG Reserved2[4];
    volatile ULONG DcbaapLow;
    volatile ULONG DcbaapHigh;
    volatile ULONG Config;
    volatile ULONG Reserved3[241];
    XHCI_PORT_REGISTER PortRegister[XHCI_MAX_PORTS];
} XHCI_OPERATIONAL_REGISTERS, *PXHCI_OPERATIONAL_REGISTERS;

typedef struct DECLSPEC_ALIGN(16) _XHCI_TRB {
    ULONG Parameter1;
    ULONG Parameter2;
    ULONG Status;
    ULONG Control;
} XHCI_TRB, *PXHCI_TRB;

typedef struct DECLSPEC_ALIGN(16) _XHCI_ERST_ENTRY {
    ULONGLONG RingSegmentBaseAddress;
    ULONG RingSegmentSize;
    ULONG Reserved;
} XHCI_ERST_ENTRY, *PXHCI_ERST_ENTRY;

typedef struct _XHCI_INTERRUPTER_REGISTER_SET {
    volatile ULONG Iman;
    volatile ULONG Imod;
    volatile ULONG ErstSize;
    volatile ULONG Reserved0;
    volatile ULONG ErstBaseLow;
    volatile ULONG ErstBaseHigh;
    volatile ULONG ErdpLow;
    volatile ULONG ErdpHigh;
} XHCI_INTERRUPTER_REGISTER_SET, *PXHCI_INTERRUPTER_REGISTER_SET;

typedef struct _XHCI_RUNTIME_REGISTERS {
    volatile ULONG MicroframeIndex;
    volatile ULONG Reserved[7];
    XHCI_INTERRUPTER_REGISTER_SET Interrupter[1];
} XHCI_RUNTIME_REGISTERS, *PXHCI_RUNTIME_REGISTERS;

typedef struct _XHCI_DOORBELL_ARRAY {
    volatile ULONG Doorbell[XHCI_MAX_SLOTS + 1];
} XHCI_DOORBELL_ARRAY, *PXHCI_DOORBELL_ARRAY;

typedef struct DECLSPEC_ALIGN(32) _XHCI_SLOT_CONTEXT {
    ULONG DevInfo;
    ULONG DevInfo2;
    ULONG TtInfo;
    ULONG DevState;
    ULONG Reserved[4];
} XHCI_SLOT_CONTEXT, *PXHCI_SLOT_CONTEXT;

typedef struct DECLSPEC_ALIGN(32) _XHCI_ENDPOINT_CONTEXT {
    ULONG EpInfo;
    ULONG EpInfo2;
    ULONGLONG TrDequeuePointer;
    ULONG TxInfo;
    ULONG Reserved[3];
} XHCI_ENDPOINT_CONTEXT, *PXHCI_ENDPOINT_CONTEXT;

typedef struct DECLSPEC_ALIGN(64) _XHCI_DEVICE_CONTEXT {
    XHCI_SLOT_CONTEXT SlotContext;
    XHCI_ENDPOINT_CONTEXT EndpointContext[32];
} XHCI_DEVICE_CONTEXT, *PXHCI_DEVICE_CONTEXT;

typedef struct _XHCI_INPUT_CONTROL_CONTEXT {
    ULONG DropContextFlags;
    ULONG AddContextFlags;
    ULONG Reserved[6];
} XHCI_INPUT_CONTROL_CONTEXT, *PXHCI_INPUT_CONTROL_CONTEXT;

typedef struct DECLSPEC_ALIGN(64) _XHCI_INPUT_CONTEXT {
    XHCI_INPUT_CONTROL_CONTEXT InputControlContext;
    XHCI_SLOT_CONTEXT SlotContext;
    XHCI_ENDPOINT_CONTEXT EndpointContext[31];
} XHCI_INPUT_CONTEXT, *PXHCI_INPUT_CONTEXT;

#define XHCI_SLOT_ROUTE_MASK               0x000FFFFF
#define XHCI_SLOT_SPEED_MASK               (0xF << 20)
#define XHCI_SLOT_SPEED_SHIFT              20
#define XHCI_SLOT_MTT_BIT                  (1u << 25)
#define XHCI_SLOT_HUB_BIT                  (1u << 26)
#define XHCI_SLOT_LAST_CTX_MASK            (0x1F << 27)
#define XHCI_SLOT_ROOT_PORT_MASK           (0xFF << 16)
#define XHCI_SLOT_TT_SLOT_MASK             0xFFu
#define XHCI_SLOT_TT_PORT_SHIFT            8
#define XHCI_SLOT_TT_PORT_MASK             (0xFFu << XHCI_SLOT_TT_PORT_SHIFT)
#define XHCI_SLOT_TT_THINK_TIME_SHIFT      16
#define XHCI_SLOT_TT_THINK_TIME_MASK       (0x3u << XHCI_SLOT_TT_THINK_TIME_SHIFT)
#define XHCI_SLOT_MAX_PORTS_SHIFT          24
#define XHCI_SLOT_MAX_PORTS_MASK           (0xFFu << XHCI_SLOT_MAX_PORTS_SHIFT)
#define XHCI_SLOT_MAX_EXIT_LAT_MASK        0xFFFFu

#define XHCI_EPCTX_MULT_SHIFT              8
#define XHCI_EPCTX_MULT_MASK               (0x3 << XHCI_EPCTX_MULT_SHIFT)
#define XHCI_EPCTX_INTERVAL_SHIFT          16
#define XHCI_EPCTX_INTERVAL_MASK           (0xFF << XHCI_EPCTX_INTERVAL_SHIFT)
#define XHCI_EPCTX_ESIT_HI_SHIFT           24
#define XHCI_EPCTX_ESIT_HI_MASK            (0xFF << XHCI_EPCTX_ESIT_HI_SHIFT)
#define XHCI_EPCTX_ERROR_COUNT_SHIFT       1
#define XHCI_EPCTX_TYPE_SHIFT              3
#define XHCI_EPCTX_TYPE_MASK               (0x7 << XHCI_EPCTX_TYPE_SHIFT)
#define XHCI_EPCTX_MAX_BURST_SHIFT         8
#define XHCI_EPCTX_MAX_BURST_MASK          (0xFF << XHCI_EPCTX_MAX_BURST_SHIFT)
#define XHCI_EPCTX_MAX_PACKET_SHIFT        16
#define XHCI_EPCTX_MAX_PACKET_MASK         (0xFFFF << XHCI_EPCTX_MAX_PACKET_SHIFT)
#define XHCI_EPCTX_MAX_ESIT_LO_SHIFT       16
#define XHCI_EPCTX_MAX_ESIT_LO_MASK        (0xFFFF << XHCI_EPCTX_MAX_ESIT_LO_SHIFT)

/* Endpoint context state values (EpInfo bits 0:2) */
#define XHCI_EPCTX_STATE_MASK              0x7u
#define XHCI_EPCTX_STATE_DISABLED          0
#define XHCI_EPCTX_STATE_RUNNING           1
#define XHCI_EPCTX_STATE_HALTED            2
#define XHCI_EPCTX_STATE_STOPPED           3
#define XHCI_EPCTX_STATE_ERROR             4

FORCEINLINE
ULONG
XhciSlotContextGetLastCtx(
    _In_ const XHCI_SLOT_CONTEXT *SlotCtx)
{
    return (SlotCtx->DevInfo & XHCI_SLOT_LAST_CTX_MASK) >> 27;
}

FORCEINLINE
VOID
XhciSlotContextSetLastCtx(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ ULONG LastCtx)
{
    SlotCtx->DevInfo &= ~XHCI_SLOT_LAST_CTX_MASK;
    SlotCtx->DevInfo |= ((LastCtx & 0x1F) << 27);
}

FORCEINLINE
VOID
XhciSlotContextSetRoute(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ ULONG Route)
{
    SlotCtx->DevInfo &= ~XHCI_SLOT_ROUTE_MASK;
    SlotCtx->DevInfo |= (Route & XHCI_SLOT_ROUTE_MASK);
}

FORCEINLINE
VOID
XhciSlotContextSetSpeed(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ ULONG Speed)
{
    SlotCtx->DevInfo &= ~XHCI_SLOT_SPEED_MASK;
    SlotCtx->DevInfo |= (Speed & 0xF) << XHCI_SLOT_SPEED_SHIFT;
}

FORCEINLINE
VOID
XhciSlotContextSetHub(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ BOOLEAN IsHub)
{
    if (IsHub)
        SlotCtx->DevInfo |= XHCI_SLOT_HUB_BIT;
    else
        SlotCtx->DevInfo &= ~XHCI_SLOT_HUB_BIT;
}

FORCEINLINE
VOID
XhciSlotContextSetMtt(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ BOOLEAN Enable)
{
    if (Enable)
        SlotCtx->DevInfo |= XHCI_SLOT_MTT_BIT;
    else
        SlotCtx->DevInfo &= ~XHCI_SLOT_MTT_BIT;
}

FORCEINLINE
VOID
XhciSlotContextSetRootPort(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ ULONG PortNumber)
{
    SlotCtx->DevInfo2 &= ~XHCI_SLOT_ROOT_PORT_MASK;
    SlotCtx->DevInfo2 |= ((PortNumber & 0xFF) << 16);
}

FORCEINLINE
VOID
XhciSlotContextSetMaxPorts(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ ULONG MaxPorts)
{
    SlotCtx->DevInfo2 &= ~XHCI_SLOT_MAX_PORTS_MASK;
    SlotCtx->DevInfo2 |= ((MaxPorts & 0xFF) << XHCI_SLOT_MAX_PORTS_SHIFT);
}

FORCEINLINE
VOID
XhciSlotContextSetMaxExitLatency(
    _Inout_ XHCI_SLOT_CONTEXT *SlotCtx,
    _In_ ULONG Latency)
{
    SlotCtx->DevInfo2 &= ~XHCI_SLOT_MAX_EXIT_LAT_MASK;
    SlotCtx->DevInfo2 |= (Latency & XHCI_SLOT_MAX_EXIT_LAT_MASK);
}

FORCEINLINE
VOID
XhciEndpointContextInit(
    _Inout_ XHCI_ENDPOINT_CONTEXT *EpCtx,
    _In_ ULONG EndpointType,
    _In_ ULONG MaxPacketSize,
    _In_ ULONG MaxBurst,
    _In_ ULONG Interval,
    _In_ ULONG Mult,
    _In_ ULONG MaxEsitPayload,
    _In_ ULONG AverageTrbLength,
    _In_ ULONGLONG DequeuePointer)
{
    ULONG MaxEsitLo = MaxEsitPayload & 0xFFFF;
    ULONG MaxEsitHi = (MaxEsitPayload >> 16) & 0xFF;

    EpCtx->EpInfo = 0;
    EpCtx->EpInfo |= ((Mult & 0x3) << XHCI_EPCTX_MULT_SHIFT);
    EpCtx->EpInfo |= ((Interval & 0xFF) << XHCI_EPCTX_INTERVAL_SHIFT);
    EpCtx->EpInfo |= (MaxEsitHi << XHCI_EPCTX_ESIT_HI_SHIFT);

    EpCtx->EpInfo2 = 0;
    EpCtx->EpInfo2 |= (3u << XHCI_EPCTX_ERROR_COUNT_SHIFT);
    EpCtx->EpInfo2 |= ((EndpointType & 0x7) << XHCI_EPCTX_TYPE_SHIFT);
    EpCtx->EpInfo2 |= ((MaxBurst & 0xFF) << XHCI_EPCTX_MAX_BURST_SHIFT);
    EpCtx->EpInfo2 |= ((MaxPacketSize & 0xFFFF) << XHCI_EPCTX_MAX_PACKET_SHIFT);

    EpCtx->TrDequeuePointer = DequeuePointer;

    EpCtx->TxInfo = 0;
    EpCtx->TxInfo |= (AverageTrbLength & 0xFFFF);
    EpCtx->TxInfo |= (MaxEsitLo << XHCI_EPCTX_MAX_ESIT_LO_SHIFT);
}


#define XHCI_WAIT_HALT_US           (1000 * 1000)
#define XHCI_WAIT_RESET_US          (1000 * 1000)
#define XHCI_WAIT_CNR_US            (1000 * 1000)
#define XHCI_TRB_LEN_MASK         0x1FFFF
#define XHCI_MAX_TRB_TRANSFER_LENGTH XHCI_TRB_LEN_MASK

/* xHCI completion codes (Table 6-90 in xHCI 1.2 spec) */
#define XHCI_COMPLETION_INVALID             0
#define XHCI_COMPLETION_SUCCESS             1
#define XHCI_COMPLETION_DATA_BUFFER_ERROR   2
#define XHCI_COMPLETION_BABBLE_ERROR        3
#define XHCI_COMPLETION_USB_TRANSACTION_ERROR 4
#define XHCI_COMPLETION_CONTEXT_ERROR       5
#define XHCI_COMPLETION_STALL_ERROR         6
#define XHCI_COMPLETION_RESOURCE_ERROR      7
#define XHCI_COMPLETION_BANDWIDTH_ERROR     8
#define XHCI_COMPLETION_NO_SLOTS_ERROR      9
#define XHCI_COMPLETION_INVALID_STREAM_TYPE 10
#define XHCI_COMPLETION_SLOT_NOT_ENABLED    11
#define XHCI_COMPLETION_ENDPOINT_NOT_ENABLED 12
#define XHCI_COMPLETION_SHORT_PACKET        13
#define XHCI_COMPLETION_RING_UNDERRUN       14
#define XHCI_COMPLETION_RING_OVERRUN        15
#define XHCI_COMPLETION_VF_EVENT_RING_FULL  16
#define XHCI_COMPLETION_PARAMETER_ERROR     17
#define XHCI_COMPLETION_BANDWIDTH_OVERRUN   18
#define XHCI_COMPLETION_CONTEXT_STATE_ERROR 19
#define XHCI_COMPLETION_NO_PING_RESPONSE    20
#define XHCI_COMPLETION_EVENT_RING_FULL     21
#define XHCI_COMPLETION_INCOMPATIBLE_DEVICE 22
#define XHCI_COMPLETION_MISSED_SERVICE      23
#define XHCI_COMPLETION_COMMAND_RING_STOPPED 24
#define XHCI_COMPLETION_COMMAND_ABORTED     25
#define XHCI_COMPLETION_STOPPED             26
#define XHCI_COMPLETION_STOPPED_LENGTH_INVALID 27
#define XHCI_COMPLETION_STOPPED_SHORT_PACKET 28
#define XHCI_COMPLETION_MAX_EXIT_LATENCY_ERROR 29
/* 30 is reserved */
#define XHCI_COMPLETION_ISOCH_BUFFER_OVERRUN 31
#define XHCI_COMPLETION_EVENT_LOST          32
#define XHCI_COMPLETION_UNDEFINED_ERROR     33
#define XHCI_COMPLETION_INVALID_STREAM_ID   34
#define XHCI_COMPLETION_SECONDARY_BANDWIDTH 35
#define XHCI_COMPLETION_SPLIT_TRANSACTION   36

#define XHCI_GET_COMPLETION_CODE(Status)    (((Status) >> 24) & 0xFF)

FORCEINLINE
VOID
XhciEndpointContextSetMaxPacketSize(
    _Inout_ XHCI_ENDPOINT_CONTEXT *EpCtx,
    _In_ ULONG MaxPacketSize)
{
    EpCtx->EpInfo2 &= ~0xFFFF0000;
    EpCtx->EpInfo2 |= (MaxPacketSize & 0xFFFF) << 16;
}
