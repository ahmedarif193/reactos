/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Hardware specific definitions
 * COPYRIGHT:   2018 Mark Jansen (mark.jansen@reactos.org)
 *              2019 Victor Perevertkin (victor.perevertkin@reactos.org)
 *              2024 ReactOS Team - Modernization for 82574L PCIe support
 */

#pragma once

#define IEEE_802_ADDR_LENGTH 6

#define HW_VENDOR_INTEL     0x8086

#define MAX_RESET_ATTEMPTS  10

#define MAX_PHY_REG_ADDRESS         0x1F
#define MAX_PHY_READ_ATTEMPTS       1800

#define MAX_EEPROM_READ_ATTEMPTS    10000


#define MAXIMUM_MULTICAST_ADDRESSES 16


/* Ethernet frame header */
typedef struct _ETH_HEADER {
    UCHAR Destination[IEEE_802_ADDR_LENGTH];
    UCHAR Source[IEEE_802_ADDR_LENGTH];
    USHORT PayloadType;
} ETH_HEADER, *PETH_HEADER;


C_ASSERT(sizeof(ETH_HEADER) == 14);


typedef enum _E1000_RCVBUF_SIZE
{
    E1000_RCVBUF_2048 = 0,
    E1000_RCVBUF_1024 = 1,
    E1000_RCVBUF_512 = 2,
    E1000_RCVBUF_256 = 3,

    E1000_RCVBUF_INDEXMASK = 3,
    E1000_RCVBUF_RESERVED = 4 | 0,

    E1000_RCVBUF_16384 = 4 | 1,
    E1000_RCVBUF_8192 =  4 | 2,
    E1000_RCVBUF_4096 =  4 | 3,
} E1000_RCVBUF_SIZE;



#include <pshpack1.h>


/* 3.2.3 Receive Descriptor Format */

#define E1000_RDESC_STATUS_PIF          (1 << 7)    /* Passed in-exact filter */
#define E1000_RDESC_STATUS_IPCS         (1 << 6)    /* IP Checksum Calculated on Packet */
#define E1000_RDESC_STATUS_TCPCS        (1 << 5)    /* TCP/UDP Checksum Calculated on Packet */
#define E1000_RDESC_STATUS_VP           (1 << 3)    /* VLAN Packet */
#define E1000_RDESC_STATUS_IXSM         (1 << 2)    /* Ignore Checksum Indication */
#define E1000_RDESC_STATUS_EOP          (1 << 1)    /* End of Packet */
#define E1000_RDESC_STATUS_DD           (1 << 0)    /* Descriptor Done */

/* Receive Descriptor Errors */
#define E1000_RDESC_ERR_RXE             (1 << 7)    /* RX Data Error */
#define E1000_RDESC_ERR_IPE             (1 << 6)    /* IP Checksum Error */
#define E1000_RDESC_ERR_TCPE            (1 << 5)    /* TCP/UDP Checksum Error */
#define E1000_RDESC_ERR_CXE             (1 << 4)    /* Carrier Extension Error (reserved 82574) */
#define E1000_RDESC_ERR_SEQ             (1 << 2)    /* Sequence Error */
#define E1000_RDESC_ERR_SE              (1 << 1)    /* Symbol Error */
#define E1000_RDESC_ERR_CE              (1 << 0)    /* CRC Error or Alignment Error */

typedef struct _E1000_RECEIVE_DESCRIPTOR
{
    UINT64 Address;

    USHORT Length;
    USHORT Checksum;
    UCHAR Status;
    UCHAR Errors;
    USHORT Special;

} E1000_RECEIVE_DESCRIPTOR, *PE1000_RECEIVE_DESCRIPTOR;


/* 3.3.3 Legacy Transmit Descriptor Format */

#define E1000_TDESC_CMD_IDE             (1 << 7)    /* Interrupt Delay Enable */
#define E1000_TDESC_CMD_VLE             (1 << 6)    /* VLAN Packet Enable */
#define E1000_TDESC_CMD_DEXT            (1 << 5)    /* Descriptor Extension (use context descriptor) */
#define E1000_TDESC_CMD_RS              (1 << 3)    /* Report Status */
#define E1000_TDESC_CMD_IC              (1 << 2)    /* Insert Checksum */
#define E1000_TDESC_CMD_IFCS            (1 << 1)    /* Insert FCS */
#define E1000_TDESC_CMD_EOP             (1 << 0)    /* End Of Packet */

#define E1000_TDESC_STATUS_DD           (1 << 0)    /* Descriptor Done */

/* Alias macros for NDIS 6.x driver compatibility */
#define E1000_TXDESC_EOP        E1000_TDESC_CMD_EOP
#define E1000_TXDESC_IFCS       E1000_TDESC_CMD_IFCS
#define E1000_TXDESC_RS         E1000_TDESC_CMD_RS
#define E1000_TXDESC_IC         E1000_TDESC_CMD_IC
#define E1000_TXDESC_DEXT       E1000_TDESC_CMD_DEXT
#define E1000_TXDESC_VLE        E1000_TDESC_CMD_VLE
#define E1000_TXDESC_IDE        E1000_TDESC_CMD_IDE
#define E1000_TXDESC_DD         E1000_TDESC_STATUS_DD
/* E1000_TXD_CMD_IDE is defined later for context descriptors with value 0x80000000 */

/* Alias macros for Receive Descriptor */
#define E1000_RXDESC_STAT_DD    E1000_RDESC_STATUS_DD
#define E1000_RXDESC_STAT_EOP   E1000_RDESC_STATUS_EOP
#define E1000_RXDESC_STAT_IPCS  E1000_RDESC_STATUS_IPCS
#define E1000_RXDESC_STAT_TCPCS E1000_RDESC_STATUS_TCPCS
#define E1000_RXDESC_STAT_VP    E1000_RDESC_STATUS_VP
#define E1000_RXDESC_STAT_PIF   E1000_RDESC_STATUS_PIF
#define E1000_RXDESC_ERR_IPE    E1000_RDESC_ERR_IPE
#define E1000_RXDESC_ERR_TCPE   E1000_RDESC_ERR_TCPE
#define E1000_RXDESC_ERR_CE     E1000_RDESC_ERR_CE
#define E1000_RXDESC_ERR_SE     E1000_RDESC_ERR_SE
#define E1000_RXDESC_ERR_SEQ    E1000_RDESC_ERR_SEQ
#define E1000_RXDESC_ERR_CXE    E1000_RDESC_ERR_CXE
#define E1000_RXDESC_ERR_RXE    E1000_RDESC_ERR_RXE

typedef struct _E1000_TRANSMIT_DESCRIPTOR
{
    UINT64 Address;

    USHORT Length;
    UCHAR ChecksumOffset;
    UCHAR Command;
    UCHAR Status;
    UCHAR ChecksumStartField;
    USHORT Special;

} E1000_TRANSMIT_DESCRIPTOR, *PE1000_TRANSMIT_DESCRIPTOR;

