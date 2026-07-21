/*
 * Extended processor-state context support
 *
 * Copyright 2021 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

/* Synced with Wine commit e8781e7c8d0. */

#include <ntdll.h>
#define __WINE_WINNT_EXCEPTION_REGISTRATION_RECORD
#include <wine/winnt.h>

#define NDEBUG
#include <debug.h>

typedef struct _RTL_CONTEXT_CHUNK
{
    LONG Offset;
    ULONG Length;
} RTL_CONTEXT_CHUNK;

typedef struct _RTL_CONTEXT_EX
{
    RTL_CONTEXT_CHUNK All;
    RTL_CONTEXT_CHUNK Legacy;
    RTL_CONTEXT_CHUNK XState;
} RTL_CONTEXT_EX;

struct context_copy_range
{
    ULONG start;
    ULONG flag;
};

static const struct context_copy_range RtlpCopyRangesAmd64[] =
{
    {0x38, 0x1}, {0x3a, 0x4}, {0x42, 0x1}, {0x48, 0x10}, {0x78, 0x2}, {0x98, 0x1},
    {0xa0, 0x2}, {0xf8, 0x1}, {0x100, 0x8}, {0x2a0, 0}, {0x4b0, 0x10}, {0x4d0, 0}
};

static const struct context_copy_range RtlpCopyRangesI386[] =
{
    {0x4, 0x10}, {0x1c, 0x8}, {0x8c, 0x4}, {0x9c, 0x2}, {0xb4, 0x1}, {0xcc, 0x20},
    {0x1ec, 0}, {0x2cc, 0}
};

static const struct context_parameters
{
    ULONG arch_flag;
    ULONG supported_flags;
    ULONG context_size;
    ULONG legacy_size;
    ULONG context_ex_size;
    ULONG alignment;
    ULONG true_alignment;
    ULONG flags_offset;
    const struct context_copy_range *copy_ranges;
} RtlpArchContextParameters[] =
{
    {
        CONTEXT_AMD64,
        0xd8000000 | CONTEXT_AMD64_ALL | CONTEXT_AMD64_XSTATE,
        sizeof(AMD64_CONTEXT),
        sizeof(AMD64_CONTEXT),
        0x20,
        7,
        TYPE_ALIGNMENT(AMD64_CONTEXT) - 1,
        FIELD_OFFSET(AMD64_CONTEXT, ContextFlags),
        RtlpCopyRangesAmd64
    },
    {
        CONTEXT_i386,
        0xd8000000 | CONTEXT_I386_ALL | CONTEXT_I386_XSTATE,
        sizeof(I386_CONTEXT),
        FIELD_OFFSET(I386_CONTEXT, ExtendedRegisters),
        0x18,
        3,
        TYPE_ALIGNMENT(I386_CONTEXT) - 1,
        FIELD_OFFSET(I386_CONTEXT, ContextFlags),
        RtlpCopyRangesI386
    }
};

static const struct context_parameters *
RtlpGetContextParameters(ULONG ContextFlags)
{
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(RtlpArchContextParameters); ++Index)
    {
        const struct context_parameters *Parameters = &RtlpArchContextParameters[Index];

        if (ContextFlags & Parameters->arch_flag)
            return ContextFlags & ~Parameters->supported_flags ? NULL : Parameters;
    }
    return NULL;
}

static ULONG
RtlpNextCompactedOffset(ULONG Offset, ULONG64 CompactionMask, ULONG FeatureIndex)
{
    ULONG64 FeatureMask = (ULONG64)1 << FeatureIndex;

    if (CompactionMask & FeatureMask)
        Offset += SharedUserData->XState.Features[FeatureIndex].Size;
    if (SharedUserData->XState.AlignedFeatures & (FeatureMask << 1))
        Offset = ALIGN_UP_BY(Offset, 64);
    return Offset;
}

static ULONG
RtlpGetCompactedXStateSize(ULONG64 Mask)
{
    ULONG64 CompactionMask = ((ULONG64)1 << 63) | Mask;
    ULONG Offset = sizeof(XSAVE_AREA_HEADER);
    ULONG Index = 2;

    Mask >>= 2;
    while (Mask)
    {
        if (Mask == 1)
            return Offset + SharedUserData->XState.Features[Index].Size;
        Offset = RtlpNextCompactedOffset(Offset, CompactionMask, Index);
        Mask >>= 1;
        ++Index;
    }
    return Offset;
}

