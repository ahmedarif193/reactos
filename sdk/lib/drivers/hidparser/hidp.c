#define _HIDPI_
#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidpddi.h>

#include "hidparser.h"
#include "hidp.h"

#define UNIMPLEMENTED DebugFunction("%s is UNIMPLEMENTED\n", __FUNCTION__)

C_ASSERT(FIELD_OFFSET(struct hid_preparsed_data, value_caps) == 44);
C_ASSERT(sizeof(struct hid_value_caps) == 104);
C_ASSERT(sizeof(struct hid_collection_node) == 16);
C_ASSERT(sizeof(HIDP_REACTOS_PREPARSED_DATA) <= sizeof(struct hid_value_caps));

static
ULONG_PTR
HidP_AlignUp(
    IN ULONG_PTR Value,
    IN ULONG_PTR Alignment)
{
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

typedef struct _HIDP_REPORT_CAPS_COUNTS
{
    USHORT Value;
    USHORT Button;
} HIDP_REPORT_CAPS_COUNTS, *PHIDP_REPORT_CAPS_COUNTS;

static
BOOLEAN
HidP_AddSize(
    IN ULONG_PTR Left,
    IN ULONG_PTR Right,
    OUT PULONG_PTR Result)
{
    if (Left > (ULONG_PTR)-1 - Right)
        return FALSE;

    *Result = Left + Right;
    return TRUE;
}

static
BOOLEAN
HidP_MultiplySize(
    IN ULONG_PTR Left,
    IN ULONG_PTR Right,
    OUT PULONG_PTR Result)
{
    if (Left && Right > (ULONG_PTR)-1 / Left)
        return FALSE;

    *Result = Left * Right;
    return TRUE;
}

static
BOOLEAN
HidP_NoCapsStatus(
    IN NTSTATUS Status)
{
    return Status == HIDP_STATUS_USAGE_NOT_FOUND || Status == HIDP_STATUS_REPORT_DOES_NOT_EXIST;
}

static
BOOLEAN
HidP_QueryReportCapsCounts(
    IN PHIDP_PREPARSED_DATA NativeData,
    IN HIDP_REPORT_TYPE ReportType,
    OUT PHIDP_REPORT_CAPS_COUNTS Counts)
{
    NTSTATUS Status;

    Counts->Value = 0;
    Status = HidP_GetSpecificValueCaps(ReportType, HID_USAGE_PAGE_UNDEFINED, HIDP_LINK_COLLECTION_UNSPECIFIED, 0, NULL, &Counts->Value, NativeData);
    if (HidP_NoCapsStatus(Status))
        Counts->Value = 0;
    else if (Status != HIDP_STATUS_SUCCESS && Status != HIDP_STATUS_BUFFER_TOO_SMALL)
        return FALSE;

    Counts->Button = 0;
    Status = HidP_GetSpecificButtonCaps(ReportType, HID_USAGE_PAGE_UNDEFINED, HIDP_LINK_COLLECTION_UNSPECIFIED, 0, NULL, &Counts->Button, NativeData);
    if (HidP_NoCapsStatus(Status))
        Counts->Button = 0;
    else if (Status != HIDP_STATUS_SUCCESS && Status != HIDP_STATUS_BUFFER_TOO_SMALL)
        return FALSE;

    return TRUE;
}

static
VOID
HidP_CopyValueCaps(
    OUT struct hid_value_caps *Destination,
    IN PHIDP_VALUE_CAPS Source)
{
    ZeroFunction(Destination, sizeof(*Destination));

    Destination->usage_page = Source->UsagePage;
    Destination->report_id = Source->ReportID;
    Destination->bit_size = Source->BitSize;
    Destination->report_count = Source->ReportCount ? Source->ReportCount : 1;
    Destination->bit_field = Source->BitField;
    Destination->link_collection = Source->LinkCollection;
    Destination->link_usage_page = Source->LinkUsagePage;
    Destination->link_usage = Source->LinkUsage;
    Destination->logical_min = Source->LogicalMin;
    Destination->logical_max = Source->LogicalMax;
    Destination->physical_min = Source->PhysicalMin;
    Destination->physical_max = Source->PhysicalMax;
    Destination->units = Source->Units;
    Destination->units_exp = Source->UnitsExp;

    if (Source->IsAbsolute)
        Destination->flags |= HID_VALUE_CAPS_IS_ABSOLUTE;
    if (Source->IsStringRange)
        Destination->flags |= HID_VALUE_CAPS_IS_STRING_RANGE;
    if (Source->IsDesignatorRange)
        Destination->flags |= HID_VALUE_CAPS_IS_DESIGNATOR_RANGE;

    if (Source->IsRange)
    {
        Destination->flags |= HID_VALUE_CAPS_IS_RANGE;
        Destination->usage_min = Source->Range.UsageMin;
        Destination->usage_max = Source->Range.UsageMax;
        Destination->string_min = Source->Range.StringMin;
        Destination->string_max = Source->Range.StringMax;
        Destination->designator_min = Source->Range.DesignatorMin;
        Destination->designator_max = Source->Range.DesignatorMax;
        Destination->data_index_min = Source->Range.DataIndexMin;
        Destination->data_index_max = Source->Range.DataIndexMax;
    }
    else
    {
        Destination->usage_min = Source->NotRange.Usage;
        Destination->usage_max = Source->NotRange.Usage;
        Destination->string_min = Destination->string_max = Source->NotRange.StringIndex;
        Destination->designator_min = Destination->designator_max = Source->NotRange.DesignatorIndex;
        Destination->data_index_min = Destination->data_index_max = Source->NotRange.DataIndex;
    }
}

static
VOID
HidP_CopyButtonCaps(
    OUT struct hid_value_caps *Destination,
    IN PHIDP_BUTTON_CAPS Source)
{
    ZeroFunction(Destination, sizeof(*Destination));

    Destination->usage_page = Source->UsagePage;
    Destination->report_id = Source->ReportID;
    Destination->bit_size = 1;
    Destination->report_count = 1;
    Destination->bit_field = Source->BitField;
    Destination->link_collection = Source->LinkCollection;
    Destination->link_usage_page = Source->LinkUsagePage;
    Destination->link_usage = Source->LinkUsage;
    Destination->logical_min = 0;
    Destination->logical_max = 1;
    Destination->physical_min = 0;
    Destination->physical_max = 1;
    Destination->flags = HID_VALUE_CAPS_IS_BUTTON;

    if (Source->IsAbsolute)
        Destination->flags |= HID_VALUE_CAPS_IS_ABSOLUTE;
    if (Source->IsStringRange)
        Destination->flags |= HID_VALUE_CAPS_IS_STRING_RANGE;
    if (Source->IsDesignatorRange)
        Destination->flags |= HID_VALUE_CAPS_IS_DESIGNATOR_RANGE;

    if (Source->IsRange)
    {
        Destination->flags |= HID_VALUE_CAPS_IS_RANGE;
        Destination->usage_min = Source->Range.UsageMin;
        Destination->usage_max = Source->Range.UsageMax;
        Destination->string_min = Source->Range.StringMin;
        Destination->string_max = Source->Range.StringMax;
        Destination->designator_min = Source->Range.DesignatorMin;
        Destination->designator_max = Source->Range.DesignatorMax;
        Destination->data_index_min = Source->Range.DataIndexMin;
        Destination->data_index_max = Source->Range.DataIndexMax;
        Destination->report_count = Source->Range.UsageMax - Source->Range.UsageMin + 1;
    }
    else
    {
        Destination->usage_min = Source->NotRange.Usage;
        Destination->usage_max = Source->NotRange.Usage;
        Destination->string_min = Destination->string_max = Source->NotRange.StringIndex;
        Destination->designator_min = Destination->designator_max = Source->NotRange.DesignatorIndex;
        Destination->data_index_min = Destination->data_index_max = Source->NotRange.DataIndex;
    }
}

static
BOOLEAN
HidP_CopyReportCaps(
    IN PHIDP_PREPARSED_DATA NativeData,
    IN HIDP_REPORT_TYPE ReportType,
    IN HIDP_REPORT_CAPS_COUNTS Counts,
    IN OUT struct hid_value_caps **Destination)
{
    PHIDP_VALUE_CAPS ValueCaps;
    PHIDP_BUTTON_CAPS ButtonCaps;
    USHORT Count, Index;
    NTSTATUS Status;

    if (Counts.Value)
    {
        Count = Counts.Value;
        ValueCaps = AllocFunction(Count * sizeof(*ValueCaps));
        if (!ValueCaps)
            return FALSE;

        Status = HidP_GetSpecificValueCaps(ReportType, HID_USAGE_PAGE_UNDEFINED, HIDP_LINK_COLLECTION_UNSPECIFIED, 0, ValueCaps, &Count, NativeData);
        if (Status != HIDP_STATUS_SUCCESS)
        {
            FreeFunction(ValueCaps);
            return FALSE;
        }

        for (Index = 0; Index < Count; Index++)
            HidP_CopyValueCaps((*Destination)++, &ValueCaps[Index]);

        FreeFunction(ValueCaps);
    }

    if (Counts.Button)
    {
        Count = Counts.Button;
        ButtonCaps = AllocFunction(Count * sizeof(*ButtonCaps));
        if (!ButtonCaps)
            return FALSE;

        Status = HidP_GetSpecificButtonCaps(ReportType, HID_USAGE_PAGE_UNDEFINED, HIDP_LINK_COLLECTION_UNSPECIFIED, 0, ButtonCaps, &Count, NativeData);
        if (Status != HIDP_STATUS_SUCCESS)
        {
            FreeFunction(ButtonCaps);
            return FALSE;
        }

        for (Index = 0; Index < Count; Index++)
            HidP_CopyButtonCaps((*Destination)++, &ButtonCaps[Index]);

        FreeFunction(ButtonCaps);
    }

    return TRUE;
}

BOOLEAN
HidP_CreatePreparsedData(
    IN PHIDP_PREPARSED_DATA NativeData,
    IN ULONG NativeSize,
    OUT PHIDP_PREPARSED_DATA *PreparsedData,
    OUT PULONG PublicSize OPTIONAL,
    OUT PULONG TotalSize OPTIONAL)
{
    HIDP_CAPS Caps;
    HIDP_REPORT_CAPS_COUNTS InputCounts, OutputCounts, FeatureCounts;
    PHIDP_LINK_COLLECTION_NODE LinkNodes = NULL;
    struct hid_preparsed_data *PublicData;
    struct hid_value_caps *ValueCaps;
    struct hid_collection_node *CollectionNodes;
    PHIDP_REACTOS_PREPARSED_DATA ReactOSData;
    NTSTATUS Status;
    ULONG NodeCount, Index;
    ULONG_PTR InputCaps, OutputCaps, FeatureCaps, TotalCaps;
    ULONG_PTR CapsSize, NodesSize, PrefixSize, PublicDataSize, NativeOffset, AllocationSize;

    if (!NativeData || !NativeSize || !PreparsedData)
        return FALSE;

    Status = HidP_GetCaps(NativeData, &Caps);
    if (Status != HIDP_STATUS_SUCCESS)
        return FALSE;

    if (!HidP_QueryReportCapsCounts(NativeData, HidP_Input, &InputCounts) || !HidP_QueryReportCapsCounts(NativeData, HidP_Output, &OutputCounts) || !HidP_QueryReportCapsCounts(NativeData, HidP_Feature, &FeatureCounts))
        return FALSE;

    InputCaps = InputCounts.Value + InputCounts.Button;
    OutputCaps = OutputCounts.Value + OutputCounts.Button;
    FeatureCaps = FeatureCounts.Value + FeatureCounts.Button;
    if (InputCaps > MAXUSHORT || OutputCaps > MAXUSHORT || FeatureCaps > MAXUSHORT)
        return FALSE;

    TotalCaps = InputCaps + OutputCaps + FeatureCaps;
    if (!HidP_MultiplySize(TotalCaps, sizeof(struct hid_value_caps), &CapsSize) || CapsSize > MAXUSHORT)
        return FALSE;

    NodeCount = Caps.NumberLinkCollectionNodes;
    if (NodeCount)
    {
        ULONG RequestedNodeCount = NodeCount;

        LinkNodes = AllocFunction(NodeCount * sizeof(*LinkNodes));
        if (!LinkNodes)
            return FALSE;

        Status = HidP_GetLinkCollectionNodes(LinkNodes, &RequestedNodeCount, NativeData);
        if (Status != HIDP_STATUS_SUCCESS)
        {
            FreeFunction(LinkNodes);
            return FALSE;
        }
        NodeCount = RequestedNodeCount;
    }

    if (NodeCount > MAXUSHORT || !HidP_MultiplySize(NodeCount, sizeof(struct hid_collection_node), &NodesSize) || !HidP_AddSize(FIELD_OFFSET(struct hid_preparsed_data, value_caps), CapsSize, &PrefixSize) || !HidP_AddSize(PrefixSize, NodesSize, &PrefixSize) || !HidP_AddSize(PrefixSize, sizeof(struct hid_value_caps), &PublicDataSize))
    {
        FreeFunction(LinkNodes);
        return FALSE;
    }

    if (PublicDataSize > MAXUSHORT)
    {
        FreeFunction(LinkNodes);
        return FALSE;
    }

    NativeOffset = HidP_AlignUp(PublicDataSize, sizeof(PVOID));
    if (!HidP_AddSize(NativeOffset, NativeSize, &AllocationSize) || AllocationSize > MAXULONG)
    {
        FreeFunction(LinkNodes);
        return FALSE;
    }

    PublicData = AllocFunction((ULONG)AllocationSize);
    if (!PublicData)
    {
        FreeFunction(LinkNodes);
        return FALSE;
    }
    ZeroFunction(PublicData, (ULONG)AllocationSize);

    *(PULONG)&PublicData->magic[0] = HIDP_KDR_MAGIC_FIRST;
    *(PULONG)&PublicData->magic[4] = HIDP_KDR_MAGIC_SECOND;
    PublicData->usage = Caps.Usage;
    PublicData->usage_page = Caps.UsagePage;
    PublicData->input_caps_start = 0;
    PublicData->input_caps_count = (USHORT)InputCaps;
    PublicData->input_caps_end = PublicData->input_caps_start + PublicData->input_caps_count;
    PublicData->input_report_byte_length = Caps.InputReportByteLength;
    PublicData->output_caps_start = PublicData->input_caps_end;
    PublicData->output_caps_count = (USHORT)OutputCaps;
    PublicData->output_caps_end = PublicData->output_caps_start + PublicData->output_caps_count;
    PublicData->output_report_byte_length = Caps.OutputReportByteLength;
    PublicData->feature_caps_start = PublicData->output_caps_end;
    PublicData->feature_caps_count = (USHORT)FeatureCaps;
    PublicData->feature_caps_end = PublicData->feature_caps_start + PublicData->feature_caps_count;
    PublicData->feature_report_byte_length = Caps.FeatureReportByteLength;
    PublicData->caps_size = (USHORT)CapsSize;
    PublicData->number_link_collection_nodes = (USHORT)NodeCount;

    ValueCaps = PublicData->value_caps + PublicData->input_caps_start;
    if (!HidP_CopyReportCaps(NativeData, HidP_Input, InputCounts, &ValueCaps))
        goto Failure;

    ValueCaps = PublicData->value_caps + PublicData->output_caps_start;
    if (!HidP_CopyReportCaps(NativeData, HidP_Output, OutputCounts, &ValueCaps))
        goto Failure;

    ValueCaps = PublicData->value_caps + PublicData->feature_caps_start;
    if (!HidP_CopyReportCaps(NativeData, HidP_Feature, FeatureCounts, &ValueCaps))
        goto Failure;

    CollectionNodes = (struct hid_collection_node *)((PUCHAR)PublicData->value_caps + PublicData->caps_size);
    for (Index = 0; Index < NodeCount; Index++)
    {
        CollectionNodes[Index].usage = LinkNodes[Index].LinkUsage;
        CollectionNodes[Index].usage_page = LinkNodes[Index].LinkUsagePage;
        CollectionNodes[Index].parent = LinkNodes[Index].Parent;
        CollectionNodes[Index].number_of_children = LinkNodes[Index].NumberOfChildren;
        CollectionNodes[Index].next_sibling = LinkNodes[Index].NextSibling;
        CollectionNodes[Index].first_child = LinkNodes[Index].FirstChild;
        CollectionNodes[Index].collection_type = LinkNodes[Index].CollectionType;
    }

    ReactOSData = (PHIDP_REACTOS_PREPARSED_DATA)((PUCHAR)PublicData + PublicDataSize - sizeof(*ReactOSData));
    ReactOSData->Magic = HIDP_REACTOS_PREPARSED_DATA_MAGIC;
    ReactOSData->NativeOffset = (ULONG)NativeOffset;
    ReactOSData->NativeSize = NativeSize;
    CopyFunction((PUCHAR)PublicData + NativeOffset, NativeData, NativeSize);

    FreeFunction(LinkNodes);
    *PreparsedData = (PHIDP_PREPARSED_DATA)PublicData;
    if (PublicSize)
        *PublicSize = (ULONG)PublicDataSize;
    if (TotalSize)
        *TotalSize = (ULONG)AllocationSize;
    return TRUE;

Failure:
    FreeFunction(LinkNodes);
    FreeFunction(PublicData);
    return FALSE;
}

static
PVOID
HidP_GetParserContext(
    IN PHIDP_PREPARSED_DATA PreparsedData)
{
    struct hid_preparsed_data *PublicData = (struct hid_preparsed_data *)PreparsedData;
    PHIDP_REACTOS_PREPARSED_DATA ReactOSData;
    ULONG_PTR Offset;

    if (!PreparsedData)
        return NULL;

    if (*(PULONG)&PublicData->magic[0] == HIDP_KDR_MAGIC_FIRST && *(PULONG)&PublicData->magic[4] == HIDP_KDR_MAGIC_SECOND)
    {
        Offset = FIELD_OFFSET(struct hid_preparsed_data, value_caps) + PublicData->caps_size + PublicData->number_link_collection_nodes * sizeof(struct hid_collection_node) + sizeof(struct hid_value_caps);
        ReactOSData = (PHIDP_REACTOS_PREPARSED_DATA)((PUCHAR)PreparsedData + Offset - sizeof(*ReactOSData));
    }
    else if (*(PULONG)PublicData->magic == HIDP_LEGACY_PREPARSED_DATA_MAGIC)
    {
        Offset = FIELD_OFFSET(struct hid_preparsed_data, value_caps) + PublicData->caps_size + PublicData->number_link_collection_nodes * sizeof(struct hid_collection_node);
        Offset = HidP_AlignUp(Offset, sizeof(PVOID));
        ReactOSData = (PHIDP_REACTOS_PREPARSED_DATA)((PUCHAR)PreparsedData + Offset);
    }
    else
    {
        return PreparsedData;
    }

    if (ReactOSData->Magic != HIDP_REACTOS_PREPARSED_DATA_MAGIC ||
        ReactOSData->NativeOffset < Offset)
    {
        return NULL;
    }

    return (PUCHAR)PreparsedData + ReactOSData->NativeOffset;
}

VOID
NTAPI
HidP_FreeCollectionDescription(
    IN PHIDP_DEVICE_DESC   DeviceDescription)
{
    //
    // free collection
    //
    HidParser_FreeCollectionDescription(DeviceDescription);
}


HIDAPI
NTSTATUS
NTAPI
HidP_GetCaps(
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    OUT PHIDP_CAPS  Capabilities)
{
    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData || !Capabilities)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    //
    // get caps
    //
    return HidParser_GetCaps(PreparsedData, Capabilities);
}

