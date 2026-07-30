/** @file
  EFI Simple File System Protocol and EFI File Protocol as defined in
  the UEFI 2.3 specification.

  The Simple File System protocol allows code running in the boot services
  environment to obtain file-based access to a device.

  Copyright (c) 2006 - 2014, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef __SIMPLE_FILE_SYSTEM_H__
#define __SIMPLE_FILE_SYSTEM_H__

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
  { \
    0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } \
  }

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_FILE_PROTOCOL *EFI_FILE_HANDLE;

/* Forward-declare the OpenVolume callback */
typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
  IN  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *This,
  OUT EFI_FILE_PROTOCOL                **Root
  );

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION  0x00010000

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64                                        Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME   OpenVolume;
};

/* ----------------------------------------------------------------------- */
/* EFI_FILE_PROTOCOL                                                       */
/* ----------------------------------------------------------------------- */

#define EFI_FILE_PROTOCOL_REVISION   0x00010000
#define EFI_FILE_PROTOCOL_REVISION2  0x00020000
#define EFI_FILE_PROTOCOL_LATEST_REVISION EFI_FILE_PROTOCOL_REVISION2
#define EFI_FILE_REVISION EFI_FILE_PROTOCOL_REVISION

/* Open modes */
#define EFI_FILE_MODE_READ    0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE   0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE  0x8000000000000000ULL

/* File attributes */
#define EFI_FILE_READ_ONLY    0x0000000000000001ULL
#define EFI_FILE_HIDDEN       0x0000000000000002ULL
#define EFI_FILE_SYSTEM       0x0000000000000004ULL
#define EFI_FILE_RESERVED     0x0000000000000008ULL
#define EFI_FILE_DIRECTORY    0x0000000000000010ULL
#define EFI_FILE_ARCHIVE      0x0000000000000020ULL
#define EFI_FILE_VALID_ATTR   0x0000000000000037ULL

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_OPEN)(
  IN  EFI_FILE_PROTOCOL  *This,
  OUT EFI_FILE_PROTOCOL  **NewHandle,
  IN  CHAR16             *FileName,
  IN  UINT64             OpenMode,
  IN  UINT64             Attributes
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_CLOSE)(
  IN EFI_FILE_PROTOCOL  *This
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_DELETE)(
  IN EFI_FILE_PROTOCOL  *This
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_READ)(
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT UINTN              *BufferSize,
  OUT    VOID               *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_WRITE)(
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT UINTN              *BufferSize,
  IN     VOID               *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_SET_POSITION)(
  IN EFI_FILE_PROTOCOL  *This,
  IN UINT64             Position
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_GET_POSITION)(
  IN  EFI_FILE_PROTOCOL  *This,
  OUT UINT64             *Position
  );

#define EFI_FILE_INFO_ID \
  { \
    0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } \
  }

typedef struct {
    UINT64    Size;
    UINT64    FileSize;
    UINT64    PhysicalSize;
    EFI_TIME  CreateTime;
    EFI_TIME  LastAccessTime;
    EFI_TIME  ModificationTime;
    UINT64    Attribute;
    CHAR16    FileName[1]; /* variable-length */
} EFI_FILE_INFO;

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_GET_INFO)(
  IN     EFI_FILE_PROTOCOL  *This,
  IN     EFI_GUID           *InformationType,
  IN OUT UINTN              *BufferSize,
  OUT    VOID               *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_SET_INFO)(
  IN EFI_FILE_PROTOCOL  *This,
  IN EFI_GUID           *InformationType,
  IN UINTN              BufferSize,
  IN VOID               *Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_FLUSH)(
  IN EFI_FILE_PROTOCOL  *This
  );

typedef struct {
    EFI_EVENT  Event;
    EFI_STATUS Status;
    UINTN      BufferSize;
    VOID       *Buffer;
} EFI_FILE_IO_TOKEN;

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_OPEN_EX)(
  IN     EFI_FILE_PROTOCOL  *This,
  OUT    EFI_FILE_PROTOCOL  **NewHandle,
  IN     CHAR16             *FileName,
  IN     UINT64             OpenMode,
  IN     UINT64             Attributes,
  IN OUT EFI_FILE_IO_TOKEN  *Token
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_READ_EX)(
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT EFI_FILE_IO_TOKEN  *Token
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_WRITE_EX)(
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT EFI_FILE_IO_TOKEN  *Token
  );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_FLUSH_EX)(
  IN     EFI_FILE_PROTOCOL  *This,
  IN OUT EFI_FILE_IO_TOKEN  *Token
  );

struct _EFI_FILE_PROTOCOL {
    UINT64                Revision;
    EFI_FILE_OPEN         Open;
    EFI_FILE_CLOSE        Close;
    EFI_FILE_DELETE       Delete;
    EFI_FILE_READ         Read;
    EFI_FILE_WRITE        Write;
    EFI_FILE_GET_POSITION GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO     GetInfo;
    EFI_FILE_SET_INFO     SetInfo;
    EFI_FILE_FLUSH        Flush;
    EFI_FILE_OPEN_EX      OpenEx;
    EFI_FILE_READ_EX      ReadEx;
    EFI_FILE_WRITE_EX     WriteEx;
    EFI_FILE_FLUSH_EX     FlushEx;
};

extern EFI_GUID gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID gEfiFileInfoGuid;

#endif
