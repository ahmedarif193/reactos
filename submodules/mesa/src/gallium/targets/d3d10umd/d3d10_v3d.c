/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 *
 * Direct3D Gallium target for the ReactOS Raspberry Pi 5 WDDM adapter.
 * There is intentionally no software fallback in this target.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <windows.h>
#include "winddk_compat.h"
#include <d3dkmthk.h>

#include "drm-uapi/drm_fourcc.h"
#include "frontend/winsys_handle.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "Resource.h"
#include "rpi5vc4_umd.h"
#include "broadcom/common/v3d_limits.h"
#include "v3d/v3d_resource.h"
#include "v3d_d3dkmt_public.h"

struct pipe_screen *d3d10_create_screen(void *adapter, void *device,
                                        const void *callbacks);
struct pipe_resource *d3d10_create_resource(
   struct pipe_screen *screen, const struct pipe_resource *templ,
   const D3D10GalliumResourceDesc *desc,
   void *runtime_resource, D3DKMT_HANDLE *allocation);
bool d3d10_get_open_resource_desc(
   const D3D10DDIARG_OPENRESOURCE *open_resource,
   D3D10GalliumResourceDesc *desc);
struct pipe_resource *d3d10_open_resource(
   struct pipe_screen *screen, const struct pipe_resource *templ,
   const D3D10DDIARG_OPENRESOURCE *open_resource,
   void *runtime_resource, D3DKMT_HANDLE *allocation);
void *d3d10_get_present_context(struct pipe_screen *screen);
bool d3d10_rotate_resource_identities(
   struct pipe_context *pipe, struct pipe_resource *const *resources,
   void *const *runtime_resources,
   unsigned count);
struct pipe_screen *
d3d10_create_screen(void *adapter, void *device, const void *callbacks)
{
   return v3d_d3dkmt_screen_create_umd(NULL, adapter, device, callbacks);
}

struct pipe_resource *
d3d10_create_resource(struct pipe_screen *screen,
                      const struct pipe_resource *templ,
                      const D3D10GalliumResourceDesc *desc,
                      void *runtime_resource, D3DKMT_HANDLE *allocation)
{
   struct pipe_resource *resource;
   RPI5VC4_RESOURCE_DATA private_data;

   if (allocation)
      *allocation = 0;
   if (!screen || !templ || !desc || !runtime_resource)
      return NULL;

   memset(&private_data, 0, sizeof(private_data));
   private_data.Magic = RPI5VC4_RESOURCE_DATA_MAGIC;
   private_data.Version = RPI5VC4_RESOURCE_DATA_VERSION;
   private_data.Dimension = desc->ResourceDimension;
   private_data.Format = desc->Format;
   private_data.Usage = desc->Usage;
   private_data.BindFlags = desc->BindFlags;
   private_data.MapFlags = desc->MapFlags;
   private_data.MiscFlags = desc->MiscFlags;
   private_data.Width = desc->Width;
   private_data.Height = desc->Height;
   private_data.Depth = desc->Depth;
   private_data.ArraySize = desc->ArraySize;
   private_data.MipLevels = desc->MipLevels;
   private_data.SampleCount = desc->SampleDesc.Count;
   private_data.SampleQuality = desc->SampleDesc.Quality;
   private_data.Flags = desc->Primary ? RPI5VC4_RESOURCE_FLAG_PRIMARY : 0;
   private_data.PrimaryVidPnSourceId = desc->Primary ?
      desc->PrimaryVidPnSourceId : RPI5VC4_RESOURCE_INVALID_VIDPN_SOURCE;
   private_data.Layout =
      templ->target == PIPE_BUFFER ||
      templ->target == PIPE_TEXTURE_1D ||
      templ->target == PIPE_TEXTURE_1D_ARRAY ||
      (templ->bind & (PIPE_BIND_LINEAR | PIPE_BIND_CURSOR |
                      PIPE_BIND_SCANOUT)) ?
         RPI5VC4_RESOURCE_LAYOUT_LINEAR :
         RPI5VC4_RESOURCE_LAYOUT_V3D_UIF;

   if (!v3d_d3dkmt_runtime_resource_begin(screen, runtime_resource,
                                           &private_data,
                                           sizeof(private_data)))
      return NULL;

   resource = screen->resource_create(screen, templ);
   v3d_d3dkmt_runtime_resource_end(screen);
   if (resource && allocation)
      *allocation =
         v3d_d3dkmt_resource_allocation(screen, resource);
   return resource;
}

