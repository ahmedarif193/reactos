/*
 * PROJECT:     ReactOS HID Parser Library
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        lib/drivers/hidparser/api.c
 * PURPOSE:     HID Parser
 * PROGRAMMERS:
 *              Michael Martin (michael.martin@reactos.org)
 *              Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "parser.h"

#define NDEBUG
#include <debug.h>

static ULONG KeyboardScanCodes[256] =
{ /*    0       1       2       3       4       5       6       7       8       9       A       B       C       D       E       F */
/* 0 */ 0x0000, 0x0000, 0x0000, 0x0000, 0x001e, 0x0030, 0x002e, 0x0020, 0x0012, 0x0021, 0x0022, 0x0023, 0x0017, 0x0024, 0x0025, 0x0026,
/* 1 */ 0x0032, 0x0031, 0x0018, 0x0019, 0x0010, 0x0013, 0x001f, 0x0014, 0x0016, 0x002f, 0x0011, 0x002d, 0x0015, 0x002c, 0x0002, 0x0003,
/* 2 */ 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a, 0x000b, 0x001c, 0x0001, 0x000e, 0x000f, 0x0039, 0x000c, 0x000d, 0x001a,
/* 3 */ 0x001b, 0x002b, 0x002b, 0x0027, 0x0028, 0x0029, 0x0033, 0x0034, 0x0035, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f, 0x0040,
/* 4 */ 0x0041, 0x0042, 0x0043, 0x0044, 0x0057, 0x0058, 0xE037, 0x0046, 0x0045, 0xE052, 0xE047, 0xE049, 0xE053, 0xE04F, 0xE051, 0xE04D,
/* 5 */ 0xE04B, 0xE050, 0xE048, 0x0045, 0xE035, 0x0037, 0x004a, 0x004e, 0xE01C, 0x004f, 0x0050, 0x0051, 0x004b, 0x004c, 0x004d, 0x0047,
/* 6 */ 0x0048, 0x0049, 0x0052, 0x0053, 0x0056, 0xE05D, 0xE05E, 0x0075, 0x00b7, 0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be,
/* 7 */ 0x00bf, 0x00c0, 0x00c1, 0x00c2, 0x0086, 0x008a, 0x0082, 0x0084, 0x0080, 0x0081, 0x0083, 0x0089, 0x0085, 0x0087, 0x0088, 0x0071,
/* 8 */ 0x0073, 0x0072, 0x0000, 0x0000, 0x0000, 0x0079, 0x0000, 0x0059, 0x005d, 0x007c, 0x005c, 0x005e, 0x005f, 0x0000, 0x0000, 0x0000,
/* 9 */ 0x007a, 0x007b, 0x005a, 0x005b, 0x0055, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
/* A */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
/* B */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
/* C */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
/* D */ 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
/* E */ 0x001D, 0x002A, 0x0038, 0xE05B, 0xE01D, 0x0036, 0xE038, 0xE05C, 0x00a4, 0x00a6, 0x00a5, 0x00a3, 0x00a1, 0x0073, 0x0072, 0x0071,
/* F */ 0x0096, 0x009e, 0x009f, 0x0080, 0x0088, 0x00b1, 0x00b2, 0x00b0, 0x008e, 0x0098, 0x00ad, 0x008c, 0x0000, 0x0000, 0x0000, 0x0000,
};

static struct
{
    USAGE Usage;
    ULONG ScanCode;
} CustomerScanCodes[] =
{
    { 0x00B5, 0xE019 },
    { 0x00B6, 0xE010 },
    { 0x00B7, 0xE024 },
    { 0x00CD, 0xE022 },
    { 0x00E2, 0xE020 },
    { 0x00E9, 0xE030 },
    { 0x00EA, 0xE02E },
    { 0x0183, 0xE06D },
    { 0x018A, 0xE06C },
    { 0x0192, 0xE021 },
    { 0x0194, 0xE06B },
    { 0x0221, 0xE065 },
    { 0x0223, 0xE032 },
    { 0x0224, 0xE06A },
    { 0x0225, 0xE069 },
    { 0x0226, 0xE068 },
    { 0x0227, 0xE067 },
    { 0x022A, 0xE066 },
};

#define NTOHS(n) (((((unsigned short)(n) & 0xFF)) << 8) | (((unsigned short)(n) & 0xFF00) >> 8))

static
ULONG
HidParser_BitsToBytes(
    _In_ ULONG BitCount)
{
    return (BitCount + 7) / 8;
}

static
USHORT
HidParser_ReportItemUsagePage(
    _In_ PHID_REPORT_ITEM ReportItem)
{
    return (USHORT)(ReportItem->UsageMinimum >> 16);
}

static
BOOLEAN
HidParser_ReportItemIsButtonCap(
    _In_ PHID_REPORT_ITEM ReportItem)
{
    USHORT UsagePage;

    if (ReportItem->BitCount == 0)
        return FALSE;

    UsagePage = HidParser_ReportItemUsagePage(ReportItem);

    /*
     * HID button capabilities cover selector arrays and one-bit button-like
     * variable fields. Multi-bit variable controls are value capabilities.
     */
    if (ReportItem->Array)
        return TRUE;

    if (UsagePage == HID_USAGE_PAGE_BUTTON ||
        UsagePage == HID_USAGE_PAGE_KEYBOARD)
    {
        return TRUE;
    }

    if (ReportItem->BitCount != 1)
        return FALSE;

    if (UsagePage == HID_USAGE_PAGE_CONSUMER)
        return TRUE;

    return UsagePage == HID_USAGE_PAGE_GENERIC &&
           (ReportItem->UsageMinimum & 0xFFFF) >=
               HID_USAGE_GENERIC_SYSTEM_POWER_DOWN &&
           (ReportItem->UsageMaximum & 0xFFFF) <=
               HID_USAGE_GENERIC_SYSTEM_WAKE_UP;
}

