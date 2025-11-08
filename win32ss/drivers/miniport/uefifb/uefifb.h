/*
 * PROJECT:         ReactOS UEFI Framebuffer miniport
 * LICENSE:         BSD-3-Clause (https://spdx.org/licenses/BSD-3-Clause.html)
 * PURPOSE:         UEFI GOP-backed framebuffer miniport driver
 */

#pragma once

#include <ntdef.h>
#include <dderror.h>
#include <miniport.h>
#include <video.h>
#include <ntddvdeo.h>
#include <devioctl.h>
#include <ntstatus.h>

BOOLEAN
NTAPI
MmIsAddressValid(_In_ PVOID VirtualAddress);

#ifndef _LOADER_PARAMETER_FRAMEBUFFER_DEFINED
#define _LOADER_PARAMETER_FRAMEBUFFER_DEFINED
typedef struct _LOADER_PARAMETER_FRAMEBUFFER
{
    LARGE_INTEGER FrameBufferBase;
    ULONG FrameBufferSize;
    ULONG HorizontalResolution;
    ULONG VerticalResolution;
    ULONG PixelsPerScanLine;
    ULONG PixelFormat;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    ULONG Reserved;
} LOADER_PARAMETER_FRAMEBUFFER, *PLOADER_PARAMETER_FRAMEBUFFER;
#endif

#ifndef EFI_GRAPHICS_PIXEL_FORMAT_DEFINED
#define EFI_GRAPHICS_PIXEL_FORMAT_DEFINED
#define PixelRedGreenBlueReserved8BitPerColor 0
#define PixelBlueGreenRedReserved8BitPerColor 1
#define PixelBitMask                           2
#endif

#define UEFIFB_EDID_LENGTH 128

typedef struct _UEFIFB_CHILD_OUTPUT
{
    LOADER_PARAMETER_FRAMEBUFFER FrameBuffer;
    UCHAR SyntheticEdid[UEFIFB_EDID_LENGTH];
    VIDEO_MODE_INFORMATION ModeInfo;
    PVIDEO_MODE_INFORMATION ModeTable;
    ULONG ModeCount;
    ULONG CurrentModeIndex;
    BOOLEAN ModeSet;
    ULONG FrameBufferOffset;
    ULONG FrameBufferMapLength;
    ULONGLONG FrameBufferLength64;
} UEFIFB_CHILD_OUTPUT, *PUEFIFB_CHILD_OUTPUT;

typedef struct _UEFIFB_DEVICE_EXTENSION
{
    LOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo;
    ULONGLONG FrameBufferLength64;
    VIDEO_MODE_INFORMATION ModeInfo;
    ULONG ModeCount;
    PVIDEO_MODE_INFORMATION ModeTable; /* allocated list of modes; currently 1 (current GOP) */
    ULONG CurrentModeIndex;
    PVOID MappedFrameBuffer;
    PVOID MappingBase;
    ULONG MappedLength;
    ULONG MappingLength;
    ULONG FrameBufferOffset;
    ULONG FrameBufferMapLength;
    PUEFIFB_CHILD_OUTPUT ChildOutputs;
    ULONG ChildCount;
    ULONG ActiveChild;
    BOOLEAN ModeSet;
    BOOLEAN DirectMap;
    BOOLEAN ForceMmMap;
    VIDEO_ACCESS_RANGE AccessRanges[1];
} UEFIFB_DEVICE_EXTENSION, *PUEFIFB_DEVICE_EXTENSION;

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfo(
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

/* Future GOP enumeration/switching (kernel stubs present) */
BOOLEAN
NTAPI
InbvQueryGopModeCount(
    _Out_ PULONG Count);

BOOLEAN
NTAPI
InbvQueryGopModeInfo(
    _In_ ULONG Index,
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

ULONG
NTAPI
InbvGetGopFrameBufferCount(VOID);

BOOLEAN
NTAPI
InbvGetGopFrameBufferInfoByIndex(
    _In_ ULONG Index,
    _Out_ PLOADER_PARAMETER_FRAMEBUFFER FrameBufferInfo);

ULONG
NTAPI
InbvGetGopPreferredMode(VOID);

BOOLEAN
NTAPI
InbvSetGopMode(
    _In_ ULONG Index);
