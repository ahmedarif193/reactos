/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     UEFI network protocol detection, driver loading, and HTTP boot
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <uefildr.h>
#include <ramdisk.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

/* Missing definitions usually found in efipciio.h
 * Layout must match EDK2 MdePkg/Include/Protocol/PciIo.h exactly. */
#define EFI_PCI_IO_PROTOCOL_GUID \
    { 0x4cf5b200, 0x68b8, 0x4ca5, { 0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } }

typedef enum {
    EfiPciIoWidthUint8,
    EfiPciIoWidthUint16,
    EfiPciIoWidthUint32,
    EfiPciIoWidthUint64,
    EfiPciIoWidthMaximum
} EFI_PCI_IO_PROTOCOL_WIDTH;

struct _EFI_PCI_IO_PROTOCOL;

/* PCI config space access (Offset is UINT32) */
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_CONFIG)(
    struct _EFI_PCI_IO_PROTOCOL *This,
    EFI_PCI_IO_PROTOCOL_WIDTH Width,
    UINT32 Offset,
    UINTN Count,
    VOID *Buffer);

typedef struct {
    EFI_PCI_IO_PROTOCOL_CONFIG Read;
    EFI_PCI_IO_PROTOCOL_CONFIG Write;
} EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS;

/* Attributes() operation enum */
typedef enum {
    EfiPciIoAttributeOperationGet,
    EfiPciIoAttributeOperationSet,
    EfiPciIoAttributeOperationEnable,
    EfiPciIoAttributeOperationDisable,
    EfiPciIoAttributeOperationSupported,
    EfiPciIoAttributeOperationMaximum
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_ATTRIBUTES_FN)(
    struct _EFI_PCI_IO_PROTOCOL *This,
    EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION Operation,
    UINT64 Attributes,
    UINT64 *Result);

#define EFI_PCI_IO_ATTRIBUTE_IO          0x0100
#define EFI_PCI_IO_ATTRIBUTE_MEMORY      0x0200
#define EFI_PCI_IO_ATTRIBUTE_BUS_MASTER  0x0400
#define EFI_PCI_DEVICE_ENABLE \
    (EFI_PCI_IO_ATTRIBUTE_IO | EFI_PCI_IO_ATTRIBUTE_MEMORY | EFI_PCI_IO_ATTRIBUTE_BUS_MASTER)

/* Memory and IO BAR access (BarIndex + UINT64 Offset) */
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_IO_MEM)(
    struct _EFI_PCI_IO_PROTOCOL *This,
    EFI_PCI_IO_PROTOCOL_WIDTH Width,
    UINT8 BarIndex,
    UINT64 Offset,
    UINTN Count,
    VOID *Buffer);

typedef struct {
    EFI_PCI_IO_PROTOCOL_IO_MEM Read;
    EFI_PCI_IO_PROTOCOL_IO_MEM Write;
} EFI_PCI_IO_PROTOCOL_IO_ACCESS;

/*
 * Full EFI_PCI_IO_PROTOCOL layout (fields up to Attributes).
 * Must match EDK2 MdePkg/Include/Protocol/PciIo.h exactly.
 */
typedef struct _EFI_PCI_IO_PROTOCOL {
    void *PollMem;                                  /* offset  0 */
    void *PollIo;                                   /* offset  1 */
    EFI_PCI_IO_PROTOCOL_IO_ACCESS Mem;              /* offset  2-3 */
    EFI_PCI_IO_PROTOCOL_IO_ACCESS Io;               /* offset  4-5 */
    EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS Pci;          /* offset  6-7 */
    void *CopyMem;                                  /* offset  8 */
    void *Map;                                      /* offset  9 */
    void *Unmap;                                    /* offset 10 */
    void *AllocateBuffer;                           /* offset 11 */
    void *FreeBuffer;                               /* offset 12 */
    void *Flush;                                    /* offset 13 */
    void *GetLocation;                              /* offset 14 */
    EFI_PCI_IO_PROTOCOL_ATTRIBUTES_FN Attributes;   /* offset 15 */
} EFI_PCI_IO_PROTOCOL;

/* Missing definitions usually found in Ip4Config2.h
 * Layout must match EDK2 MdePkg/Include/Protocol/Ip4Config2.h exactly. */
#define EFI_IP4_CONFIG2_PROTOCOL_GUID \
    { 0x5b446ed1, 0xe30b, 0x4faa, {0x87, 0x1a, 0x36, 0x54, 0xec, 0xa3, 0x60, 0x80 } }

typedef struct _EFI_IP4_CONFIG2_PROTOCOL EFI_IP4_CONFIG2_PROTOCOL;

typedef enum {
    Ip4Config2DataTypeInterfaceInfo,
    Ip4Config2DataTypePolicy,
    Ip4Config2DataTypeManualAddress,
    Ip4Config2DataTypeGateway,
    Ip4Config2DataTypeDnsServer,
    Ip4Config2DataTypeMaximum
} EFI_IP4_CONFIG2_DATA_TYPE;

typedef enum {
    Ip4Config2PolicyStatic,
    Ip4Config2PolicyDhcp,
    Ip4Config2PolicyMax
} EFI_IP4_CONFIG2_POLICY;

typedef struct {
    EFI_IPv4_ADDRESS Address;
    EFI_IPv4_ADDRESS SubnetMask;
} EFI_IP4_CONFIG2_MANUAL_ADDRESS;

typedef EFI_STATUS (EFIAPI *EFI_IP4_CONFIG2_SET_DATA)(
    EFI_IP4_CONFIG2_PROTOCOL *This,
    EFI_IP4_CONFIG2_DATA_TYPE DataType,
    UINTN DataSize,
    VOID *Data);

typedef EFI_STATUS (EFIAPI *EFI_IP4_CONFIG2_GET_DATA)(
    EFI_IP4_CONFIG2_PROTOCOL *This,
    EFI_IP4_CONFIG2_DATA_TYPE DataType,
    UINTN *DataSize,
    VOID *Data OPTIONAL);

typedef EFI_STATUS (EFIAPI *EFI_IP4_CONFIG2_REGISTER_NOTIFY)(
    EFI_IP4_CONFIG2_PROTOCOL *This,
    EFI_IP4_CONFIG2_DATA_TYPE DataType,
    EFI_EVENT Event);

typedef EFI_STATUS (EFIAPI *EFI_IP4_CONFIG2_UNREGISTER_NOTIFY)(
    EFI_IP4_CONFIG2_PROTOCOL *This,
    EFI_IP4_CONFIG2_DATA_TYPE DataType,
    EFI_EVENT Event);

struct _EFI_IP4_CONFIG2_PROTOCOL {
    EFI_IP4_CONFIG2_SET_DATA         SetData;
    EFI_IP4_CONFIG2_GET_DATA         GetData;
    EFI_IP4_CONFIG2_REGISTER_NOTIFY  RegisterDataNotify;
    EFI_IP4_CONFIG2_UNREGISTER_NOTIFY UnregisterDataNotify;
};

/* Missing definitions for EFI_TCP4_PROTOCOL.
 * Layout must match EDK2 MdePkg/Include/Protocol/Tcp4.h exactly. */
#define EFI_TCP4_SERVICE_BINDING_PROTOCOL_GUID \
    { 0x00720665, 0x67EB, 0x4a99, { 0xBA, 0xF7, 0xD3, 0xC3, 0x3A, 0x1C, 0x7C, 0xC9 } }

#define EFI_TCP4_PROTOCOL_GUID \
    { 0x65530BC7, 0xA359, 0x410f, { 0xB0, 0x10, 0x5A, 0xAD, 0xC7, 0xEC, 0x2B, 0x62 } }

typedef struct _EFI_TCP4_PROTOCOL EFI_TCP4_PROTOCOL;

typedef enum {
    Tcp4StateClosed      = 0,
    Tcp4StateListen      = 1,
    Tcp4StateSynSent     = 2,
    Tcp4StateSynReceived = 3,
    Tcp4StateEstablished = 4,
    Tcp4StateFinWait1    = 5,
    Tcp4StateFinWait2    = 6,
    Tcp4StateClosing     = 7,
    Tcp4StateTimeWait    = 8,
    Tcp4StateCloseWait   = 9,
    Tcp4StateLastAck     = 10
} EFI_TCP4_CONNECTION_STATE;

typedef struct {
    BOOLEAN           UseDefaultAddress;
    EFI_IPv4_ADDRESS  StationAddress;
    EFI_IPv4_ADDRESS  SubnetMask;
    UINT16            StationPort;
    EFI_IPv4_ADDRESS  RemoteAddress;
    UINT16            RemotePort;
    BOOLEAN           ActiveFlag;
} EFI_TCP4_ACCESS_POINT;

typedef struct {
    UINT32  ReceiveBufferSize;
    UINT32  SendBufferSize;
    UINT32  MaxSynBackLog;
    UINT32  ConnectionTimeout;
    UINT32  DataRetries;
    UINT32  FinTimeout;
    UINT32  TimeWaitTimeout;
    UINT32  KeepAliveProbes;
    UINT32  KeepAliveTime;
    UINT32  KeepAliveInterval;
    BOOLEAN EnableNagle;
    BOOLEAN EnableTimeStamp;
    BOOLEAN EnableWindowScaling;
    BOOLEAN EnableSelectiveAck;
    BOOLEAN EnablePathMtuDiscovery;
} EFI_TCP4_OPTION;

typedef struct {
    UINT8                   TypeOfService;
    UINT8                   TimeToLive;
    EFI_TCP4_ACCESS_POINT   AccessPoint;
    EFI_TCP4_OPTION         *ControlOption;
} EFI_TCP4_CONFIG_DATA;

typedef struct {
    EFI_EVENT    Event;
    EFI_STATUS   Status;
} EFI_TCP4_COMPLETION_TOKEN;

typedef struct {
    EFI_TCP4_COMPLETION_TOKEN CompletionToken;
} EFI_TCP4_CONNECTION_TOKEN;

typedef struct {
    UINT32  FragmentLength;
    VOID    *FragmentBuffer;
} EFI_TCP4_FRAGMENT_DATA;

typedef struct {
    BOOLEAN                Push;
    BOOLEAN                Urgent;
    UINT32                 DataLength;
    UINT32                 FragmentCount;
    EFI_TCP4_FRAGMENT_DATA FragmentTable[1];
} EFI_TCP4_TRANSMIT_DATA;

typedef struct {
    BOOLEAN                UrgentFlag;
    UINT32                 DataLength;
    UINT32                 FragmentCount;
    EFI_TCP4_FRAGMENT_DATA FragmentTable[1];
} EFI_TCP4_RECEIVE_DATA;

typedef struct {
    EFI_TCP4_COMPLETION_TOKEN CompletionToken;
    union {
        EFI_TCP4_RECEIVE_DATA  *RxData;
        EFI_TCP4_TRANSMIT_DATA *TxData;
    } Packet;
} EFI_TCP4_IO_TOKEN;

typedef struct {
    EFI_TCP4_COMPLETION_TOKEN CompletionToken;
    BOOLEAN                   AbortOnClose;
} EFI_TCP4_CLOSE_TOKEN;

typedef EFI_STATUS (EFIAPI *EFI_TCP4_GET_MODE_DATA)(
    EFI_TCP4_PROTOCOL         *This,
    EFI_TCP4_CONNECTION_STATE *Tcp4State OPTIONAL,
    EFI_TCP4_CONFIG_DATA      *Tcp4ConfigData OPTIONAL,
    VOID                      *Ip4ModeData OPTIONAL,
    VOID                      *MnpConfigData OPTIONAL,
    VOID                      *SnpModeData OPTIONAL);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_CONFIGURE)(
    EFI_TCP4_PROTOCOL    *This,
    EFI_TCP4_CONFIG_DATA *TcpConfigData OPTIONAL);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_ROUTES)(
    EFI_TCP4_PROTOCOL *This,
    BOOLEAN           DeleteRoute,
    EFI_IPv4_ADDRESS  *SubnetAddress,
    EFI_IPv4_ADDRESS  *SubnetMask,
    EFI_IPv4_ADDRESS  *GatewayAddress);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_CONNECT)(
    EFI_TCP4_PROTOCOL         *This,
    EFI_TCP4_CONNECTION_TOKEN *ConnectionToken);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_ACCEPT)(
    EFI_TCP4_PROTOCOL         *This,
    EFI_TCP4_CONNECTION_TOKEN *ListenToken);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_TRANSMIT)(
    EFI_TCP4_PROTOCOL  *This,
    EFI_TCP4_IO_TOKEN  *Token);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_RECEIVE)(
    EFI_TCP4_PROTOCOL  *This,
    EFI_TCP4_IO_TOKEN  *Token);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_CLOSE)(
    EFI_TCP4_PROTOCOL     *This,
    EFI_TCP4_CLOSE_TOKEN  *CloseToken);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_CANCEL)(
    EFI_TCP4_PROTOCOL         *This,
    EFI_TCP4_COMPLETION_TOKEN *Token OPTIONAL);

typedef EFI_STATUS (EFIAPI *EFI_TCP4_POLL)(
    EFI_TCP4_PROTOCOL *This);

struct _EFI_TCP4_PROTOCOL {
    EFI_TCP4_GET_MODE_DATA GetModeData;
    EFI_TCP4_CONFIGURE     Configure;
    EFI_TCP4_ROUTES        Routes;
    EFI_TCP4_CONNECT       Connect;
    EFI_TCP4_ACCEPT        Accept;
    EFI_TCP4_TRANSMIT      Transmit;
    EFI_TCP4_RECEIVE       Receive;
    EFI_TCP4_CLOSE         Close;
    EFI_TCP4_CANCEL        Cancel;
    EFI_TCP4_POLL          Poll;
};

/* RTL8168 register offsets (accessible via IO BAR0 or MEM BAR) */
#define RTL_REG_MAC0       0x00  /* MAC address bytes 0-5 (IDR0-IDR5) */
#define RTL_REG_CMD        0x37  /* Command register */
#define RTL_CMD_RESET      0x10  /* Bit 4: software reset */
#define RTL_REG_9346CR     0x50  /* Config 9346 register (EEPROM control) */
#define RTL_9346_EEM_NORMAL    0x00  /* EEM=00: normal mode */
#define RTL_9346_EEM_WRITEEN   0xC0  /* EEM=11: config register write enable */

/* GLOBALS ********************************************************************/

extern EFI_SYSTEM_TABLE *GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

static EFI_GUID gEfiSnpGuid     = EFI_SIMPLE_NETWORK_PROTOCOL_GUID;
static EFI_GUID gEfiTcp4SbGuid  = EFI_TCP4_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_GUID gEfiTcp4Guid    = EFI_TCP4_PROTOCOL_GUID;
static EFI_GUID gEfiSfsGuid     = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID gEfiLipGuid    = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID gEfiFileInfoId = EFI_FILE_INFO_ID;
static EFI_GUID gEfiPciIoGuid  = EFI_PCI_IO_PROTOCOL_GUID;

/* DHCP4 protocol GUIDs */
static EFI_GUID gEfiDhcp4SbGuid = EFI_DHCP4_SERVICE_BINDING_PROTOCOL_GUID;
static EFI_GUID gEfiDhcp4Guid   = EFI_DHCP4_PROTOCOL_GUID;
static EFI_GUID gEfiIp4Config2Guid = EFI_IP4_CONFIG2_PROTOCOL_GUID;

/* NII (Network Interface Identifier / UNDI) -- produced by RtkUndiDxe,
 * consumed by SnpDxe to create SNP instances. */
static EFI_GUID gEfiNiiGuid = {0x1ACED566, 0x76ED, 0x4218,
    {0xBC, 0x81, 0x76, 0x7F, 0x1F, 0x97, 0x7A, 0x89}};
/* NII v3.1 (31-bit revision) */
static EFI_GUID gEfiNii31Guid = {0xE18541CD, 0xF755, 0x4f73,
    {0x92, 0x8D, 0x64, 0x3C, 0x8A, 0x79, 0xB2, 0x29}};

/* Probe results -- set once by UefiProbeNetworkSupport() */
static BOOLEAN NetworkProbeComplete = FALSE;
static BOOLEAN SnpAvailable     = FALSE;
static BOOLEAN Tcp4SbAvailable  = FALSE;
static ULONG   SnpHandleCount  = 0;

