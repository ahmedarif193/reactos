/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Hardware notification device interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __HWN_W__
#define __HWN_W__

#ifdef __cplusplus
extern "C" {
#endif

#define IOCTL_HWN_SET_STATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0, METHOD_BUFFERED, FILE_WRITE_ACCESS)
#define IOCTL_HWN_GET_STATE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 1, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

DEFINE_GUID(HWN_DEVINTERFACE,
    0x81ca6602, 0xdae3, 0x476f, 0x8e, 0x3a, 0x60, 0x76, 0x6b, 0x2f, 0xff, 0x1b);
DEFINE_GUID(HWN_DEVINTERFACE_VIBRATOR,
    0x825e8fd3, 0x6467, 0x4a5b, 0xab, 0xfb, 0xbb, 0x01, 0x07, 0x34, 0xff, 0x4d);
DEFINE_GUID(HWN_DEVINTERFACE_NLED,
    0x6b2a25e2, 0xaaf5, 0x482c, 0x99, 0xa5, 0x62, 0x05, 0xcd, 0xcc, 0x17, 0x6a);

#define HWN_SUPPORTED                 0xAAAA
#define HWN_NOT_SUPPORTED             0x5555
#define HWN_CURRENT_MTE_NOT_SUPPORTED 0xFFFFFFFF

typedef enum _HWN_STATE
{
    HWN_OFF = 0,
    HWN_ON = 1,
    HWN_BLINK = 2,
    HWN_TOTAL_STATES
} HWN_STATE;

typedef enum _HWN_TYPE
{
    HWN_UNKNOWN = 0,
    HWN_LED = 1,
    HWN_VIBRATOR = 2,
    HWN_TOTAL_TYPES
} HWN_TYPE;

typedef enum _HWN_SETTINGS_OPTIONS
{
    HWN_INTENSITY = 0,
    HWN_PERIOD = 1,
    HWN_DUTY_CYCLE = 2,
    HWN_CYCLE_COUNT = 3,
    HWN_CYCLE_GRANULARITY = 4,
    HWN_CURRENT_MTE_RESERVED = 5,
    HWN_TOTAL_SETTINGS
} HWN_SETTINGS_OPTIONS;

typedef struct _HWN_SETTINGS
{
    ULONG HwNId;
    HWN_TYPE HwNType;
    ULONG HwNSettings[HWN_TOTAL_SETTINGS];
    HWN_STATE OffOnBlink;
} HWN_SETTINGS, *PHWN_SETTINGS;

#define HWN_SETTINGS_SIZE (sizeof(HWN_SETTINGS))

typedef struct _HWN_HEADER
{
    ULONG HwNPayloadSize;
    ULONG HwNPayloadVersion;
    ULONG HwNRequests;
    HWN_SETTINGS HwNSettingsInfo[ANYSIZE_ARRAY];
} HWN_HEADER, *PHWN_HEADER;

#define HWN_HEADER_SIZE (sizeof(HWN_HEADER) - sizeof(HWN_SETTINGS))

#ifdef __cplusplus
}
#endif

#endif
