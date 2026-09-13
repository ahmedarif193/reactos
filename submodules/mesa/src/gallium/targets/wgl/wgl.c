/**************************************************************************
 *
 * Copyright 2009-2010 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 *
 **************************************************************************/

/**
 * @file
 * Softpipe/LLVMpipe support.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */


#include <windows.h>

#include "util/u_debug.h"
#include "util/u_inlines.h"
#include "util/os_time.h"
#include "util/box.h"
#include "stw_winsys.h"
#include "stw_device.h"
#include "gdi/gdi_sw_winsys.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "frontend/winsys_handle.h"
#include "drm-uapi/drm_fourcc.h"

#ifdef GALLIUM_SOFTPIPE
#include "softpipe/sp_texture.h"
#include "softpipe/sp_screen.h"
#include "softpipe/sp_public.h"
#endif

#ifdef GALLIUM_LLVMPIPE
#include "llvmpipe/lp_texture.h"
#include "llvmpipe/lp_screen.h"
#include "llvmpipe/lp_public.h"
#endif

#ifdef GALLIUM_D3D12
#include "d3d12/wgl/d3d12_wgl_public.h"
#endif

#ifdef GALLIUM_ZINK
#include "zink/zink_public.h"
#endif


#ifdef GALLIUM_VC4
#include "vc4/d3dkmt/vc4_d3dkmt_public.h"
#endif

#ifdef GALLIUM_LLVMPIPE
static bool use_llvmpipe = false;
#endif
#ifdef GALLIUM_D3D12
static bool use_d3d12 = false;
#endif
#ifdef GALLIUM_ZINK
static bool use_zink = false;
#endif
#ifdef GALLIUM_VC4
static bool use_vc4 = false;
#endif

static const char *created_driver_name = NULL;

#ifdef GALLIUM_VC4
struct stw_shared_surface {
   struct pipe_resource *resource;
};
#endif

static struct pipe_screen *
wgl_screen_create_by_name(HDC hDC, const char* driver, struct sw_winsys *winsys)
{
   struct pipe_screen* screen = NULL;

#ifdef GALLIUM_LLVMPIPE
   if (strcmp(driver, "llvmpipe") == 0) {
      screen = llvmpipe_create_screen(winsys);
      if (screen)
         use_llvmpipe = true;
   }
#endif
#ifdef GALLIUM_D3D12
   if (strcmp(driver, "d3d12") == 0) {
      screen = d3d12_wgl_create_screen(winsys, hDC);
      if (screen)
         use_d3d12 = true;
   }
#endif
#ifdef GALLIUM_ZINK
   if (strcmp(driver, "zink") == 0) {
      screen = zink_create_screen(winsys, NULL);
      if (screen)
         use_zink = true;
   }
#endif
#ifdef GALLIUM_VC4
   if (strcmp(driver, "vc4") == 0) {
      screen = vc4_d3dkmt_screen_create(NULL);
      if (screen)
         use_vc4 = true;
   }
#endif
#ifdef GALLIUM_SOFTPIPE
   if (strcmp(driver, "softpipe") == 0) {
      screen = softpipe_create_screen(winsys);
   }
#endif

   return screen;
}

static struct pipe_screen *
wgl_screen_create(HDC hDC)
{
   struct sw_winsys *winsys;
   UNUSED bool sw_only = debug_get_bool_option("LIBGL_ALWAYS_SOFTWARE", false);

   winsys = gdi_create_sw_winsys(gdi_sw_acquire_hdc_by_value, gdi_sw_release_hdc_by_value);
   if (!winsys)
      return NULL;

   const char *const drivers[] = {
      debug_get_option("GALLIUM_DRIVER", ""),
#ifdef GALLIUM_VC4
      sw_only ? "" : "vc4",
#endif
#ifdef GALLIUM_D3D12
      sw_only ? "" : "d3d12",
#endif
#ifdef GALLIUM_ZINK
      sw_only ? "" : "zink",
#endif
#if defined(GALLIUM_LLVMPIPE)
      "llvmpipe",
#endif
#if defined(GALLIUM_SOFTPIPE)
      "softpipe",
#endif
   };

   /* If the default driver screen creation fails, fall back to the next option in the
    * sorted list. Don't do this if GALLIUM_DRIVER is specified.
    */
   for (unsigned i = 0; i < ARRAY_SIZE(drivers); ++i) {
      struct pipe_screen* screen = wgl_screen_create_by_name(hDC, drivers[i], winsys);
      if (screen) {
         created_driver_name = drivers[i];
#ifdef GALLIUM_VC4
         if (use_vc4)
            winsys->destroy(winsys);
#endif
         return screen;
      }
      if (i == 0 && drivers[i][0] != '\0')
         break;
   }

   winsys->destroy(winsys);
   return NULL;
}


