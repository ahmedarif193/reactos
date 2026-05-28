/*
 * PROJECT:     ReactOS ARM64 Kernel/Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        sdk/include/reactos/arm64/early_uart.h
 * PURPOSE:     ARM64 early UART with runtime ACPI-based detection
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 *
 * DESCRIPTION:
 *   Early UART for FreeLoader and ntoskrnl. The base address and register
 *   layout are determined at runtime from ACPI SPCR or DBG2 (the only two
 *   firmware-blessed mechanisms for serial console discovery on ARM64).
 *
 *   If neither table is present, or the reported port subtype has no driver, 
 *   the UART stays disabled and all output becomes a no-op.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether early UART discovery has run, and whether we have a usable port.
 * Anything more granular (which RPi model, etc.) belongs in higher layers.
 */
typedef enum _ARM64_PLATFORM_ID
{
    Arm64PlatformUnknown = 0,
    Arm64PlatformGenericAcpi,   /* SPCR/DBG2 gave us a usable serial port */
    Arm64PlatformMax
} ARM64_PLATFORM_ID;

/*
 * UART register interface type. Set from the SPCR InterfaceType / DBG2
 * PortSubtype. Arm64UartUnknown means we have no driver for this port and
 * must not perform I/O on it - even if a base address was reported.
 */
typedef enum _ARM64_UART_INTERFACE
{
    Arm64UartUnknown = 0,
    Arm64UartPl011,
    Arm64UartNs16550,
    Arm64UartQcomGeni,
    Arm64UartBcm2835Mini,
    Arm64UartMax
} ARM64_UART_INTERFACE;

/*
 * PL011 UART register offsets
 */
#define ARM64_PL011_DR              0x000   /* Data Register */
#define ARM64_PL011_FR              0x018   /* Flag Register */
#define ARM64_PL011_IBRD            0x024   /* Integer Baud Rate Divisor */
#define ARM64_PL011_FBRD            0x028   /* Fractional Baud Rate Divisor */
#define ARM64_PL011_LCR_H           0x02C   /* Line Control Register */
#define ARM64_PL011_CR              0x030   /* Control Register */
#define ARM64_PL011_IMSC            0x038   /* Interrupt Mask Set/Clear */

/* Flag Register bits */
#define ARM64_PL011_FR_TXFF         (1U << 5)   /* Transmit FIFO Full */
#define ARM64_PL011_FR_RXFE         (1U << 4)   /* Receive FIFO Empty */
#define ARM64_PL011_FR_BUSY         (1U << 3)   /* UART Busy */

/*
 * NS16550-compatible UART register offsets and bits.
 */
#define ARM64_NS16550_RBR           0x000   /* Receive Buffer Register */
#define ARM64_NS16550_THR           0x000   /* Transmit Holding Register */
#define ARM64_NS16550_LSR           0x005   /* Line Status Register */
#define ARM64_NS16550_LSR_DR        0x01    /* Data Ready */
#define ARM64_NS16550_LSR_THRE      0x20    /* Transmit Holding Register Empty */

/*
 * BCM2835 mini UART register offsets from the AUX base address reported by
 * Raspberry Pi ACPI tables.
 */
#define ARM64_BCM2835_MINI_UART_IO            0x040
#define ARM64_BCM2835_MINI_UART_LSR           0x054
#define ARM64_BCM2835_MINI_UART_LSR_DR        (1U << 0)
#define ARM64_BCM2835_MINI_UART_LSR_TX_EMPTY  (1U << 5)

/*
 * Qualcomm GENI/QUP UART register offsets and bits. This is the FIFO-mode
 * early-console path used by Linux's qcom_geni_serial earlycon driver.
 */