/* Context Transmit Descriptor for checksum offload */
typedef struct _E1000_CONTEXT_DESCRIPTOR
{
    UCHAR IPCSS;            /* IP Checksum Start */
    UCHAR IPCSO;            /* IP Checksum Offset */
    USHORT IPCSE;           /* IP Checksum Ending */
    UCHAR TUCSS;            /* TCP/UDP Checksum Start */
    UCHAR TUCSO;            /* TCP/UDP Checksum Offset */
    USHORT TUCSE;           /* TCP/UDP Checksum Ending */
    ULONG CmdTypeLen;       /* Command, Type, Length fields */
    UCHAR Status;
    UCHAR HdrLen;           /* Header Length */
    USHORT MSS;             /* Maximum Segment Size */
} E1000_CONTEXT_DESCRIPTOR, *PE1000_CONTEXT_DESCRIPTOR;

/* Context descriptor command bits */
#define E1000_TXD_CMD_DEXT      0x20000000  /* Descriptor Extension */
#define E1000_TXD_CMD_IDE       0x80000000  /* Interrupt Delay Enable */
#define E1000_TXD_DTYP_C        0x00000000  /* Context Descriptor */
#define E1000_TXD_DTYP_D        0x00100000  /* Data Descriptor */
#define E1000_TXD_CMD_TCP       0x01000000  /* TCP packet */
#define E1000_TXD_CMD_IP        0x02000000  /* IP packet */
#define E1000_TXD_CMD_TSE       0x04000000  /* TCP Seg enable */

#include <poppack.h>


C_ASSERT(sizeof(E1000_RECEIVE_DESCRIPTOR) == 16);
C_ASSERT(sizeof(E1000_TRANSMIT_DESCRIPTOR) == 16);


/* Descriptor ring sizes - increased from 128 for better performance
   Valid Range: 80-256 for 82542 and 82543 gigabit ethernet controllers
   Valid Range: 80-4096 for 82544 and newer
   Must be a multiple of 8 for proper alignment */
#define NUM_TRANSMIT_DESCRIPTORS        256
#define NUM_RECEIVE_DESCRIPTORS         256



/* ============================================================================
 * Register Definitions
 * ============================================================================ */

/* Device Control and Status Registers */
#define E1000_REG_CTRL              0x0000      /* Device Control Register, R/W */
#define E1000_REG_STATUS            0x0008      /* Device Status Register, R */
#define E1000_REG_EECD              0x0010      /* EEPROM/Flash Control/Data Register */
#define E1000_REG_EERD              0x0014      /* EEPROM Read Register, R/W */
#define E1000_REG_FLA               0x001C      /* Flash Access Register */
#define E1000_REG_CTRL_EXT          0x0018      /* Extended Device Control Register */
#define E1000_REG_MDIC              0x0020      /* MDI Control Register, R/W */
#define E1000_REG_FCAL              0x0028      /* Flow Control Address Low */
#define E1000_REG_FCAH              0x002C      /* Flow Control Address High */
#define E1000_REG_FCT               0x0030      /* Flow Control Type */
#define E1000_REG_VET               0x0038      /* VLAN Ether Type, R/W */

/* Interrupt Registers */
#define E1000_REG_ICR               0x00C0      /* Interrupt Cause Read, R/clr */
#define E1000_REG_ITR               0x00C4      /* Interrupt Throttling Register, R/W */
#define E1000_REG_ICS               0x00C8      /* Interrupt Cause Set, W */
#define E1000_REG_IMS               0x00D0      /* Interrupt Mask Set/Read Register, R/W */
#define E1000_REG_IMC               0x00D8      /* Interrupt Mask Clear, W */
#define E1000_REG_IAM               0x00E0      /* Interrupt Acknowledge Auto Mask */

/* Receive Control Registers */
#define E1000_REG_RCTL              0x0100      /* Receive Control Register, R/W */
#define E1000_REG_FCTTV             0x0170      /* Flow Control Transmit Timer Value */
#define E1000_REG_RXCSUM            0x5000      /* Receive Checksum Control, R/W */

/* Transmit Control Registers */
#define E1000_REG_TCTL              0x0400      /* Transmit Control Register, R/W */
#define E1000_REG_TCTL_EXT          0x0404      /* Extended Transmit Control Register */
#define E1000_REG_TIPG              0x0410      /* Transmit IPG Register, R/W */
#define E1000_REG_TXCW              0x0178      /* Transmit Configuration Word */
#define E1000_REG_TXDMAC            0x3000      /* TX DMA Control */
/* Note: 82574L TX checksum offload uses context descriptors, not a dedicated register */

/* Receive Descriptor Registers */
#define E1000_REG_RDBAL             0x2800      /* Receive Descriptor Base Address Low, R/W */
#define E1000_REG_RDBAH             0x2804      /* Receive Descriptor Base Address High, R/W */
#define E1000_REG_RDLEN             0x2808      /* Receive Descriptor Length, R/W */
#define E1000_REG_RDH               0x2810      /* Receive Descriptor Head, R/W */
#define E1000_REG_RDT               0x2818      /* Receive Descriptor Tail, R/W */
#define E1000_REG_RDTR              0x2820      /* Receive Delay Timer, R/W */
#define E1000_REG_RADV              0x282C      /* Receive Absolute Delay Timer, R/W */
#define E1000_REG_RSRPD             0x2C00      /* Receive Small Packet Detect */

/* Transmit Descriptor Registers */
#define E1000_REG_TDBAL             0x3800      /* Transmit Descriptor Base Address Low, R/W */
#define E1000_REG_TDBAH             0x3804      /* Transmit Descriptor Base Address High, R/W */
#define E1000_REG_TDLEN             0x3808      /* Transmit Descriptor Length, R/W */
#define E1000_REG_TDH               0x3810      /* Transmit Descriptor Head, R/W */
#define E1000_REG_TDT               0x3818      /* Transmit Descriptor Tail, R/W */
#define E1000_REG_TIDV              0x3820      /* Transmit Interrupt Delay Value, R/W */
#define E1000_REG_TADV              0x382C      /* Transmit Absolute Delay Timer, R/W */
#define E1000_REG_TXDCTL            0x3828      /* Transmit Descriptor Control */
#define E1000_REG_TARC0             0x3840      /* Transmit Arbitration Counter 0 */
#define E1000_REG_TARC1             0x3940      /* Transmit Arbitration Counter 1 */

/* Receive Address Registers */
#define E1000_REG_RAL               0x5400      /* Receive Address Low, R/W */
#define E1000_REG_RAH               0x5404      /* Receive Address High, R/W */

/* Multicast Table Array */
#define E1000_REG_MTA               0x5200      /* Multicast Table Array (128 entries) */