static
BOOLEAN
HidParser_GetButtonUsage(
    _In_ PHID_REPORT_ITEM ReportItem,
    _In_ ULONG Data,
    _Out_ PUSHORT Usage)
{
    ULONG UsageMinimum, UsageMaximum, Offset;
    LONG LogicalMinimum, LogicalMaximum, LogicalValue;

    UsageMinimum = ReportItem->UsageMinimum & 0xFFFF;
    UsageMaximum = ReportItem->UsageMaximum & 0xFFFF;
    if (!ReportItem->Array)
    {
        *Usage = (USHORT)UsageMinimum;
        return TRUE;
    }

    LogicalMinimum = (LONG)ReportItem->Minimum;
    LogicalMaximum = (LONG)ReportItem->Maximum;
    LogicalValue = (LONG)Data;
    if (LogicalValue < LogicalMinimum || LogicalValue > LogicalMaximum ||
        UsageMaximum < UsageMinimum)
    {
        return FALSE;
    }

    Offset = (ULONG)(LogicalValue - LogicalMinimum);
    if (Offset > UsageMaximum - UsageMinimum)
        return FALSE;

    *Usage = (USHORT)(UsageMinimum + Offset);
    return TRUE;
}

static
BOOLEAN
HidParser_ReportItemMatchesUsage(
    _In_ PHID_REPORT_ITEM ReportItem,
    _In_ USHORT UsagePage,
    _In_ USHORT Usage)
{
    USHORT CurrentUsagePage;
    USAGE UsageMin;
    USAGE UsageMax;

    CurrentUsagePage = HidParser_ReportItemUsagePage(ReportItem);
    if (UsagePage != HID_USAGE_PAGE_UNDEFINED && UsagePage != CurrentUsagePage)
        return FALSE;

    UsageMin = (USAGE)(ReportItem->UsageMinimum & 0xFFFF);
    UsageMax = (USAGE)(ReportItem->UsageMaximum & 0xFFFF);
    if (UsageMax < UsageMin)
        UsageMax = UsageMin;

    if (Usage != HID_USAGE_PAGE_UNDEFINED &&
        (Usage < UsageMin || Usage > UsageMax))
    {
        return FALSE;
    }

    return TRUE;
}

NTSTATUS
HidParser_GetCollectionUsagePage(
    IN PVOID CollectionContext,
    OUT PUSHORT Usage,
    OUT PUSHORT UsagePage)
{
    PHID_COLLECTION Collection;

    //
    // find collection
    //
    Collection = HidParser_GetCollectionFromContext(CollectionContext);
    if (!Collection)
    {
        //
        // collection not found
        //
        return HIDP_STATUS_USAGE_NOT_FOUND;
    }

    //
    // store result
    //
    *UsagePage = (Collection->Usage >> 16);
    *Usage = (Collection->Usage & 0xFFFF);
    return HIDP_STATUS_SUCCESS;
}

ULONG
HidParser_GetReportLength(
    IN PVOID CollectionContext,
    IN UCHAR ReportType)
{
    PHID_REPORT Report;
    ULONG Index, ReportCount, ReportLength = 0;

    ReportCount = HidParser_GetReportCountInCollection(CollectionContext);
    for (Index = 0; Index < ReportCount; Index++)
    {
        Report = HidParser_GetReportByIndex(CollectionContext, Index);
        if (Report->Type == ReportType && Report->ReportSize > ReportLength)
            ReportLength = Report->ReportSize;
    }

    return HidParser_BitsToBytes(ReportLength);
}

ULONG
HidParser_GetReportItemCountFromReportType(
    IN PVOID CollectionContext,
    IN UCHAR ReportType)
{
    PHID_REPORT Report;
    ULONG Index, ReportCount, ItemCount = 0;

    ReportCount = HidParser_GetReportCountInCollection(CollectionContext);
    for (Index = 0; Index < ReportCount; Index++)
    {
        Report = HidParser_GetReportByIndex(CollectionContext, Index);
        if (Report->Type == ReportType)
            ItemCount += Report->ItemCount;
    }
    return ItemCount;
}


ULONG
HidParser_GetReportItemTypeCountFromReportType(
    IN PVOID CollectionContext,
    IN UCHAR ReportType,
    IN ULONG bData)
{
    ULONG Index, ReportCount, ReportIndex;
    PHID_REPORT Report;
    ULONG ItemCount = 0;

    ReportCount = HidParser_GetReportCountInCollection(CollectionContext);
    for (ReportIndex = 0;
         ReportIndex < ReportCount;
         ReportIndex++)
    {
        Report = HidParser_GetReportByIndex(CollectionContext, ReportIndex);
        if (Report->Type != ReportType)
            continue;

        for (Index = 0; Index < Report->ItemCount; Index++)
        {
            if ((Report->Items[Index].HasData != FALSE) == (bData != FALSE))
                ItemCount++;
        }
    }

    //
    // no report items
    //
    return ItemCount;
}

