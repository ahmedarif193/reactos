/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Private definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _FLTMGR_H
#define _FLTMGR_H

#include <ntifs.h>
#include <ndk/obfuncs.h>
#include <ndk/exfuncs.h>
#include <ndk/iofuncs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/psfuncs.h>
#define FLT_SKIP_PUSH_LOCK_REDIRECTION
#include <fltkernel.h>
#include <pseh/pseh2.h>

#include <fltmgr_shared.h>

#define FLT_TAG_FILTER       'iFlF'
#define FLT_TAG_VOLUME       'oVlF'
#define FLT_TAG_INSTANCE     'nIlF'
#define FLT_TAG_IRP_CTRL     'cIlF'
#define FLT_TAG_NAME         'mNlF'
#define FLT_TAG_CONTEXT      'xClF'
#define FLT_TAG_GENERAL      'eGlF'
#define FLT_TAG_PORT         'oPlF'
#define FLT_TAG_WORK         'kWlF'

#define FLT_SIGNATURE_FILTER   'tliF'
#define FLT_SIGNATURE_VOLUME   'loVF'
#define FLT_SIGNATURE_INSTANCE 'tsnI'

#define FLT_OPERATION_COUNT (IRP_MJ_MAXIMUM_FUNCTION + 1 + FLT_INTERNAL_OPERATION_COUNT)
#define FLT_CONTEXT_TYPE_COUNT 7
#define FLT_CONTEXT_REGISTRATIONS_PER_TYPE 3

typedef struct _FLT_OBJECT
{
    ULONG Signature;
    volatile LONG PointerCount;
    EX_RUNDOWN_REF RundownRef;
    LIST_ENTRY PrimaryLink;
} FLT_OBJECT, *PFLT_OBJECT;

typedef struct _FLT_CONTEXT_TYPE_INFO
{
    BOOLEAN Registered;
    BOOLEAN VariableSize;
    USHORT Flags;
    SIZE_T Size;
    ULONG PoolTag;
    PFLT_CONTEXT_CLEANUP_CALLBACK CleanupCallback;
    PFLT_CONTEXT_ALLOCATE_CALLBACK AllocateCallback;
    PFLT_CONTEXT_FREE_CALLBACK FreeCallback;
} FLT_CONTEXT_TYPE_INFO, *PFLT_CONTEXT_TYPE_INFO;

typedef struct _FLT_FILTER
{
    FLT_OBJECT Base;
    UNICODE_STRING Name;
    UNICODE_STRING DefaultAltitude;
    PDRIVER_OBJECT DriverObject;
    PDRIVER_UNLOAD OldDriverUnload;
    ULONG Flags;
    FLT_REGISTRATION_FLAGS RegistrationFlags;
    PFLT_FILTER_UNLOAD_CALLBACK FilterUnload;
    PFLT_INSTANCE_SETUP_CALLBACK InstanceSetup;
    PFLT_INSTANCE_QUERY_TEARDOWN_CALLBACK InstanceQueryTeardown;
    PFLT_INSTANCE_TEARDOWN_CALLBACK InstanceTeardownStart;
    PFLT_INSTANCE_TEARDOWN_CALLBACK InstanceTeardownComplete;
    PFLT_GENERATE_FILE_NAME GenerateFileName;
    PFLT_NORMALIZE_NAME_COMPONENT NormalizeNameComponent;
    PFLT_NORMALIZE_NAME_COMPONENT_EX NormalizeNameComponentEx;
    PFLT_NORMALIZE_CONTEXT_CLEANUP NormalizeContextCleanup;
    PFLT_TRANSACTION_NOTIFICATION_CALLBACK TransactionNotification;
    PFLT_SECTION_CONFLICT_NOTIFICATION_CALLBACK SectionNotification;
    FLT_CONTEXT_TYPE_INFO Contexts[FLT_CONTEXT_TYPE_COUNT][FLT_CONTEXT_REGISTRATIONS_PER_TYPE];
    EX_PUSH_LOCK ContextLock;
    LIST_ENTRY ContextList;
    FLT_OPERATION_REGISTRATION Operations[FLT_OPERATION_COUNT];
    LIST_ENTRY InstanceList;
    LIST_ENTRY PortList;
    FAST_MUTEX PortLock;
    volatile LONG ClientPortCount;
} FLT_FILTER;