/* Statistics Registers */
#define E1000_REG_CRCERRS           0x4000      /* CRC Error Count */
#define E1000_REG_ALGNERRC          0x4004      /* Alignment Error Count */
#define E1000_REG_SYMERRS           0x4008      /* Symbol Error Count */
#define E1000_REG_RXERRC            0x400C      /* RX Error Count */
#define E1000_REG_MPC               0x4010      /* Missed Packets Count */
#define E1000_REG_SCC               0x4014      /* Single Collision Count */
#define E1000_REG_ECOL              0x4018      /* Excessive Collisions Count */
#define E1000_REG_MCC               0x401C      /* Multiple Collision Count */
#define E1000_REG_LATECOL           0x4020      /* Late Collisions Count */
#define E1000_REG_COLC              0x4028      /* Collision Count */
#define E1000_REG_DC                0x4030      /* Defer Count */
#define E1000_REG_TNCRS             0x4034      /* Transmit with No CRS */
#define E1000_REG_SEC               0x4038      /* Sequence Error Count */
#define E1000_REG_CEXTERR           0x403C      /* Carrier Extension Error Count */
#define E1000_REG_RLEC              0x4040      /* Receive Length Error Count */
#define E1000_REG_XONRXC            0x4048      /* XON Received Count */
#define E1000_REG_XONTXC            0x404C      /* XON Transmitted Count */
#define E1000_REG_XOFFRXC           0x4050      /* XOFF Received Count */
#define E1000_REG_XOFFTXC           0x4054      /* XOFF Transmitted Count */
#define E1000_REG_FCRUC             0x4058      /* FC Received Unsupported Count */
#define E1000_REG_PRC64             0x405C      /* Packets Received (64 bytes) Count */
#define E1000_REG_PRC127            0x4060      /* Packets Received (65-127 bytes) Count */
#define E1000_REG_PRC255            0x4064      /* Packets Received (128-255 bytes) Count */
#define E1000_REG_PRC511            0x4068      /* Packets Received (256-511 bytes) Count */
#define E1000_REG_PRC1023           0x406C      /* Packets Received (512-1023 bytes) Count */
#define E1000_REG_PRC1522           0x4070      /* Packets Received (1024-1522 bytes) Count */
#define E1000_REG_GPRC              0x4074      /* Good Packets Received Count */
#define E1000_REG_BPRC              0x4078      /* Broadcast Packets Received Count */
#define E1000_REG_MPRC              0x407C      /* Multicast Packets Received Count */
#define E1000_REG_GPTC              0x4080      /* Good Packets Transmitted Count */
#define E1000_REG_GORCL             0x4088      /* Good Octets Received Count Low */
#define E1000_REG_GORCH             0x408C      /* Good Octets Received Count High */
#define E1000_REG_GOTCL             0x4090      /* Good Octets Transmitted Count Low */
#define E1000_REG_GOTCH             0x4094      /* Good Octets Transmitted Count High */
#define E1000_REG_RNBC              0x40A0      /* Receive No Buffers Count */
#define E1000_REG_RUC               0x40A4      /* Receive Undersize Count */
#define E1000_REG_RFC               0x40A8      /* Receive Fragment Count */
#define E1000_REG_ROC               0x40AC      /* Receive Oversize Count */
#define E1000_REG_RJC               0x40B0      /* Receive Jabber Count */
#define E1000_REG_MGTPRC            0x40B4      /* Management Packets Received Count */
#define E1000_REG_MGTPDC            0x40B8      /* Management Packets Dropped Count */
#define E1000_REG_MGTPTC            0x40BC      /* Management Packets Transmitted Count */
#define E1000_REG_TORL              0x40C0      /* Total Octets Received Low */
#define E1000_REG_TORH              0x40C4      /* Total Octets Received High */
#define E1000_REG_TOTL              0x40C8      /* Total Octets Transmitted Low */
#define E1000_REG_TOTH              0x40CC      /* Total Octets Transmitted High */
#define E1000_REG_TPR               0x40D0      /* Total Packets Received */
#define E1000_REG_TPT               0x40D4      /* Total Packets Transmitted */
#define E1000_REG_PTC64             0x40D8      /* Packets Transmitted (64 bytes) Count */
#define E1000_REG_PTC127            0x40DC      /* Packets Transmitted (65-127 bytes) Count */
#define E1000_REG_PTC255            0x40E0      /* Packets Transmitted (128-255 bytes) Count */
#define E1000_REG_PTC511            0x40E4      /* Packets Transmitted (256-511 bytes) Count */
#define E1000_REG_PTC1023           0x40E8      /* Packets Transmitted (512-1023 bytes) Count */
#define E1000_REG_PTC1522           0x40EC      /* Packets Transmitted (1024-1522 bytes) Count */
#define E1000_REG_MPTC              0x40F0      /* Multicast Packets Transmitted Count */
#define E1000_REG_BPTC              0x40F4      /* Broadcast Packets Transmitted Count */
#define E1000_REG_TSCTC             0x40F8      /* TCP Segmentation Context Transmitted Count */
#define E1000_REG_TSCTFC            0x40FC      /* TCP Segmentation Context Transmit Fail Count */
#define E1000_REG_IAC               0x4100      /* Interrupt Assertion Count */

/*
 * 82574L MSI-X Interrupt Vector Allocation Register
 *
 * Per Linux e1000e driver, the 82574L uses a SINGLE IVAR register at 0xE4
 * (NOT multiple registers at 0x1700+ like some other Intel NICs).
 *
 * IVAR layout for 82574L:
 *   Bits 0-2:   Rx Queue 0 Vector number
 *   Bit  3:     Rx Queue 0 Valid (E1000_IVAR_INT_ALLOC_VALID)
 *   Bits 8-10:  Tx Queue 0 Vector number
 *   Bit  11:    Tx Queue 0 Valid
 *   Bits 16-18: Other Causes Vector number
 *   Bit  19:    Other Causes Valid
 *   Bit  31:    Tx interrupt on every write-back
 */
#define E1000_REG_IVAR              0x000E4     /* Interrupt Vector Allocation Register - RW */

/* Extended Interrupt Auto Clear (82574L) - clears ICR queue bits */
#define E1000_REG_EIAC              0x000DC     /* Extended Interrupt Auto Clear - RW */
#define E1000_EIAC_MASK_82574       0x01F00000  /* Mask for queue interrupt bits */

/*
 * Extended Interrupt Throttle Registers (82574L)
 * One per MSI-X vector, starting at 0xE8
 */
#define E1000_REG_EITR_82574(n)     (0x000E8 + (0x4 * (n)))

/* Legacy aliases for compatibility (use E1000_REG_EITR_82574 for new code) */
#define E1000_REG_EITR0             E1000_REG_EITR_82574(0)
#define E1000_REG_EITR1             E1000_REG_EITR_82574(1)
#define E1000_REG_EITR2             E1000_REG_EITR_82574(2)
#define E1000_REG_EITR3             E1000_REG_EITR_82574(3)
#define E1000_REG_EITR4             E1000_REG_EITR_82574(4)

/*
 * Note: EIMS/EIMC do NOT exist as separate registers on 82574L.
 * The extended interrupt bits (RXQ0, RXQ1, TXQ0, TXQ1, OTHER) are part
 * of the standard IMS/IMC registers at 0xD0/0xD8.
 * Keep these aliases for source compatibility but they point to IMS/IMC.
 */
#define E1000_REG_EIMS              E1000_REG_IMS  /* Same as IMS on 82574L */
#define E1000_REG_EIMC              E1000_REG_IMC  /* Same as IMC on 82574L */

/* Power Management Registers */
#define E1000_REG_WUC               0x5800      /* Wake Up Control */
#define E1000_REG_WUFC              0x5808      /* Wake Up Filter Control */
#define E1000_REG_WUS               0x5810      /* Wake Up Status */
#define E1000_REG_MANC              0x5820      /* Management Control */
#define E1000_REG_IPAV              0x5838      /* IP Address Valid */
#define E1000_REG_WUPL              0x5900      /* Wake Up Packet Length */
#define E1000_REG_WUPM              0x5A00      /* Wake Up Packet Memory (128 bytes) */