ULONG
HidParser_GetMaxUsageListLengthWithReportAndPage(
    IN PVOID CollectionContext,
    IN UCHAR ReportType,
    IN USAGE  UsagePage  OPTIONAL)
{
    ULONG Index, ReportCount, ReportIndex;
    PHID_REPORT Report;
    ULONG ItemCount = 0;
    USHORT CurrentUsagePage;

    ReportCount = HidParser_GetReportCountInCollection(CollectionContext);
    for (ReportIndex = 0;
         ReportIndex < ReportCount;
         ReportIndex++)
    {
        Report = HidParser_GetReportByIndex(CollectionContext, ReportIndex);
        if (Report->Type != ReportType)
            continue;

        for (Index = 0; Index < Report->ItemCount; Index++)
        {
            if (!Report->Items[Index].HasData ||
                !HidParser_ReportItemIsButtonCap(&Report->Items[Index]))
            {
                continue;
            }

            CurrentUsagePage =
                HidParser_ReportItemUsagePage(&Report->Items[Index]);
            if (UsagePage == HID_USAGE_PAGE_UNDEFINED ||
                CurrentUsagePage == UsagePage)
            {
                ItemCount++;
            }
        }
    }

    //
    // done
    //
    return ItemCount;
}

NTSTATUS
HidParser_GetSpecificValueCapsWithReport(
    IN PVOID CollectionContext,
    IN UCHAR ReportType,
    IN USHORT UsagePage,
    IN USHORT LinkCollection,
    IN USHORT Usage,
    OUT PHIDP_VALUE_CAPS  ValueCaps,
    IN OUT PUSHORT  ValueCapsLength)
{
    ULONG Index, ReportCount, ReportIndex, DataIndexBase = 0;
    PHID_REPORT Report;
    USHORT ItemCount = 0;
    USHORT Capacity;
    USHORT CurrentUsagePage;
    USHORT CurrentUsage;
    PHID_REPORT_ITEM ReportItem;
    BOOLEAN ReportFound = FALSE;

    if (!ValueCapsLength)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    Capacity = *ValueCapsLength;
    if (!ValueCaps)
        Capacity = 0;

    ReportCount = HidParser_GetReportCountInCollection(CollectionContext);
    for (ReportIndex = 0; ReportIndex < ReportCount; ReportIndex++)
    {
        Report = HidParser_GetReportByIndex(CollectionContext, ReportIndex);
        if (Report->Type != ReportType)
            continue;
        ReportFound = TRUE;

        for (Index = 0; Index < Report->ItemCount; Index++)
        {
        ReportItem = &Report->Items[Index];
        if (HidParser_ReportItemIsButtonCap(ReportItem))
            continue;
        if (LinkCollection != HIDP_LINK_COLLECTION_UNSPECIFIED &&
            ReportItem->LinkCollection != LinkCollection)
        {
            continue;
        }

        //
        // check usage page
        //
        CurrentUsagePage = HidParser_ReportItemUsagePage(ReportItem);
        CurrentUsage = (ReportItem->UsageMinimum & 0xFFFF);

        if (HidParser_ReportItemMatchesUsage(ReportItem, UsagePage, Usage))
        {
            //
            // check if there is enough place for the caps
            //
            if (ItemCount < Capacity)
            {
                //
                // zero caps
                //
                ZeroFunction(&ValueCaps[ItemCount], sizeof(HIDP_VALUE_CAPS));

                //
                // init caps
                //
                ValueCaps[ItemCount].UsagePage = CurrentUsagePage;
                ValueCaps[ItemCount].ReportID = Report->ReportID;
                ValueCaps[ItemCount].LogicalMin = ReportItem->Minimum;
                ValueCaps[ItemCount].LogicalMax = ReportItem->Maximum;
                ValueCaps[ItemCount].IsAbsolute = !ReportItem->Relative;
                ValueCaps[ItemCount].BitField = ReportItem->BitField;
                ValueCaps[ItemCount].LinkCollection =
                    ReportItem->LinkCollection;
                HidParser_GetLinkCollectionUsagePage(
                    CollectionContext,
                    ReportItem->LinkCollection,
                    &ValueCaps[ItemCount].LinkUsage,
                    &ValueCaps[ItemCount].LinkUsagePage);
                ValueCaps[ItemCount].BitSize = ReportItem->BitCount;
                ValueCaps[ItemCount].ReportCount = 1;
                ValueCaps[ItemCount].PhysicalMin =
                    ReportItem->PhysicalMinimum;
                ValueCaps[ItemCount].PhysicalMax =
                    ReportItem->PhysicalMaximum;
                ValueCaps[ItemCount].UnitsExp = ReportItem->UnitExponent;
                ValueCaps[ItemCount].Units = ReportItem->Units;

                if (ReportItem->UsageMinimum != ReportItem->UsageMaximum)
                {
                    ValueCaps[ItemCount].IsRange = TRUE;
                    ValueCaps[ItemCount].Range.UsageMin = (ReportItem->UsageMinimum & 0xFFFF);
                    ValueCaps[ItemCount].Range.UsageMax = (ReportItem->UsageMaximum & 0xFFFF);
                    ValueCaps[ItemCount].Range.DataIndexMin =
                        DataIndexBase + Index;
                    ValueCaps[ItemCount].Range.DataIndexMax =
                        DataIndexBase + Index;
                }
                else
                {
                    ValueCaps[ItemCount].NotRange.Usage = CurrentUsage;
                    ValueCaps[ItemCount].NotRange.Reserved1 = CurrentUsage;
                    ValueCaps[ItemCount].NotRange.DataIndex =
                        DataIndexBase + Index;
                    ValueCaps[ItemCount].NotRange.Reserved4 =
                        DataIndexBase + Index;
                }

                //
                // FIXME: FILLMEIN
                //
            }


            //
            // found item
            //
            ItemCount++;
        }
        }
        DataIndexBase += Report->ItemCount;
    }

    if (!ReportFound)
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;

    //
    // store result
    //
    *ValueCapsLength = ItemCount;

    if (!ItemCount)
        return HIDP_STATUS_USAGE_NOT_FOUND;

    if (ItemCount > Capacity)
        return HIDP_STATUS_BUFFER_TOO_SMALL;

    return HIDP_STATUS_SUCCESS;
}

