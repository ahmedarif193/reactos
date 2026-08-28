/*
 * PROJECT:     ReactOS Resource Hub public interface
 * LICENSE:     LGPL-2.1-or-later
 * PURPOSE:     Windows-compatible connection property query ABI
 */

#pragma once

#define RESOURCE_HUB_DEVICE_NAME L"\\Device\\RESOURCE_HUB"
#define RESOURCE_HUB_SYMBOLIC_NAME L"\\DosDevices\\RESOURCE_HUB"
#define RESOURCE_HUB_DEVICE_NAME_PREFIX RESOURCE_HUB_DEVICE_NAME L"\\"

#define RESOURCE_HUB_CONNECTION_FILE_SIZE \
    ((sizeof(LARGE_INTEGER) * 2 * sizeof(WCHAR)) + sizeof(UNICODE_NULL))
#define RESOURCE_HUB_CONNECTION_PATH_SIZE \
    (sizeof(RESOURCE_HUB_DEVICE_NAME_PREFIX) + \
     RESOURCE_HUB_CONNECTION_FILE_SIZE - sizeof(UNICODE_NULL))
#define RESOURCE_HUB_CONNECTION_FILE_CHARS \
    ((RESOURCE_HUB_CONNECTION_FILE_SIZE + sizeof(WCHAR) - 1) / sizeof(WCHAR))
#define RESOURCE_HUB_CONNECTION_PATH_CHARS \
    ((RESOURCE_HUB_CONNECTION_PATH_SIZE + sizeof(WCHAR) - 1) / sizeof(WCHAR))

#define RESOURCE_HUB_FILE_SIZE RESOURCE_HUB_CONNECTION_FILE_SIZE
#define RESOURCE_HUB_PATH_SIZE RESOURCE_HUB_CONNECTION_PATH_SIZE
#define RESOURCE_HUB_FILE_CHARS RESOURCE_HUB_CONNECTION_FILE_CHARS
#define RESOURCE_HUB_PATH_CHARS RESOURCE_HUB_CONNECTION_PATH_CHARS

#define FILE_DEVICE_RESOURCE_HUB FILE_DEVICE_BUS_EXTENDER

#define IOCTL_RH_QUERY_CONNECTION_PROPERTIES CTL_CODE(FILE_DEVICE_RESOURCE_HUB, 0x0, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define RH_QUERY_CONNECTION_PROPERTIES_INPUT_VERSION 1
#define RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_VERSION 1

typedef enum _RH_QUERY_CONNECTION_PROPERTIES_INPUT_TYPE
{
    ConnectionIdType,
    InterruptVectorType
} RH_QUERY_CONNECTION_PROPERTIES_INPUT_TYPE, *PRH_QUERY_CONNECTION_PROPERTIES_INPUT_TYPE;

typedef struct _RH_QUERY_CONNECTION_PROPERTIES_INPUT_BUFFER
{
    ULONG Version;
    RH_QUERY_CONNECTION_PROPERTIES_INPUT_TYPE QueryType;
    union
    {
        LARGE_INTEGER ConnectionId;
        ULONG InterruptVector;
    } u;
} RH_QUERY_CONNECTION_PROPERTIES_INPUT_BUFFER, *PRH_QUERY_CONNECTION_PROPERTIES_INPUT_BUFFER;

typedef struct _RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER
{
    ULONG Version;
    ULONG PropertiesLength;
    UCHAR ConnectionProperties[ANYSIZE_ARRAY];
} RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER, *PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER;

#ifdef RESHUB_USE_HELPER_ROUTINES
#include <ntstrsafe.h>

FORCEINLINE
NTSTATUS
RESOURCE_HUB_ID_TO_FILE_NAME(
    _In_ ULONG IdLowPart,
    _In_ ULONG IdHighPart,
    _Out_writes_bytes_(RESOURCE_HUB_CONNECTION_FILE_SIZE) PWCHAR FileName)
{
    return RtlStringCbPrintfW(FileName,
                              RESOURCE_HUB_CONNECTION_FILE_SIZE,
                              L"%08lx%08lx",
                              IdHighPart,
                              IdLowPart);
}

FORCEINLINE
NTSTATUS
RESOURCE_HUB_CREATE_PATH_FROM_ID(
    _Inout_ PUNICODE_STRING FileName,
    _In_ ULONG IdLowPart,
    _In_ ULONG IdHighPart)
{
    WCHAR FileNameSuffix[RESOURCE_HUB_CONNECTION_FILE_CHARS];
    NTSTATUS Status;

    NT_ASSERT(FileName->MaximumLength >= RESOURCE_HUB_CONNECTION_PATH_SIZE);

    Status = RESOURCE_HUB_ID_TO_FILE_NAME(IdLowPart,
                                           IdHighPart,
                                           FileNameSuffix);
    if (NT_SUCCESS(Status))
    {
        Status = RtlUnicodeStringPrintf(FileName,
                                        L"%s%s",
                                        RESOURCE_HUB_DEVICE_NAME_PREFIX,
                                        FileNameSuffix);
    }

    return Status;
}
#endif