/* PCI-E Extended Configuration Space */
#define E1000_REG_GCR               0x5B00      /* PCI-E Control */
#define E1000_REG_GSCL_1            0x5B10      /* PCI-E Statistic Control 1 */
#define E1000_REG_GSCL_2            0x5B14      /* PCI-E Statistic Control 2 */
#define E1000_REG_GSCL_3            0x5B18      /* PCI-E Statistic Control 3 */
#define E1000_REG_GSCL_4            0x5B1C      /* PCI-E Statistic Control 4 */
#define E1000_REG_FACTPS            0x5B30      /* Function Active and Power State to MNG */
#define E1000_REG_SWSM              0x5B50      /* SW Semaphore */
#define E1000_REG_FWSM              0x5B54      /* FW Semaphore */

/* Device Serial Number (read from EEPROM) */
#define E1000_EEPROM_DSN_LOW        0x000B      /* Device Serial Number words 0-1 */
#define E1000_EEPROM_DSN_MID        0x000C      /* Device Serial Number words 2-3 */
#define E1000_EEPROM_DSN_HIGH       0x000D      /* Device Serial Number words 4-5 (if present) */

/* PCIe Capability Space offsets (from capability header) */
#define PCIE_CAP_PM                 0xC8        /* Power Management capability offset */
#define PCIE_CAP_MSI                0xD0        /* MSI capability offset */
#define PCIE_CAP_MSIX               0xA0        /* MSI-X capability offset */
#define PCIE_CAP_EXPRESS            0xE0        /* PCI Express capability offset */
#define PCIE_CAP_AER                0x100       /* Advanced Error Reporting capability offset */
#define PCIE_CAP_DSN                0x140       /* Device Serial Number capability offset */


/* ============================================================================
 * Register Bit Definitions
 * ============================================================================ */

/* E1000_REG_CTRL */
#define E1000_CTRL_FD               (1 << 0)    /* Full Duplex */
#define E1000_CTRL_GIO_MASTER_DIS   (1 << 2)    /* GIO Master Disable */
#define E1000_CTRL_LRST             (1 << 3)    /* Link Reset */
#define E1000_CTRL_ASDE             (1 << 5)    /* Auto-Speed Detection Enable */
#define E1000_CTRL_SLU              (1 << 6)    /* Set Link Up */
#define E1000_CTRL_ILOS             (1 << 7)    /* Invert Loss of Signal (LOS) */
#define E1000_CTRL_SPEED_SHIFT      8
#define E1000_CTRL_SPEED_MASK       (3 << E1000_CTRL_SPEED_SHIFT)
#define E1000_CTRL_SPEED_10         (0 << E1000_CTRL_SPEED_SHIFT)
#define E1000_CTRL_SPEED_100        (1 << E1000_CTRL_SPEED_SHIFT)
#define E1000_CTRL_SPEED_1000       (2 << E1000_CTRL_SPEED_SHIFT)
#define E1000_CTRL_FRCSPD           (1 << 11)   /* Force Speed */
#define E1000_CTRL_FRCDPLX          (1 << 12)   /* Force Duplex */
#define E1000_CTRL_SDP0_DATA        (1 << 18)   /* SDP0 Data Value */
#define E1000_CTRL_SDP1_DATA        (1 << 19)   /* SDP1 Data Value */
#define E1000_CTRL_SDP0_IODIR       (1 << 22)   /* SDP0 IO Direction */
#define E1000_CTRL_SDP1_IODIR       (1 << 23)   /* SDP1 IO Direction */
#define E1000_CTRL_RST              (1 << 26)   /* Device Reset, Self clearing */
#define E1000_CTRL_RFCE             (1 << 27)   /* Receive Flow Control Enable */
#define E1000_CTRL_TFCE             (1 << 28)   /* Transmit Flow Control Enable */
#define E1000_CTRL_VME              (1 << 30)   /* VLAN Mode Enable */
#define E1000_CTRL_PHY_RST          (1 << 31)   /* PHY Reset */


/* E1000_REG_STATUS */
#define E1000_STATUS_FD             (1 << 0)    /* Full Duplex Indication */
#define E1000_STATUS_LU             (1 << 1)    /* Link Up Indication */
#define E1000_STATUS_FUNC_SHIFT     2
#define E1000_STATUS_FUNC_MASK      (3 << E1000_STATUS_FUNC_SHIFT)
#define E1000_STATUS_TXOFF          (1 << 4)    /* Transmission Paused */
#define E1000_STATUS_TBIMODE        (1 << 5)    /* TBI Mode */
#define E1000_STATUS_SPEEDSHIFT     6           /* Link speed setting */
#define E1000_STATUS_SPEEDMASK      (3 << E1000_STATUS_SPEEDSHIFT)
#define E1000_STATUS_SPEED_10       (0 << E1000_STATUS_SPEEDSHIFT)
#define E1000_STATUS_SPEED_100      (1 << E1000_STATUS_SPEEDSHIFT)
#define E1000_STATUS_SPEED_1000     (2 << E1000_STATUS_SPEEDSHIFT)
#define E1000_STATUS_ASDV_SHIFT     8
#define E1000_STATUS_ASDV_MASK      (3 << E1000_STATUS_ASDV_SHIFT)
#define E1000_STATUS_PHYRA          (1 << 10)   /* PHY Reset Asserted */
#define E1000_STATUS_GIO_MASTER_EN  (1 << 19)   /* GIO Master Enable Status */


/* E1000_REG_EERD - Note: Bit positions vary between chip generations */
/* Legacy 8254x (PCI) format */
#define E1000_EERD_START            (1 << 0)    /* Start Read*/
#define E1000_EERD_DONE             (1 << 4)    /* Read Done (legacy 8254x) */
#define E1000_EERD_ADDR_SHIFT       8           /* Address shift (legacy 8254x) */
#define E1000_EERD_DATA_SHIFT       16

/* 82574L and newer PCIe format (e1000e style) */
#define E1000_EERD_DONE_PCIE        (1 << 1)    /* Read Done (82574L PCIe) */
#define E1000_EERD_ADDR_SHIFT_PCIE  2           /* Address shift (82574L PCIe) */

/* E1000_REG_EECD bits */
#define E1000_EECD_SK               (1 << 0)    /* EEPROM Clock */
#define E1000_EECD_CS               (1 << 1)    /* EEPROM Chip Select */
#define E1000_EECD_DI               (1 << 2)    /* EEPROM Data In */
#define E1000_EECD_DO               (1 << 3)    /* EEPROM Data Out */
#define E1000_EECD_REQ              (1 << 6)    /* EEPROM Access Request */
#define E1000_EECD_GNT              (1 << 7)    /* EEPROM Access Granted */
#define E1000_EECD_PRES             (1 << 8)    /* EEPROM Present */
#define E1000_EECD_AUTO_RD          (1 << 9)    /* Auto Read Done */
#define E1000_EECD_SEC1VAL          (1 << 22)   /* Sector One Valid (82580+) */
#define E1000_EECD_FLASH_DETECTED_SHIFT 15
#define E1000_EECD_FLASH_DETECTED_MASK  (3 << E1000_EECD_FLASH_DETECTED_SHIFT)


