#ifndef _PCI_PCH_
#define _PCI_PCH_

#include <ntifs.h>
#include <cmreslist.h>
#include <ntstrsafe.h>
#include <ntintsafe.h>
#include <reactos/hal/acpi_pci.h>
#include <ndk/halfuncs.h>
#include <acpiioct.h>
#include <reactos/drivers/acpi/acpipci.h>
#include "pcidef.h"

#define TAG_PCI '0ICP'
#define TAG_PCI_ACPI 'aICP'

EXTERN_C const GUID GUID_REACTOS_PCI_ROOT_BUS_INTERFACE;

typedef struct _PCI_DEVICE
{
    // Entry on device list
    LIST_ENTRY ListEntry;
    // Physical Device Object of device
    PDEVICE_OBJECT Pdo;
    // PCI bus number
    ULONG BusNumber;
    // PCI slot number
    PCI_SLOT_NUMBER SlotNumber;
    // PCI configuration data
    PCI_COMMON_CONFIG PciConfig;
    // Enable memory space
    BOOLEAN EnableMemorySpace;
    // Enable I/O space
    BOOLEAN EnableIoSpace;
    // Enable bus master
    BOOLEAN EnableBusMaster;
    // Whether the device is owned by the KD
    BOOLEAN IsDebuggingDevice;
    // MSI capability offset (0 if none)
    UCHAR MsiCapability;
    // MSI-X capability offset (0 if none)
    UCHAR MsixCapability;
    // Cached MSI control value
    USHORT MsiControl;
    // Cached MSI-X control value
    USHORT MsixControl;
    // Maximum MSI messages supported
    UCHAR MsiMaxCount;
    // MSI-X table size (entries)
    USHORT MsixTableSize;
    // MSI-X table BAR indicator
    UCHAR MsixTableBir;
    // MSI-X PBA BAR indicator
    UCHAR MsixPbaBir;
    // MSI-X table offset within BAR
    ULONG MsixTableOffset;
    // MSI-X PBA offset within BAR
    ULONG MsixPbaOffset;
} PCI_DEVICE, *PPCI_DEVICE;


typedef enum
{
    dsStopped,
    dsStarted,
    dsPaused,
    dsRemoved,
    dsSurpriseRemoved
} PCI_DEVICE_STATE;


typedef struct _COMMON_DEVICE_EXTENSION
{
    // Pointer to device object, this device extension is associated with
    PDEVICE_OBJECT DeviceObject;
    // Wether this device extension is for an FDO or PDO
    BOOLEAN IsFDO;
    // Wether the device is removed
    BOOLEAN Removed;
    // Current device power state for the device
    DEVICE_POWER_STATE DevicePowerState;
} COMMON_DEVICE_EXTENSION, *PCOMMON_DEVICE_EXTENSION;

/* Physical Device Object device extension for a child device */
typedef struct _PDO_DEVICE_EXTENSION
{
    // Common device data
    COMMON_DEVICE_EXTENSION Common;
    // Functional device object
    PDEVICE_OBJECT Fdo;
    // Pointer to PCI Device informations
    PPCI_DEVICE PciDevice;
    // Device ID
    UNICODE_STRING DeviceID;
    // Instance ID
    UNICODE_STRING InstanceID;
    // Hardware IDs
    UNICODE_STRING HardwareIDs;
    // Compatible IDs
    UNICODE_STRING CompatibleIDs;
    // Textual description of device
    UNICODE_STRING DeviceDescription;
    // Textual description of device location
    UNICODE_STRING DeviceLocation;
    // Number of interfaces references
    LONG References;
} PDO_DEVICE_EXTENSION, *PPDO_DEVICE_EXTENSION;

typedef struct _PCI_ROOT_BUS_INTERFACE
{
    INTERFACE Interface;
    ULONG MinBus;
    ULONG MaxBus;
} PCI_ROOT_BUS_INTERFACE, *PPCI_ROOT_BUS_INTERFACE;

