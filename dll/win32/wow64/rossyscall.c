/*
 * PROJECT:     ReactOS WoW64 layer
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Thunks for ReactOS syscalls that have no Wine counterpart
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Wine's wow64 only provides thunks for the syscalls its own ntdll
 * implements.  This file adds thunks for the remaining entries of the
 * ReactOS syscall table (ntoskrnl/include/sysfuncs.h), so that the WoW64
 * layer is never the reason a 32-bit syscall behaves differently from its
 * 64-bit counterpart.  It also provides real implementations for the five
 * syscalls Wine stubs out with a raised exception (ALL_SYSCALL_STUBS).
 */

#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wow64_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wow);

/**********************************************************************
 * Types not present in the Wine headers.  Layouts follow the ReactOS
 * NDK (sdk/include/ndk); the *32 variants are the i386 layouts.
 */

/* OBJECT_TYPE_LIST32 comes from struct32.h */

typedef struct
{
    ULONG JobHandle;         /* HANDLE */
    ULONG MemberLevel;
    ULONG Flags;
} JOB_SET_ARRAY32;

/* cf. JOB_SET_ARRAY from winnt.h, but don't rely on it being there */
typedef struct
{
    HANDLE JobHandle;
    ULONG  MemberLevel;
    ULONG  Flags;
} JOB_SET_ARRAY64;

typedef struct
{
    UNICODE_STRING ImageName;
    HANDLE         ImageHandle;
} APPHELP_CACHE_SERVICE_LOOKUP;

typedef struct
{
    UNICODE_STRING32 ImageName;
    ULONG            ImageHandle;
} APPHELP_CACHE_SERVICE_LOOKUP32;

typedef struct
{
    ULONG Data[2];
} WNF_STATE_NAME;

typedef struct
{
    GUID TypeId;
} WNF_TYPE_ID;

/* PLUGPLAY_EVENT_BLOCK headers (payload union follows immediately) */
typedef struct
{
    GUID   EventGuid;
    ULONG  EventCategory;
    PULONG Result;
    ULONG  Flags;
    ULONG  TotalSize;
    PVOID  DeviceObject;
} PLUGPLAY_EVENT_BLOCK64;

typedef struct
{
    GUID  EventGuid;
    ULONG EventCategory;
    ULONG Result;            /* PULONG */
    ULONG Flags;
    ULONG TotalSize;
    ULONG DeviceObject;      /* PVOID */
} PLUGPLAY_EVENT_BLOCK32;

/* cf. PLUGPLAY_CONTROL_CLASS from ndk/cmtypes.h */
enum
{
    PlugPlayControlEnumerateDevice,
    PlugPlayControlRegisterNewDevice,
    PlugPlayControlDeregisterDevice,
    PlugPlayControlInitializeDevice,
    PlugPlayControlStartDevice,
    PlugPlayControlUnlockDevice,
    PlugPlayControlQueryAndRemoveDevice,
    PlugPlayControlUserResponse,
    PlugPlayControlGenerateLegacyDevice,
    PlugPlayControlGetInterfaceDeviceList,
    PlugPlayControlProperty,
    PlugPlayControlDeviceClassAssociation,
    PlugPlayControlGetRelatedDevice,
    PlugPlayControlGetInterfaceDeviceAlias,
    PlugPlayControlDeviceStatus,
    PlugPlayControlGetDeviceDepth,
    PlugPlayControlQueryDeviceRelations,
    PlugPlayControlTargetDeviceRelation,
    PlugPlayControlQueryConflictList,
    PlugPlayControlRetrieveDock,
    PlugPlayControlResetDevice,
    PlugPlayControlHaltDevice,
    PlugPlayControlGetBlockedDriverList,
    MaxPlugPlayControl
};

typedef struct
{
    UNICODE_STRING DeviceInstance;
    ULONG          Flags;
} PLUGPLAY_CONTROL_ENUMERATE_DEVICE_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            Flags;
} PLUGPLAY_CONTROL_ENUMERATE_DEVICE_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
} PLUGPLAY_CONTROL_DEVICE_CONTROL_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
} PLUGPLAY_CONTROL_DEVICE_CONTROL_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
    ULONG          Flags;
    ULONG          VetoType;
    LPWSTR         VetoName;
    ULONG          NameLength;
} PLUGPLAY_CONTROL_QUERY_REMOVE_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            Flags;
    ULONG            VetoType;
    ULONG            VetoName;
    ULONG            NameLength;
} PLUGPLAY_CONTROL_QUERY_REMOVE_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
    GUID          *FilterGuid;
    ULONG          Flags;
    PVOID          Buffer;
    ULONG          BufferSize;
} PLUGPLAY_CONTROL_INTERFACE_DEVICE_LIST_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            FilterGuid;
    ULONG            Flags;
    ULONG            Buffer;
    ULONG            BufferSize;
} PLUGPLAY_CONTROL_INTERFACE_DEVICE_LIST_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
    ULONG          Property;
    PVOID          Buffer;
    ULONG          BufferSize;
} PLUGPLAY_CONTROL_PROPERTY_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            Property;
    ULONG            Buffer;
    ULONG            BufferSize;
} PLUGPLAY_CONTROL_PROPERTY_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
    GUID          *InterfaceGuid;
    UNICODE_STRING Reference;
    BOOLEAN        Register;
    PWCHAR         SymbolicLinkName;
    ULONG          SymbolicLinkNameLength;
} PLUGPLAY_CONTROL_CLASS_ASSOCIATION_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            InterfaceGuid;
    UNICODE_STRING32 Reference;
    BOOLEAN          Register;
    ULONG            SymbolicLinkName;
    ULONG            SymbolicLinkNameLength;
} PLUGPLAY_CONTROL_CLASS_ASSOCIATION_DATA32;

typedef struct
{
    UNICODE_STRING TargetDeviceInstance;
    ULONG          Relation;
    PWCHAR         RelatedDeviceInstance;
    ULONG          RelatedDeviceInstanceLength;
} PLUGPLAY_CONTROL_RELATED_DEVICE_DATA;

typedef struct
{
    UNICODE_STRING32 TargetDeviceInstance;
    ULONG            Relation;
    ULONG            RelatedDeviceInstance;
    ULONG            RelatedDeviceInstanceLength;
} PLUGPLAY_CONTROL_RELATED_DEVICE_DATA32;

typedef struct
{
    UNICODE_STRING SymbolicLinkName;
    GUID          *AliasInterfaceClassGuid;
    PWCHAR         AliasSymbolicLinkName;
    ULONG          AliasSymbolicLinkNameLength;
} PLUGPLAY_CONTROL_INTERFACE_ALIAS_DATA;

typedef struct
{
    UNICODE_STRING32 SymbolicLinkName;
    ULONG            AliasInterfaceClassGuid;
    ULONG            AliasSymbolicLinkName;
    ULONG            AliasSymbolicLinkNameLength;
} PLUGPLAY_CONTROL_INTERFACE_ALIAS_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
    ULONG          Operation;
    ULONG          DeviceStatus;
    ULONG          DeviceProblem;
} PLUGPLAY_CONTROL_STATUS_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            Operation;
    ULONG            DeviceStatus;
    ULONG            DeviceProblem;
} PLUGPLAY_CONTROL_STATUS_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
    ULONG          Depth;
} PLUGPLAY_CONTROL_DEPTH_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            Depth;
} PLUGPLAY_CONTROL_DEPTH_DATA32;

typedef struct
{
    UNICODE_STRING DeviceInstance;
    ULONG          Relations;
    ULONG          BufferSize;
    PWCHAR         Buffer;
} PLUGPLAY_CONTROL_DEVICE_RELATIONS_DATA;

typedef struct
{
    UNICODE_STRING32 DeviceInstance;
    ULONG            Relations;
    ULONG            BufferSize;
    ULONG            Buffer;
} PLUGPLAY_CONTROL_DEVICE_RELATIONS_DATA32;

/**********************************************************************
 * Syscall prototypes not present in the Wine headers.  Signatures
 * follow the ReactOS NDK; enums are declared as ULONG and opaque
 * flat structures as void pointers.
 */

