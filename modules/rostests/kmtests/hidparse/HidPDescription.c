/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPLv2.1+ - See COPYING.LIB in the top level directory
 * PURPOSE:         HidParser description test
 * PROGRAMMER:      Thomas Faber <thomas.faber@reactos.org>
 */

#include <kmt_test.h>
#include <hidpddi.h>

#define NDEBUG
#include <debug.h>

#include "HidP.h"

static UCHAR ExampleKeyboardDescriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop), */
    0x09, 0x06,       /* Usage (Keyboard), */
    0xA1, 0x01,       /* Collection (Application), */
    0x05, 0x07,       /*   Usage Page (Key Codes); */
    0x19, 0xE0,       /*   Usage Minimum (224), */
    0x29, 0xE7,       /*   Usage Maximum (231), */
    0x15, 0x00,       /*   Logical Minimum (0), */
    0x25, 0x01,       /*   Logical Maximum (1), */
    0x75, 0x01,       /*   Report Size (1), */
    0x95, 0x08,       /*   Report Count (8), */
    0x81, 0x02,       /*   Input (Data, Variable, Absolute), ;Modifier byte */
    0x95, 0x01,       /*   Report Count (1), */
    0x75, 0x08,       /*   Report Size (8), */
    0x81, 0x01,       /*   Input (Constant), ;Reserved byte */
    0x95, 0x05,       /*   Report Count (5), */
    0x75, 0x01,       /*   Report Size (1), */
    0x05, 0x08,       /*   Usage Page (Page# for LEDs), */
    0x19, 0x01,       /*   Usage Minimum (1), */
    0x29, 0x05,       /*   Usage Maximum (5), */
    0x91, 0x02,       /*   Output (Data, Variable, Absolute), ;LED report */
    0x95, 0x01,       /*   Report Count (1), */
    0x75, 0x03,       /*   Report Size (3), */
    0x91, 0x01,       /*   Output (Constant), ;LED report padding */
    0x95, 0x06,       /*   Report Count (6), */
    0x75, 0x08,       /*   Report Size (8), */
    0x15, 0x00,       /*   Logical Minimum (0), */
    0x25, 0x65,       /*   Logical Maximum (101), */
    0x05, 0x07,       /*   Usage Page (Key Codes), */
    0x19, 0x00,       /*   Usage Minimum (0), */
    0x29, 0x65,       /*   Usage Maximum (101) */
    0x81, 0x00,       /*   Input (Data, Array), ;Key arrays (6 bytes) */
    0xC0              /* End Collection */
};

static UCHAR PowerProEliteDescriptor[] = {
    0x05, 0x01,       /* Usage Page (Generic Desktop), */
    0x09, 0x04,       /* Usage (Joystick), */
    0xa1, 0x01,       /* Collection (Application), */
    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0x01,       /*     Report ID (1) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x01,       /*     Report Count (1), */
    0x15, 0x00,       /*     Logical Minimum (0), */
    0x26, 0xff, 0x00, /*     Logical Maximum (255), */
    0x81, 0x03,       /*     Input (Constant, Variable, Absolute), */
    0x75, 0x01,       /*     Report Size (1), */
    0x95, 0x13,       /*     Report Count (19), */
    0x15, 0x00,       /*     Logical Minimum (0), */
    0x25, 0x01,       /*     Logical Maximum (1), */
    0x35, 0x00,       /*     Physical Minimum (0), */
    0x45, 0x01,       /*     Physical Maximum (1), */
    0x05, 0x09,       /*     Usage Page (Button), */
    0x19, 0x01,       /*     Usage Minimum (1), */
    0x29, 0x13,       /*     Usage Maximum (19), */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute), */
    0x75, 0x01,       /*     Report Size (1), */
    0x95, 0x0d,       /*     Report Count (13), */
    0x06, 0x00, 0xff, /*     Usage Page (Vendor-defined FF00), */
    0x81, 0x03,       /*     Input (Constant, Variable, Absolute), */
    0x15, 0x00,       /*     Logical Minimum (0), */
    0x26, 0xff, 0x00, /*     Logical Maximum (255), */
    0x05, 0x01,       /*     Usage Page (Generic Desktop), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xa1, 0x00,       /*     Collection (Physical), */
    0x75, 0x08,       /*       Report Size (8), */
    0x95, 0x04,       /*       Report Count (4), */
    0x35, 0x00,       /*       Physical Minimum (0), */
    0x46, 0xff, 0x00, /*       Physical Maximum (255), */
    0x09, 0x30,       /*       Usage (X), */
    0x09, 0x31,       /*       Usage (Y), */
    0x09, 0x32,       /*       Usage (Z), */
    0x09, 0x35,       /*       Usage (Rz), */
    0x81, 0x02,       /*       Input (Data, Variable, Absolute), */
    0xc0,             /*     End Collection */
    0x05, 0x01,       /*     Usage Page (Generic Desktop), */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x27,       /*     Report Count (39), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0x81, 0x02,       /*     Input (Data, Variable, Absolute), */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0x91, 0x02,       /*     Output (Data, Variable, Absolute), */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */

    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0x02,       /*     Report ID (2) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */
    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0xee,       /*     Report ID (238) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */
    0xa1, 0x02,       /*   Collection (Logical), */
    0x85, 0xef,       /*     Report ID (239) */
    0x75, 0x08,       /*     Report Size (8), */
    0x95, 0x30,       /*     Report Count (48), */
    0x09, 0x01,       /*     Usage (Pointer), */
    0xb1, 0x02,       /*     Feature (Data, Variable, Absolute), */
    0xc0,             /*   End Collection */
    0xc0,             /* End Collection */
};
C_ASSERT(sizeof(PowerProEliteDescriptor) == 148);

