/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio class extension 1.1 interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _ACXTARGETS_H_
#define _ACXTARGETS_H_

#ifndef WDF_EXTERN_C
  #ifdef __cplusplus
    #define WDF_EXTERN_C       extern "C"
    #define WDF_EXTERN_C_START extern "C" {
    #define WDF_EXTERN_C_END   }
  #else
    #define WDF_EXTERN_C
    #define WDF_EXTERN_C_START
    #define WDF_EXTERN_C_END
  #endif
#endif

WDF_EXTERN_C_START

#if (NTDDI_VERSION >= NTDDI_WIN2K)

typedef struct _ACX_TARGET_CIRCUIT_CONFIG {
    ULONG               Size;
    ULONG               Flags;

    WDFSTRING           SymbolicLinkName;
} ACX_TARGET_CIRCUIT_CONFIG, *PACX_TARGET_CIRCUIT_CONFIG;

typedef enum _ACX_TARGET_CIRCUIT_CONFIG_FLAGS {
    AcxTargetCircuitConfigNoFlags           = 0x00000000,
    AcxTargetCircuitConfigLazyDataLoad      = 0x00000001,
    AcxTargetCircuitConfigValidFlags        = 0x00000001
} ACX_TARGET_CIRCUIT_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_TARGET_CIRCUIT_CONFIG_INIT(
    _Out_ PACX_TARGET_CIRCUIT_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_TARGET_CIRCUIT_CONFIG));

    Config->Size = ACX_STRUCTURE_SIZE(ACX_TARGET_CIRCUIT_CONFIG);
    Config->Flags = AcxTargetCircuitConfigNoFlags;
}

typedef struct _ACX_TARGET_STREAM_CONFIG {
    ULONG               Size;
    ULONG               Flags;

    ACXTARGETCIRCUIT    TargetCircuit;

    ULONG               PinId;
    ACXDATAFORMAT       DataFormat;
    PCGUID              SignalProcessingMode;

    ACXOBJECTBAG        OptionalParameters;
} ACX_TARGET_STREAM_CONFIG, *PACX_TARGET_STREAM_CONFIG;

typedef enum _ACX_TARGET_STREAM_CONFIG_FLAGS {
    AcxTargetStreamConfigNoFlags            = 0x00000000,
    AcxTargetStreamConfigValidFlags         = 0x00000000
} ACX_TARGET_STREAM_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_TARGET_STREAM_CONFIG_INIT(
    _Out_ PACX_TARGET_STREAM_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_TARGET_STREAM_CONFIG));

    Config->Size = ACX_STRUCTURE_SIZE(ACX_TARGET_STREAM_CONFIG);
    Config->Flags = AcxTargetStreamConfigNoFlags;
}

typedef struct _ACX_TARGET_FACTORY_CIRCUIT_CONFIG {
    ULONG               Size;
    ULONG               Flags;

    WDFIOTARGET         IoTarget;
    WDFSTRING           SymbolicLinkName;
} ACX_TARGET_FACTORY_CIRCUIT_CONFIG, *PACX_TARGET_FACTORY_CIRCUIT_CONFIG;