#define ARM64_QCOM_GENI_FORCE_DEFAULT_REG      0x020
#define ARM64_QCOM_GENI_OUTPUT_CTRL            0x024
#define ARM64_QCOM_GENI_CGC_CTRL               0x028
#define ARM64_QCOM_GENI_STATUS                 0x040
#define ARM64_QCOM_GENI_FW_REVISION_RO         0x068
#define ARM64_QCOM_GENI_DMA_MODE_EN            0x258
#define ARM64_QCOM_GENI_BYTE_GRAN              0x254
#define ARM64_QCOM_GENI_TX_PACKING_CFG0        0x260
#define ARM64_QCOM_GENI_TX_PACKING_CFG1        0x264
#define ARM64_QCOM_GENI_RX_PACKING_CFG0        0x284
#define ARM64_QCOM_GENI_RX_PACKING_CFG1        0x288
#define ARM64_QCOM_GENI_M_CMD0                 0x600
#define ARM64_QCOM_GENI_M_CMD_CTRL_REG         0x604
#define ARM64_QCOM_GENI_M_IRQ_STATUS           0x610
#define ARM64_QCOM_GENI_M_IRQ_EN               0x614
#define ARM64_QCOM_GENI_M_IRQ_CLEAR            0x618
#define ARM64_QCOM_GENI_S_CMD0                 0x630
#define ARM64_QCOM_GENI_S_CMD_CTRL_REG         0x634
#define ARM64_QCOM_GENI_S_IRQ_STATUS           0x640
#define ARM64_QCOM_GENI_S_IRQ_EN               0x644
#define ARM64_QCOM_GENI_S_IRQ_CLEAR            0x648
#define ARM64_QCOM_GENI_TX_FIFO                0x700
#define ARM64_QCOM_GENI_RX_FIFO                0x780
#define ARM64_QCOM_GENI_RX_FIFO_STATUS         0x804
#define ARM64_QCOM_GENI_RX_WATERMARK_REG       0x810
#define ARM64_QCOM_GENI_RX_RFR_WATERMARK_REG   0x814
#define ARM64_QCOM_GENI_DMA_TX_IRQ_CLR         0xC44
#define ARM64_QCOM_GENI_DMA_RX_IRQ_CLR         0xD44
#define ARM64_QCOM_GENI_GSI_EVENT_EN           0xE18
#define ARM64_QCOM_GENI_IRQ_EN                 0xE1C
#define ARM64_QCOM_GENI_DMA_GENERAL_CFG        0xE30

#define ARM64_QCOM_GENI_SE_UART                2
#define ARM64_QCOM_GENI_FW_REV_PROTOCOL_MASK   0x0000FF00U
#define ARM64_QCOM_GENI_FW_REV_PROTOCOL_SHIFT  8
#define ARM64_QCOM_GENI_M_CMD_ACTIVE           (1U << 0)
#define ARM64_QCOM_GENI_S_CMD_ACTIVE           (1U << 12)
#define ARM64_QCOM_GENI_DMA_MODE               (1U << 0)
#define ARM64_QCOM_GENI_DEFAULT_CGC_EN         0x7FU
#define ARM64_QCOM_GENI_DEFAULT_IO_OUTPUT_CTRL 0x7FU
#define ARM64_QCOM_GENI_FORCE_DEFAULT          (1U << 0)
#define ARM64_QCOM_GENI_IRQ_ALL                0xFFFFFFFFU
#define ARM64_QCOM_GENI_SE_IRQ_EN_ALL          0xFU
#define ARM64_QCOM_GENI_DMA_GENERAL_CFG_ALL    0xFU
#define ARM64_QCOM_GENI_M_COMMON_IRQ_EN        0x33C0007EU
#define ARM64_QCOM_GENI_S_COMMON_IRQ_EN        0x03003E3EU
#define ARM64_QCOM_GENI_M_CMD_DONE             (1U << 0)
#define ARM64_QCOM_GENI_M_CMD_CANCEL           (1U << 4)
#define ARM64_QCOM_GENI_M_CMD_ABORT            (1U << 5)
#define ARM64_QCOM_GENI_S_CMD_DONE             (1U << 0)
#define ARM64_QCOM_GENI_S_CMD_ABORT            (1U << 5)
#define ARM64_QCOM_GENI_M_CMD_CTRL_CANCEL      (1U << 2)
#define ARM64_QCOM_GENI_M_CMD_CTRL_ABORT       (1U << 1)
#define ARM64_QCOM_GENI_S_CMD_CTRL_ABORT       (1U << 1)
#define ARM64_QCOM_GENI_UART_CTS_MASK          (1U << 1)
#define ARM64_QCOM_GENI_UART_START_TX          0x1U
#define ARM64_QCOM_GENI_UART_START_READ        0x1U
#define ARM64_QCOM_GENI_M_OPCODE_SHIFT         27
#define ARM64_QCOM_GENI_S_OPCODE_SHIFT         27
#define ARM64_QCOM_GENI_TX_PACKING_CFG0_8X4    0x0004380EU
#define ARM64_QCOM_GENI_TX_PACKING_CFG1_8X4    0x000C3E0EU
#define ARM64_QCOM_GENI_RX_LAST                (1U << 31)
#define ARM64_QCOM_GENI_RX_LAST_VALID_MASK     0x70000000U
#define ARM64_QCOM_GENI_RX_LAST_VALID_SHIFT    28
#define ARM64_QCOM_GENI_RX_FIFO_WORD_COUNT     0x01FFFFFFU
#define ARM64_QCOM_GENI_FIFO_WORD_BYTES        4
#define ARM64_QCOM_GENI_POLL_LIMIT             1000000U

