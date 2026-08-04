#ifndef _PCI_PCH_
#define _PCI_PCH_

#include <ntifs.h>
#include <ndk/rtlfuncs.h>
#include <cmreslist.h>
#include <ntstrsafe.h>
#include <ntintsafe.h>
#include <reactos/hal/acpi_pci.h>
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
    // Power Management capability offset (0 if none)
    UCHAR PmCapability;
    // Whether this device has an interrupt connected
    BOOLEAN InterruptConnected;
    // Whether MSI has been allocated/enabled for this device
    BOOLEAN MsiAllocated;
    // Number of MSI messages currently allocated
    ULONG MsiMessageCount;
} PCI_DEVICE, *PPCI_DEVICE;


typedef enum
{
    PciNotStarted = 0,
    PciStarted = 1,
    PciDeleted = 2,
    PciStopped = 3,
    PciSurpriseRemoved = 4,
    PciMaxObjectState = 6
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
    // Remove lock for PnP IRP serialization
    IO_REMOVE_LOCK RemoveLock;
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
    // Wait/Wake IRP pending on this PDO
    PIRP WaitWakeIrp;
    // Wait/Wake state machine: 0=idle, 1=requesting, 2=armed, 3=cancelling, 4=completing
    LONG WaitWakeState;
    // Spinlock protecting WaitWakeIrp and WaitWakeState
    KSPIN_LOCK WaitWakeSpinLock;
} PDO_DEVICE_EXTENSION, *PPDO_DEVICE_EXTENSION;

typedef struct _PCI_ROOT_BUS_INTERFACE
{
    INTERFACE Interface;
    ULONG MinBus;
    ULONG MaxBus;
} PCI_ROOT_BUS_INTERFACE, *PPCI_ROOT_BUS_INTERFACE;

#define PCI_MAX_ADDRESS_WINDOWS 8

typedef struct _PCI_ADDRESS_WINDOW
{
    ULONGLONG Start;
    ULONGLONG End;
    BOOLEAN Prefetchable;
} PCI_ADDRESS_WINDOW, *PPCI_ADDRESS_WINDOW;

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
    // Address apertures decoded by this bus, supplied by ACPI or its parent bridge
    ULONG IoWindowCount;
    PCI_ADDRESS_WINDOW IoWindows[PCI_MAX_ADDRESS_WINDOWS];
    ULONG MemoryWindowCount;
    PCI_ADDRESS_WINDOW MemoryWindows[PCI_MAX_ADDRESS_WINDOWS];
    // Remove lock for PnP IRP serialization
    IO_REMOVE_LOCK RemoveLock;
    // Current state of the driver
    PCI_DEVICE_STATE State;
    // Previous PnP state (for state transition tracking)
    PCI_DEVICE_STATE PreviousState;
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
    // Resource arbiter list (I/O, Memory, BusNumber)
    LIST_ENTRY ArbiterListHead;
    // System power state tracking
    SYSTEM_POWER_STATE SystemPowerState;
    // System-to-Device power state mapping table
    DEVICE_POWER_STATE StoD_PowerMapping[PowerSystemMaximum];
    // Wait/Wake support
    PIRP WaitWakeIrp;
    KSPIN_LOCK WaitWakeSpinLock;
    LONG WaitWakeChildCount;
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

/* Global driver state (pci.c) */
extern UNICODE_STRING PciRegistryPath;
extern KEVENT PciGlobalLock;
extern BOOLEAN PciLockDeviceResources;
extern LIST_ENTRY PciFdoExtensionListHead;
extern KSPIN_LOCK PciGlobalSpinLock;
extern BOOLEAN PciEnableNativeModeATA;

/* hookhal.c - saved HAL pointers for restore on unload */
extern pHalTranslateBusAddress PcipSavedTranslateBusAddress;
extern pHalAssignSlotResources PcipSavedAssignSlotResources;

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

/* pci.c - State transition helpers */

NTSTATUS
PciBeginStateTransition(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCI_DEVICE_STATE NewState);

VOID
PciCommitStateTransition(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCI_DEVICE_STATE NewState);

NTSTATUS
PciCancelStateTransition(
    _Inout_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCI_DEVICE_STATE NewState);

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

VOID
PciSetPowerLevel(
    _In_ PPCI_DEVICE Device,
    _In_ DEVICE_POWER_STATE DeviceState);

DEVICE_POWER_STATE
PciGetPowerLevel(
    _In_ PPCI_DEVICE Device);

VOID
PciSaveDeviceConfig(
    _In_ PPCI_DEVICE Device);

VOID
PciRestoreDeviceConfig(
    _In_ PPCI_DEVICE Device);


/* config.c */

typedef NTSTATUS
(NTAPI *PPCI_IPI_FUNCTION)(
    _In_ PVOID Context,
    _In_ PVOID Parameter2);

BOOLEAN
PciIsDeviceDebugging(
    _In_ ULONG BusNumber,
    _In_ PCI_SLOT_NUMBER SlotNumber);

ULONG
PciReadDeviceConfig(
    _In_ PPCI_DEVICE Device,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length);

ULONG
PciWriteDeviceConfig(
    _In_ PPCI_DEVICE Device,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length);

NTSTATUS
PciExecuteIpiConfig(
    _In_ PPCI_IPI_FUNCTION FunctionToCall,
    _In_ PVOID Context,
    _In_opt_ PVOID Parameter2);

NTSTATUS
NTAPI
PciDecodeEnable(
    _In_ PVOID Context,
    _In_ PVOID Parameter2);

/* arb.c */

NTSTATUS
PciCreateFdoArbiters(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension);

VOID
PciDestroyFdoArbiters(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension);

NTSTATUS
PciQueryArbiterInterface(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _Out_ PARBITER_INTERFACE Interface);

NTSTATUS
NTAPI
PciArbiterHandler(
    _Inout_opt_ PVOID Context,
    _In_ ARBITER_ACTION Action,
    _Inout_ PARBITER_PARAMETERS Parameters);

/* hookhal.c */

VOID
NTAPI
PciHookHal(VOID);

VOID
NTAPI
PciUnhookHal(VOID);

/* intrconn.c */

NTSTATUS
PciProgramMsiMessages(
    _In_ PPCI_DEVICE Device,
    _In_ PHYSICAL_ADDRESS MessageAddress,
    _In_ USHORT MessageData,
    _In_ ULONG MessageCount);

NTSTATUS
PciEnableMsi(
    _In_ PPCI_DEVICE Device);

NTSTATUS
PciDisableMsi(
    _In_ PPCI_DEVICE Device);

NTSTATUS
PciConnectLegacyInterrupt(
    _In_ PPCI_DEVICE Device);

VOID
PciDisconnectInterrupt(
    _In_ PPCI_DEVICE Device);

CODE_SEG("INIT")
NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath);

#endif /* _PCI_PCH_ */