/*
 * Network controller handle -- the UEFI handle that carries TCP4 SB,
 * DHCP4 SB, IP4Config2 and the rest of the network stack for our NIC.
 * All protocol operations must go through this single handle so that
 * DHCP, IP4 routing and TCP4 share the same underlying IP4 stack.
 * Set once by UefiProbeNetworkSupport().
 */
static EFI_HANDLE gNetControllerHandle = NULL;

/* HTTP boot network state -- set by UefiHttpBootInit() */
static BOOLEAN NetInitialized   = FALSE;
static EFI_IPv4_ADDRESS gLocalIp;
static EFI_IPv4_ADDRESS gSubnetMask;
static EFI_IPv4_ADDRESS gGateway;

/* DHCP4 child state for cleanup */
static EFI_SERVICE_BINDING_PROTOCOL *gDhcp4Sb     = NULL;
static EFI_HANDLE                    gDhcp4Child   = NULL;
static EFI_DHCP4_PROTOCOL           *gDhcp4        = NULL;

/* TCP4 child state for cleanup */
static EFI_SERVICE_BINDING_PROTOCOL *gTcp4Sb       = NULL;
static EFI_HANDLE                    gTcp4Child    = NULL;
static EFI_TCP4_PROTOCOL            *gTcp4         = NULL;

/* TCP4 buffered receive layer (64 KiB -- large buffer reduces the number
 * of individual Receive() calls during header parsing fallback). */
#define TCP4_RECV_BUF_SIZE  (64 * 1024)
static UINT8  gRecvBuf[TCP4_RECV_BUF_SIZE];
static UINTN  gRecvBufLen = 0;
static UINTN  gRecvBufPos = 0;

/*
 * Well-known directory for third-party UEFI DXE drivers.
 * The build system (boot_images.cmake) deploys board-variant drivers
 * here when UEFI_BOARD_VARIANT is set.  At boot time we enumerate and
 * load every .efi file found in this directory.
 */
static CHAR16 DriverDirectory[] = L"\\EFI\\BOOT\\drivers";

/* Maximum number of driver files we'll attempt to load from the directory */
#define MAX_DRIVER_FILES  32

/* TCP4 body read chunk size (2 MiB -- matches the TCP receive window
 * so each Receive() can return a full window's worth of data). */
#define TCP4_CHUNK_SIZE   (2 * 1024 * 1024)

/* TCP4 wait timeout in 100ns units (30 seconds) */
#define TCP4_WAIT_TIMEOUT_100NS  (30ULL * 10000000ULL)

/* TARGETED HARDWARE FIX ******************************************************/

/* DHCP4 option codes */
#define DHCP4_OPT_SUBNET_MASK   1
#define DHCP4_OPT_ROUTER        3
#define DHCP4_OPT_END           255
#define DHCP4_OPT_PAD           0

/* DHCP magic cookie (RFC 2132): wire bytes 0x63,0x82,0x53,0x63.
 * Stored as LE UINT32 in EFI_DHCP4_PACKET.Dhcp4.Magik. */
#define DHCP4_MAGIC_COOKIE 0x63538263

static __inline BOOLEAN
UefiIpv4IsZero(_In_ const EFI_IPv4_ADDRESS *Addr)
{
    return (Addr->Addr[0] == 0 &&
            Addr->Addr[1] == 0 &&
            Addr->Addr[2] == 0 &&
            Addr->Addr[3] == 0);
}

static BOOLEAN
UefiDhcp4FindOption(
    _In_ EFI_DHCP4_PACKET *Packet,
    _In_ UINT8 OptionCode,
    _Out_writes_bytes_(OutLen) UINT8 *OutBuf,
    _In_ UINTN OutLen)
{
    UINT8 *Opt;
    UINT8 *End;

    if (!Packet || !OutBuf || OutLen == 0)
        return FALSE;

    if (Packet->Dhcp4.Magik != DHCP4_MAGIC_COOKIE)
        return FALSE;

    Opt = (UINT8 *)Packet->Dhcp4.Option;
    End = (UINT8 *)&Packet->Dhcp4 + Packet->Length;

    while (Opt < End)
    {
        UINT8 Code = *Opt++;

        if (Code == DHCP4_OPT_PAD)
            continue;
        if (Code == DHCP4_OPT_END)
            break;

        if (Opt >= End)
            break;

        UINT8 Len = *Opt++;
        if (Opt + Len > End)
            break;

        if (Code == OptionCode)
        {
            UINTN CopyLen = (Len < OutLen) ? Len : OutLen;
            RtlCopyMemory(OutBuf, Opt, CopyLen);
            return TRUE;
        }

        Opt += Len;
    }

    return FALSE;
}

static EFI_STATUS
UefiNetIp4Config2WaitReady(
    _In_ EFI_IP4_CONFIG2_PROTOCOL *Ip4Cfg,
    _In_ EFI_IP4_CONFIG2_DATA_TYPE DataType)
{
    EFI_STATUS Status;
    UINTN Attempt;
    UINTN Size;

    for (Attempt = 0; Attempt < 25; Attempt++)
    {
        Size = 0;
        Status = Ip4Cfg->GetData(Ip4Cfg, DataType, &Size, NULL);
        if (Status == EFI_NOT_READY)
        {
            GlobalSystemTable->BootServices->Stall(200000); /* 200 ms */
            continue;
        }
        if (Status == EFI_BUFFER_TOO_SMALL || Status == EFI_SUCCESS)
            return EFI_SUCCESS;
        return Status;
    }

    return EFI_TIMEOUT;
}

static EFI_STATUS
UefiNetIp4Config2SetData(
    _In_ EFI_IP4_CONFIG2_PROTOCOL *Ip4Cfg,
    _In_ EFI_IP4_CONFIG2_DATA_TYPE DataType,
    _In_ UINTN DataSize,
    _In_ VOID *Data)
{
    EFI_STATUS Status;
    UINTN Attempt;

    for (Attempt = 0; Attempt < 3; Attempt++)
    {
        Status = Ip4Cfg->SetData(Ip4Cfg, DataType, DataSize, Data);
        if (!EFI_ERROR(Status))
            return EFI_SUCCESS;

        if (Status == EFI_NOT_READY)
        {
            Status = UefiNetIp4Config2WaitReady(Ip4Cfg, DataType);
            if (!EFI_ERROR(Status))
                return EFI_SUCCESS;
        }
        else if (Status == EFI_ACCESS_DENIED)
        {
            GlobalSystemTable->BootServices->Stall(200000); /* 200 ms */
            continue;
        }

        return Status;
    }

    return Status;
}

/**
 * @brief Set IP4Config2 policy to Static on the network controller.
 *
 * Must be called BEFORE DHCP4 so the IP4 stack accepts manual
 * configuration rather than expecting its own DHCP instance.
 * Matches EDK2 HttpBootSetIp4Policy().
 */
static BOOLEAN
UefiNetIp4Config2SetStaticPolicy(VOID)
{
    EFI_STATUS Status;
    EFI_IP4_CONFIG2_PROTOCOL *Ip4Cfg = NULL;
    EFI_IP4_CONFIG2_POLICY Policy;

    if (!gNetControllerHandle)
        return FALSE;

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        gNetControllerHandle, &gEfiIp4Config2Guid, (VOID **)&Ip4Cfg);
    if (EFI_ERROR(Status) || !Ip4Cfg)
    {
        TRACE("UEFI HttpBoot: IP4Config2 not on controller (Status %lx)\n", Status);
        return FALSE;
    }

    Policy = Ip4Config2PolicyStatic;
    Status = UefiNetIp4Config2SetData(Ip4Cfg, Ip4Config2DataTypePolicy,
                                      sizeof(Policy), &Policy);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: IP4Config2 policy set failed (Status %lx)\n", Status);
        return FALSE;
    }

    TRACE("UEFI HttpBoot: IP4Config2 policy set to Static\n");
    return TRUE;
}

/**
 * @brief Register gateway (and optionally DNS) via IP4Config2.
 *
 * Called AFTER DHCP4 completes and AFTER the DHCP4 child is deconfigured.
 * Per EDK2's pattern (HttpBootRegisterIp4Gateway), we set only the
 * gateway via IP4Config2 -- NOT the manual station address.  The station
 * address is passed explicitly to TCP4 Configure instead, avoiding a
 * conflict between the IP4Config2 default instance and TCP4's child.
 */
static BOOLEAN
UefiNetIp4Config2RegisterGateway(VOID)
{
    EFI_STATUS Status;
    EFI_IP4_CONFIG2_PROTOCOL *Ip4Cfg = NULL;
    EFI_IPv4_ADDRESS Gateway[1];

    if (!gNetControllerHandle)
        return FALSE;

    if (UefiIpv4IsZero(&gGateway))
    {
        TRACE("UEFI HttpBoot: IP4Config2 gateway skipped (no gateway)\n");
        return TRUE; /* Not an error -- local-subnet only */
    }

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        gNetControllerHandle, &gEfiIp4Config2Guid, (VOID **)&Ip4Cfg);
    if (EFI_ERROR(Status) || !Ip4Cfg)
    {
        TRACE("UEFI HttpBoot: IP4Config2 not on controller (Status %lx)\n", Status);
        return FALSE;
    }

    RtlCopyMemory(&Gateway[0], &gGateway, sizeof(EFI_IPv4_ADDRESS));
    Status = UefiNetIp4Config2SetData(Ip4Cfg, Ip4Config2DataTypeGateway,
                                      sizeof(Gateway), Gateway);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: IP4Config2 gateway set failed (Status %lx)\n", Status);
        return FALSE;
    }

    TRACE("UEFI HttpBoot: IP4Config2 gateway registered (GW=%u.%u.%u.%u)\n",
          gGateway.Addr[0], gGateway.Addr[1], gGateway.Addr[2], gGateway.Addr[3]);
    return TRUE;
}

/**
 * @brief Enable Bus Master + Memory + IO on PCI network controllers,
 *        then verify MAC address via direct MMIO read.
 *
 * Some UEFI firmwares leave PCI NICs with Bus Master disabled.
 * Additionally, some RTL8168 NICs need a software reset for the
 * EEPROM autoload to populate the MAC registers.
 *
 * This function:
 *   1. Enables PCI attributes (IO + Mem + BusMaster) via Attributes()
 *   2. Reads MAC directly from NIC MMIO (BAR0+0x00)
 *   3. If MAC is all-zero, issues RTL8168 software reset and waits
 *      for EEPROM autoload to complete
 *   4. Disconnects/reconnects so UNDI re-probes with valid state
 *
 * Must be called AFTER at least one ConnectController round.
 */
static VOID
UefiEnablePciNic(VOID)
{
    EFI_STATUS Status;
    UINTN Count = 0;
    EFI_HANDLE *Handles = NULL;
    UINTN i;
    UINTN Enabled = 0;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &gEfiPciIoGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || Count == 0)
    {
        TRACE("UEFI PCI: No PCI IO handles found (Status %lx)\n", Status);
        return;
    }

    TRACE("UEFI PCI: Scanning %lu PCI IO handles for network controllers...\n",
          (unsigned long)Count);

    for (i = 0; i < Count; i++)
    {
        EFI_PCI_IO_PROTOCOL *PciIo;
        UINT32 PciId;
        UINT8 ClassCode[3]; /* offset 0x09=ProgIf, 0x0A=SubClass, 0x0B=BaseClass */
        UINT16 VendorId, DeviceId;
        BOOLEAN IsNetwork;
        UINT64 Supports;
        UINT8 Mac[6];
        BOOLEAN AllZero;
        UINTN m;

        Status = GlobalSystemTable->BootServices->HandleProtocol(
            Handles[i], &gEfiPciIoGuid, (VOID **)&PciIo);
        if (EFI_ERROR(Status))
            continue;

        /* Read Vendor/Device ID (offset 0x00) */
        PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, 0x00, 1, &PciId);
        VendorId = (UINT16)(PciId & 0xFFFF);
        DeviceId = (UINT16)(PciId >> 16);

        /* Read Class Code: offset 0x09 = ProgIf, 0x0A = SubClass, 0x0B = BaseClass */
        PciIo->Pci.Read(PciIo, EfiPciIoWidthUint8, 0x09, 3, ClassCode);

        /* Match: class 0x02 (Network Controller) OR Realtek vendor */
        IsNetwork = (ClassCode[2] == 0x02) || (VendorId == 0x10EC);
        if (!IsNetwork)
            continue;

        TRACE("UEFI PCI: Found NIC [%04x:%04x] Class=%02x/%02x\n",
              (unsigned)VendorId, (unsigned)DeviceId,
              ClassCode[2], ClassCode[1]);

        /* Step 1: Enable IO + Memory + Bus Master via Attributes() */
        Supports = 0;
        Status = PciIo->Attributes(
            PciIo, EfiPciIoAttributeOperationSupported, 0, &Supports);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI PCI:   Attributes(Supported) failed (%lx)\n", Status);
            continue;
        }

        Supports &= (UINT64)EFI_PCI_DEVICE_ENABLE;
        Status = PciIo->Attributes(
            PciIo, EfiPciIoAttributeOperationEnable, Supports, NULL);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI PCI:   Attributes(Enable) failed (%lx)\n", Status);
            continue;
        }

        TRACE("UEFI PCI:   Enabled attributes 0x%lx\n", (unsigned long)Supports);

        /*
         * Step 2: Dump PCI BARs for diagnostics.
         * RTL8168 typically: BAR0=IO(256), BAR1=Mem(256), BAR2+=varies.
         */
        {
            UINT32 Bars[6];
            UINTN b;
            PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, 0x10, 6, Bars);
            for (b = 0; b < 6; b++)
            {
                TRACE("UEFI PCI:   BAR%lu = 0x%08lx (%s)\n",
                      (unsigned long)b, (unsigned long)Bars[b],
                      (Bars[b] & 1) ? "IO" : "MEM");
            }
        }

        /*
         * Step 3: Read MAC from NIC registers via IO BAR (BAR0).
         * RTL8168 BAR0 is IO space -- Mem.Read returns EFI_UNSUPPORTED.
         * The register layout is identical for IO and MMIO access.
         */
        RtlZeroMemory(Mac, sizeof(Mac));
        Status = PciIo->Io.Read(PciIo, EfiPciIoWidthUint8,
                                0, /* BAR index 0 = IO BAR */
                                (UINT64)RTL_REG_MAC0,
                                6, Mac);

        TRACE("UEFI PCI:   IO MAC: %02x:%02x:%02x:%02x:%02x:%02x (Status %lx)\n",
              Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5], Status);

        /* If IO failed, try MMIO on BAR1 */
        if (EFI_ERROR(Status))
        {
            Status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint8,
                                     1, /* BAR index 1 = Mem BAR */
                                     (UINT64)RTL_REG_MAC0,
                                     6, Mac);
            TRACE("UEFI PCI:   MEM(BAR1) MAC: %02x:%02x:%02x:%02x:%02x:%02x (Status %lx)\n",
                  Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5], Status);
        }

        /* Step 4: If MAC is all-zero and this is Realtek, try software reset */
        AllZero = TRUE;
        for (m = 0; m < 6; m++)
        {
            if (Mac[m] != 0) { AllZero = FALSE; break; }
        }

        if (AllZero && VendorId == 0x10EC)
        {
            UINT8 Reg8;

            /*
             * EEPROM is unprogrammed (common on compute module carrier
             * boards).  Program a locally-administered MAC directly into
             * the RTL8168 IDR registers.
             *
             * Locally-administered MACs have bit 1 of byte 0 set.
             * We derive bytes 4-5 from the PCI Device ID to give
             * different boards distinct addresses.
             */
            UINT8 NewMac[6] = { 0x02, 0xDE, 0xAD, 0x10, 0xEC, 0x68 };

            TRACE("UEFI PCI:   MAC is all-zero (unprogrammed EEPROM)\n");
            TRACE("UEFI PCI:   Programming locally-administered MAC: "
                  "%02x:%02x:%02x:%02x:%02x:%02x\n",
                  NewMac[0], NewMac[1], NewMac[2],
                  NewMac[3], NewMac[4], NewMac[5]);

            /* Enable config register writes (EEM=11) */
            Reg8 = RTL_9346_EEM_WRITEEN;
            PciIo->Io.Write(PciIo, EfiPciIoWidthUint8, 0,
                            (UINT64)RTL_REG_9346CR, 1, &Reg8);

            /* Write MAC to IDR0-IDR5 (offset 0x00-0x05) */
            PciIo->Io.Write(PciIo, EfiPciIoWidthUint8, 0,
                            (UINT64)RTL_REG_MAC0, 6, NewMac);

            /* Return to normal mode (EEM=00) */
            Reg8 = RTL_9346_EEM_NORMAL;
            PciIo->Io.Write(PciIo, EfiPciIoWidthUint8, 0,
                            (UINT64)RTL_REG_9346CR, 1, &Reg8);

            /* Verify the MAC was written */
            RtlZeroMemory(Mac, sizeof(Mac));
            PciIo->Io.Read(PciIo, EfiPciIoWidthUint8, 0,
                           (UINT64)RTL_REG_MAC0, 6, Mac);
            TRACE("UEFI PCI:   IDR readback: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  Mac[0], Mac[1], Mac[2], Mac[3], Mac[4], Mac[5]);
        }

        Enabled++;

        /*
         * Do NOT disconnect/reconnect here.  The UNDI driver is actively
         * managing the NIC (CMD=0x0C, Tx/Rx enabled).  Disconnecting it
         * after we modified the IDR registers behind its back causes a
         * hang in the driver's Stop() function.
         *
         * Instead, rely on the SNP re-init cycle (Shutdown -> Start ->
         * Initialize) in UefiConnectInitSnp() which runs later in the
         * connect loop.  The UNDI Initialize command re-reads the IDR
         * registers and picks up our programmed MAC.
         */
    }

    GlobalSystemTable->BootServices->FreePool(Handles);
    TRACE("UEFI PCI: Enabled %lu network controller(s)\n", (unsigned long)Enabled);
}