/* Functional Device Object device extension for the PCI driver device object */
typedef struct _FDO_DEVICE_EXTENSION
{
    // Common device data
    COMMON_DEVICE_EXTENSION Common;
    // Entry on device list
    LIST_ENTRY ListEntry;
    // PCI bus number serviced by this FDO (legacy single-bus view)
    ULONG BusNumber;
    // PCI segment of this root
    USHORT BusSegment;
    // Lowest bus number owned by this root bridge
    ULONG BusRangeStart;
    // Highest bus number owned by this root bridge
    ULONG BusRangeEnd;
    // Current state of the driver
    PCI_DEVICE_STATE State;
    // Whether MSI/MSI-X is allowed for this root (from ACPI/HAL)
    BOOLEAN MsiSupported;
    // Whether we've logged the _OSC outcome for this root
    BOOLEAN MsiDiagLogged;
    // Cached _OSC status/grant for diagnostics
    ULONG OscStatusFlags;
    ULONG OscControlGranted;
    ULONG OscMasked;
    BOOLEAN MsiMaskLogged;
    // Namespace device list
    LIST_ENTRY DeviceListHead;
    // Number of (not removed) devices in device list
    ULONG DeviceListCount;
    // Lock for namespace device list
    KSPIN_LOCK DeviceListLock;
    // Lower device object
    PDEVICE_OBJECT Ldo;
} FDO_DEVICE_EXTENSION, *PFDO_DEVICE_EXTENSION;

static __inline BOOLEAN
PciIsBusInRange(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ ULONG BusNumber)
{
    if (!FdoExtension)
        return TRUE;

    if (FdoExtension->BusRangeStart <= FdoExtension->BusRangeEnd)
    {
        if (BusNumber < FdoExtension->BusRangeStart ||
            BusNumber > FdoExtension->BusRangeEnd)
            return FALSE;
    }

    return TRUE;
}


/* Driver extension associated with PCI driver */
typedef struct _PCI_DRIVER_EXTENSION
{
    //
    LIST_ENTRY BusListHead;
    // Lock for namespace bus list
    KSPIN_LOCK BusListLock;
} PCI_DRIVER_EXTENSION, *PPCI_DRIVER_EXTENSION;

typedef union _PCI_TYPE1_CFG_CYCLE_BITS
{
    struct
    {
        ULONG InUse:2;
        ULONG RegisterNumber:6;
        ULONG FunctionNumber:3;
        ULONG DeviceNumber:5;
        ULONG BusNumber:8;
        ULONG Reserved2:8;
    };
    ULONG AsULONG;
} PCI_TYPE1_CFG_CYCLE_BITS, *PPCI_TYPE1_CFG_CYCLE_BITS;

/* We need a global variable to get the driver extension,
 * because at least InterfacePciDevicePresent has no
 * other way to get it... */
extern PPCI_DRIVER_EXTENSION DriverExtension;

extern BOOLEAN HasDebuggingDevice;
extern PCI_TYPE1_CFG_CYCLE_BITS PciDebuggingDevice[2];
extern BOOLEAN PciMsiEnabledByPolicy;
extern BOOLEAN PciMsixEnabledByPolicy;

/* ACPI interface for method evaluation (pci.c) */
NTSTATUS
PciAcpiEvalMethod(
    _In_ ULONG Segment,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_ PACPI_EVAL_INPUT_BUFFER InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_opt_(OutputBufferSize) PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_opt_ PULONG BytesReturned);

/* fdo.c */

NTSTATUS
FdoPnpControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);

NTSTATUS
FdoPowerControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);

/* pci.c */

NTSTATUS
PciCreateDeviceIDString(
    PUNICODE_STRING DeviceID,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateInstanceIDString(
    PUNICODE_STRING InstanceID,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateHardwareIDsString(
    PUNICODE_STRING HardwareIDs,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateCompatibleIDsString(
    PUNICODE_STRING HardwareIDs,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateDeviceDescriptionString(
    PUNICODE_STRING DeviceDescription,
    PPCI_DEVICE Device);

NTSTATUS
PciCreateDeviceLocationString(
    PUNICODE_STRING DeviceLocation,
    PPCI_DEVICE Device);

NTSTATUS
PciDuplicateUnicodeString(
    IN ULONG Flags,
    IN PCUNICODE_STRING SourceString,
    OUT PUNICODE_STRING DestinationString);

/* pdo.c */

NTSTATUS
PdoPnpControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);

NTSTATUS
PdoPowerControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp);


CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath);

#endif /* _PCI_PCH_ */