NTSTATUS
TranslateStatusForUpperLayer(
    IN NTSTATUS Status)
{
    //
    // now we are handling only this values, for others just return
    // status as it is.
    //
    switch (Status)
    {
    case HIDP_STATUS_INTERNAL_ERROR:
        return STATUS_INSUFFICIENT_RESOURCES;
    case HIDP_STATUS_INVALID_REPORT_TYPE:
        return HIDP_STATUS_INVALID_REPORT_TYPE;
    case HIDP_STATUS_BUFFER_TOO_SMALL:
        return STATUS_BUFFER_TOO_SMALL;
    case HIDP_STATUS_USAGE_NOT_FOUND:
        return STATUS_NO_DATA_DETECTED;
    default:
        return Status;
    }
}

NTSTATUS
NTAPI
HidP_GetCollectionDescription(
    IN PHIDP_REPORT_DESCRIPTOR ReportDesc,
    IN ULONG DescLength,
    IN POOL_TYPE PoolType,
    OUT PHIDP_DEVICE_DESC DeviceDescription)
{
    NTSTATUS Status;
    ULONG Index;

    //
    // get description;
    //
    Status = HidParser_GetCollectionDescription(ReportDesc, DescLength, PoolType, DeviceDescription);
    if (!NT_SUCCESS(Status))
        return TranslateStatusForUpperLayer(Status);

    for (Index = 0; Index < DeviceDescription->CollectionDescLength; Index++)
    {
        PHIDP_COLLECTION_DESC CollectionDesc = &DeviceDescription->CollectionDesc[Index];
        PHIDP_PREPARSED_DATA NativeData = CollectionDesc->PreparsedData;
        PHIDP_PREPARSED_DATA PublicData;
        ULONG NativeSize = CollectionDesc->PreparsedDataLength;
        ULONG PublicSize;

        if (!HidP_CreatePreparsedData(NativeData, NativeSize, &PublicData, &PublicSize, NULL))
        {
            HidParser_FreeCollectionDescription(DeviceDescription);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        FreeFunction(NativeData);
        CollectionDesc->PreparsedData = PublicData;
        CollectionDesc->PreparsedDataLength = (USHORT)PublicSize;
    }

    return STATUS_SUCCESS;
}

HIDAPI
ULONG
NTAPI
HidP_MaxUsageListLength(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage  OPTIONAL,
    IN PHIDP_PREPARSED_DATA  PreparsedData)
{
    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData)
        return 0;

    //
    // sanity check
    //
    ASSERT(ReportType == HidP_Input || ReportType == HidP_Output || ReportType == HidP_Feature);

    //
    // get usage length
    //
    return HidParser_MaxUsageListLength(PreparsedData, ReportType, UsagePage);
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetSpecificValueCaps(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection,
    IN USAGE  Usage,
    OUT PHIDP_VALUE_CAPS  ValueCaps,
    IN OUT PUSHORT  ValueCapsLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData)
{
    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData || !ValueCapsLength)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    //
    // sanity check
    //
    ASSERT(ReportType == HidP_Input || ReportType == HidP_Output || ReportType == HidP_Feature);

    //
    // get value caps
    //
    return HidParser_GetSpecificValueCaps(PreparsedData, ReportType, UsagePage, LinkCollection, Usage, ValueCaps, ValueCapsLength);
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetUsages(
    IN HIDP_REPORT_TYPE ReportType,
    IN USAGE UsagePage,
    IN USHORT LinkCollection  OPTIONAL,
    OUT PUSAGE UsageList,
    IN OUT PULONG UsageLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN PCHAR Report,
    IN ULONG ReportLength)
{
    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    //
    // sanity check
    //
    ASSERT(ReportType == HidP_Input || ReportType == HidP_Output || ReportType == HidP_Feature);

    //
    // get usages
    //
    return HidParser_GetUsages(PreparsedData, ReportType, UsagePage, LinkCollection, UsageList, UsageLength, Report, ReportLength);
}


#undef HidP_GetButtonCaps

HIDAPI
NTSTATUS
NTAPI
HidP_UsageListDifference(
    IN PUSAGE  PreviousUsageList,
    IN PUSAGE  CurrentUsageList,
    OUT PUSAGE  BreakUsageList,
    OUT PUSAGE  MakeUsageList,
    IN ULONG  UsageListLength)
{
    return HidParser_UsageListDifference(PreviousUsageList, CurrentUsageList, BreakUsageList, MakeUsageList, UsageListLength);
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetUsagesEx(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USHORT  LinkCollection,
    OUT PUSAGE_AND_PAGE  ButtonList,
    IN OUT ULONG  *UsageLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN PCHAR  Report,
    IN ULONG  ReportLength)
{
    return HidP_GetUsages(ReportType, HID_USAGE_PAGE_UNDEFINED, LinkCollection, &ButtonList->Usage, UsageLength, PreparsedData, Report, ReportLength);
}

HIDAPI
NTSTATUS
NTAPI
HidP_UsageAndPageListDifference(
    IN PUSAGE_AND_PAGE  PreviousUsageList,
    IN PUSAGE_AND_PAGE  CurrentUsageList,
    OUT PUSAGE_AND_PAGE  BreakUsageList,
    OUT PUSAGE_AND_PAGE  MakeUsageList,
    IN ULONG  UsageListLength)
{
    return HidParser_UsageAndPageListDifference(PreviousUsageList, CurrentUsageList, BreakUsageList, MakeUsageList, UsageListLength);
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetScaledUsageValue(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection  OPTIONAL,
    IN USAGE  Usage,
    OUT PLONG  UsageValue,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN PCHAR  Report,
    IN ULONG  ReportLength)
{
    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    //
    // sanity check
    //
    ASSERT(ReportType == HidP_Input || ReportType == HidP_Output || ReportType == HidP_Feature);

    //
    // get scaled usage value
    //
    return HidParser_GetScaledUsageValue(PreparsedData, ReportType, UsagePage, LinkCollection, Usage, UsageValue, Report, ReportLength);
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetUsageValue(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection,
    IN USAGE  Usage,
    OUT PULONG  UsageValue,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN PCHAR  Report,
    IN ULONG  ReportLength)
{
    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    //
    // sanity check
    //
    ASSERT(ReportType == HidP_Input || ReportType == HidP_Output || ReportType == HidP_Feature);

    //
    // get scaled usage value
    //
    return HidParser_GetUsageValue(PreparsedData, ReportType, UsagePage, LinkCollection, Usage, UsageValue, Report, ReportLength);
}


HIDAPI
NTSTATUS
NTAPI
HidP_TranslateUsageAndPagesToI8042ScanCodes(
    IN PUSAGE_AND_PAGE  ChangedUsageList,
    IN ULONG  UsageListLength,
    IN HIDP_KEYBOARD_DIRECTION  KeyAction,
    IN OUT PHIDP_KEYBOARD_MODIFIER_STATE  ModifierState,
    IN PHIDP_INSERT_SCANCODES  InsertCodesProcedure,
    IN PVOID  InsertCodesContext)
{
    //
    // translate usage pages
    //
    return HidParser_TranslateUsageAndPagesToI8042ScanCodes(ChangedUsageList, UsageListLength, KeyAction, ModifierState, InsertCodesProcedure, InsertCodesContext);
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetButtonCaps(
    HIDP_REPORT_TYPE ReportType,
    PHIDP_BUTTON_CAPS ButtonCaps,
    PUSHORT ButtonCapsLength,
    PHIDP_PREPARSED_DATA PreparsedData)
{
    return HidP_GetSpecificButtonCaps(ReportType, HID_USAGE_PAGE_UNDEFINED, 0, 0, ButtonCaps, ButtonCapsLength, PreparsedData);
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetSpecificButtonCaps(
    IN HIDP_REPORT_TYPE ReportType,
    IN USAGE UsagePage,
    IN USHORT LinkCollection,
    IN USAGE Usage,
    OUT PHIDP_BUTTON_CAPS ButtonCaps,
    IN OUT PUSHORT ButtonCapsLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData)
{
    ULONG Length;
    NTSTATUS Status;

    if (!ButtonCapsLength)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData)
    {
        *ButtonCapsLength = 0;
        return HIDP_STATUS_INVALID_PREPARSED_DATA;
    }

    Length = *ButtonCapsLength;
    Status = HidParser_GetSpecificButtonCaps(PreparsedData,
                                             ReportType,
                                             UsagePage,
                                             LinkCollection,
                                             Usage,
                                             ButtonCaps,
                                             &Length);

    *ButtonCapsLength = (USHORT)Length;
    return Status;
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetData(
    IN HIDP_REPORT_TYPE  ReportType,
    OUT PHIDP_DATA  DataList,
    IN OUT PULONG  DataLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetExtendedAttributes(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USHORT DataIndex,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    OUT PHIDP_EXTENDED_ATTRIBUTES  Attributes,
    IN OUT PULONG  LengthAttributes)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetLinkCollectionNodes(
    OUT PHIDP_LINK_COLLECTION_NODE  LinkCollectionNodes,
    IN OUT PULONG  LinkCollectionNodesLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData)
{
    PreparsedData = HidP_GetParserContext(PreparsedData);
    if (!PreparsedData || !LinkCollectionNodesLength)
        return HIDP_STATUS_INVALID_PREPARSED_DATA;

    return HidParser_GetLinkCollectionNodes(PreparsedData,
                                            LinkCollectionNodes,
                                            LinkCollectionNodesLength);
}

NTSTATUS
NTAPI
HidP_SysPowerEvent(
    IN PCHAR HidPacket,
    IN USHORT HidPacketLength,
    IN PHIDP_PREPARSED_DATA Ppd,
    OUT PULONG OutputBuffer)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
HidP_SysPowerCaps(
    IN PHIDP_PREPARSED_DATA Ppd,
    OUT PULONG OutputBuffer)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_GetUsageValueArray(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection  OPTIONAL,
    IN USAGE  Usage,
    OUT PCHAR  UsageValue,
    IN USHORT  UsageValueByteLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}


HIDAPI
NTSTATUS
NTAPI
HidP_UnsetUsages(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection,
    IN PUSAGE  UsageList,
    IN OUT PULONG  UsageLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN OUT PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_TranslateUsagesToI8042ScanCodes(
    IN PUSAGE  ChangedUsageList,
    IN ULONG  UsageListLength,
    IN HIDP_KEYBOARD_DIRECTION  KeyAction,
    IN OUT PHIDP_KEYBOARD_MODIFIER_STATE  ModifierState,
    IN PHIDP_INSERT_SCANCODES  InsertCodesProcedure,
    IN PVOID  InsertCodesContext)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_SetUsages(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection,
    IN PUSAGE  UsageList,
    IN OUT PULONG  UsageLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN OUT PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_SetUsageValueArray(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection  OPTIONAL,
    IN USAGE  Usage,
    IN PCHAR  UsageValue,
    IN USHORT  UsageValueByteLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    OUT PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_SetUsageValue(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection,
    IN USAGE  Usage,
    IN ULONG  UsageValue,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN OUT PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_SetScaledUsageValue(
    IN HIDP_REPORT_TYPE  ReportType,
    IN USAGE  UsagePage,
    IN USHORT  LinkCollection  OPTIONAL,
    IN USAGE  Usage,
    IN LONG  UsageValue,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN OUT PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_SetData(
    IN HIDP_REPORT_TYPE  ReportType,
    IN PHIDP_DATA  DataList,
    IN OUT PULONG  DataLength,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN OUT PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
ULONG
NTAPI
HidP_MaxDataListLength(
    IN HIDP_REPORT_TYPE  ReportType,
    IN PHIDP_PREPARSED_DATA  PreparsedData)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

HIDAPI
NTSTATUS
NTAPI
HidP_InitializeReportForID(
    IN HIDP_REPORT_TYPE  ReportType,
    IN UCHAR  ReportID,
    IN PHIDP_PREPARSED_DATA  PreparsedData,
    IN OUT PCHAR  Report,
    IN ULONG  ReportLength)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
    return STATUS_NOT_IMPLEMENTED;
}

#undef HidP_GetValueCaps

HIDAPI
NTSTATUS
NTAPI
HidP_GetValueCaps(
    HIDP_REPORT_TYPE ReportType,
    PHIDP_VALUE_CAPS ValueCaps,
    PUSHORT ValueCapsLength,
    PHIDP_PREPARSED_DATA PreparsedData)
{
    return HidP_GetSpecificValueCaps(ReportType,
                                     HID_USAGE_PAGE_UNDEFINED,
                                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                                     0,
                                     ValueCaps,
                                     ValueCapsLength,
                                     PreparsedData);
}
