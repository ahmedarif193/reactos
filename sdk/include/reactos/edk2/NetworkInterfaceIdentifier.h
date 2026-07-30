/** @file
  EFI Network Interface Identifier Protocol definitions.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#pragma once

/* Retired in UEFI 2.1b. */
#define EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_GUID \
  { \
    0xE18541CD, 0xF755, 0x4f73, {0x92, 0x8D, 0x64, 0x3C, 0x8A, 0x79, 0xB2, 0x29 } \
  }

/* Introduced in UEFI 2.1b. */
#define EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_GUID_31 \
  { \
    0x1ACED566, 0x76ED, 0x4218, {0xBC, 0x81, 0x76, 0x7F, 0x1F, 0x97, 0x7A, 0x89 } \
  }

#define EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_REVISION 0x00020000
#define EFI_NETWORK_INTERFACE_IDENTIFIER_INTERFACE_REVISION \
    EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL_REVISION

typedef struct _EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL
    EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL;
typedef EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL
    EFI_NETWORK_INTERFACE_IDENTIFIER_INTERFACE;

struct _EFI_NETWORK_INTERFACE_IDENTIFIER_PROTOCOL {
    UINT64  Revision;
    UINT64  Id;
    UINT64  ImageAddr;
    UINT32  ImageSize;
    CHAR8   StringId[4];
    UINT8   Type;
    UINT8   MajorVer;
    UINT8   MinorVer;
    BOOLEAN Ipv6Supported;
    UINT16  IfNum;
};

typedef enum {
    EfiNetworkInterfaceUndi = 1
} EFI_NETWORK_INTERFACE_TYPE;
