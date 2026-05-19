/*
 * ReactOS Hid User Library
 * Copyright (C) 2004-2005 ReactOS Team
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
/*
 * PROJECT:         ReactOS Hid User Library
 * FILE:            lib/hid/hid.c
 * PURPOSE:         ReactOS Hid User Library
 * PROGRAMMER:      Thomas Weidenmueller <w3seek@reactos.com>
 *
 * UPDATE HISTORY:
 *      07/12/2004  Created
 */

#include "precomp.h"
#include <stdarg.h>

#include <winbase.h>

#define NDEBUG
#include <debug.h>
#include "hidp.h"

HINSTANCE hDllInstance;

/* device interface GUID for HIDClass devices */
const GUID HidClassGuid = {0x4D1E55B2, 0xF16F, 0x11CF, {0x88,0xCB,0x00,0x11,0x11,0x00,0x00,0x30}};

PVOID
NTAPI
AllocFunction(
    IN ULONG ItemSize)
{
    return LocalAlloc(LPTR, ItemSize);
}

VOID
NTAPI
FreeFunction(
    IN PVOID Item)
{
    LocalFree((HLOCAL)Item);
}

VOID
NTAPI
ZeroFunction(
    IN PVOID Item,
    IN ULONG ItemSize)
{
    memset(Item, 0, ItemSize);
}

VOID
NTAPI
CopyFunction(
    IN PVOID Target,
    IN PVOID Source,
    IN ULONG Length)
{
    memcpy(Target, Source, Length);
}

VOID
__cdecl
DebugFunction(
    IN LPCSTR FormatStr, ...)
{
#if 0
    va_arg list;
    va_start(list, FormatStr);
    vDbgPrintEx(FormatStr, list);
    va_end(list);
#endif
}

typedef struct _HIDD_REPORT_CAPS_COUNTS
{
  USHORT Value;
  USHORT Button;
} HIDD_REPORT_CAPS_COUNTS, *PHIDD_REPORT_CAPS_COUNTS;

static
SIZE_T
HidD_AlignUp(SIZE_T Value, SIZE_T Alignment)
{
  return (Value + Alignment - 1) & ~(Alignment - 1);
}

static
BOOLEAN
HidD_AddSize(SIZE_T Left, SIZE_T Right, SIZE_T *Result)
{
  if (Left > (SIZE_T)-1 - Right)
    return FALSE;

  *Result = Left + Right;
  return TRUE;
}

static
BOOLEAN
HidD_MultiplySize(SIZE_T Left, SIZE_T Right, SIZE_T *Result)
{
  if (Left && Right > (SIZE_T)-1 / Left)
    return FALSE;

  *Result = Left * Right;
  return TRUE;
}

static
BOOLEAN
HidD_NoCapsStatus(NTSTATUS Status)
{
  return Status == HIDP_STATUS_USAGE_NOT_FOUND ||
         Status == HIDP_STATUS_REPORT_DOES_NOT_EXIST;
}

static
BOOLEAN
HidD_QueryReportCapsCounts(PHIDP_PREPARSED_DATA NativeData,
                           HIDP_REPORT_TYPE ReportType,
                           PHIDD_REPORT_CAPS_COUNTS Counts)
{
  NTSTATUS Status;

  Counts->Value = 0;
  Status = HidP_GetSpecificValueCaps(ReportType,
                                     HID_USAGE_PAGE_UNDEFINED,
                                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                                     0,
                                     NULL,
                                     &Counts->Value,
                                     NativeData);
  if (HidD_NoCapsStatus(Status))
    Counts->Value = 0;
  else if (Status != HIDP_STATUS_SUCCESS && Status != HIDP_STATUS_BUFFER_TOO_SMALL)
    return FALSE;

  Counts->Button = 0;
  Status = HidP_GetSpecificButtonCaps(ReportType,
                                      HID_USAGE_PAGE_UNDEFINED,
                                      HIDP_LINK_COLLECTION_UNSPECIFIED,
                                      0,
                                      NULL,
                                      &Counts->Button,
                                      NativeData);
  if (HidD_NoCapsStatus(Status))
    Counts->Button = 0;
  else if (Status != HIDP_STATUS_SUCCESS && Status != HIDP_STATUS_BUFFER_TOO_SMALL)
    return FALSE;

  return TRUE;
}

