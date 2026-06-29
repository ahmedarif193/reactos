
#define WIN32_NO_STATUS
#define _INC_WINDOWS
#define COM_NO_WINDOWS_H
#include <windef.h>
#include <wingdi.h>
#include <winuser.h>
#include <winbase.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(win);

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
    UINT32                                modeInfoIdx;
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
    UINT32 modeInfoIdx;
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
    };
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
    };
} DISPLAYCONFIG_MODE_INFO;


/* QDC_* flags for QueryDisplayConfig */
#define QDC_ALL_PATHS                   0x00000001
#define QDC_ONLY_ACTIVE_PATHS           0x00000002
#define QDC_DATABASE_CURRENT            0x00000004

/* DISPLAYCONFIG_PATH_INFO flags */
#define DISPLAYCONFIG_PATH_ACTIVE       0x00000001

/* DISPLAYCONFIG_PATH_TARGET_INFO statusFlags */
#define DISPLAYCONFIG_TARGET_IN_USE     0x00000001

/* DISPLAYCONFIG_PATH_SOURCE_INFO statusFlags */
#define DISPLAYCONFIG_SOURCE_IN_USE     0x00000001

/*
 * Count active display devices by enumerating with EnumDisplayDevicesW.
 * Returns the number of attached-to-desktop displays.
 */
