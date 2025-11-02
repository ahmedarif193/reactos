#pragma once

/*
 * Minimal subset of the EFI USB I/O protocol definitions needed by FreeLDR.
 * Based on the UEFI specification and the EDK2 UsbIo protocol header.
 * Copyright : Ahmed ARIF <arif.ing@outlook.com>
 */

#include <Uefi.h>

#define EFI_USB_IO_PROTOCOL_GUID \
  { 0x2b2f68d6, 0x0cd2, 0x44cf, { 0x8e, 0x8d, 0xd0, 0x0f, 0xf7, 0x03, 0xb0, 0xcd } }

/* USB transfer status codes */
#define EFI_USB_NOERROR                    0x00
#define EFI_USB_ERR_STALL                  0x02

/* Standard USB requests */
#define USB_REQ_CLEAR_FEATURE              0x01

/* Standard USB features */
#define USB_FEATURE_ENDPOINT_HALT          0x00

/* bmRequestType helpers */
#define USB_BMRT_HOST_TO_DEVICE            0x00
#define USB_BMRT_DEVICE_TO_HOST            0x80
#define USB_BMRT_TYPE_STANDARD             0x00
#define USB_BMRT_TYPE_CLASS                0x20
#define USB_BMRT_RECIP_INTERFACE           0x01
#define USB_BMRT_RECIP_ENDPOINT            0x02

/* USB descriptor types and helpers */
#define EFI_USB_ENDPOINT_DESCRIPTOR_TYPE   0x05
#define EFI_USB_ENDPOINT_TYPE_MASK         0x03
#define EFI_USB_ENDPOINT_CONTROL           0x00
#define EFI_USB_ENDPOINT_ISO               0x01
#define EFI_USB_ENDPOINT_BULK              0x02
#define EFI_USB_ENDPOINT_INTERRUPT         0x03
#define EFI_USB_ENDPOINT_DIR_IN            0x80

#pragma pack(push, 1)
typedef struct {
  UINT8   RequestType;
  UINT8   Request;
  UINT16  Value;
  UINT16  Index;
  UINT16  Length;
} EFI_USB_DEVICE_REQUEST;

typedef struct {
  UINT8   Length;
  UINT8   DescriptorType;
  UINT16  BcdUSB;
  UINT8   DeviceClass;
  UINT8   DeviceSubClass;
  UINT8   DeviceProtocol;
  UINT8   MaxPacketSize0;
  UINT16  IdVendor;
  UINT16  IdProduct;
  UINT16  BcdDevice;
  UINT8   StrManufacturer;
  UINT8   StrProduct;
  UINT8   StrSerialNumber;
  UINT8   NumConfigurations;
} EFI_USB_DEVICE_DESCRIPTOR;

typedef struct {
  UINT8   Length;
  UINT8   DescriptorType;
  UINT16  TotalLength;
  UINT8   NumInterfaces;
  UINT8   ConfigurationValue;
  UINT8   Configuration;
  UINT8   Attributes;
  UINT8   MaxPower;
} EFI_USB_CONFIG_DESCRIPTOR;

typedef struct {
  UINT8   Length;
  UINT8   DescriptorType;
  UINT8   InterfaceNumber;
  UINT8   AlternateSetting;
  UINT8   NumEndpoints;
  UINT8   InterfaceClass;
  UINT8   InterfaceSubClass;
  UINT8   InterfaceProtocol;
  UINT8   Interface;
} EFI_USB_INTERFACE_DESCRIPTOR;

typedef struct {
  UINT8   Length;
  UINT8   DescriptorType;
  UINT8   EndpointAddress;
  UINT8   Attributes;
  UINT16  MaxPacketSize;
  UINT8   Interval;
} EFI_USB_ENDPOINT_DESCRIPTOR;
#pragma pack(pop)

typedef enum {
  EfiUsbDataIn,
  EfiUsbDataOut,
  EfiUsbNoData
} EFI_USB_DATA_DIRECTION;

typedef struct _EFI_USB_IO_PROTOCOL EFI_USB_IO_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *EFI_USB_IO_CONTROL_TRANSFER)(
  IN EFI_USB_IO_PROTOCOL            *This,
  IN EFI_USB_DEVICE_REQUEST         *Request,
  IN EFI_USB_DATA_DIRECTION         Direction,
  IN UINT32                         Timeout,
  IN OUT VOID                       *Data OPTIONAL,
  IN UINTN                          DataLength OPTIONAL,
  OUT UINT32                        *Status
  );

typedef
EFI_STATUS
(EFIAPI *EFI_USB_IO_BULK_TRANSFER)(
  IN EFI_USB_IO_PROTOCOL            *This,
  IN UINT8                          DeviceEndpoint,
  IN OUT VOID                       *Data,
  IN OUT UINTN                      *DataLength,
  IN UINTN                          Timeout,
  OUT UINT32                        *Status
  );

typedef
EFI_STATUS
(EFIAPI *EFI_USB_IO_GET_INTERFACE_DESCRIPTOR)(
  IN EFI_USB_IO_PROTOCOL            *This,
  OUT EFI_USB_INTERFACE_DESCRIPTOR  *InterfaceDescriptor
  );

typedef
EFI_STATUS
(EFIAPI *EFI_USB_IO_GET_ENDPOINT_DESCRIPTOR)(
  IN EFI_USB_IO_PROTOCOL            *This,
  IN UINT8                          EndpointIndex,
  OUT EFI_USB_ENDPOINT_DESCRIPTOR   *EndpointDescriptor
  );

struct _EFI_USB_IO_PROTOCOL {
  EFI_USB_IO_CONTROL_TRANSFER          UsbControlTransfer;
  EFI_USB_IO_BULK_TRANSFER             UsbBulkTransfer;
  VOID                                *UsbAsyncInterruptTransfer;
  VOID                                *UsbSyncInterruptTransfer;
  VOID                                *UsbIsochronousTransfer;
  VOID                                *UsbAsyncIsochronousTransfer;
  VOID                                *UsbGetDeviceDescriptor;
  VOID                                *UsbGetConfigDescriptor;
  EFI_USB_IO_GET_INTERFACE_DESCRIPTOR  UsbGetInterfaceDescriptor;
  EFI_USB_IO_GET_ENDPOINT_DESCRIPTOR   UsbGetEndpointDescriptor;
  VOID                                *UsbGetStringDescriptor;
  VOID                                *UsbGetSupportedLanguages;
  VOID                                *UsbPortReset;
};

extern EFI_GUID gEfiUsbIoProtocolGuid;
