/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Driver Stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     SD Host Controller Interface (SDHCI) register definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Based on the SD Host Controller Simplified Specification v3.00
 */

#pragma once

#ifndef _SDHCI_H_
#define _SDHCI_H_

/**
 * @brief SDHCI Register Offsets
 */

#define SDHCI_SDMA_ADDRESS              0x00
#define SDHCI_BLOCK_SIZE                0x04
#define SDHCI_BLOCK_COUNT               0x06
#define SDHCI_ARGUMENT                  0x08
#define SDHCI_TRANSFER_MODE             0x0C
#define SDHCI_COMMAND                   0x0E
#define SDHCI_RESPONSE0                 0x10
#define SDHCI_RESPONSE1                 0x14
#define SDHCI_RESPONSE2                 0x18
#define SDHCI_RESPONSE3                 0x1C
#define SDHCI_BUFFER_DATA_PORT          0x20
#define SDHCI_PRESENT_STATE             0x24
#define SDHCI_HOST_CONTROL              0x28
#define SDHCI_POWER_CONTROL             0x29
#define SDHCI_BLOCK_GAP_CONTROL         0x2A
#define SDHCI_WAKEUP_CONTROL            0x2B
#define SDHCI_CLOCK_CONTROL             0x2C
#define SDHCI_TIMEOUT_CONTROL           0x2E
#define SDHCI_SOFTWARE_RESET            0x2F
#define SDHCI_INT_STATUS                0x30
#define SDHCI_INT_STATUS_ENABLE         0x34
#define SDHCI_INT_SIGNAL_ENABLE         0x38
#define SDHCI_AUTO_CMD_STATUS           0x3C
#define SDHCI_HOST_CONTROL2             0x3E
#define SDHCI_CAPABILITIES              0x40
#define SDHCI_CAPABILITIES2             0x44
#define SDHCI_MAX_CURRENT               0x48
#define SDHCI_MAX_CURRENT2              0x4C
#define SDHCI_FORCE_EVENT_AUTO_CMD      0x50
#define SDHCI_FORCE_EVENT_ERROR         0x52
#define SDHCI_ADMA_ERROR_STATUS         0x54
#define SDHCI_ADMA_ADDRESS_LOW          0x58
#define SDHCI_ADMA_ADDRESS_HIGH         0x5C
#define SDHCI_PRESET_VALUE_0            0x60
#define SDHCI_PRESET_VALUE_1            0x64
#define SDHCI_PRESET_VALUE_2            0x68
#define SDHCI_PRESET_VALUE_3            0x6C
#define SDHCI_SHARED_BUS_CONTROL        0xE0
#define SDHCI_SLOT_INT_STATUS           0xFC
#define SDHCI_HOST_VERSION              0xFE

/**
 * @brief Block Size Register (0x04)
 */

#define SDHCI_BLOCK_SIZE_MASK           0x0FFF
#define SDHCI_BLOCK_SIZE_SDMA_MASK      0x7000
#define SDHCI_BLOCK_SIZE_SDMA_SHIFT     12
#define SDHCI_BLOCK_SIZE_SDMA_4K        0x0000
#define SDHCI_BLOCK_SIZE_SDMA_8K        0x1000
#define SDHCI_BLOCK_SIZE_SDMA_16K       0x2000
#define SDHCI_BLOCK_SIZE_SDMA_32K       0x3000
#define SDHCI_BLOCK_SIZE_SDMA_64K       0x4000
#define SDHCI_BLOCK_SIZE_SDMA_128K      0x5000
#define SDHCI_BLOCK_SIZE_SDMA_256K      0x6000
#define SDHCI_BLOCK_SIZE_SDMA_512K      0x7000

/**
 * @brief Transfer Mode Register (0x0C)
 */

#define SDHCI_TRNS_DMA_ENABLE           0x0001
#define SDHCI_TRNS_BLK_CNT_ENABLE      0x0002
#define SDHCI_TRNS_AUTO_CMD12           0x0004
#define SDHCI_TRNS_AUTO_CMD23           0x0008
#define SDHCI_TRNS_DATA_DIR_READ        0x0010  /* 1 = read, 0 = write */
#define SDHCI_TRNS_MULTI_BLOCK          0x0020

