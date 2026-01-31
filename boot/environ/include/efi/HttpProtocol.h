/** @file
  EFI HTTP Protocol and HTTP Service Binding Protocol definitions.

  Minimal definitions for protocol presence detection during UEFI boot.
  Full method signatures will be added when HTTP boot is implemented.

  Based on UEFI 2.5+ HTTP Boot specification.

  Copyright (c) 2025 ReactOS Team
  SPDX-License-Identifier: GPL-2.0-or-later
**/

#ifndef __HTTP_PROTOCOL_H__
#define __HTTP_PROTOCOL_H__

/* -----------------------------------------------------------------------
 * EFI_HTTP_SERVICE_BINDING_PROTOCOL
 * Used to create / destroy child HTTP protocol instances.
 * ----------------------------------------------------------------------- */

#define EFI_HTTP_SERVICE_BINDING_PROTOCOL_GUID \
  { \
    0xBDC8E6AF, 0xD9BC, 0x4379, {0xA7, 0x2A, 0xE0, 0xC4, 0xE7, 0x5D, 0xAE, 0x1C } \
  }

#ifndef EFI_SERVICE_BINDING_PROTOCOL_DEFINED
#define EFI_SERVICE_BINDING_PROTOCOL_DEFINED

typedef struct _EFI_SERVICE_BINDING_PROTOCOL EFI_SERVICE_BINDING_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_SERVICE_BINDING_CREATE_CHILD)(
  IN     EFI_SERVICE_BINDING_PROTOCOL  *This,
  IN OUT EFI_HANDLE                    *ChildHandle
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SERVICE_BINDING_DESTROY_CHILD)(
  IN EFI_SERVICE_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                    ChildHandle
  );

struct _EFI_SERVICE_BINDING_PROTOCOL {
    EFI_SERVICE_BINDING_CREATE_CHILD   CreateChild;
    EFI_SERVICE_BINDING_DESTROY_CHILD  DestroyChild;
};

#endif /* EFI_SERVICE_BINDING_PROTOCOL_DEFINED */

/* -----------------------------------------------------------------------
 * EFI_HTTP_PROTOCOL
 * ----------------------------------------------------------------------- */

#define EFI_HTTP_PROTOCOL_GUID \
  { \
    0x7A59B29B, 0x910B, 0x4171, {0x82, 0x42, 0xA8, 0x5A, 0x0D, 0xF2, 0x5B, 0x5B } \
  }

typedef struct _EFI_HTTP_PROTOCOL EFI_HTTP_PROTOCOL;

///
/// HTTP methods.
///
typedef enum {
    HttpMethodGet,
    HttpMethodPost,
    HttpMethodPatch,
    HttpMethodOptions,
    HttpMethodConnect,
    HttpMethodHead,
    HttpMethodPut,
    HttpMethodDelete,
    HttpMethodTrace,
    HttpMethodMax
} EFI_HTTP_METHOD;

///
/// HTTP version.
///
typedef enum {
    HttpVersion10,
    HttpVersion11,
    HttpVersionUnsupported
} EFI_HTTP_VERSION;

///
/// HTTP status codes (subset).
///
typedef enum {
    HTTP_STATUS_UNSUPPORTED_STATUS = 0,
    HTTP_STATUS_100_CONTINUE,
    HTTP_STATUS_200_OK,
    HTTP_STATUS_201_CREATED,
    HTTP_STATUS_202_ACCEPTED,
    HTTP_STATUS_204_NO_CONTENT,
    HTTP_STATUS_206_PARTIAL_CONTENT,
    HTTP_STATUS_301_MOVED_PERMANENTLY,
    HTTP_STATUS_302_FOUND,
    HTTP_STATUS_304_NOT_MODIFIED,
    HTTP_STATUS_400_BAD_REQUEST,
    HTTP_STATUS_401_UNAUTHORIZED,
    HTTP_STATUS_403_FORBIDDEN,
    HTTP_STATUS_404_NOT_FOUND,
    HTTP_STATUS_405_METHOD_NOT_ALLOWED,
    HTTP_STATUS_408_REQUEST_TIMEOUT,
    HTTP_STATUS_500_INTERNAL_SERVER_ERROR,
    HTTP_STATUS_503_SERVICE_UNAVAILABLE
} EFI_HTTP_STATUS_CODE;

///
/// HTTP header field key/value pair.
///
typedef struct {
    CHAR8   *FieldName;
    CHAR8   *FieldValue;
} EFI_HTTP_HEADER;

