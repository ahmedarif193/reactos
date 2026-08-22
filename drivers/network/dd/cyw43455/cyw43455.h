/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     FullMAC SDIO WiFi driver for the Raspberry Pi 4/5 CYW43455
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _CYW43455_H_
#define _CYW43455_H_

#include <ntddk.h>
#include <ndis.h>
#include <windot11.h>
#include <ntddsd.h>

#define CYW_TAG '54YC'

#define CYW_SDIO_VENDOR_BROADCOM        0x02D0
#define CYW_SDIO_DEVICE_43455           0xA9BF
#define CYW_SDIO_DEVICE_4345            0x4345

#define CYW_SDIO_FUNC_BUS               0
#define CYW_SDIO_FUNC_BACKPLANE         1
#define CYW_SDIO_FUNC_RADIO             2

#define CYW_F1_BLOCKSIZE                64
#define CYW_F2_BLOCKSIZE                512

#define SDIO_CCCR_IOEx                  0x02
#define SDIO_CCCR_IORx                  0x03
#define SDIO_CCCR_INTEN                 0x04
#define SDIO_CCCR_INT_PENDING           0x05
#define SDIO_CCCR_IOABORT               0x06
#define SDIO_CCCR_BUS_IF                0x07
#define SDIO_FUNC_ENABLE_1              0x02
#define SDIO_FUNC_ENABLE_2              0x04
#define SDIO_FUNC_READY_1               0x02
#define SDIO_FUNC_READY_2               0x04
#define SDIO_INTR_ENABLE_MASTER         0x01

#define SBSDIO_FUNC1_SBADDRLOW          0x1000A
#define SBSDIO_FUNC1_SBADDRMID          0x1000B
#define SBSDIO_FUNC1_SBADDRHIGH         0x1000C
#define SBSDIO_FUNC1_FRAMECTRL          0x1000D
#define SFC_RF_TERM                     0x02
#define SBSDIO_FUNC1_CHIPCLKCSR         0x1000E
#define SBSDIO_FUNC1_WATERMARK          0x10008
#define SBSDIO_DEVICE_CTL               0x10009
#define SBSDIO_FUNC1_WFRAMEBCLO         0x10019
#define SBSDIO_FUNC1_WFRAMEBCHI         0x1001A
#define SBSDIO_FUNC1_RFRAMEBCLO         0x1001B
#define SBSDIO_FUNC1_RFRAMEBCHI         0x1001C
#define SBSDIO_FUNC1_MESBUSYCTRL        0x1001D
#define SBSDIO_FUNC1_SLEEPCSR           0x1001F

#define CY_43455_F2_WATERMARK           0x60
#define CY_43455_MES_WATERMARK          0x50
#define SBSDIO_MESBUSYCTRL_ENAB         0x80
#define SBSDIO_DEVCTL_F2WM_ENAB         0x10

#define SBSDIO_SB_OFT_ADDR_MASK         0x07FFF
#define SBSDIO_SB_OFT_ADDR_LIMIT        0x08000
#define SBSDIO_SB_ACCESS_2_4B_FLAG      0x08000
#define SBSDIO_SBWINDOW_MASK            0xFFFF8000

#define SBSDIO_FORCE_ALP                0x01
#define SBSDIO_FORCE_HT                 0x02
#define SBSDIO_ALP_AVAIL_REQ            0x08
#define SBSDIO_HT_AVAIL_REQ             0x10
#define SBSDIO_FORCE_HW_CLKREQ_OFF      0x20
#define SBSDIO_ALP_AVAIL                0x40
#define SBSDIO_HT_AVAIL                 0x80
#define SBSDIO_CSR_MASK                 0x1F
#define SBSDIO_AVAIL_MASK               (SBSDIO_ALP_AVAIL | SBSDIO_HT_AVAIL)

#define SI_ENUM_BASE_DEFAULT            0x18000000

#define CID_ID_MASK                     0x0000FFFF
#define CID_REV_MASK                    0x000F0000
#define CID_REV_SHIFT                   16
#define CID_TYPE_MASK                   0xF0000000
#define CID_TYPE_SHIFT                  28

#define BRCM_CC_4345_CHIP_ID            0x4345

#define BCMA_CORE_CHIPCOMMON            0x800
#define BCMA_CORE_INTERNAL_MEM          0x80E
#define BCMA_CORE_SDIO_DEV              0x829
#define BCMA_CORE_ARM_CR4               0x83E
#define BCMA_CORE_SYS_MEM               0x849

#define CYW43455_RAMBASE                0x198000
#define CYW43455_RAMSIZE                0x0C8000

#define ARMCR4_BCMA_IOCTL_CPUHALT       0x20

