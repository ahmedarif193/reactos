/*
 * Copyright © 2006 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Intel funded Tungsten Graphics to develop this 3D driver.
 * File originally authored by: Keith Whitwell <keithw@vmware.com>
 */

#include "elk_clip.h"


/* Point clipping, nothing to do?
 */
void elk_emit_point_clip( struct elk_clip_compile *c )
{
   /* Send an empty message to kill the thread:
    */
   elk_clip_tri_alloc_regs(c, 0);
   elk_clip_init_ff_sync(c);

   elk_clip_kill_thread(c);
}