/**
 * @brief Command Register (0x0E)
 */

#define SDHCI_CMD_RESP_NONE             0x0000
#define SDHCI_CMD_RESP_136              0x0001  /* R2 */
#define SDHCI_CMD_RESP_48               0x0002  /* R1, R3, R6, R7 */
#define SDHCI_CMD_RESP_48_BUSY          0x0003  /* R1b */
#define SDHCI_CMD_CRC_CHECK             0x0008
#define SDHCI_CMD_INDEX_CHECK           0x0010
#define SDHCI_CMD_DATA_PRESENT          0x0020
#define SDHCI_CMD_TYPE_NORMAL           0x0000
#define SDHCI_CMD_TYPE_SUSPEND          0x0040
#define SDHCI_CMD_TYPE_RESUME           0x0080
#define SDHCI_CMD_TYPE_ABORT            0x00C0
#define SDHCI_CMD_INDEX_SHIFT           8

#define SDHCI_MAKE_CMD(idx, flags) \
    ((USHORT)(((idx) << SDHCI_CMD_INDEX_SHIFT) | (flags)))

/**
 * @brief Present State Register (0x24)
 */

#define SDHCI_PS_CMD_INHIBIT            0x00000001
#define SDHCI_PS_DATA_INHIBIT           0x00000002
#define SDHCI_PS_DAT_LINE_ACTIVE        0x00000004
#define SDHCI_PS_RE_TUNING_REQUEST      0x00000008
#define SDHCI_PS_WRITE_TRANSFER_ACTIVE  0x00000100
#define SDHCI_PS_READ_TRANSFER_ACTIVE   0x00000200
#define SDHCI_PS_BUFFER_WRITE_ENABLE    0x00000400
#define SDHCI_PS_BUFFER_READ_ENABLE     0x00000800
#define SDHCI_PS_CARD_INSERTED          0x00010000
#define SDHCI_PS_CARD_STATE_STABLE      0x00020000
#define SDHCI_PS_CARD_DETECT_PIN        0x00040000
#define SDHCI_PS_WRITE_PROTECT          0x00080000
#define SDHCI_PS_DAT0_LEVEL             0x00100000
#define SDHCI_PS_DAT1_LEVEL             0x00200000
#define SDHCI_PS_DAT2_LEVEL             0x00400000
#define SDHCI_PS_DAT3_LEVEL             0x00800000
#define SDHCI_PS_CMD_LEVEL              0x01000000

/**
 * @brief Host Control Register (0x28)
 */

#define SDHCI_HC_LED_ON                 0x01
#define SDHCI_HC_DATA_WIDTH_4BIT        0x02
#define SDHCI_HC_HIGH_SPEED             0x04
#define SDHCI_HC_DMA_SELECT_MASK        0x18
#define SDHCI_HC_DMA_SELECT_SDMA        0x00
#define SDHCI_HC_DMA_SELECT_ADMA1       0x08
#define SDHCI_HC_DMA_SELECT_ADMA2       0x10
#define SDHCI_HC_DMA_SELECT_ADMA2_64    0x18
#define SDHCI_HC_DATA_WIDTH_8BIT        0x20
#define SDHCI_HC_CARD_DETECT_TEST       0x40
#define SDHCI_HC_CARD_DETECT_SIGNAL     0x80

/**
 * @brief Power Control Register (0x29)
 */

#define SDHCI_PC_BUS_POWER_ON           0x01
#define SDHCI_PC_BUS_VOLTAGE_MASK       0x0E
#define SDHCI_PC_BUS_VOLTAGE_180        0x0A  /* 1.8V */
#define SDHCI_PC_BUS_VOLTAGE_300        0x0C  /* 3.0V */
#define SDHCI_PC_BUS_VOLTAGE_330        0x0E  /* 3.3V */

/**
 * @brief Clock Control Register (0x2C)
 */

