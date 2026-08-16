/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Data engine: system/process sampling, services, users,
 *              startup items, app history, process actions
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#include <initguid.h>
#include <acpiioct.h>
#include <poclass.h>
#include <sensors.h>
#include <sensorsapi.h>
#include <setupapi.h>

#include "battery_telemetry.h"

namespace Data {

SysSnapshot g;

/* ------------------------------------------------------------------ */
/*  Dynamic API resolution (keep working on older kernels)             */
/* ------------------------------------------------------------------ */

typedef BOOL (WINAPI *PFN_QueryFullProcessImageNameW)(HANDLE, DWORD, LPWSTR, PDWORD);
typedef BOOL (WINAPI *PFN_IsHungAppWindow)(HWND);
typedef BOOL (WINAPI *PFN_IsWow64Process)(HANDLE, PBOOL);
typedef BOOL (WINAPI *PFN_GetProcessInformation)(HANDLE, PROCESS_INFORMATION_CLASS, PVOID, DWORD);

static PFN_QueryFullProcessImageNameW pQueryFullProcessImageNameW;
static PFN_IsHungAppWindow pIsHungAppWindow;
static PFN_IsWow64Process pIsWow64Process;
static PFN_GetProcessInformation pGetProcessInformation;

/* ProcessPowerThrottlingState (Win10 EcoQoS); harmless failure elsewhere */
#define TM_ProcessPowerThrottlingState ((PROCESSINFOCLASS)77)
typedef struct _TM_POWER_THROTTLING_STATE
{
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} TM_POWER_THROTTLING_STATE;
#define TM_POWER_THROTTLING_VERSION        1
#define TM_POWER_THROTTLING_EXECUTION_SPEED 0x1

/* thread state constants for suspend detection */
#define TM_THREADSTATE_WAITING 5
#define TM_WAITREASON_SUSPENDED 5

/* ------------------------------------------------------------------ */
/*  Internal state                                                     */
/* ------------------------------------------------------------------ */

static PUCHAR s_procBuf;
static ULONG  s_procBufSize;
static int    s_procBufLowTicks;

struct PrevProc
{
    ULONG    pid;
    LONGLONG createTime;
    LONGLONG cpu100ns;
    ULONGLONG ioBytes;
    ProcExtra* extra;
};
static Vec<PrevProc> s_prev;

struct WndEntry
{
    ULONG pid;
    HWND  hwnd;
    WCHAR title[128];
};
static Vec<WndEntry> s_wnds;

static Vec<ProcExtra*> s_extras;

static Vec<SvcRow> s_services;
struct SvcHostEntry { ULONG pid; WCHAR names[512]; WCHAR group[64]; };
static Vec<SvcHostEntry> s_svchostMap;
struct SvcGroupCache { WCHAR name[96]; WCHAR group[64]; };
static Vec<SvcGroupCache> s_svcGroups;

static Vec<UserRow> s_users;
static Vec<StartupRow> s_startup;
static Vec<AppHistRow> s_appHist;
static Vec<TelemetryRow> s_telemetry;
static ISensorManager* s_sensorManager;
static DWORD s_lastTelemetryRefresh;
static DWORD s_lastTelemetryDeviceScan;
static FILETIME s_appHistSince;
static BOOL s_appHistDirty;
static DWORD s_appHistLastSave;
static DWORD s_currentSession;
static DWORD s_currentPid;

struct AppHistDiskRow
{
    WCHAR image[64];
    WCHAR displayName[128];
    WCHAR path[MAX_PATH];
    LONGLONG cpu100ns;
    ULONGLONG netBytes;
    ULONGLONG notificationBytes;
};

static const WCHAR* APP_HISTORY_KEY =
    L"Software\\ReactOS\\TaskMgr11\\AppHistory";
static const DWORD APP_HISTORY_VERSION = 1;
static const DWORD APP_HISTORY_MAX_ROWS = 256;

static void LoadAppHistory(void);
static void SaveAppHistory(void);
static void DestroyAppHistory(void);

static LONGLONG s_lastQpc;
static double   s_qpcFreq;
static LONGLONG s_prevIdle, s_prevKernel, s_prevUser;
static LONGLONG s_prevCpuIdle[64], s_prevCpuKernel[64], s_prevCpuUser[64];
static ULONGLONG s_prevIoRead, s_prevIoWrite;
static BOOL s_first = TRUE;

struct DiskSampler
{
    HANDLE handle;
    DISK_PERFORMANCE previous;
    BOOL havePrevious;
    BOOL countersEnabled;
};
static DiskSampler s_diskSamplers[TM_MAX_DISKS];

struct StorageTelemetryDevice
{
    HANDLE handle;
    DWORD number;
    BYTE* temperatureBuffer;
    ULONG temperatureBufferSize;
    WCHAR model[160];
    WCHAR interfaceName[48];
};
static Vec<StorageTelemetryDevice> s_storageTelemetryDevices;

struct AcpiTelemetryDevice
{
    HANDLE handle;
    DWORD openError;
    ULONG instance;
    WCHAR name[160];
};
static Vec<AcpiTelemetryDevice> s_thermalTelemetryDevices;
static Vec<AcpiTelemetryDevice> s_fanTelemetryDevices;

struct BatteryTelemetryDevice
{
    HANDLE handle;
    DWORD openError;
    ULONG instance;
    WCHAR name[160];
};
static Vec<BatteryTelemetryDevice> s_batteryTelemetryDevices;

struct SensorTelemetryDevice
{
    ISensor* sensor;
    GUID id;
    GUID category;
    GUID type;
    PROPERTYKEY* fieldKeys;
    ULONG fieldCount;
    WCHAR name[160];
    WCHAR source[192];
};
static Vec<SensorTelemetryDevice> s_sensorTelemetryDevices;

/* network deltas */
static DWORD s_netIfIndex = (DWORD)-1;
static DWORD s_prevNetIn, s_prevNetOut;
static int   s_netInfoAge = 999;
static PMIB_IFTABLE s_netTable;
static ULONG s_netTableSize;
static BOOL  s_wsaStarted;

static WCHAR s_winDir[MAX_PATH];

static void CopyDescriptorText(const BYTE* buffer, DWORD size, DWORD offset, WCHAR* output, int outputCount);
static const WCHAR* StorageBusName(STORAGE_BUS_TYPE bus);

static const GUID s_storageTelemetryFormat = { 0xd174a22d, 0x836f, 0x4654, { 0xb2, 0x58, 0x2f, 0x5f, 0x75, 0xe4, 0x73, 0x23 } };
static const GUID s_thermalTelemetryFormat = { 0x934ec8a1, 0xb8cb, 0x4a3d, { 0x9a, 0x78, 0x28, 0xc7, 0x84, 0x1e, 0xc0, 0xc7 } };
static const GUID s_fanTelemetryFormat = { 0x0a1409c4, 0x57df, 0x4c20, { 0x9d, 0x60, 0x7f, 0xa6, 0x18, 0x9b, 0x36, 0x44 } };
static const GUID s_batteryTelemetryFormat = { 0x421c6529, 0x0a9d, 0x4f49, { 0x88, 0xc2, 0x6d, 0xca, 0xb3, 0x77, 0x59, 0x3c } };
static const GUID s_systemPowerTelemetryFormat = { 0x3f8f82b9, 0x3f33, 0x4636, { 0xa5, 0xad, 0x1c, 0xe7, 0xef, 0xe7, 0x21, 0xcf } };

struct SensorFieldInfo
{
    ULONG pid;
    ULONG kind;
    const WCHAR* name;
    const WCHAR* unit;
};

struct SensorFieldSet
{
    const GUID* format;
    const SensorFieldInfo* fields;
    ULONG count;
};

static const SensorFieldInfo s_commonFields[] =
{
    { 2, TEL_OTHER, L"Timestamp", L"UTC" },
};

static const SensorFieldInfo s_biometricFields[] =
{
    { 2, TEL_OTHER, L"Human presence", L"" },
    { 3, TEL_OTHER, L"Human proximity", L"m" },
    { 4, TEL_OTHER, L"Touch state", L"" },
};

static const SensorFieldInfo s_electricalFields[] =
{
    { 2, TEL_VOLTAGE, L"Voltage", L"V" },
    { 3, TEL_CURRENT, L"Current", L"A" },
    { 4, TEL_OTHER, L"Capacitance", L"F" },
    { 5, TEL_OTHER, L"Resistance", L"ohm" },
    { 6, TEL_OTHER, L"Inductance", L"H" },
    { 7, TEL_POWER, L"Power", L"W" },
    { 8, TEL_OTHER, L"Range", L"%" },
    { 9, TEL_OTHER, L"Frequency", L"Hz" },
};

static const SensorFieldInfo s_environmentalFields[] =
{
    { 2, TEL_TEMPERATURE, L"Temperature", L"°C" },
    { 3, TEL_HUMIDITY, L"Humidity", L"%" },
    { 4, TEL_PRESSURE, L"Atmospheric pressure", L"bar" },
    { 5, TEL_OTHER, L"Wind direction", L"°" },
    { 6, TEL_OTHER, L"Wind speed", L"m/s" },
};

static const SensorFieldInfo s_lightFields[] =
{
    { 2, TEL_LIGHT, L"Light level", L"lux" },
    { 3, TEL_LIGHT, L"Light temperature", L"K" },
    { 4, TEL_LIGHT, L"Chromaticity", L"" },
};

static const SensorFieldInfo s_locationFields[] =
{
    { 2, TEL_LOCATION, L"Latitude", L"°" },
    { 3, TEL_LOCATION, L"Longitude", L"°" },
    { 4, TEL_LOCATION, L"Altitude above sea level", L"m" },
    { 5, TEL_LOCATION, L"Altitude above ellipsoid", L"m" },
    { 6, TEL_LOCATION, L"Speed", L"kn" },
    { 7, TEL_LOCATION, L"True heading", L"°" },
    { 8, TEL_LOCATION, L"Magnetic heading", L"°" },
    { 9, TEL_LOCATION, L"Magnetic variation", L"°" },
    { 10, TEL_LOCATION, L"Fix quality", L"" },
    { 11, TEL_LOCATION, L"Fix type", L"" },
    { 12, TEL_LOCATION, L"Position dilution of precision", L"" },
    { 13, TEL_LOCATION, L"Horizontal dilution of precision", L"" },
    { 14, TEL_LOCATION, L"Vertical dilution of precision", L"" },
    { 15, TEL_LOCATION, L"Satellites used", L"" },
    { 16, TEL_LOCATION, L"Used satellite PRNs", L"" },
    { 17, TEL_LOCATION, L"Satellites in view", L"" },
    { 18, TEL_LOCATION, L"Visible satellite PRNs", L"" },
    { 19, TEL_LOCATION, L"Satellite elevations", L"°" },
    { 20, TEL_LOCATION, L"Satellite azimuths", L"°" },
    { 21, TEL_LOCATION, L"Satellite signal-to-noise ratios", L"dB" },
    { 22, TEL_LOCATION, L"Error radius", L"m" },
    { 23, TEL_LOCATION, L"Address line 1", L"" },
    { 24, TEL_LOCATION, L"Address line 2", L"" },
    { 25, TEL_LOCATION, L"City", L"" },
    { 26, TEL_LOCATION, L"State or province", L"" },
    { 27, TEL_LOCATION, L"Postal code", L"" },
    { 28, TEL_LOCATION, L"Country or region", L"" },
    { 29, TEL_LOCATION, L"Ellipsoid altitude error", L"m" },
    { 30, TEL_LOCATION, L"Sea-level altitude error", L"m" },
    { 31, TEL_LOCATION, L"GPS selection mode", L"" },
    { 32, TEL_LOCATION, L"GPS operation mode", L"" },
    { 33, TEL_LOCATION, L"GPS status", L"" },
    { 34, TEL_LOCATION, L"Geoidal separation", L"m" },
    { 35, TEL_LOCATION, L"DGPS data age", L"s" },
    { 36, TEL_LOCATION, L"Antenna altitude", L"m" },
    { 37, TEL_LOCATION, L"Differential reference station", L"" },
    { 38, TEL_LOCATION, L"NMEA sentence", L"" },
    { 39, TEL_LOCATION, L"Visible satellite IDs", L"" },
    { 40, TEL_LOCATION, L"Location source", L"" },
    { 41, TEL_LOCATION, L"Used satellite PRNs and constellations", L"" },
};

static const SensorFieldInfo s_mechanicalFields[] =
{
    { 2, TEL_OTHER, L"Boolean switch", L"" },
    { 3, TEL_OTHER, L"Multivalue switch", L"" },
    { 4, TEL_OTHER, L"Force", L"N" },
    { 5, TEL_PRESSURE, L"Absolute pressure", L"Pa" },
    { 6, TEL_PRESSURE, L"Gauge pressure", L"Pa" },
    { 7, TEL_OTHER, L"Strain", L"" },
    { 8, TEL_OTHER, L"Weight", L"kg" },
    { 10, TEL_OTHER, L"Boolean switch array", L"" },
};

static const SensorFieldInfo s_motionFields[] =
{
    { 2, TEL_OTHER, L"Acceleration X", L"g" },
    { 3, TEL_OTHER, L"Acceleration Y", L"g" },
    { 4, TEL_OTHER, L"Acceleration Z", L"g" },
    { 5, TEL_OTHER, L"Angular acceleration X", L"°/s²" },
    { 6, TEL_OTHER, L"Angular acceleration Y", L"°/s²" },
    { 7, TEL_OTHER, L"Angular acceleration Z", L"°/s²" },
    { 8, TEL_OTHER, L"Speed", L"m/s" },
    { 9, TEL_OTHER, L"Motion state", L"" },
    { 10, TEL_OTHER, L"Angular velocity X", L"°/s" },
    { 11, TEL_OTHER, L"Angular velocity Y", L"°/s" },
    { 12, TEL_OTHER, L"Angular velocity Z", L"°/s" },
};

static const SensorFieldInfo s_orientationFields[] =
{
    { 2, TEL_OTHER, L"Tilt X", L"°" },
    { 3, TEL_OTHER, L"Tilt Y", L"°" },
    { 4, TEL_OTHER, L"Tilt Z", L"°" },
    { 5, TEL_OTHER, L"Magnetic heading X", L"°" },
    { 6, TEL_OTHER, L"Magnetic heading Y", L"°" },
    { 7, TEL_OTHER, L"Magnetic heading Z", L"°" },
    { 8, TEL_OTHER, L"Distance X", L"m" },
    { 9, TEL_OTHER, L"Distance Y", L"m" },
    { 10, TEL_OTHER, L"Distance Z", L"m" },
    { 11, TEL_OTHER, L"Compensated magnetic-north heading", L"°" },
    { 12, TEL_OTHER, L"Compensated true-north heading", L"°" },
    { 13, TEL_OTHER, L"Magnetic-north heading", L"°" },
    { 14, TEL_OTHER, L"True-north heading", L"°" },
    { 15, TEL_OTHER, L"Quadrant angle", L"°" },
    { 16, TEL_OTHER, L"Rotation matrix", L"" },
    { 17, TEL_OTHER, L"Quaternion", L"" },
    { 18, TEL_OTHER, L"Simple device orientation", L"" },
    { 19, TEL_OTHER, L"Magnetic field X", L"mG" },
    { 20, TEL_OTHER, L"Magnetic field Y", L"mG" },
    { 21, TEL_OTHER, L"Magnetic field Z", L"mG" },
    { 22, TEL_OTHER, L"Magnetometer accuracy", L"" },
};

static const SensorFieldInfo s_scannerFields[] =
{
    { 2, TEL_OTHER, L"RFID tag", L"" },
};

static const SensorFieldSet s_sensorFieldSets[] =
{
    { &SENSOR_DATA_TYPE_COMMON_GUID, s_commonFields, _countof(s_commonFields) },
    { &SENSOR_DATA_TYPE_BIOMETRIC_GUID, s_biometricFields, _countof(s_biometricFields) },
    { &SENSOR_DATA_TYPE_ELECTRICAL_GUID, s_electricalFields, _countof(s_electricalFields) },
    { &SENSOR_DATA_TYPE_ENVIRONMENTAL_GUID, s_environmentalFields, _countof(s_environmentalFields) },
    { &SENSOR_DATA_TYPE_LIGHT_GUID, s_lightFields, _countof(s_lightFields) },
    { &SENSOR_DATA_TYPE_LOCATION_GUID, s_locationFields, _countof(s_locationFields) },
    { &SENSOR_DATA_TYPE_GUID_MECHANICAL_GUID, s_mechanicalFields, _countof(s_mechanicalFields) },
    { &SENSOR_DATA_TYPE_MOTION_GUID, s_motionFields, _countof(s_motionFields) },
    { &SENSOR_DATA_TYPE_ORIENTATION_GUID, s_orientationFields, _countof(s_orientationFields) },
    { &SENSOR_DATA_TYPE_SCANNER_GUID, s_scannerFields, _countof(s_scannerFields) },
};

static const WCHAR*
SensorStateName(SensorState state)
{
    switch (state)
    {
    case SENSOR_STATE_READY: return L"Ready";
    case SENSOR_STATE_NOT_AVAILABLE: return L"Unavailable";
    case SENSOR_STATE_NO_DATA: return L"No data";
    case SENSOR_STATE_INITIALIZING: return L"Initializing";
    case SENSOR_STATE_ACCESS_DENIED: return L"Access denied";
    case SENSOR_STATE_ERROR: return L"Error";
    default: return L"Unknown";
    }
}

static BOOL
CopySensorStringProperty(ISensor* sensor, const PROPERTYKEY& key, WCHAR* value, int cch)
{
    PROPVARIANT property;
    HRESULT hr;

    value[0] = 0;
    PropVariantInit(&property);
    hr = sensor->GetProperty(key, &property);
    if (SUCCEEDED(hr))
    {
        if (property.vt == VT_LPWSTR && property.pwszVal)
            StringCchCopyW(value, cch, property.pwszVal);
        else if (property.vt == VT_BSTR && property.bstrVal)
            StringCchCopyW(value, cch, property.bstrVal);
    }
    PropVariantClear(&property);
    return value[0] != 0;
}

static BOOL
AppendTelemetryText(WCHAR* text, int cch, const WCHAR* value)
{
    int used;
    int needed;

    if (!value)
        return TRUE;
    used = lstrlenW(text);
    needed = lstrlenW(value) + (used ? 2 : 0);
    if (used + needed >= cch)
    {
        if (used + 4 < cch) StringCchCatW(text, cch, L"...");
        return FALSE;
    }
    if (used) StringCchCatW(text, cch, L", ");
    StringCchCatW(text, cch, value);
    return TRUE;
}

static void
FormatTelemetryFileTime(const FILETIME& fileTime, WCHAR* text, int cch)
{
    SYSTEMTIME time;

    if (FileTimeToSystemTime(&fileTime, &time)) StringCchPrintfW(text, cch, L"%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
}

static BOOL TelemetryVariantValue(const PROPVARIANT& property, double* number, WCHAR* text, int cch);

template <typename T>
static void
FormatTelemetryList(const T* values, ULONG count, WCHAR* text, int cch, void (*format)(const T& value, WCHAR* output, int outputCount))
{
    WCHAR value[128];

    if (!values || !count)
    {
        StringCchCopyW(text, cch, L"Empty");
        return;
    }
    for (ULONG index = 0; index < count; index++)
    {
        value[0] = 0;
        format(values[index], value, _countof(value));
        if (!AppendTelemetryText(text, cch, value)) break;
    }
}

template <typename T>
static void
FormatTelemetryNumberElement(const T& value, WCHAR* output, int outputCount)
{
    StringCchPrintfW(output, outputCount, L"%.6g", (double)value);
}

static void
FormatTelemetrySigned64Element(const LARGE_INTEGER& value, WCHAR* output, int outputCount)
{
    StringCchPrintfW(output, outputCount, L"%I64d", value.QuadPart);
}

static void
FormatTelemetryUnsigned64Element(const ULARGE_INTEGER& value, WCHAR* output, int outputCount)
{
    StringCchPrintfW(output, outputCount, L"%I64u", value.QuadPart);
}

static void
FormatTelemetryBooleanElement(const VARIANT_BOOL& value, WCHAR* output, int outputCount)
{
    StringCchCopyW(output, outputCount, value != VARIANT_FALSE ? L"On" : L"Off");
}

static void
FormatTelemetryStringElement(LPWSTR const& value, WCHAR* output, int outputCount)
{
    if (value) StringCchCopyW(output, outputCount, value);
}

static void
FormatTelemetryAnsiStringElement(LPSTR const& value, WCHAR* output, int outputCount)
{
    if (value) MultiByteToWideChar(CP_ACP, 0, value, -1, output, outputCount);
}

static void
FormatTelemetryCurrencyElement(const CY& value, WCHAR* output, int outputCount)
{
    StringCchPrintfW(output, outputCount, L"%.6g", value.int64 / 10000.0);
}

static void
FormatTelemetryScodeElement(const SCODE& value, WCHAR* output, int outputCount)
{
    StringCchPrintfW(output, outputCount, L"0x%08lx", value);
}

static void
FormatTelemetryFileTimeElement(const FILETIME& value, WCHAR* output, int outputCount)
{
    FormatTelemetryFileTime(value, output, outputCount);
}

static void
FormatTelemetryClsidElement(const CLSID& value, WCHAR* output, int outputCount)
{
    StringFromGUID2(value, output, outputCount);
}

static void
FormatTelemetryVariantElement(const PROPVARIANT& value, WCHAR* output, int outputCount)
{
    double ignored = 0;

    TelemetryVariantValue(value, &ignored, output, outputCount);
}

static void
FormatTelemetryBytes(const BYTE* bytes, ULONG count, WCHAR* text, int cch)
{
    WCHAR value[8];

    StringCchCopyW(text, cch, L"0x");
    for (ULONG index = 0; bytes && index < count; index++)
    {
        StringCchPrintfW(value, _countof(value), L"%02X", bytes[index]);
        if (lstrlenW(text) + 2 >= cch)
        {
            if (lstrlenW(text) + 4 < cch) StringCchCatW(text, cch, L"...");
            break;
        }
        StringCchCatW(text, cch, value);
    }
}

static BOOL
TelemetryVariantValue(const PROPVARIANT& property, double* number, WCHAR* text, int cch)
{
    switch (property.vt)
    {
    case VT_I1: *number = property.cVal; break;
    case VT_UI1: *number = property.bVal; break;
    case VT_I2: *number = property.iVal; break;
    case VT_UI2: *number = property.uiVal; break;
    case VT_I4: *number = property.lVal; break;
    case VT_UI4: *number = property.ulVal; break;
    case VT_I8: *number = (double)property.hVal.QuadPart; StringCchPrintfW(text, cch, L"%I64d", property.hVal.QuadPart); break;
    case VT_UI8: *number = (double)property.uhVal.QuadPart; StringCchPrintfW(text, cch, L"%I64u", property.uhVal.QuadPart); break;
    case VT_INT: *number = property.lVal; break;
    case VT_UINT: *number = property.ulVal; break;
    case VT_R4: *number = property.fltVal; break;
    case VT_R8: *number = property.dblVal; break;
    case VT_CY: *number = property.cyVal.int64 / 10000.0; break;
    case VT_DATE: *number = property.date; break;
    case VT_BOOL: *number = property.boolVal != VARIANT_FALSE; StringCchCopyW(text, cch, property.boolVal != VARIANT_FALSE ? L"On" : L"Off"); return TRUE;
    case VT_LPSTR:
        if (property.pszVal) MultiByteToWideChar(CP_ACP, 0, property.pszVal, -1, text, cch);
        return FALSE;
    case VT_LPWSTR:
        if (property.pwszVal) StringCchCopyW(text, cch, property.pwszVal);
        return FALSE;
    case VT_BSTR:
        if (property.bstrVal) StringCchCopyW(text, cch, property.bstrVal);
        return FALSE;
    case VT_ERROR:
        StringCchPrintfW(text, cch, L"Error 0x%08lx", property.scode);
        return FALSE;
    case VT_FILETIME:
        FormatTelemetryFileTime(property.filetime, text, cch);
        return FALSE;
    case VT_CLSID:
        if (property.puuid) StringFromGUID2(*property.puuid, text, cch);
        return FALSE;
    case VT_BLOB:
    case VT_BLOB_OBJECT:
        FormatTelemetryBytes(property.blob.pBlobData, property.blob.cbSize, text, cch);
        return FALSE;
    case VT_BSTR_BLOB:
        FormatTelemetryBytes((const BYTE*)property.bstrblobVal.pData, property.bstrblobVal.cbSize, text, cch);
        return FALSE;
    case VT_I1 | VT_VECTOR: FormatTelemetryList(property.cac.pElems, property.cac.cElems, text, cch, FormatTelemetryNumberElement<CHAR>); return FALSE;
    case VT_UI1 | VT_VECTOR: FormatTelemetryList(property.caub.pElems, property.caub.cElems, text, cch, FormatTelemetryNumberElement<UCHAR>); return FALSE;
    case VT_I2 | VT_VECTOR: FormatTelemetryList(property.cai.pElems, property.cai.cElems, text, cch, FormatTelemetryNumberElement<SHORT>); return FALSE;
    case VT_UI2 | VT_VECTOR: FormatTelemetryList(property.caui.pElems, property.caui.cElems, text, cch, FormatTelemetryNumberElement<USHORT>); return FALSE;
    case VT_I4 | VT_VECTOR: FormatTelemetryList(property.cal.pElems, property.cal.cElems, text, cch, FormatTelemetryNumberElement<LONG>); return FALSE;
    case VT_UI4 | VT_VECTOR: FormatTelemetryList(property.caul.pElems, property.caul.cElems, text, cch, FormatTelemetryNumberElement<ULONG>); return FALSE;
    case VT_R4 | VT_VECTOR: FormatTelemetryList(property.caflt.pElems, property.caflt.cElems, text, cch, FormatTelemetryNumberElement<FLOAT>); return FALSE;
    case VT_R8 | VT_VECTOR: FormatTelemetryList(property.cadbl.pElems, property.cadbl.cElems, text, cch, FormatTelemetryNumberElement<DOUBLE>); return FALSE;
    case VT_I8 | VT_VECTOR: FormatTelemetryList(property.cah.pElems, property.cah.cElems, text, cch, FormatTelemetrySigned64Element); return FALSE;
    case VT_UI8 | VT_VECTOR: FormatTelemetryList(property.cauh.pElems, property.cauh.cElems, text, cch, FormatTelemetryUnsigned64Element); return FALSE;
    case VT_BOOL | VT_VECTOR: FormatTelemetryList(property.cabool.pElems, property.cabool.cElems, text, cch, FormatTelemetryBooleanElement); return FALSE;
    case VT_CY | VT_VECTOR: FormatTelemetryList(property.cacy.pElems, property.cacy.cElems, text, cch, FormatTelemetryCurrencyElement); return FALSE;
    case VT_DATE | VT_VECTOR: FormatTelemetryList(property.cadate.pElems, property.cadate.cElems, text, cch, FormatTelemetryNumberElement<DATE>); return FALSE;
    case VT_ERROR | VT_VECTOR: FormatTelemetryList(property.cascode.pElems, property.cascode.cElems, text, cch, FormatTelemetryScodeElement); return FALSE;
    case VT_FILETIME | VT_VECTOR: FormatTelemetryList(property.cafiletime.pElems, property.cafiletime.cElems, text, cch, FormatTelemetryFileTimeElement); return FALSE;
    case VT_LPWSTR | VT_VECTOR: FormatTelemetryList(property.calpwstr.pElems, property.calpwstr.cElems, text, cch, FormatTelemetryStringElement); return FALSE;
    case VT_BSTR | VT_VECTOR: FormatTelemetryList((LPWSTR const*)property.cabstr.pElems, property.cabstr.cElems, text, cch, FormatTelemetryStringElement); return FALSE;
    case VT_LPSTR | VT_VECTOR: FormatTelemetryList(property.calpstr.pElems, property.calpstr.cElems, text, cch, FormatTelemetryAnsiStringElement); return FALSE;
    case VT_CLSID | VT_VECTOR: FormatTelemetryList(property.cauuid.pElems, property.cauuid.cElems, text, cch, FormatTelemetryClsidElement); return FALSE;
    case VT_VARIANT | VT_VECTOR: FormatTelemetryList(property.capropvar.pElems, property.capropvar.cElems, text, cch, FormatTelemetryVariantElement); return FALSE;
    default:
        if ((property.vt & VT_ARRAY) && property.parray) StringCchPrintfW(text, cch, L"Array (%u dimensions)", SafeArrayGetDim(property.parray));
        else StringCchPrintfW(text, cch, L"Unsupported value type 0x%04x", property.vt);
        return FALSE;
    }
    return TRUE;
}

static const SensorFieldInfo*
FindSensorFieldInfo(const PROPERTYKEY& key)
{
    for (ULONG setIndex = 0; setIndex < _countof(s_sensorFieldSets); setIndex++)
    {
        const SensorFieldSet* set = &s_sensorFieldSets[setIndex];

        if (!IsEqualGUID(key.fmtid, *set->format))
            continue;
        for (ULONG fieldIndex = 0; fieldIndex < set->count; fieldIndex++)
            if (set->fields[fieldIndex].pid == key.pid) return &set->fields[fieldIndex];
        break;
    }
    return NULL;
}

static const WCHAR*
SensorCategoryName(const GUID& category)
{
    if (IsEqualGUID(category, SENSOR_CATEGORY_BIOMETRIC)) return L"Biometric";
    if (IsEqualGUID(category, SENSOR_CATEGORY_ELECTRICAL)) return L"Electrical";
    if (IsEqualGUID(category, SENSOR_CATEGORY_ENVIRONMENTAL)) return L"Environmental";
    if (IsEqualGUID(category, SENSOR_CATEGORY_LIGHT)) return L"Light";
    if (IsEqualGUID(category, SENSOR_CATEGORY_LOCATION)) return L"Location";
    if (IsEqualGUID(category, SENSOR_CATEGORY_MECHANICAL)) return L"Mechanical";
    if (IsEqualGUID(category, SENSOR_CATEGORY_MOTION)) return L"Motion";
    if (IsEqualGUID(category, SENSOR_CATEGORY_ORIENTATION)) return L"Orientation";
    if (IsEqualGUID(category, SENSOR_CATEGORY_SCANNER)) return L"Scanner";
    return L"Sensor";
}

static void
DescribeSensorField(const PROPERTYKEY& key, const GUID& category, TelemetryRow* row)
{
    const SensorFieldInfo* info;

    row->kind = TEL_OTHER;
    row->unit[0] = 0;

    if (IsEqualGUID(key.fmtid, SENSOR_DATA_TYPE_CUSTOM_GUID))
    {
        if (IsEqualGUID(category, SENSOR_CATEGORY_ELECTRICAL) && key.pid >= 7) row->kind = TEL_ELECTRICAL_CUSTOM;
        if (key.pid == 5) StringCchCopyW(row->type, _countof(row->type), L"Custom usage");
        else if (key.pid == 6) StringCchCopyW(row->type, _countof(row->type), L"Custom boolean array");
        else if (row->kind == TEL_ELECTRICAL_CUSTOM) StringCchPrintfW(row->type, _countof(row->type), L"Custom electrical value %lu", key.pid >= 7 ? key.pid - 6 : key.pid);
        else StringCchPrintfW(row->type, _countof(row->type), L"Custom value %lu", key.pid >= 7 ? key.pid - 6 : key.pid);
        return;
    }

    info = FindSensorFieldInfo(key);
    if (info)
    {
        row->kind = info->kind;
        StringCchCopyW(row->type, _countof(row->type), info->name);
        StringCchCopyW(row->unit, _countof(row->unit), info->unit);
        return;
    }

    StringCchPrintfW(row->type, _countof(row->type), L"%s value %lu", SensorCategoryName(category), key.pid);
}

static void
FormatTelemetryNumber(TelemetryRow* row)
{
    switch (row->kind)
    {
    case TEL_TEMPERATURE:
    case TEL_HUMIDITY:
    case TEL_PRESSURE:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.1f", row->value);
        break;
    case TEL_LOCATION:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.6f", row->value);
        break;
    case TEL_VOLTAGE:
    case TEL_CURRENT:
    case TEL_POWER:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.3f", row->value);
        break;
    case TEL_PERCENTAGE:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.1f", row->value);
        break;
    case TEL_CAPACITY:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.2f", row->value);
        break;
    case TEL_CYCLE_COUNT:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.0f", row->value);
        break;
    case TEL_FAN:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.0f", row->value);
        break;
    default:
        StringCchPrintfW(row->valueText, _countof(row->valueText), L"%.4g", row->value);
        break;
    }
}

static void
SetTelemetryThresholdStatus(TelemetryRow* row)
{
    if (!row->available)
        return;
    if (row->hasCritical && row->value >= row->critical)
        StringCchCopyW(row->status, _countof(row->status), L"Critical");
    else if ((row->hasWarning && row->value >= row->warning) || (row->hasLower && row->value <= row->lower))
        StringCchCopyW(row->status, _countof(row->status), L"Warning");
    else
        StringCchCopyW(row->status, _countof(row->status), L"Normal");
}

static BOOL
SameTelemetryIdentity(const TelemetryRow& first, const TelemetryRow& second)
{
    return first.sourceKind == second.sourceKind && first.instance == second.instance && first.fieldId == second.fieldId && IsEqualGUID(first.sensorId, second.sensorId) && IsEqualGUID(first.fieldFormat, second.fieldFormat);
}

static void
CollectSensorTelemetry(Vec<TelemetryRow>& rows)
{
    for (int deviceIndex = 0; deviceIndex < s_sensorTelemetryDevices.n; deviceIndex++)
    {
        SensorTelemetryDevice* device = &s_sensorTelemetryDevices[deviceIndex];
        ISensorDataReport* report = NULL;
        SensorState state = SENSOR_STATE_ERROR;

        device->sensor->GetState(&state);
        device->sensor->GetData(&report);

        if (!device->fieldCount)
        {
            TelemetryRow* row = rows.Add();
            if (!row)
            {
                if (report) report->Release();
                return;
            }
            row->sensorId = device->id;
            row->sourceKind = TEL_SOURCE_SENSOR_API;
            StringCchCopyW(row->name, _countof(row->name), device->name);
            StringCchCopyW(row->type, _countof(row->type), L"Sensor");
            StringCchCopyW(row->source, _countof(row->source), device->source);
            StringCchCopyW(row->valueText, _countof(row->valueText), SensorStateName(state));
            StringCchCopyW(row->status, _countof(row->status), SensorStateName(state));
        }

        for (ULONG fieldIndex = 0; fieldIndex < device->fieldCount; fieldIndex++)
        {
            const PROPERTYKEY& key = device->fieldKeys[fieldIndex];
            PROPVARIANT property;
            TelemetryRow* row;
            BOOL converted;

            row = rows.Add();
            if (!row)
            {
                if (report) report->Release();
                return;
            }
            row->sensorId = device->id;
            row->fieldFormat = key.fmtid;
            row->fieldId = key.pid;
            row->sourceKind = TEL_SOURCE_SENSOR_API;
            StringCchCopyW(row->name, _countof(row->name), device->name);
            StringCchCopyW(row->source, _countof(row->source), device->source);
            StringCchCopyW(row->status, _countof(row->status), SensorStateName(state));
            DescribeSensorField(key, device->category, row);
            if (IsEqualGUID(device->category, SENSOR_CATEGORY_MECHANICAL) && IsEqualGUID(device->type, SENSOR_TYPE_CUSTOM) && IsEqualPropertyKey(key, SENSOR_DATA_TYPE_CUSTOM_VALUE1))
            {
                row->kind = TEL_FAN;
                StringCchCopyW(row->type, _countof(row->type), L"Fan speed");
                StringCchCopyW(row->unit, _countof(row->unit), L"RPM");
            }
            PropVariantInit(&property);
            if (report && SUCCEEDED(report->GetSensorValue(key, &property)))
            {
                converted = TelemetryVariantValue(property, &row->value, row->valueText, _countof(row->valueText));
                row->numeric = converted && property.vt != VT_BOOL;
                row->available = property.vt != VT_EMPTY && property.vt != VT_NULL && (converted || row->valueText[0] != 0);
                if (row->numeric && !row->valueText[0]) FormatTelemetryNumber(row);
                if (property.vt == VT_ERROR)
                {
                    row->available = FALSE;
                    StringCchCopyW(row->status, _countof(row->status), L"Error");
                }
                else if (row->available) StringCchCopyW(row->status, _countof(row->status), L"Normal");
            }
            if (!row->available && !row->valueText[0]) StringCchCopyW(row->valueText, _countof(row->valueText), report ? SensorStateName(state) : L"No data");
            PropVariantClear(&property);
        }

        if (report) report->Release();
    }
}

static void
FillStorageTelemetryDeviceIdentity(StorageTelemetryDevice* device)
{
    BYTE buffer[1024];
    STORAGE_PROPERTY_QUERY query;
    DWORD returned = 0;

    for (int index = 0; index < g.diskCount; index++)
    {
        if (g.disks[index].number != device->number || !g.disks[index].model[0])
            continue;
        StringCchCopyW(device->model, _countof(device->model), g.disks[index].model);
        StringCchCopyW(device->interfaceName, _countof(device->interfaceName), g.disks[index].interfaceName);
        return;
    }

    ZeroMemory(&query, sizeof(query));
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;
    if (DeviceIoControl(device->handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer, sizeof(buffer), &returned, NULL) && returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
    {
        PSTORAGE_DEVICE_DESCRIPTOR descriptor = (PSTORAGE_DEVICE_DESCRIPTOR)buffer;
        DWORD validSize = descriptor->Size && descriptor->Size < returned ? descriptor->Size : returned;
        WCHAR vendor[64];
        WCHAR product[96];

        CopyDescriptorText(buffer, validSize, descriptor->VendorIdOffset, vendor, _countof(vendor));
        CopyDescriptorText(buffer, validSize, descriptor->ProductIdOffset, product, _countof(product));
        if (vendor[0]) StringCchCopyW(device->model, _countof(device->model), vendor);
        if (product[0])
        {
            if (device->model[0]) StringCchCatW(device->model, _countof(device->model), L" ");
            StringCchCatW(device->model, _countof(device->model), product);
        }
        StringCchCopyW(device->interfaceName, _countof(device->interfaceName), StorageBusName(descriptor->BusType));
    }
    if (!device->model[0]) StringCchPrintfW(device->model, _countof(device->model), L"Disk %lu", device->number);
    if (!device->interfaceName[0]) StringCchCopyW(device->interfaceName, _countof(device->interfaceName), L"Unknown");
}

static void
ProbeStorageTelemetryTemperature(StorageTelemetryDevice* device)
{
    STORAGE_DESCRIPTOR_HEADER header;
    STORAGE_PROPERTY_QUERY query;
    DWORD returned = 0;

    ZeroMemory(&query, sizeof(query));
    ZeroMemory(&header, sizeof(header));
    query.PropertyId = StorageDeviceTemperatureProperty;
    query.QueryType = PropertyStandardQuery;
    if (!DeviceIoControl(device->handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &header, sizeof(header), &returned, NULL) || returned < sizeof(header))
        return;
    if (header.Version < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR) || header.Size < FIELD_OFFSET(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo) || header.Size > 1024 * 1024)
        return;
    device->temperatureBuffer = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, header.Size);
    if (device->temperatureBuffer) device->temperatureBufferSize = header.Size;
}

static BOOL
AddStorageTelemetryDevice(Vec<StorageTelemetryDevice>& devices, HANDLE handle, DWORD number)
{
    StorageTelemetryDevice* device;

    for (int index = 0; index < devices.n; index++)
    {
        if (devices[index].number != number)
            continue;
        CloseHandle(handle);
        return TRUE;
    }
    device = devices.Add();
    if (!device)
    {
        CloseHandle(handle);
        return FALSE;
    }
    device->handle = handle;
    device->number = number;
    FillStorageTelemetryDeviceIdentity(device);
    ProbeStorageTelemetryTemperature(device);
    return TRUE;
}

static void
FreeTelemetryDevice(StorageTelemetryDevice& device)
{
    if (device.handle != INVALID_HANDLE_VALUE) CloseHandle(device.handle);
    if (device.temperatureBuffer) HeapFree(GetProcessHeap(), 0, device.temperatureBuffer);
}

static void
FreeTelemetryDevice(AcpiTelemetryDevice& device)
{
    if (device.handle != INVALID_HANDLE_VALUE) CloseHandle(device.handle);
}

static void
FreeTelemetryDevice(BatteryTelemetryDevice& device)
{
    if (device.handle != INVALID_HANDLE_VALUE) CloseHandle(device.handle);
}

static void
FreeTelemetryDevice(SensorTelemetryDevice& device)
{
    if (device.sensor) device.sensor->Release();
    if (device.fieldKeys) HeapFree(GetProcessHeap(), 0, device.fieldKeys);
}

template <typename T>
static void
DiscardTelemetryDeviceList(Vec<T>& devices)
{
    for (int index = 0; index < devices.n; index++)
        FreeTelemetryDevice(devices[index]);
    devices.Free();
}

template <typename T>
static void
ReplaceTelemetryDeviceList(Vec<T>& current, Vec<T>& replacement)
{
    DiscardTelemetryDeviceList(current);
    current.Swap(replacement);
}

static BOOL
DetectSensorTelemetryDevices(void)
{
    Vec<SensorTelemetryDevice> found;
    ISensorCollection* collection = NULL;
    ULONG count = 0;

    if (!s_sensorManager)
        CoCreateInstance(CLSID_SensorManager, NULL, CLSCTX_INPROC_SERVER, IID_ISensorManager, (void**)&s_sensorManager);
    if (!s_sensorManager)
        return FALSE;
    if (FAILED(s_sensorManager->GetSensorsByCategory(SENSOR_CATEGORY_ALL, &collection)) || !collection)
        return FALSE;
    if (FAILED(collection->GetCount(&count)))
    {
        collection->Release();
        return FALSE;
    }

    for (ULONG sensorIndex = 0; sensorIndex < count; sensorIndex++)
    {
        IPortableDeviceKeyCollection* fields = NULL;
        ISensor* sensor = NULL;
        SensorTelemetryDevice* device;
        BSTR friendlyName = NULL;
        WCHAR manufacturer[96] = L"";
        WCHAR model[96] = L"";
        DWORD fieldCount = 0;

        if (FAILED(collection->GetAt(sensorIndex, &sensor)) || !sensor)
            continue;
        device = found.Add();
        if (!device)
        {
            sensor->Release();
            break;
        }
        device->sensor = sensor;
        sensor->GetID(&device->id);
        sensor->GetCategory(&device->category);
        sensor->GetType(&device->type);
        sensor->GetFriendlyName(&friendlyName);
        StringCchCopyW(device->name, _countof(device->name), friendlyName && friendlyName[0] ? friendlyName : L"Sensor");
        if (friendlyName) SysFreeString(friendlyName);
        CopySensorStringProperty(sensor, SENSOR_PROPERTY_MANUFACTURER, manufacturer, _countof(manufacturer));
        CopySensorStringProperty(sensor, SENSOR_PROPERTY_MODEL, model, _countof(model));
        if (manufacturer[0] && model[0]) StringCchPrintfW(device->source, _countof(device->source), L"%s %s", manufacturer, model);
        else if (manufacturer[0]) StringCchCopyW(device->source, _countof(device->source), manufacturer);
        else if (model[0]) StringCchCopyW(device->source, _countof(device->source), model);
        else StringCchCopyW(device->source, _countof(device->source), L"Sensor API");

        sensor->GetSupportedDataFields(&fields);
        if (fields)
        {
            if (SUCCEEDED(fields->GetCount(&fieldCount)) && fieldCount)
            {
                device->fieldKeys = (PROPERTYKEY*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fieldCount * sizeof(PROPERTYKEY));
                if (device->fieldKeys)
                    for (DWORD fieldIndex = 0; fieldIndex < fieldCount; fieldIndex++)
                        if (SUCCEEDED(fields->GetAt(fieldIndex, &device->fieldKeys[device->fieldCount])))
                            device->fieldCount++;
            }
            fields->Release();
        }
    }
    collection->Release();
    ReplaceTelemetryDeviceList(s_sensorTelemetryDevices, found);
    return TRUE;
}

static BOOL
DetectStorageTelemetryDevices(void)
{
    Vec<StorageTelemetryDevice> found;
    HDEVINFO devices;
    BOOL enumerated;
    BOOL scanSucceeded = TRUE;

    devices = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    enumerated = devices != INVALID_HANDLE_VALUE;
    if (enumerated)
    {
        for (DWORD index = 0; ; index++)
        {
            PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = NULL;
            SP_DEVICE_INTERFACE_DATA interfaceData;
            STORAGE_DEVICE_NUMBER deviceNumber;
            DWORD required = 0;
            DWORD returned = 0;
            HANDLE handle;

            ZeroMemory(&interfaceData, sizeof(interfaceData));
            interfaceData.cbSize = sizeof(interfaceData);
            if (!SetupDiEnumDeviceInterfaces(devices, NULL, &GUID_DEVINTERFACE_DISK, index, &interfaceData))
            {
                if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
                scanSucceeded = FALSE;
                break;
            }
            SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, NULL, 0, &required, NULL);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
                continue;
            detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required);
            if (!detail)
            {
                scanSucceeded = FALSE;
                break;
            }
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, required, NULL, NULL))
            {
                HeapFree(GetProcessHeap(), 0, detail);
                continue;
            }
            handle = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            HeapFree(GetProcessHeap(), 0, detail);
            if (handle == INVALID_HANDLE_VALUE)
                continue;
            ZeroMemory(&deviceNumber, sizeof(deviceNumber));
            if (!DeviceIoControl(handle, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &deviceNumber, sizeof(deviceNumber), &returned, NULL) || deviceNumber.DeviceType != FILE_DEVICE_DISK)
            {
                CloseHandle(handle);
                continue;
            }
            if (!AddStorageTelemetryDevice(found, handle, deviceNumber.DeviceNumber))
            {
                scanSucceeded = FALSE;
                break;
            }
        }
        SetupDiDestroyDeviceInfoList(devices);
    }

    if (!scanSucceeded)
    {
        DiscardTelemetryDeviceList(found);
        return FALSE;
    }

    if (!found.n)
    {
        for (int index = 0; index < g.diskCount; index++)
        {
            WCHAR path[64];
            HANDLE handle;

            StringCchPrintfW(path, _countof(path), L"\\\\.\\PhysicalDrive%lu", g.disks[index].number);
            handle = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            if (handle != INVALID_HANDLE_VALUE && !AddStorageTelemetryDevice(found, handle, g.disks[index].number))
            {
                scanSucceeded = FALSE;
                break;
            }
        }
    }
    if (!scanSucceeded)
    {
        DiscardTelemetryDeviceList(found);
        return FALSE;
    }
    if (!enumerated && !found.n)
        return FALSE;
    ReplaceTelemetryDeviceList(s_storageTelemetryDevices, found);
    return TRUE;
}

