/** @file
  EFI IPv4 Configuration II Protocol definitions.

  Copyright (c) 2015 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#pragma once

#define EFI_IP4_CONFIG2_PROTOCOL_GUID \
  { \
    0x5b446ed1, 0xe30b, 0x4faa, {0x87, 0x1a, 0x36, 0x54, 0xec, 0xa3, 0x60, 0x80 } \
  }

typedef struct _EFI_IP4_CONFIG2_PROTOCOL EFI_IP4_CONFIG2_PROTOCOL;

typedef enum {
    Ip4Config2DataTypeInterfaceInfo,
    Ip4Config2DataTypePolicy,
    Ip4Config2DataTypeManualAddress,
    Ip4Config2DataTypeGateway,
    Ip4Config2DataTypeDnsServer,
    Ip4Config2DataTypeMaximum
} EFI_IP4_CONFIG2_DATA_TYPE;

typedef enum {
    Ip4Config2PolicyStatic,
    Ip4Config2PolicyDhcp,
    Ip4Config2PolicyMax
} EFI_IP4_CONFIG2_POLICY;

typedef struct {
    EFI_IPv4_ADDRESS Address;
    EFI_IPv4_ADDRESS SubnetMask;
} EFI_IP4_CONFIG2_MANUAL_ADDRESS;

#define EFI_IP4_CONFIG2_INTERFACE_INFO_NAME_SIZE 32

typedef struct {
    EFI_IPv4_ADDRESS SubnetAddress;
    EFI_IPv4_ADDRESS SubnetMask;
    EFI_IPv4_ADDRESS GatewayAddress;
} EFI_IP4_ROUTE_TABLE;

typedef struct {
    CHAR16              Name[EFI_IP4_CONFIG2_INTERFACE_INFO_NAME_SIZE];
    UINT8               IfType;
    UINT32              HwAddressSize;
    EFI_MAC_ADDRESS     HwAddress;
    EFI_IPv4_ADDRESS    StationAddress;
    EFI_IPv4_ADDRESS    SubnetMask;
    UINT32              RouteTableSize;
    EFI_IP4_ROUTE_TABLE *RouteTable OPTIONAL;
} EFI_IP4_CONFIG2_INTERFACE_INFO;

typedef
EFI_STATUS
(EFIAPI *EFI_IP4_CONFIG2_SET_DATA)(
  IN EFI_IP4_CONFIG2_PROTOCOL   *This,
  IN EFI_IP4_CONFIG2_DATA_TYPE  DataType,
  IN UINTN                      DataSize,
  IN VOID                       *Data
  );

typedef
EFI_STATUS
(EFIAPI *EFI_IP4_CONFIG2_GET_DATA)(
  IN     EFI_IP4_CONFIG2_PROTOCOL   *This,
  IN     EFI_IP4_CONFIG2_DATA_TYPE  DataType,
  IN OUT UINTN                      *DataSize,
  IN     VOID                       *Data OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_IP4_CONFIG2_REGISTER_NOTIFY)(
  IN EFI_IP4_CONFIG2_PROTOCOL   *This,
  IN EFI_IP4_CONFIG2_DATA_TYPE  DataType,
  IN EFI_EVENT                  Event
  );

typedef
EFI_STATUS
(EFIAPI *EFI_IP4_CONFIG2_UNREGISTER_NOTIFY)(
  IN EFI_IP4_CONFIG2_PROTOCOL   *This,
  IN EFI_IP4_CONFIG2_DATA_TYPE  DataType,
  IN EFI_EVENT                  Event
  );

struct _EFI_IP4_CONFIG2_PROTOCOL {
    EFI_IP4_CONFIG2_SET_DATA           SetData;
    EFI_IP4_CONFIG2_GET_DATA           GetData;
    EFI_IP4_CONFIG2_REGISTER_NOTIFY    RegisterDataNotify;
    EFI_IP4_CONFIG2_UNREGISTER_NOTIFY  UnregisterDataNotify;
};
