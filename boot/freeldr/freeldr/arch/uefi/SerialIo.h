/*
 * PROJECT:     FreeLoader
 * LICENSE:     BSD-2-Clause-Patent (from original UEFI spec)
 * PURPOSE:     Minimal Serial I/O protocol definitions for UEFI.
 */

#pragma once

#include <Uefi.h>

#define EFI_SERIAL_IO_PROTOCOL_GUID \
  { \
    0xBB25CF6F, 0xF1D4, 0x11D2, { 0x9A, 0x0C, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0xFD } \
  }

typedef struct _EFI_SERIAL_IO_PROTOCOL EFI_SERIAL_IO_PROTOCOL;

typedef enum {
  DefaultParity,
  NoParity,
  EvenParity,
  OddParity,
  MarkParity,
  SpaceParity
} EFI_PARITY_TYPE;

typedef enum {
  DefaultStopBits,
  OneStopBit,
  OneFiveStopBits,
  TwoStopBits
} EFI_STOP_BITS_TYPE;

typedef struct {
  UINT32    ControlMask;
  UINT32    Timeout;
  UINT64    BaudRate;
  UINT32    ReceiveFifoDepth;
  UINT32    DataBits;
  UINT32    Parity;
  UINT32    StopBits;
} EFI_SERIAL_IO_MODE;

typedef
EFI_STATUS
(EFIAPI *EFI_SERIAL_RESET)(
  IN EFI_SERIAL_IO_PROTOCOL *This
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SERIAL_SET_ATTRIBUTES)(
  IN EFI_SERIAL_IO_PROTOCOL         *This,
  IN UINT64                         BaudRate,
  IN UINT32                         ReceiveFifoDepth,
  IN UINT32                         Timeout,
  IN EFI_PARITY_TYPE                Parity,
  IN UINT8                          DataBits,
  IN EFI_STOP_BITS_TYPE             StopBits
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SERIAL_SET_CONTROL_BITS)(
  IN EFI_SERIAL_IO_PROTOCOL         *This,
  IN UINT32                         Control
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SERIAL_GET_CONTROL_BITS)(
  IN EFI_SERIAL_IO_PROTOCOL         *This,
  OUT UINT32                        *Control
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SERIAL_WRITE)(
  IN EFI_SERIAL_IO_PROTOCOL         *This,
  IN OUT UINTN                      *BufferSize,
  IN VOID                           *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_SERIAL_READ)(
  IN EFI_SERIAL_IO_PROTOCOL         *This,
  IN OUT UINTN                      *BufferSize,
  OUT VOID                          *Buffer
  );

struct _EFI_SERIAL_IO_PROTOCOL {
  UINT32                         Revision;
  EFI_SERIAL_RESET               Reset;
  EFI_SERIAL_SET_ATTRIBUTES      SetAttributes;
  EFI_SERIAL_SET_CONTROL_BITS    SetControl;
  EFI_SERIAL_GET_CONTROL_BITS    GetControl;
  EFI_SERIAL_WRITE               Write;
  EFI_SERIAL_READ                Read;
  EFI_SERIAL_IO_MODE             *Mode;
  CONST EFI_GUID                 *DeviceTypeGuid;
};

extern EFI_GUID  gEfiSerialIoProtocolGuid;
