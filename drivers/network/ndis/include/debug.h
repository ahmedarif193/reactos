/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        include/debug.h
 * PURPOSE:     Debugging support macros
 * DEFINES:     DBG     - Enable debug output
 */

#pragma once
#include <reactos/debug.h>

#if DBG

#define ASSERT_IRQL(x) ASSERT(KeGetCurrentIrql() <= (x))

#else /* DBG */


#define ASSERT_IRQL(x)
/*#define ASSERT(x)*/

#endif /* DBG */


#ifdef assert
#undef assert
#endif
#define assert(x) ASSERT(x)
#define assert_irql(x) ASSERT_IRQL(x)


#ifdef UNIMPLEMENTED
#undef UNIMPLEMENTED
#endif
#define UNIMPLEMENTED \
    DPRINT1("Unimplemented.\n");


#define CHECKPOINT \
    do { DPRINT1("\n"); } while(0);

#define CP CHECKPOINT

/* EOF */
