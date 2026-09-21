/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/nvsenv.h
 * PURPOSE:     Memory manager execution environment selection
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#if defined(MM_HOST_TEST)
#include <mmcc/nvs/env/mihost.h>
#else
#include <ntoskrnl.h>
#endif

#include <nvs/include/miarch.h>
