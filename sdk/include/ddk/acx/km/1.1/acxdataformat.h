/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio class extension 1.1 interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _ACXDATAFORMAT_H_
#define _ACXDATAFORMAT_H_

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

typedef enum _ACX_DATAFORMAT_TYPE {
    AcxDataFormatKsFormat       = 0x00000000,
    AcxDataFormatMaximum
} ACX_DATAFORMAT_TYPE, *PACX_DATAFORMAT_TYPE;

typedef struct _ACX_DATAFORMAT_CONFIG {
    ULONG                       Size;
    ULONG                       Flags;
    ACX_DATAFORMAT_TYPE         Type;

    union {
        PVOID                   KsFormat;
    } u;
} ACX_DATAFORMAT_CONFIG, *PACX_DATAFORMAT_CONFIG;

typedef enum _ACX_DATAFORMAT_CONFIG_FLAGS {
    AcxDataFormatConfigNoFlags      = 0x00000000,
    AcxDataFormatConfigValidFlags   = 0x00000000
} ACX_DATAFORMAT_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_DATAFORMAT_CONFIG_INIT(
    _Out_ PACX_DATAFORMAT_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_DATAFORMAT_CONFIG));

    Config->Size = ACX_STRUCTURE_SIZE(ACX_DATAFORMAT_CONFIG);
    Config->Flags = AcxDataFormatConfigNoFlags;
}

VOID
FORCEINLINE
ACX_DATAFORMAT_CONFIG_INIT_KS(
    _Out_ PACX_DATAFORMAT_CONFIG Config,
    _In_  PVOID Format
    )
{
    RtlZeroMemory(Config, sizeof(ACX_DATAFORMAT_CONFIG));

    Config->Size = ACX_STRUCTURE_SIZE(ACX_DATAFORMAT_CONFIG);
    Config->Flags = AcxDataFormatConfigNoFlags;
    Config->Type = AcxDataFormatKsFormat;
    Config->u.KsFormat = Format;
}

typedef struct _ACX_DATAFORMAT_LIST_CONFIG {
    ULONG           Size;
    ULONG           Flags;
 } ACX_DATAFORMAT_LIST_CONFIG, *PACX_DATAFORMAT_LIST_CONFIG;

typedef enum _ACX_DATAFORMAT_LIST_CONFIG_FLAGS {
    AcxDataFormatListConfigNoFlags      = 0x00000000,
    AcxDataFormatListConfigValidFlags   = 0x00000000
} ACX_DATAFORMAT_LIST_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_DATAFORMAT_LIST_CONFIG_INIT(
    _Out_ PACX_DATAFORMAT_LIST_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_DATAFORMAT_LIST_CONFIG));

    Config->Size = ACX_STRUCTURE_SIZE(ACX_DATAFORMAT_LIST_CONFIG);
    Config->Flags = AcxDataFormatListConfigNoFlags;
}

typedef struct _ACX_DATAFORMAT_LIST_ITERATOR {
    ULONG Size;
    ULONG Flags;

    PVOID Reserved[4];
} ACX_DATAFORMAT_LIST_ITERATOR, *PACX_DATAFORMAT_LIST_ITERATOR;