#define ARM64_QCOM_UART_TX_TRANS_CFG           0x25C
#define ARM64_QCOM_UART_TX_WORD_LEN            0x268
#define ARM64_QCOM_UART_TX_STOP_BIT_LEN        0x26C
#define ARM64_QCOM_UART_TX_TRANS_LEN           0x270
#define ARM64_QCOM_UART_RX_TRANS_CFG           0x280
#define ARM64_QCOM_UART_RX_WORD_LEN            0x28C
#define ARM64_QCOM_UART_TX_PARITY_CFG          0x2A4
#define ARM64_QCOM_UART_RX_PARITY_CFG          0x2A8

/*
 * Global runtime-detected UART state.
 * These are set during early boot by the detection code.
 *
 * Declaration: Always declare these as extern here.
 * The actual definitions are in:
 *   - Bootloader: boot/freeldr/freeldr/arch/uefi/arm64/early_uart.c
 *   - Kernel: ntoskrnl/arch/arm64/ke/early_uart.c
 */
extern volatile UINT64 EarlyUartBaseAddress;
extern volatile ARM64_PLATFORM_ID EarlyUartPlatformId;
extern volatile ARM64_UART_INTERFACE EarlyUartInterface;
extern volatile BOOLEAN EarlyUartInitialized;
extern volatile BOOLEAN EarlyUartHardwareInitialized;
extern volatile UINT32 EarlyUartRxCachedBytes;
extern volatile UINT32 EarlyUartRxCachedByteCount;

/*
 * Kernel-side physical-map base used by EarlyUartPhysToVa below.
 * Must match ARM64_PHYS_MAP_BASE in ntoskrnl/arch/arm64/ke/boot.c, where the
 * early identity map is established.
 */
#define ARM64_PHYS_MAP_BASE_VA      0xFFFFFC0000000000ULL

/*
 * EarlyUartPhysToVa - Convert physical UART address to kernel virtual address.
 * In the kernel context, UART is accessed via the private physical map.
 * In the bootloader context (pre-MMU or identity mapped), VA == PA.
 */
#if defined(_NTOSKRNL_) || defined(_NTOS_)
#define EarlyUartPhysToVa(PhysAddr) (ARM64_PHYS_MAP_BASE_VA + (PhysAddr))
#else
/* Bootloader uses identity mapping or pre-MMU access */
#define EarlyUartPhysToVa(PhysAddr) (PhysAddr)
#endif