#define SDHCI_CLK_INT_CLK_ENABLE        0x0001
#define SDHCI_CLK_INT_CLK_STABLE        0x0002
#define SDHCI_CLK_SD_CLK_ENABLE         0x0004
#define SDHCI_CLK_PLL_ENABLE            0x0008
#define SDHCI_CLK_GENERATOR_SELECT      0x0020
#define SDHCI_CLK_FREQ_SEL_UPPER_MASK   0x00C0
#define SDHCI_CLK_FREQ_SEL_UPPER_SHIFT  6
#define SDHCI_CLK_FREQ_SEL_MASK         0xFF00
#define SDHCI_CLK_FREQ_SEL_SHIFT        8

/*
 * SDHCI v3 divided-clock encoding. The register stores real_div / 2, with
 * zero selecting the undivided base clock. Pick a real divisor that does not
 * overclock the requested target.
 */
static __inline USHORT
SDHCI_CALC_CLK_DIVIDER(
    _In_ ULONG BaseClockKhz,
    _In_ ULONG TargetClockKhz)
{
    ULONG RealDivisor;

    if (BaseClockKhz == 0 || TargetClockKhz == 0 ||
        BaseClockKhz <= TargetClockKhz)
    {
        return 0;
    }

    for (RealDivisor = 2; RealDivisor < 2046; RealDivisor += 2)
    {
        if ((BaseClockKhz / RealDivisor) <= TargetClockKhz)
        {
            break;
        }
    }

    if (RealDivisor >= 2046)
    {
        RealDivisor = 2046;
    }

    return (USHORT)(RealDivisor >> 1);
}

/**
 * @brief Software Reset Register (0x2F)
 */

#define SDHCI_RESET_ALL                 0x01
#define SDHCI_RESET_CMD                 0x02
#define SDHCI_RESET_DATA                0x04

/**
 * @brief Normal Interrupt Status Register (0x30)
 */

#define SDHCI_INT_CMD_COMPLETE          0x00000001
#define SDHCI_INT_XFER_COMPLETE         0x00000002
#define SDHCI_INT_BLOCK_GAP             0x00000004
#define SDHCI_INT_DMA                   0x00000008
#define SDHCI_INT_BUFFER_WRITE_READY    0x00000010
#define SDHCI_INT_BUFFER_READ_READY     0x00000020
#define SDHCI_INT_CARD_INSERTION        0x00000040
#define SDHCI_INT_CARD_REMOVAL          0x00000080
#define SDHCI_INT_CARD_INTERRUPT        0x00000100
#define SDHCI_INT_INT_A                 0x00000200
#define SDHCI_INT_INT_B                 0x00000400
#define SDHCI_INT_INT_C                 0x00000800
#define SDHCI_INT_RE_TUNING_EVENT       0x00001000
#define SDHCI_INT_ERROR                 0x00008000

/* Error Interrupt Status (upper 16 bits or separate register) */
#define SDHCI_INT_CMD_TIMEOUT           0x00010000
#define SDHCI_INT_CMD_CRC               0x00020000
#define SDHCI_INT_CMD_END_BIT           0x00040000
#define SDHCI_INT_CMD_INDEX             0x00080000
#define SDHCI_INT_DATA_TIMEOUT          0x00100000
#define SDHCI_INT_DATA_CRC              0x00200000
#define SDHCI_INT_DATA_END_BIT          0x00400000
#define SDHCI_INT_CURRENT_LIMIT         0x00800000
#define SDHCI_INT_AUTO_CMD              0x01000000
#define SDHCI_INT_ADMA                  0x02000000
#define SDHCI_INT_TUNING_ERROR          0x04000000
#define SDHCI_INT_VENDOR_MASK           0xF0000000

/* Combined masks */
#define SDHCI_INT_NORMAL_MASK           0x00007FFF
#define SDHCI_INT_ERROR_MASK            0xFFFF0000

#define SDHCI_INT_CMD_ERROR_MASK \
    (SDHCI_INT_CMD_TIMEOUT | SDHCI_INT_CMD_CRC | \
     SDHCI_INT_CMD_END_BIT | SDHCI_INT_CMD_INDEX)

