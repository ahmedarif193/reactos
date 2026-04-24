/*
 * PROJECT:     ReactOS SD/SDIO/eMMC Driver Stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Internal SD definitions shared across the driver stack
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#ifndef _REACTOS_SD_SDDEF_H_
#define _REACTOS_SD_SDDEF_H_

/**
 * @brief SD/MMC card type enumeration.
 *
 * Identifies the type of card detected during enumeration.
 */
typedef enum _SD_CARD_TYPE {
    SdCardTypeUnknown = 0,
    SdCardTypeSdV1,         /**< SD v1.x (byte addressing, max 2GB) */
    SdCardTypeSdV2,         /**< SDSC v2.0 (byte addressing, max 2GB) */
    SdCardTypeSdhc,         /**< SDHC (block addressing, 2GB-32GB) */
    SdCardTypeSdxc,         /**< SDXC (block addressing, 32GB-2TB) */
    SdCardTypeSdio,         /**< SDIO function card */
    SdCardTypeCombo,        /**< SD combo card (memory + SDIO) */
    SdCardTypeMmc,          /**< Legacy MMC */
    SdCardTypeEmmc          /**< eMMC (block addressing) */
} SD_CARD_TYPE;

/**
 * @brief Card Identification Register (CID) -- 128 bits.
 *
 * Contains manufacturer and product identification fields as defined
 * by the SD Physical Layer Specification.
 */
typedef struct _SD_CID {
    UCHAR  ManufacturerId;              /**< MID [127:120] */
    USHORT OemId;                       /**< OID [119:104] */
    UCHAR  ProductName[5];              /**< PNM [103:64] */
    UCHAR  ProductRevision;             /**< PRV [63:56] */
    ULONG  ProductSerialNumber;         /**< PSN [55:24] */
    USHORT ManufacturingDate;           /**< MDT [19:8] */
    UCHAR  Crc7;                        /**< CRC [7:1] + end bit */
} SD_CID, *PSD_CID;

/**
 * @brief CSD Register -- version 1 (SDSC).
 *
 * Card-Specific Data register for standard-capacity SD cards.
 * Used to compute card capacity via C_SIZE, C_SIZE_MULT, and READ_BL_LEN.
 */
typedef struct _SD_CSD_V1 {
    UCHAR  CsdStructure;               /**< CSD_STRUCTURE [127:126] */
    UCHAR  Taac;                        /**< TAAC [119:112] */
    UCHAR  Nsac;                        /**< NSAC [111:104] */
    UCHAR  TranSpeed;                   /**< TRAN_SPEED [103:96] */
    USHORT Ccc;                         /**< CCC [95:84] */
    UCHAR  ReadBlLen;                   /**< READ_BL_LEN [83:80] */
    BOOLEAN ReadBlPartial;              /**< READ_BL_PARTIAL [79] */
    BOOLEAN WriteBlkMisalign;           /**< WRITE_BLK_MISALIGN [78] */
    BOOLEAN ReadBlkMisalign;            /**< READ_BLK_MISALIGN [77] */
    BOOLEAN DsrImp;                     /**< DSR_IMP [76] */
    USHORT CSize;                       /**< C_SIZE [73:62] */
    UCHAR  VddRCurrMin;                /**< VDD_R_CURR_MIN [61:59] */
    UCHAR  VddRCurrMax;                /**< VDD_R_CURR_MAX [58:56] */
    UCHAR  VddWCurrMin;                /**< VDD_W_CURR_MIN [55:53] */
    UCHAR  VddWCurrMax;                /**< VDD_W_CURR_MAX [52:50] */
    UCHAR  CSizeMult;                   /**< C_SIZE_MULT [49:47] */
    BOOLEAN EraseBlkEn;                /**< ERASE_BLK_EN [46] */
    UCHAR  SectorSize;                  /**< SECTOR_SIZE [45:39] */
    UCHAR  WpGrpSize;                   /**< WP_GRP_SIZE [38:32] */
    BOOLEAN WpGrpEnable;               /**< WP_GRP_ENABLE [31] */
    UCHAR  R2wFactor;                   /**< R2W_FACTOR [28:26] */
    UCHAR  WriteBlLen;                  /**< WRITE_BL_LEN [25:22] */
    BOOLEAN WriteBlPartial;            /**< WRITE_BL_PARTIAL [21] */
    BOOLEAN FileFormatGrp;             /**< FILE_FORMAT_GRP [15] */
    BOOLEAN Copy;                       /**< COPY [14] */
    BOOLEAN PermWriteProtect;          /**< PERM_WRITE_PROTECT [13] */
    BOOLEAN TmpWriteProtect;           /**< TMP_WRITE_PROTECT [12] */
    UCHAR  FileFormat;                  /**< FILE_FORMAT [11:10] */
} SD_CSD_V1, *PSD_CSD_V1;