#define FLTP_FILTER_STARTED      0x00000001
#define FLTP_FILTER_UNREGISTERING 0x00000002

#define FLTP_INSTANCE_FLAG_NO_AUTOMATIC 0x00000001
#define FLTP_INSTANCE_FLAG_NO_MANUAL    0x00000002

typedef struct _FLT_VOLUME
{
    FLT_OBJECT Base;
    ULONG Flags;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT AttachedToDeviceObject;
    PDEVICE_OBJECT BaseDeviceObject;
    PDEVICE_OBJECT DiskDeviceObject;
    FLT_FILESYSTEM_TYPE FileSystemType;
    DEVICE_TYPE DeviceType;
    UNICODE_STRING DeviceName;
    UNICODE_STRING GuidName;
    UNICODE_STRING FileSystemDriverName;
    EX_PUSH_LOCK InstanceLock;
    LIST_ENTRY InstanceList;
    ULONG InstanceCount;
    KSPIN_LOCK ActiveLock;
    LIST_ENTRY ActiveList;
    EX_PUSH_LOCK ContextLock;
    LIST_ENTRY ContextList;
} FLT_VOLUME;

#define FLTP_VOLUME_DISMOUNTED    0x00000001
#define FLTP_VOLUME_NETWORK       0x00000002
#define FLTP_VOLUME_SETUP_PENDING 0x00000004

typedef struct _FLT_INSTANCE
{
    FLT_OBJECT Base;
    PFLT_VOLUME Volume;
    PFLT_FILTER Filter;
    ULONG Flags;
    UNICODE_STRING Altitude;
    UNICODE_STRING Name;
    LIST_ENTRY VolumeLink;
    LIST_ENTRY FilterLink;
    LIST_ENTRY ContextList;
    LIST_ENTRY OwnContextList;
} FLT_INSTANCE;

#define FLTP_INSTANCE_DETACHING    0x00000001
#define FLTP_INSTANCE_INITIALIZING 0x00000002

typedef struct _FLTP_DEVICE_EXTENSION
{
    ULONG Signature;
    PDEVICE_OBJECT AttachedToDeviceObject;
    PDEVICE_OBJECT StorageStackDeviceObject;
    PFLT_VOLUME Volume;
    BOOLEAN FileSystemControlDevice;
} FLTP_DEVICE_EXTENSION, *PFLTP_DEVICE_EXTENSION;

#define FLTP_DEVICE_EXTENSION_SIGNATURE 'xDlF'

typedef struct _FLTP_STATUS_CALLBACK
{
    struct _FLTP_STATUS_CALLBACK *Next;
    PFLT_INSTANCE Instance;
    PFLT_GET_OPERATION_STATUS_CALLBACK Callback;
    PVOID RequesterContext;
    FLT_IO_PARAMETER_BLOCK Iopb;
} FLTP_STATUS_CALLBACK, *PFLTP_STATUS_CALLBACK;

typedef struct _FLTP_COMPLETION_NODE
{
    PFLT_INSTANCE Instance;
    PFLT_OPERATION_REGISTRATION Operation;
    PVOID CompletionContext;
    FLT_PARAMETERS SavedParameters;
    PFILE_OBJECT SavedTargetFileObject;
    PMDL PostMdl;
    volatile LONG State;
    BOOLEAN Synchronize;
    BOOLEAN ParametersSaved;
    BOOLEAN Swapped;
} FLTP_COMPLETION_NODE, *PFLTP_COMPLETION_NODE;

#define FLTP_NODE_IDLE      0
#define FLTP_NODE_POST      1
#define FLTP_NODE_DONE      2
#define FLTP_NODE_PENDED    3

#define FLTP_PEND_NONE      0
#define FLTP_PEND_WAITING   1
#define FLTP_PEND_RESUME    2
#define FLTP_PEND_CALLING   3