#define SDHCI_INT_DATA_ERROR_MASK \
    (SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC | \
     SDHCI_INT_DATA_END_BIT | SDHCI_INT_ADMA)

#define SDHCI_INT_ALL_MASK              0xFFFFFFFF

/**
 * @brief Host Control 2 Register (0x3E) — UHS-I support
 */

#define SDHCI_HC2_UHS_MODE_MASK         0x0007
#define SDHCI_HC2_UHS_SDR12             0x0000
#define SDHCI_HC2_UHS_SDR25             0x0001
#define SDHCI_HC2_UHS_SDR50             0x0002
#define SDHCI_HC2_UHS_SDR104            0x0003
#define SDHCI_HC2_UHS_DDR50             0x0004
#define SDHCI_HC2_UHS_HS400             0x0005  /* eMMC only */
#define SDHCI_HC2_V18_SIGNAL_ENABLE     0x0008
#define SDHCI_HC2_DRIVER_TYPE_MASK      0x0030
#define SDHCI_HC2_DRIVER_TYPE_B         0x0000
#define SDHCI_HC2_DRIVER_TYPE_A         0x0010
#define SDHCI_HC2_DRIVER_TYPE_C         0x0020
#define SDHCI_HC2_DRIVER_TYPE_D         0x0030
#define SDHCI_HC2_EXEC_TUNING           0x0040
#define SDHCI_HC2_SAMPLING_CLK_SELECT   0x0080
#define SDHCI_HC2_ASYNC_INT_ENABLE      0x4000
#define SDHCI_HC2_PRESET_VALUE_ENABLE   0x8000

/**
 * @brief Capabilities Register (0x40)
 */

#define SDHCI_CAP_TIMEOUT_CLK_MASK      0x0000003F
#define SDHCI_CAP_TIMEOUT_CLK_MHZ       0x00000080
#define SDHCI_CAP_BASE_CLK_MASK         0x0000FF00  /* 8-bit field for SDHCI v3+ */
#define SDHCI_CAP_BASE_CLK_SHIFT        8
#define SDHCI_CAP_MAX_BLOCK_MASK        0x00030000
#define SDHCI_CAP_MAX_BLOCK_SHIFT       16
#define SDHCI_CAP_8BIT_SUPPORT          0x00040000
#define SDHCI_CAP_ADMA2_SUPPORT         0x00080000
#define SDHCI_CAP_ADMA1_SUPPORT         0x00100000  /* Obsolete */
#define SDHCI_CAP_HIGH_SPEED            0x00200000
#define SDHCI_CAP_SDMA_SUPPORT          0x00400000
#define SDHCI_CAP_SUSPEND_RESUME        0x00800000
#define SDHCI_CAP_VOLTAGE_330           0x01000000
#define SDHCI_CAP_VOLTAGE_300           0x02000000
#define SDHCI_CAP_VOLTAGE_180           0x04000000
#define SDHCI_CAP_64BIT_SYSTEM_BUS      0x10000000
#define SDHCI_CAP_ASYNC_INT             0x20000000
#define SDHCI_CAP_SLOT_TYPE_MASK        0xC0000000
#define SDHCI_CAP_SLOT_TYPE_REMOVABLE   0x00000000
#define SDHCI_CAP_SLOT_TYPE_EMBEDDED    0x40000000
#define SDHCI_CAP_SLOT_TYPE_SHARED      0x80000000

/* Capabilities Register 2 (0x44) */
#define SDHCI_CAP2_SDR50_SUPPORT        0x00000001
#define SDHCI_CAP2_SDR104_SUPPORT       0x00000002
#define SDHCI_CAP2_DDR50_SUPPORT        0x00000004
#define SDHCI_CAP2_DRIVER_TYPE_A        0x00000010
#define SDHCI_CAP2_DRIVER_TYPE_C        0x00000020
#define SDHCI_CAP2_DRIVER_TYPE_D        0x00000040
#define SDHCI_CAP2_RETUNE_TIMER_MASK    0x00000F00
#define SDHCI_CAP2_RETUNE_TIMER_SHIFT   8
#define SDHCI_CAP2_USE_TUNING_SDR50     0x00002000
#define SDHCI_CAP2_RETUNE_MODES_MASK    0x0000C000
#define SDHCI_CAP2_CLK_MULTIPLIER_MASK  0x00FF0000
#define SDHCI_CAP2_CLK_MULTIPLIER_SHIFT 16
#define SDHCI_CAP2_HS400_SUPPORT        0x80000000  /* Vendor-specific */

