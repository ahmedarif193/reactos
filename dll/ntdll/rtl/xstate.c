/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Extended processor-state helpers
 * PROGRAMMERS: Wine
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTL_CONTEXT_CHUNK
{
    LONG Offset;
    ULONG Length;
} RTL_CONTEXT_CHUNK, *PRTL_CONTEXT_CHUNK;

typedef struct _RTL_CONTEXT_EX
{
    RTL_CONTEXT_CHUNK All;
    RTL_CONTEXT_CHUNK Legacy;
    RTL_CONTEXT_CHUNK XState;
} RTL_CONTEXT_EX, *PRTL_CONTEXT_EX;

static
ULONG
RtlpNextCompactedXStateOffset(ULONG Offset,
                              ULONG64 CompactionMask,
                              ULONG FeatureId)
{
    ULONG64 FeatureMask = (ULONG64)1 << FeatureId;

    if (CompactionMask & FeatureMask)
        Offset += SharedUserData->XState.Features[FeatureId].Size;

    if (SharedUserData->XState.AlignedFeatures & (FeatureMask << 1))
        Offset = ALIGN_UP_BY(Offset, 64);

    return Offset;
}

PVOID
WINAPI
RtlLocateExtendedFeature2(PVOID ContextExPointer,
                          ULONG FeatureId,
                          XSTATE_CONFIGURATION *XStateConfiguration,
                          PULONG Length)
{
    PRTL_CONTEXT_EX ContextEx = ContextExPointer;
    PXSAVE_AREA_HEADER XSaveHeader;
    ULONG64 FeatureMask;
    ULONG Offset, Index;

    if (!XStateConfiguration)
        return NULL;

    if (XStateConfiguration != &SharedUserData->XState)
        return NULL;

    if (FeatureId < 2 || FeatureId >= 64)
        return NULL;

    FeatureMask = (ULONG64)1 << FeatureId;
    XSaveHeader = (PXSAVE_AREA_HEADER)((PUCHAR)ContextEx + ContextEx->XState.Offset);

    if (Length)
        *Length = XStateConfiguration->Features[FeatureId].Size;

    if (XStateConfiguration->CompactionEnabled)
    {
        if (!(XSaveHeader->CompactionMask & FeatureMask))
            return NULL;

        Offset = sizeof(XSAVE_AREA_HEADER);
        for (Index = 2; Index < FeatureId; ++Index)
        {
            Offset = RtlpNextCompactedXStateOffset(Offset,
                                                   XSaveHeader->CompactionMask,
                                                   Index);
        }
    }
    else
    {
        if (!(FeatureMask & XStateConfiguration->EnabledFeatures))
            return NULL;

        Offset = XStateConfiguration->Features[FeatureId].Offset - sizeof(XSAVE_FORMAT);
    }

    if (ContextEx->XState.Length < Offset + XStateConfiguration->Features[FeatureId].Size)
        return NULL;

    return (PUCHAR)XSaveHeader + Offset;
}

PVOID
WINAPI
RtlLocateExtendedFeature(PVOID ContextEx,
                         ULONG FeatureId,
                         PULONG Length)
{
    return RtlLocateExtendedFeature2(ContextEx,
                                     FeatureId,
                                     &SharedUserData->XState,
                                     Length);
}