static ULONG
HashTelemetryDevicePath(PCWSTR path)
{
    ULONG hash = 2166136261u;

    while (*path)
    {
        WCHAR character = *path++;
        if (character >= L'A' && character <= L'Z') character += L'a' - L'A';
        hash = (hash ^ character) * 16777619u;
    }
    return hash ? hash : 1;
}

static void
CopyTelemetryDeviceName(HDEVINFO devices, PSP_DEVINFO_DATA deviceData, WCHAR* name, int nameCount, PCWSTR fallback, DWORD index)
{
    DWORD propertyType = 0;

    if (!SetupDiGetDeviceRegistryPropertyW(devices, deviceData, SPDRP_FRIENDLYNAME, &propertyType, (PBYTE)name, nameCount * sizeof(WCHAR), NULL)) SetupDiGetDeviceRegistryPropertyW(devices, deviceData, SPDRP_DEVICEDESC, &propertyType, (PBYTE)name, nameCount * sizeof(WCHAR), NULL);
    if (!name[0]) StringCchPrintfW(name, nameCount, L"%s %lu", fallback, index);
}

template <typename T>
static BOOL
DetectInterfaceTelemetryDevices(const GUID* interfaceGuid, DWORD access, PCWSTR fallbackName, Vec<T>& current)
{
    Vec<T> found;
    HDEVINFO devices;
    BOOL scanSucceeded = TRUE;

    devices = SetupDiGetClassDevsW(interfaceGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devices == INVALID_HANDLE_VALUE)
        return FALSE;

    for (DWORD index = 0; ; index++)
    {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        SP_DEVICE_INTERFACE_DATA interfaceData;
        SP_DEVINFO_DATA deviceData;
        T* device;
        DWORD required = 0;
        DWORD openError;
        HANDLE handle;

        ZeroMemory(&interfaceData, sizeof(interfaceData));
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices, NULL, interfaceGuid, index, &interfaceData))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            scanSucceeded = FALSE;
            break;
        }
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, NULL, 0, &required, NULL);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
            continue;
        detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_W)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required);
        if (!detail)
        {
            scanSucceeded = FALSE;
            break;
        }
        detail->cbSize = sizeof(*detail);
        ZeroMemory(&deviceData, sizeof(deviceData));
        deviceData.cbSize = sizeof(deviceData);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, required, NULL, &deviceData))
        {
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }
        handle = CreateFileW(detail->DevicePath, access, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        openError = handle == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        device = found.Add();
        if (!device)
        {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            HeapFree(GetProcessHeap(), 0, detail);
            scanSucceeded = FALSE;
            break;
        }
        device->handle = handle;
        device->openError = openError;
        device->instance = HashTelemetryDevicePath(detail->DevicePath);
        CopyTelemetryDeviceName(devices, &deviceData, device->name, _countof(device->name), fallbackName, index);
        HeapFree(GetProcessHeap(), 0, detail);
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (!scanSucceeded)
    {
        DiscardTelemetryDeviceList(found);
        return FALSE;
    }
    ReplaceTelemetryDeviceList(current, found);
    return TRUE;
}