/* Max block length decoding from capabilities */
#define SDHCI_MAX_BLOCK_LENGTH(cap) \
    (512 << (((cap) & SDHCI_CAP_MAX_BLOCK_MASK) >> SDHCI_CAP_MAX_BLOCK_SHIFT))

/* Base clock frequency in MHz from capabilities */
#define SDHCI_BASE_CLK_MHZ(cap) \
    (((cap) & SDHCI_CAP_BASE_CLK_MASK) >> SDHCI_CAP_BASE_CLK_SHIFT)

/**
 * @brief ADMA Error Status Register (0x54)
 */

#define SDHCI_ADMA_ERROR_STATE_MASK     0x03
#define SDHCI_ADMA_ERROR_STATE_STOP     0x00
#define SDHCI_ADMA_ERROR_STATE_FETCH    0x01
#define SDHCI_ADMA_ERROR_STATE_XFER     0x03
#define SDHCI_ADMA_ERROR_LENGTH         0x04

/**
 * @brief Host Controller Version Register (0xFE)
 */

#define SDHCI_VERSION_MASK              0x00FF
#define SDHCI_VENDOR_VERSION_MASK       0xFF00
#define SDHCI_VENDOR_VERSION_SHIFT      8

#define SDHCI_SPEC_100                  0x00
#define SDHCI_SPEC_200                  0x01
#define SDHCI_SPEC_300                  0x02
#define SDHCI_SPEC_400                  0x03
#define SDHCI_SPEC_410                  0x04

/**
 * @brief ADMA2 Descriptor Structure
 */

/* ADMA2 descriptor attribute bits */
#define SDHCI_ADMA2_VALID               0x01
#define SDHCI_ADMA2_END                 0x02
#define SDHCI_ADMA2_INT                 0x04
#define SDHCI_ADMA2_ACT_NOP             0x00
#define SDHCI_ADMA2_ACT_RSRVD          0x10
#define SDHCI_ADMA2_ACT_TRAN            0x20
#define SDHCI_ADMA2_ACT_LINK            0x30

/*
 * Maximum data length per ADMA2 descriptor.
 *
 * SDHCI encodes 64 KB as a descriptor length field of 0x0000.
 * Keep this macro at 0x10000 so builders can split requests correctly
 * without producing pathological 0xFFFF + 0x0001 segment pairs.
 */
#define SDHCI_ADMA2_MAX_LENGTH          0x10000

/* 32-bit ADMA2 descriptor */
#include <pshpack1.h>
typedef struct _SDHCI_ADMA2_DESCRIPTOR_32 {
    USHORT Attributes;
    USHORT Length;
    ULONG  Address;
} SDHCI_ADMA2_DESCRIPTOR_32, *PSDHCI_ADMA2_DESCRIPTOR_32;

/* 64-bit ADMA2 descriptor */
typedef struct _SDHCI_ADMA2_DESCRIPTOR_64 {
    USHORT Attributes;
    USHORT Length;
    ULONG  AddressLow;
    ULONG  AddressHigh;
} SDHCI_ADMA2_DESCRIPTOR_64, *PSDHCI_ADMA2_DESCRIPTOR_64;
#include <poppack.h>

/**
 * @brief Auto CMD Error Status Register (0x3C)
 */

#define SDHCI_AUTO_CMD_NOT_EXECUTED     0x0001
#define SDHCI_AUTO_CMD_TIMEOUT          0x0002
#define SDHCI_AUTO_CMD_CRC_ERROR        0x0004
#define SDHCI_AUTO_CMD_END_BIT_ERROR    0x0008
#define SDHCI_AUTO_CMD_INDEX_ERROR      0x0010
#define SDHCI_AUTO_CMD_NOT_ISSUED       0x0080

