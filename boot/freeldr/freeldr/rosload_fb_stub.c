/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Provide framebuffer info symbol for slim rosload builds
 */

#include "freeldr.h"

/*
 * rosload does not currently carry the full PC video stack, but winldr expects
 * this symbol to exist when filling in the GOP framebuffer handoff.  By
 * shipping a zero-initialized instance we keep the linker happy while
 * preserving the existing behaviour (no pre-configured framebuffer data for
 * BIOS builds).
 */
FREELDR_FRAMEBUFFER_INFO PcFramebufferInfo = { 0 };