static
BOOLEAN
HidParser_ButtonCapCanMerge(
    IN PHIDP_BUTTON_CAPS ButtonCaps,
    IN USAGE UsagePage,
    IN USAGE UsageMin,
    IN USAGE UsageMax,
    IN UCHAR ReportID,
    IN USHORT LinkCollection,
    IN USHORT BitField,
    IN BOOLEAN IsAbsolute)
{
    USAGE CurrentMax;

    if (ButtonCaps->UsagePage != UsagePage ||
        ButtonCaps->ReportID != ReportID ||
        ButtonCaps->LinkCollection != LinkCollection ||
        ButtonCaps->BitField != BitField ||
        ButtonCaps->IsAbsolute != IsAbsolute)
    {
        return FALSE;
    }

    CurrentMax = ButtonCaps->IsRange ? ButtonCaps->Range.UsageMax
                                     : ButtonCaps->NotRange.Usage;

    return (UsageMin <= CurrentMax + 1);
}

static
VOID
HidParser_ExtendButtonCapRange(
    IN OUT PHIDP_BUTTON_CAPS ButtonCaps,
    IN USAGE UsageMax,
    IN USHORT DataIndex)
{
    if (!ButtonCaps->IsRange)
    {
        ButtonCaps->IsRange = TRUE;
        ButtonCaps->Range.UsageMin = ButtonCaps->NotRange.Usage;
        ButtonCaps->Range.UsageMax = ButtonCaps->NotRange.Usage;
        ButtonCaps->Range.DataIndexMin = ButtonCaps->NotRange.DataIndex;
        ButtonCaps->Range.DataIndexMax = ButtonCaps->NotRange.DataIndex;
    }

    if (UsageMax > ButtonCaps->Range.UsageMax)
        ButtonCaps->Range.UsageMax = UsageMax;

    if (DataIndex > ButtonCaps->Range.DataIndexMax)
        ButtonCaps->Range.DataIndexMax = DataIndex;
}

NTSTATUS
HidParser_GetSpecificButtonCapsWithReport(
    IN PVOID CollectionContext,
    IN UCHAR ReportType,
    IN USHORT UsagePage,
    IN USHORT LinkCollection,
    IN USHORT Usage,
    OUT PHIDP_BUTTON_CAPS ButtonCaps,
    IN OUT PULONG ButtonCapsLength)
{
    ULONG Index, ReportCount, ReportIndex, DataIndexBase = 0;
    ULONG ItemCount = 0;
    ULONG Capacity;
    PHID_REPORT Report;
    PHID_REPORT_ITEM ReportItem;
    PHIDP_BUTTON_CAPS CurrentCaps;
    USHORT CurrentUsagePage;
    USAGE UsageMin;
    USAGE UsageMax;
    BOOLEAN ReportFound = FALSE;

    if (!ButtonCapsLength)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    Capacity = *ButtonCapsLength;
    if (!ButtonCaps)
        Capacity = 0;

    ReportCount = HidParser_GetReportCountInCollection(CollectionContext);
    for (ReportIndex = 0; ReportIndex < ReportCount; ReportIndex++)
    {
        Report = HidParser_GetReportByIndex(CollectionContext, ReportIndex);
        if (Report->Type != ReportType)
            continue;
        ReportFound = TRUE;

        for (Index = 0; Index < Report->ItemCount; Index++)
        {
        ReportItem = &Report->Items[Index];
        if (!HidParser_ReportItemIsButtonCap(ReportItem))
            continue;
        if (LinkCollection != HIDP_LINK_COLLECTION_UNSPECIFIED &&
            ReportItem->LinkCollection != LinkCollection)
        {
            continue;
        }

        CurrentUsagePage = HidParser_ReportItemUsagePage(ReportItem);
        if (!HidParser_ReportItemMatchesUsage(ReportItem, UsagePage, Usage))
            continue;

        UsageMin = (USAGE)(ReportItem->UsageMinimum & 0xFFFF);
        UsageMax = (USAGE)(ReportItem->UsageMaximum & 0xFFFF);
        if (UsageMax < UsageMin)
            UsageMax = UsageMin;

        if (ItemCount > 0 &&
            ItemCount <= Capacity &&
            HidParser_ButtonCapCanMerge(&ButtonCaps[ItemCount - 1],
                                        CurrentUsagePage,
                                        UsageMin,
                                        UsageMax,
                                        Report->ReportID,
                                        ReportItem->LinkCollection,
                                        ReportItem->BitField,
                                        !ReportItem->Relative))
        {
            if (ItemCount <= Capacity)
                HidParser_ExtendButtonCapRange(&ButtonCaps[ItemCount - 1],
                                               UsageMax,
                                               (USHORT)(DataIndexBase + Index));
            continue;
        }

        if (ItemCount < Capacity)
        {
            CurrentCaps = &ButtonCaps[ItemCount];
            ZeroFunction(CurrentCaps, sizeof(HIDP_BUTTON_CAPS));

            CurrentCaps->UsagePage = CurrentUsagePage;
            CurrentCaps->ReportID = Report->ReportID;
            CurrentCaps->BitField = ReportItem->BitField;
            CurrentCaps->LinkCollection = ReportItem->LinkCollection;
            HidParser_GetLinkCollectionUsagePage(
                CollectionContext,
                ReportItem->LinkCollection,
                &CurrentCaps->LinkUsage,
                &CurrentCaps->LinkUsagePage);
            CurrentCaps->IsAbsolute = !ReportItem->Relative;

            if (UsageMin != UsageMax)
            {
                CurrentCaps->IsRange = TRUE;
                CurrentCaps->Range.UsageMin = UsageMin;
                CurrentCaps->Range.UsageMax = UsageMax;
                CurrentCaps->Range.DataIndexMin =
                    (USHORT)(DataIndexBase + Index);
                CurrentCaps->Range.DataIndexMax =
                    (USHORT)(DataIndexBase + Index);
            }
            else
            {
                CurrentCaps->NotRange.Usage = UsageMin;
                CurrentCaps->NotRange.Reserved1 = UsageMin;
                CurrentCaps->NotRange.DataIndex =
                    (USHORT)(DataIndexBase + Index);
                CurrentCaps->NotRange.Reserved4 =
                    (USHORT)(DataIndexBase + Index);
            }
        }

        ItemCount++;
    }
        DataIndexBase += Report->ItemCount;
    }

    if (!ReportFound)
    {
        *ButtonCapsLength = 0;
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    }

    *ButtonCapsLength = ItemCount;

    if (!ItemCount)
        return HIDP_STATUS_USAGE_NOT_FOUND;

    if (ItemCount > Capacity)
        return HIDP_STATUS_BUFFER_TOO_SMALL;

    return HIDP_STATUS_SUCCESS;
}