static void
wgl_present(struct pipe_screen *screen,
            struct pipe_context *ctx,
            struct pipe_resource *res,
            HDC hDC)
{
   /* This will fail if any interposing layer (trace, debug, etc) has
    * been introduced between the gallium frontends and the pipe driver.
    *
    * Ideally this would get replaced with a call to
    * pipe_screen::flush_frontbuffer().
    *
    * Failing that, it may be necessary for intervening layers to wrap
    * other structs such as this stw_winsys as well...
    */

#if defined(HAVE_SWRAST)
   struct sw_winsys *winsys = NULL;
   struct sw_displaytarget *dt = NULL;
#endif

#ifdef GALLIUM_LLVMPIPE
   if (use_llvmpipe) {
      winsys = llvmpipe_screen(screen)->winsys;
      dt = llvmpipe_resource(res)->dt;
      gdi_sw_display(winsys, dt, hDC);
      return;
   }
#endif

#ifdef GALLIUM_D3D12
   if (use_d3d12) {
      d3d12_wgl_present(screen, ctx, res, hDC);
      return;
   }
#endif

#ifdef GALLIUM_ZINK
   if (use_zink) {
      screen->flush_frontbuffer(screen, ctx, res, 0, 0, hDC, 0, NULL);
      return;
   }
#endif


#ifdef GALLIUM_VC4
   if (use_vc4) {
      screen->flush_frontbuffer(screen, ctx, res, 0, 0, hDC, 0, NULL);
      return;
   }
#endif

#ifdef GALLIUM_SOFTPIPE
   winsys = softpipe_screen(screen)->winsys,
   dt = softpipe_resource(res)->dt,
   gdi_sw_display(winsys, dt, hDC);
#endif
}


#if WINVER >= 0xA00
static bool
wgl_get_adapter_luid(struct pipe_screen* screen,
   HDC hDC,
   LUID* adapter_luid)
{
   if (!stw_dev || !stw_dev->callbacks.pfnGetAdapterLuid)
      return false;

   stw_dev->callbacks.pfnGetAdapterLuid(hDC, adapter_luid);
   return true;
}
#endif


static struct stw_winsys_framebuffer *
wgl_create_framebuffer(struct pipe_screen *screen,
                       HWND hWnd,
                       int iPixelFormat)
{
#ifdef GALLIUM_D3D12
   if (use_d3d12)
      return d3d12_wgl_create_framebuffer(screen, hWnd, iPixelFormat);
#endif
   return NULL;
}

static const char *
wgl_get_name(void)
{
   return created_driver_name;
}

