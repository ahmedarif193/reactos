/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WiFiCx 1.2 class extension interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _WIFIFUNCENUM_H_
#define _WIFIFUNCENUM_H_

extern PWIFI_DRIVER_GLOBALS WifiDriverGlobals;

#define WIFI_FIRST_VERSION_SUPPORTING_CLIENT_VERSION_HIGHER_THAN_FRAMEWORK 0

#define WIFI_ALWAYS_AVAILABLE_FUNCTION_COUNT  39

#if defined(WIFI_MINIMUM_VERSION_REQUIRED)

    #if WIFI_MINIMUM_VERSION_REQUIRED > WIFI_VERSION_MINOR
    #error WIFI_MINIMUM_VERSION_REQUIRED > WIFI_VERSION_MINOR
    #endif

#endif

#if defined(WIFI_MINIMUM_VERSION_REQUIRED) && (WIFI_VERSION_MINOR == WIFI_MINIMUM_VERSION_REQUIRED)

    #if !defined(WIFI_EVERYTHING_ALWAYS_AVAILABLE)
    #define WIFI_EVERYTHING_ALWAYS_AVAILABLE
    #endif

#endif

extern BOOLEAN WifiClientVersionHigherThanFramework;

extern ULONG   WifiFunctionCount;

extern ULONG   WifiStructureCount;
extern WDF_STRUCT_INFO WifiStructures;

extern PCWSTR  WifiFrameworkExtensionName;

#ifndef WIFI_STUB

    #ifndef WIFI_VERSION_MAJOR
    #error  WIFI_VERSION_MAJOR is not defined
    #endif

    #ifndef WIFI_VERSION_MINOR
    #error  WIFI_VERSION_MINOR is not defined
    #endif

    #ifndef WIFI_MINIMUM_VERSION_REQUIRED
    #define WIFI_MINIMUM_VERSION_REQUIRED WIFI_VERSION_MINOR
    #endif

    __declspec(selectany)
    ULONG WifiMinimumVersionRequired = WIFI_MINIMUM_VERSION_REQUIRED;

#else

    extern ULONG WifiMinimumVersionRequired;

#endif

