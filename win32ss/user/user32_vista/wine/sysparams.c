
#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <wingdi.h>
#include <winuser.h>
#include <winbase.h>
#include <d3dkmthk.h>
#include "wine/debug.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
#endif

WINE_DEFAULT_DEBUG_CHANNEL(win);

/* Only define DISPLAYCONFIG types locally when system headers do not provide them. */
#if (WINVER < 0x601)
typedef enum DISPLAYCONFIG_TOPOLOGY_ID
{
    DISPLAYCONFIG_TOPOLOGY_INTERNAL       = 0x00000001,
    DISPLAYCONFIG_TOPOLOGY_CLONE          = 0x00000002,
    DISPLAYCONFIG_TOPOLOGY_EXTEND         = 0x00000004,
    DISPLAYCONFIG_TOPOLOGY_EXTERNAL       = 0x00000008,
    DISPLAYCONFIG_TOPOLOGY_FORCE_UINT32   = 0xFFFFFFFF
} DISPLAYCONFIG_TOPOLOGY_ID;

typedef enum
{
    DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME                 = 1,
    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME                 = 2,
    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE       = 3,
    DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME                = 4,
    DISPLAYCONFIG_DEVICE_INFO_SET_TARGET_PERSISTENCE          = 5,
    DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE            = 6,
    DISPLAYCONFIG_DEVICE_INFO_GET_SUPPORT_VIRTUAL_RESOLUTION  = 7,
    DISPLAYCONFIG_DEVICE_INFO_SET_SUPPORT_VIRTUAL_RESOLUTION  = 8,
    DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO         = 9,
    DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE        = 10,
    DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL             = 11,
    DISPLAYCONFIG_DEVICE_INFO_GET_MONITOR_SPECIALIZATION      = 12,
    DISPLAYCONFIG_DEVICE_INFO_SET_MONITOR_SPECIALIZATION      = 13,
    DISPLAYCONFIG_DEVICE_INFO_SET_RESERVED1                   = 14,
    DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO_2       = 15,
    DISPLAYCONFIG_DEVICE_INFO_SET_HDR_STATE                   = 16,
    DISPLAYCONFIG_DEVICE_INFO_SET_WCG_STATE                   = 17,
    DISPLAYCONFIG_DEVICE_INFO_FORCE_UINT32                    = 0xFFFFFFFF
} DISPLAYCONFIG_DEVICE_INFO_TYPE;

typedef struct DISPLAYCONFIG_DEVICE_INFO_HEADER
{
    DISPLAYCONFIG_DEVICE_INFO_TYPE  type;
    UINT32                          size;
    LUID                            adapterId;
    UINT32                          id;
} DISPLAYCONFIG_DEVICE_INFO_HEADER;

typedef struct DISPLAYCONFIG_DESKTOP_IMAGE_INFO {
    POINTL PathSourceSize;
    RECTL  DesktopImageRegion;
    RECTL  DesktopImageClip;
} DISPLAYCONFIG_DESKTOP_IMAGE_INFO;

