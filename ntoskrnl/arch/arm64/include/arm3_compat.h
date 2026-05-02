/*
 * ARM64 compatibility aliases for generic ARM3 code.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#define Write       Writable
#define NoExecute   UserNoExecute
#define Dirty       NotDirty
#define LargePage   NotLargePage
#define Global      NonGlobal