static
VOID
HidD_CopyValueCaps(struct hid_value_caps *Destination,
                   const HIDP_VALUE_CAPS *Source)
{
  memset(Destination, 0, sizeof(*Destination));

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
HidD_CopyButtonCaps(struct hid_value_caps *Destination,
                    const HIDP_BUTTON_CAPS *Source)
{
  memset(Destination, 0, sizeof(*Destination));

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
HidD_CopyReportCaps(PHIDP_PREPARSED_DATA NativeData,
                    HIDP_REPORT_TYPE ReportType,
                    HIDD_REPORT_CAPS_COUNTS Counts,
                    struct hid_value_caps **Destination)
{
  HIDP_VALUE_CAPS *ValueCaps;
  HIDP_BUTTON_CAPS *ButtonCaps;
  USHORT Count, Index;
  NTSTATUS Status;

  if (Counts.Value)
  {
    Count = Counts.Value;
    ValueCaps = LocalAlloc(LPTR, Count * sizeof(*ValueCaps));
    if (!ValueCaps)
      return FALSE;

    Status = HidP_GetSpecificValueCaps(ReportType,
                                       HID_USAGE_PAGE_UNDEFINED,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       0,
                                       ValueCaps,
                                       &Count,
                                       NativeData);
    if (Status != HIDP_STATUS_SUCCESS)
    {
      LocalFree(ValueCaps);
      return FALSE;
    }

    for (Index = 0; Index < Count; Index++)
      HidD_CopyValueCaps((*Destination)++, &ValueCaps[Index]);

    LocalFree(ValueCaps);
  }

  if (Counts.Button)
  {
    Count = Counts.Button;
    ButtonCaps = LocalAlloc(LPTR, Count * sizeof(*ButtonCaps));
    if (!ButtonCaps)
      return FALSE;

    Status = HidP_GetSpecificButtonCaps(ReportType,
                                        HID_USAGE_PAGE_UNDEFINED,
                                        HIDP_LINK_COLLECTION_UNSPECIFIED,
                                        0,
                                        ButtonCaps,
                                        &Count,
                                        NativeData);
    if (Status != HIDP_STATUS_SUCCESS)
    {
      LocalFree(ButtonCaps);
      return FALSE;
    }

    for (Index = 0; Index < Count; Index++)
      HidD_CopyButtonCaps((*Destination)++, &ButtonCaps[Index]);

    LocalFree(ButtonCaps);
  }

  return TRUE;
}

static
BOOLEAN
HidD_BuildWinePreparsedData(PHIDP_PREPARSED_DATA NativeData,
                            ULONG NativeSize,
                            PHIDP_PREPARSED_DATA *PreparsedData)
{
  HIDP_CAPS Caps;
  HIDD_REPORT_CAPS_COUNTS InputCounts, OutputCounts, FeatureCounts;
  HIDP_LINK_COLLECTION_NODE *LinkNodes = NULL;
  struct hid_preparsed_data *WineData;
  struct hid_value_caps *ValueCaps;
  struct hid_collection_node *CollectionNodes;
  PHIDP_REACTOS_PREPARSED_DATA ReactOSData;
  NTSTATUS Status;
  ULONG NodeCount, Index;
  SIZE_T InputCaps, OutputCaps, FeatureCaps, TotalCaps;
  SIZE_T CapsSize, NodesSize, FooterOffset, NativeOffset, TotalSize;

  Status = HidP_GetCaps(NativeData, &Caps);
  if (Status != HIDP_STATUS_SUCCESS)
    return FALSE;

  if (!HidD_QueryReportCapsCounts(NativeData, HidP_Input, &InputCounts) ||
      !HidD_QueryReportCapsCounts(NativeData, HidP_Output, &OutputCounts) ||
      !HidD_QueryReportCapsCounts(NativeData, HidP_Feature, &FeatureCounts))
  {
    return FALSE;
  }

  InputCaps = InputCounts.Value + InputCounts.Button;
  OutputCaps = OutputCounts.Value + OutputCounts.Button;
  FeatureCaps = FeatureCounts.Value + FeatureCounts.Button;
  if (InputCaps > MAXUSHORT || OutputCaps > MAXUSHORT || FeatureCaps > MAXUSHORT)
    return FALSE;

  TotalCaps = InputCaps + OutputCaps + FeatureCaps;
  if (!HidD_MultiplySize(TotalCaps, sizeof(struct hid_value_caps), &CapsSize) ||
      CapsSize > MAXUSHORT)
  {
    return FALSE;
  }

  NodeCount = Caps.NumberLinkCollectionNodes;
  if (NodeCount)
  {
    ULONG RequestedNodeCount = NodeCount;

    LinkNodes = LocalAlloc(LPTR, NodeCount * sizeof(*LinkNodes));
    if (!LinkNodes)
      return FALSE;

    Status = HidP_GetLinkCollectionNodes(LinkNodes, &RequestedNodeCount, NativeData);
    if (Status != HIDP_STATUS_SUCCESS)
    {
      LocalFree(LinkNodes);
      return FALSE;
    }
    NodeCount = RequestedNodeCount;
  }

  if (!HidD_MultiplySize(NodeCount, sizeof(struct hid_collection_node), &NodesSize) ||
      !HidD_AddSize(FIELD_OFFSET(struct hid_preparsed_data, value_caps), CapsSize, &FooterOffset) ||
      !HidD_AddSize(FooterOffset, NodesSize, &FooterOffset))
  {
    LocalFree(LinkNodes);
    return FALSE;
  }

  FooterOffset = HidD_AlignUp(FooterOffset, sizeof(PVOID));
  if (!HidD_AddSize(FooterOffset, sizeof(*ReactOSData), &NativeOffset))
  {
    LocalFree(LinkNodes);
    return FALSE;
  }

  NativeOffset = HidD_AlignUp(NativeOffset, sizeof(PVOID));
  if (!HidD_AddSize(NativeOffset, NativeSize, &TotalSize))
  {
    LocalFree(LinkNodes);
    return FALSE;
  }
  if (NativeOffset > MAXULONG)
  {
    LocalFree(LinkNodes);
    return FALSE;
  }

  WineData = LocalAlloc(LPTR, TotalSize);
  if (!WineData)
  {
    LocalFree(LinkNodes);
    return FALSE;
  }

  *(PULONG)WineData->magic = HIDP_WINE_PREPARSED_DATA_MAGIC;
  WineData->usage = Caps.Usage;
  WineData->usage_page = Caps.UsagePage;
  WineData->input_caps_start = 0;
  WineData->input_caps_count = (USHORT)InputCaps;
  WineData->input_caps_end = WineData->input_caps_start + WineData->input_caps_count;
  WineData->input_report_byte_length = Caps.InputReportByteLength;
  WineData->output_caps_start = WineData->input_caps_end;
  WineData->output_caps_count = (USHORT)OutputCaps;
  WineData->output_caps_end = WineData->output_caps_start + WineData->output_caps_count;
  WineData->output_report_byte_length = Caps.OutputReportByteLength;
  WineData->feature_caps_start = WineData->output_caps_end;
  WineData->feature_caps_count = (USHORT)FeatureCaps;
  WineData->feature_caps_end = WineData->feature_caps_start + WineData->feature_caps_count;
  WineData->feature_report_byte_length = Caps.FeatureReportByteLength;
  WineData->caps_size = (USHORT)CapsSize;
  WineData->number_link_collection_nodes = (USHORT)NodeCount;

  ValueCaps = WineData->value_caps + WineData->input_caps_start;
  if (!HidD_CopyReportCaps(NativeData, HidP_Input, InputCounts, &ValueCaps))
  {
    LocalFree(LinkNodes);
    LocalFree(WineData);
    return FALSE;
  }

  ValueCaps = WineData->value_caps + WineData->output_caps_start;
  if (!HidD_CopyReportCaps(NativeData, HidP_Output, OutputCounts, &ValueCaps))
  {
    LocalFree(LinkNodes);
    LocalFree(WineData);
    return FALSE;
  }

  ValueCaps = WineData->value_caps + WineData->feature_caps_start;
  if (!HidD_CopyReportCaps(NativeData, HidP_Feature, FeatureCounts, &ValueCaps))
  {
    LocalFree(LinkNodes);
    LocalFree(WineData);
    return FALSE;
  }

  CollectionNodes = (struct hid_collection_node *)((PUCHAR)WineData->value_caps + WineData->caps_size);
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

  ReactOSData = (PHIDP_REACTOS_PREPARSED_DATA)((PUCHAR)WineData + FooterOffset);
  ReactOSData->Magic = HIDP_REACTOS_PREPARSED_DATA_MAGIC;
  ReactOSData->NativeOffset = (ULONG)NativeOffset;
  ReactOSData->NativeSize = NativeSize;
  memcpy((PUCHAR)WineData + NativeOffset, NativeData, NativeSize);

  LocalFree(LinkNodes);
  *PreparsedData = (PHIDP_PREPARSED_DATA)WineData;
  return TRUE;
}

BOOL WINAPI
DllMain(HINSTANCE hinstDLL,
        DWORD dwReason,
        LPVOID lpvReserved)
{
  switch(dwReason)
  {
    case DLL_PROCESS_ATTACH:
      hDllInstance = hinstDLL;
      break;

    case DLL_THREAD_ATTACH:
      break;

    case DLL_THREAD_DETACH:
      break;

    case DLL_PROCESS_DETACH:
      break;
  }
  return TRUE;
}


/*
 * HidD_FlushQueue							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_FlushQueue(IN HANDLE HidDeviceObject)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_FLUSH_QUEUE,
                         NULL, 0,
                         NULL, 0,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_FreePreparsedData						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_FreePreparsedData(IN PHIDP_PREPARSED_DATA PreparsedData)
{
  return (LocalFree((HLOCAL)PreparsedData) == NULL);
}


/*
 * HidD_GetAttributes							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetAttributes(IN HANDLE HidDeviceObject,
                   OUT PHIDD_ATTRIBUTES Attributes)
{
  HID_COLLECTION_INFORMATION hci;
  DWORD RetLen;

  if(!DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_COLLECTION_INFORMATION,
                                       NULL, 0,
                                       &hci, sizeof(HID_COLLECTION_INFORMATION),
                                       &RetLen, NULL))
  {
    return FALSE;
  }

  /* copy the fields */
  Attributes->Size = sizeof(HIDD_ATTRIBUTES);
  Attributes->VendorID = hci.VendorID;
  Attributes->ProductID = hci.ProductID;
  Attributes->VersionNumber = hci.VersionNumber;

  return TRUE;
}


/*
 * HidD_GetFeature							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetFeature(IN HANDLE HidDeviceObject,
                OUT PVOID ReportBuffer,
                IN ULONG ReportBufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_FEATURE,
                         NULL, 0,
                         ReportBuffer, ReportBufferLength,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_GetHidGuid							EXPORTED
 *
 * @implemented
 */
HIDAPI
VOID WINAPI
HidD_GetHidGuid(OUT LPGUID HidGuid)
{
  *HidGuid = HidClassGuid;
}


/*
 * HidD_GetInputReport							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetInputReport(IN HANDLE HidDeviceObject,
                    IN OUT PVOID ReportBuffer,
                    IN ULONG ReportBufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_INPUT_REPORT,
                         NULL, 0,
                         ReportBuffer, ReportBufferLength,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_GetManufacturerString						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetManufacturerString(IN HANDLE HidDeviceObject,
                           OUT PVOID Buffer,
                           IN ULONG BufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_MANUFACTURER_STRING,
                         NULL, 0,
                         Buffer, BufferLength,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_GetNumInputBuffers						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetNumInputBuffers(IN HANDLE HidDeviceObject,
                        OUT PULONG NumberBuffers)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_GET_NUM_DEVICE_INPUT_BUFFERS,
                         NULL, 0,
                         NumberBuffers, sizeof(ULONG),
                         &RetLen, NULL) != 0;
}


/*
 * HidD_GetPhysicalDescriptor						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetPhysicalDescriptor(IN HANDLE HidDeviceObject,
                           OUT PVOID Buffer,
                           IN ULONG BufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_GET_PHYSICAL_DESCRIPTOR,
                         NULL, 0,
                         Buffer, BufferLength,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_GetPreparsedData						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetPreparsedData(IN HANDLE HidDeviceObject,
                      OUT PHIDP_PREPARSED_DATA *PreparsedData)
{
  HID_COLLECTION_INFORMATION hci;
  DWORD RetLen;
  PHIDP_PREPARSED_DATA NativeData;
  BOOLEAN Ret;

  if(PreparsedData == NULL)
  {
    return FALSE;
  }

  if(!DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_COLLECTION_INFORMATION,
                                       NULL, 0,
                                       &hci, sizeof(HID_COLLECTION_INFORMATION),
                                       &RetLen, NULL))
  {
    return FALSE;
  }

  NativeData = LocalAlloc(LPTR, hci.DescriptorSize);
  if(NativeData == NULL)
  {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }

  Ret = DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_COLLECTION_DESCRIPTOR,
                        NULL, 0,
                        NativeData, hci.DescriptorSize,
                        &RetLen, NULL) != 0;

  if(!Ret)
  {
    LocalFree((HLOCAL)NativeData);
    return FALSE;
  }

  Ret = HidD_BuildWinePreparsedData(NativeData, hci.DescriptorSize, PreparsedData);
  LocalFree((HLOCAL)NativeData);
  if(!Ret)
  {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
#if 0
  else
  {
    /* should we truncate the memory in case RetLen < hci.DescriptorSize? */
  }