static void
DetectTelemetryDevices(void)
{
    DetectSensorTelemetryDevices();
    DetectStorageTelemetryDevices();
    DetectInterfaceTelemetryDevices(&GUID_DEVICE_THERMAL_ZONE, GENERIC_READ, L"ACPI thermal zone", s_thermalTelemetryDevices);
    DetectInterfaceTelemetryDevices(&GUID_DEVICE_FAN, GENERIC_READ | GENERIC_WRITE, L"ACPI fan", s_fanTelemetryDevices);
    DetectInterfaceTelemetryDevices(&GUID_DEVICE_BATTERY, GENERIC_READ, L"Battery", s_batteryTelemetryDevices);
    s_lastTelemetryDeviceScan = GetTickCount();
}

static double
ThermalTemperatureToCelsius(ULONG temperature)
{
    return temperature / 10.0 - 273.15;
}

static void
SetTelemetryDeviceError(TelemetryRow* row, DWORD error, BOOL openFailure)
{
    if (error == ERROR_ACCESS_DENIED)
    {
        StringCchCopyW(row->valueText, _countof(row->valueText), L"Access denied");
        StringCchCopyW(row->status, _countof(row->status), L"Access denied");
    }
    else if (error == ERROR_NOT_SUPPORTED || error == ERROR_INVALID_FUNCTION)
    {
        StringCchCopyW(row->valueText, _countof(row->valueText), L"Not supported");
        StringCchCopyW(row->status, _countof(row->status), L"Unavailable");
    }
    else
    {
        StringCchCopyW(row->valueText, _countof(row->valueText), openFailure ? L"Open failed" : L"Query failed");
        StringCchCopyW(row->status, _countof(row->status), L"Unavailable");
    }
    StringCchPrintfW(row->limitsText, _countof(row->limitsText), L"Win32 error %lu", error);
}

static BOOL
QueryBatteryInformation(HANDLE handle, ULONG tag, BATTERY_QUERY_INFORMATION_LEVEL level, LONG atRate, PVOID output, DWORD outputSize, PDWORD returned)
{
    BATTERY_QUERY_INFORMATION query;

    ZeroMemory(&query, sizeof(query));
    query.BatteryTag = tag;
    query.InformationLevel = level;
    query.AtRate = atRate;
    return DeviceIoControl(handle, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), output, outputSize, returned, NULL);
}

static BOOL
QueryBatteryString(HANDLE handle, ULONG tag, BATTERY_QUERY_INFORMATION_LEVEL level, WCHAR* output, DWORD outputCount)
{
    DWORD returned = 0;
    DWORD lastCharacter;

    output[0] = 0;
    if (!QueryBatteryInformation(handle, tag, level, 0, output, outputCount * sizeof(WCHAR), &returned)) return FALSE;
    lastCharacter = returned / sizeof(WCHAR);
    if (lastCharacter >= outputCount) lastCharacter = outputCount - 1;
    output[lastCharacter] = 0;
    return TRUE;
}

static TelemetryRow*
AddBatteryTelemetryRow(Vec<TelemetryRow>& rows, const BatteryTelemetryDevice* device, PCWSTR displayName, PCWSTR source, ULONG fieldId, PCWSTR suffix, PCWSTR type, ULONG kind, PCWSTR unit)
{
    TelemetryRow* row = rows.Add();

    if (!row) return NULL;
    row->sensorId = GUID_DEVICE_BATTERY;
    row->sensorId.Data1 ^= device->instance;
    row->fieldFormat = s_batteryTelemetryFormat;
    row->fieldId = fieldId;
    row->instance = device->instance;
    row->sourceKind = TEL_SOURCE_BATTERY;
    row->kind = kind;
    if (suffix && suffix[0]) StringCchPrintfW(row->name, _countof(row->name), L"%s %s", displayName, suffix);
    else StringCchCopyW(row->name, _countof(row->name), displayName);
    StringCchCopyW(row->type, _countof(row->type), type);
    StringCchCopyW(row->source, _countof(row->source), source);
    StringCchCopyW(row->unit, _countof(row->unit), unit);
    return row;
}

static void
SetTelemetryNumericValue(TelemetryRow* row, double value)
{
    row->value = value;
    row->numeric = TRUE;
    row->available = TRUE;
    FormatTelemetryNumber(row);
    StringCchCopyW(row->status, _countof(row->status), L"Normal");
}

static void
FormatTelemetryDuration(TelemetryRow* row, ULONG seconds)
{
    ULONG days = seconds / 86400;
    ULONG hours = (seconds / 3600) % 24;
    ULONG minutes = (seconds / 60) % 60;

    row->value = seconds;
    row->numeric = TRUE;
    row->available = TRUE;
    if (days) StringCchPrintfW(row->valueText, _countof(row->valueText), L"%lu d %lu h", days, hours);
    else if (hours) StringCchPrintfW(row->valueText, _countof(row->valueText), L"%lu h %lu min", hours, minutes);
    else if (minutes) StringCchPrintfW(row->valueText, _countof(row->valueText), L"%lu min", minutes);
    else StringCchPrintfW(row->valueText, _countof(row->valueText), L"%lu s", seconds);
    StringCchCopyW(row->status, _countof(row->status), L"Normal");
}

static BOOL
IsKnownBatteryCapacity(ULONG capacity)
{
    return capacity != BATTERY_UNKNOWN_CAPACITY;
}

static void
FormatBatteryState(ULONG powerState, WCHAR* text, int textCount)
{
    text[0] = 0;
    if (powerState & BATTERY_POWER_ON_LINE) StringCchCopyW(text, textCount, L"AC power");
    else StringCchCopyW(text, textCount, L"Battery power");
    if (powerState & BATTERY_CHARGING) StringCchCatW(text, textCount, L", charging");
    if (powerState & BATTERY_DISCHARGING) StringCchCatW(text, textCount, L", discharging");
    if (powerState & BATTERY_CRITICAL) StringCchCatW(text, textCount, L", critical");
}

static void
AppendBatteryCapacityLimit(TelemetryRow* row, PCWSTR label, ULONG capacity, BOOL relative)
{
    WCHAR text[80];

    if (!IsKnownBatteryCapacity(capacity)) return;
    if (row->limitsText[0]) StringCchCatW(row->limitsText, _countof(row->limitsText), L" / ");
    if (relative) StringCchPrintfW(text, _countof(text), L"%s %lu units", label, capacity);
    else StringCchPrintfW(text, _countof(text), L"%s %.2f Wh", label, capacity / 1000.0);
    StringCchCatW(row->limitsText, _countof(row->limitsText), text);
}

static void
CollectBatteryTelemetry(Vec<TelemetryRow>& rows)
{
    for (int deviceIndex = 0; deviceIndex < s_batteryTelemetryDevices.n; deviceIndex++)
    {
        BatteryTelemetryDevice* device = &s_batteryTelemetryDevices[deviceIndex];
        BATTERY_INFORMATION information;
        BATTERY_STATUS batteryStatus;
        BATTERY_WAIT_STATUS waitStatus;
        WCHAR displayName[160];
        WCHAR manufacturer[128];
        WCHAR source[192];
        ULONG tag = BATTERY_TAG_INVALID;
        ULONG wait = 0;
        DWORD returned = 0;
        DWORD statusError;
        BOOL haveInformation;
        BOOL haveStatus;
        BOOL statusResult;
        BOOL tagResult;
        BOOL relative;
        BOOL absoluteUnits;

        StringCchCopyW(displayName, _countof(displayName), device->name);
        StringCchCopyW(source, _countof(source), L"Battery class");
        if (device->handle == INVALID_HANDLE_VALUE)
        {
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_STATE, L"", L"Battery", TEL_OTHER, L"");
            if (!row) return;
            SetTelemetryDeviceError(row, device->openError, TRUE);
            continue;
        }
        tagResult = DeviceIoControl(device->handle, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), &returned, NULL);
        if (!tagResult || returned < sizeof(tag) || tag == BATTERY_TAG_INVALID)
        {
            DWORD error = tagResult ? ERROR_DEVICE_NOT_CONNECTED : GetLastError();
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_STATE, L"", L"Battery", TEL_OTHER, L"");
            if (!row) return;
            SetTelemetryDeviceError(row, error, FALSE);
            continue;
        }

        QueryBatteryString(device->handle, tag, BatteryDeviceName, displayName, _countof(displayName));
        if (!displayName[0]) StringCchCopyW(displayName, _countof(displayName), device->name);
        if (QueryBatteryString(device->handle, tag, BatteryManufactureName, manufacturer, _countof(manufacturer)) && manufacturer[0]) StringCchPrintfW(source, _countof(source), L"Battery class (%s)", manufacturer);

        ZeroMemory(&information, sizeof(information));
        ZeroMemory(&batteryStatus, sizeof(batteryStatus));
        ZeroMemory(&waitStatus, sizeof(waitStatus));
        haveInformation = QueryBatteryInformation(device->handle, tag, BatteryInformation, 0, &information, sizeof(information), &returned) && returned >= sizeof(information);
        waitStatus.BatteryTag = tag;
        statusResult = DeviceIoControl(device->handle, IOCTL_BATTERY_QUERY_STATUS, &waitStatus, sizeof(waitStatus), &batteryStatus, sizeof(batteryStatus), &returned, NULL);
        statusError = statusResult ? (returned >= sizeof(batteryStatus) ? ERROR_SUCCESS : ERROR_INVALID_DATA) : GetLastError();
        haveStatus = statusError == ERROR_SUCCESS;
        relative = haveInformation && !!(information.Capabilities & BATTERY_CAPACITY_RELATIVE);
        absoluteUnits = haveInformation && !relative;

        {
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_STATE, L"", L"Power state", TEL_OTHER, L"");
            if (!row) return;
            if (haveStatus)
            {
                FormatBatteryState(batteryStatus.PowerState, row->valueText, _countof(row->valueText));
                row->available = TRUE;
                StringCchCopyW(row->status, _countof(row->status), (batteryStatus.PowerState & BATTERY_CRITICAL) ? L"Critical" : L"Normal");
            }
            else SetTelemetryDeviceError(row, statusError, FALSE);
        }

        if (haveStatus && haveInformation)
        {
            double percent;
            if (TmBatteryChargePercent(batteryStatus.Capacity, information.FullChargedCapacity, &percent))
            {
                TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_CHARGE_PERCENT, L"charge", L"Charge level", TEL_PERCENTAGE, L"%");
                if (!row) return;
                SetTelemetryNumericValue(row, percent);
                if (batteryStatus.PowerState & BATTERY_CRITICAL) StringCchCopyW(row->status, _countof(row->status), L"Critical");
            }
        }

        if (haveStatus && IsKnownBatteryCapacity(batteryStatus.Capacity))
        {
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_REMAINING_CAPACITY, L"remaining capacity", L"Remaining capacity", TEL_CAPACITY, absoluteUnits ? L"Wh" : L"units");
            if (!row) return;
            SetTelemetryNumericValue(row, absoluteUnits ? batteryStatus.Capacity / 1000.0 : batteryStatus.Capacity);
            if (haveInformation)
            {
                AppendBatteryCapacityLimit(row, L"Full", information.FullChargedCapacity, relative);
                AppendBatteryCapacityLimit(row, L"Design", information.DesignedCapacity, relative);
                AppendBatteryCapacityLimit(row, L"Alert 1", information.DefaultAlert1, relative);
                AppendBatteryCapacityLimit(row, L"Alert 2", information.DefaultAlert2, relative);
            }
        }

        if (haveInformation && IsKnownBatteryCapacity(information.FullChargedCapacity))
        {
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_FULL_CAPACITY, L"full-charge capacity", L"Full-charge capacity", TEL_CAPACITY, relative ? L"units" : L"Wh");
            if (!row) return;
            SetTelemetryNumericValue(row, relative ? information.FullChargedCapacity : information.FullChargedCapacity / 1000.0);
        }
        if (haveInformation && IsKnownBatteryCapacity(information.DesignedCapacity))
        {
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_DESIGN_CAPACITY, L"design capacity", L"Design capacity", TEL_CAPACITY, relative ? L"units" : L"Wh");
            if (!row) return;
            SetTelemetryNumericValue(row, relative ? information.DesignedCapacity : information.DesignedCapacity / 1000.0);
        }
        if (haveInformation)
        {
            double percent;
            if (TmBatteryHealthPercent(&information, &percent))
            {
                TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_HEALTH, L"health", L"Full/design capacity", TEL_PERCENTAGE, L"%");
                if (!row) return;
                SetTelemetryNumericValue(row, percent);
            }
        }
        if (haveInformation)
        {
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_CYCLE_COUNT, L"cycle count", L"Cycle count", TEL_CYCLE_COUNT, L"");
            if (!row) return;
            SetTelemetryNumericValue(row, information.CycleCount);
        }
        if (haveStatus && batteryStatus.Voltage != BATTERY_UNKNOWN_VOLTAGE)
        {
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_VOLTAGE, L"voltage", L"Battery voltage", TEL_VOLTAGE, L"V");
            if (!row) return;
            SetTelemetryNumericValue(row, batteryStatus.Voltage / 1000.0);
        }
        if (haveStatus && batteryStatus.Rate != (LONG)BATTERY_UNKNOWN_RATE)
        {
            double watts;
            double amperes;
            TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_POWER, L"rate", absoluteUnits ? L"Charge/discharge power" : L"Relative charge rate", absoluteUnits ? TEL_POWER : TEL_ELECTRICAL_CUSTOM, absoluteUnits ? L"W" : L"units/h");
            if (!row) return;
            if (absoluteUnits && TmBatteryPowerWatts(&information, &batteryStatus, &watts)) SetTelemetryNumericValue(row, watts);
            else SetTelemetryNumericValue(row, batteryStatus.Rate);
            if (absoluteUnits && TmBatteryCurrentAmps(&information, &batteryStatus, &amperes))
            {
                row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_CURRENT, L"current", L"Derived battery current", TEL_CURRENT, L"A");
                if (!row) return;
                SetTelemetryNumericValue(row, amperes);
                StringCchCopyW(row->limitsText, _countof(row->limitsText), L"Derived from battery power / voltage");
            }
        }

        {
            ULONG estimatedTime = BATTERY_UNKNOWN_TIME;
            if (QueryBatteryInformation(device->handle, tag, BatteryEstimatedTime, 0, &estimatedTime, sizeof(estimatedTime), &returned) && returned >= sizeof(estimatedTime) && estimatedTime != BATTERY_UNKNOWN_TIME)
            {
                TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_ESTIMATED_TIME, L"time remaining", L"Estimated time", TEL_DURATION, L"");
                if (!row) return;
                FormatTelemetryDuration(row, estimatedTime);
            }
        }
        {
            ULONG temperature = 0;
            if (QueryBatteryInformation(device->handle, tag, BatteryTemperature, 0, &temperature, sizeof(temperature), &returned) && returned >= sizeof(temperature))
            {
                TelemetryRow* row = AddBatteryTelemetryRow(rows, device, displayName, source, BATTERY_FIELD_TEMPERATURE, L"temperature", L"Battery temperature", TEL_TEMPERATURE, L"°C");
                if (!row) return;
                SetTelemetryNumericValue(row, ThermalTemperatureToCelsius(temperature));
            }
        }
    }
}

static TelemetryRow*
AddSystemPowerTelemetryRow(Vec<TelemetryRow>& rows, ULONG fieldId, PCWSTR name, PCWSTR type, ULONG kind, PCWSTR unit)
{
    TelemetryRow* row = rows.Add();

    if (!row) return NULL;
    row->sensorId = s_systemPowerTelemetryFormat;
    row->fieldFormat = s_systemPowerTelemetryFormat;
    row->fieldId = fieldId;
    row->sourceKind = TEL_SOURCE_SYSTEM_POWER;
    row->kind = kind;
    StringCchCopyW(row->name, _countof(row->name), name);
    StringCchCopyW(row->type, _countof(row->type), type);
    StringCchCopyW(row->source, _countof(row->source), L"System power status");
    StringCchCopyW(row->unit, _countof(row->unit), unit);
    return row;
}

