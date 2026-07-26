/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS allocation-layout queries
 * COPYRIGHT:   Copyright 2026 ReactOS contributors
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static BOOLEAN
CanMergeRetrievalRuns(_In_ PDataRun Left,
                      _In_ PDataRun Right)
{
    if (Left->IsSparse || Right->IsSparse)
        return Left->IsSparse && Right->IsSparse;

    return Left->LCN <= ~(ULONGLONG)0 - Left->Length &&
           Left->LCN + Left->Length == Right->LCN;
}

NTSTATUS
FileRecord::QueryRetrievalPointers(
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG StartingVcn,
    _Out_ PULONGLONG ReturnedStartingVcn,
    _Out_opt_ PNtfsRetrievalExtent Extents,
    _Inout_ PULONG ExtentCount)
{
    PAttribute Attribute;
    PDataRun Runs;
    PDataRun Run;
    ULONGLONG AllocatedClusters;
    ULONGLONG TotalClusters = 0;
    ULONGLONG CurrentVcn = 0;
    ULONG Capacity;
    ULONG Written = 0;
    BOOLEAN Found = FALSE;

    if (!ReturnedStartingVcn ||
        !ExtentCount ||
        (!Extents && *ExtentCount != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Capacity = *ExtentCount;
    *ExtentCount = 0;
    *ReturnedStartingVcn = 0;

    Attribute = GetAttribute(AttrType, StreamName);
    if (!Attribute)
        return STATUS_NOT_FOUND;
    if (!Attribute->IsNonResident)
        return STATUS_END_OF_FILE;
    if (Attribute->NonResident.FirstVCN != 0 ||
        BytesPerCluster(DiskVolume) == 0 ||
        Attribute->NonResident.AllocatedSize %
            BytesPerCluster(DiskVolume) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    AllocatedClusters =
        Attribute->NonResident.AllocatedSize /
        BytesPerCluster(DiskVolume);
    if (StartingVcn >= AllocatedClusters)
        return STATUS_END_OF_FILE;

    Runs = GetCachedDataRuns(Attribute);
    if (!Runs)
        return STATUS_FILE_CORRUPT_ERROR;

    for (Run = Runs; Run; Run = Run->NextRun)
    {
        if (Run->Length == 0 ||
            TotalClusters >
                ~(ULONGLONG)0 - Run->Length)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        TotalClusters += Run->Length;
    }
    if (TotalClusters != AllocatedClusters)
        return STATUS_FILE_CORRUPT_ERROR;

    Run = Runs;
    while (Run)
    {
        PDataRun Last = Run;
        PDataRun Next = Run->NextRun;
        ULONGLONG ExtentStart = CurrentVcn;
        ULONGLONG ExtentLength = Run->Length;

        while (Next &&
               CanMergeRetrievalRuns(Last, Next))
        {
            if (ExtentLength >
                ~(ULONGLONG)0 - Next->Length)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            ExtentLength += Next->Length;
            Last = Next;
            Next = Next->NextRun;
        }

        if (CurrentVcn >
            ~(ULONGLONG)0 - ExtentLength)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        CurrentVcn += ExtentLength;

        if (CurrentVcn > StartingVcn)
        {
            if (!Found)
            {
                *ReturnedStartingVcn = ExtentStart;
                Found = TRUE;
            }
            if (Written == Capacity)
            {
                *ExtentCount = Written;
                return Written == 0
                    ? STATUS_BUFFER_TOO_SMALL
                    : STATUS_BUFFER_OVERFLOW;
            }

            Extents[Written].NextVcn = CurrentVcn;
            Extents[Written].Lcn = Run->IsSparse
                ? (LONGLONG)-1
                : (LONGLONG)Run->LCN;
            Written++;
        }

        Run = Next;
    }

    if (!Found || CurrentVcn != AllocatedClusters)
        return STATUS_FILE_CORRUPT_ERROR;

    *ExtentCount = Written;
    return STATUS_SUCCESS;
}