#endif

  return Ret;
}


/*
 * HidD_GetProductString						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetProductString(IN HANDLE HidDeviceObject,
                      OUT PVOID Buffer,
                      IN ULONG BufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_PRODUCT_STRING,
                         NULL, 0,
                         Buffer, BufferLength,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_GetSerialNumberString						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetSerialNumberString(IN HANDLE HidDeviceObject,
                           OUT PVOID Buffer,
                           IN ULONG BufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_SERIALNUMBER_STRING,
                         NULL, 0,
                         Buffer, BufferLength,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_Hello								EXPORTED
 *
 * Undocumented easter egg function. It fills the buffer with "Hello\n"
 * and returns number of bytes filled in (lstrlen(Buffer) + 1 == 7)
 *
 * Bugs: - doesn't check Buffer for NULL
 *       - always returns 7 even if BufferLength < 7 but doesn't produce a buffer overflow
 *
 * @implemented
 */
HIDAPI
ULONG WINAPI
HidD_Hello(OUT PCHAR Buffer,
           IN ULONG BufferLength)
{
  const CHAR HelloString[] = "Hello\n";

  if(BufferLength > 0)
  {
    memcpy(Buffer, HelloString, min(sizeof(HelloString), BufferLength));
  }

  return sizeof(HelloString);
}