typedef enum _ACX_TARGET_FACTORY_CIRCUIT_CONFIG_FLAGS {
    AcxTargetFactoryCircuitConfigNoFlags       = 0x00000000,
    AcxTargetFactoryCircuitConfigValidFlags    = 0x00000000
} ACX_TARGET_FACTORY_CIRCUIT_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_TARGET_FACTORY_CIRCUIT_CONFIG_INIT(
    _Out_ PACX_TARGET_FACTORY_CIRCUIT_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_TARGET_FACTORY_CIRCUIT_CONFIG));

    Config->Size = ACX_STRUCTURE_SIZE(ACX_TARGET_FACTORY_CIRCUIT_CONFIG);
    Config->Flags = AcxTargetFactoryCircuitConfigNoFlags;
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETCIRCUITCREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_TARGET_CIRCUIT_CONFIG Config,
    _Out_
    ACXTARGETCIRCUIT* TargetCircuit
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetCircuitCreate(
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_TARGET_CIRCUIT_CONFIG Config,
    _Out_
    ACXTARGETCIRCUIT* TargetCircuit
    )
{
    return ((PFN_ACXTARGETCIRCUITCREATE) AcxFunctions[AcxTargetCircuitCreateTableIndex])(AcxDriverGlobals, Device, Attributes, Config, TargetCircuit);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
WDFIOTARGET
(NTAPI *PFN_ACXTARGETCIRCUITGETWDFIOTARGET)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
WDFIOTARGET
AcxTargetCircuitGetWdfIoTarget(
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    )
{
    return ((PFN_ACXTARGETCIRCUITGETWDFIOTARGET) AcxFunctions[AcxTargetCircuitGetWdfIoTargetTableIndex])(AcxDriverGlobals, TargetCircuit);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
WDFSTRING
(NTAPI *PFN_ACXTARGETCIRCUITGETSYMBOLICLINKNAME)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
WDFSTRING
AcxTargetCircuitGetSymbolicLinkName(
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    )
{
    return ((PFN_ACXTARGETCIRCUITGETSYMBOLICLINKNAME) AcxFunctions[AcxTargetCircuitGetSymbolicLinkNameTableIndex])(AcxDriverGlobals, TargetCircuit);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXTARGETCIRCUITGETPINSCOUNT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxTargetCircuitGetPinsCount(
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    )
{
    return ((PFN_ACXTARGETCIRCUITGETPINSCOUNT) AcxFunctions[AcxTargetCircuitGetPinsCountTableIndex])(AcxDriverGlobals, TargetCircuit);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ACXTARGETPIN
(NTAPI *PFN_ACXTARGETCIRCUITGETTARGETPIN)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    ULONG PinIndex
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ACXTARGETPIN
AcxTargetCircuitGetTargetPin(
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    ULONG PinIndex
    )
{
    return ((PFN_ACXTARGETCIRCUITGETTARGETPIN) AcxFunctions[AcxTargetCircuitGetTargetPinTableIndex])(AcxDriverGlobals, TargetCircuit, PinIndex);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXTARGETCIRCUITGETELEMENTSCOUNT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxTargetCircuitGetElementsCount(
    _In_
    ACXTARGETCIRCUIT TargetCircuit
    )
{
    return ((PFN_ACXTARGETCIRCUITGETELEMENTSCOUNT) AcxFunctions[AcxTargetCircuitGetElementsCountTableIndex])(AcxDriverGlobals, TargetCircuit);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ACXTARGETELEMENT
(NTAPI *PFN_ACXTARGETCIRCUITGETTARGETELEMENT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    ULONG ElementIndex
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ACXTARGETELEMENT
AcxTargetCircuitGetTargetElement(
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    ULONG ElementIndex
    )
{
    return ((PFN_ACXTARGETCIRCUITGETTARGETELEMENT) AcxFunctions[AcxTargetCircuitGetTargetElementTableIndex])(AcxDriverGlobals, TargetCircuit, ElementIndex);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETCIRCUITFORMATREQUESTFORPROPERTY)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetCircuitFormatRequestForProperty(
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETCIRCUITFORMATREQUESTFORPROPERTY) AcxFunctions[AcxTargetCircuitFormatRequestForPropertyTableIndex])(AcxDriverGlobals, TargetCircuit, Request, Params);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETCIRCUITFORMATREQUESTFORMETHOD)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetCircuitFormatRequestForMethod(
    _In_
    ACXTARGETCIRCUIT TargetCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETCIRCUITFORMATREQUESTFORMETHOD) AcxFunctions[AcxTargetCircuitFormatRequestForMethodTableIndex])(AcxDriverGlobals, TargetCircuit, Request, Params);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXTARGETPINGETID)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETPIN TargetPin
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxTargetPinGetId(
    _In_
    ACXTARGETPIN TargetPin
    )
{
    return ((PFN_ACXTARGETPINGETID) AcxFunctions[AcxTargetPinGetIdTableIndex])(AcxDriverGlobals, TargetPin);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
WDFIOTARGET
(NTAPI *PFN_ACXTARGETPINGETWDFIOTARGET)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETPIN TargetPin
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
WDFIOTARGET
AcxTargetPinGetWdfIoTarget(
    _In_
    ACXTARGETPIN TargetPin
    )
{
    return ((PFN_ACXTARGETPINGETWDFIOTARGET) AcxFunctions[AcxTargetPinGetWdfIoTargetTableIndex])(AcxDriverGlobals, TargetPin);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETPINRETRIEVEMODEDATAFORMATLIST)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETPIN TargetPin,
    _In_
    CONST GUID* SignalProcessingMode,
    _Out_
    ACXDATAFORMATLIST* DataFormatList
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetPinRetrieveModeDataFormatList(
    _In_
    ACXTARGETPIN TargetPin,
    _In_
    CONST GUID* SignalProcessingMode,
    _Out_
    ACXDATAFORMATLIST* DataFormatList
    )
{
    return ((PFN_ACXTARGETPINRETRIEVEMODEDATAFORMATLIST) AcxFunctions[AcxTargetPinRetrieveModeDataFormatListTableIndex])(AcxDriverGlobals, TargetPin, SignalProcessingMode, DataFormatList);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETPINFLUSHMODEDATAFORMATLISTCACHE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETPIN TargetPin,
    _In_opt_
    CONST GUID* SignalProcessingMode
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetPinFlushModeDataFormatListCache(
    _In_
    ACXTARGETPIN TargetPin,
    _In_opt_
    CONST GUID* SignalProcessingMode
    )
{
    return ((PFN_ACXTARGETPINFLUSHMODEDATAFORMATLISTCACHE) AcxFunctions[AcxTargetPinFlushModeDataFormatListCacheTableIndex])(AcxDriverGlobals, TargetPin, SignalProcessingMode);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETPINFORMATREQUESTFORPROPERTY)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETPIN TargetPin,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetPinFormatRequestForProperty(
    _In_
    ACXTARGETPIN TargetPin,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETPINFORMATREQUESTFORPROPERTY) AcxFunctions[AcxTargetPinFormatRequestForPropertyTableIndex])(AcxDriverGlobals, TargetPin, Request, Params);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETPINFORMATREQUESTFORMETHOD)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETPIN TargetPin,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetPinFormatRequestForMethod(
    _In_
    ACXTARGETPIN TargetPin,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETPINFORMATREQUESTFORMETHOD) AcxFunctions[AcxTargetPinFormatRequestForMethodTableIndex])(AcxDriverGlobals, TargetPin, Request, Params);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXTARGETELEMENTGETID)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETELEMENT TargetElement
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxTargetElementGetId(
    _In_
    ACXTARGETELEMENT TargetElement
    )
{
    return ((PFN_ACXTARGETELEMENTGETID) AcxFunctions[AcxTargetElementGetIdTableIndex])(AcxDriverGlobals, TargetElement);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
GUID
(NTAPI *PFN_ACXTARGETELEMENTGETTYPE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETELEMENT TargetElement
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
GUID
AcxTargetElementGetType(
    _In_
    ACXTARGETELEMENT TargetElement
    )
{
    return ((PFN_ACXTARGETELEMENTGETTYPE) AcxFunctions[AcxTargetElementGetTypeTableIndex])(AcxDriverGlobals, TargetElement);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
GUID
(NTAPI *PFN_ACXTARGETELEMENTGETNAMETAG)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETELEMENT TargetElement
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
GUID
AcxTargetElementGetNameTag(
    _In_
    ACXTARGETELEMENT TargetElement
    )
{
    return ((PFN_ACXTARGETELEMENTGETNAMETAG) AcxFunctions[AcxTargetElementGetNameTagTableIndex])(AcxDriverGlobals, TargetElement);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
WDFIOTARGET
(NTAPI *PFN_ACXTARGETELEMENTGETWDFIOTARGET)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETELEMENT TargetElement
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
WDFIOTARGET
AcxTargetElementGetWdfIoTarget(
    _In_
    ACXTARGETELEMENT TargetElement
    )
{
    return ((PFN_ACXTARGETELEMENTGETWDFIOTARGET) AcxFunctions[AcxTargetElementGetWdfIoTargetTableIndex])(AcxDriverGlobals, TargetElement);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETELEMENTFORMATREQUESTFORPROPERTY)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETELEMENT TargetElement,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetElementFormatRequestForProperty(
    _In_
    ACXTARGETELEMENT TargetElement,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETELEMENTFORMATREQUESTFORPROPERTY) AcxFunctions[AcxTargetElementFormatRequestForPropertyTableIndex])(AcxDriverGlobals, TargetElement, Request, Params);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETELEMENTFORMATREQUESTFORMETHOD)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETELEMENT TargetElement,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetElementFormatRequestForMethod(
    _In_
    ACXTARGETELEMENT TargetElement,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETELEMENTFORMATREQUESTFORMETHOD) AcxFunctions[AcxTargetElementFormatRequestForMethodTableIndex])(AcxDriverGlobals, TargetElement, Request, Params);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETSTREAMCREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_TARGET_STREAM_CONFIG Config,
    _Out_
    ACXTARGETSTREAM* TargetStream
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetStreamCreate(
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_TARGET_STREAM_CONFIG Config,
    _Out_
    ACXTARGETSTREAM* TargetStream
    )
{
    return ((PFN_ACXTARGETSTREAMCREATE) AcxFunctions[AcxTargetStreamCreateTableIndex])(AcxDriverGlobals, Device, Attributes, Config, TargetStream);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
WDFIOTARGET
(NTAPI *PFN_ACXTARGETSTREAMGETWDFIOTARGET)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETSTREAM TargetStream
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
WDFIOTARGET
AcxTargetStreamGetWdfIoTarget(
    _In_
    ACXTARGETSTREAM TargetStream
    )
{
    return ((PFN_ACXTARGETSTREAMGETWDFIOTARGET) AcxFunctions[AcxTargetStreamGetWdfIoTargetTableIndex])(AcxDriverGlobals, TargetStream);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXTARGETSTREAMGETELEMENTSCOUNT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETSTREAM TargetStream
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxTargetStreamGetElementsCount(
    _In_
    ACXTARGETSTREAM TargetStream
    )
{
    return ((PFN_ACXTARGETSTREAMGETELEMENTSCOUNT) AcxFunctions[AcxTargetStreamGetElementsCountTableIndex])(AcxDriverGlobals, TargetStream);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ACXTARGETELEMENT
(NTAPI *PFN_ACXTARGETSTREAMGETTARGETELEMENT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    ULONG ElementIndex
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ACXTARGETELEMENT
AcxTargetStreamGetTargetElement(
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    ULONG ElementIndex
    )
{
    return ((PFN_ACXTARGETSTREAMGETTARGETELEMENT) AcxFunctions[AcxTargetStreamGetTargetElementTableIndex])(AcxDriverGlobals, TargetStream, ElementIndex);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETSTREAMASSIGNDRMCONTENTID)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    ULONG ContentId,
    _In_
    PACXDRMRIGHTS DrmRights
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetStreamAssignDrmContentId(
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    ULONG ContentId,
    _In_
    PACXDRMRIGHTS DrmRights
    )
{
    return ((PFN_ACXTARGETSTREAMASSIGNDRMCONTENTID) AcxFunctions[AcxTargetStreamAssignDrmContentIdTableIndex])(AcxDriverGlobals, TargetStream, ContentId, DrmRights);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETSTREAMFORMATREQUESTFORPROPERTY)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetStreamFormatRequestForProperty(
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETSTREAMFORMATREQUESTFORPROPERTY) AcxFunctions[AcxTargetStreamFormatRequestForPropertyTableIndex])(AcxDriverGlobals, TargetStream, Request, Params);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETSTREAMFORMATREQUESTFORMETHOD)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetStreamFormatRequestForMethod(
    _In_
    ACXTARGETSTREAM TargetStream,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETSTREAMFORMATREQUESTFORMETHOD) AcxFunctions[AcxTargetStreamFormatRequestForMethodTableIndex])(AcxDriverGlobals, TargetStream, Request, Params);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETFACTORYCIRCUITCREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_TARGET_FACTORY_CIRCUIT_CONFIG Config,
    _Out_
    ACXTARGETFACTORYCIRCUIT* TargetFactoryCircuit
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetFactoryCircuitCreate(
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_TARGET_FACTORY_CIRCUIT_CONFIG Config,
    _Out_
    ACXTARGETFACTORYCIRCUIT* TargetFactoryCircuit
    )
{
    return ((PFN_ACXTARGETFACTORYCIRCUITCREATE) AcxFunctions[AcxTargetFactoryCircuitCreateTableIndex])(AcxDriverGlobals, Device, Attributes, Config, TargetFactoryCircuit);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
WDFIOTARGET
(NTAPI *PFN_ACXTARGETFACTORYCIRCUITGETWDFIOTARGET)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETFACTORYCIRCUIT TargetFactoryCircuit
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
WDFIOTARGET
AcxTargetFactoryCircuitGetWdfIoTarget(
    _In_
    ACXTARGETFACTORYCIRCUIT TargetFactoryCircuit
    )
{
    return ((PFN_ACXTARGETFACTORYCIRCUITGETWDFIOTARGET) AcxFunctions[AcxTargetFactoryCircuitGetWdfIoTargetTableIndex])(AcxDriverGlobals, TargetFactoryCircuit);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETFACTORYCIRCUITFORMATREQUESTFORPROPERTY)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETFACTORYCIRCUIT TargetFactoryCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetFactoryCircuitFormatRequestForProperty(
    _In_
    ACXTARGETFACTORYCIRCUIT TargetFactoryCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETFACTORYCIRCUITFORMATREQUESTFORPROPERTY) AcxFunctions[AcxTargetFactoryCircuitFormatRequestForPropertyTableIndex])(AcxDriverGlobals, TargetFactoryCircuit, Request, Params);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXTARGETFACTORYCIRCUITFORMATREQUESTFORMETHOD)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXTARGETFACTORYCIRCUIT TargetFactoryCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxTargetFactoryCircuitFormatRequestForMethod(
    _In_
    ACXTARGETFACTORYCIRCUIT TargetFactoryCircuit,
    _In_
    WDFREQUEST Request,
    _In_
    PACX_REQUEST_PARAMETERS Params
    )
{
    return ((PFN_ACXTARGETFACTORYCIRCUITFORMATREQUESTFORMETHOD) AcxFunctions[AcxTargetFactoryCircuitFormatRequestForMethodTableIndex])(AcxDriverGlobals, TargetFactoryCircuit, Request, Params);
}

#endif

WDF_EXTERN_C_END

#endif
