#ifndef _HIDP_PRIVATE_H_
#define _HIDP_PRIVATE_H_

#include <reactos/hidp_private.h>

#define HIDP_LEGACY_PREPARSED_DATA_MAGIC 0x08491759

#define HID_VALUE_CAPS_IS_BUTTON            0x04
#define HID_VALUE_CAPS_IS_ABSOLUTE          0x08
#define HID_VALUE_CAPS_IS_RANGE             0x10
#define HID_VALUE_CAPS_IS_STRING_RANGE      0x40
#define HID_VALUE_CAPS_IS_DESIGNATOR_RANGE  0x80

struct hid_collection_node
{
    USAGE usage;
    USAGE usage_page;
    USHORT parent;
    USHORT number_of_children;
    USHORT next_sibling;
    USHORT first_child;
    ULONG collection_type;
};

struct hid_value_caps
{
    USHORT usage_page;
    UCHAR report_id;
    UCHAR start_bit;
    USHORT bit_size;
    USHORT report_count;
    USHORT start_byte;
    USHORT total_bits;
    ULONG bit_field;
    USHORT end_byte;
    USHORT link_collection;
    USAGE link_usage_page;
    USAGE link_usage;
    ULONG flags;
    ULONG padding[8];
    USAGE usage_min;
    USAGE usage_max;
    USHORT string_min;
    USHORT string_max;
    USHORT designator_min;
    USHORT designator_max;
    USHORT data_index_min;
    USHORT data_index_max;
    USHORT null_value;
    USHORT unknown;
    LONG logical_min;
    LONG logical_max;
    LONG physical_min;
    LONG physical_max;
    LONG units;
    LONG units_exp;
};

struct hid_preparsed_data
{
    UCHAR magic[8];
    USAGE usage;
    USAGE usage_page;
    USHORT unknown[2];
    USHORT input_caps_start;
    USHORT input_caps_count;
    USHORT input_caps_end;
    USHORT input_report_byte_length;
    USHORT output_caps_start;
    USHORT output_caps_count;
    USHORT output_caps_end;
    USHORT output_report_byte_length;
    USHORT feature_caps_start;
    USHORT feature_caps_count;
    USHORT feature_caps_end;
    USHORT feature_report_byte_length;
    USHORT caps_size;
    USHORT number_link_collection_nodes;
    struct hid_value_caps value_caps[1];
};

PVOID NTAPI AllocFunction(ULONG Size);
VOID NTAPI FreeFunction(PVOID Item);
VOID NTAPI ZeroFunction(PVOID Item, ULONG Size);
VOID NTAPI CopyFunction(PVOID Target, PVOID Source, ULONG Size);
VOID __cdecl DebugFunction(LPCSTR Src, ...);

BOOLEAN
HidP_CreatePreparsedData(
    IN PHIDP_PREPARSED_DATA NativeData,
    IN ULONG NativeSize,
    OUT PHIDP_PREPARSED_DATA *PreparsedData,
    OUT PULONG PublicSize OPTIONAL,
    OUT PULONG TotalSize OPTIONAL);

#endif /* _HIDP_PRIVATE_H_ */