static void
CollectSystemPowerTelemetry(Vec<TelemetryRow>& rows)
{
    SYSTEM_POWER_STATUS status;
    TelemetryRow* row;

    if (!GetSystemPowerStatus(&status)) return;
    row = AddSystemPowerTelemetryRow(rows, SYSTEM_POWER_FIELD_SOURCE, L"System power source", L"Power source", TEL_OTHER, L"");
    if (!row) return;
    if (status.ACLineStatus == 1) StringCchCopyW(row->valueText, _countof(row->valueText), L"AC power");
    else if (status.ACLineStatus == 0) StringCchCopyW(row->valueText, _countof(row->valueText), L"Battery power");
    else StringCchCopyW(row->valueText, _countof(row->valueText), L"Unknown");
    row->available = status.ACLineStatus != 255;
    if (status.BatteryFlag & BATTERY_FLAG_CRITICAL) StringCchCopyW(row->status, _countof(row->status), L"Critical");
    else if (status.BatteryFlag & BATTERY_FLAG_CHARGING) StringCchCopyW(row->status, _countof(row->status), L"Charging");
    else StringCchCopyW(row->status, _countof(row->status), row->available ? L"Normal" : L"Unavailable");

    if (!(status.BatteryFlag & BATTERY_FLAG_NO_BATTERY) && status.BatteryLifePercent != 255)
    {
        row = AddSystemPowerTelemetryRow(rows, SYSTEM_POWER_FIELD_CHARGE_PERCENT, L"Aggregate battery charge", L"Charge level", TEL_PERCENTAGE, L"%");
        if (!row) return;
        SetTelemetryNumericValue(row, status.BatteryLifePercent);
        if (status.BatteryFlag & BATTERY_FLAG_CRITICAL) StringCchCopyW(row->status, _countof(row->status), L"Critical");
    }
    if (!(status.BatteryFlag & BATTERY_FLAG_NO_BATTERY) && status.BatteryLifeTime != BATTERY_LIFE_UNKNOWN)
    {
        row = AddSystemPowerTelemetryRow(rows, SYSTEM_POWER_FIELD_ESTIMATED_TIME, L"Aggregate time remaining", L"Estimated time", TEL_DURATION, L"");
        if (!row) return;
        FormatTelemetryDuration(row, status.BatteryLifeTime);
    }
}

static void
AppendThermalLimit(TelemetryRow* row, PCWSTR name, ULONG temperature)
{
    WCHAR text[64];

    if (temperature == MAXULONG)
        return;
    if (row->limitsText[0]) StringCchCatW(row->limitsText, _countof(row->limitsText), L" / ");
    StringCchPrintfW(text, _countof(text), L"%s %.1f °C", name, ThermalTemperatureToCelsius(temperature));
    StringCchCatW(row->limitsText, _countof(row->limitsText), text);
}

static void
AddThermalWarningTrip(TelemetryRow* row, ULONG temperature)
{
    double value;

    if (temperature == MAXULONG)
        return;
    value = ThermalTemperatureToCelsius(temperature);
    if (!row->hasWarning || value < row->warning)
    {
        row->warning = value;
        row->hasWarning = TRUE;
    }
}

static BOOL
ReadAcpiIntegerArgument(PACPI_METHOD_ARGUMENT argument, const BYTE* end, PULONG value, PACPI_METHOD_ARGUMENT* next)
{
    const BYTE* start = (const BYTE*)argument;
    ULONG length;

    if (start > end || (SIZE_T)(end - start) < FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data))
        return FALSE;
    length = ACPI_METHOD_ARGUMENT_LENGTH(argument->DataLength);
    if (length > (SIZE_T)(end - start) || argument->Type != ACPI_METHOD_ARGUMENT_INTEGER || argument->DataLength < sizeof(ULONG))
        return FALSE;
    *value = argument->Argument;
    *next = (PACPI_METHOD_ARGUMENT)(start + length);
    return TRUE;
}

static BOOL
QueryFanStatus(HANDLE handle, PULONG revision, PULONG control, PULONG speed)
{
    ACPI_EVAL_INPUT_BUFFER input;
    BYTE outputBytes[256];
    PACPI_EVAL_OUTPUT_BUFFER output = (PACPI_EVAL_OUTPUT_BUFFER)outputBytes;
    PACPI_METHOD_ARGUMENT argument;
    PACPI_METHOD_ARGUMENT next;
    const BYTE* end;
    DWORD returned = 0;

    ZeroMemory(&input, sizeof(input));
    ZeroMemory(outputBytes, sizeof(outputBytes));
    input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    CopyMemory(input.MethodName, "_FST", sizeof(input.MethodName));
    if (!DeviceIoControl(handle, IOCTL_ACPI_EVAL_METHOD, &input, sizeof(input), output, sizeof(outputBytes), &returned, NULL))
        return FALSE;
    if (returned < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || output->Count < 3 || output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || output->Length > returned)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    end = outputBytes + output->Length;
    argument = output->Argument;
    PULONG values[] = { revision, control, speed };
    for (ULONG index = 0; index < _countof(values); index++)
    {
        if (!ReadAcpiIntegerArgument(argument, end, values[index], &next))
        {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        argument = next;
    }
    return TRUE;
}

static void
CollectThermalTelemetry(Vec<TelemetryRow>& rows)
{
    for (int deviceIndex = 0; deviceIndex < s_thermalTelemetryDevices.n; deviceIndex++)
    {
        AcpiTelemetryDevice* device = &s_thermalTelemetryDevices[deviceIndex];
        THERMAL_INFORMATION information;
        TelemetryRow* row;
        DWORD returned = 0;
        ULONG stamp = MAXULONG;
        ULONG tripCount;

        row = rows.Add();
        if (!row)
            return;
        row->sensorId = GUID_DEVICE_THERMAL_ZONE;
        row->sensorId.Data1 ^= device->instance;
        row->fieldFormat = s_thermalTelemetryFormat;
        row->fieldId = 0;
        row->instance = device->instance;
        row->sourceKind = TEL_SOURCE_THERMAL_ZONE;
        row->kind = TEL_TEMPERATURE;
        StringCchCopyW(row->name, _countof(row->name), device->name);
        StringCchCopyW(row->type, _countof(row->type), L"Thermal zone temperature");
        StringCchCopyW(row->source, _countof(row->source), L"ACPI thermal zone");
        StringCchCopyW(row->unit, _countof(row->unit), L"°C");

        if (device->handle == INVALID_HANDLE_VALUE)
        {
            SetTelemetryDeviceError(row, device->openError, TRUE);
            continue;
        }
        ZeroMemory(&information, sizeof(information));
        if (!DeviceIoControl(device->handle, IOCTL_THERMAL_QUERY_INFORMATION, &stamp, sizeof(stamp), &information, sizeof(information), &returned, NULL))
        {
            SetTelemetryDeviceError(row, GetLastError(), FALSE);
            continue;
        }
        if (returned < FIELD_OFFSET(THERMAL_INFORMATION, ActiveTripPoint))
        {
            StringCchCopyW(row->valueText, _countof(row->valueText), L"Invalid response");
            StringCchCopyW(row->status, _countof(row->status), L"Error");
            continue;
        }
        tripCount = (returned - FIELD_OFFSET(THERMAL_INFORMATION, ActiveTripPoint)) / sizeof(information.ActiveTripPoint[0]);
        if (tripCount > information.ActiveTripPointCount) tripCount = information.ActiveTripPointCount;
        if (tripCount > MAX_ACTIVE_COOLING_LEVELS) tripCount = MAX_ACTIVE_COOLING_LEVELS;
        if (information.CurrentTemperature != MAXULONG)
        {
            row->value = ThermalTemperatureToCelsius(information.CurrentTemperature);
            row->numeric = TRUE;
            row->available = TRUE;
            FormatTelemetryNumber(row);
        }
        else
        {
            StringCchCopyW(row->valueText, _countof(row->valueText), L"Not reported");
            StringCchCopyW(row->status, _countof(row->status), L"Unavailable");
        }

        AppendThermalLimit(row, L"Passive", information.PassiveTripPoint);
        AddThermalWarningTrip(row, information.PassiveTripPoint);
        for (ULONG tripIndex = 0; tripIndex < tripCount; tripIndex++)
        {
            WCHAR name[24];
            StringCchPrintfW(name, _countof(name), L"Active %lu", tripIndex);
            AppendThermalLimit(row, name, information.ActiveTripPoint[tripIndex]);
            AddThermalWarningTrip(row, information.ActiveTripPoint[tripIndex]);
        }
        AppendThermalLimit(row, L"Critical", information.CriticalTripPoint);
        if (information.CriticalTripPoint != MAXULONG)
        {
            row->critical = ThermalTemperatureToCelsius(information.CriticalTripPoint);
            row->hasCritical = TRUE;
        }
        SetTelemetryThresholdStatus(row);
    }
}

static void
CollectFanTelemetry(Vec<TelemetryRow>& rows)
{
    for (int deviceIndex = 0; deviceIndex < s_fanTelemetryDevices.n; deviceIndex++)
    {
        AcpiTelemetryDevice* device = &s_fanTelemetryDevices[deviceIndex];
        TelemetryRow* row = rows.Add();
        ULONG revision = MAXULONG;
        ULONG control = MAXULONG;
        ULONG speed = MAXULONG;

        if (!row)
            return;
        row->sensorId = GUID_DEVICE_FAN;
        row->sensorId.Data1 ^= device->instance;
        row->fieldFormat = s_fanTelemetryFormat;
        row->fieldId = 0;
        row->instance = device->instance;
        row->sourceKind = TEL_SOURCE_ACPI_FAN;
        row->kind = TEL_FAN;
        StringCchPrintfW(row->name, _countof(row->name), L"%s speed", device->name);
        StringCchCopyW(row->type, _countof(row->type), L"Fan speed");
        StringCchCopyW(row->source, _countof(row->source), L"ACPI fan");
        StringCchCopyW(row->unit, _countof(row->unit), L"RPM");

        if (device->handle == INVALID_HANDLE_VALUE)
        {
            SetTelemetryDeviceError(row, device->openError, TRUE);
            continue;
        }
        if (!QueryFanStatus(device->handle, &revision, &control, &speed))
        {
            SetTelemetryDeviceError(row, GetLastError(), FALSE);
            continue;
        }
        if (control != MAXULONG) StringCchPrintfW(row->limitsText, _countof(row->limitsText), L"Control %lu; revision %lu", control, revision);
        else if (revision != MAXULONG) StringCchPrintfW(row->limitsText, _countof(row->limitsText), L"Revision %lu", revision);
        if (speed == MAXULONG)
        {
            StringCchCopyW(row->valueText, _countof(row->valueText), L"Not reported");
            StringCchCopyW(row->status, _countof(row->status), L"Unavailable");
            continue;
        }
        row->value = speed;
        row->numeric = TRUE;
        row->available = TRUE;
        FormatTelemetryNumber(row);
        StringCchCopyW(row->status, _countof(row->status), L"Normal");
    }
}

static void
AppendStorageTelemetryLimit(TelemetryRow* row, PCWSTR label, double value)
{
    WCHAR text[64];

    if (row->limitsText[0]) StringCchCatW(row->limitsText, _countof(row->limitsText), L" / ");
    StringCchPrintfW(text, _countof(text), L"%s %.0f%s%s", label, value, row->unit[0] ? L" " : L"", row->unit);
    StringCchCatW(row->limitsText, _countof(row->limitsText), text);
}

static void
CollectStorageTelemetry(Vec<TelemetryRow>& rows)
{
    for (int deviceIndex = 0; deviceIndex < s_storageTelemetryDevices.n; deviceIndex++)
    {
        StorageTelemetryDevice* device = &s_storageTelemetryDevices[deviceIndex];
        STORAGE_PROPERTY_QUERY query;
        PSTORAGE_TEMPERATURE_DATA_DESCRIPTOR descriptor;
        DWORD returned = 0;
        ULONG validSize;
        ULONG count;

        if (!device->temperatureBuffer)
            continue;
        ZeroMemory(&query, sizeof(query));
        query.PropertyId = StorageDeviceTemperatureProperty;
        query.QueryType = PropertyStandardQuery;
        if (!DeviceIoControl(device->handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), device->temperatureBuffer, device->temperatureBufferSize, &returned, NULL) || returned < FIELD_OFFSET(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo))
            continue;
        descriptor = (PSTORAGE_TEMPERATURE_DATA_DESCRIPTOR)device->temperatureBuffer;
        if (descriptor->Version < sizeof(STORAGE_TEMPERATURE_DATA_DESCRIPTOR) || descriptor->Size < FIELD_OFFSET(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo) || descriptor->Size > returned)
            continue;
        validSize = descriptor->Size;
        count = (validSize - FIELD_OFFSET(STORAGE_TEMPERATURE_DATA_DESCRIPTOR, TemperatureInfo)) / sizeof(STORAGE_TEMPERATURE_INFO);
        if (count > descriptor->InfoCount) count = descriptor->InfoCount;

        for (ULONG infoIndex = 0; infoIndex < count; infoIndex++)
        {
            PSTORAGE_TEMPERATURE_INFO info = &descriptor->TemperatureInfo[infoIndex];
            TelemetryRow* row;

            if (info->Temperature == (SHORT)STORAGE_TEMPERATURE_VALUE_NOT_REPORTED)
                continue;
            row = rows.Add();
            if (!row)
                return;
            row->sensorId.Data1 = 0x54534d54;
            row->sensorId.Data2 = (USHORT)device->number;
            row->sensorId.Data3 = info->Index;
            row->fieldFormat = s_storageTelemetryFormat;
            row->fieldId = info->Index;
            row->instance = device->number;
            row->sourceKind = TEL_SOURCE_STORAGE;
            row->kind = TEL_TEMPERATURE;
            if (count > 1) StringCchPrintfW(row->name, _countof(row->name), L"%s sensor %u", device->model, info->Index);
            else StringCchPrintfW(row->name, _countof(row->name), L"%s temperature", device->model);
            StringCchCopyW(row->type, _countof(row->type), L"Storage temperature");
            StringCchPrintfW(row->source, _countof(row->source), L"Disk %lu (%s)", device->number, device->interfaceName);
            StringCchCopyW(row->unit, _countof(row->unit), L"°C");
            row->value = info->Temperature;
            row->numeric = TRUE;
            row->available = TRUE;
            if (info->UnderThreshold != (SHORT)STORAGE_TEMPERATURE_VALUE_NOT_REPORTED)
            {
                row->lower = info->UnderThreshold;
                row->hasLower = TRUE;
            }
            if (info->OverThreshold != (SHORT)STORAGE_TEMPERATURE_VALUE_NOT_REPORTED)
            {
                row->warning = info->OverThreshold;
                row->hasWarning = TRUE;
            }
            else if (descriptor->WarningTemperature != (SHORT)STORAGE_TEMPERATURE_VALUE_NOT_REPORTED)
            {
                row->warning = descriptor->WarningTemperature;
                row->hasWarning = TRUE;
            }
            if (descriptor->CriticalTemperature != (SHORT)STORAGE_TEMPERATURE_VALUE_NOT_REPORTED)
            {
                row->critical = descriptor->CriticalTemperature;
                row->hasCritical = TRUE;
            }
            if (row->hasLower) AppendStorageTelemetryLimit(row, L"Low", row->lower);
            if (row->hasWarning) AppendStorageTelemetryLimit(row, L"High", row->warning);
            if (row->hasCritical) AppendStorageTelemetryLimit(row, L"Critical", row->critical);
            if (info->UnderThresholdChangable || info->OverThresholdChangable || info->EventGenerated)
            {
                if (row->limitsText[0]) StringCchCatW(row->limitsText, _countof(row->limitsText), L"; ");
                if (info->UnderThresholdChangable || info->OverThresholdChangable) StringCchCatW(row->limitsText, _countof(row->limitsText), L"adjustable");
                if (info->EventGenerated)
                {
                    if (info->UnderThresholdChangable || info->OverThresholdChangable) StringCchCatW(row->limitsText, _countof(row->limitsText), L", ");
                    StringCchCatW(row->limitsText, _countof(row->limitsText), L"notifications");
                }
            }
            FormatTelemetryNumber(row);
            SetTelemetryThresholdStatus(row);
        }
    }
}

Vec<TelemetryRow>& Telemetry(void)
{
    return s_telemetry;
}

void RefreshTelemetry(void)
{
    DWORD now = GetTickCount();
    Vec<TelemetryRow> next;

    if (s_lastTelemetryRefresh && now - s_lastTelemetryRefresh < 900)
        return;
    s_lastTelemetryRefresh = now;
    if (!s_lastTelemetryDeviceScan || now - s_lastTelemetryDeviceScan >= 30000) DetectTelemetryDevices();
    CollectSystemPowerTelemetry(next);
    CollectBatteryTelemetry(next);
    CollectSensorTelemetry(next);
    CollectThermalTelemetry(next);
    CollectFanTelemetry(next);
    CollectStorageTelemetry(next);

    for (int i = 0; i < next.n; i++)
    {
        for (int j = 0; j < s_telemetry.n; j++)
        {
            if (SameTelemetryIdentity(next[i], s_telemetry[j]))
            {
                next[i].history = s_telemetry[j].history;
                break;
            }
        }
        if (next[i].numeric && next[i].available)
            next[i].history.Push((float)next[i].value);
    }

    s_telemetry.Swap(next);
}

static void ShutdownTelemetry(void)
{
    s_telemetry.Free();
    DiscardTelemetryDeviceList(s_storageTelemetryDevices);
    DiscardTelemetryDeviceList(s_thermalTelemetryDevices);
    DiscardTelemetryDeviceList(s_fanTelemetryDevices);
    DiscardTelemetryDeviceList(s_batteryTelemetryDevices);
    DiscardTelemetryDeviceList(s_sensorTelemetryDevices);
    if (s_sensorManager)
    {
        s_sensorManager->Release();
        s_sensorManager = NULL;
    }
}

/* names that belong to the "Windows processes" section */
static const WCHAR* s_windowsProcs[] =
{
    L"System", L"System Idle Process", L"Registry", L"Memory Compression",
    L"smss.exe", L"csrss.exe", L"wininit.exe", L"winlogon.exe",
    L"services.exe", L"lsass.exe", L"lsm.exe", L"svchost.exe",
    L"spoolsv.exe", L"dwm.exe", L"conhost.exe", L"fontdrvhost.exe",
    L"userinit.exe", L"logonui.exe", L"dllhost.exe", L"wudfhost.exe",
    L"audiodg.exe", L"ctfmon.exe", L"sihost.exe", L"taskhostw.exe",
    L"mutant.exe"
};

static const WCHAR* s_criticalProcs[] =
{
    L"System", L"smss.exe", L"csrss.exe", L"wininit.exe", L"winlogon.exe",
    L"services.exe", L"lsass.exe", L"lsm.exe"
};

static BOOL NameInList(const WCHAR* name, const WCHAR** list, int n)
{
    for (int i = 0; i < n; i++)
        if (lstrcmpiW(name, list[i]) == 0) return TRUE;
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Hardware detail: logical processor info + SMBIOS (arch-neutral)    */
/* ------------------------------------------------------------------ */

#ifndef PF_VIRT_FIRMWARE_ENABLED
#define PF_VIRT_FIRMWARE_ENABLED 21
#endif

static void DetectCpuTopology(void)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    BOOL haveTopology = FALSE;

    /* Prefer the variable-size API so processor groups and all cache records
       are represented. Fall back for older ReactOS installations. */
    typedef BOOL (WINAPI *PFN_GLPIEX)(
        LOGICAL_PROCESSOR_RELATIONSHIP,
        PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,
        PDWORD);
    PFN_GLPIEX pGlpiEx = (PFN_GLPIEX)GetProcAddress(
        kernel32, "GetLogicalProcessorInformationEx");
    if (pGlpiEx)
    {
        DWORD cb = 0;
        pGlpiEx(RelationAll, NULL, &cb);
        if (cb && cb < 1024 * 1024)
        {
            BYTE* buffer = (BYTE*)HeapAlloc(GetProcessHeap(), 0, cb);
            if (buffer && pGlpiEx(RelationAll,
                                  (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer,
                                  &cb))
            {
                int cores = 0, packages = 0;
                ULONG l1 = 0, l2 = 0, l3 = 0;
                DWORD offset = 0;
                while (offset + sizeof(ULONG) * 2 <= cb)
                {
                    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX info =
                        (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer + offset);
                    if (info->Size < sizeof(ULONG) * 2 || info->Size > cb - offset)
                        break;

                    switch (info->Relationship)
                    {
                    case RelationProcessorCore: cores++; break;
                    case RelationProcessorPackage: packages++; break;
                    case RelationCache:
                        switch (info->Cache.Level)
                        {
                        case 1: l1 += info->Cache.CacheSize / 1024; break;
                        case 2: l2 += info->Cache.CacheSize / 1024; break;
                        case 3: l3 += info->Cache.CacheSize / 1024; break;
                        }
                        break;
                    default: break;
                    }
                    offset += info->Size;
                }
                if (cores) g.cores = cores;
                if (packages) g.sockets = packages;
                if (l1) g.l1KB = l1;
                if (l2) g.l2KB = l2;
                if (l3) g.l3KB = l3;
                haveTopology = cores != 0;
            }
            if (buffer) HeapFree(GetProcessHeap(), 0, buffer);
        }
    }

    if (!haveTopology)
    {
        typedef BOOL (WINAPI *PFN_GLPI)(
            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION,
            PDWORD);
        PFN_GLPI pGlpi = (PFN_GLPI)GetProcAddress(
            kernel32, "GetLogicalProcessorInformation");
        if (pGlpi)
        {
            DWORD cb = 0;
            pGlpi(NULL, &cb);
            if (cb && cb < 1024 * 1024)
            {
                SYSTEM_LOGICAL_PROCESSOR_INFORMATION* info =
                    (SYSTEM_LOGICAL_PROCESSOR_INFORMATION*)HeapAlloc(GetProcessHeap(), 0, cb);
                if (info && pGlpi(info, &cb))
                {
                    int cores = 0, packages = 0;
                    ULONG l1 = 0, l2 = 0, l3 = 0;
                    int count = cb / sizeof(*info);
                    for (int i = 0; i < count; i++)
                    {
                        switch (info[i].Relationship)
                        {
                        case RelationProcessorCore: cores++; break;
                        case RelationProcessorPackage: packages++; break;
                        case RelationCache:
                            switch (info[i].Cache.Level)
                            {
                            case 1: l1 += info[i].Cache.Size / 1024; break;
                            case 2: l2 += info[i].Cache.Size / 1024; break;
                            case 3: l3 += info[i].Cache.Size / 1024; break;
                            }
                            break;
                        default: break;
                        }
                    }
                    if (cores) g.cores = cores;
                    if (packages) g.sockets = packages;
                    if (l1) g.l1KB = l1;
                    if (l2) g.l2KB = l2;
                    if (l3) g.l3KB = l3;
                }
                if (info) HeapFree(GetProcessHeap(), 0, info);
            }
        }
    }
    if (!g.cores) g.cores = g.nCpu;
    if (!g.sockets) g.sockets = 1;

    /* virtualization state comes from the kernel's firmware checks */
    if (IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED))
        g.virtMode = 1;
}