#define SD_REG_INTSTATUS                0x020
#define SD_REG_HOSTINTMASK              0x024
#define SD_REG_TOSBMAILBOX              0x040
#define SD_REG_TOSBMAILBOXDATA          0x048
#define SD_REG_TOHOSTMAILBOXDATA        0x04C
#define SMB_DATA_VERSION                0x00040000
#define I_HMB_FRAME_IND                 0x40
#define I_HMB_HOST_INT                  0x80
#define I_HMB_SW_MASK                   0xF0
#define I_CHIPACTIVE                    0x20000000
#define CYW_HOSTINTMASK                 (I_HMB_SW_MASK | I_CHIPACTIVE)
#define SMB_NAK                         0x00000001
#define SMB_INT_ACK                     0x00000002
#define CYW_RXFLUSH_RETRIES             0xFFFF
#define SDPCM_HDRLEN_BASE               12
#define CYW_RX_TIMER_RES                10000

/* brcmfmac arms BRCMF_ESCAN_TIMER_INTERVAL_MS (10 s) for a full sweep; a
 * shorter fallback completes the scan while the firmware is still reporting
 * the 5 GHz half, which the DFS passive dwells push to the end. */
#define CYW_ESCAN_TIMEOUT_SLICE         -5000000LL
#define CYW_ESCAN_TIMEOUT_SLICES        20

#define BCM_CORE_REG_BANKIDX            0x40
#define BCM_CORE_REG_BANKPDA            0x4C

#define CC_EROMPTR                      0x0FC
#define DMP_DESC_TYPE_MSK               0x0000000F
#define DMP_DESC_EMPTY                  0x00000000
#define DMP_DESC_VALID                  0x00000001
#define DMP_DESC_COMPONENT              0x00000001
#define DMP_DESC_MASTER_PORT            0x00000003
#define DMP_DESC_ADDRESS                0x00000005
#define DMP_DESC_ADDRSIZE_GT32          0x00000008
#define DMP_DESC_EOT                    0x0000000F
#define DMP_COMP_PARTNUM                0x000FFF00
#define DMP_COMP_PARTNUM_S              8
#define DMP_COMP_NUM_MPORT              0x000001F0
#define DMP_COMP_NUM_MPORT_S            4
#define DMP_COMP_NUM_MWRAP              0x0007C000
#define DMP_COMP_NUM_MWRAP_S            14
#define DMP_COMP_NUM_SWRAP              0x00F80000
#define DMP_COMP_NUM_SWRAP_S            19
#define DMP_SLAVE_ADDR_BASE             0xFFFFF000
#define DMP_SLAVE_TYPE                  0x000000C0
#define DMP_SLAVE_TYPE_S                6
#define DMP_SLAVE_TYPE_SLAVE            0
#define DMP_SLAVE_TYPE_SWRAP            2
#define DMP_SLAVE_TYPE_MWRAP            3
#define DMP_SLAVE_SIZE_4K               0
#define DMP_SLAVE_SIZE_8K               1
#define DMP_SLAVE_SIZE_TYPE             0x00000030
#define DMP_SLAVE_SIZE_TYPE_S           4
#define DMP_SLAVE_SIZE_DESC             3
#define BCMA_IOCTL                      0x408
#define BCMA_RESET_CTL                  0x800
#define BCMA_RESET_CTL_RESET            0x0001
#define SICF_FGC                        0x0002
#define SICF_CLOCK_EN                   0x0001

#define CYW_FW_DIR                      L"\\SystemRoot\\system32\\drivers\\brcm\\"
#define CYW_FW_BIN                      L"brcmfmac43455-sdio.bin"
#define CYW_FW_CLM                      L"brcmfmac43455-sdio.clm_blob"
#define CYW_FW_NVRAM                    L"brcmfmac43455-sdio.txt"

#define CYW_NVRAM_TOKEN(words)          ((((~(words)) << 16) & 0xFFFF0000) | ((words) & 0x0000FFFF))

#define CYW_DL_BEGIN                    0x0002
#define CYW_DL_END                      0x0004
#define CYW_DL_TYPE_CLM                 2
#define CYW_DLOAD_HANDLER_VER           1
#define CYW_DLOAD_FLAG_VER_SHIFT        12
#define CYW_CLM_CHUNK_LEN               448

typedef struct _CYW_DLOAD_DATA
{
    USHORT Flag;
    USHORT DloadType;
    ULONG Len;
    ULONG Crc;
    UCHAR Data[1];
} CYW_DLOAD_DATA, *PCYW_DLOAD_DATA;

#define BCDC_PROTO_VER                  2
#define BCDC_DCMD_ERROR                 0x01
#define BCDC_DCMD_SET                   0x02
#define BCDC_DCMD_IF_MASK               0xF000
#define BCDC_DCMD_IF_SHIFT              12
#define BCDC_DCMD_ID_MASK               0xFFFF0000
#define BCDC_DCMD_ID_SHIFT              16