/* E1000_REG_MDIC */
#define E1000_MDIC_DATA_MASK        0x0000FFFF
#define E1000_MDIC_REGADD_SHIFT     16          /* PHY Register Address */
#define E1000_MDIC_PHYADD_SHIFT     21          /* PHY Address (1=Gigabit, 2=PCIe) */
#define E1000_MDIC_PHYADD_GIGABIT   1
#define E1000_MDIC_OP_WRITE         (1 << 26)   /* Write Opcode */
#define E1000_MDIC_OP_READ          (2 << 26)   /* Read Opcode */
#define E1000_MDIC_R                (1 << 28)   /* Ready Bit */
#define E1000_MDIC_I                (1 << 29)   /* Interrupt Enable */
#define E1000_MDIC_E                (1 << 30)   /* Error */


/* E1000_REG_ICR / E1000_REG_IMS / E1000_REG_IMC - Interrupt bits */
#define E1000_IMS_TXDW              (1 << 0)    /* Transmit Descriptor Written Back */
#define E1000_IMS_TXQE              (1 << 1)    /* Transmit Queue Empty */
#define E1000_IMS_LSC               (1 << 2)    /* Link Status Change */
#define E1000_IMS_RXSEQ             (1 << 3)    /* Receive Sequence Error */
#define E1000_IMS_RXDMT0            (1 << 4)    /* Receive Descriptor Minimum Threshold Reached */
#define E1000_IMS_RXO               (1 << 6)    /* Receive Overrun */
#define E1000_IMS_RXT0              (1 << 7)    /* Receiver Timer Interrupt */
#define E1000_IMS_MDAC              (1 << 9)    /* MDIO Access Complete */
#define E1000_IMS_RXCFG             (1 << 10)   /* Receiving /C/ ordered sets */
#define E1000_IMS_PHYINT            (1 << 12)   /* PHY Interrupt */
#define E1000_IMS_GPI_SDP2          (1 << 13)   /* General Purpose Interrupt SDP2 */
#define E1000_IMS_GPI_SDP3          (1 << 14)   /* General Purpose Interrupt SDP3 */
#define E1000_IMS_TXD_LOW           (1 << 15)   /* Transmit Descriptor Low Threshold hit */
#define E1000_IMS_SRPD              (1 << 16)   /* Small Receive Packet Detection */
#define E1000_IMS_ACK               (1 << 17)   /* Receive ACK Frame Detect */
#define E1000_IMS_MNG               (1 << 18)   /* Manageability Event */
#define E1000_IMS_DOCK              (1 << 19)   /* Dock/Undock */
#define E1000_IMS_RXQ0              (1 << 20)   /* Rx Queue 0 Interrupt */
#define E1000_IMS_RXQ1              (1 << 21)   /* Rx Queue 1 Interrupt */
#define E1000_IMS_TXQ0              (1 << 22)   /* Tx Queue 0 Interrupt */
#define E1000_IMS_TXQ1              (1 << 23)   /* Tx Queue 1 Interrupt */
#define E1000_IMS_OTHER             (1 << 24)   /* Other Interrupts */

/* 82574L Extended interrupts for MSI-X */
#define E1000_IMS_INT_ASSERTED      (1 << 31)   /* Interrupt Asserted (set in ICR when any interrupt) */

/*
 * Extended Interrupt Mask bits for 82574L MSI-X
 *
 * On 82574L, these are the same bits as in IMS register (bits 20-24).
 * The "EIMS/EIMC" registers are actually the same as IMS/IMC.
 * The queue interrupt bits share the IMS register with legacy bits.
 */
#define E1000_EIMS_RXQ0             E1000_IMS_RXQ0      /* Rx Queue 0 - bit 20 */
#define E1000_EIMS_RXQ1             E1000_IMS_RXQ1      /* Rx Queue 1 - bit 21 */
#define E1000_EIMS_TXQ0             E1000_IMS_TXQ0      /* Tx Queue 0 - bit 22 */
#define E1000_EIMS_TXQ1             E1000_IMS_TXQ1      /* Tx Queue 1 - bit 23 */
#define E1000_EIMS_OTHER            E1000_IMS_OTHER     /* Other - bit 24 */


/* E1000_REG_ITR */
#define MAX_INTS_PER_SEC        8000
#define DEFAULT_ITR             1000000000/(MAX_INTS_PER_SEC * 256)

/* Interrupt throttling rates for adaptive moderation */
#define E1000_ITR_LOW           (1000000000 / (2000 * 256))   /* Low load: ~2000 int/sec */
#define E1000_ITR_MEDIUM        (1000000000 / (8000 * 256))   /* Medium load: ~8000 int/sec */
#define E1000_ITR_HIGH          (1000000000 / (20000 * 256))  /* High load: ~20000 int/sec */
#define E1000_ITR_DISABLED      0                              /* No throttling */


/* E1000_REG_RCTL */
#define E1000_RCTL_EN               (1 << 1)    /* Receiver Enable */
#define E1000_RCTL_SBP              (1 << 2)    /* Store Bad Packets */
#define E1000_RCTL_UPE              (1 << 3)    /* Unicast Promiscuous Enabled */
#define E1000_RCTL_MPE              (1 << 4)    /* Multicast Promiscuous Enabled */
#define E1000_RCTL_LPE              (1 << 5)    /* Long Packet Reception Enable */
#define E1000_RCTL_LBM_SHIFT        6           /* Loopback Mode */
#define E1000_RCTL_LBM_NONE         (0 << 6)    /* No Loopback */
#define E1000_RCTL_LBM_MAC          (1 << 6)    /* MAC Loopback */
#define E1000_RCTL_RDMTS_SHIFT      8           /* Receive Descriptor Minimum Threshold Size */
#define E1000_RCTL_RDMTS_HALF       (0 << 8)    /* RDMTS = 1/2 RDLEN */
#define E1000_RCTL_RDMTS_QUARTER    (1 << 8)    /* RDMTS = 1/4 RDLEN */
#define E1000_RCTL_RDMTS_EIGHTH     (2 << 8)    /* RDMTS = 1/8 RDLEN */
#define E1000_RCTL_MO_SHIFT         12          /* Multicast Offset */
#define E1000_RCTL_BAM              (1 << 15)   /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_SHIFT      16
#define E1000_RCTL_BSIZE_MASK       (3 << 16)
#define E1000_RCTL_BSIZE_2048       (0 << 16)   /* Receive Buffer Size = 2048 */
#define E1000_RCTL_BSIZE_1024       (1 << 16)   /* Receive Buffer Size = 1024 */
#define E1000_RCTL_BSIZE_512        (2 << 16)   /* Receive Buffer Size = 512 */
#define E1000_RCTL_BSIZE_256        (3 << 16)   /* Receive Buffer Size = 256 */
#define E1000_RCTL_VFE              (1 << 18)   /* VLAN Filter Enable */
#define E1000_RCTL_CFIEN            (1 << 19)   /* Canonical Form Indicator Enable */
#define E1000_RCTL_CFI              (1 << 20)   /* Canonical Form Indicator Value */
#define E1000_RCTL_DPF              (1 << 22)   /* Discard Pause Frames */
#define E1000_RCTL_PMCF             (1 << 23)   /* Pass MAC Control Frames */
#define E1000_RCTL_BSEX             (1 << 25)   /* Buffer Size Extension */
#define E1000_RCTL_SECRC            (1 << 26)   /* Strip Ethernet CRC from incoming packet */
#define E1000_RCTL_FLXBUF_SHIFT     27          /* Flexible Buffer Size */