typedef enum _ACX_DATAFORMAT_LIST_ITERATOR_CONFIG_FLAGS {
    AcxDataFormatListIteratorConfigNoFlags      = 0x00000000,
    AcxDataFormatListIteratorConfigValidFlags   = 0x00000000
} ACX_DATAFORMAT_LIST_ITERATOR_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_DATAFORMAT_LIST_ITERATOR_INIT(
    _Out_ PACX_DATAFORMAT_LIST_ITERATOR Iterator
    )
{
    RtlZeroMemory(Iterator, sizeof(ACX_DATAFORMAT_LIST_ITERATOR));

    Iterator->Size = ACX_STRUCTURE_SIZE(ACX_DATAFORMAT_LIST_ITERATOR);
    Iterator->Flags = AcxDataFormatListIteratorConfigNoFlags;
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATCREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_DATAFORMAT_CONFIG Config,
    _Out_
    ACXDATAFORMAT* DataFormat
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatCreate(
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_DATAFORMAT_CONFIG Config,
    _Out_
    ACXDATAFORMAT* DataFormat
    )
{
    return ((PFN_ACXDATAFORMATCREATE) AcxFunctions[AcxDataFormatCreateTableIndex])(AcxDriverGlobals, Device, Attributes, Config, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
GUID
(NTAPI *PFN_ACXDATAFORMATGETMAJORFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
GUID
AcxDataFormatGetMajorFormat(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETMAJORFORMAT) AcxFunctions[AcxDataFormatGetMajorFormatTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
GUID
(NTAPI *PFN_ACXDATAFORMATGETSUBFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
GUID
AcxDataFormatGetSubFormat(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETSUBFORMAT) AcxFunctions[AcxDataFormatGetSubFormatTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
GUID
(NTAPI *PFN_ACXDATAFORMATGETSPECIFIER)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
GUID
AcxDataFormatGetSpecifier(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETSPECIFIER) AcxFunctions[AcxDataFormatGetSpecifierTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
USHORT
(NTAPI *PFN_ACXDATAFORMATGETCHANNELSCOUNT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
USHORT
AcxDataFormatGetChannelsCount(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETCHANNELSCOUNT) AcxFunctions[AcxDataFormatGetChannelsCountTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONGLONG
(NTAPI *PFN_ACXDATAFORMATGETCHANNELMASK)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONGLONG
AcxDataFormatGetChannelMask(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETCHANNELMASK) AcxFunctions[AcxDataFormatGetChannelMaskTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETSAMPLESIZE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetSampleSize(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETSAMPLESIZE) AcxFunctions[AcxDataFormatGetSampleSizeTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETBITSPERSAMPLE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetBitsPerSample(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETBITSPERSAMPLE) AcxFunctions[AcxDataFormatGetBitsPerSampleTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETVALIDBITSPERSAMPLE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetValidBitsPerSample(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETVALIDBITSPERSAMPLE) AcxFunctions[AcxDataFormatGetValidBitsPerSampleTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETSAMPLESPERBLOCK)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetSamplesPerBlock(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETSAMPLESPERBLOCK) AcxFunctions[AcxDataFormatGetSamplesPerBlockTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETBLOCKALIGN)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetBlockAlign(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETBLOCKALIGN) AcxFunctions[AcxDataFormatGetBlockAlignTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETSAMPLERATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetSampleRate(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETSAMPLERATE) AcxFunctions[AcxDataFormatGetSampleRateTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETAVERAGEBYTESPERSEC)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetAverageBytesPerSec(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETAVERAGEBYTESPERSEC) AcxFunctions[AcxDataFormatGetAverageBytesPerSecTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETENCODEDSAMPLESPERSEC)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetEncodedSamplesPerSec(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETENCODEDSAMPLESPERSEC) AcxFunctions[AcxDataFormatGetEncodedSamplesPerSecTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETENCODEDCHANNELCOUNT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetEncodedChannelCount(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETENCODEDCHANNELCOUNT) AcxFunctions[AcxDataFormatGetEncodedChannelCountTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
ULONG
(NTAPI *PFN_ACXDATAFORMATGETENCODEDAVERAGEBYTESPERSEC)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
ULONG
AcxDataFormatGetEncodedAverageBytesPerSec(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETENCODEDAVERAGEBYTESPERSEC) AcxFunctions[AcxDataFormatGetEncodedAverageBytesPerSecTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
PVOID
(NTAPI *PFN_ACXDATAFORMATGETKSDATAFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
PVOID
AcxDataFormatGetKsDataFormat(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETKSDATAFORMAT) AcxFunctions[AcxDataFormatGetKsDataFormatTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
PVOID
(NTAPI *PFN_ACXDATAFORMATGETWAVEFORMATEX)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
PVOID
AcxDataFormatGetWaveFormatEx(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETWAVEFORMATEX) AcxFunctions[AcxDataFormatGetWaveFormatExTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
PVOID
(NTAPI *PFN_ACXDATAFORMATGETWAVEFORMATEXTENSIBLE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
PVOID
AcxDataFormatGetWaveFormatExtensible(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETWAVEFORMATEXTENSIBLE) AcxFunctions[AcxDataFormatGetWaveFormatExtensibleTableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
PVOID
(NTAPI *PFN_ACXDATAFORMATGETWAVEFORMATEXTENSIBLEIEC61937)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
PVOID
AcxDataFormatGetWaveFormatExtensibleIec61937(
    _In_
    ACXDATAFORMAT DataFormat
    )
{
    return ((PFN_ACXDATAFORMATGETWAVEFORMATEXTENSIBLEIEC61937) AcxFunctions[AcxDataFormatGetWaveFormatExtensibleIec61937TableIndex])(AcxDriverGlobals, DataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
BOOLEAN
(NTAPI *PFN_ACXDATAFORMATISEQUAL)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMAT DataFormat1,
    _In_
    ACXDATAFORMAT DataFormat2
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
BOOLEAN
AcxDataFormatIsEqual(
    _In_
    ACXDATAFORMAT DataFormat1,
    _In_
    ACXDATAFORMAT DataFormat2
    )
{
    return ((PFN_ACXDATAFORMATISEQUAL) AcxFunctions[AcxDataFormatIsEqualTableIndex])(AcxDriverGlobals, DataFormat1, DataFormat2);
}

typedef
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATLISTCREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_DATAFORMAT_LIST_CONFIG Config,
    _Out_
    ACXDATAFORMATLIST* DataFormatList
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatListCreate(
    _In_
    WDFDEVICE Device,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_DATAFORMAT_LIST_CONFIG Config,
    _Out_
    ACXDATAFORMATLIST* DataFormatList
    )
{
    return ((PFN_ACXDATAFORMATLISTCREATE) AcxFunctions[AcxDataFormatListCreateTableIndex])(AcxDriverGlobals, Device, Attributes, Config, DataFormatList);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATLISTADDDATAFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _In_
    ACXDATAFORMAT AcxDataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatListAddDataFormat(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _In_
    ACXDATAFORMAT AcxDataFormat
    )
{
    return ((PFN_ACXDATAFORMATLISTADDDATAFORMAT) AcxFunctions[AcxDataFormatListAddDataFormatTableIndex])(AcxDriverGlobals, AcxDataFormatList, AcxDataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATLISTREMOVEDATAFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _In_
    ACXDATAFORMAT AcxDataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatListRemoveDataFormat(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _In_
    ACXDATAFORMAT AcxDataFormat
    )
{
    return ((PFN_ACXDATAFORMATLISTREMOVEDATAFORMAT) AcxFunctions[AcxDataFormatListRemoveDataFormatTableIndex])(AcxDriverGlobals, AcxDataFormatList, AcxDataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATLISTASSIGNDEFAULTDATAFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _In_
    ACXDATAFORMAT AcxDataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatListAssignDefaultDataFormat(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _In_
    ACXDATAFORMAT AcxDataFormat
    )
{
    return ((PFN_ACXDATAFORMATLISTASSIGNDEFAULTDATAFORMAT) AcxFunctions[AcxDataFormatListAssignDefaultDataFormatTableIndex])(AcxDriverGlobals, AcxDataFormatList, AcxDataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATLISTRETRIEVEDEFAULTDATAFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Out_
    ACXDATAFORMAT* AcxDataFormat
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatListRetrieveDefaultDataFormat(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Out_
    ACXDATAFORMAT* AcxDataFormat
    )
{
    return ((PFN_ACXDATAFORMATLISTRETRIEVEDEFAULTDATAFORMAT) AcxFunctions[AcxDataFormatListRetrieveDefaultDataFormatTableIndex])(AcxDriverGlobals, AcxDataFormatList, AcxDataFormat);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATLISTREMOVEDATAFORMATS)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatListRemoveDataFormats(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList
    )
{
    return ((PFN_ACXDATAFORMATLISTREMOVEDATAFORMATS) AcxFunctions[AcxDataFormatListRemoveDataFormatsTableIndex])(AcxDriverGlobals, AcxDataFormatList);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
VOID
(NTAPI *PFN_ACXDATAFORMATLISTBEGINITERATION)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Inout_
    PACX_DATAFORMAT_LIST_ITERATOR Iterator
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
VOID
AcxDataFormatListBeginIteration(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Inout_
    PACX_DATAFORMAT_LIST_ITERATOR Iterator
    )
{
    ((PFN_ACXDATAFORMATLISTBEGINITERATION) AcxFunctions[AcxDataFormatListBeginIterationTableIndex])(AcxDriverGlobals, AcxDataFormatList, Iterator);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXDATAFORMATLISTRETRIEVENEXTFORMAT)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Inout_
    PACX_DATAFORMAT_LIST_ITERATOR Iterator,
    _Out_
    ACXDATAFORMAT* Format
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
AcxDataFormatListRetrieveNextFormat(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Inout_
    PACX_DATAFORMAT_LIST_ITERATOR Iterator,
    _Out_
    ACXDATAFORMAT* Format
    )
{
    return ((PFN_ACXDATAFORMATLISTRETRIEVENEXTFORMAT) AcxFunctions[AcxDataFormatListRetrieveNextFormatTableIndex])(AcxDriverGlobals, AcxDataFormatList, Iterator, Format);
}

typedef
_IRQL_requires_max_(DISPATCH_LEVEL)
WDFAPI
VOID
(NTAPI *PFN_ACXDATAFORMATLISTENDITERATION)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Inout_
    PACX_DATAFORMAT_LIST_ITERATOR Iterator
    );

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
VOID
AcxDataFormatListEndIteration(
    _In_
    ACXDATAFORMATLIST AcxDataFormatList,
    _Inout_
    PACX_DATAFORMAT_LIST_ITERATOR Iterator
    )
{
    ((PFN_ACXDATAFORMATLISTENDITERATION) AcxFunctions[AcxDataFormatListEndIterationTableIndex])(AcxDriverGlobals, AcxDataFormatList, Iterator);
}

#endif

WDF_EXTERN_C_END

#endif
