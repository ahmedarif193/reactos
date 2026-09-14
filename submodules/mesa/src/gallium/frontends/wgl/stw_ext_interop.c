/*
* Copyright © Microsoft Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice (including the next
* paragraph) shall be included in all copies or substantial portions of the
* Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*/

#include "state_tracker/st_interop.h"
#include "stw_ext_interop.h"

#include "stw_context.h"
#include "stw_device.h"
#ifdef HAVE_ROS_SHARED_TEXTURE
#include "dwmgpuinterop.h"
#include "dwmpresenttrace.h"
#include "stw_winsys.h"
#include "state_tracker/st_context.h"
#include "main/texobj.h"
#include "pipe/p_screen.h"
#include "util/u_inlines.h"
#include "frontend/winsys_handle.h"
#include "drm-uapi/drm_fourcc.h"

BOOL WINAPI
wglBindSharedTextureROS(UINT version, UINT share, UINT width, UINT height,
                       UINT pitch, UINT format)
{
   struct stw_context *ctx = stw_current_context();
   if (!ctx || version != DWM_WGL_SHARED_TEXTURE_VERSION || !share ||
       !width || !height || width > 4096 || height > 4096 ||
       format != DWM_WGL_SHARED_BGRA8 || pitch < width * 4 ||
       pitch % 4 != 0) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   struct pipe_screen *screen = ctx->st->pipe->screen;
   /* The selected driver must support shared images and explicit updates. */
   if (!stw_dev->stw_winsys->can_compose ||
       !stw_dev->stw_winsys->can_compose() ||
       !screen->resource_from_handle || !screen->resource_changed) {
      SetLastError(ERROR_NOT_SUPPORTED);
      return FALSE;
   }
   struct pipe_resource templ = {0};
   struct winsys_handle handle = {0};
   templ.target = PIPE_TEXTURE_2D;
   templ.format = PIPE_FORMAT_B8G8R8A8_UNORM;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = templ.array_size = 1;
   templ.bind = PIPE_BIND_SAMPLER_VIEW;
   handle.type = WINSYS_HANDLE_TYPE_SHARED;
   handle.handle = (HANDLE)(uintptr_t)share;
   handle.stride = pitch;
   handle.modifier = DRM_FORMAT_MOD_LINEAR;
   struct pipe_resource *resource = screen->resource_from_handle(
       screen, &templ, &handle, PIPE_HANDLE_USAGE_EXPLICIT_FLUSH);
   if (!resource) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
   }
   bool ok = st_context_teximage(ctx->st, GL_TEXTURE_2D, 0,
                                 templ.format, resource, false);
   pipe_resource_reference(&resource, NULL);
   return ok;
}

BOOL WINAPI
wglUpdateSharedTextureROS(UINT version)
{
   struct stw_context *ctx = stw_current_context();
   if (!ctx || version != DWM_WGL_SHARED_TEXTURE_VERSION) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   struct gl_texture_object *tex =
       _mesa_get_current_tex_object(ctx->st->ctx, GL_TEXTURE_2D);
   struct pipe_screen *screen = ctx->st->pipe->screen;
   if (!tex || !tex->pt || !screen->resource_changed) {
      SetLastError(ERROR_NOT_SUPPORTED);
      return FALSE;
   }
   screen->resource_changed(screen, tex->pt);
   return TRUE;
}
#endif

int
wglMesaGLInteropQueryDeviceInfo(HDC dpy, HGLRC context,
                                struct mesa_glinterop_device_info *out)
{
   DHGLRC dhglrc = 0;

   if (stw_dev && stw_dev->callbacks.pfnGetDhglrc) {
      /* Convert HGLRC to DHGLRC */
      dhglrc = stw_dev->callbacks.pfnGetDhglrc(context);
   } else {
      /* not using ICD */
      dhglrc = (DHGLRC)(INT_PTR)context;
   }

   struct stw_context *ctx = stw_lookup_context(dhglrc);
   if (!ctx)
      return MESA_GLINTEROP_INVALID_CONTEXT;

   return stw_interop_query_device_info(ctx, out);
}

int
stw_interop_query_device_info(struct stw_context *ctx,
                              struct mesa_glinterop_device_info *out)
{
   return st_interop_query_device_info(ctx->st, out);
}

int
wglMesaGLInteropExportObject(HDC dpy, HGLRC context,
                             struct mesa_glinterop_export_in *in,
                             struct mesa_glinterop_export_out *out)
{
   DHGLRC dhglrc = 0;

   if (stw_dev && stw_dev->callbacks.pfnGetDhglrc) {
      /* Convert HGLRC to DHGLRC */
      dhglrc = stw_dev->callbacks.pfnGetDhglrc(context);
   } else {
      /* not using ICD */
      dhglrc = (DHGLRC)(INT_PTR)context;
   }

   struct stw_context *ctx = stw_lookup_context(dhglrc);
   if (!ctx)
      return MESA_GLINTEROP_INVALID_CONTEXT;

   return stw_interop_export_object(ctx, in, out);
}

int
stw_interop_export_object(struct stw_context *ctx,
                          struct mesa_glinterop_export_in *in,
                          struct mesa_glinterop_export_out *out)
{
   return st_interop_export_object(ctx->st, in, out);
}

int
wglMesaGLInteropFlushObjects(HDC dpy, HGLRC context,
                             unsigned count, struct mesa_glinterop_export_in *resources,
                             struct mesa_glinterop_flush_out *out)
{
   DHGLRC dhglrc = 0;

   if (stw_dev && stw_dev->callbacks.pfnGetDhglrc) {
      /* Convert HGLRC to DHGLRC */
      dhglrc = stw_dev->callbacks.pfnGetDhglrc(context);
   } else {
      /* not using ICD */
      dhglrc = (DHGLRC)(INT_PTR)context;
   }

   struct stw_context *ctx = stw_lookup_context(dhglrc);
   if (!ctx)
      return MESA_GLINTEROP_INVALID_CONTEXT;

   return stw_interop_flush_objects(ctx, count, resources, out);
}

int
stw_interop_flush_objects(struct stw_context *ctx,
                          unsigned count, struct mesa_glinterop_export_in *objects,
                          struct mesa_glinterop_flush_out *out)
{
   return st_interop_flush_objects(ctx->st, count, objects, out);
}

#ifdef HAVE_ROS_SHARED_TEXTURE
BOOL WINAPI
wglControlPresentationTraceROS(const DPT_REQUEST *request, DPT_DOMAIN *output, ULONG bytes)
{
   if (!stw_dev || !stw_dev->stw_winsys->presentation_trace) {
      SetLastError(ERROR_NOT_SUPPORTED);
      return FALSE;
   }
   return stw_dev->stw_winsys->presentation_trace(request, output, bytes);
}
#endif
