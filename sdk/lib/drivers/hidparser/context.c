/*
 * PROJECT:     ReactOS HID Parser Library
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        lib/drivers/hidparser/context.c
 * PURPOSE:     HID Parser
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "parser.h"

#define NDEBUG
#include <debug.h>

typedef struct
{
    ULONG Size;
    union
    {
        UCHAR RawData[1];
    };
}HID_COLLECTION_CONTEXT, *PHID_COLLECTION_CONTEXT;

static
BOOLEAN
HidParser_IsRawRangeValid(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN ULONG Offset,
    IN ULONG Length)
{
    ULONG RawDataSize;

    if (!CollectionContext || CollectionContext->Size < sizeof(HID_COLLECTION_CONTEXT))
        return FALSE;

    RawDataSize = CollectionContext->Size - sizeof(HID_COLLECTION_CONTEXT);

    if (Offset > RawDataSize)
        return FALSE;

    return Length <= RawDataSize - Offset;
}

static
BOOLEAN
HidParser_GetCollectionHeaderSize(
    IN PHID_COLLECTION Collection,
    OUT PULONG CollectionSize)
{
    ULONG OffsetCount;

    OffsetCount = Collection->ReportCount + Collection->NodeCount;
    if (OffsetCount == 0)
        OffsetCount = 1;

    if (OffsetCount > (MAXULONG - FIELD_OFFSET(HID_COLLECTION, Offsets)) / sizeof(ULONG))
        return FALSE;

    *CollectionSize = FIELD_OFFSET(HID_COLLECTION, Offsets) + OffsetCount * sizeof(ULONG);
    return TRUE;
}

static
BOOLEAN
HidParser_GetCollectionAtOffset(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN ULONG Offset,
    OUT PHID_COLLECTION *Collection)
{
    ULONG CollectionSize;

    if (!HidParser_IsRawRangeValid(CollectionContext, Offset, sizeof(HID_COLLECTION)))
        return FALSE;

    *Collection = (PHID_COLLECTION)(CollectionContext->RawData + Offset);

    if (!HidParser_GetCollectionHeaderSize(*Collection, &CollectionSize))
        return FALSE;

    return HidParser_IsRawRangeValid(CollectionContext, Offset, CollectionSize);
}

ULONG
HidParser_CalculateCollectionSize(
    IN PHID_COLLECTION Collection)
{
    ULONG Size = 0, Index;

    Size = sizeof(HID_COLLECTION);

    //
    // add size required for the number of report items
    //
    for(Index = 0; Index < Collection->ReportCount; Index++)
    {
        //
        // get report size
        //
        ASSERT(Collection->Reports[Index]->ItemCount);
        Size += sizeof(HID_REPORT) + Collection->Reports[Index]->ItemCount * sizeof(HID_REPORT_ITEM);
    }

    //
    // calculate size for sub collections
    //
    for(Index = 0; Index < Collection->NodeCount; Index++)
    {
        Size += HidParser_CalculateCollectionSize(Collection->Nodes[Index]);
    }

    //
    // append size for the offset
    //
    Size += (Collection->ReportCount + Collection->NodeCount) * sizeof(ULONG);

    //
    // done
    //
    return Size;
}

ULONG
HidParser_CalculateContextSize(
    IN PHID_COLLECTION Collection)
{
    ULONG Size;

    //
    // minimum size is the size of the collection
    //
    Size = HidParser_CalculateCollectionSize(Collection);

    //
    // append collection context size
    //
    Size += sizeof(HID_COLLECTION_CONTEXT);
    return Size;
}

ULONG
HidParser_StoreCollection(
    IN PHID_COLLECTION Collection,
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN ULONG CurrentOffset)
{
    ULONG Index;
    ULONG ReportSize;
    ULONG InitialOffset;
    ULONG CollectionSize;
    PHID_COLLECTION TargetCollection;

    //
    // backup initial offset
    //
    InitialOffset = CurrentOffset;

    //
    // get target collection
    //
    TargetCollection = (PHID_COLLECTION)(&CollectionContext->RawData[CurrentOffset]);

    //
    // first copy the collection details
    //
    CopyFunction(TargetCollection, Collection, sizeof(HID_COLLECTION));

    //
    // calulcate collection size
    //
    CollectionSize = sizeof(HID_COLLECTION) + sizeof(ULONG) * (Collection->ReportCount + Collection->NodeCount);

    //
    // increase offset
    //
    CurrentOffset += CollectionSize;

    //
    // sanity check
    //
    ASSERT(CurrentOffset < CollectionContext->Size);

    //
    // first store the report items
    //
    for(Index = 0; Index < Collection->ReportCount; Index++)
    {
        //
        // calculate report size
        //
        ReportSize = sizeof(HID_REPORT) + Collection->Reports[Index]->ItemCount * sizeof(HID_REPORT_ITEM);

        //
        // sanity check
        //
        ASSERT(CurrentOffset + ReportSize < CollectionContext->Size);

        //
        // copy report item
        //
        CopyFunction(&CollectionContext->RawData[CurrentOffset], Collection->Reports[Index], ReportSize);

        //
        // store offset to report item
        //
        TargetCollection->Offsets[Index] = CurrentOffset;

        //
        // move to next offset
        //
        CurrentOffset += ReportSize;
    }

    ASSERT(CurrentOffset <= CollectionContext->Size);

    //
    // now store the sub collections
    //
    for(Index = 0; Index < Collection->NodeCount; Index++)
    {
        //
        // store offset
        //
        TargetCollection->Offsets[Collection->ReportCount + Index] = CurrentOffset;

        //
        // store sub collections
        //
        CurrentOffset += HidParser_StoreCollection(Collection->Nodes[Index], CollectionContext, CurrentOffset);

        //
        // sanity check
        //
        ASSERT(CurrentOffset < CollectionContext->Size);
    }

    //
    // return size of collection
    //
    return CurrentOffset - InitialOffset;
}

NTSTATUS
HidParser_BuildCollectionContext(
    IN PHID_COLLECTION RootCollection,
    IN PVOID Context,
    IN ULONG ContextSize)
{
    PHID_COLLECTION_CONTEXT CollectionContext;
    ULONG CollectionSize;

    //
    // init context
    //
    CollectionContext = (PHID_COLLECTION_CONTEXT)Context;
    CollectionContext->Size = ContextSize;

    //
    // store collections
    //
    CollectionSize = HidParser_StoreCollection(RootCollection, CollectionContext, 0);

    //
    // sanity check
    //
    ASSERT(CollectionSize + sizeof(HID_COLLECTION_CONTEXT) == ContextSize);

    DPRINT("CollectionContext %p\n", CollectionContext);
    DPRINT("CollectionContext RawData %p\n", CollectionContext->RawData);
    DPRINT("CollectionContext Size %lu\n", CollectionContext->Size);

    //
    // done
    //
    return HIDP_STATUS_SUCCESS;
}

PHID_REPORT
HidParser_SearchReportInCollection(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN PHID_COLLECTION Collection,
    IN UCHAR ReportType)
{
    ULONG Index;
    PHID_REPORT Report;
    PHID_COLLECTION SubCollection;

    //
    // search first in local array
    //
    for(Index = 0; Index < Collection->ReportCount; Index++)
    {
        //
        // get report
        //
        Report = (PHID_REPORT)(CollectionContext->RawData + Collection->Offsets[Index]);
        if (Report->Type == ReportType)
        {
            //
            // found report
            //
            return Report;
        }
    }

    //
    // now search in sub collections
    //
    for(Index = 0; Index < Collection->NodeCount; Index++)
    {
        //
        // get collection
        //
        SubCollection = (PHID_COLLECTION)(CollectionContext->RawData + Collection->Offsets[Collection->ReportCount + Index]);

        //
        // recursively search collection
        //
        Report = HidParser_SearchReportInCollection(CollectionContext, SubCollection, ReportType);
        if (Report)
        {
            //
            // found report
            //
            return Report;
        }
    }

    //
    // not found
    //
    return NULL;
}

PHID_REPORT
HidParser_GetReportInCollection(
    IN PVOID Context,
    IN UCHAR ReportType)
{
    PHID_COLLECTION_CONTEXT CollectionContext = (PHID_COLLECTION_CONTEXT)Context;

    //
    // done
    //
    return HidParser_SearchReportInCollection(CollectionContext, (PHID_COLLECTION)&CollectionContext->RawData, ReportType);
}

ULONG
HidParser_GetReportCountInCollection(
    IN PVOID Context)
{
    PHID_COLLECTION Collection = HidParser_GetCollectionFromContext(Context);

    return Collection != NULL ? Collection->ReportCount : 0;
}

PHID_REPORT
HidParser_GetReportByIndex(
    IN PVOID Context,
    IN ULONG Index)
{
    PHID_COLLECTION_CONTEXT CollectionContext = Context;
    PHID_COLLECTION Collection = HidParser_GetCollectionFromContext(Context);

    if (Collection == NULL || Index >= Collection->ReportCount)
        return NULL;

    return (PHID_REPORT)(CollectionContext->RawData + Collection->Offsets[Index]);
}

static
ULONG
HidParser_BitsToBytes(
    IN ULONG BitCount)
{
    return (BitCount + 7) / 8;
}

static
VOID
HidParser_MarkReportIdsInCollection(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN PHID_COLLECTION Collection,
    IN OUT PUCHAR SeenReportIds,
    IN OUT PULONG ReportIdCount)
{
    ULONG Index;
    PHID_REPORT Report;
    PHID_COLLECTION SubCollection;

    for (Index = 0; Index < Collection->ReportCount; Index++)
    {
        Report = (PHID_REPORT)(CollectionContext->RawData + Collection->Offsets[Index]);

        if (!SeenReportIds[Report->ReportID])
        {
            SeenReportIds[Report->ReportID] = TRUE;
            (*ReportIdCount)++;
        }
    }

    for (Index = 0; Index < Collection->NodeCount; Index++)
    {
        SubCollection = (PHID_COLLECTION)(CollectionContext->RawData +
            Collection->Offsets[Collection->ReportCount + Index]);

        HidParser_MarkReportIdsInCollection(CollectionContext,
                                            SubCollection,
                                            SeenReportIds,
                                            ReportIdCount);
    }
}

ULONG
HidParser_GetReportIdCount(
    IN PVOID Context)
{
    UCHAR SeenReportIds[256];
    ULONG ReportIdCount = 0;
    PHID_COLLECTION_CONTEXT CollectionContext = (PHID_COLLECTION_CONTEXT)Context;

    ZeroFunction(SeenReportIds, sizeof(SeenReportIds));

    HidParser_MarkReportIdsInCollection(CollectionContext,
                                        (PHID_COLLECTION)&CollectionContext->RawData,
                                        SeenReportIds,
                                        &ReportIdCount);

    return ReportIdCount;
}

static
VOID
HidParser_GetReportIdLengthsInCollection(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN PHID_COLLECTION Collection,
    IN UCHAR ReportID,
    OUT PUSHORT InputLength,
    OUT PUSHORT OutputLength,
    OUT PUSHORT FeatureLength)
{
    ULONG Index;
    ULONG Length;
    PHID_REPORT Report;
    PHID_COLLECTION SubCollection;

    for (Index = 0; Index < Collection->ReportCount; Index++)
    {
        Report = (PHID_REPORT)(CollectionContext->RawData + Collection->Offsets[Index]);

        if (Report->ReportID != ReportID)
            continue;

        Length = HidParser_BitsToBytes(Report->ReportSize);

        if (Report->Type == HID_REPORT_TYPE_INPUT && Length > *InputLength)
            *InputLength = (USHORT)Length;
        else if (Report->Type == HID_REPORT_TYPE_OUTPUT && Length > *OutputLength)
            *OutputLength = (USHORT)Length;
        else if (Report->Type == HID_REPORT_TYPE_FEATURE && Length > *FeatureLength)
            *FeatureLength = (USHORT)Length;
    }

    for (Index = 0; Index < Collection->NodeCount; Index++)
    {
        SubCollection = (PHID_COLLECTION)(CollectionContext->RawData +
            Collection->Offsets[Collection->ReportCount + Index]);

        HidParser_GetReportIdLengthsInCollection(CollectionContext,
                                                 SubCollection,
                                                 ReportID,
                                                 InputLength,
                                                 OutputLength,
                                                 FeatureLength);
    }
}

static
BOOLEAN
HidParser_FindReportIdByIndexInCollection(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN PHID_COLLECTION Collection,
    IN ULONG ReportIndex,
    IN OUT PUCHAR SeenReportIds,
    IN OUT PULONG CurrentReportIndex,
    OUT PUCHAR ReportID)
{
    ULONG Index;
    PHID_REPORT Report;
    PHID_COLLECTION SubCollection;

    for (Index = 0; Index < Collection->ReportCount; Index++)
    {
        Report = (PHID_REPORT)(CollectionContext->RawData + Collection->Offsets[Index]);

        if (SeenReportIds[Report->ReportID])
            continue;

        SeenReportIds[Report->ReportID] = TRUE;

        if (*CurrentReportIndex == ReportIndex)
        {
            *ReportID = Report->ReportID;
            return TRUE;
        }

        (*CurrentReportIndex)++;
    }

    for (Index = 0; Index < Collection->NodeCount; Index++)
    {
        SubCollection = (PHID_COLLECTION)(CollectionContext->RawData +
            Collection->Offsets[Collection->ReportCount + Index]);

        if (HidParser_FindReportIdByIndexInCollection(CollectionContext,
                                                      SubCollection,
                                                      ReportIndex,
                                                      SeenReportIds,
                                                      CurrentReportIndex,
                                                      ReportID))
        {
            return TRUE;
        }
    }

    return FALSE;
}

NTSTATUS
HidParser_GetReportIdByIndex(
    IN PVOID Context,
    IN ULONG ReportIndex,
    OUT PUCHAR ReportID,
    OUT PUSHORT InputLength,
    OUT PUSHORT OutputLength,
    OUT PUSHORT FeatureLength)
{
    UCHAR SeenReportIds[256];
    ULONG CurrentReportIndex = 0;
    PHID_COLLECTION_CONTEXT CollectionContext = (PHID_COLLECTION_CONTEXT)Context;

    if (!Context || !ReportID || !InputLength || !OutputLength || !FeatureLength)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    ZeroFunction(SeenReportIds, sizeof(SeenReportIds));

    if (!HidParser_FindReportIdByIndexInCollection(CollectionContext,
                                                   (PHID_COLLECTION)&CollectionContext->RawData,
                                                   ReportIndex,
                                                   SeenReportIds,
                                                   &CurrentReportIndex,
                                                   ReportID))
    {
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    }

    *InputLength = 0;
    *OutputLength = 0;
    *FeatureLength = 0;

    HidParser_GetReportIdLengthsInCollection(CollectionContext,
                                             (PHID_COLLECTION)&CollectionContext->RawData,
                                             *ReportID,
                                             InputLength,
                                             OutputLength,
                                             FeatureLength);

    return HIDP_STATUS_SUCCESS;
}

PHID_COLLECTION
HidParser_GetCollectionFromContext(
    IN PVOID Context)
{
    PHID_COLLECTION_CONTEXT CollectionContext = (PHID_COLLECTION_CONTEXT)Context;

    //
    // return root collection
    //
    return (PHID_COLLECTION)CollectionContext->RawData;
}

static
BOOLEAN
HidParser_FindLinkCollectionUsage(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN ULONG CollectionOffset,
    IN USHORT LinkCollection,
    OUT PULONG Usage)
{
    PHID_COLLECTION Collection;
    ULONG Index;

    if (!HidParser_GetCollectionAtOffset(CollectionContext, CollectionOffset, &Collection))
        return FALSE;

    if (Collection->LinkCollection == LinkCollection)
    {
        *Usage = Collection->Usage;
        return TRUE;
    }

    for (Index = 0; Index < Collection->NodeCount; Index++)
    {
        if (HidParser_FindLinkCollectionUsage(CollectionContext, Collection->Offsets[Collection->ReportCount + Index], LinkCollection, Usage))
            return TRUE;
    }

    return FALSE;
}

NTSTATUS
HidParser_GetLinkCollectionUsagePage(
    IN PVOID Context,
    IN USHORT LinkCollection,
    OUT PUSHORT Usage,
    OUT PUSHORT UsagePage)
{
    PHID_COLLECTION_CONTEXT CollectionContext = Context;
    ULONG ExtendedUsage;

    if (!Context || !Usage || !UsagePage || !HidParser_FindLinkCollectionUsage(CollectionContext, 0, LinkCollection, &ExtendedUsage))
        return HIDP_STATUS_USAGE_NOT_FOUND;

    *Usage = (USHORT)(ExtendedUsage & 0xFFFF);
    *UsagePage = (USHORT)(ExtendedUsage >> 16);
    return HIDP_STATUS_SUCCESS;
}

ULONG
HidParser_GetCollectionCount(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN PHID_COLLECTION Collection)
{
    ULONG Index;
    ULONG Count = 1;
    PHID_COLLECTION SubCollection;

    for(Index = 0; Index < Collection->NodeCount; Index++)
    {
        //
        // get offset to sub collection
        //
        SubCollection = (PHID_COLLECTION)(CollectionContext->RawData + Collection->Offsets[Collection->ReportCount + Index]);

        //
        // count collection for sub nodes
        //
        Count += HidParser_GetCollectionCount(CollectionContext, SubCollection);
    }

    //
    // done
    //
    return Count;
}

ULONG
HidParser_GetTotalCollectionCount(
    IN PVOID Context)
{
    PHID_COLLECTION_CONTEXT CollectionContext;
    PHID_COLLECTION Collection;

    //
    // get parser context
    //
    CollectionContext = (PHID_COLLECTION_CONTEXT)Context;

    if (!HidParser_GetCollectionAtOffset(CollectionContext, 0, &Collection))
        return 0;

    //
    // count collections
    //
    return HidParser_GetCollectionCount(CollectionContext, Collection);
}

static
NTSTATUS
HidParser_CountLinkCollectionNodes(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN ULONG CollectionOffset,
    IN OUT PULONG LinkCollectionNodesLength)
{
    ULONG Index;
    PHID_COLLECTION Collection;
    NTSTATUS Status;

    if (!HidParser_GetCollectionAtOffset(CollectionContext, CollectionOffset, &Collection))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    if (*LinkCollectionNodesLength == MAXULONG)
        return HIDP_STATUS_INTERNAL_ERROR;

    (*LinkCollectionNodesLength)++;

    for (Index = 0; Index < Collection->NodeCount; Index++)
    {
        Status = HidParser_CountLinkCollectionNodes(CollectionContext,
                                                    Collection->Offsets[Collection->ReportCount + Index],
                                                    LinkCollectionNodesLength);
        if (Status != HIDP_STATUS_SUCCESS)
            return Status;
    }

    return HIDP_STATUS_SUCCESS;
}

static
NTSTATUS
HidParser_FillLinkCollectionNodes(
    IN PHID_COLLECTION_CONTEXT CollectionContext,
    IN ULONG CollectionOffset,
    IN USHORT Parent,
    IN OUT PULONG NextNode,
    OUT PHIDP_LINK_COLLECTION_NODE LinkCollectionNodes)
{
    ULONG Index;
    ULONG NodeIndex;
    ULONG ChildIndex;
    PHID_COLLECTION Collection;
    PHIDP_LINK_COLLECTION_NODE Node;
    NTSTATUS Status;

    if (!HidParser_GetCollectionAtOffset(CollectionContext, CollectionOffset, &Collection))
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    if (*NextNode > MAXUSHORT)
        return HIDP_STATUS_INTERNAL_ERROR;

    NodeIndex = *NextNode;
    (*NextNode)++;

    Node = &LinkCollectionNodes[NodeIndex];
    ZeroFunction(Node, sizeof(*Node));

    Node->LinkUsage = (USAGE)((Collection->Usage >> USAGE_ID_SHIFT) & USAGE_ID_MASK);
    Node->LinkUsagePage = (USAGE)((Collection->Usage >> USAGE_PAGE_SHIFT) & USAGE_PAGE_MASK);
    Node->Parent = Parent;
    Node->NumberOfChildren = (USHORT)Collection->NodeCount;
    Node->CollectionType = Collection->Type;

    if (Collection->NodeCount)
        Node->FirstChild = (USHORT)*NextNode;

    for (Index = 0; Index < Collection->NodeCount; Index++)
    {
        ChildIndex = *NextNode;
        Status = HidParser_FillLinkCollectionNodes(CollectionContext,
                                                   Collection->Offsets[Collection->ReportCount + Index],
                                                   (USHORT)NodeIndex,
                                                   NextNode,
                                                   LinkCollectionNodes);
        if (Status != HIDP_STATUS_SUCCESS)
            return Status;

        if (Index + 1 < Collection->NodeCount)
            LinkCollectionNodes[ChildIndex].NextSibling = (USHORT)*NextNode;
    }

    return HIDP_STATUS_SUCCESS;
}

HIDAPI
NTSTATUS
NTAPI
HidParser_GetLinkCollectionNodes(
    IN PVOID Context,
    OUT PHIDP_LINK_COLLECTION_NODE  LinkCollectionNodes,
    IN OUT PULONG  LinkCollectionNodesLength)
{
    ULONG RequiredLength = 0;
    ULONG NextNode = 0;
    PHID_COLLECTION_CONTEXT CollectionContext = (PHID_COLLECTION_CONTEXT)Context;
    NTSTATUS Status;

    if (!LinkCollectionNodesLength)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    Status = HidParser_CountLinkCollectionNodes(CollectionContext, 0, &RequiredLength);
    if (Status != HIDP_STATUS_SUCCESS)
        return Status;

    if (!LinkCollectionNodes || *LinkCollectionNodesLength < RequiredLength)
    {
        *LinkCollectionNodesLength = RequiredLength;
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    }

    Status = HidParser_FillLinkCollectionNodes(CollectionContext,
                                               0,
                                               HIDP_LINK_COLLECTION_UNSPECIFIED,
                                               &NextNode,
                                               LinkCollectionNodes);
    if (Status == HIDP_STATUS_SUCCESS)
        *LinkCollectionNodesLength = RequiredLength;

    return Status;
}