/**
 * @brief If SNP's CurrentAddress is all-zero, set a locally-administered MAC
 *        via StationAddress().
 *
 * The RtkUndiDxe driver reads the MAC from EEPROM via bit-bang during
 * its first hardware probe, NOT from the IDR registers.  When the
 * EEPROM is unprogrammed, the UNDI reports 00:00:00:00:00:00 to SNP
 * regardless of what we wrote to IDR.
 *
 * StationAddress() pushes our MAC through the UNDI command interface,
 * which updates both SNP->Mode->CurrentAddress and the hardware MAC
 * used for Ethernet framing.
 */
static VOID
UefiFixZeroMac(EFI_SIMPLE_NETWORK_PROTOCOL *Snp)
{
    UINT32 HwLen;
    UINTN j;
    BOOLEAN AllZero = TRUE;
    EFI_MAC_ADDRESS NewMac;
    EFI_STATUS Status;

    if (!Snp || !Snp->Mode)
        return;

    HwLen = Snp->Mode->HwAddressSize;
    if (HwLen > 32) HwLen = 32;

    for (j = 0; j < HwLen; j++)
    {
        if (Snp->Mode->CurrentAddress.Addr[j] != 0)
        {
            AllZero = FALSE;
            break;
        }
    }

    if (!AllZero)
        return;

    /* Locally-administered MAC: bit 1 of byte 0 set (02:xx:xx:xx:xx:xx) */
    RtlZeroMemory(&NewMac, sizeof(NewMac));
    NewMac.Addr[0] = 0x02;
    NewMac.Addr[1] = 0xDE;
    NewMac.Addr[2] = 0xAD;
    NewMac.Addr[3] = 0x10;
    NewMac.Addr[4] = 0xEC;
    NewMac.Addr[5] = 0x68;

    TRACE("UEFI Network: MAC all-zero after init, calling StationAddress()\n");
    Status = Snp->StationAddress(Snp, FALSE, &NewMac);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI Network: StationAddress failed (Status %lx)\n", Status);
    }
    else
    {
        TRACE("UEFI Network: StationAddress set to %02x:%02x:%02x:%02x:%02x:%02x\n",
              NewMac.Addr[0], NewMac.Addr[1], NewMac.Addr[2],
              NewMac.Addr[3], NewMac.Addr[4], NewMac.Addr[5]);
    }
}

/* INTERNAL HELPERS ***********************************************************/

/* (UefiFileNameMatch and UefiPatchHttpDxePcd removed -- no longer needed
 *  since we bypass HttpDxe.efi entirely via direct TCP4 usage.) */

/**
 * @brief Read an EFI driver file from the boot volume into memory and
 *        call LoadImage() to register it (but do NOT start it yet).
 *
 * @param Root         Opened root directory of the boot volume.
 * @param FilePath     UCS-2 path to the .efi driver file.
 * @param OutHandle    Receives the loaded image handle on success.
 * @return TRUE if the image was loaded successfully.
 */
static BOOLEAN
UefiLoadDxeImage(
    _In_  EFI_FILE_PROTOCOL *Root,
    _In_  CHAR16 *FilePath,
    _Out_ EFI_HANDLE *OutHandle)
{
    EFI_STATUS Status;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_FILE_INFO *Info = NULL;
    UINTN InfoSize;
    VOID *Buffer = NULL;
    UINTN FileSize;

    *OutHandle = NULL;

    /* Try to open the file */
    Status = Root->Open(Root, &File, FilePath, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
        return FALSE;

    /* Query file size via EFI_FILE_INFO */
    InfoSize = 0;
    Status = File->GetInfo(File, &gEfiFileInfoId, &InfoSize, NULL);
    if (Status != EFI_BUFFER_TOO_SMALL || InfoSize == 0)
    {
        TRACE("UEFI NetDrv: GetInfo sizing failed for %S (Status %lx)\n",
              FilePath, Status);
        File->Close(File);
        return FALSE;
    }

    /* Allocate info buffer from UEFI pool (heap may not be ready) */
    Status = GlobalSystemTable->BootServices->AllocatePool(
        EfiLoaderData, InfoSize, (VOID **)&Info);
    if (EFI_ERROR(Status) || !Info)
    {
        File->Close(File);
        return FALSE;
    }

    Status = File->GetInfo(File, &gEfiFileInfoId, &InfoSize, Info);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI NetDrv: GetInfo failed for %S (Status %lx)\n",
              FilePath, Status);
        GlobalSystemTable->BootServices->FreePool(Info);
        File->Close(File);
        return FALSE;
    }

    FileSize = (UINTN)Info->FileSize;
    GlobalSystemTable->BootServices->FreePool(Info);

    if (FileSize == 0)
    {
        File->Close(File);
        return FALSE;
    }

    /* Allocate a buffer and read the entire driver image */
    Status = GlobalSystemTable->BootServices->AllocatePool(
        EfiLoaderData, FileSize, &Buffer);
    if (EFI_ERROR(Status) || !Buffer)
    {
        File->Close(File);
        return FALSE;
    }

    {
        UINTN ReadSize = FileSize;
        Status = File->Read(File, &ReadSize, Buffer);
        if (EFI_ERROR(Status) || ReadSize != FileSize)
        {
            TRACE("UEFI NetDrv: Read failed for %S (Status %lx, got %lu/%lu)\n",
                  FilePath, Status, (unsigned long)ReadSize, (unsigned long)FileSize);
            GlobalSystemTable->BootServices->FreePool(Buffer);
            File->Close(File);
            return FALSE;
        }
    }
    File->Close(File);

    /* Load the PE image from the in-memory buffer (does NOT execute it) */
    Status = GlobalSystemTable->BootServices->LoadImage(
        FALSE,               /* BootPolicy = FALSE for drivers */
        GlobalImageHandle,   /* Parent */
        NULL,                /* DevicePath -- NULL since we supply buffer */
        Buffer,
        FileSize,
        OutHandle);

    GlobalSystemTable->BootServices->FreePool(Buffer);

    if (EFI_ERROR(Status) || !*OutHandle)
    {
        TRACE("UEFI NetDrv: LoadImage failed for %S (Status %lx)\n",
              FilePath, Status);
        *OutHandle = NULL;
        return FALSE;
    }

    TRACE("UEFI NetDrv: LoadImage OK for %S\n", FilePath);
    return TRUE;
}

/**
 * @brief Check if a filename has the ".efi" extension (case-insensitive).
 */
static BOOLEAN
IsEfiFile(CHAR16 *Name)
{
    UINTN Len = 0;
    CHAR16 *p;
    for (p = Name; *p; p++) Len++;
    if (Len < 5) return FALSE; /* minimum: "x.efi" */
    p = Name + Len - 4;
    return ((p[0] == L'.' || p[0] == L'.') &&
            (p[1] == L'e' || p[1] == L'E') &&
            (p[2] == L'f' || p[2] == L'F') &&
            (p[3] == L'i' || p[3] == L'I'));
}

/**
 * @brief Attempt to load external DXE drivers from \EFI\BOOT\drivers\.
 *
 * Opens the boot partition via UEFI SimpleFileSystem, enumerates the
 * drivers directory, and loads every .efi file found there.
 *
 * Uses a two-pass approach for robustness:
 *   Pass 1 -- LoadImage() all .efi files (registers PE images, no code runs)
 *   Pass 2 -- StartImage() all loaded images (entry points register driver
 *             bindings; all images are resident so inter-driver dependencies
 *             within the set can be satisfied)
 *
 * The build system populates this directory from
 * boot/freeldr/uefi_drivers/<variant>/ when UEFI_BOARD_VARIANT is set.
 */
static VOID
UefiTryLoadNetworkDrivers(VOID)
{
    EFI_STATUS Status;
    EFI_LOADED_IMAGE_PROTOCOL *LoadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Sfs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *Dir = NULL;
    EFI_HANDLE ImageHandles[MAX_DRIVER_FILES];
    UINTN ImageCount = 0;
    ULONG DriversStarted = 0;
    UINTN i;

    /* Get our own loaded image to find the boot device */
    Status = GlobalSystemTable->BootServices->HandleProtocol(
        GlobalImageHandle, &gEfiLipGuid, (VOID **)&LoadedImage);
    if (EFI_ERROR(Status) || !LoadedImage || !LoadedImage->DeviceHandle)
    {
        TRACE("UEFI NetDrv: Cannot get LoadedImage (Status %lx)\n", Status);
        return;
    }

    /* Open SimpleFileSystem on the boot device */
    Status = GlobalSystemTable->BootServices->HandleProtocol(
        LoadedImage->DeviceHandle, &gEfiSfsGuid, (VOID **)&Sfs);
    if (EFI_ERROR(Status) || !Sfs)
    {
        TRACE("UEFI NetDrv: No SimpleFileSystem on boot device (Status %lx)\n",
              Status);
        return;
    }

    /* Open the volume root */
    Status = Sfs->OpenVolume(Sfs, &Root);
    if (EFI_ERROR(Status) || !Root)
    {
        TRACE("UEFI NetDrv: OpenVolume failed (Status %lx)\n", Status);
        return;
    }

    /* Open the drivers directory */
    Status = Root->Open(Root, &Dir, DriverDirectory, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI NetDrv: %S not found (Status %lx) -- no external drivers\n",
              DriverDirectory, Status);
        Root->Close(Root);
        return;
    }

    TRACE("UEFI NetDrv: Enumerating %S ...\n", DriverDirectory);

    /*
     * Pass 1: Read directory and LoadImage() each .efi file.
     * This registers the PE images in memory but does NOT execute any
     * driver entry points yet.
     */
    {
        UINT8 EntryBuf[sizeof(EFI_FILE_INFO) + 260 * sizeof(CHAR16)];

        for (;;)
        {
            EFI_FILE_INFO *Entry = (EFI_FILE_INFO *)EntryBuf;
            UINTN BufSize = sizeof(EntryBuf);

            Status = Dir->Read(Dir, &BufSize, EntryBuf);
            if (EFI_ERROR(Status) || BufSize == 0)
                break;

            if (Entry->Attribute & EFI_FILE_DIRECTORY)
                continue;
            if (!IsEfiFile(Entry->FileName))
                continue;

            /* Build full path */
            {
                CHAR16 FullPath[300];
                UINTN dLen = 0, fLen = 0;
                CHAR16 *d, *f;

                for (d = DriverDirectory; *d; d++) dLen++;
                for (f = Entry->FileName; *f; f++) fLen++;

                if (dLen + 1 + fLen + 1 > 300)
                    continue;

                for (d = DriverDirectory, f = FullPath; *d; )
                    *f++ = *d++;
                *f++ = L'\\';
                {
                    CHAR16 *src = Entry->FileName;
                    while (*src) *f++ = *src++;
                }
                *f = L'\0';

                TRACE("UEFI NetDrv: Loading %S (%lu bytes)...\n",
                      FullPath, (unsigned long)Entry->FileSize);

                if (ImageCount < MAX_DRIVER_FILES &&
                    UefiLoadDxeImage(Root, FullPath, &ImageHandles[ImageCount]))
                {
                    ImageCount++;
                }
            }

            if (ImageCount >= MAX_DRIVER_FILES)
                break;
        }
    }

    Dir->Close(Dir);
    Root->Close(Root);

    if (ImageCount == 0)
    {
        TRACE("UEFI NetDrv: No .efi drivers found in %S\n", DriverDirectory);
        return;
    }

    TRACE("UEFI NetDrv: Loaded %lu image(s), starting drivers...\n",
          (unsigned long)ImageCount);

    /*
     * Pass 2: StartImage() all loaded images.  Now every PE image in the
     * set is resident, so inter-driver protocol lookups during entry
     * points are more likely to succeed.
     */
    for (i = 0; i < ImageCount; i++)
    {
        TRACE("UEFI NetDrv: StartImage [%lu/%lu]...\n",
              (unsigned long)(i + 1), (unsigned long)ImageCount);

        Status = GlobalSystemTable->BootServices->StartImage(
            ImageHandles[i], NULL, NULL);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI NetDrv: StartImage [%lu] failed (Status %lx)\n",
                  (unsigned long)(i + 1), Status);
            GlobalSystemTable->BootServices->UnloadImage(ImageHandles[i]);
        }
        else
        {
            DriversStarted++;
        }
    }

    TRACE("UEFI NetDrv: %lu/%lu driver(s) started successfully\n",
          (unsigned long)DriversStarted, (unsigned long)ImageCount);
}

/**
 * @brief Run a single pass of ConnectController() on every handle.
 * @param Recursive  TRUE to connect recursively, FALSE for single-level.
 * @return Number of handles that were connected.
 */
static UINTN
UefiConnectAllControllersOnce(_In_ BOOLEAN Recursive)
{
    EFI_STATUS Status;
    UINTN i, ConnHandleCount = 0;
    EFI_HANDLE *ConnHandles = NULL;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        AllHandles, NULL, NULL, &ConnHandleCount, &ConnHandles);
    if (EFI_ERROR(Status))
        return 0;

    for (i = 0; i < ConnHandleCount; i++)
    {
        GlobalSystemTable->BootServices->ConnectController(
            ConnHandles[i], NULL, NULL, Recursive);
    }
    GlobalSystemTable->BootServices->FreePool(ConnHandles);
    return ConnHandleCount;
}

/*
 * Maximum number of connect rounds.  The UEFI network stack is layered:
 *   SNP -> MNP -> IP4/ARP -> TCP4/UDP4
 * Each ConnectController round binds one layer, so we need at least 4-5
 * rounds to build the full TCP4 stack from a bare SNP instance.
 * A few extra rounds handle firmware quirks and inter-driver ordering.
 */
#define MAX_CONNECT_ROUNDS  8

/**
 * @brief Properly initialize SNP hardware on the first available NIC.
 *
 * ConnectController auto-initializes SNP to State=2, but often leaves
 * the NIC in a broken state (all-zero MAC, MediaPresent=0).  This helper
 * does Shutdown -> Start -> Initialize to get the hardware fully up.
 *
 * Called from the connect loop so the upper-stack drivers (MNP, IP4,
 * TCP4) see a working NIC when they bind.
 *
 * @return TRUE if SNP is now properly initialized, FALSE on failure.
 */
