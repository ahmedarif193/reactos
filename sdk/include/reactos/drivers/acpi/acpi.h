/*
 * PROJECT:         ReactOS PCI Bus Driver
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            include/reactos/drivers/acpi/acpi.h
 * PURPOSE:         ACPI Tables and NT Registry Data
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

//
// ACPI BIOS Registry Component Configuration Data
//
typedef struct _ACPI_E820_ENTRY
{
    PHYSICAL_ADDRESS Base;
    LARGE_INTEGER Length;
    ULONGLONG Type;
} ACPI_E820_ENTRY, *PACPI_E820_ENTRY;

typedef struct _ACPI_BIOS_MULTI_NODE
{
    PHYSICAL_ADDRESS RsdtAddress;
    ULONGLONG Count;
    ACPI_E820_ENTRY E820Entry[1];
} ACPI_BIOS_MULTI_NODE, *PACPI_BIOS_MULTI_NODE;

//
// ACPI Signatures
//
#define RSDP_SIGNATURE 0x2052545020445352       // "RSD PTR "
#define FACS_SIGNATURE 0x53434146               // "FACS"
#define FADT_SIGNATURE 0x50434146               // "FACP"
#define RSDT_SIGNATURE 0x54445352               // "RSDT"
#define APIC_SIGNATURE 0x43495041               // "APIC"
#define DSDT_SIGNATURE 0x54445344               // "DSDT"
#define SSDT_SIGNATURE 0x54445353               // "SSDT"
#define PSDT_SIGNATURE 0x54445350               // "PSDT"
#define SBST_SIGNATURE 0x54534253               // "SBST"
#define DBGP_SIGNATURE 0x50474244               // "DBGP"
#define XSDT_SIGNATURE 'TDSX'
#define BOOT_SIGNATURE 'TOOB'
#define SRAT_SIGNATURE 'TARS'
#define WDRT_SIGNATURE 'TRDW'
#define BGRT_SIGNATURE  0x54524742      	// "BGRT"
#define MCFG_SIGNATURE  0x4746434D      	// "MCFG"

//
// FADT Flags
//
#define ACPI_TMR_VAL_EXT 	0x100

//
// BGRT Flags
//
#define BGRT_STATUS_IMAGE_VALID 0x01

//
// BGRT Image Types
//
typedef enum _BGRT_IMAGE_TYPE
{
    BgrtImageTypeBitmap,
    BgrtImageTypeMax
} BGRT_IMAGE_TYPE, *PBGRT_IMAGE_TYPE;

//
// ACPI Generic Register Address
//
#include <pshpack1.h>
typedef struct _GEN_ADDR
{
    UCHAR AddressSpaceID;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR Reserved;
    PHYSICAL_ADDRESS Address;
} GEN_ADDR, *PGEN_ADDR;

//
// ACPI BIOS Structures (packed)
//
typedef struct  _RSDP
{
    // ACPI v1.0 (Rev=0)
    ULONGLONG Signature;
    UCHAR Checksum;
    UCHAR OEMID[6];
    UCHAR Revision; // Was reserved before ACPI v2.0 (Rev=2).
    ULONG RsdtAddress;
    // ACPI v2.0 (Rev=2)
    ULONG Length;
    PHYSICAL_ADDRESS XsdtAddress;
    UCHAR XChecksum;
    UCHAR Reserved33[3];
} RSDP;
typedef RSDP *PRSDP;

typedef struct _DESCRIPTION_HEADER
{
    // ACPI v1.0
    ULONG Signature;
    ULONG Length;
    UCHAR Revision;
    UCHAR Checksum;
    UCHAR OEMID[6];
    UCHAR OEMTableID[8];
    ULONG OEMRevision;
    UCHAR CreatorID[4];
    ULONG CreatorRev;
} DESCRIPTION_HEADER;
typedef DESCRIPTION_HEADER *PDESCRIPTION_HEADER;

typedef struct _FACS
{
    // ACPI v1.0 (Ver=0)
    ULONG Signature;
    ULONG Length;
    ULONG HardwareSignature;
    ULONG pFirmwareWakingVector;
    ULONG GlobalLock;
    ULONG Flags;
    PHYSICAL_ADDRESS x_FirmwareWakingVector; // Was reserved before ACPI v2.0 (Ver=1).
    UCHAR Version; // Was reserved before ACPI v2.0 (Ver=1).
    UCHAR Reserved33[3];
    ULONG OsFlags; // Was reserved before ACPI v4.0 (Ver=2).
    UCHAR Reserved40[24];
} FACS;
typedef FACS *PFACS;

typedef struct _FADT
{
    // ACPI v1.0 (H.Rev=1)
    DESCRIPTION_HEADER Header;
    ULONG facs;
    ULONG dsdt;
    UCHAR reserved44[1]; // Was int_model before ACPI v2.0.
    UCHAR pm_profile;
    USHORT sci_int_vector;
    ULONG smi_cmd_io_port;
    UCHAR acpi_on_value;
    UCHAR acpi_off_value;
    UCHAR s4bios_req;
    UCHAR pstate_control;
    ULONG pm1a_evt_blk_io_port;
    ULONG pm1b_evt_blk_io_port;
    ULONG pm1a_ctrl_blk_io_port;
    ULONG pm1b_ctrl_blk_io_port;
    ULONG pm2_ctrl_blk_io_port;
    ULONG pm_tmr_blk_io_port;
    ULONG gp0_blk_io_port;
    ULONG gp1_blk_io_port;
    UCHAR pm1_evt_len;
    UCHAR pm1_ctrl_len;
    UCHAR pm2_ctrl_len;
    UCHAR pm_tmr_len;
    UCHAR gp0_blk_len;
    UCHAR gp1_blk_len;
    UCHAR gp1_base;
    UCHAR cstate_control;
    USHORT lvl2_latency;
    USHORT lvl3_latency;
    USHORT flush_size;
    USHORT flush_stride;
    UCHAR duty_offset;
    UCHAR duty_width;
    UCHAR day_alarm_index;
    UCHAR month_alarm_index;
    UCHAR century_alarm_index;
    USHORT boot_arch;
    UCHAR reserved111[1];
    ULONG flags;
    // ACPI v1.5 (H.Rev=3)
    GEN_ADDR reset_reg;
    UCHAR reset_val;
    USHORT arm_boot_arch; // Was reserved before ACPI v5.1.
    UCHAR minor_revision; // Was reserved before ACPI v5.1.
    PHYSICAL_ADDRESS x_firmware_ctrl;
    PHYSICAL_ADDRESS x_dsdt;
    GEN_ADDR x_pm1a_evt_blk;
    GEN_ADDR x_pm1b_evt_blk;
    GEN_ADDR x_pm1a_ctrl_blk;
    GEN_ADDR x_pm1b_ctrl_blk;
    GEN_ADDR x_pm2_ctrl_blk;
    GEN_ADDR x_pm_tmr_blk;
    GEN_ADDR x_gp0_blk;
    GEN_ADDR x_gp1_blk;
    // ACPI v5.0 (H.Rev=5)
    GEN_ADDR sleep_control;
    GEN_ADDR sleep_status;
    // ACPI v6.0 (H.Rev=6)
    ULONGLONG hypervisor_id;
} FADT;
typedef FADT *PFADT;

typedef struct _DSDT
{
    DESCRIPTION_HEADER Header;
    UCHAR DiffDefBlock[ANYSIZE_ARRAY];
} DSDT;
typedef DSDT *PDSDT;

typedef struct _RSDT
{
    // ACPI v1.0 (H.Rev=1)
    DESCRIPTION_HEADER Header;
    ULONG Tables[ANYSIZE_ARRAY];
} RSDT;
typedef RSDT *PRSDT;

typedef struct _XSDT
{
    // ACPI v2.0 (H.Rev=1)
    DESCRIPTION_HEADER Header;
    PHYSICAL_ADDRESS Tables[ANYSIZE_ARRAY];
} XSDT;
typedef XSDT *PXSDT;
#include <poppack.h>

//
// Microsoft-specific (pretty much) ACPI Tables, normal MS ABI packing
//
typedef struct _DEBUG_PORT_TABLE
{
    DESCRIPTION_HEADER Header;
    UCHAR InterfaceType;
    UCHAR Reserved[3];
    GEN_ADDR BaseAddress;
} DEBUG_PORT_TABLE, *PDEBUG_PORT_TABLE;

typedef struct _WATCHDOG_TABLE
{
    DESCRIPTION_HEADER Header;
    GEN_ADDR ControlRegister;
    GEN_ADDR CountRegister;
    USHORT PciDeviceId;
    USHORT PciVendorId;
    UCHAR PciBus;
    UCHAR PciDevice;
    UCHAR PciFunction;
    UCHAR PciSegment;
    USHORT MaxCount;
    UCHAR Units;
} WATCHDOG_TABLE, *PWATCHDOG_TABLE;

typedef struct _BOOT_TABLE
{
    DESCRIPTION_HEADER Header;
    UCHAR CMOSIndex;
    UCHAR Reserved[3];
} BOOT_TABLE, *PBOOT_TABLE;

//
// MCFG (Memory Mapped Configuration) Table for PCIe
//
typedef struct _MCFG_ALLOCATION
{
    PHYSICAL_ADDRESS BaseAddress;
    USHORT PciSegment;
    UCHAR StartBusNumber;
    UCHAR EndBusNumber;
    ULONG Reserved;
} MCFG_ALLOCATION, *PMCFG_ALLOCATION;

typedef struct _MCFG_TABLE
{
    DESCRIPTION_HEADER Header;
    ULONGLONG Reserved;
    MCFG_ALLOCATION Allocations[ANYSIZE_ARRAY];
} MCFG_TABLE, *PMCFG_TABLE;

typedef struct _ACPI_SRAT
{
    DESCRIPTION_HEADER Header;
    UCHAR TableRevision;
    ULONG Reserved[2];
} ACPI_SRAT, *PACPI_SRAT;

typedef struct _BGRT_TABLE
{
    DESCRIPTION_HEADER Header;
    USHORT Version;
    UCHAR Status;
    UCHAR ImageType;
    ULONGLONG LogoAddress;
    ULONG OffsetX;
    ULONG OffsetY;
} BGRT_TABLE, *PBGRT_TABLE;

//
// ACPI _PRT (PCI Routing Table) Structures
//
typedef struct _ACPI_PRT_ENTRY
{
    ULONGLONG Address;       // Device address (high 16 bits = device, low 16 bits = function)
    ULONG Pin;              // PCI interrupt pin (0=INTA, 1=INTB, 2=INTC, 3=INTD)
    ULONG Source;           // Interrupt source (0 if SourceName used)
    CHAR SourceName[16];    // ACPI device path for interrupt controller
    ULONG SourceIndex;      // Pin on the interrupt controller
} ACPI_PRT_ENTRY, *PACPI_PRT_ENTRY;

typedef struct _ACPI_PRT_TABLE
{
    ULONG Count;
    ACPI_PRT_ENTRY Entries[64];  // Maximum 64 PCI routing entries
} ACPI_PRT_TABLE, *PACPI_PRT_TABLE;

//
// ACPI Power Management Structures
//
typedef enum _ACPI_POWER_STATE
{
    AcpiPowerStateD0 = 0,    // Fully functional
    AcpiPowerStateD1 = 1,    // Not used in PCI
    AcpiPowerStateD2 = 2,    // Not used in PCI  
    AcpiPowerStateD3 = 3,    // Power off
    AcpiPowerStateUnknown = 4
} ACPI_POWER_STATE, *PACPI_POWER_STATE;

typedef struct _ACPI_DEVICE_POWER_INFO
{
    ACPI_POWER_STATE CurrentState;
    ACPI_POWER_STATE SupportedStates[4];  // D0, D1, D2, D3
    BOOLEAN CanWakeFromD0;
    BOOLEAN CanWakeFromD1;
    BOOLEAN CanWakeFromD2;
    BOOLEAN CanWakeFromD3;
} ACPI_DEVICE_POWER_INFO, *PACPI_DEVICE_POWER_INFO;

//
// ACPI Resource Management Structures (_CRS/_PRS/_SRS)
//
typedef enum _ACPI_RESOURCE_TYPE
{
    AcpiResourceTypeMemory = 0,      // Memory ranges
    AcpiResourceTypeIo = 1,          // I/O port ranges  
    AcpiResourceTypeIrq = 2,         // Interrupt lines
    AcpiResourceTypeDma = 3,         // DMA channels
    AcpiResourceTypeBusNumber = 4,   // Bus number ranges
    AcpiResourceTypeUnknown = 5
} ACPI_RESOURCE_TYPE, *PACPI_RESOURCE_TYPE;

typedef struct _ACPI_RESOURCE_DESCRIPTOR
{
    ACPI_RESOURCE_TYPE Type;
    ULONG Length;                    // Length of this descriptor
    union {
        struct {
            PHYSICAL_ADDRESS BaseAddress;
            ULONG Length;
            BOOLEAN IsWriteable;
            BOOLEAN IsCacheable;
            BOOLEAN IsPrefetchable;
        } Memory;
        
        struct {
            ULONG BasePort;
            ULONG Length;
            BOOLEAN IsDecoded16Bit;
        } Io;
        
        struct {
            ULONG Vector;
            BOOLEAN IsLevelTriggered;
            BOOLEAN IsActiveHigh;
            BOOLEAN IsShared;
        } Irq;
        
        struct {
            ULONG Channel;
            BOOLEAN IsTypeBMaster;
            BOOLEAN IsTransferSize8Bit;
            BOOLEAN IsTransferSize16Bit;
        } Dma;
        
        struct {
            ULONG MinBusNumber;
            ULONG MaxBusNumber;
            ULONG Length;
        } BusNumber;
    } u;
} ACPI_RESOURCE_DESCRIPTOR, *PACPI_RESOURCE_DESCRIPTOR;

typedef struct _ACPI_RESOURCE_LIST
{
    ULONG Count;
    ACPI_RESOURCE_DESCRIPTOR Descriptors[32];  // Maximum 32 resources per device
} ACPI_RESOURCE_LIST, *PACPI_RESOURCE_LIST;

typedef struct _ACPI_DEVICE_RESOURCES
{
    ACPI_RESOURCE_LIST CurrentResources;      // _CRS - Current Resource Settings
    ACPI_RESOURCE_LIST PossibleResources;     // _PRS - Possible Resource Settings  
    BOOLEAN HasCurrentResources;
    BOOLEAN HasPossibleResources;
    BOOLEAN ResourcesAssigned;
} ACPI_DEVICE_RESOURCES, *PACPI_DEVICE_RESOURCES;

/* EOF */