typedef struct _FLTP_IRP_CTRL
{
    ULONG Signature;
    volatile LONG ReferenceCount;
    FLT_CALLBACK_DATA Data;
    FLT_IO_PARAMETER_BLOCK Iopb;
    LIST_ENTRY ActiveLink;
    PFLT_VOLUME Volume;
    PFLT_INSTANCE StartInstance;
    PIRP Irp;
    PDEVICE_OBJECT DeviceObject;
    ULONG Flags;
    ULONG NodeCount;
    ULONG NextNode;
    LONG PostNode;
    volatile LONG PendState;
    volatile LONG PostPendState;
    FLT_PREOP_CALLBACK_STATUS ResumeStatus;
    PVOID ResumeContext;
    PFLT_POST_OPERATION_CALLBACK SafePostCallback;
    PFLTP_STATUS_CALLBACK StatusCallbacks;
    KEVENT SyncEvent;
    NTSTATUS LowerStatus;
    PMDL OriginalMdl;
    PVOID OriginalUserBuffer;
    PVOID OriginalSystemBuffer;
    PVOID SentSystemBuffer;
    PVOID NewSystemBuffer;
    PMDL LowerMdl;
    PMDL PostMdls;
    PFLTP_COMPLETION_NODE CurrentNode;
    BOOLEAN RetainMdl;
    WORK_QUEUE_ITEM WorkItem;
    PFLT_INSTANCE Initiator;
    PFILE_OBJECT GeneratedFileObject;
    LIST_ENTRY Names;
    PFLT_COMPLETED_ASYNC_IO_CALLBACK AsyncCallback;
    PVOID AsyncContext;
    KEVENT GeneratedEvent;
    PMDL CallerMdl;
    PFLT_COMPLETE_CANCELED_CALLBACK CanceledCallback;
    PFLTP_COMPLETION_NODE Nodes;
} FLTP_IRP_CTRL, *PFLTP_IRP_CTRL;

#define FLTP_IRP_CTRL_SIGNATURE 'CIlF'

#define FLTP_IRPCTRL_SYNCHRONIZE     0x00000001
#define FLTP_IRPCTRL_PRE_PENDED      0x00000002
#define FLTP_IRPCTRL_POST_PENDED     0x00000004
#define FLTP_IRPCTRL_COMPLETED_BY_FILTER 0x00000008
#define FLTP_IRPCTRL_FAST_IO         0x00000010
#define FLTP_IRPCTRL_FS_FILTER       0x00000020
#define FLTP_IRPCTRL_GENERATED       0x00000040
#define FLTP_IRPCTRL_IRP_PENDING     0x00000080
#define FLTP_IRPCTRL_IN_COMPLETION   0x00000100
#define FLTP_IRPCTRL_ACTIVE          0x00000200
#define FLTP_IRPCTRL_FREE_ON_COMPLETE 0x00000400
#define FLTP_IRPCTRL_INLINE_COMPLETE 0x00000800
#define FLTP_IRPCTRL_SWAPPED         0x00001000
#define FLTP_IRPCTRL_CANCELLING      0x00002000
#define FLTP_IRPCTRL_FREE_IRP        0x00004000
#define FLTP_IRPCTRL_BUSY            0x00008000
#define FLTP_IRPCTRL_NAME_CHANGE     0x00010000

typedef struct _FLTP_RELATED_OBJECTS
{
    USHORT Size;
    USHORT TransactionContext;
    PFLT_FILTER Filter;
    PFLT_VOLUME Volume;
    PFLT_INSTANCE Instance;
    PFILE_OBJECT FileObject;
    PKTRANSACTION Transaction;
} FLTP_RELATED_OBJECTS, *PFLTP_RELATED_OBJECTS;

C_ASSERT(sizeof(FLTP_RELATED_OBJECTS) == sizeof(FLT_RELATED_OBJECTS));
C_ASSERT(FIELD_OFFSET(FLT_PARAMETERS, QueryFileInformation.FileInformationClass) ==
         FIELD_OFFSET(IO_STACK_LOCATION, Parameters.QueryFile.FileInformationClass) -
         FIELD_OFFSET(IO_STACK_LOCATION, Parameters));