#define BRCMF_C_UP                      2
#define BRCMF_C_DOWN                    3
#define BRCMF_C_GET_RATE                12
#define BRCMF_C_SET_INFRA               20
#define BRCMF_C_GET_BSSID               23
#define BRCMF_C_SET_SSID                26
#define BRCMF_C_GET_CHANNEL             29
#define BRCMF_C_SET_CHANNEL             30
#define BRCMF_C_SET_PASSIVE_SCAN        49
#define BRCMF_C_SCAN                    50
#define BRCMF_C_DISASSOC                52
#define BRCMF_C_GET_PM                  85
#define BRCMF_C_SET_PM                  86
#define BRCMF_C_GET_RSSI                127
#define BRCMF_C_SET_WSEC                134
#define BRCMF_C_SET_BAND                142
#define BRCMF_C_SET_AUTH                22
#define BRCMF_C_SET_WPA_AUTH            165
#define BRCMF_C_GET_VAR                 262
#define BRCMF_C_SET_VAR                 263
#define BRCMF_C_SET_WSEC_PMK            268
#define BRCMF_C_SET_SCB_AUTHORIZE       121


#define CYW_WSEC_NONE                   0x00
#define CYW_WSEC_WEP                    0x01
#define CYW_WSEC_TKIP                   0x02
#define CYW_WSEC_AES                    0x04
#define CYW_WPA_AUTH_DISABLED           0x00
#define CYW_WPA_AUTH_PSK                0x04
#define CYW_WPA2_AUTH_PSK               0x80
#define CYW_WPA3_AUTH_SAE_PSK           0x40000
#define CYW_AUTH_OPEN                   0
#define CYW_AUTH_SAE                    3
#define CYW_MFP_NONE                    0
#define CYW_MFP_CAPABLE                 1
#define CYW_MFP_REQUIRED                2

#define CYW_CRYPTO_ALGO_OFF             0
#define CYW_CRYPTO_ALGO_WEP1            1
#define CYW_CRYPTO_ALGO_TKIP            2
#define CYW_CRYPTO_ALGO_WEP128          3
#define CYW_CRYPTO_ALGO_AES_CCM         4
#define CYW_WSEC_PRIMARY_KEY            2

/* brcmf_wsec_pmk_le flags: key material is a cleartext passphrase (SAE) */
#define CYW_WSEC_PASSPHRASE             0x0001
#define CYW_SAE_PASSWORD_MAX            128

/* Firmware-assisted join dwell times (brcmfmac defaults) */
#define CYW_JOIN_ACTIVE_DWELL_MS        320
#define CYW_JOIN_PASSIVE_DWELL_MS       400
#define CYW_JOIN_PROBE_INTERVAL_MS      20

#define CYW_FC0_TYPE_DATA               0x08
#define CYW_FC1_DIR_MASK                0x03
#define CYW_FC1_TODS                    0x01
#define CYW_FC1_FROMDS                  0x02
#define CYW_FC1_PROTECTED               0x40
#define CYW_SNAP_DSAP                   0xAA
#define CYW_SNAP_SSAP                   0xAA
#define CYW_SNAP_CONTROL                0x03
#define CYW_SNAP_OUI_BRIDGE_TUNNEL      0xF8
#define ETH_P_AARP                      0x80F3
#define ETH_P_IPX                       0x8137
#define ETH_P_EAPOL                     0x888E

#define BRCMF_ESCAN_REQ_VERSION         1
#define CYW_BSS_INFO_VERSION            109
#define WL_ESCAN_ACTION_START           1
#define WL_ESCAN_ACTION_CONTINUE        2
#define WL_ESCAN_ACTION_ABORT           3
#define CYW_ESCAN_SYNCID                0x1234

#define DOT11_BSSTYPE_INFRASTRUCTURE    0
#define DOT11_BSSTYPE_INDEPENDENT       1
#define DOT11_BSSTYPE_ANY               2
#define BRCMF_SCANTYPE_ACTIVE           0
#define BRCMF_SCANTYPE_PASSIVE          1
#define BRCMF_SCAN_PARAMS_NSSID_SHIFT   16
#define CYW_SCAN_MAX_SSIDS              10

#define BRCMF_E_SET_SSID                0
#define BRCMF_E_JOIN                    1
#define BRCMF_E_AUTH                    3
#define BRCMF_E_DEAUTH                  5
#define BRCMF_E_DEAUTH_IND              6
#define BRCMF_E_ASSOC                   7
#define BRCMF_E_REASSOC                 9
#define BRCMF_E_REASSOC_IND             10
#define BRCMF_E_DISASSOC                11
#define BRCMF_E_DISASSOC_IND            12
#define BRCMF_E_LINK                    16
#define BRCMF_E_MIC_ERROR               17
#define BRCMF_E_ROAM                    19
#define BRCMF_E_TXFAIL                  20
#define BRCMF_E_EAPOL_MSG               25
#define BRCMF_E_PSK_SUP                 46
#define BRCMF_E_IF                      54
#define BRCMF_E_ESCAN_RESULT            69

#define BRCMF_E_STATUS_SUCCESS          0
#define BRCMF_E_STATUS_PARTIAL          8
#define BRCMF_E_STATUS_FWSUP_COMPLETED  6

#define BRCMF_EVENT_MSG_LINK            0x01

#define ETH_P_LINK_CTL                  0x886C
#define BCMILCP_BCM_SUBTYPE_EVENT       1
#define BCDC_HEADER_LEN                 4

