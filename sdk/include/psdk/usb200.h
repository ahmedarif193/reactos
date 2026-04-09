/*
 * usb200.h
 *
 * This file is part of the ReactOS PSDK package.
 *
 * Contributors:
 *   Magnus Olsen.
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#pragma once

/* Helper macro to enable gcc's extension. */
#ifndef __GNU_EXTENSION
#ifdef __GNUC__
#define __GNU_EXTENSION __extension__
#else
#define __GNU_EXTENSION
#endif
#endif

#include "usb100.h"

#include <pshpack1.h>

typedef enum _USB_DEVICE_TYPE {
  Usb11Device = 0,
  Usb20Device
} USB_DEVICE_TYPE;

/* Keep this in sync with Windows 10 usbspec.h */
typedef enum _USB_DEVICE_SPEED {
  UsbLowSpeed = 0,
  UsbFullSpeed,
  UsbHighSpeed,
  UsbSuperSpeed
} USB_DEVICE_SPEED;

#define USB_PORT_STATUS_CONNECT                       0x0001
#define USB_PORT_STATUS_ENABLE                        0x0002
#define USB_PORT_STATUS_SUSPEND                       0x0004
#define USB_PORT_STATUS_OVER_CURRENT                  0x0008
#define USB_PORT_STATUS_RESET                         0x0010
#define USB_PORT_STATUS_POWER                         0x0100
#define USB_PORT_STATUS_LOW_SPEED                     0x0200
#define USB_PORT_STATUS_HIGH_SPEED                    0x0400


