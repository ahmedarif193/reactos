/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Trusted execution environment variable service interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

DEFINE_GUID(GUID_EFI_VARIABLE_SERVICE,
    0x699aa2f1, 0xa42e, 0x40df, 0xba, 0xbe, 0x3a, 0xaa, 0xd2, 0xbb, 0x6a, 0x47);

#define EFI_VARIABLE_FUNCTION_GET_VARIABLE              0

#define EFI_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME    1

#define EFI_VARIABLE_FUNCTION_SET_VARIABLE              2

#define EFI_VARIABLE_FUNCTION_QUERY_VARIABLE_INFO       3

#define EFI_VARIABLE_NON_VOLATILE                           0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS                     0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS                         0x00000004
#define EFI_VARIABLE_HARDWARE_ERROR_RECORD                  0x00000008
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS             0x00000010
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS  0x00000020
#define EFI_VARIABLE_APPEND_WRITE                           0x00000040

typedef ULONG_PTR EFI_VARIABLE_STATUS, *PEFI_VARIABLE_STATUS;

typedef struct _EFI_GET_VARIABLE_IN {
    GUID VendorGuid;
    CHAR16 VariableName[ANYSIZE_ARRAY];
} EFI_GET_VARIABLE_IN, *PEFI_GET_VARIABLE_IN;

typedef struct _EFI_GET_VARIABLE_OUT {
    EFI_VARIABLE_STATUS EfiStatus;
    ULONG Attributes;
    SIZE_T DataSize;
    BYTE Data[ANYSIZE_ARRAY];
} EFI_GET_VARIABLE_OUT, *PEFI_GET_VARIABLE_OUT;

typedef struct _EFI_GET_NEXT_VARIABLE_NAME_IN {
    GUID VendorGuid;
    CHAR16 VariableName[ANYSIZE_ARRAY];
} EFI_GET_NEXT_VARIABLE_NAME_IN, *PEFI_GET_NEXT_VARIABLE_NAME_IN;

typedef struct _EFI_GET_NEXT_VARIABLE_NAME_OUT {
    EFI_VARIABLE_STATUS EfiStatus;
    GUID VendorGuid;
    ULONG NameLength;
    CHAR16 VariableName[ANYSIZE_ARRAY];
} EFI_GET_NEXT_VARIABLE_NAME_OUT, *PEFI_GET_NEXT_VARIABLE_NAME_OUT;

typedef struct _EFI_SET_VARIABLE_IN {

    ULONG VariableNameOffset;
    GUID VendorGuid;
    ULONG Attributes;
    SIZE_T DataSize;

    ULONG DataOffset;

    BYTE Buffer[ANYSIZE_ARRAY];
} EFI_SET_VARIABLE_IN, *PEFI_SET_VARIABLE_IN;

#define EFI_SET_VARIABLE_GET_VARIABLE_NAME(_SetVariable) \
            ((CHAR16*)(((ULONG_PTR)(_SetVariable)) + \
                       (_SetVariable)->VariableNameOffset))

#define EFI_SET_VARIABLE_GET_DATA(_SetVariable) \
            ((VOID*)(((ULONG_PTR)(_SetVariable)) + \
                     (_SetVariable)->DataOffset))

typedef struct _EFI_SET_VARIABLE_OUT {
    EFI_VARIABLE_STATUS EfiStatus;
} EFI_SET_VARIABLE_OUT, *PEFI_SET_VARIABLE_OUT;

typedef struct _EFI_QUERY_VARIABLE_INFO_IN {
    ULONG Attributes;
} EFI_QUERY_VARIABLE_INFO_IN, *PEFI_QUERY_VARIABLE_INFO_IN;

typedef struct _EFI_QUERY_VARIABLE_INFO_OUT {
    EFI_VARIABLE_STATUS EfiStatus;
    ULONGLONG MaximumVariableStorageSize;
    ULONGLONG RemainingVariableStorageSize;
    ULONGLONG MaximumVariableSize;
} EFI_QUERY_VARIABLE_INFO_OUT, *PEFI_QUERY_VARIABLE_INFO_OUT;