#define CYW_MAX_BSS                     48
#define CYW_MAX_BSS_IE                  512
#define CYW_CONTROL_BUFFER_SIZE         8192
#define CYW_RX_BUFFER_SIZE              262144
#define CYW_RX_POOL_COUNT              384
#define CYW_RX_INDICATE_BATCH           32
#define CYW_REG_SCRATCH_SIZE            64
#define CYW_DEFAULT_COUNTRY             "FR"
#define CYW_5G_PREFER_RSSI              (-75)
#define CYW_CHSPEC_BW_20                0x1000
#define CYW_CHSPEC_BND_2G               0x0000
#define CYW_CHSPEC_BND_5G               0xC000
#define CYW_CHAN_MAX_2G                 14
#define CYW_CHAN_MAX_5G                 177
#define CYW_ADDRESS_LENGTH              6
#define CYW_MTU_SIZE                    1500
#define CYW_MAX_FRAME_SIZE              DOT11_MAX_PDU_SIZE
#define CYW_LINK_SPEED_BPS              54000000ULL
#define CYW_MAX_LINK_SPEED_BPS          433300000ULL

#include <pshpack1.h>
typedef struct _CYW_BCDC_DCMD
{
    ULONG Cmd;
    ULONG Len;
    ULONG Flags;
    ULONG Status;
} CYW_BCDC_DCMD, *PCYW_BCDC_DCMD;

typedef struct _CYW_BCDC_HEADER
{
    UCHAR Flags;
    UCHAR Priority;
    UCHAR Flags2;
    UCHAR DataOffset;
} CYW_BCDC_HEADER, *PCYW_BCDC_HEADER;

typedef struct _CYW_ETHER_HEADER
{
    UCHAR Dest[6];
    UCHAR Src[6];
    USHORT Type;
} CYW_ETHER_HEADER, *PCYW_ETHER_HEADER;

typedef struct _CYW_DOT11_HEADER
{
    UCHAR FrameControl[2];
    UCHAR DurationId[2];
    UCHAR Address1[6];
    UCHAR Address2[6];
    UCHAR Address3[6];
    UCHAR SequenceControl[2];
} CYW_DOT11_HEADER, *PCYW_DOT11_HEADER;

typedef struct _CYW_SNAP_HEADER
{
    UCHAR Dsap;
    UCHAR Ssap;
    UCHAR Control;
    UCHAR Oui[3];
    UCHAR EtherType[2];
} CYW_SNAP_HEADER, *PCYW_SNAP_HEADER;

typedef struct _CYW_EVENT_MSG
{
    USHORT Version;
    USHORT Flags;
    ULONG EventType;
    ULONG Status;
    ULONG Reason;
    ULONG AuthType;
    ULONG DataLen;
    UCHAR Addr[6];
    char IfName[16];
    UCHAR IfIdx;
    UCHAR BssCfgIdx;
} CYW_EVENT_MSG, *PCYW_EVENT_MSG;

typedef struct _CYW_SSID_LE
{
    ULONG SsidLen;
    UCHAR Ssid[32];
} CYW_SSID_LE, *PCYW_SSID_LE;

typedef struct _CYW_SCAN_PARAMS_LE
{
    CYW_SSID_LE Ssid;
    UCHAR Bssid[6];
    CHAR BssType;
    UCHAR ScanType;
    LONG NProbes;
    LONG ActiveTime;
    LONG PassiveTime;
    LONG HomeTime;
    LONG ChannelNum;
    USHORT ChannelList[1];
} CYW_SCAN_PARAMS_LE, *PCYW_SCAN_PARAMS_LE;

typedef struct _CYW_ESCAN_PARAMS_LE
{
    ULONG Version;
    USHORT Action;
    USHORT SyncId;
    CYW_SCAN_PARAMS_LE Params;
} CYW_ESCAN_PARAMS_LE, *PCYW_ESCAN_PARAMS_LE;

#define CYW_SCAN_PARAMS_FIXED_SIZE   FIELD_OFFSET(CYW_SCAN_PARAMS_LE, ChannelList)
#define CYW_ESCAN_PARAMS_FIXED_SIZE  (FIELD_OFFSET(CYW_ESCAN_PARAMS_LE, Params) + CYW_SCAN_PARAMS_FIXED_SIZE)

typedef struct _CYW_COUNTRY_LE
{
    CHAR CountryAbbrev[4];
    LONG Rev;
    CHAR Ccode[4];
} CYW_COUNTRY_LE, *PCYW_COUNTRY_LE;

/* BCMILCP vendor header between the 802.3 header and the event message
   (fields are big-endian on the wire) */
typedef struct _CYW_BCM_ETH_HEADER
{
    USHORT Subtype;
    USHORT Length;
    UCHAR Version;
    UCHAR Oui[3];
    USHORT UsrSubtype;
} CYW_BCM_ETH_HEADER, *PCYW_BCM_ETH_HEADER;

