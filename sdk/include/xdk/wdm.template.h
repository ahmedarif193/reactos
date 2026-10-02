/*
 * wdm.h
 *
 * Windows NT WDM Driver Developer Kit
 *
 * This file is part of the ReactOS DDK package.
 *
 * Contributors:
 *   Amine Khaldi (amine.khaldi@reactos.org)
 *   Timo Kreuzer (timo.kreuzer@reactos.org)
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */
#pragma once

#ifndef _WDMDDK_
#define _WDMDDK_

#define WDM_MAJORVERSION        0x06
#define WDM_MINORVERSION        0x00

/* Included via ntddk.h? */
#ifndef _NTDDK_
#define _NTDDK_
#define _WDM_INCLUDED_
#define _DDK_DRIVER_
#define NO_INTERLOCKED_INTRINSICS
#endif /* _NTDDK_ */

/* Dependencies */
#define NT_INCLUDED
#include <excpt.h>
#include <ntdef.h>
#include <ntstatus.h>
#include <kernelspecs.h>
#include <ntiologc.h>
#include <suppress.h>

#ifndef GUID_DEFINED
#include <guiddef.h>
#endif

#ifdef _MAC
#ifndef _INC_STRING
#include <string.h>
#endif /* _INC_STRING */
#else
#include <string.h>
#endif /* _MAC */

#ifndef _KTMTYPES_
typedef GUID UOW, *PUOW;
#endif

typedef GUID *PGUID;

#if (NTDDI_VERSION >= NTDDI_WINXP)
#include <dpfilter.h>
#endif

#include "intrin.h"

__internal_kernel_driver
__drv_Mode_impl(WDM_INCLUDED)