#ifdef GALLIUM_VC4
static struct stw_shared_surface *
wgl_shared_surface_open(struct pipe_screen *screen,
                        HANDLE shared_handle,
                        struct pipe_resource *source,
                        LPCRECT rect)
{
   struct stw_shared_surface *surface;
   struct pipe_resource templ = { 0 };
   struct winsys_handle whandle = { 0 };
   unsigned width, height;

   if (!screen || !screen->resource_from_handle || !shared_handle ||
       !source || !rect || rect->right <= rect->left ||
       rect->bottom <= rect->top)
      return NULL;

   width = rect->right - rect->left;
   height = rect->bottom - rect->top;
   surface = CALLOC_STRUCT(stw_shared_surface);
   if (!surface)
      return NULL;

   templ.target = PIPE_TEXTURE_2D;
   templ.format = source->format;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SHARED;

   whandle.type = WINSYS_HANDLE_TYPE_SHARED;
   whandle.handle = shared_handle;
   whandle.stride = width * 4;
   whandle.modifier = DRM_FORMAT_MOD_LINEAR;
   surface->resource = screen->resource_from_handle(
      screen, &templ, &whandle, PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
   if (!surface->resource) {
      FREE(surface);
      return NULL;
   }
   return surface;
}

static void
wgl_shared_surface_close(struct pipe_screen *screen,
                         struct stw_shared_surface *surface)
{
   (void)screen;
   if (!surface)
      return;
   pipe_resource_reference(&surface->resource, NULL);
   FREE(surface);
}

static bool
wgl_compose(struct pipe_screen *screen,
            struct pipe_context *context,
            struct pipe_resource *source,
            struct stw_shared_surface *dest,
            LPCRECT rect,
            ULONGLONG present_token,
            HANDLE completion_event)
{
   struct pipe_fence_handle *fence = NULL;
   struct pipe_box box;
   unsigned width, height;
   bool complete;

   (void)present_token;
   if (!screen || !context || !source || !dest || !dest->resource ||
       !rect || rect->right <= rect->left || rect->bottom <= rect->top)
      return false;

   width = MIN2((unsigned)(rect->right - rect->left), source->width0);
   width = MIN2(width, dest->resource->width0);
   height = MIN2((unsigned)(rect->bottom - rect->top), source->height0);
   height = MIN2(height, dest->resource->height0);
   u_box_2d(0, 0, width, height, &box);
#ifdef GALLIUM_VC4
   if (use_vc4) {
      struct pipe_blit_info blit = {0};

      /* Copy through the VC4 tile/render path. The completion fence below
       * still protects publication and reuse of the shared window surface. */
      blit.src.resource = source;
      blit.src.format = source->format;
      blit.src.box = box;
      blit.dst.resource = dest->resource;
      blit.dst.format = dest->resource->format;
      blit.dst.box = box;
      blit.mask = PIPE_MASK_RGBA;
      blit.filter = PIPE_TEX_FILTER_NEAREST;
      if (!vc4_render_blit_for_present(context, &blit))
         return false;
   } else
#endif
      context->resource_copy_region(context, dest->resource, 0, 0, 0, 0,
                                    source, 0, &box);
   context->flush(context, &fence, PIPE_FLUSH_END_OF_FRAME);
   if (!fence)
      return false;

   {
      complete = screen->fence_finish(screen, context, fence,
                                      OS_TIMEOUT_INFINITE);
      if (complete && completion_event)
         complete = SetEvent(completion_event);
   }
   screen->fence_reference(screen, &fence, NULL);
   return complete;
}
#endif

static void
wgl_present_region(struct pipe_screen *screen, struct pipe_context *ctx,
                   struct pipe_resource *res, HDC hdc, const RECT *damage)
{
#ifdef GALLIUM_VC4
   if (use_vc4 && damage) {
      struct pipe_box box;
      u_box_2d(damage->left, damage->top,
               damage->right - damage->left,
               damage->bottom - damage->top, &box);
      screen->flush_frontbuffer(screen, ctx, res, 0, 0, hdc, 1, &box);
      return;
   }
#endif
   wgl_present(screen, ctx, res, hdc);
}

static const struct stw_winsys stw_winsys = {
   &wgl_screen_create,
   &wgl_present,
#if WINVER >= 0xA00
   &wgl_get_adapter_luid,
#else
   NULL, /* get_adapter_luid */
#endif
#ifdef GALLIUM_VC4
   &wgl_shared_surface_open,
   &wgl_shared_surface_close,
   &wgl_compose,
#else
   NULL, /* shared_surface_open */
   NULL, /* shared_surface_close */
   NULL, /* compose */
#endif
   &wgl_create_framebuffer,
   &wgl_get_name,
#ifdef GALLIUM_VC4
   &vc4_d3dkmt_trace_control,
   &vc4_present_trace,
#else
   NULL,
   NULL,
#endif
   &wgl_present_region,
};


EXTERN_C BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);


BOOL WINAPI
DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
   switch (fdwReason) {
   case DLL_PROCESS_ATTACH:
      stw_init(&stw_winsys);
      stw_init_thread();
      break;

   case DLL_THREAD_ATTACH:
      stw_init_thread();
      break;

   case DLL_THREAD_DETACH:
      stw_cleanup_thread();
      break;

   case DLL_PROCESS_DETACH:
      if (lpvReserved == NULL) {
         // We're being unloaded from the process.
         stw_cleanup_thread();
         stw_cleanup();
      } else {
         // Process itself is terminating, and all threads and modules are
         // being detached.
         //
         // The order threads (including llvmpipe rasterizer threads) are
         // destroyed can not be relied up, so it's not safe to cleanup.
         //
         // However global destructors (e.g., LLVM's) will still be called, and
         // if Microsoft OPENGL32.DLL's DllMain is called after us, it will
         // still try to invoke DrvDeleteContext to destroys all outstanding,
         // so set stw_dev to NULL to return immediately if that happens.
         stw_dev = NULL;
      }
      break;
   }
   return true;
}