typedef union _BM_REQUEST_TYPE {
#ifdef __cplusplus
  struct {
#else
  struct _BM {
#endif
    UCHAR Recipient:2;
    UCHAR Reserved:3;
    UCHAR Type:2;
    UCHAR Dir:1;
  };
  UCHAR B;
} BM_REQUEST_TYPE, *PBM_REQUEST_TYPE;

typedef struct _USB_DEFAULT_PIPE_SETUP_PACKET {
  BM_REQUEST_TYPE bmRequestType;
  UCHAR bRequest;
  union _wValue {
    __GNU_EXTENSION struct {
      UCHAR LowByte;
      UCHAR HiByte;
    };
    USHORT W;
  } wValue;
  union _wIndex {
    __GNU_EXTENSION struct {
      UCHAR LowByte;
      UCHAR HiByte;
    };
    USHORT W;
  } wIndex;
  USHORT wLength;
} USB_DEFAULT_PIPE_SETUP_PACKET, *PUSB_DEFAULT_PIPE_SETUP_PACKET;

C_ASSERT(sizeof(USB_DEFAULT_PIPE_SETUP_PACKET) == 8);

#define USB_DEVICE_QUALIFIER_DESCRIPTOR_TYPE          0x06
#define USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE 0x07

typedef struct _USB_DEVICE_QUALIFIER_DESCRIPTOR {
  UCHAR bLength;
  UCHAR bDescriptorType;
  USHORT bcdUSB;
  UCHAR bDeviceClass;
  UCHAR bDeviceSubClass;
  UCHAR bDeviceProtocol;
  UCHAR bMaxPacketSize0;
  UCHAR bNumConfigurations;
  UCHAR bReserved;
} USB_DEVICE_QUALIFIER_DESCRIPTOR, *PUSB_DEVICE_QUALIFIER_DESCRIPTOR;

/*
 * USB 3.0: 9.6.2 Binary Device Object Store (BOS), Table 9-9. BOS Descriptor.
 */
typedef struct _USB_BOS_DESCRIPTOR {
  UCHAR  bLength;
  UCHAR  bDescriptorType;
  USHORT wTotalLength;
  UCHAR  bNumDeviceCaps;
} USB_BOS_DESCRIPTOR, *PUSB_BOS_DESCRIPTOR;

/*
 * Descriptor type for BOS, matches Windows and USB 3.x specifications.
 */
#define USB_BOS_DESCRIPTOR_TYPE                             0x0F

C_ASSERT(sizeof(USB_BOS_DESCRIPTOR) == 5);

/*
 * USB 3.x Device Capability Type Codes (subset used by ReactOS).
 */
#define USB_DEVICE_CAPABILITY_WIRELESS_USB                  0x01
#define USB_DEVICE_CAPABILITY_USB20_EXTENSION               0x02
#define USB_DEVICE_CAPABILITY_SUPERSPEED_USB                0x03
#define USB_DEVICE_CAPABILITY_CONTAINER_ID                  0x04

/*
 * USB 2.0 ECN: Link Power Management (LPM) / USB 3.0: USB 2.0 Extension Descriptor.
 * Keep layout in sync with Windows 10 usbspec.h.
 */
typedef struct _USB_DEVICE_CAPABILITY_USB20_EXTENSION_DESCRIPTOR {
  UCHAR bLength;
  UCHAR bDescriptorType;
  UCHAR bDevCapabilityType;
  union {
    ULONG AsUlong;
    struct {
      ULONG Reserved:1;
      ULONG LPMCapable:1;
      ULONG BESLAndAlternateHIRDSupported:1;
      ULONG BaselineBESLValid:1;
      ULONG DeepBESLValid:1;
      ULONG Reserved1:3;
      ULONG BaselineBESL:4;
      ULONG DeepBESL:4;
      ULONG Reserved2:16;
    };
  } bmAttributes;
} USB_DEVICE_CAPABILITY_USB20_EXTENSION_DESCRIPTOR,
  *PUSB_DEVICE_CAPABILITY_USB20_EXTENSION_DESCRIPTOR;

C_ASSERT(sizeof(USB_DEVICE_CAPABILITY_USB20_EXTENSION_DESCRIPTOR) == 7);

#define USB_DEVICE_CAPABILITY_USB20_EXTENSION_BMATTRIBUTES_RESERVED_MASK 0xFFFF00E1

/*
 * USB 3.0: SuperSpeed USB Device Capability descriptor.
 */
typedef struct _USB_DEVICE_CAPABILITY_SUPERSPEED_USB_DESCRIPTOR {
  UCHAR  bLength;
  UCHAR  bDescriptorType;
  UCHAR  bDevCapabilityType;
  UCHAR  bmAttributes;
  USHORT wSpeedsSupported;
  UCHAR  bFunctionalitySupport;
  UCHAR  bU1DevExitLat;
  USHORT wU2DevExitLat;
} USB_DEVICE_CAPABILITY_SUPERSPEED_USB_DESCRIPTOR,
  *PUSB_DEVICE_CAPABILITY_SUPERSPEED_USB_DESCRIPTOR;

#define USB_DEVICE_CAPABILITY_SUPERSPEED_BMATTRIBUTES_RESERVED_MASK 0xFD
#define USB_DEVICE_CAPABILITY_SUPERSPEED_BMATTRIBUTES_LTM_CAPABLE   0x02

#define USB_DEVICE_CAPABILITY_SUPERSPEED_SPEEDS_SUPPORTED_RESERVED_MASK  0xFFF0
#define USB_DEVICE_CAPABILITY_SUPERSPEED_SPEEDS_SUPPORTED_LOW            0x0001
#define USB_DEVICE_CAPABILITY_SUPERSPEED_SPEEDS_SUPPORTED_FULL           0x0002
#define USB_DEVICE_CAPABILITY_SUPERSPEED_SPEEDS_SUPPORTED_HIGH           0x0004
#define USB_DEVICE_CAPABILITY_SUPERSPEED_SPEEDS_SUPPORTED_SUPER          0x0008

#define USB_DEVICE_CAPABILITY_SUPERSPEED_U1_DEVICE_EXIT_MAX_VALUE 0x0A
#define USB_DEVICE_CAPABILITY_SUPERSPEED_U2_DEVICE_EXIT_MAX_VALUE 0x07FF

C_ASSERT(sizeof(USB_DEVICE_CAPABILITY_SUPERSPEED_USB_DESCRIPTOR) == 10);

/*
 * USB 3.0: Container ID Device Capability descriptor.
 */
typedef struct _USB_DEVICE_CAPABILITY_CONTAINER_ID_DESCRIPTOR {
  UCHAR bLength;
  UCHAR bDescriptorType;
  UCHAR bDevCapabilityType;
  UCHAR bReserved;
  UCHAR ContainerID[16];
} USB_DEVICE_CAPABILITY_CONTAINER_ID_DESCRIPTOR,
  *PUSB_DEVICE_CAPABILITY_CONTAINER_ID_DESCRIPTOR;

C_ASSERT(sizeof(USB_DEVICE_CAPABILITY_CONTAINER_ID_DESCRIPTOR) == 20);

typedef union _USB_HIGH_SPEED_MAXPACKET {
  struct _MP {
    USHORT MaxPacket:11;
    USHORT HSmux:2;
    USHORT Reserved:3;
  } _MP;
  USHORT us;
} USB_HIGH_SPEED_MAXPACKET, *PUSB_HIGH_SPEED_MAXPACKET;

#define USB_INTERFACE_ASSOCIATION_DESCRIPTOR_TYPE     0x0B

typedef struct _USB_INTERFACE_ASSOCIATION_DESCRIPTOR {
  UCHAR bLength;
  UCHAR bDescriptorType;
  UCHAR bFirstInterface;
  UCHAR bInterfaceCount;
  UCHAR bFunctionClass;
  UCHAR bFunctionSubClass;
  UCHAR bFunctionProtocol;
  UCHAR iFunction;
} USB_INTERFACE_ASSOCIATION_DESCRIPTOR, *PUSB_INTERFACE_ASSOCIATION_DESCRIPTOR;

typedef union _USB_20_PORT_STATUS {
  USHORT AsUshort16;
  struct {
    USHORT CurrentConnectStatus:1;
    USHORT PortEnabledDisabled:1;
    USHORT Suspend:1;
    USHORT OverCurrent:1;
    USHORT Reset:1;
    USHORT L1:1;
    USHORT Reserved0:2;
    USHORT PortPower:1;
    USHORT LowSpeedDeviceAttached:1;
    USHORT HighSpeedDeviceAttached:1;
    USHORT PortTestMode:1;
    USHORT PortIndicatorControl:1;
    USHORT Reserved1:3;
  };
} USB_20_PORT_STATUS, *PUSB_20_PORT_STATUS;

C_ASSERT(sizeof(USB_20_PORT_STATUS) == sizeof(USHORT));

#define USB_PORT_STATUS_CONNECT       0x0001
#define USB_PORT_STATUS_ENABLE        0x0002
#define USB_PORT_STATUS_SUSPEND       0x0004
#define USB_PORT_STATUS_OVER_CURRENT  0x0008
#define USB_PORT_STATUS_RESET         0x0010
#define USB_PORT_STATUS_POWER         0x0100
#define USB_PORT_STATUS_LOW_SPEED     0x0200
#define USB_PORT_STATUS_HIGH_SPEED    0x0400

typedef union _USB_20_PORT_CHANGE {
  USHORT AsUshort16;
  struct {
    USHORT ConnectStatusChange:1;
    USHORT PortEnableDisableChange:1;
    USHORT SuspendChange:1;
    USHORT OverCurrentIndicatorChange:1;
    USHORT ResetChange:1;
    USHORT Reserved2:11;
  };
} USB_20_PORT_CHANGE, *PUSB_20_PORT_CHANGE;

C_ASSERT(sizeof(USB_20_PORT_CHANGE) == sizeof(USHORT));

typedef union _USB_30_PORT_STATUS {
  USHORT AsUshort16;
  struct {
    USHORT CurrentConnectStatus:1;
    USHORT PortEnabledDisabled:1;
    USHORT Reserved0:1;
    USHORT OverCurrent:1;
    USHORT Reset:1;
    USHORT PortLinkState:4;
    USHORT PortPower:1;
    USHORT NegotiatedDeviceSpeed:3;
    USHORT Reserved1:3;
  };
} USB_30_PORT_STATUS, *PUSB_30_PORT_STATUS;

C_ASSERT(sizeof(USB_30_PORT_STATUS) == sizeof(USHORT));

#define PORT_LINK_STATE_U0               0
#define PORT_LINK_STATE_U1               1
#define PORT_LINK_STATE_U2               2
#define PORT_LINK_STATE_U3               3
#define PORT_LINK_STATE_DISABLED         4
#define PORT_LINK_STATE_RX_DETECT        5
#define PORT_LINK_STATE_INACTIVE         6
#define PORT_LINK_STATE_POLLING          7
#define PORT_LINK_STATE_RECOVERY         8
#define PORT_LINK_STATE_HOT_RESET        9
#define PORT_LINK_STATE_COMPLIANCE_MODE  10
#define PORT_LINK_STATE_LOOPBACK         11
#define PORT_LINK_STATE_TEST_MODE        11 // xHCI-specific, replacing LOOPBACK

typedef union _USB_30_PORT_CHANGE {
  USHORT AsUshort16;
  struct {
    USHORT ConnectStatusChange :1;
    USHORT Reserved2 :2;
    USHORT OverCurrentIndicatorChange :1;
    USHORT ResetChange :1;
    USHORT BHResetChange :1;
    USHORT PortLinkStateChange :1;
    USHORT PortConfigErrorChange :1;
    USHORT Reserved3 :8;
  };
} USB_30_PORT_CHANGE, *PUSB_30_PORT_CHANGE;

C_ASSERT(sizeof(USB_30_PORT_CHANGE) == sizeof(USHORT));

typedef union _USB_PORT_STATUS {
  USHORT AsUshort16;
  USB_20_PORT_STATUS Usb20PortStatus;
  USB_30_PORT_STATUS Usb30PortStatus;
} USB_PORT_STATUS, *PUSB_PORT_STATUS;

C_ASSERT(sizeof(USB_PORT_STATUS) == sizeof(USHORT));

typedef union _USB_PORT_CHANGE {
  USHORT AsUshort16;
  USB_20_PORT_CHANGE Usb20PortChange;
  USB_30_PORT_CHANGE Usb30PortChange;
} USB_PORT_CHANGE, *PUSB_PORT_CHANGE;

C_ASSERT(sizeof(USB_PORT_CHANGE) == sizeof(USHORT));

typedef union _USB_PORT_STATUS_AND_CHANGE {
  ULONG AsUlong32;
  struct {
    USB_PORT_STATUS PortStatus;
    USB_PORT_CHANGE PortChange;
  };
} USB_PORT_STATUS_AND_CHANGE, *PUSB_PORT_STATUS_AND_CHANGE;

C_ASSERT(sizeof(USB_PORT_STATUS_AND_CHANGE) == sizeof(ULONG));

typedef union _USB_HUB_STATUS {
  USHORT AsUshort16;
  struct {
    USHORT LocalPowerLost:1;
    USHORT OverCurrent:1;
    USHORT Reserved:14;
  };
} USB_HUB_STATUS, *PUSB_HUB_STATUS;

C_ASSERT(sizeof(USB_HUB_STATUS) == sizeof(USHORT));

typedef union _USB_HUB_CHANGE {
  USHORT AsUshort16;
  struct {
    USHORT LocalPowerChange:1;
    USHORT OverCurrentChange:1;
    USHORT Reserved:14;
  };
} USB_HUB_CHANGE, *PUSB_HUB_CHANGE;

C_ASSERT(sizeof(USB_HUB_CHANGE) == sizeof(USHORT));

/*
 * USB 3.0 Hub Descriptor
 *
 * Modeled after the USB 3.x specification (Hub Descriptor, Table 10-XX)
 * and kept layout-compatible with Windows 10's internal definition.
 * The exact bitmap semantics of the removable/power masks are not used
 * by the current ReactOS stack; they are treated as opaque bytes.
 */
typedef struct _USB_30_HUB_DESCRIPTOR {
  UCHAR  bLength;
  UCHAR  bDescriptorType;      /* USB_30_HUB_DESCRIPTOR_TYPE (0x2A) */
  UCHAR  bNumberOfPorts;
  USHORT wHubCharacteristics;
  UCHAR  bPowerOnToPowerGood;
  UCHAR  bHubControlCurrent;
  UCHAR  bHubHdrDecLat;
  USHORT wHubDelay;
  UCHAR  DeviceRemovable[64];
} USB_30_HUB_DESCRIPTOR, *PUSB_30_HUB_DESCRIPTOR;


typedef union _USB_HUB_STATUS_AND_CHANGE {
  ULONG AsUlong32;
  struct {
    USB_HUB_STATUS HubStatus;
    USB_HUB_CHANGE HubChange;
  };
} USB_HUB_STATUS_AND_CHANGE, *PUSB_HUB_STATUS_AND_CHANGE;

C_ASSERT(sizeof(USB_HUB_STATUS_AND_CHANGE) == sizeof(ULONG));

#define USB_20_HUB_DESCRIPTOR_TYPE  0x29
#define USB_30_HUB_DESCRIPTOR_TYPE  0x2A

#define USB_REQUEST_CLEAR_TT_BUFFER     0x08
#define USB_REQUEST_RESET_TT            0x09
#define USB_REQUEST_GET_TT_STATE        0x0A
#define USB_REQUEST_STOP_TT             0x0B

#define USB_REQUEST_SET_HUB_DEPTH       0x0C
#define USB_REQUEST_GET_PORT_ERR_COUNT  0x0D

#define USB_DEVICE_CLASS_RESERVED             0x00
#define USB_DEVICE_CLASS_AUDIO                0x01
#define USB_DEVICE_CLASS_COMMUNICATIONS       0x02
#define USB_DEVICE_CLASS_HUMAN_INTERFACE      0x03
#define USB_DEVICE_CLASS_MONITOR              0x04
#define USB_DEVICE_CLASS_PHYSICAL_INTERFACE   0x05
#define USB_DEVICE_CLASS_POWER                0x06
#define USB_DEVICE_CLASS_IMAGE                0x06
#define USB_DEVICE_CLASS_PRINTER              0x07
#define USB_DEVICE_CLASS_STORAGE              0x08
#define USB_DEVICE_CLASS_HUB                  0x09
#define USB_DEVICE_CLASS_CDC_DATA             0x0A
#define USB_DEVICE_CLASS_SMART_CARD           0x0B
#define USB_DEVICE_CLASS_CONTENT_SECURITY     0x0D
#define USB_DEVICE_CLASS_VIDEO                0x0E
#define USB_DEVICE_CLASS_PERSONAL_HEALTHCARE  0x0F
#define USB_DEVICE_CLASS_AUDIO_VIDEO          0x10
#define USB_DEVICE_CLASS_BILLBOARD            0x11
#define USB_DEVICE_CLASS_DIAGNOSTIC_DEVICE    0xDC
#define USB_DEVICE_CLASS_WIRELESS_CONTROLLER  0xE0
#define USB_DEVICE_CLASS_MISCELLANEOUS        0xEF
#define USB_DEVICE_CLASS_APPLICATION_SPECIFIC 0xFE
#define USB_DEVICE_CLASS_VENDOR_SPECIFIC      0xFF

#include <poppack.h>