#ifdef __cplusplus
extern "C" {
#endif

$define(UCHAR=UCHAR)
$define(ULONG=ULONG)
$define(USHORT=USHORT)

#if !defined(_NTHALDLL_) && !defined(_BLDR_)
#define NTHALAPI DECLSPEC_IMPORT
#else
#define NTHALAPI
#endif

/* For ReactOS */
#if !defined(_NTOSKRNL_) && !defined(_BLDR_) && !defined(_NTSYSTEM_)
#define NTKERNELAPI DECLSPEC_IMPORT
#else
#define NTKERNELAPI
#endif

/* For statically-linked ntoskrnl_vista library */
#if defined(NTKRNLVISTA)
#define NTKRNLVISTAAPI
#else
#define NTKRNLVISTAAPI NTKERNELAPI
#endif

#if defined(_X86_) && !defined(_NTHAL_)
#define _DECL_HAL_KE_IMPORT  DECLSPEC_IMPORT
#elif defined(_X86_)
#define _DECL_HAL_KE_IMPORT
#else
#define _DECL_HAL_KE_IMPORT NTKERNELAPI
#endif

#if defined(_WIN64)
#define POINTER_ALIGNMENT DECLSPEC_ALIGN(8)
#else
#define POINTER_ALIGNMENT
#endif

/* Helper macro to enable gcc's extension */
#ifndef __GNU_EXTENSION
#ifdef __GNUC__
#define __GNU_EXTENSION __extension__
#else
#define __GNU_EXTENSION
#endif
#endif

#if defined(_MSC_VER)

/* Disable some warnings */
#pragma warning(disable:4115) /* Named type definition in parentheses */
#pragma warning(disable:4201) /* Nameless unions and structs */
#pragma warning(disable:4214) /* Bit fields of other types than int */
#pragma warning(disable:4820) /* Padding added, due to alignment requirement */

/* Indicate if #pragma alloc_text() is supported */
#if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_IA64)
#define ALLOC_PRAGMA 1
#endif

/* Indicate if #pragma data_seg() is supported */
#if defined(_M_IX86) || defined(_M_AMD64)
#define ALLOC_DATA_PRAGMA 1
#endif

#endif /* _MSC_VER */

/* These macros are used to create aliases for imported data. We need to do
   this to have declarations that are compatible with MS DDK */
#ifdef _M_IX86
#define __SYMBOL(_Name) "_"#_Name
#define __IMPORTSYMBOL(_Name) "__imp__"#_Name
#define __IMPORTNAME(_Name) __imp__##_Name
#else
#define __SYMBOL(_Name) #_Name
#define __IMPORTSYMBOL(_Name) "__imp_"#_Name
#define __IMPORTNAME(_Name) __imp_##_Name
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#define __CREATE_NTOS_DATA_IMPORT_ALIAS(_Name) \
    __pragma(comment(linker, "/alternatename:"__SYMBOL(_Name) "=" __IMPORTSYMBOL(_Name)))
#else /* !_MSC_VER */
#ifndef __STRINGIFY
#define __STRINGIFY(_exp) #_exp
#endif
#define _Pragma_redefine_extname(_Name, _Target) _Pragma(__STRINGIFY(redefine_extname _Name _Target))
#define __CREATE_NTOS_DATA_IMPORT_ALIAS(_Name) \
    _Pragma_redefine_extname(_Name,__IMPORTNAME(_Name))
#endif

#if defined(_WIN64)
#if !defined(USE_DMA_MACROS) && !defined(_NTHAL_)
#define USE_DMA_MACROS
#endif
#if !defined(NO_LEGACY_DRIVERS) && !defined(__REACTOS__)
#define NO_LEGACY_DRIVERS
#endif
#endif /* defined(_WIN64) */

/* Forward declarations */
struct _IRP;
struct _MDL;
struct _KAPC;
struct _KDPC;
struct _FILE_OBJECT;
struct _DMA_ADAPTER;
struct _DEVICE_OBJECT;
struct _DRIVER_OBJECT;
struct _IO_STATUS_BLOCK;
struct _DEVICE_DESCRIPTION;
struct _SCATTER_GATHER_LIST;
struct _DRIVE_LAYOUT_INFORMATION;
struct _COMPRESSED_DATA_INFO;
struct _IO_RESOURCE_DESCRIPTOR;

/* Structures not exposed to drivers */
typedef struct _OBJECT_TYPE *POBJECT_TYPE;
typedef struct _CALLBACK_OBJECT *PCALLBACK_OBJECT;
typedef struct _EPROCESS *PEPROCESS;
typedef struct _ETHREAD *PETHREAD;
typedef struct _EJOB *PESILO;
typedef struct _IO_TIMER *PIO_TIMER;
typedef struct _KINTERRUPT *PKINTERRUPT;
typedef struct _KPROCESS *PKPROCESS, *PRKPROCESS;
typedef struct _KTHREAD *PKTHREAD, *PRKTHREAD;
typedef struct _CONTEXT *PCONTEXT;

#if defined(USE_DMA_MACROS) && !defined(_NTHAL_)
typedef struct _DMA_ADAPTER *PADAPTER_OBJECT;
#elif defined(_WDM_INCLUDED_)
typedef struct _DMA_ADAPTER *PADAPTER_OBJECT;
#else
typedef struct _ADAPTER_OBJECT *PADAPTER_OBJECT;
#endif

#ifndef DEFINE_GUIDEX
#ifdef _MSC_VER
#define DEFINE_GUIDEX(name) EXTERN_C const CDECL GUID name
#else
#define DEFINE_GUIDEX(name) EXTERN_C const GUID name
#endif
#endif /* DEFINE_GUIDEX */

#ifndef STATICGUIDOF
#define STATICGUIDOF(guid) STATIC_##guid
#endif

/* GUID Comparison */
#ifndef __IID_ALIGNED__
#define __IID_ALIGNED__
#ifdef __cplusplus
inline int IsEqualGUIDAligned(REFGUID guid1, REFGUID guid2)
{
    return ( (*(PLONGLONG)(&guid1) == *(PLONGLONG)(&guid2)) &&
             (*((PLONGLONG)(&guid1) + 1) == *((PLONGLONG)(&guid2) + 1)) );
}
#else
#define IsEqualGUIDAligned(guid1, guid2) \
           ( (*(PLONGLONG)(guid1) == *(PLONGLONG)(guid2)) && \
             (*((PLONGLONG)(guid1) + 1) == *((PLONGLONG)(guid2) + 1)) )
#endif /* __cplusplus */
#endif /* !__IID_ALIGNED__ */


$define (_WDMDDK_)
$include (memaccess.h)
$include (interlocked.h)
$include (rtltypes.h)
$include (ketypes.h)
$include (mmtypes.h)
$include (extypes.h)
$include (setypes.h)
$include (potypes.h)
$include (cmtypes.h)
$include (iotypes.h)
$include (obtypes.h)
$include (pstypes.h)
$include (wmitypes.h)

$include (kdfuncs.h)
$include (kefuncs.h)
$include (rtlfuncs.h)
$include (mmfuncs.h)
$include (sefuncs.h)
$include (cmfuncs.h)
$include (iofuncs.h)
$include (pofuncs.h)
$include (exfuncs.h)
$include (obfuncs.h)
$include (psfuncs.h)
$include (wmifuncs.h)
$include (halfuncs.h)
$include (nttmapi.h)
$include (zwfuncs.h)

#if ((NTDDI_VERSION >= NTDDI_WIN11_GA))

typedef enum _POWER_LIMIT_TYPES {
    PowerLimitContinuous = 0,
    PowerLimitBurst,
    PowerLimitRapid,
    PowerLimitPreemptive,
    PowerLimitPreemptiveOffset,
    PowerLimitTypeMax
} POWER_LIMIT_TYPES, *PPOWER_LIMIT_TYPES;

typedef struct _POWER_LIMIT_ATTRIBUTES {
    POWER_LIMIT_TYPES   Type;
    ULONG               DomainId;
    ULONG               MaxValue;
    ULONG               MinValue;
    ULONG               MinTimeParameter;
    ULONG               MaxTimeParameter;
    ULONG               DefaultACValue;
    ULONG               DefaultDCValue;
    union {
        struct {
            ULONG       SupportTimeParameter : 1;
            ULONG       Reserved : 31;
        };
        ULONG           AsUlong;
    } Flags;
} POWER_LIMIT_ATTRIBUTES, *PPOWER_LIMIT_ATTRIBUTES;

typedef struct _POWER_LIMIT_VALUE {
    POWER_LIMIT_TYPES Type;
    ULONG DomainId;
    ULONG TargetValue;
    ULONG TimeParameter;
} POWER_LIMIT_VALUE, *PPOWER_LIMIT_VALUE;

#define POWER_LIMIT_VALUE_NO_CONTROL            ULONG_MAX

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN8))

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
CmCallbackGetKeyObjectIDEx (
    _In_ PLARGE_INTEGER Cookie,
    _In_ PVOID Object,
    _Out_opt_ PULONG_PTR ObjectID,
    _Outptr_opt_ PCUNICODE_STRING *ObjectName,
    _In_ ULONG Flags
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
VOID
NTAPI
CmCallbackReleaseKeyObjectIDEx (
    _In_ PCUNICODE_STRING ObjectName
    );

#endif

typedef struct _BDCB_IMAGE_INFORMATION *PBDCB_IMAGE_INFORMATION;

#if ((NTDDI_VERSION >= NTDDI_WIN11_GA))

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoCreatePowerLimitRequest (
    _Outptr_ PVOID *PowerLimitRequest,
    _In_ PDEVICE_OBJECT TargetDeviceObject,
    _In_ PDEVICE_OBJECT PolicyDeviceObject,
    _In_ PCOUNTED_REASON_CONTEXT Context
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoQueryPowerLimitAttributes (
    _Inout_ PVOID PowerLimitRequest,
    _In_ ULONG BufferCount,
    _Inout_updates_opt_(BufferCount) PPOWER_LIMIT_ATTRIBUTES Buffer,
    _Out_ PULONG AttributeCount
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoSetPowerLimitValue (
    _Inout_ PVOID PowerLimitRequest,
    _In_opt_ PCOUNTED_REASON_CONTEXT Reason,
    _In_ ULONG ValueCount,
    _In_reads_(ValueCount) PPOWER_LIMIT_VALUE Values
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoQueryPowerLimitValue (
    _Inout_ PVOID PowerLimitRequest,
    _In_ ULONG ValueCount,
    _Inout_updates_(ValueCount) PPOWER_LIMIT_VALUE Values
    );

_IRQL_requires_max_(APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
PoDeletePowerLimitRequest (
    _Inout_ PVOID PowerLimitRequest
    );

#endif

typedef struct _KTRANSACTION KTRANSACTION, *PKTRANSACTION, *RESTRICTED_POINTER PRKTRANSACTION;

typedef struct _KENLISTMENT KENLISTMENT, *PKENLISTMENT, *RESTRICTED_POINTER PRKENLISTMENT;

typedef struct _KRESOURCEMANAGER KRESOURCEMANAGER, *PKRESOURCEMANAGER, *RESTRICTED_POINTER PRKRESOURCEMANAGER;

typedef struct _KTM KTM, *PKTM, *RESTRICTED_POINTER PRKTM;

typedef
NTSTATUS
(NTAPI *PTM_RM_NOTIFICATION) (
    _In_     PKENLISTMENT EnlistmentObject,
    _In_     PVOID RMContext,
    _In_     PVOID TransactionContext,
    _In_     ULONG TransactionNotification,
    _Inout_  PLARGE_INTEGER TmVirtualClock,
    _In_     ULONG ArgumentLength,
    _In_     PVOID Argument
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCommitComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRollbackComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmEnableCallbacks (
    _In_ PKRESOURCEMANAGER ResourceManager,
    _In_ PTM_RM_NOTIFICATION CallbackRoutine,
    _In_opt_ PVOID RMKey
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmInitializeTransactionManager (
    _In_ PRKTM TransactionManager,
    _In_opt_ PCUNICODE_STRING LogFileName,
    _In_opt_ PGUID TmId,
    _In_ ULONG CreateOptions
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRenameTransactionManager (
    _In_ PUNICODE_STRING LogFileName,
    _In_ LPGUID ExistingTransactionManagerGuid
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRecoverTransactionManager (
    _In_ PKTM Tm,
    _In_ PLARGE_INTEGER TargetVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCommitTransaction (
    _In_ PKTRANSACTION Transaction,
    _In_ BOOLEAN       Wait
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRollbackTransaction (
    _In_ PKTRANSACTION Transaction,
    _In_ BOOLEAN       Wait
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCreateEnlistment (
    _Out_ PHANDLE           EnlistmentHandle,
    _In_ KPROCESSOR_MODE    PreviousMode,
    _In_ ACCESS_MASK        DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ PRKRESOURCEMANAGER ResourceManager,
    _In_ PKTRANSACTION      Transaction,
    _In_opt_ ULONG          CreateOptions,
    _In_ NOTIFICATION_MASK  NotificationMask,
    _In_opt_ PVOID          EnlistmentKey
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRecoverEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PVOID        EnlistmentKey
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrePrepareEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrepareEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmCommitEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRollbackEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrePrepareComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPrepareComplete (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmReadOnlyEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_opt_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmReferenceEnlistmentKey (
    _In_ PKENLISTMENT Enlistment,
    _Out_ PVOID *Key
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmDereferenceEnlistmentKey (
    _In_ PKENLISTMENT Enlistment,
    _Out_opt_ PBOOLEAN LastReference
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmSinglePhaseReject (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRequestOutcomeEnlistment (
    _In_ PKENLISTMENT Enlistment,
    _In_ PLARGE_INTEGER TmVirtualClock
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmRecoverResourceManager (
    _In_ PKRESOURCEMANAGER ResourceManager
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPropagationComplete (
    _In_  PKRESOURCEMANAGER ResourceManager,
    _In_  ULONG             RequestCookie,
    _In_  ULONG             BufferLength,
    _In_  PVOID             Buffer
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
TmPropagationFailed (
    _In_  PKRESOURCEMANAGER ResourceManager,
    _In_  ULONG             RequestCookie,
    _In_  NTSTATUS          Status
    );

_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
VOID
NTAPI
TmGetTransactionId (
    _In_  PKTRANSACTION Transaction,
    _Out_ PUOW TransactionId
    );

_Must_inspect_result_
_IRQL_requires_max_ (APC_LEVEL)
NTKERNELAPI
BOOLEAN
NTAPI
TmIsTransactionActive (
    _In_ PKTRANSACTION Transaction
    );

#if ((NTDDI_VERSION >= NTDDI_WINTHRESHOLD))

#define DEVICE_RESET_INTERFACE_VERSION  1

typedef enum _DEVICE_RESET_TYPE
{
    FunctionLevelDeviceReset,
    PlatformLevelDeviceReset
} DEVICE_RESET_TYPE;

typedef union _DEVICE_BUS_SPECIFIC_RESET_TYPE {
    struct {
        ULONGLONG FunctionLevelDeviceReset:1;
        ULONGLONG PlatformLevelDeviceReset:1;
        ULONGLONG SecondaryBusReset:1;
        ULONGLONG PowerControllerReset:1;
        ULONGLONG NoOpReset:1;
        ULONGLONG Reserved:59;
    } Pci;
    struct {
        ULONGLONG FunctionLevelDeviceReset:1;
        ULONGLONG PlatformLevelDeviceReset:1;
        ULONGLONG Reserved:62;
    } Acpi;
    ULONGLONG AsULONGLONG;
} DEVICE_BUS_SPECIFIC_RESET_TYPE, *PDEVICE_BUS_SPECIFIC_RESET_TYPE;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(DEVICE_RESET_HANDLER)
NTSTATUS
NTAPI
DEVICE_RESET_HANDLER(
    _In_ PVOID InterfaceContext,
    _In_ DEVICE_RESET_TYPE ResetType,
    _In_ ULONG Flags,
    _In_opt_ PVOID ResetParameters
    );

typedef DEVICE_RESET_HANDLER *PDEVICE_RESET_HANDLER;

#if ((NTDDI_VERSION >= NTDDI_WIN10_FE))

typedef struct _DEVICE_BUS_SPECIFIC_RESET_INFO {
    GUID BusTypeGuid;
    DEVICE_BUS_SPECIFIC_RESET_TYPE ResetTypeSupported;
} DEVICE_BUS_SPECIFIC_RESET_INFO, *PDEVICE_BUS_SPECIFIC_RESET_INFO;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(DEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER)
NTSTATUS
NTAPI
DEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER(
    _In_ PVOID InterfaceContext,
    _Out_ PULONG ResetInfoCount,
    _Out_ PDEVICE_BUS_SPECIFIC_RESET_INFO ResetInfoSupported
);

typedef DEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER *PDEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER;

typedef union _BUS_SPECIFIC_RESET_FLAGS {
    struct {
        ULONGLONG KeepStackReset:1;
        ULONGLONG Reserved:63;
    } u;
    ULONGLONG AsUlonglong;
} BUS_SPECIFIC_RESET_FLAGS, *PBUS_SPECIFIC_RESET_FLAGS;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(DEVICE_BUS_SPECIFIC_RESET_HANDLER)
NTSTATUS
NTAPI
DEVICE_BUS_SPECIFIC_RESET_HANDLER(
    _In_ PVOID InterfaceContext,
    _In_ CONST GUID *BusType,
    _In_ DEVICE_BUS_SPECIFIC_RESET_TYPE ResetTypeSelected,
    _In_ PBUS_SPECIFIC_RESET_FLAGS Flags,
    _In_ PVOID ResetParameters
);

typedef DEVICE_BUS_SPECIFIC_RESET_HANDLER *PDEVICE_BUS_SPECIFIC_RESET_HANDLER;

typedef union _DEVICE_RESET_STATUS_FLAGS {
    struct {
        ULONGLONG KeepStackReset:1;
        ULONGLONG RecoveringFromBusError:1;
        ULONGLONG Reserved:62;
    } u;
    ULONGLONG AsUlonglong;
} DEVICE_RESET_STATUS_FLAGS, *PDEVICE_RESET_STATUS_FLAGS;

typedef
_IRQL_requires_same_
_IRQL_requires_max_(PASSIVE_LEVEL)
_Function_class_(GET_DEVICE_RESET_STATUS)
NTSTATUS
NTAPI
GET_DEVICE_RESET_STATUS(
    _In_ PVOID InterfaceContext,
    _Out_ PBOOLEAN IsResetting,
    _Out_ PDEVICE_BUS_SPECIFIC_RESET_TYPE ResetTypeSelected,
    _Out_ PDEVICE_RESET_STATUS_FLAGS Flags
);

typedef GET_DEVICE_RESET_STATUS *PGET_DEVICE_RESET_STATUS;

#endif

typedef struct _DEVICE_RESET_INTERFACE_STANDARD {
    USHORT Size;
    USHORT Version;
    PVOID Context;
    PINTERFACE_REFERENCE InterfaceReference;
    PINTERFACE_DEREFERENCE InterfaceDereference;
    PDEVICE_RESET_HANDLER DeviceReset;
    ULONG SupportedResetTypes;
    PVOID Reserved;
#if (NTDDI_VERSION >= NTDDI_WIN10_FE)
    PDEVICE_QUERY_BUS_SPECIFIC_RESET_HANDLER QueryBusSpecificResetInfo;
    PDEVICE_BUS_SPECIFIC_RESET_HANDLER DeviceBusSpecificReset;
    PGET_DEVICE_RESET_STATUS GetDeviceResetStatus;
#endif
} DEVICE_RESET_INTERFACE_STANDARD, *PDEVICE_RESET_INTERFACE_STANDARD;

#endif

#ifdef __cplusplus
}
#endif

#endif /* !_WDMDDK_ */
