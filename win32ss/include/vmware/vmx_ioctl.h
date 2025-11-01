/*
 * ReactOS VMware SVGA shared ioctl definitions
 *
 * Copyright (C) 2024 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once

#include <devioctl.h>

#define IOCTL_VIDEO_VMWARE_QUERY_CAPS \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_VMWARE_FIFO_PRESENT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_VMWARE_FIFO_BLIT \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VIDEO_VMWARE_FIFO_FILL \
    CTL_CODE(FILE_DEVICE_VIDEO, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define VMWARE_VIDEO_CAPS_VERSION      1

#define VMWARE_VIDEO_CAP_FIFO          0x00000001
#define VMWARE_VIDEO_CAP_IRQ           0x00000002
#define VMWARE_VIDEO_CAP_FENCE         0x00000004

#define VMWARE_VIDEO_PRESENT_ASYNC     0x00000001

typedef struct _VMWARE_VIDEO_CAPS
{
    ULONG Version;
    ULONG Caps;
}
VMWARE_VIDEO_CAPS, *PVMWARE_VIDEO_CAPS;

typedef struct _VMWARE_VIDEO_RECT
{
    ULONG X;
    ULONG Y;
    ULONG Width;
    ULONG Height;
}
VMWARE_VIDEO_RECT, *PVMWARE_VIDEO_RECT;

typedef struct _VMWARE_VIDEO_PRESENT
{
    ULONG Flags;
    ULONG RectCount;
    VMWARE_VIDEO_RECT Rects[ANYSIZE_ARRAY];
}
VMWARE_VIDEO_PRESENT, *PVMWARE_VIDEO_PRESENT;

typedef struct _VMWARE_VIDEO_BLIT
{
    ULONG Flags;
    ULONG SrcX;
    ULONG SrcY;
    ULONG DestX;
    ULONG DestY;
    ULONG Width;
    ULONG Height;
}
VMWARE_VIDEO_BLIT, *PVMWARE_VIDEO_BLIT;

typedef struct _VMWARE_VIDEO_FILL
{
    ULONG Flags;
    ULONG Color;
    ULONG X;
    ULONG Y;
    ULONG Width;
    ULONG Height;
}
VMWARE_VIDEO_FILL, *PVMWARE_VIDEO_FILL;
