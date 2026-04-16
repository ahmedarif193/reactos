/*
 * Runlist operations for the ntfslx ReactOS driver.
 *
 * Provides merge, truncate, mapping-pairs build/size, and sparse check.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

/*
 * NtfslxRunlistsMerge - merge two sorted runlists into a new allocation
 *
 * Allocates a new array containing all runs from both lists, merged by VCN.
 * Caller must free *MergedRunlist with ExFreePoolWithTag.
 */
NTSTATUS
NtfslxRunlistsMerge(
    _In_reads_(DestCount) PNTFSLX_RUNLIST_ELEMENT DestRunlist,
    _In_ ULONG DestCount,
    _In_reads_(SrcCount) const NTFSLX_RUNLIST_ELEMENT *SrcRunlist,
    _In_ ULONG SrcCount,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *MergedRunlist,
    _Out_ PULONG MergedCount)
{
    PNTFSLX_RUNLIST_ELEMENT Merged;
    ULONG TotalCount;
    ULONG Di = 0, Si = 0, Mi = 0;

    *MergedRunlist = NULL;
    *MergedCount = 0;

    TotalCount = DestCount + SrcCount;
    if (TotalCount == 0)
        return STATUS_SUCCESS;

    Merged = ExAllocatePoolWithTag(
        NonPagedPool,
        TotalCount * sizeof(NTFSLX_RUNLIST_ELEMENT),
        NTFSLX_TAG);
    if (Merged == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Standard sorted merge by VCN */
    while (Di < DestCount && Si < SrcCount)
    {
        if (DestRunlist[Di].Vcn <= SrcRunlist[Si].Vcn)
        {
            Merged[Mi++] = DestRunlist[Di++];
        }
        else
        {
            Merged[Mi++] = SrcRunlist[Si++];
        }
    }

    /* Copy remaining */
    while (Di < DestCount)
        Merged[Mi++] = DestRunlist[Di++];
    while (Si < SrcCount)
        Merged[Mi++] = SrcRunlist[Si++];

    *MergedRunlist = Merged;
    *MergedCount = Mi;
    return STATUS_SUCCESS;
}

/*
 * NtfslxRunlistTruncate - truncate a runlist to a new VCN length
 *
 * Modifies the runlist in place. Runs beyond NewLength are dropped.
 * The last overlapping run is shortened.
 */
NTSTATUS
NtfslxRunlistTruncate(
    _Inout_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Inout_ PULONG RunlistCount,
    _In_ LONGLONG NewLength)
{
    PNTFSLX_RUNLIST_ELEMENT Rl = *Runlist;
    ULONG Count = *RunlistCount;
    ULONG I;

    if (NewLength <= 0)
    {
        *RunlistCount = 0;
        return STATUS_SUCCESS;
    }

    for (I = 0; I < Count; I++)
    {
        LONGLONG RunEnd = Rl[I].Vcn + Rl[I].Length;

        if (RunEnd > NewLength)
        {
            /* This run spans the truncation point */
            if (Rl[I].Vcn < NewLength)
            {
                Rl[I].Length = NewLength - Rl[I].Vcn;
                *RunlistCount = I + 1;
            }
            else
            {
                /* Entire run is beyond truncation point */
                *RunlistCount = I;
            }
            return STATUS_SUCCESS;
        }
    }

    /* NewLength is beyond all runs, nothing to do */
    return STATUS_SUCCESS;
}

/*
 * Helper: calculate bytes needed to encode a signed value in variable-length.
 */
static LONG
NtfslxEncodedLength(
    _In_ LONGLONG Value)
{
    ULONGLONG V;
    LONG Bytes;

    if (Value < 0)
        V = (ULONGLONG)(~Value);
    else
        V = (ULONGLONG)Value;

    Bytes = 1;
    while (V >= 0x80)
    {
        V >>= 8;
        Bytes++;
    }

    return Bytes;
}

/*
 * NtfslxGetSizeForMappingPairs - calculate space needed for mapping pairs
 *
 * Returns the number of bytes needed, or -1 on error.
 * LastVcn of -1 means through the end of the runlist.
 */
LONG
NtfslxGetSizeForMappingPairs(
    _In_ ULONG ClusterSizeBits,
    _In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
    _In_ ULONG RunlistCount,
    _In_ LONGLONG FirstVcn,
    _In_ LONGLONG LastVcn)
{
    LONG Size = 0;
    LONGLONG PrevLcn = 0;
    ULONG I;

    UNREFERENCED_PARAMETER(ClusterSizeBits);

    for (I = 0; I < RunlistCount; I++)
    {
        LONGLONG DeltaLcn;
        LONG LengthBytes;
        LONG OffsetBytes;

        if (Runlist[I].Vcn + Runlist[I].Length <= FirstVcn)
            continue;

        if (LastVcn >= 0 && Runlist[I].Vcn >= LastVcn)
            break;

        LengthBytes = NtfslxEncodedLength(Runlist[I].Length);

        if (Runlist[I].Lcn >= 0)
        {
            DeltaLcn = Runlist[I].Lcn - PrevLcn;
            OffsetBytes = NtfslxEncodedLength(DeltaLcn);
            PrevLcn = Runlist[I].Lcn;
        }
        else
        {
            /* Sparse/hole: no LCN offset */
            OffsetBytes = 0;
        }

        /* 1 header byte + length bytes + offset bytes */
        Size += 1 + LengthBytes + OffsetBytes;
    }

    /* Terminator byte */
    Size += 1;

    return Size;
}

/*
 * Helper: encode a signed value in variable-length little-endian.
 */
static VOID
NtfslxEncodeValue(
    _Out_writes_(ByteCount) PUCHAR Dst,
    _In_ LONG ByteCount,
    _In_ LONGLONG Value)
{
    LONG I;
    for (I = 0; I < ByteCount; I++)
    {
        Dst[I] = (UCHAR)(Value & 0xFF);
        Value >>= 8;
    }
}

/*
 * NtfslxMappingPairsBuild - build mapping pairs from a runlist
 *
 * Writes the encoded mapping pairs into Dst.
 * Returns STATUS_SUCCESS or STATUS_BUFFER_OVERFLOW.
 */
NTSTATUS
NtfslxMappingPairsBuild(
    _In_ ULONG ClusterSizeBits,
    _Out_writes_bytes_(DstLength) PUCHAR Dst,
    _In_ LONG DstLength,
    _In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
    _In_ ULONG RunlistCount,
    _In_ LONGLONG FirstVcn,
    _In_ LONGLONG LastVcn,
    _Out_opt_ PLONGLONG StopVcn)
{
    LONGLONG PrevLcn = 0;
    LONG Pos = 0;
    ULONG I;

    UNREFERENCED_PARAMETER(ClusterSizeBits);

    if (StopVcn != NULL)
        *StopVcn = FirstVcn;

    for (I = 0; I < RunlistCount; I++)
    {
        LONGLONG DeltaLcn;
        LONG LengthBytes;
        LONG OffsetBytes;
        UCHAR Header;

        if (Runlist[I].Vcn + Runlist[I].Length <= FirstVcn)
            continue;

        if (LastVcn >= 0 && Runlist[I].Vcn >= LastVcn)
            break;

        LengthBytes = NtfslxEncodedLength(Runlist[I].Length);

        if (Runlist[I].Lcn >= 0)
        {
            DeltaLcn = Runlist[I].Lcn - PrevLcn;
            OffsetBytes = NtfslxEncodedLength(DeltaLcn);
            PrevLcn = Runlist[I].Lcn;
        }
        else
        {
            DeltaLcn = 0;
            OffsetBytes = 0;
        }

        /* Check space: 1 (header) + lengths + offset + 1 (terminator) */
        if (Pos + 1 + LengthBytes + OffsetBytes + 1 > DstLength)
            return STATUS_BUFFER_OVERFLOW;

        Header = (UCHAR)((OffsetBytes << 4) | LengthBytes);
        Dst[Pos++] = Header;

        NtfslxEncodeValue(Dst + Pos, LengthBytes, Runlist[I].Length);
        Pos += LengthBytes;

        if (OffsetBytes > 0)
        {
            NtfslxEncodeValue(Dst + Pos, OffsetBytes, DeltaLcn);
            Pos += OffsetBytes;
        }

        if (StopVcn != NULL)
            *StopVcn = Runlist[I].Vcn + Runlist[I].Length;
    }

    /* Terminator */
    if (Pos >= DstLength)
        return STATUS_BUFFER_OVERFLOW;

    Dst[Pos] = 0;

    return STATUS_SUCCESS;
}

/*
 * NtfslxRunlistIsSparse - check if any run is a hole
 */
BOOLEAN
NtfslxRunlistIsSparse(
    _In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
    _In_ ULONG RunlistCount)
{
    ULONG I;

    for (I = 0; I < RunlistCount; I++)
    {
        if (Runlist[I].Lcn < 0 && Runlist[I].Length > 0)
            return TRUE;
    }

    return FALSE;
}