/**
 * @brief CSD Register -- version 2 (SDHC/SDXC).
 *
 * Card-Specific Data register for high-capacity SD cards.
 * Capacity is computed as (C_SIZE + 1) * 512KB.
 */
typedef struct _SD_CSD_V2 {
    UCHAR  CsdStructure;               /**< CSD_STRUCTURE [127:126] = 1 */
    UCHAR  Taac;                        /**< TAAC [119:112] = 0x0E */
    UCHAR  Nsac;                        /**< NSAC [111:104] = 0x00 */
    UCHAR  TranSpeed;                   /**< TRAN_SPEED [103:96] */
    USHORT Ccc;                         /**< CCC [95:84] */
    UCHAR  ReadBlLen;                   /**< READ_BL_LEN [83:80] = 9 */
    BOOLEAN ReadBlPartial;              /**< READ_BL_PARTIAL [79] = 0 */
    BOOLEAN WriteBlkMisalign;           /**< WRITE_BLK_MISALIGN [78] = 0 */
    BOOLEAN ReadBlkMisalign;            /**< READ_BLK_MISALIGN [77] = 0 */
    BOOLEAN DsrImp;                     /**< DSR_IMP [76] */
    ULONG  CSize;                       /**< C_SIZE [69:48] -- up to 22 bits */
    BOOLEAN EraseBlkEn;                /**< ERASE_BLK_EN [46] = 1 */
    UCHAR  SectorSize;                  /**< SECTOR_SIZE [45:39] = 0x7F */
    UCHAR  WpGrpSize;                   /**< WP_GRP_SIZE [38:32] = 0 */
    BOOLEAN WpGrpEnable;               /**< WP_GRP_ENABLE [31] = 0 */
    UCHAR  R2wFactor;                   /**< R2W_FACTOR [28:26] = 2 */
    UCHAR  WriteBlLen;                  /**< WRITE_BL_LEN [25:22] = 9 */
    BOOLEAN WriteBlPartial;            /**< WRITE_BL_PARTIAL [21] = 0 */
    BOOLEAN FileFormatGrp;             /**< FILE_FORMAT_GRP [15] = 0 */
    BOOLEAN Copy;                       /**< COPY [14] */
    BOOLEAN PermWriteProtect;          /**< PERM_WRITE_PROTECT [13] */
    BOOLEAN TmpWriteProtect;           /**< TMP_WRITE_PROTECT [12] */
    UCHAR  FileFormat;                  /**< FILE_FORMAT [11:10] = 0 */
} SD_CSD_V2, *PSD_CSD_V2;

/**
 * @brief Unified CSD structure supporting both v1 and v2 layouts.
 */
typedef struct _SD_CSD {
    UCHAR CsdVersion;                   /**< 0 = v1, 1 = v2, 2 = v3 (SDUC) */
    union {
        SD_CSD_V1 V1;                  /**< CSD v1 (SDSC) fields */
        SD_CSD_V2 V2;                  /**< CSD v2 (SDHC/SDXC) fields */
    };
    ULONG Raw[4];                       /**< Raw 128-bit CSD register data */
} SD_CSD, *PSD_CSD;

/**
 * @brief SD Card Configuration Register (SCR) -- 64 bits.
 *
 * Contains information about the SD card's supported features
 * including bus width support and specification version.
 */
