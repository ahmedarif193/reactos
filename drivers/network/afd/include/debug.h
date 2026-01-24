/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Ancillary Function Driver
 * FILE:        include/debug.h
 * PURPOSE:     Debugging support macros
 * DEFINES:     DBG     - Enable debug output
 *              NASSERT - Disable assertions
 */

#pragma once
#include <reactos/debug.h>

#if DBG

#ifdef ASSERT
#undef ASSERT
#endif

#ifdef NASSERT
#define ASSERT(x)
#else /* NASSERT */
#define ASSERT(x) if (!(x)) { DPRINT1("Assertion "#x" failed at %s:%d\n", __FILE__, __LINE__); DbgBreakPoint(); }
#endif /* NASSERT */

#else /* DBG */

#define ASSERTKM(x)
#ifndef ASSERT
#define ASSERT(x)
#endif

#endif /* DBG */


#undef assert
#define assert(x) ASSERT(x)


#ifdef UNIMPLEMENTED
#undef UNIMPLEMENTED
#endif

#ifdef _MSC_VER
#define UNIMPLEMENTED \
    DPRINT1("The function at %s:%d is unimplemented, \
        but come back another day.\n", __FILE__, __LINE__);

#else /* _MSC_VER */

#define UNIMPLEMENTED \
    DPRINT1("%s at %s:%d is unimplemented, " \
        "but come back another day.\n", __FUNCTION__, __FILE__, __LINE__);

#endif /* _MSC_VER */


#define CHECKPOINT \
    DPRINT("\n");

#define CP CHECKPOINT

/* EOF */