///
/// HTTP request data.
///
typedef struct {
    EFI_HTTP_METHOD  Method;
    CHAR16           *Url;
} EFI_HTTP_REQUEST_DATA;

///
/// HTTP response data.
///
typedef struct {
    EFI_HTTP_STATUS_CODE  StatusCode;
} EFI_HTTP_RESPONSE_DATA;

///
/// HTTP message (request or response).
///
typedef union {
    EFI_HTTP_REQUEST_DATA   *Request;
    EFI_HTTP_RESPONSE_DATA  *Response;
} EFI_HTTP_MESSAGE_DATA;

typedef struct {
    EFI_HTTP_MESSAGE_DATA  Data;
    UINTN                  HeaderCount;
    EFI_HTTP_HEADER        *Headers;
    UINTN                  BodyLength;
    VOID                   *Body;
} EFI_HTTP_MESSAGE;

///
/// IPv4 access point for HTTP configuration.
///
typedef struct {
    BOOLEAN            UseDefaultAddress;
    EFI_IPv4_ADDRESS   LocalAddress;
    EFI_IPv4_ADDRESS   LocalSubnet;
    UINT16             LocalPort;
} EFI_HTTPv4_ACCESS_POINT;

///
/// IPv6 access point for HTTP configuration.
///
typedef struct {
    EFI_IPv6_ADDRESS   LocalAddress;
    UINT16             LocalPort;
} EFI_HTTPv6_ACCESS_POINT;

///
/// HTTP configuration data.
///
typedef struct {
    EFI_HTTP_VERSION   HttpVersion;
    UINT32             TimeOutMillisec;
    BOOLEAN            LocalAddressIsIPv6;
    union {
        EFI_HTTPv4_ACCESS_POINT  *IPv4Node;
        EFI_HTTPv6_ACCESS_POINT  *IPv6Node;
    } AccessPoint;
} EFI_HTTP_CONFIG_DATA;

///
/// HTTP token used for async request/response.
///
typedef struct {
    EFI_EVENT           Event;
    EFI_STATUS          Status;
    EFI_HTTP_MESSAGE    *Message;
} EFI_HTTP_TOKEN;

typedef
EFI_STATUS
(EFIAPI *EFI_HTTP_GET_MODE_DATA)(
  IN  EFI_HTTP_PROTOCOL    *This,
  OUT EFI_HTTP_CONFIG_DATA *HttpConfigData
  );

typedef
EFI_STATUS
(EFIAPI *EFI_HTTP_CONFIGURE)(
  IN EFI_HTTP_PROTOCOL      *This,
  IN EFI_HTTP_CONFIG_DATA   *HttpConfigData OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_HTTP_REQUEST)(
  IN EFI_HTTP_PROTOCOL  *This,
  IN EFI_HTTP_TOKEN     *Token
  );

typedef
EFI_STATUS
(EFIAPI *EFI_HTTP_CANCEL)(
  IN EFI_HTTP_PROTOCOL  *This,
  IN EFI_HTTP_TOKEN     *Token OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_HTTP_RESPONSE)(
  IN EFI_HTTP_PROTOCOL  *This,
  IN EFI_HTTP_TOKEN     *Token
  );

typedef
EFI_STATUS
(EFIAPI *EFI_HTTP_POLL)(
  IN EFI_HTTP_PROTOCOL  *This
  );

struct _EFI_HTTP_PROTOCOL {
    EFI_HTTP_GET_MODE_DATA  GetModeData;
    EFI_HTTP_CONFIGURE      Configure;
    EFI_HTTP_REQUEST        Request;
    EFI_HTTP_CANCEL         Cancel;
    EFI_HTTP_RESPONSE       Response;
    EFI_HTTP_POLL           Poll;
};

/* -----------------------------------------------------------------------
 * EFI_HTTP_UTILITIES_PROTOCOL
 * Helper protocol for building/parsing HTTP headers.
 * ----------------------------------------------------------------------- */

#define EFI_HTTP_UTILITIES_PROTOCOL_GUID \
  { \
    0x3E35C163, 0x4074, 0x45DD, {0x43, 0x1E, 0x23, 0x98, 0x9D, 0xD8, 0x6B, 0x32 } \
  }

extern EFI_GUID gEfiHttpServiceBindingProtocolGuid;
extern EFI_GUID gEfiHttpProtocolGuid;
extern EFI_GUID gEfiHttpUtilitiesProtocolGuid;

#endif