typedef struct _SD_SCR {
    UCHAR  ScrStructure;               /**< SCR_STRUCTURE [63:60] */
    UCHAR  SdSpec;                      /**< SD_SPEC [59:56] */
    BOOLEAN DataStatAfterErase;        /**< DATA_STATUS_AFTER_ERASE [55] */
    UCHAR  SdSecurity;                  /**< SD_SECURITY [54:52] */
    UCHAR  SdBusWidths;                /**< SD_BUS_WIDTHS [51:48] */
    BOOLEAN SdSpec3;                   /**< SD_SPEC3 [47] */
    UCHAR  ExSecurity;                  /**< EX_SECURITY [46:43] */
    BOOLEAN SdSpec4;                   /**< SD_SPECX [42] bit 0 */
    UCHAR  SdSpecX;                    /**< SD_SPECX [41:38] */
    UCHAR  CmdSupport;                  /**< CMD_SUPPORT [33:32] */
    ULONG Raw[2];                       /**< Raw 64-bit SCR register data */
} SD_SCR, *PSD_SCR;

/** @brief SCR bus width support: 1-bit mode. */
#define SD_SCR_BUS_WIDTH_1              0x01
/** @brief SCR bus width support: 4-bit mode. */
#define SD_SCR_BUS_WIDTH_4              0x04

/*
 * Capacity calculation helpers
 */

/**
 * @brief Calculate SDSC (CSD v1) card capacity in bytes.
 *
 * capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN
 *
 * @param csd  Pointer to an SD_CSD_V1 structure.
 */
#define SD_CSD_V1_CAPACITY(csd) \
    ((ULONGLONG)((csd)->CSize + 1) * (1ULL << ((csd)->CSizeMult + 2)) * (1ULL << (csd)->ReadBlLen))

/**
 * @brief Calculate SDHC/SDXC (CSD v2) card capacity in bytes.
 *
 * capacity = (C_SIZE + 1) * 512KB
 *
 * @param csd  Pointer to an SD_CSD_V2 structure.
 */
#define SD_CSD_V2_CAPACITY(csd) \
    ((ULONGLONG)((csd)->CSize + 1) * 512ULL * 1024ULL)

/**
 * @brief Calculate SDHC/SDXC (CSD v2) sector count.
 *
 * sectors = (C_SIZE + 1) * 1024
 *
 * @param csd  Pointer to an SD_CSD_V2 structure.
 */
#define SD_CSD_V2_SECTORS(csd) \
    ((ULONGLONG)((csd)->CSize + 1) * 1024ULL)

/**
 * @brief Extract eMMC sector count from EXT_CSD bytes 212-215.
 *
 * Assembled byte-by-byte to avoid unaligned access faults on ARM.
 *
 * @param extcsd  Pointer to the 512-byte EXT_CSD register array.
 */
#define EMMC_SECTOR_COUNT(extcsd) \
    ((ULONG)(extcsd)[EMMC_EXT_CSD_SEC_COUNT] | \
     ((ULONG)(extcsd)[EMMC_EXT_CSD_SEC_COUNT + 1] << 8) | \
     ((ULONG)(extcsd)[EMMC_EXT_CSD_SEC_COUNT + 2] << 16) | \
     ((ULONG)(extcsd)[EMMC_EXT_CSD_SEC_COUNT + 3] << 24))

/** @brief Mask for the transfer speed unit field in TRAN_SPEED. */
#define SD_TRAN_SPEED_UNIT_MASK         0x07
/** @brief Mask for the transfer speed time value field in TRAN_SPEED. */
#define SD_TRAN_SPEED_TIME_MASK         0x78
/** @brief Bit shift for the transfer speed time value field. */
#define SD_TRAN_SPEED_TIME_SHIFT        3

/*
 * Timeout values
 */

/** @brief Command timeout in milliseconds (1 second). */
#define SD_CMD_TIMEOUT_MS               1000
/** @brief Data transfer timeout in milliseconds (5 seconds). */
#define SD_DATA_TIMEOUT_MS              5000
/** @brief Card initialization timeout in milliseconds (2 seconds for ACMD41/CMD1). */
#define SD_INIT_TIMEOUT_MS              2000
/** @brief Software reset timeout in milliseconds (100ms). */
#define SD_RESET_TIMEOUT_MS             100

/*
 * NTSTATUS codes for SD-specific errors
 */