/* ELAN0685 Windows Precision Touchpad report descriptor (04F3:320B). */
static UCHAR ElanPrecisionTouchpadDescriptor[] =
{

  0x05, 0x01, 0x09, 0x02, 0xa1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xa1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x02, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01,
  0x95, 0x02, 0x81, 0x02, 0x95, 0x06, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30,
  0x09, 0x31, 0x15, 0x81, 0x25, 0x7f, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
  0x75, 0x08, 0x95, 0x05, 0x81, 0x03, 0xc0, 0x06, 0x00, 0xff, 0x09, 0x01,
  0x85, 0x0e, 0x09, 0xc5, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
  0x04, 0xb1, 0x02, 0xc0, 0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x85,
  0x5c, 0x09, 0x01, 0x95, 0x0b, 0x75, 0x08, 0x81, 0x06, 0x85, 0x0d, 0x09,
  0xc5, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95, 0x04, 0xb1, 0x02,
  0x85, 0x0c, 0x09, 0xc6, 0x96, 0x48, 0x03, 0x75, 0x08, 0xb1, 0x02, 0x85,
  0x0b, 0x09, 0xc7, 0x96, 0x82, 0x00, 0x96, 0x82, 0x00, 0x75, 0x08, 0xb1,
  0x02, 0xc0, 0x05, 0x0d, 0x09, 0x05, 0xa1, 0x01, 0x85, 0x54, 0x05, 0x09,
  0x09, 0x01, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01, 0x81, 0x02,
  0x75, 0x01, 0x95, 0x03, 0x81, 0x03, 0x05, 0x0d, 0x09, 0x54, 0x25, 0x0f,
  0x75, 0x04, 0x95, 0x01, 0x81, 0x02, 0x05, 0x0d, 0x09, 0x56, 0x15, 0x00,
  0x25, 0x64, 0x55, 0x0c, 0x66, 0x01, 0x10, 0x47, 0xff, 0xff, 0x00, 0x00,
  0x27, 0xff, 0xff, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, 0x05,
  0x0d, 0x09, 0x22, 0xa1, 0x02, 0x09, 0x47, 0x09, 0x42, 0x15, 0x00, 0x25,
  0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x01, 0x95, 0x02, 0x81,
  0x03, 0x09, 0x51, 0x25, 0x0f, 0x75, 0x04, 0x95, 0x01, 0x81, 0x02, 0x05,
  0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xc8, 0x0d, 0x35, 0x00, 0x46, 0xd4,
  0x2b, 0x55, 0x0d, 0x65, 0x11, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, 0x09,
  0x31, 0x26, 0xd2, 0x07, 0x46, 0xe7, 0x18, 0x46, 0xe7, 0x18, 0x81, 0x02,
  0xc0, 0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x09, 0x47, 0x09, 0x42, 0x15,
  0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x01, 0x95,
  0x02, 0x81, 0x03, 0x09, 0x51, 0x25, 0x0f, 0x75, 0x04, 0x95, 0x01, 0x81,
  0x02, 0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xc8, 0x0d, 0x35, 0x00,
  0x46, 0xd4, 0x2b, 0x55, 0x0d, 0x65, 0x11, 0x75, 0x10, 0x95, 0x01, 0x81,
  0x02, 0x09, 0x31, 0x26, 0xd2, 0x07, 0x46, 0xe7, 0x18, 0x46, 0xe7, 0x18,
  0x81, 0x02, 0xc0, 0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x09, 0x47, 0x09,
  0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75,
  0x01, 0x95, 0x02, 0x81, 0x03, 0x09, 0x51, 0x25, 0x0f, 0x75, 0x04, 0x95,
  0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xc8, 0x0d,
  0x35, 0x00, 0x46, 0xd4, 0x2b, 0x55, 0x0d, 0x65, 0x11, 0x75, 0x10, 0x95,
  0x01, 0x81, 0x02, 0x09, 0x31, 0x26, 0xd2, 0x07, 0x46, 0xe7, 0x18, 0x46,
  0xe7, 0x18, 0x81, 0x02, 0xc0, 0x05, 0x0d, 0x09, 0x22, 0xa1, 0x02, 0x09,
  0x47, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x02, 0x81,
  0x02, 0x75, 0x01, 0x95, 0x02, 0x81, 0x03, 0x09, 0x51, 0x25, 0x0f, 0x75,
  0x04, 0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26,
  0xc8, 0x0d, 0x35, 0x00, 0x46, 0xd4, 0x2b, 0x55, 0x0d, 0x65, 0x11, 0x75,
  0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, 0xd2, 0x07, 0x46, 0xe7,
  0x18, 0x46, 0xe7, 0x18, 0x81, 0x02, 0xc0, 0x05, 0x0d, 0x09, 0x22, 0xa1,
  0x02, 0x09, 0x47, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95,
  0x02, 0x81, 0x02, 0x75, 0x01, 0x95, 0x02, 0x81, 0x03, 0x09, 0x51, 0x25,
  0x0f, 0x75, 0x04, 0x95, 0x01, 0x81, 0x02, 0x05, 0x01, 0x09, 0x30, 0x15,
  0x00, 0x26, 0xc8, 0x0d, 0x35, 0x00, 0x46, 0xd4, 0x2b, 0x55, 0x0d, 0x65,
  0x11, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, 0xd2, 0x07,
  0x46, 0xe7, 0x18, 0x46, 0xe7, 0x18, 0x81, 0x02, 0xc0, 0x05, 0x0d, 0x85,
  0x02, 0x09, 0x55, 0x09, 0x59, 0x75, 0x04, 0x95, 0x02, 0x25, 0x0f, 0xb1,
  0x02, 0x85, 0x07, 0x09, 0x60, 0x75, 0x01, 0x95, 0x01, 0x15, 0x00, 0x25,
  0x01, 0xb1, 0x02, 0x95, 0x0f, 0xb1, 0x03, 0x06, 0x00, 0xff, 0x06, 0x00,
  0xff, 0x85, 0x06, 0x09, 0xc5, 0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
  0x96, 0x00, 0x01, 0xb1, 0x02, 0xc0, 0x05, 0x0d, 0x09, 0x0e, 0xa1, 0x01,
  0x85, 0x03, 0x09, 0x22, 0xa1, 0x00, 0x09, 0x52, 0x15, 0x00, 0x25, 0x0a,
  0x75, 0x10, 0x95, 0x01, 0xb1, 0x02, 0xc0, 0x09, 0x22, 0xa1, 0x00, 0x85,
  0x05, 0x09, 0x57, 0x09, 0x58, 0x75, 0x01, 0x95, 0x02, 0x25, 0x01, 0xb1,
  0x02, 0x95, 0x0e, 0xb1, 0x03, 0xc0, 0xc0
};
C_ASSERT(sizeof(ElanPrecisionTouchpadDescriptor) == 679);

/* Microsoft Windows 11 haptic touchpad sample report descriptor. */
static UCHAR HapticTouchpadDescriptor[] =
{
    0x05, 0x0d, 0x09, 0x05, 0xa1, 0x01,
    0x85, 0x40, 0x05, 0x0d, 0x09, 0xb0, 0x35, 0x6e, 0x46, 0xbe, 0x00,
    0x66, 0x01, 0x01, 0x55, 0x00, 0x15, 0x01, 0x25, 0x03, 0x95, 0x01,
    0x75, 0x08, 0xb1, 0x02,
    0x85, 0x41, 0x05, 0x0e, 0x09, 0x01, 0xa1, 0x02, 0x05, 0x0e, 0x09, 0x23,
    0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x55, 0x00, 0x15, 0x00, 0x25, 0x04,
    0x95, 0x01, 0x75, 0x08, 0xb1, 0x02, 0xc0,
    0x85, 0x42, 0x05, 0x0e, 0x09, 0x01, 0xa1, 0x02,
    0x05, 0x0e, 0x09, 0x10, 0xa1, 0x02, 0x05, 0x0a, 0x19, 0x03, 0x29, 0x07,
    0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x55, 0x00, 0x16, 0x01, 0x10,
    0x26, 0xff, 0x2f, 0x95, 0x05, 0x75, 0x10, 0xb1, 0x02, 0xc0,
    0x05, 0x0e, 0x09, 0x11, 0xa1, 0x02, 0x05, 0x0a, 0x19, 0x03, 0x29, 0x07,
    0x35, 0x00, 0x45, 0x32, 0x66, 0x01, 0x10, 0x55, 0x0d, 0x15, 0x00,
    0x25, 0x32, 0x95, 0x05, 0x75, 0x08, 0xb1, 0x02, 0xc0, 0xc0,
    0x85, 0x43, 0x05, 0x0e, 0x09, 0x01, 0xa1, 0x02,
    0x05, 0x0e, 0x09, 0x21, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x55, 0x00,
    0x15, 0x01, 0x25, 0x07, 0x95, 0x01, 0x75, 0x08, 0x91, 0x02,
    0x05, 0x0e, 0x09, 0x23, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x55, 0x00,
    0x15, 0x00, 0x25, 0x04, 0x95, 0x01, 0x75, 0x08, 0x91, 0x02,
    0x05, 0x0e, 0x09, 0x24, 0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x55, 0x00,
    0x15, 0x00, 0x25, 0x05, 0x95, 0x01, 0x75, 0x08, 0x91, 0x02,
    0x05, 0x0e, 0x09, 0x25, 0x35, 0x00, 0x46, 0xe8, 0x03, 0x66, 0x01, 0x10,
    0x55, 0x0d, 0x15, 0x00, 0x26, 0xe8, 0x03, 0x95, 0x01, 0x75, 0x10,
    0x91, 0x02,
    0x05, 0x0e, 0x09, 0x28, 0x36, 0xe8, 0x03, 0x46, 0x88, 0x13,
    0x66, 0x01, 0x10, 0x55, 0x0d, 0x16, 0xe8, 0x03, 0x26, 0x88, 0x13,
    0x95, 0x01, 0x75, 0x10, 0x91, 0x02, 0xc0, 0xc0
};
C_ASSERT(sizeof(HapticTouchpadDescriptor) == 265);

static UCHAR MultipleInputReportDescriptor[] =
{
    0x05, 0x01,             /* Usage Page (Generic Desktop) */
    0x09, 0x02,             /* Usage (Mouse) */
    0xa1, 0x01,             /* Collection (Application) */
    0x85, 0x01,             /*   Report ID (1) */
    0x09, 0x30,             /*   Usage (X) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x26, 0xff, 0x00,       /*   Logical Maximum (255) */
    0x75, 0x08,             /*   Report Size (8) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x02,             /*   Input (Data, Variable, Absolute) */
    0x85, 0x02,             /*   Report ID (2) */
    0x09, 0x31,             /*   Usage (Y) */
    0x09, 0x32,             /*   Usage (Z) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x27, 0xff, 0xff, 0x00, 0x00, /* Logical Maximum (65535) */
    0x75, 0x10,             /*   Report Size (16) */
    0x95, 0x02,             /*   Report Count (2) */
    0x81, 0x02,             /*   Input (Data, Variable, Absolute) */
    0xc0                    /* End Collection */
};