NTSTATUS
HidParser_GetUsagesWithReport(
    IN PVOID CollectionContext,
    IN UCHAR  ReportType,
    IN USAGE  UsagePage,
    IN USHORT LinkCollection,
    OUT USAGE  *UsageList,
    IN OUT PULONG UsageLength,
    IN PCHAR  ReportDescriptor,
    IN ULONG  ReportDescriptorLength)
{
    ULONG Index;
    PHID_REPORT Report;
    ULONG ItemCount = 0;
    USHORT CurrentUsagePage;
    PHID_REPORT_ITEM ReportItem;
    UCHAR Activated;
    ULONG Data;
    USHORT ActiveUsage;
    PUSAGE_AND_PAGE UsageAndPage = NULL;

    if (ReportDescriptor == NULL || ReportDescriptorLength == 0)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;

    Report = HidParser_GetReportInCollectionById(CollectionContext,
                                                  ReportType,
                                                  ReportDescriptor[0]);
    if (!Report)
    {
        //
        // no such report
        //
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    }

    if (HidParser_BitsToBytes(Report->ReportSize) >
        ReportDescriptorLength - 1)
    {
        //
        // invalid report descriptor length
        //
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }

    //
    // cast to usage and page
    //
    if (UsagePage == HID_USAGE_PAGE_UNDEFINED)
    {
        //
        // the caller requested any set usages
        //
        UsageAndPage = (PUSAGE_AND_PAGE)UsageList;
    }

    for(Index = 0; Index < Report->ItemCount; Index++)
    {
        //
        // get report item
        //
        ReportItem = &Report->Items[Index];

        //
        // does it have data
        //
        if (!ReportItem->HasData ||
            !HidParser_ReportItemIsButtonCap(ReportItem))
            continue;
        if (LinkCollection != HIDP_LINK_COLLECTION_UNSPECIFIED &&
            ReportItem->LinkCollection != LinkCollection)
        {
            continue;
        }

        //
        // check usage page
        //
        CurrentUsagePage = HidParser_ReportItemUsagePage(ReportItem);

        if (UsagePage != HID_USAGE_PAGE_UNDEFINED)
        {
            //
            // does usage match
            //
            if (UsagePage != CurrentUsagePage)
                continue;
        }

        //
        // check if the specified usage is activated
        //
        ASSERT(ReportItem->ByteOffset < ReportDescriptorLength);
        if (ReportItem->BitCount > sizeof(Data) * 8)
            continue;

        //
        // one extra shift for skipping the prepended report id
        //
        Data = 0;
        CopyFunction(&Data,
                     &ReportDescriptor[ReportItem->ByteOffset + 1],
                     min(sizeof(Data),
                         ReportDescriptorLength -
                             (ReportItem->ByteOffset + 1)));

        //
        // shift data
        //
        Data >>= ReportItem->Shift;

        //
        // clear unwanted bits
        //
        Data &= ReportItem->Mask;

        //
        // is it activated
        //
        Activated = (Data != 0);

        if (!Activated)
            continue;

        if (!HidParser_GetButtonUsage(ReportItem, Data, &ActiveUsage))
            continue;

        //
        // is there enough space for the usage
        //
        if (ItemCount >= *UsageLength)
        {
            ItemCount++;
            continue;
        }

        if (UsagePage != HID_USAGE_PAGE_UNDEFINED)
        {
            //
            // store item
            //
            UsageList[ItemCount] = ActiveUsage;
        }
        else
        {
            //
            // store usage and page
            //
            UsageAndPage[ItemCount].Usage = ActiveUsage;
            UsageAndPage[ItemCount].UsagePage = CurrentUsagePage;
        }
        ItemCount++;
    }

    if (ItemCount > *UsageLength)
    {
        //
        // list too small
        //
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    }

    if (UsagePage == HID_USAGE_PAGE_UNDEFINED)
    {
        //
        // success, clear rest of array
        //
        ZeroFunction(&UsageAndPage[ItemCount], (*UsageLength - ItemCount) * sizeof(USAGE_AND_PAGE));
    }
    else
    {
        //
        // success, clear rest of array
        //
        ZeroFunction(&UsageList[ItemCount], (*UsageLength - ItemCount) * sizeof(USAGE));
    }


    //
    // store result size
    //
    *UsageLength = ItemCount;

    //
    // done
    //
    return HIDP_STATUS_SUCCESS;
}