static ULONG
RtlpGetXStateSize(ULONG64 Mask)
{
    ULONG Index = 2;

    Mask >>= 2;
    if (!Mask)
        return sizeof(XSAVE_AREA_HEADER);
    while (Mask != 1)
    {
        Mask >>= 1;
        ++Index;
    }
    return SharedUserData->XState.Features[Index].Offset +
           SharedUserData->XState.Features[Index].Size - sizeof(XSAVE_FORMAT);
}

ULONG64
NTAPI
RtlGetEnabledExtendedFeatures(ULONG64 FeatureMask)
{
    return SharedUserData->XState.EnabledFeatures & FeatureMask;
}

NTSTATUS
NTAPI
RtlGetExtendedContextLength2(ULONG ContextFlags, PULONG Length, ULONG64 CompactionMask)
{
    const struct context_parameters *Parameters;
    ULONG64 SupportedMask, Size;

    Parameters = RtlpGetContextParameters(ContextFlags);
    if (!Parameters)
        return STATUS_INVALID_PARAMETER;

    if (!(ContextFlags & 0x40))
    {
        *Length = Parameters->context_size + Parameters->context_ex_size + Parameters->alignment;
        return STATUS_SUCCESS;
    }

    SupportedMask = RtlGetEnabledExtendedFeatures(~(ULONG64)0);
    if (!SupportedMask)
        return STATUS_NOT_SUPPORTED;

    Size = Parameters->context_size + Parameters->context_ex_size + 63;
    CompactionMask &= SupportedMask & ~(ULONG64)3;
    if (SharedUserData->XState.CompactionEnabled)
        Size += RtlpGetCompactedXStateSize(CompactionMask);
    else if (CompactionMask)
        Size += RtlpGetXStateSize(CompactionMask);
    else
        Size += sizeof(XSAVE_AREA_HEADER);

    *Length = (ULONG)Size;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlGetExtendedContextLength(ULONG ContextFlags, PULONG Length)
{
    return RtlGetExtendedContextLength2(ContextFlags, Length, ~(ULONG64)0);
}

NTSTATUS
NTAPI
RtlInitializeExtendedContext2(PVOID Context,
                              ULONG ContextFlags,
                              RTL_CONTEXT_EX **ContextEx,
                              ULONG64 CompactionMask)
{
    const struct context_parameters *Parameters;
    ULONG64 SupportedMask = 0;
    RTL_CONTEXT_EX *Extended;

    Parameters = RtlpGetContextParameters(ContextFlags);
    if (!Parameters)
        return STATUS_INVALID_PARAMETER;

    if ((ContextFlags & 0x40) &&
        !(SupportedMask = RtlGetEnabledExtendedFeatures(~(ULONG64)0)))
        return STATUS_NOT_SUPPORTED;

    Context = (PVOID)(((ULONG_PTR)Context + Parameters->true_alignment) &
                      ~(ULONG_PTR)Parameters->true_alignment);
    *(PULONG)((PUCHAR)Context + Parameters->flags_offset) = ContextFlags;

    *ContextEx = Extended = (RTL_CONTEXT_EX *)((PUCHAR)Context + Parameters->context_size);
    Extended->Legacy.Offset = Extended->All.Offset = -(LONG)Parameters->context_size;
    Extended->Legacy.Length = ContextFlags & 0x20 ? Parameters->context_size : Parameters->legacy_size;

    if (ContextFlags & 0x40)
    {
        XSTATE *XState;

        CompactionMask &= SupportedMask;
        XState = (XSTATE *)ALIGN_UP_BY((ULONG_PTR)Extended + Parameters->context_ex_size, 64);
        Extended->XState.Offset = (ULONG)((ULONG_PTR)XState - (ULONG_PTR)Extended);

        if (SharedUserData->XState.CompactionEnabled)
            Extended->XState.Length = RtlpGetCompactedXStateSize(CompactionMask);
        else if (CompactionMask & ~(ULONG64)3)
            Extended->XState.Length = RtlpGetXStateSize(CompactionMask);
        else
            Extended->XState.Length = sizeof(XSAVE_AREA_HEADER);

        RtlZeroMemory(XState, Extended->XState.Length);
        if (SharedUserData->XState.CompactionEnabled)
            XState->CompactionMask = ((ULONG64)1 << 63) | CompactionMask;

        Extended->All.Length = Parameters->context_size +
                               Extended->XState.Offset +
                               Extended->XState.Length;
    }
    else
    {
        Extended->XState.Offset = 25;
        Extended->XState.Length = 0;
        Extended->All.Length = Parameters->context_size + 24;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RtlInitializeExtendedContext(PVOID Context, ULONG ContextFlags, RTL_CONTEXT_EX **ContextEx)
{
    return RtlInitializeExtendedContext2(Context, ContextFlags, ContextEx, ~(ULONG64)0);
}

PVOID
NTAPI
RtlLocateLegacyContext(RTL_CONTEXT_EX *ContextEx, PULONG Length)
{
    if (Length)
        *Length = ContextEx->Legacy.Length;
    return (PUCHAR)ContextEx + ContextEx->Legacy.Offset;
}

VOID
NTAPI
RtlSetExtendedFeaturesMask(RTL_CONTEXT_EX *ContextEx, ULONG64 FeatureMask)
{
    XSTATE *XState = (XSTATE *)((PUCHAR)ContextEx + ContextEx->XState.Offset);

    XState->Mask = RtlGetEnabledExtendedFeatures(FeatureMask) & ~(ULONG64)3;
}

ULONG64
NTAPI
RtlGetExtendedFeaturesMask(RTL_CONTEXT_EX *ContextEx)
{
    XSTATE *XState = (XSTATE *)((PUCHAR)ContextEx + ContextEx->XState.Offset);

    return XState->Mask & ~(ULONG64)3;
}

static VOID
RtlpCopyContextRanges(PUCHAR Destination,
                      ULONG ContextFlags,
                      PUCHAR Source,
                      const struct context_parameters *Parameters)
{
    const struct context_copy_range *Range = Parameters->copy_ranges;
    ULONG Start = 0;

    *(PULONG)(Destination + Parameters->flags_offset) |= ContextFlags;
    do
    {
        if (Range->flag & ContextFlags)
        {
            if (!Start)
                Start = Range->start;
        }
        else if (Start)
        {
            RtlCopyMemory(Destination + Start, Source + Start, Range->start - Start);
            Start = 0;
        }
    } while ((Range++)->start != Parameters->context_size);
}

NTSTATUS
NTAPI
RtlCopyExtendedContext(RTL_CONTEXT_EX *Destination,
                       ULONG ContextFlags,
                       RTL_CONTEXT_EX *Source)
{
    const struct context_parameters *Parameters;
    PXSAVE_AREA_HEADER DestinationXState, SourceXState;
    ULONG64 FeatureMask;
    ULONG Index, Offset, Size;

    Parameters = RtlpGetContextParameters(ContextFlags);
    if (!Parameters)
        return STATUS_INVALID_PARAMETER;

    FeatureMask = RtlGetEnabledExtendedFeatures(~(ULONG64)0);
    if (!FeatureMask && (ContextFlags & 0x40))
        return STATUS_NOT_SUPPORTED;

    RtlpCopyContextRanges(RtlLocateLegacyContext(Destination, NULL), ContextFlags, RtlLocateLegacyContext(Source, NULL), Parameters);
    if (!(ContextFlags & 0x40))
        return STATUS_SUCCESS;
    if (Destination->XState.Length < sizeof(XSAVE_AREA_HEADER))
        return STATUS_BUFFER_OVERFLOW;

    DestinationXState = (PXSAVE_AREA_HEADER)((PUCHAR)Destination + Destination->XState.Offset);
    SourceXState = (PXSAVE_AREA_HEADER)((PUCHAR)Source + Source->XState.Offset);
    RtlZeroMemory(DestinationXState, sizeof(*DestinationXState));
    DestinationXState->Mask = (SourceXState->Mask & ~(ULONG64)3) & FeatureMask;
    DestinationXState->CompactionMask = SharedUserData->XState.CompactionEnabled
        ? ((ULONG64)1 << 63) | (SourceXState->CompactionMask & FeatureMask)
        : 0;

    if (DestinationXState->CompactionMask)
        FeatureMask &= DestinationXState->CompactionMask;
    FeatureMask = DestinationXState->Mask >> 2;

    Index = 2;
    Offset = sizeof(XSAVE_AREA_HEADER);
    for (;;)
    {
        if (FeatureMask & 1)
        {
            if (!DestinationXState->CompactionMask)
                Offset = SharedUserData->XState.Features[Index].Offset - sizeof(XSAVE_FORMAT);
            Size = SharedUserData->XState.Features[Index].Size;
            if (Source->XState.Length < Offset + Size || Destination->XState.Length < Offset + Size)
                break;
            RtlCopyMemory((PUCHAR)DestinationXState + Offset, (PUCHAR)SourceXState + Offset, Size);
        }
        FeatureMask >>= 1;
        if (!FeatureMask)
            break;
        if (DestinationXState->CompactionMask)
            Offset = RtlpNextCompactedOffset(Offset, DestinationXState->CompactionMask, Index);
        ++Index;
    }
    return STATUS_SUCCESS;
}