/**
 * @brief Helper Macros for Register Access
 */

#define SDHCI_READ8(base, reg)          READ_REGISTER_UCHAR((PUCHAR)(base) + (reg))
#define SDHCI_READ16(base, reg)         READ_REGISTER_USHORT((PUSHORT)((PUCHAR)(base) + (reg)))
#define SDHCI_READ32(base, reg)         READ_REGISTER_ULONG((PULONG)((PUCHAR)(base) + (reg)))

#define SDHCI_WRITE8(base, reg, val)    WRITE_REGISTER_UCHAR((PUCHAR)(base) + (reg), (UCHAR)(val))
#define SDHCI_WRITE16(base, reg, val)   WRITE_REGISTER_USHORT((PUSHORT)((PUCHAR)(base) + (reg)), (USHORT)(val))
#define SDHCI_WRITE32(base, reg, val)   WRITE_REGISTER_ULONG((PULONG)((PUCHAR)(base) + (reg)), (ULONG)(val))

/**
 * @brief SD Specification Constants
 */

#define SD_DEFAULT_BLOCK_SIZE           512

/* Maximum clock frequencies (in kHz) */
#define SD_DEFAULT_SPEED_KHZ            25000   /* 25 MHz */
#define SD_HIGH_SPEED_KHZ               50000   /* 50 MHz */
#define SD_UHS_SDR50_KHZ                100000  /* 100 MHz */
#define SD_UHS_SDR104_KHZ               208000  /* 208 MHz */
#define SD_UHS_DDR50_KHZ                50000   /* 50 MHz DDR */
#define MMC_HIGH_SPEED_KHZ              52000   /* 52 MHz */
#define MMC_HS200_KHZ                   200000  /* 200 MHz */
#define MMC_HS400_KHZ                   200000  /* 200 MHz DDR */
#define SD_INIT_CLOCK_KHZ               400     /* 400 kHz init */

/* OCR register bits */
#define SD_OCR_VDD_170_195              0x00000080
#define SD_OCR_VDD_200_210              0x00000100
#define SD_OCR_VDD_210_220              0x00000200
#define SD_OCR_VDD_220_230              0x00000400
#define SD_OCR_VDD_230_240              0x00000800
#define SD_OCR_VDD_240_250              0x00001000
#define SD_OCR_VDD_250_260              0x00002000
#define SD_OCR_VDD_260_270              0x00004000
#define SD_OCR_VDD_270_280              0x00008000
#define SD_OCR_VDD_280_290              0x00010000
#define SD_OCR_VDD_290_300              0x00020000
#define SD_OCR_VDD_300_310              0x00040000
#define SD_OCR_VDD_310_320              0x00080000
#define SD_OCR_VDD_320_330              0x00100000
#define SD_OCR_VDD_330_340              0x00200000
#define SD_OCR_VDD_340_350              0x00400000
#define SD_OCR_VDD_350_360              0x00800000
#define SD_OCR_S18A                     0x01000000  /* Switching to 1.8V accepted */
#define SD_OCR_XPC                      0x10000000  /* SDXC power control */
#define SD_OCR_CCS                      0x40000000  /* Card Capacity Status (SDHC/SDXC) */
#define SD_OCR_BUSY                     0x80000000  /* Card power up status (busy if 0) */

/* Common OCR voltage range */
#define SD_OCR_VDD_32_33                (SD_OCR_VDD_320_330 | SD_OCR_VDD_330_340)
#define SD_OCR_VDD_RANGE               0x00FF8000  /* 2.7V - 3.6V */

/* MMC OCR bits */
#define MMC_OCR_SECTOR_MODE             0x40000000  /* Sector addressing */
#define MMC_OCR_BUSY                    0x80000000  /* Card power up status */