/*
 * HidD_SetFeature							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_SetFeature(IN HANDLE HidDeviceObject,
                IN PVOID ReportBuffer,
                IN ULONG ReportBufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_SET_FEATURE,
                         ReportBuffer, ReportBufferLength,
                         NULL, 0,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_SetNumInputBuffers						EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_SetNumInputBuffers(IN HANDLE HidDeviceObject,
                        IN ULONG NumberBuffers)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_SET_NUM_DEVICE_INPUT_BUFFERS,
                         &NumberBuffers, sizeof(ULONG),
                         NULL, 0,
                         &RetLen, NULL) != 0;
}


/*
 * HidD_SetOutputReport							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_SetOutputReport(IN HANDLE HidDeviceObject,
                     IN PVOID ReportBuffer,
                     IN ULONG ReportBufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_SET_OUTPUT_REPORT,
                         ReportBuffer, ReportBufferLength,
                         NULL, 0,
                         &RetLen, NULL) != 0;
}

/*
 * HidD_GetIndexedString							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetIndexedString(IN HANDLE HidDeviceObject,
                      IN ULONG StringIndex,
                      OUT PVOID Buffer,
                      IN ULONG BufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_INDEXED_STRING,
                         &StringIndex, sizeof(ULONG),
                         Buffer, BufferLength,
                         &RetLen, NULL) != 0;
}

/*
 * HidD_GetMsGenreDescriptor							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetMsGenreDescriptor(IN HANDLE HidDeviceObject,
                          OUT PVOID Buffer,
                          IN ULONG BufferLength)
{
  DWORD RetLen;
  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_MS_GENRE_DESCRIPTOR,
                         0, 0,
                         Buffer, BufferLength,
                         &RetLen, NULL) != 0;
}

/*
 * HidD_GetConfiguration							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_GetConfiguration(IN HANDLE HidDeviceObject,
                      OUT PHIDD_CONFIGURATION Configuration,
                      IN ULONG ConfigurationLength)
{

  // magic cookie
  Configuration->cookie = (PVOID)HidD_GetConfiguration;

  return DeviceIoControl(HidDeviceObject, IOCTL_HID_GET_DRIVER_CONFIG,
                         0, 0,
                         &Configuration->size, ConfigurationLength - sizeof(ULONG),
                         (PULONG)&Configuration->cookie, NULL) != 0;
}

/*
 * HidD_SetConfiguration							EXPORTED
 *
 * @implemented
 */
HIDAPI
BOOLEAN WINAPI
HidD_SetConfiguration(IN HANDLE HidDeviceObject,
                      IN PHIDD_CONFIGURATION Configuration,
                      IN ULONG ConfigurationLength)
{
    BOOLEAN Ret = FALSE;

    if (Configuration->cookie == (PVOID)HidD_GetConfiguration)
    {
        Ret = DeviceIoControl(HidDeviceObject, IOCTL_HID_SET_DRIVER_CONFIG,
                              0, 0,
                              (PVOID)&Configuration->size, ConfigurationLength - sizeof(ULONG),
                              (PULONG)&Configuration->cookie, NULL) != 0;
    }
    else
    {
        SetLastError(ERROR_INVALID_PARAMETER);
    }

    return Ret;
}

/* EOF */
