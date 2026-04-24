/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the ReactOS PSDK package.
 * No warranty is given; refer to the file DISCLAIMER within this package.
 */

#pragma once

#ifndef __SDDEF_H__
#define __SDDEF_H__

#ifdef __cplusplus
extern "C" {
#endif

/* SD command code type */
typedef UCHAR SD_COMMAND_CODE;

/* SD command codes - Basic Commands (Class 0) */
#define SDCMD_GO_IDLE_STATE         0   /* CMD0  */
#define SDCMD_SEND_OP_COND          1   /* CMD1 (MMC) */
#define SDCMD_ALL_SEND_CID          2   /* CMD2  */
#define SDCMD_SEND_RELATIVE_ADDR    3   /* CMD3  */
#define SDCMD_SET_DSR               4   /* CMD4  */
#define SDCMD_IO_SEND_OP_COND       5   /* CMD5 (SDIO) */
#define SDCMD_SWITCH_FUNC           6   /* CMD6  */
#define SDCMD_SELECT_CARD           7   /* CMD7  */
#define SDCMD_SEND_IF_COND          8   /* CMD8 (SD v2) / SEND_EXT_CSD (MMC) */
#define SDCMD_SEND_CSD              9   /* CMD9  */
#define SDCMD_SEND_CID              10  /* CMD10 */
#define SDCMD_VOLTAGE_SWITCH        11  /* CMD11 */
#define SDCMD_STOP_TRANSMISSION     12  /* CMD12 */
#define SDCMD_SEND_STATUS           13  /* CMD13 */
#define SDCMD_GO_INACTIVE_STATE     15  /* CMD15 */

/* Block-Oriented Read Commands (Class 2) */
#define SDCMD_SET_BLOCKLEN          16  /* CMD16 */
#define SDCMD_READ_SINGLE_BLOCK     17  /* CMD17 */
#define SDCMD_READ_MULTIPLE_BLOCK   18  /* CMD18 */
#define SDCMD_SEND_TUNING_BLOCK     19  /* CMD19 */
#define SDCMD_SPEED_CLASS_CONTROL   20  /* CMD20 */
#define SDCMD_SEND_TUNING_BLOCK_MMC 21  /* CMD21 (MMC tuning) */

/* Block-Oriented Write Commands (Class 4) */
#define SDCMD_SET_BLOCK_COUNT       23  /* CMD23 */
#define SDCMD_WRITE_BLOCK           24  /* CMD24 */
#define SDCMD_WRITE_MULTIPLE_BLOCK  25  /* CMD25 */
#define SDCMD_PROGRAM_CSD           27  /* CMD27 */

/* Block-Oriented Write Protection Commands (Class 6) */
#define SDCMD_SET_WRITE_PROT        28  /* CMD28 */
#define SDCMD_CLR_WRITE_PROT        29  /* CMD29 */
#define SDCMD_SEND_WRITE_PROT       30  /* CMD30 */

/* Erase Commands (Class 5) */
#define SDCMD_ERASE_WR_BLK_START    32  /* CMD32 */
#define SDCMD_ERASE_WR_BLK_END     33  /* CMD33 */
#define SDCMD_ERASE                 38  /* CMD38 */

/* Lock Card Commands (Class 7) */
#define SDCMD_LOCK_UNLOCK           42  /* CMD42 */

/* I/O Mode Commands (SDIO, Class 9) */
#define SDCMD_IO_RW_DIRECT          52  /* CMD52 */
#define SDCMD_IO_RW_EXTENDED        53  /* CMD53 */

/* Application-Specific Commands (Class 8) */
#define SDCMD_APP_CMD               55  /* CMD55 */
#define SDCMD_GEN_CMD               56  /* CMD56 */

/* Application-Specific Commands (ACMD, preceded by CMD55) */
#define SDACMD_SET_BUS_WIDTH        6   /* ACMD6  */
#define SDACMD_SD_STATUS            13  /* ACMD13 */
#define SDACMD_SEND_NUM_WR_BLOCKS   22  /* ACMD22 */
#define SDACMD_SET_WR_BLK_ERASE_CNT 23 /* ACMD23 */
#define SDACMD_SD_SEND_OP_COND      41  /* ACMD41 */
#define SDACMD_SET_CLR_CARD_DETECT  42  /* ACMD42 */
#define SDACMD_SEND_SCR             51  /* ACMD51 */

/* SD_COMMAND_CLASS - Specifies whether a command is standard or application */
typedef enum {
    SDCC_STANDARD = 0,
    SDCC_APP_CMD
} SD_COMMAND_CLASS;

/* SD_RESPONSE_TYPE - Types of response data from the card */
typedef enum {
    SDRT_UNSPECIFIED = 0,
    SDRT_NONE,
    SDRT_1,
    SDRT_1B,
    SDRT_2,
    SDRT_3,
    SDRT_4,
    SDRT_5,
    SDRT_5B,
    SDRT_6
} SD_RESPONSE_TYPE;

/* SD_TRANSFER_DIRECTION - Direction of data transfer */
typedef enum {
    SDTD_UNSPECIFIED = 0,
    SDTD_READ,
    SDTD_WRITE
} SD_TRANSFER_DIRECTION;

/* SD_TRANSFER_TYPE - Type of data transfer */
typedef enum {
    SDTT_UNSPECIFIED = 0,
    SDTT_CMD_ONLY,
    SDTT_SINGLE_BLOCK,
    SDTT_MULTI_BLOCK,
    SDTT_MULTI_BLOCK_NO_CMD12
} SD_TRANSFER_TYPE;

/* SDCMD_DESCRIPTOR - Defines an SD card command */
typedef struct _SDCMD_DESCRIPTOR {
    SD_COMMAND_CODE       Cmd;
    SD_COMMAND_CLASS      CmdClass;
    SD_TRANSFER_DIRECTION TransferDirection;
    SD_TRANSFER_TYPE      TransferType;
    SD_RESPONSE_TYPE      ResponseType;
} SDCMD_DESCRIPTOR, *PSDCMD_DESCRIPTOR;

/* SD_RW_DIRECT_ARGUMENT - Argument for CMD52 (IO_RW_DIRECT) */
typedef struct _SD_RW_DIRECT_ARGUMENT {
    union {
        struct {
            ULONG Data:8;
            ULONG Reserved1:1;
            ULONG Address:17;
            ULONG Reserved2:1;
            ULONG ReadAfterWrite:1;
            ULONG Function:3;
            ULONG WriteToDevice:1;
        } bits;
        ULONG AsULONG;
    } u;
} SD_RW_DIRECT_ARGUMENT, *PSD_RW_DIRECT_ARGUMENT;

/* SD_RW_EXTENDED_ARGUMENT - Argument for CMD53 (IO_RW_EXTENDED) */
typedef struct _SD_RW_EXTENDED_ARGUMENT {
    union {
        struct {
            ULONG Count:9;
            ULONG Address:17;
            ULONG OpCode:1;
            ULONG BlockMode:1;
            ULONG Function:3;
            ULONG WriteToDevice:1;
        } bits;
        ULONG AsULONG;
    } u;
} SD_RW_EXTENDED_ARGUMENT, *PSD_RW_EXTENDED_ARGUMENT;

/* Response type for Type 3 (OCR) */
typedef struct _SDRESP_TYPE3 {
    ULONG Reserved1:4;
    ULONG VoltageProfile:20;
    ULONG Reserved2:7;
    ULONG PowerState:1;
} SDRESP_TYPE3, *PSDRESP_TYPE3;

typedef struct _OCR_REGISTER_V2 {
    ULONG Reserved1:7;
    ULONG VoltageProfile1:1;
    ULONG Reserved2:7;
    ULONG VoltageProfile2:9;
    ULONG Reserved3:6;
    ULONG CardCapacityStatus:1;
    ULONG PowerState:1;
} OCR_REGISTER_V2, *POCR_REGISTER_V2;

typedef enum _SDBUS_TRANSFER_METHOD {
    SDBUS_TRANSFER_METHOD_UNSPECIFIED = 0,
    SDBUS_TRANSFER_METHOD_PIO,
    SDBUS_TRANSFER_METHOD_DB_DMA,
    SDBUS_TRANSFER_METHOD_ADMA2,
    SDBUS_TRANSFER_METHOD_SYSTEM_DMA
} SDBUS_TRANSFER_METHOD;

typedef enum _SDBUS_SPEED_MODE {
    SDBUS_SPEED_MODE_NORMAL = 0,
    SDBUS_SPEED_MODE_HIGH,
    SDBUS_SPEED_MODE_SDR50,
    SDBUS_SPEED_MODE_SDR104,
    SDBUS_SPEED_MODE_DDR50,
    SDBUS_SPEED_MODE_HS200,
    SDBUS_SPEED_MODE_HS400
} SDBUS_SPEED_MODE;

#define SDBUS_SPEED_MODE_HIGH_MASK   0x01
#define SDBUS_SPEED_MODE_SDR50_MASK  0x02
#define SDBUS_SPEED_MODE_SDR104_MASK 0x04
#define SDBUS_SPEED_MODE_DDR50_MASK  0x08
#define SDBUS_SPEED_MODE_HS200_MASK  0x10
#define SDBUS_SPEED_MODE_HS400_MASK  0x20

typedef struct _SD_SOCKET_DATA {
    ULONGLONG HostRegisterPhysicalBase;
    ULONGLONG HostRegisterPhysicalBaseOriginal;
    SDBUS_TRANSFER_METHOD TransferMethod;
    SDBUS_SPEED_MODE SpeedMode;
    ULONG BusClock;
    ULONG NumHpiIOs;
    USHORT SupportedSpeedModes;
    USHORT SupportedCardSpeedModes;
    BOOLEAN NonremovableDevice;
    BOOLEAN Support8BitBus;
    BOOLEAN SupportWideBus;
    BOOLEAN SupportHpi;
    BOOLEAN UseTuningForSDR50;
    BOOLEAN NeedsSoftwareRetuner;
    BOOLEAN SupportAutoCmd23;
    BOOLEAN SupportAutoCmd12;
    BOOLEAN SupportGPIOCardDetection;
    BOOLEAN SupportWriteProtect;
} SD_SOCKET_DATA, *PSD_SOCKET_DATA;

typedef struct _SDBUS_ADAPTER_SOCKET_INFO {
    SD_SOCKET_DATA SocketData[ANYSIZE_ARRAY];
} SDBUS_ADAPTER_SOCKET_INFO, *PSDBUS_ADAPTER_SOCKET_INFO;

typedef struct _SDSTOR_DEVICE_INFO {
    ULONG PackedCommandLookaheadCount;
    ULONG PackedCommandLookaheadFoundCount;
    ULONG PackedCommandReadIssuedCount;
    ULONG PackedCommandWriteIssuedCount;
    ULONG PackedCommandBackedOutCount;
    ULONG PackedCommandReadCompletedCount;
    ULONG PackedCommandWriteCompletedCount;
    ULONG PackedCommandLastPackedReadIrpsCount;
    ULONG PackedCommandLastPackedWriteIrpsCount;
    BOOLEAN SupportPacked;
    BOOLEAN HostSupportHpi;
} SDSTOR_DEVICE_INFO, *PSDSTOR_DEVICE_INFO;

#ifdef __cplusplus
}
#endif

#endif /* __SDDEF_H__ */