ULONG
HidParser_UsesReportId(
    IN PVOID CollectionContext,
    IN UCHAR  ReportType)
{
    PHID_REPORT Report;
    ULONG Index, ReportCount;

    ReportCount = HidParser_GetReportCountInCollection(CollectionContext);
    for (Index = 0; Index < ReportCount; Index++)
    {
        Report = HidParser_GetReportByIndex(CollectionContext, Index);
        if (Report->Type == ReportType && Report->ReportID != 0)
            return TRUE;
    }
    return FALSE;
}

NTSTATUS
HidParser_GetUsageValueWithReport(
    IN PVOID CollectionContext,
    IN UCHAR ReportType,
    IN USAGE UsagePage,
    IN USHORT LinkCollection,
    IN USAGE  Usage,
    OUT PULONG UsageValue,
    IN PCHAR ReportDescriptor,
    IN ULONG ReportDescriptorLength)
{
    ULONG Index;
    PHID_REPORT Report;
    USHORT CurrentUsagePage;
    PHID_REPORT_ITEM ReportItem;
    ULONG Data;

    if (ReportDescriptor == NULL || ReportDescriptorLength == 0)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;

    Report = HidParser_GetReportInCollectionById(CollectionContext,
                                                  ReportType,
                                                  ReportDescriptor[0]);
    if (!Report)
    {
        //
        // no such report
        //
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    }

    if (HidParser_BitsToBytes(Report->ReportSize) >
        ReportDescriptorLength - 1)
    {
        //
        // invalid report descriptor length
        //
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }

    for (Index = 0; Index < Report->ItemCount; Index++)
    {
        //
        // get report item
        //
        ReportItem = &Report->Items[Index];
        if (HidParser_ReportItemIsButtonCap(ReportItem))
            continue;
        if (LinkCollection != HIDP_LINK_COLLECTION_UNSPECIFIED &&
            ReportItem->LinkCollection != LinkCollection)
        {
            continue;
        }

        //
        // check usage page
        //
        CurrentUsagePage = HidParser_ReportItemUsagePage(ReportItem);

        //
        // does usage page match
        //
        if (UsagePage != CurrentUsagePage)
            continue;

        //
        // does the usage match
        //
        if (Usage != (ReportItem->UsageMinimum & 0xFFFF))
            continue;

        //
        // check if the specified usage is activated
        //
        ASSERT(ReportItem->ByteOffset < ReportDescriptorLength);

        //
        // one extra shift for skipping the prepended report id
        //
        Data = 0;
        CopyFunction(&Data, &ReportDescriptor[ReportItem->ByteOffset + 1], min(sizeof(ULONG), ReportDescriptorLength - (ReportItem->ByteOffset + 1)));

        //
        // shift data
        //
        Data >>= ReportItem->Shift;

        //
        // clear unwanted bits
        //
        Data &= ReportItem->Mask;

        //
        // store result
        //
        *UsageValue = Data;
        return HIDP_STATUS_SUCCESS;
    }

    //
    // usage not found
    //
    return HIDP_STATUS_USAGE_NOT_FOUND;
}