static UINT32 CountActiveDisplayDevices(void)
{
    DISPLAY_DEVICEW dd;
    UINT32 count = 0;
    DWORD iDevNum;

    dd.cb = sizeof(dd);
    for (iDevNum = 0; EnumDisplayDevicesW(NULL, iDevNum, &dd, 0); iDevNum++)
    {
        if (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
            count++;
    }

    return count ? count : 1; /* at least 1 */
}

LONG
WINAPI
GetDisplayConfigBufferSizes(
    UINT32 flags,
    UINT32 *numPathArrayElements,
    UINT32 *numModeInfoArrayElements)
{
    UINT32 cActive;

    if (!numPathArrayElements || !numModeInfoArrayElements)
        return ERROR_INVALID_PARAMETER;

    if (!(flags & (QDC_ALL_PATHS | QDC_ONLY_ACTIVE_PATHS | QDC_DATABASE_CURRENT)))
        return ERROR_INVALID_PARAMETER;

    cActive = CountActiveDisplayDevices();

    /*
     * Each active path needs:
     *   - 1 DISPLAYCONFIG_PATH_INFO entry
     *   - 1 source mode + 1 target mode = 2 DISPLAYCONFIG_MODE_INFO entries
     */
    *numPathArrayElements = cActive;
    *numModeInfoArrayElements = cActive * 2;

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
    DISPLAY_DEVICEW dd;
    DEVMODEW dm;
    DWORD iDevNum;
    UINT32 pathIdx = 0, modeIdx = 0;
    UINT32 maxPaths, maxModes;
    LUID adapterLuid;

    if (!numPathArrayElements || !pathArray ||
        !numModeInfoArrayElements || !modeInfoArray)
        return ERROR_INVALID_PARAMETER;

    if (!(flags & (QDC_ALL_PATHS | QDC_ONLY_ACTIVE_PATHS | QDC_DATABASE_CURRENT)))
        return ERROR_INVALID_PARAMETER;

    maxPaths = *numPathArrayElements;
    maxModes = *numModeInfoArrayElements;

    /* Use a synthetic adapter LUID (session-unique identifier) */
    adapterLuid.LowPart = 0x10001;
    adapterLuid.HighPart = 0;

    memset(pathArray, 0, maxPaths * sizeof(DISPLAYCONFIG_PATH_INFO));
    memset(modeInfoArray, 0, maxModes * sizeof(DISPLAYCONFIG_MODE_INFO));

    dd.cb = sizeof(dd);
    for (iDevNum = 0; EnumDisplayDevicesW(NULL, iDevNum, &dd, 0); iDevNum++)
    {
        BOOL isActive = (dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;

        if ((flags & QDC_ONLY_ACTIVE_PATHS) && !isActive)
            continue;

        if (pathIdx >= maxPaths || (modeIdx + 1) >= maxModes)
        {
            *numPathArrayElements = pathIdx;
            *numModeInfoArrayElements = modeIdx;
            return ERROR_INSUFFICIENT_BUFFER;
        }

        /* Get current display settings for this device */
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
        {
            /* Fall back to registry settings */
            if (!EnumDisplaySettingsW(dd.DeviceName, ENUM_REGISTRY_SETTINGS, &dm))
                continue;
        }

        /* Fill source mode info */
        modeInfoArray[modeIdx].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
        modeInfoArray[modeIdx].id = iDevNum;
        modeInfoArray[modeIdx].adapterId = adapterLuid;
        modeInfoArray[modeIdx].sourceMode.width = dm.dmPelsWidth;
        modeInfoArray[modeIdx].sourceMode.height = dm.dmPelsHeight;
        modeInfoArray[modeIdx].sourceMode.pixelFormat =
            (dm.dmBitsPerPel == 32) ? DISPLAYCONFIG_PIXELFORMAT_32BPP :
            (dm.dmBitsPerPel == 24) ? DISPLAYCONFIG_PIXELFORMAT_24BPP :
            (dm.dmBitsPerPel == 16) ? DISPLAYCONFIG_PIXELFORMAT_16BPP :
            (dm.dmBitsPerPel == 8)  ? DISPLAYCONFIG_PIXELFORMAT_8BPP :
            DISPLAYCONFIG_PIXELFORMAT_32BPP;
        modeInfoArray[modeIdx].sourceMode.position.x = dm.dmPosition.x;
        modeInfoArray[modeIdx].sourceMode.position.y = dm.dmPosition.y;

        /* Fill target mode info */
        modeInfoArray[modeIdx + 1].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
        modeInfoArray[modeIdx + 1].id = iDevNum;
        modeInfoArray[modeIdx + 1].adapterId = adapterLuid;
        modeInfoArray[modeIdx + 1].targetMode.targetVideoSignalInfo.activeSize.cx = dm.dmPelsWidth;
        modeInfoArray[modeIdx + 1].targetMode.targetVideoSignalInfo.activeSize.cy = dm.dmPelsHeight;
        modeInfoArray[modeIdx + 1].targetMode.targetVideoSignalInfo.totalSize.cx = dm.dmPelsWidth;
        modeInfoArray[modeIdx + 1].targetMode.targetVideoSignalInfo.totalSize.cy = dm.dmPelsHeight;
        if (dm.dmDisplayFrequency > 0)
        {
            modeInfoArray[modeIdx + 1].targetMode.targetVideoSignalInfo.vSyncFreq.Numerator = dm.dmDisplayFrequency;
            modeInfoArray[modeIdx + 1].targetMode.targetVideoSignalInfo.vSyncFreq.Denominator = 1;
        }
        modeInfoArray[modeIdx + 1].targetMode.targetVideoSignalInfo.scanLineOrdering =
            DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;

        /* Fill path info */
        pathArray[pathIdx].sourceInfo.adapterId = adapterLuid;
        pathArray[pathIdx].sourceInfo.id = iDevNum;
        pathArray[pathIdx].sourceInfo.modeInfoIdx = modeIdx;
        pathArray[pathIdx].sourceInfo.statusFlags = isActive ? DISPLAYCONFIG_SOURCE_IN_USE : 0;

        pathArray[pathIdx].targetInfo.adapterId = adapterLuid;
        pathArray[pathIdx].targetInfo.id = iDevNum;
        pathArray[pathIdx].targetInfo.modeInfoIdx = modeIdx + 1;
        pathArray[pathIdx].targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
        pathArray[pathIdx].targetInfo.rotation = DISPLAYCONFIG_ROTATION_IDENTITY;
        pathArray[pathIdx].targetInfo.scaling = DISPLAYCONFIG_SCALING_IDENTITY;
        if (dm.dmDisplayFrequency > 0)
        {
            pathArray[pathIdx].targetInfo.refreshRate.Numerator = dm.dmDisplayFrequency;
            pathArray[pathIdx].targetInfo.refreshRate.Denominator = 1;
        }
        pathArray[pathIdx].targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        pathArray[pathIdx].targetInfo.targetAvailable = TRUE;
        pathArray[pathIdx].targetInfo.statusFlags = isActive ? DISPLAYCONFIG_TARGET_IN_USE : 0;

        pathArray[pathIdx].flags = isActive ? DISPLAYCONFIG_PATH_ACTIVE : 0;

        pathIdx++;
        modeIdx += 2;
    }

    *numPathArrayElements = pathIdx;
    *numModeInfoArrayElements = modeIdx;

    if (currentTopologyId && (flags & QDC_DATABASE_CURRENT))
    {
        *currentTopologyId = (pathIdx > 1) ? DISPLAYCONFIG_TOPOLOGY_EXTEND
                                           : DISPLAYCONFIG_TOPOLOGY_INTERNAL;
    }

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
    FIXME( "DisplayConfigGetDeviceInfo: stub!\n" );
    return 1;
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
