/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         AMD64 Assembly Offsets for Windows 10 compatibility
 * COPYRIGHT:       Copyright 2024 ReactOS Team
 */

#ifndef _ASM_OFFSETS_H
#define _ASM_OFFSETS_H

/* Missing offsets for Windows 10 compatibility */
#define ThSwapBusy 0x0  /* Field removed, define as 0 */
#define KTHREAD_LargeStack 0x0  /* Field removed, define as 0 */

#endif /* _ASM_OFFSETS_H */