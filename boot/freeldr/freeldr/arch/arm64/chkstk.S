/*
 * PROJECT:     FreeLoader for ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Stack probing stub for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

.global __chkstk
__chkstk:
    /* For bootloader, we don't need stack probing */
    /* Just return */
    ret

.global _chkstk
_chkstk:
    /* For bootloader, we don't need stack probing */
    /* Just return */
    ret
