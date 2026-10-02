/*
 * PROJECT:     LiberNT NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/ppc/hardware.h
 * PURPOSE:     Windows NT PowerPC page-table entry helpers
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "archdef.h"

#define MI_PPC_ACCESS_MASK (MI_PPC_PTE_WRITE | MI_PPC_PTE_OWNER | MI_PPC_PTE_COPYONWRITE)
