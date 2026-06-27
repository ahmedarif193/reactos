/*++ NDK Version: 0095

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    ketypes.h (ARCH)

Abstract:

    Portability file to choose the correct Architecture-specific file.

Author:

    Alex Ionescu (alex.ionescu@reactos.com)   06-Oct-2004

--*/

#ifndef _ARCH_KETYPES_H
#define _ARCH_KETYPES_H

#include <arch/target.h>

//
// Include the right file for this architecture.
//
#ifdef _M_IX86
#include <i386/ketypes.h>
#elif defined(_M_PPC)
#include <powerpc/ketypes.h>
#elif defined(_M_ARM)
#include <arm/ketypes.h>
#elif NDK_TARGET_ARM64_ABI
#include <arm64/ketypes.h>
#elif NDK_TARGET_AMD64_ABI
#include <amd64/ketypes.h>
#else
#error "Unknown processor"
#endif

#endif