/* Card status bits (R1 response) */
#define SD_STATUS_OUT_OF_RANGE          0x80000000
#define SD_STATUS_ADDRESS_ERROR         0x40000000
#define SD_STATUS_BLOCK_LEN_ERROR       0x20000000
#define SD_STATUS_ERASE_SEQ_ERROR       0x10000000
#define SD_STATUS_ERASE_PARAM           0x08000000
#define SD_STATUS_WP_VIOLATION          0x04000000
#define SD_STATUS_CARD_IS_LOCKED        0x02000000
#define SD_STATUS_LOCK_UNLOCK_FAILED    0x01000000
#define SD_STATUS_COM_CRC_ERROR         0x00800000
#define SD_STATUS_ILLEGAL_COMMAND       0x00400000
#define SD_STATUS_CARD_ECC_FAILED       0x00200000
#define SD_STATUS_CC_ERROR              0x00100000
#define SD_STATUS_ERROR                 0x00080000
#define SD_STATUS_CSD_OVERWRITE         0x00010000
#define SD_STATUS_WP_ERASE_SKIP         0x00008000
#define SD_STATUS_ERASE_RESET           0x00002000
#define SD_STATUS_CURRENT_STATE_MASK    0x00001E00
#define SD_STATUS_CURRENT_STATE_SHIFT   9
#define SD_STATUS_READY_FOR_DATA        0x00000100
#define SD_STATUS_APP_CMD               0x00000020

#define SD_STATUS_ERROR_MASK \
    (SD_STATUS_OUT_OF_RANGE | SD_STATUS_ADDRESS_ERROR | \
     SD_STATUS_BLOCK_LEN_ERROR | SD_STATUS_ERASE_SEQ_ERROR | \
     SD_STATUS_ERASE_PARAM | SD_STATUS_WP_VIOLATION | \
     SD_STATUS_LOCK_UNLOCK_FAILED | SD_STATUS_COM_CRC_ERROR | \
     SD_STATUS_ILLEGAL_COMMAND | SD_STATUS_CARD_ECC_FAILED | \
     SD_STATUS_CC_ERROR | SD_STATUS_ERROR)

/* Card states from R1 response */
#define SD_STATE_IDLE                   0
#define SD_STATE_READY                  1
#define SD_STATE_IDENT                  2
#define SD_STATE_STANDBY                3
#define SD_STATE_TRANSFER               4
#define SD_STATE_DATA                   5
#define SD_STATE_RECEIVE                6
#define SD_STATE_PROGRAM                7
#define SD_STATE_DISCONNECT             8

/* Extract card state from R1 response */
#define SD_GET_STATE(status) \
    (((status) & SD_STATUS_CURRENT_STATE_MASK) >> SD_STATUS_CURRENT_STATE_SHIFT)

/* CMD8 (SEND_IF_COND) argument bits */
#define SD_CMD8_VHS_27_36               0x00000100  /* 2.7-3.6V */
#define SD_CMD8_CHECK_PATTERN           0x000000AA  /* Recommended check pattern */
#define SD_CMD8_DEFAULT_ARG             (SD_CMD8_VHS_27_36 | SD_CMD8_CHECK_PATTERN)

/* ACMD41 argument bits */
#define SD_ACMD41_HCS                   0x40000000  /* Host Capacity Support (SDHC) */
#define SD_ACMD41_XPC                   0x10000000  /* SDXC Power Control */
#define SD_ACMD41_S18R                  0x01000000  /* Switching to 1.8V request */

/* ACMD6 bus width argument */
#define SD_ACMD6_BUS_WIDTH_1            0x00000000
#define SD_ACMD6_BUS_WIDTH_4            0x00000002

/* eMMC EXT_CSD field offsets */
#define EMMC_EXT_CSD_SEC_COUNT          212 /* 4 bytes: sector count */
#define EMMC_EXT_CSD_DEVICE_TYPE        196 /* 1 byte: device type */
#define EMMC_EXT_CSD_HS_TIMING          185 /* 1 byte: high-speed timing */
#define EMMC_EXT_CSD_BUS_WIDTH          183 /* 1 byte: bus width mode */
#define EMMC_EXT_CSD_REV                192 /* 1 byte: EXT_CSD revision */
#define EMMC_EXT_CSD_POWER_CLASS        187 /* 1 byte: power class */
#define EMMC_EXT_CSD_CMD_SET_REV        189 /* 1 byte: command set revision */
#define EMMC_EXT_CSD_PART_CONFIG        179 /* 1 byte: partition config */
#define EMMC_EXT_CSD_BOOT_SIZE_MULT     226 /* 1 byte: boot partition size */
#define EMMC_EXT_CSD_RPMB_SIZE_MULT     168 /* 1 byte: RPMB partition size */
#define EMMC_EXT_CSD_GP_SIZE_MULT       143 /* 12 bytes: GP partition sizes */