NTSTATUS
HidParser_SetUsageValueWithReport(
    IN PVOID CollectionContext,
    IN UCHAR ReportType,
    IN USAGE UsagePage,
    IN USHORT LinkCollection,
    IN USAGE Usage,
    IN ULONG UsageValue,
    IN OUT PCHAR ReportDescriptor,
    IN ULONG ReportDescriptorLength)
{
    PHID_REPORT_ITEM ReportItem;
    PHID_REPORT Report;
    ULONG Bit, DataBit;
    ULONG Index;

    if (ReportDescriptor == NULL || ReportDescriptorLength == 0)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;

    Report = HidParser_GetReportInCollectionById(CollectionContext,
                                                  ReportType,
                                                  ReportDescriptor[0]);
    if (Report == NULL)
        return HIDP_STATUS_INCOMPATIBLE_REPORT_ID;

    if (HidParser_BitsToBytes(Report->ReportSize) >
        ReportDescriptorLength - 1)
    {
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }

    for (Index = 0; Index < Report->ItemCount; Index++)
    {
        ReportItem = &Report->Items[Index];
        if (HidParser_ReportItemIsButtonCap(ReportItem))
            continue;
        if (LinkCollection != HIDP_LINK_COLLECTION_UNSPECIFIED &&
            ReportItem->LinkCollection != LinkCollection)
        {
            continue;
        }
        if (!HidParser_ReportItemMatchesUsage(ReportItem,
                                              UsagePage,
                                              Usage))
        {
            continue;
        }
        if (ReportItem->Array)
            return HIDP_STATUS_IS_VALUE_ARRAY;
        if (((LONG)ReportItem->Minimum < 0 &&
             ((LONG)UsageValue < (LONG)ReportItem->Minimum ||
              (LONG)UsageValue > (LONG)ReportItem->Maximum)) ||
            ((LONG)ReportItem->Minimum >= 0 &&
             (UsageValue < ReportItem->Minimum ||
              UsageValue > ReportItem->Maximum)))
        {
            return HIDP_STATUS_VALUE_OUT_OF_RANGE;
        }
        if (ReportItem->BitCount > sizeof(UsageValue) * 8)
            return HIDP_STATUS_VALUE_OUT_OF_RANGE;

        for (Bit = 0; Bit < ReportItem->BitCount; Bit++)
        {
            DataBit = ReportItem->Shift + Bit;
            if (UsageValue & (1UL << Bit))
            {
                ReportDescriptor[ReportItem->ByteOffset + 1 + DataBit / 8] |=
                    (UCHAR)(1U << (DataBit % 8));
            }
            else
            {
                ReportDescriptor[ReportItem->ByteOffset + 1 + DataBit / 8] &=
                    (UCHAR)~(1U << (DataBit % 8));
            }
        }
        return HIDP_STATUS_SUCCESS;
    }

    return HIDP_STATUS_USAGE_NOT_FOUND;
}



NTSTATUS
HidParser_GetScaledUsageValueWithReport(
    IN PVOID CollectionContext,
    IN UCHAR ReportType,
    IN USAGE UsagePage,
    IN USHORT LinkCollection,
    IN USAGE  Usage,
    OUT PLONG UsageValue,
    IN PCHAR ReportDescriptor,
    IN ULONG ReportDescriptorLength)
{
    ULONG Index;
    PHID_REPORT Report;
    USHORT CurrentUsagePage;
    PHID_REPORT_ITEM ReportItem;
    ULONG Data;

    if (ReportDescriptor == NULL || ReportDescriptorLength == 0)
        return HIDP_STATUS_INVALID_REPORT_LENGTH;

    Report = HidParser_GetReportInCollectionById(CollectionContext,
                                                  ReportType,
                                                  ReportDescriptor[0]);
    if (!Report)
    {
        //
        // no such report
        //
        return HIDP_STATUS_REPORT_DOES_NOT_EXIST;
    }

    if (HidParser_BitsToBytes(Report->ReportSize) >
        ReportDescriptorLength - 1)
    {
        //
        // invalid report descriptor length
        //
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }

    for (Index = 0; Index < Report->ItemCount; Index++)
    {
        //
        // get report item
        //
        ReportItem = &Report->Items[Index];
        if (HidParser_ReportItemIsButtonCap(ReportItem))
            continue;
        if (LinkCollection != HIDP_LINK_COLLECTION_UNSPECIFIED &&
            ReportItem->LinkCollection != LinkCollection)
        {
            continue;
        }

        //
        // check usage page
        //
        CurrentUsagePage = HidParser_ReportItemUsagePage(ReportItem);

        //
        // does usage page match
        //
        if (UsagePage != CurrentUsagePage)
            continue;

        //
        // does the usage match
        //
        if (Usage != (ReportItem->UsageMinimum & 0xFFFF))
            continue;

        //
        // check if the specified usage is activated
        //
        ASSERT(ReportItem->ByteOffset < ReportDescriptorLength);

        //
        // one extra shift for skipping the prepended report id
        //
        Data = 0;
        CopyFunction(&Data, &ReportDescriptor[ReportItem->ByteOffset + 1], min(sizeof(ULONG), ReportDescriptorLength - (ReportItem->ByteOffset + 1)));

        //
        // shift data
        //
        Data >>= ReportItem->Shift;

        //
        // clear unwanted bits
        //
        Data &= ReportItem->Mask;

        if (ReportItem->Minimum > ReportItem->Maximum)
        {
            //
            // logical boundaries are signed values
            //

            // FIXME: scale with physical min/max
            if ((Data & ~(ReportItem->Mask >> 1)) != 0)
            {
                Data |= ~ReportItem->Mask;
            }
        }
        else
        {
            // logical boundaries are absolute values
            return HIDP_STATUS_BAD_LOG_PHY_VALUES;
        }

        //
        // store result
        //
        *UsageValue = Data;
        return HIDP_STATUS_SUCCESS;
    }

    //
    // usage not found
    //
    return HIDP_STATUS_USAGE_NOT_FOUND;
}

ULONG
HidParser_GetScanCodeFromKbdUsage(
    IN USAGE Usage)
{
    if (Usage < sizeof(KeyboardScanCodes) / sizeof(KeyboardScanCodes[0]))
    {
        //
        // valid usage
        //
        return KeyboardScanCodes[Usage];
    }

    //
    // invalid usage
    //
    return 0;
}

ULONG
HidParser_GetScanCodeFromCustUsage(
    IN USAGE Usage)
{
    ULONG i;

    //
    // find usage in array
    //
    for (i = 0; i < sizeof(CustomerScanCodes) / sizeof(CustomerScanCodes[0]); ++i)
    {
        if (CustomerScanCodes[i].Usage == Usage)
        {
            //
            // valid usage
            //
            return CustomerScanCodes[i].ScanCode;
        }
    }

    //
    // invalid usage
    //
    return 0;
}