/** @brief SD command timed out waiting for response. */
#define STATUS_SD_CMD_TIMEOUT           ((NTSTATUS)0xE0040001L)
/** @brief SD command response CRC check failed. */
#define STATUS_SD_CMD_CRC_ERROR         ((NTSTATUS)0xE0040002L)
/** @brief SD data transfer timed out. */
#define STATUS_SD_DATA_TIMEOUT          ((NTSTATUS)0xE0040003L)
/** @brief SD data CRC check failed. */
#define STATUS_SD_DATA_CRC_ERROR        ((NTSTATUS)0xE0040004L)
/** @brief No SD card detected in the slot. */
#define STATUS_SD_CARD_NOT_DETECTED     ((NTSTATUS)0xE0040005L)
/** @brief SD card was removed during operation. */
#define STATUS_SD_CARD_REMOVED          ((NTSTATUS)0xE0040006L)
/** @brief Failed to power the SD bus. */
#define STATUS_SD_BUS_POWER_ERROR       ((NTSTATUS)0xE0040007L)
/** @brief Card type is not supported by this driver stack. */
#define STATUS_SD_UNSUPPORTED_CARD      ((NTSTATUS)0xE0040008L)
/** @brief SD card is write-protected. */
#define STATUS_SD_WRITE_PROTECTED       ((NTSTATUS)0xE0040009L)
/** @brief ADMA engine reported an error. */
#define STATUS_SD_ADMA_ERROR            ((NTSTATUS)0xE004000AL)
/** @brief General SD I/O error. */
#define STATUS_SD_IO_ERROR              ((NTSTATUS)0xE004000BL)
#define STATUS_SD_TUNING_FAILED         ((NTSTATUS)0xE004000CL)
#define STATUS_SD_VOLTAGE_SWITCH_FAILED ((NTSTATUS)0xE004000DL)

#define EMMC_EXT_CSD_CMDQ_MODE_EN          15
#define EMMC_EXT_CSD_CACHE_CTRL            33
#define EMMC_EXT_CSD_PARTITION_CONFIG      179
#define EMMC_EXT_CSD_ERASE_GROUP_DEF       175
#define EMMC_EXT_CSD_BKOPS_EN              163
#define EMMC_EXT_CSD_SANITIZE_START        165
#define EMMC_EXT_CSD_PARTITIONING_SUPPORT  160
#define EMMC_EXT_CSD_RPMB_SIZE_MULT_OFFSET 168
#define EMMC_EXT_CSD_GP_SIZE_MULT_OFFSET   143
#define EMMC_EXT_CSD_BOOT_SIZE_MULT_OFFSET 226

#define EMMC_PARTITION_ACCESS_MASK         0x07

#define EMMC_PARTITION_USER                0
#define EMMC_PARTITION_BOOT0               1
#define EMMC_PARTITION_BOOT1               2
#define EMMC_PARTITION_RPMB                3
#define EMMC_PARTITION_GPP1                4
#define EMMC_PARTITION_GPP2                5
#define EMMC_PARTITION_GPP3                6
#define EMMC_PARTITION_GPP4                7

#define EMMC_SWITCH_ACCESS_CMDSET          0
#define EMMC_SWITCH_ACCESS_SET_BITS        1
#define EMMC_SWITCH_ACCESS_CLEAR_BITS      2
#define EMMC_SWITCH_ACCESS_WRITE_BYTE      3

#define SD_UHS_MODE_SDR12                  0
#define SD_UHS_MODE_SDR25                  1
#define SD_UHS_MODE_SDR50                  2
#define SD_UHS_MODE_SDR104                 3
#define SD_UHS_MODE_DDR50                  4

#define SD_SIGNALING_VOLTAGE_33V           0
#define SD_SIGNALING_VOLTAGE_18V           1

#define EMMC_STATUS_READY_FOR_DATA         0x00000100
#define EMMC_STATUS_CURRENT_STATE_MASK     0x00001E00
#define EMMC_STATUS_CURRENT_STATE_SHIFT    9
#define EMMC_STATE_TRAN                    4
#define EMMC_STATUS_SWITCH_ERROR           0x00000080

#endif /* _REACTOS_SD_SDDEF_H_ */
