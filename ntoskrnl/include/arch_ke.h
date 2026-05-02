/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * FILE:            ntoskrnl/include/arch_ke.h
 * PURPOSE:         Architecture-specific KE include forwarding
 * REASON:         Forward arch-specific include after architecture directory refactor
 * COPYRIGHT:       Copyright (c) Ahmed ARIF (arif.ing@outlook.com)
 */

#pragma once

#if defined(_M_AMD64)
#include "arch/amd64/include/ke.h"
#elif defined(_M_IX86)
#include "arch/i386/include/ke.h"
#elif defined(_M_ARM64)
#include "arch/arm64/include/ke.h"
#elif defined(_M_ARM)
#include "arch/arm/include/ke.h"
#else
#error Unsupported arch
#endif
