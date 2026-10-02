/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     SoundWire audio class interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _SOUNDWIRECLASS_H_
#define _SOUNDWIRECLASS_H_

static const GUID SDCA_POSTURE_MODULE =
{ 0x1746df6, 0x11be, 0x4d41, { 0xa0, 0xb1, 0xb4, 0x90, 0x7d, 0xc3, 0x74, 0x6f } };

#define SDCA_POSTURE_NAME L"Audio Posture"
#define SDCA_POSTURE_VERSION_MAJOR  0x1
#define SDCA_POSTURE_VERSION_MINOR  0X0
#define SDCA_POSTURE_INSTANCEID     0X0

typedef enum _SDCA_POSTURE
{
    SdcaPostureOrientationNotRotated = 0,
    SdcaPostureOrientationRotated90DegreesCounterClockwise,
    SdcaPostureOrientationRotated180DegreesCounterClockwise,
    SdcaPostureOrientationRotated270DegreesCounterClockwise,
    SdcaPostureLidClosed
} SDCA_POSTURE, *PSDCA_POSTURE;

typedef struct _SDCA_NOTIFICATION_POSTURE
{
    SDCA_POSTURE          Posture;
} SDCA_NOTIFICATION_POSTURE, *PSDCA_NOTIFICATION_POSTURE;

#endif