NTSYSAPI NTSTATUS WINAPI NtAccessCheckByType( SECURITY_DESCRIPTOR *, SID *, HANDLE, ACCESS_MASK, OBJECT_TYPE_LIST *, ULONG, GENERIC_MAPPING *, PRIVILEGE_SET *, ULONG *, ACCESS_MASK *, NTSTATUS * );
NTSYSAPI NTSTATUS WINAPI NtAccessCheckByTypeResultList( SECURITY_DESCRIPTOR *, SID *, HANDLE, ACCESS_MASK, OBJECT_TYPE_LIST *, ULONG, GENERIC_MAPPING *, PRIVILEGE_SET *, ULONG *, ACCESS_MASK *, NTSTATUS * );
NTSYSAPI NTSTATUS WINAPI NtAccessCheckByTypeResultListAndAuditAlarm( UNICODE_STRING *, void *, UNICODE_STRING *, UNICODE_STRING *, SECURITY_DESCRIPTOR *, SID *, ACCESS_MASK, ULONG, ULONG, OBJECT_TYPE_LIST *, ULONG, GENERIC_MAPPING *, BOOLEAN, ACCESS_MASK *, NTSTATUS *, BOOLEAN * );
NTSYSAPI NTSTATUS WINAPI NtAccessCheckByTypeResultListAndAuditAlarmByHandle( UNICODE_STRING *, void *, HANDLE, UNICODE_STRING *, UNICODE_STRING *, SECURITY_DESCRIPTOR *, SID *, ACCESS_MASK, ULONG, ULONG, OBJECT_TYPE_LIST *, ULONG, GENERIC_MAPPING *, BOOLEAN, ACCESS_MASK *, NTSTATUS *, BOOLEAN * );
NTSYSAPI NTSTATUS WINAPI NtAddBootEntry( void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtAddDriverEntry( void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtAllocateUserPhysicalPages( HANDLE, ULONG_PTR *, ULONG_PTR * );
NTSYSAPI NTSTATUS WINAPI NtApphelpCacheControl( ULONG, APPHELP_CACHE_SERVICE_LOOKUP * );
NTSYSAPI NTSTATUS WINAPI NtCancelDeviceWakeupRequest( HANDLE );
NTSYSAPI NTSTATUS WINAPI NtCompactKeys( ULONG, HANDLE * );
NTSYSAPI NTSTATUS WINAPI NtCompressKey( HANDLE );
NTSYSAPI NTSTATUS WINAPI NtCreateJobSet( ULONG, JOB_SET_ARRAY64 *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtCreateProcessEx( HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, HANDLE, ULONG, HANDLE, HANDLE, HANDLE, BOOLEAN );
NTSYSAPI NTSTATUS WINAPI NtCreateWaitablePort( HANDLE *, OBJECT_ATTRIBUTES *, ULONG, ULONG, ULONG );
NTSYSAPI NTSTATUS WINAPI NtCreateWnfStateName( WNF_STATE_NAME *, ULONG, ULONG, BOOLEAN, const WNF_TYPE_ID *, ULONG, SECURITY_DESCRIPTOR * );
NTSYSAPI NTSTATUS WINAPI NtDeleteBootEntry( ULONG );
NTSYSAPI NTSTATUS WINAPI NtDeleteDriverEntry( ULONG );
NTSYSAPI NTSTATUS WINAPI NtDeleteObjectAuditAlarm( UNICODE_STRING *, void *, BOOLEAN );
NTSYSAPI NTSTATUS WINAPI NtDeleteWnfStateData( const WNF_STATE_NAME *, const void * );
NTSYSAPI NTSTATUS WINAPI NtDeleteWnfStateName( const WNF_STATE_NAME * );
NTSYSAPI NTSTATUS WINAPI NtEnumerateBootEntries( void *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtEnumerateDriverEntries( void *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtEnumerateSystemEnvironmentValuesEx( ULONG, void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtFreeUserPhysicalPages( HANDLE, ULONG_PTR *, ULONG_PTR * );
NTSYSAPI NTSTATUS WINAPI NtGetCurrentProcessorNumberEx( PROCESSOR_NUMBER * );
NTSYSAPI NTSTATUS WINAPI NtGetDevicePowerState( HANDLE, ULONG * );
NTSYSAPI BOOLEAN  WINAPI NtIsSystemResumeAutomatic( void );
NTSYSAPI NTSTATUS WINAPI NtLockProductActivationKeys( ULONG *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtLockRegistryKey( HANDLE );
NTSYSAPI NTSTATUS WINAPI NtMapUserPhysicalPages( void *, ULONG_PTR, ULONG_PTR * );
NTSYSAPI NTSTATUS WINAPI NtMapUserPhysicalPagesScatter( void **, ULONG_PTR, ULONG_PTR * );
NTSYSAPI NTSTATUS WINAPI NtModifyBootEntry( void * );
NTSYSAPI NTSTATUS WINAPI NtModifyDriverEntry( void * );
NTSYSAPI NTSTATUS WINAPI NtNotifyChangeDirectoryFileEx( HANDLE, HANDLE, PIO_APC_ROUTINE, void *, IO_STATUS_BLOCK *, void *, ULONG, ULONG, BOOLEAN, ULONG );
NTSYSAPI NTSTATUS WINAPI NtPlugPlayControl( ULONG, void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtQueryBootEntryOrder( ULONG *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtQueryBootOptions( void *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtQueryDebugFilterState( ULONG, ULONG );
NTSYSAPI NTSTATUS WINAPI NtQueryDirectoryFileEx( HANDLE, HANDLE, PIO_APC_ROUTINE, void *, IO_STATUS_BLOCK *, void *, ULONG, FILE_INFORMATION_CLASS, ULONG, UNICODE_STRING * );
NTSYSAPI NTSTATUS WINAPI NtQueryDriverEntryOrder( ULONG *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtQueryInformationByName( OBJECT_ATTRIBUTES *, IO_STATUS_BLOCK *, void *, ULONG, FILE_INFORMATION_CLASS );
NTSYSAPI NTSTATUS WINAPI NtQueryOpenSubKeysEx( OBJECT_ATTRIBUTES *, ULONG, void *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtQueryPortInformationProcess( void );
NTSYSAPI NTSTATUS WINAPI NtQueryQuotaInformationFile( HANDLE, IO_STATUS_BLOCK *, void *, ULONG, BOOLEAN, void *, ULONG, SID *, BOOLEAN );
NTSYSAPI NTSTATUS WINAPI NtQueryWnfStateData( const WNF_STATE_NAME *, const WNF_TYPE_ID *, const void *, ULONG *, void *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtQueryWnfStateNameInformation( const WNF_STATE_NAME *, ULONG, const void *, void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtRequestDeviceWakeup( HANDLE );
NTSYSAPI NTSTATUS WINAPI NtRequestWakeupLatency( ULONG );
NTSYSAPI NTSTATUS WINAPI NtSaveKeyEx( HANDLE, HANDLE, ULONG );
NTSYSAPI NTSTATUS WINAPI NtSaveMergedKeys( HANDLE, HANDLE, HANDLE );
NTSYSAPI NTSTATUS WINAPI NtSetBootEntryOrder( ULONG *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtSetBootOptions( void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtSetDriverEntryOrder( ULONG *, ULONG * );
NTSYSAPI NTSTATUS WINAPI NtSetQuotaInformationFile( HANDLE, IO_STATUS_BLOCK *, void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtSetSystemEnvironmentValueEx( UNICODE_STRING *, GUID *, void *, ULONG, ULONG );
NTSYSAPI NTSTATUS WINAPI NtSetUuidSeed( char * );
NTSYSAPI NTSTATUS WINAPI NtSubscribeWnfStateChange( const WNF_STATE_NAME *, ULONG, ULONG, ULONG64 * );
NTSYSAPI NTSTATUS WINAPI NtTraceEvent( HANDLE, ULONG, ULONG, void * );
NTSYSAPI NTSTATUS WINAPI NtTranslateFilePath( void *, ULONG, void *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtUnloadKey2( OBJECT_ATTRIBUTES *, ULONG );
NTSYSAPI NTSTATUS WINAPI NtUnsubscribeWnfStateChange( const WNF_STATE_NAME * );
NTSYSAPI NTSTATUS WINAPI NtUpdateWnfStateData( const WNF_STATE_NAME *, const void *, ULONG, const WNF_TYPE_ID *, const void *, ULONG, ULONG );

/**********************************************************************
 * Conversion helpers
 */

static OBJECT_TYPE_LIST *object_type_list_32to64( const OBJECT_TYPE_LIST32 *list32, ULONG count )
{
    OBJECT_TYPE_LIST *list;
    ULONG i;

    if (!list32) return NULL;
    list = Wow64AllocateTemp( (count ? count : 1) * sizeof(*list) );
    for (i = 0; i < count; i++)
    {
        list[i].Level = list32[i].Level;
        list[i].Sbz = list32[i].Sbz;
        list[i].ObjectType = ULongToPtr( list32[i].ObjectType );
    }
    return list;
}

static HANDLE *handle_array_32to64( const ULONG *handles32, ULONG count )
{
    HANDLE *handles;
    ULONG i;

    if (!handles32) return NULL;
    handles = Wow64AllocateTemp( (count ? count : 1) * sizeof(*handles) );
    for (i = 0; i < count; i++) handles[i] = LongToHandle( handles32[i] );
    return handles;
}

static ULONG_PTR *pfn_array_32to64( const ULONG *pfns32, ULONG_PTR count )
{
    ULONG_PTR *pfns, i;

    if (!pfns32) return NULL;
    pfns = Wow64AllocateTemp( (count ? count : 1) * sizeof(*pfns) );
    for (i = 0; i < count; i++) pfns[i] = pfns32[i];
    return pfns;
}

static void put_pfn_array( ULONG *pfns32, const ULONG_PTR *pfns, ULONG_PTR count )
{
    ULONG_PTR i;

    if (!pfns32 || !pfns) return;
    for (i = 0; i < count; i++) pfns32[i] = (ULONG)pfns[i];
}

/**********************************************************************
 *           wow64_NtAccessCheckByType
 */
NTSTATUS WINAPI wow64_NtAccessCheckByType( UINT *args )
{
    SECURITY_DESCRIPTOR *sd32 = get_ptr( &args );
    SID *self_sid = get_ptr( &args );
    HANDLE token = get_handle( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_TYPE_LIST32 *types32 = get_ptr( &args );
    ULONG count = get_ulong( &args );
    GENERIC_MAPPING *mapping = get_ptr( &args );
    PRIVILEGE_SET *privs = get_ptr( &args );
    ULONG *retlen = get_ptr( &args );
    ACCESS_MASK *access_granted = get_ptr( &args );
    NTSTATUS *access_status = get_ptr( &args );

    SECURITY_DESCRIPTOR sd;

    return NtAccessCheckByType( secdesc_32to64( &sd, sd32 ), self_sid, token, access,
                                object_type_list_32to64( types32, count ), count,
                                mapping, privs, retlen, access_granted, access_status );
}


/**********************************************************************
 *           wow64_NtAccessCheckByTypeResultList
 */
NTSTATUS WINAPI wow64_NtAccessCheckByTypeResultList( UINT *args )
{
    SECURITY_DESCRIPTOR *sd32 = get_ptr( &args );
    SID *self_sid = get_ptr( &args );
    HANDLE token = get_handle( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_TYPE_LIST32 *types32 = get_ptr( &args );
    ULONG count = get_ulong( &args );
    GENERIC_MAPPING *mapping = get_ptr( &args );
    PRIVILEGE_SET *privs = get_ptr( &args );
    ULONG *retlen = get_ptr( &args );
    ACCESS_MASK *access_granted = get_ptr( &args );
    NTSTATUS *access_status = get_ptr( &args );

    SECURITY_DESCRIPTOR sd;

    return NtAccessCheckByTypeResultList( secdesc_32to64( &sd, sd32 ), self_sid, token, access,
                                          object_type_list_32to64( types32, count ), count,
                                          mapping, privs, retlen, access_granted, access_status );
}


/**********************************************************************
 *           wow64_NtAccessCheckByTypeResultListAndAuditAlarm
 */
NTSTATUS WINAPI wow64_NtAccessCheckByTypeResultListAndAuditAlarm( UINT *args )
{
    UNICODE_STRING32 *subsystem32 = get_ptr( &args );
    void *handle_id = get_handle( &args );
    UNICODE_STRING32 *typename32 = get_ptr( &args );
    UNICODE_STRING32 *objname32 = get_ptr( &args );
    SECURITY_DESCRIPTOR *sd32 = get_ptr( &args );
    SID *self_sid = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    ULONG audit_type = get_ulong( &args );
    ULONG flags = get_ulong( &args );
    OBJECT_TYPE_LIST32 *types32 = get_ptr( &args );
    ULONG count = get_ulong( &args );
    GENERIC_MAPPING *mapping = get_ptr( &args );
    BOOLEAN creation = get_ulong( &args );
    ACCESS_MASK *granted_list = get_ptr( &args );
    NTSTATUS *status_list = get_ptr( &args );
    BOOLEAN *onclose = get_ptr( &args );

    UNICODE_STRING subsystem, typename, objname;
    SECURITY_DESCRIPTOR sd;

    return NtAccessCheckByTypeResultListAndAuditAlarm( unicode_str_32to64( &subsystem, subsystem32 ), handle_id,
                                                       unicode_str_32to64( &typename, typename32 ),
                                                       unicode_str_32to64( &objname, objname32 ),
                                                       secdesc_32to64( &sd, sd32 ), self_sid, access,
                                                       audit_type, flags,
                                                       object_type_list_32to64( types32, count ), count,
                                                       mapping, creation, granted_list, status_list, onclose );
}


/**********************************************************************
 *           wow64_NtAccessCheckByTypeResultListAndAuditAlarmByHandle
 */
NTSTATUS WINAPI wow64_NtAccessCheckByTypeResultListAndAuditAlarmByHandle( UINT *args )
{
    UNICODE_STRING32 *subsystem32 = get_ptr( &args );
    void *handle_id = get_handle( &args );
    HANDLE token = get_handle( &args );
    UNICODE_STRING32 *typename32 = get_ptr( &args );
    UNICODE_STRING32 *objname32 = get_ptr( &args );
    SECURITY_DESCRIPTOR *sd32 = get_ptr( &args );
    SID *self_sid = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    ULONG audit_type = get_ulong( &args );
    ULONG flags = get_ulong( &args );
    OBJECT_TYPE_LIST32 *types32 = get_ptr( &args );
    ULONG count = get_ulong( &args );
    GENERIC_MAPPING *mapping = get_ptr( &args );
    BOOLEAN creation = get_ulong( &args );
    ACCESS_MASK *granted_list = get_ptr( &args );
    NTSTATUS *status_list = get_ptr( &args );
    BOOLEAN *onclose = get_ptr( &args );

    UNICODE_STRING subsystem, typename, objname;
    SECURITY_DESCRIPTOR sd;

    return NtAccessCheckByTypeResultListAndAuditAlarmByHandle( unicode_str_32to64( &subsystem, subsystem32 ), handle_id,
                                                               token, unicode_str_32to64( &typename, typename32 ),
                                                               unicode_str_32to64( &objname, objname32 ),
                                                               secdesc_32to64( &sd, sd32 ), self_sid, access,
                                                               audit_type, flags,
                                                               object_type_list_32to64( types32, count ), count,
                                                               mapping, creation, granted_list, status_list, onclose );
}


/**********************************************************************
 *           wow64_NtAddBootEntry
 */
NTSTATUS WINAPI wow64_NtAddBootEntry( UINT *args )
{
    void *entry = get_ptr( &args );      /* BOOT_ENTRY: flat, offset-based */
    ULONG id = get_ulong( &args );

    return NtAddBootEntry( entry, id );
}


/**********************************************************************
 *           wow64_NtAddDriverEntry
 */
NTSTATUS WINAPI wow64_NtAddDriverEntry( UINT *args )
{
    void *entry = get_ptr( &args );      /* EFI_DRIVER_ENTRY: flat, offset-based */
    ULONG id = get_ulong( &args );

    return NtAddDriverEntry( entry, id );
}


/**********************************************************************
 *           wow64_NtAllocateUserPhysicalPages
 */
NTSTATUS WINAPI wow64_NtAllocateUserPhysicalPages( UINT *args )
{
    HANDLE process = get_handle( &args );
    ULONG *count32 = get_ptr( &args );
    ULONG *pfns32 = get_ptr( &args );

    ULONG_PTR count, *pfns = NULL;
    NTSTATUS status;

    if (!count32 || !pfns32) return STATUS_ACCESS_VIOLATION;
    count = *count32;
    pfns = Wow64AllocateTemp( (count ? count : 1) * sizeof(*pfns) );
    status = NtAllocateUserPhysicalPages( process, &count, pfns );
    put_size( count32, count );
    put_pfn_array( pfns32, pfns, count );
    return status;
}


/**********************************************************************
 *           wow64_NtApphelpCacheControl
 */
NTSTATUS WINAPI wow64_NtApphelpCacheControl( UINT *args )
{
    ULONG service = get_ulong( &args );
    APPHELP_CACHE_SERVICE_LOOKUP32 *lookup32 = get_ptr( &args );

    APPHELP_CACHE_SERVICE_LOOKUP lookup, *plookup = NULL;

    if (lookup32)
    {
        unicode_str_32to64( &lookup.ImageName, &lookup32->ImageName );
        lookup.ImageHandle = LongToHandle( lookup32->ImageHandle );
        plookup = &lookup;
    }
    return NtApphelpCacheControl( service, plookup );
}


/**********************************************************************
 *           wow64_NtCancelDeviceWakeupRequest
 */
NTSTATUS WINAPI wow64_NtCancelDeviceWakeupRequest( UINT *args )
{
    HANDLE device = get_handle( &args );

    return NtCancelDeviceWakeupRequest( device );
}


/**********************************************************************
 *           wow64_NtCompactKeys
 */
NTSTATUS WINAPI wow64_NtCompactKeys( UINT *args )
{
    ULONG count = get_ulong( &args );
    ULONG *keys32 = get_ptr( &args );

    return NtCompactKeys( count, handle_array_32to64( keys32, count ));
}


/**********************************************************************
 *           wow64_NtCompressKey
 */
NTSTATUS WINAPI wow64_NtCompressKey( UINT *args )
{
    HANDLE key = get_handle( &args );

    return NtCompressKey( key );
}


/**********************************************************************
 *           wow64_NtCreateEventPair
 */
NTSTATUS WINAPI wow64_NtCreateEventPair( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    status = NtCreateEventPair( &handle, access, objattr_32to64( &attr, attr32 ));
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtCreateJobSet
 */
NTSTATUS WINAPI wow64_NtCreateJobSet( UINT *args )
{
    ULONG count = get_ulong( &args );
    JOB_SET_ARRAY32 *array32 = get_ptr( &args );
    ULONG flags = get_ulong( &args );

    JOB_SET_ARRAY64 *array = NULL;
    ULONG i;

    if (array32)
    {
        array = Wow64AllocateTemp( (count ? count : 1) * sizeof(*array) );
        for (i = 0; i < count; i++)
        {
            array[i].JobHandle = LongToHandle( array32[i].JobHandle );
            array[i].MemberLevel = array32[i].MemberLevel;
            array[i].Flags = array32[i].Flags;
        }
    }
    return NtCreateJobSet( count, array, flags );
}


/**********************************************************************
 *           wow64_NtCreateProcess
 */
NTSTATUS WINAPI wow64_NtCreateProcess( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE parent = get_handle( &args );
    BOOLEAN inherit = get_ulong( &args );
    HANDLE section = get_handle( &args );
    HANDLE debug = get_handle( &args );
    HANDLE exception = get_handle( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    status = NtCreateProcess( &handle, access, objattr_32to64( &attr, attr32 ), parent,
                              inherit, section, debug, exception );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtCreateProcessEx
 */
NTSTATUS WINAPI wow64_NtCreateProcessEx( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE parent = get_handle( &args );
    ULONG flags = get_ulong( &args );
    HANDLE section = get_handle( &args );
    HANDLE debug = get_handle( &args );
    HANDLE exception = get_handle( &args );
    BOOLEAN in_job = get_ulong( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    status = NtCreateProcessEx( &handle, access, objattr_32to64( &attr, attr32 ), parent,
                                flags, section, debug, exception, in_job );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtCreateProfile
 */
NTSTATUS WINAPI wow64_NtCreateProfile( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    HANDLE process = get_handle( &args );
    void *base = get_ptr( &args );
    ULONG size = get_ulong( &args );
    ULONG bucket = get_ulong( &args );
    void *buffer = get_ptr( &args );
    ULONG buffer_size = get_ulong( &args );
    KPROFILE_SOURCE source = get_ulong( &args );
    KAFFINITY affinity = get_ulong( &args );

    HANDLE handle = 0;
    NTSTATUS status;

    status = NtCreateProfile( &handle, process, base, size, bucket, buffer, buffer_size,
                              source, affinity );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtCreateWaitablePort
 */
NTSTATUS WINAPI wow64_NtCreateWaitablePort( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG info_len = get_ulong( &args );
    ULONG data_len = get_ulong( &args );
    ULONG msg_queue_size = get_ulong( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    status = NtCreateWaitablePort( &handle, objattr_32to64( &attr, attr32 ), info_len,
                                   data_len, msg_queue_size );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtCreateWnfStateName
 */
NTSTATUS WINAPI wow64_NtCreateWnfStateName( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );
    ULONG lifetime = get_ulong( &args );
    ULONG scope = get_ulong( &args );
    BOOLEAN persist = get_ulong( &args );
    WNF_TYPE_ID *type_id = get_ptr( &args );
    ULONG max_size = get_ulong( &args );
    SECURITY_DESCRIPTOR *sd32 = get_ptr( &args );

    SECURITY_DESCRIPTOR sd;

    return NtCreateWnfStateName( name, lifetime, scope, persist, type_id, max_size,
                                 secdesc_32to64( &sd, sd32 ));
}


/**********************************************************************
 *           wow64_NtDeleteBootEntry
 */
NTSTATUS WINAPI wow64_NtDeleteBootEntry( UINT *args )
{
    ULONG id = get_ulong( &args );

    return NtDeleteBootEntry( id );
}


/**********************************************************************
 *           wow64_NtDeleteDriverEntry
 */
NTSTATUS WINAPI wow64_NtDeleteDriverEntry( UINT *args )
{
    ULONG id = get_ulong( &args );

    return NtDeleteDriverEntry( id );
}


/**********************************************************************
 *           wow64_NtDeleteObjectAuditAlarm
 */
NTSTATUS WINAPI wow64_NtDeleteObjectAuditAlarm( UINT *args )
{
    UNICODE_STRING32 *subsystem32 = get_ptr( &args );
    void *handle_id = get_handle( &args );
    BOOLEAN onclose = get_ulong( &args );

    UNICODE_STRING subsystem;

    return NtDeleteObjectAuditAlarm( unicode_str_32to64( &subsystem, subsystem32 ), handle_id, onclose );
}


/**********************************************************************
 *           wow64_NtDeleteWnfStateData
 */
NTSTATUS WINAPI wow64_NtDeleteWnfStateData( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );
    void *scope = get_ptr( &args );

    return NtDeleteWnfStateData( name, scope );
}


/**********************************************************************
 *           wow64_NtDeleteWnfStateName
 */
NTSTATUS WINAPI wow64_NtDeleteWnfStateName( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );

    return NtDeleteWnfStateName( name );
}


/**********************************************************************
 *           wow64_NtEnumerateBootEntries
 */
NTSTATUS WINAPI wow64_NtEnumerateBootEntries( UINT *args )
{
    void *buffer = get_ptr( &args );
    ULONG *len = get_ptr( &args );

    return NtEnumerateBootEntries( buffer, len );
}


/**********************************************************************
 *           wow64_NtEnumerateDriverEntries
 */
NTSTATUS WINAPI wow64_NtEnumerateDriverEntries( UINT *args )
{
    void *buffer = get_ptr( &args );
    ULONG *len = get_ptr( &args );

    return NtEnumerateDriverEntries( buffer, len );
}


/**********************************************************************
 *           wow64_NtEnumerateSystemEnvironmentValuesEx
 */
NTSTATUS WINAPI wow64_NtEnumerateSystemEnvironmentValuesEx( UINT *args )
{
    ULONG class = get_ulong( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );

    return NtEnumerateSystemEnvironmentValuesEx( class, buffer, len );
}


/**********************************************************************
 *           wow64_NtExtendSection
 */
NTSTATUS WINAPI wow64_NtExtendSection( UINT *args )
{
    HANDLE handle = get_handle( &args );
    LARGE_INTEGER *size = get_ptr( &args );

    return NtExtendSection( handle, size );
}


/**********************************************************************
 *           wow64_NtFlushWriteBuffer
 */
NTSTATUS WINAPI wow64_NtFlushWriteBuffer( UINT *args )
{
    UNREFERENCED_PARAMETER(args);
    return NtFlushWriteBuffer();
}


/**********************************************************************
 *           wow64_NtFreeUserPhysicalPages
 */
NTSTATUS WINAPI wow64_NtFreeUserPhysicalPages( UINT *args )
{
    HANDLE process = get_handle( &args );
    ULONG *count32 = get_ptr( &args );
    ULONG *pfns32 = get_ptr( &args );

    ULONG_PTR count;
    NTSTATUS status;

    if (!count32) return STATUS_ACCESS_VIOLATION;
    count = *count32;
    status = NtFreeUserPhysicalPages( process, &count, pfn_array_32to64( pfns32, count ));
    put_size( count32, count );
    return status;
}


/**********************************************************************
 *           wow64_NtGetCurrentProcessorNumberEx
 */
NTSTATUS WINAPI wow64_NtGetCurrentProcessorNumberEx( UINT *args )
{
    PROCESSOR_NUMBER *proc_number = get_ptr( &args );

    return NtGetCurrentProcessorNumberEx( proc_number );
}


/**********************************************************************
 *           wow64_NtGetDevicePowerState
 */
NTSTATUS WINAPI wow64_NtGetDevicePowerState( UINT *args )
{
    HANDLE device = get_handle( &args );
    ULONG *state = get_ptr( &args );

    return NtGetDevicePowerState( device, state );
}


/**********************************************************************
 *           wow64_NtGetPlugPlayEvent
 */
NTSTATUS WINAPI wow64_NtGetPlugPlayEvent( UINT *args )
{
    ULONG reserved1 = get_ulong( &args );
    ULONG reserved2 = get_ulong( &args );
    PLUGPLAY_EVENT_BLOCK32 *event32 = get_ptr( &args );
    ULONG size32 = get_ulong( &args );

    static const ULONG delta = sizeof(PLUGPLAY_EVENT_BLOCK64) - sizeof(PLUGPLAY_EVENT_BLOCK32);
    PLUGPLAY_EVENT_BLOCK64 *event;
    ULONG size;
    NTSTATUS status;

    if (!event32 || size32 < sizeof(*event32))
        return NtGetPlugPlayEvent( reserved1, reserved2, event32, size32 );

    size = size32 + delta;
    event = Wow64AllocateTemp( size );
    status = NtGetPlugPlayEvent( reserved1, reserved2, event, size );
    if (!status && event->TotalSize >= sizeof(*event))
    {
        /* the payload union is flat for all event categories ReactOS
           generates; pointer-bearing payloads would need extra handling */
        event32->EventGuid = event->EventGuid;
        event32->EventCategory = event->EventCategory;
        event32->Result = PtrToUlong( event->Result );
        event32->Flags = event->Flags;
        event32->TotalSize = event->TotalSize - delta;
        event32->DeviceObject = PtrToUlong( event->DeviceObject );
        memcpy( event32 + 1, event + 1, event->TotalSize - sizeof(*event) );
    }
    return status;
}


/**********************************************************************
 *           wow64_NtImpersonateThread
 */
NTSTATUS WINAPI wow64_NtImpersonateThread( UINT *args )
{
    HANDLE thread = get_handle( &args );
    HANDLE target = get_handle( &args );
    SECURITY_QUALITY_OF_SERVICE *qos = get_ptr( &args );  /* flat, same layout */

    return NtImpersonateThread( thread, target, qos );
}


/**********************************************************************
 *           wow64_NtInitializeRegistry
 */
NTSTATUS WINAPI wow64_NtInitializeRegistry( UINT *args )
{
    USHORT flag = get_ulong( &args );

    /* the kernel takes a USHORT boot flag (cf. CM_BOOT_FLAG_*, up to 1001),
       calling through the Wine BOOLEAN prototype would truncate it */
    NTSTATUS (WINAPI *pNtInitializeRegistry)( USHORT ) = (NTSTATUS (WINAPI *)(USHORT))NtInitializeRegistry;

    return pNtInitializeRegistry( flag );
}


/**********************************************************************
 *           wow64_NtIsSystemResumeAutomatic
 */
NTSTATUS WINAPI wow64_NtIsSystemResumeAutomatic( UINT *args )
{
    UNREFERENCED_PARAMETER(args);
    return NtIsSystemResumeAutomatic();
}


/**********************************************************************
 *           wow64_NtLockProductActivationKeys
 */
NTSTATUS WINAPI wow64_NtLockProductActivationKeys( UINT *args )
{
    ULONG *private_ver = get_ptr( &args );
    ULONG *safe_mode = get_ptr( &args );

    return NtLockProductActivationKeys( private_ver, safe_mode );
}


/**********************************************************************
 *           wow64_NtLockRegistryKey
 */
NTSTATUS WINAPI wow64_NtLockRegistryKey( UINT *args )
{
    HANDLE key = get_handle( &args );

    return NtLockRegistryKey( key );
}


/**********************************************************************
 *           wow64_NtMapUserPhysicalPages
 */
NTSTATUS WINAPI wow64_NtMapUserPhysicalPages( UINT *args )
{
    void *addr = get_ptr( &args );
    ULONG_PTR count = get_ulong( &args );
    ULONG *pfns32 = get_ptr( &args );

    return NtMapUserPhysicalPages( addr, count, pfn_array_32to64( pfns32, count ));
}


/**********************************************************************
 *           wow64_NtMapUserPhysicalPagesScatter
 */
NTSTATUS WINAPI wow64_NtMapUserPhysicalPagesScatter( UINT *args )
{
    ULONG *addrs32 = get_ptr( &args );
    ULONG_PTR count = get_ulong( &args );
    ULONG *pfns32 = get_ptr( &args );

    void **addrs = NULL;
    ULONG_PTR i;

    if (addrs32)
    {
        addrs = Wow64AllocateTemp( (count ? count : 1) * sizeof(*addrs) );
        for (i = 0; i < count; i++) addrs[i] = ULongToPtr( addrs32[i] );
    }
    return NtMapUserPhysicalPagesScatter( addrs, count, pfn_array_32to64( pfns32, count ));
}


/**********************************************************************
 *           wow64_NtModifyBootEntry
 */
NTSTATUS WINAPI wow64_NtModifyBootEntry( UINT *args )
{
    void *entry = get_ptr( &args );

    return NtModifyBootEntry( entry );
}


/**********************************************************************
 *           wow64_NtModifyDriverEntry
 */
NTSTATUS WINAPI wow64_NtModifyDriverEntry( UINT *args )
{
    void *entry = get_ptr( &args );

    return NtModifyDriverEntry( entry );
}


/**********************************************************************
 *           wow64_NtNotifyChangeDirectoryFileEx
 */
NTSTATUS WINAPI wow64_NtNotifyChangeDirectoryFileEx( UINT *args )
{
    HANDLE handle = get_handle( &args );
    HANDLE event = get_handle( &args );
    ULONG apc = get_ulong( &args );
    ULONG apc_param = get_ulong( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG filter = get_ulong( &args );
    BOOLEAN subtree = get_ulong( &args );
    ULONG class = get_ulong( &args );

    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtNotifyChangeDirectoryFileEx( handle, event, apc_32to64( apc ),
                                            apc_param_32to64( apc, apc_param ),
                                            iosb_32to64( &io, io32 ), buffer, len,
                                            filter, subtree, class );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenEventPair
 */
NTSTATUS WINAPI wow64_NtOpenEventPair( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    status = NtOpenEventPair( &handle, access, objattr_32to64( &attr, attr32 ));
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenObjectAuditAlarm
 */
NTSTATUS WINAPI wow64_NtOpenObjectAuditAlarm( UINT *args )
{
    UNICODE_STRING32 *subsystem32 = get_ptr( &args );
    void *handle_id = get_handle( &args );
    UNICODE_STRING32 *typename32 = get_ptr( &args );
    UNICODE_STRING32 *objname32 = get_ptr( &args );
    SECURITY_DESCRIPTOR *sd32 = get_ptr( &args );
    HANDLE token = get_handle( &args );
    ACCESS_MASK access = get_ulong( &args );
    ACCESS_MASK granted = get_ulong( &args );
    PRIVILEGE_SET *privs = get_ptr( &args );
    BOOLEAN creation = get_ulong( &args );
    BOOLEAN granted_access = get_ulong( &args );
    BOOLEAN *onclose = get_ptr( &args );

    UNICODE_STRING subsystem, typename, objname;
    SECURITY_DESCRIPTOR sd;

    return NtOpenObjectAuditAlarm( unicode_str_32to64( &subsystem, subsystem32 ), handle_id,
                                   unicode_str_32to64( &typename, typename32 ),
                                   unicode_str_32to64( &objname, objname32 ),
                                   secdesc_32to64( &sd, sd32 ), token, access, granted,
                                   privs, creation, granted_access, onclose );
}


/**********************************************************************
 *           wow64_NtPlugPlayControl
 */
NTSTATUS WINAPI wow64_NtPlugPlayControl( UINT *args )
{
    ULONG class = get_ulong( &args );
    void *buf32 = get_ptr( &args );
    ULONG size32 = get_ulong( &args );

    NTSTATUS status;

    if (!buf32) return NtPlugPlayControl( class, buf32, size32 );

    switch (class)
    {
    case PlugPlayControlEnumerateDevice:
    {
        PLUGPLAY_CONTROL_ENUMERATE_DEVICE_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_ENUMERATE_DEVICE_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.Flags = data32->Flags;
        return NtPlugPlayControl( class, &data, sizeof(data) );
    }

    case PlugPlayControlRegisterNewDevice:
    case PlugPlayControlDeregisterDevice:
    case PlugPlayControlInitializeDevice:
    case PlugPlayControlStartDevice:
    case PlugPlayControlUnlockDevice:
    case PlugPlayControlResetDevice:
    case PlugPlayControlHaltDevice:
    {
        PLUGPLAY_CONTROL_DEVICE_CONTROL_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_DEVICE_CONTROL_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        return NtPlugPlayControl( class, &data, sizeof(data) );
    }

    case PlugPlayControlQueryAndRemoveDevice:
    {
        PLUGPLAY_CONTROL_QUERY_REMOVE_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_QUERY_REMOVE_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.Flags = data32->Flags;
        data.VetoType = data32->VetoType;
        data.VetoName = ULongToPtr( data32->VetoName );
        data.NameLength = data32->NameLength;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->VetoType = data.VetoType;
        data32->NameLength = data.NameLength;
        return status;
    }

    case PlugPlayControlUserResponse:
        /* flat structure, same layout on 32 and 64 bit */
        return NtPlugPlayControl( class, buf32, size32 );

    case PlugPlayControlGetInterfaceDeviceList:
    {
        PLUGPLAY_CONTROL_INTERFACE_DEVICE_LIST_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_INTERFACE_DEVICE_LIST_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.FilterGuid = ULongToPtr( data32->FilterGuid );
        data.Flags = data32->Flags;
        data.Buffer = ULongToPtr( data32->Buffer );
        data.BufferSize = data32->BufferSize;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->BufferSize = data.BufferSize;
        return status;
    }

    case PlugPlayControlProperty:
    {
        PLUGPLAY_CONTROL_PROPERTY_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_PROPERTY_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.Property = data32->Property;
        data.Buffer = ULongToPtr( data32->Buffer );
        data.BufferSize = data32->BufferSize;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->BufferSize = data.BufferSize;
        return status;
    }

    case PlugPlayControlDeviceClassAssociation:
    {
        PLUGPLAY_CONTROL_CLASS_ASSOCIATION_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_CLASS_ASSOCIATION_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.InterfaceGuid = ULongToPtr( data32->InterfaceGuid );
        unicode_str_32to64( &data.Reference, &data32->Reference );
        data.Register = data32->Register;
        data.SymbolicLinkName = ULongToPtr( data32->SymbolicLinkName );
        data.SymbolicLinkNameLength = data32->SymbolicLinkNameLength;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->SymbolicLinkNameLength = data.SymbolicLinkNameLength;
        return status;
    }

    case PlugPlayControlGetRelatedDevice:
    {
        PLUGPLAY_CONTROL_RELATED_DEVICE_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_RELATED_DEVICE_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.TargetDeviceInstance, &data32->TargetDeviceInstance );
        data.Relation = data32->Relation;
        data.RelatedDeviceInstance = ULongToPtr( data32->RelatedDeviceInstance );
        data.RelatedDeviceInstanceLength = data32->RelatedDeviceInstanceLength;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->RelatedDeviceInstanceLength = data.RelatedDeviceInstanceLength;
        return status;
    }

    case PlugPlayControlGetInterfaceDeviceAlias:
    {
        PLUGPLAY_CONTROL_INTERFACE_ALIAS_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_INTERFACE_ALIAS_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.SymbolicLinkName, &data32->SymbolicLinkName );
        data.AliasInterfaceClassGuid = ULongToPtr( data32->AliasInterfaceClassGuid );
        data.AliasSymbolicLinkName = ULongToPtr( data32->AliasSymbolicLinkName );
        data.AliasSymbolicLinkNameLength = data32->AliasSymbolicLinkNameLength;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->AliasSymbolicLinkNameLength = data.AliasSymbolicLinkNameLength;
        return status;
    }

    case PlugPlayControlDeviceStatus:
    {
        PLUGPLAY_CONTROL_STATUS_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_STATUS_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.Operation = data32->Operation;
        data.DeviceStatus = data32->DeviceStatus;
        data.DeviceProblem = data32->DeviceProblem;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->DeviceStatus = data.DeviceStatus;
        data32->DeviceProblem = data.DeviceProblem;
        return status;
    }

    case PlugPlayControlGetDeviceDepth:
    {
        PLUGPLAY_CONTROL_DEPTH_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_DEPTH_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.Depth = data32->Depth;
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->Depth = data.Depth;
        return status;
    }

    case PlugPlayControlQueryDeviceRelations:
    {
        PLUGPLAY_CONTROL_DEVICE_RELATIONS_DATA32 *data32 = buf32;
        PLUGPLAY_CONTROL_DEVICE_RELATIONS_DATA data;

        if (size32 != sizeof(*data32)) return STATUS_INVALID_PARAMETER;
        unicode_str_32to64( &data.DeviceInstance, &data32->DeviceInstance );
        data.Relations = data32->Relations;
        data.BufferSize = data32->BufferSize;
        data.Buffer = ULongToPtr( data32->Buffer );
        status = NtPlugPlayControl( class, &data, sizeof(data) );
        data32->BufferSize = data.BufferSize;
        return status;
    }

    default:
        FIXME( "unsupported class %lu\n", (ULONG)class );
        return STATUS_NOT_IMPLEMENTED;
    }
}


/**********************************************************************
 *           wow64_NtPrivilegeObjectAuditAlarm
 */
NTSTATUS WINAPI wow64_NtPrivilegeObjectAuditAlarm( UINT *args )
{
    UNICODE_STRING32 *subsystem32 = get_ptr( &args );
    HANDLE handle_id = get_handle( &args );
    HANDLE token = get_handle( &args );
    ACCESS_MASK access = get_ulong( &args );
    PRIVILEGE_SET *privs = get_ptr( &args );
    BOOLEAN granted = get_ulong( &args );

    UNICODE_STRING subsystem;

    return NtPrivilegeObjectAuditAlarm( unicode_str_32to64( &subsystem, subsystem32 ), handle_id,
                                        token, access, privs, granted );
}


/**********************************************************************
 *           wow64_NtPrivilegedServiceAuditAlarm
 */
NTSTATUS WINAPI wow64_NtPrivilegedServiceAuditAlarm( UINT *args )
{
    UNICODE_STRING32 *subsystem32 = get_ptr( &args );
    UNICODE_STRING32 *service32 = get_ptr( &args );
    HANDLE token = get_handle( &args );
    PRIVILEGE_SET *privs = get_ptr( &args );
    BOOLEAN granted = get_ulong( &args );

    UNICODE_STRING subsystem, service;

    return NtPrivilegedServiceAuditAlarm( unicode_str_32to64( &subsystem, subsystem32 ),
                                          unicode_str_32to64( &service, service32 ),
                                          token, privs, granted );
}


/**********************************************************************
 *           wow64_NtQueryBootEntryOrder
 */
NTSTATUS WINAPI wow64_NtQueryBootEntryOrder( UINT *args )
{
    ULONG *ids = get_ptr( &args );
    ULONG *count = get_ptr( &args );

    return NtQueryBootEntryOrder( ids, count );
}


/**********************************************************************
 *           wow64_NtQueryBootOptions
 */
NTSTATUS WINAPI wow64_NtQueryBootOptions( UINT *args )
{
    void *options = get_ptr( &args );    /* BOOT_OPTIONS: flat */
    ULONG *len = get_ptr( &args );

    return NtQueryBootOptions( options, len );
}


/**********************************************************************
 *           wow64_NtQueryDebugFilterState
 */
NTSTATUS WINAPI wow64_NtQueryDebugFilterState( UINT *args )
{
    ULONG component_id = get_ulong( &args );
    ULONG level = get_ulong( &args );

    return NtQueryDebugFilterState( component_id, level );
}


/**********************************************************************
 *           wow64_NtQueryDirectoryFileEx
 */
NTSTATUS WINAPI wow64_NtQueryDirectoryFileEx( UINT *args )
{
    HANDLE handle = get_handle( &args );
    HANDLE event = get_handle( &args );
    ULONG apc = get_ulong( &args );
    ULONG apc_param = get_ulong( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    FILE_INFORMATION_CLASS class = get_ulong( &args );
    ULONG query_flags = get_ulong( &args );
    UNICODE_STRING32 *mask32 = get_ptr( &args );

    UNICODE_STRING mask;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtQueryDirectoryFileEx( handle, event, apc_32to64( apc ),
                                     apc_param_32to64( apc, apc_param ),
                                     iosb_32to64( &io, io32 ), buffer, len, class,
                                     query_flags, unicode_str_32to64( &mask, mask32 ));
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtQueryDriverEntryOrder
 */
NTSTATUS WINAPI wow64_NtQueryDriverEntryOrder( UINT *args )
{
    ULONG *ids = get_ptr( &args );
    ULONG *count = get_ptr( &args );

    return NtQueryDriverEntryOrder( ids, count );
}


/**********************************************************************
 *           wow64_NtQueryInformationByName
 */
NTSTATUS WINAPI wow64_NtQueryInformationByName( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    FILE_INFORMATION_CLASS class = get_ulong( &args );

    struct object_attr64 attr;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtQueryInformationByName( objattr_32to64_redirect( &attr, attr32 ),
                                       iosb_32to64( &io, io32 ), buffer, len, class );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtQueryInformationPort
 */
NTSTATUS WINAPI wow64_NtQueryInformationPort( UINT *args )
{
    HANDLE handle = get_handle( &args );
    PORT_INFORMATION_CLASS class = get_ulong( &args );
    void *info = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    return NtQueryInformationPort( handle, class, info, len, retlen );
}


/**********************************************************************
 *           wow64_NtQueryIntervalProfile
 */
NTSTATUS WINAPI wow64_NtQueryIntervalProfile( UINT *args )
{
    KPROFILE_SOURCE source = get_ulong( &args );
    ULONG *interval = get_ptr( &args );

    return NtQueryIntervalProfile( source, interval );
}


/**********************************************************************
 *           wow64_NtQueryOpenSubKeys
 */
NTSTATUS WINAPI wow64_NtQueryOpenSubKeys( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG *count = get_ptr( &args );

    struct object_attr64 attr;

    return NtQueryOpenSubKeys( objattr_32to64( &attr, attr32 ), count );
}


/**********************************************************************
 *           wow64_NtQueryOpenSubKeysEx
 */
NTSTATUS WINAPI wow64_NtQueryOpenSubKeysEx( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG len = get_ulong( &args );
    void *buffer = get_ptr( &args );
    ULONG *retlen = get_ptr( &args );

    struct object_attr64 attr;
    NTSTATUS status;

    status = NtQueryOpenSubKeysEx( objattr_32to64( &attr, attr32 ), len, buffer, retlen );
    /* KEY_OPEN_SUBKEYS_INFORMATION contains pointer-sized fields; the kernel
       currently does not implement this service, revisit if that changes */
    if (!status) FIXME( "buffer conversion not implemented\n" );
    return status;
}


/**********************************************************************
 *           wow64_NtQueryPortInformationProcess
 */
NTSTATUS WINAPI wow64_NtQueryPortInformationProcess( UINT *args )
{
    UNREFERENCED_PARAMETER(args);
    return NtQueryPortInformationProcess();
}


/**********************************************************************
 *           wow64_NtQueryQuotaInformationFile
 */
NTSTATUS WINAPI wow64_NtQueryQuotaInformationFile( UINT *args )
{
    HANDLE handle = get_handle( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    BOOLEAN single_entry = get_ulong( &args );
    void *sid_list = get_ptr( &args );
    ULONG sid_list_len = get_ulong( &args );
    SID *start_sid = get_ptr( &args );
    BOOLEAN restart_scan = get_ulong( &args );

    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtQueryQuotaInformationFile( handle, iosb_32to64( &io, io32 ), buffer, len,
                                          single_entry, sid_list, sid_list_len,
                                          start_sid, restart_scan );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtQueryWnfStateData
 */
NTSTATUS WINAPI wow64_NtQueryWnfStateData( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );
    WNF_TYPE_ID *type_id = get_ptr( &args );
    void *scope = get_ptr( &args );
    ULONG *change_stamp = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG *size = get_ptr( &args );

    return NtQueryWnfStateData( name, type_id, scope, change_stamp, buffer, size );
}


/**********************************************************************
 *           wow64_NtQueryWnfStateNameInformation
 */
NTSTATUS WINAPI wow64_NtQueryWnfStateNameInformation( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );
    ULONG class = get_ulong( &args );
    void *scope = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG size = get_ulong( &args );

    return NtQueryWnfStateNameInformation( name, class, scope, buffer, size );
}


/**********************************************************************
 *           wow64_NtRequestDeviceWakeup
 */
NTSTATUS WINAPI wow64_NtRequestDeviceWakeup( UINT *args )
{
    HANDLE device = get_handle( &args );

    return NtRequestDeviceWakeup( device );
}


/**********************************************************************
 *           wow64_NtRequestWakeupLatency
 */
NTSTATUS WINAPI wow64_NtRequestWakeupLatency( UINT *args )
{
    ULONG latency = get_ulong( &args );

    return NtRequestWakeupLatency( latency );
}


/**********************************************************************
 *           wow64_NtSaveKeyEx
 */
NTSTATUS WINAPI wow64_NtSaveKeyEx( UINT *args )
{
    HANDLE key = get_handle( &args );
    HANDLE file = get_handle( &args );
    ULONG flags = get_ulong( &args );

    return NtSaveKeyEx( key, file, flags );
}


/**********************************************************************
 *           wow64_NtSaveMergedKeys
 */
NTSTATUS WINAPI wow64_NtSaveMergedKeys( UINT *args )
{
    HANDLE high_key = get_handle( &args );
    HANDLE low_key = get_handle( &args );
    HANDLE file = get_handle( &args );

    return NtSaveMergedKeys( high_key, low_key, file );
}


/**********************************************************************
 *           wow64_NtSetBootEntryOrder
 */
NTSTATUS WINAPI wow64_NtSetBootEntryOrder( UINT *args )
{
    ULONG *ids = get_ptr( &args );
    ULONG *count = get_ptr( &args );

    return NtSetBootEntryOrder( ids, count );
}


/**********************************************************************
 *           wow64_NtSetBootOptions
 */
NTSTATUS WINAPI wow64_NtSetBootOptions( UINT *args )
{
    void *options = get_ptr( &args );
    ULONG fields = get_ulong( &args );

    return NtSetBootOptions( options, fields );
}


/**********************************************************************
 *           wow64_NtSetDefaultHardErrorPort
 */
NTSTATUS WINAPI wow64_NtSetDefaultHardErrorPort( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtSetDefaultHardErrorPort( handle );
}


/**********************************************************************
 *           wow64_NtSetDriverEntryOrder
 */
NTSTATUS WINAPI wow64_NtSetDriverEntryOrder( UINT *args )
{
    ULONG *ids = get_ptr( &args );
    ULONG *count = get_ptr( &args );

    return NtSetDriverEntryOrder( ids, count );
}


/**********************************************************************
 *           wow64_NtSetHighEventPair
 */
NTSTATUS WINAPI wow64_NtSetHighEventPair( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtSetHighEventPair( handle );
}


/**********************************************************************
 *           wow64_NtSetHighWaitLowEventPair
 */
NTSTATUS WINAPI wow64_NtSetHighWaitLowEventPair( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtSetHighWaitLowEventPair( handle );
}


/**********************************************************************
 *           wow64_NtSetLowEventPair
 */
NTSTATUS WINAPI wow64_NtSetLowEventPair( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtSetLowEventPair( handle );
}


/**********************************************************************
 *           wow64_NtSetLowWaitHighEventPair
 */
NTSTATUS WINAPI wow64_NtSetLowWaitHighEventPair( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtSetLowWaitHighEventPair( handle );
}


/**********************************************************************
 *           wow64_NtSetQuotaInformationFile
 */
NTSTATUS WINAPI wow64_NtSetQuotaInformationFile( UINT *args )
{
    HANDLE handle = get_handle( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );

    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtSetQuotaInformationFile( handle, iosb_32to64( &io, io32 ), buffer, len );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtSetSystemEnvironmentValue
 */
NTSTATUS WINAPI wow64_NtSetSystemEnvironmentValue( UINT *args )
{
    UNICODE_STRING32 *name32 = get_ptr( &args );
    UNICODE_STRING32 *value32 = get_ptr( &args );

    UNICODE_STRING name, value;

    return NtSetSystemEnvironmentValue( unicode_str_32to64( &name, name32 ),
                                        unicode_str_32to64( &value, value32 ));
}


/**********************************************************************
 *           wow64_NtSetSystemEnvironmentValueEx
 */
NTSTATUS WINAPI wow64_NtSetSystemEnvironmentValueEx( UINT *args )
{
    UNICODE_STRING32 *name32 = get_ptr( &args );
    GUID *vendor = get_ptr( &args );
    void *value = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG attributes = get_ulong( &args );

    UNICODE_STRING name;

    return NtSetSystemEnvironmentValueEx( unicode_str_32to64( &name, name32 ), vendor,
                                          value, len, attributes );
}


/**********************************************************************
 *           wow64_NtSetSystemPowerState
 */
NTSTATUS WINAPI wow64_NtSetSystemPowerState( UINT *args )
{
    POWER_ACTION action = get_ulong( &args );
    SYSTEM_POWER_STATE state = get_ulong( &args );
    ULONG flags = get_ulong( &args );

    return NtSetSystemPowerState( action, state, flags );
}


/**********************************************************************
 *           wow64_NtSetUuidSeed
 */
NTSTATUS WINAPI wow64_NtSetUuidSeed( UINT *args )
{
    char *seed = get_ptr( &args );

    return NtSetUuidSeed( seed );
}


/**********************************************************************
 *           wow64_NtStartProfile
 */
NTSTATUS WINAPI wow64_NtStartProfile( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtStartProfile( handle );
}


/**********************************************************************
 *           wow64_NtStopProfile
 */
NTSTATUS WINAPI wow64_NtStopProfile( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtStopProfile( handle );
}


/**********************************************************************
 *           wow64_NtSubscribeWnfStateChange
 */
NTSTATUS WINAPI wow64_NtSubscribeWnfStateChange( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );
    ULONG change_stamp = get_ulong( &args );
    ULONG event_mask = get_ulong( &args );
    ULONG64 *subscription_id = get_ptr( &args );

    return NtSubscribeWnfStateChange( name, change_stamp, event_mask, subscription_id );
}


/**********************************************************************
 *           wow64_NtTraceEvent
 */
NTSTATUS WINAPI wow64_NtTraceEvent( UINT *args )
{
    HANDLE handle = get_handle( &args );
    ULONG flags = get_ulong( &args );
    ULONG len = get_ulong( &args );
    void *header = get_ptr( &args );     /* EVENT_TRACE_HEADER: flat */

    return NtTraceEvent( handle, flags, len, header );
}


/**********************************************************************
 *           wow64_NtTranslateFilePath
 */
NTSTATUS WINAPI wow64_NtTranslateFilePath( UINT *args )
{
    void *input = get_ptr( &args );      /* FILE_PATH: flat */
    ULONG output_type = get_ulong( &args );
    void *output = get_ptr( &args );
    ULONG output_len = get_ulong( &args );

    return NtTranslateFilePath( input, output_type, output, output_len );
}


/**********************************************************************
 *           wow64_NtUnloadKey2
 */
NTSTATUS WINAPI wow64_NtUnloadKey2( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG flags = get_ulong( &args );

    struct object_attr64 attr;

    return NtUnloadKey2( objattr_32to64( &attr, attr32 ), flags );
}


/**********************************************************************
 *           wow64_NtUnloadKeyEx
 */
NTSTATUS WINAPI wow64_NtUnloadKeyEx( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE event = get_handle( &args );

    struct object_attr64 attr;

    return NtUnloadKeyEx( objattr_32to64( &attr, attr32 ), event );
}


/**********************************************************************
 *           wow64_NtUnsubscribeWnfStateChange
 */
NTSTATUS WINAPI wow64_NtUnsubscribeWnfStateChange( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );

    return NtUnsubscribeWnfStateChange( name );
}


/**********************************************************************
 *           wow64_NtUpdateWnfStateData
 */
NTSTATUS WINAPI wow64_NtUpdateWnfStateData( UINT *args )
{
    WNF_STATE_NAME *name = get_ptr( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    WNF_TYPE_ID *type_id = get_ptr( &args );
    void *scope = get_ptr( &args );
    ULONG matching_change_stamp = get_ulong( &args );
    ULONG check_stamp = get_ulong( &args );

    return NtUpdateWnfStateData( name, buffer, len, type_id, scope,
                                 matching_change_stamp, check_stamp );
}


/**********************************************************************
 *           wow64_NtVdmControl
 */
NTSTATUS WINAPI wow64_NtVdmControl( UINT *args )
{
    ULONG code = get_ulong( &args );
    void *data = get_ptr( &args );

    return NtVdmControl( code, data );
}


/**********************************************************************
 *           wow64_NtWaitForMultipleObjects32
 */
NTSTATUS WINAPI wow64_NtWaitForMultipleObjects32( UINT *args )
{
    DWORD count = get_ulong( &args );
    LONG *handles_ptr = get_ptr( &args );
    ULONG type = get_ulong( &args );
    BOOLEAN alertable = get_ulong( &args );
    const LARGE_INTEGER *timeout = get_ptr( &args );

    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    DWORD i;

    for (i = 0; i < count && i < MAXIMUM_WAIT_OBJECTS; i++) handles[i] = LongToHandle( handles_ptr[i] );
    return NtWaitForMultipleObjects( count, handles, type, alertable, timeout );
}


/**********************************************************************
 *           wow64_NtWaitHighEventPair
 */
NTSTATUS WINAPI wow64_NtWaitHighEventPair( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtWaitHighEventPair( handle );
}


/**********************************************************************
 *           wow64_NtWaitLowEventPair
 */
NTSTATUS WINAPI wow64_NtWaitLowEventPair( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtWaitLowEventPair( handle );
}
