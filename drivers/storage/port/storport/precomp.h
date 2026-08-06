/*
 * PROJECT:     ReactOS Storport Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Storport driver common header file
 * COPYRIGHT:   Copyright 2017 Eric Kohl (eric.kohl@reactos.org)
 */

#ifndef _STORPORT_PCH_
#define _STORPORT_PCH_

#include <wdm.h>
#include <ntddk.h>
#include <stdio.h>
#include <memory.h>

/* Declare STORPORT_API functions as exports rather than imports */
#define _STORPORT_
#include <storport.h>

#include <ntddscsi.h>
#include <ntdddisk.h>
#include <mountdev.h>
#include <wdmguid.h>
#include <reactos/drivers/dumpstor.h>
#include <reactos/drivers/dumpscsi.h>

/* Memory Tags */
#define TAG_GLOBAL_DATA     'DGtS'
#define TAG_INIT_DATA       'DItS'
#define TAG_MINIPORT_DATA   'DMtS'
#define TAG_ACCRESS_RANGE   'RAtS'
#define TAG_RESOURCE_LIST   'LRtS'
#define TAG_ADDRESS_MAPPING 'MAtS'
#define TAG_INQUIRY_DATA    'QItS'
#define TAG_SENSE_DATA      'NStS'
#define TAG_SRB_CONTEXT     'CStS'
#define TAG_SRB_EXTENSION   'EStS'
#define TAG_SGL             'GStS'
#define TAG_DEVICE_RELATION 'RDtS'
#define TAG_PNP_ID          'IPtS'
#define TAG_DUMP_CONTEXT    'CptS'

typedef enum
{
    dsStopped,
    dsStarted,
    dsPaused,
    dsRemoved,
    dsSurpriseRemoved
} DEVICE_STATE;

typedef enum
{
    InvalidExtension = 0,
    DriverExtension,
    FdoExtension,
    PdoExtension
} EXTENSION_TYPE;

typedef struct _DRIVER_INIT_DATA
{
    LIST_ENTRY Entry;
    HW_INITIALIZATION_DATA HwInitData;
} DRIVER_INIT_DATA, *PDRIVER_INIT_DATA;

typedef struct _DRIVER_OBJECT_EXTENSION
{
    EXTENSION_TYPE ExtensionType;
    PDRIVER_OBJECT DriverObject;

    KSPIN_LOCK AdapterListLock;
    LIST_ENTRY AdapterListHead;
    ULONG AdapterCount;

    LIST_ENTRY InitDataListHead;
} DRIVER_OBJECT_EXTENSION, *PDRIVER_OBJECT_EXTENSION;

typedef struct _MINIPORT_DEVICE_EXTENSION
{
    struct _MINIPORT *Miniport;
    UCHAR HwDeviceExtension[0];
} MINIPORT_DEVICE_EXTENSION, *PMINIPORT_DEVICE_EXTENSION;

typedef struct _MINIPORT
{
    struct _FDO_DEVICE_EXTENSION *DeviceExtension;
    PHW_INITIALIZATION_DATA InitData;
    PORT_CONFIGURATION_INFORMATION PortConfig;
    PMINIPORT_DEVICE_EXTENSION MiniportExtension;
} MINIPORT, *PMINIPORT;

typedef struct _UNIT_DATA
{
    LIST_ENTRY ListEntry;
    INQUIRYDATA InquiryData;
} UNIT_DATA, *PUNIT_DATA;

typedef struct _PORT_HMB_BLOCK
{
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS Physical;
    SIZE_T Bytes;
} PORT_HMB_BLOCK, *PPORT_HMB_BLOCK;

