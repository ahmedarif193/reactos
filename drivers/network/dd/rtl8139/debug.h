/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Novell Eagle 2000 driver
 * FILE:        include/debug.h
 * PURPOSE:     Debugging support macros
 * DEFINES:     DBG     - Enable debug output
 *              NASSERT - Disable assertions
 */

#pragma once
#include <reactos/debug.h>

#ifdef ASSERT_IRQL
#undef ASSERT_IRQL
#endif
#ifdef ASSERT_IRQL_EQUAL
#undef ASSERT_IRQL_EQUAL
#endif

#if DBG

#define ASSERT_IRQL(x) ASSERT(KeGetCurrentIrql() <= (x))
#define ASSERT_IRQL_EQUAL(x) ASSERT(KeGetCurrentIrql() == (x))

#else /* DBG */

#define ASSERT_IRQL(x)
#define ASSERT_IRQL_EQUAL(x)
/* #define ASSERT(x) */  /* ndis.h */

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
    DPRINT1("%s at %s:%d is unimplemented, \
        but come back another day.\n", __FUNCTION__, __FILE__, __LINE__);

#endif /* _MSC_VER */


#define CHECKPOINT \
    do { DPRINT1("%s:%d\n", __FILE__, __LINE__); } while(0);

/* EOF */