C_ASSERT(FIELD_OFFSET(FLT_PARAMETERS, Read.ByteOffset) ==
         FIELD_OFFSET(IO_STACK_LOCATION, Parameters.Read.ByteOffset) -
         FIELD_OFFSET(IO_STACK_LOCATION, Parameters));
C_ASSERT(FIELD_OFFSET(FLT_PARAMETERS, Create.EaLength) ==
         FIELD_OFFSET(IO_STACK_LOCATION, Parameters.Create.EaLength) -
         FIELD_OFFSET(IO_STACK_LOCATION, Parameters));
C_ASSERT(FIELD_OFFSET(FLTP_RELATED_OBJECTS, Transaction) == FIELD_OFFSET(FLT_RELATED_OBJECTS, Transaction));

#define FLTP_DATA_TO_IRP_CTRL(CallbackData) CONTAINING_RECORD(CallbackData, FLTP_IRP_CTRL, Data)

typedef struct _FLTP_GLOBALS
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT ControlDevice;
    PDEVICE_OBJECT MessageDevice;
    UNICODE_STRING ServiceKey;
    ERESOURCE Lock;
    LIST_ENTRY FilterList;
    LIST_ENTRY VolumeList;
    FAST_MUTEX AttachLock;
    EX_PUSH_LOCK ContextLock;
    LIST_ENTRY TransactionContextList;
    volatile LONG SectionCount;
    volatile LONG NameGeneration;
    EX_PUSH_LOCK NameLock;
    FAST_MUTEX TransactionLock;
    KGUARDED_MUTEX TransactionCreateLock;
    LIST_ENTRY TransactionList;
    PKRESOURCEMANAGER ResourceManager;
    HANDLE TransactionManagerHandle;
    HANDLE ResourceManagerHandle;
    KSPIN_LOCK GeneratedLock;
    BOOLEAN (NTAPI *Is32bitProcess)(_In_opt_ PIRP Irp);
    FAST_IO_DISPATCH FastIoDispatch;
    POBJECT_TYPE ServerPortType;
} FLTP_GLOBALS;

extern FLTP_GLOBALS FltGlobals;

#define ExAcquirePushLockExclusive(Lock) ExfAcquirePushLockExclusive(Lock)
#define ExAcquirePushLockShared(Lock) ExfAcquirePushLockShared(Lock)
#define ExReleasePushLockExclusive(Lock) ExfReleasePushLockExclusive(Lock)
#define ExReleasePushLockShared(Lock) ExfReleasePushLockShared(Lock)
#define ExReleasePushLock(Lock) ExfReleasePushLock(Lock)

FORCEINLINE
VOID
FltpInitializeLock(
    _Out_ PEX_PUSH_LOCK Lock)
{
    Lock->Ptr = NULL;
}

FORCEINLINE
ULONG
FltpOperationIndex(
    _In_ UCHAR MajorFunction)
{
    if (MajorFunction <= IRP_MJ_MAXIMUM_FUNCTION)
    {
        return MajorFunction;
    }
    if (MajorFunction > (UCHAR)(0xFF - FLT_INTERNAL_OPERATION_COUNT))
    {
        return IRP_MJ_MAXIMUM_FUNCTION + 1 + (0xFF - MajorFunction);
    }
    return MAXULONG;
}

FORCEINLINE
BOOLEAN
FltpIsVolumeDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PFLTP_DEVICE_EXTENSION Extension = DeviceObject->DeviceExtension;

    return Extension != NULL &&
           Extension->Signature == FLTP_DEVICE_EXTENSION_SIGNATURE &&
           !Extension->FileSystemControlDevice;
}

VOID
FltpInitializeObject(
    _Out_ PFLT_OBJECT Object,
    _In_ ULONG Signature);

VOID
FltpReferencePointer(
    _Inout_ PFLT_OBJECT Object);

VOID
FltpDereferencePointer(
    _Inout_ PFLT_OBJECT Object);

