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
#define SDIO_CCCR_BUS_IF                0x07
#define SDIO_FUNC_ENABLE_1              0x02
#define SDIO_FUNC_ENABLE_2              0x04
#define SDIO_FUNC_READY_1               0x02
#define SDIO_FUNC_READY_2               0x04
#define SDIO_INTR_ENABLE_MASTER         0x01

#define SBSDIO_FUNC1_SBADDRLOW          0x1000A
#define SBSDIO_FUNC1_SBADDRMID          0x1000B
#define SBSDIO_FUNC1_SBADDRHIGH         0x1000C
#define SBSDIO_FUNC1_CHIPCLKCSR         0x1000E
#define SBSDIO_FUNC1_WATERMARK          0x10008
#define SBSDIO_FUNC1_MESBUSYCTRL        0x1001D
#define SBSDIO_FUNC1_SLEEPCSR           0x1001F

#define CY_43455_F2_WATERMARK           0x60
#define SBSDIO_MESBUSYCTRL_ENAB         0x80

#define SBSDIO_SB_OFT_ADDR_MASK         0x07FFF
#define SBSDIO_SB_OFT_ADDR_LIMIT        0x08000
#define SBSDIO_SB_ACCESS_2_4B_FLAG      0x08000
#define SBSDIO_SBWINDOW_MASK            0xFFFF8000

#define SBSDIO_ALP_AVAIL_REQ            0x08
#define SBSDIO_HT_AVAIL_REQ             0x10
#define SBSDIO_FORCE_HT                 0x20
#define SBSDIO_ALP_AVAIL                0x40
#define SBSDIO_HT_AVAIL                 0x80
#define SBSDIO_CSR_MASK                 0x1F

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

#define ARMCR4_BCMA_IOCTL_CPUHALT       0x20

#define BCM_CORE_REG_BANKIDX            0x40
#define BCM_CORE_REG_BANKPDA            0x4C

#define CC_EROMPTR                      0x0FC
#define DMP_DESC_TYPE_MSK               0x0000000F
#define DMP_DESC_COMPONENT              0x00000001
#define DMP_DESC_ADDRESS                0x00000005
#define DMP_DESC_ADDRSIZE_GT32          0x00000008
#define DMP_DESC_EOT                    0x0000000F
#define DMP_COMP_PARTNUM                0x000FFF00
#define DMP_COMP_PARTNUM_S              8
#define DMP_COMP_NUM_MPORT              0x000001F0
#define DMP_COMP_NUM_MPORT_S            4
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
#define BRCMF_C_SET_INFRA               20
#define BRCMF_C_GET_BSSID               23
#define BRCMF_C_SET_SSID                26
#define BRCMF_C_SET_CHANNEL             30
#define BRCMF_C_SET_PASSIVE_SCAN        49
#define BRCMF_C_SCAN                    50
#define BRCMF_C_DISASSOC                52
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

#define CYW_CRYPTO_ALGO_AES_CCM         4
#define CYW_WSEC_PRIMARY_KEY            2

#define CYW_FC0_TYPE_DATA               0x08
#define CYW_FC1_FROMDS                  0x02
#define CYW_SNAP_DSAP                   0xAA
#define CYW_SNAP_SSAP                   0xAA
#define CYW_SNAP_CONTROL                0x03
#define ETH_P_EAPOL                     0x888E

#define BRCMF_ESCAN_REQ_VERSION         1
#define WL_ESCAN_ACTION_START           1
#define WL_ESCAN_ACTION_CONTINUE        2
#define WL_ESCAN_ACTION_ABORT           3
#define CYW_ESCAN_SYNCID                0x1234

#define DOT11_BSSTYPE_ANY               2
#define BRCMF_SCANTYPE_ACTIVE           0
#define BRCMF_SCANTYPE_PASSIVE          1

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
#define BRCMF_E_IF                      54
#define BRCMF_E_ESCAN_RESULT            69

#define BRCMF_E_STATUS_SUCCESS          0
#define BRCMF_E_STATUS_PARTIAL          8

#define BRCMF_EVENT_MSG_LINK            0x01

#define ETH_P_LINK_CTL                  0x886C
#define BCMILCP_BCM_SUBTYPE_EVENT       1
#define BCDC_HEADER_LEN                 4

#define CYW_MAX_BSS                     48
#define CYW_MAX_BSS_IE                  512
#define CYW_CONTROL_BUFFER_SIZE         2048
#define CYW_ADDRESS_LENGTH              6
#define CYW_MTU_SIZE                    1500
#define CYW_MAX_FRAME_SIZE              DOT11_MAX_PDU_SIZE
#define CYW_LINK_SPEED_BPS              54000000ULL

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
#include <poppack.h>

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
    LONG Rssi;
    ULONG LinkQuality;
    USHORT CapabilityInformation;
    USHORT BeaconPeriod;
    UCHAR IeBlob[CYW_MAX_BSS_IE];
    ULONG IeLength;
} CYW_BSS, *PCYW_BSS;

