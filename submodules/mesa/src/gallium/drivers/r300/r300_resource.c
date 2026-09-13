/*
 * Copyright 2010 Red Hat Inc.
 * Authors: Dave Airlie
 * SPDX-License-Identifier: MIT
 */

#include "r300_context.h"
#include "r300_texture.h"
#include "r300_texture_desc.h"
#include "r300_transfer.h"
#include "r300_screen_buffer.h"

static bool
r300_3d_subdata_needs_layered_upload(struct pipe_context *pipe,
                                     struct pipe_resource *resource,
                                     unsigned level,
                                     const struct pipe_box *box)
{
   struct r300_resource *tex = r300_resource(resource);
   struct r300_screen *screen = r300_context(pipe)->screen;
   bool is_rs690 = screen->caps.family == CHIP_RS600 ||
                   screen->caps.family == CHIP_RS690 ||
                   screen->caps.family == CHIP_RS740;
   unsigned tile_width, tile_height;

   if (resource->target != PIPE_TEXTURE_3D || box->depth <= 1)
      return false;

   if (!tex->tex.microtile && !tex->tex.macrotile[level])
      return false;

   tile_width = r300_get_pixel_alignment(resource->format,
                                         resource->nr_samples,
                                         tex->tex.microtile,
                                         tex->tex.macrotile[level],
                                         DIM_WIDTH, is_rs690,
                                         resource->bind & PIPE_BIND_SCANOUT);
   tile_height = r300_get_pixel_alignment(resource->format,
                                          resource->nr_samples,
                                          tex->tex.microtile,
                                          tex->tex.macrotile[level],
                                          DIM_HEIGHT, is_rs690,
                                          resource->bind & PIPE_BIND_SCANOUT);

   return box->x % tile_width || box->y % tile_height ||
          box->width % tile_width || box->height % tile_height;
}

static void
r300_texture_subdata(struct pipe_context *pipe,
                     struct pipe_resource *resource,
                     unsigned level,
                     unsigned usage,
                     const struct pipe_box *box,
                     const void *data,
                     unsigned stride,
                     uintptr_t layer_stride)
{
   if (r300_3d_subdata_needs_layered_upload(pipe, resource, level, box)) {
      struct pipe_box layer_box = *box;

      layer_box.depth = 1;

      for (layer_box.z = box->z; layer_box.z < box->z + box->depth;
           layer_box.z++) {
         u_default_texture_subdata(pipe, resource, level, usage, &layer_box,
                                   data, stride, layer_stride);
         data += layer_stride;
      }
      return;
   }

   u_default_texture_subdata(pipe, resource, level, usage, box, data, stride,
                             layer_stride);
}

static struct pipe_resource *
r300_resource_create(struct pipe_screen *screen,
                    const struct pipe_resource *templ)
{
   if (templ->target == PIPE_BUFFER)
      return r300_buffer_create(screen, templ);
   else
      return r300_texture_create(screen, templ);

}

void r300_init_resource_functions(struct r300_context *r300)
{
   r300->context.buffer_map = r300_buffer_transfer_map;
   r300->context.texture_map = r300_texture_transfer_map;
   r300->context.transfer_flush_region = u_default_transfer_flush_region;
   r300->context.buffer_unmap = r300_buffer_transfer_unmap;
   r300->context.texture_unmap = r300_texture_transfer_unmap;
   r300->context.buffer_subdata = u_default_buffer_subdata;
   r300->context.texture_subdata = r300_texture_subdata;
}

void r300_init_screen_resource_functions(struct r300_screen *r300screen)
{
   r300screen->screen.resource_create = r300_resource_create;
   r300screen->screen.resource_from_handle = r300_texture_from_handle;
   r300screen->screen.resource_get_handle = r300_resource_get_handle;
   r300screen->screen.resource_destroy = r300_resource_destroy;
}