#define E1000_RCTL_FILTER_BITS      (E1000_RCTL_SBP | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BAM | E1000_RCTL_PMCF)


/* E1000_REG_RXCSUM - Receive Checksum Control */
#define E1000_RXCSUM_PCSS_MASK      0x000000FF  /* Packet Checksum Start */
#define E1000_RXCSUM_IPOFL          (1 << 8)    /* IP Checksum Offload Enable */
#define E1000_RXCSUM_TUOFL          (1 << 9)    /* TCP/UDP Checksum Offload Enable */
#define E1000_RXCSUM_IPV6OFL        (1 << 10)   /* IPv6 Checksum Offload Enable */
#define E1000_RXCSUM_CRCOFL         (1 << 11)   /* CRC32 Offload Enable (82574L) */
#define E1000_RXCSUM_IPPCSE         (1 << 12)   /* IP Payload Checksum Enable (82574L) */
#define E1000_RXCSUM_PCSD           (1 << 13)   /* Packet Checksum Disable (82574L) */


/* E1000_REG_TCTL */
#define E1000_TCTL_EN               (1 << 1)    /* Transmit Enable */
#define E1000_TCTL_PSP              (1 << 3)    /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT         4           /* Collision Threshold */
#define E1000_TCTL_CT_MASK          (0xFF << 4)
#define E1000_TCTL_COLD_SHIFT       12          /* Collision Distance */
#define E1000_TCTL_COLD_MASK        (0x3FF << 12)
#define E1000_TCTL_SWXOFF           (1 << 22)   /* Software XOFF Transmission */
#define E1000_TCTL_RTLC             (1 << 24)   /* Retransmit on Late Collision */
#define E1000_TCTL_NRTU             (1 << 25)   /* No Retransmit on Underrun */
#define E1000_TCTL_MULR             (1 << 28)   /* Multiple Request Support */

/* Default TCTL values for full duplex */
#define E1000_TCTL_CT_DEF           (0x0F << E1000_TCTL_CT_SHIFT)
#define E1000_TCTL_COLD_DEF         (0x40 << E1000_TCTL_COLD_SHIFT)  /* Full Duplex */
#define E1000_TCTL_COLD_HD          (0x200 << E1000_TCTL_COLD_SHIFT) /* Half Duplex */


/* E1000_REG_TIPG */
#define E1000_TIPG_IPGT_DEF         (10 << 0)   /* IPG Transmit Time */
#define E1000_TIPG_IPGR1_DEF        (10 << 10)  /* IPG Receive Time 1 */
#define E1000_TIPG_IPGR2_DEF        (10 << 20)  /* IPG Receive Time 2 */


/* E1000_REG_TXCSUM - Transmit Checksum Offload (82574L specific) */
/* Note: 82574L uses legacy TX descriptors with checksum fields */


/* E1000_REG_TXDCTL - Transmit Descriptor Control */
#define E1000_TXDCTL_PTHRESH_SHIFT  0           /* Prefetch Threshold */
#define E1000_TXDCTL_PTHRESH_MASK   0x3F
#define E1000_TXDCTL_HTHRESH_SHIFT  8           /* Host Threshold */
#define E1000_TXDCTL_HTHRESH_MASK   (0x3F << 8)
#define E1000_TXDCTL_WTHRESH_SHIFT  16          /* Write Back Threshold */
#define E1000_TXDCTL_WTHRESH_MASK   (0x3F << 16)
#define E1000_TXDCTL_GRAN           (1 << 24)   /* Descriptor Granularity */
#define E1000_TXDCTL_LWTHRESH_SHIFT 25          /* Low Threshold */
#define E1000_TXDCTL_LWTHRESH_MASK  (0x7F << 25)


/* E1000_REG_RAH */
#define E1000_RAH_AV                (1 << 31)   /* Address Valid */
#define E1000_RAH_ASEL_SHIFT        16          /* Address Select */
#define E1000_RAH_ASEL_DEST         (0 << 16)   /* Destination Address */
#define E1000_RAH_ASEL_SRC          (1 << 16)   /* Source Address */
#define E1000_RAH_POOL_SHIFT        18          /* Pool Select (82574) */


/* E1000_REG_WUC - Wake Up Control */
#define E1000_WUC_PME_EN            (1 << 1)    /* PME Enable */
#define E1000_WUC_PME_STATUS        (1 << 2)    /* PME Status */
#define E1000_WUC_APMPME            (1 << 4)    /* Assert PME on APM Wakeup */


/* E1000_REG_WUFC - Wake Up Filter Control */
#define E1000_WUFC_LNKC             (1 << 0)    /* Link Status Change Wakeup Enable */
#define E1000_WUFC_MAG              (1 << 1)    /* Magic Packet Wakeup Enable */
#define E1000_WUFC_EX               (1 << 2)    /* Directed Exact Wakeup Enable */
#define E1000_WUFC_MC               (1 << 3)    /* Directed Multicast Wakeup Enable */
#define E1000_WUFC_BC               (1 << 4)    /* Broadcast Wakeup Enable */
#define E1000_WUFC_ARP              (1 << 5)    /* ARP Request Wakeup Enable */
#define E1000_WUFC_IPV4             (1 << 6)    /* Directed IPv4 Wakeup Enable */
#define E1000_WUFC_IPV6             (1 << 7)    /* Directed IPv6 Wakeup Enable */
#define E1000_WUFC_FLX0             (1 << 16)   /* Flexible Filter 0 Enable */
#define E1000_WUFC_FLX1             (1 << 17)   /* Flexible Filter 1 Enable */
#define E1000_WUFC_FLX2             (1 << 18)   /* Flexible Filter 2 Enable */
#define E1000_WUFC_FLX3             (1 << 19)   /* Flexible Filter 3 Enable */
#define E1000_WUFC_ALL_FILTERS      0x000F00FF  /* All wakeup filters */


/* E1000_REG_MANC - Management Control */
#define E1000_MANC_SMBUS_EN         (1 << 0)    /* SMBus Enabled */
#define E1000_MANC_ASF_EN           (1 << 1)    /* ASF Enabled */
#define E1000_MANC_ARP_EN           (1 << 13)   /* ARP Request Filtering Enable */
#define E1000_MANC_RCV_TCO_EN       (1 << 17)   /* Receive TCO Packets Enabled */
#define E1000_MANC_BLK_PHY_RST_ON_IDE (1 << 18) /* Block PHY Reset on IDE event */
#define E1000_MANC_EN_MAC_ADDR_FILTER (1 << 19) /* Enable MAC Address Filtering */
#define E1000_MANC_EN_MNG2HOST      (1 << 21)   /* Enable MNG to Host packets */


/* NVM */
#define E1000_NVM_REG_CHECKSUM      0x03f
#define NVM_MAGIC_SUM               0xBABA


