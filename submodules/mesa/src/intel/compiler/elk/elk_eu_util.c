/*
 * Copyright © 2006 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Intel funded Tungsten Graphics to develop this 3D driver.
 * File originally authored by: Keith Whitwell <keithw@vmware.com>
 */

#include "elk_eu_defines.h"
#include "elk_eu.h"


void elk_math_invert( struct elk_codegen *p,
			     struct elk_reg dst,
			     struct elk_reg src)
{
   elk_gfx4_math(p,
	     dst,
	     ELK_MATH_FUNCTION_INV,
	     0,
	     src,
	     ELK_MATH_PRECISION_FULL);
}



void elk_copy4(struct elk_codegen *p,
	       struct elk_reg dst,
	       struct elk_reg src,
	       unsigned count)
{
   unsigned i;

   dst = vec4(dst);
   src = vec4(src);

   for (i = 0; i < count; i++)
   {
      unsigned delta = i*32;
      elk_MOV(p, byte_offset(dst, delta),    byte_offset(src, delta));
      elk_MOV(p, byte_offset(dst, delta+16), byte_offset(src, delta+16));
   }
}


void elk_copy8(struct elk_codegen *p,
	       struct elk_reg dst,
	       struct elk_reg src,
	       unsigned count)
{
   unsigned i;

   dst = vec8(dst);
   src = vec8(src);

   for (i = 0; i < count; i++)
   {
      unsigned delta = i*32;
      elk_MOV(p, byte_offset(dst, delta),    byte_offset(src, delta));
   }
}


void elk_copy_indirect_to_indirect(struct elk_codegen *p,
				   struct elk_indirect dst_ptr,
				   struct elk_indirect src_ptr,
				   unsigned count)
{
   unsigned i;

   for (i = 0; i < count; i++)
   {
      unsigned delta = i*32;
      elk_MOV(p, deref_4f(dst_ptr, delta),    deref_4f(src_ptr, delta));
      elk_MOV(p, deref_4f(dst_ptr, delta+16), deref_4f(src_ptr, delta+16));
   }
}


void elk_copy_from_indirect(struct elk_codegen *p,
			    struct elk_reg dst,
			    struct elk_indirect ptr,
			    unsigned count)
{
   unsigned i;

   dst = vec4(dst);

   for (i = 0; i < count; i++)
   {
      unsigned delta = i*32;
      elk_MOV(p, byte_offset(dst, delta),    deref_4f(ptr, delta));
      elk_MOV(p, byte_offset(dst, delta+16), deref_4f(ptr, delta+16));
   }
}
