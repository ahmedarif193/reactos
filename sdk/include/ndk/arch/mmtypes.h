/*++ NDK Version: 0095

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    mmtypes.h (ARCH)

Abstract:

    Portability file to choose the correct Architecture-specific file.

Author:

    Alex Ionescu (alex.ionescu@reactos.com)   06-Oct-2004

--*/

#ifndef _ARCH_MMTYPES_H
#define _ARCH_MMTYPES_H

#include <arch/target.h>

//
// Include the right file for this architecture.
//
#if defined(_M_IX86)
#include <i386/mmtypes.h>
#elif defined(_M_PPC)
#include <powerpc/mmtypes.h>
#elif defined(_M_ARM)
#include <arm/mmtypes.h>
#elif NDK_TARGET_ARM64_ABI
#include <arm64/mmtypes.h>
#elif NDK_TARGET_AMD64_ABI
#include <amd64/mmtypes.h>
#else
#error "Unknown processor"
#endif

#endif
