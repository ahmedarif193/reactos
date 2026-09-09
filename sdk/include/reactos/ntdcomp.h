/*
 * PROJECT:     ReactOS DirectComposition
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Native composition channel entry points
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

LONG NTAPI NtDCompositionCreateConnection(BOOL ConnectionFlag, HANDLE Event, HANDLE *Connection);
LONG NTAPI NtDCompositionDestroyConnection(HANDLE Connection);
LONG NTAPI NtDCompositionCreateChannel(UINT *Channel, UINT *SectionSize, PVOID *SectionBase);
LONG NTAPI NtDCompositionDestroyChannel(UINT Channel);
LONG NTAPI NtDCompositionProcessChannelBatchBuffer(UINT Channel, UINT ByteCount, UINT *CommandCount, BOOLEAN *State);
LONG NTAPI NtDCompositionCommitChannel(UINT Channel, UINT *CommitId, BOOLEAN *State, ULONG Flags, HANDLE Synchronization, const VOID *Commands, const UINT *Resources, UINT ResourceCount);

/* Producer protocol values observed on Windows 11 build 26100. The external
 * buffer address occupies 64 bits in the command stream. */
typedef enum _DCOMP_COMMAND
{
    DCompCommandExternalBuffer = 0,
    DCompCommandCreateResource = 2,
    DCompCommandReleaseResource = 4,
    DCompCommandSetIntegerProperty = 11,
    DCompCommandSetFloatProperty = 12,
    DCompCommandAddVisualChild = 20,
    DCompCommandRemoveVisualChild = 23
} DCOMP_COMMAND;

#define DCOMP_RESOURCE_VISUAL 181

/* Compositor commands have a byte length, an opcode and a resource identifier.
 * These values belong to the consumer protocol, not the producer commands. */
typedef enum _DCOMP_MIL_COMMAND
{
    DCompMilCreateResource = 37,
    DCompMilReleaseResource = 38,
    DCompMilChannelDescription = 42,
    DCompMilResourceNotificationId = 297,
    DCompMilVisualForceLowColor = 377,
    DCompMilVisualInsertChild = 378,
    DCompMilVisualProtection = 379,
    DCompMilVisualRemoveAllChildren = 380,
    DCompMilVisualResampleMode = 382,
    DCompMilVisualContextOverrides = 388,
    DCompMilVisualHeatMap = 392,
    DCompMilVisualOffset = 394,
    DCompMilVisualOpacity = 395,
    DCompMilVisualOptions = 396,
    DCompMilVisualRedrawRegion = 398,
    DCompMilVisualRelativeOffset = 399,
    DCompMilVisualRelativeSize = 400,
    DCompMilVisualRenderOptions = 401,
    DCompMilVisualSize = 403,
    DCompMilVisualVisible = 407
} DCOMP_MIL_COMMAND;

typedef struct _DCOMP_EXTERNAL_BUFFER
{
    UINT Command;
    UINT Reserved;
    ULONGLONG Address;
    UINT ByteCount;
    UINT Padding;
} DCOMP_EXTERNAL_BUFFER;

#ifdef __cplusplus
}
#endif
