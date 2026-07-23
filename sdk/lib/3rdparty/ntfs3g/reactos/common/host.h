/*
 * PROJECT:     ReactOS NTFS-3G Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Host primitives supplied by each execution environment
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

void *
Ntfs3gRosHostAllocate(size_t Size);

void
Ntfs3gRosHostFree(void *Buffer);

void
Ntfs3gRosHostAcquire(void);

void
Ntfs3gRosHostRelease(void);

int64_t
Ntfs3gRosHostGetTime(void);

void
Ntfs3gRosHostLog(int IsError,
                 const char *Message);

int *
ntfs3g_errno_location(void);