static UCHAR UsageValueArrayDescriptor[] =
{
    0x05, 0x01,             /* Usage Page (Generic Desktop) */
    0x09, 0x04,             /* Usage (Joystick) */
    0xa1, 0x01,             /* Collection (Application) */
    0x85, 0x20,             /*   Report ID (32) */
    0x75, 0x02,             /*   Report Size (2) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x03,             /*   Input (Constant, Variable, Absolute) */
    0x09, 0x30,             /*   Usage (X) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x25, 0x3f,             /*   Logical Maximum (63) */
    0x75, 0x06,             /*   Report Size (6) */
    0x95, 0x05,             /*   Report Count (5) */
    0x81, 0x02,             /*   Input (Data, Variable, Absolute) */
    0x85, 0x21,             /*   Report ID (33) */
    0x09, 0x31,             /*   Usage (Y) */
    0x75, 0x08,             /*   Report Size (8) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x02,             /*   Input (Data, Variable, Absolute) */
    0x85, 0x22,             /*   Report ID (34) */
    0x75, 0x03,             /*   Report Size (3) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x03,             /*   Input (Constant, Variable, Absolute) */
    0x05, 0x09,             /*   Usage Page (Button) */
    0x09, 0x01,             /*   Usage (Button 1) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x25, 0x01,             /*   Logical Maximum (1) */
    0x75, 0x01,             /*   Report Size (1) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x02,             /*   Input (Data, Variable, Absolute) */
    0x75, 0x04,             /*   Report Size (4) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x03,             /*   Input (Constant, Variable, Absolute) */
    0xc0                    /* End Collection */
};

static UCHAR ConsumerControlDescriptor[] =
{
    0x05, 0x0c,             /* Usage Page (Consumer) */
    0x09, 0x01,             /* Usage (Consumer Control) */
    0xa1, 0x01,             /* Collection (Application) */
    0x85, 0x10,             /*   Report ID (16) */
    0x15, 0x00,             /*   Logical Minimum (0) */
    0x25, 0x01,             /*   Logical Maximum (1) */
    0x75, 0x01,             /*   Report Size (1) */
    0x95, 0x03,             /*   Report Count (3) */
    0x0a, 0xe2, 0x00,       /*   Usage (Mute) */
    0x0a, 0xe9, 0x00,       /*   Usage (Volume Increment) */
    0x0a, 0xea, 0x00,       /*   Usage (Volume Decrement) */
    0x81, 0x02,             /*   Input (Data, Variable, Absolute) */
    0x75, 0x05,             /*   Report Size (5) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x03,             /*   Input (Constant, Variable, Absolute) */
    0xc0                    /* End Collection */
};

static UCHAR SystemControlDescriptor[] =
{
    0x05, 0x01,             /* Usage Page (Generic Desktop) */
    0x09, 0x80,             /* Usage (System Control) */
    0xa1, 0x01,             /* Collection (Application) */
    0x85, 0x11,             /*   Report ID (17) */
    0x15, 0x01,             /*   Logical Minimum (1) */
    0x25, 0x03,             /*   Logical Maximum (3) */
    0x19, 0x81,             /*   Usage Minimum (System Power Down) */
    0x29, 0x83,             /*   Usage Maximum (System Wake Up) */
    0x75, 0x02,             /*   Report Size (2) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x00,             /*   Input (Data, Array, Absolute) */
    0x75, 0x06,             /*   Report Size (6) */
    0x95, 0x01,             /*   Report Count (1) */
    0x81, 0x03,             /*   Input (Constant, Variable, Absolute) */
    0xc0                    /* End Collection */
};

typedef struct _HIDP_TEST_SCAN_CODES
{
    UCHAR Buffer[16];
    ULONG Length;
} HIDP_TEST_SCAN_CODES, *PHIDP_TEST_SCAN_CODES;

static
BOOLEAN
NTAPI
TestInsertScanCodes(
    _In_opt_ PVOID Context,
    _In_reads_bytes_(Length) PCHAR NewScanCodes,
    _In_ ULONG Length)
{
    PHIDP_TEST_SCAN_CODES ScanCodes = Context;

    if (Length > sizeof(ScanCodes->Buffer) - ScanCodes->Length)
        return FALSE;

    RtlCopyMemory(&ScanCodes->Buffer[ScanCodes->Length],
                  NewScanCodes,
                  Length);
    ScanCodes->Length += Length;
    return TRUE;
}


static
VOID
TestGetCollectionDescription(VOID)
{
    NTSTATUS Status;
    HIDP_DEVICE_DESC DeviceDescription;

    /* Empty report descriptor */
    RtlFillMemory(&DeviceDescription, sizeof(DeviceDescription), 0x55);
    Status = HidP_GetCollectionDescription(NULL,
                                           0,
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_NO_DATA_DETECTED);
    ok_eq_pointer(DeviceDescription.CollectionDesc, NULL);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 0);
    ok_eq_pointer(DeviceDescription.ReportIDs, NULL);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 0);
    if (NT_SUCCESS(Status)) HidP_FreeCollectionDescription(&DeviceDescription);

    /* Sample keyboard report descriptor from the HID spec */
    Status = HidP_GetCollectionDescription(ExampleKeyboardDescriptor,
                                           sizeof(ExampleKeyboardDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 1);
    if (!skip(NT_SUCCESS(Status), "Parsing failure\n"))
    {
        if (!skip(DeviceDescription.CollectionDescLength >= 1, "No collection\n"))
        {
            ok_eq_uint(DeviceDescription.CollectionDesc[0].UsagePage, HID_USAGE_PAGE_GENERIC);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].Usage, HID_USAGE_GENERIC_KEYBOARD);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].InputLength, 9);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].OutputLength, 2);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].FeatureLength, 0);
            ok(DeviceDescription.CollectionDesc[0].PreparsedDataLength != 0,
               "Expected non-empty preparsed data\n");
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 1, "No report IDs\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[0].ReportID, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].InputLength, 8);
            ok_eq_uint(DeviceDescription.ReportIDs[0].OutputLength, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].FeatureLength, 0);
        }
        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    /* Regression test for CORE-11538 */
    Status = HidP_GetCollectionDescription(PowerProEliteDescriptor,
                                           sizeof(PowerProEliteDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 4);
    if (!skip(NT_SUCCESS(Status), "Parsing failure\n"))
    {
        if (!skip(DeviceDescription.CollectionDescLength >= 1, "No collection\n"))
        {
            ok_eq_uint(DeviceDescription.CollectionDesc[0].UsagePage, HID_USAGE_PAGE_GENERIC);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].Usage, HID_USAGE_GENERIC_JOYSTICK);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].InputLength, 49);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].OutputLength, 49);
            ok_eq_uint(DeviceDescription.CollectionDesc[0].FeatureLength, 49);
            ok(DeviceDescription.CollectionDesc[0].PreparsedDataLength != 0,
               "Expected non-empty preparsed data\n");
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 1, "No first report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[0].ReportID, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[0].InputLength, 49);
            ok_eq_uint(DeviceDescription.ReportIDs[0].OutputLength, 49);
            ok_eq_uint(DeviceDescription.ReportIDs[0].FeatureLength, 49);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 2, "No second report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[1].ReportID, 2);
            ok_eq_uint(DeviceDescription.ReportIDs[1].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[1].InputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[1].OutputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[1].FeatureLength, 49);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 3, "No third report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[2].ReportID, 238);
            ok_eq_uint(DeviceDescription.ReportIDs[2].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[2].InputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[2].OutputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[2].FeatureLength, 49);
        }
        if (!skip(DeviceDescription.ReportIDsLength >= 4, "No fourth report ID\n"))
        {
            ok_eq_uint(DeviceDescription.ReportIDs[3].ReportID, 239);
            ok_eq_uint(DeviceDescription.ReportIDs[3].CollectionNumber, 1);
            ok_eq_uint(DeviceDescription.ReportIDs[3].InputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[3].OutputLength, 0);
            ok_eq_uint(DeviceDescription.ReportIDs[3].FeatureLength, 49);
        }
        HidP_FreeCollectionDescription(&DeviceDescription);
    }
}

