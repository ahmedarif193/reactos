/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        include/debug.h
 * PURPOSE:     Debugging support macros
 * DEFINES:     DBG     - Enable debug output
 *              NASSERT - Disable assertions
 */

#pragma once
#include <reactos/debug.h>

#if DBG

#define ASSERT_IRQL(x) ASSERT(KeGetCurrentIrql() <= (x))

#else /* DBG */

#define ASSERT_IRQL(x) ((VOID)0)

#endif /* DBG */


#ifdef assert
#undef assert
#endif
#define assert(x) ASSERT(x)
#define assert_irql(x) ASSERT_IRQL(x)


#ifdef UNIMPLEMENTED
#undef UNIMPLEMENTED
#endif

#ifdef _MSC_VER
#define UNIMPLEMENTED \
    DPRINT1("The function at %s:%d is unimplemented, \
        but come back another day.\n", __FILE__, __LINE__);

#else /* _MSC_VER */

#define UNIMPLEMENTED \
    DPRINT1("(%s:%d)(%s) is unimplemented, \
        but come back another day.\n", __FILE__, __LINE__, __FUNCTION__);

#endif /* _MSC_VER */


#define CHECKPOINT \
    do { DPRINT("(%s:%d)\n", __FILE__, __LINE__); } while(0);

#define CP CHECKPOINT

#include <memtrack.h>
