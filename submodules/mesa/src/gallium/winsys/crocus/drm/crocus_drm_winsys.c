/*
 * Copyright © 2017 Intel Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#include "intel/dev/intel_device_info.h"

#include "util/os_file.h"

#include "virtio/virtio-gpu/drm_hw.h"

#include "crocus_drm_public.h"
#include "crocus/crocus_screen.h"

struct pipe_screen *
crocus_drm_screen_create(int fd, const struct pipe_screen_config *config)
{
   int newfd = os_dupfd_cloexec(fd);
   if (newfd < 0)
      return NULL;
   return crocus_screen_create(newfd, config);
}

/**
 * Check if the native-context type exposed by virtgpu is one we
 * support, and that we support the underlying device.
 */
bool
crocus_drm_probe_nctx(int fd, const struct virgl_renderer_capset_drm *caps)
{
#ifdef HAVE_INTEL_VIRTIO
   if (caps->context_type != VIRTGPU_DRM_CONTEXT_I915)
      return false;

   if (debug_get_bool_option("INTEL_VIRTIO_DISABLE", false))
      return false;

   struct intel_device_info devinfo;

   if (!intel_get_device_info_from_pci_id(caps->u.intel.pci_device_id,
                                          &devinfo))
      return false;

   if (devinfo.ver > 8)
      return false;

   if (devinfo.ver == 8) {
      if (devinfo.platform != INTEL_PLATFORM_CHV &&
          !getenv("CROCUS_GEN8"))
      return false;
   }

   return true;
#else
   return false;
#endif
}
