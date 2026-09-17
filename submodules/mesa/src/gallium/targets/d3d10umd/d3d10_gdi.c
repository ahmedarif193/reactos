/**************************************************************************
 *
 * Copyright 2012-2021 VMware, Inc.
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
#include <stdint.h>


#include "util/u_debug.h"
#include "target-helpers/inline_debug_helper.h"
#include "llvmpipe/lp_public.h"
#include "softpipe/sp_public.h"
#include "sw/gdi/gdi_sw_winsys.h"

#include "winddk_compat.h"
#include <d3dkmthk.h>

#include "Resource.h"

extern struct pipe_screen *
d3d10_create_screen(void *adapter, void *device, const void *callbacks);

extern struct pipe_resource *
d3d10_create_resource(struct pipe_screen *screen,
                      const struct pipe_resource *templ,
                      const D3D10GalliumResourceDesc *desc,
                      void *runtime_resource, D3DKMT_HANDLE *allocation);

extern bool
d3d10_get_open_resource_desc(
   const D3D10DDIARG_OPENRESOURCE *open_resource,
   D3D10GalliumResourceDesc *desc);

extern struct pipe_resource *
d3d10_open_resource(struct pipe_screen *screen,
                    const struct pipe_resource *templ,
                    const D3D10DDIARG_OPENRESOURCE *open_resource,
                    void *runtime_resource,
                    D3DKMT_HANDLE *allocation);

extern void *
d3d10_get_present_context(struct pipe_screen *screen);

extern bool
d3d10_rotate_resource_identities(struct pipe_context *pipe,
                                 struct pipe_resource *const *resources,
                                 void *const *runtime_resources,
                                 unsigned count);

static HDC
d3d10_gdi_acquire_hdc(void *winsys_drawable_handle) {
   D3DKMT_PRESENT *pPresentInfo = (D3DKMT_PRESENT *)winsys_drawable_handle;

   HWND hWnd = pPresentInfo->hWindow;
   return GetDC(hWnd);
}

static void
d3d10_gdi_release_hdc(void *winsys_drawable_handle, HDC hDC) {
   D3DKMT_PRESENT *pPresentInfo = (D3DKMT_PRESENT *)winsys_drawable_handle;

   HWND hWnd = pPresentInfo->hWindow;
   ReleaseDC(hWnd, hDC);
}

struct pipe_screen *
d3d10_create_screen(void *adapter, void *device, const void *callbacks)
{
   const char *default_driver;
   const char *driver;
   struct pipe_screen *screen = NULL;
   struct sw_winsys *winsys;

   (void)adapter;
   (void)device;
   (void)callbacks;

   winsys = gdi_create_sw_winsys(d3d10_gdi_acquire_hdc, d3d10_gdi_release_hdc);
   if(!winsys)
      goto no_winsys;

#ifdef GALLIUM_LLVMPIPE
   default_driver = "llvmpipe";
#else
   default_driver = "softpipe";
#endif

   driver = debug_get_option("GALLIUM_DRIVER", default_driver);

#ifdef GALLIUM_LLVMPIPE
   if (strcmp(driver, "llvmpipe") == 0) {
      screen = llvmpipe_create_screen( winsys );
   }
#else
   (void)driver;
#endif

   if (screen == NULL) {
      screen = softpipe_create_screen( winsys );
   }

   if (screen == NULL)
      goto no_screen;

   return debug_screen_wrap( screen );

no_screen:
   winsys->destroy(winsys);
no_winsys:
   return NULL;
}

void *
d3d10_get_present_context(struct pipe_screen *screen)
{
   (void)screen;
   return NULL;
}

bool
d3d10_rotate_resource_identities(struct pipe_context *pipe,
                                 struct pipe_resource *const *resources,
                                 void *const *runtime_resources,
                                 unsigned count)
{
   (void)pipe;
   (void)resources;
   (void)runtime_resources;
   (void)count;
   return false;
}

struct pipe_resource *
d3d10_create_resource(struct pipe_screen *screen,
                      const struct pipe_resource *templ,
                      const D3D10GalliumResourceDesc *desc,
                      void *runtime_resource, D3DKMT_HANDLE *allocation)
{
   (void)desc;
   (void)runtime_resource;
   if (allocation)
      *allocation = 0;
   return screen->resource_create(screen, templ);
}

bool
d3d10_get_open_resource_desc(
   const D3D10DDIARG_OPENRESOURCE *open_resource,
   D3D10GalliumResourceDesc *desc)
{
   (void)open_resource;
   (void)desc;
   return false;
}

struct pipe_resource *
d3d10_open_resource(struct pipe_screen *screen,
                    const struct pipe_resource *templ,
                    const D3D10DDIARG_OPENRESOURCE *open_resource,
                    void *runtime_resource,
                    D3DKMT_HANDLE *allocation)
{
   (void)screen;
   (void)templ;
   (void)open_resource;
   (void)runtime_resource;
   if (allocation)
      *allocation = 0;
   return NULL;
}
