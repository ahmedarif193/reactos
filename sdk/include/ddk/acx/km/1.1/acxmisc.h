/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Audio class extension 1.1 interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _ACXMISC_H_
#define _ACXMISC_H_

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

typedef struct _ACX_OBJECTBAG_CONFIG {
    ULONG               Size;
    ULONG               Flags;
    WDFKEY              ParentKey;
    PCUNICODE_STRING    Name;
    HANDLE              Handle;
} ACX_OBJECTBAG_CONFIG, *PACX_OBJECTBAG_CONFIG;

typedef enum _ACX_OBJECTBAG_CONFIG_FLAGS {
    AcxObjectBagConfigNoFlags           = 0x00000000,
    AcxObjectBagConfigEnableReads       = 0x00000001,
    AcxObjectBagConfigEnableWrites      = 0x00000002,
    AcxObjectBagConfigFailIfExist       = 0x00000004,
    AcxObjectBagConfigOpenWithHandle    = 0x00000008,
    AcxObjectBagConfigValidFlags        = 0x0000000F
} ACX_OBJECTBAG_CONFIG_FLAGS;

VOID
FORCEINLINE
ACX_OBJECTBAG_CONFIG_INIT(
    _Out_ PACX_OBJECTBAG_CONFIG Config
    )
{
    RtlZeroMemory(Config, sizeof(ACX_OBJECTBAG_CONFIG));
    Config->Size = ACX_STRUCTURE_SIZE(ACX_OBJECTBAG_CONFIG);
    Config->Flags = AcxObjectBagConfigEnableReads | AcxObjectBagConfigEnableWrites;
}