static BOOLEAN
UefiConnectInitSnp(VOID)
{
    EFI_STATUS Status;
    UINTN Count = 0;
    EFI_HANDLE *Handles = NULL;
    BOOLEAN AnyOk = FALSE;
    UINTN h;

    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &gEfiSnpGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || Count == 0)
        return FALSE;

    for (h = 0; h < Count; h++)
    {
        EFI_SIMPLE_NETWORK_PROTOCOL *Snp = NULL;
        UINT32 HwLen, i;

        Status = GlobalSystemTable->BootServices->HandleProtocol(
            Handles[h], &gEfiSnpGuid, (VOID **)&Snp);
        if (EFI_ERROR(Status) || !Snp || !Snp->Mode)
            continue;

        TRACE("UEFI Network: SNP[%lu] pre-init State=%u MediaPresent=%u\n",
              (unsigned long)h, Snp->Mode->State, Snp->Mode->MediaPresent);

        /*
         * The SNP is typically in State=2 (Initialized) with all-zero MAC
         * after ConnectController auto-initialized it.  We need to cycle
         * through Shutdown -> Start -> Initialize to get real hardware state.
         */
        if (Snp->Mode->State == EfiSimpleNetworkInitialized)
        {
            Snp->Shutdown(Snp);
            /* Shutdown leaves it in Started state */
        }

        if (Snp->Mode->State == EfiSimpleNetworkStopped)
        {
            Status = Snp->Start(Snp);
            if (EFI_ERROR(Status))
            {
                TRACE("UEFI Network: SNP[%lu] Start failed (%lx)\n",
                      (unsigned long)h, Status);
                continue;
            }
        }

        if (Snp->Mode->State == EfiSimpleNetworkStarted)
        {
            Status = Snp->Initialize(Snp, 0, 0);
            if (EFI_ERROR(Status))
            {
                TRACE("UEFI Network: SNP[%lu] Initialize failed (%lx)\n",
                      (unsigned long)h, Status);
                continue;
            }
        }

        if (Snp->Mode->State != EfiSimpleNetworkInitialized)
        {
            TRACE("UEFI Network: SNP[%lu] unexpected state %u\n",
                  (unsigned long)h, Snp->Mode->State);
            continue;
        }

        /* Enable receive filters */
        Snp->ReceiveFilters(
            Snp,
            EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
            0, FALSE, 0, NULL);

        /* Fix zero MAC via StationAddress() if UNDI cached zeros from EEPROM */
        UefiFixZeroMac(Snp);

        HwLen = Snp->Mode->HwAddressSize;
        if (HwLen > 32) HwLen = 32;
        TRACE("UEFI Network: SNP[%lu] MAC: ", (unsigned long)h);
        for (i = 0; i < HwLen; i++)
        {
            TRACE("%02x%s", Snp->Mode->CurrentAddress.Addr[i],
                  (i + 1 < HwLen) ? ":" : "");
        }
        TRACE("  State=%u  MediaPresent=%u\n",
              Snp->Mode->State, Snp->Mode->MediaPresent);

        AnyOk = TRUE;
    }

    GlobalSystemTable->BootServices->FreePool(Handles);
    return AnyOk;
}

/**
 * @brief Force-connect all handles so freshly-loaded drivers bind to hardware.
 *
 * Performs up to MAX_CONNECT_ROUNDS of ConnectController() on every handle.
 * The UEFI network stack is layered:
 *
 *   Non-recursive rounds: PCI bus enumeration, UNDI -> SNP binding
 *   (SNP re-init: Shutdown -> Start -> Initialize for real hardware state)
 *   Recursive rounds: MNP -> IP4/ARP -> TCP4/UDP4
 *
 * We keep early rounds non-recursive so SNP can be re-initialized and its
 * MAC fixed before upper-stack drivers bind.  Once SNP is ready, we switch
 * to recursive connects to build the remaining stack quickly.
 *
 * Upper-stack protocols install on existing handles (not new ones), so we
 * do NOT use handle-count as a stop condition.  Instead we check for TCP4
 * Service Binding each round.
 */
static VOID
UefiConnectAllControllers(VOID)
{
    UINTN Round;
    BOOLEAN SnpInitDone = FALSE;
    BOOLEAN PciEnabled = FALSE;

    for (Round = 1; Round <= MAX_CONNECT_ROUNDS; Round++)
    {
        EFI_STATUS Status;
        UINTN Count;
        UINTN Tcp4SbCount = 0;
        EFI_HANDLE *Tcp4SbHandles = NULL;
        BOOLEAN Recursive;

        /*
         * Keep the first rounds non-recursive until SNP has been
         * re-initialized and the MAC is fixed. This prevents MNP/IP
         * from binding with a stale all-zero station address.
         */
        Recursive = (SnpInitDone != FALSE);
        Count = UefiConnectAllControllersOnce(Recursive);
        TRACE("UEFI Network: Connect round %lu (%s) -- %lu handles\n",
              (unsigned long)Round,
              Recursive ? "recursive" : "non-recursive",
              (unsigned long)Count);

        /*
         * After the first connect round, PCI IO handles exist.
         * Enable Bus Master on any PCI NIC so the hardware can DMA
         * its MAC address from EEPROM.  This must happen before SNP
         * re-init and before upper-stack drivers bind.
         */
        if (!PciEnabled)
        {
            UefiEnablePciNic();
            PciEnabled = TRUE;
        }

        /*
         * After PCI enablement and the round that creates SNP,
         * re-initialize the NIC hardware properly.  ConnectController
         * auto-initializes SNP to State=2 but often leaves it with an
         * all-zero MAC and MediaPresent=0.  Upper-stack drivers need a
         * working NIC.
         */
        if (!SnpInitDone)
        {
            UINTN SnpCount = 0;
            EFI_HANDLE *SnpHandles = NULL;
            Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
                ByProtocol, &gEfiSnpGuid, NULL, &SnpCount, &SnpHandles);
            if (!EFI_ERROR(Status) && SnpCount > 0)
            {
                GlobalSystemTable->BootServices->FreePool(SnpHandles);
                if (UefiConnectInitSnp())
                    SnpInitDone = TRUE;
            }
            else if (SnpHandles)
            {
                GlobalSystemTable->BootServices->FreePool(SnpHandles);
            }
        }

        /* Check if TCP4 Service Binding is now available */
        Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
            ByProtocol, &gEfiTcp4SbGuid, NULL, &Tcp4SbCount, &Tcp4SbHandles);
        if (!EFI_ERROR(Status) && Tcp4SbCount > 0)
        {
            TRACE("UEFI Network: TCP4 Service Binding found after round %lu (%lu handle(s))\n",
                  (unsigned long)Round, (unsigned long)Tcp4SbCount);
            GlobalSystemTable->BootServices->FreePool(Tcp4SbHandles);
            return;
        }
        if (Tcp4SbHandles)
            GlobalSystemTable->BootServices->FreePool(Tcp4SbHandles);
    }

    TRACE("UEFI Network: Connect rounds complete (TCP4 SB not found)\n");
}

/**
 * @brief Log SNP details for the first available network interface.
 */
static VOID
UefiLogSnpDetails(
    _In_ EFI_HANDLE *Handles,
    _In_ UINTN Count)
{
    EFI_STATUS Status;
    EFI_SIMPLE_NETWORK_PROTOCOL *Snp = NULL;

    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Handles[0], &gEfiSnpGuid, (VOID **)&Snp);
    if (!EFI_ERROR(Status) && Snp && Snp->Mode)
    {
        UINTN i;
        UINT32 HwLen = Snp->Mode->HwAddressSize;
        if (HwLen > 32) HwLen = 32;

        TRACE("  SNP[0] MAC: ");
        for (i = 0; i < HwLen; i++)
        {
            TRACE("%02x%s", Snp->Mode->PermanentAddress.Addr[i],
                  (i + 1 < HwLen) ? ":" : "");
        }
        TRACE("  State=%u  MediaPresent=%u\n",
              Snp->Mode->State, Snp->Mode->MediaPresent);
    }
}

/* ============================================================================
 * Phase 1: SNP Initialization
 * ============================================================================ */

/**
 * @brief Initialize the Simple Network Protocol on the first available NIC.
 *
 * Handles the SNP state machine:
 *   - Stopped (state 0): Start + Initialize
 *   - Started (state 1): Initialize
 *   - Initialized (state 2) with all-zero MAC: Shutdown + Start + Initialize
 *   - Initialized (state 2) with real MAC: already good
 *
 * After initialization, enables unicast + broadcast receive filters.
 *
 * @return TRUE if SNP is initialized and ready, FALSE on failure.
 */
static BOOLEAN
UefiNetSnpInitialize(VOID)
{
    EFI_STATUS Status;
    UINTN Count = 0;
    EFI_HANDLE *Handles = NULL;
    EFI_SIMPLE_NETWORK_PROTOCOL *Snp = NULL;
    UINT32 HwLen, i;
    BOOLEAN AllZero;

    /* Locate SNP handles */
    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &gEfiSnpGuid, NULL, &Count, &Handles);
    if (EFI_ERROR(Status) || Count == 0)
    {
        TRACE("UEFI HttpBoot: No SNP handles found\n");
        return FALSE;
    }

    /* Get SNP protocol from first handle */
    Status = GlobalSystemTable->BootServices->HandleProtocol(
        Handles[0], &gEfiSnpGuid, (VOID **)&Snp);
    GlobalSystemTable->BootServices->FreePool(Handles);

    if (EFI_ERROR(Status) || !Snp || !Snp->Mode)
    {
        TRACE("UEFI HttpBoot: Failed to get SNP protocol (Status %lx)\n", Status);
        return FALSE;
    }

    TRACE("UEFI HttpBoot: SNP State=%u before init\n", Snp->Mode->State);

    /* State machine: bring SNP to Initialized state */
    switch (Snp->Mode->State)
    {
    case EfiSimpleNetworkStopped:
        /* Stopped -> Start -> Initialize */
        Status = Snp->Start(Snp);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI HttpBoot: SNP Start failed (Status %lx)\n", Status);
            return FALSE;
        }
        Status = Snp->Initialize(Snp, 0, 0);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI HttpBoot: SNP Initialize failed (Status %lx)\n", Status);
            return FALSE;
        }
        break;

    case EfiSimpleNetworkStarted:
        /* Started -> Initialize */
        Status = Snp->Initialize(Snp, 0, 0);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI HttpBoot: SNP Initialize failed (Status %lx)\n", Status);
            return FALSE;
        }
        break;

    case EfiSimpleNetworkInitialized:
        /* Check if MAC is all zeros (stale init) */
        HwLen = Snp->Mode->HwAddressSize;
        if (HwLen > 32) HwLen = 32;
        AllZero = TRUE;
        for (i = 0; i < HwLen; i++)
        {
            if (Snp->Mode->CurrentAddress.Addr[i] != 0)
            {
                AllZero = FALSE;
                break;
            }
        }
        if (AllZero)
        {
            /*
             * Shutdown -> Initialize (NOT Start).
             * Shutdown takes State 2 (Initialized) to State 1 (Started).
             * Initialize takes State 1 (Started) to State 2 (Initialized).
             * Start() is only valid from State 0 (Stopped) and would fail
             * with EFI_ALREADY_STARTED here.
             */
            TRACE("UEFI HttpBoot: SNP Initialized but MAC all-zeros, re-initializing\n");
            Snp->Shutdown(Snp);
            Status = Snp->Initialize(Snp, 0, 0);
            if (EFI_ERROR(Status))
            {
                TRACE("UEFI HttpBoot: SNP re-Initialize failed (Status %lx)\n", Status);
                return FALSE;
            }
        }
        else
        {
            TRACE("UEFI HttpBoot: SNP already initialized with valid MAC\n");
        }
        break;

    default:
        TRACE("UEFI HttpBoot: SNP unexpected state %u\n", Snp->Mode->State);
        return FALSE;
    }

    /* Fix zero MAC via StationAddress() if UNDI cached zeros from EEPROM */
    UefiFixZeroMac(Snp);

    /* Enable receive filters: unicast + broadcast */
    Status = Snp->ReceiveFilters(
        Snp,
        EFI_SIMPLE_NETWORK_RECEIVE_UNICAST | EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST,
        0,     /* Disable none */
        FALSE, /* Don't reset multicast filter */
        0,     /* No multicast addresses */
        NULL);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: ReceiveFilters failed (Status %lx) -- continuing\n", Status);
        /* Non-fatal: some drivers don't support filter configuration */
    }

    /* Log the real MAC address */
    HwLen = Snp->Mode->HwAddressSize;
    if (HwLen > 32) HwLen = 32;

    TRACE("UEFI HttpBoot: SNP initialized -- MAC: ");
    for (i = 0; i < HwLen; i++)
    {
        TRACE("%02x%s", Snp->Mode->CurrentAddress.Addr[i],
              (i + 1 < HwLen) ? ":" : "");
    }
    TRACE("  State=%u  MediaPresent=%u\n",
          Snp->Mode->State, Snp->Mode->MediaPresent);

    return TRUE;
}

/* ============================================================================
 * Phase 2: DHCP4 IP Configuration
 * ============================================================================ */

/**
 * @brief Obtain an IP address via DHCP4.
 *
 * Locates the DHCP4 Service Binding, creates a child, configures retry
 * timeouts, runs the DHCP4 handshake (synchronous), and extracts the
 * assigned IP address, subnet mask, and gateway.
 *
 * @param TimeoutSeconds  Total timeout for DHCP discovery (unused -- we use
 *                        per-retry timeouts of 4/8/16/32 seconds).
 * @return TRUE if DHCP succeeded and an IP was assigned, FALSE on failure.
 */