/* PHY (Read with MDIC) */
#define E1000_PHY_CONTROL           0x00
#define E1000_PHY_STATUS            0x01
#define E1000_PHY_ID1               0x02
#define E1000_PHY_ID2               0x03
#define E1000_PHY_AUTONEG_ADV       0x04
#define E1000_PHY_LP_ABILITY        0x05
#define E1000_PHY_AUTONEG_EXP       0x06
#define E1000_PHY_1000T_CTRL        0x09
#define E1000_PHY_1000T_STATUS      0x0A
#define E1000_PHY_EXT_STATUS        0x0F
#define E1000_PHY_SPECIFIC_CTRL     0x10
#define E1000_PHY_SPECIFIC_STATUS   0x11


/* E1000_PHY_STATUS */
#define E1000_PS_LINK_STATUS        (1 << 2)


/* E1000_PHY_SPECIFIC_STATUS */
#define E1000_PSS_SPEED_AND_DUPLEX  (1 << 11)   /* Speed and Duplex Resolved */
#define E1000_PSS_SPEEDSHIFT        14
#define E1000_PSS_SPEEDMASK         (3 << E1000_PSS_SPEEDSHIFT)


/* ============================================================================
 * MSI-X Support Definitions for 82574L
 * ============================================================================ */

/* MSI-X vector assignments for 82574L (5 vectors available) */
#define E1000_MSIX_VECTOR_RXQ0      0           /* Rx Queue 0 */
#define E1000_MSIX_VECTOR_RXQ1      1           /* Rx Queue 1 */
#define E1000_MSIX_VECTOR_TXQ0      2           /* Tx Queue 0 */
#define E1000_MSIX_VECTOR_TXQ1      3           /* Tx Queue 1 */
#define E1000_MSIX_VECTOR_OTHER     4           /* Other (Link, etc.) */
#define E1000_MSIX_VECTOR_COUNT     5

/*
 * IVAR bit positions for 82574L
 *
 * Per Linux e1000e driver (82571.h), the 82574L uses:
 *   - Bits 0-2: Vector number (0-4)
 *   - Bit 3: Valid bit (E1000_IVAR_INT_ALLOC_VALID = 0x8)
 *   - Repeat pattern at byte boundaries for different queues
 *
 * Single IVAR register layout (at 0xE4):
 *   Bits 0-3:   Rx Queue 0 (vector in 0-2, valid in 3)
 *   Bits 8-11:  Tx Queue 0 (vector in 8-10, valid in 11)
 *   Bits 16-19: Other Causes (vector in 16-18, valid in 19)
 *   Bit 31:     Tx interrupt on every write-back (set for performance)
 */
#define E1000_IVAR_INT_ALLOC_VALID  0x08        /* Entry valid bit (bit 3 of each field) */
#define E1000_IVAR_VECTOR_MASK      0x07        /* Vector number mask (bits 0-2) */

/* Shift values for IVAR fields */
#define E1000_IVAR_RXQ0_SHIFT       0           /* Rx Queue 0 at bits 0-3 */
#define E1000_IVAR_TXQ0_SHIFT       8           /* Tx Queue 0 at bits 8-11 */
#define E1000_IVAR_OTHER_SHIFT      16          /* Other at bits 16-19 */
#define E1000_IVAR_TX_WB_ON_EITR    (1UL << 31) /* Tx interrupt on every write-back */

/* Legacy alias for compatibility (code may use either name) */
#define E1000_IVAR_VALID            E1000_IVAR_INT_ALLOC_VALID

/* Deprecated - kept for source compatibility but not used on 82574L */
#define E1000_IVAR0                 E1000_REG_IVAR
#define E1000_IVAR_MISC             E1000_REG_IVAR  /* No separate IVAR_MISC on 82574L */
#define E1000_IVAR_RXQ1_SHIFT       0               /* Not used - single queue mode */
#define E1000_IVAR_TXQ1_SHIFT       0               /* Not used - single queue mode */
#define E1000_IVAR_MISC_OTHER_SHIFT E1000_IVAR_OTHER_SHIFT


/* ============================================================================
 * PCI Configuration Space Definitions
 * ============================================================================ */

/* PCI Power Management Capability */
#define PCI_PM_CAP_ID               0x01
#define PCI_PM_CTRL_STATE_MASK      0x0003
#define PCI_PM_CTRL_STATE_D0        0x0000
#define PCI_PM_CTRL_STATE_D1        0x0001
#define PCI_PM_CTRL_STATE_D2        0x0002
#define PCI_PM_CTRL_STATE_D3        0x0003
#define PCI_PM_CTRL_PME_ENABLE      (1 << 8)
#define PCI_PM_CTRL_PME_STATUS      (1 << 15)

/* PCI MSI Capability */
#define PCI_MSI_CAP_ID              0x05
#define PCI_MSI_CTRL_ENABLE         (1 << 0)
#define PCI_MSI_CTRL_64BIT          (1 << 7)
#define PCI_MSI_CTRL_PERVECTOR_MASK (1 << 8)

/* PCI MSI-X Capability */
#define PCI_MSIX_CAP_ID             0x11
#define PCI_MSIX_CTRL_ENABLE        (1 << 15)
#define PCI_MSIX_CTRL_FUNCMASK      (1 << 14)
#define PCI_MSIX_CTRL_TABSIZE_MASK  0x07FF

/* PCI Express Capability */
#define PCI_EXPRESS_CAP_ID          0x10
#define PCI_EXP_DEVCTL              0x08        /* Device Control offset from capability */
#define PCI_EXP_DEVCTL_CERE         (1 << 0)    /* Correctable Error Reporting Enable */
#define PCI_EXP_DEVCTL_NFERE        (1 << 1)    /* Non-Fatal Error Reporting Enable */
#define PCI_EXP_DEVCTL_FERE         (1 << 2)    /* Fatal Error Reporting Enable */
#define PCI_EXP_DEVCTL_URRE         (1 << 3)    /* Unsupported Request Reporting Enable */
#define PCI_EXP_DEVCTL_RELAX_EN     (1 << 4)    /* Relaxed Ordering Enable */
#define PCI_EXP_DEVCTL_MAX_PAYLOAD_SHIFT 5
#define PCI_EXP_DEVCTL_MAX_PAYLOAD_MASK (7 << 5)
#define PCI_EXP_DEVCTL_EXT_TAG      (1 << 8)    /* Extended Tag Field Enable */
#define PCI_EXP_DEVCTL_PHANTOM      (1 << 9)    /* Phantom Functions Enable */
#define PCI_EXP_DEVCTL_AUX_PME      (1 << 10)   /* Aux Power PM Enable */
#define PCI_EXP_DEVCTL_NOSNOOP_EN   (1 << 11)   /* No Snoop Enable */
#define PCI_EXP_DEVCTL_MAX_READ_REQ_SHIFT 12
#define PCI_EXP_DEVCTL_MAX_READ_REQ_MASK (7 << 12)

#define PCI_EXP_DEVSTA              0x0A        /* Device Status offset from capability */
#define PCI_EXP_DEVSTA_CED          (1 << 0)    /* Correctable Error Detected */
#define PCI_EXP_DEVSTA_NFED         (1 << 1)    /* Non-Fatal Error Detected */
#define PCI_EXP_DEVSTA_FED          (1 << 2)    /* Fatal Error Detected */
#define PCI_EXP_DEVSTA_URD          (1 << 3)    /* Unsupported Request Detected */