VOID
HidParser_DispatchKey(
    IN PCHAR ScanCodes,
    IN HIDP_KEYBOARD_DIRECTION KeyAction,
    IN PHIDP_INSERT_SCANCODES InsertCodesProcedure,
    IN PVOID InsertCodesContext)
{
    ULONG Index;
    ULONG Length = 0;

    //
    // count code length
    //
    for(Index = 0; Index < sizeof(ULONG); Index++)
    {
        if (ScanCodes[Index] == 0)
        {
            //
            // last scan code
            //
            break;
        }

        //
        // is this a key break
        //
        if (KeyAction == HidP_Keyboard_Break)
        {
            //
            // add break - see USB HID to PS/2 Scan Code Translation Table
            //
            ScanCodes[Index] |= 0x80;
        }

        //
        // more scan counts
        //
        Length++;
    }

    if (Length > 0)
    {
         //
         // dispatch scan codes
         //
         InsertCodesProcedure(InsertCodesContext, ScanCodes, Length);
    }
}

NTSTATUS
HidParser_TranslateKbdUsage(
    IN USAGE Usage,
    IN HIDP_KEYBOARD_DIRECTION  KeyAction,
    IN OUT PHIDP_KEYBOARD_MODIFIER_STATE  ModifierState,
    IN PHIDP_INSERT_SCANCODES  InsertCodesProcedure,
    IN PVOID  InsertCodesContext)
{
    ULONG ScanCode;
    CHAR FakeShift[] = {0xE0, 0x2A, 0x00};
    CHAR FakeCtrl[] = {0xE1, 0x1D, 0x00};

    //
    // get scan code
    //
    ScanCode = HidParser_GetScanCodeFromKbdUsage(Usage);
    if (!ScanCode)
    {
        //
        // invalid lookup or no scan code available
        //
        DPRINT1("No Scan code for Usage %x\n", Usage);
        return HIDP_STATUS_I8042_TRANS_UNKNOWN;
    }

    if (ScanCode & 0xFF00)
    {
        //
        // swap scan code
        //
        ScanCode = NTOHS(ScanCode);
    }

    if (Usage == 0x46 && KeyAction == HidP_Keyboard_Make)
    {
        // Print Screen generates additional FakeShift
        HidParser_DispatchKey(FakeShift, KeyAction, InsertCodesProcedure, InsertCodesContext);
    }

    if (Usage == 0x48)
    {
        // Pause/Break generates additional FakeCtrl. Note: it's always before key press/release.
        HidParser_DispatchKey(FakeCtrl, KeyAction, InsertCodesProcedure, InsertCodesContext);
    }

    //
    // FIXME: translate modifier states
    //
    HidParser_DispatchKey((PCHAR)&ScanCode, KeyAction, InsertCodesProcedure, InsertCodesContext);

    if (Usage == 0x46 && KeyAction == HidP_Keyboard_Break)
    {
        // Print Screen generates additional FakeShift
        HidParser_DispatchKey(FakeShift, KeyAction, InsertCodesProcedure, InsertCodesContext);
    }

    //
    // done
    //
    return HIDP_STATUS_SUCCESS;
}

NTSTATUS
HidParser_TranslateCustUsage(
    IN USAGE Usage,
    IN HIDP_KEYBOARD_DIRECTION  KeyAction,
    IN OUT PHIDP_KEYBOARD_MODIFIER_STATE  ModifierState,
    IN PHIDP_INSERT_SCANCODES  InsertCodesProcedure,
    IN PVOID  InsertCodesContext)
{
    ULONG ScanCode;

    //
    // get scan code
    //
    ScanCode = HidParser_GetScanCodeFromCustUsage(Usage);
    if (!ScanCode)
    {
        //
        // invalid lookup or no scan code available
        //
        DPRINT1("No Scan code for Usage %x\n", Usage);
        return HIDP_STATUS_I8042_TRANS_UNKNOWN;
    }

    if (ScanCode & 0xFF00)
    {
        //
        // swap scan code
        //
        ScanCode = NTOHS(ScanCode);
    }

    //
    // FIXME: translate modifier states
    //
    HidParser_DispatchKey((PCHAR)&ScanCode, KeyAction, InsertCodesProcedure, InsertCodesContext);

    //
    // done
    //
    return HIDP_STATUS_SUCCESS;
}

NTSTATUS
HidParser_TranslateSystemUsage(
    IN USAGE Usage,
    IN HIDP_KEYBOARD_DIRECTION KeyAction,
    IN PHIDP_INSERT_SCANCODES InsertCodesProcedure,
    IN PVOID InsertCodesContext)
{
    ULONG ScanCode;

    switch (Usage)
    {
        case HID_USAGE_GENERIC_SYSTEM_POWER_DOWN:
            ScanCode = 0xE05E;
            break;
        case HID_USAGE_GENERIC_SYSTEM_SLEEP:
            ScanCode = 0xE05F;
            break;
        case HID_USAGE_GENERIC_SYSTEM_WAKE_UP:
            ScanCode = 0xE063;
            break;
        default:
            return HIDP_STATUS_I8042_TRANS_UNKNOWN;
    }

    ScanCode = NTOHS(ScanCode);
    HidParser_DispatchKey((PCHAR)&ScanCode,
                          KeyAction,
                          InsertCodesProcedure,
                          InsertCodesContext);
    return HIDP_STATUS_SUCCESS;
}