static void DetectMemoryDevices(void)
{
    typedef UINT (WINAPI *PFN_GetSystemFirmwareTable)(DWORD, DWORD, PVOID, DWORD);
    PFN_GetSystemFirmwareTable pGet = (PFN_GetSystemFirmwareTable)
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetSystemFirmwareTable");
    if (!pGet) return;

    const DWORD RSMB = 0x52534D42;   /* 'RSMB' */
    UINT cb = pGet(RSMB, 0, NULL, 0);
    if (!cb || cb > 1024 * 1024) return;
    BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, cb);
    if (!buf) return;
    if (pGet(RSMB, 0, buf, cb) != cb)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return;
    }

    /* RawSMBIOSData: BYTE method, major, minor, dmiRev; DWORD Length; data */
    if (cb < 8)
    {
        HeapFree(GetProcessHeap(), 0, buf);
        return;
    }
    DWORD len = *(DWORD*)(buf + 4);
    BYTE* p = buf + 8;
    BYTE* end = p + (len < cb - 8 ? len : cb - 8);

    ULONGLONG installed = 0;
    int used = 0, total = 0;
    DWORD speed = 0;
    BYTE ff = 0;

    while (p + 4 <= end)
    {
        BYTE type = p[0], slen = p[1];
        if (slen < 4) break;

        if (type == 17 && p + slen <= end)
        {
            total++;
            ULONGLONG bytes = 0;
            if (slen >= 0x0E)
            {
                WORD size = *(WORD*)(p + 0x0C);
                if (size == 0x7FFF && slen >= 0x20)
                {
                    DWORD ext = *(DWORD*)(p + 0x1C) & 0x7FFFFFFF;   /* MB */
                    bytes = (ULONGLONG)ext * 1024 * 1024;
                }
                else if (size != 0 && size != 0xFFFF)
                {
                    /* bit15: 1 = KB units, 0 = MB units */
                    if (size & 0x8000)
                        bytes = (ULONGLONG)(size & 0x7FFF) * 1024;
                    else
                        bytes = (ULONGLONG)size * 1024 * 1024;
                }
            }
            if (bytes)
            {
                used++;
                installed += bytes;
                if (!ff && slen > 0x0E) ff = p[0x0E];
                if (slen >= 0x17)
                {
                    WORD spd = *(WORD*)(p + 0x15);
                    if (spd && spd != 0xFFFF && spd > speed) speed = spd;
                }
            }
        }
        else if (type == 127)
        {
            break;
        }

        /* skip formatted area + trailing strings (double NUL) */
        BYTE* q = p + slen;
        while (q + 1 < end && (q[0] || q[1])) q++;
        p = q + 2;
    }
    HeapFree(GetProcessHeap(), 0, buf);

    g.ramInstalled = installed;
    g.ramSlotsUsed = used;
    g.ramSlotsTotal = total;
    g.ramSpeedMTs = speed;
    g.ramFormFactor = ff;
}

static BOOL GetVolumeDiskNumber(WCHAR driveLetter, DWORD* diskNumber)
{
    WCHAR path[] = L"\\\\.\\C:";
    path[4] = driveLetter;
    HANDLE volume = CreateFileW(path, 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                NULL, OPEN_EXISTING, 0, NULL);
    if (volume == INVALID_HANDLE_VALUE)
        return FALSE;

    STORAGE_DEVICE_NUMBER number;
    DWORD returned = 0;
    BOOL ok = DeviceIoControl(volume,
                              IOCTL_STORAGE_GET_DEVICE_NUMBER,
                              NULL, 0, &number, sizeof(number),
                              &returned, NULL);
    CloseHandle(volume);
    if (!ok || number.DeviceType != FILE_DEVICE_DISK)
        return FALSE;

    *diskNumber = number.DeviceNumber;
    return TRUE;
}

static void CopyDescriptorText(const BYTE* descriptor,
                               DWORD descriptorSize,
                               ULONG offset,
                               WCHAR* output,
                               int outputCount)
{
    output[0] = 0;
    if (!offset || offset >= descriptorSize)
        return;

    const char* text = (const char*)descriptor + offset;
    int length = 0;
    while (offset + length < descriptorSize && text[length])
        length++;
    while (length && (text[length - 1] == ' ' || text[length - 1] == '\t'))
        length--;
    if (!length)
        return;

    int converted = MultiByteToWideChar(CP_ACP, 0, text, length,
                                         output, outputCount - 1);
    if (converted > 0)
        output[converted] = 0;
}

static const WCHAR* StorageBusName(STORAGE_BUS_TYPE bus)
{
    switch ((int)bus)
    {
    case BusTypeScsi: return L"SCSI";
    case BusTypeAtapi: return L"ATAPI";
    case BusTypeAta: return L"ATA";
    case BusType1394: return L"IEEE 1394";
    case BusTypeSsa: return L"SSA";
    case BusTypeFibre: return L"Fibre Channel";
    case BusTypeUsb: return L"USB";
    case BusTypeRAID: return L"RAID";
    case BusTypeiScsi: return L"iSCSI";
    case BusTypeSas: return L"SAS";
    case BusTypeSata: return L"SATA";
    case BusTypeSd: return L"SD";
    case BusTypeMmc: return L"MMC";
    case BusTypeVirtual: return L"Virtual";
    case BusTypeFileBackedVirtual: return L"File-backed virtual";
    case 0x10: return L"Storage Spaces";
    case 0x11: return L"NVMe";
    case 0x12: return L"SCM";
    case 0x13: return L"UFS";
    default: return L"Unknown";
    }
}

static int FindDiskIndex(DWORD number)
{
    for (int i = 0; i < g.diskCount; i++)
        if (g.disks[i].number == number)
            return i;
    return -1;
}

static DiskSnapshot* AddDisk(DWORD number)
{
    if (g.diskCount >= TM_MAX_DISKS)
        return NULL;
    DiskSnapshot* disk = &g.disks[g.diskCount++];
    ZeroMemory(disk, sizeof(*disk));
    disk->number = number;
    return disk;
}

static void AppendDiskVolume(DiskSnapshot* disk, const WCHAR* root)
{
    WCHAR name[4] = { root[0], L':', 0, 0 };
    if (disk->volumes[0])
        StringCchCatW(disk->volumes, _countof(disk->volumes), L" ");
    StringCchCatW(disk->volumes, _countof(disk->volumes), name);
}

static BOOL SameDriveLetter(WCHAR first, WCHAR second)
{
    if (first >= L'a' && first <= L'z') first -= L'a' - L'A';
    if (second >= L'a' && second <= L'z') second -= L'a' - L'A';
    return first == second;
}

static void SortDisks(void)
{
    for (int i = 1; i < g.diskCount; i++)
    {
        DiskSnapshot disk = g.disks[i];
        int j = i;
        while (j > 0 && g.disks[j - 1].number > disk.number)
        {
            g.disks[j] = g.disks[j - 1];
            j--;
        }
        g.disks[j] = disk;
    }
}

static void DetectDiskDetails(int index)
{
    DiskSnapshot* disk = &g.disks[index];
    DiskSampler* sampler = &s_diskSamplers[index];
    WCHAR path[64];
    StringCchPrintfW(path, _countof(path),
                     L"\\\\.\\PhysicalDrive%lu", disk->number);
    sampler->handle = CreateFileW(path, 0,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
    if (sampler->handle != INVALID_HANDLE_VALUE)
    {
        disk->present = TRUE;
        DWORD returned = 0;
        GET_LENGTH_INFORMATION length;
        if (DeviceIoControl(sampler->handle, IOCTL_DISK_GET_LENGTH_INFO,
                            NULL, 0, &length, sizeof(length),
                            &returned, NULL))
        {
            disk->capacity = (ULONGLONG)length.Length.QuadPart;
        }

        BYTE descriptorBuffer[1024];
        ZeroMemory(descriptorBuffer, sizeof(descriptorBuffer));
        STORAGE_PROPERTY_QUERY query;
        ZeroMemory(&query, sizeof(query));
        query.PropertyId = StorageDeviceProperty;
        query.QueryType = PropertyStandardQuery;
        if (DeviceIoControl(sampler->handle, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof(query),
                            descriptorBuffer, sizeof(descriptorBuffer),
                            &returned, NULL) &&
            returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
        {
            PSTORAGE_DEVICE_DESCRIPTOR descriptor =
                (PSTORAGE_DEVICE_DESCRIPTOR)descriptorBuffer;
            DWORD validSize = descriptor->Size;
            if (!validSize || validSize > returned)
                validSize = returned;

            WCHAR vendor[64], product[96];
            CopyDescriptorText(descriptorBuffer, validSize,
                               descriptor->VendorIdOffset,
                               vendor, _countof(vendor));
            CopyDescriptorText(descriptorBuffer, validSize,
                               descriptor->ProductIdOffset,
                               product, _countof(product));
            if (vendor[0])
                StringCchCopyW(disk->model, _countof(disk->model), vendor);
            if (product[0])
            {
                if (disk->model[0])
                    StringCchCatW(disk->model, _countof(disk->model), L" ");
                StringCchCatW(disk->model, _countof(disk->model), product);
            }
            StringCchCopyW(disk->interfaceName,
                           _countof(disk->interfaceName),
                           StorageBusName(descriptor->BusType));

            /* SD/MMC media has no mechanical seek penalty, but that does not
               make it an SSD. Prefer the bus-specific media type over the
               generic seek-penalty heuristic below. */
            if (descriptor->BusType == BusTypeSd)
            {
                StringCchCopyW(disk->type, _countof(disk->type), L"SD card");
            }
            else if (descriptor->BusType == BusTypeMmc)
            {
                StringCchCopyW(disk->type, _countof(disk->type),
                               descriptor->RemovableMedia ? L"MMC card" : L"eMMC");
            }
        }

        DEVICE_SEEK_PENALTY_DESCRIPTOR penalty;
        ZeroMemory(&penalty, sizeof(penalty));
        ZeroMemory(&query, sizeof(query));
        query.PropertyId = StorageDeviceSeekPenaltyProperty;
        query.QueryType = PropertyStandardQuery;
        if (!disk->type[0] &&
            DeviceIoControl(sampler->handle, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof(query), &penalty, sizeof(penalty),
                            &returned, NULL))
        {
            StringCchCopyW(disk->type, _countof(disk->type),
                           penalty.IncursSeekPenalty ? L"HDD" : L"SSD");
        }
    }

    if (!disk->capacity)
        disk->capacity = disk->formatted;
    if (!disk->model[0])
        StringCchCopyW(disk->model, _countof(disk->model), L"Disk device");
    if (!disk->type[0])
        StringCchCopyW(disk->type, _countof(disk->type), L"Unknown");
    if (!disk->interfaceName[0])
        StringCchCopyW(disk->interfaceName,
                       _countof(disk->interfaceName), L"Unknown");
}

static void DetectDisks(void)
{
    g.diskCount = 0;
    ZeroMemory(g.disks, sizeof(g.disks));
    ZeroMemory(s_diskSamplers, sizeof(s_diskSamplers));
    for (int i = 0; i < TM_MAX_DISKS; i++)
        s_diskSamplers[i].handle = INVALID_HANDLE_VALUE;

    /* Enumerate mounted, accessible local volumes just as Explorer does,
       then group their drive letters by physical disk number. */
    WCHAR drives[256];
    if (GetLogicalDriveStringsW(_countof(drives), drives))
    {
        for (WCHAR* root = drives; *root; root += lstrlenW(root) + 1)
        {
            UINT driveType = GetDriveTypeW(root);
            if (driveType == DRIVE_NO_ROOT_DIR || driveType == DRIVE_REMOTE ||
                driveType == DRIVE_CDROM || driveType == DRIVE_RAMDISK)
            {
                continue;
            }

            ULARGE_INTEGER available, total, freeBytes;
            if (!GetDiskFreeSpaceExW(root, &available, &total, &freeBytes))
                continue;

            DWORD number;
            if (!GetVolumeDiskNumber(root[0], &number))
                continue;

            int index = FindDiskIndex(number);
            DiskSnapshot* disk = index >= 0 ? &g.disks[index] : AddDisk(number);
            if (!disk)
                continue;

            disk->present = TRUE;
            AppendDiskVolume(disk, root);
            disk->formatted += total.QuadPart;
            if (s_winDir[0] && SameDriveLetter(root[0], s_winDir[0]))
                disk->system = TRUE;
        }
    }

    /* Preserve the old disk-0 fallback on systems where volume-to-disk
       mapping is unavailable, so the Performance page still has a disk. */
    if (!g.diskCount)
    {
        DWORD number = 0;
        if (s_winDir[0] && s_winDir[1] == L':')
            GetVolumeDiskNumber(s_winDir[0], &number);
        DiskSnapshot* disk = AddDisk(number);
        if (disk)
            disk->system = TRUE;
    }

    SortDisks();
    for (int i = 0; i < g.diskCount; i++)
        DetectDiskDetails(i);

    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                      0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
    {
        WCHAR pagingFiles[1024];
        DWORD type = 0, cb = sizeof(pagingFiles);
        if (RegQueryValueExW(key, L"PagingFiles", NULL, &type,
                            (LPBYTE)pagingFiles, &cb) == ERROR_SUCCESS &&
            (type == REG_MULTI_SZ || type == REG_SZ))
        {
            pagingFiles[_countof(pagingFiles) - 1] = 0;
            for (WCHAR* entry = pagingFiles; *entry; entry += lstrlenW(entry) + 1)
            {
                if (entry[1] == L':')
                {
                    DWORD number;
                    if (GetVolumeDiskNumber(entry[0], &number))
                    {
                        int index = FindDiskIndex(number);
                        if (index >= 0)
                            g.disks[index].pageFile = TRUE;
                    }
                }
            }
        }
        RegCloseKey(key);
    }
}

static const WCHAR* NetworkTypeName(DWORD type)
{
    switch (type)
    {
    case IF_TYPE_IEEE80211: return L"Wi-Fi";
    case IF_TYPE_ETHERNET_CSMACD: return L"Ethernet";
    case IF_TYPE_PPP: return L"PPP";
    default: return L"Network";
    }
}

static void RefreshNetworkMetadata(const MIB_IFROW* row)
{
    StringCchCopyW(g.netType, _countof(g.netType), NetworkTypeName(row->dwType));
    g.netLinkBps = row->dwSpeed;
    g.netIpv4[0] = 0;
    g.netIpv6[0] = 0;
    g.netDns[0] = 0;
    g.netName[0] = 0;

    ULONG size = 0;
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                  GAA_FLAG_SKIP_DNS_SERVER;
    DWORD error = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &size);
    if (error == ERROR_BUFFER_OVERFLOW && size && size < 1024 * 1024)
    {
        PIP_ADAPTER_ADDRESSES addresses =
            (PIP_ADAPTER_ADDRESSES)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (addresses &&
            GetAdaptersAddresses(AF_UNSPEC, flags, NULL, addresses, &size) == NO_ERROR)
        {
            for (PIP_ADAPTER_ADDRESSES adapter = addresses;
                 adapter; adapter = adapter->Next)
            {
                if (adapter->IfIndex != row->dwIndex &&
                    adapter->Ipv6IfIndex != row->dwIndex)
                    continue;

                if (adapter->FriendlyName && adapter->FriendlyName[0])
                    StringCchCopyW(g.netName, _countof(g.netName), adapter->FriendlyName);
                if (adapter->Description && adapter->Description[0])
                    StringCchCopyW(g.netAdapter, _countof(g.netAdapter), adapter->Description);
                if (adapter->DnsSuffix && adapter->DnsSuffix[0])
                    StringCchCopyW(g.netDns, _countof(g.netDns), adapter->DnsSuffix);
                if (adapter->Length >= FIELD_OFFSET(IP_ADAPTER_ADDRESSES, ReceiveLinkSpeed) +
                                       sizeof(adapter->ReceiveLinkSpeed))
                {
                    g.netLinkBps = adapter->ReceiveLinkSpeed > adapter->TransmitLinkSpeed ?
                                   adapter->ReceiveLinkSpeed : adapter->TransmitLinkSpeed;
                }

                for (PIP_ADAPTER_UNICAST_ADDRESS address = adapter->FirstUnicastAddress;
                     address; address = address->Next)
                {
                    SOCKADDR* socketAddress = address->Address.lpSockaddr;
                    if (!socketAddress)
                        continue;
                    if (socketAddress->sa_family == AF_INET && !g.netIpv4[0])
                    {
                        SOCKADDR_IN* ipv4 = (SOCKADDR_IN*)socketAddress;
                        InetNtopW(AF_INET, &ipv4->sin_addr,
                                  g.netIpv4, _countof(g.netIpv4));
                    }
                    else if (socketAddress->sa_family == AF_INET6 && !g.netIpv6[0])
                    {
                        SOCKADDR_IN6* ipv6 = (SOCKADDR_IN6*)socketAddress;
                        WCHAR addressText[64];
                        if (InetNtopW(AF_INET6, &ipv6->sin6_addr,
                                     addressText, _countof(addressText)))
                        {
                            if (ipv6->sin6_scope_id)
                                StringCchPrintfW(g.netIpv6, _countof(g.netIpv6),
                                                 L"%s%%%lu", addressText,
                                                 ipv6->sin6_scope_id);
                            else
                                StringCchCopyW(g.netIpv6, _countof(g.netIpv6), addressText);
                        }
                    }
                }
                break;
            }
        }
        if (addresses)
            HeapFree(GetProcessHeap(), 0, addresses);
    }

    if (!g.netAdapter[0])
    {
        int descriptionLength = row->dwDescrLen;
        if (descriptionLength > MAXLEN_IFDESCR)
            descriptionLength = MAXLEN_IFDESCR;
        int converted = MultiByteToWideChar(CP_ACP, 0,
                                             (const char*)row->bDescr,
                                             descriptionLength,
                                             g.netAdapter,
                                             _countof(g.netAdapter) - 1);
        if (converted > 0)
            g.netAdapter[converted] = 0;
    }
    if (!g.netName[0])
        StringCchCopyW(g.netName, _countof(g.netName), g.netType);

    if (!g.netDns[0])
    {
        DWORD count = _countof(g.netDns);
        GetComputerNameExW(ComputerNameDnsFullyQualified, g.netDns, &count);
    }
}

static void PollDiskPerformance(double dt,
                                const SYSTEM_PERFORMANCE_INFORMATION* systemPerformance)
{
    ULONGLONG ioRead = (ULONGLONG)systemPerformance->IoReadTransferCount.QuadPart;
    ULONGLONG ioWrite = (ULONGLONG)systemPerformance->IoWriteTransferCount.QuadPart;
    g.diskReadBps = 0;
    g.diskWriteBps = 0;
    if (!s_first)
    {
        if (ioRead >= s_prevIoRead)
            g.diskReadBps = (double)(ioRead - s_prevIoRead) / dt;
        if (ioWrite >= s_prevIoWrite)
            g.diskWriteBps = (double)(ioWrite - s_prevIoWrite) / dt;
    }
    s_prevIoRead = ioRead;
    s_prevIoWrite = ioWrite;

    int validCount = 0;
    for (int i = 0; i < g.diskCount; i++)
    {
        DiskSnapshot* disk = &g.disks[i];
        DiskSampler* sampler = &s_diskSamplers[i];
        disk->readBps = 0;
        disk->writeBps = 0;
        disk->activePct = 0;
        disk->responseMs = 0;
        disk->perfValid = FALSE;
        if (sampler->handle == INVALID_HANDLE_VALUE)
            continue;

        DISK_PERFORMANCE current;
        DWORD returned = 0;
        ZeroMemory(&current, sizeof(current));
        if (!DeviceIoControl(sampler->handle, IOCTL_DISK_PERFORMANCE,
                             NULL, 0, &current, sizeof(current),
                             &returned, NULL))
        {
            sampler->havePrevious = FALSE;
            continue;
        }

        /* The first successful request holds the enable reference. Balance
           each later request; the retained reference is released at exit. */
        if (sampler->countersEnabled)
        {
            DWORD ignored;
            DeviceIoControl(sampler->handle, IOCTL_DISK_PERFORMANCE_OFF,
                            NULL, 0, NULL, 0, &ignored, NULL);
        }
        else
        {
            sampler->countersEnabled = TRUE;
        }

        if (sampler->havePrevious)
        {
            LONGLONG queryDelta = current.QueryTime.QuadPart -
                                  sampler->previous.QueryTime.QuadPart;
            LONGLONG readTimeDelta = current.ReadTime.QuadPart -
                                     sampler->previous.ReadTime.QuadPart;
            LONGLONG writeTimeDelta = current.WriteTime.QuadPart -
                                      sampler->previous.WriteTime.QuadPart;
            LONGLONG idleDelta = current.IdleTime.QuadPart -
                                 sampler->previous.IdleTime.QuadPart;
            ULONG readCountDelta = current.ReadCount -
                                   sampler->previous.ReadCount;
            ULONG writeCountDelta = current.WriteCount -
                                    sampler->previous.WriteCount;

            if (current.BytesRead.QuadPart >=
                sampler->previous.BytesRead.QuadPart)
            {
                disk->readBps = (double)(current.BytesRead.QuadPart -
                                         sampler->previous.BytesRead.QuadPart) / dt;
            }
            if (current.BytesWritten.QuadPart >=
                sampler->previous.BytesWritten.QuadPart)
            {
                disk->writeBps = (double)(current.BytesWritten.QuadPart -
                                          sampler->previous.BytesWritten.QuadPart) / dt;
            }
            if (queryDelta > 0)
            {
                LONGLONG activeDelta;
                if (current.IdleTime.QuadPart ||
                    sampler->previous.IdleTime.QuadPart)
                {
                    activeDelta = queryDelta - idleDelta;
                }
                else
                {
                    activeDelta = readTimeDelta + writeTimeDelta;
                }
                if (activeDelta < 0)
                    activeDelta = 0;
                if (activeDelta > queryDelta)
                    activeDelta = queryDelta;
                disk->activePct = 100.0 * activeDelta / queryDelta;
            }
            ULONG operationCount = readCountDelta + writeCountDelta;
            LONGLONG serviceTime = readTimeDelta + writeTimeDelta;
            if (operationCount && serviceTime > 0)
                disk->responseMs = serviceTime / (10000.0 * operationCount);
            disk->perfValid = TRUE;
            validCount++;
        }

        sampler->previous = current;
        sampler->havePrevious = TRUE;
    }

    /* A system-wide transfer delta cannot be split accurately among several
       disks. Retain the legacy fallback only when there is a single disk. */
    if (!validCount && g.diskCount == 1)
    {
        g.disks[0].readBps = g.diskReadBps;
        g.disks[0].writeBps = g.diskWriteBps;
    }
}

/* ------------------------------------------------------------------ */
/*  Init                                                               */
/* ------------------------------------------------------------------ */

void Init(void)
{
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    pQueryFullProcessImageNameW = (PFN_QueryFullProcessImageNameW)GetProcAddress(k32, "QueryFullProcessImageNameW");
    pIsWow64Process = (PFN_IsWow64Process)GetProcAddress(k32, "IsWow64Process");
    pGetProcessInformation = (PFN_GetProcessInformation)GetProcAddress(k32, "GetProcessInformation");
    pIsHungAppWindow = (PFN_IsHungAppWindow)GetProcAddress(u32, "IsHungAppWindow");

    s_currentPid = GetCurrentProcessId();
    ProcessIdToSessionId(s_currentPid, &s_currentSession);
    LoadAppHistory();

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    s_qpcFreq = (double)f.QuadPart;

    WSADATA wsaData;
    s_wsaStarted = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;

    GetWindowsDirectoryW(s_winDir, _countof(s_winDir));

    /* static hardware info */
    SYSTEM_BASIC_INFORMATION sbi;
    if (NT_SUCCESS(NtQuerySystemInformation(SystemBasicInformation, &sbi, sizeof(sbi), NULL)))
    {
        g.nCpu = sbi.NumberOfProcessors;
        g.pageSize = sbi.PageSize;
        g.ramBytes = (ULONGLONG)sbi.NumberOfPhysicalPages * sbi.PageSize;
    }
    if (g.nCpu < 1) g.nCpu = 1;
    if (!g.pageSize) g.pageSize = 4096;

    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        DWORD cb = sizeof(g.cpuName);
        RegQueryValueExW(hk, L"ProcessorNameString", NULL, NULL, (LPBYTE)g.cpuName, &cb);
        DWORD mhz = 0; cb = sizeof(mhz);
        if (RegQueryValueExW(hk, L"~MHz", NULL, NULL, (LPBYTE)&mhz, &cb) == ERROR_SUCCESS)
            g.cpuMHz = mhz;
        RegCloseKey(hk);
    }
    if (!g.cpuName[0])
        StringCchCopyW(g.cpuName, _countof(g.cpuName), L"Processor");

    DWORD cch = _countof(g.hostName);
    GetComputerNameW(g.hostName, &cch);

    DetectCpuTopology();
    DetectMemoryDevices();
    DetectDisks();

    RefreshServices();
    RefreshStartup();
    RefreshUsers();
    Tick();          /* prime deltas; the frame collects the first live sample */
}