/* eMMC card status bits */
#define MMC_STATUS_SWITCH_ERROR         0x00000080

/* eMMC bus width values for EXT_CSD[183] */
#define EMMC_BUS_WIDTH_1                0x00
#define EMMC_BUS_WIDTH_4                0x01
#define EMMC_BUS_WIDTH_8                0x02
#define EMMC_BUS_WIDTH_4_DDR            0x05
#define EMMC_BUS_WIDTH_8_DDR            0x06

/* eMMC timing modes for EXT_CSD[185] */
#define EMMC_TIMING_LEGACY              0x00
#define EMMC_TIMING_HIGH_SPEED          0x01
#define EMMC_TIMING_HS200               0x02
#define EMMC_TIMING_HS400               0x03

/* eMMC device type bits from EXT_CSD[196] */
#define EMMC_DEVICE_TYPE_HS_26          0x01
#define EMMC_DEVICE_TYPE_HS_52          0x02
#define EMMC_DEVICE_TYPE_DDR_52_18      0x04
#define EMMC_DEVICE_TYPE_DDR_52_12      0x08
#define EMMC_DEVICE_TYPE_HS200_18       0x10
#define EMMC_DEVICE_TYPE_HS200_12       0x20
#define EMMC_DEVICE_TYPE_HS400_18       0x40
#define EMMC_DEVICE_TYPE_HS400_12       0x80

/* SDIO CCCR (Card Common Control Registers) offsets */
#define SDIO_CCCR_REVISION              0x00
#define SDIO_CCCR_SD_SPEC               0x01
#define SDIO_CCCR_IO_ENABLE             0x02
#define SDIO_CCCR_IO_READY              0x03
#define SDIO_CCCR_INT_ENABLE            0x04
#define SDIO_CCCR_INT_PENDING           0x05
#define SDIO_CCCR_IO_ABORT              0x06
#define SDIO_CCCR_BUS_INTERFACE         0x07
#define SDIO_CCCR_CARD_CAPABILITY       0x08
#define SDIO_CCCR_CIS_POINTER           0x09  /* 3 bytes */
#define SDIO_CCCR_BUS_SUSPEND           0x0C
#define SDIO_CCCR_FUNCTION_SELECT       0x0D
#define SDIO_CCCR_EXEC_FLAGS            0x0E
#define SDIO_CCCR_READY_FLAGS           0x0F
#define SDIO_CCCR_FN0_BLOCK_SIZE        0x10  /* 2 bytes */
#define SDIO_CCCR_POWER_CONTROL         0x12
#define SDIO_CCCR_HIGH_SPEED            0x13
#define SDIO_CCCR_UHS_SUPPORT           0x14
#define SDIO_CCCR_DRIVER_STRENGTH       0x15
#define SDIO_CCCR_INT_EXTENSION         0x16

#define SDIO_UHS_SDR50                  0x01
#define SDIO_UHS_SDR104                 0x02
#define SDIO_UHS_DDR50                  0x04

/* SDIO FBR (Function Basic Registers) base offset per function */
#define SDIO_FBR_BASE(fn)               ((fn) * 0x100)
#define SDIO_FBR_STD_INTERFACE          0x00
#define SDIO_FBR_EXT_INTERFACE          0x01
#define SDIO_FBR_POWER                  0x02
#define SDIO_FBR_CIS_POINTER            0x09  /* 3 bytes */
#define SDIO_FBR_CSA_POINTER            0x0C  /* 3 bytes */
#define SDIO_FBR_CSA_DATA               0x0F
#define SDIO_FBR_BLOCK_SIZE             0x10  /* 2 bytes */

#endif /* _SDHCI_H_ */