#define WIFI_IS_FUNCTION_AVAILABLE(FunctionName)                               \
(                                                                              \
    (FunctionName ## TableIndex < WIFI_ALWAYS_AVAILABLE_FUNCTION_COUNT)        \
    ||                                                                         \
    (!WifiClientVersionHigherThanFramework)                                    \
    ||                                                                         \
    (FunctionName ## TableIndex < WifiFunctionCount)                           \
)

#define WIFI_IS_STRUCTURE_AVAILABLE(StructName)                                \
(                                                                              \
    (!WifiClientVersionHigherThanFramework)                                    \
    ||                                                                         \
    (INDEX_ ## StructName < WifiStructureCount)                                \
)

#define WIFI_IS_FIELD_AVAILABLE(StructName, FieldName)                         \
(                                                                              \
    (!WifiClientVersionHigherThanFramework)                                    \
    ||                                                                         \
    (                                                                          \
        (INDEX_ ## StructName < WifiStructureCount)                            \
        &&                                                                     \
        (FIELD_OFFSET(StructName, FieldName) < WifiStructures[INDEX_ ## StructName])\
    )                                                                          \
)

#if defined(WIFI_EVERYTHING_ALWAYS_AVAILABLE)

#define WIFI_STRUCTURE_SIZE(StructName)  (ULONG)sizeof(StructName)

#else

#define WIFI_STRUCTURE_SIZE(StructName)                                        \
(ULONG)                                                                        \
(                                                                              \
    WifiClientVersionHigherThanFramework                                       \
        ? (                                                                    \
            (INDEX_ ## StructName < WifiStructureCount)                        \
            ? WifiStructures[INDEX_ ## StructName]                             \
            : (size_t)(-1)                                                     \
          )                                                                    \
        : sizeof(StructName)                                                   \
)

#endif

typedef enum _WIFIFUNCENUM {

    WifiDeviceInitConfigTableIndex = 0,
    WifiDeviceGetOsWdiVersionTableIndex = 1,
    WifiDeviceSetDeviceCapabilitiesTableIndex = 2,
    WifiDeviceSetStationCapabilitiesTableIndex = 3,
    WifiDeviceSetWiFiDirectCapabilitiesTableIndex = 4,
    WifiDeviceSetBandCapabilitiesTableIndex = 5,
    WifiDeviceSetPhyCapabilitiesTableIndex = 6,
    WifiDeviceInitializeTableIndex = 7,
    WifiDeviceReceiveIndicationTableIndex = 8,
    WifiAdapterInitializeTableIndex = 9,
    WifiRequestGetInOutBufferTableIndex = 10,
    WifiRequestGetMessageIdTableIndex = 11,
    WifiRequestSetBytesNeededTableIndex = 12,
    WifiRequestCompleteTableIndex = 13,
    WifiAdapterPowerOffloadSetRsnRekeyCapabilitiesTableIndex = 14,
    WifiAdapterSetWakeCapabilitiesTableIndex = 15,
    WifiAdapterReportWakeReasonTableIndex = 16,
    WifiDirectDeviceCreateTableIndex = 17,
    WifiDirectDeviceInitializeTableIndex = 18,
    WifiAdapterGetPortIdTableIndex = 19,
    WifiAdapterInitGetTypeTableIndex = 20,
    WifiAdapterGetTypeTableIndex = 21,
    WifiDirectDeviceGetPortIdTableIndex = 22,
    WifiAdapterInitAddTxDemuxTableIndex = 23,
    WifiTxQueueGetDemuxPeerAddressTableIndex = 24,
    WifiTxQueueGetDemuxWmmInfoTableIndex = 25,
    WifiAdapterAddPeerTableIndex = 26,
    WifiAdapterRemovePeerTableIndex = 27,
    WifiPowerOffloadGetTypeTableIndex = 28,
    WifiPowerOffloadGetAdapterTableIndex = 29,
    WifiPowerOffloadGet80211RSNRekeyParametersTableIndex = 30,
    WifiDeviceGetPowerOffloadListTableIndex = 31,
    WifiPowerOffloadListGetCountTableIndex = 32,
    WifiPowerOffloadListGetElementTableIndex = 33,
    WifiWakeSourceGetTypeTableIndex = 34,
    WifiWakeSourceGetAdapterTableIndex = 35,
    WifiDeviceGetWakeSourceListTableIndex = 36,
    WifiWakeSourceListGetCountTableIndex = 37,
    WifiWakeSourceListGetElementTableIndex = 38,
    WifiPowerOffloadGetActionFrameWakePatternParametersTableIndex = 39,
    WifiFunctionTableNumEntries = 40,
} WIFIFUNCENUM;

typedef enum _WIFISTRUCTENUM {

    INDEX_WIFI_ADAPTER_ATTRIBUTES                      = 0,
    INDEX_WIFI_ADAPTER_POWER_OFFLOAD_RSN_REKEY_CAPABILITIES = 1,
    INDEX_WIFI_ADAPTER_TX_DEMUX                        = 2,
    INDEX_WIFI_ADAPTER_WAKE_CAPABILITIES               = 3,
    INDEX_WIFI_BAND_CAPABILITIES                       = 4,
    INDEX_WIFI_BAND_INFO                               = 5,
    INDEX_WIFI_DEVICE_CAPABILITIES                     = 6,
    INDEX_WIFI_DEVICE_CONFIG                           = 7,
    INDEX_WIFI_DRIVER_GLOBALS                          = 8,
    INDEX_WIFI_PHY_CAPABILITIES                        = 9,
    INDEX_WIFI_PHY_INFO                                = 10,
    INDEX_WIFI_POWER_OFFLOAD_80211RSNREKEY_PARAMETERS  = 11,
    INDEX_WIFI_POWER_OFFLOAD_LIST                      = 12,
    INDEX_WIFI_STATION_CAPABILITIES                    = 13,
    INDEX_WIFI_WAKE_SOURCE_LIST                        = 14,
    INDEX_WIFI_WIFIDIRECT_CAPABILITIES                 = 15,
    INDEX_WIFI_POWER_OFFLOAD_ACTION_FRAME_WAKE_PATTERN_PARAMETERS = 16,
    WIFI_STRUCTURE_TABLE_NUM_ENTRIES                   = 17,
} WIFISTRUCTENUM;

#define Wifi_STRUCTURE_TABLE_NUM_ENTRIES WIFI_STRUCTURE_TABLE_NUM_ENTRIES

#endif
