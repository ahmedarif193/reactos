/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 machine initialization for UEFI
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <freeldr.h>
#include <disk.h>
#include <arch/arm64/arm64.h>
#include <uefildr.h>
#include <arch/uefi/machuefi.h>
#include <arch/uefi/uefisym.h>
#include <reactos/arm64/early_uart.h>
#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

/* Reference debug channel to avoid unused variable warning in debug builds */
#if DBG
static inline void UseDebugChannel(void) { (void)DbgDefaultChannel; }
#endif

/* External UEFI globals */
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
