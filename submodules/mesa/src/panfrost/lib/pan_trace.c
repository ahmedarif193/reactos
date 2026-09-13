/*
 * Copyright © 2026 Amazon.com, Inc. or its affiliates.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pan_trace.h"

#include "util/os_misc.h"

#define PAN_TRACE_ENV_VAR "PAN_CPU_TRACE"

#define CATEGORY(str, flag) { str, ARRAY_SIZE(str) - 1, (uint64_t) (flag) }

/* To be kept in sync with the pan_trace_category enum in pan_trace.h. */
/* clang-format off */
static struct {
   const char *str;
   size_t len;
   uint64_t flag;
} categories_table[] = {
   /* Library categories. */
   CATEGORY("lib.afbc",      PAN_TRACE_LIB_AFBC),
   CATEGORY("lib.desc",      PAN_TRACE_LIB_DESC),
   CATEGORY("lib.kmod",      PAN_TRACE_LIB_KMOD),

   CATEGORY("lib",           PAN_TRACE_LIB_AFBC |
                             PAN_TRACE_LIB_DESC |
                             PAN_TRACE_LIB_KMOD),

   /* Gallium categories. */
   CATEGORY("gl.blit",       PAN_TRACE_GL_BLIT),
   CATEGORY("gl.bo",         PAN_TRACE_GL_BO),
   CATEGORY("gl.cmdstream",  PAN_TRACE_GL_CMDSTREAM),
   CATEGORY("gl.context",    PAN_TRACE_GL_CONTEXT),
   CATEGORY("gl.csf",        PAN_TRACE_GL_CSF),
   CATEGORY("gl.disk_cache", PAN_TRACE_GL_DISK_CACHE),
   CATEGORY("gl.fb_preload", PAN_TRACE_GL_FB_PRELOAD),
   CATEGORY("gl.jm",         PAN_TRACE_GL_JM),
   CATEGORY("gl.job",        PAN_TRACE_GL_JOB),
   CATEGORY("gl.mempool",    PAN_TRACE_GL_MEMPOOL),
   CATEGORY("gl.resource",   PAN_TRACE_GL_RESOURCE),
   CATEGORY("gl.shader",     PAN_TRACE_GL_SHADER),

   CATEGORY("gl",            PAN_TRACE_GL_BLIT |
                             PAN_TRACE_GL_BO |
                             PAN_TRACE_GL_CMDSTREAM |
                             PAN_TRACE_GL_CONTEXT |
                             PAN_TRACE_GL_CSF |
                             PAN_TRACE_GL_DISK_CACHE |
                             PAN_TRACE_GL_FB_PRELOAD |
                             PAN_TRACE_GL_JM |
                             PAN_TRACE_GL_JOB |
                             PAN_TRACE_GL_MEMPOOL |
                             PAN_TRACE_GL_RESOURCE |
                             PAN_TRACE_GL_SHADER),

   /* Vulkan categories. */
   CATEGORY("vk.csf",        PAN_TRACE_VK_CSF),

   CATEGORY("vk",            PAN_TRACE_VK_CSF),
};
/* clang-format on */

uint64_t pan_trace_categories = 0;

static bool
is_separator(char c)
{
   return c == ',' || c == ';' || c == ' ';
}

void
pan_trace_init(void)
{
   const char *list = os_get_option(PAN_TRACE_ENV_VAR);
   const char *str = NULL;
   uint64_t categories = 0;
   char prev_char = ',';

   if (!list)
      return;

   /* Parse list and flag enabled categories. */
   for (int i = 0; prev_char; prev_char = list[i++]) {
      if (!is_separator(list[i]) && list[i]) {
         if (is_separator(prev_char))
            str = &list[i];
      } else if (!is_separator(prev_char)) {
         for (int j = 0; j < ARRAY_SIZE(categories_table); j++) {
            size_t len = &list[i] - str;
            if (categories_table[j].len == len &&
                !strncasecmp(categories_table[j].str, str, len)) {
               categories |= categories_table[j].flag;
               break;
            }
         }
      }
   }

   pan_trace_categories = categories;
}