static BOOLEAN
UefiNetDhcp4Configure(UINT32 TimeoutSeconds)
{
    EFI_STATUS Status;
    EFI_DHCP4_CONFIG_DATA CfgData;
    EFI_DHCP4_MODE_DATA ModeData;
    UINT32 RetryTimeouts[] = { 4, 8, 16, 32 };

    (VOID)TimeoutSeconds;

    if (!gNetControllerHandle)
    {
        TRACE("UEFI HttpBoot: No network controller handle\n");
        return FALSE;
    }

    /* Get DHCP4 Service Binding from the same controller as TCP4 SB */
    Status = GlobalSystemTable->BootServices->HandleProtocol(
        gNetControllerHandle, &gEfiDhcp4SbGuid, (VOID **)&gDhcp4Sb);
    if (EFI_ERROR(Status) || !gDhcp4Sb)
    {
        TRACE("UEFI HttpBoot: DHCP4 SB not on controller (Status %lx)\n", Status);
        gDhcp4Sb = NULL;
        return FALSE;
    }

    /* Create DHCP4 child */
    gDhcp4Child = NULL;
    Status = gDhcp4Sb->CreateChild(gDhcp4Sb, &gDhcp4Child);
    if (EFI_ERROR(Status) || !gDhcp4Child)
    {
        TRACE("UEFI HttpBoot: DHCP4 CreateChild failed (Status %lx)\n", Status);
        gDhcp4Sb = NULL;
        return FALSE;
    }

    /* Get DHCP4 protocol from child */
    Status = GlobalSystemTable->BootServices->HandleProtocol(
        gDhcp4Child, &gEfiDhcp4Guid, (VOID **)&gDhcp4);
    if (EFI_ERROR(Status) || !gDhcp4)
    {
        TRACE("UEFI HttpBoot: Failed to get DHCP4 protocol (Status %lx)\n", Status);
        gDhcp4Sb->DestroyChild(gDhcp4Sb, gDhcp4Child);
        gDhcp4Child = NULL;
        gDhcp4Sb = NULL;
        return FALSE;
    }

    /* Configure DHCP4 with retry timeouts */
    RtlZeroMemory(&CfgData, sizeof(CfgData));
    CfgData.DiscoverTryCount = 4;
    CfgData.DiscoverTimeout  = RetryTimeouts;
    CfgData.RequestTryCount  = 4;
    CfgData.RequestTimeout   = RetryTimeouts;

    Status = gDhcp4->Configure(gDhcp4, &CfgData);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: DHCP4 Configure failed (Status %lx)\n", Status);
        gDhcp4Sb->DestroyChild(gDhcp4Sb, gDhcp4Child);
        gDhcp4Child = NULL;
        gDhcp4 = NULL;
        gDhcp4Sb = NULL;
        return FALSE;
    }

    /* Start DHCP4 -- synchronous (CompletionEvent = NULL) */
    TRACE("UEFI HttpBoot: Starting DHCP4...\n");
    Status = gDhcp4->Start(gDhcp4, NULL);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: DHCP4 Start failed (Status %lx)\n", Status);
        gDhcp4->Stop(gDhcp4);
        gDhcp4->Configure(gDhcp4, NULL);
        gDhcp4Sb->DestroyChild(gDhcp4Sb, gDhcp4Child);
        gDhcp4Child = NULL;
        gDhcp4 = NULL;
        gDhcp4Sb = NULL;
        return FALSE;
    }

    /* Get mode data to extract IP configuration */
    RtlZeroMemory(&ModeData, sizeof(ModeData));
    Status = gDhcp4->GetModeData(gDhcp4, &ModeData);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: DHCP4 GetModeData failed (Status %lx)\n", Status);
        gDhcp4->Stop(gDhcp4);
        gDhcp4->Configure(gDhcp4, NULL);
        gDhcp4Sb->DestroyChild(gDhcp4Sb, gDhcp4Child);
        gDhcp4Child = NULL;
        gDhcp4 = NULL;
        gDhcp4Sb = NULL;
        return FALSE;
    }

    if (ModeData.State != Dhcp4Bound)
    {
        TRACE("UEFI HttpBoot: DHCP4 not bound (State=%u)\n", ModeData.State);
        gDhcp4->Stop(gDhcp4);
        gDhcp4->Configure(gDhcp4, NULL);
        gDhcp4Sb->DestroyChild(gDhcp4Sb, gDhcp4Child);
        gDhcp4Child = NULL;
        gDhcp4 = NULL;
        gDhcp4Sb = NULL;
        return FALSE;
    }

    /* Store the IP configuration */
    {
        EFI_IPv4_ADDRESS Ip;
        EFI_IPv4_ADDRESS Mask;
        EFI_IPv4_ADDRESS Gw;
        EFI_DHCP4_PACKET *Pkt;
        UINT8 Buf[4];

        RtlCopyMemory(&Ip, &ModeData.ClientAddress, sizeof(Ip));
        RtlCopyMemory(&Mask, &ModeData.SubnetMask, sizeof(Mask));
        RtlCopyMemory(&Gw, &ModeData.RouterAddress, sizeof(Gw));

        Pkt = ModeData.ReplyPacket;
        if (Pkt)
        {
            if (!UefiIpv4IsZero(&Pkt->Dhcp4.Header.YourAddr))
            {
                RtlCopyMemory(&Ip, &Pkt->Dhcp4.Header.YourAddr, sizeof(Ip));
            }

            if (UefiDhcp4FindOption(Pkt, DHCP4_OPT_SUBNET_MASK, Buf, sizeof(Buf)))
            {
                RtlCopyMemory(&Mask, Buf, sizeof(Mask));
            }

            if (UefiDhcp4FindOption(Pkt, DHCP4_OPT_ROUTER, Buf, sizeof(Buf)))
            {
                RtlCopyMemory(&Gw, Buf, sizeof(Gw));
            }
        }

        RtlCopyMemory(&gLocalIp, &Ip, sizeof(EFI_IPv4_ADDRESS));
        RtlCopyMemory(&gSubnetMask, &Mask, sizeof(EFI_IPv4_ADDRESS));
        RtlCopyMemory(&gGateway, &Gw, sizeof(EFI_IPv4_ADDRESS));
    }

    TRACE("UEFI HttpBoot: DHCP4 bound -- IP: %u.%u.%u.%u  Mask: %u.%u.%u.%u  GW: %u.%u.%u.%u\n",
          gLocalIp.Addr[0], gLocalIp.Addr[1], gLocalIp.Addr[2], gLocalIp.Addr[3],
          gSubnetMask.Addr[0], gSubnetMask.Addr[1], gSubnetMask.Addr[2], gSubnetMask.Addr[3],
          gGateway.Addr[0], gGateway.Addr[1], gGateway.Addr[2], gGateway.Addr[3]);

    /*
     * Deconfigure the DHCP4 child to release its internal IP4 instance.
     * Per EDK2 HttpBootDhcp4Dora(): on success, pass a zeroed config
     * (not NULL) to release the IP4 binding while keeping the child alive.
     * This frees the station address so TCP4's IP4 child can claim it.
     */
    {
        EFI_DHCP4_CONFIG_DATA ZeroCfg;
        RtlZeroMemory(&ZeroCfg, sizeof(ZeroCfg));
        gDhcp4->Configure(gDhcp4, &ZeroCfg);
    }

    /* Register gateway via IP4Config2 (not manual address -- per EDK2,
     * the station address is passed explicitly to TCP4 Configure instead). */
    if (!UefiNetIp4Config2RegisterGateway())
        TRACE("UEFI HttpBoot: IP4Config2 gateway registration failed -- continuing\n");

    return TRUE;
}

/* ============================================================================
 * MD5 checksum (RFC 1321) for download integrity verification
 * ============================================================================ */

typedef struct {
    UINT32 State[4];
    UINT64 Count;        /* total bytes processed */
    UINT8  Buffer[64];   /* partial block accumulator */
} MD5_CTX;

#define MD5_F(x, y, z) (((x) & (y)) | ((~(x)) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~(z))))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~(z))))
#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_STEP(f, a, b, c, d, x, t, s) \
    (a) += f((b), (c), (d)) + (x) + (t); \
    (a) = MD5_ROTL((a), (s)); \
    (a) += (b)

static VOID
Md5Transform(UINT32 State[4], const UINT8 Block[64])
{
    UINT32 a = State[0], b = State[1], c = State[2], d = State[3];
    UINT32 M[16];
    UINTN i;

    for (i = 0; i < 16; i++)
    {
        M[i] = (UINT32)Block[i*4]           |
               ((UINT32)Block[i*4+1] << 8)  |
               ((UINT32)Block[i*4+2] << 16) |
               ((UINT32)Block[i*4+3] << 24);
    }

    /* Round 1 */
    MD5_STEP(MD5_F, a,b,c,d, M[ 0], 0xd76aa478,  7);
    MD5_STEP(MD5_F, d,a,b,c, M[ 1], 0xe8c7b756, 12);
    MD5_STEP(MD5_F, c,d,a,b, M[ 2], 0x242070db, 17);
    MD5_STEP(MD5_F, b,c,d,a, M[ 3], 0xc1bdceee, 22);
    MD5_STEP(MD5_F, a,b,c,d, M[ 4], 0xf57c0faf,  7);
    MD5_STEP(MD5_F, d,a,b,c, M[ 5], 0x4787c62a, 12);
    MD5_STEP(MD5_F, c,d,a,b, M[ 6], 0xa8304613, 17);
    MD5_STEP(MD5_F, b,c,d,a, M[ 7], 0xfd469501, 22);
    MD5_STEP(MD5_F, a,b,c,d, M[ 8], 0x698098d8,  7);
    MD5_STEP(MD5_F, d,a,b,c, M[ 9], 0x8b44f7af, 12);
    MD5_STEP(MD5_F, c,d,a,b, M[10], 0xffff5bb1, 17);
    MD5_STEP(MD5_F, b,c,d,a, M[11], 0x895cd7be, 22);
    MD5_STEP(MD5_F, a,b,c,d, M[12], 0x6b901122,  7);
    MD5_STEP(MD5_F, d,a,b,c, M[13], 0xfd987193, 12);
    MD5_STEP(MD5_F, c,d,a,b, M[14], 0xa679438e, 17);
    MD5_STEP(MD5_F, b,c,d,a, M[15], 0x49b40821, 22);

    /* Round 2 */
    MD5_STEP(MD5_G, a,b,c,d, M[ 1], 0xf61e2562,  5);
    MD5_STEP(MD5_G, d,a,b,c, M[ 6], 0xc040b340,  9);
    MD5_STEP(MD5_G, c,d,a,b, M[11], 0x265e5a51, 14);
    MD5_STEP(MD5_G, b,c,d,a, M[ 0], 0xe9b6c7aa, 20);
    MD5_STEP(MD5_G, a,b,c,d, M[ 5], 0xd62f105d,  5);
    MD5_STEP(MD5_G, d,a,b,c, M[10], 0x02441453,  9);
    MD5_STEP(MD5_G, c,d,a,b, M[15], 0xd8a1e681, 14);
    MD5_STEP(MD5_G, b,c,d,a, M[ 4], 0xe7d3fbc8, 20);
    MD5_STEP(MD5_G, a,b,c,d, M[ 9], 0x21e1cde6,  5);
    MD5_STEP(MD5_G, d,a,b,c, M[14], 0xc33707d6,  9);
    MD5_STEP(MD5_G, c,d,a,b, M[ 3], 0xf4d50d87, 14);
    MD5_STEP(MD5_G, b,c,d,a, M[ 8], 0x455a14ed, 20);
    MD5_STEP(MD5_G, a,b,c,d, M[13], 0xa9e3e905,  5);
    MD5_STEP(MD5_G, d,a,b,c, M[ 2], 0xfcefa3f8,  9);
    MD5_STEP(MD5_G, c,d,a,b, M[ 7], 0x676f02d9, 14);
    MD5_STEP(MD5_G, b,c,d,a, M[12], 0x8d2a4c8a, 20);

    /* Round 3 */
    MD5_STEP(MD5_H, a,b,c,d, M[ 5], 0xfffa3942,  4);
    MD5_STEP(MD5_H, d,a,b,c, M[ 8], 0x8771f681, 11);
    MD5_STEP(MD5_H, c,d,a,b, M[11], 0x6d9d6122, 16);
    MD5_STEP(MD5_H, b,c,d,a, M[14], 0xfde5380c, 23);
    MD5_STEP(MD5_H, a,b,c,d, M[ 1], 0xa4beea44,  4);
    MD5_STEP(MD5_H, d,a,b,c, M[ 4], 0x4bdecfa9, 11);
    MD5_STEP(MD5_H, c,d,a,b, M[ 7], 0xf6bb4b60, 16);
    MD5_STEP(MD5_H, b,c,d,a, M[10], 0xbebfbc70, 23);
    MD5_STEP(MD5_H, a,b,c,d, M[13], 0x289b7ec6,  4);
    MD5_STEP(MD5_H, d,a,b,c, M[ 0], 0xeaa127fa, 11);
    MD5_STEP(MD5_H, c,d,a,b, M[ 3], 0xd4ef3085, 16);
    MD5_STEP(MD5_H, b,c,d,a, M[ 6], 0x04881d05, 23);
    MD5_STEP(MD5_H, a,b,c,d, M[ 9], 0xd9d4d039,  4);
    MD5_STEP(MD5_H, d,a,b,c, M[12], 0xe6db99e5, 11);
    MD5_STEP(MD5_H, c,d,a,b, M[15], 0x1fa27cf8, 16);
    MD5_STEP(MD5_H, b,c,d,a, M[ 2], 0xc4ac5665, 23);

    /* Round 4 */
    MD5_STEP(MD5_I, a,b,c,d, M[ 0], 0xf4292244,  6);
    MD5_STEP(MD5_I, d,a,b,c, M[ 7], 0x432aff97, 10);
    MD5_STEP(MD5_I, c,d,a,b, M[14], 0xab9423a7, 15);
    MD5_STEP(MD5_I, b,c,d,a, M[ 5], 0xfc93a039, 21);
    MD5_STEP(MD5_I, a,b,c,d, M[12], 0x655b59c3,  6);
    MD5_STEP(MD5_I, d,a,b,c, M[ 3], 0x8f0ccc92, 10);
    MD5_STEP(MD5_I, c,d,a,b, M[10], 0xffeff47d, 15);
    MD5_STEP(MD5_I, b,c,d,a, M[ 1], 0x85845dd1, 21);
    MD5_STEP(MD5_I, a,b,c,d, M[ 8], 0x6fa87e4f,  6);
    MD5_STEP(MD5_I, d,a,b,c, M[15], 0xfe2ce6e0, 10);
    MD5_STEP(MD5_I, c,d,a,b, M[ 6], 0xa3014314, 15);
    MD5_STEP(MD5_I, b,c,d,a, M[13], 0x4e0811a1, 21);
    MD5_STEP(MD5_I, a,b,c,d, M[ 4], 0xf7537e82,  6);
    MD5_STEP(MD5_I, d,a,b,c, M[11], 0xbd3af235, 10);
    MD5_STEP(MD5_I, c,d,a,b, M[ 2], 0x2ad7d2bb, 15);
    MD5_STEP(MD5_I, b,c,d,a, M[ 9], 0xeb86d391, 21);

    State[0] += a;
    State[1] += b;
    State[2] += c;
    State[3] += d;
}

static VOID
Md5Init(MD5_CTX *Ctx)
{
    Ctx->Count = 0;
    Ctx->State[0] = 0x67452301;
    Ctx->State[1] = 0xefcdab89;
    Ctx->State[2] = 0x98badcfe;
    Ctx->State[3] = 0x10325476;
}

static VOID
Md5Update(MD5_CTX *Ctx, const VOID *Data, UINTN Len)
{
    const UINT8 *p = (const UINT8 *)Data;
    UINTN Index = (UINTN)(Ctx->Count & 0x3F);

    Ctx->Count += Len;

    /* Fill partial buffer first */
    if (Index > 0)
    {
        UINTN Fill = 64 - Index;
        if (Len < Fill)
        {
            RtlCopyMemory(&Ctx->Buffer[Index], p, Len);
            return;
        }
        RtlCopyMemory(&Ctx->Buffer[Index], p, Fill);
        Md5Transform(Ctx->State, Ctx->Buffer);
        p += Fill;
        Len -= Fill;
    }

    /* Process full 64-byte blocks */
    while (Len >= 64)
    {
        Md5Transform(Ctx->State, p);
        p += 64;
        Len -= 64;
    }

    /* Save remaining bytes */
    if (Len > 0)
        RtlCopyMemory(Ctx->Buffer, p, Len);
}

static VOID
Md5Final(MD5_CTX *Ctx, UINT8 Digest[16])
{
    UINT8 Pad[72];
    UINTN Index = (UINTN)(Ctx->Count & 0x3F);
    UINTN PadLen;
    UINT64 Bits = Ctx->Count * 8;
    UINTN i;

    /* Pad to 56 mod 64, then append 8-byte LE bit count */
    PadLen = (Index < 56) ? (56 - Index) : (120 - Index);
    RtlZeroMemory(Pad, sizeof(Pad));
    Pad[0] = 0x80;

    Pad[PadLen + 0] = (UINT8)(Bits);
    Pad[PadLen + 1] = (UINT8)(Bits >> 8);
    Pad[PadLen + 2] = (UINT8)(Bits >> 16);
    Pad[PadLen + 3] = (UINT8)(Bits >> 24);
    Pad[PadLen + 4] = (UINT8)(Bits >> 32);
    Pad[PadLen + 5] = (UINT8)(Bits >> 40);
    Pad[PadLen + 6] = (UINT8)(Bits >> 48);
    Pad[PadLen + 7] = (UINT8)(Bits >> 56);

    Md5Update(Ctx, Pad, PadLen + 8);

    /* Encode final state as little-endian bytes */
    for (i = 0; i < 4; i++)
    {
        Digest[i*4 + 0] = (UINT8)(Ctx->State[i]);
        Digest[i*4 + 1] = (UINT8)(Ctx->State[i] >> 8);
        Digest[i*4 + 2] = (UINT8)(Ctx->State[i] >> 16);
        Digest[i*4 + 3] = (UINT8)(Ctx->State[i] >> 24);
    }
}

/* ============================================================================
 * Phase 3: TCP4-based HTTP Download
 *
 * Instead of using EFI_HTTP_PROTOCOL (which requires HttpDxe.efi with
 * PcdAllowHttpConnections=TRUE for plain HTTP), we talk directly to
 * EFI_TCP4_PROTOCOL and construct raw HTTP/1.1 request/response text.
 * ============================================================================ */

/**
 * @brief Trivial event callback for TCP4 async tokens (no-op).
 */
static VOID EFIAPI
UefiTcp4Notify(
    IN EFI_EVENT Event,
    IN VOID *Context)
{
    (VOID)Event;
    (VOID)Context;
}

/**
 * @brief Wait for a TCP4 completion token, with timeout.
 *
 * Polls Tcp4->Poll() in a loop, checking Token->Status directly.
 * We cannot use CheckEvent() here because the token's event is created
 * with EVT_NOTIFY_SIGNAL, and per the UEFI spec CheckEvent() returns
 * EFI_INVALID_PARAMETER for signal-type events.  Instead, the TCP4
 * driver sets Token->Status from EFI_NOT_READY to the result code
 * when the operation completes.
 *
 * @param Tcp4   TCP4 protocol instance.
 * @param Token  Completion token to wait for (Status must be EFI_NOT_READY).
 * @return Token->Status on completion, EFI_TIMEOUT if timed out.
 */
