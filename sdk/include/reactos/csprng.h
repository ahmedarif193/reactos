/*
 * PROJECT:     ReactOS CSPRNG Library
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     User-mode interface to the kernel random number generator
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

BOOLEAN
NTAPI
RosCsprngFill(
    _Out_writes_bytes_all_(Length) PVOID Buffer,
    _In_ SIZE_T Length);

#ifdef __cplusplus
}
#endif
