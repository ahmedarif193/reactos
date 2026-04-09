#ifndef _NTFSLX_H_
#define _NTFSLX_H_

#include <ntifs.h>
#include <ntdddisk.h>

#define NTFSLX_TAG 'xlTN'
#define NTFSLX_DEVICE_NAME L"\\NtfsLx"
#define NTFSLX_DEFAULT_UPCASE_LENGTH 0x10000

#define NTFSLX_RECORD_MAGIC_FILE 0x454C4946UL
#define NTFSLX_RECORD_MAGIC_INDX 0x58444E49UL
#define NTFSLX_RECORD_MAGIC_BAAD 0x44414142UL

#define NTFSLX_MFT_RECORD_IN_USE 0x0001
#define NTFSLX_CASE_SENSITIVE 0
#define NTFSLX_IGNORE_CASE 1

#define ROUND_UP(N, S) ((((N) + (S) - 1) / (S)) * (S))
#define ROUND_DOWN(N, S) ((N) - ((N) % (S)))

#include <pshpack1.h>
typedef struct _NTFSLX_BIOS_PARAMETER_BLOCK
{
    USHORT BytesPerSector;
    UCHAR SectorsPerCluster;
    USHORT ReservedSectors;
    UCHAR Fats;
    USHORT RootEntries;
    USHORT Sectors;
    UCHAR MediaType;
    USHORT SectorsPerFat;
    USHORT SectorsPerTrack;
    USHORT Heads;
    ULONG HiddenSectors;
    ULONG LargeSectors;
} NTFSLX_BIOS_PARAMETER_BLOCK, *PNTFSLX_BIOS_PARAMETER_BLOCK;

typedef struct _NTFSLX_BOOT_SECTOR
{
    UCHAR Jump[3];
    ULONGLONG OemId;
    NTFSLX_BIOS_PARAMETER_BLOCK Bpb;
    UCHAR Unused[4];
    ULONGLONG SectorCount;
    ULONGLONG MftLcn;
    ULONGLONG MftMirrLcn;
    CHAR ClustersPerMftRecord;
    UCHAR Reserved0[3];
    CHAR ClustersPerIndexRecord;
    UCHAR Reserved1[3];
    ULONGLONG SerialNumber;
    ULONG Checksum;
    UCHAR BootStrap[426];
    USHORT EndOfSectorMarker;
} NTFSLX_BOOT_SECTOR, *PNTFSLX_BOOT_SECTOR;

typedef struct _NTFSLX_RECORD_HEADER
{
    ULONG Magic;
    USHORT UsaOffset;
    USHORT UsaCount;
} NTFSLX_RECORD_HEADER, *PNTFSLX_RECORD_HEADER;

typedef struct _NTFSLX_MFT_RECORD
{
    NTFSLX_RECORD_HEADER Ntfs;
    ULONGLONG Lsn;
    USHORT SequenceNumber;
    USHORT LinkCount;
    USHORT AttributesOffset;
    USHORT Flags;
    ULONG BytesInUse;
    ULONG BytesAllocated;
    ULONGLONG BaseMftRecord;
    USHORT NextAttributeInstance;
    USHORT Reserved;
    ULONG MftRecordNumber;
} NTFSLX_MFT_RECORD, *PNTFSLX_MFT_RECORD;
#include <poppack.h>

C_ASSERT(sizeof(NTFSLX_BOOT_SECTOR) == 512);
C_ASSERT(sizeof(NTFSLX_RECORD_HEADER) == 8);
C_ASSERT(sizeof(NTFSLX_MFT_RECORD) == 48);

typedef struct _NTFSLX_VOLUME_INFO
{
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG BytesPerCluster;
    ULONG BytesPerFileRecord;
    ULONG BytesPerIndexRecord;
    ULONGLONG SectorCount;
    ULONGLONG ClusterCount;
    ULONGLONG MftLcn;
    ULONGLONG MftMirrLcn;
    ULONGLONG SerialNumber;
} NTFSLX_VOLUME_INFO, *PNTFSLX_VOLUME_INFO;

typedef enum _NTFSLX_DEVICE_KIND
{
    NtfslxDeviceKindControl = 1,
    NtfslxDeviceKindVolume
} NTFSLX_DEVICE_KIND;

typedef struct _NTFSLX_DEVICE_EXTENSION
{
    ULONG Signature;
    NTFSLX_DEVICE_KIND Kind;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT StorageDevice;
    PVPB Vpb;
    ERESOURCE Resource;
    NTFSLX_VOLUME_INFO VolumeInfo;
    PUSHORT UpcaseTable;
    ULONG UpcaseTableLength;
} NTFSLX_DEVICE_EXTENSION, *PNTFSLX_DEVICE_EXTENSION;

typedef struct _NTFSLX_GLOBAL_DATA
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT ControlDeviceObject;
} NTFSLX_GLOBAL_DATA, *PNTFSLX_GLOBAL_DATA;

extern NTFSLX_GLOBAL_DATA NtfslxGlobalData;

static __inline BOOLEAN
NtfslxIsPowerOfTwo(_In_ ULONG Value)
{
    return (Value != 0) && ((Value & (Value - 1)) == 0);
}

static __inline BOOLEAN
NtfslxIsControlDevice(_In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    return DeviceExtension->Kind == NtfslxDeviceKindControl;
}

static __inline BOOLEAN
NtfslxIsVolumeDevice(_In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    return DeviceExtension->Kind == NtfslxDeviceKindVolume;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

VOID
NtfslxInitializeFunctionPointers(
    _In_ PDRIVER_OBJECT DriverObject);

NTSTATUS
NTAPI
NtfslxFsdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
NtfslxFileSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
NtfslxMountVolume(
    _In_ PDEVICE_OBJECT ControlDevice,
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PVPB Vpb);

NTSTATUS
NtfslxProbeVolume(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Out_ PNTFSLX_VOLUME_INFO VolumeInfo);

NTSTATUS
NtfslxDeviceIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(*OutputBufferSize, *OutputBufferSize) PVOID OutputBuffer,
    _Inout_opt_ PULONG OutputBufferSize,
    _In_ BOOLEAN Override);

NTSTATUS
NtfslxReadDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ LONGLONG StartingOffset,
    _In_ ULONG Length,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ BOOLEAN Override);

ULONG
NtfslxRecordSizeFromClusters(
    _In_ CHAR ClustersPerRecord,
    _In_ ULONG ClusterSize);

PUSHORT
NtfslxGenerateDefaultUpcase(
    VOID);

NTSTATUS
NtfslxPostReadMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record,
    _In_ ULONG Size);

int
NtfslxUcsNCompare(
    _In_reads_(Length) const USHORT *String1,
    _In_reads_(Length) const USHORT *String2,
    _In_ SIZE_T Length);

int
NtfslxUcsNCaseCompare(
    _In_reads_(Length) const USHORT *String1,
    _In_reads_(Length) const USHORT *String2,
    _In_ SIZE_T Length,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength);

BOOLEAN
NtfslxAreNamesEqual(
    _In_reads_(String1Length) const USHORT *String1,
    _In_ SIZE_T String1Length,
    _In_reads_(String2Length) const USHORT *String2,
    _In_ SIZE_T String2Length,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength);

LONG
NtfslxCollateNames(
    _In_reads_(Name1Length) const USHORT *Name1,
    _In_ ULONG Name1Length,
    _In_reads_(Name2Length) const USHORT *Name2,
    _In_ ULONG Name2Length,
    _In_ LONG InvalidCharReturn,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength);

#endif /* _NTFSLX_H_ */
