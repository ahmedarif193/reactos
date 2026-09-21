/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/poolenv.h
 * PURPOSE:     Pool allocator environment interface
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#if defined(POOL_HOST_TEST)
#include <mmcc/pool/env/poolhost.h>
#else
#include "../env/km/poolkm.h"
#endif

#include "pooldef.h"