static EFI_STATUS
UefiTcp4WaitForToken(
    _In_ EFI_TCP4_PROTOCOL *Tcp4,
    _In_ EFI_TCP4_COMPLETION_TOKEN *Token)
{
    EFI_STATUS Status;
    EFI_EVENT TimerEvent = NULL;

    Status = GlobalSystemTable->BootServices->CreateEvent(
        EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvent);
    if (EFI_ERROR(Status))
        TimerEvent = NULL;

    if (TimerEvent)
    {
        GlobalSystemTable->BootServices->SetTimer(
            TimerEvent, TimerRelative, TCP4_WAIT_TIMEOUT_100NS);
    }

    for (;;)
    {
        Tcp4->Poll(Tcp4);

        /* Check if the operation completed by inspecting Token->Status.
         * The TCP4 driver changes it from EFI_NOT_READY to the result. */
        if (Token->Status != EFI_NOT_READY)
            break;

        if (TimerEvent)
        {
            Status = GlobalSystemTable->BootServices->CheckEvent(TimerEvent);
            if (Status == EFI_SUCCESS)
            {
                TRACE("UEFI HttpBoot: TCP4 wait timed out\n");
                GlobalSystemTable->BootServices->CloseEvent(TimerEvent);
                return EFI_TIMEOUT;
            }
        }
    }

    if (TimerEvent)
        GlobalSystemTable->BootServices->CloseEvent(TimerEvent);

    return Token->Status;
}

/**
 * @brief Parse an IPv4 dotted-decimal string into an EFI_IPv4_ADDRESS.
 */
static BOOLEAN
UefiParseIpv4(
    _In_ PCSTR Str,
    _Out_ EFI_IPv4_ADDRESS *Addr)
{
    UINTN i;
    PCSTR p = Str;

    for (i = 0; i < 4; i++)
    {
        UINT32 Val = 0;
        if (*p < '0' || *p > '9') return FALSE;
        while (*p >= '0' && *p <= '9')
        {
            Val = Val * 10 + (*p - '0');
            p++;
        }
        if (Val > 255) return FALSE;
        Addr->Addr[i] = (UINT8)Val;
        if (i < 3)
        {
            if (*p != '.') return FALSE;
            p++;
        }
    }
    return TRUE;
}

/**
 * @brief Extract the host portion from an HTTP URL.
 *
 * Given "http://192.168.1.100:8080/path", extracts "192.168.1.100".
 */
static BOOLEAN
UefiNetExtractHost(
    _In_ PCSTR Url,
    _Out_writes_(BufSize) CHAR8 *HostBuf,
    _In_ UINTN BufSize)
{
    PCSTR p;
    UINTN i;

    if (Url[0] == 'h' && Url[1] == 't' && Url[2] == 't' && Url[3] == 'p' &&
        Url[4] == ':' && Url[5] == '/' && Url[6] == '/')
        p = Url + 7;
    else
        return FALSE;

    for (i = 0; *p && *p != '/' && *p != ':' && i + 1 < BufSize; i++, p++)
        HostBuf[i] = *p;
    HostBuf[i] = '\0';
    return (i > 0);
}

/**
 * @brief Extract the path portion from an HTTP URL.
 *
 * Returns pointer to the '/' starting the path, or "/" if no path.
 */
static PCSTR
UefiNetExtractPath(_In_ PCSTR Url)
{
    PCSTR p;

    if (Url[0] == 'h' && Url[1] == 't' && Url[2] == 't' && Url[3] == 'p' &&
        Url[4] == ':' && Url[5] == '/' && Url[6] == '/')
        p = Url + 7;
    else
        return "/";

    /* Skip host */
    while (*p && *p != '/' && *p != ':') p++;
    /* Skip optional port */
    if (*p == ':') { p++; while (*p >= '0' && *p <= '9') p++; }

    return (*p == '/') ? p : "/";
}

/**
 * @brief Extract the port from an HTTP URL (default 80).
 */
static UINT16
UefiNetExtractPort(_In_ PCSTR Url)
{
    PCSTR p;
    UINT32 Port = 0;

    if (Url[0] == 'h' && Url[1] == 't' && Url[2] == 't' && Url[3] == 'p' &&
        Url[4] == ':' && Url[5] == '/' && Url[6] == '/')
        p = Url + 7;
    else
        return 80;

    /* Skip host */
    while (*p && *p != '/' && *p != ':') p++;

    if (*p == ':')
    {
        p++;
        while (*p >= '0' && *p <= '9')
        {
            Port = Port * 10 + (*p - '0');
            p++;
        }
        if (Port > 0 && Port <= 65535)
            return (UINT16)Port;
    }

    return 80;
}

/**
 * @brief Create a TCP4 child, configure, add default route, and connect.
 *
 * @param RemoteIp  Remote IPv4 address.
 * @param Port      Remote TCP port.
 * @return TRUE on success (gTcp4 is usable), FALSE on failure.
 */
static BOOLEAN
UefiTcp4Connect(
    _In_ EFI_IPv4_ADDRESS *RemoteIp,
    _In_ UINT16 Port)
{
    EFI_STATUS Status;
    EFI_TCP4_CONFIG_DATA CfgData;
    EFI_TCP4_CONNECTION_TOKEN ConnToken;
    EFI_IPv4_ADDRESS ZeroAddr;

    if (!gNetControllerHandle)
    {
        TRACE("UEFI HttpBoot: No network controller handle\n");
        return FALSE;
    }

    /* Get TCP4 Service Binding */
    Status = GlobalSystemTable->BootServices->HandleProtocol(
        gNetControllerHandle, &gEfiTcp4SbGuid, (VOID **)&gTcp4Sb);
    if (EFI_ERROR(Status) || !gTcp4Sb)
    {
        TRACE("UEFI HttpBoot: TCP4 SB not on controller (Status %lx)\n", Status);
        gTcp4Sb = NULL;
        return FALSE;
    }

    /* Create TCP4 child */
    gTcp4Child = NULL;
    Status = gTcp4Sb->CreateChild(gTcp4Sb, &gTcp4Child);
    if (EFI_ERROR(Status) || !gTcp4Child)
    {
        TRACE("UEFI HttpBoot: TCP4 CreateChild failed (Status %lx)\n", Status);
        gTcp4Sb = NULL;
        return FALSE;
    }

    /* Get TCP4 protocol from child */
    Status = GlobalSystemTable->BootServices->HandleProtocol(
        gTcp4Child, &gEfiTcp4Guid, (VOID **)&gTcp4);
    if (EFI_ERROR(Status) || !gTcp4)
    {
        TRACE("UEFI HttpBoot: Failed to get TCP4 protocol (Status %lx)\n", Status);
        gTcp4Sb->DestroyChild(gTcp4Sb, gTcp4Child);
        gTcp4Child = NULL;
        gTcp4Sb = NULL;
        return FALSE;
    }

    /* Configure TCP4 with our DHCP-obtained local address */
    RtlZeroMemory(&CfgData, sizeof(CfgData));
    CfgData.TypeOfService = 0;
    CfgData.TimeToLive = 64;
    CfgData.AccessPoint.UseDefaultAddress = FALSE;
    RtlCopyMemory(&CfgData.AccessPoint.StationAddress, &gLocalIp, sizeof(EFI_IPv4_ADDRESS));
    RtlCopyMemory(&CfgData.AccessPoint.SubnetMask, &gSubnetMask, sizeof(EFI_IPv4_ADDRESS));
    CfgData.AccessPoint.StationPort = 0; /* ephemeral */
    RtlCopyMemory(&CfgData.AccessPoint.RemoteAddress, RemoteIp, sizeof(EFI_IPv4_ADDRESS));
    CfgData.AccessPoint.RemotePort = Port;
    CfgData.AccessPoint.ActiveFlag = TRUE;

    /* Tune TCP for bulk download throughput.
     * Default UEFI TCP4 buffers are tiny (8KB per EDK2 Socket.h defaults),
     * giving a minuscule receive window that throttles the sender.
     * EDK2 TcpDxe supports up to 2MB (TCP_RCV_BUF_SIZE in TcpProto.h).
     * We try progressively less aggressive configs until one is accepted. */
    {
        static EFI_TCP4_OPTION TcpOption;
        BOOLEAN Configured = FALSE;

        /* Attempt 1: 2MB receive + window scaling (EDK2 max).
         * NOTE: EnableSelectiveAck/EnablePathMtuDiscovery are forced FALSE
         * by EDK2 TcpDispatcher.c -- do NOT request them. */
        RtlZeroMemory(&TcpOption, sizeof(TcpOption));
        TcpOption.ReceiveBufferSize   = 2 * 1024 * 1024;  /* 2MB */
        TcpOption.SendBufferSize      = 64 * 1024;
        TcpOption.ConnectionTimeout   = 30;
        TcpOption.DataRetries         = 12;
        TcpOption.FinTimeout          = 2;
        TcpOption.TimeWaitTimeout     = 2;
        TcpOption.EnableNagle         = FALSE;
        TcpOption.EnableWindowScaling = TRUE;
        CfgData.ControlOption = &TcpOption;

        Status = gTcp4->Configure(gTcp4, &CfgData);
        if (!EFI_ERROR(Status))
        {
            TRACE("UEFI HttpBoot: TCP4 configured (2MB window, scaling)\n");
            Configured = TRUE;
        }

        /* Attempt 2: 64KB receive, no window scaling */
        if (!Configured)
        {
            TRACE("UEFI HttpBoot: TCP4 attempt 1 failed (%lx), trying 64KB\n", Status);
            gTcp4->Configure(gTcp4, NULL);

            RtlZeroMemory(&TcpOption, sizeof(TcpOption));
            TcpOption.ReceiveBufferSize  = 64 * 1024;
            TcpOption.SendBufferSize     = 64 * 1024;
            TcpOption.ConnectionTimeout  = 30;
            TcpOption.DataRetries        = 12;
            TcpOption.FinTimeout         = 2;
            TcpOption.TimeWaitTimeout    = 2;
            TcpOption.EnableNagle        = FALSE;
            CfgData.ControlOption = &TcpOption;

            Status = gTcp4->Configure(gTcp4, &CfgData);
            if (!EFI_ERROR(Status))
            {
                TRACE("UEFI HttpBoot: TCP4 configured (64KB buffers)\n");
                Configured = TRUE;
            }
        }

        /* Attempt 3: No ControlOption at all (firmware defaults ~8KB) */
        if (!Configured)
        {
            TRACE("UEFI HttpBoot: TCP4 attempt 2 failed (%lx), using defaults\n", Status);
            gTcp4->Configure(gTcp4, NULL);

            CfgData.ControlOption = NULL;
            Status = gTcp4->Configure(gTcp4, &CfgData);
            if (!EFI_ERROR(Status))
            {
                TRACE("UEFI HttpBoot: TCP4 configured (firmware defaults)\n");
                Configured = TRUE;
            }
        }

        if (!Configured)
        {
            TRACE("UEFI HttpBoot: TCP4 Configure failed (Status %lx)\n", Status);
            gTcp4Sb->DestroyChild(gTcp4Sb, gTcp4Child);
            gTcp4Child = NULL;
            gTcp4 = NULL;
            gTcp4Sb = NULL;
            return FALSE;
        }
    }

    /* Add default route via gateway so we can reach hosts outside our subnet */
    if (!UefiIpv4IsZero(&gGateway))
    {
        RtlZeroMemory(&ZeroAddr, sizeof(ZeroAddr));
        Status = gTcp4->Routes(gTcp4, FALSE, &ZeroAddr, &ZeroAddr, &gGateway);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI HttpBoot: TCP4 Routes (default gw) failed (Status %lx) -- continuing\n",
                  Status);
            /* Non-fatal: local-subnet connections still work */
        }
        else
        {
            TRACE("UEFI HttpBoot: TCP4 default route added via %u.%u.%u.%u\n",
                  gGateway.Addr[0], gGateway.Addr[1], gGateway.Addr[2], gGateway.Addr[3]);
        }
    }

    /* Initiate TCP connection */
    RtlZeroMemory(&ConnToken, sizeof(ConnToken));
    Status = GlobalSystemTable->BootServices->CreateEvent(
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
        UefiTcp4Notify, NULL, &ConnToken.CompletionToken.Event);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: CreateEvent for TCP4 connect failed (Status %lx)\n", Status);
        gTcp4->Configure(gTcp4, NULL);
        gTcp4Sb->DestroyChild(gTcp4Sb, gTcp4Child);
        gTcp4Child = NULL;
        gTcp4 = NULL;
        gTcp4Sb = NULL;
        return FALSE;
    }
    ConnToken.CompletionToken.Status = EFI_NOT_READY;

    TRACE("UEFI HttpBoot: TCP4 connecting to %u.%u.%u.%u:%u...\n",
          RemoteIp->Addr[0], RemoteIp->Addr[1], RemoteIp->Addr[2], RemoteIp->Addr[3],
          (unsigned)Port);

    Status = gTcp4->Connect(gTcp4, &ConnToken);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: TCP4 Connect failed (Status %lx)\n", Status);
        GlobalSystemTable->BootServices->CloseEvent(ConnToken.CompletionToken.Event);
        gTcp4->Configure(gTcp4, NULL);
        gTcp4Sb->DestroyChild(gTcp4Sb, gTcp4Child);
        gTcp4Child = NULL;
        gTcp4 = NULL;
        gTcp4Sb = NULL;
        return FALSE;
    }

    Status = UefiTcp4WaitForToken(gTcp4, &ConnToken.CompletionToken);
    GlobalSystemTable->BootServices->CloseEvent(ConnToken.CompletionToken.Event);

    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: TCP4 Connect completion failed (Status %lx)\n", Status);
        gTcp4->Configure(gTcp4, NULL);
        gTcp4Sb->DestroyChild(gTcp4Sb, gTcp4Child);
        gTcp4Child = NULL;
        gTcp4 = NULL;
        gTcp4Sb = NULL;
        return FALSE;
    }

    TRACE("UEFI HttpBoot: TCP4 connected successfully\n");

    /* Reset receive buffer */
    gRecvBufLen = 0;
    gRecvBufPos = 0;

    return TRUE;
}

/**
 * @brief Send data over the TCP4 connection.
 */