static
VOID
TestElanPrecisionTouchpad(VOID)
{
    HIDP_LINK_COLLECTION_NODE Nodes[6];
    HIDP_VALUE_CAPS ValueCaps[5], ModeCaps, FeatureCaps;
    HIDP_DEVICE_DESC DeviceDescription;
    HIDP_CAPS Caps;
    UCHAR Report[29] = {0};
    UCHAR ModeReport[3] = {0x03, 0, 0};
    UCHAR SelectiveReport[3] = {0x05, 0, 0};
    UCHAR CertificationReport[257] = {0};
    UCHAR CertificationValue[256];
    UCHAR NewCertificationValue[256];
    PHIDP_REPORT_IDS TouchReport = NULL;
    ULONG NodeCount, Index, Value;
    USHORT ValueCapsLength;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(ElanPrecisionTouchpadDescriptor,
                                           sizeof(ElanPrecisionTouchpadDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulong(DeviceDescription.CollectionDescLength, 4);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 12);
    if (DeviceDescription.CollectionDescLength < 3)
    {
        HidP_FreeCollectionDescription(&DeviceDescription);
        return;
    }

    ok_eq_uint(DeviceDescription.CollectionDesc[2].UsagePage,
               HID_USAGE_PAGE_DIGITIZER);
    ok_eq_uint(DeviceDescription.CollectionDesc[2].Usage,
               HID_USAGE_DIGITIZER_TOUCH_PAD);
    ok_eq_uint(DeviceDescription.CollectionDesc[2].CollectionNumber, 3);
    ok_eq_uint(DeviceDescription.CollectionDesc[2].InputLength, 29);

    for (Index = 0; Index < DeviceDescription.ReportIDsLength; Index++)
    {
        if (DeviceDescription.ReportIDs[Index].ReportID == 0x54)
        {
            TouchReport = &DeviceDescription.ReportIDs[Index];
            break;
        }
    }
    ok(TouchReport != NULL, "Touch report ID 54 was not found\n");
    if (TouchReport != NULL)
    {
        ok_eq_uint(TouchReport->CollectionNumber, 3);
        ok_eq_uint(TouchReport->InputLength, 29);
    }

    Status = HidP_GetCaps(DeviceDescription.CollectionDesc[2].PreparsedData,
                          &Caps);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        HidP_FreeCollectionDescription(&DeviceDescription);
        return;
    }

    ok_eq_uint(Caps.InputReportByteLength, 29);
    ok_eq_uint(Caps.FeatureReportByteLength, 257);
    ok_eq_uint(Caps.NumberFeatureValueCaps, 4);
    ok_eq_uint(Caps.NumberFeatureDataIndices, 259);

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Feature,
                 HID_USAGE_PAGE_DIGITIZER,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_DIGITIZER_CONTACT_COUNT_MAXIMUM,
                 &FeatureCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[2].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
    {
        ok_eq_uint(FeatureCaps.ReportID, 2);
        ok_eq_uint(FeatureCaps.BitSize, 4);
        ok_eq_uint(FeatureCaps.ReportCount, 1);
    }

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Feature,
                 0xff00,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 0x00c5,
                 &FeatureCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[2].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
    {
        ok_eq_uint(FeatureCaps.ReportID, 6);
        ok_eq_uint(FeatureCaps.BitSize, 8);
        ok_eq_uint(FeatureCaps.ReportCount, 256);
        ok_eq_uint(FeatureCaps.NotRange.Usage, 0x00c5);
        ok_eq_uint(FeatureCaps.NotRange.DataIndex, 3);
    }

    CertificationReport[0] = 6;
    for (Index = 0; Index < RTL_NUMBER_OF(CertificationValue); Index++)
    {
        CertificationReport[Index + 1] = (UCHAR)Index;
        NewCertificationValue[Index] = (UCHAR)(Index ^ 0x5a);
    }

    RtlZeroMemory(CertificationValue, sizeof(CertificationValue));
    Status = HidP_GetUsageValueArray(
                 HidP_Feature,
                 0xff00,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 0x00c5,
                 (PCHAR)CertificationValue,
                 sizeof(CertificationValue),
                 DeviceDescription.CollectionDesc[2].PreparsedData,
                 (PCHAR)CertificationReport,
                 sizeof(CertificationReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok(RtlCompareMemory(CertificationValue,
                        CertificationReport + 1,
                        sizeof(CertificationValue)) ==
           sizeof(CertificationValue),
       "Certification value array differs from the feature report\n");

    Status = HidP_GetUsageValueArray(
                 HidP_Feature,
                 0xff00,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 0x00c5,
                 (PCHAR)CertificationValue,
                 sizeof(CertificationValue) - 1,
                 DeviceDescription.CollectionDesc[2].PreparsedData,
                 (PCHAR)CertificationReport,
                 sizeof(CertificationReport));
    ok_eq_hex(Status, HIDP_STATUS_BUFFER_TOO_SMALL);

    RtlZeroMemory(CertificationReport + 1,
                  sizeof(CertificationReport) - 1);
    Status = HidP_SetUsageValueArray(
                 HidP_Feature,
                 0xff00,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 0x00c5,
                 (PCHAR)NewCertificationValue,
                 sizeof(NewCertificationValue),
                 DeviceDescription.CollectionDesc[2].PreparsedData,
                 (PCHAR)CertificationReport,
                 sizeof(CertificationReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok(RtlCompareMemory(NewCertificationValue,
                        CertificationReport + 1,
                        sizeof(NewCertificationValue)) ==
           sizeof(NewCertificationValue),
       "Set certification value array differs from its source\n");
    ok_eq_uint(Caps.NumberLinkCollectionNodes, 6);

    NodeCount = RTL_NUMBER_OF(Nodes);
    Status = HidP_GetLinkCollectionNodes(Nodes,
                                         &NodeCount,
                                         DeviceDescription.CollectionDesc[2].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(NodeCount, RTL_NUMBER_OF(Nodes));
    if (Status == HIDP_STATUS_SUCCESS && NodeCount == RTL_NUMBER_OF(Nodes))
    {
        ok_eq_uint(Nodes[0].LinkUsagePage, HID_USAGE_PAGE_DIGITIZER);
        ok_eq_uint(Nodes[0].LinkUsage, HID_USAGE_DIGITIZER_TOUCH_PAD);
        ok_eq_uint(Nodes[0].NumberOfChildren, 5);
        for (Index = 1; Index < RTL_NUMBER_OF(Nodes); Index++)
        {
            ok_eq_uint(Nodes[Index].LinkUsagePage, HID_USAGE_PAGE_DIGITIZER);
            ok_eq_uint(Nodes[Index].LinkUsage, HID_USAGE_DIGITIZER_FINGER);
            ok_eq_uint(Nodes[Index].Parent, 0);
        }
    }

    ValueCapsLength = RTL_NUMBER_OF(ValueCaps);
    Status = HidP_GetSpecificValueCaps(HidP_Input,
                                       HID_USAGE_PAGE_GENERIC,
                                       HIDP_LINK_COLLECTION_UNSPECIFIED,
                                       HID_USAGE_GENERIC_X,
                                       ValueCaps,
                                       &ValueCapsLength,
                                       DeviceDescription.CollectionDesc[2].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, RTL_NUMBER_OF(ValueCaps));
    if (Status == HIDP_STATUS_SUCCESS &&
        ValueCapsLength == RTL_NUMBER_OF(ValueCaps))
    {
        for (Index = 0; Index < RTL_NUMBER_OF(ValueCaps); Index++)
        {
            ok_eq_uint(ValueCaps[Index].LinkCollection, Index + 1);
            ok_eq_uint(ValueCaps[Index].BitField, 2);
            ok_eq_uint(ValueCaps[Index].BitSize, 16);
            ok_eq_long(ValueCaps[Index].LogicalMax, 3528);
            ok_eq_long(ValueCaps[Index].PhysicalMax, 11220);
            ok_eq_ulong(ValueCaps[Index].Units, 0x11);
            ok_eq_ulong(ValueCaps[Index].UnitsExp, 0x0d);
        }
    }

    Report[0] = 0x54;
    Report[4] = 0x33;
    Report[5] = 100;
    Report[7] = 200;

    Status = HidP_GetUsageValue(HidP_Input,
                                HID_USAGE_PAGE_GENERIC,
                                1,
                                HID_USAGE_GENERIC_X,
                                &Value,
                                DeviceDescription.CollectionDesc[2].PreparsedData,
                                (PCHAR)Report,
                                sizeof(Report));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, 100);

    Status = HidP_GetUsageValue(HidP_Input,
                                HID_USAGE_PAGE_GENERIC,
                                2,
                                HID_USAGE_GENERIC_X,
                                &Value,
                                DeviceDescription.CollectionDesc[2].PreparsedData,
                                (PCHAR)Report,
                                sizeof(Report));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, 0);

    Status = HidP_GetUsageValue(HidP_Input,
                                HID_USAGE_PAGE_DIGITIZER,
                                1,
                                HID_USAGE_DIGITIZER_TIP_SWITCH,
                                &Value,
                                DeviceDescription.CollectionDesc[2].PreparsedData,
                                (PCHAR)Report,
                                sizeof(Report));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, 1);

    Status = HidP_GetUsageValue(HidP_Input,
                                HID_USAGE_PAGE_DIGITIZER,
                                1,
                                HID_USAGE_DIGITIZER_CONFIDENCE,
                                &Value,
                                DeviceDescription.CollectionDesc[2].PreparsedData,
                                (PCHAR)Report,
                                sizeof(Report));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, 1);

    Status = HidP_GetUsageValue(HidP_Input,
                                HID_USAGE_PAGE_DIGITIZER,
                                1,
                                HID_USAGE_DIGITIZER_CONTACT_IDENTIFIER,
                                &Value,
                                DeviceDescription.CollectionDesc[2].PreparsedData,
                                (PCHAR)Report,
                                sizeof(Report));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, 3);

    if (DeviceDescription.CollectionDescLength >= 4)
    {
        Status = HidP_GetCaps(
                     DeviceDescription.CollectionDesc[3].PreparsedData,
                     &Caps);
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        if (Status == HIDP_STATUS_SUCCESS)
        {
            ok_eq_uint(Caps.FeatureReportByteLength, 3);
            ok_eq_uint(Caps.NumberFeatureValueCaps, 3);
            ok_eq_uint(Caps.NumberFeatureDataIndices, 3);
        }

        ValueCapsLength = 1;
        Status = HidP_GetSpecificValueCaps(
                     HidP_Feature,
                     HID_USAGE_PAGE_DIGITIZER,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     HID_USAGE_DIGITIZER_DEVICE_MODE,
                     &ModeCaps,
                     &ValueCapsLength,
                     DeviceDescription.CollectionDesc[3].PreparsedData);
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_uint(ValueCapsLength, 1);
        if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
        {
            ok_eq_uint(ModeCaps.ReportID, 3);
            ok_eq_uint(ModeCaps.BitField, 2);
            ok_eq_uint(ModeCaps.BitSize, 16);
        }

        Status = HidP_SetUsageValue(
                     HidP_Feature,
                     HID_USAGE_PAGE_DIGITIZER,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     HID_USAGE_DIGITIZER_DEVICE_MODE,
                     3,
                     DeviceDescription.CollectionDesc[3].PreparsedData,
                     (PCHAR)ModeReport,
                     sizeof(ModeReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_uint(ModeReport[0], 3);
        ok_eq_uint(ModeReport[1], 3);
        ok_eq_uint(ModeReport[2], 0);

        Value = 0;
        Status = HidP_GetUsageValue(
                     HidP_Feature,
                     HID_USAGE_PAGE_DIGITIZER,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     HID_USAGE_DIGITIZER_DEVICE_MODE,
                     &Value,
                     DeviceDescription.CollectionDesc[3].PreparsedData,
                     (PCHAR)ModeReport,
                     sizeof(ModeReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_ulong(Value, 3);

        ValueCapsLength = 1;
        Status = HidP_GetSpecificValueCaps(
                     HidP_Feature,
                     HID_USAGE_PAGE_DIGITIZER,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     HID_USAGE_DIGITIZER_SURFACE_SWITCH,
                     &FeatureCaps,
                     &ValueCapsLength,
                     DeviceDescription.CollectionDesc[3].PreparsedData);
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_uint(ValueCapsLength, 1);
        if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
        {
            ok_eq_uint(FeatureCaps.ReportID, 5);
            ok_eq_uint(FeatureCaps.BitSize, 1);
            ok_eq_uint(FeatureCaps.ReportCount, 1);
            ok_eq_uint(FeatureCaps.NotRange.DataIndex, 1);
        }

        Status = HidP_SetUsageValue(
                     HidP_Feature,
                     HID_USAGE_PAGE_DIGITIZER,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     HID_USAGE_DIGITIZER_SURFACE_SWITCH,
                     1,
                     DeviceDescription.CollectionDesc[3].PreparsedData,
                     (PCHAR)SelectiveReport,
                     sizeof(SelectiveReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        Status = HidP_SetUsageValue(
                     HidP_Feature,
                     HID_USAGE_PAGE_DIGITIZER,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     HID_USAGE_DIGITIZER_BUTTON_SWITCH,
                     1,
                     DeviceDescription.CollectionDesc[3].PreparsedData,
                     (PCHAR)SelectiveReport,
                     sizeof(SelectiveReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_uint(SelectiveReport[0], 5);
        ok_eq_uint(SelectiveReport[1], 3);
        ok_eq_uint(SelectiveReport[2], 0);
    }

    HidP_FreeCollectionDescription(&DeviceDescription);
}

static
VOID
TestHapticTouchpad(VOID)
{
    static const struct
    {
        USAGE Usage;
        USHORT BitSize;
        LONG LogicalMin;
        LONG LogicalMax;
        LONG PhysicalMin;
        LONG PhysicalMax;
        ULONG Units;
        ULONG UnitsExp;
        ULONG Value;
    } OutputFields[] =
    {
        {HID_USAGE_HAPTICS_MANUAL_TRIGGER, 8, 1, 7, 0, 0, 0, 0, 5},
        {HID_USAGE_HAPTICS_INTENSITY, 8, 0, 4, 0, 0, 0, 0, 4},
        {HID_USAGE_HAPTICS_REPEAT_COUNT, 8, 0, 5, 0, 0, 0, 0, 2},
        {HID_USAGE_HAPTICS_RETRIGGER_PERIOD, 16, 0, 1000, 0, 1000, 0x1001, 0x0d, 500},
        {HID_USAGE_HAPTICS_WAVEFORM_CUTOFF_TIME, 16, 1000, 5000, 1000, 5000, 0x1001, 0x0d, 3000}
    };
    HIDP_LINK_COLLECTION_NODE Nodes[6];
    HIDP_DEVICE_DESC DeviceDescription;
    HIDP_VALUE_CAPS ValueCaps;
    HIDP_CAPS Caps;
    UCHAR FeatureReport[16] = {0};
    UCHAR OutputReport[8] = {0};
    ULONG NodeCount, Index, Value;
    USHORT ValueCapsLength;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(HapticTouchpadDescriptor,
                                           sizeof(HapticTouchpadDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 4);
    if (DeviceDescription.CollectionDescLength != 1 ||
        DeviceDescription.ReportIDsLength != 4)
    {
        HidP_FreeCollectionDescription(&DeviceDescription);
        return;
    }

    ok_eq_uint(DeviceDescription.CollectionDesc[0].UsagePage,
               HID_USAGE_PAGE_DIGITIZER);
    ok_eq_uint(DeviceDescription.CollectionDesc[0].Usage,
               HID_USAGE_DIGITIZER_TOUCH_PAD);
    ok_eq_uint(DeviceDescription.CollectionDesc[0].InputLength, 0);
    ok_eq_uint(DeviceDescription.CollectionDesc[0].OutputLength,
               sizeof(OutputReport));
    ok_eq_uint(DeviceDescription.CollectionDesc[0].FeatureLength,
               sizeof(FeatureReport));

    ok_eq_uint(DeviceDescription.ReportIDs[0].ReportID, 0x40);
    ok_eq_uint(DeviceDescription.ReportIDs[0].FeatureLength, 2);
    ok_eq_uint(DeviceDescription.ReportIDs[1].ReportID, 0x41);
    ok_eq_uint(DeviceDescription.ReportIDs[1].FeatureLength, 2);
    ok_eq_uint(DeviceDescription.ReportIDs[2].ReportID, 0x42);
    ok_eq_uint(DeviceDescription.ReportIDs[2].FeatureLength,
               sizeof(FeatureReport));
    ok_eq_uint(DeviceDescription.ReportIDs[3].ReportID, 0x43);
    ok_eq_uint(DeviceDescription.ReportIDs[3].OutputLength,
               sizeof(OutputReport));

    Status = HidP_GetCaps(DeviceDescription.CollectionDesc[0].PreparsedData,
                          &Caps);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    if (Status != HIDP_STATUS_SUCCESS)
    {
        HidP_FreeCollectionDescription(&DeviceDescription);
        return;
    }

    ok_eq_uint(Caps.InputReportByteLength, 0);
    ok_eq_uint(Caps.OutputReportByteLength, sizeof(OutputReport));
    ok_eq_uint(Caps.FeatureReportByteLength, sizeof(FeatureReport));
    ok_eq_uint(Caps.NumberLinkCollectionNodes, RTL_NUMBER_OF(Nodes));

    NodeCount = RTL_NUMBER_OF(Nodes);
    Status = HidP_GetLinkCollectionNodes(
                 Nodes,
                 &NodeCount,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(NodeCount, RTL_NUMBER_OF(Nodes));
    if (Status == HIDP_STATUS_SUCCESS && NodeCount == RTL_NUMBER_OF(Nodes))
    {
        ok_eq_uint(Nodes[0].LinkUsagePage, HID_USAGE_PAGE_DIGITIZER);
        ok_eq_uint(Nodes[0].LinkUsage, HID_USAGE_DIGITIZER_TOUCH_PAD);
        ok_eq_uint(Nodes[0].NumberOfChildren, 3);
        ok_eq_uint(Nodes[1].LinkUsagePage, HID_USAGE_PAGE_HAPTICS);
        ok_eq_uint(Nodes[1].LinkUsage, HID_USAGE_HAPTICS_SIMPLE_CONTROLLER);
        ok_eq_uint(Nodes[1].Parent, 0);
        ok_eq_uint(Nodes[2].LinkUsagePage, HID_USAGE_PAGE_HAPTICS);
        ok_eq_uint(Nodes[2].LinkUsage, HID_USAGE_HAPTICS_SIMPLE_CONTROLLER);
        ok_eq_uint(Nodes[2].Parent, 0);
        ok_eq_uint(Nodes[2].NumberOfChildren, 2);
        ok_eq_uint(Nodes[3].LinkUsagePage, HID_USAGE_PAGE_HAPTICS);
        ok_eq_uint(Nodes[3].LinkUsage, HID_USAGE_HAPTICS_WAVEFORM_LIST);
        ok_eq_uint(Nodes[3].Parent, 2);
        ok_eq_uint(Nodes[4].LinkUsagePage, HID_USAGE_PAGE_HAPTICS);
        ok_eq_uint(Nodes[4].LinkUsage, HID_USAGE_HAPTICS_DURATION_LIST);
        ok_eq_uint(Nodes[4].Parent, 2);
        ok_eq_uint(Nodes[5].LinkUsagePage, HID_USAGE_PAGE_HAPTICS);
        ok_eq_uint(Nodes[5].LinkUsage, HID_USAGE_HAPTICS_SIMPLE_CONTROLLER);
        ok_eq_uint(Nodes[5].Parent, 0);
    }

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Feature,
                 HID_USAGE_PAGE_DIGITIZER,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_DIGITIZER_BUTTON_PRESS_THRESHOLD,
                 &ValueCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
    {
        ok_eq_uint(ValueCaps.ReportID, 0x40);
        ok_eq_uint(ValueCaps.BitSize, 8);
        ok_eq_long(ValueCaps.LogicalMin, 1);
        ok_eq_long(ValueCaps.LogicalMax, 3);
        ok_eq_long(ValueCaps.PhysicalMin, 110);
        ok_eq_long(ValueCaps.PhysicalMax, 190);
        ok_eq_ulong(ValueCaps.Units, 0x0101);
        ok_eq_ulong(ValueCaps.UnitsExp, 0);
    }

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Feature,
                 HID_USAGE_PAGE_HAPTICS,
                 1,
                 HID_USAGE_HAPTICS_INTENSITY,
                 &ValueCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
    {
        ok_eq_uint(ValueCaps.ReportID, 0x41);
        ok_eq_uint(ValueCaps.BitSize, 8);
        ok_eq_long(ValueCaps.LogicalMin, 0);
        ok_eq_long(ValueCaps.LogicalMax, 4);
    }

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Feature,
                 HID_USAGE_PAGE_ORDINAL,
                 3,
                 3,
                 &ValueCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
    {
        ok_eq_uint(ValueCaps.ReportID, 0x42);
        ok_eq_uint(ValueCaps.BitSize, 16);
        ok_eq_long(ValueCaps.LogicalMin, 0x1001);
        ok_eq_long(ValueCaps.LogicalMax, 0x2fff);
    }

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Feature,
                 HID_USAGE_PAGE_ORDINAL,
                 4,
                 3,
                 &ValueCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
    {
        ok_eq_uint(ValueCaps.ReportID, 0x42);
        ok_eq_uint(ValueCaps.BitSize, 8);
        ok_eq_long(ValueCaps.LogicalMin, 0);
        ok_eq_long(ValueCaps.LogicalMax, 50);
        ok_eq_long(ValueCaps.PhysicalMin, 0);
        ok_eq_long(ValueCaps.PhysicalMax, 50);
        ok_eq_ulong(ValueCaps.Units, 0x1001);
        ok_eq_ulong(ValueCaps.UnitsExp, 0x0d);
    }

    for (Index = 0; Index < RTL_NUMBER_OF(OutputFields); Index++)
    {
        ValueCapsLength = 1;
        Status = HidP_GetSpecificValueCaps(
                     HidP_Output,
                     HID_USAGE_PAGE_HAPTICS,
                     5,
                     OutputFields[Index].Usage,
                     &ValueCaps,
                     &ValueCapsLength,
                     DeviceDescription.CollectionDesc[0].PreparsedData);
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_uint(ValueCapsLength, 1);
        if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
        {
            ok_eq_uint(ValueCaps.ReportID, 0x43);
            ok_eq_uint(ValueCaps.BitSize, OutputFields[Index].BitSize);
            ok_eq_long(ValueCaps.LogicalMin, OutputFields[Index].LogicalMin);
            ok_eq_long(ValueCaps.LogicalMax, OutputFields[Index].LogicalMax);
            ok_eq_long(ValueCaps.PhysicalMin, OutputFields[Index].PhysicalMin);
            ok_eq_long(ValueCaps.PhysicalMax, OutputFields[Index].PhysicalMax);
            ok_eq_ulong(ValueCaps.Units, OutputFields[Index].Units);
            ok_eq_ulong(ValueCaps.UnitsExp, OutputFields[Index].UnitsExp);
        }
    }

    FeatureReport[0] = 0x40;
    Status = HidP_SetUsageValue(
                 HidP_Feature,
                 HID_USAGE_PAGE_DIGITIZER,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_DIGITIZER_BUTTON_PRESS_THRESHOLD,
                 2,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)FeatureReport,
                 sizeof(FeatureReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(FeatureReport[1], 2);

    RtlZeroMemory(FeatureReport, sizeof(FeatureReport));
    FeatureReport[0] = 0x41;
    Status = HidP_SetUsageValue(
                 HidP_Feature,
                 HID_USAGE_PAGE_HAPTICS,
                 1,
                 HID_USAGE_HAPTICS_INTENSITY,
                 4,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)FeatureReport,
                 sizeof(FeatureReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(FeatureReport[1], 4);

    RtlZeroMemory(FeatureReport, sizeof(FeatureReport));
    FeatureReport[0] = 0x42;
    Status = HidP_SetUsageValue(
                 HidP_Feature,
                 HID_USAGE_PAGE_ORDINAL,
                 3,
                 3,
                 HID_USAGE_HAPTICS_WAVEFORM_CLICK,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)FeatureReport,
                 sizeof(FeatureReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    Status = HidP_SetUsageValue(
                 HidP_Feature,
                 HID_USAGE_PAGE_ORDINAL,
                 4,
                 3,
                 50,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)FeatureReport,
                 sizeof(FeatureReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(FeatureReport[1], 0x03);
    ok_eq_uint(FeatureReport[2], 0x10);
    ok_eq_uint(FeatureReport[11], 50);

    Value = 0;
    Status = HidP_GetUsageValue(
                 HidP_Feature,
                 HID_USAGE_PAGE_ORDINAL,
                 3,
                 3,
                 &Value,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)FeatureReport,
                 sizeof(FeatureReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, HID_USAGE_HAPTICS_WAVEFORM_CLICK);

    OutputReport[0] = 0x43;
    for (Index = 0; Index < RTL_NUMBER_OF(OutputFields); Index++)
    {
        Status = HidP_SetUsageValue(
                     HidP_Output,
                     HID_USAGE_PAGE_HAPTICS,
                     5,
                     OutputFields[Index].Usage,
                     OutputFields[Index].Value,
                     DeviceDescription.CollectionDesc[0].PreparsedData,
                     (PCHAR)OutputReport,
                     sizeof(OutputReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    }
    ok_eq_uint(OutputReport[1], 5);
    ok_eq_uint(OutputReport[2], 4);
    ok_eq_uint(OutputReport[3], 2);
    ok_eq_uint(OutputReport[4], 0xf4);
    ok_eq_uint(OutputReport[5], 0x01);
    ok_eq_uint(OutputReport[6], 0xb8);
    ok_eq_uint(OutputReport[7], 0x0b);

    for (Index = 0; Index < RTL_NUMBER_OF(OutputFields); Index++)
    {
        Value = 0;
        Status = HidP_GetUsageValue(
                     HidP_Output,
                     HID_USAGE_PAGE_HAPTICS,
                     5,
                     OutputFields[Index].Usage,
                     &Value,
                     DeviceDescription.CollectionDesc[0].PreparsedData,
                     (PCHAR)OutputReport,
                     sizeof(OutputReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_ulong(Value, OutputFields[Index].Value);
    }

    HidP_FreeCollectionDescription(&DeviceDescription);
}

static
VOID
TestUsageValueArrays(VOID)
{
    HIDP_DEVICE_DESC DeviceDescription;
    HIDP_BUTTON_CAPS ButtonCaps;
    HIDP_VALUE_CAPS ValueCaps;
    HIDP_CAPS Caps;
    UCHAR ArrayReport[5] = {0x20, 0x47, 0x88, 0xcc, 0x10};
    UCHAR OriginalReport[sizeof(ArrayReport)];
    UCHAR ScalarReport[5] = {0x21, 0x2a, 0, 0, 0};
    UCHAR UsageValue[4];
    static const UCHAR ExpectedValue[] = {0x11, 0x22, 0x33, 0x04};
    ULONG ScalarValue;
    USHORT ButtonCapsLength, ValueCapsLength;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(UsageValueArrayDescriptor,
                                           sizeof(UsageValueArrayDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 3);

    Status = HidP_GetCaps(DeviceDescription.CollectionDesc[0].PreparsedData,
                          &Caps);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    if (Status == HIDP_STATUS_SUCCESS)
    {
        ok_eq_uint(Caps.InputReportByteLength, sizeof(ArrayReport));
        ok_eq_uint(Caps.NumberInputValueCaps, 2);
        ok_eq_uint(Caps.NumberInputButtonCaps, 1);
        ok_eq_uint(Caps.NumberInputDataIndices, 7);
    }

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 &ValueCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
    {
        ok_eq_uint(ValueCaps.ReportID, 0x20);
        ok_eq_uint(ValueCaps.BitSize, 6);
        ok_eq_uint(ValueCaps.ReportCount, 5);
        ok_eq_uint(ValueCaps.NotRange.Usage, HID_USAGE_GENERIC_X);
        ok_eq_uint(ValueCaps.NotRange.DataIndex, 0);
    }

    ValueCapsLength = 1;
    Status = HidP_GetSpecificValueCaps(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_Y,
                 &ValueCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ValueCapsLength == 1)
        ok_eq_uint(ValueCaps.NotRange.DataIndex, 5);

    ButtonCapsLength = 1;
    Status = HidP_GetSpecificButtonCaps(
                 HidP_Input,
                 HID_USAGE_PAGE_BUTTON,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 1,
                 &ButtonCaps,
                 &ButtonCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ButtonCapsLength, 1);
    if (Status == HIDP_STATUS_SUCCESS && ButtonCapsLength == 1)
    {
        ok_eq_uint(ButtonCaps.ReportID, 0x22);
        ok_eq_uint(ButtonCaps.NotRange.DataIndex, 6);
    }

    RtlFillMemory(UsageValue, sizeof(UsageValue), 0xff);
    Status = HidP_GetUsageValueArray(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 (PCHAR)UsageValue,
                 sizeof(UsageValue),
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)ArrayReport,
                 sizeof(ArrayReport));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok(RtlCompareMemory(UsageValue,
                        ExpectedValue,
                        sizeof(ExpectedValue)) == sizeof(ExpectedValue),
       "Non-byte-aligned value array differs from the report\n");

    Status = HidP_GetUsageValueArray(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 (PCHAR)UsageValue,
                 sizeof(UsageValue) - 1,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)ArrayReport,
                 sizeof(ArrayReport));
    ok_eq_hex(Status, HIDP_STATUS_BUFFER_TOO_SMALL);

    ScalarValue = 0;
    Status = HidP_GetUsageValue(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 &ScalarValue,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)ArrayReport,
                 sizeof(ArrayReport));
    ok_eq_hex(Status, HIDP_STATUS_IS_VALUE_ARRAY);

    RtlCopyMemory(OriginalReport, ArrayReport, sizeof(ArrayReport));
    Status = HidP_SetUsageValue(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 1,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)ArrayReport,
                 sizeof(ArrayReport));
    ok_eq_hex(Status, HIDP_STATUS_IS_VALUE_ARRAY);
    ok(RtlCompareMemory(ArrayReport,
                        OriginalReport,
                        sizeof(ArrayReport)) == sizeof(ArrayReport),
       "Scalar write modified a value-array report\n");

    RtlCopyMemory(OriginalReport, ArrayReport, sizeof(ArrayReport));
    Status = HidP_SetUsageValueArray(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 (PCHAR)UsageValue,
                 sizeof(UsageValue),
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)ArrayReport,
                 sizeof(ArrayReport));
    ok_eq_hex(Status, HIDP_STATUS_NOT_IMPLEMENTED);
    ok(RtlCompareMemory(ArrayReport,
                        OriginalReport,
                        sizeof(ArrayReport)) == sizeof(ArrayReport),
       "Unsupported value-array write modified the report\n");

    Status = HidP_GetUsageValueArray(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 (PCHAR)UsageValue,
                 sizeof(UsageValue),
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)ScalarReport,
                 sizeof(ScalarReport));
    ok_eq_hex(Status, HIDP_STATUS_INCOMPATIBLE_REPORT_ID);

    Status = HidP_GetUsageValueArray(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_Y,
                 (PCHAR)UsageValue,
                 sizeof(UsageValue),
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)ScalarReport,
                 sizeof(ScalarReport));
    ok_eq_hex(Status, HIDP_STATUS_NOT_VALUE_ARRAY);

    HidP_FreeCollectionDescription(&DeviceDescription);
}

static
VOID
TestMultipleInputReports(VOID)
{
    HIDP_DEVICE_DESC DeviceDescription;
    HIDP_VALUE_CAPS ValueCaps[3];
    UCHAR Report1[5] = {1, 42, 0, 0, 0};
    UCHAR Report2[5] = {2, 0x34, 0x12, 0x78, 0x56};
    HIDP_CAPS Caps;
    USHORT ValueCapsLength;
    ULONG Value;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(MultipleInputReportDescriptor,
                                           sizeof(MultipleInputReportDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
    ok_eq_ulong(DeviceDescription.ReportIDsLength, 2);
    ok_eq_uint(DeviceDescription.CollectionDesc[0].InputLength, 5);
    ok_eq_uint(DeviceDescription.ReportIDs[0].ReportID, 1);
    ok_eq_uint(DeviceDescription.ReportIDs[0].InputLength, 2);
    ok_eq_uint(DeviceDescription.ReportIDs[1].ReportID, 2);
    ok_eq_uint(DeviceDescription.ReportIDs[1].InputLength, 5);

    Status = HidP_GetCaps(DeviceDescription.CollectionDesc[0].PreparsedData,
                          &Caps);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(Caps.InputReportByteLength, 5);

    ValueCapsLength = RTL_NUMBER_OF(ValueCaps);
    Status = HidP_GetSpecificValueCaps(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_PAGE_UNDEFINED,
                 ValueCaps,
                 &ValueCapsLength,
                 DeviceDescription.CollectionDesc[0].PreparsedData);
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_uint(ValueCapsLength, RTL_NUMBER_OF(ValueCaps));

    Value = 0;
    Status = HidP_GetUsageValue(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_X,
                 &Value,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)Report1,
                 sizeof(Report1));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, 42);

    Value = 0;
    Status = HidP_GetUsageValue(
                 HidP_Input,
                 HID_USAGE_PAGE_GENERIC,
                 HIDP_LINK_COLLECTION_UNSPECIFIED,
                 HID_USAGE_GENERIC_Y,
                 &Value,
                 DeviceDescription.CollectionDesc[0].PreparsedData,
                 (PCHAR)Report2,
                 sizeof(Report2));
    ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
    ok_eq_ulong(Value, 0x1234);

    HidP_FreeCollectionDescription(&DeviceDescription);
}

static
VOID
TestConsumerAndSystemControls(VOID)
{
    HIDP_DEVICE_DESC DeviceDescription;
    USAGE_AND_PAGE Usages[3];
    UCHAR ConsumerReport[] = {0x10, 0x05};
    UCHAR SystemReport[] = {0x11, 0x02};
    HIDP_KEYBOARD_MODIFIER_STATE ModifierState;
    HIDP_TEST_SCAN_CODES ScanCodes;
    ULONG UsageLength;
    NTSTATUS Status;

    Status = HidP_GetCollectionDescription(ConsumerControlDescriptor,
                                           sizeof(ConsumerControlDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
        ok_eq_ulong(DeviceDescription.ReportIDsLength, 1);
        ok_eq_uint(DeviceDescription.CollectionDesc[0].UsagePage,
                   HID_USAGE_PAGE_CONSUMER);
        ok_eq_uint(DeviceDescription.CollectionDesc[0].Usage,
                   HID_USAGE_CONSUMERCTRL);
        ok_eq_ulong(HidP_MaxUsageListLength(
                        HidP_Input,
                        HID_USAGE_PAGE_UNDEFINED,
                        DeviceDescription.CollectionDesc[0].PreparsedData),
                    3);

        RtlZeroMemory(Usages, sizeof(Usages));
        UsageLength = RTL_NUMBER_OF(Usages);
        Status = HidP_GetUsagesEx(
                     HidP_Input,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     Usages,
                     &UsageLength,
                     DeviceDescription.CollectionDesc[0].PreparsedData,
                     (PCHAR)ConsumerReport,
                     sizeof(ConsumerReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_ulong(UsageLength, 2);
        ok_eq_uint(Usages[0].UsagePage, HID_USAGE_PAGE_CONSUMER);
        ok_eq_uint(Usages[0].Usage, 0x00e2);
        ok_eq_uint(Usages[1].UsagePage, HID_USAGE_PAGE_CONSUMER);
        ok_eq_uint(Usages[1].Usage, 0x00ea);

        RtlZeroMemory(&ModifierState, sizeof(ModifierState));
        RtlZeroMemory(&ScanCodes, sizeof(ScanCodes));
        Status = HidP_TranslateUsageAndPagesToI8042ScanCodes(
                     Usages,
                     UsageLength,
                     HidP_Keyboard_Make,
                     &ModifierState,
                     TestInsertScanCodes,
                     &ScanCodes);
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_ulong(ScanCodes.Length, 4);
        ok_eq_uint(ScanCodes.Buffer[0], 0xe0);
        ok_eq_uint(ScanCodes.Buffer[1], 0x20);
        ok_eq_uint(ScanCodes.Buffer[2], 0xe0);
        ok_eq_uint(ScanCodes.Buffer[3], 0x2e);

        HidP_FreeCollectionDescription(&DeviceDescription);
    }

    Status = HidP_GetCollectionDescription(SystemControlDescriptor,
                                           sizeof(SystemControlDescriptor),
                                           NonPagedPool,
                                           &DeviceDescription);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong(DeviceDescription.CollectionDescLength, 1);
        ok_eq_ulong(DeviceDescription.ReportIDsLength, 1);
        ok_eq_uint(DeviceDescription.CollectionDesc[0].UsagePage,
                   HID_USAGE_PAGE_GENERIC);
        ok_eq_uint(DeviceDescription.CollectionDesc[0].Usage,
                   HID_USAGE_GENERIC_SYSTEM_CTL);
        ok_eq_ulong(HidP_MaxUsageListLength(
                        HidP_Input,
                        HID_USAGE_PAGE_UNDEFINED,
                        DeviceDescription.CollectionDesc[0].PreparsedData),
                    1);

        RtlZeroMemory(Usages, sizeof(Usages));
        UsageLength = RTL_NUMBER_OF(Usages);
        Status = HidP_GetUsagesEx(
                     HidP_Input,
                     HIDP_LINK_COLLECTION_UNSPECIFIED,
                     Usages,
                     &UsageLength,
                     DeviceDescription.CollectionDesc[0].PreparsedData,
                     (PCHAR)SystemReport,
                     sizeof(SystemReport));
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_ulong(UsageLength, 1);
        ok_eq_uint(Usages[0].UsagePage, HID_USAGE_PAGE_GENERIC);
        ok_eq_uint(Usages[0].Usage, HID_USAGE_GENERIC_SYSTEM_SLEEP);

        RtlZeroMemory(&ModifierState, sizeof(ModifierState));
        RtlZeroMemory(&ScanCodes, sizeof(ScanCodes));
        Status = HidP_TranslateUsageAndPagesToI8042ScanCodes(
                     Usages,
                     UsageLength,
                     HidP_Keyboard_Make,
                     &ModifierState,
                     TestInsertScanCodes,
                     &ScanCodes);
        ok_eq_hex(Status, HIDP_STATUS_SUCCESS);
        ok_eq_ulong(ScanCodes.Length, 2);
        ok_eq_uint(ScanCodes.Buffer[0], 0xe0);
        ok_eq_uint(ScanCodes.Buffer[1], 0x5f);

        HidP_FreeCollectionDescription(&DeviceDescription);
    }
}

NTSTATUS
TestHidPDescription(
    IN PDEVICE_OBJECT DeviceObject,
    IN ULONG ControlCode,
    IN PVOID Buffer OPTIONAL,
    IN SIZE_T InLength,
    IN OUT PSIZE_T OutLength)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(InLength);
    UNREFERENCED_PARAMETER(OutLength);

    PAGED_CODE();

    NT_VERIFY(ControlCode == IOCTL_TEST_DESCRIPTION);

    TestGetCollectionDescription();
    TestElanPrecisionTouchpad();
    TestHapticTouchpad();
    TestHidClassPtpCapabilities();
    TestHidClassPtpConfiguration();
    TestUsageValueArrays();
    TestMultipleInputReports();
    TestConsumerAndSystemControls();

    return STATUS_SUCCESS;
}
