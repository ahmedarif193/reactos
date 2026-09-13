/*
 * Copyright © 2026 Amazon.com, Inc. or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_TRACE_H
#define __PAN_TRACE_H

#include "util/perf/cpu_trace.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Panfrost wrappers for Mesa CPU scope traces.
 *
 * CPU tracing (not to be confused with GPU command stream tracing with
 * PAN_MESA_DEBUG=trace) is often enabled in release builds. Since each trace
 * pushed down the underlying tracing backend (Perfetto, Gpuvis, sysprof, etc)
 * has a slight cost, Panfrost extends the Mesa CPU scope traces by
 * associating each trace to a category that must be enabled at run-time. This
 * allows to keep adding useful trace points into Panfrost without worrying
 * about the implied inherent latency.
 *
 * There are 3 categories ("lib" for the shared utilities, "gl" for Gallium,
 * "vk" for PanVK) divided into subcategories so that subsystems can easily be
 * traced. A list of categories can be passed to Panfrost through the
 * PAN_CPU_TRACE environment variable like that:
 *
 *   $ PAN_CPU_TRACE=gl,lib program
 *
 * Passing a category enables traces for all its subcategories. Subcategories
 * can be passed instead for finer grained traces like that:
 *
 *   $ PAN_CPU_TRACE=gl.blit,gl.bo,gl.job program
 */

/* Panfrost trace categories.
 *
 * To be kept in sync with the categories_table array in pan_trace.c.
 */
/* clang-format off */
enum pan_trace_category {
   /* Library categories. */
   PAN_TRACE_LIB_AFBC      = BITFIELD_BIT(1),  /* "lib.afbc" */
   PAN_TRACE_LIB_DESC      = BITFIELD_BIT(2),  /* "lib.desc" */
   PAN_TRACE_LIB_KMOD      = BITFIELD_BIT(3),  /* "lib.kmod" */

   /* Gallium categories. */
   PAN_TRACE_GL_BLIT       = BITFIELD_BIT(4),  /* "gl.blit" */
   PAN_TRACE_GL_BO         = BITFIELD_BIT(5),  /* "gl.bo" */
   PAN_TRACE_GL_CMDSTREAM  = BITFIELD_BIT(6),  /* "gl.cmdstream" */
   PAN_TRACE_GL_CONTEXT    = BITFIELD_BIT(7),  /* "gl.context" */
   PAN_TRACE_GL_CSF        = BITFIELD_BIT(8),  /* "gl.csf" */
   PAN_TRACE_GL_DISK_CACHE = BITFIELD_BIT(9),  /* "gl.disk_cache" */
   PAN_TRACE_GL_FB_PRELOAD = BITFIELD_BIT(10), /* "gl.fb_preload" */
   PAN_TRACE_GL_JM         = BITFIELD_BIT(11), /* "gl.jm" */
   PAN_TRACE_GL_JOB        = BITFIELD_BIT(12), /* "gl.job" */
   PAN_TRACE_GL_MEMPOOL    = BITFIELD_BIT(13), /* "gl.mempool" */
   PAN_TRACE_GL_RESOURCE   = BITFIELD_BIT(14), /* "gl.resource" */
   PAN_TRACE_GL_SHADER     = BITFIELD_BIT(15), /* "gl.shader" */

   /* Vulkan categories. */
   PAN_TRACE_VK_CSF        = BITFIELD_BIT(16), /* "vk.csf" */
};
/* clang-format on */

extern uint64_t pan_trace_categories;

/* Add a Mesa CPU scope trace for a given Panfrost category using printf like
 * formatting.
 */
#define PAN_TRACE_SCOPE(category, format, ...)                               \
   MESA_TRACE_SCOPE_IF(category & pan_trace_categories, format, ##__VA_ARGS__)

/* Add a Mesa CPU scope trace for a given Panfrost category using current
 * function name.
 */
#define PAN_TRACE_FUNC(category)                                             \
   MESA_TRACE_FUNC_IF(category & pan_trace_categories)

/* Parse the PAN_CPU_TRACE environment variable and initialize CPU tracing.
 * The PAN_CPU_TRACE environment variable stores a list of categories (see
 * enum pan_trace_category) separated by a comma, a semicolon or a space.
 */
void pan_trace_init(void);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* __PAN_TRACE_H */