/*
 * Inline UART access macros.
 * These read/write to the runtime-detected UART address.
 *
 * Note: Use ULONG_PTR for portability between bootloader (UEFI) and kernel contexts.
 * UINTN is a UEFI-only type; ULONG_PTR is defined in both environments.
 */
#define EARLY_UART_READ(offset) \
    (*(volatile UINT32*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)))

#define EARLY_UART_WRITE(offset, value) \
    (*(volatile UINT32*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)) = (value))

#define EARLY_UART_READ8(offset) \
    (*(volatile UCHAR*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)))

#define EARLY_UART_WRITE8(offset, value) \
    (*(volatile UCHAR*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)) = (UCHAR)(value))

static __inline BOOLEAN
EarlyUartReady(VOID)
{
    return EarlyUartInitialized &&
           EarlyUartBaseAddress != 0 &&
           EarlyUartInterface != Arm64UartUnknown;
}

static __inline UINT32
EarlyUartQcomGeniReadProtocol(VOID)
{
    return (EARLY_UART_READ(ARM64_QCOM_GENI_FW_REVISION_RO) &
            ARM64_QCOM_GENI_FW_REV_PROTOCOL_MASK) >>
           ARM64_QCOM_GENI_FW_REV_PROTOCOL_SHIFT;
}

static __inline BOOLEAN
EarlyUartQcomGeniPollBit(UINT32 Offset, UINT32 Bit, BOOLEAN Set)
{
    UINT32 Guard;

    for (Guard = ARM64_QCOM_GENI_POLL_LIMIT; Guard > 0; --Guard)
    {
        if ((EARLY_UART_READ(Offset) & Bit) == (Set ? Bit : 0))
            return TRUE;

        __asm__ __volatile__("yield");
    }

    return FALSE;
}

static __inline VOID
EarlyUartQcomGeniPollTxDone(VOID)
{
    if (!EarlyUartQcomGeniPollBit(ARM64_QCOM_GENI_M_IRQ_STATUS,
                                  ARM64_QCOM_GENI_M_CMD_DONE,
                                  TRUE))
    {
        EARLY_UART_WRITE(ARM64_QCOM_GENI_M_CMD_CTRL_REG,
                         ARM64_QCOM_GENI_M_CMD_CTRL_ABORT);
        EarlyUartQcomGeniPollBit(ARM64_QCOM_GENI_M_IRQ_STATUS,
                                 ARM64_QCOM_GENI_M_CMD_ABORT,
                                 TRUE);
        EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_CLEAR,
                         ARM64_QCOM_GENI_M_CMD_ABORT);
    }
}

static __inline VOID
EarlyUartQcomGeniAbortRx(VOID)
{
    EARLY_UART_WRITE(ARM64_QCOM_GENI_S_CMD_CTRL_REG,
                     ARM64_QCOM_GENI_S_CMD_CTRL_ABORT);
    EarlyUartQcomGeniPollBit(ARM64_QCOM_GENI_S_CMD_CTRL_REG,
                             ARM64_QCOM_GENI_S_CMD_CTRL_ABORT,
                             FALSE);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_S_IRQ_CLEAR,
                     ARM64_QCOM_GENI_S_CMD_DONE |
                     ARM64_QCOM_GENI_S_CMD_ABORT);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_FORCE_DEFAULT_REG,
                     ARM64_QCOM_GENI_FORCE_DEFAULT);
}