void Shutdown(void)
{
    SaveAppHistory();
    DestroyAppHistory();
    ShutdownTelemetry();
    for (int i = 0; i < s_extras.n; i++)
    {
        if (s_extras[i]->icon) DestroyIcon(s_extras[i]->icon);
        HeapFree(GetProcessHeap(), 0, s_extras[i]);
    }
    s_extras.Clear();
    if (s_procBuf) HeapFree(GetProcessHeap(), 0, s_procBuf);
    s_procBuf = NULL;
    if (s_netTable) HeapFree(GetProcessHeap(), 0, s_netTable);
    s_netTable = NULL;
    s_netTableSize = 0;
    for (int i = 0; i < g.diskCount; i++)
    {
        DiskSampler* sampler = &s_diskSamplers[i];
        if (sampler->handle == INVALID_HANDLE_VALUE)
            continue;
        if (sampler->countersEnabled)
        {
            DWORD ignored;
            DeviceIoControl(sampler->handle, IOCTL_DISK_PERFORMANCE_OFF,
                            NULL, 0, NULL, 0, &ignored, NULL);
            sampler->countersEnabled = FALSE;
        }
        CloseHandle(sampler->handle);
        sampler->handle = INVALID_HANDLE_VALUE;
    }
    if (s_wsaStarted)
    {
        WSACleanup();
        s_wsaStarted = FALSE;
    }
}

/* ------------------------------------------------------------------ */
/*  Window enumeration (app detection)                                 */
/* ------------------------------------------------------------------ */

static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lp)
{
    (void)lp;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER)) return TRUE;
    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;

    WCHAR title[128] = L"";
    GetWindowTextW(hwnd, title, _countof(title));

    /* keep the first titled window per process; else remember untitled */
    for (int i = 0; i < s_wnds.n; i++)
    {
        if (s_wnds[i].pid == pid)
        {
            if (!s_wnds[i].title[0] && title[0])
            {
                s_wnds[i].hwnd = hwnd;
                StringCchCopyW(s_wnds[i].title, 128, title);
            }
            return TRUE;
        }
    }
    WndEntry* e = s_wnds.Add();
    if (e)
    {
        e->pid = pid;
        e->hwnd = hwnd;
        StringCchCopyW(e->title, 128, title);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Extra (slow) process info, resolved lazily                         */
/* ------------------------------------------------------------------ */

static void DevicePathToDos(WCHAR* path, int cch)
{
    /* \Device\HarddiskVolume1\... -> C:\... */
    if (path[0] != L'\\') return;
    WCHAR drives[512];
    if (!GetLogicalDriveStringsW(_countof(drives), drives)) return;
    for (WCHAR* d = drives; *d; d += lstrlenW(d) + 1)
    {
        WCHAR root[3] = { d[0], L':', 0 };
        WCHAR dev[MAX_PATH];
        if (QueryDosDeviceW(root, dev, _countof(dev)))
        {
            int len = lstrlenW(dev);
            if (_wcsnicmp(path, dev, len) == 0 && path[len] == L'\\')
            {
                WCHAR tmp[MAX_PATH];
                StringCchPrintfW(tmp, _countof(tmp), L"%s%s", root, path + len);
                StringCchCopyW(path, cch, tmp);
                return;
            }
        }
    }
}

static void WellKnownUser(const WCHAR* image, WCHAR* buf, int cch)
{
    if (NameInList(image, s_criticalProcs, _countof(s_criticalProcs)) ||
        lstrcmpiW(image, L"svchost.exe") == 0 ||
        lstrcmpiW(image, L"System Idle Process") == 0)
        StringCchCopyW(buf, cch, L"SYSTEM");
    else
        buf[0] = 0;
}

static void ResolveExtra(ProcRow& p)
{
    ProcExtra* x = p.x;
    x->resolved = TRUE;

    if (p.pid == 0 || p.pid == 4)
    {
        StringCchCopyW(x->user, _countof(x->user), L"SYSTEM");
        StringCchCopyW(x->desc, _countof(x->desc),
                       p.pid ? L"NT Kernel & System" : L"Percentage of time the processor is idle");
        return;
    }

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid);
    if (!h)
        h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.pid);

    if (h)
    {
        /* image path */
        WCHAR path[MAX_PATH] = L"";
        DWORD cch = _countof(path);
        BOOL got = FALSE;
        if (pQueryFullProcessImageNameW)
            got = pQueryFullProcessImageNameW(h, 0, path, &cch);
        if (!got)
        {
            if (GetProcessImageFileNameW(h, path, _countof(path)))
            {
                DevicePathToDos(path, _countof(path));
                got = path[0] == L'C' || path[1] == L':';
            }
        }
        if (got)
            StringCchCopyW(x->path, _countof(x->path), path);

        /* user name from token */
        HANDLE tok;
        if (OpenProcessToken(h, TOKEN_QUERY, &tok))
        {
            BYTE tuBuf[256];
            DWORD cb = 0;
            if (GetTokenInformation(tok, TokenUser, tuBuf, sizeof(tuBuf), &cb))
            {
                TOKEN_USER* tu = (TOKEN_USER*)tuBuf;
                WCHAR name[96] = L"", dom[96] = L"";
                DWORD cn = _countof(name), cd = _countof(dom);
                SID_NAME_USE use;
                if (LookupAccountSidW(NULL, tu->User.Sid, name, &cn, dom, &cd, &use))
                {
                    if (g_app.st.fullAcctName && dom[0])
                        StringCchPrintfW(x->user, _countof(x->user), L"%s\\%s", dom, name);
                    else
                        StringCchCopyW(x->user, _countof(x->user), name);
                }
            }
            CloseHandle(tok);
        }

        /* architecture */
        PROCESS_MACHINE_INFORMATION machineInfo;
        if (pGetProcessInformation && pGetProcessInformation(h, ProcessMachineTypeInfo, &machineInfo, sizeof(machineInfo)))
        {
            switch (machineInfo.ProcessMachine)
            {
                case IMAGE_FILE_MACHINE_I386:
                    StringCchCopyW(x->arch, _countof(x->arch), L"x86");
                    break;

                case IMAGE_FILE_MACHINE_AMD64:
                case IMAGE_FILE_MACHINE_ARM64EC:
                    StringCchCopyW(x->arch, _countof(x->arch), L"x64");
                    break;

                case IMAGE_FILE_MACHINE_ARM64:
                    StringCchCopyW(x->arch, _countof(x->arch), L"ARM64");
                    break;
            }
        }
        else if (pIsWow64Process)
        {
            BOOL wow = FALSE;
            if (pIsWow64Process(h, &wow))
#if defined(_M_ARM64)
                StringCchCopyW(x->arch, _countof(x->arch), wow ? L"x86" : L"ARM64");
#else
                StringCchCopyW(x->arch, _countof(x->arch), wow ? L"x86" : L"x64");
#endif
        }

        CloseHandle(h);
    }

    if (!x->user[0])
        WellKnownUser(p.image, x->user, _countof(x->user));

    if (x->path[0])
    {
        /* description from version resource */
        DWORD dummy;
        DWORD cb = GetFileVersionInfoSizeW(x->path, &dummy);
        if (cb && cb < 256 * 1024)
        {
            void* blk = HeapAlloc(GetProcessHeap(), 0, cb);
            if (blk && GetFileVersionInfoW(x->path, 0, cb, blk))
            {
                struct LANGCP { WORD lang, cp; } *lc = NULL;
                UINT lcLen = 0;
                VerQueryValueW(blk, L"\\VarFileInfo\\Translation", (void**)&lc, &lcLen);
                WORD lang = 0x0409, cp = 0x04B0;
                if (lc && lcLen >= sizeof(LANGCP)) { lang = lc->lang; cp = lc->cp; }
                WCHAR sub[64];
                StringCchPrintfW(sub, _countof(sub),
                                 L"\\StringFileInfo\\%04x%04x\\FileDescription", lang, cp);
                WCHAR* descr = NULL;
                UINT dLen = 0;
                if (VerQueryValueW(blk, sub, (void**)&descr, &dLen) && descr && descr[0])
                    StringCchCopyW(x->desc, _countof(x->desc), descr);

                if (!x->desc[0])
                {
                    StringCchPrintfW(sub, _countof(sub),
                                     L"\\StringFileInfo\\040904B0\\FileDescription");
                    if (VerQueryValueW(blk, sub, (void**)&descr, &dLen) && descr && descr[0])
                        StringCchCopyW(x->desc, _countof(x->desc), descr);
                }
            }
            if (blk) HeapFree(GetProcessHeap(), 0, blk);
        }

        /* small icon */
        SHFILEINFOW sfi;
        ZeroMemory(&sfi, sizeof(sfi));
        if (SHGetFileInfoW(x->path, 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON))
            x->icon = sfi.hIcon;
    }

    if (!x->desc[0])
        StringCchCopyW(x->desc, _countof(x->desc), p.image);
}

/* company name of an exe (startup page) */
BOOL GetFileCompany(const WCHAR* path, WCHAR* buf, int cch)
{
    buf[0] = 0;
    DWORD dummy;
    DWORD cb = GetFileVersionInfoSizeW(path, &dummy);
    if (!cb || cb > 256 * 1024) return FALSE;
    void* blk = HeapAlloc(GetProcessHeap(), 0, cb);
    if (!blk) return FALSE;
    BOOL ok = FALSE;
    if (GetFileVersionInfoW(path, 0, cb, blk))
    {
        struct LANGCP { WORD lang, cp; } *lc = NULL;
        UINT lcLen = 0;
        VerQueryValueW(blk, L"\\VarFileInfo\\Translation", (void**)&lc, &lcLen);
        WORD lang = 0x0409, cp = 0x04B0;
        if (lc && lcLen >= 4) { lang = lc->lang; cp = lc->cp; }
        WCHAR sub[64];
        StringCchPrintfW(sub, _countof(sub), L"\\StringFileInfo\\%04x%04x\\CompanyName", lang, cp);
        WCHAR* v = NULL; UINT vLen = 0;
        if (VerQueryValueW(blk, sub, (void**)&v, &vLen) && v && v[0])
        {
            StringCchCopyW(buf, cch, v);
            ok = TRUE;
        }
    }
    HeapFree(GetProcessHeap(), 0, blk);
    return ok;
}

/* ------------------------------------------------------------------ */
/*  Main tick                                                          */
/* ------------------------------------------------------------------ */

static const WndEntry* FindWnd(ULONG pid)
{
    for (int i = 0; i < s_wnds.n; i++)
        if (s_wnds[i].pid == pid) return &s_wnds[i];
    return NULL;
}

static int __cdecl ComparePrevProc(const void* first, const void* second)
{
    const PrevProc* a = (const PrevProc*)first;
    const PrevProc* b = (const PrevProc*)second;
    if (a->pid != b->pid)
        return a->pid < b->pid ? -1 : 1;
    if (a->createTime != b->createTime)
        return a->createTime < b->createTime ? -1 : 1;
    return 0;
}

static const PrevProc* FindPrev(ULONG pid, LONGLONG create)
{
    int low = 0, high = s_prev.n;
    while (low < high)
    {
        int middle = low + (high - low) / 2;
        const PrevProc& previous = s_prev[middle];
        if (previous.pid < pid ||
            (previous.pid == pid && previous.createTime < create))
            low = middle + 1;
        else
            high = middle;
    }
    if (low < s_prev.n && s_prev[low].pid == pid &&
        s_prev[low].createTime == create)
        return &s_prev[low];
    return NULL;
}

static PMIB_IFTABLE QueryNetworkTable(void)
{
    ULONG needed = s_netTableSize;
    DWORD error;

    if (s_netTable)
    {
        error = GetIfTable(s_netTable, &needed, FALSE);
        if (error == NO_ERROR)
            return s_netTable;
        if (error != ERROR_INSUFFICIENT_BUFFER)
            return NULL;
    }
    else
    {
        needed = 0;
        error = GetIfTable(NULL, &needed, FALSE);
        if (error != ERROR_INSUFFICIENT_BUFFER)
            return NULL;
    }

    if (!needed || needed >= 1024 * 1024)
        return NULL;

    PMIB_IFTABLE table = s_netTable ?
        (PMIB_IFTABLE)HeapReAlloc(GetProcessHeap(), 0, s_netTable, needed) :
        (PMIB_IFTABLE)HeapAlloc(GetProcessHeap(), 0, needed);
    if (!table)
        return NULL;

    s_netTable = table;
    s_netTableSize = needed;
    ULONG size = s_netTableSize;
    return GetIfTable(s_netTable, &size, FALSE) == NO_ERROR ? s_netTable : NULL;
}

const WCHAR* SvchostServices(ULONG pid)
{
    for (int i = 0; i < s_svchostMap.n; i++)
        if (s_svchostMap[i].pid == pid) return s_svchostMap[i].names;
    return NULL;
}

const WCHAR* SvchostGroup(ULONG pid)
{
    for (int i = 0; i < s_svchostMap.n; i++)
        if (s_svchostMap[i].pid == pid && s_svchostMap[i].group[0])
            return s_svchostMap[i].group;
    return NULL;
}

static AppHistRow* FindAppHistory(const ProcRow& process)
{
    const WCHAR* path = process.x ? process.x->path : NULL;
    if (process.x && process.x->appHistorySlot > 0 &&
        process.x->appHistorySlot <= (DWORD)s_appHist.n)
    {
        AppHistRow* cached = &s_appHist[process.x->appHistorySlot - 1];
        if ((path && path[0] && cached->path[0] &&
             lstrcmpiW(cached->path, path) == 0) ||
            lstrcmpiW(cached->image, process.image) == 0)
            return cached;
        process.x->appHistorySlot = 0;
    }

    for (int i = 0; i < s_appHist.n; i++)
    {
        if (path && path[0] && s_appHist[i].path[0] &&
            lstrcmpiW(s_appHist[i].path, path) == 0)
        {
            if (process.x) process.x->appHistorySlot = i + 1;
            return &s_appHist[i];
        }
        if (lstrcmpiW(s_appHist[i].image, process.image) == 0)
        {
            if (process.x) process.x->appHistorySlot = i + 1;
            return &s_appHist[i];
        }
    }
    return NULL;
}

static BOOL SetAppHistoryDisplayName(AppHistRow* history,
                                     const ProcRow& process)
{
    const WCHAR* name = process.image;
    if (process.x && process.x->desc[0] &&
        lstrcmpiW(process.x->desc, process.image) != 0)
    {
        name = process.x->desc;
    }

    WCHAR displayName[_countof(history->displayName)];
    StringCchCopyW(displayName, _countof(displayName), name);
    int length = lstrlenW(displayName);
    if (length > 4 && lstrcmpiW(displayName + length - 4, L".exe") == 0)
        displayName[length - 4] = 0;

    if (lstrcmpW(history->displayName, displayName) == 0)
        return FALSE;
    StringCchCopyW(history->displayName, _countof(history->displayName), displayName);
    return TRUE;
}

static void AccumHistory(const ProcRow& process, LONGLONG cpuDelta)
{
    if (process.pid == s_currentPid || process.pid <= 4 ||
        process.sessionId != s_currentSession)
        return;

    AppHistRow* history = FindAppHistory(process);
    BOOL changed = FALSE;
    if (!history)
    {
        if (process.category != CAT_APP || !process.x || !process.x->resolved)
            return;
        if ((DWORD)s_appHist.n >= APP_HISTORY_MAX_ROWS)
            return;
        history = s_appHist.Add();
        if (!history)
            return;
        StringCchCopyW(history->image, _countof(history->image), process.image);
        if (process.x) process.x->appHistorySlot = s_appHist.n;
        changed = TRUE;
    }

    if (SetAppHistoryDisplayName(history, process))
        changed = TRUE;
    if (process.x)
    {
        if (process.x->path[0] &&
            lstrcmpW(history->path, process.x->path) != 0)
        {
            StringCchCopyW(history->path, _countof(history->path), process.x->path);
            changed = TRUE;
        }
        if (!history->icon && process.x->icon)
        {
            history->icon = CopyIcon(process.x->icon);
            if (history->icon) history->iconResolved = TRUE;
        }
    }
    if (cpuDelta > 0)
    {
        history->cpu100ns += cpuDelta;
        changed = TRUE;
    }
    if (changed) s_appHistDirty = TRUE;
}