/* wl_bss_info version 109 as emitted by the 43455 firmware */
typedef struct _CYW_BSS_INFO_LE
{
    ULONG Version;
    ULONG Length;
    UCHAR Bssid[6];
    USHORT BeaconPeriod;
    USHORT Capability;
    UCHAR SsidLen;
    UCHAR Ssid[32];
    UCHAR Pad51;
    ULONG RatesetCount;
    UCHAR RatesetRates[16];
    USHORT Chanspec;
    USHORT AtimWindow;
    UCHAR DtimPeriod;
    UCHAR Pad77;
    SHORT Rssi;
    CHAR PhyNoise;
    UCHAR NCap;
    UCHAR Pad82[2];
    ULONG NbssCap;
    UCHAR CtlCh;
    UCHAR Pad89[3];
    ULONG Reserved32;
    UCHAR Flags;
    UCHAR VhtCap;
    UCHAR Reserved[2];
    UCHAR BasicMcs[16];
    USHORT IeOffset;
    UCHAR Pad118[2];
    ULONG IeLength;
    USHORT Snr;
    UCHAR Pad126[2];
} CYW_BSS_INFO_LE, *PCYW_BSS_INFO_LE;

typedef struct _CYW_ESCAN_RESULT_LE
{
    ULONG BufLen;
    ULONG Version;
    USHORT SyncId;
    USHORT BssCount;
    CYW_BSS_INFO_LE BssInfo[1];
} CYW_ESCAN_RESULT_LE, *PCYW_ESCAN_RESULT_LE;
#include <poppack.h>

C_ASSERT(sizeof(CYW_BCDC_HEADER) == BCDC_HEADER_LEN);
C_ASSERT(sizeof(CYW_ETHER_HEADER) == 14);
C_ASSERT(sizeof(CYW_BCM_ETH_HEADER) == 10);
C_ASSERT(sizeof(CYW_EVENT_MSG) == 48);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, SsidLen) == 18);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, RatesetCount) == 52);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, Chanspec) == 72);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, Rssi) == 78);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, NbssCap) == 84);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, CtlCh) == 88);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, IeOffset) == 116);
C_ASSERT(FIELD_OFFSET(CYW_BSS_INFO_LE, IeLength) == 120);
C_ASSERT(sizeof(CYW_BSS_INFO_LE) == 128);
C_ASSERT(FIELD_OFFSET(CYW_ESCAN_RESULT_LE, BssInfo) == 12);

/* The join/status structures below deliberately use natural alignment: the
 * brcmfmac reference declares them without packing, so the firmware ABI
 * includes the padding the C_ASSERTs below pin down. */

typedef struct _CYW_JOIN_SCAN_PARAMS_LE
{
    UCHAR ScanType;
    LONG NProbes;
    LONG ActiveTime;
    LONG PassiveTime;
    LONG HomeTime;
} CYW_JOIN_SCAN_PARAMS_LE, *PCYW_JOIN_SCAN_PARAMS_LE;

typedef struct _CYW_ASSOC_PARAMS_LE
{
    UCHAR Bssid[6];
    ULONG ChanspecNum;
    USHORT ChanspecList[1];
} CYW_ASSOC_PARAMS_LE, *PCYW_ASSOC_PARAMS_LE;

typedef struct _CYW_JOIN_PARAMS
{
    CYW_SSID_LE Ssid;
    CYW_ASSOC_PARAMS_LE Assoc;
} CYW_JOIN_PARAMS, *PCYW_JOIN_PARAMS;

typedef struct _CYW_EXT_JOIN_PARAMS_LE
{
    CYW_SSID_LE Ssid;
    CYW_JOIN_SCAN_PARAMS_LE Scan;
    CYW_ASSOC_PARAMS_LE Assoc;
} CYW_EXT_JOIN_PARAMS_LE, *PCYW_EXT_JOIN_PARAMS_LE;

typedef struct _CYW_SCB_VAL_LE
{
    ULONG Val;
    UCHAR Ea[6];
} CYW_SCB_VAL_LE, *PCYW_SCB_VAL_LE;

typedef struct _CYW_CHANNEL_INFO_LE
{
    ULONG HwChannel;
    ULONG TargetChannel;
    ULONG ScanChannel;
} CYW_CHANNEL_INFO_LE, *PCYW_CHANNEL_INFO_LE;

typedef struct _CYW_WSEC_PMK_LE
{
    USHORT KeyLen;
    USHORT Flags;
    UCHAR Key[CYW_SAE_PASSWORD_MAX];
} CYW_WSEC_PMK_LE, *PCYW_WSEC_PMK_LE;

C_ASSERT(sizeof(CYW_JOIN_SCAN_PARAMS_LE) == 20);
C_ASSERT(FIELD_OFFSET(CYW_ASSOC_PARAMS_LE, ChanspecNum) == 8);
C_ASSERT(FIELD_OFFSET(CYW_ASSOC_PARAMS_LE, ChanspecList) == 12);
C_ASSERT(FIELD_OFFSET(CYW_EXT_JOIN_PARAMS_LE, Assoc) == 56);
C_ASSERT(sizeof(CYW_SCB_VAL_LE) == 12);
C_ASSERT(sizeof(CYW_WSEC_PMK_LE) == 132);