static __inline BOOLEAN
EarlyUartQcomGeniInitialize(VOID)
{
    UINT32 Value;

    if (EarlyUartHardwareInitialized)
        return TRUE;

    if (EarlyUartQcomGeniReadProtocol() != ARM64_QCOM_GENI_SE_UART)
    {
        EarlyUartInterface = Arm64UartUnknown;
        return FALSE;
    }

    if (EARLY_UART_READ(ARM64_QCOM_GENI_STATUS) & ARM64_QCOM_GENI_M_CMD_ACTIVE)
        EarlyUartQcomGeniPollTxDone();

    if (EARLY_UART_READ(ARM64_QCOM_GENI_STATUS) & ARM64_QCOM_GENI_S_CMD_ACTIVE)
        EarlyUartQcomGeniAbortRx();

    EARLY_UART_WRITE(ARM64_QCOM_GENI_TX_PACKING_CFG0,
                     ARM64_QCOM_GENI_TX_PACKING_CFG0_8X4);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_TX_PACKING_CFG1,
                     ARM64_QCOM_GENI_TX_PACKING_CFG1_8X4);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_RX_PACKING_CFG0,
                     ARM64_QCOM_GENI_TX_PACKING_CFG0_8X4);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_RX_PACKING_CFG1,
                     ARM64_QCOM_GENI_TX_PACKING_CFG1_8X4);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_BYTE_GRAN, 0);

    EARLY_UART_WRITE(ARM64_QCOM_GENI_GSI_EVENT_EN, 0);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_CLEAR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_S_IRQ_CLEAR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_DMA_TX_IRQ_CLR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_DMA_RX_IRQ_CLR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_IRQ_EN,
                     ARM64_QCOM_GENI_SE_IRQ_EN_ALL);

    Value = EARLY_UART_READ(ARM64_QCOM_GENI_CGC_CTRL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_CGC_CTRL,
                     Value | ARM64_QCOM_GENI_DEFAULT_CGC_EN);

    Value = EARLY_UART_READ(ARM64_QCOM_GENI_DMA_GENERAL_CFG);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_DMA_GENERAL_CFG,
                     Value | ARM64_QCOM_GENI_DMA_GENERAL_CFG_ALL);

    EARLY_UART_WRITE(ARM64_QCOM_GENI_OUTPUT_CTRL,
                     ARM64_QCOM_GENI_DEFAULT_IO_OUTPUT_CTRL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_FORCE_DEFAULT_REG,
                     ARM64_QCOM_GENI_FORCE_DEFAULT);

    Value = EARLY_UART_READ(ARM64_QCOM_GENI_IRQ_EN);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_IRQ_EN,
                     Value | ARM64_QCOM_GENI_SE_IRQ_EN_ALL);

    Value = EARLY_UART_READ(ARM64_QCOM_GENI_DMA_MODE_EN);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_DMA_MODE_EN,
                     Value & ~ARM64_QCOM_GENI_DMA_MODE);

    EARLY_UART_WRITE(ARM64_QCOM_GENI_GSI_EVENT_EN, 0);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_RX_WATERMARK_REG, 8);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_RX_RFR_WATERMARK_REG, 14);

    Value = EARLY_UART_READ(ARM64_QCOM_GENI_M_IRQ_EN);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_EN,
                     Value | ARM64_QCOM_GENI_M_COMMON_IRQ_EN);

    Value = EARLY_UART_READ(ARM64_QCOM_GENI_S_IRQ_EN);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_S_IRQ_EN,
                     Value | ARM64_QCOM_GENI_S_COMMON_IRQ_EN);

    EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_CLEAR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_S_IRQ_CLEAR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_DMA_TX_IRQ_CLR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_DMA_RX_IRQ_CLR, ARM64_QCOM_GENI_IRQ_ALL);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_IRQ_EN,
                     ARM64_QCOM_GENI_SE_IRQ_EN_ALL);

    Value = EARLY_UART_READ(ARM64_QCOM_GENI_DMA_MODE_EN);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_DMA_MODE_EN,
                     Value & ~ARM64_QCOM_GENI_DMA_MODE);

    EARLY_UART_WRITE(ARM64_QCOM_UART_TX_TRANS_CFG,
                     ARM64_QCOM_GENI_UART_CTS_MASK);
    EARLY_UART_WRITE(ARM64_QCOM_UART_TX_PARITY_CFG, 0);
    EARLY_UART_WRITE(ARM64_QCOM_UART_RX_TRANS_CFG, 0);
    EARLY_UART_WRITE(ARM64_QCOM_UART_RX_PARITY_CFG, 0);
    EARLY_UART_WRITE(ARM64_QCOM_UART_TX_WORD_LEN, 8);
    EARLY_UART_WRITE(ARM64_QCOM_UART_RX_WORD_LEN, 8);
    EARLY_UART_WRITE(ARM64_QCOM_UART_TX_STOP_BIT_LEN, 0);

    EARLY_UART_WRITE(ARM64_QCOM_GENI_S_CMD0,
                     ARM64_QCOM_GENI_UART_START_READ <<
                     ARM64_QCOM_GENI_S_OPCODE_SHIFT);

    EarlyUartHardwareInitialized = TRUE;
    return TRUE;
}

