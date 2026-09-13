/*
 * Copyright 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef INTEL_VIRTIO_PRIV_H_
#define INTEL_VIRTIO_PRIV_H_

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "drm-uapi/drm.h"
#include "drm-uapi/i915_drm.h"
#include "drm-uapi/virtgpu_drm.h"
#include <xf86drm.h>

#include "util/libsync.h"
#include "util/list.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/os_file.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"
#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/perf/cpu_trace.h"
#include "util/u_sync_provider.h"

#include "common/intel_gem.h"

#include "virtio/virtio-gpu/drm_hw.h"

#include "intel_virtio.h"

#include "vdrm.h"

#define virtio_ioctl(fd, name, args) ({                           \
   MESA_TRACE_SCOPE(#name);                                       \
   int ret = drmIoctl((fd), DRM_IOCTL_ ## name, (args));          \
   ret;                                                           \
})

struct intel_virtio_device {
   struct list_head list_item;
   struct vdrm_device *vdrm;
   struct util_sync_provider *sync;
   int fd;

   uint32_t next_blob_id;
   uint32_t refcnt;

   bool vpipe;
};

struct intel_virtio_device *fd_to_intel_virtio_device(int fd);

int i915_virtio_gem_execbuffer2(struct intel_virtio_device *dev,
                                struct drm_i915_gem_execbuffer2 *exec);

#endif /* INTEL_VIRTIO_PRIV_H_ */