static bool
d3d10_v3d_open_data(const D3D10DDIARG_OPENRESOURCE *open_resource,
                    const RPI5VC4_RESOURCE_DATA **private_data,
                    uint32_t *allocation_size)
{
   const RPI5VC4_RESOURCE_DATA *data;
   const D3DDDI_OPENALLOCATIONINFO *allocation;
   uint32_t size;

   if (!open_resource || !private_data || !allocation_size ||
       open_resource->NumAllocations != 1 ||
       !open_resource->pOpenAllocationInfo ||
       !open_resource->pOpenAllocationInfo[0].hAllocation ||
       open_resource->PrivateDriverDataSize != sizeof(*data) ||
       !open_resource->pPrivateDriverData)
      return false;

   data = (const RPI5VC4_RESOURCE_DATA *)
      open_resource->pPrivateDriverData;
   if (data->Magic != RPI5VC4_RESOURCE_DATA_MAGIC ||
       data->Version != RPI5VC4_RESOURCE_DATA_VERSION ||
       (data->Flags & ~RPI5VC4_RESOURCE_VALID_FLAGS) ||
       ((data->Flags & RPI5VC4_RESOURCE_FLAG_PRIMARY) == 0 &&
        data->PrimaryVidPnSourceId != RPI5VC4_RESOURCE_INVALID_VIDPN_SOURCE) ||
       (data->Layout != RPI5VC4_RESOURCE_LAYOUT_LINEAR &&
        data->Layout != RPI5VC4_RESOURCE_LAYOUT_V3D_UIF))
      return false;

   size = data->AllocationSize;
   allocation = &open_resource->pOpenAllocationInfo[0];
   if (!size &&
       allocation->PrivateDriverDataSize == sizeof(RPI5VC4_ALLOCATION_DATA) &&
       allocation->pPrivateDriverData) {
      const RPI5VC4_ALLOCATION_DATA *allocation_data =
         (const RPI5VC4_ALLOCATION_DATA *)allocation->pPrivateDriverData;

      if (allocation_data->Flags & ~RPI5VC4_ALLOCATION_VALID_FLAGS)
         return false;
      size = allocation_data->Size;
   }
   if (!size)
      return false;

   *private_data = data;
   *allocation_size = size;
   return true;
}

bool
d3d10_get_open_resource_desc(
   const D3D10DDIARG_OPENRESOURCE *open_resource,
   D3D10GalliumResourceDesc *desc)
{
   const RPI5VC4_RESOURCE_DATA *data;
   uint32_t allocation_size;

   if (!desc ||
       !d3d10_v3d_open_data(open_resource, &data, &allocation_size))
      return false;

   memset(desc, 0, sizeof(*desc));
   desc->ResourceDimension = (D3D10DDIRESOURCE_TYPE)data->Dimension;
   desc->Usage = data->Usage;
   desc->BindFlags = data->BindFlags;
   desc->MapFlags = data->MapFlags;
   desc->MiscFlags = data->MiscFlags;
   desc->Format = (DXGI_FORMAT)data->Format;
   desc->SampleDesc.Count = data->SampleCount;
   desc->SampleDesc.Quality = data->SampleQuality;
   desc->MipLevels = data->MipLevels;
   desc->ArraySize = data->ArraySize;
   desc->Width = data->Width;
   desc->Height = data->Height;
   desc->Depth = data->Depth;
   desc->Primary =
      (data->Flags & RPI5VC4_RESOURCE_FLAG_PRIMARY) != 0;
   desc->PrimaryVidPnSourceId = desc->Primary ?
      data->PrimaryVidPnSourceId : D3DDDI_ID_UNINITIALIZED;
   return true;
}

struct pipe_resource *
d3d10_open_resource(struct pipe_screen *screen,
                    const struct pipe_resource *templ,
                    const D3D10DDIARG_OPENRESOURCE *open_resource,
                    void *runtime_resource,
                    D3DKMT_HANDLE *allocation_out)
{
   const RPI5VC4_RESOURCE_DATA *data;
   struct winsys_handle winsys_handle;
   struct pipe_resource *resource;
   uint32_t allocation_size;
   uint32_t allocation;
   uint32_t handle;

   if (allocation_out)
      *allocation_out = 0;
   if (!screen || !templ || !runtime_resource ||
       !d3d10_v3d_open_data(open_resource, &data, &allocation_size))
      return NULL;

   allocation = open_resource->pOpenAllocationInfo[0].hAllocation;
   handle = v3d_d3dkmt_open_runtime_resource(
      screen, runtime_resource, allocation, allocation_size);
   if (!handle)
      return NULL;

   memset(&winsys_handle, 0, sizeof(winsys_handle));
   winsys_handle.type = WINSYS_HANDLE_TYPE_KMS;
   winsys_handle.handle = (HANDLE)(uintptr_t)handle;
   winsys_handle.stride = data->Stride;
   winsys_handle.size = allocation_size;
   winsys_handle.modifier =
      data->Layout == RPI5VC4_RESOURCE_LAYOUT_V3D_UIF ?
         DRM_FORMAT_MOD_BROADCOM_UIF : DRM_FORMAT_MOD_LINEAR;
   resource = screen->resource_from_handle(screen, templ, &winsys_handle, 0);
   if (!resource) {
      v3d_d3dkmt_discard_runtime_resource(screen, handle);
      return NULL;
   }

   if (allocation_out)
      *allocation_out = allocation;
   return resource;
}

void *
d3d10_get_present_context(struct pipe_screen *screen)
{
   return v3d_d3dkmt_present_context(screen);
}

bool
d3d10_rotate_resource_identities(struct pipe_context *pipe,
                                 struct pipe_resource *const *resources,
                                 void *const *runtime_resources,
                                 unsigned count)
{
   return v3d_resource_rotate_identities(
      pipe, resources, runtime_resources, count);
}