static __inline VOID
EarlyUartQcomGeniPutc(CHAR Ch)
{
    if (!EarlyUartQcomGeniInitialize())
        return;

    if (EARLY_UART_READ(ARM64_QCOM_GENI_STATUS) & ARM64_QCOM_GENI_M_CMD_ACTIVE)
    {
        EarlyUartQcomGeniPollTxDone();
    }

    if (EARLY_UART_READ(ARM64_QCOM_GENI_STATUS) & ARM64_QCOM_GENI_M_CMD_ACTIVE)
    {
        EARLY_UART_WRITE(ARM64_QCOM_GENI_M_CMD_CTRL_REG,
                         ARM64_QCOM_GENI_M_CMD_CTRL_CANCEL);
        if (!EarlyUartQcomGeniPollBit(ARM64_QCOM_GENI_M_IRQ_STATUS,
                                      ARM64_QCOM_GENI_M_CMD_CANCEL,
                                      TRUE))
        {
            EARLY_UART_WRITE(ARM64_QCOM_GENI_M_CMD_CTRL_REG,
                             ARM64_QCOM_GENI_M_CMD_CTRL_ABORT);
            EarlyUartQcomGeniPollBit(ARM64_QCOM_GENI_M_IRQ_STATUS,
                                     ARM64_QCOM_GENI_M_CMD_ABORT,
                                     TRUE);
            EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_CLEAR,
                             ARM64_QCOM_GENI_M_CMD_ABORT);
        }
        EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_CLEAR,
                         ARM64_QCOM_GENI_M_CMD_CANCEL);
    }

    EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_CLEAR,
                     ARM64_QCOM_GENI_M_CMD_DONE);
    EARLY_UART_WRITE(ARM64_QCOM_UART_TX_TRANS_LEN, 1);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_M_CMD0,
                     ARM64_QCOM_GENI_UART_START_TX <<
                     ARM64_QCOM_GENI_M_OPCODE_SHIFT);
    EARLY_UART_WRITE(ARM64_QCOM_GENI_TX_FIFO, (UINT32)(UCHAR)Ch);
    EarlyUartQcomGeniPollTxDone();
}

static __inline BOOLEAN
EarlyUartQcomGeniGetc(_Out_ UCHAR *Byte)
{
    UINT32 Status;
    UINT32 WordCount;
    UINT32 ByteCount;

    if (!EarlyUartQcomGeniInitialize() || Byte == NULL)
        return FALSE;

    if (EarlyUartRxCachedByteCount == 0)
    {
        Status = EARLY_UART_READ(ARM64_QCOM_GENI_M_IRQ_STATUS);
        EARLY_UART_WRITE(ARM64_QCOM_GENI_M_IRQ_CLEAR, Status);

        Status = EARLY_UART_READ(ARM64_QCOM_GENI_S_IRQ_STATUS);
        EARLY_UART_WRITE(ARM64_QCOM_GENI_S_IRQ_CLEAR, Status);

        Status = EARLY_UART_READ(ARM64_QCOM_GENI_RX_FIFO_STATUS);
        WordCount = Status & ARM64_QCOM_GENI_RX_FIFO_WORD_COUNT;
        if (WordCount == 0)
            return FALSE;

        ByteCount = ARM64_QCOM_GENI_FIFO_WORD_BYTES;
        if ((WordCount == 1) && (Status & ARM64_QCOM_GENI_RX_LAST))
        {
            ByteCount = (Status & ARM64_QCOM_GENI_RX_LAST_VALID_MASK) >>
                        ARM64_QCOM_GENI_RX_LAST_VALID_SHIFT;
            if (ByteCount == 0)
                ByteCount = ARM64_QCOM_GENI_FIFO_WORD_BYTES;
        }

        EarlyUartRxCachedBytes = EARLY_UART_READ(ARM64_QCOM_GENI_RX_FIFO);
        EarlyUartRxCachedByteCount = ByteCount;
    }

    *Byte = (UCHAR)(EarlyUartRxCachedBytes & 0xFF);
    EarlyUartRxCachedBytes >>= 8;
    EarlyUartRxCachedByteCount--;
    return TRUE;
}

