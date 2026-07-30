/** @file
  EFI PCI I/O Protocol definitions.

  Copyright (c) 2006 - 2018, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#pragma once

#define EFI_PCI_IO_PROTOCOL_GUID \
  { \
    0x4cf5b200, 0x68b8, 0x4ca5, {0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a } \
  }

typedef struct _EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;

typedef enum {
    EfiPciIoWidthUint8,
    EfiPciIoWidthUint16,
    EfiPciIoWidthUint32,
    EfiPciIoWidthUint64,
    EfiPciIoWidthFifoUint8,
    EfiPciIoWidthFifoUint16,
    EfiPciIoWidthFifoUint32,
    EfiPciIoWidthFifoUint64,
    EfiPciIoWidthFillUint8,
    EfiPciIoWidthFillUint16,
    EfiPciIoWidthFillUint32,
    EfiPciIoWidthFillUint64,
    EfiPciIoWidthMaximum
} EFI_PCI_IO_PROTOCOL_WIDTH;

#define EFI_PCI_IO_ATTRIBUTE_IO          0x0100
#define EFI_PCI_IO_ATTRIBUTE_MEMORY      0x0200
#define EFI_PCI_IO_ATTRIBUTE_BUS_MASTER  0x0400
#define EFI_PCI_DEVICE_ENABLE \
    (EFI_PCI_IO_ATTRIBUTE_IO | EFI_PCI_IO_ATTRIBUTE_MEMORY | EFI_PCI_IO_ATTRIBUTE_BUS_MASTER)

typedef enum {
    EfiPciIoOperationBusMasterRead,
    EfiPciIoOperationBusMasterWrite,
    EfiPciIoOperationBusMasterCommonBuffer,
    EfiPciIoOperationMaximum
} EFI_PCI_IO_PROTOCOL_OPERATION;

typedef enum {
    EfiPciIoAttributeOperationGet,
    EfiPciIoAttributeOperationSet,
    EfiPciIoAttributeOperationEnable,
    EfiPciIoAttributeOperationDisable,
    EfiPciIoAttributeOperationSupported,
    EfiPciIoAttributeOperationMaximum
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_POLL_IO_MEM)(
  IN  EFI_PCI_IO_PROTOCOL        *This,
  IN  EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN  UINT8                      BarIndex,
  IN  UINT64                     Offset,
  IN  UINT64                     Mask,
  IN  UINT64                     Value,
  IN  UINT64                     Delay,
  OUT UINT64                     *Result
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_IO_MEM)(
  IN     EFI_PCI_IO_PROTOCOL        *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN     UINT8                      BarIndex,
  IN     UINT64                     Offset,
  IN     UINTN                      Count,
  IN OUT VOID                       *Buffer
  );

typedef struct {
    EFI_PCI_IO_PROTOCOL_IO_MEM Read;
    EFI_PCI_IO_PROTOCOL_IO_MEM Write;
} EFI_PCI_IO_PROTOCOL_ACCESS;

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_CONFIG)(
  IN     EFI_PCI_IO_PROTOCOL        *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN     UINT32                     Offset,
  IN     UINTN                      Count,
  IN OUT VOID                       *Buffer
  );

typedef struct {
    EFI_PCI_IO_PROTOCOL_CONFIG Read;
    EFI_PCI_IO_PROTOCOL_CONFIG Write;
} EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS;

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_COPY_MEM)(
  IN EFI_PCI_IO_PROTOCOL        *This,
  IN EFI_PCI_IO_PROTOCOL_WIDTH  Width,
  IN UINT8                      DestBarIndex,
  IN UINT64                     DestOffset,
  IN UINT8                      SrcBarIndex,
  IN UINT64                     SrcOffset,
  IN UINTN                      Count
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_MAP)(
  IN     EFI_PCI_IO_PROTOCOL            *This,
  IN     EFI_PCI_IO_PROTOCOL_OPERATION  Operation,
  IN     VOID                           *HostAddress,
  IN OUT UINTN                          *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS           *DeviceAddress,
  OUT    VOID                           **Mapping
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_UNMAP)(
  IN EFI_PCI_IO_PROTOCOL *This,
  IN VOID                *Mapping
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER)(
  IN  EFI_PCI_IO_PROTOCOL *This,
  IN  EFI_ALLOCATE_TYPE   Type,
  IN  EFI_MEMORY_TYPE     MemoryType,
  IN  UINTN               Pages,
  OUT VOID                **HostAddress,
  IN  UINT64              Attributes
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_FREE_BUFFER)(
  IN EFI_PCI_IO_PROTOCOL *This,
  IN UINTN               Pages,
  IN VOID                *HostAddress
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_FLUSH)(
  IN EFI_PCI_IO_PROTOCOL *This
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_GET_LOCATION)(
  IN  EFI_PCI_IO_PROTOCOL *This,
  OUT UINTN               *SegmentNumber,
  OUT UINTN               *BusNumber,
  OUT UINTN               *DeviceNumber,
  OUT UINTN               *FunctionNumber
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_ATTRIBUTES)(
  IN  EFI_PCI_IO_PROTOCOL                      *This,
  IN  EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION  Operation,
  IN  UINT64                                   Attributes,
  OUT UINT64                                   *Result OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES)(
  IN  EFI_PCI_IO_PROTOCOL *This,
  IN  UINT8               BarIndex,
  OUT UINT64              *Supports OPTIONAL,
  OUT VOID                **Resources OPTIONAL
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES)(
  IN     EFI_PCI_IO_PROTOCOL *This,
  IN     UINT64              Attributes,
  IN     UINT8               BarIndex,
  IN OUT UINT64              *Offset,
  IN OUT UINT64              *Length
  );

struct _EFI_PCI_IO_PROTOCOL {
    EFI_PCI_IO_PROTOCOL_POLL_IO_MEM        PollMem;
    EFI_PCI_IO_PROTOCOL_POLL_IO_MEM        PollIo;
    EFI_PCI_IO_PROTOCOL_ACCESS             Mem;
    EFI_PCI_IO_PROTOCOL_ACCESS             Io;
    EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS      Pci;
    EFI_PCI_IO_PROTOCOL_COPY_MEM           CopyMem;
    EFI_PCI_IO_PROTOCOL_MAP                Map;
    EFI_PCI_IO_PROTOCOL_UNMAP              Unmap;
    EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER    AllocateBuffer;
    EFI_PCI_IO_PROTOCOL_FREE_BUFFER        FreeBuffer;
    EFI_PCI_IO_PROTOCOL_FLUSH              Flush;
    EFI_PCI_IO_PROTOCOL_GET_LOCATION       GetLocation;
    EFI_PCI_IO_PROTOCOL_ATTRIBUTES         Attributes;
    EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES GetBarAttributes;
    EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES SetBarAttributes;
    UINT64                                 RomSize;
    VOID                                   *RomImage;
};