typedef struct _FDO_DEVICE_EXTENSION
{
    EXTENSION_TYPE ExtensionType;

    PDEVICE_OBJECT Device;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    PDRIVER_OBJECT_EXTENSION DriverExtension;
    DEVICE_STATE PnpState;
    LIST_ENTRY AdapterListEntry;
    MINIPORT Miniport;
    ULONG PortNumber;
    ULONG BusNumber;
    ULONG SlotNumber;
    PCM_RESOURCE_LIST AllocatedResources;
    PCM_RESOURCE_LIST TranslatedResources;
    BUS_INTERFACE_STANDARD BusInterface;
    BOOLEAN BusInitialized;
    PMAPPED_ADDRESS MappedAddressList;
    PVOID UncachedExtensionVirtualBase;
    PHYSICAL_ADDRESS UncachedExtensionPhysicalBase;
    ULONG UncachedExtensionSize;
    PHW_PASSIVE_INITIALIZE_ROUTINE HwPassiveInitRoutine;
    PKINTERRUPT Interrupt;
    ULONG InterruptIrql;
    /* Non-NULL when the adapter is connected message-based; each entry owns
     * the KINTERRUPT whose lock serializes that message. */
    PIO_INTERRUPT_MESSAGE_INFO MessageInfo;
    ULONG PerfFlags;
    ULONG PerfConcurrentChannels;
    BOOLEAN PerfConfigured;

    /*
     * Per-request state comes from lookasides once the adapter is up: the
     * SRB extension must stay physically contiguous, so its list allocates
     * with MmAllocateContiguousMemory underneath.
     */
    NPAGED_LOOKASIDE_LIST SrbContextLookaside;
    NPAGED_LOOKASIDE_LIST MiniportSrbLookaside;
    NPAGED_LOOKASIDE_LIST SrbExtensionLookaside;
    NPAGED_LOOKASIDE_LIST SglLookaside;
    ULONG SglLookasideSize;
    BOOLEAN RequestPoolsReady;

    /* Host memory buffer blocks lent to the adapter (StorPortAllocateHostMemoryBuffer). */
#define PORT_HMB_MAX_BLOCKS 8
    PORT_HMB_BLOCK HmbBlocks[PORT_HMB_MAX_BLOCKS];
    ULONG HmbBlockCount;

    /*
     * Legacy miniports get one port-owned timer through
     * StorPortNotification(RequestTimerCall).
     */
    KTIMER MiniportTimer;
    KDPC MiniportTimerDpc;
    KDPC MiniportTimerRequestDpc;
    KSPIN_LOCK MiniportTimerLock;
    PHW_TIMER MiniportTimerRoutine;
    PHW_TIMER MiniportTimerRequestedRoutine;
    ULONG MiniportTimerRequestedValue;
    BOOLEAN MiniportTimerArmed;
    BOOLEAN MiniportTimerRequestPending;
    BOOLEAN MiniportTimerRequestDpcActive;

    /* Shared backing lock for StorPortAcquireSpinLockEx miniport locks */
    KSPIN_LOCK MiniportExLock;

    KSPIN_LOCK PdoListLock;
    LIST_ENTRY PdoListHead;
    ULONG PdoCount;
    BOOLEAN BusScanned;
    volatile BOOLEAN DumpMode;
} FDO_DEVICE_EXTENSION, *PFDO_DEVICE_EXTENSION;


typedef struct _PDO_DEVICE_EXTENSION
{
    EXTENSION_TYPE ExtensionType;

    PDEVICE_OBJECT Device;
    PFDO_DEVICE_EXTENSION FdoExtension;
    DEVICE_STATE PnpState;
    LIST_ENTRY PdoListEntry;

    ULONG Bus;
    ULONG Target;
    ULONG Lun;
    ULONG QueueDepth;
    BOOLEAN DeviceClaimed;
    PINQUIRYDATA InquiryBuffer;
    struct _PORT_DUMP_CONTEXT *DumpContext;

} PDO_DEVICE_EXTENSION, *PPDO_DEVICE_EXTENSION;


/*
 * Per-request state owned by the port driver for the lifetime of one SRB.
 * It is anchored in the IRP so the completion path (which only receives the
 * SRB) can find and release it again.
 */
typedef struct _STOR_SRB_CONTEXT
{
    PVOID SrbExtensionAllocation;
    PSTORAGE_REQUEST_BLOCK MiniportSrb;
    PSCSI_REQUEST_BLOCK LegacySrb;
    PSTOR_SCATTER_GATHER_LIST Sgl;
    ULONG SglAllocationSize;
    ULONG SrbExtensionSize;
    struct _FDO_DEVICE_EXTENSION* FdoExtension;
} STOR_SRB_CONTEXT, *PSTOR_SRB_CONTEXT;

#define PortGetSrbContext(Irp)  ((PSTOR_SRB_CONTEXT)((Irp)->Tail.Overlay.DriverContext[0]))
#define PORT_DUMP_IRP_MARKER     ((PVOID)(ULONG_PTR)'pruD')

FORCEINLINE BOOLEAN PortIsExtendedSrb(_In_opt_ PVOID Srb)
{
    return Srb != NULL && ((PSTORAGE_REQUEST_BLOCK_HEADER)Srb)->Function == SRB_FUNCTION_STORAGE_REQUEST_BLOCK;
}

FORCEINLINE PIRP PortGetOriginalRequestFromSrb(_In_ PVOID Srb)
{
    return (PIRP)(PortIsExtendedSrb(Srb) ? ((PSTORAGE_REQUEST_BLOCK)Srb)->OriginalRequest : ((PSCSI_REQUEST_BLOCK)Srb)->OriginalRequest);
}

/*
 * STOR_SCATTER_GATHER_LIST ends in a flexible array, so it cannot be embedded
 * by value. This mirrors it with room for the single element a dump write
 * needs; the miniport reads it back through the real type, so the layout must
 * stay identical.
 */
typedef struct _PORT_DUMP_SGL
{
    ULONG NumberOfElements;
    ULONG_PTR Reserved;
    STOR_SCATTER_GATHER_ELEMENT List[1];
} PORT_DUMP_SGL, *PPORT_DUMP_SGL;