/*
 * EarlyUartPutc - Output a single character to the early UART.
 * Waits for transmit FIFO to have space.
 */
static __inline VOID
EarlyUartPutc(CHAR Ch)
{
    if (!EarlyUartReady())
        return;

    switch (EarlyUartInterface)
    {
        case Arm64UartNs16550:
            while (!(EARLY_UART_READ8(ARM64_NS16550_LSR) & ARM64_NS16550_LSR_THRE))
            {
                __asm__ __volatile__("yield");
            }

            EARLY_UART_WRITE8(ARM64_NS16550_THR, (UCHAR)Ch);
            break;

        case Arm64UartBcm2835Mini:
            while (!(EARLY_UART_READ(ARM64_BCM2835_MINI_UART_LSR) &
                     ARM64_BCM2835_MINI_UART_LSR_TX_EMPTY))
            {
                __asm__ __volatile__("yield");
            }

            EARLY_UART_WRITE(ARM64_BCM2835_MINI_UART_IO, (UINT32)(UCHAR)Ch);
            break;

        case Arm64UartQcomGeni:
            EarlyUartQcomGeniPutc(Ch);
            break;

        case Arm64UartPl011:
            while (EARLY_UART_READ(ARM64_PL011_FR) & ARM64_PL011_FR_TXFF)
            {
                __asm__ __volatile__("yield");
            }

            EARLY_UART_WRITE(ARM64_PL011_DR, (UINT32)(UCHAR)Ch);
            break;

        default:
            break;
    }
}

/*
 * EarlyUartGetc - Poll one character from the early UART.
 * Returns TRUE when a byte was read, FALSE if no byte is available.
 */
static __inline BOOLEAN
EarlyUartGetc(_Out_ UCHAR *Byte)
{
    if (!EarlyUartReady() || Byte == NULL)
        return FALSE;

    switch (EarlyUartInterface)
    {
        case Arm64UartNs16550:
            if (!(EARLY_UART_READ8(ARM64_NS16550_LSR) & ARM64_NS16550_LSR_DR))
                return FALSE;

            *Byte = EARLY_UART_READ8(ARM64_NS16550_RBR);
            break;

        case Arm64UartBcm2835Mini:
            if (!(EARLY_UART_READ(ARM64_BCM2835_MINI_UART_LSR) &
                  ARM64_BCM2835_MINI_UART_LSR_DR))
            {
                return FALSE;
            }

            *Byte = (UCHAR)(EARLY_UART_READ(ARM64_BCM2835_MINI_UART_IO) & 0xFF);
            break;

        case Arm64UartQcomGeni:
            return EarlyUartQcomGeniGetc(Byte);

        case Arm64UartPl011:
            if (EARLY_UART_READ(ARM64_PL011_FR) & ARM64_PL011_FR_RXFE)
                return FALSE;

            *Byte = (UCHAR)(EARLY_UART_READ(ARM64_PL011_DR) & 0xFF);
            break;

        default:
            return FALSE;
    }

    return TRUE;
}

/*
 * EarlyUartDrainReceiveFifo - Drop stale input before a protocol takes over.
 */
