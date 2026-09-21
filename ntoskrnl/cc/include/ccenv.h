/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/include/ccenv.h
 * PURPOSE:     Cache manager environment interface
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#if defined(CC_HOST_TEST)
#include <mmcc/cc/env/cchost.h>
#else
#include "../env/km/cckm.h"
#endif

#include "ccdef.h"