/* ============================================================================
 * Advanced Error Reporting (AER) Extended Capability
 * ============================================================================ */

#define PCI_AER_CAP_ID              0x0001

/* AER registers (offsets from AER capability base) */
#define PCI_AER_UNCOR_STATUS        0x04        /* Uncorrectable Error Status */
#define PCI_AER_UNCOR_MASK          0x08        /* Uncorrectable Error Mask */
#define PCI_AER_UNCOR_SEVER         0x0C        /* Uncorrectable Error Severity */
#define PCI_AER_COR_STATUS          0x10        /* Correctable Error Status */
#define PCI_AER_COR_MASK            0x14        /* Correctable Error Mask */
#define PCI_AER_CAP_CTRL            0x18        /* Advanced Error Capabilities and Control */
#define PCI_AER_HEADER_LOG          0x1C        /* Header Log (16 bytes) */
#define PCI_AER_ROOT_COMMAND        0x2C        /* Root Error Command */
#define PCI_AER_ROOT_STATUS         0x30        /* Root Error Status */
#define PCI_AER_ERR_SRC_ID          0x34        /* Error Source Identification */

/* Uncorrectable Error bits */
#define PCI_AER_UNC_DLP             (1 << 4)    /* Data Link Protocol Error */
#define PCI_AER_UNC_SURPDN          (1 << 5)    /* Surprise Down Error */
#define PCI_AER_UNC_POISON_TLP      (1 << 12)   /* Poisoned TLP */
#define PCI_AER_UNC_FCP             (1 << 13)   /* Flow Control Protocol Error */
#define PCI_AER_UNC_COMP_TIME       (1 << 14)   /* Completion Timeout */
#define PCI_AER_UNC_COMP_ABORT      (1 << 15)   /* Completer Abort */
#define PCI_AER_UNC_UNX_COMP        (1 << 16)   /* Unexpected Completion */
#define PCI_AER_UNC_RX_OVER         (1 << 17)   /* Receiver Overflow */
#define PCI_AER_UNC_MALF_TLP        (1 << 18)   /* Malformed TLP */
#define PCI_AER_UNC_ECRC            (1 << 19)   /* ECRC Error */
#define PCI_AER_UNC_UNSUP           (1 << 20)   /* Unsupported Request Error */

/* Correctable Error bits */
#define PCI_AER_COR_RCVR            (1 << 0)    /* Receiver Error */
#define PCI_AER_COR_BAD_TLP         (1 << 6)    /* Bad TLP */
#define PCI_AER_COR_BAD_DLLP        (1 << 7)    /* Bad DLLP */
#define PCI_AER_COR_REP_ROLL        (1 << 8)    /* Replay Num Rollover */
#define PCI_AER_COR_REP_TIMER       (1 << 12)   /* Replay Timer Timeout */
#define PCI_AER_COR_ADV_NFAT        (1 << 13)   /* Advisory Non-Fatal Error */


/* ============================================================================
 * Checksum Offload Constants
 * ============================================================================ */

/* IP Header checksum constants */
#define E1000_CSUM_IP_START         14          /* Start after Ethernet header */
#define E1000_CSUM_IP_OFFSET        24          /* IP header checksum at offset 10 from IP start */
#define E1000_CSUM_IP_END           34          /* End of IP header (min) */

/* TCP checksum constants (IPv4) */
#define E1000_CSUM_TCP_START        34          /* Start after IP header (IPv4, no options) */
#define E1000_CSUM_TCP_OFFSET       50          /* TCP checksum at offset 16 from TCP start */

/* UDP checksum constants (IPv4) */
#define E1000_CSUM_UDP_START        34          /* Start after IP header */
#define E1000_CSUM_UDP_OFFSET       40          /* UDP checksum at offset 6 from UDP start */

/* Ethernet protocol types */
#define ETH_TYPE_IPV4               0x0800
#define ETH_TYPE_IPV6               0x86DD
#define ETH_TYPE_VLAN               0x8100

/* IP Protocol numbers */
#define IP_PROTOCOL_TCP             6
#define IP_PROTOCOL_UDP             17


/* ============================================================================
 * Receive Side Scaling (RSS) Definitions for 82574L
 * ============================================================================ */

/* RSS Register addresses */
#define E1000_REG_MRQC              0x5818      /* Multiple Receive Queues Command */
#define E1000_REG_RETA              0x5C00      /* Redirection Table (32 DWORDs = 128 entries) */
#define E1000_REG_RSSRK             0x5C80      /* RSS Random Key (10 DWORDs = 40 bytes) */

/* RSS register array sizes */
#define E1000_RETA_SIZE             32          /* 32 DWORDs for indirection table */
#define E1000_RSSRK_SIZE            10          /* 10 DWORDs for 40-byte hash key */

/* MRQC (Multiple Receive Queues Command) Register bits */
#define E1000_MRQC_RSS_ENABLE           0x00000001  /* Enable RSS */
#define E1000_MRQC_RSS_FIELD_MASK       0xFFFF0000  /* RSS field enable mask */

/* RSS Hash Field Selection bits (in MRQC) */
#define E1000_MRQC_RSS_FIELD_IPV4           0x00010000  /* IPv4 */
#define E1000_MRQC_RSS_FIELD_IPV4_TCP       0x00020000  /* IPv4/TCP */
#define E1000_MRQC_RSS_FIELD_IPV6           0x00040000  /* IPv6 */
#define E1000_MRQC_RSS_FIELD_IPV6_TCP       0x00080000  /* IPv6/TCP */
#define E1000_MRQC_RSS_FIELD_IPV6_TCP_EX    0x00100000  /* IPv6/TCP with extensions */
#define E1000_MRQC_RSS_FIELD_IPV6_EX        0x00200000  /* IPv6 with extensions */
#define E1000_MRQC_RSS_FIELD_IPV4_UDP       0x00400000  /* IPv4/UDP (82576+) */
#define E1000_MRQC_RSS_FIELD_IPV6_UDP       0x00800000  /* IPv6/UDP (82576+) */
#define E1000_MRQC_RSS_FIELD_IPV6_UDP_EX    0x01000000  /* IPv6/UDP with extensions (82576+) */


/* ============================================================================
 * Wake-on-LAN Register Definitions
 * ============================================================================ */

/* WUC (Wake Up Control) Register bits - expanded definitions */
#define E1000_WUC_MAGIC_PACKET      (1 << 1)    /* Magic Packet Wakeup Enable */
#define E1000_WUC_LINK_CHANGE       (1 << 3)    /* Link Status Change Wakeup Enable */


/* ============================================================================
 * TCTL (Transmit Control) Standard Values
 * ============================================================================ */

/* Collision threshold - IEEE 802.3 standard */
#define E1000_TCTL_CT_IEEE          (0x0F << E1000_TCTL_CT_SHIFT)

/* Collision distance for full duplex (alias for E1000_TCTL_COLD_DEF) */
#define E1000_TCTL_COLD_FD          E1000_TCTL_COLD_DEF


/* ============================================================================
 * Additional Receive Address Register Aliases
 * ============================================================================ */

#define E1000_REG_RAL0              E1000_REG_RAL       /* Receive Address Low 0 */
#define E1000_REG_RAH0              E1000_REG_RAH       /* Receive Address High 0 */
