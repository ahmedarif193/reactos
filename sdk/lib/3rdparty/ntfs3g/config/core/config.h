/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Freestanding NTFS-3G core configuration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define DISABLE_PLUGINS 1
#define HAVE_CTYPE_H 1
#define HAVE_DAEMON 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LOCALE_H 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_STDARG_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCHR 1
#define HAVE_STRING_H 1
#define HAVE_STRNLEN 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_WCHAR_H 1
#define IGNORE_MTAB 1
#define PACKAGE "ntfs-3g"
#define PACKAGE_NAME "ntfs-3g"
#define PACKAGE_STRING "ntfs-3g 2026.7.7"
#define PACKAGE_TARNAME "ntfs-3g"
#define PACKAGE_VERSION "2026.7.7"
#define STDC_HEADERS 1
#define VERSION "2026.7.7"
#define WORDS_LITTLEENDIAN 1
#define _FILE_OFFSET_BITS 64

typedef unsigned int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;

#ifndef S_IFLNK
#define S_IFLNK 0xa000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0xc000
#endif
#ifndef S_ISLNK
#define S_ISLNK(mode) (((mode) & 0xf000) == S_IFLNK)
#endif
#ifndef major
#define major(device) (((device) >> 8) & 0xff)
#endif
#ifndef minor
#define minor(device) ((device) & 0xff)
#endif
#ifndef makedev
#define makedev(major_id, minor_id) (((major_id) << 8) | (minor_id))
#endif

#define BLKSSZGET 0x1268
#define BLKGETSIZE64 0x80041272
#define BLKBSZSET 0x40041271

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

int ntfs3g_ffs(int Value);
#define ffs ntfs3g_ffs