typedef struct _CYW_WSEC_KEY
{
    ULONG Index;
    ULONG Len;
    UCHAR Data[32];
    ULONG Pad1[18];
    ULONG Algo;
    ULONG Flags;
    ULONG Pad2[3];
    ULONG IvInit;
    ULONG Pad3;
    struct
    {
        ULONG Hi;
        USHORT Lo;
    } Rxiv;
    ULONG Pad4[2];
    UCHAR Ea[6];
} CYW_WSEC_KEY, *PCYW_WSEC_KEY;

typedef struct _CYW_BSS
{
    DOT11_MAC_ADDRESS Bssid;
    DOT11_BSS_TYPE BssType;
    UCHAR Ssid[DOT11_SSID_MAX_LENGTH];
    ULONG SsidLength;
    ULONG ChannelNumber;
    ULONG ChCenterFrequency;
    USHORT Chanspec;
    LONG Rssi;
    ULONG LinkQuality;
    USHORT CapabilityInformation;
    USHORT BeaconPeriod;
    UCHAR IeBlob[CYW_MAX_BSS_IE];
    ULONG IeLength;
} CYW_BSS, *PCYW_BSS;

typedef struct _CYW_RX_BUF
{
    struct _CYW_RX_BUF *Next;
    PUCHAR Buffer;
    PMDL Mdl;
    DOT11_EXTSTA_RECV_CONTEXT RecvContext;
} CYW_RX_BUF, *PCYW_RX_BUF;

/* A persistent nonpaged transfer buffer with a preallocated MDL that is
   re-pointed at the requested sub-range for each SDIO transfer */
typedef struct _CYW_DMA_BUF
{
    PUCHAR Buffer;
    ULONG Size;
    PMDL Mdl;
} CYW_DMA_BUF, *PCYW_DMA_BUF;

#define CYW_DMA_BUF_COUNT               4

typedef struct _CYW_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    PDEVICE_OBJECT Pdo;
    SDBUS_INTERFACE_STANDARD SdBus;
    BOOLEAN SdBusOpened;
    PFILE_OBJECT SdioControlFileObject;
    PDEVICE_OBJECT SdioControlDeviceObject;

    ULONG ChipId;
    ULONG ChipRev;
    ULONG RamBase;
    ULONG RamSize;
    ULONG Cr4WrapBase;
    ULONG SdioCoreBase;
    ULONG RstVec;
    ULONG CurrentBackplaneWindow;

    ULONG BcdcRequestId;
    UCHAR TxSeq;
    UCHAR TxMax;
    UCHAR TxFlow;
    UCHAR RxSeq;
    BOOLEAN RxSeqValid;
    ULONG RxBadHdrCount;
    ULONG RxBadSeqCount;
    ULONG RxFailCount;
    ULONG RxFlushStuckCount;
    BOOLEAN TimerResSet;
    PUCHAR ControlBuffer;
    PUCHAR TxBuffer;
    PUCHAR RxBuffer;
    PUCHAR RegScratch;
    CYW_DMA_BUF DmaBufs[CYW_DMA_BUF_COUNT];
    ULONG DmaBufCount;
    USHORT GlomLens[256];
    ULONG GlomCount;
    volatile LONG JoinRetries;
    KEVENT CtrlEvent;
    KEVENT BusEvent;
    LIST_ENTRY TxQueue;
    KSPIN_LOCK TxLock;
    volatile LONG OutstandingTxNbls;
    KEVENT TxDrainEvent;
    ULONG CtrlResponseLen;
    volatile LONG CtrlResponseStatus;
    volatile LONG ExpectedBcdcRequestId;
    volatile LONG ExpectedBcdcCommand;
    volatile LONG BusThreadRunning;
    KEVENT BusThreadExited;
    KMUTEX F2Lock;
    KMUTEX CmdLock;
    KMUTEX ConnectLock;
    KMUTEX BackplaneLock;
    NDIS_HANDLE RxNblPool;
    PCYW_RX_BUF RxBufFree;
    KSPIN_LOCK RxBufLock;
    volatile LONG OutstandingRxNbls;
    KEVENT RxDrainEvent;

    UCHAR PermanentAddress[CYW_ADDRESS_LENGTH];
    UCHAR CurrentAddress[CYW_ADDRESS_LENGTH];

    DOT11_PHY_TYPE CurrentPhyType;
    ULONG CurrentOperationMode;
    ULONG PacketFilter;
    ULONG Dot11PacketFilter;
    ULONG AutoConfigEnabled;
    BOOLEAN RadioOn;

    NDIS_SPIN_LOCK Lock;
    PNDIS_OID_REQUEST PendingScanOid;
    ULONG ScanSeq;
    ULONG ConnectSeq;
    BOOLEAN ScanInProgress;
    CYW_BSS Bss[CYW_MAX_BSS];
    ULONG BssCount;

    UCHAR DesiredSsid[DOT11_SSID_MAX_LENGTH];
    ULONG DesiredSsidLength;
    UCHAR DesiredBssid[CYW_ADDRESS_LENGTH];
    BOOLEAN HasDesiredBssid;
    ULONG AuthAlgorithm;
    ULONG UnicastCipher;
    ULONG MulticastCipher;
    ULONG DefaultKeyId;
    UCHAR SaePassword[CYW_SAE_PASSWORD_MAX];
    ULONG SaePasswordLen;
    UCHAR ConnectedBssid[CYW_ADDRESS_LENGTH];
    ULONG ConnectedChannelFrequency;
    LONG ConnectedRssi;
    ULONG CurrentRateUnits500Kbps;
    BOOLEAN Associated;
    BOOLEAN LinkUp;
    BOOLEAN FirmwareSupplicant;
    BOOLEAN FirmwareHandshakeComplete;
    BOOLEAN PortAuthorized;
    PNDIS_OID_REQUEST PendingConnectOid;

    volatile LONG64 TxOkCount;
    volatile LONG64 TxErrCount;
    volatile LONG64 RxOkCount;

    PVOID BusThread;
    volatile LONG BusThreadStop;

    volatile LONG ScanWorkQueued;
    KSPIN_LOCK WorkItemLock;
    volatile LONG WorkItemsPending;
    KEVENT WorkItemsDrainedEvent;
    volatile LONG Paused;
    volatile LONG Halting;
    volatile LONG CardIntRegistered;
    volatile LONG CardInterruptPending;
} CYW_ADAPTER, *PCYW_ADAPTER;