void Tick(void)
{
    /* ---- timing ---- */
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    double dt = s_lastQpc ? (qpc.QuadPart - s_lastQpc) / s_qpcFreq : 1.0;
    if (dt <= 0.001) dt = 0.001;
    s_lastQpc = qpc.QuadPart;

    /* ---- global performance counters ---- */
    SYSTEM_PERFORMANCE_INFORMATION perf;
    ZeroMemory(&perf, sizeof(perf));
    NtQuerySystemInformation(SystemPerformanceInformation, &perf, sizeof(perf), NULL);

    SYSTEM_TIMEOFDAY_INFORMATION tod;
    ZeroMemory(&tod, sizeof(tod));
    if (NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, &tod, sizeof(tod), NULL)))
        g.upSeconds = (ULONGLONG)((tod.CurrentTime.QuadPart - tod.BootTime.QuadPart) / 10000000);

    SYSTEM_FILECACHE_INFORMATION fc;
    ZeroMemory(&fc, sizeof(fc));
    NtQuerySystemInformation(SystemFileCacheInformation, &fc, sizeof(fc), NULL);

    /* current cpu speed (guarded; older kernels fail this cleanly) */
    {
        struct TM_PPI
        {
            ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState;
        } ppi[64];
        if (g.nCpu <= 64 &&
            NT_SUCCESS(NtPowerInformation(ProcessorInformation, NULL, 0,
                                          ppi, sizeof(ppi[0]) * g.nCpu)))
        {
            DWORD cur = 0, maxMhz = 0;
            for (int i = 0; i < g.nCpu; i++)
            {
                if (ppi[i].CurrentMhz > cur) cur = ppi[i].CurrentMhz;
                if (ppi[i].MaxMhz > maxMhz) maxMhz = ppi[i].MaxMhz;
            }
            if (cur) g.cpuCurMHz = cur;
            if (maxMhz && !g.cpuMHz) g.cpuMHz = maxMhz;
        }
    }

    /* ---- cpu total ---- */
    {
        SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION sppi[64];
        ULONG got = 0;
        LONGLONG idle = 0, kern = 0, user = 0;
        if (NT_SUCCESS(NtQuerySystemInformation(SystemProcessorPerformanceInformation,
                                                sppi, sizeof(sppi), &got)))
        {
            int n = got / sizeof(sppi[0]);
            if (n > 64) n = 64;
            for (int i = 0; i < n; i++)
            {
                idle += sppi[i].IdleTime.QuadPart;
                kern += sppi[i].KernelTime.QuadPart;   /* includes idle */
                user += sppi[i].UserTime.QuadPart;

                LONGLONG cpuIdle = sppi[i].IdleTime.QuadPart;
                LONGLONG cpuKernel = sppi[i].KernelTime.QuadPart;
                LONGLONG cpuUser = sppi[i].UserTime.QuadPart;
                LONGLONG idleDelta = cpuIdle - s_prevCpuIdle[i];
                LONGLONG kernelDelta = cpuKernel - s_prevCpuKernel[i];
                LONGLONG userDelta = cpuUser - s_prevCpuUser[i];
                LONGLONG totalDelta = kernelDelta + userDelta;
                double busyPercent = 0;
                double kernelPercent = 0;
                if (totalDelta > 0 && s_prevCpuKernel[i])
                {
                    double busy = (double)(totalDelta - idleDelta) / totalDelta;
                    double kernelBusy = (double)(kernelDelta - idleDelta) / totalDelta;
                    if (busy < 0) busy = 0;
                    if (busy > 1) busy = 1;
                    if (kernelBusy < 0) kernelBusy = 0;
                    if (kernelBusy > 1) kernelBusy = 1;
                    busyPercent = busy * 100.0;
                    kernelPercent = kernelBusy * 100.0;
                }
                g.hCpuLogical[i].Push((float)busyPercent);
                g.hCpuLogicalKernel[i].Push((float)kernelPercent);
                s_prevCpuIdle[i] = cpuIdle;
                s_prevCpuKernel[i] = cpuKernel;
                s_prevCpuUser[i] = cpuUser;
            }
        }
        LONGLONG dIdle = idle - s_prevIdle;
        LONGLONG dKern = kern - s_prevKernel;
        LONGLONG dUser = user - s_prevUser;
        LONGLONG dTotal = dKern + dUser;
        if (dTotal > 0 && s_prevKernel)
        {
            double busy = (double)(dTotal - dIdle) / (double)dTotal;
            if (busy < 0) busy = 0;
            if (busy > 1) busy = 1;
            g.cpuTotalPct = busy * 100.0;
            double kernBusy = (double)(dKern - dIdle) / (double)dTotal;
            if (kernBusy < 0) kernBusy = 0;
            g.cpuKernelPct = kernBusy * 100.0;
        }
        s_prevIdle = idle; s_prevKernel = kern; s_prevUser = user;
    }

    /* ---- memory ---- */
    g.memTotal = g.ramBytes;
    g.memAvail = (ULONGLONG)perf.AvailablePages * g.pageSize;
    if (g.memAvail > g.memTotal) g.memAvail = g.memTotal;
    g.memInUse = g.memTotal - g.memAvail;
    g.memCommit = (ULONGLONG)perf.CommittedPages * g.pageSize;
    g.memCommitLimit = (ULONGLONG)perf.CommitLimit * g.pageSize;
    g.memCached = (ULONGLONG)fc.CurrentSize;
    g.memPagedPool = (ULONGLONG)perf.PagedPoolPages * g.pageSize;
    g.memNonPagedPool = (ULONGLONG)perf.NonPagedPoolPages * g.pageSize;

    /* IOCTL_DISK_PERFORMANCE is preferred; ReactOS installations without
       diskperf support retain the system-wide transfer-rate fallback. */
    PollDiskPerformance(dt, &perf);
    if (g.diskReadBps < 0) g.diskReadBps = 0;
    if (g.diskWriteBps < 0) g.diskWriteBps = 0;
    for (int i = 0; i < g.diskCount; i++)
    {
        if (g.disks[i].readBps < 0) g.disks[i].readBps = 0;
        if (g.disks[i].writeBps < 0) g.disks[i].writeBps = 0;
    }

    /* ---- network ---- */
    {
        g.netPresent = FALSE;
        g.netConnected = FALSE;
        MIB_IFROW selected;
        ZeroMemory(&selected, sizeof(selected));
        MIB_IFROW* best = NULL;

        /* Once selected, query only that interface. Re-enumerate when it is
           unavailable or disconnected so another connected adapter can win. */
        if (s_netIfIndex != (DWORD)-1)
        {
            selected.dwIndex = s_netIfIndex;
            if (GetIfEntry(&selected) == NO_ERROR &&
                selected.dwOperStatus >= IF_OPER_STATUS_CONNECTED)
                best = &selected;
        }

        if (!best)
        {
            PMIB_IFTABLE table = QueryNetworkTable();
            if (table)
            {
                for (DWORD i = 0; i < table->dwNumEntries; i++)
                {
                    MIB_IFROW* row = &table->table[i];
                    if (row->dwType == MIB_IF_TYPE_LOOPBACK) continue;
                    BOOL connected = row->dwOperStatus >= IF_OPER_STATUS_CONNECTED;
                    BOOL bestConnected = best &&
                        best->dwOperStatus >= IF_OPER_STATUS_CONNECTED;
                    if (s_netIfIndex == row->dwIndex && connected)
                    {
                        best = row;
                        break;
                    }
                    if (!best || (connected && !bestConnected) ||
                        (connected == bestConnected &&
                         (row->dwInOctets + row->dwOutOctets) >
                         (best->dwInOctets + best->dwOutOctets)))
                        best = row;
                }
            }
        }

        if (best)
        {
            if (s_netIfIndex != best->dwIndex)
            {
                s_netIfIndex = best->dwIndex;
                s_prevNetIn = best->dwInOctets;
                s_prevNetOut = best->dwOutOctets;
                s_netInfoAge = 999;
            }
            DWORD dIn = best->dwInOctets - s_prevNetIn;    /* wraps ok (unsigned) */
            DWORD dOut = best->dwOutOctets - s_prevNetOut;
            g.netRecvBps = dIn / dt;
            g.netSendBps = dOut / dt;
            s_prevNetIn = best->dwInOctets;
            s_prevNetOut = best->dwOutOctets;
            g.netPresent = TRUE;
            g.netConnected = best->dwOperStatus >= IF_OPER_STATUS_CONNECTED;

            if (++s_netInfoAge > 30)
            {
                s_netInfoAge = 0;
                RefreshNetworkMetadata(best);
            }
        }
        if (!g.netPresent)
        {
            g.netRecvBps = 0;
            g.netSendBps = 0;
        }
    }

    /* ---- window map ---- */
    s_wnds.Clear();
    EnumWindows(EnumWndProc, 0);

    /* ---- process list ---- */
    if (!s_procBuf)
    {
        s_procBufSize = 64 * 1024;
        s_procBuf = (PUCHAR)HeapAlloc(GetProcessHeap(), 0, s_procBufSize);
    }
    NTSTATUS st = STATUS_NO_MEMORY;
    ULONG need = 0;
    while (s_procBuf)
    {
        st = NtQuerySystemInformation(SystemProcessInformation,
                                      s_procBuf, s_procBufSize, &need);
        if (st != STATUS_INFO_LENGTH_MISMATCH) break;
        ULONG newSize;
        if (need > s_procBufSize && need <= MAXULONG - 64 * 1024)
            newSize = need + 64 * 1024;
        else if (s_procBufSize <= MAXULONG / 2)
            newSize = s_procBufSize * 2;
        else
            break;
        PUCHAR nb = (PUCHAR)HeapReAlloc(GetProcessHeap(), 0, s_procBuf, newSize);
        if (!nb) break;
        s_procBuf = nb;
        s_procBufSize = newSize;
    }

    /* stash previous cumulative counters for delta computation */
    s_prev.Clear();
    for (int i = 0; i < g.procs.n; i++)
    {
        PrevProc pp;
        pp.pid = g.procs[i].pid;
        pp.createTime = g.procs[i].createTime;
        pp.cpu100ns = g.procs[i].cpu100ns;
        pp.ioBytes = g.procs[i].ioBytes;
        pp.extra = g.procs[i].x;
        s_prev.Push(pp);
    }
    if (s_prev.n > 1)
        qsort(s_prev.p, s_prev.n, sizeof(PrevProc), ComparePrevProc);

    g.procs.Clear();
    g.procCount = 0;
    g.threadCount = 0;
    g.handleCount = 0;

    /* GC mark */
    for (int i = 0; i < s_extras.n; i++)
        s_extras[i]->inUse = FALSE;

    if (NT_SUCCESS(st))
    {
        PUCHAR ptr = s_procBuf;
        for (;;)
        {
            PSYSTEM_PROCESS_INFORMATION spi = (PSYSTEM_PROCESS_INFORMATION)ptr;
            ProcRow* p = g.procs.Add();
            if (!p) break;

            p->pid = HandleToUlong(spi->UniqueProcessId);
            p->ppid = HandleToUlong(spi->InheritedFromUniqueProcessId);
            p->sessionId = spi->SessionId;
            p->threads = spi->NumberOfThreads;
            p->handles = spi->HandleCount;
            p->basePri = spi->BasePriority;
            p->createTime = spi->CreateTime.QuadPart;
            p->cpu100ns = spi->KernelTime.QuadPart + spi->UserTime.QuadPart;
            p->ioBytes = (ULONGLONG)spi->ReadTransferCount.QuadPart +
                         (ULONGLONG)spi->WriteTransferCount.QuadPart;

            if (spi->ImageName.Buffer && spi->ImageName.Length)
            {
                /* basename only (some kernels return a path) */
                WCHAR tmp[MAX_PATH];
                UINT n = spi->ImageName.Length / sizeof(WCHAR);
                if (n >= _countof(tmp)) n = _countof(tmp) - 1;
                CopyMemory(tmp, spi->ImageName.Buffer, n * sizeof(WCHAR));
                tmp[n] = 0;
                const WCHAR* base = wcsrchr(tmp, L'\\');
                StringCchCopyW(p->image, _countof(p->image), base ? base + 1 : tmp);
            }
            else
            {
                StringCchCopyW(p->image, _countof(p->image),
                               p->pid == 0 ? L"System Idle Process" : L"System");
            }

            /* Private working set is ideal. ReactOS currently leaves it zero,
               but does provide the process commit charge in PrivatePageCount. */
            p->memBytes = (ULONGLONG)spi->WorkingSetPrivateSize.QuadPart;
            if (!p->memBytes)
                p->memBytes = (ULONGLONG)spi->PrivatePageCount;
            if (!p->memBytes)
                p->memBytes = (ULONGLONG)spi->PagefileUsage;

            /* deltas */
            const PrevProc* pv = FindPrev(p->pid, p->createTime);
            LONGLONG cpuDelta;
            if (pv)
            {
                cpuDelta = p->cpu100ns - pv->cpu100ns;
                p->diskBps = (double)(LONGLONG)(p->ioBytes - pv->ioBytes) / dt;
                if (p->diskBps < 0) p->diskBps = 0;
            }
            else
            {
                cpuDelta = 0;
                p->flags |= PF_NEW;
            }
            double denom = dt * g.nCpu * 10000000.0;
            p->cpuPct = denom > 0 ? (cpuDelta * 100.0) / denom : 0;
            if (p->cpuPct < 0) p->cpuPct = 0;
            if (p->cpuPct > 100) p->cpuPct = 100;

            /* suspended? all threads in Wait/Suspended */
            if (spi->NumberOfThreads)
            {
                PSYSTEM_THREAD_INFORMATION th = (PSYSTEM_THREAD_INFORMATION)(spi + 1);
                BOOL allSusp = TRUE;
                for (ULONG t = 0; t < spi->NumberOfThreads; t++)
                {
                    if (!(th[t].ThreadState == TM_THREADSTATE_WAITING &&
                          th[t].WaitReason == TM_WAITREASON_SUSPENDED))
                    {
                        allSusp = FALSE;
                        break;
                    }
                }
                if (allSusp) p->flags |= PF_SUSPENDED;
            }

            /* windows */
            const WndEntry* we = (p->pid > 4) ? FindWnd(p->pid) : NULL;
            if (we)
            {
                p->flags |= PF_HASWINDOW;
                p->mainWnd = we->hwnd;
                StringCchCopyW(p->wndTitle, _countof(p->wndTitle),
                               we->title[0] ? we->title : p->image);
                if (pIsHungAppWindow && pIsHungAppWindow(we->hwnd))
                    p->flags |= PF_HUNG;
            }

            if (lstrcmpiW(p->image, L"svchost.exe") == 0)
                p->flags |= PF_SVCHOST;
            if (NameInList(p->image, s_criticalProcs, _countof(s_criticalProcs)))
                p->flags |= PF_CRITICAL;
            /* category */
            if ((p->flags & PF_HASWINDOW) && p->wndTitle[0])
                p->category = CAT_APP;
            else if (NameInList(p->image, s_windowsProcs, _countof(s_windowsProcs)))
                p->category = CAT_WINDOWS;
            else
                p->category = CAT_BACKGROUND;

            /* extras cache */
            ProcExtra* x = pv ? pv->extra : NULL;
            if (!x)
            {
                x = (ProcExtra*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ProcExtra));
                if (x)
                {
                    x->pid = p->pid;
                    x->createTime = p->createTime;
                    s_extras.Push(x);
                }
            }
            if (x) x->inUse = TRUE;
            p->x = x;

            /* leaf = explicitly set by us, or exactly idle base priority */
            if ((x && x->efficiencySet) || (p->basePri == 4 && p->pid > 4))
                p->flags |= PF_EFFICIENCY;

            AccumHistory(*p, cpuDelta);

            if (p->pid != 0)
            {
                g.procCount++;
                g.threadCount += p->threads;
                g.handleCount += p->handles;
            }

            if (!spi->NextEntryOffset) break;
            ptr += spi->NextEntryOffset;
        }
    }

    /* resolve a few extras per tick (icons, users, descriptions) */
    {
        int budget = 8;
        for (int i = 0; i < g.procs.n && budget; i++)
        {
            if (g.procs[i].x && !g.procs[i].x->resolved)
            {
                ResolveExtra(g.procs[i]);
                budget--;
            }
        }
    }

    /* GC extras of dead processes */
    for (int i = s_extras.n - 1; i >= 0; i--)
    {
        if (!s_extras[i]->inUse)
        {
            if (s_extras[i]->icon) DestroyIcon(s_extras[i]->icon);
            HeapFree(GetProcessHeap(), 0, s_extras[i]);
            s_extras.RemoveAt(i);
        }
    }

    /* Release capacity retained after a short-lived process/thread storm,
       with enough hysteresis and headroom to avoid resize oscillation. */
    if (NT_SUCCESS(st) && need && s_procBufSize > 64 * 1024 &&
        need <= s_procBufSize / 2 && need <= MAXULONG - 64 * 1024)
    {
        if (++s_procBufLowTicks >= 30)
        {
            ULONG target = (ULONG)(((ULONGLONG)need + 64 * 1024 +
                                    64 * 1024 - 1) & ~((ULONGLONG)64 * 1024 - 1));
            if (target < s_procBufSize)
            {
                PUCHAR smaller = (PUCHAR)HeapReAlloc(GetProcessHeap(), 0,
                                                     s_procBuf, target);
                if (smaller)
                {
                    s_procBuf = smaller;
                    s_procBufSize = target;
                }
            }

            int rowTarget = g.procs.n + g.procs.n / 2 + 16;
            if (rowTarget < 64) rowTarget = 64;
            if (g.procs.cap > rowTarget * 2) g.procs.Trim(rowTarget);
            if (s_prev.cap > rowTarget * 2) s_prev.Trim(rowTarget);
            s_procBufLowTicks = 0;
        }
    }
    else
    {
        s_procBufLowTicks = 0;
    }

    /* history rings */
    g.hCpu.Push((float)g.cpuTotalPct);
    g.hCpuKernel.Push((float)g.cpuKernelPct);
    g.hMem.Push(g.memTotal ? (float)(100.0 * g.memInUse / g.memTotal) : 0.0f);
    for (int i = 0; i < g.diskCount; i++)
    {
        DiskSnapshot* disk = &g.disks[i];
        disk->hTransfer.Push((float)(disk->readBps + disk->writeBps));
        if (disk->perfValid)
            disk->hActive.Push((float)disk->activePct);
    }
    g.hNetRecv.Push((float)g.netRecvBps);
    g.hNetSend.Push((float)g.netSendBps);

    DWORD tick = GetTickCount();
    if (s_appHistDirty && tick - s_appHistLastSave >= 60000)
    {
        SaveAppHistory();
        s_appHistLastSave = tick;
    }

    s_first = FALSE;
}

ProcRow* FindProc(ULONG pid)
{
    for (int i = 0; i < g.procs.n; i++)
        if (g.procs[i].pid == pid) return &g.procs[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Services                                                           */
/* ------------------------------------------------------------------ */

Vec<SvcRow>& Services(void) { return s_services; }

static const WCHAR* CachedGroup(const WCHAR* name)
{
    for (int i = 0; i < s_svcGroups.n; i++)
        if (lstrcmpiW(s_svcGroups[i].name, name) == 0) return s_svcGroups[i].group;
    return NULL;
}

static void CacheGroup(SC_HANDLE scm, const WCHAR* name)
{
    if (CachedGroup(name)) return;
    SvcGroupCache* c = s_svcGroups.Add();
    if (!c) return;
    StringCchCopyW(c->name, _countof(c->name), name);

    SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_CONFIG);
    if (svc)
    {
        BYTE buf[8192];
        DWORD need = 0;
        if (QueryServiceConfigW(svc, (LPQUERY_SERVICE_CONFIGW)buf, sizeof(buf), &need))
        {
            LPQUERY_SERVICE_CONFIGW cfg = (LPQUERY_SERVICE_CONFIGW)buf;
            if (cfg->lpBinaryPathName)
            {
                /* svchost.exe -k <group> */
                const WCHAR* k = StrStrIW(cfg->lpBinaryPathName, L"-k ");
                if (k && StrStrIW(cfg->lpBinaryPathName, L"svchost"))
                {
                    k += 3;
                    while (*k == L' ') k++;
                    int o = 0;
                    while (k[o] && k[o] != L' ' && k[o] != L'"' && o < 62)
                    {
                        c->group[o] = k[o];
                        o++;
                    }
                    c->group[o] = 0;
                }
            }
        }
        CloseServiceHandle(svc);
    }
}

void RefreshServices(void)
{
    s_services.Clear();
    s_svchostMap.Clear();

    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm) return;

    DWORD cb = 0, count = 0, resume = 0;
    EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          NULL, 0, &cb, &count, &resume, NULL);
    if (cb)
    {
        BYTE* buf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, cb + 4096);
        if (buf)
        {
            resume = 0;
            if (EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32,
                                      SERVICE_STATE_ALL, buf, cb + 4096, &cb, &count,
                                      &resume, NULL))
            {
                LPENUM_SERVICE_STATUS_PROCESSW es = (LPENUM_SERVICE_STATUS_PROCESSW)buf;
                for (DWORD i = 0; i < count; i++)
                {
                    SvcRow* r = s_services.Add();
                    if (!r) break;
                    StringCchCopyW(r->name, _countof(r->name), es[i].lpServiceName);
                    StringCchCopyW(r->disp, _countof(r->disp), es[i].lpDisplayName);
                    r->pid = es[i].ServiceStatusProcess.dwProcessId;
                    r->state = es[i].ServiceStatusProcess.dwCurrentState;

                    CacheGroup(scm, r->name);
                    const WCHAR* grp = CachedGroup(r->name);
                    if (grp)
                        StringCchCopyW(r->group, _countof(r->group), grp);

                    /* svchost service-name grouping */
                    if (r->pid && r->state == SERVICE_RUNNING)
                    {
                        SvcHostEntry* he = NULL;
                        for (int m = 0; m < s_svchostMap.n; m++)
                            if (s_svchostMap[m].pid == r->pid) { he = &s_svchostMap[m]; break; }
                        if (!he)
                        {
                            he = s_svchostMap.Add();
                            if (he)
                            {
                                he->pid = r->pid;
                                if (grp)
                                    StringCchCopyW(he->group, _countof(he->group), grp);
                            }
                        }
                        if (he)
                        {
                            if (he->names[0])
                                StringCchCatW(he->names, _countof(he->names), L", ");
                            StringCchCatW(he->names, _countof(he->names), es[i].lpDisplayName);
                        }
                    }
                }
            }
            HeapFree(GetProcessHeap(), 0, buf);
        }
    }
    CloseServiceHandle(scm);
}

BOOL SvcControl(const WCHAR* name, int op)
{
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return FALSE;
    DWORD access = (op == 1) ? SERVICE_START :
                   (op == 0) ? SERVICE_STOP :
                               (SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS);
    SC_HANDLE svc = OpenServiceW(scm, name, access | SERVICE_QUERY_STATUS);
    BOOL ok = FALSE;
    if (svc)
    {
        SERVICE_STATUS ss;
        if (op == 0 || op == 2)
        {
            ok = ControlService(svc, SERVICE_CONTROL_STOP, &ss);
            if (op == 2)
            {
                /* wait for stop (max ~5s) then start */
                for (int i = 0; i < 25; i++)
                {
                    if (!QueryServiceStatus(svc, &ss)) break;
                    if (ss.dwCurrentState == SERVICE_STOPPED) break;
                    Sleep(200);
                }
                ok = StartServiceW(svc, 0, NULL);
            }
        }
        else if (op == 1)
        {
            ok = StartServiceW(svc, 0, NULL);
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

/* ------------------------------------------------------------------ */
/*  Users                                                              */
/* ------------------------------------------------------------------ */

Vec<UserRow>& Users(void) { return s_users; }

static const WCHAR* WtsStateName(int st)
{
    switch (st)
    {
        case WTSActive: return L"Active";
        case WTSConnected: return L"Connected";
        case WTSDisconnected: return L"Disconnected";
        case WTSIdle: return L"Idle";
        case WTSListen: return L"Listen";
        default: return L"Down";
    }
}

void RefreshUsers(void)
{
    s_users.Clear();

    PWTS_SESSION_INFOW si = NULL;
    DWORD count = 0;
    BOOL wtsOk = WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &si, &count);

    if (wtsOk && si)
    {
        for (DWORD i = 0; i < count; i++)
        {
            if (si[i].State == WTSListen) continue;

            /* pure service sessions are not "users" */
            if (si[i].pWinStationName &&
                lstrcmpiW(si[i].pWinStationName, L"Services") == 0)
                continue;

            WCHAR* user = NULL;
            DWORD cb = 0;
            WCHAR name[96] = L"";
            if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, si[i].SessionId,
                                            WTSUserName, &user, &cb) && user)
            {
                StringCchCopyW(name, _countof(name), user);
                WTSFreeMemory(user);
            }

            UserRow* r = s_users.Add();
            if (!r) break;
            r->sessionId = si[i].SessionId;
            StringCchCopyW(r->user, _countof(r->user), name);
            StringCchCopyW(r->state, _countof(r->state), WtsStateName(si[i].State));
        }
        WTSFreeMemory(si);
    }

    /* wtsapi32 session enumeration is authoritative (implemented for the
       console session on ReactOS); resolve missing user names from the
       session's processes */
    for (int u = 0; u < s_users.n; u++)
    {
        if (s_users[u].user[0]) continue;
        for (int i = 0; i < g.procs.n; i++)
        {
            ProcRow& p = g.procs[i];
            if (p.sessionId == s_users[u].sessionId && p.x && p.x->resolved &&
                p.x->user[0] && (p.flags & PF_HASWINDOW))
            {
                StringCchCopyW(s_users[u].user, _countof(s_users[u].user),
                               p.x->user);
                break;
            }
        }
        if (!s_users[u].user[0])
        {
            DWORD cch2 = _countof(s_users[u].user);
            GetUserNameW(s_users[u].user, &cch2);
        }
    }

    UpdateUserUsage();
}

