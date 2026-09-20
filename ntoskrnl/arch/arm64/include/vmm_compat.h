/*
 * PROJECT:         LiberNT Kernel (ARM64)
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/*
 * ARM64 compatibility aliases for generic ARM3 code.
 */

#pragma once

#define Write       Writable
#define NoExecute   UserNoExecute
#define Dirty       NotDirty
#define Global      NonGlobal