static BOOLEAN
UefiTcp4Send(
    _In_ VOID *Data,
    _In_ UINTN Length)
{
    EFI_STATUS Status;
    EFI_TCP4_IO_TOKEN TxToken;
    EFI_TCP4_TRANSMIT_DATA TxData;
    EFI_TCP4_FRAGMENT_DATA Fragment;

    Fragment.FragmentLength = (UINT32)Length;
    Fragment.FragmentBuffer = Data;

    RtlZeroMemory(&TxData, sizeof(TxData));
    TxData.Push = TRUE;
    TxData.Urgent = FALSE;
    TxData.DataLength = (UINT32)Length;
    TxData.FragmentCount = 1;
    TxData.FragmentTable[0] = Fragment;

    RtlZeroMemory(&TxToken, sizeof(TxToken));
    Status = GlobalSystemTable->BootServices->CreateEvent(
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
        UefiTcp4Notify, NULL, &TxToken.CompletionToken.Event);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: CreateEvent for TCP4 send failed (Status %lx)\n", Status);
        return FALSE;
    }
    TxToken.CompletionToken.Status = EFI_NOT_READY;
    TxToken.Packet.TxData = &TxData;

    Status = gTcp4->Transmit(gTcp4, &TxToken);
    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: TCP4 Transmit failed (Status %lx)\n", Status);
        GlobalSystemTable->BootServices->CloseEvent(TxToken.CompletionToken.Event);
        return FALSE;
    }

    Status = UefiTcp4WaitForToken(gTcp4, &TxToken.CompletionToken);
    GlobalSystemTable->BootServices->CloseEvent(TxToken.CompletionToken.Event);

    if (EFI_ERROR(Status))
    {
        TRACE("UEFI HttpBoot: TCP4 Transmit completion failed (Status %lx)\n", Status);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Receive raw data from TCP4 (single receive operation).
 *
 * @param Buffer    Destination buffer.
 * @param Size      Buffer size (max bytes to receive).
 * @param Received  Receives the actual bytes received.
 * @return EFI_SUCCESS on data received, EFI_CONNECTION_FIN on EOF, or error.
 */
static EFI_STATUS
UefiTcp4RecvRaw(
    _Out_ VOID *Buffer,
    _In_ UINTN Size,
    _Out_ UINTN *Received)
{
    EFI_STATUS Status;
    EFI_TCP4_IO_TOKEN RxToken;
    EFI_TCP4_RECEIVE_DATA RxData;
    EFI_TCP4_FRAGMENT_DATA Fragment;

    *Received = 0;

    Fragment.FragmentLength = (UINT32)Size;
    Fragment.FragmentBuffer = Buffer;

    RtlZeroMemory(&RxData, sizeof(RxData));
    RxData.UrgentFlag = FALSE;
    RxData.DataLength = (UINT32)Size;
    RxData.FragmentCount = 1;
    RxData.FragmentTable[0] = Fragment;

    RtlZeroMemory(&RxToken, sizeof(RxToken));
    Status = GlobalSystemTable->BootServices->CreateEvent(
        EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
        UefiTcp4Notify, NULL, &RxToken.CompletionToken.Event);
    if (EFI_ERROR(Status))
        return Status;

    RxToken.CompletionToken.Status = EFI_NOT_READY;
    RxToken.Packet.RxData = &RxData;

    Status = gTcp4->Receive(gTcp4, &RxToken);
    if (EFI_ERROR(Status))
    {
        GlobalSystemTable->BootServices->CloseEvent(RxToken.CompletionToken.Event);
        return Status;
    }

    Status = UefiTcp4WaitForToken(gTcp4, &RxToken.CompletionToken);
    GlobalSystemTable->BootServices->CloseEvent(RxToken.CompletionToken.Event);

    if (EFI_ERROR(Status))
        return Status;

    *Received = RxData.DataLength;
    return EFI_SUCCESS;
}

/**
 * @brief Read one byte from the buffered TCP4 receive layer.
 *
 * Refills the 4KB buffer when empty.  Efficient for header parsing.
 */
static EFI_STATUS
UefiTcp4RecvByte(_Out_ UINT8 *Byte)
{
    EFI_STATUS Status;
    UINTN Got;

    if (gRecvBufPos >= gRecvBufLen)
    {
        /* Buffer empty -- refill */
        Status = UefiTcp4RecvRaw(gRecvBuf, TCP4_RECV_BUF_SIZE, &Got);
        if (EFI_ERROR(Status) || Got == 0)
            return EFI_ERROR(Status) ? Status : EFI_CONNECTION_FIN;

        gRecvBufLen = Got;
        gRecvBufPos = 0;
    }

    *Byte = gRecvBuf[gRecvBufPos++];
    return EFI_SUCCESS;
}

/**
 * @brief Read exactly N bytes from the TCP4 connection.
 *
 * Uses the buffered layer first, then reads remaining data directly.
 */
static BOOLEAN
UefiTcp4RecvExact(
    _Out_ VOID *Buffer,
    _In_ UINTN Length)
{
    UINT8 *Dst = (UINT8 *)Buffer;
    UINTN Remaining = Length;

    /* First, drain anything left in the receive buffer */
    while (Remaining > 0 && gRecvBufPos < gRecvBufLen)
    {
        *Dst++ = gRecvBuf[gRecvBufPos++];
        Remaining--;
    }

    /* Read the rest directly from TCP4 */
    while (Remaining > 0)
    {
        EFI_STATUS Status;
        UINTN Got = 0;

        Status = UefiTcp4RecvRaw(Dst, Remaining, &Got);
        if (EFI_ERROR(Status) || Got == 0)
        {
            TRACE("UEFI HttpBoot: TCP4 RecvExact: short read (%lu/%lu)\n",
                  (unsigned long)(Length - Remaining), (unsigned long)Length);
            return FALSE;
        }
        Dst += Got;
        Remaining -= Got;
    }

    return TRUE;
}

/**
 * @brief Gracefully close the TCP4 connection and destroy the child.
 */
static VOID
UefiTcp4Close(VOID)
{
    if (gTcp4 && gTcp4Sb && gTcp4Child)
    {
        EFI_STATUS Status;
        EFI_TCP4_CLOSE_TOKEN CloseToken;

        RtlZeroMemory(&CloseToken, sizeof(CloseToken));
        Status = GlobalSystemTable->BootServices->CreateEvent(
            EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
            UefiTcp4Notify, NULL, &CloseToken.CompletionToken.Event);
        if (!EFI_ERROR(Status))
        {
            CloseToken.CompletionToken.Status = EFI_NOT_READY;
            CloseToken.AbortOnClose = FALSE;

            Status = gTcp4->Close(gTcp4, &CloseToken);
            if (!EFI_ERROR(Status))
            {
                UefiTcp4WaitForToken(gTcp4, &CloseToken.CompletionToken);
            }
            GlobalSystemTable->BootServices->CloseEvent(CloseToken.CompletionToken.Event);
        }

        gTcp4->Configure(gTcp4, NULL);
        gTcp4Sb->DestroyChild(gTcp4Sb, gTcp4Child);
    }
    gTcp4 = NULL;
    gTcp4Child = NULL;
    gTcp4Sb = NULL;

    gRecvBufLen = 0;
    gRecvBufPos = 0;
}

/**
 * @brief Format and send an HTTP/1.1 GET request over TCP4.
 */
static BOOLEAN
UefiTcp4SendHttpGet(
    _In_ PCSTR Path,
    _In_ PCSTR Host)
{
    /*
     * Build: "GET /path HTTP/1.1\r\nHost: host\r\nConnection: close\r\n\r\n"
     * Max URL path ~512 + host ~256 + fixed overhead ~64 = ~832 bytes.
     */
    CHAR8 Req[1024];
    UINTN Pos = 0;
    PCSTR s;

    /* "GET " */
    Req[Pos++] = 'G'; Req[Pos++] = 'E'; Req[Pos++] = 'T'; Req[Pos++] = ' ';

    /* Path */
    for (s = Path; *s && Pos + 64 < sizeof(Req); )
        Req[Pos++] = *s++;

    /* " HTTP/1.1\r\n" */
    {
        static const CHAR8 Ver[] = " HTTP/1.1\r\n";
        UINTN i;
        for (i = 0; Ver[i]; i++) Req[Pos++] = Ver[i];
    }

    /* "Host: <host>\r\n" */
    {
        static const CHAR8 Hdr[] = "Host: ";
        UINTN i;
        for (i = 0; Hdr[i]; i++) Req[Pos++] = Hdr[i];
    }
    for (s = Host; *s && Pos + 32 < sizeof(Req); )
        Req[Pos++] = *s++;
    Req[Pos++] = '\r'; Req[Pos++] = '\n';

    /* "Connection: close\r\n" */
    {
        static const CHAR8 Conn[] = "Connection: close\r\n";
        UINTN i;
        for (i = 0; Conn[i]; i++) Req[Pos++] = Conn[i];
    }

    /* Final "\r\n" */
    Req[Pos++] = '\r'; Req[Pos++] = '\n';

    TRACE("UEFI HttpBoot: Sending HTTP GET %s (Host: %s)\n", Path, Host);

    return UefiTcp4Send(Req, Pos);
}

/**
 * @brief Receive and parse HTTP response headers from TCP4.
 *
 * Reads byte-by-byte until "\r\n\r\n" is found, then parses the
 * status line and Content-Length header.
 *
 * @param StatusCode     Receives the HTTP status code (e.g. 200).
 * @param ContentLength  Receives Content-Length value (0 if not present).
 * @return TRUE on success, FALSE on error.
 */
static BOOLEAN
UefiTcp4RecvHttpHeaders(
    _Out_ UINT32 *StatusCode,
    _Out_ UINTN *ContentLength)
{
    CHAR8 HdrBuf[4096];
    UINTN HdrLen = 0;
    UINT8 Byte;
    EFI_STATUS Status;
    UINTN i;

    *StatusCode = 0;
    *ContentLength = 0;

    /* Read until we see \r\n\r\n (end of headers) */
    while (HdrLen + 1 < sizeof(HdrBuf))
    {
        Status = UefiTcp4RecvByte(&Byte);
        if (EFI_ERROR(Status))
        {
            TRACE("UEFI HttpBoot: TCP4 header read error (Status %lx)\n", Status);
            return FALSE;
        }

        HdrBuf[HdrLen++] = (CHAR8)Byte;

        /* Check for \r\n\r\n */
        if (HdrLen >= 4 &&
            HdrBuf[HdrLen - 4] == '\r' && HdrBuf[HdrLen - 3] == '\n' &&
            HdrBuf[HdrLen - 2] == '\r' && HdrBuf[HdrLen - 1] == '\n')
        {
            break;
        }
    }
    HdrBuf[HdrLen] = '\0';

    /* Parse status line: "HTTP/1.x NNN ..." */
    {
        PCSTR p = (PCSTR)HdrBuf;
        /* Skip to first space (after HTTP/1.x) */
        while (*p && *p != ' ') p++;
        if (*p == ' ') p++;
        /* Parse 3-digit status code */
        if (p[0] >= '0' && p[0] <= '9' &&
            p[1] >= '0' && p[1] <= '9' &&
            p[2] >= '0' && p[2] <= '9')
        {
            *StatusCode = (UINT32)((p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0'));
        }
    }

    TRACE("UEFI HttpBoot: HTTP status = %u\n", (unsigned)*StatusCode);

    /* Parse Content-Length header (case-insensitive scan) */
    for (i = 0; i + 16 < HdrLen; i++)
    {
        CHAR8 c = HdrBuf[i];
        /* Look for "\nContent-Length:" (after a line break) */
        if ((c == '\n') &&
            (HdrBuf[i+1] == 'C' || HdrBuf[i+1] == 'c') &&
            (HdrBuf[i+2] == 'o' || HdrBuf[i+2] == 'O') &&
            (HdrBuf[i+3] == 'n' || HdrBuf[i+3] == 'N') &&
            (HdrBuf[i+4] == 't' || HdrBuf[i+4] == 'T') &&
            (HdrBuf[i+5] == 'e' || HdrBuf[i+5] == 'E') &&
            (HdrBuf[i+6] == 'n' || HdrBuf[i+6] == 'N') &&
            (HdrBuf[i+7] == 't' || HdrBuf[i+7] == 'T') &&
            HdrBuf[i+8] == '-' &&
            (HdrBuf[i+9] == 'L' || HdrBuf[i+9] == 'l') &&
            (HdrBuf[i+10] == 'e' || HdrBuf[i+10] == 'E') &&
            (HdrBuf[i+11] == 'n' || HdrBuf[i+11] == 'N') &&
            (HdrBuf[i+12] == 'g' || HdrBuf[i+12] == 'G') &&
            (HdrBuf[i+13] == 't' || HdrBuf[i+13] == 'T') &&
            (HdrBuf[i+14] == 'h' || HdrBuf[i+14] == 'H') &&
            HdrBuf[i+15] == ':')
        {
            PCSTR v = (PCSTR)&HdrBuf[i + 16];
            UINTN Val = 0;
            while (*v == ' ') v++;  /* skip optional whitespace */
            while (*v >= '0' && *v <= '9')
            {
                Val = Val * 10 + (*v - '0');
                v++;
            }
            *ContentLength = Val;
            TRACE("UEFI HttpBoot: Content-Length = %lu\n", (unsigned long)Val);
            break;
        }
    }

    return TRUE;
}

/**
 * @brief High-level: parse URL, connect via TCP4, send HTTP GET, receive headers.
 *
 * On success, the TCP4 connection remains open for body reading.
 *
 * @param Url            ASCII URL to request.
 * @param StatusCode     Receives the HTTP status code.
 * @param ContentLength  Receives Content-Length value.
 * @return TRUE on HTTP 200, FALSE on failure.
 */
static BOOLEAN
UefiTcp4HttpGet(
    _In_  PCSTR Url,
    _Out_ UINT32 *StatusCode,
    _Out_ UINTN *ContentLength)
{
    CHAR8 HostBuf[256];
    EFI_IPv4_ADDRESS RemoteIp;
    UINT16 Port;
    PCSTR Path;

    *StatusCode = 0;
    *ContentLength = 0;

    /* Extract components from URL */
    if (!UefiNetExtractHost(Url, HostBuf, sizeof(HostBuf)))
    {
        TRACE("UEFI HttpBoot: Failed to extract host from URL\n");
        return FALSE;
    }

    if (!UefiParseIpv4((PCSTR)HostBuf, &RemoteIp))
    {
        TRACE("UEFI HttpBoot: Failed to parse IP from host '%s'\n", HostBuf);
        return FALSE;
    }

    Port = UefiNetExtractPort(Url);
    Path = UefiNetExtractPath(Url);

    /* Connect TCP4 */
    if (!UefiTcp4Connect(&RemoteIp, Port))
        return FALSE;

    /* Send HTTP GET request */
    if (!UefiTcp4SendHttpGet(Path, (PCSTR)HostBuf))
    {
        UefiTcp4Close();
        return FALSE;
    }

    /* Receive and parse headers */
    if (!UefiTcp4RecvHttpHeaders(StatusCode, ContentLength))
    {
        UefiTcp4Close();
        return FALSE;
    }

    return (*StatusCode == 200);
}

/**
 * @brief Read the HTTP response body via TCP4.
 *
 * Reads in large chunks via UefiTcp4RecvExact.
 * If Md5Ctx is non-NULL, each chunk is fed into the MD5 hash
 * incrementally so no extra pass over the data is needed.
 *
 * @param Buffer        Destination buffer.
 * @param ContentLength Expected body size.
 * @param BytesRead     Receives actual bytes read.
 * @param Md5Ctx        Optional MD5 context to update per chunk.
 * @return TRUE on success, FALSE on failure.
 */
static BOOLEAN
UefiTcp4HttpReadBody(
    _Out_writes_bytes_(ContentLength) PVOID Buffer,
    _In_  UINTN ContentLength,
    _Out_ UINTN *BytesRead,
    _Inout_opt_ MD5_CTX *Md5Ctx)
{
    UINTN TotalRead = 0;
    EFI_TIME StartTime, Now;
    UINT32 LastLogSec = 0;  /* elapsed seconds at last log */
    UINTN  LastLogBytes = 0; /* TotalRead at last log */
    UINT32 StartSec;
    BOOLEAN HaveClock;

    *BytesRead = 0;

    /* Get start time for periodic status and throughput calculation */
    HaveClock = !EFI_ERROR(
        GlobalSystemTable->RuntimeServices->GetTime(&StartTime, NULL));
    StartSec = HaveClock
        ? (UINT32)StartTime.Hour * 3600 + (UINT32)StartTime.Minute * 60 + StartTime.Second
        : 0;

    TRACE("UEFI HttpBoot: Downloading %lu bytes (%lu MiB)...\n",
          (unsigned long)ContentLength,
          (unsigned long)(ContentLength / (1024 * 1024)));

    while (TotalRead < ContentLength)
    {
        UINTN ChunkSize = ContentLength - TotalRead;
        if (ChunkSize > TCP4_CHUNK_SIZE)
            ChunkSize = TCP4_CHUNK_SIZE;

        if (!UefiTcp4RecvExact((UINT8 *)Buffer + TotalRead, ChunkSize))
        {
            TRACE("UEFI HttpBoot: Download FAILED at %lu / %lu bytes (%lu MiB)\n",
                  (unsigned long)TotalRead, (unsigned long)ContentLength,
                  (unsigned long)(TotalRead / (1024 * 1024)));
            *BytesRead = TotalRead;
            return FALSE;
        }

        /* Update MD5 hash with this chunk */
        if (Md5Ctx)
            Md5Update(Md5Ctx, (UINT8 *)Buffer + TotalRead, ChunkSize);

        TotalRead += ChunkSize;

        /* Log status every 2 seconds with instantaneous + average speed */
        if (HaveClock &&
            !EFI_ERROR(GlobalSystemTable->RuntimeServices->GetTime(&Now, NULL)))
        {
            UINT32 NowSec = (UINT32)Now.Hour * 3600 +
                            (UINT32)Now.Minute * 60 + Now.Second;
            UINT32 Elapsed = (NowSec >= StartSec)
                ? (NowSec - StartSec)
                : (NowSec + 86400 - StartSec);  /* midnight wrap */

            if (Elapsed >= LastLogSec + 2)
            {
                UINT32 Interval = Elapsed - LastLogSec;
                UINTN IntervalBytes = TotalRead - LastLogBytes;
                UINTN CurMbPerSec = (Interval > 0)
                    ? (IntervalBytes / (1024 * 1024)) / Interval
                    : 0;
                UINTN AvgMbPerSec = (Elapsed > 0)
                    ? (TotalRead / (1024 * 1024)) / Elapsed
                    : 0;
                TRACE("UEFI HttpBoot: %lu%% (%lu/%lu MiB) %lu MiB/s (avg %lu)  [%lus]\n",
                      (unsigned long)((TotalRead * 100) / ContentLength),
                      (unsigned long)(TotalRead / (1024 * 1024)),
                      (unsigned long)(ContentLength / (1024 * 1024)),
                      (unsigned long)CurMbPerSec,
                      (unsigned long)AvgMbPerSec,
                      (unsigned long)Elapsed);
                LastLogSec = Elapsed;
                LastLogBytes = TotalRead;
            }
        }
    }

    /* Final summary */
    {
        UINT32 ElapsedFinal = 0;
        if (HaveClock &&
            !EFI_ERROR(GlobalSystemTable->RuntimeServices->GetTime(&Now, NULL)))
        {
            UINT32 NowSec = (UINT32)Now.Hour * 3600 +
                            (UINT32)Now.Minute * 60 + Now.Second;
            ElapsedFinal = (NowSec >= StartSec)
                ? (NowSec - StartSec)
                : (NowSec + 86400 - StartSec);
        }

        UINTN MbPerSec = (ElapsedFinal > 0)
            ? (TotalRead / (1024 * 1024)) / ElapsedFinal
            : 0;
        TRACE("UEFI HttpBoot: Download complete: %lu MiB in %lus (%lu MiB/s)\n",
              (unsigned long)(TotalRead / (1024 * 1024)),
              (unsigned long)ElapsedFinal,
              (unsigned long)MbPerSec);
    }

    *BytesRead = TotalRead;
    return TRUE;
}

/**
 * @brief Clean up TCP4 and DHCP4 children.
 */
static VOID
UefiTcp4HttpCleanup(VOID)
{
    UefiTcp4Close();

    if (gDhcp4Sb && gDhcp4Child)
    {
        if (gDhcp4)
        {
            gDhcp4->Stop(gDhcp4);
            gDhcp4->Configure(gDhcp4, NULL);
        }
        gDhcp4Sb->DestroyChild(gDhcp4Sb, gDhcp4Child);
    }
    gDhcp4 = NULL;
    gDhcp4Child = NULL;
    gDhcp4Sb = NULL;
}

/* ============================================================================
 * Phase 4: Full Boot Integration
 * ============================================================================ */

/**
 * @brief Download an ISO via HTTP and set up the ramdisk.
 *
 * Orchestrates the full HTTP boot sequence (using raw TCP4):
 *   1. SNP init -> DHCP4 -> TCP4 connect -> HTTP GET
 *   2. Allocate ramdisk pages via MmAllocateMemoryWithType
 *   3. Download body into ramdisk buffer
 *   4. Set gInitRamDiskBase / gInitRamDiskSize
 *   5. Cleanup TCP4/DHCP
 *
 * @param Url  ASCII URL of the ISO to download.
 * @return TRUE on success, FALSE on failure.
 */
BOOLEAN
UefiHttpBootDownload(
    _In_ PCSTR Url)
{
    UINT32 HttpStatus;
    UINTN ContentLength = 0;
    UINTN BytesRead = 0;
    PVOID RamDiskPtr;

    if (!Url || !*Url)
    {
        TRACE("UEFI HttpBoot: No URL provided\n");
        return FALSE;
    }

    TRACE("UEFI HttpBoot: Starting download from %s\n", Url);

    /* Step 0: Probe network hardware and load drivers if not done yet.
     * This detects PCI NICs, loads external DXE drivers (e.g. RtkUndiDxe),
     * connects controllers, and discovers SNP/TCP4 protocols. */
    if (!NetworkProbeComplete)
    {
        UefiProbeNetworkSupport();
    }

    /* Step 1: Initialize SNP + DHCP if not already done */
    if (!NetInitialized)
    {
        if (!SnpAvailable)
        {
            TRACE("UEFI HttpBoot: No SNP -- network unavailable\n");
            return FALSE;
        }

        if (!Tcp4SbAvailable)
        {
            TRACE("UEFI HttpBoot: No TCP4 Service Binding -- network unavailable\n");
            return FALSE;
        }

        if (!UefiNetSnpInitialize())
        {
            TRACE("UEFI HttpBoot: SNP initialization failed\n");
            return FALSE;
        }

        UefiNetIp4Config2SetStaticPolicy();

        if (!UefiNetDhcp4Configure(60))
        {
            TRACE("UEFI HttpBoot: DHCP4 configuration failed\n");
            return FALSE;
        }

        NetInitialized = TRUE;
    }

    /* Step 2+3: Connect via TCP4, send HTTP GET, receive headers */
    if (!UefiTcp4HttpGet(Url, &HttpStatus, &ContentLength))
    {
        TRACE("UEFI HttpBoot: HTTP GET failed (HttpStatus=%u)\n",
              (unsigned)HttpStatus);
        UefiTcp4HttpCleanup();
        return FALSE;
    }

    if (ContentLength == 0)
    {
        TRACE("UEFI HttpBoot: Content-Length is 0 or missing -- cannot proceed\n");
        UefiTcp4HttpCleanup();
        return FALSE;
    }

    TRACE("UEFI HttpBoot: ISO size = %lu bytes (%lu MiB)\n",
          (unsigned long)ContentLength,
          (unsigned long)(ContentLength / (1024 * 1024)));

    /*
     * Step 4: Allocate ramdisk buffer via FreeLDR's memory manager.
     *
     * We MUST use FreeLDR's allocator (not UEFI AllocatePages) because:
     *
     * 1. MmInitializeMemoryManager() has already run and built the page
     *    lookup table.  A raw UEFI AllocatePages() would not update that
     *    table, so FreeLDR's allocator could later hand out overlapping
     *    pages for the kernel, drivers, or registry hives — corrupting
     *    the ramdisk.
     *
     * 2. UefiMemGetMemoryMap() pins all EfiConventionalMemory as
     *    EfiLoaderData.  After pinning, no EfiConventionalMemory remains
     *    in UEFI's map, so AllocatePages(AllocateAnyPages) would fail
     *    with EFI_OUT_OF_RESOURCES.
     *
     * 3. Using LoaderXIPRom type ensures the kernel's memory manager
     *    marks these pages as non-reclaimable ROM, which is required
     *    for the ramdisk backing store to survive into the kernel.
     *    A raw UEFI allocation would be EfiLoaderData → LoaderLoadedProgram,
     *    which the kernel treats as reclaimable.
     */
    RamDiskPtr = MmAllocateMemoryWithType((SIZE_T)ContentLength, LoaderXIPRom);
    if (!RamDiskPtr)
    {
        TRACE("UEFI HttpBoot: MmAllocateMemoryWithType failed for %lu bytes\n",
              (unsigned long)ContentLength);
        UefiTcp4HttpCleanup();
        return FALSE;
    }

    TRACE("UEFI HttpBoot: Ramdisk allocated at %p (%lu bytes, LoaderXIPRom)\n",
          RamDiskPtr, (unsigned long)ContentLength);

    /* Step 5: Download body into ramdisk with incremental MD5 */
    {
        MD5_CTX Md5;
        UINT8 Digest[16];
        UINTN i;

        Md5Init(&Md5);

        if (!UefiTcp4HttpReadBody(RamDiskPtr, ContentLength, &BytesRead, &Md5))
        {
            TRACE("UEFI HttpBoot: Body download failed (got %lu / %lu bytes)\n",
                  (unsigned long)BytesRead, (unsigned long)ContentLength);
            /* Cannot free LoaderXIPRom pages (no MmFreeMemory in FreeLDR),
             * but this is an error path — we'll show an error and halt. */
            UefiTcp4HttpCleanup();
            return FALSE;
        }

        Md5Final(&Md5, Digest);

        TRACE("UEFI HttpBoot: MD5: ");
        for (i = 0; i < 16; i++)
            TRACE("%02x", Digest[i]);
        TRACE("\n");
    }

    /* Step 6: Set ramdisk globals.
     * RamDiskInitialize() will see gInitRamDiskBase != NULL and use it
     * directly (ramdisk.c:2287-2318), skipping the file-read path.
     * The pages are already marked LoaderXIPRom in the lookup table,
     * so the kernel will protect them as ROM in the PFN database. */
    gInitRamDiskBase = RamDiskPtr;
    gInitRamDiskSize = (ULONG)ContentLength;

    TRACE("UEFI HttpBoot: Download complete! RamDisk at %p, size %lu bytes\n",
          gInitRamDiskBase, (unsigned long)gInitRamDiskSize);

    /* Step 7: Cleanup network state */
    UefiTcp4HttpCleanup();

    return TRUE;
}

/* PUBLIC API *****************************************************************/

/**
 * @brief Probe firmware for UEFI network protocol support.
 *
 * 1. Attempts to load external network DXE drivers from the boot volume
 *    (e.g. RtkUndiDxe.efi for LattePanda Mu PCIe Ethernet).
 * 2. Connects all controllers so drivers bind to hardware.
 * 3. Enumerates SNP and TCP4 Service Binding handles.
 *
 * Must be called while Boot Services are still active.
 */
VOID
UefiProbeNetworkSupport(VOID)
{
    EFI_STATUS Status;
    UINTN Count;
    EFI_HANDLE *Handles = NULL;

    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
    {
        WARN("UefiProbeNetworkSupport: Boot Services unavailable\n");
        return;
    }

    /*
     * Phase 0: Early PCI enumeration and NIC initialization.
     * Run one non-recursive ConnectController round to create PCI IO handles,
     * then enable Bus Master and program MAC into the NIC's IDR
     * registers.  This MUST happen BEFORE loading UNDI drivers
     * because RtkUndiDxe caches the MAC from its first hardware
     * probe and never re-reads IDR on Shutdown/Initialize.
     */
    UefiConnectAllControllersOnce(FALSE);
    UefiEnablePciNic();

    /*
     * Phase 0.5: Check if firmware already provides NII/UNDI, meaning
     * it has a built-in NIC driver (e.g. OVMF e1000e).  If so, the
     * firmware likely has the full network stack and we skip loading
     * external DXE drivers to avoid conflicts (e.g. debug-build TcpDxe
     * asserting on missing EFI_RNG_PROTOCOL).
     */
    {
        UINTN NiiCount = 0;
        EFI_HANDLE *NiiHandles = NULL;
        BOOLEAN FirmwareHasNii = FALSE;

        Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
            ByProtocol, &gEfiNii31Guid, NULL, &NiiCount, &NiiHandles);
        if (!EFI_ERROR(Status) && NiiCount > 0)
            FirmwareHasNii = TRUE;
        if (NiiHandles)
            GlobalSystemTable->BootServices->FreePool(NiiHandles);

        if (!FirmwareHasNii)
        {
            NiiCount = 0;
            NiiHandles = NULL;
            Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
                ByProtocol, &gEfiNiiGuid, NULL, &NiiCount, &NiiHandles);
            if (!EFI_ERROR(Status) && NiiCount > 0)
                FirmwareHasNii = TRUE;
            if (NiiHandles)
                GlobalSystemTable->BootServices->FreePool(NiiHandles);
        }

        if (FirmwareHasNii)
        {
            TRACE("UEFI Network: Firmware NII/UNDI already present, skipping external drivers\n");
        }
        else
        {
            /*
             * Phase 1: Try loading external network DXE drivers from the boot
             * partition.  This is needed for boards like LattePanda Mu where the
             * PCIe Ethernet driver isn't built into firmware.
             */
            UefiTryLoadNetworkDrivers();
        }
    }

    /*
     * Phase 2: Force-connect all controllers.  This causes freshly-loaded
     * drivers (and any firmware-resident ones) to bind to their hardware.
     * Equivalent to the UEFI Shell's "connect -r" command.
     */
    UefiConnectAllControllers();

    /*
     * Phase 3: Probe for network protocols.
     */

    /* --- EFI_NETWORK_INTERFACE_IDENTIFIER (NII/UNDI) --- */
    Count = 0;
    Handles = NULL;
    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &gEfiNii31Guid, NULL, &Count, &Handles);
    if (!EFI_ERROR(Status) && Count > 0)
    {
        TRACE("UEFI Network: NII 3.1 (UNDI) available (%lu handle(s))\n",
              (unsigned long)Count);
    }
    else
    {
        /* Try older NII GUID */
        Count = 0;
        Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
            ByProtocol, &gEfiNiiGuid, NULL, &Count, &Handles);
        if (!EFI_ERROR(Status) && Count > 0)
        {
            TRACE("UEFI Network: NII (UNDI) available (%lu handle(s))\n",
                  (unsigned long)Count);
        }
        else
        {
            TRACE("UEFI Network: NII/UNDI not available (Status %lx)\n", Status);
        }
    }
    if (Handles)
    {
        GlobalSystemTable->BootServices->FreePool(Handles);
        Handles = NULL;
    }

    /* --- EFI_SIMPLE_NETWORK_PROTOCOL --- */
    Count = 0;
    Handles = NULL;
    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &gEfiSnpGuid, NULL, &Count, &Handles);
    if (!EFI_ERROR(Status) && Count > 0)
    {
        SnpAvailable = TRUE;
        SnpHandleCount = (ULONG)Count;
        UefiLogSnpDetails(Handles, Count);
        TRACE("UEFI Network: SNP available (%lu handle(s))\n",
              (unsigned long)Count);
    }
    else
    {
        TRACE("UEFI Network: SNP not available (Status %lx)\n", Status);
    }
    if (Handles)
    {
        GlobalSystemTable->BootServices->FreePool(Handles);
        Handles = NULL;
    }

    /* --- EFI_TCP4_SERVICE_BINDING_PROTOCOL --- */
    Count = 0;
    Handles = NULL;
    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol, &gEfiTcp4SbGuid, NULL, &Count, &Handles);
    if (!EFI_ERROR(Status) && Count > 0)
    {
        Tcp4SbAvailable = TRUE;
        /* Save the first TCP4 SB handle as our network controller.
         * All protocols (DHCP4 SB, IP4Config2, TCP4 SB) are installed
         * on the same MNP child handle in the UEFI network stack. */
        gNetControllerHandle = Handles[0];
        TRACE("UEFI Network: TCP4 Service Binding available (%lu handle(s))\n",
              (unsigned long)Count);
    }
    else
    {
        TRACE("UEFI Network: TCP4 Service Binding not available (Status %lx)\n",
              Status);
    }
    if (Handles)
    {
        GlobalSystemTable->BootServices->FreePool(Handles);
        Handles = NULL;
    }

    NetworkProbeComplete = TRUE;

    /* Summary */
    TRACE("UEFI Network probe complete: SNP=%s  TCP4_SB=%s\n",
          SnpAvailable    ? "YES" : "no",
          Tcp4SbAvailable ? "YES" : "no");
}