DRIVER_INITIALIZE DriverEntry;

MINIPORT_INITIALIZE              CywMiniportInitializeEx;
MINIPORT_HALT                    CywMiniportHaltEx;
MINIPORT_PAUSE                   CywMiniportPauseEx;
MINIPORT_RESTART                 CywMiniportRestartEx;
MINIPORT_SHUTDOWN                CywMiniportShutdownEx;
MINIPORT_DEVICE_PNP_EVENT_NOTIFY CywMiniportDevicePnpEventNotify;
MINIPORT_UNLOAD                  CywMiniportUnload;
MINIPORT_SEND_NET_BUFFER_LISTS   CywMiniportSendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS CywMiniportReturnNetBufferLists;
MINIPORT_CANCEL_SEND             CywMiniportCancelSend;
MINIPORT_OID_REQUEST             CywMiniportOidRequest;
MINIPORT_CANCEL_OID_REQUEST      CywMiniportCancelOidRequest;

NTSTATUS CywSdioOpen(_In_ PCYW_ADAPTER Adapter);
VOID CywSdioClose(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywSdioReadByte(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG Address, _Out_ PUCHAR Value);
NTSTATUS CywSdioWriteByte(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG Address, _In_ UCHAR Value);
NTSTATUS CywSdioReadBytes(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG Address, _Out_ PUCHAR Buffer, _In_ ULONG Length);
NTSTATUS CywSdioReadBlocks(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG Address, _Out_ PUCHAR Buffer, _In_ ULONG Length, _In_ ULONG BlockSize);
NTSTATUS CywSdioWriteBytes(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG Address, _In_ PUCHAR Buffer, _In_ ULONG Length);
NTSTATUS CywSdioWriteBlocks(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG Address, _In_ PUCHAR Buffer, _In_ ULONG Length, _In_ ULONG BlockSize);
NTSTATUS CywSdioEnableFunction(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function);
NTSTATUS CywSdioSetBlockSize(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG BlockSize);
NTSTATUS CywRegisterDmaBuf(_In_ PCYW_ADAPTER Adapter, _In_ PUCHAR Buffer, _In_ ULONG Size);
VOID CywFreeDmaBufs(_In_ PCYW_ADAPTER Adapter);

NTSTATUS CywBackplaneSetWindow(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address);
NTSTATUS CywBackplaneReadl(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address, _Out_ PULONG Value);
NTSTATUS CywBackplaneWritel(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address, _In_ ULONG Value);
NTSTATUS CywBackplaneReadlSc(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address, _Out_ PULONG Value, _Inout_ PUCHAR Scratch);
NTSTATUS CywBackplaneWritelSc(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address, _In_ ULONG Value, _Inout_ PUCHAR Scratch);
NTSTATUS CywRamWrite(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address, _In_ PUCHAR Buffer, _In_ ULONG Length);

NTSTATUS CywChipRecognize(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywChipDownloadFirmware(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywChipBringUp(_In_ PCYW_ADAPTER Adapter);

NTSTATUS CywFilCmdSet(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Cmd, _In_ PVOID Data, _In_ ULONG Length);
NTSTATUS CywFilCmdGet(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Cmd, _Out_ PVOID Data, _In_ ULONG Length);
NTSTATUS CywFilIovarSet(_In_ PCYW_ADAPTER Adapter, _In_ PCSTR Name, _In_ PVOID Data, _In_ ULONG Length);
NTSTATUS CywFilIovarGet(_In_ PCYW_ADAPTER Adapter, _In_ PCSTR Name, _Out_ PVOID Data, _In_ ULONG Length);
NTSTATUS CywFilIovarSetInt(_In_ PCYW_ADAPTER Adapter, _In_ PCSTR Name, _In_ ULONG Value);
NTSTATUS CywActivateEvents(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywQueryRssi(_In_ PCYW_ADAPTER Adapter, _Out_ PLONG Rssi);
NTSTATUS CywQueryRate(_In_ PCYW_ADAPTER Adapter, _Out_ PULONG RateUnits500Kbps);
ULONG CywChannelToFrequency(_In_ ULONG Channel);
UCHAR CywDataRateIndexFromUnits(_In_ ULONG RateUnits500Kbps);


NTSTATUS CywScanStart(_In_ PCYW_ADAPTER Adapter, _In_reads_bytes_opt_(RequestLength) PDOT11_SCAN_REQUEST_V2 Request, _In_ ULONG RequestLength);
VOID CywProcessEvent(_In_ PCYW_ADAPTER Adapter, _In_ PUCHAR Frame, _In_ ULONG Length);
NTSTATUS CywSdpcmSendData(_In_ PCYW_ADAPTER Adapter, _In_ PUCHAR Eth, _In_ ULONG EthLen);
NTSTATUS CywSdpcmSendNb(_In_ PCYW_ADAPTER Adapter, _In_ PNET_BUFFER Nb);
ULONG CywBuildEthFromNbl(_In_ PNET_BUFFER Nb, _Out_writes_(Capacity) PUCHAR Dest, _In_ ULONG Capacity);
BOOLEAN CywTxAllowed(_In_ PCYW_ADAPTER Adapter, _In_reads_(EthLen) PUCHAR Eth, _In_ ULONG EthLen);
PNET_BUFFER_LIST CywRxData(_In_ PCYW_ADAPTER Adapter, _In_ PUCHAR Body, _In_ ULONG BodyLen);
PCYW_RX_BUF CywAcquireRxBuffer(_In_ PCYW_ADAPTER Adapter);
VOID CywReleaseRxBuffer(_In_ PCYW_ADAPTER Adapter, _In_ PCYW_RX_BUF RxBuffer);
BOOLEAN CywQueueWorkItem(_In_ PCYW_ADAPTER Adapter, _In_ NDIS_IO_WORKITEM_ROUTINE Routine, _In_opt_ PVOID Context);
VOID CywCompleteWorkItem(_In_ PCYW_ADAPTER Adapter, _In_ NDIS_HANDLE WorkItem);
NTSTATUS CywStartBusThread(_In_ PCYW_ADAPTER Adapter);
VOID CywStopBusThread(_In_ PCYW_ADAPTER Adapter);
VOID CywDrainTxQueue(_In_ PCYW_ADAPTER Adapter);
ULONG CywBuildBssList(_In_ PCYW_ADAPTER Adapter, _Out_ PUCHAR Buffer, _In_ ULONG BufferLength, _Out_ PULONG BytesNeeded);
VOID CywIndicateScanComplete(_In_ PCYW_ADAPTER Adapter, _In_ NDIS_STATUS ScanStatus);
VOID CywIndicateAssocStart(_In_ PCYW_ADAPTER Adapter);
VOID CywIndicateAssocComplete(_In_ PCYW_ADAPTER Adapter, _In_ DOT11_ASSOC_STATUS Status);
VOID CywIndicateConnectComplete(_In_ PCYW_ADAPTER Adapter, _In_ DOT11_ASSOC_STATUS Status);
VOID CywIndicateLinkState(_In_ PCYW_ADAPTER Adapter, _In_ BOOLEAN Connected, _In_ ULONG64 SpeedBps);
VOID CywIndicateDisassociation(_In_ PCYW_ADAPTER Adapter);
VOID CywIndicateLinkQuality(_In_ PCYW_ADAPTER Adapter);
VOID CywCompletePendingConnect(_In_ PCYW_ADAPTER Adapter, _In_ NDIS_STATUS Status);
VOID CywAbortPendingOids(_In_ PCYW_ADAPTER Adapter, _In_ NDIS_STATUS Status);
VOID CywQueueLinkUpWork(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywConnect(_In_ PCYW_ADAPTER Adapter);
VOID CywQueueConnectWork(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywDisconnect(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywSetKey(_In_ PCYW_ADAPTER Adapter, _In_ BOOLEAN Pairwise, _In_opt_ PUCHAR Ea, _In_ DOT11_CIPHER_ALGORITHM Algorithm, _In_ BOOLEAN Delete, _In_reads_bytes_opt_(KeyLen) PUCHAR Key, _In_ ULONG KeyLen, _In_ ULONG KeyIndex);

PVOID CywAllocate(_In_ ULONG Size);
VOID CywFree(_In_ PVOID Buffer);

#endif /* _CYW43455_H_ */