typedef struct _CYW_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT NextDeviceObject;
    SDBUS_INTERFACE_STANDARD SdBus;
    BOOLEAN SdBusOpened;

    ULONG ChipId;
    ULONG ChipRev;
    ULONG RamBase;
    ULONG RamSize;
    ULONG Cr4CoreBase;
    ULONG Cr4WrapBase;
    ULONG RstVec;
    ULONG ChipCommonBase;
    ULONG SocramBase;
    ULONG CurrentBackplaneWindow;
    BOOLEAN ChipUp;
    BOOLEAN FirmwareReady;

    ULONG BcdcRequestId;
    UCHAR TxSeq;
    UCHAR TxMax;
    UCHAR TxFlow;
    PUCHAR ControlBuffer;
    PMDL ControlMdl;
    PUCHAR TxBuffer;
    PUCHAR RxBuffer;
    KEVENT CtrlEvent;
    ULONG CtrlResponseLen;
    BOOLEAN RxThreadRunning;
    KMUTEX F2Lock;
    KMUTEX CmdLock;
    NDIS_HANDLE RxNblPool;

    UCHAR PermanentAddress[CYW_ADDRESS_LENGTH];
    UCHAR CurrentAddress[CYW_ADDRESS_LENGTH];

    DOT11_PHY_TYPE CurrentPhyType;
    ULONG CurrentOperationMode;
    ULONG PacketFilter;
    BOOLEAN RadioOn;

    NDIS_SPIN_LOCK Lock;
    PNDIS_OID_REQUEST PendingScanOid;
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
    UCHAR ConnectedBssid[CYW_ADDRESS_LENGTH];
    BOOLEAN Associated;
    BOOLEAN LinkUp;
    PNDIS_OID_REQUEST PendingConnectOid;

    HANDLE RxThreadHandle;
    PVOID RxThread;
    volatile LONG RxThreadStop;

    NDIS_HANDLE InterruptWorkItem;
    volatile LONG InterruptPending;
    BOOLEAN Halting;
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
NTSTATUS CywSdioEnableFunction(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function);
NTSTATUS CywSdioSetBlockSize(_In_ PCYW_ADAPTER Adapter, _In_ UCHAR Function, _In_ ULONG BlockSize);

NTSTATUS CywBackplaneSetWindow(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address);
NTSTATUS CywBackplaneReadl(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address, _Out_ PULONG Value);
NTSTATUS CywBackplaneWritel(_In_ PCYW_ADAPTER Adapter, _In_ ULONG Address, _In_ ULONG Value);
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

NTSTATUS CywScanStart(_In_ PCYW_ADAPTER Adapter, _In_opt_ PDOT11_SCAN_REQUEST_V2 Request);
VOID CywProcessEvent(_In_ PCYW_ADAPTER Adapter, _In_ PUCHAR Frame, _In_ ULONG Length);
NTSTATUS CywSdpcmSendData(_In_ PCYW_ADAPTER Adapter, _In_ PUCHAR Eth, _In_ ULONG EthLen);
PNET_BUFFER_LIST CywRxData(_In_ PCYW_ADAPTER Adapter, _In_ PUCHAR Body, _In_ ULONG BodyLen);
NTSTATUS CywStartRxThread(_In_ PCYW_ADAPTER Adapter);
VOID CywStopRxThread(_In_ PCYW_ADAPTER Adapter);
ULONG CywBuildBssList(_In_ PCYW_ADAPTER Adapter, _Out_ PUCHAR Buffer, _In_ ULONG BufferLength, _Out_ PULONG BytesNeeded);
VOID CywIndicateScanComplete(_In_ PCYW_ADAPTER Adapter, _In_ NDIS_STATUS ScanStatus);
VOID CywIndicateAssocStart(_In_ PCYW_ADAPTER Adapter);
VOID CywIndicateAssocComplete(_In_ PCYW_ADAPTER Adapter, _In_ DOT11_ASSOC_STATUS Status);
VOID CywIndicateConnectComplete(_In_ PCYW_ADAPTER Adapter, _In_ DOT11_ASSOC_STATUS Status);
VOID CywIndicateLinkState(_In_ PCYW_ADAPTER Adapter, _In_ BOOLEAN Connected);
NTSTATUS CywConnect(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywDisconnect(_In_ PCYW_ADAPTER Adapter);
NTSTATUS CywSetKey(_In_ PCYW_ADAPTER Adapter, _In_ BOOLEAN Pairwise, _In_opt_ PUCHAR Ea, _In_ PUCHAR Key, _In_ ULONG KeyLen, _In_ ULONG KeyIndex);

PVOID CywAllocate(_In_ ULONG Size);
VOID CywFree(_In_ PVOID Buffer);

#endif /* _CYW43455_H_ */