typedef enum {
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER = -1,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15 = 0,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO = 1,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO = 2,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPONENT_VIDEO = 3,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI = 4,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI = 5,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS = 6,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_D_JPN = 8,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDI = 9,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL = 10,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED = 11,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL = 12,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED = 13,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDTVDONGLE = 14,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST = 15,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED = 16,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL = 17,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_USB_TUNNEL,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL = 0x80000000,
    DISPLAYCONFIG_OUTPUT_TECHNOLOGY_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY;

typedef enum {
    DISPLAYCONFIG_ROTATION_IDENTITY = 1,
    DISPLAYCONFIG_ROTATION_ROTATE90 = 2,
    DISPLAYCONFIG_ROTATION_ROTATE180 = 3,
    DISPLAYCONFIG_ROTATION_ROTATE270 = 4,
    DISPLAYCONFIG_ROTATION_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_ROTATION;

typedef enum {
    DISPLAYCONFIG_SCALING_IDENTITY = 1,
    DISPLAYCONFIG_SCALING_CENTERED = 2,
    DISPLAYCONFIG_SCALING_STRETCHED = 3,
    DISPLAYCONFIG_SCALING_ASPECTRATIOCENTEREDMAX = 4,
    DISPLAYCONFIG_SCALING_CUSTOM = 5,
    DISPLAYCONFIG_SCALING_PREFERRED = 128,
    DISPLAYCONFIG_SCALING_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_SCALING;

typedef struct DISPLAYCONFIG_RATIONAL {
    UINT32 Numerator;
    UINT32 Denominator;
} DISPLAYCONFIG_RATIONAL;

typedef enum {
    DISPLAYCONFIG_SCANLINE_ORDERING_UNSPECIFIED = 0,
    DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE = 1,
    DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED = 2,
    DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_UPPERFIELDFIRST,
    DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED_LOWERFIELDFIRST = 3,
    DISPLAYCONFIG_SCANLINE_ORDERING_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_SCANLINE_ORDERING;

typedef struct DISPLAYCONFIG_PATH_TARGET_INFO {
    LUID                                  adapterId;
    UINT32                                id;
    union {
        UINT32 modeInfoIdx;
        struct {
            UINT32 desktopModeInfoIdx : 16;
            UINT32 targetModeInfoIdx : 16;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY outputTechnology;
    DISPLAYCONFIG_ROTATION                rotation;
    DISPLAYCONFIG_SCALING                 scaling;
    DISPLAYCONFIG_RATIONAL                refreshRate;
    DISPLAYCONFIG_SCANLINE_ORDERING       scanLineOrdering;
    BOOL                                  targetAvailable;
    UINT32                                statusFlags;
} DISPLAYCONFIG_PATH_TARGET_INFO;

typedef struct DISPLAYCONFIG_PATH_SOURCE_INFO {
    LUID   adapterId;
    UINT32 id;
    union {
        UINT32 modeInfoIdx;
        struct {
          UINT32 cloneGroupId : 16;
          UINT32 sourceModeInfoIdx : 16;
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
  UINT32 statusFlags;
} DISPLAYCONFIG_PATH_SOURCE_INFO;

typedef struct DISPLAYCONFIG_PATH_INFO {
    DISPLAYCONFIG_PATH_SOURCE_INFO sourceInfo;
    DISPLAYCONFIG_PATH_TARGET_INFO targetInfo;
    UINT32                         flags;
} DISPLAYCONFIG_PATH_INFO;

typedef enum {
    DISPLAYCONFIG_PIXELFORMAT_8BPP = 1,
    DISPLAYCONFIG_PIXELFORMAT_16BPP = 2,
    DISPLAYCONFIG_PIXELFORMAT_24BPP = 3,
    DISPLAYCONFIG_PIXELFORMAT_32BPP = 4,
    DISPLAYCONFIG_PIXELFORMAT_NONGDI = 5,
    DISPLAYCONFIG_PIXELFORMAT_FORCE_UINT32 = 0xffffffff
} DISPLAYCONFIG_PIXELFORMAT;

typedef struct DISPLAYCONFIG_SOURCE_MODE
{
    UINT32                      width;
    UINT32                      height;
    DISPLAYCONFIG_PIXELFORMAT   pixelFormat;
    POINTL                      position;
} DISPLAYCONFIG_SOURCE_MODE;
typedef struct DISPLAYCONFIG_2DREGION {
    UINT32 cx;
    UINT32 cy;
} DISPLAYCONFIG_2DREGION;

typedef struct DISPLAYCONFIG_VIDEO_SIGNAL_INFO {
    UINT64                          pixelRate;
    DISPLAYCONFIG_RATIONAL          hSyncFreq;
    DISPLAYCONFIG_RATIONAL          vSyncFreq;
    DISPLAYCONFIG_2DREGION          activeSize;
    DISPLAYCONFIG_2DREGION          totalSize;
    union {
        struct {
            UINT32 videoStandard : 16;
            UINT32 vSyncFreqDivider : 6;
            UINT32 reserved : 10;
        } AdditionalSignalInfo;
        UINT32 videoStandard;
    } DUMMYUNIONNAME;
  DISPLAYCONFIG_SCANLINE_ORDERING scanLineOrdering;
} DISPLAYCONFIG_VIDEO_SIGNAL_INFO;
typedef struct DISPLAYCONFIG_TARGET_MODE
{
    DISPLAYCONFIG_VIDEO_SIGNAL_INFO   targetVideoSignalInfo;
} DISPLAYCONFIG_TARGET_MODE;
typedef enum {
    DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE = 1,
    DISPLAYCONFIG_MODE_INFO_TYPE_TARGET = 2,
    DISPLAYCONFIG_MODE_INFO_TYPE_DESKTOP_IMAGE = 3,
    DISPLAYCONFIG_MODE_INFO_TYPE_FORCE_UINT32 = 0xFFFFFFFF
} DISPLAYCONFIG_MODE_INFO_TYPE;
typedef struct DISPLAYCONFIG_MODE_INFO {
    DISPLAYCONFIG_MODE_INFO_TYPE infoType;
    UINT32                       id;
    LUID                         adapterId;
    union {
        DISPLAYCONFIG_TARGET_MODE        targetMode;
        DISPLAYCONFIG_SOURCE_MODE        sourceMode;
        DISPLAYCONFIG_DESKTOP_IMAGE_INFO desktopImageInfo;
    } DUMMYUNIONNAME;
} DISPLAYCONFIG_MODE_INFO;
#endif /* (WINVER < 0x601) */


/*
 * Display configuration.
 *
 * Every attached GDI display is one active path.  The WDDM adapter LUID and
 * VidPn source of a display come from D3DKMTOpenAdapterFromGdiDisplayName,
 * which is also what a user-mode driver uses when it later asks
 * DisplayConfigGetDeviceInfo about the same (adapterId, id) pair.  One target
 * per source is assumed and the target takes the source ordinal; clone and
 * multi-target topologies are not described.
 */

#define DISPLAYCONFIG_MAX_PATHS 16

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#endif

typedef struct _DISPLAYCONFIG_LOCAL_PATH
{
    WCHAR GdiDeviceName[CCHDEVICENAME];
    WCHAR AdapterDeviceId[128];
    LUID AdapterId;
    UINT32 SourceId;
    DEVMODEW Mode;
} DISPLAYCONFIG_LOCAL_PATH;

static BOOL
DisplayConfigResolveAdapter(
    const WCHAR *GdiDeviceName,
    LUID *AdapterId,
    UINT32 *SourceId)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME Open;
    D3DKMT_CLOSEADAPTER Close;

    memset(&Open, 0, sizeof(Open));
    lstrcpynW(Open.DeviceName, GdiDeviceName, ARRAY_SIZE(Open.DeviceName));
    if (!NT_SUCCESS(D3DKMTOpenAdapterFromGdiDisplayName(&Open)))
        return FALSE;
    *AdapterId = Open.AdapterLuid;
    *SourceId = Open.VidPnSourceId;
    Close.hAdapter = Open.hAdapter;
    D3DKMTCloseAdapter(&Close);
    return TRUE;
}

/* Collect the attached displays; returns the count or -1 when the WDDM
 * adapter of a display cannot be resolved. */
static int
DisplayConfigCollectPaths(
    DISPLAYCONFIG_LOCAL_PATH *Paths,
    UINT32 Capacity)
{
    DISPLAY_DEVICEW Device;
    DWORD Index;
    UINT32 Count = 0;

    for (Index = 0; Count < Capacity; Index++)
    {
        DISPLAYCONFIG_LOCAL_PATH *Path = &Paths[Count];

        memset(&Device, 0, sizeof(Device));
        Device.cb = sizeof(Device);
        if (!EnumDisplayDevicesW(NULL, Index, &Device, 0))
            break;
        if (!(Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ||
            (Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
            continue;

        memset(Path, 0, sizeof(*Path));
        lstrcpynW(Path->GdiDeviceName, Device.DeviceName, ARRAY_SIZE(Path->GdiDeviceName));
        lstrcpynW(Path->AdapterDeviceId, Device.DeviceID, ARRAY_SIZE(Path->AdapterDeviceId));
        Path->Mode.dmSize = sizeof(Path->Mode);
        if (!EnumDisplaySettingsW(Device.DeviceName, ENUM_CURRENT_SETTINGS, &Path->Mode))
            continue;
        if (!DisplayConfigResolveAdapter(Device.DeviceName, &Path->AdapterId, &Path->SourceId))
            return -1;
        Count++;
    }
    return (int)Count;
}

static BOOL
DisplayConfigValidQueryFlags(
    UINT32 flags)
{
    UINT32 scope = flags & (QDC_ALL_PATHS | QDC_ONLY_ACTIVE_PATHS | QDC_DATABASE_CURRENT);

    if (scope != QDC_ALL_PATHS && scope != QDC_ONLY_ACTIVE_PATHS && scope != QDC_DATABASE_CURRENT)
        return FALSE;
    return (flags & ~(scope | QDC_VIRTUAL_MODE_AWARE | QDC_INCLUDE_HMD | QDC_VIRTUAL_REFRESH_RATE_AWARE)) == 0;
}

static DISPLAYCONFIG_PIXELFORMAT
DisplayConfigPixelFormat(
    DWORD BitsPerPel)
{
    switch (BitsPerPel)
    {
        case 8:  return DISPLAYCONFIG_PIXELFORMAT_8BPP;
        case 16: return DISPLAYCONFIG_PIXELFORMAT_16BPP;
        case 24: return DISPLAYCONFIG_PIXELFORMAT_24BPP;
        case 32: return DISPLAYCONFIG_PIXELFORMAT_32BPP;
        default: return DISPLAYCONFIG_PIXELFORMAT_NONGDI;
    }
}

static void
DisplayConfigFillSignal(
    const DEVMODEW *Mode,
    DISPLAYCONFIG_VIDEO_SIGNAL_INFO *Signal)
{
    UINT32 Refresh = Mode->dmDisplayFrequency > 1 ? Mode->dmDisplayFrequency : 60;

    memset(Signal, 0, sizeof(*Signal));
    Signal->activeSize.cx = Mode->dmPelsWidth;
    Signal->activeSize.cy = Mode->dmPelsHeight;
    Signal->totalSize = Signal->activeSize;
    Signal->pixelRate = (UINT64)Mode->dmPelsWidth * Mode->dmPelsHeight * Refresh;
    Signal->hSyncFreq.Numerator = Mode->dmPelsHeight * Refresh;
    Signal->hSyncFreq.Denominator = 1;
    Signal->vSyncFreq.Numerator = Refresh;
    Signal->vSyncFreq.Denominator = 1;
    Signal->videoStandard = 0; /* D3DKMDT_VSS_OTHER */
    Signal->scanLineOrdering = (Mode->dmDisplayFlags & DM_INTERLACED) ?
        DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED : DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
}

LONG
WINAPI
GetDisplayConfigBufferSizes(
    UINT32 flags,
    UINT32 *numPathArrayElements,
    UINT32 *numModeInfoArrayElements)
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    int Count;

    if (numPathArrayElements == NULL || numModeInfoArrayElements == NULL)
        return ERROR_INVALID_PARAMETER;
    if (!DisplayConfigValidQueryFlags(flags))
        return ERROR_INVALID_PARAMETER;

    Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
    if (Count < 0)
        return ERROR_GEN_FAILURE;
    *numPathArrayElements = Count;
    *numModeInfoArrayElements = Count * 2;
    return ERROR_SUCCESS;
}

LONG
WINAPI
QueryDisplayConfig(
    UINT32                    flags,
    UINT32                    *numPathArrayElements,
    DISPLAYCONFIG_PATH_INFO   *pathArray,
    UINT32                    *numModeInfoArrayElements,
    DISPLAYCONFIG_MODE_INFO   *modeInfoArray,
    DISPLAYCONFIG_TOPOLOGY_ID *currentTopologyId)
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    int Count;
    int Index;

    if (numPathArrayElements == NULL || pathArray == NULL ||
        numModeInfoArrayElements == NULL || modeInfoArray == NULL)
        return ERROR_INVALID_PARAMETER;
    if (!DisplayConfigValidQueryFlags(flags))
        return ERROR_INVALID_PARAMETER;
    /* The topology id is defined only for the database query. */
    if ((flags & QDC_DATABASE_CURRENT) ? currentTopologyId == NULL : currentTopologyId != NULL)
        return ERROR_INVALID_PARAMETER;

    Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
    if (Count < 0)
        return ERROR_GEN_FAILURE;
    if (*numPathArrayElements < (UINT32)Count || *numModeInfoArrayElements < (UINT32)Count * 2)
        return ERROR_INSUFFICIENT_BUFFER;

    for (Index = 0; Index < Count; Index++)
    {
        const DISPLAYCONFIG_LOCAL_PATH *Local = &Paths[Index];
        DISPLAYCONFIG_PATH_INFO *Path = &pathArray[Index];
        DISPLAYCONFIG_MODE_INFO *SourceMode = &modeInfoArray[Index * 2];
        DISPLAYCONFIG_MODE_INFO *TargetMode = &modeInfoArray[Index * 2 + 1];
        UINT32 Refresh = Local->Mode.dmDisplayFrequency > 1 ? Local->Mode.dmDisplayFrequency : 60;

        memset(Path, 0, sizeof(*Path));
        Path->sourceInfo.adapterId = Local->AdapterId;
        Path->sourceInfo.id = Local->SourceId;
        Path->sourceInfo.modeInfoIdx = Index * 2;
        Path->sourceInfo.statusFlags = DISPLAYCONFIG_SOURCE_IN_USE;
        Path->targetInfo.adapterId = Local->AdapterId;
        Path->targetInfo.id = Local->SourceId;
        Path->targetInfo.modeInfoIdx = Index * 2 + 1;
        Path->targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
        Path->targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
        Path->targetInfo.scaling = DISPLAYCONFIG_SCALING_IDENTITY;
        Path->targetInfo.refreshRate.Numerator = Refresh;
        Path->targetInfo.refreshRate.Denominator = 1;
        Path->targetInfo.scanLineOrdering = (Local->Mode.dmDisplayFlags & DM_INTERLACED) ?
            DISPLAYCONFIG_SCANLINE_ORDERING_INTERLACED : DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        Path->targetInfo.targetAvailable = TRUE;
        Path->targetInfo.statusFlags = DISPLAYCONFIG_TARGET_IN_USE;
        Path->flags = DISPLAYCONFIG_PATH_ACTIVE;

        memset(SourceMode, 0, sizeof(*SourceMode));
        SourceMode->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        SourceMode->id = Local->SourceId;
        SourceMode->adapterId = Local->AdapterId;
        SourceMode->sourceMode.width = Local->Mode.dmPelsWidth;
        SourceMode->sourceMode.height = Local->Mode.dmPelsHeight;
        SourceMode->sourceMode.pixelFormat = DisplayConfigPixelFormat(Local->Mode.dmBitsPerPel);
        SourceMode->sourceMode.position.x = Local->Mode.dmPosition.x;
        SourceMode->sourceMode.position.y = Local->Mode.dmPosition.y;

        memset(TargetMode, 0, sizeof(*TargetMode));
        TargetMode->infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        TargetMode->id = Local->SourceId;
        TargetMode->adapterId = Local->AdapterId;
        DisplayConfigFillSignal(&Local->Mode, &TargetMode->targetMode.targetVideoSignalInfo);
    }

    *numPathArrayElements = Count;
    *numModeInfoArrayElements = Count * 2;
    if (currentTopologyId != NULL)
        *currentTopologyId = Count > 1 ? DISPLAYCONFIG_TOPOLOGY_EXTEND : DISPLAYCONFIG_TOPOLOGY_INTERNAL;
    return ERROR_SUCCESS;
}

typedef enum ORIENTATION_PREFERENCE {
    ORIENTATION_PREFERENCE_NONE              = 0x0,
    ORIENTATION_PREFERENCE_LANDSCAPE         = 0x1,
    ORIENTATION_PREFERENCE_PORTRAIT          = 0x2,
    ORIENTATION_PREFERENCE_LANDSCAPE_FLIPPED = 0x4,
    ORIENTATION_PREFERENCE_PORTRAIT_FLIPPED  = 0x8
} ORIENTATION_PREFERENCE;

/***********************************************************************
 *              DisplayConfigGetDeviceInfo (USER32.@)
 */
LONG WINAPI DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER *packet)
{
    DISPLAYCONFIG_LOCAL_PATH Paths[DISPLAYCONFIG_MAX_PATHS];
    const DISPLAYCONFIG_LOCAL_PATH *Local = NULL;
    UINT32 ExpectedSize;
    int Count;
    int Index;

    if (packet == NULL || packet->size < sizeof(*packet))
        return ERROR_INVALID_PARAMETER;

    switch (packet->type)
    {
        case DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME:
            ExpectedSize = sizeof(DISPLAYCONFIG_SOURCE_DEVICE_NAME);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME:
            ExpectedSize = sizeof(DISPLAYCONFIG_TARGET_DEVICE_NAME);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE:
            ExpectedSize = sizeof(DISPLAYCONFIG_TARGET_PREFERRED_MODE);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME:
            ExpectedSize = sizeof(DISPLAYCONFIG_ADAPTER_NAME);
            break;
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE:
            ExpectedSize = sizeof(DISPLAYCONFIG_TARGET_BASE_TYPE);
            break;
        default:
            FIXME("DisplayConfigGetDeviceInfo: type %d not supported\n", packet->type);
            return ERROR_INVALID_PARAMETER;
    }
    if (packet->size != ExpectedSize)
        return ERROR_INVALID_PARAMETER;

    Count = DisplayConfigCollectPaths(Paths, ARRAY_SIZE(Paths));
    if (Count < 0)
        return ERROR_GEN_FAILURE;
    for (Index = 0; Index < Count; Index++)
    {
        if (Paths[Index].AdapterId.LowPart == packet->adapterId.LowPart &&
            Paths[Index].AdapterId.HighPart == packet->adapterId.HighPart &&
            (packet->type == DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME ||
             Paths[Index].SourceId == packet->id))
        {
            Local = &Paths[Index];
            break;
        }
    }
    if (Local == NULL)
        return ERROR_GEN_FAILURE;

    switch (packet->type)
    {
        case DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME:
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME *Name = (DISPLAYCONFIG_SOURCE_DEVICE_NAME *)packet;

            lstrcpynW(Name->viewGdiDeviceName, Local->GdiDeviceName, ARRAY_SIZE(Name->viewGdiDeviceName));
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME:
        {
            DISPLAYCONFIG_TARGET_DEVICE_NAME *Name = (DISPLAYCONFIG_TARGET_DEVICE_NAME *)packet;
            DISPLAY_DEVICEW Monitor;

            memset(&Name->flags, 0, (char *)(Name + 1) - (char *)&Name->flags);
            Name->outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
            memset(&Monitor, 0, sizeof(Monitor));
            Monitor.cb = sizeof(Monitor);
            if (EnumDisplayDevicesW(Local->GdiDeviceName, 0, &Monitor, 0))
            {
                lstrcpynW(Name->monitorFriendlyDeviceName, Monitor.DeviceString, ARRAY_SIZE(Name->monitorFriendlyDeviceName));
                lstrcpynW(Name->monitorDevicePath, Monitor.DeviceID, ARRAY_SIZE(Name->monitorDevicePath));
            }
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_PREFERRED_MODE:
        {
            DISPLAYCONFIG_TARGET_PREFERRED_MODE *Preferred = (DISPLAYCONFIG_TARGET_PREFERRED_MODE *)packet;

            Preferred->width = Local->Mode.dmPelsWidth;
            Preferred->height = Local->Mode.dmPelsHeight;
            DisplayConfigFillSignal(&Local->Mode, &Preferred->targetMode.targetVideoSignalInfo);
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME:
        {
            DISPLAYCONFIG_ADAPTER_NAME *Name = (DISPLAYCONFIG_ADAPTER_NAME *)packet;

            lstrcpynW(Name->adapterDevicePath, Local->AdapterDeviceId, ARRAY_SIZE(Name->adapterDevicePath));
            return ERROR_SUCCESS;
        }
        case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE:
        {
            DISPLAYCONFIG_TARGET_BASE_TYPE *Base = (DISPLAYCONFIG_TARGET_BASE_TYPE *)packet;

            Base->baseOutputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
            return ERROR_SUCCESS;
        }
        default:
            return ERROR_INVALID_PARAMETER;
    }
}

/***********************************************************************
 *              DisplayConfigSetDeviceInfo (USER32.@)
 */
LONG WINAPI DisplayConfigSetDeviceInfo( DISPLAYCONFIG_DEVICE_INFO_HEADER *packet )
{
    FIXME( "DisplayConfigSetDeviceInfo: stub!\n" );
    return 1;
}

/**********************************************************************
 *              GetDisplayAutoRotationPreferences (USER32.@)
 */
BOOL WINAPI GetDisplayAutoRotationPreferences( ORIENTATION_PREFERENCE *orientation )
{
    FIXME("(%p): stub\n", orientation);
    *orientation = ORIENTATION_PREFERENCE_NONE;
    return TRUE;
}

/***********************************************************************
 *              SetDisplayConfig (USER32.@)
 */
LONG WINAPI SetDisplayConfig(UINT32 path_info_count, DISPLAYCONFIG_PATH_INFO *path_info, UINT32 mode_info_count,
        DISPLAYCONFIG_MODE_INFO *mode_info, UINT32 flags)
{
    FIXME("path_info_count %u, path_info %p, mode_info_count %u, mode_info %p, flags %#x stub.\n",
            path_info_count, path_info, mode_info_count, mode_info, flags);

    return ERROR_SUCCESS;
}

/**********************************************************************
 *              SetDisplayAutoRotationPreferences (USER32.@)
 */
BOOL WINAPI SetDisplayAutoRotationPreferences( ORIENTATION_PREFERENCE orientation )
{
    FIXME("(%d): stub\n", orientation);
    return TRUE;
}