#define DECLARE_CONST_ACXOBJECTBAG_SYSTEM_PROPERTY_NAME(name) \
    DECLARE_CONST_UNICODE_STRING(name, L"sys_" #name)

#define DECLARE_CONST_ACXOBJECTBAG_DRIVER_PROPERTY_NAME(ven, name) \
    DECLARE_CONST_UNICODE_STRING(name, L"drv_" #ven L"_" #name)

#define DECLARE_CONST_ACXOBJECTBAG_USB_PROPERTY_NAME(name) \
    DECLARE_CONST_UNICODE_STRING(name, L"usb_" #name)

#define DECLARE_CONST_ACXOBJECTBAG_HDAUDIO_PROPERTY_NAME(name) \
    DECLARE_CONST_UNICODE_STRING(name, L"hda_" #name)

#define DECLARE_CONST_ACXOBJECTBAG_SOUNDWIRE_PROPERTY_NAME(name) \
    DECLARE_CONST_UNICODE_STRING(name, L"sdca_" #name)

#define DECLARE_CONST_ACXOBJECTBAG_BLUETOOTH_PROPERTY_NAME(name) \
    DECLARE_CONST_UNICODE_STRING(name, L"Bluetooth_" #name)

#define DECLARE_CONST_ACXOBJECTBAG_BLUETOOTHLEAUDIO_PROPERTY_NAME(name) \
    DECLARE_CONST_UNICODE_STRING(name, L"BluetoothLEAudio_" #name)

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGCREATE)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_OBJECTBAG_CONFIG Config,
    _Out_
    ACXOBJECTBAG* ObjectBag
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagCreate(
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_OBJECTBAG_CONFIG Config,
    _Out_
    ACXOBJECTBAG* ObjectBag
    )
{
    return ((PFN_ACXOBJECTBAGCREATE) AcxFunctions[AcxObjectBagCreateTableIndex])(AcxDriverGlobals, Attributes, Config, ObjectBag);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGOPEN)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_OBJECTBAG_CONFIG Config,
    _Out_
    ACXOBJECTBAG* ObjectBag
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagOpen(
    _In_
    PWDF_OBJECT_ATTRIBUTES Attributes,
    _In_
    PACX_OBJECTBAG_CONFIG Config,
    _Out_
    ACXOBJECTBAG* ObjectBag
    )
{
    return ((PFN_ACXOBJECTBAGOPEN) AcxFunctions[AcxObjectBagOpenTableIndex])(AcxDriverGlobals, Attributes, Config, ObjectBag);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDI1)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    CHAR Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddI1(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    CHAR Value
    )
{
    return ((PFN_ACXOBJECTBAGADDI1) AcxFunctions[AcxObjectBagAddI1TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDUI1)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    UCHAR Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddUI1(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    UCHAR Value
    )
{
    return ((PFN_ACXOBJECTBAGADDUI1) AcxFunctions[AcxObjectBagAddUI1TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDI2)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    SHORT Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddI2(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    SHORT Value
    )
{
    return ((PFN_ACXOBJECTBAGADDI2) AcxFunctions[AcxObjectBagAddI2TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDUI2)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    USHORT Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddUI2(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    USHORT Value
    )
{
    return ((PFN_ACXOBJECTBAGADDUI2) AcxFunctions[AcxObjectBagAddUI2TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDI4)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddI4(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG Value
    )
{
    return ((PFN_ACXOBJECTBAGADDI4) AcxFunctions[AcxObjectBagAddI4TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDUI4)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddUI4(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG Value
    )
{
    return ((PFN_ACXOBJECTBAGADDUI4) AcxFunctions[AcxObjectBagAddUI4TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDI8)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG64 Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddI8(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG64 Value
    )
{
    return ((PFN_ACXOBJECTBAGADDI8) AcxFunctions[AcxObjectBagAddI8TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDUI8)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG64 Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddUI8(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG64 Value
    )
{
    return ((PFN_ACXOBJECTBAGADDUI8) AcxFunctions[AcxObjectBagAddUI8TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDSIZET)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    size_t Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddSizeT(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    size_t Value
    )
{
    return ((PFN_ACXOBJECTBAGADDSIZET) AcxFunctions[AcxObjectBagAddSizeTTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDGUID)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    GUID Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddGuid(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    GUID Value
    )
{
    return ((PFN_ACXOBJECTBAGADDGUID) AcxFunctions[AcxObjectBagAddGuidTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDSTRING)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFSTRING Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddString(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFSTRING Value
    )
{
    return ((PFN_ACXOBJECTBAGADDSTRING) AcxFunctions[AcxObjectBagAddStringTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDMULTISTRING)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFCOLLECTION Values
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddMultiString(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFCOLLECTION Values
    )
{
    return ((PFN_ACXOBJECTBAGADDMULTISTRING) AcxFunctions[AcxObjectBagAddMultiStringTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Values);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDUNICODESTRING)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    PCUNICODE_STRING Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddUnicodeString(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    PCUNICODE_STRING Value
    )
{
    return ((PFN_ACXOBJECTBAGADDUNICODESTRING) AcxFunctions[AcxObjectBagAddUnicodeStringTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGADDBLOB)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFMEMORY Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagAddBlob(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFMEMORY Value
    )
{
    return ((PFN_ACXOBJECTBAGADDBLOB) AcxFunctions[AcxObjectBagAddBlobTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEI1)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    CHAR* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveI1(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    CHAR* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEI1) AcxFunctions[AcxObjectBagRetrieveI1TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEUI1)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    UCHAR* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveUI1(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    UCHAR* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEUI1) AcxFunctions[AcxObjectBagRetrieveUI1TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEI2)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    SHORT* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveI2(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    SHORT* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEI2) AcxFunctions[AcxObjectBagRetrieveI2TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEUI2)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    USHORT* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveUI2(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    USHORT* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEUI2) AcxFunctions[AcxObjectBagRetrieveUI2TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEI4)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveI4(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEI4) AcxFunctions[AcxObjectBagRetrieveI4TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEUI4)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveUI4(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEUI4) AcxFunctions[AcxObjectBagRetrieveUI4TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEI8)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG64* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveI8(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    LONG64* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEI8) AcxFunctions[AcxObjectBagRetrieveI8TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEUI8)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG64* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveUI8(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    ULONG64* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEUI8) AcxFunctions[AcxObjectBagRetrieveUI8TableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVESIZET)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    size_t* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveSizeT(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    size_t* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVESIZET) AcxFunctions[AcxObjectBagRetrieveSizeTTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEGUID)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    GUID* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveGuid(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    GUID* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEGUID) AcxFunctions[AcxObjectBagRetrieveGuidTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVESTRING)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFSTRING Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveString(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    WDFSTRING Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVESTRING) AcxFunctions[AcxObjectBagRetrieveStringTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEMULTISTRING)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    PWDF_OBJECT_ATTRIBUTES StringsAttributes,
    _In_
    WDFCOLLECTION Values
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveMultiString(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    PWDF_OBJECT_ATTRIBUTES StringsAttributes,
    _In_
    WDFCOLLECTION Values
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEMULTISTRING) AcxFunctions[AcxObjectBagRetrieveMultiStringTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, StringsAttributes, Values);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEUNICODESTRING)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    PUNICODE_STRING Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveUnicodeString(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_
    PUNICODE_STRING Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEUNICODESTRING) AcxFunctions[AcxObjectBagRetrieveUnicodeStringTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, Value);
}

typedef
_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
WDFAPI
NTSTATUS
(NTAPI *PFN_ACXOBJECTBAGRETRIEVEBLOB)(
    _In_
    PACX_DRIVER_GLOBALS DriverGlobals,
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_opt_
    PWDF_OBJECT_ATTRIBUTES ValueAttributes,
    _Out_
    WDFMEMORY* Value
    );

_Must_inspect_result_
_IRQL_requires_max_(PASSIVE_LEVEL)
FORCEINLINE
NTSTATUS
AcxObjectBagRetrieveBlob(
    _In_
    ACXOBJECTBAG ObjectBag,
    _In_
    PCUNICODE_STRING ValueName,
    _In_opt_
    PWDF_OBJECT_ATTRIBUTES ValueAttributes,
    _Out_
    WDFMEMORY* Value
    )
{
    return ((PFN_ACXOBJECTBAGRETRIEVEBLOB) AcxFunctions[AcxObjectBagRetrieveBlobTableIndex])(AcxDriverGlobals, ObjectBag, ValueName, ValueAttributes, Value);
}

#endif

WDF_EXTERN_C_END

#endif
