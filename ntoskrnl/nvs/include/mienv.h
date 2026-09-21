/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/mienv.h
 * PURPOSE:     Memory manager environment definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#if defined(MM_HOST_TEST)
#include <mmcc/nvs/env/mihost.h>
#else
#include <nvs/env/km/mikm.h>
#endif

#include <nvs/include/miarch.h>
#include <nvs/include/mipte.h>