C_ASSERT(FIELD_OFFSET(PORT_DUMP_SGL, NumberOfElements) == FIELD_OFFSET(STOR_SCATTER_GATHER_LIST, NumberOfElements));
C_ASSERT(FIELD_OFFSET(PORT_DUMP_SGL, Reserved) == FIELD_OFFSET(STOR_SCATTER_GATHER_LIST, Reserved));
C_ASSERT(FIELD_OFFSET(PORT_DUMP_SGL, List) == FIELD_OFFSET(STOR_SCATTER_GATHER_LIST, List));

typedef struct _PORT_DUMP_CONTEXT
{
    PPDO_DEVICE_EXTENSION PdoExtension;
    PIRP Irp;
    SCSI_REQUEST_BLOCK Srb;
    STOR_SRB_CONTEXT SrbContext;
    PORT_DUMP_SGL Sgl;
    PVOID SrbExtension;
    volatile LONG Completed;
    NTSTATUS Status;
    ULONG BytesPerSector;
} PORT_DUMP_CONTEXT, *PPORT_DUMP_CONTEXT;

/* pdo.c */

VOID PortFreeSrbContext(_In_ PIRP Irp);

VOID PortFdoInitializeRequestPools(_In_ PFDO_DEVICE_EXTENSION FdoExtension);

NTSTATUS PortSrbStatusToNtStatus(_In_ UCHAR SrbStatus);

VOID PortFreeDumpContext(_In_opt_ PPORT_DUMP_CONTEXT DumpContext);

NTSTATUS PortGetDumpInterface(_In_ PPDO_DEVICE_EXTENSION PdoExtension, _Out_ PROS_STORAGE_DUMP_INTERFACE Interface);

/* fdo.c */

NTSTATUS
NTAPI
PortFdoScsi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);

NTSTATUS
NTAPI
PortFdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);


/* miniport.c */

NTSTATUS
MiniportInitialize(
    _In_ PMINIPORT Miniport,
    _In_ PFDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PHW_INITIALIZATION_DATA HwInitializationData);

NTSTATUS
MiniportFindAdapter(
    _In_ PMINIPORT Miniport);

NTSTATUS
MiniportAdapterControlPreFind(
    _In_ PMINIPORT Miniport,
    _Inout_ PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList);

NTSTATUS
MiniportHwInitialize(
    _In_ PMINIPORT Miniport);

BOOLEAN
MiniportHwMSInterrupt(
    _In_ PMINIPORT Miniport,
    _In_ ULONG MessageId);

BOOLEAN
MiniportHwInterrupt(
    _In_ PMINIPORT Miniport);

BOOLEAN
MiniportBuildIo(
    _In_ PMINIPORT Miniport,
    _In_ PSCSI_REQUEST_BLOCK Srb);

BOOLEAN
MiniportStartIo(
    _In_ PMINIPORT Miniport,
    _In_ PSCSI_REQUEST_BLOCK Srb);

/* misc.c */

NTSTATUS
NTAPI
ForwardIrpAndForget(
    _In_ PDEVICE_OBJECT LowerDevice,
    _In_ PIRP Irp);

INTERFACE_TYPE
GetBusInterface(
    PDEVICE_OBJECT DeviceObject);

PCM_RESOURCE_LIST
CopyResourceList(
    POOL_TYPE PoolType,
    PCM_RESOURCE_LIST Source);

NTSTATUS
QueryBusInterface(
    PDEVICE_OBJECT DeviceObject,
    PGUID Guid,
    USHORT Size,
    USHORT Version,
    PBUS_INTERFACE_STANDARD Interface,
    PVOID InterfaceSpecificData);

BOOLEAN
TranslateResourceListAddress(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    INTERFACE_TYPE BusType,
    ULONG SystemIoBusNumber,
    STOR_PHYSICAL_ADDRESS IoAddress,
    ULONG NumberOfBytes,
    BOOLEAN InIoSpace,
    PPHYSICAL_ADDRESS TranslatedAddress);

NTSTATUS
GetResourceListInterrupt(
    PFDO_DEVICE_EXTENSION DeviceExtension,
    PULONG Vector,
    PKIRQL Irql,
    KINTERRUPT_MODE *InterruptMode,
    PBOOLEAN ShareVector,
    PKAFFINITY Affinity);

NTSTATUS
AllocateAddressMapping(
    PMAPPED_ADDRESS *MappedAddressList,
    STOR_PHYSICAL_ADDRESS IoAddress,
    PVOID MappedAddress,
    ULONG NumberOfBytes,
    ULONG BusNumber);

/* pdo.c */

NTSTATUS
PortCreatePdo(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ ULONG Bus,
    _In_ ULONG Target,
    _In_ ULONG Lun,
    _Out_ PPDO_DEVICE_EXTENSION *PdoExtension);

NTSTATUS
PortDeletePdo(
    _In_ PPDO_DEVICE_EXTENSION PdoExtension);

NTSTATUS
NTAPI
PortPdoScsi(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);

NTSTATUS
NTAPI
PortPdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp);


/* storport.c */

PHW_INITIALIZATION_DATA
PortGetDriverInitData(
    PDRIVER_OBJECT_EXTENSION DriverExtension,
    INTERFACE_TYPE InterfaceType);

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

#endif /* _STORPORT_PCH_ */