NTSTATUS
FltpDuplicateString(
    _Out_ PUNICODE_STRING Destination,
    _In_ PCUNICODE_STRING Source);

VOID
FltpFreeString(
    _Inout_ PUNICODE_STRING String);

LONG
FltpCompareAltitude(
    _In_ PCUNICODE_STRING First,
    _In_ PCUNICODE_STRING Second);

BOOLEAN
FltpIsValidAltitude(
    _In_ PCUNICODE_STRING Altitude);

NTSTATUS
FltpInitializeVolumes(
    _In_ PDRIVER_OBJECT DriverObject);

NTSTATUS
FltpCreateVolume(
    _In_ PDEVICE_OBJECT FilterDevice,
    _Out_ PFLT_VOLUME *Volume);

VOID
FltpTeardownVolume(
    _In_ PFLT_VOLUME Volume);

PFLT_VOLUME
FltpReferenceVolumeFromDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

NTSTATUS
FltpMountVolume(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

VOID
FltpDetachDevice(
    _In_ PDEVICE_OBJECT SourceDevice,
    _In_ PDEVICE_OBJECT TargetDevice);

NTSTATUS
FltpQueryObjectName(
    _In_ PVOID Object,
    _Out_ PUNICODE_STRING Name);

VOID
FltpAttachFilterToVolumes(
    _In_ PFLT_FILTER Filter);

VOID
FltpAttachFiltersToVolume(
    _In_ PFLT_VOLUME Volume,
    _In_ BOOLEAN NewlyMounted);

NTSTATUS
FltpCreateInstance(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_VOLUME Volume,
    _In_ PCUNICODE_STRING Altitude,
    _In_opt_ PCUNICODE_STRING InstanceName,
    _In_ FLT_INSTANCE_SETUP_FLAGS SetupFlags,
    _Out_opt_ PFLT_INSTANCE *RetInstance);

VOID
FltpTeardownInstance(
    _In_ PFLT_INSTANCE Instance,
    _In_ FLT_INSTANCE_TEARDOWN_FLAGS Reason);

PFLT_FILTER
FltpReferenceFilterByName(
    _In_ PCUNICODE_STRING Name);

NTSTATUS
FltpReadInstanceRegistration(
    _In_ PFLT_FILTER Filter,
    _In_opt_ PCUNICODE_STRING InstanceName,
    _Out_ PUNICODE_STRING ResolvedName,
    _Out_ PUNICODE_STRING Altitude,
    _Out_ PULONG Flags);

VOID
FltpAutoAttach(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_VOLUME Volume,
    _In_ FLT_INSTANCE_SETUP_FLAGS SetupFlags);

VOID
FltpDrainInstance(
    _In_ PFLT_INSTANCE Instance);

DRIVER_DISPATCH FltpDispatch;
DRIVER_DISPATCH FltpControlDispatch;
DRIVER_DISPATCH FltpMessageDispatch;

NTSTATUS
FltpPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
FltpProcessIrp(
    _In_ PFLT_VOLUME Volume,
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

VOID
FltpInitializeFastIo(
    _In_ PDRIVER_OBJECT DriverObject);

NTSTATUS
FltpRegisterFsFilterCallbacks(
    _In_ PDRIVER_OBJECT DriverObject);

PFLTP_IRP_CTRL
FltpAllocateIrpCtrl(
    _In_ PFLT_VOLUME Volume,
    _In_ ULONG NodeCount);

VOID
FltpReferenceIrpCtrl(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl);

VOID
FltpDereferenceIrpCtrl(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl);

NTSTATUS
FltpAttachNodes(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_opt_ PFLT_INSTANCE BelowInstance);

NTSTATUS
FltpStartIrp(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl);

PFLT_INSTANCE
FltpCreateTargetInstance(
    _In_ PIRP Irp);

NTSTATUS
FltpGeneratedIo(
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ UCHAR MajorFunction,
    _In_ UCHAR MinorFunction,
    _In_ ULONG IrpFlags,
    _In_ UCHAR OperationFlags,
    _In_ CONST FLT_PARAMETERS *Parameters,
    _Out_opt_ PULONG_PTR Information);

extern CONST GUID FltpTargetInstanceEcp;

ULONG
FltpCollectNodes(
    _In_ PFLT_VOLUME Volume,
    _In_opt_ PFLT_INSTANCE BelowInstance,
    _In_ UCHAR MajorFunction,
    _In_ ULONG IrpFlags,
    _In_opt_ PFILE_OBJECT FileObject,
    _Out_ PFLTP_IRP_CTRL *IrpCtrl);

FLT_PREOP_CALLBACK_STATUS
FltpRunPreCallbacks(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl);

BOOLEAN
FltpRunPostCallbacks(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ FLT_POST_OPERATION_FLAGS Flags);

VOID
FltpBeginPostPhase(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl);

VOID
FltpInitializeRelatedObjects(
    _Out_ PFLTP_RELATED_OBJECTS FltObjects,
    _In_ PFLT_INSTANCE Instance,
    _In_opt_ PFILE_OBJECT FileObject);

VOID
FltpRunStatusCallbacks(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _In_ NTSTATUS Status);

VOID
FltpReleaseNodes(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl);

NTSTATUS
FltpDecodeParameters(
    _In_ PFLT_IO_PARAMETER_BLOCK Iopb,
    _In_ FLT_PARAMETERS *Parameters,
    _Out_ PMDL **MdlAddressPointer,
    _Out_ PVOID **Buffer,
    _Out_ PULONG *Length,
    _Out_opt_ LOCK_OPERATION *DesiredAccess);

VOID
FltpFreeMdl(
    _In_ PMDL Mdl);

VOID
FltpFinishNode(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Inout_ PFLTP_COMPLETION_NODE Node);

VOID
FltpCaptureCompletion(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl);

VOID
FltpIrpToParameters(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack,
    _Inout_ PFLT_CALLBACK_DATA Data);

VOID
FltpParametersToIrp(
    _Inout_ PFLTP_IRP_CTRL IrpCtrl,
    _Inout_ PIRP Irp,
    _Inout_ PIO_STACK_LOCATION Stack);

NTSTATUS
FltpInitializeContexts(
    _Inout_ PFLT_FILTER Filter,
    _In_opt_ CONST FLT_CONTEXT_REGISTRATION *Registration);

VOID
FltpCleanupFileObjectContexts(
    _In_ PFILE_OBJECT FileObject);

VOID
FltpDeleteInstanceContexts(
    _In_ PFLT_INSTANCE Instance);

VOID
FltpDeleteVolumeContexts(
    _In_ PFLT_VOLUME Volume,
    _In_opt_ PFLT_FILTER Filter);

VOID
FltpDeleteFilterContexts(
    _In_ PFLT_FILTER Filter);

VOID
FltpDeleteTransactionContexts(
    _In_ PKTRANSACTION Transaction);

BOOLEAN
FltpHasSections(
    _In_opt_ PFILE_OBJECT FileObject);

PLIST_ENTRY
FltpFileObjectNameList(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN Create);

VOID
FltpReleaseNameList(
    _Inout_ PLIST_ENTRY List);

VOID
FltpSynchronizeSections(
    _In_ PFLT_CALLBACK_DATA Data);

NTSTATUS
FltpTrackTransaction(
    _In_ PKTRANSACTION Transaction);

VOID
FltpDeleteInstanceEnlistments(
    _In_ PFLT_INSTANCE Instance);

NTSTATUS
FltpInitializePorts(
    _In_ PDRIVER_OBJECT DriverObject);

VOID
FltpCloseFilterPorts(
    _In_ PFLT_FILTER Filter);

NTSTATUS
FltpOpenServiceKey(
    _In_ PCUNICODE_STRING ServiceName,
    _In_opt_ PCWSTR SubKey,
    _Out_ PHANDLE Key);

NTSTATUS
FltpQueryStringValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _Out_ PUNICODE_STRING Value);

NTSTATUS
FltpQueryDwordValue(
    _In_ HANDLE Key,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Value);

#endif
