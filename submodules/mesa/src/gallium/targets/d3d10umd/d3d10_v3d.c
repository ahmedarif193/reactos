/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Direct3D Gallium target for the ReactOS Raspberry Pi 5 WDDM adapter.
 * There is intentionally no software fallback in this target.
 */

#include <stddef.h>
#include <stdint.h>

#include <windows.h>
#include "winddk_compat.h"
#include <d3dkmthk.h>

#include "pipe/p_screen.h"
#include "v3d_d3dkmt_public.h"

struct pipe_screen *d3d10_create_screen(void *adapter, void *device,
                                        const void *callbacks);
struct pipe_resource *d3d10_create_resource(
   struct pipe_screen *screen, const struct pipe_resource *templ,
   void *runtime_resource, D3DKMT_HANDLE *allocation);
struct pipe_screen *
d3d10_create_screen(void *adapter, void *device, const void *callbacks)
{
   return v3d_d3dkmt_screen_create_umd(NULL, adapter, device, callbacks);
}

struct pipe_resource *
d3d10_create_resource(struct pipe_screen *screen,
                      const struct pipe_resource *templ,
                      void *runtime_resource, D3DKMT_HANDLE *allocation)
{
   struct pipe_resource *resource;

   if (allocation)
      *allocation = 0;
   if (!screen || !templ || !runtime_resource ||
       !v3d_d3dkmt_runtime_resource_begin(screen, runtime_resource))
      return NULL;

   resource = screen->resource_create(screen, templ);
   v3d_d3dkmt_runtime_resource_end(screen);
   if (resource && allocation)
      *allocation =
         v3d_d3dkmt_resource_allocation(screen, resource);
   return resource;
}