static __inline VOID
EarlyUartDrainReceiveFifo(VOID)
{
    UCHAR Byte;
    ULONG Guard;

    for (Guard = 2048; Guard > 0; Guard--)
    {
        if (!EarlyUartGetc(&Byte))
            break;
    }
}

/*
 * EarlyUartPuts - Output a null-terminated string to the early UART.
 * Handles CR/LF conversion (adds CR before LF).
 */
static __inline VOID
EarlyUartPuts(const CHAR *String)
{
    if (!EarlyUartReady() || !String)
        return;

    while (*String)
    {
        if (*String == '\n')
            EarlyUartPutc('\r');
        EarlyUartPutc(*String++);
    }
}

/*
 * EarlyUartPutHex - Output a hexadecimal value.
 * Nibbles parameter specifies how many hex digits to output (1-16).
 */
static __inline VOID
EarlyUartPutHex(UINT64 Value, UINT32 Nibbles)
{
    static const CHAR HexDigits[] = "0123456789ABCDEF";
    INT32 Index;

    if (!EarlyUartReady())
        return;

    if (Nibbles > 16)
        Nibbles = 16;

    for (Index = (INT32)Nibbles - 1; Index >= 0; --Index)
    {
        UINT32 Shift = (UINT32)Index * 4;
        EarlyUartPutc(HexDigits[(Value >> Shift) & 0xFULL]);
    }
}

/*
 * EarlyUartPutDec - Output a decimal value (unsigned 32-bit).
 */
static __inline VOID
EarlyUartPutDec(UINT32 Value)
{
    CHAR Buffer[12];  /* Max 10 digits for 32-bit + sign + null */
    UINT32 Pos = 0;

    if (!EarlyUartReady())
        return;

    if (Value == 0)
    {
        EarlyUartPutc('0');
        return;
    }

    /* Build string in reverse */
    while (Value && Pos < sizeof(Buffer) - 1)
    {
        Buffer[Pos++] = (CHAR)('0' + (Value % 10));
        Value /= 10;
    }

    /* Output in correct order */
    while (Pos > 0)
    {
        EarlyUartPutc(Buffer[--Pos]);
    }
}

/*
 * EarlyUartDetectPlatform - Walk ACPI SPCR then DBG2 (in that order). Sets
 * the globals on success. Must be called while UEFI tables are still mapped
 * (i.e. before ExitBootServices). Kernel doesn't re-detect; FreeLDR passes
 * the result through the loader block.
 *
 * Returns Arm64PlatformGenericAcpi on success, Arm64PlatformUnknown if no
 * serial console was discovered.
 */
ARM64_PLATFORM_ID
EarlyUartDetectPlatform(VOID);

/*
 * EarlyUartInitialize / EarlyUartInitializeWithInterface - main entry points.
 *
 *   UartBase == 0:               run EarlyUartDetectPlatform()
 *   UartBase != 0, iface == Unk: caller doesn't know the register layout;
 *                                we record the base but leave the UART off
 *                                (EarlyUartReady() returns FALSE)
 *   UartBase != 0, iface valid:  use as-is (loader-block path)
 *
 * Always returns TRUE; the "initialised" bit just means detection has run.
 * Use EarlyUartReady() to check whether I/O is actually safe.
 */
BOOLEAN
EarlyUartInitialize(UINT64 UartBaseOverride);

BOOLEAN
EarlyUartInitializeWithInterface(
    UINT64 UartBaseOverride,
    ARM64_UART_INTERFACE UartInterfaceOverride);

/*
 * EarlyUartGetBaseAddress - Get the detected UART base address.
 * Returns 0 if not yet detected or detection failed.
 */
static __inline UINT64
EarlyUartGetBaseAddress(VOID)
{
    return EarlyUartBaseAddress;
}

/*
 * EarlyUartIsInitialized - Has detection run (regardless of outcome)?
 * Use EarlyUartReady() above when you actually want to know if I/O is safe.
 */
static __inline BOOLEAN
EarlyUartIsInitialized(VOID)
{
    return EarlyUartInitialized;
}

#ifdef __cplusplus
}
#endif