/**
 * @brief Initialize HTTP boot networking (using TCP4 for HTTP transport).
 *
 * Performs SNP initialization and DHCP4 to obtain an IP address.
 * Called internally by UefiHttpBootDownload() when the user selects
 * a boot entry with HttpBootUrl.
 */
VOID
UefiHttpBootInit(VOID)
{
    if (!SnpAvailable)
    {
        TRACE("UEFI HttpBoot: No SNP -- skipping network init\n");
        return;
    }

    if (!Tcp4SbAvailable)
    {
        TRACE("UEFI HttpBoot: No TCP4 Service Binding -- skipping network init\n");
        return;
    }

    TRACE("UEFI HttpBoot: Initializing network...\n");

    /* Phase 1: SNP initialization */
    if (!UefiNetSnpInitialize())
    {
        TRACE("UEFI HttpBoot: SNP initialization failed -- HTTP boot unavailable\n");
        return;
    }

    /* Phase 1.5: Set IP4Config2 policy to Static before DHCP.
     * Per EDK2 HttpBootSetIp4Policy(), this must happen before
     * DHCP4->Start() so the IP4 stack accepts manual configuration. */
    UefiNetIp4Config2SetStaticPolicy();

    /* Phase 2: DHCP4 IP configuration */
    if (!UefiNetDhcp4Configure(60))
    {
        TRACE("UEFI HttpBoot: DHCP4 failed -- HTTP boot unavailable\n");
        return;
    }

    NetInitialized = TRUE;
    TRACE("UEFI HttpBoot: Network initialized successfully\n");
}

/* Query helpers */

BOOLEAN
UefiIsNetInitialized(VOID)
{
    return NetInitialized;
}

BOOLEAN
UefiHasSimpleNetworkProtocol(VOID)
{
    return SnpAvailable;
}

BOOLEAN
UefiHasTcp4ServiceBindingProtocol(VOID)
{
    return Tcp4SbAvailable;
}

ULONG
UefiGetSnpHandleCount(VOID)
{
    return SnpHandleCount;
}
