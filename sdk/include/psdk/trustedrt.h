/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Trusted runtime device interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define WTR_DEVICE_NAME  L"\\Device\\WindowsTrustedRT"
#define WTR_DEVICE_USER_NAME L"\\\\.\\WindowsTrustedRT"
#define WTR_DEVICE_NAME_USER_LINK L"\\DosDevices\\WindowsTrustedRT"

#define IOCTL_TR_SERVICE_QUERY      CTL_CODE(FILE_DEVICE_TRUST_ENV, 0, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_TR_EXECUTE_FUNCTION   CTL_CODE(FILE_DEVICE_TRUST_ENV, 1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_TR_ENUMERATE_SERVICES CTL_CODE(FILE_DEVICE_TRUST_ENV, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define TRUSTED_RUNTIME_INTERFACE_VERSION_1 1
#define TRUSTED_RUNTIME_INTERFACE_VERSION TRUSTED_RUNTIME_INTERFACE_VERSION_1

typedef struct _TR_SERVICE_INFORMATION_V1 {
    ULONG InterfaceVersion;
    ULONG ServiceMajorVersion;
    ULONG ServiceMinorVersion;
} TR_SERVICE_INFORMATION_V1, *PTR_SERVICE_INFORMATION_V1;

typedef TR_SERVICE_INFORMATION_V1 TR_SERVICE_INFORMATION,
                                  *PTR_SERVICE_INFORMATION;

typedef struct _TR_SERVICE_REQUEST_V1 {
    ULONG InterfaceVersion;
    ULONG ServiceMajorVersion;
    ULONG ServiceMinorVersion;
    ULONG FunctionCode;
    _Field_size_bytes_(InputBufferSize) PVOID64 InputBuffer;
    ULONG64 InputBufferSize;
    _Field_size_bytes_(OutputBufferSize) PVOID64 OutputBuffer;
    ULONG64 OutputBufferSize;
} TR_SERVICE_REQUEST_V1, *PTR_SERVICE_REQUEST_V1;

typedef TR_SERVICE_REQUEST_V1 TR_SERVICE_REQUEST, *PTR_SERVICE_REQUEST;

typedef struct _TR_SERVICE_REQUEST_RESPONSE_V1 {
    ULONG InterfaceVersion;
    ULONG64 BytesWritten;
} TR_SERVICE_REQUEST_RESPONSE_V1, *PTR_SERVICE_REQUEST_RESPONSE_V1;

typedef TR_SERVICE_REQUEST_RESPONSE_V1 TR_SERVICE_REQUEST_RESPONSE,
                                       *PTR_SERVICE_REQUEST_RESPONSE;