void UpdateUserUsage(void)
{
    /* Session discovery is relatively expensive; live process totals are not. */
    for (int u = 0; u < s_users.n; u++)
    {
        s_users[u].cpuPct = 0;
        s_users[u].memBytes = 0;
        s_users[u].nProc = 0;
        for (int i = 0; i < g.procs.n; i++)
        {
            if (g.procs[i].pid == 0) continue;
            if (g.procs[i].sessionId == s_users[u].sessionId)
            {
                s_users[u].cpuPct += g.procs[i].cpuPct;
                s_users[u].memBytes += g.procs[i].memBytes;
                s_users[u].nProc++;
            }
        }
    }
}

BOOL UserDisconnect(DWORD session)
{
    return WTSDisconnectSession(WTS_CURRENT_SERVER_HANDLE, session, FALSE);
}

BOOL UserLogoff(DWORD session)
{
    /* not in this SDK's wtsapi32.h; resolve at runtime */
    typedef BOOL (WINAPI *PFN_WTSLogoffSession)(HANDLE, DWORD, BOOL);
    HMODULE wts = GetModuleHandleW(L"wtsapi32.dll");
    if (!wts) wts = LoadLibraryW(L"wtsapi32.dll");
    PFN_WTSLogoffSession pLogoff = wts ?
        (PFN_WTSLogoffSession)GetProcAddress(wts, "WTSLogoffSession") : NULL;
    if (pLogoff)
        return pLogoff(WTS_CURRENT_SERVER_HANDLE, session, FALSE);
    return FALSE;
}

/* ------------------------------------------------------------------ */
/*  Startup items                                                      */
/* ------------------------------------------------------------------ */

Vec<StartupRow>& StartupItems(void) { return s_startup; }

static const WCHAR* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const WCHAR* APPROVED_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
static const WCHAR* APPROVED_FOLDER_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\StartupFolder";

static BOOL ReadApproved(HKEY root, const WCHAR* key, const WCHAR* value, BOOL* enabled)
{
    HKEY hk;
    BOOL found = FALSE;
    if (RegOpenKeyExW(root, key, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS)
    {
        BYTE data[32];
        DWORD cb = sizeof(data), type = 0;
        if (RegQueryValueExW(hk, value, NULL, &type, data, &cb) == ERROR_SUCCESS &&
            type == REG_BINARY && cb >= 1)
        {
            /* byte0: 0x02 = enabled, 0x03 (bit0) = disabled */
            *enabled = !(data[0] & 1);
            found = TRUE;
        }
        RegCloseKey(hk);
    }
    return found;
}

static void ExtractExePath(const WCHAR* cmd, WCHAR* out, int cch)
{
    out[0] = 0;
    WCHAR expanded[512];
    ExpandEnvironmentStringsW(cmd, expanded, _countof(expanded));
    const WCHAR* p = expanded;
    while (*p == L' ') p++;
    if (*p == L'"')
    {
        p++;
        int o = 0;
        while (*p && *p != L'"' && o < cch - 1) out[o++] = *p++;
        out[o] = 0;
    }
    else
    {
        /* up to first space that yields an existing file, else whole string */
        StringCchCopyW(out, cch, p);
        WCHAR* sp = out;
        while ((sp = wcschr(sp, L' ')) != NULL)
        {
            *sp = 0;
            if (GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) return;
            *sp = L' ';
            sp++;
        }
    }
}

static void AddRunEntries(HKEY root, int source)
{
    HKEY hk;
    if (RegOpenKeyExW(root, RUN_KEY, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return;
    for (DWORD i = 0;; i++)
    {
        WCHAR name[128];
        DWORD cn = _countof(name);
        WCHAR data[512];
        DWORD cb = sizeof(data), type;
        LONG rc = RegEnumValueW(hk, i, name, &cn, NULL, &type, (LPBYTE)data, &cb);
        if (rc != ERROR_SUCCESS) break;
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;

        StartupRow* r = s_startup.Add();
        if (!r) break;
        StringCchCopyW(r->name, _countof(r->name), name);
        StringCchCopyW(r->valueName, _countof(r->valueName), name);
        StringCchCopyW(r->command, _countof(r->command), data);
        r->source = source;
        r->enabled = TRUE;
        ReadApproved(root, APPROVED_KEY, name, &r->enabled);

        WCHAR exe[MAX_PATH];
        ExtractExePath(data, exe, _countof(exe));
        if (exe[0])
            GetFileCompany(exe, r->publisher, _countof(r->publisher));
    }
    RegCloseKey(hk);
}

static void AddFolderEntries(int csidl, int source)
{
    WCHAR dir[MAX_PATH];
    if (FAILED(SHGetFolderPathW(NULL, csidl, NULL, SHGFP_TYPE_CURRENT, dir)))
        return;
    WCHAR pattern[MAX_PATH];
    StringCchPrintfW(pattern, _countof(pattern), L"%s\\*", dir);

    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW(pattern, &fd);
    if (hf == INVALID_HANDLE_VALUE) return;
    do
    {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (lstrcmpiW(fd.cFileName, L"desktop.ini") == 0) continue;

        StartupRow* r = s_startup.Add();
        if (!r) break;
        StringCchCopyW(r->name, _countof(r->name), fd.cFileName);
        PathRemoveExtensionW(r->name);
        StringCchCopyW(r->valueName, _countof(r->valueName), fd.cFileName);
        StringCchPrintfW(r->command, _countof(r->command), L"%s\\%s", dir, fd.cFileName);
        r->source = source;
        r->enabled = TRUE;
        HKEY root = (source == SS_FOLDER_USER) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        ReadApproved(root, APPROVED_FOLDER_KEY, fd.cFileName, &r->enabled);

        /* resolve .lnk target for publisher */
        WCHAR target[MAX_PATH] = L"";
        if (StrStrIW(fd.cFileName, L".lnk"))
        {
            IShellLinkW* sl = NULL;
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                           IID_IShellLinkW, (void**)&sl)))
            {
                IPersistFile* pf = NULL;
                if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)))
                {
                    if (SUCCEEDED(pf->Load(r->command, STGM_READ)))
                        sl->GetPath(target, _countof(target), NULL, SLGP_UNCPRIORITY);
                    pf->Release();
                }
                sl->Release();
            }
        }
        else
        {
            StringCchCopyW(target, _countof(target), r->command);
        }
        if (target[0])
            GetFileCompany(target, r->publisher, _countof(r->publisher));
    } while (FindNextFileW(hf, &fd));
    FindClose(hf);
}

void RefreshStartup(void)
{
    s_startup.Clear();
    AddRunEntries(HKEY_CURRENT_USER, SS_HKCU_RUN);
    AddRunEntries(HKEY_LOCAL_MACHINE, SS_HKLM_RUN);
    AddFolderEntries(CSIDL_STARTUP, SS_FOLDER_USER);
    AddFolderEntries(CSIDL_COMMON_STARTUP, SS_FOLDER_COMMON);
}

BOOL SetStartupEnabled(const StartupRow& it, BOOL enable)
{
    HKEY root = (it.source == SS_HKCU_RUN || it.source == SS_FOLDER_USER)
                ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const WCHAR* key = (it.source == SS_HKCU_RUN || it.source == SS_HKLM_RUN)
                       ? APPROVED_KEY : APPROVED_FOLDER_KEY;
    HKEY hk;
    if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE, NULL, &hk, NULL) != ERROR_SUCCESS)
        return FALSE;
    BYTE data[12];
    ZeroMemory(data, sizeof(data));
    data[0] = enable ? 0x02 : 0x03;
    if (!enable)
    {
        /* store disable timestamp like Windows does */
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        CopyMemory(&data[4], &ft, sizeof(ft));
    }
    LONG rc = RegSetValueExW(hk, it.valueName, 0, REG_BINARY, data, sizeof(data));
    RegCloseKey(hk);
    return rc == ERROR_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  App history                                                        */
/* ------------------------------------------------------------------ */

static void SetAppHistorySinceNow(void)
{
    GetSystemTimeAsFileTime(&s_appHistSince);
}

static void ClearAppHistoryRows(void)
{
    for (int i = 0; i < s_appHist.n; i++)
    {
        if (s_appHist[i].icon)
            DestroyIcon(s_appHist[i].icon);
    }
    for (int i = 0; i < s_extras.n; i++)
        s_extras[i]->appHistorySlot = 0;
    s_appHist.Clear();
}

static void DestroyAppHistory(void)
{
    ClearAppHistoryRows();
    s_appHist.Free();
}

static void LoadAppHistoryIcon(AppHistRow* history)
{
    history->iconResolved = TRUE;
    if (!history->path[0])
        return;

    SHFILEINFOW fileInfo;
    ZeroMemory(&fileInfo, sizeof(fileInfo));
    if (SHGetFileInfoW(history->path, 0, &fileInfo, sizeof(fileInfo),
                       SHGFI_ICON | SHGFI_SMALLICON))
    {
        history->icon = fileInfo.hIcon;
    }
}

static void LoadAppHistory(void)
{
    ClearAppHistoryRows();
    SetAppHistorySinceNow();
    s_appHistDirty = FALSE;
    s_appHistLastSave = GetTickCount();

    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_HISTORY_KEY, 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return;

    DWORD version = 0, type = 0, size = sizeof(version);
    if (RegQueryValueExW(key, L"Version", NULL, &type,
                         (LPBYTE)&version, &size) != ERROR_SUCCESS ||
        type != REG_DWORD || version != APP_HISTORY_VERSION)
    {
        RegCloseKey(key);
        return;
    }

    FILETIME since;
    size = sizeof(since);
    if (RegQueryValueExW(key, L"Since", NULL, &type,
                         (LPBYTE)&since, &size) == ERROR_SUCCESS &&
        type == REG_BINARY && size == sizeof(since))
    {
        s_appHistSince = since;
    }

    size = 0;
    if (RegQueryValueExW(key, L"Rows", NULL, &type, NULL, &size) == ERROR_SUCCESS &&
        type == REG_BINARY && size &&
        size <= APP_HISTORY_MAX_ROWS * sizeof(AppHistDiskRow) &&
        size % sizeof(AppHistDiskRow) == 0)
    {
        AppHistDiskRow* rows =
            (AppHistDiskRow*)HeapAlloc(GetProcessHeap(), 0, size);
        if (rows && RegQueryValueExW(key, L"Rows", NULL, &type,
                                     (LPBYTE)rows, &size) == ERROR_SUCCESS)
        {
            DWORD count = size / sizeof(AppHistDiskRow);
            for (DWORD i = 0; i < count; i++)
            {
                rows[i].image[_countof(rows[i].image) - 1] = 0;
                rows[i].displayName[_countof(rows[i].displayName) - 1] = 0;
                rows[i].path[_countof(rows[i].path) - 1] = 0;
                if (!rows[i].image[0])
                    continue;

                AppHistRow* history = s_appHist.Add();
                if (!history)
                    break;
                StringCchCopyW(history->image, _countof(history->image),
                               rows[i].image);
                StringCchCopyW(history->displayName,
                               _countof(history->displayName),
                               rows[i].displayName[0] ? rows[i].displayName
                                                     : rows[i].image);
                StringCchCopyW(history->path, _countof(history->path),
                               rows[i].path);
                history->cpu100ns = rows[i].cpu100ns > 0 ?
                                    rows[i].cpu100ns : 0;
                history->netBytes = rows[i].netBytes;
                history->notificationBytes = rows[i].notificationBytes;
            }
        }
        if (rows)
            HeapFree(GetProcessHeap(), 0, rows);
    }

    RegCloseKey(key);
}

static void SaveAppHistory(void)
{
    if (!s_appHistDirty)
        return;

    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, APP_HISTORY_KEY, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;

    DWORD version = APP_HISTORY_VERSION;
    LONG result = RegSetValueExW(key, L"Version", 0, REG_DWORD,
                                 (const BYTE*)&version, sizeof(version));
    if (result == ERROR_SUCCESS)
    {
        result = RegSetValueExW(key, L"Since", 0, REG_BINARY,
                               (const BYTE*)&s_appHistSince,
                               sizeof(s_appHistSince));
    }

    Vec<AppHistDiskRow> rows;
    for (int i = 0; result == ERROR_SUCCESS && i < s_appHist.n; i++)
    {
        AppHistDiskRow* row = rows.Add();
        if (!row)
        {
            result = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        StringCchCopyW(row->image, _countof(row->image), s_appHist[i].image);
        StringCchCopyW(row->displayName, _countof(row->displayName),
                       s_appHist[i].displayName);
        StringCchCopyW(row->path, _countof(row->path), s_appHist[i].path);
        row->cpu100ns = s_appHist[i].cpu100ns;
        row->netBytes = s_appHist[i].netBytes;
        row->notificationBytes = s_appHist[i].notificationBytes;
    }

    if (result == ERROR_SUCCESS)
    {
        if (rows.n)
        {
            result = RegSetValueExW(key, L"Rows", 0, REG_BINARY,
                                   (const BYTE*)rows.p,
                                   rows.n * sizeof(AppHistDiskRow));
        }
        else
        {
            result = RegDeleteValueW(key, L"Rows");
            if (result == ERROR_FILE_NOT_FOUND)
                result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);
    if (result == ERROR_SUCCESS)
        s_appHistDirty = FALSE;
}

Vec<AppHistRow>& AppHistory(void)
{
    return s_appHist;
}

void ResolveAppHistoryIcons(int budget)
{
    for (int i = 0; i < s_appHist.n && budget > 0; i++)
    {
        if (s_appHist[i].iconResolved)
            continue;
        LoadAppHistoryIcon(&s_appHist[i]);
        budget--;
    }
}

void ClearAppHistory(void)
{
    ClearAppHistoryRows();
    SetAppHistorySinceNow();
    s_appHistDirty = TRUE;
    SaveAppHistory();
    s_appHistLastSave = GetTickCount();
}

void GetAppHistorySince(FILETIME* since)
{
    if (since)
        *since = s_appHistSince;
}

BOOL AppHistoryNetworkAvailable(void)
{
    /* ReactOS does not yet expose historical network bytes by process/app. */
    return FALSE;
}

BOOL AppHistoryNotificationsAvailable(void)
{
    /* There is no notification broker usage-accounting backend yet. */
    return FALSE;
}

BOOL OpenAppHistory(const AppHistRow& app)
{
    if (!app.path[0])
        return FALSE;

    WCHAR directory[MAX_PATH];
    StringCchCopyW(directory, _countof(directory), app.path);
    PathRemoveFileSpecW(directory);
    return (UINT_PTR)ShellExecuteW(NULL, L"open", app.path, NULL,
                                   directory, SW_SHOWNORMAL) > 32;
}

/* ------------------------------------------------------------------ */
/*  Actions                                                            */
/* ------------------------------------------------------------------ */

static BOOL KillWorker(ULONG pid, BOOL tree, int depth)
{
    if (tree && depth < 16)
    {
        /* terminate children first; depth cap guards ppid cycles from pid reuse */
        for (int i = 0; i < g.procs.n; i++)
        {
            ProcRow& c = g.procs[i];
            if (c.ppid == pid && c.pid != pid && c.pid > 4)
                KillWorker(c.pid, TRUE, depth + 1);
        }
    }

    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok;
}

BOOL KillProcess(ULONG pid, BOOL tree)
{
    return KillWorker(pid, tree, 0);
}

BOOL SetEfficiency(ULONG pid, BOOL on)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION,
                           FALSE, pid);
    if (!h)
        h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return FALSE;

    /* EcoQoS if this kernel knows it (NT10 parity); ignore failure */
    TM_POWER_THROTTLING_STATE pts;
    pts.Version = TM_POWER_THROTTLING_VERSION;
    pts.ControlMask = TM_POWER_THROTTLING_EXECUTION_SPEED;
    pts.StateMask = on ? TM_POWER_THROTTLING_EXECUTION_SPEED : 0;
    NtSetInformationProcess(h, TM_ProcessPowerThrottlingState, &pts, sizeof(pts));

    BOOL ok = SetPriorityClass(h, on ? IDLE_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
    CloseHandle(h);

    if (ok)
    {
        ProcRow* p = FindProc(pid);
        if (p)
        {
            if (p->x) p->x->efficiencySet = on;
            if (on) p->flags |= PF_EFFICIENCY;
            else    p->flags &= ~PF_EFFICIENCY;
        }
    }
    return ok;
}

BOOL IsEfficiency(const ProcRow& p)
{
    return (p.flags & PF_EFFICIENCY) != 0;
}

DWORD GetPriClass(ULONG pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    DWORD cls = 0;
    if (h)
    {
        cls = GetPriorityClass(h);
        CloseHandle(h);
    }
    if (!cls)
    {
        /* infer from base priority */
        ProcRow* p = FindProc(pid);
        if (p)
        {
            switch (p->basePri)
            {
                case 4:  cls = IDLE_PRIORITY_CLASS; break;
                case 6:  cls = BELOW_NORMAL_PRIORITY_CLASS; break;
                case 10: cls = ABOVE_NORMAL_PRIORITY_CLASS; break;
                case 13: cls = HIGH_PRIORITY_CLASS; break;
                case 24: cls = REALTIME_PRIORITY_CLASS; break;
                default: cls = NORMAL_PRIORITY_CLASS; break;
            }
        }
    }
    return cls;
}

BOOL SetPriClass(ULONG pid, DWORD cls)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = SetPriorityClass(h, cls);
    CloseHandle(h);
    return ok;
}

BOOL GetAffinity(ULONG pid, DWORD_PTR* mask, DWORD_PTR* sysMask)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = GetProcessAffinityMask(h, mask, sysMask);
    CloseHandle(h);
    return ok;
}

BOOL SetAffinity(ULONG pid, DWORD_PTR mask)
{
    HANDLE h = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    BOOL ok = SetProcessAffinityMask(h, mask);
    CloseHandle(h);
    return ok;
}

BOOL OpenFileLocation(const ProcRow& p)
{
    if (!p.x || !p.x->path[0]) return FALSE;

    WCHAR dir[MAX_PATH];
    StringCchCopyW(dir, _countof(dir), p.x->path);
    PathRemoveFileSpecW(dir);

    /* try select-in-folder via explorer, fallback: open the folder */
    WCHAR params[MAX_PATH + 16];
    StringCchPrintfW(params, _countof(params), L"/select,\"%s\"", p.x->path);
    HINSTANCE hi = ShellExecuteW(NULL, NULL, L"explorer.exe", params, NULL, SW_SHOWNORMAL);
    if ((UINT_PTR)hi > 32) return TRUE;

    return (UINT_PTR)ShellExecuteW(NULL, NULL, dir, NULL, NULL, SW_SHOWNORMAL) > 32;
}

void SearchOnline(const WCHAR* term)
{
    WCHAR url[512];
    StringCchPrintfW(url, _countof(url), L"https://www.bing.com/search?q=%s", term);
    /* crude escaping: spaces only */
    for (WCHAR* c = url; *c; c++)
        if (*c == L' ') *c = L'+';
    ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
}

BOOL ShowFileProperties(const ProcRow& p)
{
    if (!p.x || !p.x->path[0]) return FALSE;
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_INVOKEIDLIST;
    sei.hwnd = g_app.hFrame;
    sei.lpVerb = L"properties";
    sei.lpFile = p.x->path;
    sei.nShow = SW_SHOW;
    return ShellExecuteExW(&sei);
}

BOOL RunTask(const WCHAR* cmd, BOOL admin)
{
    WCHAR expanded[1024];
    ExpandEnvironmentStringsW(cmd, expanded, _countof(expanded));

    /* split first token (quoted or not) + args */
    WCHAR file[MAX_PATH];
    const WCHAR* args = NULL;
    const WCHAR* p = expanded;
    while (*p == L' ') p++;
    if (*p == L'"')
    {
        p++;
        int o = 0;
        while (*p && *p != L'"' && o < MAX_PATH - 1) file[o++] = *p++;
        file[o] = 0;
        if (*p == L'"') p++;
    }
    else
    {
        int o = 0;
        while (*p && *p != L' ' && o < MAX_PATH - 1) file[o++] = *p++;
        file[o] = 0;
    }
    while (*p == L' ') p++;
    if (*p) args = p;

    HINSTANCE hi = ShellExecuteW(NULL, admin ? L"runas" : NULL,
                                 file, args, NULL, SW_SHOWNORMAL);
    if ((UINT_PTR)hi > 32) return TRUE;

    /* last resort: raw CreateProcess on the whole line */
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    WCHAR line[1024];
    StringCchCopyW(line, _countof(line), expanded);
    if (CreateProcessW(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return TRUE;
    }
    return FALSE;
}

} /* namespace Data */
